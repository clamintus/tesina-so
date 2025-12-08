#include "types.h"

enum screen_state {
	STATE_INTRO,
	STATE_LISTING,
	STATE_SINGLEPOST,
	STATE_WRITING
};

typedef struct client_state {
	enum screen_state current_screen;
	Post 		  **cached_posts;
	unsigned int      num_posts;
	unsigned int      selected_post;
	char 		  state_label[100];
	
	// Server characteristics
	char		  board_title[257];
	char		  server_addr[100];
	char*		  user;
	char*		  pass;
	int		  auth_level;
	
	// Commands
	int pagenav_enabled;
	int listnav_enabled;
	int readpost_enabled;
	int goback_enabled;
	int quit_enabled;
} ClientState;

int updateWinSize();
int drawTui( ClientState *state );
int drawTui_listView( ClientState *state );
