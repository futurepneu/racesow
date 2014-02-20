#include "cg_local.h"
#include "../qalgo/sha2.h"
#include "../qalgo/base64.h"

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
	rs_authToken = trap_Cvar_Get( "rs_authToken", "", CVAR_READONLY | CVAR_USERINFO );
	trap_Cvar_ForceSet( rs_authToken->name, "" );
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
 * Read the users password from file
 * @return Users saved password
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
 * Save the users password to a file
 * @param password The users password
 */
void RS_PasswordWrite( const char *password )
{
	const char *filename;
	int filenum;

	filename = RS_PasswordFilename();
	if( !filename || trap_FS_FOpenFile( filename, &filenum, FS_WRITE ) == -1 )
		return;

	trap_FS_Write( password, strlen( password ), filenum );
	trap_FS_FCloseFile( filenum );
}

/**
 * RS_CG_Login
 * Save the user name and password
 * @param user Username
 * @param pass Password
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
}

/**
 * RS_CG_GenToken
 * Generate the auth token for the current salt/user/pass values
 * @return void
 */
void RS_CG_GenToken( const char *salt )
{
	const char *pass = RS_PasswordRead(),
		*message = va( "%s|%s", salt, pass );
	unsigned char digest[SHA256_DIGEST_SIZE];
	size_t *outlen;

	if( rs_authUser->string[0] == '\0' || !pass )
		return;

	sha256( (const unsigned char*)message, strlen( message ), digest );
	trap_Cvar_ForceSet( rs_authToken->name, (const char*)base64_encode( digest, (size_t)SHA256_DIGEST_SIZE, outlen ) );
}
