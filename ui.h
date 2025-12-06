#include "types.h"

typedef struct client_state {
	Post **cached_posts;
	int  num_posts;
	int  selected_post;
	char state_label[100];
	
	// Commands
	int pagenav_enabled;
	int listnav_enabled;
	int readpost_enabled;
	int goback_enabled;
	int quit_enabled;
} ClientState;

int updateWinSize();
int drawTui_listView( ClientState *state );
