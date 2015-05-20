#define MAX_AUTH_CHARS 64		/**< Maximum length of auth name */
#define RS_NICK_TIMEOUT 20		/**< Proctected nick timer length */

typedef enum
{
	QSTATUS_NONE,				/**< no query was sent */
	QSTATUS_FAILED,				/**< query returned in failure */
	QSTATUS_PENDING,			/**< query in progress */
	QSTATUS_SUCCESS				/**< query succeded */
} qstatus_t;

typedef struct rs_authmap_s
{
	char *b64name;				/**< Base64 encoded name of the map */
	int id;						/**< database id of the map */
	int races;					/**< number of races finished since last map report */
	unsigned int playMillis;	/**< milliseconds carry for playTime */
	unsigned long int playTime;	/**< time playing the map in seconds */
} rs_authmap_t;

typedef struct rs_authplayer_s
{
	gclient_t *client;			/**< client for the player */
	qstatus_t status;			/**< status of the inprogress query */
	qstatus_t nickStatus;		/**< status of the nickname query */
	char login[MAX_INFO_VALUE];	/**< mm login name of the player */
	char nick[MAX_NAME_CHARS];	/**< registered ingame nickname for user */
	char last[MAX_NAME_CHARS];	/**< The last nickname the player tried to use */
	bool admin;					/**< has admin privleges */
	int id;						/**< database id for player, 0 for unauthenticated */
	int mapRaces;				/**< number of races finished since last race report */
	int playRaces;				/**< number of races finished since last player report*/
	unsigned int failTime;		/**< realtime to rename the player */
	unsigned int thinkTime;		/**< realtime for next protectednick think */
	unsigned int playMillis;	/**< milliseconds carry for playTime */
	unsigned int mapMillis;		/**< milliseconds carry for mapTime */
	unsigned long int playTime;	/**< time spent playing overall in seconds */
	unsigned long int mapTime;	/**< time spent playing on the map in seconds */
} rs_authplayer_t;

extern rs_authmap_t authmap;
extern rs_authplayer_t *authplayers;

void RS_InitAuth( void );
void RS_ShutdownAuth( void );
void RS_ThinkAuth( void );
bool RS_SetName( gclient_t *client, const char *name );
void RS_Playtime ( gclient_t *client ); // racesow

void RS_PlayerEnter( gclient_t *client );
void RS_PlayerDisconnect( gclient_t *client );
void RS_PlayerUpdatePlaytime( gclient_t *client ); // racesow
void RS_PlayerReset( rs_authplayer_t *player );
void RS_PlayerUserinfoChanged( rs_authplayer_t *player, char *oldname );
