/* 
 * Copyright 1993, 1995, Silicon Graphics, Inc.
 * ALL RIGHTS RESERVED
 *
 * This source code ("Source Code") was originally derived from a
 * code base owned by Silicon Graphics, Inc. ("SGI")
 * 
 * LICENSE: SGI grants the user ("Licensee") permission to reproduce,
 * distribute, and create derivative works from this Source Code,
 * provided that: (1) the user reproduces this entire notice within
 * both source and binary format redistributions and any accompanying
 * materials such as documentation in printed or electronic format;
 * (2) the Source Code is not to be used, or ported or modified for
 * use, except in conjunction with OpenGL Performer; and (3) the
 * names of Silicon Graphics, Inc.  and SGI may not be used in any
 * advertising or publicity relating to the Source Code without the
 * prior written permission of SGI.  No further license or permission
 * may be inferred or deemed or construed to exist with regard to the
 * Source Code or the code base of which it forms a part. All rights
 * not expressly granted are reserved.
 * 
 * This Source Code is provided to Licensee AS IS, without any
 * warranty of any kind, either express, implied, or statutory,
 * including, but not limited to, any warranty that the Source Code
 * will conform to specifications, any implied warranties of
 * merchantability, fitness for a particular purpose, and freedom
 * from infringement, and any warranty that the documentation will
 * conform to the program, or any warranty that the Source Code will
 * be error free.
 * 
 * IN NO EVENT WILL SGI BE LIABLE FOR ANY DAMAGES, INCLUDING, BUT NOT
 * LIMITED TO DIRECT, INDIRECT, SPECIAL OR CONSEQUENTIAL DAMAGES,
 * ARISING OUT OF, RESULTING FROM, OR IN ANY WAY CONNECTED WITH THE
 * SOURCE CODE, WHETHER OR NOT BASED UPON WARRANTY, CONTRACT, TORT OR
 * OTHERWISE, WHETHER OR NOT INJURY WAS SUSTAINED BY PERSONS OR
 * PROPERTY OR OTHERWISE, AND WHETHER OR NOT LOSS WAS SUSTAINED FROM,
 * OR AROSE OUT OF USE OR RESULTS FROM USE OF, OR LACK OF ABILITY TO
 * USE, THE SOURCE CODE.
 * 
 * Contact information:  Silicon Graphics, Inc., 
 * 1600 Amphitheatre Pkwy, Mountain View, CA  94043, 
 * or:  http://www.sgi.com
 */

/*
 *	generic.c
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <ctype.h>
#ifndef WIN32
#include <sys/sysmp.h>
#endif

#ifndef WIN32
/* X Window includes */
#include <X11/X.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#endif


/* OpenGL Performer includes */
#include <Performer/pf.h>
#include <Performer/pfutil.h>
#include <Performer/pfdu.h>
#include <Performer/pfui.h>

/* common includes */
#include "main.h"
#include "custom.h"

#define streq(a,b) (strcmp(a,b) == 0)
#define strsuffix(s, suf) \
	(strlen(s) >= strlen(suf) && streq(s + (strlen(s)-strlen(suf)), suf))

/******************************************************************************
 *			    Global Variables
 *****************************************************************************/

/* Data shared between processes */
SharedViewState	*ViewState;

/*
 * Command line optional settings for initialization. These variables are
 * set before the call to pfConfig and are only used for initialization.
 * So they don't need to be in the ViewState shared memory structure. They
 * are initialized in the function InitSharedMem().
*/
int	WinSizeX		= -1;
int	WinSizeY		= -1;
int	Multipipe		= -1;
int	NumPipes		= -1;
int	NumChans		= -1;
int	MultiPipeInput		= 1;
int	NotifyLevel		= PFNFY_INFO;
int	ProcSplit		= PFMP_DEFAULT;
int	PrioritizeProcs		= 0;
int	GangDraw		= 0;
int	InitHPR			= FALSE;
int	InitXYZ			= FALSE;
char*	WriteFileDbg		= NULL;
int	ChanOrder[MAX_CHANS]	= {-1};
int	ChanPipes[MAX_CHANS]	= {-1};
int	InitFOV			= FALSE;
int	InitFOVasym		= FALSE;
int     ChooseShaderVisual      = 0;

int NumHyperpipes		= -1;
int NumHyperpipeSingles		= 0;
int TotHyperpipePipes		= -1;

int NumHyperpipePipes[MAX_HYPERPIPES];
int HyperpipeNetIds[MAX_HYPERPIPES];
char* HyperpipeSingles[MAX_PIPES];

int 	Multisamples = -1;
int	ZBits	     = -1;

#if defined(__sgi)
static GLXHyperpipeNetworkSGIX *HyperpipeNet = NULL;
static int NumHyperpipeNet = 0;
#endif

/*********************************************************************
*								     *
*		APPLICATION PROCESS ROUTINES			     *
*								     *
**********************************************************************/

/* The following globals are used by the Application process only */
int	    NumFiles	    = 0;
char	    **DatabaseFiles = NULL;

static int	numScreens;


/*---------------------------------------------------------------------*/


/* Determine if Simulation is finished */
int
SimDone(void)
{
    return(ViewState->exitFlag);
}

/* Initialize MP, system config */
void
InitConfig(void)
{
    int 	numCPUs = 0;
    int 	availCPUs;

    /* Set multiprocessing mode. */
    pfMultiprocess(ProcSplit);

    /* Set number of pfPipes desired. */
    if (Multipipe == POWERWALL)
    {
    	PowerWall* pwall = &(ViewState->powerwall);
    	int i, numElts = pwall->cols*pwall->rows;
    	int num_singles=0;
    	for (i=0; i<numElts; i++)
    	    if (pwall->elt[i].type == 0)
    	    	num_singles++;

    	pfNotify (PFNFY_DEBUG, PFNFY_PRINT,
    	    "POWERWALL MODE: calling pfMultipipe(%d)",
    	    	pfGetNumCompositedPipes() + num_singles );

    	pfMultipipe(pfGetNumCompositedPipes() + num_singles);
    }

    else if (ViewState->numPanoramicElts > 0)
    {
    	int i, num_singles = 0;
    	for (i=0; i<ViewState->numPanoramicElts; i++)
    	    if (ViewState->panoramicDisplayElt[i].type == SINGLE_PIPE_DISPLAY_ELT)
    	    	num_singles++;

    	ViewState->totalChans = pfGetNumCompositedPipes() + num_singles;

    	pfNotify (PFNFY_DEBUG, PFNFY_PRINT,
    	    "PANORAMIC MODE: CALLING pfMultipipe(%d)", ViewState->totalChans );

    	pfMultipipe( ViewState->totalChans );
    }

    else
    {
    	pfNotify (PFNFY_DEBUG, PFNFY_PRINT,
    	    "CALLING pfMultipipe(%d)",NumPipes );

	pfMultipipe(NumPipes);
    }

    /* Enable hyperpipe mode */
    if (Multipipe == HYPERPIPE) {
	int i;
	for (i = 0; i < NumHyperpipes; i++)
	    pfHyperpipe(NumHyperpipePipes[i]);
    }

    /* Set intersection callback. */
    pfIsectFunc(IsectFunc);

    /* Check for restricted CPUs */
    pfQuerySys(PFQSYS_NUM_CPUS,&numCPUs);
    pfQuerySys(PFQSYS_NUM_CPUS_AVAILABLE,&availCPUs);

    if (numCPUs != availCPUs)
    {
	pfNotify(PFNFY_NOTICE, PFNFY_RESOURCE, 
	    "Only %d out of %d processors available on this machine !!!",
			availCPUs, numCPUs);

#ifndef WIN32
	if ((geteuid() == 0) && FreeInitCPUs)
#else
        if(FreeInitCPUs)
#endif
	    pfuFreeAllCPUs();
	else
	    pfNotify(PFNFY_NOTICE, PFNFY_PRINT, "Not freeing CPUs.");
    } 
    else
	pfNotify(PFNFY_INFO, PFNFY_PRINT, 
	    "All %d processors available on this machine.", numCPUs);

    /* call initConfig customisation in perfly */
    initConfig();
}


