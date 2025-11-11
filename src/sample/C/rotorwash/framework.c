/*
// Copyright 1995, 1999, 2000 Silicon Graphics, Inc.
// ALL RIGHTS RESERVED
//
// This source code ("Source Code") was originally derived from a
// code base owned by Silicon Graphics, Inc. ("SGI")
// 
// LICENSE: SGI grants the user ("Licensee") permission to reproduce,
// distribute, and create derivative works from this Source Code,
// provided that: (1) the user reproduces this entire notice within
// both source and binary format redistributions and any accompanying
// materials such as documentation in printed or electronic format;
// (2) the Source Code is not to be used, or ported or modified for
// use, except in conjunction with OpenGL Performer; and (3) the
// names of Silicon Graphics, Inc.  and SGI may not be used in any
// advertising or publicity relating to the Source Code without the
// prior written permission of SGI.  No further license or permission
// may be inferred or deemed or construed to exist with regard to the
// Source Code or the code base of which it forms a part. All rights
// not expressly granted are reserved.
// 
// This Source Code is provided to Licensee AS IS, without any
// warranty of any kind, either express, implied, or statutory,
// including, but not limited to, any warranty that the Source Code
// will conform to specifications, any implied warranties of
// merchantability, fitness for a particular purpose, and freedom
// from infringement, and any warranty that the documentation will
// conform to the program, or any warranty that the Source Code will
// be error free.
// 
// IN NO EVENT WILL SGI BE LIABLE FOR ANY DAMAGES, INCLUDING, BUT NOT
// LIMITED TO DIRECT, INDIRECT, SPECIAL OR CONSEQUENTIAL DAMAGES,
// ARISING OUT OF, RESULTING FROM, OR IN ANY WAY CONNECTED WITH THE
// SOURCE CODE, WHETHER OR NOT BASED UPON WARRANTY, CONTRACT, TORT OR
// OTHERWISE, WHETHER OR NOT INJURY WAS SUSTAINED BY PERSONS OR
// PROPERTY OR OTHERWISE, AND WHETHER OR NOT LOSS WAS SUSTAINED FROM,
// OR AROSE OUT OF USE OR RESULTS FROM USE OF, OR LACK OF ABILITY TO
// USE, THE SOURCE CODE.
// 
// Contact information:  Silicon Graphics, Inc., 
// 1600 Amphitheatre Pkwy, Mountain View, CA  94043, 
// or:  http://www.sgi.com
//
//
//
// Command-line options:
//  -b	: norborder window
//  -f	: full screen
//  -F	: put X input handling in a forked process
//  -m procsplit : multiprocess mode
//  -w	: write scene to file
// 
// Run-time controls:
//       ESC-key: exits
//    Left-mouse: advance
//  Middle-mouse: stop
//   Right-mouse: retreat
*/

#include <stdlib.h>
#include <string.h>
#include <math.h>
#ifdef __linux__
#include <pthread.h>
#define __USE_UNIX98
#endif
#include <signal.h> /* for sigset for forked X event handler process  */
#ifndef WIN32
#include <getopt.h> /* for cmdline handler  */
#include <X11/keysym.h>
#endif


#include <Performer/pf.h>
#include <Performer/pr.h>

#include <Performer/pfutil.h>
#include <Performer/pfdu.h>
#include "sea.h"

/*
 structure that resides in shared memory so that the 
 application, cull, and draw processes can access it.
*/

typedef struct
{
    pfPipeWindow    *pw;
    int		    exitFlag;
    int		    inWindow, reset;
    float	    mouseX, mouseY;
    pfCoord	    heli, heliOrig;
    pfVec3          heliveli, helithpr;
    float           radii[4];
    pfVec3          *coords3;
    pfVec2          *Tcoords;
    int		    winSizeX, winSizeY;
    int		    mouseButtons;
    pfCoord	    view, viewOrig;
    float	    accelRate;
    float	    sceneSize;
    int		    drawStats;
    int		    drawWire;
    int		    drawHighlight;
    int		    XInputInited;
} SharedData;

static SharedData *Shared;


/* APP process variables */

/* for configuring multi-process  */
static int ProcSplit = PFMP_APPCULLDRAW;
/* static int ProcSplit = PFMP_DEFAULT; */
/* write out scene upon read-in - uses pfDebugPrint  */
static int WriteScene = 0;
static int FullScreen = 0;
static int WinType = PFPWIN_TYPE_X;
static int NoBorder = 0;
static int ForkedXInput = 0;
char ProgName[PF_MAXSTRING];

