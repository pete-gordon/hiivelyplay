/*===================	========================
 *        GRRLIB (GX version) 3.0 alpha
 *        Code     : NoNameNo
 *        GX hints : RedShade
 *
 *        Template Code (Minimum Requirement)
 * ============================================*/




#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <malloc.h>
#include <math.h>
#include <ogcsys.h>
#include <gccore.h>
#include <wiiuse/wpad.h>
#include <fat.h>
#include <sys/dir.h>
#include <errno.h>
#include <asndlib.h>

#include "types.h"
#include "hvl_replay.h"

#include "GRRLIB/GRRLIB.h"

#include "gfx/dirtop.h"
#include "gfx/dirbot.h"
#include "gfx/btnplay.h"
#include "gfx/btnstop.h"
#include "gfx/btninfo.h"
#include "gfx/btnnext.h"
#include "gfx/btnprev.h"
#include "gfx/btnexit.h"
#include "gfx/btnup.h"
#include "gfx/btndown.h"
#include "gfx/player1_point.h"
#include "gfx/dejfont.h"
#include "gfx/dejfont2.h"
#include "gfx/infobox.h"

Mtx GXmodelView2D;

u8 *tex_dirtop;
u8 *tex_dirbot;
u8 *tex_pointer;
u8 *tex_font1, *tex_font2;
u8 *tex_infobox;

#define AUDIO_BUFFERS 2

int mixitplease[AUDIO_BUFFERS], nextmixit;
int rumblestate=0, rumblecount=0;
int subsongnum;

// Info box variables
int infoboxy, infoboxy_target, infotop;
BOOL infobox_visible;

// Input variables
int button_over;
int bpressed = -1;
ir_t wiimote_cursor;
u32 wiimote_held_buttons, wiimote_down_buttons;
int goneupordown;

// Gui state variables
int reptim1, reptim2;
int selfile, topfile, actfile;

#define FREQ 48000
#define HIVELY_LEN FREQ/50
#define OUTPUT_LEN 4096

#define MAXPATHLEN 256
#define SHOWNAMELEN 40
#define TUNE_FOLDER "hvltunes"

char *scrollatxt =	"Welcome to HiivelyPlay v2.0 brought to you by IRIS in AD 2008   ::   "
"Code & GFX by Xeron/IRIS  -  Uses GRRLib by NoNameNo  -  Deja Vu font from dejavu.sf.net   ::   "
"Thanks to everyone who directly or indirectly contributed to HivelyTracker, especially Spot, "
"BuZz, pieknyman, Syphus, FluBBa, Dexter, Pink, Itix and Per Johansson.   ::   "
"Greetings to everyone active on the Amiga, C64 and Wii demoscenes!   ::   "
"Get the latest HivelyTracker software, news and info from www.hivelytracker.com !   ::   ";

char *scrollbtxt =	"Controls: On the main screen use the up and down arrows to scroll through the list of tunes. "
"The play button will start playing the tune highighted with the black outline box. Alternatively "
"you can just click directly on a song to start it playing. To stop playback, hit the stop button. "
"You can also scroll up and down in the list with the DPad. If the file has any subsongs, "
"you can select them with left/right on the DPad. The 'Next' and 'Prev' buttons will skip to the "
"next or previous song if one is already playing. The 'info' button brings up a box containing "
"information about the currently playing song. The 'exit' button quits. In the info panel, if "
"there are more than 10 instruments, you can scroll through them with up/down on the DPad.    ::   ";


char scrollabuf[68], scrollbbuf[68];
int scrollaoffs=0, scrollax=-10;
int scrollboffs=0, scrollbx=-10;

struct foundtune
{
	char name[MAXPATHLEN];
	char showname[SHOWNAMELEN];
	int size;
};

struct button
{
	u8 *tex;
	const unsigned char *origtex;
	int x, y, w, h;
};

struct foundtune *tunelist = NULL;
int numfiles=0, afiles=0;
float fadd;
u32 acticol;

struct hvl_tune *tune = NULL;

static vu32 curr_audio = 0;
static u8 audioBuf[AUDIO_BUFFERS][OUTPUT_LEN] ATTRIBUTE_ALIGN(32);

