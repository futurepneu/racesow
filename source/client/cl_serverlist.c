/*
Copyright (C) 1997-2001 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/
// cl_serverlist.c  -- interactuates with the master server

#include "client.h"
#include "../qalgo/q_trie.h"

#define MAX_MASTER_SERVERS					4

#ifdef PUBLIC_BUILD
#define SERVERBROWSER_PROTOCOL_VERSION		APP_PROTOCOL_VERSION
#else
#define SERVERBROWSER_PROTOCOL_VERSION		APP_PROTOCOL_VERSION
//#define SERVERBROWSER_PROTOCOL_VERSION	12
#endif

//=========================================================

typedef struct serverlist_s
{
	char address[48];
	unsigned int pingTimeStamp;
	unsigned int lastValidPing;
	unsigned int lastUpdatedByMasterServer;
	unsigned int masterServerUpdateSeq;
	struct serverlist_s *pnext;
} serverlist_t;

serverlist_t *masterList, *favoritesList;

// cache of resolved master server addresses/names
static trie_t *serverlist_masters_trie = NULL;

static qboolean filter_allow_full = qfalse;
static qboolean filter_allow_empty = qfalse;

static unsigned int masterServerUpdateSeq;

static unsigned int localQueryTimeStamp = 0;

static qthread_t **resolverThreads;
static qmutex_t *resolveLock;

//=========================================================

/*
* CL_FreeServerlist
*/
static void CL_FreeServerlist( serverlist_t **serversList )
{
	serverlist_t *ptr;

	while( *serversList )
	{
		ptr = *serversList;
		*serversList = ptr->pnext;
		Mem_ZoneFree( ptr );
	}
}

/*
* CL_ServerIsInList
*/
static serverlist_t *CL_ServerFindInList( serverlist_t *serversList, char *adr )
{

	serverlist_t *server;

	server = serversList;
	while( server )
	{
		if( !Q_stricmp( server->address, adr ) )
			return server;
		server = server->pnext;
	}

	return NULL;
}

/*
* CL_AddServerToList
*/
static qboolean CL_AddServerToList( serverlist_t **serversList, char *adr, unsigned int days )
{
	serverlist_t *newserv;
	netadr_t nadr;

	if( !adr || !strlen( adr ) )
		return qfalse;

	if( !NET_StringToAddress( adr, &nadr ) )
		return qfalse;

	newserv = CL_ServerFindInList( *serversList, adr );
	if( newserv ) {
		// ignore excessive updates for about a second or so, which may happen
		// when we're querying multiple master servers at once
		if( !newserv->masterServerUpdateSeq ||
			newserv->lastUpdatedByMasterServer + 1000 < Sys_Milliseconds() ) {
			newserv->lastUpdatedByMasterServer = Sys_Milliseconds();
			newserv->masterServerUpdateSeq = masterServerUpdateSeq;
		}
		return qfalse;
	}

	newserv = (serverlist_t *)Mem_ZoneMalloc( sizeof( serverlist_t ) );
	Q_strncpyz( newserv->address, adr, sizeof( newserv->address ) );
	newserv->pingTimeStamp = 0;
	if( days == 0 )
		newserv->lastValidPing = Com_DaysSince1900();
	else
		newserv->lastValidPing = days;
	newserv->lastUpdatedByMasterServer = Sys_Milliseconds();
	newserv->masterServerUpdateSeq = masterServerUpdateSeq;
	newserv->pnext = *serversList;
	*serversList = newserv;

	return qtrue;
}

#define SERVERSFILE "serverscache.txt"
/*
* CL_WriteServerCache
*/
void CL_WriteServerCache( void )
{
	serverlist_t *server;
	int filehandle;
	char str[256];
	netadr_t adr;

	if( FS_FOpenFile( SERVERSFILE, &filehandle, FS_WRITE ) == -1 )
	{
		Com_Printf( "CL_WriteServerList: Couldn't create the cache file\n" );
		return;
	}

	Q_snprintfz( str, sizeof( str ), "// servers cache file generated by %s. Do not modify\n", APPLICATION );
	FS_Print( filehandle, str );

	FS_Print( filehandle, "master\n" );
	server = masterList;
	while( server )
	{
		if( server->lastValidPing + 7 > Com_DaysSince1900() )
		{
			if( NET_StringToAddress( server->address, &adr ) )
			{
				Q_snprintfz( str, sizeof( str ), "%s %i\n", server->address, (int)server->lastValidPing );
				FS_Print( filehandle, str );
			}
		}

		server = server->pnext;
	}

	FS_Print( filehandle, "favorites\n" );
	server = favoritesList;
	while( server )
	{
		if( server->lastValidPing + 7 > Com_DaysSince1900() )
		{
			if( NET_StringToAddress( server->address, &adr ) )
			{
				Q_snprintfz( str, sizeof( str ), "%s %i\n", server->address, (int)server->lastValidPing );
				FS_Print( filehandle, str );
			}
		}

		server = server->pnext;
	}

	FS_FCloseFile( filehandle );
}

