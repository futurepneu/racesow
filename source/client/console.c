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
// console.c

#include "client.h"

#define CON_MAXLINES	500
typedef struct
{
	char *text[CON_MAXLINES];
	int x;              // offset in current line for next print
	int linewidth;      // characters across screen (FIXME)
	int display;        // bottom of console displays this line
	int totallines;     // total lines in console scrollback
	int numlines;		// non-empty lines in console scrollback

	float times[NUM_CON_TIMES]; // cls.realtime time the line was generated
	// for transparent notify lines
} console_t;

static console_t con;

qboolean con_initialized;

static cvar_t *con_notifytime;
static cvar_t *con_drawNotify;
static cvar_t *con_chatmode;
cvar_t *con_printText;

static cvar_t *con_chatX, *con_chatY;
static cvar_t *con_chatWidth;
static cvar_t *con_chatFontFamily;
static cvar_t *con_chatFontStyle;
static cvar_t *con_chatFontSize;
static cvar_t *con_chatCGame;

// console input line editing
#define	    MAXCMDLINE	256
static char key_lines[32][MAXCMDLINE];
static unsigned int key_linepos;	// byte offset of cursor in edit line
static int input_prestep;			// pixels to skip at start when drawing
static int edit_line = 0;
static int history_line = 0;
static int search_line = 0;
static char search_text[MAXCMDLINE*2+4];

// messagemode[2]
static qboolean chat_team;
static char chat_buffer[MAXCMDLINE];
static int chat_prestep = 0;
static unsigned int chat_linepos = 0;
static unsigned int chat_bufferlen = 0;

static int touch_x, touch_y;

/*
* Con_NumPadValue
*/
static int Con_NumPadValue( int key )
{
	switch( key )
	{
	case KP_SLASH:
		return '/';

	case KP_STAR:
		return '*';

	case KP_MINUS:
		return '-';

	case KP_PLUS:
		return '+';

	case KP_HOME:
		return '7';

	case KP_UPARROW:
		return '8';

	case KP_PGUP:
		return '9';

	case KP_LEFTARROW:
		return '4';

	case KP_5:
		return '5';

	case KP_RIGHTARROW:
		return '6';

	case KP_END:
		return '1';

	case KP_DOWNARROW:
		return '2';

	case KP_PGDN:
		return '3';

	case KP_INS:
		return '0';

	case KP_DEL:
		return '.';
	}

	return key;
}

/*
* Con_ClearTyping
*/
static void Con_ClearTyping( void )
{
	key_lines[edit_line][1] = 0; // clear any typing
	key_linepos = 1;
	search_line = edit_line;
	search_text[0] = 0;
}

/*
* Con_Close
*/
void Con_Close( void )
{
	scr_con_current = 0;

	Con_ClearTyping();
	Con_ClearNotify();
	Key_ClearStates();
}

/*
* Con_ToggleConsole_f
*/
void Con_ToggleConsole_f( void )
{
	SCR_EndLoadingPlaque(); // get rid of loading plaque

	if( cls.state == CA_GETTING_TICKET || cls.state == CA_CONNECTING || cls.state == CA_CONNECTED )
		return;

	Con_ClearTyping();
	Con_ClearNotify();

	if( cls.key_dest == key_console )
	{
		// close console
		CL_SetKeyDest( cls.old_key_dest );
	}
	else
	{
		// open console
		CL_SetOldKeyDest( cls.key_dest );
		CL_SetKeyDest( key_console );
		IN_ShowIME( qtrue );
	}
}

/*
* Con_Clear_f
*/
void Con_Clear_f( void )
{
	int i;

	for( i = 0; i < CON_MAXLINES; i++ )
	{
		Q_free( con.text[i] );
		con.text[i] = NULL;
	}
	con.numlines = 0;
	con.display = 0;
}

/*
* Con_BufferText
*
* Copies into console text 'buffer' as a single 'delim'-separated string
* Returns resulting number of characters, not counting the trailing zero
*/
static size_t Con_BufferText( char *buffer, const char *delim )
{
	int l, x;
	const char *line;
	size_t length, delim_len = strlen( delim );

	if( !con_initialized )
		return 0;

	length = 0;
	for( l = con.numlines - 1; l >= 0; l-- )
	{
		line = con.text[l] ? con.text[l] : "";
		x = strlen( line );

		if( buffer && *line )
		{
			memcpy( buffer + length, line, x );
			memcpy( buffer + length + x, delim, delim_len );
		}

		length += x + delim_len;
	}

	if( buffer )
		buffer[length] = '\0';

	return length;
}

/*
* Con_Dump_f
* 
* Save the console contents out to a file
*/
static void Con_Dump_f( void )
{
	int file;
	size_t buffer_size;
	char *buffer;
	size_t name_size;
	char *name;
	const char *newline = "\r\n";

	if( !con_initialized )
		return;

	if( Cmd_Argc() != 2 )
	{
		Com_Printf( "usage: condump <filename>\n" );
		return;
	}

	name_size = sizeof( char ) * ( strlen( Cmd_Argv( 1 ) ) + strlen( ".txt" ) + 1 );
	name = Mem_TempMalloc( name_size );

	Q_strncpyz( name, Cmd_Argv( 1 ), name_size );
	COM_DefaultExtension( name, ".txt", name_size );
	COM_SanitizeFilePath( name );

	if( !COM_ValidateRelativeFilename( name ) )
	{
		Com_Printf( "Invalid filename.\n" );
		Mem_TempFree( name );
		return;
	}

	if( FS_FOpenFile( name, &file, FS_WRITE ) == -1 )
	{
		Com_Printf( "Couldn't open: %s\n", name );
		Mem_TempFree( name );
		return;
	}

	buffer_size = Con_BufferText( NULL, newline ) + 1;
	buffer = Mem_TempMalloc( buffer_size );

	Con_BufferText( buffer, newline );

	FS_Write( buffer, buffer_size - 1, file );

	FS_FCloseFile( file );

	Mem_TempFree( buffer );

	Com_Printf( "Dumped console text: %s\n", name );
	Mem_TempFree( name );
}

/*
* Con_ClearNotify
*/
void Con_ClearNotify( void )
{
	int i;

	for( i = 0; i < NUM_CON_TIMES; i++ )
		con.times[i] = 0;
}

/*
* Con_SetMessageModeCvar
* 
* Called from CL_SetKeyDest
*/
void Con_SetMessageModeCvar( void )
{
	if( cls.key_dest == key_message )
		Cvar_ForceSet( "con_messageMode", chat_team ? "2" : "1" );
	else
		Cvar_ForceSet( "con_messageMode", "0" );
}

/*
* Con_MessageMode_f
*/
static void Con_MessageMode_f( void )
{
	chat_team = qfalse;
	if( cls.state == CA_ACTIVE )
	{
		CL_SetKeyDest( key_message );
		IN_ShowIME( qtrue );
	}
}

/*
* Con_MessageMode2_f
*/
static void Con_MessageMode2_f( void )
{
	chat_team = Cmd_Exists( "say_team" ); // if not, make it a normal "say: "
	if( cls.state == CA_ACTIVE )
	{
		CL_SetKeyDest( key_message );
		IN_ShowIME( qtrue );
	}
}

