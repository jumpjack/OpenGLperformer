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
 * cmdline.c
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#ifndef WIN32
#include <getopt.h>
#endif

#include <Performer/pf.h>
#include <Performer/pfutil.h>
#include <Performer/pfui.h>
#include <Performer/pfdu.h>

#include "perfly.h"
#include "cmdline.h"

/* **************************************************************************
 *		Process the command line options
 ****************************************************************************/

static void
usage(char *progName)
{
    char	*baseName;
    static char *helpList =
	"usage:\n"
	"	%s [options] model.ext ...\n"
	"\n"
	"options:\n"
	"  -A <string>                   -welcome text\n"
	"  -a <string>                   -overlay text\n"
	"  -B <int>,<int>,..             -Specify mapping of processes to processors for locked CPUs\n"
	"  -b r,g,b[,a]                  -Earth/Sky clear color\n"
	"  -c <v,numChannels>            -Number of channels\n"
	"      v -> optional request for mapping to video channels\n"
	"  -C a,b,c,...                  -Pipes for channels (L-to-R)\n"
	"  -d                            -Drive\n"
	"  -D                            -Lock down draw process\n"
	"  -e <h,p,r>                    -Initial view angles\n"
	"  -E <string>                   -Earth sky mode\n"
	"      tag      -> tag clear\n"
	"      clear    -> regular clear\n"
	"      sky      -> sky only\n"
	"      skygrnd  -> sky and ground\n"
	"      skyclear -> sky and clear\n"
	"  -f                            -Fly\n"
	"  -F <path>                     -Set file path\n"
	"  -g <format>                   -GUI format\n"
	"      0 -> GUI OFF\n"
	"      1 -> GUI_VERTICAL\n"
	"      2 -> GUI_HORIZONTAL\n"
	"  -G                            -Use GANGDRAW with Multipipe\n"
	"  -h                            -Print command list\n"
	"  -H <horiz,vert,offset>        -Set Custom Multipipe Field of View\n"
	"  -i <count>                    -Set file loading repeat count\n"
	"  -I <count>                    -Exit after printing stats for count frames\n"
	"  -j <font_type>                -Set font type\n"
	"      0 -> PFDOBJFNT_TEXTURED\n"
	"      1 -> PFDOBJFNT_OUTLINED\n"
	"      2 -> PFDOBJFNT_FILLED\n"
	"      3 -> PFDOBJFNT_EXTRUDED\n"
	"  -J <font_name>                -Set Font to Named Font\n"
	"  -k r,g,b[,a]                  -Scribed-style draw color\n"
	"  -K <count>                    -Combine billboards up to size count\n"
	"  -l <style>                    -Set lighting enable\n"
	"      0 -> lighting off\n"
	"      1 -> lighting eye\n"
	"      2 -> lighting sky\n"
	"  -l <x,y,z,r,g,b>              -Add a light source to the scene\n"
	"  -L                            -Free processes, then lock down (must be root)\n"
	"  -m <mode>                     -Multi-processing mode\n"
	"     -1 -> PFMP_DEFAULT\n"
	"      0 -> PFMP_APPCULLDRAW\n"
	"      2 -> PFMP_APP_CULLDRAW\n"
	"      4 -> PFMP_APPCULL_DRAW\n"
	"      6 -> PFMP_APP_CULL_DRAW\n"
	"      131072 -> PFMP_APPCULL_DL_DRAW\n"
	"      131074 -> PFMP_APP_CULL_DL_DRAW\n"
	"      65540 -> PFMP_APPCULLoDRAW\n"
	"      65542 -> PFMP_APP_CULLoDRAW\n"
	"  -M <mode>                     -Multi-pipe mode\n"
	"      0 -> single pipe mode\n"
	"      1 -> multipipe mode\n"
	"      2[,h=<netId>...][,s=<dpyName>...] -> hyperpipe mode\n"
	"          <netId>   - hyperpipe network id (see hyperpipeinfo(1))\n"
	"          <dpyName> - X display name for single pipe (including screen)\n"
	"      3 -> Single Compositor mode\n"
	"      4 -> Software Compositor mode\n"
	"      5,<ncols>,<nrows>,<xoverlap>,<yoverlap>[,p=<dpyName>]..[,c=<netId>] -> Powerwall mode\n"
	"      6,[p=<dpyName>]..[,c=<netId>] -> Panoramic Mode with multi-compositor support\n"
	"  -n <notify>                   -Debug level\n";
    static char *helpList2 =
	"  -N                            -Assign non-degrading priorities to processes\n"
	"  -o <mode>,<value>             -Set builder mode (default value is OFF)\n"
	"  -O <mode>,<value>             -Set builder mode (default value is ON)\n"
	"  -p <x,y,z>                    -Initial view position\n"
	"  -P <phase>                    -Phase\n"
	"      0 -> PFPHASE_FLOAT\n"
	"      1 -> PFPHASE_LOCK\n"
	"      2 -> PFPHASE_FREE_RUN\n"
	"      3 -> PFPHASE_LIMIT\n"
	"  -q <optimize>                 -Optimize pfGeoStates for single global gstate\n"
	"  -Q <optimize>                 -Flatten tree and remove empty nodes\n"
	"      0 -> Off, 1 -> On\n"
	"  -r <frameRate>                -Target frame rate in Hertz\n"
	"  -R <mode>                     -Retessellate input geometry\n"
	"      0 -> Off, 1 -> On\n"
	"  -s <LODscale>                 -Set LOD scale factor\n"
	"  -S <mode>                     -Show triangle strips via color coding\n"
	"      0 -> Off, 1 -> On\n"
        "  -t <0xVisID0>,<0xVisID1>,...  -specify GLX Visual ID (in Hex)\n" 
	"  -T <mode>                     -use OpenGL display lists\n"
	"      0 -> Off, 1 -> On\n"
	"  -u                            -Inflate input files\n"
	"  -U                            -Start Calligraphic boards and LPoint process\n"
	"  -v <type>                     -Use vertex arrays of specified type\n"
	"      0 -> Off \n"
	"      1 -> PFGS_PA_C4UBN3ST2FV3F-all attrs packed in arrays\n"
	"      2 -> PFGS_PA_C4UBN3ST2F   -all attrs but verts packed in arrays\n"
	"  -V <mode>                     -Set Dynamic Resolution (DVR) mode\n"
	"     <mode,xsize,ysize>         -Set DVR mode and initial size.\n"
	"      0 -> PFPVC_DVR_OFF \n"
	"      1 -> PFPVC_DVR_MANUAL \n"
	"      2 -> PFPVC_DVR_AUTOMATIC \n"
	"  -w <file>.out                 -Write scene in ASCII\n"
	"  -w <file.ext>                 -Write scene in .ext format\n"
	"  -W <Size>                     -Window size\n"
	"  -W <xSize,ySize>              -Window size\n"
	"  -x <fork>                     -GLX input handling mode\n"
	"      0 -> input handling is not forked.\n"
	"      1 -> forked X input handling is used.\n"
	"      2 -> forked X input allowing multi-pipe (default).\n"
	"  -X <radius>                   -Explode input files\n"
	"  -y ext,mode,value             -Set loader mode for filetype ext to value\n"
	"  -Y ext,alias                  -Substitute <ext> for file extension <alias>\n"
	"  -z near,far                   -Set near/far clip ranges\n"
	"  -Z                            -Don't initially free CPUs before lock\n"
        "  -1 demo_path_file             -Demo path file name\n"
        "  -3                            -use default Z depth\n"
        "  -4                            -minimal multisample visual (4 samples) \n"
        "  -5 <total_horiz_fov>,<vert_fov>,<overlap in percent> -Set Nx1 Custom Asymmetirc Multipipe Field of View\n"
	"  -7                            -Trackball (default, undoes effect of -f, -d)\n"
        "  -9                            -Use visual with stencil, destination alpha\n"
	"  --snap-frame	<int>		 -Save a snapshot of the scene on specified frame.\n"
	"  --snap-file <filename>	 -Specify filename for snap image created through option --snap-frame\n"
    	"  --compositor id,mode [,p=<dpyname> ..] -Specify compositor inputs and initial composition mode\n"
	"  --powerwall-compositor-setup <n>x<m> -Shrinks and offsets channels viewports for specified compositor setup\n"
	"  --master-elt <i> - index of display elemnt that will display the GUI panel in M5 and M6 modes.\n"
	"\n"
        "See 'man perfly' for more details.\n";
    
    /* just use program's base name: /usr/fred/base -> base */
    if (progName == NULL)
        baseName = "UNKNOWN";
    else if ((baseName = strchr(progName, '/')) == NULL)
	    baseName = progName;
    
    /* print list of command-line options and arguments */
    fprintf(stderr, helpList, baseName);
    fprintf(stderr, helpList2);
    fflush(stderr);
    exit(1);
}

