#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <err.h>
#include <string.h>
#include <time.h>
#include <ctype.h>

#include "types.h"
#include "ui.h"

#define CUP(x,y) "\033[y;xH"

//struct termios term;
struct winsize window;
int max_posts_per_page;

int updateWinSize()
{
	if ( ioctl( 0, TIOCGWINSZ, &window ) == -1 )
	{
		warn( "updateWinSize: ioctl error" );
		return 1;
	}

	max_posts_per_page = window.ws_row - 12;
	return 0;
}

void draw_box()
{
	for ( unsigned short y = 1; y < window.ws_row; y++ )
		printf( "\033[%d;1H┃\033[%d;%dH┃", y, y, window.ws_col );
	for ( unsigned short x = 1; x < window.ws_col; x++ )
		printf( "\033[1;%dH━\033[%d;%dH━", x, window.ws_row, x );
	printf( "\033[1;1H┏"
		"\033[1;%dH┓"
		"\033[%d;1H┗"
		"\033[%d;%dH┛", window.ws_col, window.ws_row, window.ws_row, window.ws_col );
}

void draw_hline( int row )
{
	if ( row == 1 || row == window.ws_row ) return;

	for ( unsigned short x = 1; x < window.ws_col; x++ )
		printf( "\033[%d;%dH─", row, x );
	printf( "\033[%d;1H┠\033[%d;%dH┨", row, row, window.ws_col );
}

char *stringifyTimestamp( time_t timestamp )
{
	char *result = malloc( 10 );
	struct tm post_tm;
	struct tm now_tm;
	struct tm *tmp;
	time_t now = time( NULL );

	/* needs to be replaced with actual logic */
	//sprintf( result, "01/01" );
	tmp = gmtime( &now );
	//memcpy( &now_tm, tmp, sizeof( struct tm ) );		// copy current time in now_tm
	now_tm = *tmp;
	tmp = gmtime( &timestamp );
	post_tm = *tmp;

	if ( post_tm.tm_year == now_tm.tm_year && post_tm.tm_mon == now_tm.tm_mon && post_tm.tm_mday == now_tm.tm_mday )
		strftime( result, 10, "%R", &post_tm );
	else
		strftime( result, 10, "%m/%d", &post_tm );

	return result;
}

void printWrapped( const char* str, size_t size, unsigned short x0, unsigned short y0, unsigned short x1, unsigned short y1 )
{
	unsigned short x_len = x1 - x0 + 1;
	unsigned short y_len = y1 - y0 + 1;
	
	unsigned short x = x0;
	unsigned short y = y0;
	unsigned int   l = 0;

	char* curr = str;
	char* lines[1024];

	if ( x_len <= 0 || y_len <= 0 )
		return;

	lines[ l++ ] = curr;
	lines[ 1 ]   = curr + strlen( str );

	//printf( "\033[%d;%dH", y, x );

	char* curline = curr;
	char* curword = curr;

	while ( *curr )
	{
	      continue_outer:
		//int has_blank = false;
		
		// non dovremmo arrivare a questo punto,
		// ma se dovessimo evitiamo di corrompere la stack
		if ( l > 1024 )
			break;

	      nextword:
		while ( !isspace( *curr ) )
		{
			if ( *curr == '\0' || curr - str == size )
			{
				lines[ l ] = curr;
				goto endloop;
			}

			if ( curr - curline >= x_len )
			{
				if ( curline == curword )
				{
					// La riga corrente non ha spazi (è una sola parola). Andiamo a capo
					//printf( "%.*s\033[%d;%dH", curr - curline, curline, ++y, x0 );
					//if ( y == y1 )
					//	return 1;			// segnala che il campo è pieno

					// Simulazione (per contare le righe necessarie)
					lines[ l++ ] = curr;
					curline = curr;
					curword = curr;
					goto continue_outer;
					
				}

				// La riga corrente ha spazi: wrappiamola
				lines[ l++ ] = curword;
				curr = curword;
				curline = curr;
				goto continue_outer;
			}

			curr++;
			continue;
		}

		if ( curr - str == size )
		{
			lines[ l ] = curr;
			goto endloop;
		}

		if ( *curr == '\n' )
		{
			lines[ l++ ] = ++curr;
			curline = curr;
			curword = curr;
			continue;
		}

		//has_blank = true;
		curword = ++curr;
		if ( curr - curline >= x_len )
		{
			lines[ l++ ] = curr;
			curline = curr;
			//curword = curr;
			continue;
		}

		//goto nextword;
	}

endloop:
	// Ora stampiamo effettivamente le righe
	unsigned int start_index = 0;
	if ( l > y_len )
		start_index = l - y_len;

	for ( unsigned int i = 0; i < y_len && start_index + i < l; i++ )
	{
		int line_len = lines[ start_index + i + 1 ] - lines[ start_index + i ];
		printf( "\033[%d;%dH%.*s", y0 + i,
				           x0,
					   line_len, lines[ start_index + i ] );
					   //lines[ start_index + i ][ line_len - 1 ] == '\n' ? "" : "\n" );
	}
	//printf( "\033[%d;%dH%s", l > y_len ? y1 : y0 + l - 1, x0, lines[ l - 1 ] );

	//printf( "\033[%d;%dH%d lines", y1 + 1, x0 + 2, l );

	fflush( stdout );
}


