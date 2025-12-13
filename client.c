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
#include <termios.h>
#include "types.h"
#include "helpers.h"
#include "ui.h"

int   s_sock;
Post *gLoadedPosts[10] = { 0 };

extern int max_posts_per_page;

ClientState gState;

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
			if ( ( rc = sockReceiveAll( sockfd, msg_buf + 1, 12 ) ) < 0 )
				return -1;
			*len += rc;
			if ( ( rc = sockReceiveAll( sockfd, msg_buf + 13, msg_buf[12] ) ) < 0 )
				return -1;
			*len += rc;
			conv_u16( msg_buf + 2, TO_HOST );	// n_posts
			conv_u64( msg_buf + 4, TO_HOST );	// local_time
			break;

		case SERV_ENTRIES:
			if ( ( rc = sockReceiveAll( sockfd, msg_buf + 1, 3 ) ) < 0 )
				return -1;
			*len += rc;
			conv_u16( msg_buf + 1, TO_HOST );

			unsigned char *scanptr = msg_buf + 4;
			for ( int i = 0; i < msg_buf[3]; i++ )
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

		case SERV_NOT_OK:
			if ( ( rc = sockReceiveAll( sockfd, msg_buf + 1, 1 ) ) < 0 )
				return -1;
			*len += rc;
				
		default:
			break;
	}

	return msg_buf[0] == resp;
}

void exitProgram( int exit_code )
{
	if ( gState.current_screen != STATE_INTRO ) printf( "\033[2J" );

	close( s_sock );
	for ( int i = 0; i < 10; i++ )
		if ( gLoadedPosts[i] )
		{
			free( gLoadedPosts[i] );
			gLoadedPosts[i] = NULL;
		}

	setTerminalMode( TERM_CANON );
	exit( exit_code );
}

int loadPosts( char* msg_buf, size_t* msg_size, unsigned char page )
{
	msg_buf[0] = CLI_GETPOSTS;
	msg_buf[1] = page;
	msg_buf[2] = max_posts_per_page;
	*msg_size  = 3;

	int ret = SendAndGetResponse( s_sock, msg_buf, msg_size, SERV_ENTRIES );

	if ( msg_buf[3] == 0 )
		return 0;

	// Post parsing
	uint16_t       n_posts;
	unsigned char* scanptr = msg_buf + 4;
	memcpy( &n_posts, msg_buf + 1, 2 );
	gState.num_posts    = n_posts;
	gState.loaded_page  = page;
	gState.loaded_posts = msg_buf[3];
	for ( int i = 0; i < max_posts_per_page; i++ )
		if ( gState.cached_posts[ i ] )
		{
			free( gState.cached_posts[ i ] );
			gState.cached_posts[ i ] = NULL;
		}
	
	for ( int i = 0; i < gState.loaded_posts; i++ )
	{
		Post curr_post;
		memcpy( &curr_post, scanptr, POST_HEADER_SIZE );

		size_t post_size = curr_post.len_mittente + curr_post.len_oggetto + curr_post.len_testo;
		gState.cached_posts[ i ] = malloc( POST_HEADER_SIZE + post_size );

		*gState.cached_posts[ i ] = curr_post;
		memcpy( gState.cached_posts[ i ]->data, scanptr + POST_HEADER_SIZE, post_size );

		scanptr += POST_HEADER_SIZE + post_size;
	}

	return gState.loaded_posts;
}

