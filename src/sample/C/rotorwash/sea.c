/*
 * Copyright 1999, 2000, Silicon Graphics, Inc.
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

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <signal.h> /* for sigset for forked X event handler process*/
#ifndef WIN32
#include <getopt.h> /* for cmdline handler*/
#include <X11/keysym.h>
#endif

#include <Performer/pf.h>
#include <Performer/pr.h>
#include <Performer/pfutil.h>

#define ROTATE
#define NUM_FRAMES 16
#define MESH_SIZE 150.0f
#define RES       60
#define VPOS ((j*2*RES) + i*2)

pfGeoSet *fluxedGset;

static pfVec3 *norms;
static int *lengths;

pfTexture *TiTex[NUM_FRAMES];

void UpdateSea(pfVec3 *eye)
{
    int i;
    pfVec3 V;
    float fres;
    pfGeoSet *gset;
    pfGeoState *gstate;
    pfVec4 *Dcolors;
    pfVec3 *coords;
    unsigned short *dummyList;
    static float phase = 0.0f;
    static int frame_count = 0;

    frame_count ++;
    if (frame_count >= NUM_FRAMES*2) frame_count = 0;

    gset = (pfGeoSet *)  pfGetFluxWritableData((pfFlux *)fluxedGset);

    gstate = pfGetGSetGState(gset);

    pfGetGSetAttrLists(gset,PFGS_COORD3, (void **)&coords, &dummyList);
    pfGetGSetAttrLists(gset,PFGS_COLOR4, (void **)&Dcolors, &dummyList);

    phase += .05;
    if(phase > M_PI*2.0f) phase -= M_PI*2.0f;
    if(TiTex[frame_count/2])
      pfGStateAttr(gstate,PFSTATE_TEXTURE, TiTex[frame_count/2]);
  
    if(eye)
    {

      /* We know the ocean is horizontal so */
      /* it's simpler just to reflect z not the */
      /* v around N */

      for(i = 0; i < 2*RES*RES; i++)
      {
        coords[i][2] =  (sinf(phase + 0.25f*coords[i][1]) + 
			 0.65f * cosf(phase + 0.15f * coords[i][1] +
				      coords[i][0]) );

        pfSetVec3( norms[i],
		   -sinf(phase + 0.15f * coords[i][1]+coords[i][0]), 
		   sinf(phase + 0.15f * coords[i][1]+coords[i][0]) - 
		      cosf(phase + 0.25f * coords[i][1]), 
		   2.0f);
        pfNormalizeVec3( norms[i] );

        pfSubVec3( V , coords[i],  *eye );
        pfNormalizeVec3(V);
        fres = pfDotVec3(V,norms[i]);

        pfSetVec4(Dcolors[i],0.2f, 0.35f+.05f * fres, .35f+.15f * fres, 1.0f );
      }
    }

    pfFluxWriteComplete((pfFlux *)fluxedGset);
}

