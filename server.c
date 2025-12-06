#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
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
#include "types.h"
#include "helpers.h"

const char* CONFIG_PATH   = "serverconf";
const char* DATABASE_PATH = "msgdb";
const char* USERDB_PATH   = "users";
const int   LISTENQ       = 1024;

#define MAXCONNS 1024
#define BUF_SIZE 65536

struct session_data {
	pthread_t       tid;
	struct in_addr  client_addr;
	int             sockfd;
} sessions[ MAXCONNS ] = { 0 };

char buffer[BUF_SIZE];

int gAllowGuests = 0;
int gPort = 3010;

Post *gPosts[2048];
int gPostCount = 0;
char *gUsers[256][2];
int gUserCount = 0;
int is_admin[256];

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

		if ( sscanf( buffer, "%[^=]=%s", key_buf, value_buf ) < 2 )
		{
			printf( "loadConfig: impossibile interpretare la riga \"%s\".\n", buffer );
			continue;
		}

		/* actual parsing */
		if ( !strcmp( key_buf, "AllowGuests" ) )
		{
			if ( *value_buf == '1' )
				gAllowGuests = 1;
			else if ( *value_buf == '0' )
				gAllowGuests = 0;
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
	}

	while ( fclose( fp ) )
	{
		if ( errno != EINTR )
			err( EXIT_FAILURE, "loadConfig: errore nella close" );
	}

	return 0;
}

