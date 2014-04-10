#include "g_local.h"
#include "../qalgo/base64.h"

rs_authmap_t authmap;
rs_authplayer_t *authplayers;

void RS_InitAuth( void )
{
	authplayers = ( rs_authplayer_t* )G_Malloc( gs.maxclients * sizeof( authplayers[0] ) );
	authmap.b64name = (char*)base64_encode( (unsigned char *)level.mapname, strlen( level.mapname ), NULL );

	// Authenticate the map
	RS_AuthMap();
}

void RS_ShutdownAuth( void )
{
	G_Free( authplayers );
	free( authmap.b64name );
}

void RS_ThinkAuth( void )
{
	rs_authplayer_t *player;
	for( player = authplayers; player < authplayers + gs.maxclients; player++ )
	{
		if( player->client && player->client->team != TEAM_SPECTATOR )
			player->playTime += game.frametime;
	}
}

/**
 * Print player info for debugging
 */
static void RS_PlayerInfo( rs_authplayer_t *player )
{
	G_Printf( "client: %d\n", player->client );
	G_Printf( "status: %d\n", player->status );
	G_Printf( "nickStatus: %d\n", player->nickStatus );
	G_Printf( "name: %s\n", player->name );
	G_Printf( "nick: %s\n", player->nick );
	G_Printf( "admin: %d\n", player->admin );
	G_Printf( "id: %d\n", player->id );
	G_Printf( "failTime: %d\n", player->failTime );
	G_Printf( "playTime: %d\n", player->playTime );
	G_Printf( "races: %d\n", player->races );
}

/**
 * Setup the auth info for a player who just connected and entered the game
 * @param client Client who connected
 */
void RS_PlayerEnter( gclient_t *client )
{
	int playerNum = (int)( client - game.clients );
	if( playerNum < 0 && playerNum >= gs.maxclients )
		return;

	rs_authplayer_t *player = &authplayers[playerNum];
	memset( player, 0, sizeof( *player ) );
	player->client = client;
	Q_strncpyz( player->nick, client->netname, sizeof( player->nick ) );
}


/**
 * Destroy the authinfo for a player who just disconnected
 * @param client Client who disconnected
 */
void RS_PlayerDisconnect( gclient_t *client )
{
	int playerNum = (int)( client - game.clients );
	if( playerNum < 0 && playerNum >= gs.maxclients )
		return;

	rs_authplayer_t *player = &authplayers[playerNum];
	memset( player, 0, sizeof( rs_authplayer_t ) );
}

/**
 * Reset the auth specific fields on a player
 * @param player The player to reset
 */
void RS_PlayerReset( rs_authplayer_t *player )
{
	player->id = 0;
	player->status = QSTATUS_NONE;
	player->admin = false;
	player->playTime = 0;
	player->races = 0;
}