#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <inttypes.h>
#include <fcntl.h>
#include <string.h>
#include <strings.h>
#include <err.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <endian.h>
#include <pthread.h>
#include <signal.h>
#include "types.h"
#include "helpers.h"
#ifndef POSIX_MUTEX
 #include <sys/sem.h>
#endif

const char* CONFIG_PATH   = "serverconf";
const char* DATABASE_PATH = "msgdb";
const char* USERDB_PATH   = "users";
const int   LISTENQ       = 1024;

#define MAXCONNS 1024
#define MAXPOSTS 2048
#define MAXUSERS 256
#define BUF_SIZE 65536
#define BUF_NPOSTS 50

struct session_data {
	pthread_t       tid;
	struct in_addr  client_addr;
	int             sockfd;
	unsigned char  *buf;
} sessions[ MAXCONNS ] = { 0 };

char buffer[BUF_SIZE];
#ifdef POSIX_MUTEX
pthread_mutex_t mutexes[2];
#else
int semfd;
#endif

int  gAllowGuests = 0;
int  gPort = 3010;
char gBoardTitle[256+1] = { 0 };

Post *gPosts[ MAXPOSTS ];
int gPostCount = 0;
char *gUsers[ MAXUSERS ][2];
int gUserCount = 0;
int is_admin[ MAXUSERS ];

volatile sig_atomic_t gShutdown = 0;

static inline void _semaction( int, int );
#define sessions_lock() _semaction(1, -1)
#define sessions_unlock() _semaction(1, 1)
#define database_lock() _semaction(0, -1)
#define database_unlock() _semaction(0, 1)

int loadConfig( void )
{
	FILE* fp;
	char* endptr;
	char key_buf[256];
	char value_buf[256];
	int length;
        
    configfileopen:
	fp = fopen( CONFIG_PATH, "r" );
	if ( !fp )
	{
		if ( errno == EINTR )
			goto configfileopen;
		return -1;
	}

	while ( fgets( buffer, BUF_SIZE, fp ) )
	{
		length = strlen( buffer );
		if ( buffer[ length - 1 ] == '\n' )
			buffer[ --length ] = '\0';

		if ( length > 250 )
		{
			fprintf( stderr, "server: impossibile caricare il file di configurazione. Contiene righe troppo lunghe.\n" );
			exit( EXIT_FAILURE );
		}

		if ( sscanf( buffer, "%[^=]=%[^\n]", key_buf, value_buf ) < 2 )
		{
			fprintf( stderr, "loadConfig: impossibile interpretare la riga \"%s\".\n", buffer );
			continue;
		}

		/* actual parsing */
		if ( !strcmp( key_buf, "AllowGuests" ) )
		{
			if ( value_buf[1] || *value_buf < '0' || *value_buf > '1' )
			{
				fprintf( stderr, "loadConfig: valore errato per AllowGuests.\n" );
				exit( EXIT_FAILURE );
			}
			gAllowGuests = *value_buf == '1';
		}
		else if ( !strcmp( key_buf, "Port" ) )
		{
			gPort = strtol( value_buf, &endptr, 10 );
			if ( *endptr || gPort <= 0 || gPort > 65535 )
			{
				fprintf( stderr, "loadConfig: valore porta non valido.\n" );
				exit( EXIT_FAILURE );
			}
		}
		else if ( !strcmp( key_buf, "Title" ) )
			strncpy( gBoardTitle, value_buf, 256 + 1 );
	}

	while ( fclose( fp ) )
	{
		if ( errno != EINTR )
			err( EXIT_FAILURE, "loadConfig: errore nella close" );
	}

	return 0;
}

void unloadDatabase( void );
void unloadUsers( void );