/*
* Con_CheckResize
* 
* If the line width has changed, reformat the buffer.
*/
void Con_CheckResize( void )
{
	int charWidth, width;

	charWidth = SMALL_CHAR_WIDTH * VID_GetPixelRatio();
	if( !charWidth )
		charWidth = 1;

	width = viddef.width / charWidth - 2;
	if( width == con.linewidth )
		return;

	if( width < 1 )		// video hasn't been initialized yet
		con.linewidth = 78;
	else
		con.linewidth = width;
}

/*
* Con_ChangeFontSize
*/
void Con_ChangeFontSize( int ch )
{
	SCR_ChangeSystemFontSmallSize( ch );
}

/*
* Con_Init
*/
void Con_Init( void )
{
	int i;

	if( con_initialized )
		return;

	for( i = 0; i < 32; i++ )
	{
		key_lines[i][0] = ']';
		key_lines[i][1] = 0;
	}
	key_linepos = 1;

	con.totallines = CON_MAXLINES;
	con.numlines = 0;
	con.display = 0;
	con.linewidth = 78;

	touch_x = touch_y = -1;

	Com_Printf( "Console initialized.\n" );

	//
	// register our commands
	//
	con_notifytime = Cvar_Get( "con_notifytime", "3", CVAR_ARCHIVE );
	con_drawNotify = Cvar_Get( "con_drawNotify", "0", CVAR_ARCHIVE );
	con_printText  = Cvar_Get( "con_printText", "1", CVAR_ARCHIVE );
	con_chatmode = Cvar_Get( "con_chatmode", "3", CVAR_ARCHIVE );

	con_chatX  = Cvar_Get( "con_chatX", "0", CVAR_READONLY );
	con_chatY = Cvar_Get( "con_chatY", "0", CVAR_READONLY );
	con_chatWidth  = Cvar_Get( "con_chatWidth", "0", CVAR_READONLY );
	con_chatFontFamily  = Cvar_Get( "con_chatFontFamily", "", CVAR_READONLY );
	con_chatFontStyle  = Cvar_Get( "con_chatFontStyle", "0", CVAR_READONLY );
	con_chatFontSize  = Cvar_Get( "con_chatFontSize", "10", CVAR_READONLY );
	con_chatCGame = Cvar_Get( "con_chatCGame", "0", CVAR_READONLY );

	Cmd_AddCommand( "toggleconsole", Con_ToggleConsole_f );
	Cmd_AddCommand( "messagemode", Con_MessageMode_f );
	Cmd_AddCommand( "messagemode2", Con_MessageMode2_f );
	Cmd_AddCommand( "clear", Con_Clear_f );
	Cmd_AddCommand( "condump", Con_Dump_f );
	con_initialized = qtrue;
}

/*
* Con_Shutdown
*/
void Con_Shutdown( void )
{
	if( !con_initialized )
		return;

	Con_Clear_f();	// free scrollback text

	Cmd_RemoveCommand( "toggleconsole" );
	Cmd_RemoveCommand( "messagemode" );
	Cmd_RemoveCommand( "messagemode2" );
	Cmd_RemoveCommand( "clear" );
	Cmd_RemoveCommand( "condump" );

	con_initialized = qfalse;
}

/*
* Con_Linefeed
*/
static void Con_Linefeed( void )
{
	// shift scrollback text up in the buffer to make room for a new line
	if (con.numlines == con.totallines )
		Q_free( con.text[con.numlines - 1] );
	memmove( con.text + 1, con.text, sizeof( con.text[0] ) * min( con.numlines, con.totallines - 1 ) );
	con.text[0] = NULL;

	// shift the timings array
	memmove( con.times + 1, con.times, sizeof( con.times[0] ) * ( NUM_CON_TIMES - 1 ) );

	con.x = 0;
	if( con.display )
	{
		// the console is scrolled up, stay in the same place if possible
		con.display++;
		clamp_high( con.display, con.totallines - 1 );
	}
	con.numlines++;
	clamp_high( con.numlines, con.totallines );
}

/*
* Con_Print
* 
* Handles cursor positioning, line wrapping, etc
* All console printing must go through this in order to be logged to disk
* If no console is visible, the text will appear at the top of the game window
*/
static void addchartostr( char **s, int c ) {
	int len = *s ? strlen( *s ) : 0;
	char *newstr = Q_realloc( *s, len + 2 );
	newstr[len] = c;
	newstr[len+1] = '\0';
	*s = newstr;
}
static void Con_Print2( const char *txt, qboolean notify )
{
	int c, l;
	int color;
	qboolean colorflag = qfalse;

	if( !con_initialized )
		return;

	if( con_printText && con_printText->integer == 0 )
		return;

	color = ColorIndex( COLOR_WHITE );

	while( ( c = *txt ) )
	{
		// count word length
		for( l = 0; l < con.linewidth; l++ )
			if( (unsigned char)txt[l] <= ' ' )
				break;

		// word wrap
		if( l != con.linewidth && ( con.x + l > con.linewidth ) )
			con.x = 0;

		if( !con.x )
		{
			Con_Linefeed();
			// mark time for transparent overlay
			con.times[0] = cls.realtime;
			if( !notify )
				con.times[0] -= con_notifytime->value*1000 + 1;

			if( color != ColorIndex( COLOR_WHITE ) )
			{
				addchartostr( &con.text[0], Q_COLOR_ESCAPE );
				addchartostr( &con.text[0], '0' + color );
				con.x += 2;
			}
		}

		switch( c )
		{
		case '\n':
			color = ColorIndex( COLOR_WHITE );
			con.x = 0;
			break;

		case '\r':
			break;

		default: // display character and advance
			addchartostr( &con.text[0], c );
			con.x++;
			if( con.x >= con.linewidth )	// haha welcome to 1995 lol
				con.x = 0;

			if( colorflag )
			{
				if( *txt != Q_COLOR_ESCAPE )
					color = ColorIndex( *txt );
				colorflag = qfalse;
			}
			else if( *txt == Q_COLOR_ESCAPE )
				colorflag = qtrue;

			//			if( Q_IsColorString( txt ) ) {
			//				color = ColorIndex( *(txt+1) );
			//			}
			break;
		}

		txt++;
	}
}

void Con_Print( const char *txt )
{
	Con_Print2( txt, qtrue );
}

void Con_PrintSilent( const char *txt )
{
	Con_Print2( txt, qfalse );
}

/*
==============================================================================

DRAWING

==============================================================================
*/

/*
* Q_ColorCharCount
*/
int Q_ColorCharCount( const char *s, int byteofs )
{
	char c;
	const char *end = s + byteofs;
	int charcount = 0;

	while( s < end )
	{
		int gc = Q_GrabCharFromColorString( &s, &c, NULL );
		if( gc == GRABCHAR_CHAR )
			charcount++;
		else if( gc == GRABCHAR_COLOR )
			;
		else if( gc == GRABCHAR_END )
			break;
		else
			assert( 0 );
	}

	return charcount;
}

/*
* Q_ColorCharOffset
*/
int Q_ColorCharOffset( const char *s, int charcount )
{
	const char *start = s;
	char c;

	while( *s && charcount )
	{
		int gc = Q_GrabCharFromColorString( &s, &c, NULL );
		if( gc == GRABCHAR_CHAR )
			charcount--;
		else if( gc == GRABCHAR_COLOR )
			;
		else if( gc == GRABCHAR_END )
			break;
		else
			assert( 0 );
	}

	return s - start;
}