int loadDatabase( void )
{
	FILE* fp;
	char* endptr;
	char id_buf[9];
	char mittente[256];
	char oggetto[256];
	char testo[64001];
	unsigned long len_mittente;
	unsigned long len_oggetto;
	unsigned long len_testo;
	signed long long timestamp;
	int count = 0;
        
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
		if ( sscanf( buffer, "%[^\x1f]\x1f%lld\x1f%[^\x1f]\x1f%[^\x1f]\x1f%[^\n]", id_buf, &timestamp, mittente, oggetto, testo ) < 5 )
			continue;

		len_mittente = strlen( mittente );
		len_oggetto = strlen( oggetto );
		len_testo = strlen( testo );

		Post *newpost = malloc( POST_HEADER_SIZE + len_mittente + len_oggetto + len_testo );
		if ( newpost == NULL )
		{
			fprintf( stderr, "loadDatabase: memoria insufficiente\n" );
			fclose( fp );
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
      dbfileopen2:
	FILE *fp = fopen( DATABASE_PATH, "w" );
	if ( !fp )
	{
		if ( errno == EINTR )
			goto dbfileopen2;
		return -1;
	}

	for ( int i = 0; i < gPostCount; i++ )
	{
		Post *curr_post = gPosts[ i ];
		if ( !curr_post->id )
			continue;
		uint16_t len_testo;
		memcpy( &len_testo, &curr_post->len_testo, 2 );
		// for ( int j = 0; j < sizeof( curr_post->id ); j++ )
		// 	fprintf( fp, "%02x", ( ( unsigned char *)&curr_post->id )[ j ] );
		fprintf( fp, "%08x\x1f%u\x1f", curr_post->id, curr_post->timestamp );
		fwrite( curr_post->data, curr_post->len_mittente, 1, fp );
		fputc( '\x1f', fp );
		fwrite( curr_post->data + curr_post->len_mittente, curr_post->len_oggetto, 1, fp );
		fputc( '\x1f', fp );
		fwrite( curr_post->data + curr_post->len_mittente + curr_post->len_oggetto, len_testo, 1, fp );
		fputc( '\n', fp );
	}

	while ( fclose( fp ) )
	{
		if ( errno != EINTR )
			err( EXIT_FAILURE, "storeDatabase: errore nella close" );
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

int delDatabaseEntry( uint32_t entry_id )
{
	if ( !entry_id )
		return -1;
	for ( int i = 0; i < gPostCount; i++ )
	{
		if ( gPosts[ i ]->id == entry_id )
		{
			gPosts[ i ] = 0;
			return 0;
		}
	}

	return -1;
}

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
		if ( sscanf( buffer, "%[^\x1f]\x1f%[^\x1f]\x1f%d", user, pass, is_admin + gUserCount ) < 3 )
			continue;

		gUsers[ gUserCount ][ 0 ] = strdup( user );
		gUsers[ gUserCount ][ 1 ] = strdup( pass );
		if ( gUsers[ gUserCount ][ 0 ]  == NULL || gUsers[ gUserCount ][ 1 ] == NULL )
		{
			fprintf( stderr, "loadUsers: memoria insufficiente\n" );
			fclose( fp );
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
	unloadDatabase();
	unloadUsers();
	err( eval, fmt );
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
	 - ricevi in timeout di 1 minuto
	 - se non ricevuto: riprova poi closeSocket -> -1
	 - se ricevuto non RESP: closeSocket -> 1
	 - se ricevuto RESP: decodifica BUF -> host order -> ritorna (0)	*/

int SendAndGetResponse( int sockfd, unsigned char* msg_buf, size_t *len, Client_Frametype resp )
{
	int ret;
	uint16_t tmp_u16;
	uint32_t tmp_u32;
	uint64_t tmp_u64;
	//char encode_buf[8];
	
	uint8_t value_u8;

	switch ( *msg_buf )
	{
		case SERV_WELCOME:
			conv_u16( msg_buf + 2, TO_NETWORK );	// n_posts
			conv_u64( msg_buf + 4, TO_NETWORK );	// local_time
			break;

		case SERV_ENTRIES:
			unsigned char* scanptr = msg_buf;
			int 	       data_length;
			uint8_t        n       = scanptr[1];
			scanptr += 2;

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

	while ( send( sockfd, msg_buf, *len, 0 ) < 0 )
	{
		if ( errno != EINTR )
			return -1;
	}

	while ( ( *len = recv( sockfd, msg_buf, 1, 0 ) ) < 1 )
	{
		if ( errno != EINTR )
			return -1;
	}

	size_t 	n_to_receive;
	ssize_t rc;

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


void* clientSession( void* arg )
{
	struct session_data *session = ( struct session_data *)arg;
	unsigned char	    client_user[256];
	unsigned char	    client_pass[256];
	int 		    user_auth_level = -1;
	unsigned char 	    msg_buf[655360];
	size_t 		    msg_size;

	printf( "server: Spawnato un nuovo thread per gestire la connessione in entrata di %s\n", inet_ntoa( session->client_addr ) );

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
				closeSocket( session->sockfd );
				return NULL;
			}
			else if ( ret )
			{
				fprintf( stderr, "server (#%lu): Ricevuto campo inaspettato (%#08b diverso da LOGIN). Chiudo la connessione.\n",
						(unsigned long)session->tid, *msg_buf );
				closeSocket( session->sockfd );
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

	uint16_t post_count = gPostCount;
	msg_buf[0] = SERV_WELCOME;
	msg_buf[1] = ( unsigned char )user_auth_level;
	memcpy( msg_buf + 2, &post_count, 2 );
	/* inserisci orario locale... */
	int64_t local_time = ( int64_t )time( NULL );
	memcpy( msg_buf + 4, &local_time, 8 );
	msg_size = 12;

	/* Main loop */
	while (1)
	{
		int ret = SendAndGetResponse( session->sockfd, msg_buf, &msg_size, 0 );
		if ( ret < 0 )
		{
			printf( "server: Sessione di %s terminata\n", inet_ntoa( session->client_addr ) );
			closeSocket( session->sockfd );
			return NULL;
		}

		switch ( *msg_buf )
		{
			case CLI_GETPOSTS:
				uint8_t        page    = msg_buf[1];
				uint8_t        limit   = msg_buf[2];
				uint8_t	       count   = 0;
				unsigned char* scanptr = msg_buf + 2;

				for ( unsigned int i = (page - 1) * limit; i < page * limit; i++ )
				{
					if ( i >= gPostCount )
						break;

					Post *curr_post = gPosts[i];

					uint16_t len_testo;
					memcpy( &len_testo, &curr_post->len_testo, 2 );
					size_t post_size = curr_post->len_mittente + curr_post->len_oggetto + len_testo;

					memcpy( scanptr, curr_post, POST_HEADER_SIZE + post_size );
					scanptr += POST_HEADER_SIZE + post_size;
					count++;
				}

				msg_buf[0] = SERV_ENTRIES;
				msg_buf[1] = count;
				msg_size = scanptr - msg_buf;
				break;

			case CLI_POST:
				msg_size = 2;

				if ( gPostCount == 2048 )
				{
					msg_buf[0] = SERV_NOT_OK;
					msg_buf[1] = 0xFF;
					break;
				}

				memcpy( client_user, msg_buf + 3, msg_buf[1] );
				memcpy( client_pass, msg_buf + 3 + msg_buf[1], msg_buf[2] );
				client_user[ msg_buf[1] ] = '\0';
				client_user[ msg_buf[2] ] = '\0';
				if ( tryLogin( client_user, client_pass ) < 1 )
				{
					msg_buf[0] = SERV_NOT_OK;
					msg_buf[1] = 0;
					break;
				}

				Post *sent_post = (Post *)( msg_buf + 3 + msg_buf[1] + msg_buf[2] );
				uint16_t len_testo;
				memcpy( &len_testo, &sent_post->len_testo, 2 );

				gPosts[ gPostCount ] = malloc( POST_HEADER_SIZE + strlen( client_user ) + sent_post->len_oggetto + len_testo );
				if ( !gPosts[ gPostCount ] )
				{
					/* Out of memory! */
					msg_buf[0] = SERV_NOT_OK;
					msg_buf[1] = 0x01;
					break;
				}
				Post *new_post = gPosts[ gPostCount++ ];

				memcpy( new_post, sent_post, POST_HEADER_SIZE );

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
					(char*)sent_post + POST_HEADER_SIZE + sent_post->len_mittente,  sent_post->len_oggetto );
				memcpy( (char*)new_post  + POST_HEADER_SIZE + userlen                 + sent_post->len_oggetto,
					(char*)sent_post + POST_HEADER_SIZE + sent_post->len_mittente + sent_post->len_oggetto, len_testo );

				printf( "server: Nuovo messaggio pubblicato da %s\n", inet_ntoa( session->client_addr ) );
				if ( storeDatabase() < 0 )
					printf( "server: Impossibile aggiornare il database dei messaggi\n" );

				msg_buf[0] = SERV_OK;
				msg_size = 1;
				break;

			case CLI_DELPOST:
				msg_size = 2;

				memcpy( client_user, msg_buf + 3, msg_buf[1] );
				memcpy( client_pass, msg_buf + 3 + msg_buf[1], msg_buf[2] );
				client_user[ msg_buf[1] ] = '\0';
				client_pass[ msg_buf[2] ] = '\0';
				if ( tryLogin( client_user, client_pass ) < 1 )
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
					msg_buf[1] = 0x0;
					break;
				}

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
					break;
				}

				if ( memcmp( gPosts[ post_index ]->data, client_user, gPosts[ post_index ]->len_mittente ) )
				{
					msg_buf[0] = SERV_NOT_OK;
					msg_buf[1] = 0x1;
					break;
				}

				bzero( &gPosts[ post_index ]->id, 4 );
				storeDatabase();
				unloadDatabase();
				loadDatabase();

				msg_buf[0] = SERV_OK;
				msg_size = 1;
				break;

			default:
				break;

		}
	}

}

int main( int argc, char *argv[] )
{
	int s_list;
	int s_client;
	struct sockaddr_in serv_addr;
	struct sockaddr_in client_addr;
	int sin_size;

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
	while ( s_client = accept( s_list, ( struct sockaddr *)&client_addr, &sin_size ) )
	{
		if ( s_client < 0 ) 
		{
			if ( errno != EINTR )
				warn( "server: Impossibile accettare una connessione in entrata" );
			continue;
		}

		int slot = -1;
		for ( int i = 0; i < MAXCONNS; i++ )
			if ( !sessions[ i ].tid )
			{
				slot = i;
				break;
			}

		if ( slot == -1 )
		{
			printf( "server: Il server è pieno, respingo la connessione in entrata.\n" );
			closeSocket( s_client );
			continue;
		}

		sessions[ slot ].sockfd  = s_client;
		sessions[ slot ].client_addr = client_addr.sin_addr;

		if ( pthread_create( &sessions[ slot ].tid, NULL, clientSession, &sessions[ slot ] ) )
		{
			warn( "server: Impossibile spawnare un nuovo thread per processare la sessione di %s. Chiudo la connessione.\nMotivo",
					inet_ntoa( client_addr.sin_addr ) );
			closeSocket( s_client );
			sessions[ slot ].tid = 0;
		}

	}
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