static BOOL sndPlaying = FALSE;

int16 hivelyLeft[HIVELY_LEN], hivelyRight[HIVELY_LEN];
size_t hivelyIndex=0;

#define NUMBUTTONS 8
struct button buttons[] = {	{ NULL, btnplay,  40, 336,  56, 56 },
{ NULL, btnstop,  96, 336,  56, 56 },
{ NULL, btnprev, 152, 336, 112, 56 },
{ NULL, btnnext, 264, 336, 112, 56 },
{ NULL, btninfo, 376, 336, 112, 56 },
{ NULL, btnexit, 488, 336, 112, 56 },
{ NULL, btnup,   580,  64,  56, 56 },
{ NULL, btndown, 580, 240,  56, 56 } };

enum button_name
{
	BUTTON_PLAY = 0,
	BUTTON_STOP = 1,
	BUTTON_PREV = 2,
	BUTTON_NEXT = 3,
	BUTTON_INFO = 4,
	BUTTON_EXIT = 5,
	BUTTON_UP = 6,
	BUTTON_DOWN = 7
};


// Called when more audio data is needed in a buffer
static void mixcallback(void *usrdata,u8 *stream,u32 length)
{
	int16 *out;
	int i, alen;
	size_t streamPos;

	alen = length>>1;
	streamPos = 0;

	if( tune )
	{
		// Mix to 16bit interleaved stereo
		out = (int16*) stream;
		// Flush remains of previous frame
		for(i = hivelyIndex; i < (HIVELY_LEN) && streamPos < alen; i++)
		{
			out[streamPos++] = hivelyLeft[i];
			out[streamPos++] = hivelyRight[i];
		}

		while(streamPos < alen)
		{
			hvl_DecodeFrame( tune, (int8 *) hivelyLeft, (int8 *) hivelyRight, 2 );
			for(i = 0; i < (HIVELY_LEN) && streamPos < alen; i++)
			{
				out[streamPos++] = hivelyLeft[i];
				out[streamPos++] = hivelyRight[i];
			}
		}
		hivelyIndex = i;
	}

	DCFlushRange(stream,length);
}

void handle_mixing( void )
{
	while( mixitplease[nextmixit] )
	{
		mixitplease[nextmixit] = 0;
		mixcallback( NULL, (u8*)audioBuf[nextmixit], OUTPUT_LEN );
		nextmixit = (nextmixit+1)%AUDIO_BUFFERS;
	}
}

// Callback tied to ASND
// is called when the buffer associated with voiceNumber has been played
static void asndCallback(s32 voiceNumber)
{
	DCFlushRange( audioBuf[curr_audio], OUTPUT_LEN );
	ASND_AddVoice(voiceNumber, audioBuf[curr_audio], OUTPUT_LEN);

	curr_audio = (curr_audio+1)%AUDIO_BUFFERS;
	mixitplease[curr_audio] = 1;
}

void initmusicstuff( void )
{
	hvl_InitReplayer();
	sndPlaying = FALSE;

	int i;
	for( i=0; i<AUDIO_BUFFERS; i++ )
	{
		mixitplease[i] = 0;
	}
	nextmixit = 0;

	ASND_Init();
}


// Text printing functions
void dejprint( u8 *font, s32 x, s32 y, char *str, u8 alpha )
{
	int i;

	for( i=0; str[i]; i++, x+=10 )
	{
		if( ( str[i] < 33 ) || ( str[i] > 126 ) )
			continue;

		GRRLIB_DrawTile( x, y, 10, 24, font, 0, 1.0f, 1.0f, alpha, str[i]-33, 96 );
	}
}

void dejshadow( u8 *font, s32 x, s32 y, char *str, u8 malpha )
{
	dejprint( tex_font1, x+2, y+2, str, 0x20 );
	dejprint( font, x, y, str, malpha );
}

void dejcentre( u8 *font, s32 y, char *str )
{
	int xp;
	
	xp = 320-strlen(str)*5;
	dejprint( tex_font1, xp+2, y+2, str, 0x20 );
	dejprint( font, xp, y, str, 0xff );
}