/* light source created and updated in DRAW-process  */
static pfLight *Sun;

pfRotorWash *wash_mesh;

static void CullChannel(pfChannel *chan, void *data);
static void DrawChannel(pfChannel *chan, void *data);
static void OpenPipeWin(pfPipeWindow *pw);
static void UpdateView(void);
static void GetGLInput(void);
#ifndef WIN32
static void InitXInput(pfWSConnection dsp);
static void DoXInput(void);
static void GetXInput(Display *dsp);
#endif
static void Usage(void);


#if defined(WIN32)
static char *optarg;
static int optind = 1, opterr = 0;

static int getopt(int argc, char *argv[], const char *optstring)
{
    static int lastoptind = 1;

    int retval = -1;
    int loop;

    if (lastoptind == -1)
        optind = 1;

    /* printf ("argv[%d]=[%s]\n", optind, argv[optind]); */

    if ((argv == NULL) || (argv[optind] == NULL))
        goto error;
    else
    {
        char *found, *thisarg = argv[optind];
        
        /* verify this argument is an option */
        if ((!thisarg) || (thisarg[0] != '-') || (strlen(thisarg)<2))
            goto error;

        /* now we know it's an option */
        /* find this option in option string */
        found = strchr(optstring, thisarg[1]);
        if (!found)
            goto error;

        retval = *found;

        optind++;

        if (found[1] == ':')
        {

            /* if the 3rd char is non-null this argument must      */
            /* contain the value along with the option, ie "-x0"   */
            /* otherwise it is of the form, "-x 0".  Dereferencing */
            /* thisarg[2] is safe after the strlen<2 check above   */

            if (thisarg[2])
                optarg = &thisarg[2];
            else
                optarg = argv[optind++];
        }

        lastoptind = optind;
    }

    return retval;

error:
    optarg = NULL;
    lastoptind = -1;
    opterr = 1;
    return -1;
}
#endif

/*
//	Usage() -- print usage advice and exit. This procedure
//	is executed in the application process.
*/


static void
Usage (void)
{
    pfNotify(PFNFY_FATAL, PFNFY_USAGE, 
	     "Usage: %s [-m procSplit] [file.ext ...]\n", ProgName);
    exit(1);
}

/*
//	docmdline() -- use getopt to get command-line arguments, 
//	executed at the start of the application process.
*/

static int
docmdline(int argc, char *argv[])
{
    int	    opt;
    
    strcpy(ProgName, argv[0]);
    
    /* process command-line arguments  */
    while ((opt = getopt(argc, argv, "fFbm:wxp:?")) != -1)
    {
	switch (opt)
	{
	case 'f': 
	    FullScreen = 1;
	    break;
	case 'F': 
	    ForkedXInput = 1;
	    break;
	case 'm':
	case 'p':
	    ProcSplit = atoi(optarg);
	    break;
	case 'w': 
	    WriteScene = 1;
	    break;
	case 'x': 
	    WinType &= ~(PFPWIN_TYPE_X);
	    break;
	case 'b': 
	    NoBorder ^= 1;
	    break;
	case '?': 
	    Usage();
	}
    }
    return optind;
}


/*
//	main() -- program entry point. this procedure
//	is executed in the application process.
*/