int loadDatabase( void )
{
	FILE* fp;
	//char* endptr;
	//char id_buf[9];
	//char mittente[256];
	//char oggetto[256];
	//char testo[64001];
	char* id_buf;
	char* mittente;
	char* oggetto;
	char* testo;
	unsigned long len_mittente;
	unsigned long len_oggetto;
	unsigned long len_testo;
	signed long long timestamp;
        
    dbfileopen:
	fp = fopen( DATABASE_PATH, "r" );
	if ( !fp )
	{
		if ( errno == EINTR )
			goto dbfileopen;
		return -1;
	}

	while ( fgets( buffer, BUF_SIZE, fp ) )
	{
		//if ( sscanf( buffer, "%[^\x1f]\x1f%lld\x1f%[^\x1f]\x1f%[^\x1f]\x1f%[^\n]", id_buf, &timestamp, mittente, oggetto, testo ) < 5 )
		//	continue;
		const char* RS = "\n";
		const char* FS = "\x1f";
		char* curr_field;
		char* buffer2 = buffer;

		if ( gPostCount == MAXPOSTS )
		{
			fprintf( stderr, "server: Il database contiene troppe entries, ne ho caricate solo %d.\n", MAXPOSTS );
			break;
		}

		curr_field = strsep( &buffer2, FS );
		if ( !buffer2 ) continue;
		id_buf = curr_field;
		curr_field = strsep( &buffer2, FS );
		if ( !buffer2 ) continue;
		timestamp = atoll( curr_field );
		curr_field = strsep( &buffer2, FS );
		if ( !buffer2 ) continue;
		mittente = curr_field;
		curr_field = strsep( &buffer2, FS );
		if ( !buffer2 ) continue;
		oggetto = curr_field;
		curr_field = strsep( &buffer2, RS );
		if ( !buffer2 ) testo = "";
		testo = curr_field;

		len_mittente = strlen( mittente );
		len_oggetto = strlen( oggetto );
		len_testo = strlen( testo );

		Post *newpost = malloc( POST_HEADER_SIZE + len_mittente + len_oggetto + len_testo );
		if ( newpost == NULL )
		{
			fprintf( stderr, "loadDatabase: memoria insufficiente\n" );
			fclose( fp );
			unloadDatabase();
			exit( EXIT_FAILURE );
		}
		newpost->id = strtoul( id_buf, NULL, 16 );
		newpost->timestamp = timestamp;
		newpost->len_mittente = len_mittente;
		newpost->len_oggetto = len_oggetto;
		newpost->len_testo = len_testo;
		strncpy( newpost->data, mittente, len_mittente );
		strncpy( newpost->data + len_mittente, oggetto, len_oggetto );
		strncpy( newpost->data + len_mittente + len_oggetto, testo, len_testo );

		gPosts[ gPostCount++ ] = newpost;
	}

	while ( fclose( fp ) )
	{
		if ( errno != EINTR )
			err( EXIT_FAILURE, "loadDatabase: errore nella close" );
	}

	return gPostCount;
}

int storeDatabase( void )
{
	const char* temp_filepath = "msgdb.tmp";
      dbfileopen2:
	FILE *fp = fopen( temp_filepath, "w" );
	if ( !fp )
	{
		if ( errno == EINTR )
			goto dbfileopen2;
		warn( "server: storeDatabase: impossibile creare il file temporaneo" );
		return -1;
	}

	for ( int i = 0; i < gPostCount; i++ )
	{
		Post *curr_post = gPosts[ i ];
		if ( !curr_post->id )
			continue;
		uint16_t len_testo;
		int64_t timestamp;
		memcpy( &len_testo, &curr_post->len_testo, 2 );
		memcpy( &timestamp, &curr_post->timestamp, 8 );
		// for ( int j = 0; j < sizeof( curr_post->id ); j++ )
		// 	fprintf( fp, "%02x", ( ( unsigned char *)&curr_post->id )[ j ] );
		if ( fprintf( fp, "%08x\x1f%" PRId64 "\x1f", curr_post->id, timestamp ) < 11 )
		{
database_error:
			if ( fclose( fp ) == EOF )
				warn( "server: storeDatabase: errore nella close" );
			if ( unlink( temp_filepath ) == -1 )
				warn( "server: storeDatabase: impossibile eliminare il file temporaneo" );
			return -1;
		}
		if ( curr_post->len_mittente &&
		     fwrite( curr_post->data, curr_post->len_mittente, 1, fp ) < 1 ) goto database_error;
		if ( fputc( '\x1f', fp ) == EOF ) goto database_error;
		if ( curr_post->len_oggetto &&
		     fwrite( curr_post->data + curr_post->len_mittente, curr_post->len_oggetto, 1, fp ) < 1 ) goto database_error;
		if ( fputc( '\x1f', fp ) == EOF ) goto database_error;
		if ( len_testo &&
		     fwrite( curr_post->data + curr_post->len_mittente + curr_post->len_oggetto, len_testo, 1, fp ) < 1 ) goto database_error;
		if ( fputc( '\n', fp ) == EOF ) goto database_error;
	}

	// Salvataggio riuscito
	while ( fflush( fp ) == EOF )
	{
		if ( errno == EINTR )
			continue;
		goto database_error;
	}
	
	while ( fclose( fp ) )
	{
		if ( errno != EINTR )
			goto database_error;
	}

	// Scambio atomicamente i file
	if ( rename( temp_filepath, DATABASE_PATH ) == -1 )
	{
		warn( "server: storeDatabase: ERRORE: impossibile rinominare il file temporaneo!" );
		return -1;
	}

	return 0;
}

void unloadDatabase( void )
{
	for ( int i = 0; i < gPostCount; i++ )
	{
		free( gPosts[ i ] );
		gPosts[ i ] = NULL;
	}

	gPostCount = 0;
}

//int delDatabaseEntry( uint32_t entry_id )
//{
//	if ( !entry_id )
//		return -1;
//	for ( int i = 0; i < gPostCount; i++ )
//	{
//		if ( gPosts[ i ]->id == entry_id )
//		{
//			gPosts[ i ] = 0;
//			return 0;
//		}
//	}
//
//	return -1;
//}

