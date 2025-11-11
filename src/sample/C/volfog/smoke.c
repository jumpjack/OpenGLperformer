/*
 * Copyright 1999, 2000, Silicon Graphics, Inc.
 * ALL RIGHTS RESERVED
 *
 * UNPUBLISHED -- Rights reserved under the copyright laws of the United
 * States.   Use of a copyright notice is precautionary only and does not
 * imply publication or disclosure.
 *
 * U.S. GOVERNMENT RESTRICTED RIGHTS LEGEND:
 * Use, duplication or disclosure by the Government is subject to restrictions
 * as set forth in FAR 52.227.19(c)(2) or subparagraph (c)(1)(ii) of the Rights
 * in Technical Data and Computer Software clause at DFARS 252.227-7013 and/or
 * in similar or successor clauses in the FAR, or the DOD or NASA FAR
 * Supplement.  Contractor/manufacturer is Silicon Graphics, Inc.,
 * 2011 N. Shoreline Blvd. Mountain View, CA 94039-7311.
 *
 * Permission to use, copy, modify, distribute, and sell this software
 * and its documentation for any purpose is hereby granted without
 * fee, provided that (i) the above copyright notices and this
 * permission notice appear in all copies of the software and related
 * documentation, and (ii) the name of Silicon Graphics may not be
 * used in any advertising or publicity relating to the software
 * without the specific, prior written permission of Silicon Graphics.
 *
 * THE SOFTWARE IS PROVIDED "AS-IS" AND WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS, IMPLIED OR OTHERWISE, INCLUDING WITHOUT LIMITATION, ANY
 * WARRANTY OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
 *
 * IN NO EVENT SHALL SILICON GRAPHICS BE LIABLE FOR ANY SPECIAL,
 * INCIDENTAL, INDIRECT OR CONSEQUENTIAL DAMAGES OF ANY KIND, OR ANY
 * DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS,
 * WHETHER OR NOT ADVISED OF THE POSSIBILITY OF DAMAGE, AND ON ANY
 * THEORY OF LIABILITY, ARISING OUT OF OR IN CONNECTION WITH THE USE
 * OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <Performer/pf.h>
#include <Performer/pr.h>
#include <Performer/pfutil.h>


#define SPOKES     12       /* number of vertices around a smoke column */
#define MAX_RINGS  500       /* max number of tri strips in a smoke column */

static pfGeoState *gstate;  /* common for all smoke geosets */

static pfGeoSet *fluxedGset;  /* smoke column */
static pfGeoSet *fluxedGset2; /* top and bottom polygon of the column */
static pfGeode *geode0;

static int *lengths;
static int *lengths2;
static pfVec3 *speeds;
static pfVec3 *smokeCoords;
static unsigned short *vertIndex;
static int numRings;
static int firstRing = 0;

static float smokePos[3] = {0,0,0};
static float smokeSpeed[3] = {0,0,10};
static float windSpeed[3] = {0.01,0.01,0};
static float speedVariation[3] = {0.002,0};
static float ringRadius = 50;
static float ringThickness = 3;
static float height = 120;
static float height2 = 142; /* this parameter need some tweaking based on
			       based on how fast the top of the smoke column
			       tapers */

static float initialRing[SPOKES][2]; /* usually a circle */

/************************************************************************/
static float invrand = 1.0/(float)(RAND_MAX);

static float rand3(float range, float ratio)
{
    return -range * ratio + (1-ratio)*(float)rand() * invrand * range;
}


/************************************************************************/
void updateSpeed(pfVec3 *speed, int i, int j) 
{
  (*speed)[0] = 
    smokeSpeed[0] + (numRings-i)*10*windSpeed[0] + 
    initialRing[j][0]*0.025 + rand3(0.1*initialRing[j][0], 0.35);
  (*speed)[1] = 
    smokeSpeed[1] + (numRings-i)*10*windSpeed[1] + 
      initialRing[j][1]*0.025 + rand3(0.1*initialRing[j][1], 0.35);
  (*speed)[2] = 
    smokeSpeed[2] + (numRings-i)*10*windSpeed[2] + 
      rand3(speedVariation[1],0.5);
}

void updateSpeedOld(pfVec3 *speed, int j) 
{
  (*speed)[0] += speedVariation[0]*initialRing[j][0]*0.2 + 
      rand3(speedVariation[0]*initialRing[j][0], 0.35) + windSpeed[0];
  (*speed)[1] += speedVariation[0]*initialRing[j][1]*0.2 + 
      rand3(speedVariation[0]*initialRing[j][1], 0.35) + windSpeed[1];
  (*speed)[2] += rand3(speedVariation[1],0.5) + windSpeed[2];
}