char optionStr[] = 
    "A:a:b:B:c:C:dDe:E:fF:g:GhH:i:I:j:J:k:K:l:Lm:M:"
    "o:O:Nn:p:P:q:Q:r:R:s:S:t:T:uUv:V:W:w:x:X:y:Y:z:Z234791:5:6:?-:";


/* ====================================================================== */
/* The linux glibc-version of getopt() contains an apparent memory        */
/* corruption bug which makes perfly fail when loading certain '.perfly'  */
/* files (such as town.perfly).  These routines work around the bug by    */
/* implementing a limited version of getopt() called WARgetopt().         */
/* ====================================================================== */
/* and on win32 there is no such thing as getopt() ... */

#if defined(__linux__) || defined(WIN32)
static char *optarg;
static int optind = 1, opterr = 0;

static int WARgetopt(int argc, char *argv[], const char *optstring)
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


int
ApplyCompositorParams( pfCompositor* comp, int id)
{
    int i, ncp = pfGetNum(ViewState->listCompParams);
    for (i=0; i<ncp; i++)
    {
    	CompositorParams* cp =
    	    (CompositorParams*)pfGet(ViewState->listCompParams, i);

    	if (cp->id == id)
    	{
    	    int j, nchildren = pfGetNum(cp->listChildren);

    	    pfCompositorMode( comp, PFCOMP_COMPOSITION_MODE, cp ->compmode );
    	    pfCompositorMode( comp, PFLOAD_BALANCE, cp ->dynamic );

    	    for (j=0; j<nchildren; j++)
    	    	pfCompositorAddChild( comp, pfGet(cp->listChildren, j) );
    	    
    	    return 1;
    	}
    }
    return 0;
}