int
main (int argc, char *argv[])
{
    int		    arg;
    int		    found;
    pfPipe         *p;
    pfBox           bbox;
    float	    farPlane = 10000.0f;
    pfWSConnection  dsp=NULL;
    pfScene *scene = NULL;
    pfGroup *terrain = NULL;
    pfDCS *helipos = NULL;
    pfNode *sea = NULL;
    pfNode *land = NULL;
    pfTexEnv *tev = NULL;
    pfTexture *TiTex = NULL;
    pfGeode *geode = NULL;
    pfVec4 *colors = NULL;
    pfGeoState *gstate = NULL;
    pfGeoSet *gset = NULL;
    pfDCS *sign1 = NULL;
    pfChannel *chan = NULL;
    pfEarthSky *eSky = NULL;

    
    arg = docmdline(argc, argv);
    
    pfInit();
    
    /* configure multi-process selection  */
    pfMultiprocess(ProcSplit);
    
    /* allocate shared before fork()'ing parallel processes  */
    Shared = (SharedData*)pfCalloc(1, sizeof(SharedData), pfGetSharedArena());
    Shared->drawStats = 0;
    Shared->drawWire = 0;
    Shared->drawHighlight = 0;
    Shared->radii[0] = 10.0f*0.55;
    Shared->radii[1] = 15.0f*0.55;
    Shared->radii[2] = 20.0f*0.55;
    Shared->radii[3] = 30.0f*0.55;
    pfSetVec3( Shared->heli.xyz, -20.0f, 2.0f, 25.0f);
    pfSetVec3( Shared->heli.hpr,   0.0f, 0.0f,  0.0f);
    pfSetVec3( Shared->heliveli,   0.0f, 0.0f,  0.0f);
    pfSetVec3( Shared->helithpr,   0.0f, 0.0f,  0.0f);
    /* Load all loader DSO's before pfConfig() forks  */
    for (found = arg; found < argc; found++)
	pfdInitConverter(argv[found]);

    /* initiate multi-processing mode set in pfMultiprocess call  */
    /* FORKs for Performer processes,  CULL and DRAW, etc. happen here. */
    
    pfConfig();

    /*
      severity
	PFNFY_ALWAYS   | Always	print regardless of notify level
	PFNFY_FATAL    | Fatal error, the dying	gasp of	a doomed process
	PFNFY_WARN     | Serious warning, rarely used for frame-time errors
	PFNFY_NOTICE   | Warning, may be used for frame	time errors
	PFNFY_INFO     | Information on	progress as well as errors
	PFNFY_DEBUG    | Debug information of significant verbosity
	PFNFY_FP_DEBUG | Debug information and floating	point exceptions
      error (any, but predefined are)
	PFNFY_USAGE
	PFNFY_RESOURCE
	PFNFY_SYSERR
	PFNFY_ASSERT
	PFNFY_PRINT
	PFNFY_INTERNAL
	PFNFY_FP_OVERFLOW
	PFNFY_FP_DIVZERO
	PFNFY_FP_INVALID
	PFNFY_FP_UNDERFLOW
	PFNFY_MORE does a continuation of the	previous message.

    */

    pfNotify(PFNFY_INFO,PFNFY_PRINT,"done with pfConfig");
    
    /* configure pipes and windows  */
    p = pfGetPipe(0);
    Shared->pw = pfNewPWin(p);
    pfPWinName( Shared->pw, "OpenGL|Performer");
    pfPWinType( Shared->pw, WinType );
    if (NoBorder)
      pfPWinMode( Shared->pw, PFWIN_NOBORDER, 1);
    /* Open and configure the GL window.  */
    pfPWinConfigFunc( Shared->pw, OpenPipeWin);
    pfConfigPWin( Shared->pw );

    pfNotify(PFNFY_INFO,PFNFY_PRINT,"done with pfConfigPWin");
    
    if (FullScreen)
      pfPWinFullScreen( Shared->pw );
    else
      pfPWinOriginSize( Shared->pw, 0, 0, 300, 300);
    
    /* set off the draw process to open windows and call init callbacks  */
    pfFrame();

    pfNotify(PFNFY_INFO,PFNFY_PRINT,"done with initial pfFrame");

    /*
    // create forked XInput handling process 
    // since the Shared pointer has already been initialized, that structure
    // will be visible to the XInput process. Nothing else created in the
    // application after this fork whose handles are not put in shared memory
    // (such as the database and channels) will be visible to the
    // XInput process.
    */
    if (WinType & PFPWIN_TYPE_X)
    {
#ifndef WIN32
	pid_t	    fpid = 0;
	if (ForkedXInput)
	{
	    if ((fpid = fork()) < 0)
		pfNotify(PFNFY_FATAL, PFNFY_SYSERR, "Fork of XInput process failed.");
	    else if (fpid)
		pfNotify(PFNFY_NOTICE,PFNFY_PRINT,"XInput running in forked process %d",
			 fpid);
	    else if (!fpid)
		DoXInput();
	}
	else
#endif
	{
	    dsp = pfGetCurWSConnection();
	}
    }
    
    /* specify directories where geometry and textures exist  */
    if (!(getenv("PFPATH")))
        pfFilePath(
                   "."
                   ":./Models"
                   ":./BVR"
                   ":/usr/share/Performer/data"
                   );
    pfNotify(PFNFY_INFO, PFNFY_PRINT,"FilePath: %s\n", pfGetFilePath());

    
    /* load files named by command line arguments  */
    scene = pfNewScene();
    terrain = pfNewGroup();
    helipos = pfNewDCS();
    pfDCSCoord(helipos, &(Shared->heli));
    pfAddChild(scene, terrain);
    pfAddChild(scene, helipos);
    pfNotify(PFNFY_INFO,PFNFY_PRINT,"made a scene,terrain,helipos");



    for (found = 0; arg < argc; arg++)
    {
        pfNode	   *root;
	if ((root = pfdLoadFile(argv[arg])) != NULL)
	{
	  if(!found) /* for the first model */
            {
                pfAddChild(helipos, root);
            }
            else
            {
	        pfAddChild(terrain, root);
            }
	    found++;
	}
    }


    pfNotify(PFNFY_INFO,PFNFY_PRINT,"making a rotorwash");
    wash_mesh = pfNewRotorWash(1, 10, pfGetSharedArena());
    pfRotorWashDisplacement(wash_mesh, 0.05f);
    pfRotorWashColor(wash_mesh, NULL, 0.7f, 0.9f, 1.0f, 1.0f);
    pfRotorWashAlpha(wash_mesh, 1.0f, 1.0f);
    pfRotorWashRadii(wash_mesh, 
		     Shared->radii[0],Shared->radii[1],
		     Shared->radii[2],Shared->radii[3]);


    pfNotify(PFNFY_INFO,PFNFY_PRINT,"making the sea");
    sea = MakeSea(0);

    UpdateSea(&(Shared->view.xyz));

    pfNotify(PFNFY_INFO,PFNFY_PRINT,"making the land");

    land = MakeLand(40,1);
    pfAddChild(terrain,land);
    pfAddChild(terrain,sea);

    pfRotorWashCompute(wash_mesh, (pfNode*)terrain, 
		       Shared->heli.xyz[0], Shared->heli.xyz[1], 0.0f );

    pfAddChild(scene,  pfGetRotorWashNode(wash_mesh) );

    /* if no files successfully loaded, terminate program  */

    /* Write out nodes in scene (for debugging)  */
    if (WriteScene)
    {
	FILE *fp;
	if (fp = fopen("scene.out", "w"))
	{
	    pfPrint(scene, PFTRAV_SELF|PFTRAV_DESCEND, PFPRINT_VB_DEBUG, fp);
	    fclose(fp);
	}
	else
	    pfNotify(PFNFY_WARN, PFNFY_RESOURCE,
		     "Could not open scene.out for debug printing.");
    }
    
    /* determine extent of scene's geometry  */
    pfuTravCalcBBox(scene, &bbox);


    /* Create titles */
    Shared->coords3 = (pfVec3*)pfCalloc(8,sizeof(pfVec3),pfGetSharedArena());
    Shared->Tcoords = (pfVec2*)pfCalloc(8,sizeof(pfVec2),pfGetSharedArena());

    pfSetVec3( Shared->coords3[0],  0.0f, 5.0f,  1.1f );
    pfSetVec3( Shared->coords3[1],  0.0f, 5.0f,  1.5f );
    pfSetVec3( Shared->coords3[2], -2.0f, 5.0f,  1.5f );
    pfSetVec3( Shared->coords3[3], -2.0f, 5.0f,  1.1f );
    pfSetVec2( Shared->Tcoords[0],  1.0f, 0.8f);
    pfSetVec2( Shared->Tcoords[1],  1.0f, 1.0f);
    pfSetVec2( Shared->Tcoords[2],  0.0f, 1.0f);
    pfSetVec2( Shared->Tcoords[3],  0.0f, 0.8f);

    tev = pfNewTEnv( pfGetSharedArena() );
    pfTEnvMode(tev, PFTE_REPLACE);
    TiTex = pfNewTex( pfGetSharedArena() );
    pfTexFilter(TiTex,PFTEX_MINFILTER, PFTEX_BILINEAR);
    pfTexFilter(TiTex,PFTEX_MAGFILTER, PFTEX_BILINEAR);
    pfTexRepeat(TiTex,PFTEX_WRAP_S, PFTEX_CLAMP);
    pfTexRepeat(TiTex,PFTEX_WRAP_T, PFTEX_CLAMP);
    pfLoadTexFile(TiTex,"message.rgb");

    colors = (pfVec4*) pfCalloc(1, sizeof(pfVec4), pfGetSharedArena());
    pfSetVec4( colors, 1.0f, 1.0f, 1.0f, 1.0f);

    geode = pfNewGeode();

    gstate = pfNewGState( pfGetSharedArena() );
    pfGStateAttr(gstate,PFSTATE_TEXTURE, TiTex);
    pfGStateMode(gstate,PFSTATE_TRANSPARENCY, PFTR_OFF);
    pfGStateMode(gstate,PFSTATE_ENTEXTURE, PF_ON);
    pfGStateMode(gstate,PFSTATE_ENLIGHTING,0);
    pfGStateMode(gstate,PFSTATE_CULLFACE,PFCF_OFF);
    pfGStateAttr(gstate,PFSTATE_TEXENV, tev);

    gset = pfNewGSet( pfGetSharedArena() );
    pfGSetAttr(gset, PFGS_COORD3, PFGS_PER_VERTEX, Shared->coords3, 0);
    pfGSetAttr(gset, PFGS_TEXCOORD2, PFGS_PER_VERTEX, Shared->Tcoords, 0);
    pfGSetAttr(gset, PFGS_COLOR4, PFGS_OVERALL, colors, 0);
    pfGSetPrimType(gset, PFGS_QUADS);
    pfGSetNumPrims(gset, 1);
    pfGSetGState(gset, gstate);
    pfAddGSet(geode,gset);

    sign1 = pfNewDCS();
    pfAddChild(sign1,geode);
    
    pfFrameRate(30.0f);
    pfPhase(PFPHASE_FREE_RUN);
    
    chan = pfNewChan(p);
    pfAddChan( Shared->pw, chan);
    pfChanTravFunc(chan,PFTRAV_CULL, CullChannel);
    pfChanTravFunc(chan,PFTRAV_DRAW, DrawChannel);
    pfChanScene(chan,scene);
    pfChanNearFar(chan,1.0f, farPlane);
    
    /* Create an earth/sky model that draws sky/ground/horizon  */
    eSky = pfNewESky();
    pfESkyMode(eSky,PFES_BUFFER_CLEAR, PFES_SKY_CLEAR);
    pfESkyAttr(eSky,PFES_GRND_HT, -10.0f);
    pfESkyColor(eSky,PFES_CLEAR, 0.160f, 0.275f, 0.227f, 1.0f);
    pfChanESky(chan,eSky);
    
    pfChanTravMode(chan,PFTRAV_CULL, PFCULL_ALL);
    
    /* vertical FOV is matched to window aspect ratio  */
    pfChanFOV(chan,45.0f, -1.0f);
    if (found)
    {
	float sceneSize;
	/* Set initial view to be "in front" of scene  */
	
	/* view point at center of bbox  */
	pfAddVec3( Shared->view.xyz, bbox.min, bbox.max);
	pfScaleVec3( Shared->view.xyz, 0.5f, Shared->view.xyz );
	
	/* find max dimension  */
	sceneSize = bbox.max[PF_X] - bbox.min[PF_X];
	sceneSize = PF_MAX2(sceneSize, bbox.max[PF_Y] - bbox.min[PF_Y]);
	sceneSize = PF_MAX2(sceneSize, bbox.max[PF_Z] - bbox.min[PF_Z]);
	sceneSize = PF_MIN2(sceneSize, 0.5f * farPlane);
	Shared->sceneSize = sceneSize;
	
	/* offset so all is visible  */
	Shared->view.xyz[PF_Y] -=      sceneSize;
	Shared->view.xyz[PF_Z] += 0.25f*sceneSize;	
    }  else
    {
	pfSetVec3( Shared->view.xyz, 0.0f, 0.0f, 10.0f);
	PFSET_VEC3(bbox.min, -5000.0f, -5000.0f, -1000000.0f);
	PFSET_VEC3(bbox.max, 5000.0f, 5000.0f, 10000000.0f);
	Shared->sceneSize = 1000.0f;
    }
    pfSetVec3( Shared->view.xyz, -90.0f,  65.0f, 55.0f);
    pfSetVec3( Shared->view.hpr, 235.0f, -25.0f,  0.0f);
    pfChanView(chan,Shared->view.xyz, Shared->view.hpr);
    PFCOPY_VEC3(Shared->viewOrig.xyz, Shared->view.xyz);
    PFCOPY_VEC3(Shared->viewOrig.hpr, Shared->view.hpr);
    PFCOPY_VEC3(Shared->heliOrig.xyz, Shared->heli.xyz);
    PFCOPY_VEC3(Shared->heliOrig.hpr, Shared->heli.hpr);

    pfAddChild(scene,sign1);

    /* main simulation loop  */
    while (!Shared->exitFlag)
    {
        static float phase = 0.0f;
        static int hlight = 0;

        if (hlight != Shared->drawHighlight)
        {
            if(Shared->drawHighlight)
                pfRotorWashHighlight(wash_mesh,1.0f, 0.0f, 0.0f);
            else
                pfRotorWashUnhighlight( wash_mesh );
            hlight = Shared->drawHighlight;
	    }

        phase +=.01;
        if(phase > 1.0f)
          phase -= 1.0f;

	/* wait until next frame boundary  */
	pfSync();
	
	pfFrame();
	
	/* Set view parameters for next frame  */
	UpdateView();

        UpdateSea(&(Shared->view.xyz));
	pfDCSCoord(helipos, &(Shared->heli));
        pfRotorWashRadii( wash_mesh, 
			  Shared->radii[0], Shared->radii[1],
			  Shared->radii[2],Shared->radii[3]);
	pfRotorWashCompute( wash_mesh, (pfNode*)terrain, 
			    Shared->heli.xyz[0], Shared->heli.xyz[1], phase);

	pfChanView(chan,Shared->view.xyz, Shared->view.hpr);
	pfDCSCoord(sign1, &(Shared->view));
	/* initiate traversal using current state  */
#ifndef WIN32   
	if (!ForkedXInput  && (WinType & PFPWIN_TYPE_X))
	{
	    if (!Shared->XInputInited)
		InitXInput(dsp);
	    if (Shared->XInputInited)
		GetXInput(dsp);
	}
#endif
    }
    
    /* terminate cull and draw processes (if they exist)  */
    pfExit();
    
    /* exit to operating system  */
    return 0;
}

