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
unsigned int max_post_lines;

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
	tmp = localtime( &now );
	//memcpy( &now_tm, tmp, sizeof( struct tm ) );		// copy current time in now_tm
	now_tm = *tmp;
	tmp = localtime( &timestamp );
	post_tm = *tmp;

	if ( post_tm.tm_year == now_tm.tm_year && post_tm.tm_mon == now_tm.tm_mon && post_tm.tm_mday == now_tm.tm_mday )
		strftime( result, 10, "%R", &post_tm );
	else
		strftime( result, 10, "%m/%d", &post_tm );

	return result;
}

#define ANSIREV "\033[7m\033[1m"
#define ANSIRST "\033[0m"
unsigned int printWrapped( const char* str, size_t size, unsigned short x0, unsigned short y0, unsigned short x1, unsigned short y1, unsigned int skip )
{
	unsigned short x_len = x1 - x0 + 1;
	unsigned short y_len = y1 - y0 + 1;
	
	unsigned short x = x0;
	unsigned short y = y0;
	unsigned int   l = 0;

	char* curr = str;
	char* lines[1024];

	if ( x_len <= 0 || y_len <= 0 )
		return ( unsigned int )-1;

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
	if ( skip != ( unsigned int )-1 )
		start_index = skip;

	// Also draw the scroll indicators
	if ( skip != 0 && skip != ( unsigned int )-1 )
		printf( "\033[%d;%dH" ANSIREV "[↑]" ANSIRST, y0 - 1, window.ws_col / 2 - 1 );
	if ( l > y_len && l != y_len + skip )
		printf( "\033[%d;%dH" ANSIREV "[↓]" ANSIRST, y1 + 1, window.ws_col / 2 - 1 );

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

	return l;
}


int draw_header( ClientState *state )
{
	char left_text[257];
	char right_text[100];
	const char* listing_str    = "Lista post";
	const char* writing_str    = "Nuovo post";
	const char* singlepost_str = "Leggi post";

	switch ( state->current_screen )
	{
		case STATE_LISTING:
			if ( *state->board_title )
				sprintf( left_text, "%s   |   %s   |   Pagina %u di %u", state->board_title, listing_str,
				      							 state->loaded_page,
											 state->num_posts / max_posts_per_page + 1 );  // da troncare
			else
				sprintf( left_text, "Bacheca Elettronica di %s   |   %s", state->server_addr, listing_str );
			break;

		case STATE_WRITING:
			sprintf( left_text, "%s   |   %.*s", writing_str, window.ws_col - 6 - strlen( writing_str ) - 7,
							     state->buf_oggetto[0] == '\0' ? "(nessun oggetto)" : state->buf_oggetto );
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
	
	if ( state->current_screen & UI_LISTNAV )
		printf( ANSIREV " K " ANSIRST "  " ANSIREV " J " ANSIRST "  Naviga lista\033[%d;5H", window.ws_row - 2 );

	if ( state->current_screen & UI_TEXTNAV && state->post_lines > max_post_lines )
		printf( ANSIREV " K " ANSIRST "  " ANSIREV " J " ANSIRST "  Scorri testo\033[%d;5H", window.ws_row - 2 );

	if ( state->current_screen & UI_PAGENAV && state->num_posts > max_posts_per_page )
		printf( "%s  %s  Cambia pagina\033[%d;36H", state->loaded_page > 1 ? ANSIREV " H " ANSIRST : "   ", 
				                            state->loaded_page < state->num_posts / max_posts_per_page + 1 ?
							                                     ANSIREV " L " ANSIRST : "   ",
							    window.ws_row - 4 );

	if ( state->current_screen & UI_READPOST )
		printf( "\033[%d;36H" ANSIREV " ENTER " ANSIRST "  Leggi post\033[%d;36H", window.ws_row - 4, window.ws_row - 2 );

	if ( state->current_screen & UI_WRITEPOST && state->auth_level > -1 )
		printf( ANSIREV " W " ANSIRST "  Scrivi post" );
	if ( state->current_screen & UI_SENDPOST )
		printf( "\033[%d;9H" ANSIREV " ^X " ANSIRST "  Pubblica\033[%d;9H", window.ws_row - 4, window.ws_row - 2 );

	if ( state->current_screen & UI_BACK )
		printf( ANSIREV " %sB " ANSIRST "  Torna indietro", state->current_screen == STATE_WRITING ? "^" : "" );

	if ( state->current_screen == STATE_WRITING )
		printf( "\033[%d;36H" ANSIREV " <TAB> " ANSIRST "  Cambia campo", window.ws_row - 4 );
	
	if ( state->current_screen != STATE_WRITING )
		printf( "\033[%d;63H" ANSIREV " Q " ANSIRST "  Disconnetti ed esci", window.ws_row - 4 );

	printf( "\033[%d;1H\033[0K\033[%dG┃", window.ws_row - 1, window.ws_col );
	//draw_box();
	
	if ( state->state_label[0] != '\0' )
		printf( "\033[%d;%dH" ANSIREV " %s " ANSIRST, window.ws_row - 1, window.ws_col - strlen( state->state_label ) - 3, state->state_label );
}

int drawTui_listView( ClientState *state )
{
	//if ( state->num_posts > max_posts_per_page ) state->pagenav_enabled = true;
	////state->pagenav_enabled = true;
	//state->listnav_enabled = true;
	//state->readpost_enabled = state->quit_enabled = true;
	//state->goback_enabled = false;
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
		printf( "%s %s %.*s %.*s", selected ? "*" : " ", ora_post, 
								 post->len_mittente, post->data,
								 oggetto_trunc_pos,  post->data + post->len_mittente );

		//free( mittente );
		//free( oggetto );
		free( ora_post );
	}
}