/*
 * Allocate Global pointers and set Global variables-
 * This must be called before pfConfig() if forked processes
 * must share globals and/or pointers to shared memory.
 * Initialize shared memory structures
 * 
 * This queries X for machine config info and currently assumes the 
 * default display and screen.
 * 
 * XXX we really need to do config info after we have the desired gl context
 * so that we can do the necessary OpenGL queries on extensions.
*/


void
InitSharedMem(int argc, char *argv[])
{
    void 	*arena;
    int		i;
#ifndef WIN32
    Display	*dsp;
#endif
    int		screen; 

    /* Get pointer to shared memory malloc arena created by pfInit()*/
    arena = pfGetSharedArena();
#ifndef WIN32
    dsp = pfGetCurWSConnection(); /* get display for machine config queries */
    screen = DefaultScreen( dsp );
#else
    screen = 0; /* XXX Alex */
#endif
    /*
     * Malloc structures now, before pfConfig(), so forked processes have same
     * pointers.
    */
    ViewState = (SharedViewState *) pfCalloc(1, sizeof(SharedViewState), arena);

    /* Find out how big the window can be */
#ifdef WIN32
    ViewState->maxScreenX = GetSystemMetrics(SM_CXSCREEN);
    ViewState->maxScreenY = GetSystemMetrics(SM_CYSCREEN);   
#else
    ViewState->maxScreenX = DisplayWidth(dsp, screen);
    ViewState->maxScreenY = DisplayHeight(dsp, screen);
#endif
    /* get number of screens */
#ifdef WIN32
#ifdef SM_CMONITORS
    numScreens = GetSystemMetrics(SM_CMONITORS);
#else
    numScreens = 1; /* XXX Alex -- SM_CMONITORS only for win98/Me ??? */
#endif
#else
    numScreens = ScreenCount(dsp);
#endif

    /*
     * Set window size - this is set here, before the call to pfConfig,
     * so that all processes will get the same copy. These do not need
     * to be in shared memory, because they are initialization values
     * which are not changed during the course of the application
    */
    if ((WinSizeX < 0) || (WinSizeY < 0))
    {	
	/* If no window size specified, set to be max possible */
	WinSizeX   = ViewState->maxScreenX;
	WinSizeY   = ViewState->maxScreenY;
    }
    else
    {	
	/* If the window size was set by command line arguments */
	WinSizeX = PF_MIN2(WinSizeX,ViewState->maxScreenX);
	WinSizeY = PF_MIN2(WinSizeY,ViewState->maxScreenY);
    }

    /* Initialize the channel-pipe assignment table: 0->0, 1->1, ... */
    for (i = 0; i < MAX_CHANS; i++)
	ChanOrder[i] = ChanPipes[i] = i;

    /* Initialize Keyboard and GUI ViewState data */
    initViewState();

    /* Set default multipipe mode */
    Multipipe = (numScreens > 1);


    /* initialize some elements of the powerwall data structure */
    ViewState->powerwall.num_composited_cols = 1;
    ViewState->powerwall.num_composited_rows = 1;
    ViewState->powerwall.cols = ViewState->powerwall.rows = 0;
    /* ViewState->numChans = 0; */
    ViewState->totalChans = 0;

    ViewState->listCompParams = 
    	    pfNewList(sizeof(CompositorParams*), 1, pfGetSharedArena());

    ViewState->numPanoramicElts = 0;
    ViewState->master_pipe_index = -1;


    /* Process the command line args */
    NumFiles = processCmdLine(argc, argv, &DatabaseFiles);

    /* set a path to everywhere we want to look for data.
     * append to any path set from the command line.
     */
    {
    size_t oldLength = 0;
    size_t newLength = 0;
    size_t fullLength = 0;
    const char *oldPath = pfGetFilePath();
    char *fullPath = NULL;
#ifdef WIN32
    const char *pfroot = getenv("PFROOT");
    char newPath[1024*3 + 100];
    sprintf(newPath,
            "."
            ";%s/Data"
            ";%s/Data/clipdata/hunter"
            ";%s/Data/clipdata/moffett"
            ";../Data"
            ";../../Data"
            ";../../Data/polyhedra"
            ";../../../Data", 
            pfroot, pfroot, pfroot);
#else
    char *newPath = 
       "."
       ":/usr/share/Performer/data"
       ":/usr/share/Performer/data/clipdata/hunter"
       ":/usr/share/Performer/data/clipdata/moffett"
       ":../data"
       ":../../data"
       ":../../data/polyhedra"
       ":../../../data"
       ":/usr/demos/models"
       ":/usr/demos/data/flt";
#endif

    if (oldPath != NULL)
	oldLength = strlen(oldPath);
    if (newPath != NULL)
	newLength = strlen(newPath);
    fullLength = oldLength + newLength;

    if (fullLength > 0)
    {
	/* allocate space for old, ":", new, and ZERO */
	fullPath = (char *)pfMalloc(fullLength + 2, NULL);
	fullPath[0] = '\0';
	if (oldPath != NULL)
	    strcat(fullPath, oldPath);
	if (oldPath != NULL && newPath != NULL)
#ifdef WIN32
	    strcat(fullPath, ";");
#else
	    strcat(fullPath, ":");
#endif
	if (newPath != NULL)
	    strcat(fullPath, newPath);
	pfFilePath(fullPath);
	pfFree(fullPath);
    }
    }

    /* Set Debug Level */
    pfNotifyLevel(NotifyLevel);

    /* Open loader DSOs before pfConfig() */
    for (i = 0 ; i < NumFiles ; i++)
	if (strsuffix(DatabaseFiles[i], ".perfly"))
	    initConfigFile(DatabaseFiles[i]);
	else
	    pfdInitConverter(DatabaseFiles[i]);

    if (!ViewState->guiFormat)
	ViewState->guiFormat = GUI_VERTICAL;

    /* Figure out the number of channels and pipelines required */
    switch (Multipipe) 
    {
    case TRUE:
	if (NumChans < 0)
	    NumChans = numScreens;
	NumPipes = NumChans;
	if (!ViewState->guiFormat)
	    ViewState->guiFormat = GUI_HORIZONTAL;
	break;
    case FALSE:
	if (NumChans < 0)
	    NumChans = 1;
	else if ((NumChans > 1) && (!ViewState->guiFormat))
		ViewState->guiFormat = GUI_HORIZONTAL;
	NumPipes = 1;
	break;

     case HYPERPIPE:
	{
#if defined(__sgi)
	    int i, lPipe;
	    int hasHyperpipe, numNets;

	    pfQueryFeature(PFQFTR_HYPERPIPE, &hasHyperpipe);
	    if (!hasHyperpipe) {
		pfNotify(PFNFY_FATAL, PFNFY_RESOURCE,
			"This machine does not support hyperpipe extension");
		exit(1);
	    }

	    TotHyperpipePipes = 0;
	    if (NumHyperpipes != 0) {
		HyperpipeNet = glXQueryHyperpipeNetworkSGIX(dsp,
			&NumHyperpipeNet);
		if (NumHyperpipeNet == 0) {
		    pfNotify(PFNFY_FATAL, PFNFY_RESOURCE,
			    "This machine has no hyperpipes");
		    exit(1);
		}

		/* check the requested configuration */
		for (i = 0, numNets = -1; i < NumHyperpipeNet; i++)
		    if (numNets < HyperpipeNet[i].networkId)
			numNets = HyperpipeNet[i].networkId;
		numNets += 1;

		if (NumHyperpipes == -1) {
		    NumHyperpipes = numNets;
		    for (i = 0; i < NumHyperpipes; i++)
			HyperpipeNetIds[i] = i;
		} else {
		    for (i = 0; i < NumHyperpipes; i++)
			if (HyperpipeNetIds[i] < 0 ||
				HyperpipeNetIds[i] >= numNets) {
			    pfNotify(PFNFY_FATAL, PFNFY_RESOURCE,
				"Hyperpipe net %d not available",
				HyperpipeNetIds[i]);
			    exit(1);
			}
		}

		/* count the pipes */
		for (i = 0; i < NumHyperpipes; i++) {
		    int j;
		    int netid = HyperpipeNetIds[i];
		    int hpipeCount = 0;
		    for (j = 0; j < NumHyperpipeNet; j++)
			if (netid == HyperpipeNet[j].networkId)
			    hpipeCount++;

		    TotHyperpipePipes += NumHyperpipePipes[i] = hpipeCount;
		}
	    }

	    NumPipes = TotHyperpipePipes + NumHyperpipeSingles;
    	    NumChans = NumHyperpipes + NumHyperpipeSingles;
    	    
	    /* correct the ChanPipes mapping to channels for hyperpipe */
	    for (i = lPipe = 0; i < NumPipes; lPipe++) {
		ChanPipes[lPipe] = i;
		i += lPipe < NumHyperpipes ? NumHyperpipePipes[lPipe] : 1;
	    }

	    if (NumPipes <= 0) {
		pfNotify(PFNFY_FATAL, PFNFY_RESOURCE,
			    "No pipes configured for hyperpipe mode");
		exit(1);
	    }
#else
	    pfNotify(PFNFY_FATAL, PFNFY_RESOURCE,
		    "Application not built with hyperpipe support\n");
	    exit(1);
#endif
	}
	break;



    case POWERWALL:
	{
	    PowerWall *pwall = &(ViewState->powerwall);
	    int numElts = pwall->cols * pwall->rows;

	    NumPipes = numElts;
	    ViewState->numChans = NumChans = numElts;
	}
	break;
    }

    ViewState->numChans = NumChans;
}


