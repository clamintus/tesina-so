// Qui è descritto nel dettaglio il protocollo binario di comunicazione tra client e server che ho sviluppato.
// Questo file header non è incluso da nessun file sorgente, è inteso solo come riferimento.

#include "types.h"


/* PACCHETTI INVIATI DAL CLIENT (RICEVUTI DAL SERVER) */

struct Cli_Login {
	uint8_t frame_type = CLI_LOGIN;
	uint8_t len_user;
	uint8_t len_password;
	char	user[];
	char	password[];
};

struct Cli_GetPosts {
	uint8_t frame_type = CLI_GETPOSTS;
	uint8_t page;
	uint8_t limit;
};

struct Cli_Post {
	uint8_t frame_type = CLI_POST;
	uint8_t len_user;
	uint8_t len_password;
	char	user[];
	char	password[];
	Post	attached_post;		// post che si intende pubblicare, vedi types.h.
};					// Il server scarterà i campi mittente, id e timestamp di questa struttura.

struct Cli_DelPost {
	uint8_t  frame_type = CLI_DELPOST;
	uint8_t  len_user;
	uint8_t  len_password;
	char	 user[];
	char	 password[];
	uint32_t post_id;
};


/* PACCHETTI RICEVUTI DAL CLIENT (INVIATI DAL SERVER) */

struct Serv_Authenticate {
	uint8_t frame_type = SERV_AUTHENTICATE;
};

struct Serv_Welcome {
	uint8_t  frame_type = SERV_WELCOME;
	uint8_t  auth_level;		// 0: utente standard, 1: amministratore
	uint16_t post_count;
	int64_t  server_time;
	uint8_t  len_title;
	char	 board_title[];
};

struct Serv_Entries {
	uint8_t  frame_type = SERV_ENTRIES;
	uint16_t total_posts_count;
	uint8_t	 loaded_posts_count;
	Post	 posts[];
};

typedef uint8_t ServError;		/* valori possibili:										*
					 *												*
					 * - 0x0: Forbidden										*
					 *   	- (CLI_LOGIN) Credenziali sbagliate							*
					 *   	- (CLI_POST) Non autorizzato								*
					 *   	- (CLI_DELPOST) Non autorizzato								*
					 *												*
					 * - 0x1: Errore temporaneo									*
					 *   	- (CLI_POST) Errore nell'allocazione di memoria nel server				*
					 *   												*
					 * - 0x2: Errore non temporaneo									*
					 *   	- (CLI_POST) Errore nella scrittura del database, i dati sono da considerarsi persi	*
					 *   												*
					 * - 0xFF: Impossibile soddisfare la richiesta							*
					 *   	- (CLI_POST) Il server è pieno, impossibile ricevere nuovi post				*
					 *   	- (CLI_DELPOST) Post da cancellare non trovato						*/

struct Serv_Ok {
	uint8_t frame_type = SERV_OK;
};

struct Serv_Not_Ok {
	uint8_t   frame_type = SERV_NOT_OK;
	ServError error_description;
};