int drawTui_readPost( ClientState *state )
{
	//state->pagenav_enabled  = false;
	//state->listnav_enabled  = false;
	//state->readpost_enabled = false;
	//state->goback_enabled   = true;

	draw_header( state );

	Post *curr_post = state->cached_posts[ state->selected_post ];

	//int l = 0;
	//unsigned short padding_x = window.ws_col / 10;
	unsigned short padding_x = 1;
	unsigned short padding_y = 1;

	printf( "\033[5;%1$dHAutore: %3$.*2$s\033[6;%1$dHOggetto: %5$.*4$s", 2 + padding_x,
									     curr_post->len_mittente, curr_post->data,
									     curr_post->len_oggetto,  curr_post->data + curr_post->len_mittente );
	// TODO: printa anche la data!
	
	//printf( "\033[7;%dHOffset: %u", 2 + padding_x, state->more_lines );

	//printf( "\033[8;3H%s", curr_post->data + curr_post->len_mittente + curr_post->len_oggetto );
	state->post_lines = printWrapped( curr_post->data + curr_post->len_mittente + curr_post->len_oggetto,
					  curr_post->len_testo,
					   	      2 + padding_x,
					   	      9 + padding_y,
					  window.ws_col - padding_x - 1,
					  window.ws_row - padding_y - 8,
					  state->post_offset            );

	max_post_lines    = window.ws_row - 2 * padding_y - 16;
	state->more_lines = state->post_lines - state->post_offset > max_post_lines;

	draw_footer( state );
}

int drawTui_writePost( ClientState *state )
{
	draw_header( state );
	draw_footer( state );

	// Come prima cosa riattiviamo il cursore: deve indicare all'utente dove sta scrivendo
	printf( "\033[5;3HOggetto: \033[?25h" );
	
	if ( state->current_draft_field == FIELD_TESTO )	// per lasciare il cursore sull'oggetto alla fine
	{
		if ( state->len_oggetto > window.ws_col - 11 - 3 )
			printf( "...%s", state->buf_oggetto + ( state->len_oggetto - ( window.ws_col - 11 - 3 ) + 3 ) );
		else
			printf( "%s", state->buf_oggetto );
	}

	printWrapped( state->buf_testo,				// stringa
		      state->len_testo,				// lunghezza
		      3, 8,					// x0 y0
		      window.ws_col - 2, window.ws_row - 9,	// x1 y1
		      -1				    );  // vogliamo sempre l'ultima parte del testo

	if ( state->current_draft_field == FIELD_OGGETTO )
	{
		if ( state->len_oggetto > window.ws_col - 11 - 3 )
			printf( "\033[5;12H...%s", state->buf_oggetto + ( state->len_oggetto - ( window.ws_col - 11 - 3 ) + 3 ) );
		else
			printf( "\033[5;12H%s", state->buf_oggetto );
	}
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
	else if ( state->current_screen == STATE_WRITING )
	{
		drawTui_writePost( state );
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