void showdirlist( struct foundtune *list, int top, int num, int sel, int active )
{
	int i;

	if( !list ) return;

	if( ( active != -1 ) && ( active >= top ) && ( active < (top+12) ) )
		GRRLIB_Rectangle( 4, 58+(active-top)*20, 572, 22, 0x80000000|acticol, 1 );
	
	if( ( sel != -1 ) && ( sel >= top ) && ( sel < (top+12) ) )
		GRRLIB_Rectangle( 4, 58+(sel-top)*20, 572, 22, 0x80000000, 0 );

	for( i=0; i<12; i++ )
	{
		if( (i+top) >= num ) break;
		dejshadow( tex_font1, 290 - (strlen(list[i+top].showname)*5), 58+i*20,list[i+top].showname, 0xff );
	}
}

void load_tune(struct dirent* entry, char* path)
{
	struct stat buf;

	snprintf(tunelist[numfiles].name, MAXPATHLEN, "%s/%s", path, entry->d_name);

	strncpy( tunelist[numfiles].showname, entry->d_name, SHOWNAMELEN );
	tunelist[numfiles].showname[SHOWNAMELEN-1] = 0;
	int j = strlen( tunelist[numfiles].showname );
	if( j > 4 )
	{
		if( ( strncmp( &tunelist[numfiles].showname[j-4], ".hvl", SHOWNAMELEN ) == 0 ) ||
			( strncmp( &tunelist[numfiles].showname[j-4], ".ahx", SHOWNAMELEN) == 0 ) ||
			( strncmp( &tunelist[numfiles].showname[j-4], ".HVL", SHOWNAMELEN ) == 0 ) ||
			( strncmp( &tunelist[numfiles].showname[j-4], ".AHX", SHOWNAMELEN ) == 0 ) )
		{
			tunelist[numfiles].showname[j-4] = 0;
		}

		if( ( strncmp( tunelist[numfiles].showname, "hvl.", 4 ) == 0 ) ||
			( strncmp( tunelist[numfiles].showname, "ahx.", 4 ) == 0 ) ||
			( strncmp( tunelist[numfiles].showname, "HVL.", 4 ) == 0 ) ||
			( strncmp( tunelist[numfiles].showname, "AHX.", 4 ) == 0 ) )
		{
			j = strlen( tunelist[numfiles].showname );
			for( int i=4; i<j; i++ )
				tunelist[numfiles].showname[i-4] = tunelist[numfiles].showname[i];
		}
	}
	errno = 0;
	stat(tunelist[numfiles].name, &buf);
	tunelist[numfiles].size = -1;
	if (errno == 0)
	{
		tunelist[numfiles].size = buf.st_size;
	}

	numfiles++;
	if( numfiles == afiles )
	{
		afiles += 4;
		tunelist = realloc( tunelist, afiles * sizeof( struct foundtune ) );
	}
}

void scan_dir_recursive(char* folder )
{
	struct dirent* entry;
	DIR* directory = opendir(folder);
	if (directory == NULL)
	{
		return;
	}

	while ((entry = readdir(directory)) != NULL) {
		if (errno == EBADF)
		{
			errno = 0;
			break;
		}
		if (entry->d_type == DT_DIR)
		{
			char path[1024];
			// Skip . and ..
			if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
			{
				continue;
			}
			snprintf(path, sizeof(path), "%s/%s", folder, entry->d_name);
			scan_dir_recursive(path);
		}
		else if (entry->d_type == DT_REG) {
			load_tune(entry, folder);
		}
	}
	closedir(directory);
}

void scan_tunes_dir(void)
{
	if( tunelist == NULL )
	{
		numfiles = 0;
		afiles = 8;
		tunelist = (struct foundtune *)malloc( sizeof( struct foundtune ) * afiles );
	}
	errno = 0;
	scan_dir_recursive(TUNE_FOLDER);
}

void deinitmusicstuff(void)
{
	ASND_End();
}

void stopmusic( void )
{
	if(!sndPlaying) return;

	ASND_Pause(1);
	ASND_StopVoice(0);

	curr_audio = 0;
	sndPlaying = FALSE;
}


