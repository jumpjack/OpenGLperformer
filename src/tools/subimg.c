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
 *   g++ -o subimg subimg.C -lpf -lGL -I/usr/include/Performer/
 *
 */

////////////////////////////////////////////////////////////////////////////////

#include <stdlib.h>
#include <stdio.h>
#include <assert.h>

#if defined(WIN32)
#include <image.h>
#elif defined(__linux__)
#include <Performer/image.h>
#else
#include <gl/image.h>
#endif

////////////////////////////////////////////////////////////////////////////////

int
main (int argc, char** argv)
{
    if (argc!=7)
    {
	fprintf(stderr,"Usage - %s inimage outimage x1 x2 y1 y2 ...\n", 
	    argv[0]);
	exit(1);
    }

    IMAGE* inimage = iopen (argv[1], "r");
    if (inimage==NULL)
    {
	fprintf(stderr,"%s: can't open input image file %s\n", 
	    argv[0], argv[1]);
	exit(1);
    }

    int imgW = inimage->xsize;
    int imgH = inimage->ysize;
    int imgZ = inimage->zsize;
    int numChans = inimage->dim;

    // first two arguments are number of rows and number of columns
    int left = atoi(argv[3]);
    if (left < 0)
	left += imgW;

    if ( (left < 0) || (left >= imgW) )
    {
	fprintf(stderr,"%s: invalid x1 param %s\n", argv[0], argv[3]);
	exit(1);
    }


    int right = atoi(argv[4]);
    if (right < 0)
	right += imgW;

    if ( (right < 0) || (right >= imgW) )
    {
	fprintf(stderr,"%s: invalid x2 param %s\n", argv[0], argv[4]);
	exit(1);
    }


    int bottom = atoi(argv[5]);
    if (bottom < 0)
	bottom += imgH;

    if ( (bottom < 0) || (bottom >= imgH) )
    {
	fprintf(stderr,"%s: invalid y1 param %s\n", argv[0], argv[5]);
	exit(1);
    }


    int top = atoi(argv[6]);
    if (top < 0)
	top += imgH;

    if ( (top < 0) || (top >= imgH) )
    {
	fprintf(stderr,"%s: invalid y2 param %s\n", argv[0], argv[6]);
	exit(1);
    }

    if (left > right)
    {
	fprintf(stderr,"%s: x1 cannot be greater than x2\n", argv[0]);
	exit(1);
    }

    if (bottom > top)
    {
	fprintf(stderr,"%s: y1 cannot be greater than y2\n", argv[0]);
	exit(1);
    }

    int subW = right - left +1;
    int subH = top - bottom +1;


    // also open a new image for writing
    IMAGE *outimage = iopen(argv[2], "w", 1, numChans, subW, subH, imgZ);

    if (outimage == NULL) 
    {
	fprintf(stderr,"%s: can't open output file %s\n", argv[0], argv[2]);
	exit(1);
    }

    // allocate a buffer large enough to hold a whole row of input image
    // (one channel only)
    unsigned short * rowbuf = (unsigned short*)
                                 calloc (sizeof(unsigned short), imgW);
    assert(rowbuf);

    int c; // chan index
    for (c=0; c<imgZ; c++)
    {
	int r; // row index
	for (r=0; r<subH; r++)
	{
	    getrow (inimage, rowbuf, r+bottom, c);
	    putrow (outimage, rowbuf+left, r, c);
	}
    }

    iclose(inimage);
    iclose(outimage);
    return 0;
}

////////////////////////////////////////////////////////////////////////////////
