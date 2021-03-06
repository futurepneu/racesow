#include "g_as_local.h"

extern cvar_t *rs_statsEnabled;

void RS_InitQuery( void );
void RS_ShutdownQuery( void );

void RS_AuthNick( rs_authplayer_t *player, char *nick );
void RS_AuthMap( void );

void RS_ReportRace( rs_authplayer_t *player, int rtime, int *cp, int cpNum, bool oneliner );
void RS_ReportMap( const char *tags, const char *oneliner, bool force );
void RS_ReportPlayer( rs_authplayer_t *player );
void RS_ReportNick( rs_authplayer_t *player, const char* nick );

void RS_QueryPlayer( rs_authplayer_t *player );
void RS_QueryTop( gclient_t *client, const char* mapname, int limit, int cmd );
void RS_QueryMaps( gclient_t *client, const char* pattern, const char* tags, int page );
void RS_QueryRandmap( char* tags[], void *data );