int main( int argc, char *argv[] )
{
	char 		   *s_addr;
	int  		    s_port;
	char 		    user[256];
	char 		    pass[256];
	int  		    auth_level = -1;		/* 1 amministratore, 0: utente standard, -1: anonimo */
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
		setTerminalMode( TERM_CANON );
		printf( "%s richiede l'autenticazione per poter accedere alla bacheca.\n\n", argv[1] );

		while (1)
		{
			int len_user, len_pass;

			if ( ( len_user = getValidInput( user, 256, "Nome utente: " ) ) < 0 )
				exit( EXIT_FAILURE );
			setTerminalMode( TERM_CANON_NOECHO );
			if ( ( len_pass = getValidInput( pass, 256, "Password: " ) ) < 0 )
				exit( EXIT_FAILURE );
			setTerminalMode( TERM_CANON );

			msg_buf[0] = CLI_LOGIN;
			msg_buf[1] = len_user;
			msg_buf[2] = len_pass;
			memcpy( msg_buf + 3           , user, len_user );
			memcpy( msg_buf + 3 + len_user, pass, len_pass );
			msg_size = 3 + len_user + len_pass;

			if ( SendAndGetResponse( s_sock, msg_buf, &msg_size, 0 ) < 0 )
				exit( EXIT_FAILURE );

			if ( *msg_buf == SERV_WELCOME )
			{
				auth_level = msg_buf[1];
				break;
			}

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
	char     board_title[260];
	memcpy( &n_posts, msg_buf + 2, 2 );
	memcpy( &server_time, msg_buf + 4, 8 );
	sprintf( board_title, msg_buf[12] ? " (%.*s)" : "", msg_buf[12], msg_buf + 13 );

	printf( "\nBenvenuto nella bacheca elettronica di %s%s.\nPost presenti: %u\nOrario del server: %lld\n", argv[1], board_title, n_posts, server_time );
	printf( "\nInvio) Leggi i post\n    q) Esci\n\n" );

	setTerminalMode( TERM_RAW );

	// Client state setup
	gState.current_screen = STATE_INTRO;
	gState.cached_posts = NULL;		// da popolare con un'array di N puntatori a Post (N verrà dedotto con updateWindowSize())
	gState.num_posts = 0;
	gState.selected_post = 0;
	*gState.state_label = '\0';
	strncpy( gState.board_title, msg_buf + 13, msg_buf[12] + 1 );
	strncpy( gState.server_addr, argv[1], 100 );
	gState.user = user;
	gState.pass = pass;
	gState.auth_level = auth_level;

	gState.quit_enabled = true;

	updateWinSize();
	gState.cached_posts = malloc( sizeof( char* ) * max_posts_per_page );

	while (1)
	{
		int action = getchar();

		switch ( action )
		{
			case 'q':
			case 'Q':
				if ( gState.quit_enabled )
					exitProgram( EXIT_SUCCESS );

			case '\n':
				// TODO: rendere possibile inserire a capo in post
				if ( gState.current_screen == STATE_INTRO )
				{
					printf( "Caricamento post...\n" );
					// carica post...
					loadPosts( msg_buf, &msg_size, 1 );
					gState.current_screen = STATE_LISTING;
					drawTui( &gState );
				}
				else if ( gState.current_screen == STATE_LISTING && gState.readpost_enabled )
				{
					gState.current_screen = STATE_SINGLEPOST;
					drawTui( &gState );
				}
				break;


			case 'k':
			case 'K':
				if ( gState.current_screen == STATE_WRITING )
				{
					// inserisci lettera nel buffer...
				}
				else if ( gState.listnav_enabled && gState.selected_post > 0 )
				{
					gState.selected_post--;
					drawTui( &gState );
				}
				break;

			case 'j':
			case 'J':
				if ( gState.current_screen == STATE_WRITING )
				{
					// inserisci lettera nel buffer...
				}
				else if ( gState.listnav_enabled && gState.selected_post != gState.loaded_posts - 1 )
				{
					gState.selected_post++;
					drawTui( &gState );
				}
				break;


			case 'h':
			case 'H':
				if ( gState.current_screen == STATE_WRITING )
				{
					// inserisci lettera nel buffer...
				}
				else if ( gState.pagenav_enabled && gState.loaded_page > 1 )
				{
					sprintf( gState.state_label, "Caricamento dei post..." );
					drawTui( &gState );
					loadPosts( msg_buf, &msg_size, --gState.loaded_page );
					gState.selected_post = 0;
					drawTui( &gState );
				}
				break;

			case 'l':
			case 'L':
				if ( gState.current_screen == STATE_WRITING )
				{
					// inserisci lettera nel buffer...
				}
				else if ( gState.pagenav_enabled && gState.loaded_page < gState.num_posts / max_posts_per_page + 1 )
				{
					sprintf( gState.state_label, "Caricamento dei post..." );
					drawTui( &gState );
					loadPosts( msg_buf, &msg_size, ++gState.loaded_page );
					gState.selected_post = 0;
					drawTui( &gState );
				}
				break;


			case 'b':
			case 'B':
				if ( gState.goback_enabled )
				{
					gState.current_screen = STATE_LISTING;
					drawTui( &gState );
				}
		}
	}



	//if ( ( get1 & 0b11011111 ) == 'Q' ) exitProgram( EXIT_SUCCESS );
	//{
	//	int get2 = getchar();
	//	printf( "%d %d\n", get1, get2 );
	//}


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

	msg_buf[0] = CLI_DELPOST;
	msg_buf[1] = strlen( user );
	msg_buf[2] = strlen( pass );
	strcpy( msg_buf + 3, user );
	strcpy( msg_buf + 3 + msg_buf[1], pass );
	uint32_t post_id = 0xdbc5ab84;
	memcpy( msg_buf + 3 + msg_buf[1] + msg_buf[2], &post_id, 4 );
	msg_size = 3 + msg_buf[1] + msg_buf[2] + 4;
	ret = SendAndGetResponse( s_sock, msg_buf, &msg_size, SERV_OK );
	printf( "%d\n", ret );
	
	return 0;

	for ( int i = 0; i < 10; i++ )
		if ( gLoadedPosts[i] )
		{
			free( gLoadedPosts[i] );
			gLoadedPosts[i] = NULL;
		}

	return 0;
}
