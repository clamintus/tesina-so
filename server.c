#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <err.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "types.h"
#include "helpers.h"

const char* CONFIG_PATH   = "serverconf";
const char* DATABASE_PATH = "msgdb";
const int   LISTENQ       = 1024;

#define BUF_SIZE 65536
char buffer[BUF_SIZE];

int gAllowGuests = 0;
int gPort = 3010;

Post *gPosts[2048];
int gPostCount = 0;

int loadConfig( void )
{
	FILE* fp;
	char* endptr;
	char key_buf[256];
	char value_buf[256];
	int length;
        
    configfileopen:
	fp = fopen( CONFIG_PATH, "r" );
	if ( !fp )
	{
		if ( errno == EINTR )
			goto configfileopen;
		return -1;
	}

	while ( fgets( buffer, BUF_SIZE, fp ) )
	{
		length = strlen( buffer );
		if ( buffer[ length - 1 ] == '\n' )
			buffer[ --length ] = '\0';

		if ( length > 250 )
		{
			fprintf( stderr, "server: impossibile caricare il file di configurazione. Contiene righe troppo lunghe.\n" );
			exit( EXIT_FAILURE );
		}

		if ( sscanf( buffer, "%[^=]=%s", key_buf, value_buf ) < 2 )
		{
			printf( "loadConfig: impossibile interpretare la riga \"%s\".\n", buffer );
			continue;
		}

		/* actual parsing */
		if ( !strcmp( key_buf, "AllowGuests" ) )
		{
			if ( *value_buf == '1' )
				gAllowGuests = 1;
			else if ( *value_buf == '0' )
				gAllowGuests = 0;
		}
		else if ( !strcmp( key_buf, "Port" ) )
		{
			gPort = strtol( value_buf, &endptr, 10 );
			if ( *endptr || gPort <= 0 || gPort > 65535 )
			{
				fprintf( stderr, "loadConfig: valore porta non valido.\n" );
				exit( EXIT_FAILURE );
			}
		}
	}

	while ( fclose( fp ) )
	{
		if ( errno != EINTR )
			err( EXIT_FAILURE, "loadConfig: errore nella close" );
	}

	return 0;
}

int loadDatabase( void )
{
	FILE* fp;
	char* endptr;
	char id_buf[9];
	char mittente[256];
	char oggetto[256];
	char testo[64001];
	unsigned long len_mittente;
	unsigned long len_oggetto;
	unsigned long len_testo;
	unsigned long long timestamp;
	int count = 0;
        
    dbfileopen:
	fp = fopen( DATABASE_PATH, "r" );
	if ( !fp )
	{
		if ( errno == EINTR )
			goto dbfileopen;
		return -1;
	}

	while ( fgets( buffer, BUF_SIZE, fp ) )
	{
		if ( sscanf( buffer, "%[^\x1f]\x1f%llu\x1f%[^\x1f]\x1f%[^\x1f]\x1f%s", id_buf, &timestamp, mittente, oggetto, testo ) < 5 )
			continue;

		len_mittente = strlen( mittente );
		len_oggetto = strlen( oggetto );
		len_testo = strlen( testo );

		Post *newpost = malloc( POST_HEADER_SIZE + len_mittente + len_oggetto + len_testo );
		if ( newpost == NULL )
		{
			fprintf( stderr, "loadDatabase: memoria insufficiente\n" );
			fclose( fp );
			exit( EXIT_FAILURE );
		}
		newpost->id = strtoul( id_buf, NULL, 16 );
		newpost->timestamp = timestamp;
		newpost->len_mittente = len_mittente;
		newpost->len_oggetto = len_oggetto;
		newpost->len_testo = len_testo;
		strncpy( newpost->data, mittente, len_mittente );
		strncpy( newpost->data + len_mittente, oggetto, len_oggetto );
		strncpy( newpost->data + len_mittente + len_oggetto, testo, len_testo );

		gPosts[ gPostCount++ ] = newpost;
	}

	while ( fclose( fp ) )
	{
		if ( errno != EINTR )
			err( EXIT_FAILURE, "loadDatabase: errore nella close" );
	}

	return gPostCount;
}