/************************************************************************/
void UpdateSmoke(void)
{
    int i, j, newRing;
    pfGeoSet *gset, *gset2;
    pfVec3 *coords, *coords2, *coordsPtr, *speedPtr, *ptr;
    unsigned short *dummyList;
    static int frame_count = 0;

    frame_count ++;

    gset = (pfGeoSet *)  pfGetFluxWritableData((pfFlux *)fluxedGset);
    gset2 = (pfGeoSet *)  pfGetFluxWritableData((pfFlux *)fluxedGset2);

    pfGetGSetAttrLists(gset,PFGS_COORD3, (void **)&coords, &dummyList);
    ptr = coords;
    pfGetGSetAttrLists(gset2,PFGS_COORD3, (void **)&coords2, &dummyList);

    newRing = 0;

    /* check whether the top ring is above the top height, if so remove it */
    coordsPtr = smokeCoords + firstRing*SPOKES;

    for(j=0; j<SPOKES; j++) {
	if( (*(coordsPtr+j))[2] > height2 && numRings>1 ) {
	    firstRing = (firstRing + 1) % MAX_RINGS;
	    numRings--;
	    pfGSetNumPrims(gset, numRings);
	    break;
	}
    }
    /* compute the new coordinates of all the rings, except the one on the  */
    /* ground */
    coordsPtr = smokeCoords + firstRing*SPOKES;
    speedPtr = speeds + firstRing*SPOKES;

    for(i=0; i<numRings; i++) {
	for(j=0; j<SPOKES; j++) {
	  /* compute new coords */

	    (*coordsPtr)[0] += (*speedPtr)[0];
	    (*coordsPtr)[1] += (*speedPtr)[1];
	    (*coordsPtr)[2] += (*speedPtr)[2];

	    if( *(coordsPtr)[2] > height) {
  	      /*PFGET_VEC3( *speedPtr, &sx, &sy, sz );*/
		(*speedPtr)[0] -= 0.01 * initialRing[j][0] + 
		  rand3(speedVariation[0]* 10 *initialRing[j][0], 0.25) +
		  windSpeed[0];
		(*speedPtr)[0] -= 0.01 * initialRing[j][1] + 
		  rand3(speedVariation[0]* 10 *initialRing[j][1], 0.25) +
		  windSpeed[1];
		(*speedPtr)[0] += rand3(speedVariation[1],0.5)+windSpeed[2];
	      /*PFSET_VEC3( speedPtr, sx, sy, sz );*/
	    }
	    else {
		updateSpeed(speedPtr, i, j);
	    }

	    if(i==numRings-1 && (*coordsPtr)[2] > smokePos[2]+ringThickness)
		newRing = 1;
	    
	    (*coords)[0] = (*coordsPtr)[0];
	    (*coords)[1] = (*coordsPtr)[1];
	    (*coords)[2] = (*coordsPtr)[2];

	    coords++;
	    coordsPtr++;
	    speedPtr++;
	}

	if(i+firstRing == MAX_RINGS-1) {
	    coordsPtr = smokeCoords;
	    speedPtr = speeds;
	}

    }

    if(newRing) {
      /* start moving up the ring on the ground */
	for(j=0; j<SPOKES; j++) {
	  /* compute new coords */
	    (*coordsPtr)[0] += (*speedPtr)[0];
	    (*coordsPtr)[1] += (*speedPtr)[1];
	    (*coordsPtr)[2] += (*speedPtr)[2];

	    (*speedPtr)[0] += smokeSpeed[0];
	    (*speedPtr)[1] += smokeSpeed[1];
	    (*speedPtr)[2] += smokeSpeed[2];

	    updateSpeed(speedPtr, i, j);

	    (*coords)[0] = (*coordsPtr)[0];
	    (*coords)[1] = (*coordsPtr)[1];
	    (*coords)[2] = (*coordsPtr)[2];

	      coords++;
	      coordsPtr++;
	    speedPtr++;
	}

	if(i + firstRing == MAX_RINGS-1) {
	    coordsPtr = smokeCoords;
	    speedPtr = speeds;
	}
	
	if(numRings < MAX_RINGS-1) {
	    numRings++;
	    pfGSetNumPrims(gset, numRings);
	
	    /* add new ground ring */
	    for(j=0; j<SPOKES; j++) {
	   
	      (*coordsPtr)[0] = initialRing[j][0]*(1 + rand3(0.2,0.5)) + 
		smokePos[0];
	      (*coordsPtr)[1] = initialRing[j][1]*(1 + rand3(0.2,0.5)) + 
		smokePos[1];
	      (*coordsPtr)[2] = smokePos[2];
		
	      (*speedPtr)[0] = rand3(speedVariation[0]*initialRing[j][0],0.5);
	      (*speedPtr)[1] = rand3(speedVariation[0]*initialRing[j][1],0.5);
	      (*speedPtr)[2] = 0;
		
	      coords2[SPOKES-1-j][0] = (*coordsPtr)[0];
	      coords2[SPOKES-1-j][1] = (*coordsPtr)[1];
	      coords2[SPOKES-1-j][2] = (*coordsPtr)[2];

	    (*coords)[0] = (*coordsPtr)[0];
	    (*coords)[1] = (*coordsPtr)[1];
	    (*coords)[2] = (*coordsPtr)[2];

	      coords++;
	      coordsPtr++;
	      speedPtr++;
	    }
	    coords2 += SPOKES;
	}
	else {
	    fprintf(stderr,"Too many smoke rings. Increase ring thickness or "
		    "reduce the smoke height.\n");
	}
    }
    else {
	for(j=0; j<SPOKES; j++) {
	  /* compute new coords for the ground ring */
	  (*coordsPtr)[0]   += (*speedPtr)[0];
	  (*coordsPtr)[1]   += (*speedPtr)[1];
	
	  (*speedPtr)[0]   += rand3(speedVariation[0]*initialRing[j][0],0.5);
	  (*speedPtr)[1]   += rand3(speedVariation[1]*initialRing[j][1],0.5);


	    (coords2[SPOKES-1-j])[0] = (*coordsPtr)[0];
	    (coords2[SPOKES-1-j])[1] = (*coordsPtr)[1];
	    (coords2[SPOKES-1-j])[2] = (*coordsPtr)[2];

	    (*coords)[0] = (*coordsPtr)[0];
	    (*coords)[1] = (*coordsPtr)[1];
	    (*coords)[2] = (*coordsPtr)[2];

	    speedPtr++;
	}
	coords2 += SPOKES;
    }

#if 0
    /* copy the coordinates  */
    for(i=0; i<=numRings; i++)
	for(j=0; j<SPOKES; j++)
	    *coords++ = *--coordsPtr;
#endif

    for(j=0; j<SPOKES; j++) {
      /* ring 0 */
      (*coords2)[0] = (*ptr)[0];
      (*coords2)[1] = (*ptr)[1];
      (*coords2)[2] = (*ptr)[2];
      coords2++;
      ptr++;
    }
    pfGSetBBox( gset2, NULL, PFBOUND_DYNAMIC);

    pfGSetBBox( gset, NULL, PFBOUND_DYNAMIC);
    pfNodeBSphere( geode0, NULL, PFBOUND_DYNAMIC);

    pfFluxWriteComplete((pfFlux *)fluxedGset);
    pfFluxWriteComplete((pfFlux *)fluxedGset2);
}

