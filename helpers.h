#include "types.h"

enum conv_type {
	TO_HOST,
	TO_NETWORK
};

uint16_t conv_u16( void* u16_addr, enum conv_type to_what );
uint32_t conv_u32( void* u32_addr, enum conv_type to_what );
uint64_t conv_u64( void* u64_addr, enum conv_type to_what );

int getValidInput( char* dest, int max_size, const char* prompt );
ssize_t getPostSize( Post *post );
int sockReceiveAll( int sockfd, unsigned char* msg_buf, size_t len );
