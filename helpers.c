#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#ifndef __SWITCH__
 #include <termios.h>
 #include <endian.h>
#else
 #include <sys/endian.h>
#endif
#include "helpers.h"
#include "types.h"
#ifdef __SWITCH__
#include "switchport.h"
#endif

#ifndef __SWITCH__
struct termios gOldTerminal;

void setTerminalMode( enum terminal_mode mode )
{
	struct termios tmp_term;

	tcgetattr( 0, &tmp_term );

	gOldTerminal = tmp_term;
	if      ( mode == TERM_RAW )          tmp_term.c_lflag &= ~ICANON & ~ECHO;
	else if ( mode == TERM_CANON )        tmp_term.c_lflag |= ICANON | ECHO;
	else if ( mode == TERM_CANON_NOECHO ) tmp_term.c_lflag &= ~ECHO;
	tcsetattr( 0, TCSANOW, &tmp_term );
}

void restoreTerminal( void )
{
	tcsetattr( 0, TCSANOW, &gOldTerminal );
}
#else
//void setTerminalMode( enum terminal_mode mode ) {
//	if ( mode == TERM_CANON )
//		swkbdConfigMakePresetUserName( &gSwkbd );
//	else if ( mode == TERM_CANON_NOECHO )
//		swkbdConfigMakePresetPassword( &gSwkbd );
//	else
//		swkbdConfigMakePresetDefault( &gSwkbd );
//}
//void restoreTerminal( void ) {}
#endif

uint16_t conv_u16( void* u16_addr, enum conv_type to_what )
{
	uint16_t tmp_u16;

	memcpy( &tmp_u16, u16_addr, 2 );
	tmp_u16 = to_what == TO_NETWORK ? htons( tmp_u16 ) : ntohs( tmp_u16 );
	memcpy( u16_addr, &tmp_u16, 2 );
	
	return to_what == TO_HOST ? tmp_u16 : ntohs( tmp_u16 );
}

uint32_t conv_u32( void* u32_addr, enum conv_type to_what )
{
	uint32_t tmp_u32;

	memcpy( &tmp_u32, u32_addr, 4 );
	tmp_u32 = to_what == TO_NETWORK ? htonl( tmp_u32 ) : ntohl( tmp_u32 );
	memcpy( u32_addr, &tmp_u32, 4 );

	return to_what == TO_HOST ? tmp_u32 : ntohl( tmp_u32 );
}

uint64_t conv_u64( void* u64_addr, enum conv_type to_what )
{
	uint64_t tmp_u64;

	memcpy( &tmp_u64, u64_addr, 8 );
	tmp_u64 = to_what == TO_NETWORK ? htobe64( tmp_u64 ) : be64toh( tmp_u64 );
	memcpy( u64_addr, &tmp_u64, 8 );

	return to_what == TO_HOST ? tmp_u64 : be64toh( tmp_u64 );
}
	
//int getValidInput( char* dest, int max_size, const char* prompt )
//{
//#ifdef __SWITCH__
//	swkbdConfigSetGuideText( &gSwkbd, prompt );
//	swkbdConfigSetStringLenMax( &gSwkbd, max_size );
//
//	if ( !R_SUCCEEDED( swkbdShow( &gSwkbd, dest, max_size ) ) )
//		return -1;
//
//	return strlen( dest );
//#else
//	int length;
//
//	while (1)
//	{
//		printf( prompt );
//		fflush( stdout );
//		dest[ max_size - 2 ] = '\0';
//		char* ret = fgets( dest, max_size, stdin );
//		if ( ret == NULL )
//			return -1;
//		if ( dest[ max_size - 2 ] != '\0' && dest[ max_size - 2 ] != '\n' )
//		{
//			while( fgets( dest, max_size, stdin ) != NULL && strlen( dest ) == max_size - 1 );
//			puts( "Errore: Stringa di input troppo lunga, riprovare." );
//			fflush( stdout );
//		}
//		else if ( dest[0] != '\n' )
//			break;
//	}
//
//	length = strlen( dest );
//	dest[ --length ] = '\0';
//
//	return length;
//#endif
//}

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

ssize_t sockReceiveAll( int sockfd, unsigned char* msg_buf, size_t len )
{
	size_t  n_left = len;
	ssize_t ret;

	while ( n_left )
	{
		if ( ( ret = recv( sockfd, msg_buf, n_left, 0 ) ) <= 0 )
		{
			if ( !ret )
				return -1;

			if ( errno == EINTR )
				ret = 0;
			else
				return -1;
		}
		msg_buf += ret;
		n_left  -= ret;
	}

	return ( ssize_t )len;
}

ssize_t sockSendAll( int sockfd, unsigned char* msg_buf, size_t len )
{
	size_t  n_left = len;
	ssize_t ret;

	while ( n_left )
	{
		if ( ( ret = send( sockfd, msg_buf, n_left, 0 ) ) < 0 )
		{
			if ( errno == EINTR )
				ret = 0;
			else
				return -1;
		}
		msg_buf += ret;
		n_left  -= ret;
	}

	return ( ssize_t )len;
}

#ifdef __SWITCH__
SwkbdTextCheckResult validaOggetto( char* oggetto, size_t len_oggetto )
{
	for ( unsigned int i = 0; i < len_oggetto; i++ )
		if ( oggetto[ i ] == '\n' )
		{
			strncpy( oggetto, "Oggetto non valido.", len_oggetto );
			return SwkbdTextCheckResult_Bad;
		}

	return SwkbdTextCheckResult_OK;
}
#endif