#ifndef WIN32
static void 
InitXInput(pfWSConnection dsp)
{
    Window w;
    
    /* wait for X Window to exist in Performer shared memory */
   if (w = pfGetPWinWSWindow(Shared->pw))
   {
	XSelectInput(dsp, w, PointerMotionMask |
			ButtonPressMask | ButtonReleaseMask | 
			KeyPressMask | KeyReleaseMask);
	XMapWindow(dsp, w);
	XFlush(dsp);
	Shared->XInputInited = 1;
    }
}

/*
// DoXInput() runs an asychronous forked even handling process.
//  Shared memory structures can be read from this process
//  but NO performer calls that set any structures should be 
//  issues by routines in this process.
*/
void
DoXInput(void)
{
  /* windows from draw should now exist so can attach X input handling */
  /* to the X window  */
    
    Display *dsp = pfGetCurWSConnection();

#ifndef __linux__    
    prctl(PR_TERMCHILD);        /* Exit when parent does  */
#else
    prctl(PR_SET_PDEATHSIG, SIGHUP, 0, 0, 0);
#endif
    sigset(SIGHUP, SIG_DFL);    /* Exit when sent SIGHUP by TERMCHILD  */
    
    InitXInput(dsp);
    
    while (1)
    {
	XEvent          event;
	if (!Shared->XInputInited)
	    InitXInput(dsp);
	if (Shared->XInputInited)
	{
	    XPeekEvent(dsp, &event);
	    GetXInput(dsp);
	}
    }
}
#endif