void startmusic( void )
{
	int i;

	if( sndPlaying ) return;

	for( i=0; i<AUDIO_BUFFERS; i++ )
	{
		memset( audioBuf[i], 0, OUTPUT_LEN );
		DCFlushRange( audioBuf[i], OUTPUT_LEN );
		mixitplease[i] = 1;
	}
	nextmixit = 0;

	curr_audio = 0;
	sndPlaying = TRUE;

	ASND_SetVoice(
		0, // Voice number
		VOICE_STEREO_16BIT,
		FREQ,
		0, // start delay
		audioBuf[curr_audio],
		OUTPUT_LEN,
		255,255, // Volumes
		asndCallback
	);

	ASND_Pause(0);

	curr_audio++;
}

void playtune( int which )
{
	char fname[MAXPATHLEN+12];
	fname[0] = '\0';

	if( tune )
	{
		stopmusic();
		hvl_FreeTune( tune );
		tune = NULL;
	}

	snprintf(fname, MAXPATHLEN-1, "%s", tunelist[which].name);

	tune = hvl_LoadTune( fname, FREQ, 0, tunelist[which].size );
	if( !tune )
	{
		return;
	}

	subsongnum = 0;
	BOOL subsongOk = hvl_InitSubsong( tune, subsongnum );
	if (subsongOk == TRUE)
	{
		startmusic();
	}
}

void stoptune( void )
{
	stopmusic();
	if( tune )
	{
		hvl_FreeTune( tune );
		tune = NULL;
	}
}


int overbutton( int x, int y )
{
	for( int i=0; i<NUMBUTTONS; i++ )
	{
		if( ( x >= buttons[i].x ) &&
			( x < (buttons[i].x+buttons[i].w) ) &&
			( y >= buttons[i].y ) &&
			( y < (buttons[i].y+buttons[i].h) ) )
			return i;
	}

	return -1;
}

void go_up( void )
{
	goneupordown = 1;

	if( reptim1 > 0 )
	{
		reptim1--;
	} else if( reptim2 > 0 ) {
		reptim2--;
	} else {
		if( selfile > 0 )
		{
			selfile--;
			if( selfile < topfile ) topfile=selfile;
		}					

		if( reptim1 == -1 )
		{
			reptim1 = 25;
		} else {
			reptim2 = 2;
		}
	}
}

void go_down( void )
{
	goneupordown = 1;

	if( reptim1 > 0 )
	{
		reptim1--;
	} else if( reptim2 > 0 ) {
		reptim2--;
	} else {
		if( selfile < (numfiles-1) )
		{
			selfile++;
			if( selfile >= (topfile+12) )
				topfile = selfile-11;
		}

		if( reptim1 == -1 )
		{
			reptim1 = 25;
		} else {
			reptim2 = 2;
		}
	}
}

void quitprogram(void)
{
	stoptune();
	deinitmusicstuff();
	sleep( 5 );
	exit( 0 );
}

void read_wiimote(ir_t* ir)
{
	WPAD_IR( 0, ir );
	WPAD_ScanPads();

	if( !ir->smooth_valid )
	{
		ir->sx = -96;
		ir->sy = -96;
	}

	wiimote_held_buttons = WPAD_ButtonsHeld(0);
	wiimote_down_buttons = WPAD_ButtonsDown(0);
}

void draw_vumeters( void )
{
	int vx, vu, i;

	fadd += 0.04f;
	if( fadd > (3.14159265f*2.0f) ) fadd -= 3.14159265f*2.0f;
	
	acticol = 	( ((int)(sin(fadd)*64.0f+171.0f)) << 16 ) |
	( ((int)(cos(fadd)*64.0f+171.0f)) << 8 ) |
	( ((int)(sin(fadd*0.662f)*64.0f+171.0f)) );

	if( !tune ) return;

	vx = 320 - tune->ht_Channels * 16;
	for( i=0; i<tune->ht_Channels; i++, vx += 32 )
	{
		vu = tune->ht_Voices[i].vc_VUMeter >> 7;
		if( vu > 128 ) vu = 128;
		GRRLIB_Rectangle( vx+2, 460-vu, 30, vu+2, 0x40000000|acticol, 1 );
		GRRLIB_Rectangle( vx, 458-vu, 30, vu+2, 0xff000000|acticol, 1 );
	}
}

