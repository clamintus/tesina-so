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
#ifndef __SWITCH__
#include <termios.h>
#endif
#include <signal.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <time.h>
#include "types.h"
#include "helpers.h"
#include "ui.h"

#ifdef __SWITCH__
#include <switch.h>
#include "switchport.h"
#include "font.h"
#endif

int   s_sock;
//Post *gLoadedPosts[10] = { 0 };
ClientState gState;

#ifdef STATIC_BUFFER
unsigned char msg_buf[655360];
size_t msg_size;
#endif

#ifdef __SWITCH__
PadState gPad;
PadRepeater gPadRepeater;
SwkbdConfig gSwkbd;
#endif

volatile sig_atomic_t gNewDataAvailable = 0;
volatile sig_atomic_t gResized = 0;

#define OGGETTO_MAXLEN 255
#define TESTO_MAXLEN 60000

int post_limit;
extern int max_posts_per_page;
extern struct winsize window;

/*
 *  CLIENT:
 *  - argv[] -> addr, port
 *  - connetti a addr, port
 *  - se ricevo AUTHENTICATE -> chiedi user e pass -> invia LOGIN
 *  - main menu
 */

#ifndef __SWITCH__
void oob_handler( int sig )
{
	gNewDataAvailable = 1;
}

void resize_handler( int sig )
{
	gResized = 1;
}
#endif

int parseCmdLine( int argc, char *argv[], char **server_addr, int *server_port )
{
	char *endptr;

	if ( argc != 3 )
	{
esci:
		printf( "Sintassi: %s [indirizzo] [porta]\n", argv[0] );
		_exit( 1 );
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
			// Broken pipe, Connection reset, Timed out...
			return -1;
	}

	ssize_t rc;

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
	if ( gState.current_screen != STATE_INTRO ) printf( "\033[2J\033[H" );

	close( s_sock );
	for ( int i = 0; i < gState.loaded_posts; i++ )
		if ( gState.cached_posts[i] )
		{
			free( gState.cached_posts[i] );
			gState.cached_posts[i] = NULL;
		}

	free( gState.cached_posts );
	gState.cached_posts = NULL;
	if ( gState.opened_post ) free( gState.opened_post );
	gState.opened_post = NULL;

	setTerminalMode( TERM_CANON );
	printf( "\033[?25h" );	// show cursor
	_exit( exit_code );
}

int loadPosts( unsigned char* msg_buf, size_t* msg_size, unsigned char page )
{
	msg_buf[0] = CLI_GETPOSTS;
	msg_buf[1] = page;
	msg_buf[2] = post_limit;
	*msg_size  = 3;

	int ret = SendAndGetResponse( s_sock, msg_buf, msg_size, SERV_ENTRIES );
	
	if ( ret <= 0 )
		return ret;

	//if ( msg_buf[3] == 0 )
	//	return 0;

	// Post parsing
	uint16_t       n_posts;
	unsigned char* scanptr = msg_buf + 4;
	memcpy( &n_posts, msg_buf + 1, 2 );
	gState.num_posts    = n_posts;
	gState.loaded_page  = page;
	gState.loaded_posts = msg_buf[3];

	for ( int i = 0; i < post_limit; i++ )
		if ( gState.cached_posts[ i ] )
		{
			free( gState.cached_posts[ i ] );
			gState.cached_posts[ i ] = NULL;
		}

	if ( gState.loaded_posts == 0 && page > 1 )
	{
		if ( gState.num_posts == 0 )
		{
			gState.loaded_page = 1;
			return 0;
		}

		unsigned char last_available_page = ( gState.num_posts - 1 ) / post_limit + 1;
		gState.selected_post = ( gState.num_posts - 1 ) % post_limit;
		return loadPosts( msg_buf, msg_size, last_available_page );
	}

	
	for ( int i = 0; i < gState.loaded_posts; i++ )
	{
		Post curr_post;
		memcpy( &curr_post, scanptr, POST_HEADER_SIZE );

		size_t post_size = curr_post.len_mittente + curr_post.len_oggetto + curr_post.len_testo;
		gState.cached_posts[ i ] = malloc( POST_HEADER_SIZE + post_size + 1 );

		*gState.cached_posts[ i ] = curr_post;
		memcpy( gState.cached_posts[ i ]->data, scanptr + POST_HEADER_SIZE, post_size );

		scanptr += POST_HEADER_SIZE + post_size;
	}

	if ( gState.selected_post >= gState.loaded_posts )
		gState.selected_post = gState.loaded_posts - 1;

	return gState.loaded_posts;
}

