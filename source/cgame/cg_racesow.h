extern cvar_t *rs_authUser;
extern cvar_t *rs_authToken;

void RS_CG_Init( void );
void RS_CG_Login( const char *user, const char *pass );
void RS_CG_GenToken( const char *salt );