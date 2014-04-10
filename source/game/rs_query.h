#include "g_as_local.h"

void RS_InitQuery( void );
void RS_ShutdownQuery( void );

void RS_AuthRegister( rs_authplayer_t *player, const char *name, const char *pass, const char *email );
void RS_AuthPlayer( rs_authplayer_t *player, const char *name, const char *ctoken, int uTime );
void RS_AuthNick( gclient_t *client, const char *nick );
void RS_AuthMap( void );

void RS_ReportRace( gclient_t *client, int playerId, int mapId, int time, CScriptArrayInterface *checkpoints );
void RS_ReportMap( int playTime, int races );
void RS_ReportPlayer( const char *name, int mapId, int playTime, int races );

void RS_QueryTop( gclient_t *client, const char* mapname, int limit );
void RS_QueryMaps( gclient_t *client, const char* pattern, const char* tags, int page );
void RS_QueryRandmap( char* tags[], void *data );