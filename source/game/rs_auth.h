typedef enum
{
	QSTATUS_NONE,				/**< no query was sent */
	QSTATUS_FAILED,				/**< query returned in failure */
	QSTATUS_PENDING,			/**< query in progress */
	QSTATUS_SUCCESS				/**< query succeded */
} qstatus_t;

typedef struct rs_authmap_s
{
	qstatus_t status;			/**< status of current query */
	int id;						/**< database id of the map */
	int playTime;				/**< time playing the map in milliseconds */
	int races;					/**< number of races finished */
} rs_authmap_t;

typedef struct rs_authplayer_s
{
	gclient_t *client;			/**< client for the player */
	qstatus_t status;			/**< status of the inprogress query */
	qstatus_t nickStatus;		/**< status of the nickname query */
	char name[MAX_NAME_CHARS];	/**< username */
	char nick[MAX_NAME_CHARS];	/**< ingame nickname */
	int id;						/**< database id for player, 0 for unauthenticated */
	int failTime;				/**< leveltime to rename the player */
	int playTime;				/**< time spent playing the map in milliseconds */
	int races;					/**< number of races finished */
} rs_authplayer_t;

extern rs_authmap_t authmap;
extern rs_authplayer_t *authplayers;

void RS_InitAuth( void );
void RS_ShutdownAuth( void );
void RS_ThinkAuth( void );

void RS_PlayerEnter( gclient_t *client );
void RS_PlayerDisconnect( gclient_t *client );