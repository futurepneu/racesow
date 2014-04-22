#include "g_as_local.h"

extern cvar_t *rs_statsEnabled;

void RS_InitQuery( void );
void RS_ShutdownQuery( void );

void RS_AuthNick( rs_authplayer_t *player, char *nick );
void RS_AuthMap( void );

void RS_ReportRace( rs_authplayer_t *player, int rtime, int *cp, int cpNum );
void RS_ReportMap( const char *tags );
void RS_ReportPlayer( rs_authplayer_t *player );

void RS_QueryPlayer( rs_authplayer_t *player );
void RS_QueryTop( gclient_t *client, const char* mapname, int limit );
void RS_QueryMaps( gclient_t *client, const char* pattern, const char* tags, int page );
void RS_QueryRandmap( char* tags[], void *data );