#if 0
/*
* Q_ColorStrLastColor
*/
static int Q_ColorStrLastColor( const char *s, int byteofs )
{
	char c;
	const char *end = s + byteofs;
	int lastcolor = ColorIndex(COLOR_WHITE), colorindex;

	while( s < end )
	{
		int gc = Q_GrabCharFromColorString( &s, &c, &colorindex );
		if( gc == GRABCHAR_CHAR )
			;
		else if( gc == GRABCHAR_COLOR )
			lastcolor = colorindex;
		else if( gc == GRABCHAR_END )
			break;
		else
			assert( 0 );
	}

	return lastcolor;
}
#endif

/*
* Con_DrawInput
* 
* The input line scrolls horizontally if typing goes beyond the right edge
*/
static void Con_DrawInput( int vislines )
{
	char draw_search_text[MAXCMDLINE*2+4];
	const char *text = key_lines[edit_line];
	float pixelRatio = VID_GetPixelRatio();
	int smallCharHeight = SCR_strHeight( cls.consoleFont );
	int text_y = vislines - (int)( 14 * pixelRatio ) - smallCharHeight;
	int margin = 8 * pixelRatio;
	int promptwidth = SCR_strWidth( "]", cls.consoleFont, 1 );
	int cursorwidth = SCR_strWidth( "_", cls.consoleFont, 1 );
	int input_width = viddef.width - margin * 2;
	int prewidth;	// width of input line before cursor

	if( cls.key_dest != key_console )
		return;

	if( search_text[0] ) {
		text = draw_search_text;
		Q_snprintfz( draw_search_text, sizeof( draw_search_text ), "%s : %s", key_lines[edit_line], search_text );
	}

	prewidth = SCR_strWidth( text, cls.consoleFont, key_linepos );

	// don't let the cursor go beyond the left screen edge
	clamp_high( input_prestep, prewidth - promptwidth);
	// don't let it go beyond the right screen edge
	clamp_low( input_prestep, prewidth - ( input_width - cursorwidth ) );

	SCR_DrawClampString( margin - input_prestep,
		text_y, text, margin, text_y,
		viddef.width - margin, viddef.height, cls.consoleFont, colorWhite );

	if( (int)( cls.realtime>>8 )&1 )
		SCR_DrawRawChar( margin + prewidth - input_prestep, text_y, '_',
		cls.consoleFont, colorWhite );
}

/*
* Con_DrawNotify
* 
* Draws the last few lines of output transparently over the game top
*/
void Con_DrawNotify( void )
{
	int v;
	char *text;
	const char *say;
	const char *translated;
	int i;
	int time;
	char *s;
	float pixelRatio = VID_GetPixelRatio();

	v = 0;
	if( con_drawNotify->integer || developer->integer )
	{
		int x = 8 * pixelRatio;

		for( i = min( NUM_CON_TIMES, con.numlines ) - 1; i >= 0; i-- )
		{
			time = con.times[i];
			if( time == 0 )
				continue;
			time = cls.realtime - time;
			if( time > con_notifytime->value*1000 )
				continue;
			text = con.text[i] ? con.text[i] : "";

			SCR_DrawString( x, v, ALIGN_LEFT_TOP, text, cls.consoleFont, colorWhite );

			v += SCR_strHeight( cls.consoleFont );
		}
	}

	if( cls.key_dest == key_message )
	{
		int x, y;
		int width, prewidth;
		int promptwidth, cursorwidth;
		struct qfontface_s *font = NULL;

		if( con_chatCGame->integer )
		{
			width = con_chatWidth->integer;

			if( *con_chatFontFamily->string && con_chatFontSize->integer ) {
				font = SCR_RegisterFont( con_chatFontFamily->string, con_chatFontStyle->integer, con_chatFontSize->integer );
			}
			if( !font )
				font = cls.consoleFont;

			x = con_chatX->integer;
			y = con_chatY->integer;
		}
		else
		{
			width = viddef.width;
			x = 8 * pixelRatio;
			y = v;
			font = cls.consoleFont;
		}

		// 48 is an arbitrary offset for not overlapping the FPS and clock prints
		width -= 48 * viddef.height / 600;
		cursorwidth = SCR_strWidth( "_", font, 0 );

		if( chat_team )
		{
			say = "say_team:";
		}
		else
		{
			say = "say:";
		}

		translated = L10n_TranslateString( "common", say );
		if( !translated ) {
			translated = say;
		}
		SCR_DrawString( x, y, ALIGN_LEFT_TOP, translated, font, colorWhite );
		promptwidth = SCR_strWidth( translated, font, 0 ) + SCR_strWidth( " ", font, 0 );

		s = chat_buffer;
		prewidth = chat_linepos ? SCR_strWidth( s, font, chat_linepos ) : 0;

		// don't let the cursor go beyond the left screen edge
		clamp_high( chat_prestep, prewidth );

		// don't let it go beyond the right screen edge
		clamp_low( chat_prestep, prewidth - ( width - promptwidth - cursorwidth ) );

		// FIXME: we double the font height to compensate for alignment issues
		SCR_DrawClampString( x + promptwidth - chat_prestep,
			y, s, x + promptwidth, y,
			x + width, y + SCR_strHeight( font ) * 2, font, colorWhite );

		if( (int)( cls.realtime>>8 )&1 )
			SCR_DrawRawChar( x + promptwidth + prewidth - chat_prestep, y, '_',
			font, colorWhite );
	}
}

/*
* Con_GetMessageArea
*/
static void Con_GetMessageArea( int *x1, int *y1, int *x2, int *y2 )
{
	int x, y;
	int width;
	struct qfontface_s *font = NULL;

	if( con_chatCGame->integer )
	{
		width = con_chatWidth->integer;

		if( *con_chatFontFamily->string && con_chatFontSize->integer ) {
			font = SCR_RegisterFont( con_chatFontFamily->string, con_chatFontStyle->integer, con_chatFontSize->integer );
		}
		if( !font )
			font = cls.consoleFont;

		x = con_chatX->integer;
		y = con_chatY->integer;
	}
	else
	{
		int i;
		int time;

		width = viddef.width;
		x = 8 * VID_GetPixelRatio();
		y = 0;
		font = cls.consoleFont;

		for( i = min( NUM_CON_TIMES, con.numlines ) - 1; i >= 0; i-- )
		{
			time = con.times[i];
			if( time == 0 )
				continue;
			time = cls.realtime - time;
			if( time > con_notifytime->value*1000 )
				continue;

			y += SCR_strHeight( cls.consoleFont );
		}
	}

	// 48 is an arbitrary offset for not overlapping the FPS and clock prints
	width -= 48 * viddef.height / 600;

	*x1 = x;
	*y1 = y;
	*x2 = x + width;
	*y2 = y + SCR_strHeight( font );
}