int reauth( unsigned char* msg_buf, size_t* msg_size )
{
	char user[257];
	char pass[257];
	int len_user, len_pass;
	int ret;

	printf( "\033[2J\033[1;1HOccorre l'autenticazione per continuare.\n\n\033[?25h" );
	_fflush( stdout );
	setTerminalMode( TERM_CANON );
	
	if ( ( len_user = getValidInput( user, 256, "Nome utente: " ) ) < 0 )
		return -1;
	setTerminalMode( TERM_CANON_NOECHO );
	if ( ( len_pass = getValidInput( pass, 256, "\033[?25lPassword: " ) ) < 0 )
		return -1;
	setTerminalMode( TERM_RAW );

	msg_buf[0] = CLI_LOGIN;
	msg_buf[1] = len_user;
	msg_buf[2] = len_pass;
	memcpy( msg_buf + 3           , user, len_user );
	memcpy( msg_buf + 3 + len_user, pass, len_pass );
	*msg_size = 3 + len_user + len_pass;

	if ( ( ret = SendAndGetResponse( s_sock, msg_buf, msg_size, SERV_WELCOME ) ) < 0 )
		return ret;

	if ( !ret )
		return 0;

	gState.auth_level = msg_buf[1];
	strcpy( gState.user, user );
	strcpy( gState.pass, pass );
	
	return 1;
}