/* Initialize the visual database */
void
InitScene(void)
{
    int		 loaded		= 0;
    pfScene	*scene		= NULL;
    double	 startTime	= 0.0f;
    double	 elapsedTime	= 0.0f;

    /* Read in any databases specified on cmd line */
    ViewState->scene = scene = pfNewScene();

    startTime = pfGetTime();
	/* Read all files mentioned on command line */
	loaded = initSceneGraph(scene);

	/* Set up scene graph for collisions */
	if (loaded)
	    pfuCollideSetup(scene, PFUCOLLIDE_STATIC, 0xffffffff);

	if (loaded)
	    ViewState->texList = pfuMakeSceneTexList(scene);

	/* Initialize EarthSky, fog, and sun model */
	initEnvironment();
    elapsedTime = pfGetTime() - startTime;

    if (loaded)
    {
	pfNotify(PFNFY_INFO, PFNFY_PRINT, "Total scene-graph statistics");
	pfdPrintSceneGraphStats((pfNode *)scene, elapsedTime);
    }
    else
    {
    	pfSphere	bsphere;

	pfNotify(PFNFY_INFO, PFNFY_PRINT, "No Databases loaded");
	pfNotify(PFNFY_INFO, PFNFY_MORE, NULL);

	/* Set a default bounding sphere */
	pfSetVec3(bsphere.center, 0.0f, 0.0f, 0.0f);
	bsphere.radius = BOUND;
	pfNodeBSphere(scene, &bsphere, PFBOUND_DYNAMIC);
    }

    /* Write out nodes in scene (for debugging) */
    if (WriteFileDbg)
    {
	if (!strstr(WriteFileDbg, ".out") && pfdInitConverter(WriteFileDbg))
	{
	    /* it's a valid extension, go for it */
	    if (!pfdStoreFile((pfNode *)scene, WriteFileDbg))
		pfNotify(PFNFY_WARN, PFNFY_RESOURCE,
			 "No store function available for %s\n", WriteFileDbg);
	}
	else 
	{
	    /* write the file as ASCII */
	    FILE *fp;
	    if (fp = fopen(WriteFileDbg, "w"))
	    {
		pfPrint(scene, PFTRAV_SELF|PFTRAV_DESCEND, PFPRINT_VB_OFF, fp);
		fclose(fp);
	    }
	    else
		pfNotify(PFNFY_WARN, PFNFY_RESOURCE,
			 "Could not open scene.out for debug printing.");
	}
    }

    /* Set initial view position and angles */
    initView(scene);
}