#define ANSIREV "\033[7m\033[1m"
#define ANSIRST "\033[0m"
int draw_header( ClientState *state )
{
	char left_text[257];
	char right_text[100];
	const char* listing_str    = "Lista post   |   Pagina 1 di 1";
	const char* writing_str    = "Scrivi post";
	const char* singlepost_str = "Leggi post   |   Oggetto";

	switch ( state->current_screen )
	{
		case STATE_LISTING:
			if ( *state->board_title )
				sprintf( left_text, "%s   |   %s", state->board_title, listing_str );		// da troncare
			else
				sprintf( left_text, "Bacheca Elettronica di %s   |   %s", state->server_addr, listing_str );
			break;

		case STATE_WRITING:
			sprintf( left_text, "%s", writing_str );
			break;

		case STATE_SINGLEPOST:
			sprintf( left_text, "%s", singlepost_str );
			break;
	}

	sprintf( right_text, "Loggato come:  %s%s%s", state->auth_level > 0 ? "\033[1m" : "", state->user, state->auth_level > 0 ? ANSIRST : "" );

	draw_hline( 3 );
	printf( "\033[2;4H%s\033[2;%dH%s", left_text, window.ws_col - 3 - strlen( right_text ) + ( state->auth_level ? 8 : 0 ), right_text );
	//printf( "\033[2;4H%s", left_text );
}

int draw_footer( ClientState *state )
{
	draw_hline( window.ws_row - 6 );
	printf( "\033[%d;5H", window.ws_row - 4 );
	
	if ( state->listnav_enabled )
		printf( ANSIREV " K " ANSIRST "  " ANSIREV " J " ANSIRST "  Naviga lista\033[%d;5H", window.ws_row - 2 );
	if ( state->pagenav_enabled )
		printf( ANSIREV " ← " ANSIRST "  " ANSIREV " → " ANSIRST "  Cambia pagina\033[%d;36H", window.ws_row - 4 );
	if ( state->readpost_enabled )
		printf( "\033[%d;36H" ANSIREV " ENTER " ANSIRST "  Leggi post\033[%d;36H", window.ws_row - 4, window.ws_row - 2 );
	if ( state->goback_enabled )
		printf( ANSIREV " B " ANSIRST "  Torna indietro" );
	if ( state->quit_enabled )
		printf( "\033[%d;63H" ANSIREV " Q " ANSIRST "  Disconnetti ed esci", window.ws_row - 4 );

	printf( "\033[%d;1H\033[2K", window.ws_row - 1 );
	draw_box();
	
	if ( state->state_label[0] != '\0' )
		printf( "\033[%d;%dH" ANSIREV " %s " ANSIRST, window.ws_row - 1, window.ws_col - strlen( state->state_label ) - 3, state->state_label );
}

