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
 *   g++ -o assemble assemble.C -lGL 
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
    if (argc<5)
    {
	fprintf(stderr,"Usage - %s nx ny outimage imgfiles ...\n", argv[0]);
	exit(1);
    }

    // first two arguments are number of rows and number of columns
    int nx = atoi(argv[1]);
    int ny = atoi(argv[2]);

    // make sure they are positive integers
    if ( (nx<1) || (ny<1) )
    {
	 fprintf(stderr,"%s - invalid nx ny values\n", argv[0]);
	 fprintf(stderr,"Usage - %s nx ny outimage imgfiles ...\n", argv[0]);
	 exit(1);
    }

    int numTiles = nx*ny;

    // for the moment, all nx*ny file names must be provided
    if (argc < (numTiles+4))
    {
	fprintf(stderr,"%s - missing imgfiles in cmdline (expected %d, "
	    "found %d)\n", argv[0], numTiles, argc-4);
	fprintf(stderr,"Usage - %s nx ny outimage imgfiles ...\n", argv[0]);
	exit(1);
    }

    // store a ptr to 1st argument specifying a tile filename
    char** tileName = argv + 4;

    // open first image to query format and size, then close it
    IMAGE *img = iopen( tileName[0], "r");
    if (img == NULL) 
    {
	fprintf(stderr,"%s: can't open image file %s\n", argv[0], tileName[0]);
        exit(1);
    }

    int tileW = img->xsize;
    int tileH = img->ysize;
    int tileZ = img->zsize;
    int numChans = img->dim;

    iclose(img);

    // we will be opening one row of tile images at the time..

    // allocate an array of nx IMAGE pointers
    IMAGE** tile = (IMAGE**)calloc(nx, sizeof(IMAGE*));
    assert(tile);

    // also open a new image for writing
    IMAGE *outImage = iopen(argv[3], "w", 1, 
        numChans, tileW*nx, tileH*ny, tileZ);

    if (outImage == NULL) 
    {
	fprintf(stderr,"%s: can't open output file %s\n", argv[0], argv[2]);
	exit(1);
    }

    // allocate a buffer large enough to hold a whole row of output image
    // (one channel only)
    unsigned short * rowbuf = (unsigned short*) 
             calloc (sizeof(unsigned short), tileW*nx );
    assert(rowbuf);

    // now process one row of nx tiles at the time.
    int tileX, tileY;
    for (tileY=0; tileY<ny; tileY++)
    {    
	// first open all tile images for this row
	for (tileX=0; tileX<nx; tileX++)
	{
	    int firstTile = nx*tileY;
	    tile[tileX] = iopen( tileName[firstTile+tileX], "r" );    
	    if (tile[tileX]==NULL)
	    {
		fprintf(stderr,"%s: can't open image file %s\n", 
		    argv[0], tileName[firstTile+tileX]);
		exit(1);
	    }
	}

	// now process one row at the time, reading from all open tiles
	// then writing to output image

	int firstRow = tileH*tileY;	
	int c; // channel index
	for (c=0; c<numChans; c++)
	{
	    int r; // row index
	    for (r=0; r<tileH; r++)
	    {
		for (tileX=0; tileX<nx; tileX++)
		    getrow (tile[tileX], rowbuf+ (tileX*tileW), r, c); 

		putrow (outImage, rowbuf, firstRow +r, c);
	    }
	}

	// now close all open image tiles
	for (tileX=0; tileX<nx; tileX++)
	    iclose(tile[tileX]);
    }
      
    iclose(outImage);
    
    return 0;
}

////////////////////////////////////////////////////////////////////////////////