/*
* Con_DrawConsole
* 
* Draws the console with the solid background
*/
void Con_DrawConsole( void )
{
	int i, x, y;
	int rows;
	char *text;
	int row;
	unsigned int lines;
	char version[256];
	time_t long_time;
	struct tm *newtime;
	int smallCharHeight = SCR_strHeight( cls.consoleFont );
	float pixelRatio = VID_GetPixelRatio();
	int scaled;

	lines = viddef.height * scr_con_current;
	if( lines <= 0 )
		return;
	if( !smallCharHeight )
		return;

	if( lines > viddef.height )
		lines = viddef.height;

	// draw the background
	re.DrawStretchPic( 0, 0, viddef.width, lines, 0, 0, 1, 1, colorWhite, cls.consoleShader );
	scaled = ( ( pixelRatio >= 1.0f ) ? 2 * pixelRatio : 1 );
	SCR_DrawFillRect( 0, lines - scaled, viddef.width, scaled, colorRed );

	// get date from system
	time( &long_time );
	newtime = localtime( &long_time );

#ifdef PUBLIC_BUILD
	Q_snprintfz( version, sizeof( version ), "%02d:%02d %s v%4.2f", newtime->tm_hour, newtime->tm_min,
		APPLICATION, APP_VERSION );
#else
	Q_snprintfz( version, sizeof( version ), "%02d:%02d %s v%4.2f rev:%s", newtime->tm_hour, newtime->tm_min,
		APPLICATION, APP_VERSION, revisioncvar->string );
#endif

	scaled = ( ( pixelRatio >= 1.25f ) ? 4 * pixelRatio : 4 );
	SCR_DrawString( viddef.width-SCR_strWidth( version, cls.consoleFont, 0 ) - scaled,
		lines - SCR_strHeight( cls.consoleFont ) - scaled, 
		ALIGN_LEFT_TOP, version, cls.consoleFont, colorRed );

	// prepare to draw the text
	scaled = 14 * pixelRatio;
	rows = ( lines - smallCharHeight - scaled ) / smallCharHeight;  // rows of text to draw
	y = lines - smallCharHeight - scaled - smallCharHeight;

	row = con.display;	// first line to be drawn
	if( con.display )
	{
		int width = SCR_strWidth( "^", cls.consoleFont, 0 );

		// draw arrows to show the buffer is backscrolled
		for( x = 0; x < con.linewidth; x += 4 )
			SCR_DrawRawChar( ( x+1 )*width, y, '^', cls.consoleFont, colorRed );

		// the arrows obscure one line of scrollback
		y -= smallCharHeight;
		rows--;
		row++;
	}

	// draw from the bottom up
	for( i = 0; i < rows; i++, y -= smallCharHeight, row++ )
	{
		if( row >= con.numlines )
			break; // reached top of scrollback

		text = con.text[row] ? con.text[row] : "";

		SCR_DrawString( 8 * pixelRatio, y, ALIGN_LEFT_TOP, text, cls.consoleFont, colorWhite );
	}

	// draw the input prompt, user text, and cursor if desired
	Con_DrawInput( lines );
}


/*
* Con_DisplayList

* New function for tab-completion system
* Added by EvilTypeGuy
* MEGA Thanks to Taniwha
*/
static void Con_DisplayList( char **list )
{
	int i = 0;
	int pos = 0;
	int len = 0;
	int maxlen = 0;
	int width = ( con.linewidth - 4 );
	char **walk = list;

	while( *walk )
	{
		len = (int)strlen( *walk );
		if( len > maxlen )
			maxlen = len;
		walk++;
	}
	maxlen += 1;

	while( *list )
	{
		len = (int)strlen( *list );

		if( pos + maxlen >= width )
		{
			Com_Printf( "\n" );
			pos = 0;
		}

		Com_Printf( "%s", *list );
		for( i = 0; i < ( maxlen - len ); i++ )
			Com_Printf( " " );

		pos += maxlen;
		list++;
	}

	if( pos )
		Com_Printf( "\n\n" );
}

/*
* Con_CompleteCommandLine

* New function for tab-completion system
* Added by EvilTypeGuy
* Thanks to Fett erich@heintz.com
* Thanks to taniwha
*/
static void Con_CompleteCommandLine( void )
{
	char *cmd = "";
	char *s;
	int c, v, a, d, ca, i;
	int cmd_len;
	char **list[6] = { 0, 0, 0, 0, 0, 0 };

	s = key_lines[edit_line] + 1;
	if( *s == '\\' || *s == '/' )
		s++;
	if( !*s )
		return;

	// Count number of possible matches
	c = Cmd_CompleteCountPossible( s );
	v = Cvar_CompleteCountPossible( s );
	a = Cmd_CompleteAliasCountPossible( s );
	d = Dynvar_CompleteCountPossible( s );
	ca = 0;

	if( !( c + v + a + d ) )
	{
		// now see if there's any valid cmd in there, providing
		// a list of matching arguments
		list[4] = Cmd_CompleteBuildArgList( s );
		if( !list[4] )
		{
			// No possible matches, let the user know they're insane
			Com_Printf( "\nNo matching aliases, commands, cvars, or dynvars were found.\n\n" );
			return;
		}

		// count the number of matching arguments
		for( ca = 0; list[4][ca]; ca++ );
		if( !ca )
		{
			// the list is empty, although non-NULL list pointer suggests that the command
			// exists, so clean up and exit without printing anything
			Mem_TempFree( list[4] );
			return;
		}
	}

	if( c + v + a + d + ca == 1 )
	{
		// find the one match to rule them all
		if( c )
			list[0] = Cmd_CompleteBuildList( s );
		else if( v )
			list[0] = Cvar_CompleteBuildList( s );
		else if( a )
			list[0] = Cmd_CompleteAliasBuildList( s );
		else if( d )
			list[0] = (char **) Dynvar_CompleteBuildList( s );
		else
			list[0] = list[4], list[4] = NULL;
		cmd = *list[0];
		cmd_len = (int)strlen( cmd );
	}
	else
	{
		int i_start = 0;

		if( c )
			cmd = *( list[0] = Cmd_CompleteBuildList( s ) );
		if( v )
			cmd = *( list[1] = Cvar_CompleteBuildList( s ) );
		if( a )
			cmd = *( list[2] = Cmd_CompleteAliasBuildList( s ) );
		if( d )
			cmd = *( list[3] = (char **) Dynvar_CompleteBuildList( s ) );
		if( ca )
			s = strstr( s, " " ) + 1, cmd = *( list[4] ), i_start = 4;

		cmd_len = (int)strlen( s );
		do
		{
			for( i = i_start; i < 5; i++ )
			{
				char ch = cmd[cmd_len];
				char **l = list[i];
				if( l )
				{
					while( *l && ( *l )[cmd_len] == ch )
						l++;
					if( *l )
						break;
				}
			}
			if( i == 5 )
				cmd_len++;
		}
		while( i == 5 );

		// Print Possible Commands
		if( c )
		{
			Com_Printf( S_COLOR_RED "%i possible command%s%s\n", c, ( c > 1 ) ? "s: " : ":", S_COLOR_WHITE );
			Con_DisplayList( list[0] );
		}

		if( v )
		{
			Com_Printf( S_COLOR_CYAN "%i possible variable%s%s\n", v, ( v > 1 ) ? "s: " : ":", S_COLOR_WHITE );
			Con_DisplayList( list[1] );
		}

		if( a )
		{
			Com_Printf( S_COLOR_MAGENTA "%i possible alias%s%s\n", a, ( a > 1 ) ? "es: " : ":", S_COLOR_WHITE );
			Con_DisplayList( list[2] );
		}

		if( d )
		{
			Com_Printf( S_COLOR_YELLOW "%i possible dynvar%s%s\n", d, ( d > 1 ) ? "s: " : ":", S_COLOR_WHITE );
			Con_DisplayList( list[3] );
		}

		if( ca )
		{
			Com_Printf( S_COLOR_GREEN "%i possible argument%s%s\n", ca, ( ca > 1 ) ? "s: " : ":", S_COLOR_WHITE );
			Con_DisplayList( list[4] );
		}
	}

	if( cmd )
	{
		int skip = 1;
		char *cmd_temp = NULL, *p;

		if ( con_chatmode && con_chatmode->integer != 3 )
		{
			skip++;
			key_lines[edit_line][1] = '/';
		}

		if( ca )
		{
			size_t temp_size;

			temp_size = sizeof( key_lines[edit_line] );
			cmd_temp = Mem_TempMalloc( temp_size );

			Q_strncpyz( cmd_temp, key_lines[edit_line] + skip, temp_size );
			p = strstr( cmd_temp, " " );
			if( p )
				*(p+1) = '\0';

			cmd_len += strlen( cmd_temp );

			Q_strncatz( cmd_temp, cmd, temp_size );
			cmd = cmd_temp;
		}

		Q_strncpyz( key_lines[edit_line] + skip, cmd, sizeof( key_lines[edit_line] ) - (1+skip) );
		key_linepos = min( cmd_len + skip, sizeof( key_lines[edit_line] ) - 1 );

		if( c + v + a + d == 1 && key_linepos < sizeof( key_lines[edit_line] ) - 1 )
		{
			key_lines[edit_line][key_linepos] = ' ';
			key_linepos++;
		}
		key_lines[edit_line][key_linepos] = 0;

		if( cmd == cmd_temp )
			Mem_TempFree( cmd );
	}

	for( i = 0; i < 5; ++i )
	{
		if( list[i] )
			Mem_TempFree( list[i] );
	}
}