/*
* CL_ReadServerCache
*/
void CL_ReadServerCache( void )
{
	int filelen, filehandle;
	qbyte *buf = NULL;
	char *ptr, *token;
	netadr_t adr;
	char adrString[64];
	qboolean favorite = qfalse;

	filelen = FS_FOpenFile( SERVERSFILE, &filehandle, FS_READ );
	if( !filehandle || filelen < 1 )
	{
		FS_FCloseFile( filehandle );
	}
	else
	{
		buf = Mem_TempMalloc( filelen + 1 );
		filelen = FS_Read( buf, filelen, filehandle );
		FS_FCloseFile( filehandle );
	}

	if( !buf )
		return;

	ptr = ( char * )buf;
	while( ptr )
	{
		token = COM_ParseExt( &ptr, qtrue );
		if( !token[0] )
			break;

		if( !Q_stricmp( token, "master" ) )
		{
			favorite = qfalse;
			continue;
		}

		if( !Q_stricmp( token, "favorites" ) )
		{
			favorite = qtrue;
			continue;
		}

		if( NET_StringToAddress( token, &adr ) )
		{
			Q_strncpyz( adrString, token, sizeof( adrString ) );
			token = COM_ParseExt( &ptr, qfalse );
			if( !token[0] )
				continue;

			if( favorite )
				CL_AddServerToList( &favoritesList, adrString, (unsigned int)atoi( token ) );
			else
				CL_AddServerToList( &masterList, adrString, (unsigned int)atoi( token ) );
		}
	}

	Mem_TempFree( buf );
}

/*
* CL_ParseGetInfoResponse
*
* Handle a server responding to a detailed info broadcast
*/
void CL_ParseGetInfoResponse( const socket_t *socket, const netadr_t *address, msg_t *msg )
{
	char *s = MSG_ReadString( msg );
	Com_DPrintf( "%s\n", s );
}

/*
* CL_ParseGetStatusResponse
*
* Handle a server responding to a detailed info broadcast
*/
void CL_ParseGetStatusResponse( const socket_t *socket, const netadr_t *address, msg_t *msg )
{
	char *s = MSG_ReadString( msg );
	Com_DPrintf( "%s\n", s );
}


/*
* CL_QueryGetInfoMessage
*/
static void CL_QueryGetInfoMessage( const char *cmdname )
{
	netadr_t adr;
	char *requeststring;
	char *server;

	//get what master
	server = Cmd_Argv( 1 );
	if( !server || !( *server ) )
	{
		Com_Printf( "%s: no address provided %s...\n", Cmd_Argv( 0 ), server ? server : "" );
		return;
	}

	requeststring = va( cmdname );

	// send a broadcast packet
	Com_DPrintf( "quering %s...\n", server );

	if( NET_StringToAddress( server, &adr ) )
	{
		socket_t *socket;

		if( NET_GetAddressPort( &adr ) == 0 )
			NET_SetAddressPort( &adr, PORT_SERVER );

		socket = ( adr.type == NA_IP6 ? &cls.socket_udp6 : &cls.socket_udp );
		Netchan_OutOfBandPrint( socket, &adr, requeststring );
	}
	else
	{
		Com_Printf( "Bad address: %s\n", server );
	}
}


/*
* CL_QueryGetInfoMessage_f - getinfo 83.97.146.17:27911
*/
void CL_QueryGetInfoMessage_f( void )
{
	CL_QueryGetInfoMessage( "getinfo" );
}


/*
* CL_QueryGetStatusMessage_f - getstatus 83.97.146.17:27911
*/
void CL_QueryGetStatusMessage_f( void )
{
	CL_QueryGetInfoMessage( "getstatus" );
}

