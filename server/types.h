#pragma once
#include <stdint.h>

#define POST_HEADER_SIZE 16

typedef enum client_frametype : uint8_t
{
	CLI_LOGIN = 0b10010000,
	CLI_GETPOSTS,
	CLI_GETPOST,
	CLI_POST,
	CLI_DELPOST
} Client_Frametype;

typedef enum server_frametype : uint8_t
{
	SERV_AUTHENTICATE = 0b01100000,
	SERV_WELCOME,
	SERV_ENTRIES,
	SERV_ENTRY,
	SERV_OK,
	SERV_NOT_OK,
	SERV_BYE
} Server_Frametype;

typedef struct __attribute__((packed)) post
{
	uint32_t id;
	uint8_t  len_mittente;
	uint8_t  len_oggetto;
	uint16_t len_testo;
	int64_t  timestamp;
	char     data[];		// mittente + oggetto + testo
} Post;

typedef struct __attribute__((packed)) loginform
{
	uint8_t len_user;
	uint8_t len_pass;
	char    user_and_pass[];
} LoginForm;

typedef struct __attribute__((packed)) welcomeform
{
	uint8_t  is_admin;
	uint16_t posts;
	uint64_t local_time;
} WelcomeForm;
