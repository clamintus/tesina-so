#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <err.h>
#include <string.h>
#include <time.h>
#include <ctype.h>
#include <sys/param.h>

#include "types.h"
#include "ui.h"

#define CUP(x,y) "\033[y;xH"

//struct termios term;
struct winsize window;
extern unsigned int post_limit;
unsigned int max_posts_per_page;
unsigned int max_post_lines;

int updateWinSize( ClientState *state )
{
	if ( ioctl( 0, TIOCGWINSZ, &window ) == -1 )
	{
		warn( "updateWinSize: ioctl error" );
		return 1;
	}

	if ( window.ws_col < 87 )
	{
		state->current_layout = LAYOUT_MOBILE;
		max_posts_per_page = MAX( ( int )window.ws_row - 5, 1 );
	}
	else
	{
		state->current_layout = LAYOUT_STANDARD;
		max_posts_per_page = MAX( ( int )window.ws_row - 12, 1 );
	}

	max_posts_per_page = MIN( max_posts_per_page, UINT8_MAX );

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

void draw_hline2( int row )
{
	if ( row == 1 || row == window.ws_row ) return;

	for ( unsigned short x = 0; x < window.ws_col; x++ )
		printf( "\033[%d;%dH─", row, x );
}

void draw_hline( int row )
{
	draw_hline2( row );
	printf( "\033[%d;1H┠\033[%d;%dH┨", row, row, window.ws_col );
}


char *stringifyTimestamp( time_t timestamp )
{
	char *result = malloc( 10 );
	struct tm post_tm;
	struct tm now_tm;
	struct tm *tmp;
	time_t now = time( NULL );

	tmp = localtime( &now );
	//memcpy( &now_tm, tmp, sizeof( struct tm ) );		// copy current time in now_tm
	now_tm = *tmp;
	tmp = localtime( &timestamp );
	post_tm = *tmp;

	if ( post_tm.tm_year == now_tm.tm_year && post_tm.tm_mon == now_tm.tm_mon && post_tm.tm_mday == now_tm.tm_mday )
		strftime( result, 10, "%R", &post_tm );
	else
		strftime( result, 10, "%d/%m", &post_tm );

	return result;
}

#define ANSIREV "\033[7m\033[1m"
#define ANSIITA "\033[2m\033[3m"
#define ANSIRST "\033[0m"
#define ANSIDIS "\033[30m\033[100m"
#define ANSINEW "\033[1m\033[3m\033[97m\033[5m"
unsigned int printWrapped( const char* str, size_t size, unsigned short x0, unsigned short y0, unsigned short x1, unsigned short y1, unsigned int skip )
{
	unsigned short x_len = x1 - x0 + 1;
	unsigned short y_len = y1 - y0 + 1;
	
	//unsigned short x = x0;
	//unsigned short y = y0;
	unsigned int   l = 0;

	const char* curr = str;
	const char* lines[4096];

	if ( x_len <= 0 || y_len <= 0 )
		return ( unsigned int )-1;

	lines[ l++ ] = curr;

	const char* curline = curr;
	const char* curword = curr;

	while ( *curr )
	{
	      continue_outer:
		// non dovremmo arrivare a questo punto,
		// ma se dovessimo evitiamo di corrompere la stack
		if ( l >= 4096 )
			break;

		while ( !isspace( *curr ) )
		{
			if ( *curr == '\0' || curr - str == ( ptrdiff_t )size )
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

		if ( curr - str == ( ptrdiff_t )size )
		{
			lines[ l ] = curr;
			goto endloop;
		}

		if ( *curr == '\v' )
		{
			lines[ l++ ] = ++curr;
			curline = curr;
			curword = curr;
			continue;
		}

		curword = ++curr;
		if ( curr - curline >= x_len )
		{
			lines[ l++ ] = curr;
			curline = curr;
			//curword = curr;
			continue;
		}
	}
	
	lines[ l ] = str + strnlen( str, size );

endloop:
	// Ora stampiamo effettivamente le righe
	unsigned int start_index = 0;
	if ( l > y_len )
		start_index = l - y_len;
	if ( skip != ( unsigned int )-1 )
		start_index = skip;

	// Also draw the scroll indicators
	if ( start_index != 0 )
		printf( "\033[%d;%dH" ANSIREV "[↑]" ANSIRST, y0 - 1, window.ws_col / 2 - 1 );
	if ( l > y_len && l != y_len + start_index )
		printf( "\033[%d;%dH" ANSIREV "[↓]" ANSIRST, y1 + 1, window.ws_col / 2 - 1 );

	for ( unsigned int i = 0; i < y_len && start_index + i < l; i++ )
	{
		int line_len = lines[ start_index + i + 1 ] - lines[ start_index + i ];
		if ( line_len && lines[ start_index + i ][ line_len - 1 ] == '\v' )
			line_len--;
		printf( "\033[%d;%dH%.*s", y0 + i,
				           x0,
					   line_len, lines[ start_index + i ] );
					   //lines[ start_index + i ][ line_len - 1 ] == '\n' ? "" : "\n" );
	}

	//printf( "\033[%d;%dH%d lines", y1 + 1, x0 + 2, l );

	return l;
}


int draw_header( ClientState *state )
{
	char left_text[512];
	char right_text[200];
	int left_text_len;
	int right_text_len;
	const char* listing_str    = "Lista post";
	const char* writing_str    = "Nuovo post";
	const char* singlepost_str = "Leggi post";
	int row_hdr = state->current_layout == LAYOUT_MOBILE ? 1 : 2;
	int col_off = state->current_layout == LAYOUT_MOBILE ? 1 : 3;

	if ( state->current_layout == LAYOUT_MOBILE )
	{
		switch ( state->current_screen )
		{
			case STATE_LISTING:
				if ( *state->board_title )
					strcpy( left_text, state->board_title );
				else
					strcpy( left_text, state->server_addr );
				break;

			case STATE_WRITING:
				sprintf( left_text, "%s", state->buf_oggetto[0] == '\0' ? "(nessun oggetto)" : state->buf_oggetto );
				break;

			case STATE_SINGLEPOST:
				strcpy( left_text, singlepost_str );
				break;
		}
	}
	else
     {	
	switch ( state->current_screen )
	{
		case STATE_LISTING:
			if ( *state->board_title )
				sprintf( left_text, "%s   |   %s   |   Pagina %u di %u", state->board_title, listing_str,
				      							 state->loaded_page, state->num_posts ?
											 ( state->num_posts - 1 ) / post_limit + 1 :
				      							 1	 		);  // da troncare
			else
				sprintf( left_text, "Bacheca Elettronica di %s   |   %s   |   Pagina %u di %u", state->server_addr, listing_str,
				      										state->loaded_page,
				      					state->num_posts ? ( state->num_posts - 1 ) / post_limit + 1 :
				      							   1  							);
			break;

		case STATE_WRITING:
			sprintf( left_text, "%s   |   %.*s", writing_str, (int)( window.ws_col - 6 - strlen( writing_str ) - 7 ),
							     state->buf_oggetto[0] == '\0' ? "(nessun oggetto)" : state->buf_oggetto );
			break;

		case STATE_SINGLEPOST:
			sprintf( left_text, "%s", singlepost_str );
			break;
	}
     }

	if ( state->current_layout == LAYOUT_MOBILE )
	{
		snprintf( right_text, 100, "%u/%u ", state->loaded_page, state->num_posts ? ( state->num_posts - 1 ) / post_limit + 1 : 1 );
		switch ( state->auth_level )
		{
			case 1:
				snprintf( right_text + strlen( right_text ), 100, "👑\033[1m%s" ANSIRST, state->user );
				right_text_len = -8;
				break;

			case 0:
				snprintf( right_text + strlen( right_text ), 100, "👤%s", state->user );
				right_text_len = 0;
				break;

			case -1:
				snprintf( right_text + strlen( right_text ), 100, "🫥" ANSIITA "anon" ANSIRST );
				right_text_len = -12;
				break;
		}
	}
	else
	{
		switch ( state->auth_level )
		{
			case 1:
				snprintf( right_text, 100, "Loggato come:  \033[1m%s" ANSIRST, state->user );
				right_text_len = -8;
				break;

			case 0:
				snprintf( right_text, 100, "Loggato come:  %s", state->user );
				right_text_len = 0;
				break;

			case -1:
				snprintf( right_text, 100, "Loggato come:  " ANSIITA "anonymous" ANSIRST );
				right_text_len = -12;
				break;
		}
	}
	right_text_len += strlen( right_text );
	left_text_len = strlen( left_text );

	if ( state->current_layout == LAYOUT_STANDARD )
		draw_hline( 3 );

	// Spazio per ltext: window.ws_row - right_text_len - padding
	const int padding_hdr = 3;

	int x_max = (int)window.ws_col - right_text_len - padding_hdr - 2 * col_off;
	if ( left_text_len > x_max )
	{
		for ( int i = 1; i <= 3 && left_text_len - i >= 0; i++ )
			left_text[ x_max - i ] = '.';
		left_text[ x_max ] = '\0';
	}

	if ( state->current_layout == LAYOUT_MOBILE )
	{
		// Dipingi barra bianca + lascia pennello bianco
		printf( "\033[%d;1H\033[7m", row_hdr );
		for ( int i = 0; i < window.ws_col; i++ )
			putchar( ' ' );
	}
	printf( "\033[%d;%dH%s\033[%d;%dH%s", row_hdr, 1 + col_off, left_text,
					      row_hdr, window.ws_col - right_text_len - col_off + 1 + 
					      ( state->current_layout == LAYOUT_MOBILE ? 2 : 0 ), right_text );

	return state->current_layout == LAYOUT_MOBILE ? 1 : 3;
}

int draw_footer( ClientState *state )
{
	unsigned short ROW1, ROW2, ROWSTATE;

	if ( state->current_layout == LAYOUT_STANDARD )
	{
		draw_hline( window.ws_row - 6 );
		printf( "\033[%d;5H", window.ws_row - 4 );
	}
	
	if ( state->current_layout == LAYOUT_MOBILE )
	{
		ROW1 = window.ws_row - 2;
		ROW2 = window.ws_row;
		ROWSTATE = window.ws_row - 3;
	}
	else
	{
		ROW1 = window.ws_row - 4;
		ROW2 = window.ws_row - 2;
		ROWSTATE = window.ws_row - 1;
	}

	// nel layout grande funzionava bene, NON RISCHIO DI INCASINARLO
	if ( state->current_layout != LAYOUT_STANDARD )
	{
		// pulisci righe
		printf( "\033[%d;1H\033[2K"
			"\033[%d;1H\033[2K", ROW1, ROW2 );
	}

	if ( state->current_screen & UI_LISTNAV && state->selected_post < state->loaded_posts )
		if ( state->current_layout == LAYOUT_MOBILE )
			printf( "\033[%d;4H🔼 " ANSIREV " K  J " ANSIRST " 🔽", ROW1 );
		else
			printf( ANSIREV " K " ANSIRST "  " ANSIREV " J " ANSIRST "  Naviga lista\033[%d;5H", ROW2 );

	if ( state->current_screen & UI_TEXTNAV && state->post_lines > max_post_lines )
		if ( state->current_layout == LAYOUT_MOBILE )
			printf( "\033[%d;4H🔼 " ANSIREV " K  J " ANSIRST " 🔽", ROW1 );
		else
			printf( ANSIREV " K " ANSIRST "  " ANSIREV " J " ANSIRST "  Scorri testo\033[%d;5H", ROW2 );

	if ( state->current_screen & UI_PAGENAV && state->num_posts > post_limit && state->num_posts != ( unsigned int ) -1 )
		if ( state->current_layout == LAYOUT_MOBILE )
			printf( "\033[%d;4H⏪ %s%s ⏩", ROW2,
							    state->loaded_page > 1 ? ANSIREV " H " ANSIRST : ANSIDIS " H " ANSIRST, 
				                            state->loaded_page < ( state->num_posts - 1 ) / post_limit + 1 ?
							                             ANSIREV " L " ANSIRST : ANSIDIS " L " ANSIRST );
		else
		printf( "%s  %s  Cambia pagina\033[%d;36H", state->loaded_page > 1 ? ANSIREV " H " ANSIRST : ANSIDIS " H " ANSIRST, 
				                            state->loaded_page < ( state->num_posts - 1 ) / post_limit + 1 ?
							                             ANSIREV " L " ANSIRST : ANSIDIS " L " ANSIRST,
							    ROW2 );

	if ( state->current_screen == STATE_SINGLEPOST && ( state->more_oggetto || state->ogg_offset ) )
		if ( state->current_layout == LAYOUT_MOBILE )
			printf( "\033[%d;%dH⏪ %s%s ⏩", ROW1, MAX( window.ws_col / 2 - 3, 21 ),
							     state->ogg_offset   ? ANSIREV " H " ANSIRST : ANSIDIS " H " ANSIRST, 
				                             state->more_oggetto ? ANSIREV " L " ANSIRST : ANSIDIS " L " ANSIRST );
		else
		printf( "\033[%d;36H%s  %s  Scorri ogg.\033[%d;36H", ROW1,
							     state->ogg_offset   ? ANSIREV " H " ANSIRST : ANSIDIS " H " ANSIRST, 
				                             state->more_oggetto ? ANSIREV " L " ANSIRST : ANSIDIS " L " ANSIRST,
							     ROW1 );

	if ( state->current_screen & UI_READPOST && state->selected_post < state->loaded_posts )
		if ( state->current_layout == LAYOUT_MOBILE )
			printf( "\033[%d;%dH" ANSIREV " ↵ " ANSIRST " 📖", ROW1, MAX( window.ws_col / 2 - 3, 21 ) );
		else
			printf( "\033[%d;36H" ANSIREV " ENTER " ANSIRST "  Leggi post\033[%d;36H", ROW1, ROW2 );

	if ( state->current_screen & UI_WRITEPOST )
		if ( state->current_layout == LAYOUT_MOBILE )
			printf( "\033[%d;%dH" ANSIREV " W " ANSIRST " 📝", ROW2, MAX( window.ws_col / 2 - 3, 21 ) );
		else
			printf( ANSIREV " W " ANSIRST "  Scrivi post" );

	if ( state->current_screen & UI_SENDPOST )
		if ( state->current_layout == LAYOUT_MOBILE )
			printf( "\033[%d;4H" ANSIREV " ^X " ANSIRST " 📤\033[%d;4H", ROW1, ROW2 );
		else	
			printf( "\033[%d;9H" ANSIREV " ^X " ANSIRST "  Pubblica\033[%d;9H", ROW1, ROW2 );

	if ( state->current_screen & UI_BACK )
		if ( state->current_layout == LAYOUT_MOBILE )
			printf( "\033[%d;%dH" ANSIREV " %sB " ANSIRST " 🔙",
					//state->current_screen == STATE_WRITING ? ROW2 : ROW1,
					ROW2,
					state->current_screen == STATE_SINGLEPOST ? MAX( window.ws_col / 2 - 3, 21 ) : 4,
					state->current_screen == STATE_WRITING ? "^" : "" );
		else			   
		printf( "\033[%d;%dH" ANSIREV " %sB " ANSIRST "  Torna indietro\033[%d;5H", ROW2,
											    state->current_screen == STATE_SINGLEPOST ? 36 : 9,
											    state->current_screen == STATE_WRITING ? "^" : "",
		     							      		    ROW2 );

	if ( state->current_screen == STATE_WRITING )
		if ( state->current_layout == LAYOUT_MOBILE )
			printf( "\033[%d;%dH" ANSIREV " ⇥ " ANSIRST " 🔃", ROW1, MAX( window.ws_col / 2 - 3, 21 ) );
		else
			printf( "\033[%d;36H" ANSIREV " TAB " ANSIRST "  Cambia campo", ROW1 );
	
	if ( state->current_screen == STATE_LISTING )
		if ( state->current_layout != LAYOUT_MOBILE )
			printf( "\033[%d;63H" ANSIREV " R " ANSIRST "  Aggiorna\033[%d;63H", ROW1, ROW2 );

	if ( state->current_screen & UI_DELPOST && state->loaded_posts > 0 &&
	     ( state->auth_level != 0 ||
	       ( ( state->current_screen != STATE_SINGLEPOST &&
		   !strncmp( state->cached_posts[ state->selected_post ]->data,
		             state->user,
		             state->cached_posts[ state->selected_post ]->len_mittente ) ) ||
	         ( state->current_screen == STATE_SINGLEPOST &&
		   !strncmp( state->opened_post->data,
			     state->user,
			     state->opened_post->len_mittente ) ) ) ) )
		if ( state->current_layout == LAYOUT_MOBILE )
			printf( "\033[%d;%dH" ANSIREV " D " ANSIRST " 🗑️",
					ROW2,
					state->current_screen == STATE_LISTING ? MAX( window.ws_col - 9, 33 ) : 4 );
		else
			printf( ANSIREV " D " ANSIRST "  Elimina post\033[%d;90H", ROW1 );
		
	if ( state->current_screen != STATE_WRITING )
		if ( state->current_layout == LAYOUT_MOBILE )
			printf( "\033[%d;%dH" ANSIREV " Q " ANSIRST " 🚪", ROW1, MAX( window.ws_col - 9, 33 ) );
		else
			printf( "\033[%d;63H" ANSIREV " Q " ANSIRST "  Disconnetti ed esci", ROW1 );

	if ( state->current_layout == LAYOUT_STANDARD )
		printf( "\033[%d;2H\033[0K\033[%dG┃", ROWSTATE, window.ws_col );
	else
		printf( "\033[%d;1H\033[0K", ROWSTATE );
	
	if ( state->state_label[0] != '\0' )
		if ( state->current_layout == LAYOUT_MOBILE )
			printf( "\033[%d;%dH" ANSIREV "%s" ANSIRST, ROWSTATE, MAX( ( int )window.ws_col - ( int )strlen( state->state_label ), 1 ),
			     					    state->state_label );
		else
		printf( "\033[%d;%dH" ANSIREV " %s " ANSIRST, ROWSTATE, window.ws_col - ( int )strlen( state->state_label ) - 3, state->state_label );

	return state->current_layout == LAYOUT_MOBILE ? 4 : 7;
}

int drawTui_listView( ClientState *state )
{
	//if ( state->num_posts > max_posts_per_page ) state->pagenav_enabled = true;
	////state->pagenav_enabled = true;
	//state->listnav_enabled = true;
	//state->readpost_enabled = state->quit_enabled = true;
	//state->goback_enabled = false;
	draw_header( state );
	draw_footer( state );

	int y_off = state->current_layout == LAYOUT_MOBILE ? 1 : 4;
	int x_off = state->current_layout == LAYOUT_MOBILE ? 0 : 3;

	
	if ( state->loaded_posts == 0 )
	{
		printf( "\033[5;6H" ANSIITA "(nessun post)" ANSIRST );
		return 0;
	}

	for ( unsigned int i = state->page_offset; i < state->loaded_posts && i < state->page_offset + max_posts_per_page; i++ )
	{
		Post *post = state->cached_posts[ i ];
		char *ora_post;
		int64_t timestamp;
		int selected = state->selected_post == i;
		int is_new = post->timestamp > state->most_recent_post_shown && strncmp( post->data, state->user, post->len_mittente );

		memcpy( &timestamp, &post->timestamp, 8 );
		ora_post = stringifyTimestamp( (time_t)timestamp );

		int mittente_trunc_pos = state->current_layout == LAYOUT_MOBILE ? window.ws_col - 6 :
										  window.ws_col - 6 - 5 - 3;
		int oggetto_trunc_pos  = state->current_layout == LAYOUT_MOBILE ? window.ws_col - 6 - 1 - post->len_mittente :
										  window.ws_col - 6 - 5 - 4 - post->len_mittente;
		if ( mittente_trunc_pos < 0 ) mittente_trunc_pos = 0;
		if ( oggetto_trunc_pos  < 0 ) oggetto_trunc_pos  = 0;
		//if ( strlen( oggetto ) > oggetto_trunc_pos ) oggetto[ oggetto_trunc_pos ] = '\0';	// tronca oggetto se più lungo di schermo
		if ( post->len_mittente < mittente_trunc_pos ) mittente_trunc_pos = post->len_mittente;
		if ( post->len_oggetto &&
		     post->len_oggetto < oggetto_trunc_pos ) oggetto_trunc_pos = post->len_oggetto;

		char* SEL   = state->current_layout == LAYOUT_MOBILE ? "\033[7m" : "> ";
		char* UNSEL = state->current_layout == LAYOUT_MOBILE ? ""        : "  ";

		printf( "\033[%d;%dH", 1 + y_off + i - state->page_offset, x_off );
		printf( "%s%s%s %.*s %.*s%s", selected ? SEL : UNSEL,
					      is_new ? ANSINEW : "", ora_post,
								     mittente_trunc_pos, post->data,
								     oggetto_trunc_pos,  post->len_oggetto ? post->data + post->len_mittente :
								 					     ANSIITA "(nessun oggetto)",
		     			      ANSIRST );

		free( ora_post );
	}

	return 0;
}

int drawTui_readPost( ClientState *state )
{
	//state->pagenav_enabled  = false;
	//state->listnav_enabled  = false;
	//state->readpost_enabled = false;
	//state->goback_enabled   = true;

	const char* aut_label = state->current_layout == LAYOUT_MOBILE ? "✍️ " : "Autore: ";
	const char* ogg_label = state->current_layout == LAYOUT_MOBILE ? "🪧 " : "Oggetto: ";
	const char* dat_label = state->current_layout == LAYOUT_MOBILE ? "🕒 " : "Postato il: ";
	int hdr_size = draw_header( state );
	int ftr_size = state->current_layout == LAYOUT_MOBILE ? 4 : 7;

	//Post *curr_post = state->cached_posts[ state->selected_post ];
	Post *curr_post = state->opened_post;
	char data_buf[20];
	uint64_t time_buf;
	memcpy( &time_buf, &curr_post->timestamp, 8 );
	strftime( data_buf, 20, "%d/%m/%Y %H:%M:%S", localtime( ( time_t *)&time_buf ) );

	//unsigned short padding_x = window.ws_col / 10;
	unsigned short padding_x = state->current_layout == LAYOUT_MOBILE ? 0 : window.ws_col / 10;
	unsigned short padding_y = 1;

	char oggetto_buf[257];
	size_t oggetto_len     = curr_post->len_oggetto - state->ogg_offset;
	size_t oggetto_maxlen  = window.ws_col - 2 * padding_x - strlen( ogg_label );
	size_t mittente_maxlen = window.ws_col - 2 * padding_x - strlen( aut_label );
	
	if ( curr_post->len_oggetto )
	{
		memcpy( oggetto_buf, curr_post->data + curr_post->len_mittente + state->ogg_offset, oggetto_len );
		oggetto_buf[ oggetto_len ] = '\0';
		if ( state->ogg_offset )
			for ( size_t i = 0; i < 3 && i < oggetto_len; i++ )
				oggetto_buf[ i ] = '.';
		if ( oggetto_len > oggetto_maxlen )
		{
			state->more_oggetto = 1;
			for ( size_t i = oggetto_maxlen - 1; i >= oggetto_maxlen - 3 && i >= 0; i-- )
				oggetto_buf[ i ] = '.';
			oggetto_buf[ oggetto_maxlen ] = '\0';
		}
		else
			state->more_oggetto = 0;
	}
	else
		strcpy( oggetto_buf, ANSIITA "(nessun oggetto)" ANSIRST );

	size_t mittente_len = curr_post->len_mittente > mittente_maxlen ? mittente_maxlen : curr_post->len_mittente;
	//printf( "\033[5;%1$dH"
	//	"Autore: %3$.*2$s"
	//	"\033[6;%1$dH"
	//	"Oggetto: %5$.*4$s", 1 + padding_x,
	//								     curr_post->len_mittente, curr_post->data,
	//								     curr_post->len_oggetto ? curr_post->len_oggetto : -1,
	//								     curr_post->len_oggetto ? curr_post->data + curr_post->len_mittente :
	//								     			      ANSIITA "(nessun oggetto)" ANSIRST );
	printf( "\033[%d;%dH" "%s%.*s", 1 + hdr_size + padding_y, 1 + padding_x, aut_label, mittente_len, curr_post->data );
	printf( "\033[%d;%dH" "%s%s",   2 + hdr_size + padding_y, 1 + padding_x, ogg_label, oggetto_buf );
	printf( "\033[%d;%dH" "%s%s",   3 + hdr_size + padding_y, 1 + padding_x, dat_label, data_buf );

	state->post_lines = printWrapped( curr_post->data + curr_post->len_mittente + curr_post->len_oggetto,
					  curr_post->len_testo,
					   	      1 + padding_x,
					   5 + hdr_size + padding_y,
			       window.ws_col 		- padding_x,
			       window.ws_row - ftr_size - padding_y,
			       state->post_offset                    );

	max_post_lines    = window.ws_row - 2 * padding_y - hdr_size - ftr_size - 4;
	state->more_lines = state->post_lines - state->post_offset > max_post_lines;

	draw_footer( state );

	return 0;
}

int drawTui_writePost( ClientState *state )
{
	int hdr_size = draw_header( state );
	int ftr_size = draw_footer( state );

	const char* oggetto_text = "Oggetto: ";
	int oggetto_x = state->current_layout == LAYOUT_MOBILE ? 1 : 3;
	int oggetto_y = hdr_size + 2;
	int testo_pad_x = state->current_layout == LAYOUT_MOBILE ? 0 : 2;
	int testo_pad_y = hdr_size + ftr_size;

	if ( state->current_layout == LAYOUT_STANDARD )
	{
		// più carino
		testo_pad_y -= 2;
	}

	// Come prima cosa riattiviamo il cursore: deve indicare all'utente dove sta scrivendo
	printf( "\033[%d;%dH%s\033[?25h", oggetto_y, oggetto_x, oggetto_text );
	
	if ( state->current_draft_field == FIELD_TESTO )	// per lasciare il cursore sull'oggetto alla fine
	{
		if ( state->len_oggetto > window.ws_col - 11 - oggetto_x )
			printf( "...%s", state->buf_oggetto + ( state->len_oggetto - ( window.ws_col - 11 - oggetto_x ) + 3 ) );
		else
			printf( "%s", state->buf_oggetto );
	}

	printWrapped( state->buf_testo,						// stringa
		      state->len_testo,						// lunghezza
		      oggetto_x, oggetto_y + 2,					// x0 y0
		      window.ws_col - testo_pad_x, window.ws_row - testo_pad_y, // x1 y1
		      -1				    );  		// vogliamo sempre l'ultima parte del testo

	if ( state->current_draft_field == FIELD_OGGETTO )
	{
		if ( state->len_oggetto > window.ws_col - 11 - oggetto_x )
			printf( "\033[%d;%dH...%s", oggetto_y, oggetto_x + ( int )strlen( oggetto_text ),
						    state->buf_oggetto + ( state->len_oggetto - ( window.ws_col - 11 - oggetto_x ) + 3 ) );
		else
			printf( "\033[%d;%dH%s", oggetto_y, oggetto_x + ( int )strlen( oggetto_text ), state->buf_oggetto );
	}

	return 0;
}

	
	
int drawTui( ClientState *state )
{
	if ( updateWinSize( state ) )
		return 1;
	//test
	//window.ws_col = 50;
	printf( "\033[2J" );	// Erase-in Display
	if ( state->current_layout == LAYOUT_STANDARD )
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

	fflush( stdout );
	return 0;
}

void drawError( ClientState *state, const char *error_msg )
{
	const char* btn = "[ Esci ]";
	char* lines[10];
	int   l = 0;
	int   lmax = strlen( btn );
	int   x = 1;
	int   y = 1;

	char* msg = strdup( error_msg );
	char* msg2 = msg;

	msg = strtok( msg, "\n" );
	while ( msg )
	{
		lines[ l++ ] = msg;
		msg = strtok( NULL, "\n" );
	}

	for ( int i = 0; i < l; i++ )
		if ( strlen( lines[ i ] ) > lmax )
			lmax = strlen( lines[ i ] );

	if ( state->current_screen != STATE_INTRO )
	{
		x = MAX( (int)window.ws_col / 2 - lmax / 2, 1 );
		y = (int)window.ws_row / 2 - l / 2 - 1;

		printf( ANSIREV );
		for ( int i = y - 1; i < y + l + 3; i++ )
			for ( int j = x - 1; j < x + lmax + 1; j++ )
				printf( "\033[%d;%dH ", i, j );
		printf( ANSIRST "\033[%d;%dH%s" ANSIREV, y + l + 1, window.ws_col / 2 - strlen( btn ) / 2, btn );
		printf( "\033[?25l" );
	}

	for ( int i = 0; i < l; i++ )
	{
		if ( state->current_screen == STATE_INTRO )
			printf( "%s\n", lines[ i ] );
		else
			printf( "\033[%d;%dH%s", y + i, x, lines[ i ] );
	}
	
	printf( ANSIRST );
	if ( state->current_screen == STATE_INTRO )
		printf( "\nPremi Invio per uscire." );
	fflush( stdout );

	free( msg2 );
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
