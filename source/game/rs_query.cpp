#include <vector>

#include "g_local.h"
#include "../qcommon/cjson.h"
#include "../matchmaker/mm_query.h"
#include "../qalgo/base64.h"
#include "../qalgo/sha2.h"

static stat_query_api_t *rs_sqapi;

cvar_t *rs_statsEnabled;
cvar_t *rs_statsId;
cvar_t *rs_statsKey;

void RS_InitQuery( void )
{
	rs_statsEnabled = trap_Cvar_Get( "rs_statsEnabled", "0", CVAR_ARCHIVE );
	rs_statsId = trap_Cvar_Get( "rs_statsId", "", CVAR_ARCHIVE );
	rs_statsKey = trap_Cvar_Get( "rs_statsKey", "", CVAR_ARCHIVE );
	rs_sqapi = trap_GetStatQueryAPI();
	if( !rs_sqapi )
		trap_Cvar_ForceSet( rs_statsEnabled->name, "0" );
}

void RS_ShutdownQuery( void )
{
}

/**
 * Parse a race record from a database query into a race string
 * @param  record The json object of the record
 * @return        String representing the race
 */
static char *RS_ParseRace( cJSON *record )
{
	static char args[1024];
	cJSON *node;
	std::vector<int> argv;
	int cpNum, cpTime;

	// Parse the world record into a string to hand to angelscript
	// "racetime cp1 cp2 cp3... cpN"
	argv.push_back( cJSON_GetObjectItem( record, "time" )->valueint );
	node = cJSON_GetObjectItem( record, "checkpoints" )->child;
	for( ; node; node=node->next )
	{
		cpNum = cJSON_GetObjectItem( node, "number" )->valueint;
		cpTime = cJSON_GetObjectItem( node, "time" )->valueint;
		if( cpNum + 1 >= (int)argv.size() )
			argv.resize( cpNum + 2 );
		argv[cpNum + 1] = cpTime;
	}

	memset( args, 0, sizeof( args ) );
	for(std::vector<int>::iterator it = argv.begin(); it != argv.end(); ++it)
		Q_strncatz( args, va( "%d ", *it ), sizeof( args ) - 1 );

	return args;
}

/**
 * RS_GenToken
 * Generate the server token for the given string
 * @param dst The token string to append to
 * @param str The message to generate a token for
 * @return void
 */
static void RS_GenToken( char *token, const char *str )
{
	unsigned char digest[SHA256_DIGEST_SIZE];
	char *digest64,
		*message = va( "%s|%s", str, rs_statsKey->string );
	size_t outlen;

	sha256( (const unsigned char*)message, strlen( message ), digest );
	digest64 = (char*)base64_encode( digest, (size_t)SHA256_DIGEST_SIZE, &outlen );

	Q_strncatz( token, digest64, MAX_STRING_CHARS );
	free( digest64 );
}

/**
 * Sign a query with the generated server token
 * @param query The query to sign
 * @param uTime The time to sign the query with
 */
static void RS_SignQuery( stat_query_t *query, int uTime )
{
	char *token;

	token = (char*)G_Malloc( MAX_STRING_CHARS );
	Q_strncpyz( token, rs_statsId->string, sizeof( token ) );
	Q_strncatz( token, ".", sizeof( token ) );
	RS_GenToken( token, va( "%d", uTime ) );

	rs_sqapi->SetField( query, "sid", rs_statsId->string );
	rs_sqapi->SetField( query, "uTime", va( "%d", uTime ) );
	rs_sqapi->SetField( query, "sToken", token );
	G_Free( token );
}

/**
 * Handle callback for register query
 * @param query   The query calling this function
 * @param success True on any response
 * @param customp rs_authplayer_t for the player to register
 */