/* Initialize graphics pipeline(s) */
void
InitPipe(void)
{
    pfPipeWindow *pw;
    int i, lPipe;
    char str[PF_MAXSTRING];

    if ((ViewState->master_pipe_index < 0) || (ViewState->master_pipe_index>=NumChans) )
    {
    	if (Multipipe)
    	    ViewState->master_pipe_index = ChanPipes[ChanOrder[(NumChans - 1) >> 1]];
    	else
    	    ViewState->master_pipe_index = 0;
    }

    
    /* init the pipe screens before pipe initialization for hyperpipe */
#if defined(__sgi)
    if (Multipipe == HYPERPIPE) {
	int j;
	lPipe = 0;

	/* map the hyperpipe pipes to pfPipes */
	for (i=0; i < NumHyperpipes; i++) {
	    int netid = HyperpipeNetIds[i];
	    for (j=0; j < NumHyperpipeNet; j++)
		if (netid == HyperpipeNet[j].networkId)
		    pfPipeWSConnectionName( pfGetPipe(lPipe++),
			    HyperpipeNet[j].pipeName);
	}

	/* map the single pipes to pfPipes */
	for (j=0; lPipe < NumPipes; lPipe++, j++)
	    pfPipeWSConnectionName( pfGetPipe(lPipe), HyperpipeSingles[j]);
    } else
#endif

    if (Multipipe == POWERWALL)
    {
	PowerWall *pwall = &(ViewState->powerwall);
	int numElts = pwall->cols * pwall->rows;
    	int numCompPipes = pfGetNumCompositedPipes();

	int i;

	for (i=0; i<numElts; i++)
	{
	    if (pwall->elt[i].type == 1) /* if elt is a compositor */
	    {
    	    	pfCompositor* comp = pwall->elt[i].compositor;
		ViewState->pipe[i] =  pfGetCompositorMasterPipe(comp);

    		/* PAOLO FINDME MASTERPIPE */
    	    	if (i == ViewState->master_pipe_index)
    	    	    pfCompositorChannelClipped(comp, 0, PF_OFF);
	    }
	    else
    	    {
    	    	int index = pwall->elt[i].index;

		ViewState->pipe[i] =  pfGetPipe( numCompPipes + index );
		
		if (pwall->namesProvided)
                {
                    pfPipeWSConnectionName(ViewState->pipe[i], 
		    	HyperpipeSingles[index]);
 
                    pfNotify (PFNFY_DEBUG, PFNFY_PRINT,
                        "POWERWALL PIPE %d (SINGLE-PIPE): 0x%x (%s)",
                        i, ViewState->pipe[i], HyperpipeSingles[index]);
                }
                else
                {
                    pfPipeScreen(ViewState->pipe[i],i);
 
                    pfNotify (PFNFY_DEBUG, PFNFY_PRINT,
                        "POWERWALL PIPE %d (SINGLE-PIPE): 0x%x (screen %d)",
                        i, ViewState->pipe[i], i);
                }  	    
	    }
	}
    }
    else if( ViewState->numPanoramicElts > 0)
    {
    	int i, n = ViewState->numPanoramicElts;
    	int numCompPipes = pfGetNumCompositedPipes();

    	for (i=0; i<n; i++)
    	{
    	    PanoramicDisplayElt *elt = &(ViewState->panoramicDisplayElt[i]);
    	    if (elt->type == SINGLE_PIPE_DISPLAY_ELT)
    	    {
    	    	ViewState->pipe[i] = pfGetPipe( numCompPipes + elt->id );
		pfPipeWSConnectionName(ViewState->pipe[i], elt->pipe_name);
    	    }
    	    else if (elt->type == HW_COMPOSITOR_DISPLAY_ELT)
    	    {
    	    	pfCompositor* comp = elt->compositor;
		ViewState->pipe[i] =  pfGetCompositorMasterPipe(comp);

    	    	if (i == ViewState->master_pipe_index)
    	    	    pfCompositorChannelClipped(comp, 0, PF_OFF);
    	    }
    	}
    }
    else
    {
        for (i=0; i < NumPipes; i++)
    	{
    	    ViewState->pipe[i] = pfGetPipe(i);

	    if ((NumPipes > 1) || (numScreens == 1))
		pfPipeScreen(ViewState->pipe[i], PF_MIN2(i, numScreens-1));
    	}
    }




    i = lPipe = 0;
    while (i < NumPipes)
    {
	int pipeCount;
    	pipeCount = lPipe < NumHyperpipes ? NumHyperpipePipes[lPipe] : 1;

	ViewState->pw[i] = pw = pfNewPWin(ViewState->pipe[i]);
	sprintf(str,"OpenGL Performer [pipe %d]", i);
	pfPWinName(pw,str);

    	if ( (ViewState->maxScreenX == WinSizeX) && 
    	     (ViewState->maxScreenY == WinSizeY))
    	    pfPWinFullScreen(pw);
    	else
	    pfPWinOriginSize(pw, 0, 0, WinSizeX, WinSizeY);


	if (ViewState->input != PFUINPUT_GL)
	    pfPWinConfigFunc(pw, OpenXWin);
	else
	    pfPWinConfigFunc(pw, OpenWin);

	{
	    /* init pipe specific visual information in pipewindows */
	    int j;
	    for (j = 0; j < pipeCount; j++)
		if (ViewState->visualIDs[i+j] > -1) {
		    pfPipeWindow* tpw = pfGetPipePWin(ViewState->pipe[i+j], 0);
		    pfPWinFBConfigId(tpw, ViewState->visualIDs[i+j]);
		}
	}

	pfConfigPWin(pw);

	i += pipeCount;
	lPipe++;
    }
    
    pfStageConfigFunc(-1, PFPROC_DRAW, OpenPipeline);
    pfStageConfigFunc(-1, PFPROC_CULL, ConfigCull);
    pfStageConfigFunc(-1, PFPROC_LPOINT, ConfigLPoint);
    pfConfigStage(-1, PFPROC_CULL | PFPROC_DRAW | PFPROC_LPOINT);

    pfNotify(PFNFY_INFO,  PFNFY_PRINT,
		"Initialized %d Pipe%s", NumPipes, (NumPipes < 2) ? "" : "s");
    pfNotify(PFNFY_INFO,  PFNFY_MORE, NULL);
}


/* Initialize GUI Panel and widget callback routines */
void
InitGUI(void)
{
    pfPipe		*p;

    if ((ViewState->master_pipe_index < 0) || (ViewState->master_pipe_index>=NumChans) )
    {
    	pfNotify (PFNFY_FATAL, PFNFY_PRINT,
    	    "InitGUI - did not expect ViewState->master_pipe_index to be %d", 
    	    	ViewState->master_pipe_index );

    	if (Multipipe)
    	    ViewState->master_pipe_index = ChanPipes[ChanOrder[(NumChans - 1) >> 1]];
    	else
    	    ViewState->master_pipe_index = 0;
    }

    if (ViewState->compositor)
	pfCompositorChannelClipped(ViewState->compositor, 0, PF_OFF);

    /* Init GUI and internal data for pipe of master channel */
    p = ViewState->pipe[ViewState->master_pipe_index];
    pfuInitGUI(pfGetPipePWin(p, 0));

    /* Set the initial enable for the GUI */
    pfuEnableGUI(ViewState->gui);
}