int main( int argc, char *argv[] )
{
	char 		   *s_addr;
	int  		    s_port;
	char 		    user[256];
	char 		    pass[256];
	int  		    auth_level = -1;		/* 1 amministratore, 0: utente standard, -1: anonimo */
	struct sockaddr_in  servaddr;
	struct hostent     *he;
#ifndef STATIC_BUFFER
	unsigned char 	    msg_buf[655360];
	size_t 		    msg_size;
#endif
#ifndef __SWITCH__
	struct sigaction    sa = { 0 };
	sigset_t	    sigset;

	sigfillset( &sigset );
	sa.sa_handler   = oob_handler;
	sa.sa_mask      = sigset;
	sigaction( SIGURG, &sa, NULL );
	sa.sa_handler   = resize_handler;
	sa.sa_mask      = sigset;
	sigaction( SIGWINCH, &sa, NULL );
	sa.sa_handler   = SIG_IGN;
	sigaction( SIGINT, &sa, NULL );
	sigaction( SIGPIPE, &sa, NULL );

	parseCmdLine( argc, argv, &s_addr, &s_port );
#else
	consoleInit( NULL );
	consoleDebugInit( debugDevice_CONSOLE );
	padConfigureInput( 1, HidNpadStyleSet_NpadStandard );

	padInitializeDefault( &gPad );
	padRepeaterInitialize( &gPadRepeater, 60, 4 );
	socketInitializeDefault();

	swkbdCreate( &gSwkbd, 0 );
	swkbdConfigMakePresetDefault( &gSwkbd );

	
	// Otteniamo indirizzo e porta dall'utente
	
	char address_buf[ 4097 ];
	char port_buf[ 10 ];

	//if ( getValidInput( address_buf, 4097, "Inserisci l'indirizzo del server" ) == -1 )
	//	_exit( EXIT_FAILURE );
	//if ( getValidInput( port_buf, 10, "Inserisci la porta" ) == -1 )
	//	_exit( EXIT_FAILURE );
	sprintf( address_buf, "192.168.1.130" );
	sprintf( port_buf, "3000" );

	char *endptr;

	s_addr = address_buf;
	s_port = strtol( port_buf, &endptr, 10 );
	if ( *endptr != '\0' )
		_exit( 1 );


#endif


	gState.current_screen = STATE_INTRO;

	if ( s_port < 0 || s_port > 65535 )
	{
		fprintf( stderr, "client: Numero di porta non valido.\n" );
		_exit( EXIT_FAILURE );
	}


	/* Impostazione buffer I/O e grafica */

	char *fb;
	if ( updateWinSize( &gState ) )
		err( EXIT_FAILURE, "client: Impossibile ottenere le dimensioni della finestra" );
	post_limit = max_posts_per_page;
	fb = malloc( window.ws_row * window.ws_col );
	if ( fb == NULL )
		err( EXIT_FAILURE, "client: Impossibile allocare memoria per il framebuffer" );

	_fflush( stdout );
	if ( setvbuf( stdin, NULL, _IONBF, 0 ) )
		warn( "client: Impossibile impostare l'input non bufferizzato" );
	if ( setvbuf( stdout, fb, _IOFBF, window.ws_row * window.ws_col ) )
		warn( "client: Impossibile impostare l'output bufferizzato" );

#ifdef __SWITCH__
	// Impostazione del font (per disegnare le ANSI box)
	ConsoleFont cp437_font;
	cp437_font.gfx         = OLDSCHOOL_MODEL30_16;
	cp437_font.asciiOffset = 0;
	cp437_font.numChars    = 256;
	cp437_font.tileWidth   = 8;
	cp437_font.tileHeight  = 16;

	PrintConsole *console = consoleGetDefault();
	consoleSetFont( console, &cp437_font );
#endif


	/* Risoluzione del nome host (se necessaria) */

	bzero( &servaddr, sizeof( servaddr ) );

	if ( !inet_aton( s_addr, &servaddr.sin_addr ) )
	{
		printf( "Risoluzione di %s... ", s_addr );
		_fflush( stdout );

		if ( ( he = gethostbyname( s_addr ) ) == NULL )
		{
			printf( "fallita.\n" );
			_exit( EXIT_FAILURE );
		}

		servaddr.sin_addr = *( struct in_addr *)he->h_addr_list[0];
		printf( "%s\n", inet_ntoa( servaddr.sin_addr ) );
		_fflush( stdout );
	}


	/* Connessione al server della bacheca */

	if ( ( s_sock = socket( AF_INET, SOCK_STREAM, 0 ) ) < 0 )
		err( EXIT_FAILURE, "client: Errore nella creazione della socket" );

	servaddr.sin_family = AF_INET;
	servaddr.sin_port   = htons( (unsigned short int)s_port );

	printf( "Connessione a %s:%d...\n", inet_ntoa( servaddr.sin_addr ), s_port );
	_fflush( stdout );

	if ( connect( s_sock, (struct sockaddr *)&servaddr, sizeof( servaddr ) ) < 0 )
		err( EXIT_FAILURE, "client: impossibile connettersi al server" );


	/* Impostazioni socket */

	struct timeval timeout;
	timeout.tv_sec  = 10;
	timeout.tv_usec = 0;
	if ( setsockopt( s_sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof( struct timeval ) ) == -1 )
		warn( "client: Impossibile impostare il timeout sulla socket" );

#ifndef __SWITCH__
	// Ci registriamo per gestire SIGURG
	fcntl( s_sock, F_SETOWN, getpid() );
#endif

	
	/* Main loop */

#ifdef DEBUG
	printf( "Connessione stabilita.\n\n" );
	printf( "Indirizzo: %s\tPorta: %d\n", s_addr, s_port );
	_fflush( stdout );
#endif

	int ret;
       	while ( ( ret = recv( s_sock, msg_buf, 1, 0 ) ) < 1 )
	{
		if ( ret == -1 && errno == EINTR )
			continue;
		warn( "client: ricezione fallita" );
		exitProgram( EXIT_FAILURE );
	}

	if ( *msg_buf == SERV_AUTHENTICATE )
	{
		setTerminalMode( TERM_CANON );
		printf( "%s richiede l'autenticazione per poter accedere alla bacheca.\n\n", s_addr );
		_fflush( stdout );

		while (1)
		{
			int len_user, len_pass;

			if ( ( len_user = getValidInput( user, 256, "Nome utente: " ) ) < 0 )
				_exit( EXIT_FAILURE );
			setTerminalMode( TERM_CANON_NOECHO );
			if ( ( len_pass = getValidInput( pass, 256, "Password: " ) ) < 0 )
				_exit( EXIT_FAILURE );
			setTerminalMode( TERM_CANON );

			msg_buf[0] = CLI_LOGIN;
			msg_buf[1] = len_user;
			msg_buf[2] = len_pass;
			memcpy( msg_buf + 3           , user, len_user );
			memcpy( msg_buf + 3 + len_user, pass, len_pass );
			msg_size = 3 + len_user + len_pass;

			if ( SendAndGetResponse( s_sock, msg_buf, &msg_size, 0 ) < 0 )
			{
				printf( "Connessione persa.\n" );
				_fflush( stdout );
				_exit( EXIT_FAILURE );
			}

			if ( *msg_buf == SERV_WELCOME )
			{
				auth_level = msg_buf[1];
				break;
			}

			printf( "\nCredenziali errate, riprova.\n" );
			_fflush( stdout );
		}
	}
	else
	{
		if ( sockReceiveAll( s_sock, msg_buf + 1, 12 ) < 0 )
			exitProgram( EXIT_FAILURE );
		conv_u16( msg_buf + 2, TO_HOST );
		conv_u64( msg_buf + 4, TO_HOST );
		if ( sockReceiveAll( s_sock, msg_buf + 13, msg_buf[12] ) < 0 )
			exitProgram( EXIT_FAILURE );
	}

	uint16_t n_posts;
	int64_t  server_time;
	char     server_time_str_buf[20];
	char     board_title[260];
	memcpy( &n_posts, msg_buf + 2, 2 );
	memcpy( &server_time, msg_buf + 4, 8 );
	strftime( server_time_str_buf, 20, "%d/%m/%Y %H:%M:%S", localtime( ( time_t *)&server_time ) );
	sprintf( board_title, msg_buf[12] ? " (%.*s)" : "", msg_buf[12], msg_buf + 13 );

	printf( "\nBenvenuto nella bacheca elettronica di %s%s.\nPost presenti: %u\nOrario del server: %s\n", s_addr, board_title, n_posts, server_time_str_buf );
	printf( "\nInvio) Leggi i post\n    q) Esci\n\n" );
	_fflush( stdout );

	setTerminalMode( TERM_RAW );

	// Client state setup
	gState.current_screen = STATE_INTRO;
	gState.cached_posts = NULL;		// da popolare con un'array di N puntatori a Post (N verrà dedotto con updateWindowSize())
	gState.num_posts = 0;
	gState.page_offset = 0;
	gState.selected_post = 0;
	*gState.state_label = '\0';
	strncpy( gState.board_title, msg_buf + 13, msg_buf[12] + 1 );
	strncpy( gState.server_addr, s_addr, 100 );
	gState.user = user;
	gState.pass = pass;
	gState.auth_level = auth_level;

	//gState.quit_enabled = true;

	gState.cached_posts = malloc( sizeof( char* ) * post_limit );
	for ( int i = 0; i < post_limit; i++ )
		gState.cached_posts[ i ] = NULL;

#ifdef __SWITCH__
	while( appletMainLoop() )
#else
	while (1)
#endif
	{
		if ( gNewDataAvailable )
		{
oob:
			// handle OOB message...
			char buf;
			int oob_ret;

			oob_ret = recv( s_sock, &buf, 1, MSG_OOB );

			if ( oob_ret <= 0 )
			{
				if ( oob_ret == -1 && errno == EINTR )
					goto oob;

				if ( oob_ret == -1 )
					err( EXIT_FAILURE, "ATTENZIONE: recv fallita nella gestione della notifica OOB!" );

				else if ( oob_ret == 0 )
				{
					gState.current_screen = STATE_ERROR;
					drawError( "Connessione col server persa." );
					while( getchar() != '\n' );
					exitProgram( EXIT_FAILURE );
				}
			}

			//if ( buf != '!' )
			//	continue;
				
			unsigned int old_posts = gState.num_posts;

			if ( loadPosts( msg_buf, &msg_size, gState.loaded_page ) == -1 )
			{
				gState.current_screen = STATE_ERROR;
				drawError( "Connessione col server persa." );
				while ( getchar() != '\n' );
				exitProgram( EXIT_FAILURE );
			}

			if ( gState.num_posts > old_posts )
				sprintf( gState.state_label, "Nuovi post disponibili!" );
			drawTui( &gState );

			gNewDataAvailable = 0;
		}

		if ( gResized )
		{
resize:
			gState.post_offset = 0;		// per non incasinare il testo del post
			updateWinSize( &gState );
			drawTui( &gState );

			gResized = 0;
		}

		int action = 0;

#ifndef __SWITCH__
		//stdio mi ignora i segnali, sono costretto a fare così
		fd_set input_set;
		FD_ZERO( &input_set );
		FD_SET( STDIN_FILENO, &input_set );

		if ( select( STDIN_FILENO+1, &input_set, NULL, NULL, NULL ) == -1 )
		{
			if ( errno == EINTR )
			{
				// Potremmo essere stati svegliati da SIGURG
				if ( gNewDataAvailable )
					goto oob;
				
				// La finestra potrebbe essere stata ridimensionata
				if ( gResized )
					goto resize;
				
				continue;
			}

			warn( "Errore nella select()" );
		}
		
		// La select() ha ritornato, getchar() è garantita non bloccante ora perché abbiamo i dati in input
		action = getchar();
#else
		//su Switch non posso né usare SIGURG (i segnali non esistono) né fare la getchar() bloccante
		//perché appletMainLoop() deve essere chiamato periodicamente per non bloccare la console
		//fd_set oob_set;
		//FD_ZERO( &oob_set );
		//FD_SET( s_sock, &oob_set );

		//struct timeval tv;
		//tv.tv_sec  = 0;
		//tv.tv_usec = 0;

		//// La Switch non rileva gli input con select()!
		//if ( select( s_sock+1, &oob_set, NULL, NULL, &tv ) == -1 )
		//	_exit( EXIT_FAILURE );


		//// gestiamo qui quello che su Linux gestivamo con i segnali

		//if ( FD_ISSET( s_sock, &oob_set ) )
		//	goto oob;

		// L'approccio classico con select() sembra non funzionare sia con il set in readfds che in exceptfds, facciamo così
		char buf;
		if ( recv( s_sock, &buf, 1, MSG_OOB | MSG_PEEK | MSG_DONTWAIT ) == 1 )	// o ritorna 0 (dati OOB presenti/connessione chiusa)
											// o -1 (nessun dato/altro errore, assumiamo EWOULDBLOCK) 
			goto oob;


		// ora possiamo gestire l'input della Switch

		padUpdate( &gPad );
		padRepeaterUpdate( &gPadRepeater, padGetButtons( &gPad ) & ( HidNpadButton_AnyUp | HidNpadButton_AnyDown ) );

		u64 actions = padRepeaterGetButtons( &gPadRepeater ) | padGetButtonsDown( &gPad );
		if ( actions & HidNpadButton_AnyUp )
			action = 'k';
		if ( actions & HidNpadButton_AnyDown )
			action = 'j';
		if ( actions & HidNpadButton_L )
			action = 'h';
		if ( actions & HidNpadButton_R )
			action = 'l';
		if ( actions & HidNpadButton_A )
			action = '\n';
		if ( actions & HidNpadButton_X )
			action = 'w';
		if ( actions & HidNpadButton_Y )
			action = 'd';
		if ( actions & HidNpadButton_B )
			action = 'b';
		if ( actions & HidNpadButton_Plus )
			action = 'q';
#endif

		if ( action == 0 )
		{
#ifdef __SWITCH__
			consoleUpdate( NULL );
#endif
			continue;
		}

		if ( action == EOF )
			exitProgram( EXIT_FAILURE );

		switch ( action )
		{
			case 'q':
			case 'Q':
				if ( gState.current_screen != STATE_WRITING )
					exitProgram( EXIT_SUCCESS );
				goto inserisci;
				break;

			case '\n':
				if ( gState.current_screen == STATE_WRITING )
				{
					action = '\v';
					goto inserisci;
				}
				if ( gState.current_screen == STATE_INTRO )
				{
					printf( "Caricamento post...\n\033[?25l" );
					// carica post...
					if ( loadPosts( msg_buf, &msg_size, 1 ) == -1 )
					{
						drawError( "Connessione persa.\nImpossibile ottenere i post." );
						gState.current_screen = STATE_ERROR;
						break;
					}
					if ( gState.num_posts )
						gState.most_recent_post_shown = gState.cached_posts[ 0 ]->timestamp;
					gState.current_screen = STATE_LISTING;
					drawTui( &gState );
				}
				else if ( gState.current_screen & UI_READPOST && gState.selected_post < gState.loaded_posts )
				{
					Post *curr_post = gState.cached_posts[ gState.selected_post ];
					if ( gState.opened_post ) free ( gState.opened_post );
					uint16_t len_testo;
					memcpy( &len_testo, &curr_post->len_testo, 2 );
					size_t post_size = POST_HEADER_SIZE + curr_post->len_mittente + curr_post->len_oggetto + len_testo;
					gState.opened_post = malloc( post_size + 1 );
					if ( !gState.opened_post )
					{
						sprintf( gState.state_label, "Memoria piena!" );
						drawTui( &gState );
						break;
					}
					memcpy( gState.opened_post, curr_post, post_size );
					if ( gState.opened_post->timestamp > gState.most_recent_post_shown ) gState.most_recent_post_shown = gState.cached_posts[0]->timestamp;
					gState.current_screen = STATE_SINGLEPOST;
					gState.state_label[0] = '\0';
					gState.post_offset = 0;
					drawTui( &gState );
				}
				else if ( gState.current_screen == STATE_ERROR )
				{
					exitProgram( EXIT_FAILURE );
				}
				break;


			case 'k':
			case 'K':
				if ( gState.current_screen == STATE_WRITING )
				{
					goto inserisci;
				}
				else if ( gState.current_screen & UI_TEXTNAV && gState.post_offset > 0 )
				{
					gState.post_offset--;
					drawTui( &gState );
				}
				else if ( gState.current_screen & UI_LISTNAV && gState.selected_post > 0 )
				{
					if ( --gState.selected_post < gState.page_offset )
						gState.page_offset = gState.selected_post;
					drawTui( &gState );
				}
				break;

			case 'j':
			case 'J':
				if ( gState.current_screen == STATE_WRITING )
				{
					goto inserisci;
				}
				else if ( gState.current_screen & UI_TEXTNAV && gState.more_lines )
				{
					gState.post_offset++;
					drawTui( &gState );
				}
				else if ( gState.current_screen & UI_LISTNAV && gState.selected_post + 1 < gState.loaded_posts )
				{
					if ( ++gState.selected_post >= max_posts_per_page )
						gState.page_offset = gState.selected_post - max_posts_per_page + 1;
					drawTui( &gState );
				}
				break;


			case 'h':
			case 'H':
				if ( gState.current_screen == STATE_WRITING )
				{
					// inserisci lettera nel buffer...
					goto inserisci;
				}
				else if ( gState.current_screen & UI_PAGENAV && gState.loaded_page > 1 )
				{
					sprintf( gState.state_label, "Caricamento dei post..." );
					drawTui( &gState );

					if ( gState.loaded_posts &&
					     gState.cached_posts[0]->timestamp > gState.most_recent_post_shown )
						gState.most_recent_post_shown = gState.cached_posts[0]->timestamp;

					if ( loadPosts( msg_buf, &msg_size, --gState.loaded_page ) == -1 )
					{
						drawError( "Connessione persa.\nImpossibile aggiornare i post." );
						gState.current_screen = STATE_ERROR;
						break;
					}
					gState.selected_post = 0;
					gState.page_offset = 0;
					gState.state_label[0] = '\0';
					drawTui( &gState );
				}
				break;

			case 'l':
			case 'L':
				if ( gState.current_screen == STATE_WRITING )
				{
					// inserisci lettera nel buffer...
					goto inserisci;
				}
				else if ( gState.current_screen & UI_PAGENAV && gState.num_posts != 0 &&
					  gState.loaded_page < ( gState.num_posts - 1 ) / post_limit + 1 )
				{
					sprintf( gState.state_label, "Caricamento dei post..." );
					drawTui( &gState );

					if ( gState.loaded_posts &&
					     gState.cached_posts[0]->timestamp > gState.most_recent_post_shown )
						gState.most_recent_post_shown = gState.cached_posts[0]->timestamp;

					if ( loadPosts( msg_buf, &msg_size, ++gState.loaded_page ) == -1 )
					{
						drawError( "Connessione persa.\nImpossibile aggiornare i post." );
						gState.current_screen = STATE_ERROR;
						break;
					}
					gState.selected_post = 0;
					gState.page_offset = 0;
					gState.state_label[0] = '\0';
					drawTui( &gState );
				}
				break;


			case 'b':
			case 'B':
				if ( gState.current_screen == STATE_WRITING )
				{
					goto inserisci;
				}
				else if ( gState.current_screen & UI_BACK && gState.current_screen != STATE_WRITING )
				{
					if ( gState.opened_post ) free( gState.opened_post );
					gState.opened_post = NULL;
					gState.current_screen = STATE_LISTING;
					gState.state_label[0] = '\0';
					drawTui( &gState );
				}
				break;

			
			case 'w':
			case 'W':
				if ( gState.current_screen == STATE_WRITING )
				{
					goto inserisci;
				}
				//else if ( gState.current_screen == UI_WRITEPOST && auth_level > -1 )
				else if ( gState.current_screen & UI_WRITEPOST )
				{
					gState.current_screen = STATE_WRITING;
					gState.state_label[0] = '\0';
					gState.buf_testo[0] = '\0';
					gState.len_testo    = 0;
					gState.buf_oggetto[0] = '\0';
					gState.len_oggetto    = 0;
					gState.current_draft_field = FIELD_OGGETTO;
					drawTui( &gState );
				}
				break;

			case '\x02':
				if ( gState.current_screen == STATE_WRITING )
				{
					gState.current_screen = STATE_LISTING;
					gState.state_label[0] = '\0';
					printf( "\033[?25l" );
					drawTui( &gState );
				}
				break;

			case '\x09':
				if ( gState.current_screen == STATE_WRITING )
				{
					gState.current_draft_field = gState.current_draft_field == FIELD_OGGETTO ? FIELD_TESTO : FIELD_OGGETTO;
					drawTui( &gState );
				}
				break;

			case '\x7f':
				if ( gState.current_screen == STATE_WRITING )
				{
					if ( gState.current_draft_field == FIELD_OGGETTO && gState.len_oggetto > 0 )
						gState.buf_oggetto[ --gState.len_oggetto ] = '\0';
					else if ( gState.current_draft_field == FIELD_TESTO && gState.len_testo > 0 )
						gState.buf_testo[ --gState.len_testo ] = '\0';
					else break;
					drawTui( &gState );
				}
				break;

			case '\x18':
				if ( gState.current_screen == STATE_WRITING )
				{
					if ( gState.len_testo == 0 )
					{
						sprintf( gState.state_label, "Messaggio vuoto!" );
						drawTui( &gState );
						gState.state_label[0] = '\0';
						break;
					}

					if ( gState.auth_level < 0 )
					{
						int reauth_ret = reauth( msg_buf, &msg_size );
						if ( !reauth_ret )
						{
							sprintf( gState.state_label, "Credenziali errate!" );
							drawTui( &gState );
							gState.state_label[0] = '\0';
							break;
						}
						else if ( reauth_ret == -1 )
						{
							sprintf( gState.state_label, "Errore di comunicazione" );
							drawTui( &gState );
							gState.state_label[0] = '\0';
							break;
						}
						else if ( reauth_ret == -2 )
							goto writepost_unauthorized;
					}
						

					sprintf( gState.state_label, "Invio del messaggio..." );
					drawTui( &gState );

					gState.state_label[0] = '\0';
					msg_buf[0] = CLI_POST;
					msg_buf[1] = strlen( user );
					msg_buf[2] = strlen( pass );
					strcpy( msg_buf + 3, user );
					strcpy( msg_buf + 3 + strlen( user ), pass );

					Post *newpost = malloc( POST_HEADER_SIZE + strlen( user ) + gState.len_oggetto + gState.len_testo + 1 );
					if ( !newpost )
					{
						sprintf( gState.state_label, "Errore di memoria" );
						drawTui( &gState );
						gState.state_label[0] = '\0';
						break;
					}

					//uint32_t id = 0x11223344;
					//uint64_t timestamp = 0xFFFFFFFFFFFFFFFF;
					newpost->len_mittente = strlen( user );
					newpost->len_oggetto = gState.len_oggetto;
					uint16_t len_testo = ( uint16_t )gState.len_testo;
					//memcpy( &newpost->id, &id, 4 );
					memcpy( &newpost->len_testo, &len_testo, 2 );
					//memcpy( &newpost->timestamp, &timestamp, 8 );

					strcpy( newpost->data, user );
					strcat( newpost->data, gState.buf_oggetto );
					strcat( newpost->data, gState.buf_testo );

					size_t post_size = POST_HEADER_SIZE + newpost->len_mittente + newpost->len_oggetto + len_testo;
					memcpy( msg_buf + 3 + strlen( user ) + strlen( pass ), newpost, post_size );
					msg_size = 3 + strlen( user ) + strlen( pass ) + post_size;
					ret = SendAndGetResponse( s_sock, msg_buf, &msg_size, SERV_OK );
					free( newpost );

					if ( ret )
					{
						sprintf( gState.state_label, "Post pubblicato!" );
						drawTui( &gState );
						gState.current_screen = STATE_LISTING;
						printf( "\033[?25l" );
						if ( loadPosts( msg_buf, &msg_size, gState.loaded_page ) == -1 )
						{
							drawError( "Connessione persa.\nImpossibile aggiornare i post." );
							gState.current_screen = STATE_ERROR;
							break;
						}

					}
					else if ( ret == -1 )
					{
						drawError( "Connessione persa.\nImpossibile inviare il post!" );
						gState.current_screen = STATE_ERROR;
						break;
					}
					else
						switch ( ( unsigned char )msg_buf[1] )
						{
							case 0x0:
							      writepost_unauthorized:
								sprintf( gState.state_label, "Non autorizzato" );
								break;

							case 0x1:
								sprintf( gState.state_label, "Errore del server, riprova" );
								break;

							case 0xFF:
								sprintf( gState.state_label, "Il server è pieno!" );
								break;

							default:
								sprintf( gState.state_label, "Errore sconosciuto" );
						}

					drawTui( &gState );	// lo faccio due volte perché è più bello :>
				}
				break;


			case 'd':
			case 'D':
				if ( gState.current_screen == STATE_WRITING )
				{
					goto inserisci;
				}
				else if ( gState.current_screen & UI_DELPOST && 1 && gState.loaded_posts > 0 &&
					  ( gState.auth_level != 0 ||
				     	    ( ( gState.current_screen != STATE_SINGLEPOST &&
						!strncmp( gState.cached_posts[ gState.selected_post ]->data,
						          user,
						          gState.cached_posts[ gState.selected_post ]->len_mittente ) ) ||
					      ( gState.current_screen == STATE_SINGLEPOST &&
					        !strncmp( gState.opened_post->data,
						          user,
						  	  gState.opened_post->len_mittente ) ) ) ) )
				{
					if ( gState.auth_level < 0 && reauth( msg_buf, &msg_size ) <= 0 )
					{
						sprintf( gState.state_label, "Credenziali errate!" );
						drawTui( &gState );
						break;
					}
					msg_buf[0] = CLI_DELPOST;
					msg_buf[1] = ( unsigned char )strlen( user );
					msg_buf[2] = ( unsigned char )strlen( pass );
					strcpy( msg_buf + 3, user );
					strcpy( msg_buf + 3 + msg_buf[1], pass );
					memcpy( msg_buf + 3 + msg_buf[1] + msg_buf[2], gState.current_screen == STATE_SINGLEPOST ? &gState.opened_post->id : &gState.cached_posts[ gState.selected_post ]->id, 4 );
					msg_size = 3 + msg_buf[1] + msg_buf[2] + 4;
					ret = SendAndGetResponse( s_sock, msg_buf, &msg_size, SERV_OK );
					if ( ret )
					{
						sprintf( gState.state_label, "Post cancellato!" );
						gState.current_screen = STATE_LISTING;
						if ( loadPosts( msg_buf, &msg_size, gState.loaded_page ) == -1 )
						{
							drawError( "Connessione persa dopo l'eliminazione del post.\nIl post è stato cancellato." );
							gState.current_screen = STATE_ERROR;
							break;
						}
						drawTui( &gState );
						break;
					}
					else if ( ret == -1 )
					{
						drawError( "Connessione persa!\nImpossibile cancellare il post." );
						gState.current_screen = STATE_ERROR;
						break;
					}
					else
						switch( ( unsigned char )msg_buf[1] )
						{
							case 0x0:
							case 0xFF:
								sprintf( gState.state_label, "Post non trovato" );
								break;
							
							case 0x1:
								sprintf( gState.state_label, "Non autorizzato" );
								break;

							default:
								sprintf( gState.state_label, "Errore sconosciuto" );
						}

					drawTui( &gState );
					gState.state_label[0] = '\0';
				}
				break;

			
			default:
				if ( gState.current_screen == STATE_WRITING && action >= ' ' )
					goto inserisci;
				break;

			inserisci:
				if ( gState.current_draft_field == FIELD_OGGETTO && gState.len_oggetto < OGGETTO_MAXLEN && action != '\v' )
				{
					gState.buf_oggetto[ gState.len_oggetto++ ] = (char)action;
					gState.buf_oggetto[ gState.len_oggetto   ] = '\0';
				}
				else if ( gState.current_draft_field == FIELD_TESTO && gState.len_testo < TESTO_MAXLEN )
				{
					gState.buf_testo[ gState.len_testo++ ] = (char)action;
					gState.buf_testo[ gState.len_testo   ] = '\0';
				}
				else break;
				drawTui( &gState );
				break;
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

	const char* TEST_MITT = "Sergio";
	const char* TEST_OGG  = "Prova";
	const char* TEST_TEXT = "Questo e' un messaggio di prova!!!!!!!!!!!!!!!!!!!!!";
	

	
	return 0;

	//for ( int i = 0; i < 10; i++ )
	//	if ( gLoadedPosts[i] )
	//	{
	//		free( gLoadedPosts[i] );
	//		gLoadedPosts[i] = NULL;
	//	}

	return 0;
}
