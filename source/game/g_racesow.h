#include "g_as_local.h"

extern cvar_t *rs_statsEnabled;
extern cvar_t *rs_grenade_minKnockback;
extern cvar_t *rs_grenade_maxKnockback;
extern cvar_t *rs_grenade_splash;
extern cvar_t *rs_grenade_speed;
extern cvar_t *rs_grenade_timeout;
extern cvar_t *rs_grenade_gravity;
extern cvar_t *rs_grenade_friction;
extern cvar_t *rs_grenade_prestep;
extern cvar_t *rs_rocket_minKnockback;
extern cvar_t *rs_rocket_maxKnockback;
extern cvar_t *rs_rocket_splash;
extern cvar_t *rs_rocket_speed;
extern cvar_t *rs_rocket_prestep;
extern cvar_t *rs_rocket_antilag;
extern cvar_t *rs_plasma_minKnockback;
extern cvar_t *rs_plasma_maxKnockback;
extern cvar_t *rs_plasma_splash;
extern cvar_t *rs_plasma_speed;
extern cvar_t *rs_plasma_prestep;
extern cvar_t *rs_plasma_hack;
extern cvar_t *rs_gunblade_minKnockback;
extern cvar_t *rs_gunblade_maxKnockback;
extern cvar_t *rs_gunblade_splash;
extern cvar_t *rs_splashfrac;

void RS_Init( void );
void RS_Shutdown( void );
void RS_removeProjectiles( edict_t *owner );
void RS_SplashFrac( const vec3_t origin, const vec3_t mins, const vec3_t maxs, const vec3_t point, float maxradius, vec3_t pushdir, float *kickFrac, float *dmgFrac );

void RS_AuthRegister( gclient_t *client, const char *name, const char *pass, const char *nick );
void RS_AuthPlayer( gclient_t *client, const char *name, const char *ctoken, int authTime, int mapId );
void RS_AuthNick( gclient_t *client, const char *nick );
void RS_AuthMap();

void RS_ReportRace( gclient_t *client, int playerId, int mapId, int time, CScriptArrayInterface *checkpoints );
void RS_ReportMap( int playTime, int races );
void RS_ReportPlayer( const char *name, int mapId, int playTime, int races );

void RS_QueryTop( gclient_t *client, const char* mapname, int limit );