/* Initialize Viewing Channel(s) */
void
InitChannel(void)
{
    int		    	i;
    pfPipe		*pipe;
    pfFrameStats    	*frameStats;
    pfChannel	    	*chan;
    pfVec3   		xyz, hpr;
    float		fov;

    ViewState->totalChans = NumChans;

    if (Multipipe)
    {
	if (Multipipe == POWERWALL)
    	{
	    PowerWall *pwall = &(ViewState->powerwall);

	    for (i=0; i< NumChans; i++) 
		ViewState->chan[i] = pfNewChan(ViewState->pipe[i]);

    	    if ( (pwall->num_composited_cols * pwall->num_composited_rows) > 1 )
	    {
		int vpwidth = ViewState->maxScreenX/pwall->num_composited_cols;
		int vpheight = ViewState->maxScreenY/pwall->num_composited_rows;
		int row, col;

		for (i=0,row=0; row<pwall->rows ; row++) 
	        {
	    	    int vpB = (row%pwall->num_composited_rows)*vpheight;
		    for (col=0; col<pwall->cols; col++, i++)
		    {
	    		int vpL = (col%pwall->num_composited_cols)*vpwidth;

			pfNotify (PFNFY_DEBUG, PFNFY_PRINT, 
			    "InitChannel: POWERWALL CHANNEL [%d,%d] (index=%d) VIEWPORT: left=%d right=%d bott=%d top=%d",
				col, row, ChanOrder[i], vpL, vpL+vpwidth/*-1*/, vpB, vpB+vpheight/*-1*/ );

			pfNotify (PFNFY_DEBUG, PFNFY_MORE, 
			    "InitChannel: POWERWALL CHANNEL [%d,%d] (index=%d) VIEWPORT: left=%f right=%f bott=%f top=%f",
				col, row, ChanOrder[i], 
				((float)vpL)/(float)(ViewState->maxScreenX), 
				((float)(vpL+vpwidth/*-1*/))/(float)(ViewState->maxScreenX), 
				((float)vpB)/(float)(ViewState->maxScreenY), 
				((float)(vpB+vpheight/*-1*/))/(float)(ViewState->maxScreenY) );
				
			pfChanViewport ( ViewState->chan[ChanOrder[i]],
				((float)vpL)/(float)(ViewState->maxScreenX), 
				((float)(vpL+vpwidth/*-1*/))/(float)(ViewState->maxScreenX), 
				((float)vpB)/(float)(ViewState->maxScreenY), 
				((float)(vpB+vpheight/*-1*/))/(float)(ViewState->maxScreenY) );
		    }
		}
    	    }

    	    /* Paolo XXX - HACK ALERT:
    	       In multi-compositor-powerwall mode, we set numPipes and numChans to the
    	       number of elements in powerwall. Ie we only include master composited
    	       pipes in this count.
    	       The ViewState->chan array currently includes all master channels. Here
    	       we append all slave-composited channels to the chan array, without
    	       incrementing the value of numChans.
    	       We will be looping through this extended array in a couple of places,
    	       where stuff needs to be done explicitely on ALL composited channels
    	    */
    	    {
    	    	int c = NumChans;
    	
    		for (i=0; i<NumChans; i++)
    	    	{
    	    	    if (pwall->elt[i].type == 1) /* if element is a compositor */
    	    	    {
    	    	    	pfCompositor* comp = pwall->elt[i].compositor;
    	    	    	int p, num_pipes = pfGetCompositorNumPipes(comp);
    	    	    	for (p=1; p<num_pipes; p++)
    	    	    	{
    			    /* PAOLO FINDME MASTERPIPE */
    	    	    	    if (i == ViewState->master_pipe_index)
    	    	    		ViewState->chan[c] = pfGetCompositorChan(comp, p, 1);
    	    	    	    else
    	    	    		ViewState->chan[c] = pfGetCompositorChan(comp, p, 0);
    	    	    	    c++;
    	    	    	}
    	    	    }
    	    	}

    	    	pwall->numChans = c;
    	    	ViewState->totalChans = c;
    	    }
    	}

	else if (ViewState->numPanoramicElts > 0)
    	{
    	    int i, n = ViewState->numPanoramicElts;
    	    int numCompPipes = pfGetNumCompositedPipes();
    	    int num_slave_pipes = 0;

    	    for (i=0; i<n; i++)
    	    {
    	    	PanoramicDisplayElt *elt = &(ViewState->panoramicDisplayElt[i]);
    		ViewState->chan[i] = pfNewChan(ViewState->pipe[i]);

    	    	if (elt->type == HW_COMPOSITOR_DISPLAY_ELT)
    	    	{
    	    	    int chan_ind = pfGetChanPWinIndex(ViewState->chan[i]);
    	    	    pfCompositor* comp = elt->compositor;
    	    	    int j, m = pfGetCompositorNumPipes(comp);

    	    	    for (j=1; j<m; j++)
    	    	    {
    	    	    	ViewState->chan[n + num_slave_pipes] = pfGetCompositorChan(comp,j,chan_ind);
   	    	    	num_slave_pipes++;
   	    	    }
    	    	}
    	    }

    	    ViewState->totalChans = NumChans + num_slave_pipes;
    	}

	/* Distribute channels across pipes as defined by ChanOrder table */
	else if (Multipipe != HYPERPIPE )
	{
	    for (i=0; i< NumChans; i++)
		ViewState->chan[i] = pfNewChan(ViewState->pipe[i]);
	}
	else
	{
	    int pipeIndex = 0;
	    for (i=0; i< NumChans; i++) 
	    {
		ViewState->chan[i] = pfNewChan(ViewState->pipe[pipeIndex]);
		pipeIndex += i < NumHyperpipes ? NumHyperpipePipes[i] : 1;
	    }
	}
    }
    else 
    {
	pipe = ViewState->pipe[0];
	for (i=0; i< NumChans; i++)
	    ViewState->chan[i] = pfNewChan(pipe);

	/* Config channel viewports for the Multichannel Option board */
	if ((NumChans > 1) || ViewState->MCO)
	{
	    if (!ViewState->MCO)
		pfuConfigMCO(ViewState->chan, NumChans);
	    else
		pfuConfigMCO(ViewState->chan, -NumChans);
	}


    	/* XXX Paolo: HACK ALERT 
    	 * In single-compositor mode (-M3 or -M4) we force numChans to 1, but 
    	 * there really are multiple channels (two for each composited pipe,
    	 * where the first chan on each pipe is the GUI and chan1 is the 3d scene).
    	 * Here we copy pointers to all (non-GUI) composited channels into the 
    	 * viewstate chan array (without setting numChans though).
    	 * The composited channel pointers will be used in a couple of places
    	 * by perfly..
    	 */
    	if (ViewState->compositor)
    	{
    	    int n = pfGetCompositorNumPipes(ViewState->compositor);
    	    for (i=0; i<n; i++)
    	    {
    	    	/* Note that we ate grabbing pointer to second channel (index 1)
    	    	   on each pipe as channel 0 is the GUI channel */
    	    	ViewState->chan[i] = pfGetCompositorChan(ViewState->compositor,i,1);
    	    }
    	}
    }
	
    /* 
     * Disable sorting for OPAQUE bin in CULLoDRAW mode to 
     * improve overlap and parallelism between CULL and DRAW.
     * Still draw transparent geometry last though.
    */
    if (ProcSplit != PFMP_DEFAULT &&
	(ProcSplit & (PFMP_CULLoDRAW | PFMP_FORK_DRAW)) == 
		     (PFMP_CULLoDRAW | PFMP_FORK_DRAW))
    {
	for (i=0 ; i < NumChans ; i++)
	    pfChanBinOrder(ViewState->chan[i], PFSORT_OPAQUE_BIN, PFSORT_NO_ORDER);
    }

    /* Keep user-provided channel ordering within bounds */
    for (i=0 ; i < NumChans ; i++)
	ChanOrder[i] = PF_MIN2(NumChans-1, ChanOrder[i]);


#if 0
    /* Master channel is source for keyboard and mouse input */
    if (Multipipe == POWERWALL)
    {
    	/* PAOLO FINDME MASTERPIPE */
	ViewState->masterChan = ViewState->chan[ViewState->master_pipe_index];

    	///ViewState->powerwall.master_elt = 0;
    }
    else if (ViewState->numPanoramicElts > 0)
    {
    	/* PAOLO FINDME MASTERPIPE */
	ViewState->masterChan = ViewState->chan[ViewState->master_pipe_index];
    	///ViewState->powerwall.master_elt = 0;
    	///ViewState->master_pipe_index = 0;
    }
    else
    {
    	ViewState->master_pipe_index = ChanOrder[(NumChans - 1) >> 1];
	ViewState->masterChan = ViewState->chan[ViewState->master_pipe_index];
    }

#else
    /* PAOLO FINDME MASTERPIPE */
    ViewState->masterChan = ViewState->chan[ViewState->master_pipe_index];
#endif



    /* Determine whether master channel is on a composited pipe */
    {
    	pfPipe* master_pipe = pfGetChanPipe(ViewState->masterChan);
    	if (pfGetPipeCompositor(master_pipe))
    	{
    	    ViewState->master_pipe_is_composited = 1;
    	}
    	else
    	{
    	    ViewState->master_pipe_is_composited = 0;
    	}
    }


    /* If multipipe, then we want channels to share their viewports */
    if (Multipipe)
    {
   	int		share;

	/* Get the default */
	share = pfGetChanShare(ViewState->masterChan);

    	/* Add in the viewport share bit */
	share |= PFCHAN_VIEWPORT;

	if(Multipipe == POWERWALL)
	{
	    share &= ~PFCHAN_FOV;
	    share |= PFCHAN_NEARFAR;
	    share &= ~PFCHAN_VIEWPORT;
	}

	else if (ViewState->numPanoramicElts > 0)
	{
	    /* remove viewport from channel share */
	    share &= ~PFCHAN_VIEWPORT;
    	}

	if (GangDraw)
	{
	    /* Add GangDraw to share mask */
	    share |= PFCHAN_SWAPBUFFERS_HW;
	    pfNotify(PFNFY_NOTICE,PFNFY_PRINT, "!!!!! Asking for gang draw !!!!!!!");
	}

	pfChanShare(ViewState->masterChan, share);
    }

    /* Attach channels to master channel to form a channel group. */
    for (i=0; i< NumChans; i++)
	if (ViewState->chan[i] != ViewState->masterChan)
	    pfAttachChan(ViewState->masterChan, ViewState->chan[i]);

    /* Make sure we don't wrap field-of-view */
    if (NumChans * 45.0f > 360.0f)
	fov = 360.0f / NumChans;
    else
    	fov = 45.0f;

    /* if channel offset is supplied, use it instead of default */
    if(InitFOV)
    {
	fov = ViewState->fov[2];
	pfNotify(PFNFY_DEBUG, PFNFY_PRINT,
		 "Using user-supplied offset of %f\n", fov );
    }

    if ( (Multipipe != HYPERPIPE) && (Multipipe != POWERWALL) )
    {
    	/* Set each channel's viewing offset */
    	pfSetVec3(xyz, 0.0f, 0.0f, 0.0f);

	/* Horizontally tile the channels from left to right */
    	for (i=0; i< NumChans; i++)
    	{
	    pfSetVec3(hpr, (((NumChans - 1)*0.5f) - i)*fov, 0.0f, 0.0f);
	    pfChanViewOffsets(ViewState->chan[ChanOrder[i]], xyz, hpr);
    	}
    }

    chan = ViewState->masterChan;

    {
	int             share;

        /* Get the default */
        share = pfGetChanShare(ViewState->masterChan);
	share &= ~PFCHAN_APPFUNC;
        pfChanShare(ViewState->masterChan, share);
    }

    /* Set the callback routines for the pfChannel */
    pfChanTravFunc(chan, PFTRAV_APP, AppFunc);
    pfChanTravFunc(chan, PFTRAV_CULL, CullFunc);
    pfChanTravFunc(chan, PFTRAV_DRAW, DrawFunc);
    pfChanTravFunc(chan, PFTRAV_LPOINT, LpointFunc);

    /* Attach the visual database to the channel */
    pfChanScene(chan, ViewState->scene);

    /* Attach the EarthSky model to the channel */
    pfChanESky(chan, ViewState->eSky);

    /* Initialize the near and far clipping planes */
    pfChanNearFar(chan, ViewState->nearPlane, ViewState->farPlane);

    if (Multipipe != POWERWALL)
    {
	/* Vertical FOV is matched to window aspect ratio. */
	pfChanAutoAspect(chan, PFFRUST_CALC_VERT);
	pfMakeSimpleChan(chan, fov);
    }

    /* Initialize the viewing position and direction */
    pfChanView(chan, ViewState->initViews[0].xyz, ViewState->initViews[0].hpr);

    /* custom intiailization */
    initChannel(chan);

    /* Set up Stats structure */
    frameStats = pfGetChanFStats(chan);
    pfFStatsAttr(frameStats, PFFSTATS_UPDATE_SECS, 2.0f);
    pfFStatsClass(frameStats, PFSTATS_ENGFX, PFSTATS_ON);
    pfFStatsClass(frameStats, PFFSTATS_ENDB, PFSTATS_ON);

    /* For GLX windows and X input, get the window info on the master chan */
    if (ViewState->input == PFUINPUT_X)
    {
	pfuGetGLXWin(pfGetChanPipe(ViewState->masterChan), 
		&(ViewState->xWinInfo));
    }

    /* Call custom initGUI routine with the pipe of the master chan */
    initGUI(ViewState->masterChan);

    /* Initialize model for interactive motion */
    initXformer();
}