/*
==============================================================================

LINE TYPING INTO THE CONSOLE

==============================================================================
*/


/*
* Con_Key_Copy
* 
* Copies console text to clipboard
* Should be Con_Copy prolly
*/
static void Con_Key_Copy( void )
{
	size_t buffer_size;
	char *buffer;
	const char *newline = "\r\n";

	if( search_text[0] ) {
		CL_SetClipboardData( search_text );
		return;
	}

	buffer_size = Con_BufferText( NULL, newline ) + 1;
	buffer = Mem_TempMalloc( buffer_size );

	Con_BufferText( buffer, newline );

	CL_SetClipboardData( buffer );

	Mem_TempFree( buffer );
}

/*
* Con_Key_Paste
* 
* Inserts stuff from clipboard to console
* Should be Con_Paste prolly
*/
static void Con_Key_Paste( qboolean primary )
{
	char *cbd;
	char *tok;

	cbd = CL_GetClipboardData( primary );
	if( cbd )
	{
		int i;

		tok = strtok( cbd, "\n\r\b" );

		while( tok != NULL )
		{
			i = (int)strlen( tok );
			if( i + key_linepos >= MAXCMDLINE )
				i = MAXCMDLINE - key_linepos;

			if( i > 0 )
			{
				Q_strncatz( key_lines[edit_line], tok, sizeof( key_lines[edit_line] ) );
				key_linepos += i;
			}

			tok = strtok( NULL, "\n\r\b" );

			if( tok != NULL )
			{
				if( key_lines[edit_line][1] == '\\' || key_lines[edit_line][1] == '/' )
					Cbuf_AddText( key_lines[edit_line] + 2 ); // skip the >
				else
					Cbuf_AddText( key_lines[edit_line] + 1 ); // valid command

				Cbuf_AddText( "\n" );
				Com_Printf( "%s\n", key_lines[edit_line] );
				edit_line = ( edit_line + 1 ) & 31;
				history_line = edit_line;
				search_line = edit_line;
				search_text[0] = 0;
				key_lines[edit_line][0] = ']';
				key_lines[edit_line][1] = 0;
				key_linepos = 1;
				if( cls.state == CA_DISCONNECTED )
					SCR_UpdateScreen(); // force an update, because the command may take some time
			}
		}

		CL_FreeClipboardData( cbd );
	}
}

/*
* Con_CharEvent
* 
* Interactive line editing and console scrollback only for (Unicode) chars
*/
void Con_CharEvent( qwchar key )
{
	if( !con_initialized )
		return;

	key = Con_NumPadValue( key );

	if( cls.state == CA_GETTING_TICKET || cls.state == CA_CONNECTING || cls.state == CA_CONNECTED )
		return;

	switch( key )
	{
	case 22: // CTRL - V : paste
		Con_Key_Paste( qfalse );
		return;

	case 12: // CTRL - L : clear
		Cbuf_AddText( "clear\n" );
		return;

		/*
		case 8: // CTRL+H or Backspace
		if (key_linepos > 1)
		{
		// skip to the end of color sequence
		while (Q_IsColorString(key_lines[edit_line] + key_linepos))
		key_linepos += 2;

		strcpy (key_lines[edit_line] + key_linepos - 1, key_lines[edit_line] + key_linepos);
		key_linepos--;
		}
		return;
		*/

	case 16: // CTRL+P : history prev
		do
		{
			history_line = ( history_line - 1 ) & 31;
		} while( history_line != edit_line && !key_lines[history_line][1] );

		if( history_line == edit_line )
			history_line = ( edit_line+1 )&31;

		Q_strncpyz( key_lines[edit_line], key_lines[history_line], sizeof( key_lines[edit_line] ) );
		key_linepos = (unsigned int)strlen( key_lines[edit_line] );
		input_prestep = 0;
		return;

	case 14: // CTRL+N : history next
		if( history_line == edit_line )
			return;

		do
		{
			history_line = ( history_line + 1 ) & 31;
		} while( history_line != edit_line && !key_lines[history_line][1] );

		if( history_line == edit_line )
		{
			key_lines[edit_line][0] = ']';
			key_linepos = 1;
		}
		else
		{
			Q_strncpyz( key_lines[edit_line], key_lines[history_line], sizeof( key_lines[edit_line] ) );
			key_linepos = (unsigned int)strlen( key_lines[edit_line] );
			input_prestep = 0;
		}
		return;

	case 3: // CTRL+C: copy text to clipboard
		Con_Key_Copy();
		return;

	case 1: // CTRL+A: jump to beginning of line (same as HOME)
		key_linepos = 1;
		return;
	case 5: // CTRL+E: jump to end of line (same as END)
		key_linepos = (unsigned int)strlen( key_lines[edit_line] );
		return;
	case 18: // CTRL+R
		{
			size_t search_len = strlen( key_lines[edit_line] + 1 );
			if( !search_len ) {
				break;
			}
			do {
				search_line = ( search_line - 1 ) & 31;
			} while( search_line != edit_line && Q_strnicmp( key_lines[search_line]+1, key_lines[edit_line]+1, search_len ) );

			if( search_line != edit_line ) {
				Q_strncpyz( search_text, key_lines[search_line] + 1, sizeof( search_text ) );
			}
			else {
				search_text[0] = '\0';
			}
		}
		break;
	}

	if( key < 32 || key > 0x1FFFFF )
		return; // non-printable

	if( key_linepos < MAXCMDLINE-1 )
	{
		char *utf = Q_WCharToUtf8( key );
		int utflen = strlen( utf );

		if( strlen( key_lines[edit_line] ) + utflen >= MAXCMDLINE )
			return;		// won't fit

		// move remainder to the right
		memmove( key_lines[edit_line] + key_linepos + utflen,
			key_lines[edit_line] + key_linepos,
			strlen( key_lines[edit_line] + key_linepos ) + 1);	// +1 for trailing 0

		// insert the char sequence
		memcpy( key_lines[edit_line] + key_linepos, utf, utflen );
		key_linepos += utflen;
	}
}

