#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <string.h>
#include <err.h>
#include "types.h"
#include "helpers.h"

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
		case SERV_WELCOME:
			conv_u16( msg_buf + 2, TO_NETWORK );	// n_posts
			conv_u64( msg_buf + 4, TO_NETWORK );	// local_time
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

	size_t 	n_to_receive;
	ssize_t rc;

	/* Receive and decode */
	switch ( *msg_buf )
	{
		case SERV_WELCOME:
			if ( ( rc = sockReceiveAll( sockfd, msg_buf + 1, 11 ) ) < 0 )
				return -1;
			*len += rc;
			conv_u16( msg_buf + 2, TO_HOST );
			conv_u64( msg_buf + 4, TO_HOST );

		case SERV_ENTRIES:
			if ( ( rc = sockReceiveAll( sockfd, msg_buf + 1, 1 ) ) < 0 )
				return -1;
			*len += rc;

			for ( int i = 0; i < 10; i++ )
				if ( gLoadedPosts[i] )
				{
					free( gLoadedPosts[i] );
					gLoadedPosts[i] = NULL;
				}

			unsigned char *scanptr = msg_buf + 1;
			for ( int i = 0; i < msg_buf[1]; i++ )
			{
				tmp_u32 = conv_u32( scanptr,     TO_HOST );	// id
				tmp_u16 = conv_u16( scanptr + 6, TO_HOST );	// len_testo
				tmp_u64 = conv_u64( scanptr + 8, TO_HOST );	// timestamp
				scanptr += POST_HEADER_SIZE + tmp_u16 + scanptr[4] + scanptr[5];
			}
				
		case CLI_GETPOSTS:
			if ( rc = sockReceiveAll( sockfd, msg_buf + 1, 2 ) < 0 )
				return -1;
			*len += rc;
			break;

		default:
			break;
	}


int main( int argc, char *argv[] )
{
	char 		   *s_addr;
	int  		    s_port;
	int  		    s_sock;
	char 		    user[256];
	char 		    pass[256];
	int  		    auth_level = -1;
	char  		    msg_buf[65536];
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

	if ( connect( s_sock, &servaddr, sizeof( servaddr ) ) < 0 )
		err( EXIT_FAILURE, "client: impossibile connettersi al server" );

	
	/* Main loop */

#ifdef DEBUG
	printf( "Indirizzo: %s\tPorta: %d\n", s_addr, s_port );
#endif

	return 0;
}
