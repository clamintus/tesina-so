#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <strings.h>
#include <string.h>
#include <errno.h>
#include <err.h>
#include "types.h"
#include "helpers.h"

int   s_sock;
Post *gLoadedPosts[10] = { 0 };

/*
 *  CLIENT:
 *  - argv[] -> addr, port
 *  - connetti a addr, port
 *  - se ricevo AUTHENTICATE -> chiedi user e pass -> invia LOGIN
 *  - main menu
 */

int parseCmdLine( int argc, char *argv[], char **server_addr, int *server_port )
{
	char *endptr;

	if ( argc != 3 )
	{
esci:
		printf( "Sintassi: %s [indirizzo] [porta]\n", argv[0] );
		exit( 1 );
	}

	*server_addr = argv[1];
	*server_port = strtol( argv[2], &endptr, 10 );
	if ( *endptr != '\0' )
		goto esci;
}

int SendAndGetResponse( int sockfd, unsigned char* msg_buf, size_t *len, Server_Frametype resp )
{
	int ret;
	uint16_t tmp_u16;
	uint32_t tmp_u32;
	uint64_t tmp_u64;
	//char encode_buf[8];
	
	uint8_t value_u8;


	/* Encoding */

	switch ( *msg_buf )
	{
		case CLI_POST:
		case CLI_DELPOST:
			int offset = 3 + msg_buf[1] + msg_buf[2];	// CLI_POST + header + len_user + len_pass
			conv_u32( msg_buf + offset, TO_NETWORK );	// id
			if ( *msg_buf == CLI_POST )
			{
				conv_u16( msg_buf + offset + 6, TO_NETWORK );	// len_testo
				conv_u64( msg_buf + offset + 8, TO_NETWORK );	// timestamp
			}
			break;

		default:
			break;
	}


	/* Invio il pacchetto e ricevo il primo byte della risposta, che ne identifica il tipo */

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

	ssize_t rc;

	/* Receive and decode */
	switch ( *msg_buf )
	{
		case SERV_WELCOME:
			if ( ( rc = sockReceiveAll( sockfd, msg_buf + 1, 11 ) ) < 0 )
				return -1;
			*len += rc;
			conv_u16( msg_buf + 2, TO_HOST );	// n_posts
			conv_u64( msg_buf + 4, TO_HOST );	// local_time
			break;

		case SERV_ENTRIES:
			if ( ( rc = sockReceiveAll( sockfd, msg_buf + 1, 1 ) ) < 0 )
				return -1;
			*len += rc;

			unsigned char *scanptr = msg_buf + 2;
			for ( int i = 0; i < msg_buf[1]; i++ )
			{
				if ( ( rc = sockReceiveAll( sockfd, scanptr, POST_HEADER_SIZE ) ) < 0 )
					return -1;
				*len += rc;
				tmp_u32 = conv_u32( scanptr,     TO_HOST );	// id
				tmp_u16 = conv_u16( scanptr + 6, TO_HOST );	// len_testo
				tmp_u64 = conv_u64( scanptr + 8, TO_HOST );	// timestamp

				if ( ( rc = sockReceiveAll( sockfd, scanptr + POST_HEADER_SIZE, tmp_u16 + scanptr[4] + scanptr[5] ) ) < 0 )
					return -1;
				*len += rc;
				scanptr += POST_HEADER_SIZE + rc;
			}
			break;	
				
		default:
			break;
	}

	return msg_buf[0] == resp;
}

void exitProgram( int exit_code )
{
	close( s_sock );
	for ( int i = 0; i < 10; i++ )
		if ( gLoadedPosts[i] )
		{
			free( gLoadedPosts[i] );
			gLoadedPosts[i] = NULL;
		}

	exit( exit_code );
}

