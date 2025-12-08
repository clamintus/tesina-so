#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <err.h>
#include <string.h>
#include <time.h>

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

	int l = 0;
	printf( "\033[5;3HAutore: %.*s\033[6;3HOggetto: %.*s", curr_post->len_mittente, curr_post->data,
	     						       curr_post->len_oggetto,  curr_post->data + curr_post->len_mittente );

	// logica di stampa wrappata...
	printf( "\033[8;3H%s", curr_post->data + curr_post->len_mittente + curr_post->len_oggetto );
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