int
processCmdLine(int argc, char *argv[], char ***files)
{
#if !defined(__linux__) && !defined(WIN32)
    extern char *optarg;
    extern int        optind;
#endif
    /* save and restore optarg&optind so we can be re-entrant */
    char * 	       saved_optarg = optarg;
    int 	       saved_optind = optind;
    int                opt;
    int                guiFormatSet = 0;
    int		       returnval;
    
    /* process command-line arguments */
    #if defined(__linux__) || defined(WIN32)
    while ((opt = WARgetopt(argc, argv, optionStr)) != -1)
    #else
    while ((opt = getopt(argc, argv, optionStr)) != -1)
    #endif
    {
        switch (opt)
        {
        case '-':
            {
                /* Handle long-form arguments e.g. --frame-rate : */
                char *arg = optarg;
                optarg=argv[optind++];
            
                if (strcmp(arg, "snap-frame") == 0)
                {
		    if (!optarg)
			usage(argv[0]);
                    ViewState->snapFrame=atoi(optarg);
                }
                else if (strcmp(arg, "snap-file") == 0)
                {
		    if (!optarg)
			usage(argv[0]);
                    ViewState->snapFile = strdup(optarg);
                }

                else if (strcmp(arg, "master-elt") == 0)
                {
		    if (!optarg)
		    {
			pfNotify (PFNFY_WARN, PFNFY_PRINT,
			    "Missing argument for --master-elt option.");
			usage(argv[0]);
		    }
		    ViewState->master_pipe_index = atoi(optarg);
                }

                else if (strcmp(arg, "powerwall-compositor-setup") == 0)
                {
		    if (!optarg)
		    {
			pfNotify (PFNFY_WARN, PFNFY_PRINT,
			    "Missing argument for powerwall-compositor-setup option.");
			usage(argv[0]);
		    }
		    if ( (sscanf(optarg,"%dx%d",
			&(ViewState->powerwall.num_composited_cols),
			&(ViewState->powerwall.num_composited_rows)) != 2) ||
			 (ViewState->powerwall.num_composited_cols < 1) ||
			 (ViewState->powerwall.num_composited_rows < 1) )
		    {
			pfNotify (PFNFY_WARN, PFNFY_PRINT,
			    "Invalid argument \"%s\" for powerwall-compositor-setup"
				" option.", optarg);
			usage(argv[0]);
		    }

		    pfNotify (PFNFY_DEBUG, PFNFY_PRINT,
			"powerwall-compositor-setup option read-in: %dx%d\n",
			    ViewState->powerwall.num_composited_cols,
			    ViewState->powerwall.num_composited_rows );
                }

    	    	else if (strcmp(arg, "compositor") == 0)
                {
    	    	    char* str;
    	    	    char* comma;
    	    	    CompositorParams *cp;
    	    	    char* cmode;
    	    	    int len;

		    if (!optarg)
		    {
			pfNotify (PFNFY_WARN, PFNFY_PRINT,
			    "Missing argument for --compositor option.");
			usage(argv[0]);
		    }

    	    	    str = pfStrdup(optarg, pfGetSharedArena());
    	    	    comma = strchr(str,',');

    	    	    if (comma==NULL)
		    {
			pfNotify (PFNFY_WARN, PFNFY_PRINT,
			    "Invalid argument for --compositor option.");
			usage(argv[0]);
		    }

    	    	    *comma = '\0';
    	    	    
    	    	    cp = pfCalloc( 1, sizeof(CompositorParams), pfGetSharedArena());
    	    	    cp->listChildren = pfNewList(sizeof(char*), 1, pfGetSharedArena());
    	    	    cp->software = 0;
    	    	    cp->id = atoi(str);

    	    	    cmode = str+2;
    	    	    comma = strchr(cmode,',');
    	    	    if (comma)    	    	    
    	    	    	*comma = '\0'; 	    	    
    	    
    	    	    len = strlen(cmode);

    	    	    if ( (len<2) || (len>3))
		    {
			pfNotify (PFNFY_WARN, PFNFY_PRINT,
			    "Invalid argument for --compositor option.");
			usage(argv[0]);
		    }

    	    	    if (len==3)
    	    	    {
    	    	    	if (tolower(cmode[2]) != 'd')
			{
			    pfNotify (PFNFY_WARN, PFNFY_PRINT,
				"Invalid argument for --compositor option.");
			    usage(argv[0]);
			}
    	    	    	cp->dynamic = 1;
    	    	    }
    	    	    else
    	    	    	cp->dynamic = 0;

    	    	    if ( (tolower(cmode[0])=='h') && (tolower(cmode[1])=='s'))
    	    	    	cp -> compmode = PFCOMP_COMPMODE_HORIZ_STRIPES;

    	    	    else if ( (tolower(cmode[0])=='v') && (tolower(cmode[1])=='s'))
    	    	    	cp -> compmode = PFCOMP_COMPMODE_VERT_STRIPES;

    	    	    else if ( (tolower(cmode[0])=='l') && (tolower(cmode[1])=='t'))
    	    	    	cp -> compmode = PFCOMP_COMPMODE_LEFT_TILES;

    	    	    else if ( (tolower(cmode[0])=='r') && (tolower(cmode[1])=='t'))
    	    	    	cp -> compmode = PFCOMP_COMPMODE_RIGHT_TILES;

    	    	    else if ( (tolower(cmode[0])=='b') && (tolower(cmode[1])=='t'))
    	    	    	cp -> compmode = PFCOMP_COMPMODE_BOTT_TILES;

    	    	    else if ( (tolower(cmode[0])=='t') && (tolower(cmode[1])=='t'))
    	    	    	cp -> compmode = PFCOMP_COMPMODE_TOP_TILES;

    	    	    else if ( (tolower(cmode[0])=='a') && (tolower(cmode[1])=='a'))
    	    	    	cp -> compmode = PFCOMP_COMPMODE_ANTIALIAS;

    	    	    else
		    {
			pfNotify (PFNFY_WARN, PFNFY_PRINT,
			    "Invalid compmode for --compositor option.");
			usage(argv[0]);
		    }

    	    	    if (comma)
    	    	    {
    	    	    	char* s = comma+1;

    	    	    	while (s && *s)
    	    	    	{
    	    	    	    char *nextComma = strchr(s,',');
    	    	    	    if (nextComma)
    	    	    	    	*nextComma = '\0';

    	    	    	    if ( tolower(s[0]) != 'p')
    	    	    	    {
				pfNotify (PFNFY_WARN, PFNFY_PRINT,
				    "Invalid compmode for --compositor option.");
				usage(argv[0]);
    	    	    	    }

    	    	    	    pfAdd(cp->listChildren, s+2);
    	    	    	    if (nextComma)
        	    	    	s = nextComma+1;
    	    	    	    else
    	    	    	    	s = NULL;
    	    	    	}
    	    	    }
    	    	    pfAdd (ViewState->listCompParams, cp);

    	    	}


                else
                    usage(argv[0]);
	        break;
            }
	    /* Specify introductory 3D welcome text */
	case 'A':	
	    strcpy(ViewState->welcomeText, optarg);
	    break;
	    
	    /* Specify overlay-plane attribution text */
	case 'a':	
            ViewState->useOverlay = 1;
	    strcpy(ViewState->overlayText, optarg);
	    break;
	    
	    /* Specify mapping of processes to processors for locked CPUs */
	case 'B': 
	    {
		int i;
		int count;
		int num = 0;
		char	*start = optarg;

		while ((sscanf(start, "%d%n", &i, &count) == 1) && (num <= 4))
		{
		    if (*(start += count) != '\0')
			++start;
		    num ++;
		    switch (num)
		    {
			case 1:
			    AppCPU = i;
			    break;
			case 2:
			    CullCPU = i;
			    break;
			case 3:
			    DrawCPU = i;
			    break;
			case 4:
			    LPointCPU = i;
			    break;
		    }
		}

		if (num == 0)
		    usage(argv[0]);
	    }
	    break;
	    
	    /* Specify the Earth/Sky clear color */
	case 'b':
	    if (sscanf(optarg, "%f,%f,%f,%f",
		       &ViewState->earthSkyColor[0],
		       &ViewState->earthSkyColor[1],
		       &ViewState->earthSkyColor[2],
		       &ViewState->earthSkyColor[3]) != 4 &&
		sscanf(optarg, "%f,%f,%f",
		       &ViewState->earthSkyColor[0],
		       &ViewState->earthSkyColor[1],
		       &ViewState->earthSkyColor[2]) != 3)
		usage(argv[0]);
	    break;
	    
	    /* Specify number of channels to use */
	case 'c':	
	    /* accept v,%d - v is optional */
	    if (sscanf(optarg, "v,%d", &NumChans) != 1)
	    {
		ViewState->MCO = 0;
		if (sscanf(optarg, "%d", &NumChans) != 1)
		    usage(argv[0]);
	    } 
	    else
		ViewState->MCO = 1;
	    if (NumChans < 1)
		usage(argv[0]);
	    if (!guiFormatSet && (NumChans > 1))
		ViewState->guiFormat = GUI_HORIZONTAL;
	    break;
	    
	    /* Specify the pipe to use for each channel */
	case 'C':	
	    {
		int	 	index = 0;
		int	 	count = 0;
		char	*start = optarg;
		
		while (sscanf(start, "%d%n", &ChanOrder[index++], &count) == 1)
		    if (*(start += count) != '\0')
			++start;
	    }
	    break;
	    
	    /* Set pfuXformer default to Drive */
	case 'd':
	    ViewState->xformerModel = PFITDF_DRIVE;
	    break;
	    
	    /* lock down processor for just draw process */
	case 'D':
	    ViewState->procLock |= PFMP_FORK_DRAW;
	    break;
	    
	    /* Set the initial view Euler angles (h p r) */
	case 'e':
	    if (sscanf(optarg, "%f,%f,%f",
		       &ViewState->initViews[0].hpr[PF_H],
		       &ViewState->initViews[0].hpr[PF_P],
		       &ViewState->initViews[0].hpr[PF_R]) == 3)
		InitHPR = TRUE;
	    else
		usage(argv[0]);
	    break;
	    
	    /* Set Earth Sky mode */
	case 'E':
	    if (strcmp(optarg, "tag") == 0)
		if (ViewState->haveTagClear)
		    ViewState->earthSkyMode = PFES_TAG;
		else
		    ViewState->earthSkyMode = PFES_FAST;
	    else if (strcmp(optarg, "sky") == 0)
		ViewState->earthSkyMode = PFES_SKY;
	    else if (strcmp(optarg, "skygrnd") == 0)
		ViewState->earthSkyMode = PFES_SKY_GRND;
	    else if (strcmp(optarg, "skyclear") == 0)
		ViewState->earthSkyMode = PFES_SKY_CLEAR;
	    else if (strcmp(optarg, "clear") == 0)
		ViewState->earthSkyMode = PFES_FAST;
	    else 
		usage(argv[0]);
	    break;
	    
	    /* Set pfuXformer default to Fly */
	case 'f':
	    ViewState->xformerModel = PFITDF_FLY;
	    break;
	    
	    /* Specify the file search path -- look here first */
	case 'F':
	    {
	    size_t oldLength = 0;
	    size_t newLength = 0;
	    size_t fullLength = 0;
	    const char *oldPath = pfGetFilePath();
	    char *newPath = optarg;
	    char *fullPath = NULL;

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
		    strcat(fullPath, ":");
		if (newPath != NULL)
		    strcat(fullPath, newPath);
		pfFilePath(fullPath);
		pfFree(fullPath);
	    }
	    }
	    break;
	    
	    /* Specify the GUI format */
	case 'g':
	    ViewState->gui = ((ViewState->guiFormat = atoi(optarg)) ? 1 : 0);
	    if( ViewState->guiFormat != GUI_HORIZONTAL )
		ViewState->guiFormat = GUI_VERTICAL;
	    guiFormatSet = 1;
	    break;
	    
	    /* Initialize with GUI Panel disabled */
	case 'G':
	    GangDraw ^= 1;
	    break;
	    
	    /* set repeat count for file loading */
	case 'H':
	    {
	    int ret = sscanf(optarg, "%f,%f,%f", 
				&ViewState->fov[0],
				&ViewState->fov[1],
				&ViewState->fov[2]);
	    if (ret < 2)
		usage(argv[0]);

	    if (ret == 2)
		ViewState->fov[2] = 0.0f;

	    InitFOV = TRUE;
	    }
	    break;
	case 'i':
	    ViewState->iterate = atoi(optarg);
	    break;
	    
	    /* set repeat count for file loading */
	case 'I':
	    ViewState->printStats = atoi(optarg);
	    ViewState->exitCount = atoi(optarg);
	    break;
	    
	case 'j':
	    ViewState->objFontType = atoi(optarg);
	    break;

	case 'J':
	    ViewState->objFontName = strdup(optarg);
	    break;
	    
	    /* Specify the scribed-style draw color */
	case 'k':
	    if (sscanf(optarg, "%f,%f,%f,%f",
		       &ViewState->scribeColor[0],
		       &ViewState->scribeColor[1],
		       &ViewState->scribeColor[2],
		       &ViewState->scribeColor[3]) != 4 &&
		sscanf(optarg, "%f,%f,%f",
		       &ViewState->scribeColor[0],
		       &ViewState->scribeColor[1],
		       &ViewState->scribeColor[2]) != 3)
		usage(argv[0]);
	    break;
	    
	case 'K':
	    ViewState->combineBillboards = atoi(optarg);
	    break;

	    
	    /* remember light source definition */
	case 'l':
	    {
		float lxyz[3], lrgb[3], l;
		if (sscanf(optarg, "%f,%f,%f,%f,%f,%f", 
			   &lxyz[0], &lxyz[1], &lxyz[2], &lrgb[0], &lrgb[1], &lrgb[2]) == 6)
		{
		    if (ViewState->lamps < (MAX_LAMPS - 1))
		    {
			/* set default default values */
			pfSetVec3(ViewState->lampXYZ[ViewState->lamps], 0.0f,0.0f,0.0f);
			pfSetVec3(ViewState->lampRGB[ViewState->lamps], 1.0f,1.0f,1.0f);
			
			/* read specific values */
			sscanf(optarg, "%f,%f,%f,%f,%f,%f",
			       &ViewState->lampXYZ[ViewState->lamps][0],
			       &ViewState->lampXYZ[ViewState->lamps][1],
			       &ViewState->lampXYZ[ViewState->lamps][2],
			       &ViewState->lampRGB[ViewState->lamps][0],
			       &ViewState->lampRGB[ViewState->lamps][1], 
			       &ViewState->lampRGB[ViewState->lamps][2]);
			
			/* advance lamp-definition count */
			ViewState->lamps += 1;
		    }
		}
		else if (sscanf(optarg, "%f", &l) == 1)
		    ViewState->lighting = l;
		else
		    usage(argv[0]);
	    }
	    break;
	    
	    /* lock down processors for app, cull, and draw */
	case 'L':
	    ViewState->procLock = ((uint)~0);
	    break;
	    
	    /* Specify multiprocessing mode */
	case 'm':
	    ProcSplit = atoi(optarg);
	    break;
	    
	    /* Enable multipipe/hyperpipe mode */
	case 'M':
	    {
		char* arg = (char*)malloc(strlen(optarg)+1);
		char* s;
    	    	char* comma;
    	    	
		strcpy(arg, optarg);
    	    	comma =  strchr(arg, ',');

		s = strtok(arg, ",");
		if (s == NULL)
		    usage(argv[0]);
		if ((Multipipe = atoi(s)) < 0 || Multipipe > PANORAMIC) 
		    usage(argv[0]);

		if ( (Multipipe == COMPOSITOR) || (Multipipe == SOFTWARE_COMPOSITOR) )
		{
    	    	    int netId = -1;
    	    	    
    	    	    if (comma)
    	    	    	netId = atoi(comma+1);

    	    	    if ( (Multipipe == COMPOSITOR) && (pfGetNumHWCompositors()>0) )
    	    	    {
    	    	    	if (netId==-1)
    	    	    	    netId = pfGetHWCompositorNetworkId(0);

    	    	    	pfNotify (PFNFY_INFO, PFNFY_PRINT,
    	    	    	    "Using hw compositor on networkId %d", netId);

    	    	    	ViewState->compositor = pfNewCompositor(netId);
    	    	    }
    	    	    else
    	    	    {
    	    	    	pfNotify (PFNFY_INFO, PFNFY_PRINT,
    	    	    	    "Creating a software compositor");
    	    	    	ViewState->compositor = pfNewCompositor(PFCOMP_SOFTWARE);
    	    	    }

    	    	    ApplyCompositorParams( ViewState->compositor, netId );

    	    	    Multipipe = 0;
    	    	    NumChans = 1;
    	    	    NumPipes = 1;
     	    	}



		else if (Multipipe == 2)
		{
		    int ni = 0, si = 0;
		    while ((s = strtok(NULL, ",")) != NULL) {
			if (strncmp(s, "h=", 2) == 0) {
			    if (si != 0) {
				fprintf(stderr, "hyperpipes must preceed "
					"single pipes\n");
				usage(argv[0]);
			    }
			    if (ni >= MAX_HYPERPIPES) {
				fprintf(stderr, "too many hyperpipes "
					"(max=%d)\n", MAX_HYPERPIPES);
				usage(argv[0]);
			    }
			    HyperpipeNetIds[ni++] = atoi(&s[2]);
			} else if (strncmp(s, "s=", 2) == 0) {
			    if (si >= MAX_PIPES) {
				fprintf(stderr, "too many single pipes "
					"(max=%d)\n", MAX_PIPES);
				usage(argv[0]);
			    }
			    HyperpipeSingles[si++] = strdup(&s[2]);
			} else {
			    fprintf(stderr, "invalid hyperpipe network id or "
				    "display name specification \"%s\"\n",
				    optarg);
			    usage(argv[0]);
			}
		    }

		    if (ni > 0 || si > 0) {
			NumHyperpipes = ni;
			NumHyperpipeSingles = si;
		    }
		}


/* ////////////////////////////////////////////////////////////////////// */

		else if (Multipipe == POWERWALL)
		{
		    int commasfound=0;
		    char* lastcomma = optarg; 

		    /* NxM Multipipe Asynmmetric PowerWall configs.
		     * cmdline should provide at least: numcols, numrows, 
	     	     * hor-overlap, vert-overlap. After these four values,
		     * cmd option may include the list of ALL partecipating
		     * pipes (or compositors).
		     * For example:
		     *   -M6,2,1,0.0,0.0,p=:0.0,p=0.3
		     * describes a 2x1 powerwall config with no overlap,
		     * with pipe :0.0 opn the left and pipe :0.3 on the right.
		     *
	     	     */

		    PowerWall *pwall = &(ViewState->powerwall);

		    /* make sure the first char after -M6 is a comma */
		    if (optarg[1]!=',')
		    {
			pfNotify (PFNFY_WARN, PFNFY_PRINT, 
			    "PowerWall Configuration: invalid option string \"%s\"", optarg );
			usage(argv[0]);
		    }

		    /* read first four tokens in option string. These are number-of-cols,
			number-of-cols, x-overlap, y-overlap; These Must be there! */
            	    if(sscanf(optarg+2, "%d,%d,%f,%f", &(pwall->cols), 
			&(pwall->rows), &(pwall->xoverlap), 
			&(pwall->yoverlap)) != 4) 
		    {

		    	pfNotify (PFNFY_WARN, PFNFY_PRINT, 
			    "PowerWall Configuration: invalid string \"%s\".",
			    optarg );
		   	usage(argv[0]);
		    }

		    if (pwall->cols < 1)
		    {
		        pfNotify (PFNFY_WARN,PFNFY_PRINT, 
			    "PowerWall Configuration: invalid number of "
			    "columns %d (must be at least 1)", pwall->cols);
		    	usage(argv[0]);
		    }
		    if (pwall->rows < 1)
		    {
		        pfNotify (PFNFY_WARN,PFNFY_PRINT, 
			    "PowerWall Configuration: invalid number of "
			    "rows %d (must be at least 1)", pwall->rows);
		    	usage(argv[0]);
		    }

		    if (pwall->xoverlap > 0.5f)
		    {
		        pfNotify (PFNFY_WARN,PFNFY_PRINT, 
			    "PowerWall Configuration: invalid xoverlap %f"
			    "(must be between 0.0 and 0.5)", pwall->xoverlap);
		    	usage(argv[0]);
		    }
		    if (pwall->yoverlap > 0.5f)
		    {
		        pfNotify (PFNFY_WARN,PFNFY_PRINT, 
			    "PowerWall Configuration: invalid yoverlap %f"
			    "(must be between 0.0 and 0.5)", pwall->yoverlap);
		    	usage(argv[0]);
		    }

		    /* Check to see if option string also includes list
		       of partecipating pipes/compositors. 
		       So, look to see if option string contains a fourth
		       comma character.
		    */

		    pwall->namesProvided = 0;

		    for( commasfound=0; commasfound<5; commasfound++)
		    {
			char* p = strchr(lastcomma+1, ',');
			if (p==NULL)
			    break;
			lastcomma = p;
		    }	

		    if (commasfound==4)
		    {
			int i,n=pwall->cols*pwall->rows;
#ifndef WIN32
			Display *dsp = pfGetCurWSConnection();
			int numScreens = ScreenCount(dsp);
			if (n>numScreens)
			{
			    pfNotify( PFNFY_WARN, PFNFY_PRINT,
			    	"PowerWall Configuration: %d pipes "
				"requested, only %d available.", n, 
				numScreens);
			}
#endif
			for (i=0; i<n; i++)
			{
			    pwall->elt[i].type = 0; /*SINGLE_PIPE*/
			    pwall->elt[i].index = i;
			}
		    }
		    else if (commasfound==5)
		    {
			void* arena = pfGetSharedArena();
			int np /*si*/ = 0; /* num pipes found*/
			int nc /*ni*/ = 0;  /* num compositors found */

			char* s;
			int i = 0; /* total found (pipes + compositors) */
			int n = pwall->cols * pwall->rows; /* expected total */

			for (s=strtok(lastcomma+1, ","); s!=NULL && i<n; 
			    s=strtok(NULL, ","), i++ )
			{
			    if (strncmp(s, "c=", 2) == 0) 
			    {
				if (nc >= MAX_HYPERPIPES) 
				{
				    pfNotify(PFNFY_WARN, PFNFY_PRINT, 
				    	"Too many compositors (max=%d)", 
				    	MAX_HYPERPIPES);
				    usage(argv[0]);
				}
				
				HyperpipeNetIds[nc] = atoi(&s[2]);

				pwall->elt[i].type = 1; /*COMPOSITOR;*/
				pwall->elt[i].index = atoi(&s[2]);

				pwall->elt[i].compositor = pfNewCompositor(pwall->elt[i].index);

				ApplyCompositorParams(pwall->elt[i].compositor, 
				    pwall->elt[i].index);
				nc++;
			    } 
			    else if (strncmp(s, "p=", 2) == 0) 
			    {
				if (np >= MAX_PIPES) 
				{
				    pfNotify(PFNFY_WARN, PFNFY_PRINT, 
				    	"Too many single pipes (max=%d)", 
					MAX_PIPES);
				    usage(argv[0]);
				}

				HyperpipeSingles[np] = pfStrdup(&s[2], arena);

				pwall->elt[i].type = 0; /*SINGLE_PIPE*/
				pwall->elt[i].index = np;
				np++;
		    	    } 
			    else 
			    {
				pfNotify(PFNFY_WARN, PFNFY_PRINT, 
				    "Invalid compositor id or display name "
				    "specification \"%s\"\n", s);
				usage(argv[0]);
			    }
			}

			if (i!=n)
			{
			    pfNotify (PFNFY_WARN,PFNFY_PRINT, 
				"PowerWall Configuration: invalid option "
				"string \"%s\".", optarg );
			    usage(argv[0]);
			}

			pwall->namesProvided = 1;

			pfNotify (PFNFY_INFO, PFNFY_PRINT, 
			    "PowerWall Configuration: participating elements:");

			for (i=0; i<n; i++)
			{
			    pfNotify (PFNFY_INFO, PFNFY_MORE, "Element %d: "
				"type:%d index: %d (%s)", i, pwall->elt[i].type, 
				pwall->elt[i].index, pwall->elt[i].type ? "" 
				: HyperpipeSingles[pwall->elt[i].index ]);
			}

		    } /* end of if(commasfound==5)*/
		    else
		    { 
			pfNotify(PFNFY_INFO, PFNFY_PRINT,
			    "PowerWall Configuration: no pipe names "
			    "were provided.");
			pfNotify(PFNFY_DEBUG, PFNFY_PRINT,
			    "PowerWall Configuration: commasfound = %d in "
			    "\"%s\"", commasfound, optarg);
		    }
		} /* end of if (Multipipe == POWERWALL) */

/* ////////////////////////////////////////////////////////////////////// */

		else if (Multipipe == PANORAMIC)
		{
    	    	    int numElts = 0;
    	    	    int num_single_pipes = 0;

		    while ((s = strtok(NULL, ",")) != NULL) 
    	    	    {
    	    	    	PanoramicDisplayElt * elt = &(ViewState->panoramicDisplayElt[numElts]);

			if (numElts >= MAX_PIPES) 
    	    	    	{
			    pfNotify (PFNFY_WARN, PFNFY_PRINT, 
    	    	    	    	"too many elements in panoramic display mode "
				    "(max=%d)\n", MAX_PIPES);
			    usage(argv[0]);
			}

			if (strncmp(s, "p=", 2) == 0) 
    	    	    	{
    	    	    	    elt->type = SINGLE_PIPE_DISPLAY_ELT;
    	    	    	    elt->pipe_name = strdup (s+2);
    	    	    	    elt->id = num_single_pipes;
    	    	    	    num_single_pipes++;
			} 

			else if (strncmp(s, "c=", 2) == 0) 
    	    	    	{
	    	    	    elt->type = HW_COMPOSITOR_DISPLAY_ELT;
    	    	    	    elt->id = atoi (s+2);
    	    	    	    elt->compositor = pfNewCompositor (elt->id);
			    ApplyCompositorParams( elt->compositor, elt->id );
			}

    	    	    	else
        	    	{
    	    	    	    pfNotify (PFNFY_WARN, PFNFY_PRINT,
    	    	    	    	"Invalid -M6 parameters \"%s\"", s );
 			    usage(argv[0]);
			}
    	    	    	numElts++;
 		    }

    	    	    Multipipe = 1;
		    
		    if (numElts)
		    {
    	    	    	ViewState->numChans = NumChans = NumPipes = numElts;
    	    	    	ViewState->numPanoramicElts = numElts;
		    }
		}

		free(arg);
	    }
	    break;
	    
	    /* Set the notification (debug) level */
	case 'n':
	    NotifyLevel = atoi(optarg);
	    break;
	    
	    /* Assign non-degrading priorities to Performer processes. */
	case 'N':
	    PrioritizeProcs = 1;
	    break;
	    
        case 'o':
            {
                int mode;
                int value;
		int found;
		
                found = sscanf(optarg, "%d,%d", &mode, &value);
                if (found == 2)
		    pfdBldrMode(mode, value);
		else if (found == 1)
		    pfdBldrMode(mode, 0);
		else
		    usage(argv[0]);
            }
            break;
	    
	case 'O':
            {
                int mode;
                int value;
		int found;
		
                found = sscanf(optarg, "%d,%d", &mode, &value);
                if (found == 2)
		    pfdBldrMode(mode, value);
		else if (found == 1)
		    pfdBldrMode(mode, 1);
		else
		    usage(argv[0]);
            }
	    break;
	    
	    /* Set the initial view position (x y z)*/
	case 'p':
	    if (sscanf(optarg, "%f,%f,%f",
		       &ViewState->initViews[0].xyz[PF_X],
		       &ViewState->initViews[0].xyz[PF_Y],
		       &ViewState->initViews[0].xyz[PF_Z]) == 3)
		InitXYZ = TRUE;
	    else
		usage(argv[0]);
	    break;
	    
	    /* Set the frame phase */
	case 'P':
	    ViewState->phase = atoi(optarg);
	    break;
	    
	    /* Whether to optimize into a scene geostate */
	case 'q':
	    ViewState->optimizeGStates = atoi(optarg);
	    break;
	    
	    /* Scene cleaning */
	case 'Q':
	    ViewState->optimizeTree = atoi(optarg);
	    break;
	    
	    /* Set the frame rate */
	case 'r':
	    ViewState->frameRate = (float)atof(optarg);
	    if( ViewState->frameRate <= 0.0000001f)
		ViewState->frameRate = 30.0f;
	    break;
	    
	    /* Enable retessellation of input geometry */
	case 'R':
	    pfdBldrMode(PFDBLDR_MESH_RETESSELLATE, atoi(optarg));
	    break;
	    
	    /* set LOD scale factor */
	case 's':
	    ViewState->LODscale = atof(optarg);
	    break;
	    
	    /* Enable color-coding of individual triangle strips */
	case 'S':
	    pfdBldrMode(PFDBLDR_MESH_SHOW_TSTRIPS, atoi(optarg));
	    break;

	    /* Visual ids for up to 8 pipes, hex (start with 0x) or decimal */
	case 't':
	    {
	        int i, nscanned = sscanf(optarg,"%i,%i,%i,%i,%i,%i,%i,%i",
					 &ViewState->visualIDs[0],
					 &ViewState->visualIDs[1],
					 &ViewState->visualIDs[2],
					 &ViewState->visualIDs[3],
					 &ViewState->visualIDs[4],
					 &ViewState->visualIDs[5],
					 &ViewState->visualIDs[6],
					 &ViewState->visualIDs[7]);
		if (nscanned < 1)
		{
		    nscanned = 1;
		    ViewState->visualIDs[0] = -1;
		}
		/* For all remaining pipes, use the last number specified */
		for (i = nscanned; i < MAX_PIPES; ++i)
		    ViewState->visualIDs[i] = ViewState->visualIDs[nscanned-1];
	    }
	    break;

	/* set display-list mode */
	case 'T':
	    if (atoi(optarg))
		ViewState->drawMode = DLIST_GEOSET;
	    else
		ViewState->drawMode = DLIST_OFF;
	    break;

        /* inflate files */
	case 'u':
	    ViewState->explode = -1;
	    break;

	/* start calligraphics */

	case 'U':
	    ViewState->startCallig = 1;
	    break;
	
	    /* set pfGeoSet packed attribute format for vertex arrays */
	case 'v':
	    PackedAttrFormat = atoi(optarg);
	    break;
	    /* change DVR mode */
	case 'V':
	    if (sscanf(optarg, "%d,%d,%d", &ViewState->doDVR, &ViewState->dvrXSize, &ViewState->dvrYSize) != 3)
		ViewState->doDVR = atoi(optarg);
	    break;
	    /* Write scene to a file */
	case 'w':
	    if (optarg[0] == '-')
		usage(argv[0]);
	    WriteFileDbg = strdup(optarg);
	    break;
	    
	    /* Set the window size */
	case 'W':
	    if (sscanf(optarg, "%d,%d", &WinSizeX, &WinSizeY) != 2)
		if (sscanf(optarg, "%d", &WinSizeX) == 1)
		    WinSizeY = WinSizeX;
		else
		    usage(argv[0]);
	    break;
	    
	case 'x':
	    {
		int i = atoi(optarg);
		switch(i)
		{ 
		case 0: 
		    ViewState->input = PFUINPUT_NOFORK_X; 
		    MultiPipeInput = 0;
		    break;
		case 1:
		    ViewState->input = PFUINPUT_X;
		    MultiPipeInput = 0;
		    break;
		case 2:
		    ViewState->input = PFUINPUT_X;
		    MultiPipeInput = 1;
		    break;
		}
	    }
	    break;
	    
	    /* explode files */
	case 'X':
	    ViewState->explode = (float)atof(optarg);
	    break;
	    /* Set specific loader mode */
        case 'y':
            {
                int mode;
                float val;
                char buf[64], *c;
		if ((c=strchr(optarg, ',')) != NULL) *c = ' ';
	    	if (sscanf(optarg,"%s %d,%f",buf,&mode,&val) == 3)
		    pfdConverterMode(buf,mode,val);
		else 
		    usage(argv[0]);
	    }
	    break;
	    /* Add an file extension alias */
	case 'Y':
	    {
		char buf[64],buf2[64], *c;
		
		if ((c=strchr(optarg, ',')) != NULL) *c = ' ';
	    	if (sscanf(optarg,"%s %s",buf,buf2) == 2) 
		    pfdAddExtAlias(buf,buf2);
		else
		    usage(argv[0]);
	    }
	    break;

	case 'z':
	    {
		float nearPlane, farPlane;
		if (sscanf(optarg, "%f,%f", &nearPlane, &farPlane) == 2)
		{
		    ViewState->nearPlane = nearPlane;
		    ViewState->farPlane = farPlane;
		    ViewState->nearFogRange = ViewState->nearPlane;
		    ViewState->farFogRange = ViewState->farPlane;
		}
		else
		    usage(argv[0]);
	    }
	    
	    break;
	    
	case 'Z':
	    FreeInitCPUs = 0;
	    break;
	    
	case '1':
	    strcpy(ViewState->demoPathFileName, optarg);
	    ViewState->doDemoPath = 1;
	    break;

	case '2':
	    ViewState->xformerModel = PFITDF_TETHER;
	    break;
	case '3':
	    ZBits = 23;
	    break;
	case '4':
	    Multisamples = 4;
	    break;
	case '7':
	    ViewState->xformerModel = PFITDF_TRACKBALL;
	    break;

        case '5':
            if(sscanf(optarg, "%f,%f,%f",
                      &ViewState->fov[0],
                      &ViewState->fov[1],
                      &ViewState->fov[3]) == 3)
            {
                ViewState->fov[2] = 0.0;
                InitFOVasym = TRUE; 
            }
            else
                usage(argv[0]);
            break;

	case '9':
	  ChooseShaderVisual = 1;
	  break;

	case '?':
	case 'h':
	default:
	    usage(argv[0]);
	}
    }

    /* preprocess results from cmd line */
    /************************************/

    /* force lpoint process if cmdline ask for Calligraphics (-U) */
    if (ViewState->startCallig)
	if (ProcSplit == PFMP_DEFAULT)
	    	ProcSplit = PFMP_FORK_LPOINT | PFMP_FORK_CULL | PFMP_FORK_DRAW;
	else
	    ProcSplit |= PFMP_FORK_LPOINT;

    /* prepare returned results */
    /****************************/

    /* return address of database filename list through "files" argument */
    *files = &(argv[optind]);

    /* function return value is number of database files to be read */
    returnval = argc - optind;

    /* restore original global vars for re-entrancy */
    optind = saved_optind;
    optarg = saved_optarg;

    return returnval;
}