/*
* Con_SendChatMessage
*/
static void Con_SendChatMessage( const char *text, qboolean team )
{
	char *cmd;
	char buf[MAXCMDLINE], *p;

	// convert double quotes to single quotes
	Q_strncpyz( buf, text, sizeof(buf) );
	for( p = buf; *p; p++ )
		if( *p == '"' )
			*p = '\'';

	if( team && Cmd_Exists( "say_team" ) )
		cmd = "say_team";
	else if( Cmd_Exists( "say" ) )
		cmd = "say";
	else
		cmd = "cmd say";

	Cbuf_AddText( va("%s \"%s\"\n", cmd, buf) );
}

/*
* Con_Key_Enter
*
* Handle K_ENTER keypress in console
* Set "ignore_ctrldown" to prevent Ctrl-M/J from sending the message to chat
*/
static void Con_Key_Enter( qboolean ignore_ctrl )
{
	enum {COMMAND, CHAT, TEAMCHAT} type;
	char *p;
	int chatmode = con_chatmode ? con_chatmode->integer : 3;
	/* 1 = always chat unless with a slash;  non-1 = smart: unknown commands are chat.
	0 used to be the NetQuake way (always a command),
	but no one will probably want it in now */

	if( search_text[0] ) {
		key_lines[edit_line][0] = ']';
		Q_strncpyz( key_lines[edit_line] + 1, search_text, sizeof( key_lines[edit_line] ) - 1 );
		key_linepos = (unsigned int)strlen( key_lines[edit_line] );
		input_prestep = 0;
		search_line = 0;
		search_text[0] = 0;
	}

	// decide whether to treat the text as chat or command
	p = key_lines[edit_line] + 1;
	if( cls.state <= CA_HANDSHAKE || cls.demo.playing )
		type = COMMAND;
	else if( *p == '\\' || *p == '/' )
		type = COMMAND;
	else if( ( Key_IsDown(K_RCTRL) || Key_IsDown(K_LCTRL)) && !ignore_ctrl )
		type = TEAMCHAT;
	else if( chatmode == 1 || !Cmd_CheckForCommand( p ) )
		type = CHAT;
	else
		type = COMMAND;

	// do appropriate action
	switch( type )
	{
	case CHAT:
	case TEAMCHAT:
		for( p = key_lines[edit_line] + 1; *p; p++ ) {
			if( *p != ' ' )
				break;
		}
		if( !*p )
			break;		// just whitespace
		Con_SendChatMessage( key_lines[edit_line] + 1, type == TEAMCHAT );
		break;

	case COMMAND:
		p = key_lines[edit_line] + 1;	// skip the "]"
		if( *p == '\\' || ( *p == '/' && p[1] != '/' ) )
			p++;
		Cbuf_AddText( p );
		Cbuf_AddText( "\n" );
		break;
	}

	// echo to the console and cycle command history
	Com_Printf( "%s\n", key_lines[edit_line] );
	edit_line = ( edit_line + 1 ) & 31;
	history_line = edit_line;
	search_line = edit_line;
	search_text[0] = 0;
	key_lines[edit_line][0] = ']';
	key_lines[edit_line][1] = 0;
	key_linepos = 1;
	if( cls.state == CA_DISCONNECTED )
		SCR_UpdateScreen(); // force an update, because the command
	// may take some time
}

/*
* Con_HistoryUp
*/
static void Con_HistoryUp( void )
{
	do
	{
		history_line = ( history_line - 1 ) & 31;
	} while( history_line != edit_line && !key_lines[history_line][1] );

	if( history_line == edit_line )
		history_line = ( edit_line+1 )&31;

	Q_strncpyz( key_lines[edit_line], key_lines[history_line], sizeof( key_lines[edit_line] ) );
	key_linepos = (unsigned int)strlen( key_lines[edit_line] );
	input_prestep = 0;
}

/*
* Con_HistoryDown
*/
static void Con_HistoryDown( void )
{
	if( history_line == edit_line )
		return;

	do
	{
		history_line = ( history_line + 1 ) & 31;
	} while( history_line != edit_line && !key_lines[history_line][1] );

	if( history_line == edit_line )
	{
		key_lines[edit_line][0] = ']';
		key_lines[edit_line][1] = 0;
		key_linepos = 1;
	}
	else
	{
		Q_strncpyz( key_lines[edit_line], key_lines[history_line], sizeof( key_lines[edit_line] ) );
		key_linepos = (unsigned int)strlen( key_lines[edit_line] );
		input_prestep = 0;
	}
}