void RS_AuthRegister_Done( stat_query_t *query, qboolean success, void *customp )
{
	rs_authplayer_t *player = ( rs_authplayer_t* )customp;
	cJSON *data;
	int playerNum;

	// Did they disconnect?
	playerNum = (int)( player->client - game.clients );
	if( playerNum < 0 || playerNum >= gs.maxclients )
		return;
	
	data = (cJSON*)rs_sqapi->GetRoot( query );
	if( rs_sqapi->GetStatus( query ) != 200 )
	{
		G_PrintMsg( &game.edicts[ playerNum + 1 ], "%sError:%s Failed to register\n",
					S_COLOR_RED, S_COLOR_WHITE );
		if( data )
			G_PrintMsg( &game.edicts[ playerNum + 1 ], "%sError:%s %s\n",
					S_COLOR_RED, S_COLOR_WHITE, cJSON_GetObjectItem( data, "error" )->valuestring );
		player->status = QSTATUS_FAILED;
		return;
	}

	RS_PlayerReset( player );
	player->status = QSTATUS_SUCCESS;
	player->id = cJSON_GetObjectItem( data, "id" )->valueint;
	player->admin = cJSON_GetObjectItem( data, "admin" )->type == cJSON_True;
	Q_strncpyz( player->name, cJSON_GetObjectItem( data, "username" )->valuestring, sizeof( player->name ) );

	G_PrintMsg( NULL, "%s%s authenticated as %s\n", player->client->netname, S_COLOR_WHITE, player->name );
	if( player->admin )
		G_PrintMsg( &game.edicts[ playerNum + 1 ], "You are an admin. Player id: %d\n", player->id );
	else
		G_PrintMsg( &game.edicts[ playerNum + 1 ], "Player id: %d\n", player->id );
}

/**
 * Send the query to register a new player
 * @param player The player being registered
 * @param name   Username
 * @param pass   Prehashed password
 * @param email  Email address
 */
void RS_AuthRegister( rs_authplayer_t *player, const char *name, const char *pass, const char *email )
{
	stat_query_t *query;
	char url[MAX_STRING_CHARS], *b64name, *b64email, *b64nick;
	int playerNum;

	if( !rs_statsEnabled->integer )
		return;

	if( !name || !strlen( name ) ||
		!pass || !strlen( pass ) ||
		!email || !strlen( email ) )
		return;

	playerNum = (int)( player->client - game.clients );
	if( playerNum < 0 && playerNum >= gs.maxclients )
		return;
	if( player->status == QSTATUS_PENDING )
	{
		G_PrintMsg( &game.edicts[playerNum + 1], "%sError: %sPlease wait for your current query to finish\n",
					S_COLOR_RED, S_COLOR_WHITE );
		return;
	}
	if( player->id )
	{
		G_PrintMsg( &game.edicts[playerNum + 1], "%sError: %sYou are already logged in.\n",
					S_COLOR_RED, S_COLOR_WHITE );
		return;
	}

	// Make the URL
	b64name = (char*)base64_encode( (unsigned char *)name, strlen( name ), NULL );
	Q_strncpyz( url, "api/player/", sizeof( url ) - 1 );
	Q_strncatz( url, b64name, sizeof( url ) - 1 );
	free( b64name );

	// Form the query and query parameters
	b64nick = (char*)base64_encode( (unsigned char *)player->client->netname, strlen( player->client->netname ), NULL );
	b64email = (char*)base64_encode( (unsigned char *)email, strlen( email ), NULL );
	query = rs_sqapi->CreateQuery( url, qfalse );
	rs_sqapi->SetField( query, "email", b64email );
	rs_sqapi->SetField( query, "nick", b64nick );
	rs_sqapi->SetField( query, "cToken", pass );

	RS_SignQuery( query, (int)time( NULL ) );
	rs_sqapi->SetCallback( query, RS_AuthRegister_Done, (void*)player );
	rs_sqapi->Send( query );
	free( b64email );
	free( b64nick );
	query = NULL;
}

/**
 * AuthPlayer callback handler
 * @param query   Query calling this function
 * @param success True on any response
 * @param customp rs_authplayer_t of the player being queried
 */