int loadUsers( void )
{
	char user[256];
	char pass[256];

      userdbfileopen:
	FILE *fp = fopen( USERDB_PATH, "r" );
	if ( !fp )
	{
		if ( errno == EINTR )
			goto userdbfileopen;
		return -1;
	}

	while ( fgets( buffer, BUF_SIZE, fp ) )
	{
		if ( gUserCount == MAXUSERS )
		{
			fprintf( stderr, "loadUsers: Il server ha troppi utenti configurati! Ne puoi impostare massimo %d.\n", MAXUSERS );
			fclose( fp );
			unloadDatabase();
			unloadUsers();
			exit( EXIT_FAILURE );
		}

		if ( sscanf( buffer, "%[^\x1f]\x1f%[^\x1f]\x1f%d", user, pass, is_admin + gUserCount ) < 3 )
		{
			fprintf( stderr, "loadUsers: impossibile interpretare la riga \"%s\".\n", buffer );
			continue;
		}

		gUsers[ gUserCount ][ 0 ] = strdup( user );
		gUsers[ gUserCount ][ 1 ] = strdup( pass );
		if ( gUsers[ gUserCount ][ 0 ]  == NULL || gUsers[ gUserCount ][ 1 ] == NULL )
		{
			fprintf( stderr, "loadUsers: memoria insufficiente\n" );
			fclose( fp );
			unloadDatabase();
			unloadUsers();
			exit( EXIT_FAILURE );
		}

		gUserCount++;
	}

	while ( fclose( fp ) )
	{
		if ( errno != EINTR )
			err( EXIT_FAILURE, "loadUsers: errore nella close" );
	}

	return gUserCount;
}
	
void unloadUsers( void )
{
	for ( int i = 0; i < gUserCount; i++ )
	{
		free( gUsers[ i ][ 0 ] );
		free( gUsers[ i ][ 1 ] );
		is_admin[ i ] = 0;
	}

	gUserCount = 0;
}

/*  0: utente standard    *
 *  1: amministratore     *
 * -1: credenziali errate */

int tryLogin( const char* user, const char* pass )
{
	for ( int i = 0; i < gUserCount; i++ )
		if ( !strcmp( gUsers[ i ][ 0 ], user ) )
		{
			if ( !strcmp( gUsers[ i ][ 1 ], pass ) )
				return is_admin[ i ];
			else
				return -1;
		}

	return -1;
}

void deinitAndErr( int eval, const char* fmt )
{
	//database_lock();
	unloadDatabase();
	unloadUsers();
	//database_unlock();
	warn( fmt );
#ifdef POSIX_MUTEX
	while ( pthread_mutex_destroy( &mutexes[0] ) );	// non dovrebbero mai fallire perché
	while ( pthread_mutex_destroy( &mutexes[1] ) ); // i thread non dovrebbero ancora esistere
#else	
	if ( semctl( semfd, 0, IPC_RMID ) < 0 )
		err( eval, "server: Impossibile eliminare il semaforo" );
#endif
	exit( eval );
}

void deinitAndExit( void )
{
	database_lock();
	unloadDatabase();
	unloadUsers();
	database_unlock();
#ifdef POSIX_MUTEX
	int mutex1_destroyed = 0;
	if ( pthread_mutex_destroy( &mutexes[0] ) || !( mutex1_destroyed = 1 ) || pthread_mutex_destroy( &mutexes[1] ) )
	{
		fprintf( stderr, "server: ATTENZIONE: Non sono riuscito a eliminare il mutex perché un thread lo sta tenendo ancora bloccato. Non dovrebbero esserci altri thread attivi a questo punto!" );
		// Evita di perdere dati preferendo un deadlock con CPU al 100%. Questo è chiaramente uno scenario catastrofico
		if ( !mutex1_destroyed )
			while ( pthread_mutex_destroy( &mutexes[0] ) );
		while ( pthread_mutex_destroy( &mutexes[1] ) );
	}
#else
	if ( semctl( semfd, 0, IPC_RMID ) < 0 )
		warn( "server: Impossibile eliminare il semaforo" );
#endif
	printf( "server: Server terminato correttamente.\n" );
	exit( EXIT_SUCCESS );
}

static inline void _semaction( int which, int val )
{
#ifdef POSIX_MUTEX
	if ( val < 0 )
		pthread_mutex_lock( &mutexes[ which ] );
	else
		pthread_mutex_unlock( &mutexes[ which ] );
#else
	struct sembuf sem_action;

	sem_action.sem_num = which;
	sem_action.sem_op  = val;
	sem_action.sem_flg = 0;

retry_semaction:
	if ( semop( semfd, &sem_action, 1 ) < 0 )
	{
		if ( errno == EINTR )
			goto retry_semaction;
		//deinitAndErr( EXIT_FAILURE, "server: Impossibile bloccare il semaforo, arresto forzato" );

		// C'è un problema grave (magari semaforo rimosso), il sistema è in stato inconsistente. Termino tutto il processo
		warn( "server: Impossibile bloccare il semaforo, arresto forzato" );
		exit( EXIT_FAILURE );
	}
#endif
}

