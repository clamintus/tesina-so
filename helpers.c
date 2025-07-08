#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include "types.h"

int getValidInput( char* dest, int max_size, const char* prompt )
{
	int length;

	while (1)
	{
		printf( prompt );
		dest[ max_size - 2 ] = '\0';
		char* ret = fgets( dest, max_size, stdin );
		if ( ret == NULL )
			return -1;
		if ( dest[ max_size - 2 ] != '\0' && dest[ max_size - 2 ] != '\n' )
		{
			while( strlen( fgets( dest, max_size, stdin ) ) == max_size - 1 );
			puts( "Errore: Stringa di input troppo lunga, riprovare." );
		}
		else if ( dest[0] != '\n' )
			break;
	}

	length = strlen( dest );
	dest[ --length ] = '\0';

	return length;
}

ssize_t getPostSize( Post *post )
{
	unsigned short len_testo;

	if ( !post )
		return -1;
	len_testo    = ntohs( post->len_testo );

	return 4 + 	         // id
	       1 +               // len_mittente
	       1 +               // len_oggetto
	       2 +               // len_testo
	       8 +               // timestamp
	       post->len_mittente +
	       post->len_oggetto  +
	       len_testo;
}	

//ssize_t encode_LoginForm( void* dst, const char* user, const char* pass )
//{
//	uint8_t len_user = strlen( user );
//	uint8_t len_pass = strlen( pass );
//
//	*dst = malloc( 2 + len_user + len_pass );
//	if ( !(*dst) ) return -1;
//	memcpy( *dst               , &len_user, 1 );
//	memcpy( *dst + 1           , &len_pass, 1 );
//	memcpy( *dst + 2           , user, len_user );
//	memcpy( *dst + 2 + len_user, pass, len_pass );
//
//	return 2 + len_user + len_pass;
//}