int storeDatabase( void )
{
      dbfileopen2:
	FILE *fp = fopen( DATABASE_PATH, "w" );
	if ( !fp )
	{
		if ( errno == EINTR )
			goto dbfileopen2;
		return -1;
	}

	for ( int i = 0; i < gPostCount; i++ )
	{
		Post *curr_post = gPosts[ i ];
		if ( !curr_post->id )
			continue;
		// for ( int j = 0; j < sizeof( curr_post->id ); j++ )
		// 	fprintf( fp, "%02x", ( ( unsigned char *)&curr_post->id )[ j ] );
		fprintf( fp, "%08x\x1f%u\x1f", curr_post->id, curr_post->timestamp );
		fwrite( curr_post->data, curr_post->len_mittente, 1, fp );
		fputc( '\x1f', fp );
		fwrite( curr_post->data + curr_post->len_mittente, curr_post->len_oggetto, 1, fp );
		fputc( '\x1f', fp );
		fwrite( curr_post->data + curr_post->len_mittente + curr_post->len_oggetto, curr_post->len_testo, 1, fp );
		fputc( '\n', fp );
	}

	while ( fclose( fp ) )
	{
		if ( errno != EINTR )
			err( EXIT_FAILURE, "storeDatabase: errore nella close" );
	}

	return 0;
}

void unloadDatabase( void )
{
	for ( int i = 0; i < gPostCount; i++ )
	{
		free( gPosts[ i ] );
		gPosts[ i ] = NULL;
	}

	gPostCount = 0;
}

int delDatabaseEntry( uint32_t entry_id )
{
	if ( !entry_id )
		return -1;
	for ( int i = 0; i < gPostCount; i++ )
	{
		if ( gPosts[ i ]->id == entry_id )
		{
			gPosts[ i ] = 0;
			return 0;
		}
	}

	return -1;
}

void deinitAndErr( int eval, const char* fmt )
{
	unloadDatabase();
	err( eval, fmt );
}

int main( int argc, char *argv[] )
{
	int s_list;
	struct sockaddr_in serv_addr;

	if ( loadConfig() < 0 )
	{
		fprintf( stderr, "server: File di configurazione non trovato.\n" );
		exit( EXIT_FAILURE );
	}
	if ( loadDatabase() < 0 )
	{
		int db_fp;
	        while ( ( db_fp = creat( DATABASE_PATH, 0666 ) ) < 0 )
			if ( errno != EINTR )
				err( EXIT_FAILURE, "server: Impossibile creare il file database" );
		while ( close( db_fp ) < 0 )
			if ( errno != EINTR )
				err( EXIT_FAILURE, "server: Impossibile chiudere il nuovo database" );
	}
#ifdef DEBUG
	printf( "Caricate %d entry nel database.\n", gPostCount );
#endif

	if ( s_list = socket( AF_INET, SOCK_STREAM, 0 ) < 0 )
		deinitAndErr( EXIT_FAILURE, "server: Errore nella creazione della socket" );

	serv_addr.sin_family      = AF_INET;
	serv_addr.sin_port        = gPort;
	serv_addr.sin_addr.s_addr = INADDR_ANY;

	if ( bind( s_list, ( struct sockaddr *)&serv_addr, sizeof( serv_addr ) < 0 ) )
		deinitAndErr( EXIT_FAILURE, "server: Errore nel binding della socket" );

	if ( listen( s_list, LISTENQ ) < 0 )
		deinitAndErr( EXIT_FAILURE, "server: Impossibile ascoltare sulla porta" );



}

void test( void )
{
	if ( loadConfig() < 0 )
	{
		fprintf( stderr, "server: File di configurazione non trovato.\n" );
		exit( EXIT_FAILURE );
	}
	printf( "allow = %d\nport = %d\n", gAllowGuests, gPort );
	loadDatabase();
	unloadDatabase();
	loadDatabase();
	storeDatabase();
	unloadDatabase();
}