void closeSocket( int sockfd )
{
	while ( close( sockfd ) < 0 )
	{
		if ( errno != EINTR )
			warn( "server: Errore durante la close del socket" );
	}
}

/*	scambia (SOCK, BUF, LEN, RESP):
	 - codifica BUF -> network order
	 - invia LEN bytes
	 - ricevi in timeout di n minuti
	 - se ricevuto -> decodifica BUF -> host order
	 - se ricevuto RESP -> 1
	 - se ricevuto non RESP -> 0

	 - Broken pipe, timeout, conn. reset -> -1			*/

int SendAndGetResponse( int sockfd, unsigned char* msg_buf, size_t *len, Client_Frametype resp )
{
	//int ret;
	//uint16_t tmp_u16;
	//uint32_t tmp_u32;
	//uint64_t tmp_u64;
	//char encode_buf[8];
	
	//uint8_t value_u8;

	switch ( *msg_buf )
	{
		case SERV_WELCOME:
			conv_u16( msg_buf + 2, TO_NETWORK );	// n_posts
			conv_u64( msg_buf + 4, TO_NETWORK );	// local_time
			break;

		case SERV_ENTRIES:
			unsigned char* scanptr = msg_buf;
			unsigned int   data_length;
			uint8_t        n       = scanptr[3];
			conv_u16( msg_buf + 1, TO_NETWORK );	// n_posts
			scanptr += 4;

			for ( unsigned int i = 0; i < n; i++ )
			{
				conv_u32( scanptr    , TO_NETWORK );	// id
		  data_length = conv_u16( scanptr + 6, TO_NETWORK );	// len_testo
				conv_u64( scanptr + 8, TO_NETWORK );	// timestamp

				data_length += scanptr[4];		// len_mittente
				data_length += scanptr[5];		// len_oggetto
				
				scanptr += POST_HEADER_SIZE + data_length;
			}
			break;

		default:
			break;
	}

	ssize_t sc;
	ssize_t rc;
	size_t  n_to_send = *len;
	size_t 	n_to_receive;

	while ( n_to_send &&
	        ( sc = send( sockfd, msg_buf, n_to_send, 0 ) ) < ( ssize_t )n_to_send )
	{
		if ( sc == -1 )
		{
			if ( errno == EINTR )
				continue;

			return -1;
		}

		n_to_send -= sc;
	}

	while ( ( rc = recv( sockfd, msg_buf, 1, 0 ) ) < 1 )
	{
		if ( rc == -1 && errno == EINTR )
			continue;

		return -1;
	}
	*len += rc;

	/* Receive and decode */
	switch ( *msg_buf )
	{
		case CLI_LOGIN:
		case CLI_POST:
		case CLI_DELPOST:
			if ( ( rc = sockReceiveAll( sockfd, msg_buf + 1, 2 ) ) < 0 )
				return -1;
			*len += rc;
			n_to_receive = msg_buf[1] + msg_buf[2];
			if ( ( rc = sockReceiveAll( sockfd, msg_buf + 3, n_to_receive ) ) < 0 )
				return -1;
			*len += rc;
			
			if ( *msg_buf == CLI_POST )
			{
				unsigned char* msg_buf_2;
				msg_buf_2 = msg_buf + 3 + n_to_receive;
				if ( ( rc = sockReceiveAll( sockfd, msg_buf_2, POST_HEADER_SIZE ) ) < 0 )
					return -1;
				*len += rc;
				n_to_receive  = conv_u16( msg_buf_2 + 6, TO_HOST );	// len_testo
				n_to_receive += msg_buf_2[4];				// len_mittente
				n_to_receive += msg_buf_2[5];				// len_oggetto
				if ( ( rc = sockReceiveAll( sockfd, msg_buf_2 + POST_HEADER_SIZE, n_to_receive ) ) < 0 )
					return -1;
				*len += rc;
			}
			else if ( *msg_buf == CLI_DELPOST )
			{
				if ( ( rc = sockReceiveAll( sockfd, msg_buf + 3 + n_to_receive, 4 ) ) < 0 )
					return -1;
				*len += rc;
				conv_u32( msg_buf + 3 + n_to_receive, TO_HOST );	// id
			}
			break;

		case CLI_GETPOSTS:
			if ( ( rc = sockReceiveAll( sockfd, msg_buf + 1, 2 ) ) < 0 )
				return -1;
			*len += rc;
			break;

		default:
			break;
	}

	return *msg_buf != resp;
}