/*
* CL_PingServer_f
*/
void CL_PingServer_f( void )
{
	char *address_string;
	char requestString[64];
	netadr_t adr;
	serverlist_t *pingserver;
	socket_t *socket;

	if( Cmd_Argc() < 2 )
		Com_Printf( "Usage: pingserver [ip:port]\n" );

	address_string = Cmd_Argv( 1 );

	if( !NET_StringToAddress( address_string, &adr ) )
		return;

	pingserver = CL_ServerFindInList( masterList, address_string );
	if( !pingserver )
		pingserver = CL_ServerFindInList( favoritesList, address_string );
	if( !pingserver )
		return;

	// never request a second ping while awaiting for a ping reply
	if( pingserver->pingTimeStamp + SERVER_PINGING_TIMEOUT > Sys_Milliseconds() )
		return;

	pingserver->pingTimeStamp = Sys_Milliseconds();

	Q_snprintfz( requestString, sizeof( requestString ), "info %i %s %s", SERVERBROWSER_PROTOCOL_VERSION,
		filter_allow_full ? "full" : "",
		filter_allow_empty ? "empty" : "" );

	socket = ( adr.type == NA_IP6 ? &cls.socket_udp6 : &cls.socket_udp );
	Netchan_OutOfBandPrint( socket, &adr, requestString );
}

/*
* CL_ParseStatusMessage
* Handle a reply from a ping
*/
void CL_ParseStatusMessage( const socket_t *socket, const netadr_t *address, msg_t *msg )
{
	char *s = MSG_ReadString( msg );
	serverlist_t *pingserver;
	char adrString[64];

	Com_DPrintf( "%s\n", s );

	Q_strncpyz( adrString, NET_AddressToString( address ), sizeof( adrString ) );

	// ping response
	pingserver = CL_ServerFindInList( masterList, adrString );
	if( !pingserver )
		pingserver = CL_ServerFindInList( favoritesList, adrString );

	if( pingserver && pingserver->pingTimeStamp ) // valid ping
	{
		unsigned int ping = Sys_Milliseconds() - pingserver->pingTimeStamp;
		CL_UIModule_AddToServerList( adrString, va( "\\\\ping\\\\%i%s", ping, s ) );
		pingserver->pingTimeStamp = 0;
		pingserver->lastValidPing = Com_DaysSince1900();
		return;
	}

	// assume LAN response
	if( NET_IsLANAddress( address ) && ( localQueryTimeStamp + LAN_SERVER_PINGING_TIMEOUT > Sys_Milliseconds() ) ) {
		unsigned int ping = Sys_Milliseconds() - localQueryTimeStamp;
		CL_UIModule_AddToServerList( adrString, va( "\\\\ping\\\\%i%s", ping, s ) );
		return;
	}

	// add the server info, but ignore the ping, cause it's not valid
	CL_UIModule_AddToServerList( adrString, s );
}

/*
* CL_ParseGetServersResponseMessage
* Handle a reply from getservers message to master server
*/
static void CL_ParseGetServersResponseMessage( msg_t *msg, qboolean extended )
{
	const char *header;
	char adrString[64];
	qbyte addr[16];
	unsigned short port;
	netadr_t adr;

	MSG_BeginReading( msg );
	MSG_ReadLong( msg ); // skip the -1

	//jump over the command name
	header = ( extended ? "getserversExtResponse" : "getserversResponse" );
	if( !MSG_SkipData( msg, strlen( header ) ) )
	{
		Com_Printf( "Invalid master packet ( missing %s )\n", header );
		return;
	}

	while( msg->readcount + 7 <= msg->cursize )
	{
		char prefix = MSG_ReadChar( msg );

		switch( prefix )
		{
		case '\\':
			MSG_ReadData( msg, addr, 4 );
			port = ShortSwap( MSG_ReadShort( msg ) ); // both endians need this swapped.
			Q_snprintfz( adrString, sizeof( adrString ), "%u.%u.%u.%u:%u", addr[0], addr[1], addr[2], addr[3], port );
			break;

		case '/':
			if( extended )
			{
				MSG_ReadData( msg, addr, 16 );
				port = ShortSwap( MSG_ReadShort( msg ) ); // both endians need this swapped.
				Q_snprintfz( adrString, sizeof( adrString ), "[%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x]:%hu",
								addr[ 0], addr[ 1], addr[ 2], addr[ 3], addr[ 4], addr[ 5], addr[ 6], addr[ 7],
								addr[ 8], addr[ 9], addr[10], addr[11], addr[12], addr[13], addr[14], addr[15],
								port );
			}
			else
			{
				Com_Printf( "Invalid master packet ( IPv6 prefix in a non-extended response )\n" );
				return;
			}

			break;

		default:
			Com_Printf( "Invalid master packet ( missing separator )\n" );
			return;
		}

		if( port == 0 )  // last server seen
			return;

		Com_DPrintf( "%s\n", adrString );
		if( !NET_StringToAddress( adrString, &adr ) )
		{
			Com_Printf( "Bad address: %s\n", adrString );
			continue;
		}

		CL_AddServerToList( &masterList, adrString, 0 );
	}
}