int main( int argc, char *argv[] )
{
	char 		   *s_addr;
	int  		    s_port;
	char 		    user[256];
	char 		    pass[256];
	int  		    auth_level = -1;
	char  		    msg_buf[655360];
	size_t		    msg_size;
	struct sockaddr_in  servaddr;
	struct hostent     *he;

	parseCmdLine( argc, argv, &s_addr, &s_port );
	if ( s_port < 0 || s_port > 65535 )
	{
		fprintf( stderr, "client: Numero di porta non valido.\n" );
		exit( EXIT_FAILURE );
	}


	/* Risoluzione del nome host (se necessaria) */

	bzero( &servaddr, sizeof( servaddr ) );

	if ( !inet_aton( s_addr, &servaddr.sin_addr ) )
	{
		printf( "Risoluzione di %s... ", s_addr );
		fflush( stdout );

		if ( ( he = gethostbyname( s_addr ) ) == NULL )
		{
			printf( "fallita.\n" );
			exit( EXIT_FAILURE );
		}

		servaddr.sin_addr = *( struct in_addr *)he->h_addr_list[0];
		printf( "%s\n", inet_ntoa( servaddr.sin_addr ) );
	}


	/* Connessione al server della bacheca */

	if ( ( s_sock = socket( AF_INET, SOCK_STREAM, 0 ) ) < 0 )
		err( EXIT_FAILURE, "client: Errore nella creazione della socket" );

	servaddr.sin_family = AF_INET;
	servaddr.sin_port   = htons( (unsigned short int)s_port );

	printf( "Connessione a %s:%d...\n", inet_ntoa( servaddr.sin_addr ), s_port );

	if ( connect( s_sock, (struct sockaddr *)&servaddr, sizeof( servaddr ) ) < 0 )
		err( EXIT_FAILURE, "client: impossibile connettersi al server" );

	
	/* Main loop */

#ifdef DEBUG
	printf( "Connessione stabilita.\n\n" );
	printf( "Indirizzo: %s\tPorta: %d\n", s_addr, s_port );
#endif

	int ret;
       	while ( ( ret = recv( s_sock, msg_buf, 1, 0 ) ) < 0 )
	{
		if ( errno != EINTR )
		{
			warn( "client: ricezione fallita" );
			exitProgram( EXIT_FAILURE );
		}
	}

	if ( *msg_buf == SERV_AUTHENTICATE )
	{
		printf( "%s richiede l'autenticazione per poter accedere alla bacheca.\n\n", argv[1] );

		while (1)
		{
			int len_user, len_pass;

			if ( ( len_user = getValidInput( user, 256, "Nome utente: " ) ) < 0 )
				exit( EXIT_FAILURE );
			if ( ( len_pass = getValidInput( pass, 256, "Password: " ) ) < 0 )
				exit( EXIT_FAILURE );

			msg_buf[0] = CLI_LOGIN;
			msg_buf[1] = len_user;
			msg_buf[2] = len_pass;
			memcpy( msg_buf + 3           , user, len_user );
			memcpy( msg_buf + 3 + len_user, pass, len_pass );
			msg_size = 3 + len_user + len_pass;

			if ( SendAndGetResponse( s_sock, msg_buf, &msg_size, 0 ) < 0 )
				exit( EXIT_FAILURE );

			if ( *msg_buf == SERV_WELCOME )
				break;

			printf( "\nCredenziali errate, riprova.\n" );
		}
	}
	else
	{
		if ( sockReceiveAll( s_sock, msg_buf + 1, 11 ) < 0 )
			exitProgram( EXIT_FAILURE );
		conv_u16( msg_buf + 2, TO_HOST );
		conv_u64( msg_buf + 4, TO_HOST );
	}

	uint16_t n_posts;
	int64_t  server_time;
	memcpy( &n_posts, msg_buf + 2, 2 );
	memcpy( &server_time, msg_buf + 4, 8 );

	printf( "\nBenvenuto nella bacheca elettronica.\nPost presenti: %u\nOrario del server: %lld\n", n_posts, server_time );

	msg_buf[0] = CLI_GETPOSTS;
	msg_buf[1] = 1;
	msg_buf[2] = 10;
	msg_size   = 3;

	ret = SendAndGetResponse( s_sock, msg_buf, &msg_size, SERV_ENTRIES );

	msg_buf[0] = CLI_POST;
	msg_buf[1] = strlen( user );
	msg_buf[2] = strlen( pass );
	strcpy( msg_buf + 3, user );
	strcpy( msg_buf + 3 + strlen( user ), pass );

	const char* TEST_MITT = "Sergio";
	const char* TEST_OGG  = "Prova";
	const char* TEST_TEXT = "Questo e' un messaggio di prova!!!!!!!!!!!!!!!!!!!!!";
	
	Post *newpost = malloc( POST_HEADER_SIZE + strlen( TEST_MITT ) + strlen( TEST_OGG ) + strlen( TEST_TEXT ) );
	uint32_t id = 0x11223344;
	uint64_t timestamp = 0xFFFFFFFFFFFFFFFF;
	newpost->len_mittente = strlen( TEST_MITT );
	newpost->len_oggetto = strlen( TEST_OGG );
	uint16_t len_testo = strlen( TEST_TEXT );
	memcpy( &newpost->id, &id, 4 );
	memcpy( &newpost->len_testo, &len_testo, 2 );
	memcpy( &newpost->timestamp, &timestamp, 8 );

	strcpy( newpost->data, TEST_MITT );
	strcat( newpost->data, TEST_OGG );
	strcat( newpost->data, TEST_TEXT );

	size_t post_size = POST_HEADER_SIZE + newpost->len_mittente + newpost->len_oggetto + len_testo;
	memcpy( msg_buf + 3 + strlen( user ) + strlen( pass ), newpost, post_size );
	msg_size = 3 + strlen( user ) + strlen( pass ) + post_size;
	ret = SendAndGetResponse( s_sock, msg_buf, &msg_size, SERV_OK );
	free( newpost );
	
	return 0;

	for ( int i = 0; i < 10; i++ )
		if ( gLoadedPosts[i] )
		{
			free( gLoadedPosts[i] );
			gLoadedPosts[i] = NULL;
		}

	return 0;
}