void do_scroll( void )
{
	int i;

	dejshadow( tex_font2, scrollax, 415, scrollabuf, 0xff );
	dejshadow( tex_font2, scrollbx, 433, scrollbbuf, 0xa0 );

	
	scrollax-=3;
	if( scrollax <= -20 )
	{
		scrollax += 10;
		for( i=0; i<65; i++ )
			scrollabuf[i] = scrollabuf[i+1];
		scrollabuf[65] = scrollatxt[scrollaoffs];
		scrollaoffs++;
		if( scrollatxt[scrollaoffs] == 0 )
			scrollaoffs = 0;
	}

	scrollbx-=2;
	if( scrollbx <= -20 )
	{
		scrollbx = -10;
		for( i=0; i<65; i++ )
			scrollbbuf[i] = scrollbbuf[i+1];
		scrollbbuf[65] = scrollbtxt[scrollboffs];
		scrollboffs++;
		if( scrollbtxt[scrollboffs] == 0 )
			scrollboffs = 0;
	}
}

void draw_interface( int button_over )
{
	int i;

	GRRLIB_DrawImg( (640-516)/2,   32, 516, 48, tex_dirtop, 0, 1.0f, 1.0f, 255 );
	GRRLIB_DrawImg( (640-516)/2,  280, 516, 48, tex_dirbot, 0, 1.0f, 1.0f, 255 );
	showdirlist( tunelist, topfile, numfiles, selfile, actfile );
	
	for( i=0; i<NUMBUTTONS; i++ )
		GRRLIB_DrawTile(	buttons[i].x, buttons[i].y,
							buttons[i].w, buttons[i].h,
				   buttons[i].tex,
				   0, 1.0f, 1.0f, 255,
				   ((bpressed==i)&&(button_over==bpressed))?1:0,
							2 );
		
}

void draw_infobox( void )
{
	char tmpstr[256];

	GRRLIB_DrawImg( 32, infoboxy, 576, 360, tex_infobox, 0, 1.0f, 1.0f, 255 );
	dejcentre( tex_font2, infoboxy+20, tunelist[actfile].showname );

	dejshadow( tex_font1,  52, infoboxy+50, "Song Title:", 0x80 );
	dejshadow( tex_font1, 182, infoboxy+50, tune->ht_Name, 0xff );

	sprintf( tmpstr, "%3d", tune->ht_InstrumentNr );
	dejshadow( tex_font1,  52, infoboxy+70, "Instruments:", 0x80 );
	dejshadow( tex_font1, 182, infoboxy+70, tmpstr, 0xff );

	sprintf( tmpstr, "%3d", tune->ht_Channels );
	dejshadow( tex_font1, 242, infoboxy+70, "Channels:", 0x80 );
	dejshadow( tex_font1, 372, infoboxy+70, tmpstr, 0xff );

	sprintf( tmpstr, "%3d", tune->ht_SubsongNr );
	dejshadow( tex_font1, 432, infoboxy+70, "Subsongs:", 0x80 );
	dejshadow( tex_font1, 562, infoboxy+70, tmpstr, 0xff );

	sprintf( tmpstr, "%3d", tune->ht_TrackLength );
	dejshadow( tex_font1,  52, infoboxy+90, "Track Len:", 0x80 );
	dejshadow( tex_font1, 182, infoboxy+90, tmpstr, 0xff );

	sprintf( tmpstr, "%3d", tune->ht_PositionNr );
	dejshadow( tex_font1, 242, infoboxy+90, "Positions:", 0x80 );
	dejshadow( tex_font1, 372, infoboxy+90, tmpstr, 0xff );

	sprintf( tmpstr, "%3d", tune->ht_SpeedMultiplier );
	dejshadow( tex_font1, 432, infoboxy+90, "Spd. Mult:", 0x80 );
	dejshadow( tex_font1, 562, infoboxy+90, tmpstr, 0xff );

	for( int i=0; i<10; i++ )
	{
		if( (i+infotop) >= tune->ht_InstrumentNr )
			break;

		sprintf( tmpstr, "%02d:", i+infotop+1 );
		dejshadow( tex_font1, 52, infoboxy+i*20+130, tmpstr, 0x80 );
		dejshadow( tex_font1, 92, infoboxy+i*20+130, tune->ht_Instruments[i+infotop+1].ins_Name, 0xff );
	}
}