void ComputePowerWallFrustums(void)
{
    PowerWall *pwall = &(ViewState->powerwall);
    pfVec3 fov;

    float w = (1.0f-pwall->xoverlap)*pwall->cols +pwall->xoverlap;
    float h = (1.0f-pwall->yoverlap)*pwall->rows +pwall->yoverlap;

    float scaleX, scaleY;
    float d;

    int c;
    int row, col;
    float left, right, top, bott;

    int vpwidth = ViewState->maxScreenX/pwall->num_composited_cols;
    int vpheight = ViewState->maxScreenY/pwall->num_composited_rows;

    float scrnw, scrnh;

    fov[0] = ViewState->fov[0];
    fov[1] = ViewState->fov[1];
    fov[2] = ViewState->fov[2];

    if (fov[1] <= 0.0f)
    {
	if (fov[0] <= 0.0f)
	{
	    pfNotify (PFNFY_WARN, PFNFY_PRINT,
		"ComputePowerWallFrustums: invalid fov %f, %f\n",
		    fov[0], fov[1] );
	    fov[0] = 45.0f;
	}
	d = ((float)(vpwidth)*w/2.0f) / pfTan (fov[0]/2.0f);
	fov[1] = pfArcTan2 ( ((float)(vpheight)*h/2.0f), d ) * 2.0f;

	pfNotify (PFNFY_DEBUG, PFNFY_PRINT,
	    "fov[0]=%f ScrnW=%d ScrnH=%d ScrnAR=%f  PWall_W=%f PWall_H=%f --> fov[1]=%f",
		fov[0], vpwidth, vpheight, 
		(float)(vpwidth)/(float)(vpheight),
		(float)(vpwidth)*w, (float)(vpheight)*h, fov[1] );

	pfNotify (PFNFY_DEBUG, PFNFY_PRINT,
	    "Overall powerwall AR = %f / %f = %f",
		(float)(vpwidth)*w, (float)(vpheight)*h, 
		(float)(vpwidth*w)/(float)(vpheight*h) );

    }
    else if (fov[0] <= 0.0f)
    {
	d = ((float)(vpheight)*h/2.0f) / pfTan (fov[1]/2.0f);
	fov[0] = pfArcTan2 ( ((float)(vpwidth)*w/2.0f), d ) * 2.0f;

	pfNotify (PFNFY_DEBUG, PFNFY_PRINT,
	    "fov[1]=%f ScrnW=%d ScrnH=%d ScrnAR=%f  PWall_W=%f PWall_H=%f --> fov[0]=%f",
		fov[1], vpwidth, vpheight, 
		(float)(vpwidth)/(float)(vpheight),
		vpwidth*w, vpheight*h, fov[0] );
    }

    scaleX = 2.0f * pfTan (fov[0]/2.0f);
    scaleY = 2.0f * pfTan (fov[1]/2.0f);

    scrnw = scaleX / w;
    scrnh = scaleY / h;

    c = 0;

    /* bott = -1.0f * (height/2.0f); */
    /* top = bott + 1.0f; */

    bott = -scaleY/2.0f;
    top = bott + scrnh;

    pfNotify (PFNFY_DEBUG, PFNFY_PRINT, "POWERWALL FRUSTUMS:");

    for (row=0; row<pwall->rows; row++)
    {
        left = -scaleX/2.0f;
        right = left + scrnw;

	for (col=0; col<pwall->cols; col++, c++)
	{
	    pfChannel *chan = ViewState->chan[ ChanOrder[c] ];
	    float chan_near, chan_far;
	    pfGetChanNearFar( chan, &chan_near, &chan_far);

	    pfNotify (PFNFY_DEBUG, PFNFY_MORE,
		"Chan[%d][%d] (index=%d) - l=%f r=%f b=%f t=%f  AR= w/h= %f",
		    row, col, c, 
		    left*chan_near, right*chan_near, bott*chan_near, top*chan_near,
		     (right-left)/(top-bott) );

    	    if (chan == ViewState->masterChan)
    	    {
    	    	float l, r, b, t;
    	    	int vpW, vpH, vpL, vpB;
    	    	float L, B;

    	    	pfGetChanViewport(chan, &l, &r, &b, &t);

    	    	vpW = ViewState->maxScreenX / pwall->num_composited_cols;
    	    	vpH = ViewState->maxScreenY / pwall->num_composited_rows;
    	    	vpL = (col % pwall->num_composited_cols) * vpW;
    	    	vpB = (row % pwall->num_composited_rows) * vpH;
    	    	
    	    	L = ((float)vpL) / ((float)(ViewState->maxScreenX));
    	    	B = ((float)vpB) / ((float)(ViewState->maxScreenY));

    	    	if (l != L)
    	    	    left += (((l-L)/(r-L))*(right-left));

    	    	if (b != B)
    	    	    bott += (((b-B)/(t-B))*(top-bott));
    	    }

	    pfMakePerspChan (chan, left*chan_near, right*chan_near, bott*chan_near, top*chan_near );
	    
	    left = right - (scrnw*pwall->xoverlap);
	    right = left + scrnw;
	}

	bott = top - (scrnh*pwall->yoverlap);
	top = bott + scrnh;
    }
}



