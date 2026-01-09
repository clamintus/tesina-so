#pragma once

#include <stdarg.h>

void _warn( const char* fmt, va_list ap );
void warn( const char* fmt, ... );
void err( int eval, const char* fmt, ... );
