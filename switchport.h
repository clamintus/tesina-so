#pragma once

#include <stdint.h>
#include <switch.h>


struct winsize {
	unsigned short ws_row;
	unsigned short ws_col;
};

void switchUiInit( void );

//uint64_t htobe64( uint64_t h64 )
//{
//	char buf[8];
//	char tmp;
//
//	memcpy( buf, &h64, 4 );
//	tmp = buf[0];
//	buf[0] = buf[7];
//	buf[7] = tmp;
//
//	tmp = buf[1];
//	buf[1] = buf[6];
//	buf[6] = tmp;
//	
//	tmp = buf[2];
//	buf[2] = buf[5];
//	buf[5] = tmp;
//
//	tmp = buf[3];
//	buf[3] = buf[4];
//	buf[4] = tmp;
//
//	return *( uint64_t *)&buf;
//};
//
//uint64_t be64toh( uint64_t be64 )
//{
//	return htobe64( be64 );
//};