/************************************************************************/
static int makeFluxedGset(pfFluxMemory *fmem)
{
    int j;
    pfGeoSet   *gset;
    pfVec3 *coords;

    if (fmem == NULL)
	return pfFluxedGSetInit(fmem);

    pfFluxedGSetInit(fmem);

    gset = (pfGeoSet*)pfGetData(fmem);

    coords = (pfVec3*) pfCalloc( SPOKES*MAX_RINGS, sizeof(pfVec3),
				 pfGetSharedArena() );

    for(j=0; j<SPOKES*2; j++) {
	(coords[j])[0] = smokeCoords[j][0];
	(coords[j])[1] = smokeCoords[j][1];
	(coords[j])[2] = smokeCoords[j][2];
    }

    pfGSetAttr(gset,PFGS_COORD3, PFGS_PER_VERTEX, coords, 0);
    pfGSetPrimLengths(gset,lengths);
    pfGSetPrimType(gset,PFGS_TRISTRIPS);
    pfGSetNumPrims(gset,1);
    pfGSetGState(gset,gstate);

    return 0;
}


/************************************************************************/
static int makeFluxedGset2(pfFluxMemory *fmem)
{
    int j;
    pfGeoSet   *gset;
    pfVec3 *coords;

    if (fmem == NULL)
	return pfFluxedGSetInit(fmem);

    pfFluxedGSetInit(fmem);

    gset = (pfGeoSet*)pfGetData(fmem);

    coords = (pfVec3*) pfCalloc(SPOKES*2,sizeof(pfVec3),pfGetSharedArena() );

    for(j=0; j< SPOKES*2; j++) {
	(coords[j])[0] = smokeCoords[j][0];
	(coords[j])[1] = smokeCoords[j][1];
	(coords[j])[2] = smokeCoords[j][2];
    }

    pfGSetAttr(gset,PFGS_COORD3, PFGS_PER_VERTEX, coords, 0);
    pfGSetPrimLengths(gset,lengths2);
    pfGSetPrimType(gset,PFGS_POLYS);
    pfGSetNumPrims(gset,2);
    pfGSetGState(gset,gstate);

    return 0;
}