void RS_AuthPlayer_Done( stat_query_t *query, qboolean success, void *customp )
{
	rs_authplayer_t *player = ( rs_authplayer_t* )customp;
	cJSON *data, *node;
	int playerNum;

	// Did they disconnect?
	playerNum = (int)( player->client - game.clients );
	if( playerNum < 0 || playerNum >= gs.maxclients )
		return;

	RS_PlayerReset( player );
	if( rs_sqapi->GetStatus( query ) != 200 )
	{
		G_PrintMsg( NULL, "%sError:%s %s failed to authenticate as %s\n", 
					S_COLOR_RED, S_COLOR_WHITE, player->client->netname, player->name );
		player->status = QSTATUS_FAILED;
		return;
	}

	data = (cJSON*)rs_sqapi->GetRoot( query );
	player->status = QSTATUS_SUCCESS;
	player->id = cJSON_GetObjectItem( data, "id" )->valueint;
	player->admin = cJSON_GetObjectItem( data, "admin" )->type == cJSON_True;
	Q_strncpyz( player->name, cJSON_GetObjectItem( data, "username" )->valuestring, sizeof( player->name ) );
	Q_strncpyz( player->nick, cJSON_GetObjectItem( data, "simplified" )->valuestring, sizeof( player->nick ) );

	// Update protected nick
	RS_PlayerUserinfoChanged( player, NULL );

	// Notify of login
	G_PrintMsg( NULL, "%s%s authenticated as %s\n", player->client->netname, S_COLOR_WHITE, player->name );
	if( player->admin )
		G_PrintMsg( &game.edicts[ playerNum + 1 ], "You are an admin. Player id: %d\n", player->id );
	else
		G_PrintMsg( &game.edicts[ playerNum + 1 ], "Player id: %d\n", player->id );

	// Check for a world record
	node = cJSON_GetObjectItem( data, "record" );
	if( node->type != cJSON_Object )
		return;
	G_Gametype_ScoreEvent( player->client, "rs_loadplayer", RS_ParseRace( node ) );
}

/**
 * Authenticate and login a player
 * @param player Playerauth to authenticate
 * @param name   Player's login name
 * @param ctoken Player's authentication token
 * @param uTime  Unixtimestamp used to sign the token
 */
void RS_AuthPlayer( rs_authplayer_t *player, const char *name, const char *ctoken, int uTime )
{
	stat_query_t *query;
	char url[MAX_STRING_CHARS], *b64name;
	int playerNum;

	if( !rs_statsEnabled->integer )
		return;

	if( !name || !strlen( name ) || !ctoken || !strlen( ctoken ) )
		return;

	playerNum = (int)( player->client - game.clients );
	if( playerNum < 0 && playerNum >= gs.maxclients )
		return;
	if( player->status == QSTATUS_PENDING )
	{
		G_PrintMsg( &game.edicts[playerNum + 1], "%sError: %sPlease wait for your current query to finish\n",
					S_COLOR_ORANGE, S_COLOR_WHITE );
		return;
	}

	// Make the URL
	b64name = (char*)base64_encode( (unsigned char *)name, strlen( name ), NULL );
	Q_strncpyz( url, "api/player/", sizeof( url ) - 1 );
	Q_strncatz( url, b64name, sizeof( url ) - 1 );
	free( b64name );

	// Form the query and query parameters
	query = rs_sqapi->CreateQuery( url, qtrue );
	rs_sqapi->SetField( query, "mid", va( "%d", authmap.id ) );
	rs_sqapi->SetField( query, "cToken", ctoken );

	RS_SignQuery( query, uTime );
	rs_sqapi->SetCallback( query, RS_AuthPlayer_Done, (void*)player );
	rs_sqapi->Send( query );
	player->status = QSTATUS_PENDING;
	query = NULL;
}


/**
 * AuthNick callback function
 * @param query   Query calling this function
 * @param success True on any response
 * @param customp rs_authplayer_t player to nick check
 */
