#include "cg_local.h"
#include "../qalgo/sha2.h"
#include "../qalgo/base64.h"

#define RS_HASH_ITERATIONS 100000

cvar_t *rs_authMessage;
cvar_t *rs_authUser;
cvar_t *rs_authToken;

/**
 * RS_CG_Init
 * Setup racesow user variables
 * @return void
 */
void RS_CG_Init( void )
{
	rs_authMessage = trap_Cvar_Get( "rs_authMessage", "", CVAR_READONLY );
	rs_authUser = trap_Cvar_Get( "rs_authUser", "", CVAR_READONLY | CVAR_USERINFO );
	rs_authToken = trap_Cvar_Get( "rs_authToken", "", CVAR_READONLY | CVAR_USERINFO );
	trap_Cvar_ForceSet( rs_authToken->name, "" );
	trap_Cvar_ForceSet( rs_authMessage->name, "" );
}

/**
 * Generate Filename for password file
 * ATM these are taken from mm_common
 * @return Filename of password file
 */
static const char *RS_PasswordFilename( void )
{
	static char filename[MAX_STRING_CHARS];
	char *user64;

	if( rs_authUser->string[0] == '\0' )
		return NULL;

	user64 = (char*)base64_encode( (unsigned char*)rs_authUser->string, strlen( rs_authUser->string ), NULL );

	Q_strncpyz( filename, user64, sizeof( filename ) - 1 );
	Q_strncatz( filename, ".profile", sizeof( filename ) - 1 );

	free( user64 );

	return filename;
}

/**
 * Read the users hashed password from file
 * @return Users saved and hashed password
 */
const char *RS_PasswordRead()
{
	static char buffer[MAX_STRING_CHARS];
	const char *filename;
	int filenum;
	size_t bytes;

	filename = RS_PasswordFilename();
	if( !filename || trap_FS_FOpenFile( filename, &filenum, FS_READ ) == -1 )
		return NULL;

	bytes = trap_FS_Read(buffer, sizeof( buffer ) - 1,  filenum );
	trap_FS_FCloseFile( filenum );

	if( bytes == 0 || bytes >= sizeof(buffer) - 1 )
		return NULL;	

	buffer[bytes] = '\0';

	return buffer;
}

/**
 * Hash the users password and save to a file
 * @param password The users raw password
 */
void RS_PasswordWrite( const char *password )
{
	const char *filename;
	char *hash;
	int filenum;
	unsigned char digest[SHA256_DIGEST_SIZE];

	sha256( (const unsigned char*)password, strlen( password ), digest);
	for( int i = 1; i < RS_HASH_ITERATIONS; i++ )
		sha256( digest, SHA256_DIGEST_SIZE, digest );
	hash = (char*)base64_encode( digest, (size_t)SHA256_DIGEST_SIZE, NULL );

	filename = RS_PasswordFilename();
	if( !filename || trap_FS_FOpenFile( filename, &filenum, FS_WRITE ) == -1 )
	{
		free( hash );
		return;
	}

	trap_FS_Write( hash, strlen( hash ), filenum );
	free( hash );
	trap_FS_FCloseFile( filenum );
}

/**
 * RS_CG_Login
 * Save the user name and password
 * @param user Username
 * @param pass User's raw password
 * @return void
 */
void RS_CG_Login( const char *user, const char *pass )
{
	if( user[0] == '\0' || pass[0] == '\0' )
	{
		CG_Printf( "Invalid username or password\n" );
		return;
	}

	trap_Cvar_ForceSet( rs_authUser->name, user );
	RS_PasswordWrite( pass );

	// Regenerate the token
	if( strlen( rs_authMessage->string ) )
		RS_CG_GenToken( rs_authMessage->string );
}

/**
 * RS_CG_GenToken
 * Generate the auth token for the current salt/user/pass values
 * @return void
 */
void RS_CG_GenToken( const char *salt )
{
	static char message[MAX_STRING_CHARS];
	unsigned char digest[SHA256_DIGEST_SIZE];
	char *token, *password;

	// Save the server message for future login attempts
	trap_Cvar_ForceSet( rs_authMessage->name, salt );

	password = (char*)RS_PasswordRead();
	if( !password || !strlen( rs_authUser->string ) )
	{
		trap_Cvar_ForceSet( rs_authToken->name, "" );
		return;
	}

	Q_strncpyz( message, va( "%s|", salt ), sizeof( message ) - 1 );
	Q_strncatz( message, password, sizeof( message ) - 1 );

	sha256( (const unsigned char*)message, strlen( message ), digest );
	token = (char*)base64_encode( digest, (size_t)SHA256_DIGEST_SIZE, NULL );
	trap_Cvar_ForceSet( rs_authToken->name, token );
	free( token );
}