/*
//	UpdateView() updates the eyepoint based on the information
//	placed in shared memory by GetInput().
*/
static void    
UpdateView(void)
{
    static float speed = 0.0f;
    pfCoord *view = &Shared->view;
    float cp;
    float mx, my;
    static double thisTime = -1.0f;
    double prevTime;
    float deltaTime;

    prevTime = thisTime;
    thisTime = pfGetTime();

    if (prevTime < 0.0f)
	return;

    if (!Shared->inWindow || Shared->reset)
    {
	speed = 0;
	Shared->reset = 0;
	Shared->accelRate = 0.05f * Shared->sceneSize;
	return;
    }

    deltaTime = thisTime - prevTime;
#ifndef WIN32
    switch (Shared->mouseButtons)
    {
    case Button1Mask: /* LEFTMOUSE: faster forward or slower backward*/
    case Button1Mask|Button2Mask:
	speed += Shared->accelRate * deltaTime;
	if (speed > Shared->sceneSize)
	    speed = Shared->sceneSize;
	break;
    case Button3Mask: /* RIGHTMOUSE: faster backward or slower foreward*/
    case Button3Mask|Button2Mask:
	speed -= Shared->accelRate * deltaTime;
	if (speed < -Shared->sceneSize)
	    speed = -Shared->sceneSize;
	break;
    }
#endif
    mx = 2.0f * (Shared->mouseX / (float)Shared->winSizeX) - 1.0f;
    my = 2.0f * (Shared->mouseY / (float)Shared->winSizeY) - 1.0f;
    if (Shared->mouseButtons)
    {    
	/* update view direction */
	view->hpr[PF_H] -= mx * PF_ABS(mx) * 30.0f * deltaTime;
	view->hpr[PF_P] += my * PF_ABS(my) * 30.0f * deltaTime;
	view->hpr[PF_R]  = 0.0f;
	
	/* update view position */
	cp = cosf(PF_DEG2RAD(view->hpr[PF_P]));
	view->xyz[PF_X] += speed*sinf(-PF_DEG2RAD(view->hpr[PF_H]))*cp;
	view->xyz[PF_Y] += speed*cosf(-PF_DEG2RAD(view->hpr[PF_H]))*cp;
	view->xyz[PF_Z] += speed*sinf( PF_DEG2RAD(view->hpr[PF_P]));
    }
    else
    {
	speed = 0.0f;
	Shared->accelRate = 0.05f * Shared->sceneSize;
        Shared->radii[0] = (10.0f + mx * 5.0f)*0.55;
        Shared->radii[1] = (15.0f + mx * 5.0f)*0.55;
        Shared->radii[2] = (25.0f + my * 5.0f)*0.55;
        Shared->radii[3] = (30.0f + my * 5.0f)*0.55;
    }

    Shared->heli.xyz[0] += Shared->heliveli[0];
    Shared->heli.xyz[1] += Shared->heliveli[1];
    Shared->heli.xyz[2] += Shared->heliveli[2];
    Shared->heliveli[0] *= 0.975f;
    Shared->heliveli[1] *= 0.975f;
    Shared->heliveli[2] *= 0.95f;
    Shared->helithpr[1] *= 0.90f;
    Shared->helithpr[2] *= 0.90f;
    Shared->heli.hpr[1] += 0.1f * (Shared->helithpr[1] - Shared->heli.hpr[1]);
    Shared->heli.hpr[2] += 0.1f * (Shared->helithpr[2] - Shared->heli.hpr[2]);
}