/************************************************************************/
pfNode *MakeSmoke(float *pos, float radius, float thickness,
		  float *speed, float *wind)
{
    int i,j, index;
    unsigned short *ptr;
    float alpha;
    pfGroup *group = NULL;

    if(pos) {
	smokePos[0] = pos[0];
	smokePos[1] = pos[1];
	smokePos[2] = pos[2];
    }
    
    if(speed) {
	smokeSpeed[0] = speed[0];
	smokeSpeed[1] = speed[1];
	smokeSpeed[2] = speed[2];
    }

    if(wind) {
	windSpeed[0] = wind[0];
	windSpeed[1] = wind[1];
	windSpeed[2] = wind[2];
    }

    lengths = (int *) pfCalloc(MAX_RINGS,sizeof(int),pfGetSharedArena());
    for(i=0; i<MAX_RINGS; i++)
	lengths[i] = 2*(SPOKES+1);

    lengths2 = (int *) pfCalloc(2,sizeof(int),pfGetSharedArena());
    lengths2[0] = lengths2[1]= SPOKES;

    vertIndex =   pfCalloc(2*(SPOKES+1)*MAX_RINGS,
			   sizeof(unsigned short),  pfGetSharedArena());
    
    index = 0;
    ptr = vertIndex;

    for(i=0; i<MAX_RINGS; i++) {
	for(j=0; j<SPOKES; j++) {
	    *ptr++ = index;
	    if(i<MAX_RINGS-1)
		*ptr++ = index+SPOKES;
	    else
		*ptr++ = j;
	    index++;
	}
	*ptr = ptr[-SPOKES*2];
	ptr++;
	*ptr = ptr[-SPOKES*2];
	ptr++;
    }
    
    smokeCoords = (pfVec3*) pfCalloc(SPOKES*MAX_RINGS,sizeof(pfVec3),pfGetSharedArena() );
    speeds = (pfVec3*) pfCalloc(SPOKES*MAX_RINGS,sizeof(pfVec3),pfGetSharedArena() );
    numRings = 1;

    ringRadius = radius;
    ringThickness = thickness;

    /* middle of the base, no speed */
    pfSetVec3(smokeCoords[0], smokePos[0], smokePos[1], smokePos[2]);

    /* create the ring base - circle */
    for(j=0; j<SPOKES; j++) {
	alpha = 2*M_PI * (float)j / (float)SPOKES;
	initialRing[j][0] = fcos(alpha) * ringRadius;
	initialRing[j][1] = fsin(alpha) * ringRadius;
    }
   

    /* create the first ring */
    for(j=0; j<SPOKES; j++) {
      pfSetVec3( smokeCoords[j], 
		 initialRing[j][0]*(0.5 +rand3(0.1,0.5)) + smokePos[0],
		 initialRing[j][1]*(0.5 +rand3(0.1,0.5)) + smokePos[1],
		 smokePos[2]);
      pfSetVec3( speeds[j],
		 smokeSpeed[0]+rand3(speedVariation[0]*initialRing[j][0],0.25),
		 smokeSpeed[1]+rand3(speedVariation[0]*initialRing[j][1],0.25),
		 smokeSpeed[2] + rand3(speedVariation[1],0.5));
    }
    for(j=0; j<SPOKES; j++) {
      pfSetVec3( smokeCoords[j+SPOKES],
		 initialRing[j][0]*(1 + rand3(0.2,0.5)) + smokePos[0],
		 initialRing[j][1]*(1 + rand3(0.2,0.5)) + smokePos[1],
		 smokePos[2]);
      pfSetVec3( speeds[j+SPOKES],
		 rand3(speedVariation[0]*initialRing[j][0],0.5), 
		 rand3(speedVariation[0]*initialRing[j][1],0.5),
		 0);
    }


    gstate = pfNewGState( pfGetSharedArena() );
    pfGStateMode(gstate,PFSTATE_TRANSPARENCY, PFTR_OFF);
    pfGStateMode(gstate,PFSTATE_ENTEXTURE, PF_OFF);
    pfGStateMode(gstate,PFSTATE_ENLIGHTING,PF_OFF);
    /*pfGStateMode(gstate,PFSTATE_CULLFACE,PFCF_OFF);*/ /*XXX ???*/

    geode0 = pfNewGeode();
    fluxedGset =  
      (pfGeoSet *) pfNewFluxInitFunc( makeFluxedGset,
				      PFFLUX_DEFAULT_NUM_BUFFERS,
				      pfGetSharedArena() );
    pfAddGSet( geode0,fluxedGset );

    fluxedGset2 =  
      (pfGeoSet *) pfNewFluxInitFunc( makeFluxedGset2,
				      PFFLUX_DEFAULT_NUM_BUFFERS,
				      pfGetSharedArena() );
    pfAddGSet( geode0,fluxedGset2 );

    group = pfNewGroup();
    pfAddChild(group, geode0);

    return (pfNode  *) group;
}
