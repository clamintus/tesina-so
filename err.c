#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <errno.h>
#include <switch.h>

#define MAXSIZE 1024

extern PadState gPad;

void _warn( const char* fmt, va_list ap )
{
	char buf[ MAXSIZE+1 ];
	char error[ 1024 ];

	vsprintf( buf, fmt, ap );
	sprintf( error, ": %s\n", strerror( errno ) );
	strcat( buf, error );

	fprintf( stderr, buf );
}

void warn( const char* fmt, ... )
{
	va_list ap;
	va_start( ap, fmt );
	_warn( fmt, ap );
	va_end( ap );
}

void err( int eval, const char* fmt, ... )
{
	va_list ap;
	va_start( ap, fmt );
	_warn( fmt, ap );
	va_end( ap );

	while ( appletMainLoop() )
	{
		padUpdate( &gPad );
		if ( padGetButtons( &gPad ) & HidNpadButton_Plus )
			consoleExit( NULL );
	}
}
