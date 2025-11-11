/*
 * Copyright 2005, Silicon Graphics, Inc.
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
////////////////////////////////////////////////////////////////////////////////

/*
 *   On linux, compile with:
 *   g++ -o izoom izoom.C -lpf -lGL -I/usr/include/Performer/
 *
 */

////////////////////////////////////////////////////////////////////////////////

#include <stdio.h>
#include <assert.h>

#include <Performer/pf.h>
#include <Performer/pr/pfTexture.h>
#include <Performer/pf/pfPipeWindow.h>
#include <GL/glu.h>

#if defined(WIN32)
#include <image.h>
#elif defined(__linux__)
#include <Performer/image.h>
#else
#include <gl/image.h>
#endif

////////////////////////////////////////////////////////////////////////////////

char* progname;
char* infilename;
char* outfilename;
float scaleX;
float scaleY;

////////////////////////////////////////////////////////////////////////////////

void
usage(char* progname)
{
    printf ("Usage: %s infile outfile scaleX scaleY\n", progname);
    exit(1);
}

////////////////////////////////////////////////////////////////////////////////

void
clear_pack_params(void)
{
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glPixelStorei(GL_PACK_LSB_FIRST, 0);
    glPixelStorei(GL_PACK_ROW_LENGTH, 0);
    glPixelStorei(GL_PACK_SKIP_PIXELS, 0);
    glPixelStorei(GL_PACK_SKIP_ROWS, 0);
    glPixelStorei(GL_PACK_SWAP_BYTES, 0);

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_UNPACK_LSB_FIRST, 0);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
    glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
    glPixelStorei(GL_UNPACK_SWAP_BYTES, 0);
}

////////////////////////////////////////////////////////////////////////////////

void
OpenPipeWin(pfPipeWindow *pw)
{
    pw->open();
    clear_pack_params();

    pfTexture* intex = new pfTexture;
    if (!intex->loadFile(infilename))
    {
        printf ("%s: failed to open infile %s\n",
	    progname, infilename);
	pfExit();
    }

    uint *img;
    int nc, sx, sy, sz;
    intex->getImage(&img, &nc, &sx, &sy, &sz);

    if( sz!=1 )
    {
        printf ("%s: unspupported z-size %d in file %s\n",
	    progname, sz, infilename);
	pfExit();
    }

    GLenum imgformat = intex->getFormat( PFTEX_IMAGE_FORMAT );
    if (imgformat==PFTEX_DEFAULT)
        imgformat = GL_RGB;

    GLenum extformat = intex->getFormat( PFTEX_EXTERNAL_FORMAT );
    
    int scaledW = (int)( (float)sx * scaleX);
    int scaledH = (int)( (float)sy * scaleY);

    uint *scaledImg = (uint*)pfCalloc(
        nc*scaledW*scaledH, 4, pfGetSharedArena());

    /*
    printf ("\n\nCalling gluScaleImage:  \n\n");
    printf ("num comps     = %d\n", nc );
    printf ("img format    = 0x%x\n", imgformat );
    printf ("ext format    = 0x%x\n", extformat );
    printf ("original size = %d x %d\n", sx, sy );
    printf ("scaled size   = %d x %d\n", scaledW, scaledH );
    */
    
    if (gluScaleImage( imgformat, 
    		       sx, sy, 
		       extformat, 
		       img,
		       scaledW, scaledH, 
		       extformat,
		       scaledImg) )
    {
        printf ("%s: gluScaleImage failed for image %s\n", 
	    progname, infilename);
	pfExit();
    }

    pfTexture* outtex = new pfTexture;
    outtex->setFormat (PFTEX_IMAGE_FORMAT, imgformat);
    outtex->setFormat (PFTEX_EXTERNAL_FORMAT, extformat);
    outtex->setImage( scaledImg, nc, scaledW, scaledH, 1);
    outtex->saveFile(outfilename);
}

////////////////////////////////////////////////////////////////////////////////

int
izoom_using_glu_scale_image (int argc, char** argv)
{
    infilename = argv[1];
    outfilename = argv[2];
    scaleX = atof (argv[3]);      
    scaleY = atof (argv[4]);      
    
    pfInit();
    pfMultiprocess(PFMP_APPCULLDRAW);
    pfConfig();
    
    pfPipe* p = pfGetPipe(0);
    pfPipeWindow* pw = new pfPipeWindow(p);
    pw->setName(argv[0]);
    pw->setOriginSize(0,0,500,500);
    pw->setConfigFunc(OpenPipeWin);
    pw->config();

    pfFrame();    
    pfFrame();    

    pfExit();
}

////////////////////////////////////////////////////////////////////////////////

int
main (int argc, char** argv)
{
    progname = argv[0];
    
    if (argc!=5)
        usage(progname);

    if ( (atof(argv[3]) != 0.5f) || (atof(argv[4]) != 0.5f))
    {
        return izoom_using_glu_scale_image (argc, argv);
    }

    IMAGE* inimage = iopen (argv[1], "r");
    if (inimage==NULL)
    {
	fprintf(stderr,"%s: can't open input image file %s\n", argv[0], argv[1]);
	exit(1);
    }

    int imgW = inimage->xsize;
    int imgH = inimage->ysize;
    int imgZ = inimage->zsize;
    int numChans = inimage->dim;

    if (imgW & 1)
    {
	fprintf (stderr,
	    "%s: width of image %s (%d pixels) is not divisible by 2\n", 
		argv[0], argv[1], imgW);
	exit(1);
    }

    if (imgH & 1)
    {
	fprintf (stderr,
	    "%s: height of image %s (%d pixels) is not divisible by 2\n", 
		argv[0], argv[1], imgH );
	exit(1);
    }

    int subW = imgW/2;
    int subH = imgH/2;

    // also open a new image for writing
    IMAGE *outimage = iopen(argv[2], "w", 1, numChans, subW, subH, imgZ);

    if (outimage == NULL) 
    {
	fprintf(stderr,"%s: can't open output file %s\n", argv[0], argv[2]);
	exit(1);
    }

    // allocate a buffer large enough to hold a single row of output image
    // (one channel only)
    unsigned short * outbuf = (unsigned short*) calloc (sizeof(unsigned short), subW);
    assert(outbuf);

    // allocate two buffers large enough to hold a single row of input image
    // (one channel only)
    unsigned short * inbuf1 = (unsigned short*) 
                                  calloc (sizeof(unsigned short), imgW);
    assert(inbuf1);
    unsigned short * inbuf2 = (unsigned short*) 
                                  calloc (sizeof(unsigned short), imgW);
    assert(inbuf2);

    int c; // chan index
    for (c=0; c<imgZ; c++)
    {
	int r; // row index
	for (r=0; r<subH; r++)
	{
	    getrow (inimage, inbuf1, r*2, c);
	    getrow (inimage, inbuf2, r*2+1, c);

	    for (int x=0; x<subW; x++)
	    {
		unsigned long val = 
		    inbuf1[x*2] + inbuf1[x*2+1] + inbuf2[x*2] + inbuf2[x*2+1];
		outbuf[x] = (unsigned short)(val >> 2);
	    }

	    putrow (outimage, outbuf, r, c);
	}
    }

    iclose(inimage);
    iclose(outimage);
    return 0;
}

////////////////////////////////////////////////////////////////////////////////