static int makeFluxedGset(pfFluxMemory *fmem)
{
    pfGeoState *gstate;
    pfGeoSet   *gset;
    pfVec4 *Dcolors;
    pfVec3 *coords;
    pfVec2 *Btex;
    pfTexEnv *tev = NULL;
    pfFlux *myflux=0;
    int i,j;
    float step;

    if (fmem == NULL)
	return pfFluxedGSetInit(fmem);

    pfFluxedGSetInit(fmem);

    gset = (pfGeoSet*)pfGetData(fmem);


    if( !gset ) {
      pfNotify(PFNFY_FATAL,PFNFY_INTERNAL,"no pfGeoSet within Flux data");
      pfExit();
    }

    tev = pfNewTEnv( pfGetSharedArena() );
    pfTEnvMode(tev,PFTE_MODULATE);

    gstate = pfNewGState( pfGetSharedArena() );
    pfGStateMode(gstate,PFSTATE_TRANSPARENCY, PFTR_OFF);
    pfGStateMode(gstate,PFSTATE_ENTEXTURE, PF_ON);
    pfGStateMode(gstate,PFSTATE_ENLIGHTING,0);
    pfGStateMode(gstate,PFSTATE_CULLFACE,PFCF_OFF);
    pfGStateAttr(gstate,PFSTATE_TEXENV, tev);
    pfGStateAttr(gstate,PFSTATE_TEXTURE, TiTex[0]);

    Dcolors = (pfVec4*) pfCalloc(2*RES*RES,sizeof(pfVec4), pfGetSharedArena());
    coords = (pfVec3*) pfCalloc(2*RES*RES,sizeof(pfVec3),pfGetSharedArena() );
    Btex = (pfVec2*) pfCalloc(2*RES*RES,sizeof(pfVec2),pfGetSharedArena());

    step = 2.0 / (float)(RES-1);

    /* ceiling information */
    for(j = 0; j < RES; j++)
    {
      *(lengths+j) = 2*RES;
      for(i = 0; i < RES; i++)
      {
	pfSetVec3(coords[VPOS],
		  -MESH_SIZE+i*MESH_SIZE*step, 
		  -MESH_SIZE+j*MESH_SIZE*step, 0.0f );
	pfSetVec3(coords[VPOS+1],
		  -MESH_SIZE+i*MESH_SIZE*step, 
		  -MESH_SIZE+(j+1)*MESH_SIZE*step, 0.0f );

        pfSetVec3(norms[VPOS],   0.0f, 0.0f, 1.0f);
        pfSetVec3(norms[VPOS+1], 0.0f, 0.0f, 1.0f);

        pfSetVec2(Btex[VPOS],   
		  0.15f * coords[VPOS][1],  0.15f * coords[VPOS][0]);
        pfSetVec2(Btex[VPOS+1], 
		  0.15f * coords[VPOS+1][1], 0.15f * coords[VPOS+1][0]);
      }
    }

    pfGSetAttr(gset,PFGS_COORD3, PFGS_PER_VERTEX, coords, 0);
    pfGSetAttr(gset,PFGS_COLOR4, PFGS_PER_VERTEX, Dcolors, 0);
    pfGSetAttr(gset,PFGS_TEXCOORD2, PFGS_PER_VERTEX, Btex, 0);
    pfGSetPrimLengths(gset,lengths);
    pfGSetPrimType(gset,PFGS_TRISTRIPS);
    pfGSetNumPrims(gset,RES);
    pfGSetGState(gset,gstate);

    return 0;
}


pfNode *MakeSea(int animate)
{
    int i;
    pfGeode *geode0 = NULL;
    pfGroup *group = NULL;

    /* Set up geoset */

    lengths = (int *) pfCalloc(RES,sizeof(int),pfGetSharedArena());
    norms = (pfVec3*) pfCalloc(2*RES*RES, sizeof(pfVec3),pfGetSharedArena());

    for(i=0; i<NUM_FRAMES; i++)
    {
      char fname[80];
      if(!i || animate)
      {
        TiTex[i] = pfNewTex( pfGetSharedArena() );
        pfTexFilter(TiTex[i],PFTEX_MINFILTER, PFTEX_MIPMAP_TRILINEAR);
        pfTexFilter(TiTex[i],PFTEX_MAGFILTER, PFTEX_BILINEAR);
        pfTexRepeat(TiTex[i],PFTEX_WRAP_S, PFTEX_REPEAT);
        pfTexRepeat(TiTex[i],PFTEX_WRAP_T, PFTEX_REPEAT);
        if(i < 10)
          sprintf(fname, "Animations_small/Ranim0%1d.bw", i);
        else
          sprintf(fname, "Animations_small/Ranim%2d.bw", i);
        pfLoadTexFile(TiTex[i], fname);
        pfTexFilter(TiTex[i],PFTEX_MAGFILTER, PFTEX_BILINEAR);
      }
      else TiTex[i] = NULL;
    }

    geode0 = pfNewGeode();

    fluxedGset =  
      (pfGeoSet *) pfNewFluxInitFunc( makeFluxedGset,
				      PFFLUX_DEFAULT_NUM_BUFFERS,
				      pfGetSharedArena() );
    pfAddGSet( geode0,fluxedGset );

    group = pfNewGroup();
    pfAddChild(group, geode0);

    return (pfNode  *) group;
}



/************************************************************************/
/************************************************************************/

extern pfRotorWash *wash_mesh;

typedef struct terrain_vertex {
    float normal[3];
    float height;
} terrain_vertex;

static terrain_vertex *terrain = NULL;
#define XRANGE (2.5*M_PI)
#define ZRANGE (3*M_PI)

#define TERR_SCALE 5

/************************************************************************/

float terrainFunction(float x, float z, int type) 
{
#define RAD 50
#define PEAK 15

    switch(type) {
    case 0:
	return (TERR_SCALE*fsin((x)+(z))*fcos((x)-(z)));

    case 1:
	return -RAD*150+PEAK + 150*fsqrt(RAD*RAD - x*x-z*z) + 
	    (TERR_SCALE*fsin((x)+(z))*fcos((x)-(z)) + 
	     TERR_SCALE*0.4*fsin((x*3)+(z*2))*fcos((x*2)-(z*3)));
    }

    return 0;
}

