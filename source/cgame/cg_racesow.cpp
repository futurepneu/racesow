#include "cg_local.h"
#include "../qalgo/sha2.h"
#include "../qalgo/base64.h"

#define RS_HASH_ITERATIONS 100000

cvar_t *rs_authTime;
cvar_t *rs_authUser;
cvar_t *rs_authToken;

/**
 * RS_CG_Init
 * Setup racesow user variables
 * @return void
 */
void RS_CG_Init( void )
{
	rs_authUser = trap_Cvar_Get( "rs_authUser", "", CVAR_ARCHIVE | CVAR_USERINFO );
	rs_authTime = trap_Cvar_Get( "rs_authTime", "", CVAR_READONLY | CVAR_USERINFO );
	rs_authToken = trap_Cvar_Get( "rs_authToken", "", CVAR_READONLY | CVAR_USERINFO );
	trap_Cvar_ForceSet( rs_authTime->name, "" );
	trap_Cvar_ForceSet( rs_authToken->name, "" );
}

/**
 * Generate Filename for password file
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
static const char *RS_PasswordRead( void )
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
static void RS_PasswordWrite( const char *password )
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
 * RS_CG_GenToken
 * Generate the auth token for the current time / username / password
 * @return void
 */
static void RS_CG_GenToken()
{
	static char message[MAX_STRING_CHARS];
	unsigned char digest[SHA256_DIGEST_SIZE];
	char *token, *password;

	// Set the login time
	trap_Cvar_ForceSet( rs_authTime->name, va( "%d", (int)time( NULL ) ) );

	// Read the password
	password = (char*)RS_PasswordRead();
	if( !password || !strlen( rs_authUser->string ) )
		return;

	Q_strncpyz( message, va( "%s|", rs_authTime->string ), sizeof( message ) );
	Q_strncatz( message, password, sizeof( message ) );

	sha256( (const unsigned char*)message, strlen( message ), digest );
	token = (char*)base64_encode( digest, (size_t)SHA256_DIGEST_SIZE, NULL );
	trap_Cvar_ForceSet( rs_authToken->name, token );
	free( token );
}

/**
 * RS_CG_SLogin
 * Handler for server login command. Login the user with existing credentials.
 * @return void
 */
void RS_CG_SLogin( void )
{
	RS_CG_GenToken();
	trap_Cmd_ExecuteText( EXEC_NOW, va( "__login \"%s\" \"%s\" \"%s\"", rs_authUser->string, rs_authToken->string, rs_authTime->string ) );
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
	trap_Cvar_ForceSet( rs_authUser->name, user );
	RS_PasswordWrite( pass );
	RS_CG_GenToken();

	trap_Cmd_ExecuteText( EXEC_NOW, va( "__login \"%s\" \"%s\" \"%s\"", rs_authUser->string, rs_authToken->string, rs_authTime->string ) );
}

/**
 * RS_CG_Register
 * Register the given username, password, email
 * @param user Username
 * @param pass User's raw password
 * @param email Email address
 * @return void
 */
void RS_CG_Register( const char *user, const char *pass, const char *email )
{
	trap_Cvar_ForceSet( rs_authUser->name, user );
	RS_PasswordWrite( pass );
	trap_Cvar_ForceSet( rs_authToken->name, RS_PasswordRead() );

	trap_Cmd_ExecuteText( EXEC_NOW, va( "__register \"%s\" \"%s\" \"%s\"", rs_authUser->string, rs_authToken->string, email ) );
}