int drawTui_listView( ClientState *state )
{
	if ( state->num_posts > max_posts_per_page ) state->pagenav_enabled = true;
	//state->pagenav_enabled = true;
	state->listnav_enabled = true;
	state->readpost_enabled = state->quit_enabled = true;
	state->goback_enabled = false;
	//strcpy( state->state_label, "We Are Charlie Kirk" );
	//strcpy( state->state_label, "Prova" );
	//*state->state_label = '\0';
	draw_header( state );
	draw_footer( state );

	
	for ( int i = 0; i < state->loaded_posts; i++ )
	{
		Post *post = state->cached_posts[ i ];
		char *ora_post;
		//char *mittente = malloc( post->len_mittente + 1 );
		//char *oggetto = malloc( post->len_oggetto + 1 );
		int64_t timestamp;
		int selected = state->selected_post == i;

		memcpy( &timestamp, &post->timestamp, 8 );
		//strncpy( mittente, post->data, post->len_mittente );
		//strncpy( oggetto, post->data + post->len_mittente, post->len_oggetto );
		ora_post = stringifyTimestamp( (time_t)timestamp );
		
		int oggetto_trunc_pos = window.ws_col - 6 - 5 - 4 - post->len_mittente;
		//if ( strlen( oggetto ) > oggetto_trunc_pos ) oggetto[ oggetto_trunc_pos ] = '\0';	// tronca oggetto se più lungo di schermo
		if ( post->len_oggetto < oggetto_trunc_pos ) oggetto_trunc_pos = post->len_oggetto;

		printf( "\033[%d;3H", 5 + i );
		printf( "%s %s %.*s %.*s\n\033[3GP", selected ? "*" : " ", ora_post, 
									   post->len_mittente, post->data,
									   oggetto_trunc_pos,  post->data + post->len_mittente );

		//free( mittente );
		//free( oggetto );
		free( ora_post );
	}
}

int drawTui_readPost( ClientState *state )
{
	state->pagenav_enabled  = false;
	state->listnav_enabled  = false;
	state->readpost_enabled = false;
	state->goback_enabled   = true;

	draw_header( state );
	draw_footer( state );

	Post *curr_post = state->cached_posts[ state->selected_post ];

	//int l = 0;
	//unsigned short padding_x = window.ws_col / 10;
	unsigned short padding_x = 1;
	unsigned short padding_y = 1;

	printf( "\033[5;%1$dHAutore: %3$.*2$s\033[6;%1$dHOggetto: %5$.*4$s", 2 + padding_x,
									     curr_post->len_mittente, curr_post->data,
									     curr_post->len_oggetto,  curr_post->data + curr_post->len_mittente );
	// TODO: printa anche la data!

	//printf( "\033[8;3H%s", curr_post->data + curr_post->len_mittente + curr_post->len_oggetto );
	printWrapped( curr_post->data + curr_post->len_mittente + curr_post->len_oggetto,
		      curr_post->len_testo,
	                          2 + padding_x,
		                  9 + padding_y,
		      window.ws_col - padding_x - 1,
		      window.ws_row - padding_y - 1 );
}
	
int drawTui( ClientState *state )
{
	if ( updateWinSize() )
		return 1;
	//test
	//window.ws_col = 50;
	printf( "\033[2J" );	// Erase-in Display
	draw_box();

	if ( state->current_screen == STATE_LISTING )
	{
		drawTui_listView( state );
	}
	else if ( state->current_screen == STATE_SINGLEPOST )
	{
		drawTui_readPost( state );
	}
}


//int test()
//{
//	const char *testo = "Solito messaggio di prova";
//	const char *mittente = "Sergio";
//	const char *oggetto = "Provaaaaaaaaaaaaaaaijeejfeifjeijfejwfhouefhowuehfoweuhfewoufhweoufhweoufhweofweouhfewuhofwehofwehfowehfoeuwfhewoufhwoehfweuofhweuofhweouhfweouhfhweoufhweoufwhefoweufwuoehfwouehf+++";
//
//	Post **posts = malloc( sizeof( char* ) * 2 );
//	posts[0] = malloc( sizeof( Post ) + strlen( mittente ) + strlen( oggetto ) + strlen( testo ) + 1 );
//	posts[1] = malloc( sizeof( Post ) + strlen( mittente ) + strlen( oggetto ) + strlen( testo ) + 1 );
//	posts[0]->id = 1;
//	posts[0]->len_mittente = strlen( mittente );
//	posts[0]->len_oggetto = strlen( oggetto );
//	posts[0]->len_testo = strlen( testo );
//	posts[0]->timestamp = 1765033000;
//	sprintf( posts[0]->data, "%s%s%s", mittente, oggetto, testo );
//	memcpy( posts[1], posts[0], sizeof( Post ) + strlen( mittente ) + strlen( oggetto ) + strlen( testo ) + 1 );
//	memcpy( posts[1]->data, "admin", 5 );
//	posts[1]->timestamp = posts[0]->timestamp - 100000;
//	posts[1]->id = 2;
//	ClientState dummy_state = {posts, 2, 0};
//
//	if ( drawTui_listView( &dummy_state ) )
//		return 1;
//
//	free( posts[1] );
//	free( posts[0] );
//	free( posts );
//	return 0;
//}