inline static void notifyAllClientsExcept( struct session_data *sender )
{
	int targets[MAXCONNS];
	int count = 0;

	sessions_lock();
	for ( int i = 0; i < MAXCONNS; i++ )
	{
		if ( &sessions[ i ] == sender )
			continue;
		if ( sessions[ i ].tid )
		{
			targets[ count++ ] = sessions[ i ].sockfd;
#ifdef DEBUG
			printf( "Inviando segnale OOB a %s...\n", inet_ntoa( sessions[ i ].client_addr ) );
#endif
		}
	}
	sessions_unlock();

	for ( int i = 0; i < count; i++ )
		send( targets[ i ], "!", 1, MSG_OOB | MSG_NOSIGNAL );
}		

void closeSession( struct session_data *session )
{
	closeSocket( session->sockfd );
	free( session->buf );
	sessions_lock();
	session->tid = 0;
	sessions_unlock();
	printf( "server: Sessione di %s terminata\n", inet_ntoa( session->client_addr ) );
}


void* clientSession( void* arg )
{
	struct session_data *session = ( struct session_data *)arg;
	sigset_t            sigset;
	char		    client_user[256];
	char		    client_pass[256];
	int 		    user_auth_level = -1;
	unsigned char* 	    msg_buf = session->buf;
	size_t 		    msg_size;

	sigfillset( &sigset );
	pthread_sigmask( SIG_BLOCK, &sigset, NULL );
	printf( "server: Spawnato un nuovo thread per gestire la connessione in entrata di %s\n", inet_ntoa( session->client_addr ) );

	pthread_detach( pthread_self() );

	if ( !gAllowGuests )
	{
		/* Login required */
		msg_buf[0] = SERV_AUTHENTICATE;
		msg_size = 1;
			
		while (1)
		{
			int ret = SendAndGetResponse( session->sockfd, msg_buf, &msg_size, CLI_LOGIN );
			
			if ( ret < 0 )
			{
				closeSession( session );
				return NULL;
			}
			else if ( ret )
			{
				fprintf( stderr, "server (#%lu): Ricevuto campo inaspettato (%#08b diverso da LOGIN). Chiudo la connessione.\n",
						(unsigned long)session->tid, *msg_buf );
				closeSession( session );
				return NULL;
			}
			
			// Rimane il caso CLI_LOGIN
			memcpy( client_user, msg_buf + 3             , msg_buf[1] );
			memcpy( client_pass, msg_buf + 3 + msg_buf[1], msg_buf[2] );
			client_user[ msg_buf[1] ] = '\0';
			client_pass[ msg_buf[2] ] = '\0';
			
			if ( ( user_auth_level = tryLogin( client_user, client_pass ) ) >= 0 )
				break;
			
			/* Autenticazione fallita, rispondi con SERV_NOT_OK */
			msg_buf[0] = SERV_NOT_OK;
			msg_size = 2;
		}
	}

	database_lock();
	uint16_t post_count = gPostCount;
	database_unlock();
	msg_buf[0] = SERV_WELCOME;
	msg_buf[1] = ( unsigned char )user_auth_level;
	memcpy( msg_buf + 2, &post_count, 2 );
	/* inserisci orario locale... */
	int64_t local_time = ( int64_t )time( NULL );
	memcpy( msg_buf + 4, &local_time, 8 );
	msg_buf[12] = ( unsigned char )strlen( gBoardTitle );
	strcpy( ( char* )msg_buf + 13, gBoardTitle );
	msg_size = 13 + msg_buf[12];

	/* Main loop */
	while (1)
	{
		int ret = SendAndGetResponse( session->sockfd, msg_buf, &msg_size, 0 );
		if ( ret < 0 )
		{
			closeSession( session );
			return NULL;
		}

		switch ( *msg_buf )
		{
			case CLI_GETPOSTS:
				uint8_t        page    = msg_buf[1];
				uint8_t        limit   = msg_buf[2];
				uint8_t	       count   = 0;
				unsigned char* scanptr = msg_buf + 4;

				if ( limit > BUF_NPOSTS ) limit = BUF_NPOSTS;

				database_lock();
				//for ( unsigned int i = (page - 1) * limit; i < page * limit; i++ )
				if ( page > 0 )
					for ( int i = gPostCount - 1 - limit * (page - 1); i >= gPostCount - limit * page; i-- )
					{
						//if ( i >= gPostCount )
						if ( i < 0 )
						{
							//semunlock();
							break;
						}

						Post *curr_post = gPosts[i];

						uint16_t len_testo;
						memcpy( &len_testo, &curr_post->len_testo, 2 );
						size_t post_size = curr_post->len_mittente + curr_post->len_oggetto + len_testo;

						memcpy( scanptr, curr_post, POST_HEADER_SIZE + post_size );
						scanptr += POST_HEADER_SIZE + post_size;
						count++;
					}

				database_unlock();
				msg_buf[0] = SERV_ENTRIES;
				memcpy( msg_buf + 1, &gPostCount, 2 );
				msg_buf[3] = count;
				msg_size = scanptr - msg_buf;
				break;

			case CLI_POST:
				msg_size = 2;

				database_lock();
				if ( gPostCount == MAXPOSTS )
				{
					msg_buf[0] = SERV_NOT_OK;
					msg_buf[1] = 0xFF;
					database_unlock();
					break;
				}

				memcpy( client_user, msg_buf + 3, msg_buf[1] );
				memcpy( client_pass, msg_buf + 3 + msg_buf[1], msg_buf[2] );
				client_user[ msg_buf[1] ] = '\0';
				client_pass[ msg_buf[2] ] = '\0';
				if ( tryLogin( client_user, client_pass ) < 0 )
				{
					msg_buf[0] = SERV_NOT_OK;
					msg_buf[1] = 0;
					database_unlock();
					break;
				}

				unsigned char *sent_post_addr = msg_buf + 3 + msg_buf[1] + msg_buf[2];
				unsigned char len_mittente = sent_post_addr[4];
				unsigned char len_oggetto  = sent_post_addr[5];
				uint16_t len_testo;
				memcpy( &len_testo, sent_post_addr + 6, 2 );	// sent_post->len_testo
				
				gPosts[ gPostCount ] = malloc( POST_HEADER_SIZE + strlen( client_user ) + len_oggetto + len_testo );
				if ( !gPosts[ gPostCount ] )
				{
					/* Out of memory! */
					msg_buf[0] = SERV_NOT_OK;
					msg_buf[1] = 0x01;
					database_unlock();
					break;
				}
				Post *new_post = gPosts[ gPostCount++ ];

				memcpy( new_post, sent_post_addr, POST_HEADER_SIZE );

				// ignora l'ID inviato insieme al post e creane uno ex novo
				arc4random_buf( &new_post->id, 4 );
				// ignora anche il timestamp inviato dal client, utilizza al suo posto l'orario attuale del server
				int64_t curr_time = time( NULL );
				memcpy( &new_post->timestamp, &curr_time, 8 );
				// ignora il mittente (potrebbe essere spoofato, utilizza al suo posto l'username del client)
				size_t userlen = strlen( client_user );
				new_post->len_mittente = userlen;

				memcpy( (char*)new_post  + POST_HEADER_SIZE, client_user, userlen );
				memcpy( (char*)new_post  + POST_HEADER_SIZE + userlen,
					sent_post_addr   + POST_HEADER_SIZE + len_mittente,  len_oggetto );
				memcpy( (char*)new_post  + POST_HEADER_SIZE + userlen      + len_oggetto,
					sent_post_addr   + POST_HEADER_SIZE + len_mittente + len_oggetto, len_testo );

				// Sanitizziamo per neutralizzare silenziosamente eventuali attacchi (injection)
				for ( size_t i = 0; i < len_oggetto + len_testo; i++ )
				{
					if ( new_post->data[ userlen + i ] == '\x1f' ||
					     new_post->data[ userlen + i ] == '\n'   )
						new_post->data[ userlen + i ] = ' ';
				}

				int store_res = storeDatabase();
				database_unlock();
				printf( "server: Nuovo messaggio pubblicato da %s\n", inet_ntoa( session->client_addr ) );
				if ( store_res < 0 )
				{
					printf( "server: Impossibile aggiornare il database dei messaggi\n" );
					msg_buf[0] = SERV_NOT_OK;
					msg_buf[1] = 0x02;
					msg_size = 2;
					break;
				}

				msg_buf[0] = SERV_OK;
				msg_size = 1;
				notifyAllClientsExcept( session );
				break;

			case CLI_DELPOST:
				msg_size = 2;

				memcpy( client_user, msg_buf + 3, msg_buf[1] );
				memcpy( client_pass, msg_buf + 3 + msg_buf[1], msg_buf[2] );
				client_user[ msg_buf[1] ] = '\0';
				client_pass[ msg_buf[2] ] = '\0';
				if ( tryLogin( client_user, client_pass ) < 0 )
				{
					msg_buf[0] = SERV_NOT_OK;
					msg_buf[1] = 0;
					break;
				}

				uint32_t post_id;
				memcpy( &post_id, msg_buf + 3 + msg_buf[1] + msg_buf[2], 4 );
				if ( post_id == 0x0 )
				{
					msg_buf[0] = SERV_NOT_OK;
					msg_buf[1] = 0xFF;
					break;
				}

				database_lock();
				int post_index = -1;
				for ( int i = 0; i < gPostCount; i++ )
				{
					if ( !memcmp( &gPosts[i]->id, &post_id, 4 ) )
						post_index = i;
				}

				if ( post_index == -1 )
				{
					msg_buf[0] = SERV_NOT_OK;
					msg_buf[1] = 0xFF;
					database_unlock();
					break;
				}

				if ( user_auth_level < 1 && strncmp( gPosts[ post_index ]->data, client_user, gPosts[ post_index ]->len_mittente ) )
				{
					msg_buf[0] = SERV_NOT_OK;
					msg_buf[1] = 0;
					database_unlock();
					break;
				}

				bzero( &gPosts[ post_index ]->id, 4 );
				if ( storeDatabase() >= 0 )
				{
					unloadDatabase();
					loadDatabase();
					database_unlock();
				}
				else
				{
					database_unlock();
					fprintf( stderr, "server: Impossibile aggiornare il database dei messaggi" );
					closeSession( session );
					return NULL;
				}

				notifyAllClientsExcept( session );
				msg_buf[0] = SERV_OK;
				msg_size = 1;
				break;

			case CLI_LOGIN:
				memcpy( client_user, msg_buf + 3             , msg_buf[1] );
				memcpy( client_pass, msg_buf + 3 + msg_buf[1], msg_buf[2] );
				client_user[ msg_buf[1] ] = '\0';
				client_pass[ msg_buf[2] ] = '\0';
				
				if ( ( user_auth_level = tryLogin( client_user, client_pass ) ) < 0 )
				{
					/* Autenticazione fallita, rispondi con SERV_NOT_OK */
					msg_buf[0] = SERV_NOT_OK;
					msg_size = 2;
					break;
				}

				msg_buf[0] = SERV_WELCOME;
				msg_buf[1] = ( unsigned char )user_auth_level;
				// Possiamo lasciare anche garbage, tanto il client non li leggerà
				//memcpy( msg_buf + 2, &gPostCount, 2 );
				//int64_t local_time = ( int64_t )time( NULL );
				//memcpy( msg_buf + 4, &local_time, 8 );
				msg_buf[12] = 0;
				msg_size = 13;
				break;

			default:
				break;

		}
	}

}