/************************************************************************/
pfNode *MakeLand(int res, int ftype) 
{
    float x,z,stepx,stepz,delta,scalex,scalez;
    float derx, derz, nx, ny, nz, invlen;
    float xmax, xmin, zmax, zmin;
    int i,j, index;
    float xstep,zstep;
    float xl, zl;
    int u,v;
    int PatchStep = 4;
    int terrRes;
    pfMaterial *mat = NULL;
    pfTexture *tex = NULL;
    pfTexEnv *tev = NULL;
    pfGeoState *gstate = NULL;
    pfGeoSet *gset = NULL;
    pfGroup *root = NULL;
    pfGeode *terr = NULL;
    pfVec3 *coords = NULL;
    pfVec3 *normals = NULL;
    pfVec2 *texcoords =NULL;
    int *lengths = NULL;


    root = pfNewGroup();
    terr = pfNewGeode();

    xmax =  MESH_SIZE*0.4*4/3;
    xmin = -MESH_SIZE*0.4*4/3;
    zmax =  MESH_SIZE*0.4*4/3;
    zmin = -MESH_SIZE*0.4*4/3;

    x = -XRANGE*0.5;

    terrRes =  1 + PatchStep * ((res + PatchStep-1)/PatchStep);

    if(!terrain) {
	terrain = (terrain_vertex*) pfCalloc( terrRes*terrRes,
					      sizeof(terrain_vertex),
					      pfGetSharedArena() );
	if(!terrain) {
	    printf("Not enough memory for terrain mesh\n");
	    exit(0);
	}
    }


    stepx = XRANGE/(float)(terrRes-1);
    /* 1 unit in x or z is how much in eye-coords */
    scalex = (xmax-xmin)/(-2*x);

    delta =  stepx/10.0;

    for(i=0; i<terrRes; i++) {
	z = -ZRANGE*0.5;
	stepz = ZRANGE/(float)(terrRes-1);
	scalez = (zmax-zmin)/(-2*z);

	for(j=0; j<terrRes; j++) {
	    terrain[i+terrRes*j].height = terrainFunction(x,z,ftype);

	    /*printf("%d,%d: %g\n", i,j,terrain[i+terrRes*j]);*/
	    
	    derx = terrainFunction(x+delta,z,ftype) - 
	           terrainFunction(x-delta,z,ftype);
	    derz = terrainFunction(x,z+delta,ftype) - 
                   terrainFunction(x,z-delta,ftype);
	    
	    /* normal is a cross products of vectors (0,derz,2*delta*scalez)
	       and  (2*delta*scalex,derx,0) */
	    /* note the result is predivided by 2*delta */
	    nx = -derx*scalez;
	    ny = 2*delta*scalex*scalez;
	    nz = -derz*scalex;
	    
	    /* normalize */
	    invlen = 1.0/fsqrt(nx*nx + ny*ny + nz*nz);
	    terrain[i+terrRes*j].normal[0] = nx *invlen;
	    terrain[i+terrRes*j].normal[1] = ny *invlen;
	    terrain[i+terrRes*j].normal[2] = nz *invlen;	    
	    
	    z+=stepz;
	}
	x+=stepx;
    }


    xstep = (xmax-xmin)/(terrRes-1);
    zstep = (zmax-zmin)/(terrRes-1);

#define TEX_SCALE 0.03
    /* terrain */

    /* create a set of square areas and compute a 4 point bbox for each
     */
    
    /* material */
    mat = pfNewMtl( pfGetSharedArena() );
    pfMtlColor(mat,PFMTL_AMBIENT, 0.0, 0.0, 0.0);
    pfMtlColor(mat,PFMTL_DIFFUSE, 0.9, 1.0, 0.15);
    pfMtlColor(mat,PFMTL_SPECULAR, 0.0, 0.0, 0.0);
    pfMtlColor(mat,PFMTL_EMISSION, 0.0, 0.0, 0.0);
    pfMtlColorMode(mat,PFMTL_BOTH, PFMTL_CMODE_AMBIENT_AND_DIFFUSE);
    
    /* texture */
    tex = pfNewTex( pfGetSharedArena() );
    pfTexFilter(tex,PFTEX_MINFILTER, PFTEX_BILINEAR);
    pfTexFilter(tex,PFTEX_MAGFILTER, PFTEX_BILINEAR);
    pfTexRepeat(tex,PFTEX_WRAP_S, PFTEX_REPEAT);
    pfTexRepeat(tex,PFTEX_WRAP_T, PFTEX_REPEAT);
    pfLoadTexFile(tex,"ground.rgb");

    tev = pfNewTEnv( pfGetSharedArena() );
    pfTEnvMode(tev, PFTE_MODULATE);

    gstate = pfNewGState( pfGetSharedArena() );
    pfGStateMode(gstate,PFSTATE_TRANSPARENCY, PFTR_OFF);
    pfGStateMode(gstate,PFSTATE_CULLFACE,PFCF_OFF);
    pfGStateMode(gstate,PFSTATE_ENLIGHTING, PF_ON);
    pfGStateMode(gstate,PFSTATE_ENTEXTURE, PF_ON);
    pfGStateMode(gstate,PFSTATE_ENTEXGEN, PF_OFF);
    pfGStateAttr(gstate,PFSTATE_FRONTMTL, (void *)mat);
    pfGStateAttr(gstate,PFSTATE_BACKMTL, (void *)mat);
    pfGStateAttr(gstate,PFSTATE_TEXTURE, (void *)tex);
    pfGStateAttr(gstate,PFSTATE_TEXENV, tev);

#if 1
    pfRotorWashTextures(wash_mesh,&gstate, 1, "Animations_small/land", 16);
    pfRotorWashColor(wash_mesh,gstate, 0.3, 0.25, 0.1, 1);
#else
    /* use the same textures on land as on the water */
    pfRotorWashTextures(wash_mesh,&gstate, 1, NULL, 0);
    pfRotorWashColor(wash_mesh,gstate, 0.1, 0.05, 0, 0.75);
#endif

    /* only one layer */
    xstep = (xmax-xmin)/(terrRes-1);
    zstep = (zmax-zmin)/(terrRes-1);


    z = zmin;

    lengths = (int *) pfCalloc(PatchStep, sizeof(int), pfGetSharedArena());

    for(i=0;i<PatchStep;i++)
	lengths[i] = PatchStep*2+2;

    for(i=0;i<terrRes-1;i+=PatchStep) {
      x = xmin;
      for(j=0;j<terrRes-1;j+=PatchStep) {
	gset = pfNewGSet( pfGetSharedArena() );

	zl = z;
	    
	pfGSetPrimType(gset, PFGS_TRISTRIPS);
	pfGSetPrimLengths(gset, lengths);
	pfGSetNumPrims(gset, PatchStep);
		
	coords = (pfVec3 *)pfCalloc( PatchStep*(PatchStep*2 + 2), 
				     sizeof(pfVec3),
				     pfGetSharedArena() );
	normals = (pfVec3 *)pfCalloc( PatchStep*(PatchStep*2 + 2),
				      sizeof(pfVec3),
				     pfGetSharedArena() );
	texcoords = (pfVec2 *)pfCalloc( PatchStep*(PatchStep*2 + 2),
					sizeof(pfVec2),
					pfGetSharedArena());
		
	index = 0;

	for(u=0;u<PatchStep;u++) {
		
	  xl = x;
	  /* if(zl>zmax+zstep*0.5) */
	  /*    break; */
				
	  for(v=0;v<=PatchStep;v++) {
	    /* if(xl > xmax+xstep*0.5) */
	    /*    break; */
	    pfSetVec3(coords[index],
		      xl, zl+zstep, terrain[(i+u+1)*terrRes+j+v].height);
	    pfSetVec2( texcoords[index],xl*TEX_SCALE, (zl+zstep)*TEX_SCALE);
	    pfSetVec3( normals[index++],
		       terrain[(i+u+1)*terrRes+j+v].normal[0],
		       terrain[(i+u+1)*terrRes+j+v].normal[2],
		       terrain[(i+u+1)*terrRes+j+v].normal[1]);
		  
	    pfSetVec3( coords[index], xl, zl, 
		       terrain[(i+u)*terrRes+j+v].height);
	    pfSetVec2( texcoords[index], xl*TEX_SCALE, zl*TEX_SCALE);
	    pfSetVec3( normals[index++], 
		       terrain[(i+u)*terrRes+j+v].normal[0],
		       terrain[(i+u)*terrRes+j+v].normal[2],
		       terrain[(i+u)*terrRes+j+v].normal[1]);
	    xl += xstep;
	  }
	  zl += zstep;
	}
	pfGSetAttr(gset,PFGS_COORD3, PFGS_PER_VERTEX, coords, NULL);
	pfGSetAttr(gset,PFGS_NORMAL3, PFGS_PER_VERTEX, normals, NULL);
	pfGSetAttr(gset,PFGS_TEXCOORD2, PFGS_PER_VERTEX, texcoords, NULL);
	pfGSetGState(gset,gstate);
    
	pfAddGSet(terr,gset);
	    
	x += PatchStep*xstep;
      }
      z += PatchStep*zstep;
    }

    pfAddChild(root,terr);	    


    return root;
}