void RS_AuthNick_Done( stat_query_t *query, qboolean success, void *customp )
{
	static char simpName[MAX_NAME_CHARS];
	rs_authplayer_t *player = ( rs_authplayer_t* )customp;
	cJSON *data;
	int playerNum;

	// Did they disconnect?
	playerNum = (int)( player->client - game.clients );
	if( playerNum < 0 || playerNum >= gs.maxclients )
		return;

	if( rs_sqapi->GetStatus( query ) != 200 )
	{
		// Query failed, give them benefit of the doubt
		player->failTime = 0;
		player->nickStatus = QSTATUS_SUCCESS;
		return;
	}

	data = ((cJSON*)rs_sqapi->GetRoot( query ))->child;

	// Did they change name before the query returned?
	Q_strncpyz( simpName, COM_RemoveColorTokens( player->client->netname ), sizeof( simpName ) );
	if( Q_stricmp( simpName, data->string ) )
	{
		RS_PlayerUserinfoChanged( player, NULL );
		return;
	}

	// Update players latest nick
	Q_strncpyz( player->last, simpName, sizeof( player->last ) );

	// Is it protected?
	if( data->type == cJSON_True )
	{
		player->failTime = game.realtime + ( 1000 * RS_NICK_TIMEOUT );
		player->thinkTime = game.realtime;
		player->nickStatus = QSTATUS_SUCCESS;
		return;
	}

	// Not protected, deactivate the timer
	player->failTime = 0;
	player->thinkTime = 0;
	player->nickStatus = QSTATUS_SUCCESS;
}

/**
 * Validate a given nickname
 * @param player The player to check nickstatus for
 * @param nick   The simplified nickname to check
 */
void RS_AuthNick( rs_authplayer_t *player, char *nick )
{
	stat_query_t *query;
	char *b64name;

	if( !rs_statsEnabled->integer )
		return;

	// If a player changes nick while a query is inprogress
	// The callback will start another query when finished
	if( player->nickStatus == QSTATUS_PENDING )
		return;

	b64name = (char*)base64_encode( (unsigned char *)nick, strlen( nick ), NULL );
	query = rs_sqapi->CreateQuery( va( "api/nick/%s", b64name ), qtrue );
	free( b64name );

	RS_SignQuery( query, (int)time( NULL ) );
	rs_sqapi->SetCallback( query, RS_AuthNick_Done, (void*)player );
	rs_sqapi->Send( query );
	player->nickStatus = QSTATUS_PENDING;
	query = NULL;
}

/**
 * AuthMap callback function
 * @param query Query calling this function
 * @param success True on any response
 * @param customp Extra parameters, should be NULL
 * @return void
 */
void RS_AuthMap_Done( stat_query_t *query, qboolean success, void *customp )
{
	cJSON *data, *node;

	if( rs_sqapi->GetStatus( query ) != 200 )
	{
		G_Printf( "%sError:%s Failed to query map.\nDisabling statistics reporting.", 
					S_COLOR_RED, S_COLOR_WHITE );
		trap_Cvar_ForceSet( rs_statsEnabled->name, "0" );
		return;
	}

	// We assume the response is properly formed
	data = (cJSON*)rs_sqapi->GetRoot( query );
	authmap.id = cJSON_GetObjectItem( data, "id" )->valueint;
	G_Printf( "Map id: %d\n", authmap.id );

	// Check for a world record
	node = cJSON_GetObjectItem( data, "record" );
	if( node->type != cJSON_Object )
		return;

	G_Gametype_ScoreEvent( NULL, "rs_loadmap", RS_ParseRace( node ) );
}

/**
 * Get map id and world record
 * @return void
 */
void RS_AuthMap( void )
{
	stat_query_t *query;

	if( !rs_statsEnabled->integer )
		return;

	// Form the query
	query = rs_sqapi->CreateQuery( va( "api/map/%s", authmap.b64name ), qtrue );
	rs_sqapi->SetCallback( query, RS_AuthMap_Done, NULL );

	RS_SignQuery( query, (int)time( NULL ) );
	rs_sqapi->Send( query );
	query = NULL;
}

/**
 * Callback for report race
 * @return void
 */
void RS_ReportRace_Done( stat_query_t *query, qboolean success, void *customp )
{
}

/**
 * Report a race to the database
 * @param player      Player who made the record
 * @param rtime       Time of the race
 * @param cp          Checkpoints of the race
 * @param cpNum       Number of checkpoints
 */