/*
* CL_ParseGetServersResponse
* Handle a reply from getservers message to master server
*/
void CL_ParseGetServersResponse( const socket_t *socket, const netadr_t *address, msg_t *msg, qboolean extended )
{
	serverlist_t *server;
	netadr_t adr;

//	CL_ReadServerCache();

	// add the new server addresses to the local addresses list
	masterServerUpdateSeq++;
	if( !masterServerUpdateSeq ) {
		// wrapped
		masterServerUpdateSeq = 1;
	}
	CL_ParseGetServersResponseMessage( msg, extended );

//	CL_WriteServerCache();

	// dump servers we just received an update on from the master server
	server = masterList;
	while( server )
	{
		if( server->masterServerUpdateSeq == masterServerUpdateSeq 
			&& NET_StringToAddress( server->address, &adr ) )
			CL_UIModule_AddToServerList( server->address, "\\\\EOT" );

		server = server->pnext;
	}
}

/*
* CL_ResolveMasterAddress
*/
static void CL_ResolveMasterAddress( const char *master )
{
	netadr_t adr, *padr;

	if( master && *master ) {
		NET_StringToAddress( master, &adr );

		QMutex_Lock( resolveLock );

		padr = ( netadr_t * )malloc( sizeof( adr ) );
		*padr = adr;

		Trie_Insert( serverlist_masters_trie, master, padr );

		QMutex_Unlock( resolveLock );
	}
}

/*
* CL_MasterResolverThreadEntry
*/
static void *CL_MasterResolverThreadEntry( void *pmlist )
{
	CL_ResolveMasterAddress( ( char * )pmlist );
	free( pmlist );
	return NULL;
}

/*
* CL_MasterAddressCache_Init
*/
static void CL_MasterAddressCache_Init( void )
{
	int numMasters;
	const char *ptr;
	const char *master;
	const char *masterservers;

	Trie_Create( TRIE_CASE_INSENSITIVE, &serverlist_masters_trie );

	resolverThreads = NULL;

	masterservers = Cvar_String( "masterservers" );
	if( !*masterservers ) {
		return;
	}

	// count the number of master servers
	numMasters = 0;
	for( ptr = masterservers; ptr; ) {
		master = COM_Parse( &ptr );
		if( !*master ) {
			break;
		}
		numMasters++;
	}

	// don't allow too many as each will spawn its own resolver thread
	if( numMasters > MAX_MASTER_SERVERS )
		numMasters = MAX_MASTER_SERVERS;

	resolveLock = QMutex_Create();
	if( resolveLock != NULL )
	{
		unsigned numResolverThreads;

		numResolverThreads = 0;
		resolverThreads = malloc( sizeof( *resolverThreads ) * (numMasters+1) );
		memset( resolverThreads, 0, sizeof( *resolverThreads ) * (numMasters+1) );

		for( ptr = masterservers; ptr; )
		{
			char *master_copy;
			qthread_t *thread;

			master = COM_Parse( &ptr );
			if( !*master )
				break;

			master_copy = ( char * )malloc( strlen( master ) + 1 );
			memcpy( master_copy, master, strlen( master ) + 1 );

			thread = QThread_Create( CL_MasterResolverThreadEntry, ( void * )master_copy );
			if( thread != NULL ) {
				resolverThreads[numResolverThreads++] = thread;
				continue;
			}

			// we shouldn't get here if all goes well with the resolving thread
			free( master_copy );
		}
	}
}