/*
* Con_KeyDown
* 
* Interactive line editing and console scrollback except for ascii char
*/
void Con_KeyDown( int key )
{
	qboolean ctrl_is_down = Key_IsDown( K_LCTRL ) || Key_IsDown( K_RCTRL );

	if( !con_initialized )
		return;

	if( cls.state == CA_GETTING_TICKET || cls.state == CA_CONNECTING || cls.state == CA_CONNECTED )
		return;

	key = Con_NumPadValue( key );

	if( ( ( key == K_INS ) || ( key == KP_INS ) ) && ( Key_IsDown(K_LSHIFT) || Key_IsDown(K_RSHIFT)) )
	{
		Con_Key_Paste( qtrue );
		return;
	}

	if( key == K_ENTER || key == KP_ENTER )
	{
		Con_Key_Enter( qfalse );
		return;
	}

	if( key == K_TAB )
	{
		// command completion
		Con_CompleteCommandLine();
		return;
	}

	if( ( key == K_LEFTARROW ) || ( key == KP_LEFTARROW ) )
	{
		int charcount;
		// jump over invisible color sequences
		charcount = Q_ColorCharCount( key_lines[edit_line], key_linepos );
		if( charcount > 1 )
		{
			key_linepos = Q_ColorCharOffset( key_lines[edit_line], charcount - 1 );
			key_linepos = Q_Utf8SyncPos( key_lines[edit_line], key_linepos,	UTF8SYNC_LEFT );
			key_linepos = max( key_linepos, 1 ); // Q_Utf8SyncPos could jump over [
		}
		return;
	}

	if( key == K_BACKSPACE )
	{
		if( key_linepos > 1 )
		{
			int oldwidth = SCR_strWidth( key_lines[edit_line], cls.consoleFont, key_linepos );
			int newwidth;

			// skip to the end of color sequence
			while( 1 )
			{
				char c, *tmp = key_lines[edit_line] + key_linepos;
				if( Q_GrabCharFromColorString( ( const char ** )&tmp, &c, NULL ) == GRABCHAR_COLOR )
					key_linepos = tmp - key_lines[edit_line]; // advance, try again
				else	// GRABCHAR_CHAR or GRABCHAR_END
					break;
			}

			{
				int oldpos = key_linepos;
				key_linepos = Q_Utf8SyncPos( key_lines[edit_line], key_linepos - 1,
					UTF8SYNC_LEFT );
				key_linepos = max( key_linepos, 1 ); // Q_Utf8SyncPos could jump over [
				strcpy( key_lines[edit_line] + key_linepos,
					key_lines[edit_line] + oldpos );	// safe!
			}

			// keep the cursor in the same on-screen position if possible
			newwidth = SCR_strWidth( key_lines[edit_line], cls.consoleFont, key_linepos );
			input_prestep += ( newwidth - oldwidth );
			clamp_low( input_prestep, 0 );
		}

		return;
	}

	if( key == K_DEL )
	{
		char *s = key_lines[edit_line] + key_linepos;
		int wc = Q_GrabWCharFromUtf8String( ( const char ** )&s );
		if( wc )
			strcpy( key_lines[edit_line] + key_linepos, s );	// safe!
		return;
	}

	if( ( key == K_RIGHTARROW ) || ( key == KP_RIGHTARROW ) )
	{
		if( strlen( key_lines[edit_line] ) == key_linepos )
		{
			if( strlen( key_lines[( edit_line + 31 ) & 31] ) <= key_linepos )
				return;

			key_lines[edit_line][key_linepos] = key_lines[( edit_line + 31 ) & 31][key_linepos];
			key_linepos++;
			key_lines[edit_line][key_linepos] = 0;
		}
		else
		{
			int charcount;

			// jump over invisible color sequences
			charcount = Q_ColorCharCount( key_lines[edit_line], key_linepos );
			key_linepos = Q_ColorCharOffset( key_lines[edit_line], charcount + 1 );
			key_linepos = Q_Utf8SyncPos( key_lines[edit_line], key_linepos, UTF8SYNC_RIGHT );
		}
		return;
	}

	if( ( key == K_UPARROW ) || ( key == KP_UPARROW ) )
	{
		Con_HistoryUp();
		return;
	}

	if( ( key == K_DOWNARROW ) || ( key == KP_DOWNARROW ) )
	{
		Con_HistoryDown();
		return;
	}
	
	if( key == K_MWHEELUP || key == K_MWHEELDOWN )
	{
		if( ctrl_is_down ) {
			Con_ChangeFontSize( key == K_MWHEELUP ? 1 : -1 );
			return;
		}
	}

	if( key == K_PGUP || key == KP_PGUP || key == K_MWHEELUP ) // wsw : pb : support mwheel in console
	{
		con.display += 2;
		clamp_high( con.display, con.numlines - 1 );
		clamp_low( con.display, 0 );	// in case con.numlines is 0
		return;
	}

	if( key == K_PGDN || key == KP_PGDN || key == K_MWHEELDOWN ) // wsw : pb : support mwheel in console
	{
		con.display -= 2;
		clamp_low( con.display, 0 );
		return;
	}

	if( key == K_HOME || key == KP_HOME )
	{
		if( ctrl_is_down )
		{
			int smallCharHeight = SCR_strHeight( cls.consoleFont );
			int vislines = (int)( viddef.height * bound( 0.0, scr_con_current, 1.0 ) );
			int rows = ( vislines - smallCharHeight - (int)( 14 * VID_GetPixelRatio() ) ) / smallCharHeight;  // rows of text to draw
			con.display = con.numlines - rows + 1;
			clamp_low( con.display, 0 );
		}
		else
			key_linepos = 1;
		return;
	}

	if( key == K_END || key == KP_END )
	{
		if( ctrl_is_down )
			con.display = 0;
		else
			key_linepos = (unsigned int)strlen( key_lines[edit_line] );
		return;
	}

	// key is a normal printable key normal which wil be HANDLE later in response to WM_CHAR event
}

//============================================================================

/*
* Con_MessageKeyPaste
*/
static void Con_MessageKeyPaste( qboolean primary )
{
	char *cbd;
	char *tok;

	cbd = CL_GetClipboardData( primary );
	if( cbd )
	{
		int i;

		tok = strtok( cbd, "\n\r\b" );

		// only allow pasting of one line for malicious reasons
		if( tok != NULL )
		{
			i = (int)strlen( tok );
			if( i + chat_linepos >= MAXCMDLINE )
				i = MAXCMDLINE - chat_linepos;

			if( i > 0 )
			{
				Q_strncatz( chat_buffer, tok, sizeof( chat_buffer ) );
				chat_linepos += i;
				chat_bufferlen += i;
			}

			tok = strtok( NULL, "\n\r\b" );

			if( tok != NULL )
			{
				Con_SendChatMessage( chat_buffer, chat_team );
				chat_bufferlen = 0;
				chat_linepos = 0;
				chat_buffer[0] = 0;
				CL_SetKeyDest( key_game );
			}
		}

		CL_FreeClipboardData( cbd );
	}
}

/*
* Con_MessageCharEvent
*/
void Con_MessageCharEvent( qwchar key )
{
	if( !con_initialized )
		return;

	key = Con_NumPadValue( key );

	switch( key )
	{
	case 12:
		// CTRL - L : clear
		chat_bufferlen = 0;
		chat_linepos = 0;
		memset( chat_buffer, 0, MAXCMDLINE );
		return;
	case 1: // CTRL+A: jump to beginning of line (same as HOME)
		chat_linepos = 0;
		return;
	case 5: // CTRL+E: jump to end of line (same as END)
		chat_linepos = chat_bufferlen;
		return;
	case 22: // CTRL - V : paste
		Con_MessageKeyPaste( qfalse );
		return;
	}

	if( key < 32 || key > 0x1FFFFF )
		return; // non-printable

	if( chat_linepos < MAXCMDLINE-1 )
	{
		const char *utf = Q_WCharToUtf8( key );
		size_t utflen = strlen( utf );

		if( chat_bufferlen + utflen >= MAXCMDLINE )
			return;		// won't fit

		// move remainder to the right
		memmove( chat_buffer + chat_linepos + utflen,
			chat_buffer + chat_linepos,
			strlen( chat_buffer + chat_linepos ) + 1 );	// +1 for trailing 0

		// insert the char sequence
		memcpy( chat_buffer + chat_linepos, utf, utflen );
		chat_bufferlen += utflen;
		chat_linepos += utflen;
	}
}

/*
* Con_MessageCompletion
*/
static void Con_MessageCompletion( const char *partial, qboolean teamonly )
{
	char comp[256];
	size_t comp_len;
	size_t partial_len;
	char **args;
	const char *p;

	// only complete at the end of the line
	if( chat_linepos != chat_bufferlen )
		return;

	p = strrchr( chat_buffer, ' ' );
	if( p && *(p+1) ) {
		partial = p + 1;
	}
	else {
		partial = chat_buffer;
	}

	comp[0] = '\0';

	args = Cmd_CompleteBuildArgListExt( teamonly ? "say_team" : "say", partial );
	if( args ) {
		int i;

		// check for single match
		if( args[0] && !args[1] ) {
			Q_strncpyz( comp, args[0], sizeof( comp ) );
		}
		else if( args[0] ) {
			char ch;
			size_t start_pos, pos;

			start_pos = strlen( partial );
			for( pos = start_pos; pos + 1 < sizeof( comp ); pos++ ) {
				ch = args[0][pos];
				if( !ch )
					break;
				for( i = 1; args[i] && args[i][pos] == ch; i++ );
				if( args[i] )
					break;
			}
			Q_strncpyz( comp, args[0], sizeof( comp ) );
			comp[pos] = '\0';
		}

		Mem_Free( args );
	}

	if( comp[0] == '\0' ) {
		return;
	}

	partial_len = strlen( partial );
	comp_len = strlen( comp );

	// add ': ' to string if completing at the beginning of the string
	if( comp[0] && ( chat_linepos == partial_len ) && ( chat_bufferlen + comp_len + 2 < MAXCMDLINE ) ) {
		Q_strncatz( comp, ", ", sizeof( comp ) );
		comp_len += 2;
	}

	if( chat_bufferlen + comp_len >= MAXCMDLINE )
		return;		// won't fit

	chat_linepos -= partial_len;
	chat_bufferlen -= partial_len;
	memcpy( chat_buffer + chat_linepos, comp, comp_len + 1 );
	chat_bufferlen += comp_len;
	chat_linepos += comp_len;
}