/*
//	CullChannel() -- traverse the scene graph and generate a
// 	display list for the draw process.  This procedure is 
//	executed in the CULL process.
*/

static void
CullChannel(pfChannel *chan, void *data)
{

  /* pfDrawGeoSet or other display listable Performer routines */
  /* could be invoked before or after pfCull() */
    pfCull();
}

/*
//	OpenPipeWin() -- create a win: setup the GL and OpenGL Performer.
//	This procedure is executed in the DRAW process 
//	(when there is a separate draw process).
*/

static void
OpenPipeWin(pfPipeWindow *pw)
{
    pfOpenPWin(pw);
    
    /* create a light source in the "south-west" (QIII)  */
    Sun = pfNewLight(pfGetSharedArena());
    pfLightPos(Sun, -0.3f, -0.3f, 1.0f, 0.0f);
}


/*
//	DrawChannel() -- draw a channel and read input queue. this
//	procedure is executed in the draw process (when there is a
//	separate draw process).
*/

static void
DrawChannel (pfChannel *channel, void * data)
{
  /* rebind light so it stays fixed in position  */
  pfLightOn(Sun);
    
    /* erase framebuffer and draw Earth-Sky model  */
  pfClearChan( channel );
    
    if(Shared->drawWire)
    {
        pfEnable(PFEN_WIREFRAME);
        pfOverride(PFSTATE_ENWIREFRAME, PF_ON);
    }
    /* invoke Performer draw-processing for this frame  */
    pfDraw();

    pfOverride(PFSTATE_ENWIREFRAME, PF_OFF);
    pfDisable(PFEN_WIREFRAME);
    
    /* draw Performer throughput statistics  */
    
    if (Shared->drawStats)
      pfDrawChanStats(channel);
    
    /* read window origin and size (it may have changed)  */
    pfGetPWinSize( pfGetChanPWin(channel),
		   &Shared->winSizeX, &Shared->winSizeY);
}

