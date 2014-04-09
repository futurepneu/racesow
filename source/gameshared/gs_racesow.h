#ifdef __cplusplus
extern "C" {
#endif

typedef struct rs_racetime_s
{
	int timedelta;
	int hour;
	int min;
	int sec;
	int milli;
} rs_racetime_t;

void RS_ResetPjState(int playerNum);
qboolean RS_QueryPjState(int playerNum);
void RS_IncrementWallJumps(int playerNum);
void RS_IncrementDashes(int playerNum);
void RS_IncrementJumps(int playerNum);

void RS_Racetime( int timedelta, rs_racetime_t *racetime );

#ifdef __cplusplus
};
#endif