void RS_ReportRace( rs_authplayer_t *player, int rtime, int *cp, int cpNum )
{
	stat_query_t *query;
	int i;

	if( !rs_statsEnabled->integer )
		return;

	// Use cJSON to format the checkpoint array
	cJSON *arr = cJSON_CreateArray();
	for( i = 0; i < cpNum; i++ )
		cJSON_AddItemToArray( arr, cJSON_CreateNumber( cp[i] ) );

	// Form the query
	query = rs_sqapi->CreateQuery( "api/race/", qfalse );
	rs_sqapi->SetField( query, "pid", va( "%d", player->id ) );
	rs_sqapi->SetField( query, "mid", va( "%d", authmap.id ) );
	rs_sqapi->SetField( query, "time", va( "%d", rtime ) );
	rs_sqapi->SetField( query, "checkpoints", cJSON_Print( arr ) );

	RS_SignQuery( query, (int)time( NULL ) );
	rs_sqapi->SetCallback( query, RS_ReportRace_Done, (void*)player );
	rs_sqapi->Send( query );
	query = NULL;
}

/**
 * Report Map data
 * @param tags Space separated list of tags to add to the map
 */
void RS_ReportMap( const char *tags )
{
	char tagset[1024], *token, *b64tags;
	stat_query_t *query;
	cJSON *arr = cJSON_CreateArray();

	if( !rs_statsEnabled->integer )
		return;

	// Make the taglist
	Q_strncpyz( tagset, ( tags ? tags : "" ), sizeof( tagset ) );
	token = strtok( tagset, " " );
	while( token != NULL )
	{
		cJSON_AddItemToArray( arr, cJSON_CreateString( token ) );
		token = strtok( NULL, " " );
	}
	token = cJSON_Print( arr );
	b64tags = (char*)base64_encode( (unsigned char *)token, strlen( token ), NULL );

	// Form the query
	query = rs_sqapi->CreateQuery( va( "api/map/%s", authmap.b64name ), qfalse );
	rs_sqapi->SetField( query, "playTime", va( "%d", authmap.playTime ) );
	rs_sqapi->SetField( query, "races", va( "%d", authmap.races ) );
	rs_sqapi->SetField( query, "tags", b64tags );

	// Reset the fields
	authmap.playTime = 0;
	authmap.races = 0;

	RS_SignQuery( query, (int)time( NULL ) );
	rs_sqapi->Send( query );
	query = NULL;
}

/**
 * Report player statistics to the database
 * @param player The player to report
 */
void RS_ReportPlayer( rs_authplayer_t *player )
{
	stat_query_t *query;
	char *b64name, url[MAX_STRING_CHARS];

	if( !rs_statsEnabled->integer )
		return;

	// Not authenticated
	if( !player->id )
		return;

	// Make the URL
	b64name = (char*)base64_encode( (unsigned char *)player->name, strlen( player->name ), NULL );
	Q_strncpyz( url, "api/player/", sizeof( url ) - 1 );
	Q_strncatz( url, b64name, sizeof( url ) - 1 );
	free( b64name );

	// Form the query
	query = rs_sqapi->CreateQuery( url, qfalse );
	rs_sqapi->SetField( query, "mid", va( "%d", authmap.id ) );
	rs_sqapi->SetField( query, "playTime", va( "%d", player->playTime ) );
	rs_sqapi->SetField( query, "races", va( "%d", player->races ) );

	// reset the fields
	player->playTime = 0;
	player->races = 0;

	RS_SignQuery( query, (int)time( NULL ) );
	rs_sqapi->Send( query );
	query = NULL;
}

/**
 * Callback for top
 * @return void
 */