/*
* CL_MasterAddressCache_Shutdown
*/
static void CL_MasterAddressCache_Shutdown( void )
{
	unsigned i;
	trie_dump_t *dump;

	if( resolverThreads ) {
		for( i = 0; resolverThreads[i]; i++ ) {
			QThread_Join( resolverThreads[i] );
		}
		free( resolverThreads );
		resolverThreads = NULL;
	}

	QMutex_Destroy( &resolveLock );

	// free allocated memory
	if( serverlist_masters_trie ) {
		Trie_Dump( serverlist_masters_trie, "", TRIE_DUMP_BOTH, &dump );
		for( i = 0; i < dump->size; ++i ) {
			free( dump->key_value_vector[i].value );
		}
		Trie_Destroy( serverlist_masters_trie );
	}
}

/*
* CL_GetServers_f
*/
void CL_GetServers_f( void )
{
	netadr_t adr, *padr;
	char *requeststring;
	int i;
	char *modname, *master;
	trie_error_t err;

	filter_allow_full = qfalse;
	filter_allow_empty = qfalse;
	for( i = 2; i < Cmd_Argc(); i++ )
	{
		if( !Q_stricmp( "full", Cmd_Argv( i ) ) )
			filter_allow_full = qtrue;

		if( !Q_stricmp( "empty", Cmd_Argv( i ) ) )
			filter_allow_empty = qtrue;
	}

	if( !Q_stricmp( Cmd_Argv( 1 ), "local" ) )
	{
		if( localQueryTimeStamp + LAN_SERVER_PINGING_TIMEOUT > Sys_Milliseconds() ) {
			return;
		}

		padr = &adr;
		localQueryTimeStamp = Sys_Milliseconds();

		// send a broadcast packet
		Com_DPrintf( "pinging broadcast...\n" );

		// erm... modname isn't sent in local queries?

		requeststring = va( "info %i %s %s", SERVERBROWSER_PROTOCOL_VERSION,
			filter_allow_full ? "full" : "",
			filter_allow_empty ? "empty" : "" );

		for( i = 0; i < NUM_BROADCAST_PORTS; i++ )
		{
			NET_BroadcastAddress( padr, PORT_SERVER + i );
			Netchan_OutOfBandPrint( &cls.socket_udp, padr, requeststring );
		}
		return;
	}

	//get what master
	master = Cmd_Argv( 2 );
	if( !master || !( *master ) )
		return;

	modname = Cmd_Argv( 3 );
	// never allow anyone to use DEFAULT_BASEGAME as mod name
	if( !modname || !modname[0] || !Q_stricmp( modname, DEFAULT_BASEGAME ) )
		modname = APPLICATION;

	assert( modname[0] );

	// check memory cache
	QMutex_Lock( resolveLock );
	err = Trie_Find( serverlist_masters_trie, master, TRIE_EXACT_MATCH, (void **)&padr );
	QMutex_Unlock( resolveLock );

	if( err == TRIE_OK && ( padr->type == NA_IP || padr->type == NA_IP6 ) )
	{
		const char *cmdname;
		socket_t *socket;

		if ( padr->type == NA_IP )
		{
			cmdname = "getservers";
			socket = &cls.socket_udp;
		}
		else
		{
			cmdname = "getserversExt";
			socket = &cls.socket_udp6;
		}

		// create the message
		requeststring = va( "%s %c%s %i %s %s", cmdname, toupper( modname[0] ), modname+1, SERVERBROWSER_PROTOCOL_VERSION,
			filter_allow_full ? "full" : "",
			filter_allow_empty ? "empty" : "" );

		if( NET_GetAddressPort( padr ) == 0 )
			NET_SetAddressPort( padr, PORT_MASTER );

		Netchan_OutOfBandPrint( socket, padr, requeststring );

		Com_DPrintf( "quering %s...%s: %s\n", master, NET_AddressToString(padr), requeststring );
	}
	else
	{
		Com_Printf( "Bad address: %s\n", master );
	}
}

/*
* CL_InitServerList
*/
void CL_InitServerList( void )
{
	CL_FreeServerlist( &masterList );
	CL_FreeServerlist( &favoritesList );

//	CL_ReadServerCache();

	CL_MasterAddressCache_Init();
}

/*
* CL_ShutDownServerList
*/
void CL_ShutDownServerList( void )
{
//	CL_WriteServerCache();

	CL_FreeServerlist( &masterList );
	CL_FreeServerlist( &favoritesList );

	CL_MasterAddressCache_Shutdown();
}