/*
 * All latency critical processing should be done here. This is
 * typically the viewing position. One should also read latency critical
 * devices here.
*/
void
PreFrame(void)
{
    /* Poll the mouse buttons and position */
    pfuGetMouse(&ViewState->mouse);

    /* Update the simulation */
    localPreFrame(ViewState->masterChan);

    /* if using real-time input device for view, update view here */
    if (ViewState->rtView)
	updateView(ViewState->masterChan);
}


/* Application simulation update is done here */
void
PostFrame(void)
{
    /* Get snapshot of event/key queues */
    pfuGetEvents(&ViewState->events);

    /* if doing app traversal using view, want to update view here */
    if (!ViewState->rtView)
	updateView(ViewState->masterChan);

    /* Update the simulation state */
    updateSim(ViewState->masterChan);
}

/*************************************************************************
 *									 *
 *		    ISECT PROCESS ROUTINES				 *
 *									 *
 *************************************************************************/

/*
 * Collide the viewing position represented by the pfiXformer with the
 * scene.
*/
/* ARGSUSED (suppress compiler warn) */
void
IsectFunc(void *data)
{
    if (ViewState->xformer)
#ifdef  __PFUI_D_H__
	pfiCollideXformerD(ViewState->xformer);
#else
	pfiCollideXformer(ViewState->xformer);
#endif
}


/*************************************************************************
 *									 *
 *		    APP PROCESS ROUTINES				 *
 *									 *
 *************************************************************************/

/* In App callback routine, pre-pfApp() processing */
void
PreApp(pfChannel *chan, void *data)
{
    preApp(chan, data);
    return;
}


/* In App callback routine, post-pfApp() processing */
void
PostApp(pfChannel *chan, void *data)
{
    postApp(chan, data);
    return;
}

/*************************************************************************
 *									 *
 *		    CULL PROCESS ROUTINES				 *
 *									 *
 *************************************************************************/

/* ARGSUSED (suppress compiler warn) */
void
ConfigCull(int pipe, unsigned int stage)
{
    if ((pfGetMPBitmask() & PFMP_FORK_CULL) &&
	(ViewState->procLock & PFMP_FORK_CULL) &&
	(CullCPU > -1))
	    pfuLockDownProc(CullCPU);
}

/* ARGSUSED (suppress compiler warn) */
void
ConfigLPoint(int pipe, unsigned int stage)
{
    if ((pfGetMPBitmask() & PFMP_FORK_LPOINT) &&
	(ViewState->procLock & PFMP_FORK_LPOINT) &&
	(LPointCPU > -1))
	    pfuLockDownProc(LPointCPU);
}

/* In Lpoint callback routine, pre-pfLpoint processing */
void
PreLpoint(pfChannel *chan, void *data)
{
    preLpoint(chan, data);
    return;
}

/* In Lpoint callback routine, post-pfLpoint processing */
void
PostLpoint(pfChannel *chan, void *data)
{
    postLpoint(chan, data);
    return;
}

/* In Cull callback routine, pre-pfCull() processing */
void
PreCull(pfChannel *chan, void *data)
{
    preCull(chan, data);
    return;
}


/* In Cull callback routine, post-pfCull() processing */
void
PostCull(pfChannel *chan, void *data)
{
    postCull(chan, data);
    return;
}


/*****************************************************************************
 *									     *
 *		DRAW PROCESS ROUTINES					     *
 *									     *
 *****************************************************************************/

/* Initialize window */

char const WinName[80] = "OpenGL Performer";

/*
 * Initialize Draw Process and Pipe
 */
/* ARGSUSED (suppress compiler warn) */
void
OpenPipeline(int pipe, unsigned int stage )
{
	/* Not using pfu-Process manager - do cmdline specified locking */
    if ((ViewState->procLock & PFMP_FORK_DRAW) && (DrawCPU > -1))
	pfuLockDownProc(DrawCPU);
}

/* 
 * Create and configure an X window.
 */

