#include <stdint.h>

#define POST_HEADER_SIZE 12

typedef struct __attribute__((packed)) post
{
	uint32_t id;
	uint8_t  len_mittente;
	uint8_t  len_oggetto;
	uint16_t len_testo;
	int64_t  timestamp;
	char     data[];		// mittente + oggetto + testo
} Post;