#ifndef WIN32
static void
GetXInput(pfWSConnection dsp)
{
    static int x=0, y=0;
    
    if (XEventsQueued(dsp, QueuedAfterFlush))
#ifdef __linux__
    while (XPending(dsp))
#else
    while (XEventsQueued(dsp, QueuedAlready))
#endif
    {
	XEvent event;
	
	XNextEvent(dsp, &event);
	
	switch (event.type) 
	{
	case ConfigureNotify:
	    break;
	case FocusIn:
	    Shared->inWindow = 1;
	    break;
	case FocusOut:
	    Shared->inWindow = 0;
	    break;
	case MotionNotify: 
	    {
		XMotionEvent *motion_event = (XMotionEvent *) &event;
		Shared->inWindow = 1;
		x =  motion_event->x;
		y = Shared->winSizeY - motion_event->y;
	    }
	    break;
	case ButtonPress: 
	    {
		XButtonEvent *button_event = (XButtonEvent *) &event;
		x = event.xbutton.x;
		y = Shared->winSizeY - event.xbutton.y;
		Shared->inWindow = 1;
		switch (button_event->button) {
		case Button1:
		    Shared->mouseButtons |= Button1Mask;
		    break;
		case Button2:
		    Shared->mouseButtons |= Button2Mask;
		    break;
		case Button3:
		    Shared->mouseButtons |= Button3Mask;
		    break;
		}
	    }
	    break;
	case ButtonRelease:
	    {
		XButtonEvent *button_event = (XButtonEvent *) &event;
		switch (button_event->button) {
		case Button1:
		    Shared->mouseButtons &= ~Button1Mask;
		    break;
		case Button2:
		    Shared->mouseButtons &= ~Button2Mask;
		    break;
		case Button3:
		    Shared->mouseButtons &= ~Button3Mask;
		    break;
		}
	    }
	    break;
	case KeyPress:
	    {
		char buf[100];
		int rv;
                static int track_info = 0;
		KeySym ks;
		Shared->inWindow = 1;
		rv = XLookupString(&event.xkey, buf, sizeof(buf), &ks, 0);
		switch(ks) {
		case XK_Escape: 
		    Shared->exitFlag = 1;
		    exit(0);
		    break;
		case XK_space:
		    Shared->reset = 1;
		    PFCOPY_VEC3(Shared->view.xyz, Shared->viewOrig.xyz);
		    PFCOPY_VEC3(Shared->view.hpr, Shared->viewOrig.hpr);
		    PFCOPY_VEC3(Shared->heli.xyz, Shared->heliOrig.xyz);
		    PFCOPY_VEC3(Shared->heli.hpr, Shared->heliOrig.hpr);
		    pfNotify(PFNFY_NOTICE, PFNFY_PRINT,  "Reset");
		    break;
		case XK_g:
		  /* Shared->drawStats = !Shared->drawStats;*/
		    break;
		case XK_w:
		  /* Shared->drawWire = !Shared->drawWire;*/
		    Shared->drawHighlight = !Shared->drawHighlight;
		    break;
		case XK_s:
		    Shared->drawHighlight = !Shared->drawHighlight;
		    break;
		case XK_k:
		    Shared->heliveli[1] += 0.05f;
                    Shared->helithpr[1] -= 5.0f;
		    break;
		case XK_j:
		    Shared->heliveli[1] -= 0.05f;
                    Shared->helithpr[1] += 5.0f;
		    break;
		case XK_l:
		    Shared->heliveli[0] += 0.05f;
                    Shared->helithpr[2] += 5.0f;
		    break;
		case XK_h:
		    Shared->heliveli[0] -= 0.05f;
                    Shared->helithpr[2] -= 5.0f;
		    break;
		case XK_i:
                    track_info = !track_info;
                    if(track_info)
                    {
                        pfSetVec3(Shared->coords3[0],  0.0f, 5.0f,  0.3f );
                        pfSetVec3(Shared->coords3[3], -2.0f, 5.0f,  0.3f );
                        pfSetVec2(Shared->Tcoords[0],  1.0f, 0.4f);
                        pfSetVec2(Shared->Tcoords[3],  0.0f, 0.4f);
                    }
                    else
                    {
                        pfSetVec3(Shared->coords3[0],  0.0f, 5.0f,  1.1f );
                        pfSetVec3(Shared->coords3[3], -2.0f, 5.0f,  1.1f );
                        pfSetVec2(Shared->Tcoords[0],  1.0f, 0.8f);
                        pfSetVec2(Shared->Tcoords[3],  0.0f, 0.8f);
                    }
		default:
		    break;
		}
	    }
	    break;
	default:
	    break;
	}/* switch  */
    }
    Shared->mouseX = x;
    Shared->mouseY = y;
}
#endif