void
OpenXWin(pfPipeWindow *pw)
{
    /* -1 -> use default screen or that specified by shell DISPLAY variable */
    pfuFBConfigConstraint constraints[10];
    pfuGLXWindow *win;
    pfPipe *p;
    pfFBConfig fbc;
    
    int numConstraints;
    
    /* use pfuChooseFBConfig to limit config to that of FBAttrs */
    /* Only select new visual if one wasn't explicitly set */
    if (!pfGetPWinFBConfigId(pw))
    {
    	/* Initialize frame buffer config constraints */
    	constraints[0] = pfuFBConstraintValue(PFFB_RGBA, TRUE, 10.0f);
	constraints[1] = pfuFBConstraintValue(PFFB_DOUBLEBUFFER, TRUE, 10.0f);
	constraints[2] = pfuFBConstraintValue(PFFB_STEREO, FALSE, 10.0f);
	constraints[3] = pfuFBConstraintRange(PFFB_RED_SIZE, 8, 12, 1.0f, 
	    0.25f);
	constraints[4] = pfuFBConstraintRange(PFFB_STENCIL_SIZE, 8, 16, 
	    0.05f, 0.05f);

	if (ZBits > -1)
	{
    	    constraints[5] = pfuFBConstraintValue(PFFB_DEPTH_SIZE, ZBits, 
	    	1.0f);
	}
	else
	{
    	    constraints[5] = pfuFBConstraintRange(PFFB_DEPTH_SIZE, 24, 32, 
	    	1.0f, 0.05f);
	}

	if (Multisamples > -1)
	{
    	    constraints[6] = pfuFBConstraintValue(PFFB_SAMPLES, Multisamples, 
	    	1.0f);
	}
	else
	{
#if defined(WIN32) || defined(__ia64__) || defined(__linux__)
    	    constraints[6] = pfuFBConstraintRange(PFFB_SAMPLES, 0, 1, 0.1f, 
	    	1.0f);
#else
    	    if (strcmp(pfGetMachString(), "INV_VGER") == 0)
	    {
	    	constraints[6] = pfuFBConstraintRange(PFFB_SAMPLES, 0, 1, 0.1f, 
		    1.0f);
	    }
	    else
	    	constraints[6] = pfuFBConstraintValue(PFFB_SAMPLES, 8, 0.1f);
#endif
    	}
	
	numConstraints = 7;
    	
    	if (ChooseShaderVisual)
	{
	    constraints[numConstraints] = pfuFBConstraintRange(
	    	PFFB_ALPHA_SIZE, 8, 12, 1.0f, 0.25f);
	    numConstraints ++;
    	}
	
	fbc = pfuSelectFBConfig(NULL, pfGetPWinScreen(pw), constraints, 
	    numConstraints, pfGetSharedArena());
	
	if (fbc)
	{
#ifdef WIN32 
	    pfPWinFBConfigId(pw, fbc);
#else
	    pfPWinFBConfig(pw, fbc);
#endif
	}
    }
    
    p = pfGetPWinPipe(pw);
    if (!(win = pfuGLXWinopen(p, pw, NULL)))
        pfNotify(PFNFY_FATAL, PFNFY_RESOURCE,
	    "OpenXPipeline: Could not create GLX Window.");

    win = win; /* suppress compiler warn */

    /* set initial modes and welcome screen */
    InitGfx(pw);
}

/* 
 * Create and configure a GL window.
*/
void
OpenWin(pfPipeWindow *pw)
{
    pfOpenPWin(pw);
    /* set initial modes and welcome screen */
    InitGfx(pw);
}


/* Initialize graphics state */
void
ConfigPWin(pfPipeWindow *pw)
{
    /* set up default lighting parameters */
    pfLightModel	*lm;
    pfMaterial 		*mtl;

    /* 
     * Set up default material in case the database does not
     * have any. Use PFMTL_CMODE_AD so pfGeoSet colors supply the
     * ambient and diffuse material color.
    */
    mtl = pfNewMtl(NULL);
    pfMtlColorMode(mtl, PFMTL_FRONT, PFMTL_CMODE_AD);
    pfApplyMtl(mtl);

    /* Set up default lighting model. */
    lm = pfNewLModel(NULL);
    pfLModelAmbient(lm, 0.0f, 0.0f, 0.0f);

    /* 
     * Local viewer is required so that lighting matches across adjacent
     * channels since rotational offsets are encoded in the viewing 
     * matrix because GL will not fog channels properly if the offsets
     * are encoded in the projection matrix. 
    */
    if (NumChans > 1)
	pfLModelLocal(lm, 1);

    pfApplyLModel(lm);
    if (!ViewState->lighting)
	pfDisable(PFEN_LIGHTING);
    
    /* Prebind textures to be used in simulation */
    if (!(pfGetPWinType(pw) & PFPWIN_TYPE_STATS))
    {
	pfEnable(PFEN_TEXTURE);
	/* Do not show textures in hyperpipe mode */
	pfuDownloadTexList(ViewState->texList,
		Multipipe != HYPERPIPE ? PFUTEX_SHOW : 0);
    }
}

void
InitGfx(pfPipeWindow *pw)
{

    /* Set up default state values.  */
    ConfigPWin(pw);

    /* call custom routine to initialize pipe */
    initPipe(pw);
    
}


/* In Draw callback routine, pre-pfDraw() processing */
void
PreDraw(pfChannel *chan, void *data)
{
    pfPipe 	*pipe = pfGetChanPipe(chan);
    int 	pipeId = pfGetId(pipe);
#if 0
    pfPipeVideoChannel *pvc;
    int		id=0;
#endif /* 0 */

    /*
     * If this pipe does not display the GUI, then clear the space where
     * the GUI would ordinarily be.
     *
     * if we have DVR, we have to apply our resize by the GUI viewport size
    */
    /* XXX should really only do this once per pipe !!! */
    if ((Multipipe == 1) && ViewState->gui &&
	(chan != ViewState->masterChan) && 
	(ViewState->redrawOverlay || 
	 (ViewState->drawFlags[pipeId] & REDRAW_WINDOW)))
    {
	pfChannel	*gchan;
	pfPipeVideoChannel *pvchan;
	int		xo, yo, xs, ys;
	float		xscale, yscale;

	pvchan = pfGetChanPVChan(chan);
	pfGetPVChanScale(pvchan, &xscale, &yscale);
	
	gchan = pfuGetGUIChan();
	if (gchan)
	{
	    pfGetChanOrigin(gchan, &xo, &yo);
	    pfGetChanSize(gchan, &xs, &ys);
	    xo = xo*xscale + 0.5;
	    xs = xs*xscale + 0.5;
	    yo = yo*yscale + 0.5;
	    ys = ys*yscale + 0.5;
	}
	else
	{
	    xo = yo = 0;
	    pfGetPipeSize(pipe, &xs, &ys);
	}
	if (xs > 0)
	{
	    glPushAttrib(GL_SCISSOR_BIT);
	    glScissor(xo, yo, xs, ys);
	    glClearColor(0.5f, 0.5f, 0.5f, 1.0f);
	    glClear(GL_COLOR_BUFFER_BIT);
	    glPopAttrib();
	}
    }

    /* Clear the frame buffer */
    pfClearChan(chan);

    /* Update Draw modes that have been changed in ViewState structure. */
    localPreDraw(chan, data);
}


/* In Draw callback routine, post-pfDraw() processing */
/* ARGSUSED (suppress compiler warn) */
void
PostDraw(pfChannel *chan, void *data)
{
    /* Display Stats, if active */
    if (ViewState->stats)
	pfDrawChanStats(chan);

    /*
     * Read GL keyboard, mouse. Make sure we only collect input on
     * master channel. If hyperpiping we must collect on all pipes.
    */
    if (chan == ViewState->masterChan)
    	pfuCollectInput();

    /* Call local post draw routine */
    localPostDraw(chan, data);
}