void draw_gui(ir_t* ir)
{
	GRRLIB_FillScreen(0xFFE2E0E0);

	draw_vumeters();
	do_scroll();

	if( infobox_visible )
	{
		draw_interface( -1 );
		draw_infobox();
	}
	else
	{
		draw_interface( button_over );
	}

	GRRLIB_DrawImg( ir->sx-48, ir->sy-48, 96, 96, tex_pointer, ir->angle, 1.0f, 1.0f, 255 );
}

void open_infobox( void )
{
	if( !tune ) return;

	infoboxy  = -360;
	infoboxy_target = 40;
	reptim1 = -1;
	reptim2 = 0;
	infobox_visible = TRUE;
}

int main( void )
{
	int i, button_over, clicked;
	
	for( i=0; i<66; i++ )
	{
		scrollabuf[i] = ' ';
		scrollbbuf[i] = ' ';
	}
	scrollabuf[66] = 0;
	scrollbbuf[66] = 0;
	
	wiimote_cursor.sx = -96;
	wiimote_cursor.sy = -96;

	tex_dirtop  = GRRLIB_LoadTexture( dirtop );
	tex_dirbot  = GRRLIB_LoadTexture( dirbot );
	tex_pointer = GRRLIB_LoadTexture( player1_point );
	tex_font1   = GRRLIB_LoadTexture( dejfont );
	tex_font2   = GRRLIB_LoadTexture( dejfont2 );
	tex_infobox = GRRLIB_LoadTexture( infobox );

	for( i=0; i<NUMBUTTONS; i++ )
	{
		buttons[i].tex = GRRLIB_LoadTexture( buttons[i].origtex );
	}

	VIDEO_Init();
	WPAD_Init();
	fatInitDefault();
	initmusicstuff();

	WPAD_SetDataFormat( 0, WPAD_FMT_BTNS_ACC_IR );
	WPAD_SetVRes( 0, 640, 480 );

	GRRLIB_InitVideo();
	GRRLIB_Start();
	
	button_over = -1;
	clicked = 0;
	topfile = 0;
	selfile = 0;
	actfile = -1;
	fadd = 0.0f;
	reptim1 = -1;
	reptim2 = 0;
	infoboxy = -360;
	infoboxy_target = -360;
	infotop=0;
	infobox_visible = FALSE;
	
	scan_tunes_dir();

	while(1){
		// We mix the audio buffers in the foreground
		// because if we do it in the callback, it messes
		// up the GX graphics.
		handle_mixing();

		read_wiimote(&wiimote_cursor);
		
		if( wiimote_held_buttons & WPAD_BUTTON_HOME )
		{
			quitprogram();
		}

		// Different controls when info box is visible
		if (infobox_visible)
		{
			if( tune->ht_InstrumentNr > 10 )
			{
				if( wiimote_held_buttons & WPAD_BUTTON_UP )
				{
					if( reptim1 > 0 )
					{
						reptim1--;
					} else if( reptim2 > 0 ) {
						reptim2--;
					} else {
						if( infotop > 0 )
							infotop--;

						if( reptim1 == -1 )
						{
							reptim1 = 25;
						} else {
							reptim2 = 2;
						}
					}
				} else if( wiimote_held_buttons & WPAD_BUTTON_DOWN ) {
					if( reptim1 > 0 )
					{
						reptim1--;
					} else if( reptim2 > 0 ) {
						reptim2--;
					} else {
						if( infotop < (tune->ht_InstrumentNr-10) )
							infotop++;

						if( reptim1 == -1 )
						{
							reptim1 = 25;
						} else {
							reptim2 = 2;
						}
					}
				} else {
					reptim1 = -1;
					reptim2 = 0;
				}
			}

			// Close infobox
			if( wiimote_down_buttons & WPAD_BUTTON_A )
			{
				infoboxy_target = -360;
			}

			// Approach the target
			if( infoboxy > infoboxy_target ) { infoboxy -= 8; if( infoboxy < infoboxy_target ) infoboxy = infoboxy_target; }
			if( infoboxy < infoboxy_target ) { infoboxy += 8; if( infoboxy > infoboxy_target ) infoboxy = infoboxy_target; }

			// Infobox is off screen
			if( ( infoboxy == infoboxy_target ) && ( infoboxy_target == -360 ) )
			{
				infobox_visible = FALSE;
			}
		}
		else
		{
			goneupordown = 0;

			if( bpressed == -1 )
			{
				int selpointed = 0;

				i = overbutton( wiimote_cursor.sx, wiimote_cursor.sy );
				if( i != button_over )
				{
					button_over = i;
					if( button_over != -1 ) rumblecount = 3;
				}

				if( button_over == -1 )
				{
					int bot;

					bot = numfiles<12 ? numfiles : 12;

					if( ( wiimote_cursor.sy >= 60 ) && ( wiimote_cursor.sy < (60+(bot*20)) ) && ( wiimote_cursor.sx < 580 ) )
					{
						selfile = (wiimote_cursor.sy-60)/20+topfile;
						if( ( wiimote_held_buttons & WPAD_BUTTON_A ) &&
							( actfile != selfile ) )
						{
							playtune( selfile );
							if( tune )
								actfile = selfile;
							else
								actfile = -1;
						}
						selpointed = 1;
					}
				}

				if( selpointed == 0 )
				{
					if( wiimote_held_buttons & WPAD_BUTTON_DOWN )
						go_down();

					if( wiimote_held_buttons & WPAD_BUTTON_UP )
						go_up();
				}

				if (wiimote_held_buttons & WPAD_BUTTON_A)
				{
					if( !clicked )
						bpressed = button_over;
					clicked = 1;
				} else {
					clicked = 0;
				}
			} else {
				button_over = overbutton( wiimote_cursor.sx, wiimote_cursor.sy );

				if( (wiimote_held_buttons & WPAD_BUTTON_A) == 0 )
				{
					if( bpressed == button_over )
					{
						switch( bpressed )
						{
							case BUTTON_PLAY:
								if( ( selfile > -1 ) && ( selfile < numfiles ) )
								{
									playtune( selfile );
									if( tune )
										actfile = selfile;
									else
										actfile = -1;
								}
								break;

							case BUTTON_STOP:
								if( tune )
									stoptune();
							
							actfile = -1;
							break;

							case BUTTON_PREV:
								if( actfile > 0 )
								{
									playtune( actfile-1 );
									if( tune )
										actfile--;
									else
										actfile = -1;
								}
								break;

							case BUTTON_NEXT:
								if( actfile < (numfiles-1) )
								{
									playtune( actfile+1 );
									if( tune )
										actfile++;
									else
										actfile = -1;
								}
								break;

							case BUTTON_INFO:
								open_infobox();
								break;

							case BUTTON_EXIT:
								quitprogram();
								break;
						}
					}

					bpressed = -1;
					clicked = 0;
				} else {
					// Holding a button down
					switch( bpressed )
					{
						case BUTTON_UP:
							go_up();
							break;

						case BUTTON_DOWN:
							go_down();
							break;
					}
				}
			}

			if( !goneupordown )
			{
				reptim1 = -1;
				reptim2 = 0;
			}

			if( rumblecount > 0 )
			{
				rumblecount--;
				if( !rumblestate )
				{
					WPAD_Rumble( 0, 1 );
					rumblestate = 1;
				}
			} else {
				if( rumblestate )
				{
					WPAD_Rumble( 0, 0 );
					rumblestate = 0;
				}
			}

			if( tune )
			{
				if( wiimote_down_buttons & WPAD_BUTTON_RIGHT )
				{
					if( subsongnum < tune->ht_SubsongNr )
					{
						stopmusic();
						subsongnum++;
						hvl_InitSubsong( tune, subsongnum );
						startmusic();
					}
				}

				if( wiimote_down_buttons & WPAD_BUTTON_LEFT )
				{
					if( subsongnum > 0 )
					{
						stopmusic();
						subsongnum--;
						hvl_InitSubsong( tune, subsongnum );
						startmusic();
					}
				}
			}
		}
		draw_gui(&wiimote_cursor);
		GRRLIB_Render();
	}

	return 0;
}