void RS_QueryTop_Done( stat_query_t *query, qboolean success, void *customp )
{
	int count, playerNum, i;
	rs_racetime_t top, racetime, timediff;
	cJSON *data, *node;
	char *mapname;

	gclient_t *client = (gclient_t *)customp;
	playerNum = (int)( client - game.clients );

	if( playerNum < 0 || playerNum >= gs.maxclients )
		return;

	if( rs_sqapi->GetStatus( query ) != 200 )
	{
		G_PrintMsg( &game.edicts[ playerNum + 1 ], "%sError:%s Top query failed\n", 
					S_COLOR_RED, S_COLOR_WHITE );
		return;
	}

	// We assume the response is properly formed
	data = (cJSON*)rs_sqapi->GetRoot( query );
	count = cJSON_GetObjectItem( data, "count" )->valueint;
	mapname = cJSON_GetObjectItem( data, "map" )->valuestring;

	G_PrintMsg( &game.edicts[ playerNum + 1 ], "%sTop %d times on map %s\n",
	 			S_COLOR_ORANGE, count, mapname );

	node = cJSON_GetObjectItem( data, "races" )->child;
	if( node )
		RS_Racetime( cJSON_GetObjectItem( node, "time" )->valueint, &top );

	for( i = 0; node != NULL; i++, node=node->next )
	{
		// Calculate the racetime and difftime from top for each record
		RS_Racetime( cJSON_GetObjectItem( node, "time" )->valueint, &racetime );
		RS_Racetime( racetime.timedelta - top.timedelta, &timediff );

		// Print the row
		G_PrintMsg( &game.edicts[ playerNum + 1 ], "%s%d. %s%02d:%02d.%02d %s+[%02d:%02d.%02d] %s%s %s%s %s\n",
			S_COLOR_WHITE, i + 1,
			S_COLOR_GREEN, ( racetime.hour * 60 ) + racetime.min, racetime.sec, racetime.milli / 10,
			( top.timedelta == racetime.timedelta ? S_COLOR_YELLOW : S_COLOR_RED ), 
			( timediff.hour * 60 ) + timediff.min, timediff.sec, timediff.milli / 10,
			S_COLOR_WHITE, cJSON_GetObjectItem( node, "playerName" )->valuestring,
			S_COLOR_WHITE, cJSON_GetObjectItem( node, "created" )->valuestring,
			( i == 0 ? cJSON_GetObjectItem( data, "oneliner" )->valuestring : "" ) );
	}
}


void RS_QueryTop( gclient_t *client, const char* mapname, int limit )
{
	stat_query_t *query;
	char *b64name = (char*)base64_encode( (unsigned char *)mapname, strlen( mapname ), NULL );
	int	playerNum = (int)( client - game.clients );

	if( !rs_statsEnabled->integer )
	{
		G_PrintMsg( &game.edicts[playerNum +1], "%sError:%s No database connected\n", 
					S_COLOR_RED, S_COLOR_WHITE );
		return;
	}

	// Form the query
	query = rs_sqapi->CreateQuery( "api/race/", qtrue );
	rs_sqapi->SetField( query, "map", b64name );
	rs_sqapi->SetField( query, "limit", va( "%d", limit ) );

	RS_SignQuery( query, (int)time( NULL ) );
	rs_sqapi->SetCallback( query, RS_QueryTop_Done, (void*)client );
	rs_sqapi->Send( query );
	free( b64name );
	query = NULL;
}

void RS_QueryMaps_Done( stat_query_t *query, qboolean success, void *customp )
{
	edict_t *ent;
	int playerNum, start, i, j;
	cJSON *data, *node, *tag;

	gclient_t *client = (gclient_t *)customp;
	playerNum = (int)( client - game.clients );

	if( playerNum < 0 || playerNum >= gs.maxclients )
		return;

	ent = &game.edicts[ playerNum + 1 ];

	if( rs_sqapi->GetStatus( query ) != 200 )
	{
		G_PrintMsg( ent, "%sError:%s Maplist query failed\n", 
					S_COLOR_RED, S_COLOR_WHITE );
		return;
	}

	// We assume the response is properly formed
	data = (cJSON*)rs_sqapi->GetRoot( query );
	start = cJSON_GetObjectItem( data, "start" )->valueint;

	node = cJSON_GetObjectItem( data, "maps" )->child;
	for( i = 0; node != NULL; i++, node=node->next )
	{
		// Print the row
		G_PrintMsg( ent, "%s# %d%s: %-25s ",
			S_COLOR_ORANGE, start + i + 1, S_COLOR_WHITE,
			cJSON_GetObjectItem( node, "name" )->valuestring );

		tag = cJSON_GetObjectItem( node, "tags" )->child;
		for( j = 0; tag != NULL; j++, tag=tag->next )
		{
			G_PrintMsg( ent, "%s%s", ( j == 0 ? "" : ", " ), tag->valuestring );
		}
		G_PrintMsg( ent, "\n" );
	}
}