/*
* Con_MessageKeyDown
*/
void Con_MessageKeyDown( int key )
{
	qboolean ctrl_is_down = Key_IsDown( K_LCTRL ) || Key_IsDown( K_RCTRL );

	if( !con_initialized )
		return;

	key = Con_NumPadValue( key );

	if( key == K_ENTER || key == KP_ENTER )
	{
		if( chat_bufferlen > 0 )
		{
			Con_SendChatMessage( chat_buffer, chat_team || ctrl_is_down );
			chat_bufferlen = 0;
			chat_linepos = 0;
			chat_buffer[0] = 0;
		}

		CL_SetKeyDest( key_game );
		return;
	}

	if( key == K_HOME || key == KP_HOME )
	{
		if( !ctrl_is_down )
			chat_linepos = 0;
		return;
	}

	if( key == K_END || key == KP_END )
	{
		if( !ctrl_is_down )
			chat_linepos = chat_bufferlen;
		return;
	}

	if( ( ( key == K_INS ) || ( key == KP_INS ) ) && ( Key_IsDown(K_LSHIFT) || Key_IsDown(K_RSHIFT) ) )
	{
		Con_MessageKeyPaste( qtrue );
		return;
	}

	if( key == K_TAB )
	{
		Con_MessageCompletion( chat_buffer, chat_team || ctrl_is_down );
		return;
	}

	if( ( key == K_LEFTARROW ) || ( key == KP_LEFTARROW ) )
	{
		if( chat_linepos > 0 )
		{
			int charcount;

			// jump over invisible color sequences
			charcount = Q_ColorCharCount( chat_buffer, chat_linepos );
			chat_linepos = Q_ColorCharOffset( chat_buffer, charcount - 1 );
			chat_linepos = Q_Utf8SyncPos( chat_buffer, chat_linepos, UTF8SYNC_LEFT );
		}
		return;
	}

	if( ( key == K_RIGHTARROW ) || ( key == KP_RIGHTARROW ) )
	{
		if( chat_linepos < chat_bufferlen )
		{
			int charcount;

			// jump over invisible color sequences
			charcount = Q_ColorCharCount( chat_buffer, chat_linepos );
			chat_linepos = Q_ColorCharOffset( chat_buffer, charcount + 1 );
			chat_linepos = Q_Utf8SyncPos( chat_buffer, chat_linepos, UTF8SYNC_RIGHT );
		}
		return;
	}

	if( key == K_DEL )
	{
		char *s = chat_buffer + chat_linepos;
		int wc = Q_GrabWCharFromUtf8String( ( const char ** )&s );
		if( wc )
		{
			strcpy( chat_buffer + chat_linepos, s );	// safe!
			chat_bufferlen -= (s - (chat_buffer + chat_linepos));
		}
		return;
	}

	if( key == K_BACKSPACE )
	{
		if( chat_linepos )
		{
			int oldpos = chat_linepos;

			// skip to the end of color sequence
			while( 1 )
			{
				char c, *tmp = chat_buffer + chat_linepos;
				if( Q_GrabCharFromColorString( ( const char ** )&tmp, &c, NULL ) == GRABCHAR_COLOR )
					chat_linepos = tmp - chat_buffer; // advance, try again
				else	// GRABCHAR_CHAR or GRABCHAR_END
					break;
			}

			chat_linepos = Q_Utf8SyncPos( chat_buffer, chat_linepos - 1, UTF8SYNC_LEFT );
			strcpy( chat_buffer + chat_linepos, chat_buffer + oldpos );	// safe!
			chat_bufferlen -= (oldpos - chat_linepos);
		}
		return;
	}

	if( key == K_ESCAPE )
	{
		CL_SetKeyDest( key_game );
		chat_bufferlen = 0;
		chat_linepos = 0;
		chat_buffer[0] = 0;
		return;
	}
}

/*
* Con_TouchDown
*/
static void Con_TouchDown( int x, int y )
{
	int smallCharHeight = SCR_strHeight( cls.consoleFont );

	if( cls.key_dest == key_console )
	{
		if( touch_x >= 0 )
			return;

		if( touch_y >= 0 )
		{
			int dist = ( y - touch_y ) / smallCharHeight;
			con.display += dist;
			clamp_high( con.display, con.numlines - 1 );
			clamp_low( con.display, 0 );
			touch_y += dist * smallCharHeight;
		}
		else if( scr_con_current )
		{
			if( y < ( ( viddef.height * scr_con_current ) - (int)( 14 * VID_GetPixelRatio() ) - smallCharHeight ) )
			{
				touch_x = -1;
				touch_y = y;
			}
			else if( y < ( viddef.height * scr_con_current ) )
			{
				touch_x = x;
				touch_y = y;
			}
		}
	}
	else if( cls.key_dest == key_message )
	{
		touch_x = x;
		touch_y = y;
	}
}

/*
* Con_TouchUp
*/
static void Con_TouchUp( int x, int y )
{
	if( ( touch_x < 0 ) && ( touch_y < 0 ) )
		return;

	if( ( x < 0 ) || ( y < 0 ) )
	{
		touch_x = touch_y = -1;
		return;
	}

	if( cls.key_dest == key_console )
	{
		if( touch_x >= 0 )
		{
			int smallCharHeight = SCR_strHeight( cls.consoleFont );

			if( ( x - touch_x ) >= ( smallCharHeight * 4 ) )
				Con_CompleteCommandLine();
			else if( ( y - touch_y ) >= ( smallCharHeight * 2 ) )
				Con_HistoryUp();
			else if( ( touch_y - y ) >= ( smallCharHeight * 2 ) )
				Con_HistoryDown();
			else
				IN_ShowIME( qtrue );
		}
	}
	else if( cls.key_dest == key_message )
	{
		int x1, y1, x2, y2;
		Con_GetMessageArea( &x1, &y1, &x2, &y2 );
		if( ( x >= x1 ) && ( y >= y1 ) && ( x < x2 ) && ( y < y2 ) )
			IN_ShowIME( qtrue );
	}

	touch_x = touch_y = -1;
}

/*
* Con_TouchEvent
*/
void Con_TouchEvent( qboolean down, int x, int y )
{
	if( !con_initialized )
		return;

	if( down )
		Con_TouchDown( x, y );
	else
		Con_TouchUp( x, y );
}

//============================================================================
