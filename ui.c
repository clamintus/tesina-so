#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <err.h>
#include <string.h>
#include <time.h>

#include "types.h"

#define CUP(x,y) "\033[y;xH"

struct termios term;
struct winsize window;

typedef struct client_state {
	Post *cached_posts;
	int  num_posts;
	int  selected_post;
	
	// Commands
	int pagenav_enabled;
	int listnav_enabled;
} ClientState;

int updateWinSize()
{
	if ( ioctl( 0, TIOCGWINSZ, &window ) == -1 )
	{
		warn( "updateWinSize: ioctl error" );
		return 1;
	}

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

int drawTui_listView( ClientState *state )
{
	if ( updateWinSize() )
		return 1;
	//test
	//window.ws_col = 50;
	printf( "\033[2J" );	// Erase-in Display
	draw_box();
	//draw_hline( 3 );
	// forse in drawTui_header()?
	printf( "\033[2;4HBacheca Elettronica di 127.0.0.1   |   Lista post   |   Pagina 1 di 1" );
	printf( "\033[5;3H" );
	// drawTui_commands()...
	
	int max_posts_per_page = window.ws_row - 10;
	if ( state->num_posts > max_posts_per_page ) state->pagenav_enabled = true;

	for ( int i = 0; i < state->num_posts && i < max_posts_per_page; i++ )
	{
		Post *post = &state->cached_posts[ i ];
		char *ora_post;
		char *mittente = malloc( post->len_mittente + 1 );
		char *oggetto = malloc( post->len_oggetto + 1 );
		int64_t timestamp;
		int selected = 1;

		memcpy( &timestamp, &post->timestamp, 8 );
		strncpy( mittente, post->data, post->len_mittente );
		strncpy( oggetto, post->data + post->len_mittente, post->len_oggetto );
		ora_post = stringifyTimestamp( (time_t)timestamp );
		
		int oggetto_trunc_pos = window.ws_col - 6 - 5 - 4 - post->len_mittente;
		if ( strlen( oggetto ) > oggetto_trunc_pos ) oggetto[ oggetto_trunc_pos ] = '\0';	// tronca oggetto se più lungo di schermo

		printf( "%s %s %s %s\n\033[3GP", selected ? "*" : " ", ora_post, mittente, oggetto );

		free( mittente );
		free( oggetto );
		free( ora_post );
	}
}

int main()
{
	const char *testo = "Solito messaggio di prova";
	const char *mittente = "Sergio";
	const char *oggetto = "Provaaaaaaaaaaaaaaaijeejfeifjeijfejwfhouefhowuehfoweuhfewoufhweoufhweoufhweofweouhfewuhofwehofwehfowehfoeuwfhewoufhwoehfweuofhweuofhweouhfweouhfhweoufhweoufwhefoweufwuoehfwouehf+++";

	Post *post = malloc( sizeof( Post ) + strlen( mittente ) + strlen( oggetto ) + strlen( testo ) + 1 );
	post->id = 1;
	post->len_mittente = strlen( mittente );
	post->len_oggetto = strlen( oggetto );
	post->len_testo = strlen( testo );
	post->timestamp = 1765033000;
	sprintf( post->data, "%s%s%s", mittente, oggetto, testo );
	ClientState dummy_state = {post, 1, 1};

	if ( drawTui_listView( &dummy_state ) )
		return 1;

	free( post );
	return 0;
}