void RS_QueryMaps( gclient_t *client, const char *pattern, const char *tags, int page )
{
	stat_query_t *query;
	char tagset[1024], *token, *b64tags, *b64pattern;
	cJSON *arr = cJSON_CreateArray();
	int	playerNum = (int)( client - game.clients );

	if( !rs_statsEnabled->integer )
	{
		G_PrintMsg( &game.edicts[playerNum +1], "%sError:%s No database connected\n", 
					S_COLOR_RED, S_COLOR_WHITE );
		return;
	}

	// Make the pattern
	b64pattern = (char*)base64_encode( (unsigned char *)pattern, strlen( pattern ), NULL );
	// Make the taglist
	Q_strncpyz( tagset, tags, sizeof( tagset ) );
	token = strtok( tagset, " " );
	while( token != NULL )
	{
		cJSON_AddItemToArray( arr, cJSON_CreateString( token ) );
		token = strtok( NULL, " " );
	}
	token = cJSON_Print( arr );
	b64tags = (char*)base64_encode( (unsigned char *)token, strlen( token ), NULL );

	// which page to display?
	page = page == 0 ? 0 : page - 1;

	// Form the query
	query = rs_sqapi->CreateQuery( "api/map/", qtrue );
	rs_sqapi->SetField( query, "pattern", b64pattern );
	rs_sqapi->SetField( query, "tags", b64tags );
	rs_sqapi->SetField( query, "start", va( "%d", page * RS_MAPLIST_ITEMS ) );
	rs_sqapi->SetField( query, "limit", va( "%d", RS_MAPLIST_ITEMS ) );

	RS_SignQuery( query, (int)time( NULL ) );
	rs_sqapi->SetCallback( query, RS_QueryMaps_Done, (void*)client );
	rs_sqapi->Send( query );
	free( b64pattern );
	free( b64tags );
	query = NULL;
}

void RS_QueryRandmap_Done( stat_query_t *query, qboolean success, void *customp )
{
	static char mapname[MAX_STRING_CHARS];
	char **votedata = (char**)customp;
	cJSON *data = (cJSON*)rs_sqapi->GetRoot( query );

	// invalid response?
	if( !data || rs_sqapi->GetStatus( query ) != 200 )
	{
		G_PrintMsg( NULL, "Invalid map picked, vote canceled\n" );
		G_CallVotes_Reset();
		return;
	}

	// Did we get any results?
	int count = cJSON_GetObjectItem( data, "count" )->valueint;
	if( !count )
	{
		G_PrintMsg( NULL, "Invalid map picked, vote canceled\n" );
		G_CallVotes_Reset();
		return;
	}

	// copy the mapname
	cJSON *map = cJSON_GetObjectItem( cJSON_GetObjectItem( data, "maps" )->child, "name" );
	Q_strncpyz( mapname, map->valuestring, sizeof( mapname ) );

	// do we have the map or is it the current map?
	if( !trap_ML_FilenameExists( mapname ) || !Q_stricmp( mapname, level.mapname ) )
	{
		G_PrintMsg( NULL, "Invalid map picked, vote canceled\n" );
		G_CallVotes_Reset();
		return;
	}

	// Save the mapname
	*votedata = mapname;
	G_PrintMsg( NULL, "Randmap picked: %s\n", mapname );
}

void RS_QueryRandmap( char* tags[], void *data )
{
	stat_query_t *query;
	char *b64tags;
	cJSON *arr = cJSON_CreateArray();

	// Format the tags
	while( *tags )
		cJSON_AddItemToArray( arr, cJSON_CreateString( *tags++ ) );
	b64tags = cJSON_Print( arr );
	b64tags = (char*)base64_encode( (unsigned char *)b64tags, strlen( b64tags ), NULL );

	// Form the query
	query = rs_sqapi->CreateQuery( "api/map/", qtrue );
	rs_sqapi->SetField( query, "pattern", "" );
	rs_sqapi->SetField( query, "tags", b64tags );
	rs_sqapi->SetField( query, "rand", "1" );
	
	RS_SignQuery( query, (int)time( NULL ) );
	rs_sqapi->SetCallback( query, RS_QueryRandmap_Done, data );
	rs_sqapi->Send( query );
	query = NULL;
	free( b64tags );
}
