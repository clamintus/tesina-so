#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
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
	unsigned long long timestamp;
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
		if ( sscanf( buffer, "%[^\x1f]\x1f%llu\x1f%[^\x1f]\x1f%[^\x1f]\x1f%s", id_buf, &timestamp, mittente, oggetto, testo ) < 5 )
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
		// for ( int j = 0; j < sizeof( curr_post->id ); j++ )
		// 	fprintf( fp, "%02x", ( ( unsigned char *)&curr_post->id )[ j ] );
		fprintf( fp, "%08x\x1f%u\x1f", curr_post->id, curr_post->timestamp );
		fwrite( curr_post->data, curr_post->len_mittente, 1, fp );
		fputc( '\x1f', fp );
		fwrite( curr_post->data + curr_post->len_mittente, curr_post->len_oggetto, 1, fp );
		fputc( '\x1f', fp );
		fwrite( curr_post->data + curr_post->len_mittente + curr_post->len_oggetto, curr_post->len_testo, 1, fp );
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

void deinitAndErr( int eval, const char* fmt )
{
	unloadDatabase();
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

int sockReceiveAll( int sockfd, unsigned char* msg_buf, size_t len )
{
	int n_left = len;
	int ret;

	while ( n_left )
	{
		if ( ( ret = recv( sockfd, msg_buf, n_left, 0 ) ) < 0 )
		{
			if ( errno == EINTR )
				ret = 0;
			else
				return -1;
		}
		msg_buf += ret;
		n_left  -= ret;
	}

	return len;
}

enum conv_type {
	TO_HOST,
	TO_NETWORK
};

uint16_t conv_u16( void* u16_addr, enum conv_type to_what )
{
	uint16_t tmp_u16;

	memcpy( &tmp_u16, u16_addr, 2 );
	tmp_u16 = to_what == TO_NETWORK ? htons( tmp_u16 ) : ntohs( tmp_u16 );
	memcpy( u16_addr, &tmp_u16, 2 );
	
	return tmp_u16;
}
uint32_t conv_u32( void* u32_addr, enum conv_type to_what )
{
	uint32_t tmp_u32;

	memcpy( &tmp_u32, u32_addr, 4 );
	tmp_u32 = to_what == TO_NETWORK ? htonl( tmp_u32 ) : ntohl( tmp_u32 );
	memcpy( u32_addr, &tmp_u32, 4 );

	return tmp_u32;
}
uint64_t conv_u64( void* u64_addr, enum conv_type to_what )
{
	uint64_t tmp_u64;

	memcpy( &tmp_u64, u64_addr, 8 );
	tmp_u64 = to_what == TO_NETWORK ? htobe64( tmp_u64 ) : be64toh( tmp_u64 );
	memcpy( u64_addr, &tmp_u64, 8 );

	return tmp_u64;
}
	
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
			if ( rc = sockReceiveAll( sockfd, msg_buf + 1, 2 ) < 0 )
				return -1;
			*len += rc;
			n_to_receive = msg_buf[1] + msg_buf[2];
			if ( rc = sockReceiveAll( sockfd, msg_buf + 3, n_to_receive ) < 0 )
				return -1;
			*len += rc;
			
			if ( *msg_buf == CLI_POST )
			{
				unsigned char* msg_buf_2;
				msg_buf_2 = msg_buf + 3 + n_to_receive;
				if ( rc = sockReceiveAll( sockfd, msg_buf_2, POST_HEADER_SIZE ) < 0 )
					return -1;
				*len += rc;
				n_to_receive  = conv_u16( msg_buf_2 + 6, TO_HOST );	// len_testo
				n_to_receive += msg_buf_2[4];				// len_mittente
				n_to_receive += msg_buf_2[5];				// len_oggetto
				if ( rc = sockReceiveAll( sockfd, msg_buf_2 + POST_HEADER_SIZE, n_to_receive ) < 0 )
					return -1;
				*len += rc;
			}
			else if ( *msg_buf == CLI_DELPOST )
			{
				if ( rc = sockReceiveAll( sockfd, msg_buf + 3 + n_to_receive, 4 ) < 0 )
					return -1;
				*len += rc;
				conv_u32( msg_buf + 3 + n_to_receive, TO_HOST );	// id
			}
			break;

		case CLI_GETPOSTS:
			if ( rc = sockReceiveAll( sockfd, msg_buf + 1, 2 ) < 0 )
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
	unsigned char 	    msg_buf[65535];
	size_t 		    msg_size;

	printf( "server: Spawnato un nuovo thread per gestire la connessione in entrata di %s\n", inet_ntoa( session->client_addr ) );

	if ( !gAllowGuests )
	{
		/* Login required */
		msg_buf[0] = SERV_AUTHENTICATE;
		msg_size = 1;
		
		int ret = SendAndGetResponse( session->sockfd, msg_buf, &msg_size, CLI_LOGIN );
		
		if ( ret < 0 )
		{
			closeSocket( session->sockfd );
			return NULL;
		}
		else if ( ret )
		{
			fprintf( stderr, "server: Ricevuto campo inaspettato (%#08b diverso da LOGIN). Chiudo la connessione.\n", *msg_buf );
			closeSocket( session->sockfd );
			return NULL;
		}
		
		// Rimane il caso CLI_LOGIN
		memcpy( client_user, msg_buf + 3             , msg_buf[1] );
		memcpy( client_pass, msg_buf + 3 + msg_buf[1], msg_buf[2] );
		user_auth_level = 0;

		msg_buf[0] = SERV_OK;
		while ( send( session->sockfd, msg_buf, 1, 0 ) < 0 )
		{
			if ( errno != EINTR )
			{
				warn( "server: Errore nella send, chiudo la connessione.\nMotivo" );
				closeSocket( session->sockfd );
				return NULL;
			}
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
				err( EXIT_FAILURE, "server: Impossibile creare il file database" );
		while ( close( db_fp ) < 0 )
			if ( errno != EINTR )
				err( EXIT_FAILURE, "server: Impossibile chiudere il nuovo database" );
	}
#ifdef DEBUG
	printf( "Caricate %d entry nel database.\n", gPostCount );
#endif

	if ( s_list = socket( AF_INET, SOCK_STREAM, 0 ) < 0 )
		deinitAndErr( EXIT_FAILURE, "server: Errore nella creazione della socket" );

	serv_addr.sin_family      = AF_INET;
	serv_addr.sin_port        = gPort;
	serv_addr.sin_addr.s_addr = INADDR_ANY;

	if ( bind( s_list, ( struct sockaddr *)&serv_addr, sizeof( serv_addr ) < 0 ) )
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
