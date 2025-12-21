#include "types.h"

#ifdef __ANDROID__
 #define true 1
 #define false 0
#endif

/* Comandi:							 *
 * 								 *
 * STATE_INTRO:      readpost					 *
 * STATE_LISTING:    pagenav, listnav, readpost, writepost, quit *
 * STATE_SINGLEPOST: textnav, goback,  delpost,  quit            *
 * STATE_WRITING:    goback,  sendpost                           */

#define UI_READPOST  1
#define UI_PAGENAV   (1 << 1)
#define UI_LISTNAV   (1 << 2)
#define UI_TEXTNAV   (1 << 3)
#define UI_WRITEPOST (1 << 4)
#define UI_SENDPOST  (1 << 5)
#define UI_DELPOST   (1 << 6)
#define UI_BACK      (1 << 7)

enum screen_state : uint8_t {
	STATE_INTRO      = UI_READPOST,
	STATE_LISTING    = UI_PAGENAV | UI_LISTNAV | UI_READPOST | UI_WRITEPOST | UI_DELPOST,
	STATE_SINGLEPOST = UI_TEXTNAV | UI_BACK | UI_DELPOST,
	STATE_WRITING    = UI_SENDPOST | UI_BACK,
	STATE_ERROR      = 0
};

enum screen_layout : uint8_t {
	LAYOUT_STANDARD,
	LAYOUT_MOBILE
};

enum draft_state : uint8_t {
	FIELD_OGGETTO,
	FIELD_TESTO
};

typedef struct client_state {
	enum screen_state  current_screen;
	enum screen_layout current_layout;
	Post 		   **cached_posts;
	unsigned int       num_posts;
	unsigned int	   loaded_page;
	unsigned int       page_offset;
	unsigned int	   loaded_posts;
	unsigned int       selected_post;
	Post		   *opened_post;
	unsigned int	   post_lines;
	unsigned int	   post_offset;
	int		   more_lines;
	enum draft_state   current_draft_field;
	char               buf_oggetto[257];
	char		   buf_testo[60001];
	unsigned char	   len_oggetto;
	unsigned int	   len_testo;
	char 		   state_label[100];
	unsigned long	   most_recent_post_shown;
	
	// Server characteristics
	char		   board_title[257];
	char		   server_addr[100];
	char*		   user;
	char*		   pass;
	int		   auth_level;
	
	// Commands
	//int pagenav_enabled;
	//int listnav_enabled;
	//int readpost_enabled;
	//int goback_enabled;
	//int quit_enabled;
} ClientState;

int updateWinSize( ClientState *state );
int drawTui( ClientState *state );
//int drawTui_listView( ClientState *state );
//int drawTui_readPost( ClientState *state );
int drawError( char *error_msg );