void term_handler( int sig )
{
	gShutdown = 1;
}

int main( int argc, char *argv[] )
{
	int s_list;
	int s_client;
	struct sockaddr_in serv_addr;
	struct sockaddr_in client_addr;
	struct sigaction   sa  = { 0 };
	struct sigaction   sa2 = { 0 };
	unsigned int sin_size;

	sa.sa_handler  = term_handler;
	sa2.sa_handler = SIG_IGN;
	sigaction( SIGPIPE, &sa2, NULL );
	sigaction( SIGINT,  &sa,  NULL );
	sigaction( SIGTERM, &sa2, NULL );

#ifdef POSIX_MUTEX
	pthread_mutex_init( &mutexes[0], NULL );
	pthread_mutex_init( &mutexes[1], NULL );
#else
	if ( ( semfd = semget( IPC_PRIVATE, 2, 0666 ) ) < 0 )
		err( EXIT_FAILURE, "Impossibile ottenere i semafori" );

	unsigned short semvals[2] = { 1, 1 };
	if ( semctl( semfd, 0, SETALL, semvals ) < 0 )
		err( EXIT_FAILURE, "Impossibile inizializzare i semafori" );
#endif

	if ( loadConfig() < 0 )
	{
		fprintf( stderr, "server: File di configurazione non trovato.\n" );
		exit( EXIT_FAILURE );
	}
	if ( loadDatabase() < 0 )
	{
		int db_fp;
	        while ( ( db_fp = creat( DATABASE_PATH, 0666 ) ) < 0 )
			if ( errno != EINTR )
				err( EXIT_FAILURE, "Impossibile creare il file database" );
		while ( close( db_fp ) < 0 )
			if ( errno != EINTR )
				err( EXIT_FAILURE, "Impossibile chiudere il nuovo database" );
	}
	if ( loadUsers() < 0 )
	{
		int userdb_fp;
	        while ( ( userdb_fp = creat( USERDB_PATH, 0666 ) ) < 0 )
			if ( errno != EINTR )
				err( EXIT_FAILURE, "Impossibile creare il file utenti" );
		while ( close( userdb_fp ) < 0 )
			if ( errno != EINTR )
				err( EXIT_FAILURE, "Impossibile chiudere il nuovo database" );
	}

	if ( gUserCount == 0 && !gAllowGuests )
	{
		fprintf( stderr, "server: Nessun utente trovato, ma il server è configurato per non permettere connessioni anonime.\n" );
		exit( EXIT_SUCCESS );
	}

#ifdef DEBUG
	printf( "Caricate %d entry nel database.\n", gPostCount );
	printf( "Caricate %d credenziali utente.\n", gUserCount );
#endif

	if ( ( s_list = socket( AF_INET, SOCK_STREAM, 0 ) ) < 0 )
		deinitAndErr( EXIT_FAILURE, "server: Errore nella creazione della socket" );

	int reuse = 1;
	if ( setsockopt( s_list, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof( reuse ) ) == -1 )
		warn( "server: Impossibile impostare il riutilizzo della socket di ascolto" );

	bzero( &serv_addr, sizeof( serv_addr ) );
	serv_addr.sin_family      = AF_INET;
	serv_addr.sin_port        = htons( ( short int )gPort );
	serv_addr.sin_addr.s_addr = INADDR_ANY;

	if ( bind( s_list, ( struct sockaddr *)&serv_addr, sizeof( serv_addr ) ) < 0 )
		deinitAndErr( EXIT_FAILURE, "server: Errore nel binding della socket" );

	if ( listen( s_list, LISTENQ ) < 0 )
		deinitAndErr( EXIT_FAILURE, "server: Impossibile ascoltare sulla porta" );
	printf( "server: In ascolto sulla porta %d\n", gPort );

	sin_size = sizeof( client_addr );
	while (1)
	{
		if ( ( s_client = accept( s_list, ( struct sockaddr *)&client_addr, &sin_size ) ) < 0 )
		{
			if ( errno != EINTR )
				warn( "server: Impossibile accettare una connessione in entrata" );

			// Interrotto da SIGINT/SIGTERM per arrestare il server? Controlliamo
			if ( gShutdown )
				break;

			continue;
		}

		struct timeval timeout;
		timeout.tv_sec  = 60 * 5;
		timeout.tv_usec = 0;
		if ( setsockopt( s_client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof( struct timeval ) ) == -1 )
			warn( "server: Impossibile impostare il timeout per la sessione di %s, rischio deadlock! Motivo", inet_ntoa( client_addr.sin_addr ) );

		sessions_lock();
		int slot = -1;
		for ( int i = 0; i < MAXCONNS; i++ )
			if ( !sessions[ i ].tid )
			{
				slot = i;
				break;
			}
		sessions_unlock();

		if ( slot == -1 )
		{
			printf( "server: Il server è pieno, respingo la connessione in entrata.\n" );
			closeSocket( s_client );
			continue;
		}

		sessions[ slot ].sockfd  = s_client;
		sessions[ slot ].client_addr = client_addr.sin_addr;
		sessions[ slot ].buf = malloc( BUF_SIZE * BUF_NPOSTS );
		if ( !sessions[ slot ].buf )
		{
			fprintf( stderr, "server: Impossibile allocare %d byte per lo scambio di messaggi, respingo la connessione in entrata.\n", BUF_SIZE * BUF_NPOSTS );
			closeSocket( s_client );
			continue;
		}
#ifdef DEBUG
		printf( "server: Allocati %d byte per lo scambio di messaggi della nuova sessione\n", BUF_SIZE * BUF_NPOSTS );
#endif

		if ( pthread_create( &sessions[ slot ].tid, NULL, clientSession, &sessions[ slot ] ) )
		{
			warn( "server: Impossibile spawnare un nuovo thread per processare la sessione di %s. Chiudo la connessione.\nMotivo",
					inet_ntoa( client_addr.sin_addr ) );
			closeSocket( s_client );
			sessions_lock();
			sessions[ slot ].tid = 0;
			sessions_unlock();
		}

	}

	// qui non serve sincronizzare (non sto più scrivendo su sessions perché non sto più accettando connessioni)
	for ( int i = 0; i < MAXCONNS; i++ )
		if ( sessions[ i ].tid )
		{
#ifdef DEBUG
			printf( "server: Terminando la sessione #%d (%s)...\n", i, inet_ntoa( sessions[ i ].client_addr ) );
#endif
			if ( shutdown( sessions[ i ].sockfd, SHUT_RD ) == -1 )
				warn( "server: Impossibile arrestare la socket della sessione #%d (%s) durante lo spegnimento", i, inet_ntoa( sessions[ i ].client_addr ) );
		}
	
	//for ( int i = 0; i < MAXCONNS; i++ )
	//	if ( sessions[ i ].tid )
	//		pthread_join( sessions[ i ].tid, NULL );
	
	// Ormai abbiamo detachato i thread delle sessioni subito per evitare l'accumulo di zombie,
	// non possiamo più fare pthread_join. Facciamo semplicemente così, che è anche meglio
	while (1)
	{
		int active_sessions = 0;

		sessions_lock();
		for ( int i = 0; i < MAXCONNS; i++ )
			if ( sessions[ i ].tid )
			{
				active_sessions = 1;
				break;
			}
		sessions_unlock();

		if ( !active_sessions )
			break;

		usleep( 10000 );
	}

	closeSocket( s_list );
	deinitAndExit();
}

void test( void )
{
	if ( loadConfig() < 0 )
	{
		fprintf( stderr, "server: File di configurazione non trovato.\n" );
		exit( EXIT_FAILURE );
	}
	printf( "allow = %d\nport = %d\n", gAllowGuests, gPort );
	loadDatabase();
	unloadDatabase();
	loadDatabase();
	storeDatabase();
	unloadDatabase();
}
