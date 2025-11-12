/*
 * Copyright (C) 1995-1997, Silicon Graphics, Inc.
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
 *  pfpfb.c $Revision: 1.255 $
 *
 *  Performer Fast Binary File Format (.pfb) Load and Store routines.
 *
 *  Author: Rob Mace
 */

#include <stdlib.h>
#include <string.h>
#ifdef __sgi
#include <bstring.h>
#endif  /* __linux__ */


#ifdef WIN32
#include "wintypes.h"
#endif

#include "pfpfb.h"


#ifndef P_32_SWAP
static uint _tmp32bitSwapData;
#define	P_32_SWAP(a) {						\
        _tmp32bitSwapData = *(uint *)a; 			\
	((char *)a)[0] = ((char *)&_tmp32bitSwapData)[3];	\
	((char *)a)[1] = ((char *)&_tmp32bitSwapData)[2];	\
	((char *)a)[2] = ((char *)&_tmp32bitSwapData)[1];	\
	((char *)a)[3] = ((char *)&_tmp32bitSwapData)[0];	\
}
#endif  /* P_32_SWAP */

#ifndef P_16_SWAP
static ushort _tmp16bitSwapData;
#define	P_16_SWAP(a) {							\
	_tmp16bitSwapData = *(ushort *)a;				\
	((char *)a)[0] = ((char *)&_tmp16bitSwapData)[1];		\
	((char *)a)[1] = ((char *)&_tmp16bitSwapData)[0];		\
}
#endif  /* P_16_SWAP */



#define PFB_MAGIC_NUMBER	 0xdb0ace00
#define PFB_MAGIC_ENCODED	 0xdb0ace01
#define PFB_MAGIC_NUMBER_LE  0x00ce0adb
#define PFB_MAGIC_ENCODED_LE 0x01ce0adb
#define PFB_VERSION_NUMBER    33




static int r_swap = False;

typedef size_t (*pfbReadFuncPtrType)(void *ptr, size_t size, size_t nitems, FILE *stream);
typedef int    (*pfbSeekFuncPtrType)(FILE *stream, long offset, int whence);
typedef long   (*pfbTellFuncPtrType)(FILE *stream);
typedef struct pfbFileReadFunctions_struct {
  pfbReadFuncPtrType fread;
  pfbSeekFuncPtrType fseek;
  pfbTellFuncPtrType ftell;
} pfbFileReadFunction_t;

static pfbFileReadFunction_t pfbFileReadFunction;

#define PFB_MEMORY_COMPRESSION_METHOD_NONE 0
#define PFB_MEMORY_COMPRESSION_METHOD_RLE  1

typedef struct pfbMemoryFile_struct {
  unsigned char *start, *position;
  long offset, size;
} pfbMemoryFile;


static
size_t fread_from_memory(void *ptr,size_t size,size_t nitems,FILE *stream)
{
  void *curPtr;
  size_t i, count;
  pfbMemoryFile *file= (pfbMemoryFile *)stream;
 
  
  count = 0;
  curPtr = ptr;

  if(!file || !ptr)
    return 0;

  for(i=0; i<nitems; i++) {
    if((file->offset + size) > file->size) {
      /* not enough data to read this chunk so just bail prematurely */
      return count; 
    }
    
    memcpy(curPtr, file->position, size);
    
    file->position = ((unsigned char *)file->position) + size;
    curPtr = ((unsigned char *)curPtr) + size;
    file->offset += size;
    count++;
  }

  return count;
}

static
int fseek_from_memory(FILE *stream,long offset,int whence)
{
  pfbMemoryFile *file = (pfbMemoryFile *)stream;

  if(!stream)
    return -1;

  switch(whence) {
  case SEEK_SET: /* start of file + offset */
    
    /* XXX Alex ... make sure offset isn't larger than file->size! */
    file->offset = offset;
    file->position = ((unsigned char *)file->start) + offset;
    
    break;

  case SEEK_CUR: /* current position in file + offset */
    file->offset += offset;
    file->position = ((unsigned char *)file->position) + offset;

    break;

  case SEEK_END: /* EOF + offset */
    pfNotify(PFNFY_FATAL,PFNFY_PRINT,"Unexpected call to fseek(0x%x,%ld,SEEK_END)",
             stream,offset);
    break;

  default:
    return -1; /* huh .. unrecognized token used for 'whence' */
  }

  return 0; /* worked ... */
}

static
long ftell_from_memory(FILE *stream)
{
  pfbMemoryFile *file = (pfbMemoryFile *)stream;

  if(!stream)
    return -1;

  return file->offset;
}

size_t swapped_fread32(void *ptr, size_t size, size_t nitems, FILE *stream)
{
    unsigned char *p = ptr;
    int i, n;

    if ((size != 4) && (nitems > 1))
    	n = (size * nitems)/4;
    else if ((size != 4) && nitems == 1)
    	n = size/4;
    else
    	n = nitems;

    /* do 32bit swap */
    nitems = pfbFileReadFunction.fread(ptr, 4, n, stream);
    for(i=0; i<n; ++i)
    {
    	P_32_SWAP(p);
    	p += 4;
    }

    return nitems;
}

/* ARGSUSED */
size_t swapped_fread16(void *ptr, size_t size, size_t nitems, FILE *stream)
{
        unsigned char *p = ptr;
        int i;

        /* do 16bit swap */
        nitems = pfbFileReadFunction.fread(ptr, 2, nitems, stream);
        for(i=0; i<nitems; ++i)
        {
            P_16_SWAP(p);
            p += 2;
        }
        return nitems;
}

size_t pfb_fread(void *ptr, size_t size, size_t nitems, FILE *stream)
{
    if (r_swap)
    {
	if(size == 1)
	{
	    /* normal fread */
	    nitems = pfbFileReadFunction.fread(ptr, size, nitems, stream);
	    return nitems;
	}
	else if(size == 2)
	{
	    /* swapped_fread16 */
	    nitems = swapped_fread16(ptr, size, nitems, stream);
	    return nitems;
	}
	else if((size == 4) || (size%4 == 0))
	{
	    /* swapped_fread32 */
	    nitems = swapped_fread32(ptr, size, nitems, stream);
	    return nitems;
	}
    }
    else
    {
	/* normal fread */
	nitems = pfbFileReadFunction.fread(ptr, size, nitems, stream);
	return nitems;
    }
    return 0; /* make compiler happy -- should never get here */
}

static int getNumGSetVertices(pfGeoSet *gset)
{
    pfVec3 *attrib;
    ushort *indexPtr;
    int *lengths;
    int j, type, nprims, vertcount;

    pfGetGSetAttrLists(gset, PFGS_COORD3, (void **)&attrib, &indexPtr);

    if(indexPtr)  /* don't bother to handle indexed data */
	return 0;

    /* get number of vertices */
    type = pfGetGSetPrimType(gset);
    nprims = pfGetGSetNumPrims(gset);

    switch(type)
    {
    case PFGS_TRIS:
	vertcount = 3 * nprims;
	break;
	
    case PFGS_QUADS:
	vertcount = 4 * nprims;
	break;
	
    case PFGS_TRISTRIPS:
    case PFGS_FLAT_TRISTRIPS:
    case PFGS_TRIFANS:
    case PFGS_FLAT_TRIFANS:
    case PFGS_POLYS:

	lengths = pfGetGSetPrimLengths(gset);
	
	vertcount = 0;
	for(j = 0; j<nprims; j++)
	    vertcount += lengths[j];
	break;

    case PFGS_POINTS:
    case PFGS_LINES:
    case PFGS_LINESTRIPS:
    case PFGS_FLAT_LINESTRIPS:
    default:
	vertcount = 0;
	break;
    }
    
    return vertcount;
}

/*
 *  Historic versions at which specific features were added.
 */
#define PFBV_NODE_BSPHERE		5
#define PFBV_GSET_DO_DP			5
#define PFBV_LPSTATE_CALLIG		5
#define PFBV_CLIPTEXTURE		6
#define PFBV_GSET_BBOX_FLUX		8
#define PFBV_FLUX_SYNC_GROUP		10
#define PFBV_SWITCH_VAL_FLUX		11
#define PFBV_UFUNC			12
#define PFBV_UDATA_SLOT_FUNCS		16
#define PFBV_FLUX_SYNC_GROUP_NAMES	17
#define PFBV_ASD_SYNC_GROUP		17
#define PFBV_ASD_GSTATES		18
#define PFBV_MULTITEXTURE		19
#define PFBV_ANISOTROPY			20
#define PFBV_SHADER                     21
#define PFBV_ANISOTROPY_PFA		22
#define PFBV_IBR_TEXTURE		23
#define PFBV_VERSION2_6                 24 /* first needed for texCoords in
					      pfIBRnode */
#define PFBV_REMOVE_SHADER              25 
#define PFBV_SHADER_V2                  26 /* complete shader re-work */
#define PFBV_GEOARRAY                   27 /* GeoArray Type */
#define PFBV_SURFACES			28 /* parametric surface support */
#define PFBV_TEXTURE_COMPRESSION	29 /* added tokens for compressed 
					      external and internal texture 
					      formats */
#define PFBV_SHADER_OBJECTS             30 /* OpenGL shader_objects support */
#define PFBV_MAX_TEXTURES_8             31 /* bump up max textures to 8 */
#define PFBV_SURFACE_CLONING            32 /* fix cloning of pfParaSurfaces w/ topo info */
#define PFBV_GEOARRAY_STRIDE_SAVE       33

/* if you add any types here be sure to modify PFB_VERSION_NUMBER */

/*
 *	This hold the value of PF_MAX_TEXTURES as of pfb version 19.
 *	If PF_MAX_TEXTURES changes, we can maintain binary compatibility 
 *	by extending structures by (PF_MAX_TEXTURES - PF_MAX_TEXTURES_19).
 *
 *	The value of PF_MAX_TEXTURES differs between the Linux and IRIX 
 *	versions of libpf. In the pfb format, we always have PF_MAX_TEXTURES_19
 *	elements for texture related objects.
 *      But, as of pf 3.2 PF_MAX_TEXTURES is the same on all platforms (8).
 *
 */
#define PF_MAX_TEXTURES_19	4	
#define PF_MAX_TEXTURES_31      8

/*
#if !((PF_MAJOR_VERSION == 2) && (PF_MINOR_VERSION == 0))
#endif
*/

/* These bugs have been fixed in 2.0.2 */
#define XXX_FIXED_TEX_SPLINE_BUG
#define XXX_FIXED_GET_LOD_TRANSITION_BUG
#define XXX_FIXED_TEX_BORDER_COLOR_BUG
#define XXX_FIXED_FONT_CHAR_SPACING_BUG

/*
 *  For pre Performer 2.1
 */
#ifndef TV_ALPHA
#define TV_ALPHA		0
#endif
#ifndef TV_IA_GETS_RG
#define TV_IA_GETS_RG		0
#endif
#ifndef TV_IA_GETS_BA
#define TV_IA_GETS_BA		0
#endif
#ifndef PFTEX_I_8
#define PFTEX_I_8		1111111
#endif


#undef pfdDescendObject_pfb
#undef pfdLoadObjectRef_pfb
#undef pfdStoreObjectRef_pfb
#undef pfdDescendObject_pfa
#undef pfdLoadObjectRef_pfa
#undef pfdStoreObjectRef_pfa


typedef struct list_s {
    void *data;
    int id;
    int size;
    struct list_s *next;
    struct list_s *bnext;
} list_t;


typedef struct udata_info_s {
    int type;
    int list;
    int id;
    int slot;
    void *parent;
} udata_info_t;

/*
 *  defines for udata_info_t.type
 */
#define UDT_MEMORY		0
#define UDT_PF_OBJECT		1
#define UDT_USER_DEFINED	2


typedef struct udata_fix_s {
    int type;
    pfObject *obj;
    pfObject **patch;
    int list;
    int id;
    int which;
    void *func;
    struct udata_fix_s *next;
} udata_fix_t;

/*
 *  defines for udata_fix_t.type
 */
#define UDF_UDATA	0
#define UDF_PATCH	1
#define UDF_TRAV_DATA	2
#define UDF_RASTER_FUNC	3
#define UDF_CALLIG_FUNC	4


typedef struct {
    pfdStoreUserDataFuncType_pfb store;
    pfdLoadUserDataFuncType_pfb load;
    pfdDescendUserDataFuncType_pfb descend;
} udata_func_t;


typedef struct custom_node_s {
    pfType *type;
    char *name;
    int used;
    pfdDescendCustomNodeFuncType_pfb descend_func;
    pfdStoreCustomNodeFuncType_pfb store_func;
    pfdNewCustomNodeFuncType_pfb new_func;
    pfdLoadCustomNodeFuncType_pfb load_func;
} custom_node_t;


typedef struct node_trav_s {
    pfNodeTravFuncType app_pre, app_post;
    void *app_data;
    pfNodeTravFuncType cull_pre, cull_post;
    void *cull_data;
    pfNodeTravFuncType draw_pre, draw_post;
    void *draw_data;
    pfNodeTravFuncType isect_pre, isect_post;
    void *isect_data;
} node_trav_t;


/*
 *  Object types
 */
typedef struct {
    int side;
    float alpha;
    float shininess;
    pfVec3 ambient;
    pfVec3 diffuse;
    pfVec3 specular;
    pfVec3 emission;
    int cmode[2];
    int udata;
} mtl_t;

typedef struct {
    float atten[3];
    int local;
    int twoside;
    pfVec4 ambient;
    int udata;
} lmodel_t;

typedef struct {
    pfVec3 ambient;
    pfVec3 diffuse;
    pfVec3 specular;
    float atten[3];
    pfVec4 pos;
    pfVec3 sl_dir;
    float sl_cone[2];
    int on;
    int udata;
} light_t;

typedef struct {
    pfVec3 ambient;
    pfVec3 diffuse;
    pfVec3 specular;
    float atten[3];
    pfVec4 pos;
    pfVec3 sl_dir;
    float sl_cone[2];
    int on;
    int shadow_enable;
    int projtex_enable;
    int freeze_shadows;
    int fog_enable;
    float shadow_displace_scale;
    float shadow_displace_offset;
    float shadow_size;
    float intensity;
    int shadow_tex;
    int proj_tex;
    int proj_fog;
    int proj_frust;
} lsource_t;

typedef struct {
    int elt_size;
    int num_elts;
    int udata;
} queue_t;


#define TEX_0_DATA	\
    int format[5];	\
    uint filter[4];	\
    int wrap[3];	\
    pfVec4 bcolor;	\
    int btype;		\
    pfVec2 ssp[4];	\
    float ssc;		\
    pfVec2 dsp[4];	\
    float dsc;		\
    int tdetail[2];	\
    int lmode[3];	\
    int losource[2];	\
    int lodest[2];	\
    int lsize[2];	\
    int image;		\
    int comp;		\
    int xsize;		\
    int ysize;		\
    int zsize;		\
    int load_image;	\
    int list_size;	\
    float frame;	\
    int num_levels;	\
    int udata;

#define TEX_1_DATA	\
    TEX_0_DATA		\
    int type;

#define TEX_DATA	\
    TEX_1_DATA		\
    int aniso_degree;

typedef struct {
    TEX_0_DATA
} tex_0_t;

typedef struct {
    TEX_1_DATA
} tex_1_t;

typedef struct {
    TEX_DATA
} tex_t;

typedef struct {
    float center[3];
    int clip_size;
    int virtual_size[3];
    int invalid_border;
    int virtual_LOD_offset;
    int effective_levels;
    int master;
    uint DTR_mode;
    float DTR_max_i_border;
    float LOD_range[2];
} cliptex_t;

typedef struct {
    int level;
    int obj;
    int type;
    int phase_margin;
    int phase_shift[3];
} cliplevel_t;

typedef struct {
    int level;
    int obj;
} level_t;

typedef struct {
    int size[3];
    int origin[3];
    int mem_image_format;
    int mem_image_type;
    int mem_info[2];
    int name_size;
    int fname_size;
    int file_image_format;
    int file_image_type;
    int file_tile[3];
    int num_file_tiles[3];
    int header_offset;
    int priority;
    int default_tile_mode;
    int default_tile;
    int read_func;
    int read_queue;
    int udata;
} itile_t;

typedef struct {
    int name_size;
    int image_size[3];
    int proto_tile;
    int mem_region_size[3];
    int mem_region_org[3];
    int tex_region_size[3];
    int tex_region_org[3];
    int tex;
    int tex_level;
    int tex_type;
    int tex_size[3];
    int read_queue_func;
    int tile_file_name_func;
    int format_string_size;
    int num_format_args;
    int num_stream_servers[3];
    int stream_server_size;
    int master;
    int auto_center;
    int auto_stream_server_queues;
    int auto_tile_file_name;
    int auto_tile_read_queue;
    int udata;
} icache_t;

typedef struct {
    int queue;
    int name_length;
} stream_server_t;

typedef struct tex_list_s {
    char *name;
    char *names[TEX_MAX_IMAGES];
    tex_t *t;
    cliptex_t *ct;
    pfTexture *tex;
    pfTexture *dtex;
    pfClipTexture *master;
    int *itlist;
    pfTexture **ptlist;
    pfObject **level_objs;
    level_t *levels;
    cliplevel_t *cliplevels;
    uint *image;
    uint *images[TEX_MAX_IMAGES];
    int  iimages[TEX_MAX_IMAGES];
    int  num_images;
    uint *load_image;
    struct tex_list_s *next;
} tex_list_t;

typedef struct {
    int mode;
    int component;
    float color[4];
    int udata;
} tenv_t;

typedef struct {
    int type;
    pfVec3 color;
    float range[2];
    float offsets[2];
    int r_points;
    float r_range[12];
    float r_density[12];
    float r_bias;
    int udata;
} fog_t;

typedef struct {
    int mode_s;
    int mode_t;
    int mode_r;
    int mode_q;
    float plane_s[4];
    float plane_t[4];
    float plane_r[4];
    float plane_q[4];
    int udata;
} tgen_t;

typedef struct {
    float range[2];
    float bias[3];
    int udata;
} tlod_t;

typedef struct {
    int level;
    int tsid;
    int vert[3];
    int attr[3];
    int refvert[3];
    int sattr[3];
    int child[4];
    ushort gstateid;
    ushort mask;
} asd_face_t;

typedef struct {
    float switchin;
    float morph;
} asd_lodrange_t;

typedef struct {
    pfVec3 v0; 
    pfVec3 vd;
    int neighborid[2];
    int vertid;
} asd_vert_t;

#define LPSTATE_0_DATA		\
    int size_mode;		\
    int transp_mode;		\
    int fog_mode;		\
    int dir_mode;		\
    int shape_mode;		\
    int range_mode;		\
    float size_min_pixel;	\
    float size_max_pixel;	\
    float size_actual;		\
    float transp_pixel_size;	\
    float transp_exponent;	\
    float transp_scale;		\
    float transp_clamp;		\
    float fog_scale;		\
    float intensity;		\
    float size_diff_thresh;	\
    float shape[5];		\
    pfVec4 back_color;		\
    int udata;

#define LPSTATE_1_DATA		\
    LPSTATE_0_DATA		\
    int draw_mode;		\
    int quality_mode;		\
    float significance;		\
    float min_defocus;		\
    float max_defocus;

#define LPSTATE_DATA		\
    LPSTATE_1_DATA		\
    int rfunc;			\
    int rfunc_data;		\
    int cfunc;			\
    int cfunc_data;

typedef struct {
    LPSTATE_DATA
} lpstate_t;

typedef struct {
    LPSTATE_1_DATA
} lpstate_1_t;

typedef struct {
    LPSTATE_0_DATA
} lpstate_0_t;

typedef struct {
    uint mode;
    pfVec3 fg_color;
    pfVec3 bg_color;
    float alpha;
    float normal_length[2];
    float line_width;
    float point_size;
    ushort fg_line_pat;
    ushort bg_line_pat;
    uint fg_fill_pat[32];
    uint bg_fill_pat[32];
    int gstate[2];
    int tex;
    int tenv;
    int tgen;
    int udata;
} hlight_t;

typedef struct {
    int type;
    float left;
    float right;
    float bottum;
    float top;
    float nearPlane;
    float farPlane;
    pfMatrix mat;
    int udata;
} frust_t;

#define FLUX_0_DATA	\
    int num_buffers;	\
    int data_size;	\
    uint flags;		\
    uint mask;		\
    int udata;

#define FLUX_DATA	\
    FLUX_0_DATA		\
    uint sync_group;

typedef struct {
    FLUX_DATA
} flux_t;

typedef struct {
    FLUX_0_DATA
} flux_0_t;

typedef struct {
    int function;
    int user_function;
    int dst_data;
    int dst_data_list;
    int dst_ilist;
    int dst_offset;
    int dst_stride;
    int num_srcs;
    int interations;
    int items;
    uint mask;
    float range[5];
    uint flags;
    int udata;
} engine_t;

typedef struct {
    int data;
    int data_list;
    int ilist;
    int icount;
    int offset;
    int stride;
} engine_src_t;

#define GSET_0_DATA		\
    int ptype;			\
    int pcount;			\
    int llist;			\
    int vlist[3];		\
    int clist[3];		\
    int nlist[3];		\
    int tlist[3];		\
    int draw_mode[3];		\
    int gstate[2];		\
    float line_width;		\
    float point_size;		\
    int draw_bin;		\
    uint isect_mask;		\
    int hlight;			\
    int bbox_mode;		\
    pfBox bbox;			\
    int udata;

#define GSET_1_DATA		\
    GSET_0_DATA			\
    uint draw_order;		\
    int decal_plane;		\
    float dplane_normal[3];	\
    float dplane_offset;

#define GSET_2_DATA		\
    GSET_1_DATA			\
    int bbox_flux;

#define GSET_3_DATA             \
    GSET_2_DATA                 \
    int multi_tlist[3 * (PF_MAX_TEXTURES_19 - 1)];

#define GSET_4_DATA             \
    GSET_3_DATA                 \
    int appearance;


/* 
 * if index of a geoSet has this bit set,
 * look into the geoArray List instead
 */
#define PFB_GARRAY_IXBIT (1<<30)


/*
 *	Allocate 7 slots for each texture unit. Ignore texture unit # 0 
 *	because it is taken care of by the tlist variable above.
 */
#define GSET_DATA		\
    GSET_4_DATA			\
    int multi_tlist2[3 * (PF_MAX_TEXTURES_31 - PF_MAX_TEXTURES_19)];

typedef struct {
    GSET_DATA
} gset_t;

typedef struct {
    GSET_0_DATA
} gset_0_t;

typedef struct {
    GSET_1_DATA
} gset_1_t;

typedef struct {
    GSET_2_DATA
} gset_2_t;

typedef struct {
    GSET_3_DATA
} gset_3_t;

typedef struct {
    GSET_4_DATA
} gset_4_t;

typedef struct {
    pfMatrix mat;
    int justify;
    int char_size;
    int font;
    float spacing_scale[3];
    pfVec4 color;
    int gstate[2];
    uint isect_mask;
    pfBox bbox;
} string_t;


/*
 *  Currently the Performer morph api is broken.  You can not get back
 *  floatsPerElt and nelts and you can not tell what an Attr is used
 *  for.  There fore we put all morphs into a list and deley descending
 *  into them until the rest of the scene graph is read.
 */
typedef struct morph_list_s {
    pfMorph *morph;
    int *attr_type;
    int *fpe;
    int *e;
    int *list_id;
    int *id;
    struct morph_list_s *next;
} morph_list_t;

typedef struct {
  int contCurve, disCurve;
  int boundary, boundaryDir;
} edge_t;

typedef struct {
  pfVec3 origin;
  pfMatrix matrix;
} rep_t;

typedef struct {
  pfReal beginT, endT;
  pfBool closed;
  pfReal closedTol;
} curve2d_t;

typedef struct {
  pfReal beginT, endT;
  pfBool closed;
  pfReal closedTol;
} curve3d_t;

typedef struct {
  pfReal x0, y0, t0;
  pfReal x1, y1, t1;
} line_t;

typedef struct {
  int topoId, solidId, surfaceId;
  pfReal beginU, endU, beginV, endV;
  int handednessHint, trimLoopCount;
  int geoStateIndex;
} surface_t;

/*
 *  lists
 */
#define L_MTL		 0
#define L_TEX		 1
#define L_TENV		 2
#define L_GSTATE	 3
#define L_LLIST		 4
#define L_VLIST		 5
#define L_CLIST		 6
#define L_NLIST		 7
#define L_TLIST		 8
#define L_ILIST		 9
#define L_GSET		10
#define L_UDATA		11
#define L_NODE		12
#define L_FLIST		13
#define L_MORPH		14
#define L_LODSTATE	15
#define L_FOG		16
#define L_TGEN		17
#define L_LMODEL	18
#define L_LIGHT		19
#define L_CTAB		20
#define L_LPSTATE	21
#define L_HLIGHT	22
#define L_LSOURCE	23
#define L_FRUST		24
#define L_FONT		25
#define L_STRING	26
#define L_IMAGE		27
#define L_CUSTOM	28
#define L_TLOD		29
#define L_ASDDATA	30
#define L_QUEUE		31
#define L_ITILE		32
#define L_ICACHE	33
#define L_FLUX		34
#define L_ENGINE	35
#define L_UFUNC		36
#define L_UDATA_NAME	37
#define L_UDATA_LIST	38
#define L_SG_NAME	39
#define L_SHADER        40
#define L_IBR_TEX       41
#define L_APPEARANCE    42
#define L_MESH          43
#define L_GARRAY	44
#define L_VARRAY_1	45
#define L_VARRAY_2	46
#define L_VARRAY_4	47
#define L_EDGE		48
#define L_TOPO 		49
#define L_CURVE2D	50
#define L_DISCURVE2D    51
#define L_BOUNDARY      52
#define L_SOLID         53
#define L_GPROG		54
#define L_GPROGPARM	55
#define L_SCALAR        56
#define L_SHADOBJ       57
#define L_SHADPROG      58
#define L_COUNT		59

static const int list_conv[L_COUNT] =
{
    L_MTL,
    L_TEX,
    L_TENV,
    L_GSTATE,
    L_LLIST,
    L_VLIST,
    L_CLIST,
    L_NLIST,
    L_TLIST,
    L_ILIST,
    L_GSET,
    L_UDATA,
    L_NODE,
    L_FLIST,
    L_MORPH,
    L_LODSTATE,
    L_FOG,
    L_TGEN,
    L_LMODEL,
    L_LIGHT,
    L_CTAB,
    L_LPSTATE,
    L_HLIGHT,
    L_LSOURCE,
    L_FRUST,
    L_FONT,
    L_STRING,
    L_IMAGE,
    L_CUSTOM,
    L_TLOD,
    L_ASDDATA,
    L_QUEUE,
    L_ITILE,
    L_ICACHE,
    L_FLUX,
    L_ENGINE,
    L_UFUNC,
    L_UDATA_NAME,
    L_UDATA_LIST,
    L_SG_NAME,
    L_SHADER,
    L_IBR_TEX,
    L_APPEARANCE,
    L_MESH,
    L_GARRAY,
    L_VARRAY_1,
    L_VARRAY_2,
    L_VARRAY_4,
    L_EDGE,
    L_TOPO,
    L_CURVE2D,
    L_DISCURVE2D,
    L_BOUNDARY,
    L_SOLID,
    L_GPROG,
    L_GPROGPARM,
    L_SCALAR,
    L_SHADOBJ,
    L_SHADPROG,
};

static const char *l_name[L_COUNT] =
{
    "Material",
    "Texture",
    "TexEnv",
    "GeoState",
    "Length List",
    "Vertex List",
    "Color List",
    "Normal List",
    "TexCoord List",
    "Index List",
    "GeoSet",
    "User Data",
    "Node",
    "Other List",
    "Morph",
    "LOD State",
    "Fog",
    "Tex Gen",
    "Light Model",
    "Light",
    "Color Table",
    "Light Point State",
    "Highlight",
    "Light Source",
    "Frustum",
    "Font",
    "String",
    "Image",
    "Custom",
    "TexLOD",
    "ASD Data",
    "Queue",
    "ImageTile",
    "ImageCache",
    "Flux",
    "Engine",
    "User Func",
    "User Data Slot Name",
    "User Data List",
    "Flux Sync Group Name",
    "Shader",
    "IBR texture",
    "islAppearance",
    "Mesh",
    "GeoArray",
    "Vertex Array Data (bytes)",
    "Vertex Array Data (short)",
    "Vertex Array Data",
    "Edge",
    "Topo",
    "Curve2d",
    "DisCurve2d",
    "Boundary",
    "Solid",
    "GProgram",
    "GProgram parameters",
    "Scalar",
    "Shader Object",
    "Shader Program"
};


#define PFA_WRITE_LIST(id, func);					\
    if (glb->l_num[id])							\
    {									\
	list_t *lp;							\
	int i;								\
									\
	if (glb->add_comments)						\
	    fprintf(glb->ofp, "#---------------------- %ss\n",		\
		    l_name[id]);					\
	fprintf(glb->ofp, "%d %d\n", id, glb->l_num[id]);		\
	lp = glb->l_list[id];						\
	i = 0;								\
	while (lp)							\
	{								\
	    if (glb->add_comments)					\
		fprintf(glb->ofp, "#---------------------- %s %d\n",	\
			l_name[id], i++);				\
	    func(lp->data, glb);					\
	    lp = lp->next;						\
	}								\
    }

#define PFB_WRITE_LIST(id, func);					\
    if (glb->l_num[id])							\
    {									\
	list_t *lp;							\
	int buf[2];							\
	int start, end;							\
									\
	buf[0] = id;							\
	buf[1] = glb->l_num[id];					\
	fwrite(buf, sizeof(int), 2, glb->ofp);				\
	fseek(glb->ofp, sizeof(int), SEEK_CUR);				\
	start = (int)ftell(glb->ofp);					\
	lp = glb->l_list[id];						\
	while (lp)							\
	{								\
	    func(lp->data, glb);					\
	    lp = lp->next;						\
	}								\
	end = (int)ftell(glb->ofp);					\
	fseek(glb->ofp, start - (int)sizeof(int), SEEK_SET);		\
	buf[0] = end - start;						\
	fwrite(buf, sizeof(int), 1, glb->ofp);				\
	fseek(glb->ofp, end, SEEK_SET);					\
    }

#define PFA_WRITE_SLIST(id, func);					\
    if (glb->l_num[id])							\
    {									\
	list_t *lp;							\
	int i;								\
									\
	if (glb->add_comments)						\
	    fprintf(glb->ofp, "#---------------------- %ss\n",		\
		    l_name[id]);					\
	fprintf(glb->ofp, "%d %d\n", id, glb->l_num[id]);		\
	lp = glb->l_list[id];						\
	i = 0;								\
	while (lp)							\
	{								\
	    if (glb->add_comments)					\
		fprintf(glb->ofp, "#---------------------- %s %d\n",	\
			l_name[id], i++);				\
	    func(lp->data, lp->size, glb);				\
	    lp = lp->next;						\
	}								\
    }

#define PFB_WRITE_SLIST(id, func);					\
    if (glb->l_num[id])							\
    {									\
	list_t *lp;							\
	int buf[2];							\
	int start, end;							\
									\
	buf[0] = id;							\
	buf[1] = glb->l_num[id];					\
	fwrite(buf, sizeof(int), 2, glb->ofp);				\
	fseek(glb->ofp, sizeof(int), SEEK_CUR);				\
	start = (int)ftell(glb->ofp);					\
	lp = glb->l_list[id];						\
	while (lp)							\
	{								\
	    func(lp->data, lp->size, glb);				\
	    lp = lp->next;						\
	}								\
	end = (int)ftell(glb->ofp);					\
	fseek(glb->ofp, start - (int)sizeof(int), SEEK_SET);		\
	buf[0] = end - start;						\
	fwrite(buf, sizeof(int), 1, glb->ofp);				\
	fseek(glb->ofp, end, SEEK_SET);					\
    }

#define PFA_WRITE_ID(id, names)					\
(								\
    fprintf(glb->ofp, "%d", (id)),				\
    (glb->add_comments)?					\
	fprintf(glb->ofp, "\t\t# %s\n",				\
		((id) & N_CUSTOM)?				\
		    glb->custom_nodes[((id)&N_CUSTOM_MASK) >>	\
				      N_CUSTOM_SHIFT].name	\
		    :						\
		    names[id])					\
	:							\
	fprintf(glb->ofp, "\n")					\
)


/*
 *  memory types
 */
#define MEM_ARENA	0
#define MEM_CBUF	1
#define MEM_FLUX	2


static const int aa_table[] = {2,
			       PFAA_OFF,
			       PFAA_ON};

static const int af_table[] = {9,
			       PFAF_OFF,
			       PFAF_NEVER,
			       PFAF_LESS,
			       PFAF_EQUAL,
			       PFAF_LEQUAL,
			       PFAF_GREATER,
			       PFAF_NOTEQUAL,
			       PFAF_GEQUAL,
			       PFAF_ALWAYS};

static const int cf_table[] = {4,
			       PFCF_OFF,
			       PFCF_BACK,
			       PFCF_FRONT,
			       PFCF_BOTH};

static const int decal_table[] = {12,
				  PFDECAL_OFF,
				  PFDECAL_LAYER,
				  PFDECAL_BASE,
				  PFDECAL_BASE_FAST,
				  PFDECAL_BASE_HIGH_QUALITY,
				  PFDECAL_BASE_STENCIL,
				  PFDECAL_BASE_DISPLACE,
				  PFDECAL_LAYER_FAST,
				  PFDECAL_LAYER_HIGH_QUALITY,
				  PFDECAL_LAYER_STENCIL,
				  PFDECAL_LAYER_DISPLACE,
				  PFDECAL_LAYER_DISPLACE_AWAY};

static const int tr_table[] = {14,
			       PFTR_OFF,
			       PFTR_ON,
			       PFTR_HIGH_QUALITY,
			       PFTR_FAST,
			       PFTR_BLEND_ALPHA,
			       PFTR_MS_ALPHA,
			       PFTR_MS_ALPHA_MASK,
			       PFTR_OFF | PFTR_NO_OCCLUDE,
			       PFTR_ON | PFTR_NO_OCCLUDE,
			       PFTR_HIGH_QUALITY | PFTR_NO_OCCLUDE,
			       PFTR_FAST | PFTR_NO_OCCLUDE,
			       PFTR_BLEND_ALPHA | PFTR_NO_OCCLUDE,
			       PFTR_MS_ALPHA | PFTR_NO_OCCLUDE,
			       PFTR_MS_ALPHA_MASK | PFTR_NO_OCCLUDE};

#define STATE_TRANSPARENCY	 1
#define STATE_ANTIALIAS		 2
#define STATE_DECAL		 3
#define STATE_ALPHAFUNC		 4
#define STATE_ENLIGHTING	 5
#define STATE_ENTEXTURE		 6
#define STATE_ENFOG		 7
#define STATE_CULLFACE		 8
#define STATE_ENWIREFRAME	 9
#define STATE_ENCOLORTABLE	10
#define STATE_ENHIGHLIGHTING	11
#define STATE_ENLPOINTSTATE	12
#define STATE_ENTEXGEN		13
#define STATE_ALPHAREF		14
#define STATE_FRONTMTL		15
#define STATE_BACKMTL		16
#define STATE_TEXTURE		17
#define STATE_TEXENV		18
#define STATE_FOG		19
#define STATE_LIGHTMODEL	20
#define STATE_LIGHTS		21
#define STATE_COLORTABLE	22
#define STATE_HIGHLIGHT		23
#define STATE_LPOINTSTATE	24
#define STATE_TEXGEN		25
#define STATE_ENTEXMAT		26
#define STATE_TEXMAT		27
#define STATE_ENTEXLOD		28
#define STATE_TEXLOD		29
#define STATE_VERTEX_PROGRAM	30
#define STATE_FRAGMENT_PROGRAM	31
#define STATE_ENVTXPROG		32
#define STATE_ENFRAGPROG	33
#define STATE_GPROGPARMS        34
#define STATE_SHADPROG          35
#define STATE_ENSHADPROG        36
#define STATE_END		-1

static const uint64_t gst_table[] = {36,
				PFSTATE_TRANSPARENCY,
				PFSTATE_ANTIALIAS,
				PFSTATE_DECAL,
				PFSTATE_ALPHAFUNC,
				PFSTATE_ENLIGHTING,
				PFSTATE_ENTEXTURE,
				PFSTATE_ENFOG,
				PFSTATE_CULLFACE,
				PFSTATE_ENWIREFRAME,
				PFSTATE_ENCOLORTABLE,
				PFSTATE_ENHIGHLIGHTING,
				PFSTATE_ENLPOINTSTATE,
				PFSTATE_ENTEXGEN,
				PFSTATE_ALPHAREF,
				PFSTATE_FRONTMTL,
				PFSTATE_BACKMTL,
				PFSTATE_TEXTURE,
				PFSTATE_TEXENV,
				PFSTATE_FOG,
				PFSTATE_LIGHTMODEL,
				PFSTATE_LIGHTS,
				PFSTATE_COLORTABLE,
				PFSTATE_HIGHLIGHT,
				PFSTATE_LPOINTSTATE,
				PFSTATE_TEXGEN,
				PFSTATE_ENTEXMAT,
				PFSTATE_TEXMAT,
				PFSTATE_ENTEXLOD,
				PFSTATE_TEXLOD,
				PFSTATE_VTXPROG,
				PFSTATE_FRAGPROG,
				PFSTATE_ENVTXPROG,
				PFSTATE_ENFRAGPROG,
				PFSTATE_GPROGPARMS,
				PFSTATE_SHADPROG,
				PFSTATE_ENSHADPROG, };

static const char *gst_names[] = {"",
				  "PFSTATE_TRANSPARENCY",
				  "PFSTATE_ANTIALIAS",
				  "PFSTATE_DECAL",
				  "PFSTATE_ALPHAFUNC",
				  "PFSTATE_ENLIGHTING",
				  "PFSTATE_ENTEXTURE",
				  "PFSTATE_ENFOG",
				  "PFSTATE_CULLFACE",
				  "PFSTATE_ENWIREFRAME",
				  "PFSTATE_ENCOLORTABLE",
				  "PFSTATE_ENHIGHLIGHTING",
				  "PFSTATE_ENLPOINTSTATE",
				  "PFSTATE_ENTEXGEN",
				  "PFSTATE_ALPHAREF",
				  "PFSTATE_FRONTMTL",
				  "PFSTATE_BACKMTL",
				  "PFSTATE_TEXTURE",
				  "PFSTATE_TEXENV",
				  "PFSTATE_FOG",
				  "PFSTATE_LIGHTMODEL",
				  "PFSTATE_LIGHTS",
				  "PFSTATE_COLORTABLE",
				  "PFSTATE_HIGHLIGHT",
				  "PFSTATE_LPOINTSTATE",
				  "PFSTATE_TEXGEN",
				  "PFSTATE_ENTEXMAT",
				  "PFSTATE_TEXMAT",
				  "PFSTATE_ENTEXLOD",
				  "PFSTATE_TEXLOD",
				  "PFSTATE_VTXPROG",
				  "PFSTATE_FRAGPROG",
				  "PFSTATE_ENVTXPROG",
				  "PFSTATE_ENFRAGPROG",
				  "PFSTATE_GPROGPARMS",
				  "PFSTATE_SHADPROG",
				  "PFSTATE_ENSHADPROG",
};

/*
 *  tables for performer in general
 */
static const int oo_table[] = {2,
			       PF_OFF,
			       PF_ON};

/*
 *  tables for materials
 */
static const int mtls_table[] = {3,
				 PFMTL_FRONT,
				 PFMTL_BACK,
				 PFMTL_BOTH};

static const int cmode_table[] = {7,
				  PFMTL_CMODE_OFF,
				  PFMTL_CMODE_AMBIENT_AND_DIFFUSE,
				  PFMTL_CMODE_AMBIENT,
				  PFMTL_CMODE_DIFFUSE,
				  PFMTL_CMODE_SPECULAR,
				  PFMTL_CMODE_EMISSION,
				  PFMTL_CMODE_COLOR};

/*
 *  tables for frustums
 */
static const int frust_table[] = {3,
				  PFFRUST_SIMPLE,
				  PFFRUST_ORTHOGONAL,
				  PFFRUST_PERSPECTIVE};


/*
 *  tables for textures
 */
static const int txr_table[] = {3,
				PFTEX_REPEAT,
				PFTEX_CLAMP,
				PFTEX_SELECT};

static const int txef_table[] = {14,
				 PFTEX_PACK_8,
				 PFTEX_PACK_16,
				 PFTEX_BYTE,
				 PFTEX_UNSIGNED_BYTE,
				 PFTEX_SHORT,
				 PFTEX_UNSIGNED_SHORT,
				 PFTEX_INT,
				 PFTEX_UNSIGNED_INT,
				 PFTEX_FLOAT,
				 PFTEX_UNSIGNED_SHORT_5_5_5_1,
				 PFTEX_COMPRESSED_RGB_S3TC_DXT1,
				 PFTEX_COMPRESSED_RGBA_S3TC_DXT1,
				 PFTEX_COMPRESSED_RGBA_S3TC_DXT3,
				 PFTEX_COMPRESSED_RGBA_S3TC_DXT5
};

static const int txif_table[] = {29,
				 PFTEX_RGB_5,
				 PFTEX_RGB_4,
				 PFTEX_RGBA_4,
				 PFTEX_I_8,
				 PFTEX_IA_12,
				 PFTEX_I_12A_4,
				 PFTEX_RGBA_8,
				 PFTEX_RGB_12,
				 PFTEX_RGBA_12,
				 PFTEX_I_16,
				 PFTEX_IA_8,
				 PFTEX_RGB5_A1,

				 PFTEX_RGBA_32,
				 PFTEX_RGB_32,
				 PFTEX_A_32,
				 PFTEX_I_32,
				 PFTEX_L_32,
				 PFTEX_IA_32,
				 PFTEX_RGBA_F16,
				 PFTEX_RGB_F16,
				 PFTEX_A_F16,
				 PFTEX_I_F16,
				 PFTEX_L_F16,
				 PFTEX_IA_F16,

				 PFTEX_COMPRESSED_RGB_S3TC_DXT1,
				 PFTEX_COMPRESSED_RGBA_S3TC_DXT1,
				 PFTEX_COMPRESSED_RGBA_S3TC_DXT3,
				 PFTEX_COMPRESSED_RGBA_S3TC_DXT5,

				 PFTEX_RGB_8    /* SCR 917917 */
};

static const uint txf_table[] = {17,
				 PFTEX_MAGFILTER,
				 PFTEX_MINFILTER,
				 PFTEX_COLOR,
				 PFTEX_ALPHA,
				 PFTEX_MIPMAP,
				 PFTEX_SHARPEN,
				 PFTEX_DETAIL,
				 PFTEX_BICUBIC,
				 PFTEX_MODULATE,
				 PFTEX_ADD,
				 PFTEX_LEQUAL,
				 PFTEX_GEQUAL,
				 PFTEX_POINT,
				 PFTEX_LINEAR,
				 PFTEX_BILINEAR,
				 PFTEX_TRILINEAR,
				 PFTEX_QUADLINEAR};

static const int txb_table[] = {3,
				PFTEX_BORDER_NONE,
				PFTEX_BORDER_COLOR,
				PFTEX_BORDER_TEXELS};

/*
 *  texture types
 */
#define TEXTYPE_TEXTURE		0
#define TEXTYPE_CLIPTEXTURE	1

#define BROKEN_SPLINE 500000.0f

static const int txll_table[] = {3,
				 PFTEX_LIST_APPLY,
				 PFTEX_LIST_AUTO_IDLE,
				 PFTEX_LIST_AUTO_SUBLOAD};

static const int txlb_table[] = {2,
				 PFTEX_BASE_APPLY,
				 PFTEX_BASE_AUTO_SUBLOAD};

static const int txls_table[] = {3,
				 PFTEX_SOURCE_IMAGE,
				 PFTEX_SOURCE_FRAMEBUFFER,
				 PFTEX_SOURCE_VIDEO};

#define COMPONMENT_MASK		0x00000f
#define TWO_BYTE_COMPONENTS	0x000010
#define FOUR_BYTE_COMPONENTS	0x000020
#define ROW_SIZE_MASK		0xffff00
#define ROW_SIZE_SHIFT		8
#define COMPONENT_ORDER_IGL	1
#define COMPONENT_ORDER_OGL	2
#define COMPONENT_ORDER		COMPONENT_ORDER_OGL

#define IS_PFI		0x00000001
#define IS_PFI_MULTI	0x00000002

/*
 *  tables for ImageTiles
 */
static const int itif_table[] = {4,
				 PFTEX_LUMINANCE,
				 PFTEX_LUMINANCE_ALPHA,
				 PFTEX_RGB,
				 PFTEX_RGBA};

#define GLOBAL_READ_QUEUE_ID -2

#define QUEUE_TO_ID(q)					\
    (							\
	(q != NULL) ?					\
	    (						\
		(q == pfGetGlobalReadQueue()) ?		\
		    GLOBAL_READ_QUEUE_ID		\
		:					\
		    find_in_list(L_QUEUE, q, glb)	\
	    )						\
	:						\
	    -1						\
    )

#define ID_TO_QUEUE(id)					\
    (							\
	(id == GLOBAL_READ_QUEUE_ID) ?			\
	    pfGetGlobalReadQueue()			\
	:						\
	    (pfQueue*)glb->rl_list[L_QUEUE][id]		\
    )


/*
 *  tables for ImageCache
 */
static const int icfnf_table[] =
{14,
    PFIMAGECACHE_TILE_FILENAMEARG_VSIZE_S,
    PFIMAGECACHE_TILE_FILENAMEARG_VSIZE_T,
    PFIMAGECACHE_TILE_FILENAMEARG_VSIZE_R,
    PFIMAGECACHE_TILE_FILENAMEARG_TILENUM_S,
    PFIMAGECACHE_TILE_FILENAMEARG_TILENUM_T,
    PFIMAGECACHE_TILE_FILENAMEARG_TILENUM_R,
    PFIMAGECACHE_TILE_FILENAMEARG_TILEORG_S,
    PFIMAGECACHE_TILE_FILENAMEARG_TILEORG_T,
    PFIMAGECACHE_TILE_FILENAMEARG_TILEORG_R,
    PFIMAGECACHE_TILE_FILENAMEARG_STREAMSERVERNAME,
    PFIMAGECACHE_TILE_FILENAMEARG_CACHENAME,
    PFIMAGECACHE_TILE_FILENAMEARG_FILENUM_S,
    PFIMAGECACHE_TILE_FILENAMEARG_FILENUM_T,
    PFIMAGECACHE_TILE_FILENAMEARG_FILENUM_R,
};

static const int ictt_table[] = {1,
				 PFTLOAD_DEST_TEXTURE};

/*
 *  tables for TexEnvs
 */
static const int tem_table[] = {6,
				PFTE_MODULATE,
				PFTE_BLEND,
				PFTE_DECAL,
				PFTE_ALPHA,
				PFTE_REPLACE,
				PFTE_ADD};

static const int tec_table[] = {8,
				PFTE_COMP_OFF,
				PFTE_COMP_I_GETS_R,
				PFTE_COMP_I_GETS_G,
				PFTE_COMP_I_GETS_B,
				PFTE_COMP_I_GETS_A,
				PFTE_COMP_IA_GETS_RG,
				PFTE_COMP_IA_GETS_BA,
				PFTE_COMP_I_GETS_I};

/*
 *  tables for Fog
 */
static const int fogt_table[] = {7,
				 PFFOG_PIX_LIN,
				 PFFOG_PIX_EXP,
				 PFFOG_PIX_EXP2,
				 PFFOG_PIX_SPLINE,
				 PFFOG_VTX_LIN,
				 PFFOG_VTX_EXP,
				 PFFOG_VTX_EXP2};

/*
 *  tables for TexGen
 */
static const int tgen_table[] = {7,
				 PFTG_OFF,
				 PFTG_OBJECT_LINEAR,
				 PFTG_EYE_LINEAR,
				 PFTG_EYE_LINEAR_IDENT,
				 PFTG_SPHERE_MAP,
				 PFTG_REFLECTION_MAP,
				 PFTG_NORMAL_MAP};

/*
 *  tables for LPointState
 */
static const int lpssm_table[] = {2,
				  PFLPS_SIZE_MODE_ON,
				  PFLPS_SIZE_MODE_OFF};

static const int lpstm_table[] = {4,
				  PFLPS_TRANSP_MODE_ON,
				  PFLPS_TRANSP_MODE_OFF,
				  PFLPS_TRANSP_MODE_ALPHA,
				  PFLPS_TRANSP_MODE_TEX};

static const int lpsfm_table[] = {4,
				  PFLPS_FOG_MODE_ON,
				  PFLPS_FOG_MODE_OFF,
				  PFLPS_FOG_MODE_ALPHA,
				  PFLPS_FOG_MODE_TEX};

static const int lpsdm_table[] = {4,
				  PFLPS_DIR_MODE_ON,
				  PFLPS_DIR_MODE_OFF,
				  PFLPS_DIR_MODE_ALPHA,
				  PFLPS_DIR_MODE_TEX};

static const int lpsshm_table[] = {3,
				   PFLPS_SHAPE_MODE_UNI,
				   PFLPS_SHAPE_MODE_BI,
				   PFLPS_SHAPE_MODE_BI_COLOR};

static const int lpsrm_table[] = {2,
				  PFLPS_RANGE_MODE_DEPTH,
				  PFLPS_RANGE_MODE_TRUE};

static const int lpsdrwm_table[] = {2,
				    PFLPS_DRAW_MODE_RASTER,
				    PFLPS_DRAW_MODE_CALLIGRAPHIC};

static const int lpsqm_table[] = {3,
				  PFLPS_QUALITY_MODE_HIGH,
				  PFLPS_QUALITY_MODE_MEDIUM,
				  PFLPS_QUALITY_MODE_LOW};

/*
 *  tables for Highlight
 */
static const uint hlm_table[] = {14,
				 PFHL_LINES,
				 PFHL_LINES_R,
				 PFHL_LINESPAT,
				 PFHL_LINESPAT2,
				 PFHL_FILL,
				 PFHL_FILLPAT,
				 PFHL_FILLPAT2,
				 PFHL_FILLTEX,
				 PFHL_FILL_R,
				 PFHL_SKIP_BASE,
				 PFHL_POINTS,
				 PFHL_NORMALS,
				 PFHL_BBOX_LINES,
				 PFHL_BBOX_FILL};


/*
 *  flags for pfFlux
 */
#define FF_PUSH			0x00000001
#define FF_ON_DEMAND		0x00000002
#define FF_COPY_LAST_DATA	0x00000004

#define FRAME_TIME_FLUX		-2

/*
 *  flags for pfFlux
 */
#define EF_RANGE_CHECK		0x00000001
#define EF_TIME_SWING		0x00000002
#define EF_MATRIX_POST_MULT	0x00000004

static const int ef_table[] = {9,
			       PFENG_SUM,
			       PFENG_MORPH,
			       PFENG_TRANSFORM,
			       PFENG_ALIGN,
			       PFENG_MATRIX,
			       PFENG_ANIMATE,
			       PFENG_BBOX,
			       PFENG_USER_FUNCTION,
			       PFENG_TIME};


/*
 *  tables for GeoSets
 */
static const int gsb_table[] = {4,
				PFGS_OFF,
				PFGS_PER_VERTEX,
				PFGS_PER_PRIM,
				PFGS_OVERALL};

static const int gspt_table[] = {11,
				 PFGS_TRISTRIPS,
				 PFGS_TRIS,
				 PFGS_POINTS,
				 PFGS_LINES,
				 PFGS_LINESTRIPS,
				 PFGS_FLAT_LINESTRIPS,
				 PFGS_QUADS,
				 PFGS_FLAT_TRISTRIPS,
				 PFGS_POLYS,
				 PFGS_TRIFANS,
				 PFGS_FLAT_TRIFANS};


/*
 *  tables for User Data
 */
static const char hex_char[16] = {'0', '1', '2', '3', '4', '5', '6', '7',
				  '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};

/*
 *  tables for String
 */
static const int strj_table[] = {3,
				 PFSTR_LEFT,
				 PFSTR_CENTER,
				 PFSTR_RIGHT};

static const int strcs_table[] = {3,
				  PFSTR_CHAR,
				  PFSTR_SHORT,
				  PFSTR_INT};

/*
 *  tables for Font
 */
static const int fontcs_table[] = {2,
				   PFFONT_CHAR_SPACING_FIXED,
				   PFFONT_CHAR_SPACING_VARIABLE};


/*
 *  tables for Nodes
 */
#define N_LIGHTPOINT	 0
#define N_TEXT		 1
#define N_GEODE		 2
#define N_BILLBOARD	 3
#define N_LIGHTSOURCE	 4
#define N_GROUP		 5
#define N_SCS		 6
#define N_DCS		 7
#define N_PARTITION	 8
#define N_SCENE		 9
#define N_SWITCH	10
#define N_LOD		11
#define N_SEQUENCE	12
#define N_LAYER		13
#define N_MORPH		14
#define N_ASD		15
#define N_FCS		16
#define N_DOUBLE_DCS	17
#define N_DOUBLE_FCS	18
#define N_DOUBLE_SCS	19
#define N_IBR_NODE	20
#define N_SUBDIV_SURFACE 21
#define N_TORUS	        22
#define N_NURB_SURFACE  23
#define N_NURB_CURVE2D  24
#define N_LINE2D	25
#define N_PIECEWISEPOLYCURVE2D 26
#define N_PIECEWISEPOLYSURFACE 27
#define N_PLANE_SURF	28
#define N_SPHERE_SURF   29
#define N_CONE          30
#define N_CYLINDER      31
#define N_LINE3D	32
#define N_NURB_CURVE3D  33
#define N_PIECEWISEPOLYCURVE3D 34
#define N_HSPLINE_SURFACE 35
#define N_SWEPT_SURFACE 36
#define N_FRENET_SWEPT_SURFACE 37
#define N_COONS         38
#define N_COMPOSITE_CURVE3D 39
#define N_CIRCLE2D	40
#define N_SUPERQUADCURVE2D 41
#define N_RULED         42
#define N_CIRCLE3D      43
#define N_SUPERQUADCURVE3D 44
#define N_ORIENTEDLINE3D 45
/* if you add anything here be sure to update the node_names array below */

#define N_CUSTOM		0x10000000
#define N_CUSTOM_MASK		0x0fff0000
#define N_NOT_CUSTOM_MASK	0x0000ffff
#define N_CUSTOM_SHIFT		16
#define N_CUSTOM_MAX		0x1000

static const char *node_names[] = {"LightPoint",
				   "Text",
				   "Geode",
				   "Billboard",
				   "LightSource",
				   "Group",
				   "SCS",
				   "DCS",
				   "Partition",
				   "Scene",
				   "Switch",
				   "LOD",
				   "Sequence",
				   "Layer",
				   "Morph",
				   "ASD",
				   "FCS",
				   "DoubleDCS",
				   "DoubleFCS",
				   "DoubleSCS",
				   "IBRnode",
				   "SubdivSurface",
				   "TorusSurface",
				   "NurbsSurface",
				   "NurbCurve2d",
				   "Line2d", 
				   "PieceWisePolyCurve2d",
				   "PieceWisePolySurface",
				   "PlaneSurface",
				   "SphereSurface",
				   "ConeSurface",
				   "CylinderSurface",
				   "Line3d",
				   "NurbCurve3d",
				   "PieceWisePolyCurve3d",
				   "HsplineSurface",
				   "SweptSurface",
				   "FrenetSweptSurface",
				   "CoonsSurface",
				   "CompositeCurve3d",				  
				   "Circle2d",
				   "SuperQuadCurve2d",
				   "RuledSurface",
				   "Circle3d",
				   "SuperQuadCurve3d",
				   "OrientedLine3d",
};

/* 
 * Scalar types.
 * Keep this and scalar_names array in sync along with S_COUNT
 *
 */
#define S_COMPOSITE 0
#define S_COS       1
#define S_SIN       2
#define S_POLY      3
#define S_COUNT     4 /* # of scalar types */

static const char *scalar_names[] = {"Composite Scalar",
				     "Cos Scalar",
				     "Sin Scalar",
				     "Poly Scalar",
};

static const int bound_table[] = {2,
				  PFBOUND_STATIC,
				  PFBOUND_DYNAMIC};

#define CS_UNCONSTRAINED 0xffffffff

static const uint mat_table[] = {7,
				 PFMAT_TRANS,
				 PFMAT_ROT,
				 PFMAT_SCALE,
				 PFMAT_NONORTHO,
				 PFMAT_PROJ,
				 PFMAT_HOM_SCALE,
				 PFMAT_MIRROR};

#define	PFB_SWITCH_ON	-1
#define	PFB_SWITCH_OFF	-2

static const int sqi_table[] = {2,
				PFSEQ_CYCLE,
				PFSEQ_SWING};

static const int sqm_table[] = {4,
				PFSEQ_STOP,
				PFSEQ_START,
				PFSEQ_PAUSE,
				PFSEQ_RESUME};


static const int layer_table[] = {12,
				  PFDECAL_OFF,
				  PFDECAL_LAYER,
				  PFDECAL_BASE,
				  PFDECAL_BASE_FAST,
				  PFDECAL_BASE_HIGH_QUALITY,
				  PFDECAL_BASE_STENCIL,
				  PFDECAL_BASE_DISPLACE,
				  PFDECAL_LAYER_FAST,
				  PFDECAL_LAYER_HIGH_QUALITY,
				  PFDECAL_LAYER_STENCIL,
				  PFDECAL_LAYER_DISPLACE,
				  PFDECAL_LAYER_DISPLACE_AWAY};

static const int bbr_table[] = {3,
				PFBB_AXIAL_ROT,
				PFBB_POINT_ROT_EYE,
				PFBB_POINT_ROT_WORLD};

#define BB_POS_FLUX	-2

#define MORPH_ATTR_COLOR4	1
#define MORPH_ATTR_NORMAL3	2
#define MORPH_ATTR_TEXCOORD2	3
#define MORPH_ATTR_COORD3	4
#define MORPH_ATTR_OTHER	5
#define MORPH_ATTR_MISSING	-1

#define ASD_ATTR_NORMALS	0x1
#define ASD_ATTR_COLORS		0x2
#define ASD_ATTR_TCOORDS	0x4

static const int asd_em_table[] = {3,
				   PFASD_DIST,
				   PFASD_DIST_SQUARE,
				   PFASD_CALLBACK};


/*
 *  Tables Names
 *  These are used when an item is not found in a table.
 */
static const struct {
    const void *addr;
    char *name;
} table_names[] =
{
    {aa_table, "Anti-Alias State"},
    {af_table, "Alpha Function State"},
    {cf_table, "Cull Face State"},
    {decal_table, "Decal State"},
    {tr_table, "Transparency State"},
    {gst_table, "GeoState"},
    {oo_table, "PF_ON/PF_OFF"},
    {mtls_table, "Material Side"},
    {cmode_table, "Material Cmode"},
    {frust_table, "Frustum Type"},
    {txr_table, "Texture Repeat"},
    {txef_table, "Texture External Format"},
    {txif_table, "Texture Internal Format"},
    {txf_table, "Texture Filter"},
    {txb_table, "Texture Border Type"},
    {txll_table, "Texture Load List Mode"},
    {txlb_table, "Texture Load Base Mode"},
    {txls_table, "Texture Load Source Mode"},
    {tem_table, "Texture Environment Mode"},
    {tec_table, "Texture Environment Component"},
    {fogt_table, "Fog Type"},
    {tgen_table, "Texture Generation Mode"},
    {lpssm_table, "Light Point State Size Mode"},
    {lpstm_table, "Light Point State Transperency Mode"},
    {lpsfm_table, "Light Point State Fog Mode"},
    {lpsdm_table, "Light Point State Direction Mode"},
    {lpsshm_table, "Light Point State Shape Mode"},
    {lpsrm_table, "Light Point State Range Mode"},
    {hlm_table, "Highlight mode"},
    {gsb_table, "GeoSet Attrbute Binding"},
    {gspt_table, "GeoSet Primitive Type"},
    {strj_table, "String Justify Mode"},
    {strcs_table, "String Character Size Mode"},
    {fontcs_table, "Font Character Spacing"},
    {mat_table, "Matrix Type"},
    {sqi_table, "Sequence Interval"},
    {sqm_table, "Sequence Mode"},
    {layer_table, "Layer Mode"},
    {bbr_table, "Billboard Mode"},
    {asd_em_table, "ASD Eval Method"},
    {icfnf_table, "Image Cache File Name Format"},
    {ictt_table, "Image Cache Texture Type"},
    {NULL, "Unknown"},
};


#define COMMENT(comment) (glb->add_comments ? (comment) : "")


#define READ_LINE()		\
if (!read_line(glb))		\
{				\
    glb->read_error = 1;	\
    return(NULL);		\
}

#define READ_ERROR()		\
{				\
    glb->read_error = 1;	\
    return(NULL);		\
}


#define GET_BUF(s) (((s) <= glb->buf_size)? glb->buf : set_buf_size((s), glb))

/*
 *  To be MP safe when globals are shared the pfb/pfa file format uses few
 *  non-constant globals.  Those that are used are truely global such as
 *  lists of shared state.  Transient globals that are used in loading or
 *  storing a single file are all stored in the following type of structure.
 *  This structure is malloced with each load or store and passed to all
 *  functions that need it.
 */
typedef struct {
    list_t *l_list[L_COUNT];
    list_t *l_list_end[L_COUNT];
    list_t **buckets[L_COUNT];
    int bucket_mask[L_COUNT];
    int l_num[L_COUNT];
    int add_comments;
    void *arena;
    FILE *ifp, *ofp;
    int version;
    int line_num;
    char *line_buf;
    int buf_size;
    int *buf;
    int read_error;
    void **rl_list[L_COUNT];
    int **children;
    int *hlight_gstates;
    morph_list_t *morph_head, *morph_tail;
    void **unknown;
    int unknown_size;
    int unknown_count;
    uint *crypt_key;
    pfdDecryptFuncType_pfb decrypt_func;
    pfdEncryptFuncType_pfb encrypt_func;
    udata_func_t *udfunc;
    int num_udata_slots;
    int max_udata_slot;
    int data_total;
    udata_info_t *udi;
    udata_fix_t *udf;
    int udi_size;
    custom_node_t *custom_nodes;
    int num_custom_nodes;
    int save_texture_image;
    int save_texture_path;
    int save_texture_pfi;
    int disable_rep_save;
    int subdivsurface_save_geosets;
    int dont_load_user_funcs;
    int share_gs_objects;
    pfdShare *share;
    int compile_gl;
    int surface_count;
    pfTessParaSurfaceAction *tessAction;
    pfList *topo_map, *solid_map;
    int maxTextures;
    pfList *boundaryList;
} pfa_global_t;

typedef struct {
  pfObject *object;
  int originalId;
} object_id_map_t;

/*
 *  local prototypes
 */
static int read_line(pfa_global_t *glb);
static int *set_buf_size(int size, pfa_global_t *glb);
static void *pfa_read_mtl(pfa_global_t *glb);
static void *pfb_read_mtl(pfa_global_t *glb);
static void *pfa_read_lmodel(pfa_global_t *glb);
static void *pfb_read_lmodel(pfa_global_t *glb);
static void *pfa_read_light(pfa_global_t *glb);
static void *pfb_read_light(pfa_global_t *glb);
static void *pfa_read_lsource(pfa_global_t *glb);
static void *pfb_read_lsource(pfa_global_t *glb);
static void *pfa_read_frust(pfa_global_t *glb);
static void *pfb_read_frust(pfa_global_t *glb);
static void *pfa_read_tex(pfa_global_t *glb);
static void *pfa_read_ibr_tex(pfa_global_t *glb);
static void *pfb_read_tex(pfa_global_t *glb);
static void *pfb_read_ibr_tex(pfa_global_t *glb);
static void *pfa_read_mesh(pfa_global_t *glb);
static void *pfb_read_mesh(pfa_global_t *glb);
static void *pfa_read_tenv(pfa_global_t *glb);
static void *pfb_read_tenv(pfa_global_t *glb);
static void *pfa_read_fog(pfa_global_t *glb);
static void *pfb_read_fog(pfa_global_t *glb);
static void *pfa_read_tgen(pfa_global_t *glb);
static void *pfb_read_tgen(pfa_global_t *glb);
static void *pfa_read_tlod(pfa_global_t *glb);
static void *pfb_read_tlod(pfa_global_t *glb);
static void *pfa_read_gprogram(pfa_global_t *glb);
static void *pfb_read_gprogram(pfa_global_t *glb);
static void *pfa_read_shader_object(pfa_global_t *glb);
static void *pfb_read_shader_object(pfa_global_t *glb);
static void *pfa_read_shader_program(pfa_global_t *glb);
static void *pfb_read_shader_program(pfa_global_t *glb);
static void *pfa_read_gprogramparms(pfa_global_t *glb);
static void *pfb_read_gprogramparms(pfa_global_t *glb);
static void *pfa_read_ctab(pfa_global_t *glb);
static void *pfb_read_ctab(pfa_global_t *glb);
static void *pfa_read_lpstate(pfa_global_t *glb);
static void *pfb_read_lpstate(pfa_global_t *glb);
static void *pfa_read_hlight(int hl_num, pfa_global_t *glb);
static void *pfb_read_hlight(int hl_num, pfa_global_t *glb);
static void *pfa_read_gstate(pfa_global_t *glb);
static void *pfb_read_gstate(pfa_global_t *glb);

static void *pfa_read_appearance(pfa_global_t *glb);
static void *pfb_read_appearance(pfa_global_t *glb);

static void *pfa_read_slist_header(void **data, int unit_size, int *size,
				   pfa_global_t *glb);
static void *pfb_read_slist_header(void **data, int unit_size, int *size,
				   pfa_global_t *glb);
static void *pfa_read_llist(pfa_global_t *glb);
static void *pfb_read_llist(pfa_global_t *glb);
static void *pfa_read_vlist(pfa_global_t *glb);
static void *pfb_read_vlist(pfa_global_t *glb);
static void *pfa_read_clist(pfa_global_t *glb);
static void *pfb_read_clist(pfa_global_t *glb);
static void *pfa_read_nlist(pfa_global_t *glb);
static void *pfb_read_nlist(pfa_global_t *glb);
static void *pfa_read_tlist(pfa_global_t *glb);
static void *pfb_read_tlist(pfa_global_t *glb);
static void *pfa_read_ilist(pfa_global_t *glb);
static void *pfb_read_ilist(pfa_global_t *glb);
static void *pfa_read_flist(pfa_global_t *glb);
static void *pfb_read_flist(pfa_global_t *glb);
static void *pfa_read_asddata(pfa_global_t *glb);
static void *pfb_read_asddata(pfa_global_t *glb);
static void *pfa_read_flux(pfa_global_t *glb);
static void *pfb_read_flux(pfa_global_t *glb);
static void *pfa_read_sg_name(pfa_global_t *glb);
static void *pfb_read_sg_name(pfa_global_t *glb);
static void *pfa_read_shader(pfa_global_t *glb);
static void *pfb_read_shader(pfa_global_t *glb);
static void *pfa_read_engine(pfa_global_t *glb);
static void *pfb_read_engine(pfa_global_t *glb);
static void *pfa_read_gset(pfa_global_t *glb);
static void *pfb_read_gset(pfa_global_t *glb);
static void *pfa_read_geoArray(pfa_global_t *glb);
static void *pfb_read_geoArray(pfa_global_t *glb);
static void *pfa_read_varray_1(pfa_global_t *glb);
static void *pfb_read_varray_1(pfa_global_t *glb);
static void *pfa_read_varray_2(pfa_global_t *glb);
static void *pfb_read_varray_2(pfa_global_t *glb);
static void *pfa_read_varray_4(pfa_global_t *glb);
static void *pfb_read_varray_4(pfa_global_t *glb);
static void *pfa_read_topo(pfa_global_t *glb);
static void *pfb_read_topo(pfa_global_t *glb);
static void *pfa_read_scalar(pfa_global_t *glb);
static void *pfb_read_scalar(pfa_global_t *glb);
static void *pfa_read_curve2d_general(pfa_global_t *glb);
static void *pfb_read_curve2d_general(pfa_global_t *glb);
static void *pfa_read_discurve2d(pfa_global_t *glb);
static void *pfb_read_discurve2d(pfa_global_t *glb);
static void *pfa_read_udata(int udata_num, pfa_global_t *glb);
static void *pfb_read_udata(int udata_num, pfa_global_t *glb);
static void *pfa_read_udata_list(pfa_global_t *glb);
static void *pfb_read_udata_list(pfa_global_t *glb);
static void *pfa_read_udata_name(pfa_global_t *glb);
static void *pfb_read_udata_name(pfa_global_t *glb);
static void *pfa_read_ufunc(pfa_global_t *glb);
static void *pfa_load_ufunc(FILE *ifp);
static void *pfb_read_ufunc(pfa_global_t *glb);
static void *pfb_load_ufunc(FILE *ifp);
static void *find_ufunc(char *name, char *dso_name, const char *from);
static void *pfa_read_image(pfa_global_t *glb);
static void *pfb_read_image(pfa_global_t *glb);
static uint *flip_components(uint *image, int size, int num_comp, int comp_size,
			     int row_size);
static void *pfa_read_itile(pfa_global_t *glb);
static void *pfb_read_itile(pfa_global_t *glb);
static void *pfa_read_icache(pfa_global_t *glb);
static void *pfb_read_icache(pfa_global_t *glb);
static void *pfa_read_queue(pfa_global_t *glb);
static void *pfb_read_queue(pfa_global_t *glb);
static void *pfa_read_morph(pfa_global_t *glb);
static void *pfb_read_morph(pfa_global_t *glb);
static void *pfa_read_lodstate(pfa_global_t *glb);
static void *pfb_read_lodstate(pfa_global_t *glb);
static void *pfa_read_font(pfa_global_t *glb);
static void *pfb_read_font(pfa_global_t *glb);
static void *pfa_read_string(pfa_global_t *glb);
static void *pfb_read_string(pfa_global_t *glb);
static void *pfa_read_node(int node_num, pfa_global_t *glb);
static void *pfb_read_node(int node_num, pfa_global_t *glb);
static void *pfa_read_custom(pfa_global_t *glb);
static void *pfb_read_custom(pfa_global_t *glb);
static void connect_nodes(pfa_global_t *glb);
static void descend_node(pfNode *node, pfa_global_t *glb);
static void descend_gset(pfGeoSet *gset, pfa_global_t *glb);
static void descend_geoArray(pfGeoArray *ga, pfa_global_t *glb);
static void descend_gstate(pfGeoState *gstate, pfa_global_t *glb);
static void descend_appearance(islAppearance *a, pfa_global_t *glb);
static void descend_hlight(pfHighlight *hlight, pfa_global_t *glb);
static void descend_lpstate(pfLPointState *lpstate, pfa_global_t *glb);
static void descend_tex(pfTexture *tex, pfa_global_t *glb);
static void descend_ibr_tex(pfIBRtexture *ibrtex, pfa_global_t *glb);
static void descend_parasurface(pfParaSurface *surf,pfa_global_t *glb);
static void descend_mesh(pfMesh*mesh, pfa_global_t *glb);
static void descend_icache(pfImageCache *icache, pfa_global_t *glb);
static void descend_itile(pfImageTile *itile, pfa_global_t *glb);
static void descend_string(pfString *string, pfa_global_t *glb);
static void descend_font(pfFont *font, pfa_global_t *glb);
static void descend_flux(pfFlux *flux, pfa_global_t *glb);
static void descend_engine(pfEngine *engine, pfa_global_t *glb);
static void descend_ufunc(void *ufunc, const char *what, pfa_global_t *glb);
static void descend_unknown_data(void *data, pfa_global_t *glb);
static void really_descend_unknown_datas(pfa_global_t *glb);
static void descend_udata_list(int count, void *parent, pfa_global_t *glb);
static void descend_udata(void *udata, int slot, void *parent,
			  pfa_global_t *glb);
static void set_udata_info(int type, int list, int slot, void *parent,
			   pfa_global_t *glb);
static void set_trav_data(pfNode *node, int which, int udata_id,
			  pfa_global_t *glb);
static void set_raster_func(pfLPointState *lpstate, void *func, int udata_id,
			    pfa_global_t *glb);
static void set_callig_func(pfLPointState *lpstate, void *func, int udata_id,
			    pfa_global_t *glb);
static int descend_unknown(void *data, pfType *type, pfa_global_t *glb);
static void descend_morph(pfMorph *morph, pfa_global_t *glb);
static void really_descend_morphs(pfa_global_t *glb);
static morph_list_t *find_morph(pfMorph *morph, pfa_global_t *glb);
static void free_morph_list(pfa_global_t *glb);
static void pfa_write_mtl(pfMaterial *mtl, pfa_global_t *glb);
static void pfb_write_mtl(pfMaterial *mtl, pfa_global_t *glb);
static void pfa_write_lmodel(pfLightModel *lmodel, pfa_global_t *glb);
static void pfb_write_lmodel(pfLightModel *lmodel, pfa_global_t *glb);
static void pfa_write_light(pfLight *light, pfa_global_t *glb);
static void pfb_write_light(pfLight *light, pfa_global_t *glb);
static void pfa_write_lsource(pfLightSource *lsource, pfa_global_t *glb);
static void pfb_write_lsource(pfLightSource *lsource, pfa_global_t *glb);
static void pfa_write_frust(pfFrustum *frust, pfa_global_t *glb);
static void pfb_write_frust(pfFrustum *frust, pfa_global_t *glb);
static int save_tex_image(pfTexture*tex, const char **name_ptr,
			  pfa_global_t *glb);
static int save_tex_multi_image(pfTexture*tex, const char **name_ptr, 
				int imageIndex, pfa_global_t *glb);
static void pfa_write_tex(pfTexture *tex, pfa_global_t *glb);
static void pfa_write_ibr_tex(pfIBRtexture *tex, pfa_global_t *glb);
static void pfb_write_tex(pfTexture *tex, pfa_global_t *glb);
static void pfb_write_ibr_tex(pfIBRtexture *tex, pfa_global_t *glb);
static void pfa_write_mesh(pfMesh *mesh, pfa_global_t *glb);
static void pfb_write_mesh(pfMesh *mesh, pfa_global_t *glb);
static void pfa_write_rep(pfRep *rep,pfa_global_t *glb);
static void pfb_write_rep(pfRep *rep,pfa_global_t *glb,int *oldPos);
static void pfa_write_curve2d(pfCurve2d *curve,pfa_global_t *glb);
static void pfb_write_curve2d(pfCurve2d *curve,pfa_global_t *glb,int *oldPos);
static void pfa_write_discurve2d(pfDisCurve2d *curve,pfa_global_t *glb);
static void pfb_write_discurve2d(pfDisCurve2d *curve,pfa_global_t *glb);
static void pfa_write_curve2d_general(pfCurve2d *curve,pfa_global_t *glb);
static void pfb_write_curve2d_general(pfCurve2d *curve,pfa_global_t *glb);
static void pfa_write_topo(pfTopo *topo,pfa_global_t *glb);
static void pfb_write_topo(pfTopo *topo,pfa_global_t *glb);
static void pfa_write_scalar(pfScalar *scalar,pfa_global_t *glb);
static void pfb_write_scalar(pfScalar *scalar,pfa_global_t *glb);
static void* pfa_read_line2d(pfa_global_t *glb,pfLine2d *line);
static int pfb_read_line2d(pfa_global_t *glb,pfLine2d *line);
static void* pfa_read_circle2d(pfa_global_t *glb,pfCircle2d *circle);
static int pfb_read_circle2d(pfa_global_t *glb,pfCircle2d *circle);
static void* pfa_read_super_quad_curve2d(pfa_global_t *glb,pfSuperQuadCurve2d *c);
static int pfb_read_super_quad_curve2d(pfa_global_t *glb,pfSuperQuadCurve2d *c);
static void* pfa_read_piecewisepolycurve2d(pfa_global_t *glb,pfPieceWisePolyCurve2d *curve);
static int pfb_read_piecewisepolycurve2d(pfa_global_t *glb,pfPieceWisePolyCurve2d *curve);
static void pfa_write_tenv(pfTexEnv *tenv, pfa_global_t *glb);
static void pfb_write_tenv(pfTexEnv *tenv, pfa_global_t *glb);
static void pfa_write_fog(pfFog *fog, pfa_global_t *glb);
static void pfb_write_fog(pfFog *fog, pfa_global_t *glb);
static void pfa_write_tgen(pfTexGen *tgen, pfa_global_t *glb);
static void pfb_write_tgen(pfTexGen *tgen, pfa_global_t *glb);
static void pfa_write_tlod(pfTexLOD *tlod, pfa_global_t *glb);
static void pfb_write_tlod(pfTexLOD *tlod, pfa_global_t *glb);
static void pfa_write_gprogram(pfGProgram *gp, pfa_global_t *glb);
static void pfb_write_gprogram(pfGProgram *gp, pfa_global_t *glb);
static void pfa_write_gprogramparms(pfGProgramParms *gp, pfa_global_t *glb);
static void pfb_write_gprogramparms(pfGProgramParms *gp, pfa_global_t *glb);
static void pfa_write_shader_object(pfShaderObject *so, pfa_global_t *glb);
static void pfb_write_shader_object(pfShaderObject *so, pfa_global_t *glb);
static void pfa_write_shader_program(pfShaderProgram *sp, pfa_global_t *glb);
static void pfb_write_shader_program(pfShaderProgram *sp, pfa_global_t *glb);
static void pfa_write_ctab(pfColortable *ctab, pfa_global_t *glb);
static void pfb_write_ctab(pfColortable *ctab, pfa_global_t *glb);
static void pfa_write_lpstate(pfLPointState *lpstate, pfa_global_t *glb);
static void pfb_write_lpstate(pfLPointState *lpstate, pfa_global_t *glb);
static void pfa_write_hlight(pfHighlight *hlight, pfa_global_t *glb);
static void pfb_write_hlight(pfHighlight *hlight, pfa_global_t *glb);
static void pfa_write_gstate(pfGeoState *gstate, pfa_global_t *glb);
static void pfb_write_gstate(pfGeoState *gstate, pfa_global_t *glb);
static void pfa_write_appearance(void *a, size_t size, pfa_global_t *glb);
static void pfb_write_appearance(void *a, size_t size, pfa_global_t *glb);
static int pfa_write_slist_header(void *data, int size, pfa_global_t *glb);
static int pfb_write_slist_header(void *data, int size, pfa_global_t *glb);
static void pfa_write_llist(int *llist, int size, pfa_global_t *glb);
static void pfb_write_llist(int *llist, int size, pfa_global_t *glb);
static void pfa_write_vlist(pfVec3 *vlist, int size, pfa_global_t *glb);
static void pfb_write_vlist(pfVec3 *vlist, int size, pfa_global_t *glb);
static void pfa_write_clist(pfVec4 *clist, int size, pfa_global_t *glb);
static void pfb_write_clist(pfVec4 *clist, int size, pfa_global_t *glb);
static void pfa_write_nlist(pfVec3 *nlist, int size, pfa_global_t *glb);
static void pfb_write_nlist(pfVec3 *nlist, int size, pfa_global_t *glb);
static void pfa_write_tlist(pfVec2 *tlist, int size, pfa_global_t *glb);
static void pfb_write_tlist(pfVec2 *tlist, int size, pfa_global_t *glb);
static void pfa_write_ilist(ushort *ilist, int size, pfa_global_t *glb);
static void pfb_write_ilist(ushort *ilist, int size, pfa_global_t *glb);
static void pfa_write_flist(float *flist, int size, pfa_global_t *glb);
static void pfb_write_flist(float *flist, int size, pfa_global_t *glb);
static void pfa_write_asddata(void *asddata, int size, pfa_global_t *glb);
static void pfb_write_asddata(void *asddata, int size, pfa_global_t *glb);
static void pfa_write_flux(pfFlux *flux, pfa_global_t *glb);
static void pfb_write_flux(pfFlux *flux, pfa_global_t *glb);
static void pfa_write_sg_name(char *name, pfa_global_t *glb);
static void pfb_write_sg_name(char *name, pfa_global_t *glb);
static void pfa_write_engine(pfEngine *engine, pfa_global_t *glb);
static void pfb_write_engine(pfEngine *engine, pfa_global_t *glb);
static void pfa_write_gset(pfGeoSet *gset, pfa_global_t *glb);
static void pfb_write_gset(pfGeoSet *gset, pfa_global_t *glb);
static void pfa_write_geoArray(pfGeoArray *ga, pfa_global_t *glb);
static void pfb_write_geoArray(pfGeoArray *ga, pfa_global_t *glb);
static void pfa_write_varray_1(char *va, int size, pfa_global_t *glb);
static void pfb_write_varray_1(char *va, int size, pfa_global_t *glb);
static void pfa_write_varray_2(short *va, int size, pfa_global_t *glb);
static void pfb_write_varray_2(short *va, int size, pfa_global_t *glb);
static void pfa_write_varray_4(int *va, int size, pfa_global_t *glb);
static void pfb_write_varray_4(int *va, int size, pfa_global_t *glb);
static void pfa_write_udata(void *udata, pfa_global_t *glb);
static void pfb_write_udata(void *udata, pfa_global_t *glb);
static void pfa_write_udata_list(pfObject *parent, pfa_global_t *glb);
static void pfb_write_udata_list(pfObject *parent, pfa_global_t *glb);
static void pfa_write_udata_name(char *name, pfa_global_t *glb);
static void pfb_write_udata_name(char *name, pfa_global_t *glb);
static void pfa_write_ufunc(void *ufunc, pfa_global_t *glb);
static void pfb_write_ufunc(void *ufunc, pfa_global_t *glb);
static void pfa_write_image(uint *image, int comp, pfa_global_t *glb);
static void pfb_write_image(uint *image, int comp, pfa_global_t *glb);
static void pfa_write_itile(pfImageTile *itile, pfa_global_t *glb);
static void pfb_write_itile(pfImageTile *itile, pfa_global_t *glb);
static void pfa_write_icache(pfImageCache *icache, pfa_global_t *glb);
static void pfb_write_icache(pfImageCache *icache, pfa_global_t *glb);
static void pfa_write_queue(pfQueue *queue, pfa_global_t *glb);
static void pfb_write_queue(pfQueue *queue, pfa_global_t *glb);
static void pfa_write_morph(pfMorph *morph, pfa_global_t *glb);
static void pfb_write_morph(pfMorph *morph, pfa_global_t *glb);
static void pfa_write_lodstate(pfLODState *ls, pfa_global_t *glb);
static void pfb_write_lodstate(pfLODState *ls, pfa_global_t *glb);
static void pfa_write_font(pfFont *font, pfa_global_t *glb);
static void pfb_write_font(pfFont *font, pfa_global_t *glb);
static void pfa_write_string(pfString *string, pfa_global_t *glb);
static void pfb_write_string(pfString *string, pfa_global_t *glb);
static void pfa_write_node(pfNode *node, int custom, pfa_global_t *glb);
static void pfb_write_node(pfNode *node, int custom, pfa_global_t *glb);
static void pfa_write_name(const char *s, pfa_global_t *glb);
static void pfb_write_name(const char *s, pfa_global_t *glb);
static void pfa_write_custom_list(pfa_global_t *glb);
static void pfb_write_custom_list(pfa_global_t *glb);
static void free_load_data(pfa_global_t *glb);
static void free_store_data(pfa_global_t *glb);
static int find_in_list(int id, void *data, pfa_global_t *glb);
static list_t *find_list_item(int id, void *data, pfa_global_t *glb);
static int find_in_unknown_list(void *data, int *list, pfa_global_t *glb);
static int find_data_list(void *data, pfa_global_t *glb);
static int add_to_list(int id, void *data, pfa_global_t *glb);
static int add_to_slist(int id, void *data, int size, pfa_global_t *glb);
static void double_buckets(int id, pfa_global_t *glb);
static int find_in_table(const int *table, int item);
static uint to_table(const uint *table, uint src);
static uint from_table(const uint *table, uint src);
static pfObject *share_obj(pfObject *obj, pfa_global_t *glb);
static pfTexture *share_tex(tex_list_t *tl, pfa_global_t *glb);
static void free_tex_list_t(tex_list_t *tl);
static pfTexture *make_tex(tex_list_t *tl, pfa_global_t *glb);
static void set_pfi_multi(pfTexture *tex, tex_list_t *tl);
static int mem_diff(uint *p1, uint *p2, size_t size);
static void pfb_decrypt(int size, uint *key, void *data);
static void pfb_encrypt(int size, uint *key, void *src, void *dst);
static int get_udata(pfObject *obj, pfa_global_t *glb);
static void set_udata(pfObject *obj, int udata_id, pfa_global_t *glb);
static void set_udata_slot(pfObject *obj, int slot, int udata_id,
			   pfa_global_t *glb);
static void connect_udata(pfa_global_t *glb);
static udata_func_t *copy_udfunc(udata_func_t *udfunc);
static int is_custom_node(pfNode *node, pfa_global_t *glb);
static int is_pfi_tex(pfTexture *tex);

/*
 *  True globals
 */
static int share_gs_objects = PF_ON;
static int compile_gl = PFPFB_COMPILE_GL_OFF;
static tex_list_t *tex_list = NULL;
static uint *crypt_key_pfb = NULL;
static pfdDecryptFuncType_pfb decrypt_func_pfb;
static pfdEncryptFuncType_pfb encrypt_func_pfb;
static udata_func_t *udfunc_pfa;
static udata_func_t *udfunc_pfb;
static int save_texture_image = PF_OFF;
static int save_texture_path = PF_OFF;
static int save_texture_pfi = PF_OFF;
static int dont_load_user_funcs = PF_OFF;
static int disable_rep_save = PF_OFF;
static int subdivsurface_save_geosets = PF_OFF;
static custom_node_t *custom_nodes_pfb = NULL;
static int num_custom_nodes_pfb = 0;
static custom_node_t *custom_nodes_pfa = NULL;
static int num_custom_nodes_pfa = 0;
static pfTessParaSurfaceAction *tessAction = NULL;


#define PFA_DUMMY_READ pfa_read_gset
static void *(*pfa_read_func[L_COUNT])(pfa_global_t *glb) =
{
    pfa_read_mtl,
    pfa_read_tex,
    pfa_read_tenv,
    pfa_read_gstate,
    pfa_read_llist,
    pfa_read_vlist,
    pfa_read_clist,
    pfa_read_nlist,
    pfa_read_tlist,
    pfa_read_ilist,
    pfa_read_gset,
    PFA_DUMMY_READ,
    PFA_DUMMY_READ,
    pfa_read_flist,
    pfa_read_morph,
    pfa_read_lodstate,
    pfa_read_fog,
    pfa_read_tgen,
    pfa_read_lmodel,
    pfa_read_light,
    pfa_read_ctab,
    pfa_read_lpstate,
    PFA_DUMMY_READ,
    pfa_read_lsource,
    pfa_read_frust,
    pfa_read_font,
    pfa_read_string,
    pfa_read_image,
    pfa_read_custom,
    pfa_read_tlod,
    pfa_read_asddata,
    pfa_read_queue,
    pfa_read_itile,
    pfa_read_icache,
    pfa_read_flux,
    pfa_read_engine,
    pfa_read_ufunc,
    pfa_read_udata_name,
    pfa_read_udata_list,
    pfa_read_sg_name,
    pfa_read_shader,
    pfa_read_ibr_tex,
    pfa_read_appearance,
    pfa_read_mesh,
    pfa_read_geoArray,
    pfa_read_varray_1,
    pfa_read_varray_2,
    pfa_read_varray_4,
    PFA_DUMMY_READ, /* XXX Alex was for edge */
    pfa_read_topo,
    pfa_read_curve2d_general,
    pfa_read_discurve2d,
    PFA_DUMMY_READ, /* XXX Alex was for boundary */
    PFA_DUMMY_READ, /* XXX Alex was for solid */
    pfa_read_gprogram,
    pfa_read_gprogramparms,
    pfa_read_scalar,
    pfa_read_shader_object,
    pfa_read_shader_program,
};

#define PFB_DUMMY_READ pfb_read_gset
static void *(*pfb_read_func[L_COUNT])(pfa_global_t *glb) =
{
    pfb_read_mtl,
    pfb_read_tex,
    pfb_read_tenv,
    pfb_read_gstate,
    pfb_read_llist,
    pfb_read_vlist,
    pfb_read_clist,
    pfb_read_nlist,
    pfb_read_tlist,
    pfb_read_ilist,
    pfb_read_gset,
    PFA_DUMMY_READ,
    PFB_DUMMY_READ,
    pfb_read_flist,
    pfb_read_morph,
    pfb_read_lodstate,
    pfb_read_fog,
    pfb_read_tgen,
    pfb_read_lmodel,
    pfb_read_light,
    pfb_read_ctab,
    pfb_read_lpstate,
    PFB_DUMMY_READ,
    pfb_read_lsource,
    pfb_read_frust,
    pfb_read_font,
    pfb_read_string,
    pfb_read_image,
    pfb_read_custom,
    pfb_read_tlod,
    pfb_read_asddata,
    pfb_read_queue,
    pfb_read_itile,
    pfb_read_icache,
    pfb_read_flux,
    pfb_read_engine,
    pfb_read_ufunc,
    pfb_read_udata_name,
    pfb_read_udata_list,
    pfb_read_sg_name,
    pfb_read_shader,
    pfb_read_ibr_tex,
    pfb_read_appearance,
    pfb_read_mesh,
    pfb_read_geoArray,
    pfb_read_varray_1,
    pfb_read_varray_2,
    pfb_read_varray_4,
    PFB_DUMMY_READ, /* XXX Alex  was for edge */
    pfb_read_topo,
    pfb_read_curve2d_general,
    pfb_read_discurve2d,
    PFB_DUMMY_READ, /* XXX Alex was for boundary */
    PFB_DUMMY_READ, /* XXX Alex was for solid */
    pfb_read_gprogram,
    pfb_read_gprogramparms,
    pfb_read_scalar,
    pfb_read_shader_object,
    pfb_read_shader_program,
};

PFPFB_DLLEXPORT
void pfdSetTessellationAction(pfTessParaSurfaceAction *action)
{
  if(action == NULL)
    return;

  if(action != tessAction) {
    pfRef(action);
    tessAction = action;
  } /* else already set */
}

PFPFB_DLLEXPORT
pfTessParaSurfaceAction *pfdGetTessellationAction()
{
  return tessAction;
}

static void tessellate_root(pfNode *root,pfa_global_t *glb)
{
  char *p = NULL;
  int createdAction = 0;
  float oldTessValue = -1.0;

  if(root == NULL) /* display warning ... shouldn't be NULL if we get 
		      here because surface count is > 0 */
    return;

  p = getenv("PF_DEFAULT_TESS_VALUE");

  /* if PF_DEFAULT_TESS_VALUE is set to 'no', or any value <= 0.0 then
   * surfaces upon load.
   */
  if(p) {
    if(!strcmp(p,"no") || atof(p) <= 0.0)
      return;
  }
  
  if(glb->tessAction == NULL) {
    glb->tessAction = pfNewTessParaSurfaceAction(glb->arena);
    if(glb->tessAction == NULL) {
      pfNotify(PFNFY_WARN,PFNFY_PRINT,
	       "Unable to create new pfTessParaSurfaceAction.");
      pfNotify(PFNFY_WARN,PFNFY_MORE,"Skipping tessellation step.");
      return;
    }
    createdAction = 1;
  }

  pfNotify(PFNFY_INFO,PFNFY_PRINT,"Tessellating root (%d surfaces)",
	   glb->surface_count);

  if(p) {
    oldTessValue = pfGetTessParaSurfaceActionChordalDevTol(glb->tessAction);
    pfTessParaSurfaceActionChordalDevTol(glb->tessAction, atof(p));
  }

  pfNotify(PFNFY_INFO,PFNFY_MORE,"Chordal Dev Tol = %g",
	   pfGetTessParaSurfaceActionChordalDevTol(glb->tessAction));

  pfdTessellateGeometry(root, glb->tessAction);

  /* reset old value, used if we didn't create the action */
  if(p && oldTessValue != -1.0)
    pfTessParaSurfaceActionChordalDevTol(glb->tessAction, oldTessValue);

  /* XXX Alex -- memory leak ... */
  if(createdAction)
    ; /* pfUnref(glb->tessAction); */
}

PFPFB_DLLEXPORT pfNode* pfdLoadFile_pfa(const char *fileName)
{
    pfNode *root;
    int i;
    char path[PF_MAXSTRING];
    int list_type, real_list_type, list_size;
    char *s;
    pfa_global_t *glb;

    glb = (pfa_global_t *)malloc(sizeof(pfa_global_t));
    bzero(glb, sizeof(pfa_global_t));

    glb->tessAction = tessAction;
    glb->compile_gl = compile_gl;
    glb->udfunc = copy_udfunc(udfunc_pfa);
    glb->dont_load_user_funcs = dont_load_user_funcs;
    if ((glb->share_gs_objects = share_gs_objects) == PF_ON)
	glb->share = pfdGetGlobalShare();

    /*
     *  find the input file
     */
    if (!pfFindFile(fileName, path, R_OK))
    {
	pfNotify(PFNFY_WARN, PFNFY_RESOURCE,
		 "pfdLoadFile_pfa:  Could not find readable \"%s\".\n",
		 fileName);
	free(glb);
	return(FALSE);
    }

    /*
     *  open the input file for reading
     */
    if ((glb->ifp = fopen(path, "r")) == NULL)
    {
	pfNotify(PFNFY_WARN, PFNFY_RESOURCE,
		 "pfdLoadFile_pfa:  Could not open \"%s\" for reading.\n",
		 path);
	free(glb);
	return(FALSE);
    }

    /*
     *  read the file
     */
    pfNotify(PFNFY_NOTICE, PFNFY_PRINT, "pfdLoadFile_pfa: Loading \"%s\"",
	     path);
    glb->arena = pfGetSharedArena();
    glb->line_num = 0;
    glb->line_buf = (char *)malloc(1024 * sizeof(char));
    glb->read_error = 0;
    if (glb->num_custom_nodes = num_custom_nodes_pfa)
    {
	glb->custom_nodes = (custom_node_t *)malloc(num_custom_nodes_pfa *
						    sizeof(custom_node_t));
	bcopy(custom_nodes_pfa, glb->custom_nodes,
	      num_custom_nodes_pfa * sizeof(custom_node_t));
	for (i = 0; i < num_custom_nodes_pfa; i++)
	    glb->custom_nodes[i].name = strdup(custom_nodes_pfa[i].name);
    }
    set_buf_size(512, glb);

    while (!glb->read_error && read_line(glb))
    {
	if (sscanf(glb->line_buf, "%d %d", &list_type, &list_size) == 2 &&
	    list_type >= 0 && list_type < L_COUNT)
	{
	    real_list_type = list_conv[list_type];
	    glb->rl_list[real_list_type] = (void *)malloc(list_size *
							  sizeof(void *));
	    glb->l_num[real_list_type] = list_size;

	    if (list_type == L_NODE)
	    {
		glb->children = (int **)malloc(list_size * sizeof(int *));
		for (i = 0; i < list_size && !glb->read_error; i++)
		    glb->rl_list[list_type][i] = pfa_read_node(i, glb);
		/* If the first incarnation of shaders is not supported, then
		   L_NODE is the last possible list. If you add another list to
		   the end, break out of this while loop elsewhere */
		if((glb->version < PFBV_SHADER) || (glb->version >= PFBV_REMOVE_SHADER && (glb->version < PFBV_SURFACES)))
		  break;
	    }
	    else if (list_type == L_HLIGHT)
	    {
		glb->hlight_gstates = (int *)malloc(list_size * sizeof(int));
		for (i = 0; i < list_size && !glb->read_error; i++)
		    glb->rl_list[list_type][i] = pfa_read_hlight(i, glb);
	    }
	    else if (list_type == L_UDATA)
	    {
		glb->udi = (udata_info_t *)malloc(list_size *
						  sizeof(udata_info_t));
		for (i = 0; i < list_size && !glb->read_error; i++)
		    glb->rl_list[list_type][i] = pfa_read_udata(i, glb);
	    }
	    else
	    {
	        for (i = 0; i < list_size && !glb->read_error; i++) {
		    PFASSERTDEBUG(list_type < (sizeof(pfa_read_func) / sizeof(pfa_read_func[0])));
		    glb->rl_list[real_list_type][i] = pfa_read_func[list_type](glb);
		}
		/*
		  For pfa versions between PFBV_SHADER(inclusive) and 
		  PFBV_REMOVE_SHADER(exclusive), L_SHADER is the last possible list type.
		  Break out of the list reading loop here if such a list has been
		  read.
		*/
		if((glb->version >= PFBV_SHADER) && (glb->version < PFBV_REMOVE_SHADER) 
		   && (list_type == (L_SHADER)))
		  break;
            }
	}
	else if (strtoul(glb->line_buf, &s, 16) == PFB_MAGIC_NUMBER)
	{
		int stuff = atoi(s);
		if(r_swap)
		{
			P_32_SWAP(&stuff);
		}

	    if ((glb->version = stuff) > PFB_VERSION_NUMBER)
	    {
		pfNotify(PFNFY_WARN, PFNFY_PRINT,
			 "%s:  \"%s\" has a newer pfa version number.\n",
			 "pfdLoadFile_pfa", path);
		pfNotify(PFNFY_WARN, PFNFY_PRINT,
			 "It may not load correctly.\n");
	    }

	    if(glb->version >= PFBV_MAX_TEXTURES_8)
	      glb->maxTextures = PF_MAX_TEXTURES_31; /* which is == 8 */
	    else if(glb->version >= PFBV_MULTITEXTURE)
	      glb->maxTextures = PF_MAX_TEXTURES_19; /* which is == 4 */
	    else
	      glb->maxTextures = 1; /* not really used but god to set just in case */
	}
	else
	    glb->read_error = 1;
    }

    if (!glb->read_error)
    {
	/*
	 *  connect nodes to thier children
	 */
	connect_nodes(glb);

	/*
	 *  set root node 
	 */
	if (glb->l_num[L_NODE]) {
	    root = glb->rl_list[L_NODE][0];
        }
	else
	{
	    glb->read_error = 1;
	    pfNotify(PFNFY_WARN, PFNFY_PRINT,
		     "pfdLoadFile_pfa:  No nodes found.\n");
	}
    }

    /*
     *  close the file
     */
    fclose(glb->ifp);

    /*
     * tessellate the file
     */
    if(glb->surface_count > 0 && getenv("PF_LOADER_FORCE_TESS"))
      tessellate_root(root, glb);

    /*
     *  warn about errors
     */
    if (glb->read_error)
    {
	pfNotify(PFNFY_WARN, PFNFY_PRINT,
		 "pfdLoadFile_pfa:  Error reading line %d.\n", glb->line_num);
	pfNotify(PFNFY_WARN, PFNFY_PRINT,
		 "pfdLoadFile_pfa:  Aborting read of \"%s\".\n", path);
	root = NULL;
    }

    /*
     *  free temporary memory used in creating the file.
     */
    free_load_data(glb);

    return(root);
}


PFPFB_DLLEXPORT pfNode* pfdLoadFile_pfb(const char *fileName)
{
    pfNode *root;
    int i;
    char path[PF_MAXSTRING];
    int list_type, real_list_type, list_size;
    int buf[4];
    pfa_global_t *glb;
    int whichEndian = 1;
    
    glb = (pfa_global_t *)malloc(sizeof(pfa_global_t));
    bzero(glb, sizeof(pfa_global_t));

    glb->tessAction = tessAction;
    glb->compile_gl = compile_gl;
    glb->udfunc = copy_udfunc(udfunc_pfb);
    glb->dont_load_user_funcs = dont_load_user_funcs;
    if ((glb->share_gs_objects = share_gs_objects) == PF_ON)
	glb->share = pfdGetGlobalShare();

    pfbFileReadFunction.fread = fread;
    pfbFileReadFunction.fseek = fseek;
    pfbFileReadFunction.ftell = ftell;

    /*
     *  find the input file
     */
    if (!pfFindFile(fileName, path, R_OK))
    {
	pfNotify(PFNFY_WARN, PFNFY_RESOURCE,
		 "pfdLoadFile_pfb:  Could not find readable \"%s\".\n",
		 fileName);
	free(glb);
	return(FALSE);
    }

    /*
     *  open the input file for reading
     */
    if ((glb->ifp = fopen(path, "rb")) == NULL)
    {
	pfNotify(PFNFY_WARN, PFNFY_RESOURCE,
		 "pfdLoadFile_pfb:  Could not open \"%s\" for reading.\n",
		 path);
	free(glb);
	return(FALSE);
    }

    /*
     *  read the file
     */
    glb->arena = pfGetSharedArena();
    glb->read_error = 0;
    if (glb->num_custom_nodes = num_custom_nodes_pfb)
    {
	glb->custom_nodes = (custom_node_t *)malloc(num_custom_nodes_pfb *
						    sizeof(custom_node_t));
	bcopy(custom_nodes_pfb, glb->custom_nodes,
	      num_custom_nodes_pfb * sizeof(custom_node_t));
	for (i = 0; i < num_custom_nodes_pfb; i++)
	    glb->custom_nodes[i].name = strdup(custom_nodes_pfb[i].name);
    }
	set_buf_size(512, glb);

	/* determine machine's endianness */
	if(*(char *)&whichEndian == 1)
	{
	    pfNotify(PFNFY_DEBUG, PFNFY_PRINT, "machine is little-endian\n");
	}
	else
	{
	    pfNotify(PFNFY_DEBUG, PFNFY_PRINT, "machine is big-endian\n");
	}

    /*
     *  read header
     */
    fread(buf, sizeof(int), 4, glb->ifp);



	/* determine file's endianness */
	if(buf[0] == PFB_MAGIC_NUMBER) 
	{
		/* use normal fread() */
		r_swap = False;
	}
	else if(buf[0] == PFB_MAGIC_NUMBER_LE) 
	{
		/* use swapped fread() */
		pfNotify(PFNFY_DEBUG, PFNFY_PRINT, "file is opposite endian, swapping bytes\n");
		r_swap = True;
		P_32_SWAP(&buf[0]);
		P_32_SWAP(&buf[1]);
		P_32_SWAP(&buf[2]);
		P_32_SWAP(&buf[3]);
	}
	else if(buf[0] == PFB_MAGIC_ENCODED) 
	{
		r_swap = False;
		if (crypt_key_pfb == NULL)
		{
	    pfNotify(PFNFY_WARN, PFNFY_PRINT,
		     "%s: Can't read \"%s\" because PFPFB_CRYPT_KEY is not set",
		     "pfdLoadFile_pfb", path);
	    free(glb);
	    return(FALSE);
		}
		i = (int)(crypt_key_pfb[0] * sizeof(uint) + sizeof(uint));
		glb->crypt_key = (uint *)malloc(i);
		bcopy(crypt_key_pfb, glb->crypt_key, i);
		glb->decrypt_func = decrypt_func_pfb;
    }
	else if(buf[0] == PFB_MAGIC_ENCODED_LE)
	{
		r_swap = True;
		P_32_SWAP(&buf[0]);
		P_32_SWAP(&buf[1]);
		P_32_SWAP(&buf[2]);
		P_32_SWAP(&buf[3]);

		if (crypt_key_pfb == NULL)
		{
	    pfNotify(PFNFY_WARN, PFNFY_PRINT,
		     "%s: Can't read \"%s\" because PFPFB_CRYPT_KEY is not set",
		     "pfdLoadFile_pfb", path);
	    free(glb);
	    return(FALSE);
		}
		i = (int)(crypt_key_pfb[0] * sizeof(uint) + sizeof(uint));
		glb->crypt_key = (uint *)malloc(i);
		bcopy(crypt_key_pfb, glb->crypt_key, i);
		glb->decrypt_func = decrypt_func_pfb;
	}
    else 
    {
		pfNotify(PFNFY_WARN, PFNFY_PRINT,
			 "pfdLoadFile_pfb:  \"%s\" Is not a .pfb file.\n",
			 path);
		free(glb);
		return(FALSE);
    }

    if (glb->crypt_key)
	pfNotify(PFNFY_NOTICE, PFNFY_PRINT,
		 "pfdLoadFile_pfb: Loading ENCRYPTED \"%s\"", path);
    else
	pfNotify(PFNFY_NOTICE, PFNFY_PRINT,
		 "pfdLoadFile_pfb: Loading \"%s\"", path);

    if ((glb->version = buf[1]) > PFB_VERSION_NUMBER)
    {
	pfNotify(PFNFY_WARN, PFNFY_PRINT,
		 "pfdLoadFile_pfb:  \"%s\" has a newer pfb version number [%d].\n",
		 path, buf[1]);
	pfNotify(PFNFY_WARN, PFNFY_PRINT, "It may not load correctly.\n");
    }

    if(glb->version >= PFBV_MAX_TEXTURES_8)
      glb->maxTextures = PF_MAX_TEXTURES_31; /* which is == 8 */
    else if(glb->version >= PFBV_MULTITEXTURE)
      glb->maxTextures = PF_MAX_TEXTURES_19; /* which is == 4 */
    else
      glb->maxTextures = 1; /* not really used but god to set just in case */

    pfNotify(PFNFY_DEBUG, PFNFY_INTERNAL,
        "pfdLoadFile_pfb: .pfb File Version is %d\n", glb->version);
    fseek(glb->ifp, buf[3], SEEK_SET);
    /*
     *  read lists
     */
    while (!glb->read_error && pfb_fread(buf, sizeof(int), 3, glb->ifp) == 3)
    {
	list_type = buf[0];
	list_size = buf[1];

	if (list_type < 0 || list_type >= L_COUNT)
	{
	    pfNotify(PFNFY_WARN, PFNFY_PRINT,
		     "%s:  Encountered an unknown list type \"%d\".\n",
		     "pfdLoadFile_pfb",  list_type);
	    pfNotify(PFNFY_WARN, PFNFY_MORE, "This list will be skipped.\n");
	    pfNotify(PFNFY_WARN, PFNFY_MORE,
		     "This file will probably not display properly.\n");
	    fseek(glb->ifp, buf[2], SEEK_CUR);
	    break;
	}

	real_list_type = list_conv[list_type];

	glb->rl_list[real_list_type] = (void *)malloc(list_size *
						      sizeof(void *));
	glb->l_num[real_list_type] = list_size;

	if (list_type == L_NODE)
	{
	    glb->children = (int **)malloc(list_size * sizeof(int *));
	    for (i = 0; i < list_size && !glb->read_error; i++) {
	        PFASSERTDEBUG(list_type < (sizeof(pfa_read_func) / sizeof(pfa_read_func[0])));
		glb->rl_list[list_type][i] = pfb_read_node(i, glb);
	    }
	    
	    /* If the first incarnation of shaders is not supported, then
	       L_NODE is the last possible list. If you add another list to
	       the end, break out of the while loop elsewhere */
	    if((glb->version < PFBV_SHADER) || (glb->version >= PFBV_REMOVE_SHADER) && (glb->version < PFBV_SURFACES))
	      break;

	}
	else if (list_type == L_HLIGHT)
	{
	    glb->hlight_gstates = (int *)malloc(list_size * sizeof(int));
	    for (i = 0; i < list_size && !glb->read_error; i++)
		glb->rl_list[list_type][i] = pfb_read_hlight(i, glb);
	}
	else if (list_type == L_UDATA)
	{
	    glb->udi = (udata_info_t *)malloc(list_size * sizeof(udata_info_t));
	    for (i = 0; i < list_size && !glb->read_error; i++)
		glb->rl_list[list_type][i] = pfb_read_udata(i, glb);
	}
	else
	{
	  for (i = 0; i < list_size && !glb->read_error; i++)
	    glb->rl_list[real_list_type][i] = pfb_read_func[list_type](glb);
	  
	  /*
	    For pfb versions between PFBV_SHADER(inclusive) and 
	    PFBV_REMOVE_SHADER(exclusive), L_SHADER is the last possible list type.
	    Break out of the list reading loop here if such a list has been
	    read.
	   */
	  if((glb->version >= PFBV_SHADER) && (glb->version < PFBV_REMOVE_SHADER) 
	     && (list_type == (L_SHADER)))
	    break;
 
	}
    }

    if (!glb->read_error)
    {
	/*
	 *  connect nodes to their children
	 */
	connect_nodes(glb);

	/*
	 *  set root node 
	 */
	if (glb->l_num[L_NODE]) {
	    root = glb->rl_list[L_NODE][0];
        }
	else
	{
	    glb->read_error = 1;
	    pfNotify(PFNFY_WARN, PFNFY_PRINT,
		     "pfdLoadFile_pfb:  No nodes found.\n");
	}
    }

    /*
     *  close the file
     */
    fclose(glb->ifp);

    /*
     * tessellate the file
     */
    if(glb->surface_count > 0 && getenv("PF_LOADER_FORCE_TESS"))
      tessellate_root(root, glb);

    /*
     *  warn about errors
     */
    if (glb->read_error)
    {
	pfNotify(PFNFY_WARN, PFNFY_PRINT,
		 "pfdLoadFile_pfb:  Read error.\n");
	pfNotify(PFNFY_WARN, PFNFY_PRINT,
		 "pfdLoadFile_pfb:  Aborting read of \"%s\".\n", path);
	root = NULL;
    }

    /*
     *  free temporary memory used in creating the file.
     */
    free_load_data(glb);
    
    return(root);
}

PFPFB_DLLEXPORT pfNode* pfdLoadFile_pfb_fromMemory(int fileSize, 
    unsigned char *addr)
{
    pfNode *root;
    int i;
    int list_type, real_list_type, list_size;
    int buf[4];
    pfa_global_t *glb;
    int whichEndian = 1;
    pfbMemoryFile *memFile;

    glb = (pfa_global_t *)malloc(sizeof(pfa_global_t));
    bzero(glb, sizeof(pfa_global_t));

    glb->tessAction = tessAction;
    glb->compile_gl = compile_gl;
    glb->udfunc = copy_udfunc(udfunc_pfb);
    glb->dont_load_user_funcs = dont_load_user_funcs;
    if ((glb->share_gs_objects = share_gs_objects) == PF_ON)
	glb->share = pfdGetGlobalShare();

    pfbFileReadFunction.fread = fread_from_memory;
    pfbFileReadFunction.fseek = fseek_from_memory;
    pfbFileReadFunction.ftell = ftell_from_memory;

    memFile = malloc(sizeof(pfbMemoryFile));
    if(!memFile) {
      pfNotify(PFNFY_WARN,PFNFY_PRINT,
	       "pfLoadFile_pfb_fromMemory(): unable to alloc %d bytes. Not loading"
	       " database at addr 0x%x",sizeof(pfbMemoryFile),addr);
      free(glb);
      return FALSE;
    }

    memFile->size = fileSize;
    memFile->offset = 0;
    memFile->start = addr;
    memFile->position = memFile->start;

    glb->ifp = (FILE *)memFile;

    /*
     *  read the file
     */
    glb->arena = pfGetSharedArena();
    glb->read_error = 0;
    if (glb->num_custom_nodes = num_custom_nodes_pfb)
    {
	glb->custom_nodes = (custom_node_t *)malloc(num_custom_nodes_pfb *
						    sizeof(custom_node_t));
	bcopy(custom_nodes_pfb, glb->custom_nodes,
	      num_custom_nodes_pfb * sizeof(custom_node_t));
	for (i = 0; i < num_custom_nodes_pfb; i++)
	    glb->custom_nodes[i].name = strdup(custom_nodes_pfb[i].name);
    }
	set_buf_size(512, glb);

	/* determine machine's endianness */
	if(*(char *)&whichEndian == 1)
	{
		pfNotify(PFNFY_DEBUG, PFNFY_PRINT, "machine is little-endian\n");
	}
	else
	{
		pfNotify(PFNFY_DEBUG, PFNFY_PRINT, "machine is big-endian\n");
	}

    /*
     *  read header
     */
    pfbFileReadFunction.fread(buf, sizeof(int), 4, glb->ifp);

	/* determine file's endianness */
	if(buf[0] == PFB_MAGIC_NUMBER) 
	{
		/* use normal fread() */
		r_swap = False;
	}
	else if(buf[0] == PFB_MAGIC_NUMBER_LE) 
	{
		/* use swapped fread() */
		pfNotify(PFNFY_DEBUG, PFNFY_PRINT, "file is opposite endian, swapping bytes\n");
		r_swap = True;
		P_32_SWAP(&buf[0]);
		P_32_SWAP(&buf[1]);
		P_32_SWAP(&buf[2]);
		P_32_SWAP(&buf[3]);
	}
	else if(buf[0] == PFB_MAGIC_ENCODED) 
	{
		r_swap = False;
		if (crypt_key_pfb == NULL)
		{
	    pfNotify(PFNFY_WARN, PFNFY_PRINT,
		     "%s: Can't read \"0x%x\" because PFPFB_CRYPT_KEY is not set",
		     "pfdLoadFile_pfb_fromMemory", addr);
	    free(glb);
	    return(FALSE);
		}
		i = (int)(crypt_key_pfb[0] * sizeof(uint) + sizeof(uint));
		glb->crypt_key = (uint *)malloc(i);
		bcopy(crypt_key_pfb, glb->crypt_key, i);
		glb->decrypt_func = decrypt_func_pfb;
    }
	else if(buf[0] == PFB_MAGIC_ENCODED_LE)
	{
		r_swap = True;
		P_32_SWAP(&buf[0]);
		P_32_SWAP(&buf[1]);
		P_32_SWAP(&buf[2]);
		P_32_SWAP(&buf[3]);

		if (crypt_key_pfb == NULL)
		{
	    pfNotify(PFNFY_WARN, PFNFY_PRINT,
		     "%s: Can't read \"0x%x\" because PFPFB_CRYPT_KEY is not set",
		     "pfdLoadFile_pfb_fromMemory", addr);
	    free(glb);
	    return(FALSE);
		}
		i = (int)(crypt_key_pfb[0] * sizeof(uint) + sizeof(uint));
		glb->crypt_key = (uint *)malloc(i);
		bcopy(crypt_key_pfb, glb->crypt_key, i);
		glb->decrypt_func = decrypt_func_pfb;
	}
    else 
    {
		pfNotify(PFNFY_WARN, PFNFY_PRINT,
			 "pfdLoadFile_pfb_fromMemory:  \"0x%x\" Is not a .pfb file.\n",
			 addr);
		free(glb);
		return(FALSE);
    }

    if (glb->crypt_key)
	pfNotify(PFNFY_NOTICE, PFNFY_PRINT,
		 "pfdLoadFile_pfb_fromMemory: Loading ENCRYPTED \"0x%x\"", addr);
    else
	pfNotify(PFNFY_NOTICE, PFNFY_PRINT,
		 "pfdLoadFile_pfb_fromMemory: Loading 0x%x", addr);

    if ((glb->version = buf[1]) > PFB_VERSION_NUMBER)
    {
	pfNotify(PFNFY_WARN, PFNFY_PRINT,
		 "pfdLoadFile_pfb_fromMemory:  \"0x%x\" has a newer pfb version number [%d].\n",
		 addr, buf[1]);
	pfNotify(PFNFY_WARN, PFNFY_PRINT, "It may not load correctly.\n");
    }

    if(glb->version >= PFBV_MAX_TEXTURES_8)
      glb->maxTextures = PF_MAX_TEXTURES_31; /* which is == 8 */
    else if(glb->version >= PFBV_MULTITEXTURE)
      glb->maxTextures = PF_MAX_TEXTURES_19; /* which is == 4 */
    else
      glb->maxTextures = 1; /* not really used but god to set just in case */

    pfNotify(PFNFY_DEBUG, PFNFY_INTERNAL,
        "pfdLoadFile_pfb_fromMemory: .pfb File Version is %d\n", glb->version);
    pfbFileReadFunction.fseek(glb->ifp, buf[3], SEEK_SET);
    /*
     *  read lists
     */
    while (!glb->read_error && pfb_fread(buf, sizeof(int), 3, glb->ifp) == 3
	   && memFile->offset < memFile->size)
    {
	list_type = buf[0];
	list_size = buf[1];

	if (list_type < 0 || list_type >= L_COUNT)
	{
	    pfNotify(PFNFY_WARN, PFNFY_PRINT,
		     "%s:  Encountered an unknown list type \"%d\".\n",
		     "pfdLoadFile_pfb_fromMemory",  list_type);
	    pfNotify(PFNFY_WARN, PFNFY_MORE, "This list will be skipped.\n");
	    pfNotify(PFNFY_WARN, PFNFY_MORE,
		     "This file will probably not display properly.\n");
	    pfbFileReadFunction.fseek(glb->ifp, buf[2], SEEK_CUR);
	    break;
	}

	real_list_type = list_conv[list_type];

	glb->rl_list[real_list_type] = (void *)malloc(list_size *
						      sizeof(void *));
	glb->l_num[real_list_type] = list_size;

	if (list_type == L_NODE)
	{
	    glb->children = (int **)malloc(list_size * sizeof(int *));
	    for (i = 0; i < list_size && !glb->read_error; i++) {
	        PFASSERTDEBUG(list_type < (sizeof(pfa_read_func) / sizeof(pfa_read_func[0])));
		glb->rl_list[list_type][i] = pfb_read_node(i, glb);
	    }
	    
	    /* If the first incarnation of shaders is not supported, then
	       L_NODE is the last possible list. If you add another list to
	       the end, break out of the while loop elsewhere */
	    if((glb->version < PFBV_SHADER) || (glb->version >= PFBV_REMOVE_SHADER) && (glb->version < PFBV_SURFACES))
	      break;

	}
	else if (list_type == L_HLIGHT)
	{
	    glb->hlight_gstates = (int *)malloc(list_size * sizeof(int));
	    for (i = 0; i < list_size && !glb->read_error; i++)
		glb->rl_list[list_type][i] = pfb_read_hlight(i, glb);
	}
	else if (list_type == L_UDATA)
	{
	    glb->udi = (udata_info_t *)malloc(list_size * sizeof(udata_info_t));
	    for (i = 0; i < list_size && !glb->read_error; i++)
		glb->rl_list[list_type][i] = pfb_read_udata(i, glb);
	}
	else
	{
	  for (i = 0; i < list_size && !glb->read_error; i++)
	    glb->rl_list[real_list_type][i] = pfb_read_func[list_type](glb);
	  
	  /*
	    For pfb versions between PFBV_SHADER(inclusive) and 
	    PFBV_REMOVE_SHADER(exclusive), L_SHADER is the last possible list type.
	    Break out of the list reading loop here if such a list has been
	    read.
	   */
	  if((glb->version >= PFBV_SHADER) && (glb->version < PFBV_REMOVE_SHADER) 
	     && (list_type == (L_SHADER)))
	    break;
 
	}
    }

    if (!glb->read_error)
    {
	/*
	 *  connect nodes to their children
	 */
	connect_nodes(glb);

	/*
	 *  set root node 
	 */
	if (glb->l_num[L_NODE]) {
	    root = glb->rl_list[L_NODE][0];
        }
	else
	{
	    glb->read_error = 1;
	    pfNotify(PFNFY_WARN, PFNFY_PRINT,
		     "pfdLoadFile_pfb_fromMemory:  No nodes found.\n");
	}
    }
    
    /*
     *  close the file
     */
    free(memFile);

    /*
     * tessellate the file
     */
    if(glb->surface_count > 0 && getenv("PF_LOADER_FORCE_TESS"))
      tessellate_root(root, glb);

    /*
     *  warn about errors
     */
    if (glb->read_error)
    {
	pfNotify(PFNFY_WARN, PFNFY_PRINT,
		 "pfdLoadFile_pfb_fromMemory:  Read error.\n");
	pfNotify(PFNFY_WARN, PFNFY_PRINT,
		 "pfdLoadFile_pfb_fromMemory:  Aborting read of \"0x%x\".\n", addr);
	root = NULL;
    }

    /*
     *  free temporary memory used in creating the file.
     */
    free_load_data(glb);
    
    return(root);
}


static int read_line(pfa_global_t *glb)
{
    while (fgets(glb->line_buf, 1024, glb->ifp))
    {
	glb->line_num++;
	if (glb->line_buf[0] != '#')
	    return(1);
    }

    return(0);
}


static int *set_buf_size(int size, pfa_global_t *glb)
{
    if (glb->buf_size < size)
    {
	if (glb->buf)
	    free(glb->buf);
	glb->buf = (int *)malloc(size * sizeof(int));
	glb->buf_size = size;
    }

    return(glb->buf);
}


static void *pfa_read_mtl(pfa_global_t *glb)
{
    pfMaterial *mtl;
    mtl_t m;

    READ_LINE();
    m.side = atoi(glb->line_buf);
    READ_LINE();
    m.alpha = atof(glb->line_buf);
    READ_LINE();
    m.shininess = atof(glb->line_buf);
    READ_LINE();
    sscanf(glb->line_buf, "%f %f %f",
	   &m.ambient[0], &m.ambient[1], &m.ambient[2]);
    READ_LINE();
    sscanf(glb->line_buf, "%f %f %f",
	   &m.diffuse[0], &m.diffuse[1], &m.diffuse[2]);
    READ_LINE();
    sscanf(glb->line_buf, "%f %f %f",
	   &m.specular[0], &m.specular[1], &m.specular[2]);
    READ_LINE();
    sscanf(glb->line_buf, "%f %f %f",
	   &m.emission[0], &m.emission[1], &m.emission[2]);
    READ_LINE();
    sscanf(glb->line_buf, "%d %d", &m.cmode[0], &m.cmode[1]);
    READ_LINE();
    m.udata = atoi(glb->line_buf);

    mtl = pfNewMtl(glb->arena);

    pfMtlSide(mtl, mtls_table[m.side]);
    pfMtlAlpha(mtl, m.alpha);
    pfMtlShininess(mtl, m.shininess);
    pfMtlColor(mtl, PFMTL_AMBIENT, m.ambient[0], m.ambient[1], m.ambient[2]);
    pfMtlColor(mtl, PFMTL_DIFFUSE, m.diffuse[0], m.diffuse[1], m.diffuse[2]);
    pfMtlColor(mtl, PFMTL_SPECULAR,
	       m.specular[0], m.specular[1], m.specular[2]);
    pfMtlColor(mtl, PFMTL_EMISSION,
	       m.emission[0], m.emission[1], m.emission[2]);
    pfMtlColorMode(mtl, mtls_table[m.cmode[0]], cmode_table[m.cmode[1]]);
    if (m.udata != -1)
	set_udata((pfObject *)mtl, m.udata, glb);

    return(share_obj((pfObject *)mtl, glb));
}


static void *pfb_read_mtl(pfa_global_t *glb)
{
    pfMaterial *mtl;
    mtl_t m;

    pfb_fread(&m, sizeof(mtl_t), 1, glb->ifp);

    mtl = pfNewMtl(glb->arena);

    pfMtlSide(mtl, mtls_table[m.side]);
    pfMtlAlpha(mtl, m.alpha);
    pfMtlShininess(mtl, m.shininess);
    pfMtlColor(mtl, PFMTL_AMBIENT, m.ambient[0], m.ambient[1], m.ambient[2]);
    pfMtlColor(mtl, PFMTL_DIFFUSE, m.diffuse[0], m.diffuse[1], m.diffuse[2]);
    pfMtlColor(mtl, PFMTL_SPECULAR,
	       m.specular[0], m.specular[1], m.specular[2]);
    pfMtlColor(mtl, PFMTL_EMISSION,
	       m.emission[0], m.emission[1], m.emission[2]);
    pfMtlColorMode(mtl, mtls_table[m.cmode[0]], cmode_table[m.cmode[1]]);
    if (m.udata != -1)
	set_udata((pfObject *)mtl, m.udata, glb);

    return(share_obj((pfObject *)mtl, glb));
}


static void *pfa_read_lmodel(pfa_global_t *glb)
{
    pfLightModel *lmodel;
    lmodel_t lm;

    READ_LINE();
    sscanf(glb->line_buf, "%f %f %f",
	   &lm.atten[0], &lm.atten[1], &lm.atten[2]);
    READ_LINE();
    lm.local = atoi(glb->line_buf);
    READ_LINE();
    lm.twoside = atoi(glb->line_buf);
    READ_LINE();
    sscanf(glb->line_buf, "%f %f %f",
	   &lm.ambient[0], &lm.ambient[1], &lm.ambient[2]);
    READ_LINE();
    lm.udata = atoi(glb->line_buf);

    lmodel = pfNewLModel(glb->arena);
    pfLModelAtten(lmodel, lm.atten[0], lm.atten[1], lm.atten[2]);
    pfLModelLocal(lmodel, oo_table[lm.local]);
    pfLModelTwoSide(lmodel, oo_table[lm.twoside]);
    pfLModelAmbient(lmodel, lm.ambient[0], lm.ambient[1], lm.ambient[2]);
    if (lm.udata != -1)
	set_udata((pfObject *)lmodel, lm.udata, glb);

    return(share_obj((pfObject *)lmodel, glb));
}


static void *pfb_read_lmodel(pfa_global_t *glb)
{
    pfLightModel *lmodel;
    lmodel_t lm;

    pfb_fread(&lm, sizeof(lmodel_t), 1, glb->ifp);

    lmodel = pfNewLModel(glb->arena);
    pfLModelAtten(lmodel, lm.atten[0], lm.atten[1], lm.atten[2]);
    pfLModelLocal(lmodel, oo_table[lm.local]);
    pfLModelTwoSide(lmodel, oo_table[lm.twoside]);
    pfLModelAmbient(lmodel, lm.ambient[0], lm.ambient[1], lm.ambient[2]);
    if (lm.udata != -1)
	set_udata((pfObject *)lmodel, lm.udata, glb);

    return(share_obj((pfObject *)lmodel, glb));
}


static void *pfa_read_light(pfa_global_t *glb)
{
    pfLight *light;
    light_t l;

    READ_LINE();
    sscanf(glb->line_buf, "%f %f %f",
	   &l.ambient[0], &l.ambient[1], &l.ambient[2]);
    READ_LINE();
    sscanf(glb->line_buf,
	   "%f %f %f", &l.diffuse[0], &l.diffuse[1], &l.diffuse[2]);
    READ_LINE();
    sscanf(glb->line_buf, "%f %f %f",
	   &l.specular[0], &l.specular[1], &l.specular[2]);
    READ_LINE();
    sscanf(glb->line_buf, "%f %f %f", &l.atten[0], &l.atten[1], &l.atten[2]);
    READ_LINE();
    sscanf(glb->line_buf, "%f %f %f %f",
	   &l.pos[0], &l.pos[1], &l.pos[2], &l.pos[3]);
    READ_LINE();
    sscanf(glb->line_buf, "%f %f %f", &l.sl_dir[0], &l.sl_dir[1], &l.sl_dir[2]);
    READ_LINE();
    sscanf(glb->line_buf, "%f %f", &l.sl_cone[0], &l.sl_cone[1]);
    READ_LINE();
    l.on = atoi(glb->line_buf);
    READ_LINE();
    l.udata = atoi(glb->line_buf);

    light = pfNewLight(glb->arena);
    pfLightColor(light, PFLT_AMBIENT, l.ambient[0], l.ambient[1], l.ambient[2]);
    pfLightColor(light, PFLT_DIFFUSE, l.diffuse[0], l.diffuse[1], l.diffuse[2]);
    pfLightColor(light, PFLT_SPECULAR,
		 l.specular[0], l.specular[1], l.specular[2]);
    pfLightAtten(light, l.atten[0], l.atten[1], l.atten[2]);
    pfLightPos(light, l.pos[0], l.pos[1], l.pos[2], l.pos[3]);
    pfSpotLightDir(light, l.sl_dir[0], l.sl_dir[1], l.sl_dir[2]);
    pfSpotLightCone(light, l.sl_cone[0], l.sl_cone[1]);
    if (l.on)
	pfLightOn(light);
    else
	pfLightOff(light);
    if (l.udata != -1)
	set_udata((pfObject *)light, l.udata, glb);

    return(share_obj((pfObject *)light, glb));
}


static void *pfb_read_light(pfa_global_t *glb)
{
    pfLight *light;
    light_t l;

    pfb_fread(&l, sizeof(light_t), 1, glb->ifp);

    light = pfNewLight(glb->arena);
    pfLightColor(light, PFLT_AMBIENT, l.ambient[0], l.ambient[1], l.ambient[2]);
    pfLightColor(light, PFLT_DIFFUSE, l.diffuse[0], l.diffuse[1], l.diffuse[2]);
    pfLightColor(light, PFLT_SPECULAR,
		 l.specular[0], l.specular[1], l.specular[2]);
    pfLightAtten(light, l.atten[0], l.atten[1], l.atten[2]);
    pfLightPos(light, l.pos[0], l.pos[1], l.pos[2], l.pos[3]);
    pfSpotLightDir(light, l.sl_dir[0], l.sl_dir[1], l.sl_dir[2]);
    pfSpotLightCone(light, l.sl_cone[0], l.sl_cone[1]);
    if (l.on)
	pfLightOn(light);
    else
	pfLightOff(light);
    if (l.udata != -1)
	set_udata((pfObject *)light, l.udata, glb);

    return(share_obj((pfObject *)light, glb));
}


static void *pfa_read_lsource(pfa_global_t *glb)
{
    pfLightSource *lsource;
    lsource_t l;

    READ_LINE();
    sscanf(glb->line_buf, "%f %f %f",
	   &l.ambient[0], &l.ambient[1], &l.ambient[2]);
    READ_LINE();
    sscanf(glb->line_buf, "%f %f %f",
	   &l.diffuse[0], &l.diffuse[1], &l.diffuse[2]);
    READ_LINE();
    sscanf(glb->line_buf, "%f %f %f",
	   &l.specular[0], &l.specular[1], &l.specular[2]);
    READ_LINE();
    sscanf(glb->line_buf, "%f %f %f", &l.atten[0], &l.atten[1], &l.atten[2]);
    READ_LINE();
    sscanf(glb->line_buf, "%f %f %f %f",
	   &l.pos[0], &l.pos[1], &l.pos[2], &l.pos[3]);
    READ_LINE();
    sscanf(glb->line_buf, "%f %f %f", &l.sl_dir[0], &l.sl_dir[1], &l.sl_dir[2]);
    READ_LINE();
    sscanf(glb->line_buf, "%f %f", &l.sl_cone[0], &l.sl_cone[1]);
    READ_LINE();
    l.on = atoi(glb->line_buf);
    READ_LINE();
    l.shadow_enable = atoi(glb->line_buf);
    READ_LINE();
    l.projtex_enable = atoi(glb->line_buf);
    READ_LINE();
    l.freeze_shadows = atoi(glb->line_buf);
    READ_LINE();
    l.fog_enable = atoi(glb->line_buf);
    READ_LINE();
    l.shadow_displace_scale = atof(glb->line_buf);
    READ_LINE();
    l.shadow_displace_offset = atof(glb->line_buf);
    READ_LINE();
    l.shadow_size = atof(glb->line_buf);
    READ_LINE();
    l.intensity = atof(glb->line_buf);
    READ_LINE();
    l.shadow_tex = atoi(glb->line_buf);
    READ_LINE();
    l.proj_tex = atoi(glb->line_buf);
    READ_LINE();
    l.proj_fog = atoi(glb->line_buf);
    READ_LINE();
    l.proj_frust = atoi(glb->line_buf);

    lsource = pfNewLSource();
    pfLSourceColor(lsource, PFLT_AMBIENT,
		   l.ambient[0], l.ambient[1], l.ambient[2]);
    pfLSourceColor(lsource, PFLT_DIFFUSE,
		   l.diffuse[0], l.diffuse[1], l.diffuse[2]);
    pfLSourceColor(lsource, PFLT_SPECULAR,
		 l.specular[0], l.specular[1], l.specular[2]);
    pfLSourceAtten(lsource, l.atten[0], l.atten[1], l.atten[2]);
    pfLSourcePos(lsource, l.pos[0], l.pos[1], l.pos[2], l.pos[3]);
    pfSpotLSourceDir(lsource, l.sl_dir[0], l.sl_dir[1], l.sl_dir[2]);
    pfSpotLSourceCone(lsource, l.sl_cone[0], l.sl_cone[1]);
    if (l.on)
	pfLSourceOn(lsource);
    else
	pfLSourceOff(lsource);
    pfLSourceMode(lsource, PFLS_SHADOW_ENABLE, oo_table[l.shadow_enable]);
    pfLSourceMode(lsource, PFLS_PROJTEX_ENABLE, oo_table[l.projtex_enable]);
    pfLSourceMode(lsource, PFLS_FREEZE_SHADOWS, oo_table[l.freeze_shadows]);
    pfLSourceMode(lsource, PFLS_FOG_ENABLE, oo_table[l.fog_enable]);
    pfLSourceVal(lsource, PFLS_SHADOW_DISPLACE_SCALE, l.shadow_displace_scale);
    pfLSourceVal(lsource, PFLS_SHADOW_DISPLACE_OFFSET,
		 l.shadow_displace_offset);
    pfLSourceVal(lsource, PFLS_SHADOW_SIZE, l.shadow_size);
    pfLSourceVal(lsource, PFLS_INTENSITY, l.intensity);
    if (l.shadow_tex != -1)
	pfLSourceAttr(lsource, PFLS_SHADOW_TEX,
		      glb->rl_list[L_TEX][l.shadow_tex]);
    if (l.proj_tex != -1)
	pfLSourceAttr(lsource, PFLS_PROJ_TEX, glb->rl_list[L_TEX][l.proj_tex]);
    if (l.proj_fog != -1)
	pfLSourceAttr(lsource, PFLS_PROJ_FOG, glb->rl_list[L_FOG][l.proj_fog]);
    if (l.proj_frust != -1)
	pfLSourceAttr(lsource, PFLS_PROJ_FRUST,
		      glb->rl_list[L_FRUST][l.proj_frust]);

    return(lsource);
}


static void *pfb_read_lsource(pfa_global_t *glb)
{
    pfLightSource *lsource;
    lsource_t l;

    pfb_fread(&l, sizeof(lsource_t), 1, glb->ifp);

    lsource = pfNewLSource();
    pfLSourceColor(lsource, PFLT_AMBIENT,
		   l.ambient[0], l.ambient[1], l.ambient[2]);
    pfLSourceColor(lsource, PFLT_DIFFUSE,
		   l.diffuse[0], l.diffuse[1], l.diffuse[2]);
    pfLSourceColor(lsource, PFLT_SPECULAR,
		 l.specular[0], l.specular[1], l.specular[2]);
    pfLSourceAtten(lsource, l.atten[0], l.atten[1], l.atten[2]);
    pfLSourcePos(lsource, l.pos[0], l.pos[1], l.pos[2], l.pos[3]);
    pfSpotLSourceDir(lsource, l.sl_dir[0], l.sl_dir[1], l.sl_dir[2]);
    pfSpotLSourceCone(lsource, l.sl_cone[0], l.sl_cone[1]);
    if (l.on)
	pfLSourceOn(lsource);
    else
	pfLSourceOff(lsource);
    pfLSourceMode(lsource, PFLS_SHADOW_ENABLE, oo_table[l.shadow_enable]);
    pfLSourceMode(lsource, PFLS_PROJTEX_ENABLE, oo_table[l.projtex_enable]);
    pfLSourceMode(lsource, PFLS_FREEZE_SHADOWS, oo_table[l.freeze_shadows]);
    pfLSourceMode(lsource, PFLS_FOG_ENABLE, oo_table[l.fog_enable]);
    pfLSourceVal(lsource, PFLS_SHADOW_DISPLACE_SCALE, l.shadow_displace_scale);
    pfLSourceVal(lsource, PFLS_SHADOW_DISPLACE_OFFSET,
		 l.shadow_displace_offset);
    pfLSourceVal(lsource, PFLS_SHADOW_SIZE, l.shadow_size);
    pfLSourceVal(lsource, PFLS_INTENSITY, l.intensity);
    if (l.shadow_tex != -1)
	pfLSourceAttr(lsource, PFLS_SHADOW_TEX,
		      glb->rl_list[L_TEX][l.shadow_tex]);
    if (l.proj_tex != -1)
	pfLSourceAttr(lsource, PFLS_PROJ_TEX, glb->rl_list[L_TEX][l.proj_tex]);
    if (l.proj_fog != -1)
	pfLSourceAttr(lsource, PFLS_PROJ_FOG, glb->rl_list[L_FOG][l.proj_fog]);
    if (l.proj_frust != -1)
	pfLSourceAttr(lsource, PFLS_PROJ_FRUST,
		      glb->rl_list[L_FRUST][l.proj_frust]);

    return(lsource);
}


static void *pfa_read_frust(pfa_global_t *glb)
{
    pfFrustum *frust;
    frust_t f;

    READ_LINE();
    sscanf(glb->line_buf, "%d", &f.type);
    READ_LINE();
    sscanf(glb->line_buf, "%f %f %f %f %f %f",
	   &f.left, &f.right, &f.bottum, &f.top, &f.nearPlane, &f.farPlane);
    READ_LINE();
    sscanf(glb->line_buf, "%f %f %f %f",
	   &f.mat[0][0], &f.mat[0][1], &f.mat[0][2], &f.mat[0][3]);
    READ_LINE();
    sscanf(glb->line_buf, "%f %f %f %f",
	   &f.mat[1][0], &f.mat[1][1], &f.mat[1][2], &f.mat[1][3]);
    READ_LINE();
    sscanf(glb->line_buf, "%f %f %f %f",
	   &f.mat[2][0], &f.mat[2][1], &f.mat[2][2], &f.mat[2][3]);
    READ_LINE();
    sscanf(glb->line_buf, "%f %f %f %f",
	   &f.mat[3][0], &f.mat[3][1], &f.mat[3][2], &f.mat[3][3]);
    READ_LINE();
    f.udata = atoi(glb->line_buf);

    frust = pfNewFrust(glb->arena);

    pfFrustNearFar(frust, f.nearPlane, f.farPlane);
    if (frust_table[f.type] == PFFRUST_ORTHOGONAL)
	pfMakeOrthoFrust(frust, f.left, f.right, f.bottum, f.top);
    else
	pfMakePerspFrust(frust, f.left, f.right, f.bottum, f.top);
    if (f.mat[0][0] != 1.0f || f.mat[0][1] != 0.0f ||
	f.mat[0][2] != 0.0f || f.mat[0][3] != 0.0f ||
	f.mat[1][0] != 0.0f || f.mat[1][1] != 1.0f ||
	f.mat[1][2] != 0.0f || f.mat[1][3] != 0.0f ||
	f.mat[2][0] != 0.0f || f.mat[2][1] != 0.0f ||
	f.mat[2][2] != 1.0f || f.mat[2][3] != 0.0f ||
	f.mat[3][0] != 0.0f || f.mat[3][1] != 0.0f ||
	f.mat[3][2] != 0.0f || f.mat[3][3] != 1.0f)
	pfOrthoXformFrust(frust, frust, f.mat);
    if (f.udata != -1)
	set_udata((pfObject *)frust, f.udata, glb);

    return(frust);
}


static void *pfb_read_frust(pfa_global_t *glb)
{
    pfFrustum *frust;
    frust_t f;

    pfb_fread(&f, sizeof(frust_t), 1, glb->ifp);

    frust = pfNewFrust(glb->arena);

    pfFrustNearFar(frust, f.nearPlane, f.farPlane);
    if (frust_table[f.type] == PFFRUST_ORTHOGONAL)
	pfMakeOrthoFrust(frust, f.left, f.right, f.bottum, f.top);
    else
	pfMakePerspFrust(frust, f.left, f.right, f.bottum, f.top);
    if (f.mat[0][0] != 1.0f || f.mat[0][1] != 0.0f ||
	f.mat[0][2] != 0.0f || f.mat[0][3] != 0.0f ||
	f.mat[1][0] != 0.0f || f.mat[1][1] != 1.0f ||
	f.mat[1][2] != 0.0f || f.mat[1][3] != 0.0f ||
	f.mat[2][0] != 0.0f || f.mat[2][1] != 0.0f ||
	f.mat[2][2] != 1.0f || f.mat[2][3] != 0.0f ||
	f.mat[3][0] != 0.0f || f.mat[3][1] != 0.0f ||
	f.mat[3][2] != 0.0f || f.mat[3][3] != 1.0f)
	pfOrthoXformFrust(frust, frust, f.mat);
    if (f.udata != -1)
	set_udata((pfObject *)frust, f.udata, glb);

    return(frust);
}


static void *pfa_read_tex(pfa_global_t *glb)
{
    int i;
    tex_t *t;
    tex_list_t *tl;

    tl = (tex_list_t *)malloc(sizeof(tex_list_t));

    tl->t = t = (tex_t *)malloc(sizeof(tex_t));

    READ_LINE();
    if (strncmp(glb->line_buf, "NULL", 4))
    {
	if (glb->line_buf[0] == '"')
	{
	    for (i = 1; glb->line_buf[i] != '"' && glb->line_buf[i] != '\0';
		 i++);
	    glb->line_buf[i] = '\0';
	    tl->name = strdup(&glb->line_buf[1]);
	}
	else
	    READ_ERROR();
    }
    else
	tl->name = NULL;

    if (glb->version >= PFBV_CLIPTEXTURE)
    {
	READ_LINE();
	t->type = atoi(glb->line_buf);
    }
    else
	t->type = TEXTYPE_TEXTURE;

    READ_LINE();
    sscanf(glb->line_buf, "%d %d %d %d %d", &t->format[0], &t->format[1],
	   &t->format[2], &t->format[3], &t->format[4]);

    READ_LINE();
    sscanf(glb->line_buf, "%x %x %x %x",
	   &t->filter[0], &t->filter[1], &t->filter[2], &t->filter[3]);

    READ_LINE();
    sscanf(glb->line_buf, "%d %d %d", &t->wrap[0], &t->wrap[1], &t->wrap[2]);

    READ_LINE();
    sscanf(glb->line_buf, "%f %f %f %f",
	   &t->bcolor[0], &t->bcolor[1], &t->bcolor[2], &t->bcolor[3]);

    READ_LINE();
    t->btype = atoi(glb->line_buf);

    READ_LINE();
    sscanf(glb->line_buf, "%f %f %f %f %f %f %f %f %f",
	   &t->ssp[0][0], &t->ssp[0][1], &t->ssp[1][0], &t->ssp[1][1],
	   &t->ssp[2][0], &t->ssp[2][1], &t->ssp[3][0], &t->ssp[3][1], &t->ssc);

    READ_LINE();
    sscanf(glb->line_buf, "%f %f %f %f %f %f %f %f %f",
	   &t->dsp[0][0], &t->dsp[0][1], &t->dsp[1][0], &t->dsp[1][1],
	   &t->dsp[2][0], &t->dsp[2][1], &t->dsp[3][0], &t->dsp[3][1], &t->dsc);

    READ_LINE();
    sscanf(glb->line_buf, "%d %d", &t->tdetail[0], &t->tdetail[1]);

    READ_LINE();
    sscanf(glb->line_buf, "%d %d %d", &t->lmode[0], &t->lmode[1], &t->lmode[2]);
    READ_LINE();
    sscanf(glb->line_buf, "%d %d", &t->losource[0], &t->losource[1]);
    READ_LINE();
    sscanf(glb->line_buf, "%d %d", &t->lodest[0], &t->lodest[1]);
    READ_LINE();
    sscanf(glb->line_buf, "%d %d", &t->lsize[0], &t->lsize[1]);
    READ_LINE();
    sscanf(glb->line_buf, "%d %d %d %d %d",
	   &t->image, &t->comp, &t->xsize, &t->ysize, &t->zsize);
    READ_LINE();
    t->load_image = atoi(glb->line_buf);
    READ_LINE();
    sscanf(glb->line_buf, "%d %g", &t->list_size, &t->frame);
    if (t->list_size > 0)
    {
	tl->itlist = malloc(t->list_size * sizeof(int));
	for (i = 0; i < t->list_size; i++)
	{
	    READ_LINE();
	    tl->itlist[i] = atoi(glb->line_buf);
	}
    }
    else
	tl->itlist = NULL;

    if (t->type == TEXTYPE_TEXTURE)
    {
	tl->ct = NULL;
	tl->cliplevels = NULL;

	READ_LINE();
	t->num_levels = atoi(glb->line_buf);
	if (t->num_levels > 0)
	{
	    tl->levels = (level_t *)malloc(t->num_levels * sizeof(level_t));
	    for (i = 0; i < t->num_levels; i++)
	    {
		READ_LINE();
		sscanf(glb->line_buf, "%d %d",
		       &tl->levels[i].level, &tl->levels[i].obj);
	    }
	}
	else
	    tl->levels = NULL;
    }
    else /* (t->type == TEXTYPE_CLIPTEXTURE) */
    {
	cliptex_t *ct;

	tl->levels = NULL;
	tl->ct = ct = (cliptex_t *)malloc(sizeof(cliptex_t));
	READ_LINE();
	sscanf(glb->line_buf, "%g %g %g",
	       &ct->center[0], &ct->center[1], &ct->center[2]);
	READ_LINE();
	ct->clip_size = atoi(glb->line_buf);
	READ_LINE();
	sscanf(glb->line_buf, "%d %d %d", &ct->virtual_size[0],
	       &ct->virtual_size[1], &ct->virtual_size[2]);
	READ_LINE();
	ct->invalid_border = atoi(glb->line_buf);
	READ_LINE();
	ct->virtual_LOD_offset = atoi(glb->line_buf);
	READ_LINE();
	ct->effective_levels = atoi(glb->line_buf);
	READ_LINE();
	ct->master = atoi(glb->line_buf);
	READ_LINE();
	ct->DTR_mode = (uint)strtoul(glb->line_buf, NULL, 16);
	READ_LINE();
	sscanf(glb->line_buf, "%g", &ct->DTR_max_i_border);
	READ_LINE();
	sscanf(glb->line_buf, "%g %g",
	       &ct->LOD_range[0], &ct->LOD_range[1]);

	READ_LINE();
	t->num_levels = atoi(glb->line_buf);
	if (t->num_levels > 0)
	{
	    tl->cliplevels = (cliplevel_t *)malloc(t->num_levels *
						   sizeof(cliplevel_t));
	    for (i = 0; i < t->num_levels; i++)
	    {
		READ_LINE();
		sscanf(glb->line_buf, "%d %d %d %d %d %d %d",
		       &tl->cliplevels[i].level, &tl->cliplevels[i].obj,
		       &tl->cliplevels[i].type, &tl->cliplevels[i].phase_margin,
		       &tl->cliplevels[i].phase_shift[0],
		       &tl->cliplevels[i].phase_shift[1],
		       &tl->cliplevels[i].phase_shift[2]);
	    }
	}
	else
	    tl->cliplevels = NULL;
    }

    t->aniso_degree = 1;
    if (glb->version >= PFBV_ANISOTROPY_PFA)
    {
	    READ_LINE();
    	    t->aniso_degree = atoi(glb->line_buf);
    }

    READ_LINE();
    t->udata = atoi(glb->line_buf);

    if((t->format[4] >> 2) == 1)
    {
	/* cube map */
	READ_LINE();
	tl->num_images = atoi(glb->line_buf);

	for(i=0; i<tl->num_images; i++)
	{
	    READ_LINE();
	    tl->iimages[i] = atoi(glb->line_buf);
	    
	    READ_LINE();
	    if (strncmp(glb->line_buf, "NULL", 4))
	    {
		if (glb->line_buf[0] == '"')
		{
		    for (i = 1; glb->line_buf[i] != '"' && glb->line_buf[i] != '\0';
			 i++);
		    glb->line_buf[i] = '\0';
		    tl->names[i] = strdup(&glb->line_buf[1]);
		}
		else
		    READ_ERROR();
	    }
	    else
		tl->names[i] = NULL;
	}
    }
    else
	tl->num_images = 0;

    return(share_tex(tl, glb));
}


static void *pfb_read_tex(pfa_global_t *glb)
{
    int size;
    tex_t *t;
    tex_list_t *tl;



    tl = (tex_list_t *)malloc(sizeof(tex_list_t));

    pfb_fread(&size, sizeof(int), 1, glb->ifp);
    if (size == -1)
	tl->name = NULL;
    else
    {
	tl->name = (char *)malloc(size+1);
	pfb_fread(tl->name, 1, size, glb->ifp);
	tl->name[size] = '\0';
    }

    tl->t = t = (tex_t *)malloc(sizeof(tex_t));

    t -> aniso_degree = 1;
    if (glb->version >= PFBV_ANISOTROPY)
	pfb_fread(t, sizeof(tex_t), 1, glb->ifp);
    else
    if (glb->version >= PFBV_CLIPTEXTURE)
	pfb_fread(t, sizeof(tex_1_t), 1, glb->ifp);
    else
    {
	pfb_fread(t, sizeof(tex_0_t), 1, glb->ifp);
	t->type = TEXTYPE_TEXTURE;
    }

    if (t->list_size > 0)
    {
	tl->itlist = malloc(t->list_size * sizeof(int));
	pfb_fread(tl->itlist, sizeof(int), t->list_size, glb->ifp);
    }
    else
	tl->itlist = NULL;

    if (t->type == TEXTYPE_TEXTURE)
    {
	tl->ct = NULL;
	tl->cliplevels = NULL;

	if (t->num_levels > 0)
	{
	    tl->levels = (level_t *)malloc(t->num_levels * sizeof(level_t));
	    pfb_fread(tl->levels, sizeof(level_t), t->num_levels, glb->ifp);
	}
	else
	    tl->levels = NULL;
    }
    else /* (t->type == TEXTYPE_CLIPTEXTURE) */
    {
	tl->levels = NULL;
	tl->ct = (cliptex_t *)malloc(sizeof(cliptex_t));
	pfb_fread(tl->ct, sizeof(cliptex_t), 1, glb->ifp);

	if (t->num_levels > 0)
	{
	    tl->cliplevels = (cliplevel_t *)malloc(t->num_levels *
						   sizeof(cliplevel_t));
	    pfb_fread(tl->cliplevels, sizeof(cliplevel_t), t->num_levels, glb->ifp);
	}
	else
	    tl->cliplevels = NULL;
    }

    if((t->format[4] >> 2) == 1)
    {
	int i;

	/* cube map */
	pfb_fread(&tl->num_images, sizeof(int), 1, glb->ifp);

	for(i=0; i<tl->num_images; i++)
	{
	    pfb_fread(&tl->iimages[i], sizeof(int), 1, glb->ifp);

	    pfb_fread(&size, sizeof(int), 1, glb->ifp);

	    if(size > 0)
	    {
	        tl->names[i] = (char *)malloc(size+1);
		pfb_fread(tl->names[i], 1, size, glb->ifp);
		tl->names[i][size] = '\0';
	    }
	    else
	        tl->names[i] = NULL;
	}
    }
    else
	tl->num_images = 0;

    return(share_tex(tl, glb));
}


static void *pfa_read_ibr_tex(pfa_global_t *glb)
{
    pfIBRtexture *tex;
    pfVec3       *dirs;
    pfTexture **textures;
    int       i, j, n, t, numTex, numDirs;
    float     f;

    tex = pfNewIBRtexture(glb->arena);

    READ_LINE();
    sscanf(glb->line_buf, "%d", &i);
    pfIBRtextureFlags(tex, 0xffffffff, 0); /* set all to 0 */
    pfIBRtextureFlags(tex, i, 1); /* set those that are set in 'i' */

    READ_LINE();
    sscanf(glb->line_buf, "%g", &f);
    pfIBRtextureDirection(tex, f);

    READ_LINE();
    sscanf(glb->line_buf, "%d", &n);

    if(n > 0)
    {
	int   numInRing[PFDD_MAX_RINGS];
	float horAngle[PFDD_MAX_RINGS];
	
	/* n rings specified */	
	numDirs = 0;
	for(i=0; i<n; i++)
	{
	    READ_LINE();
	    sscanf(glb->line_buf, "%d %g", 
		   &numInRing[i], &horAngle[i]);
	    numDirs += numInRing[i];
	}

	dirs = pfMalloc(sizeof(pfVec3) * numDirs, glb->arena);

	/* compute the directions */
	i = 0;

	for(j=0; j<n; j++)
	{
	    float sinH, cosH, sinV, cosV, angle;

	    sinH = fsin(horAngle[j]/ 180.0 *M_PI);
	    cosH = fcos(horAngle[j]/ 180.0 *M_PI);
	
	    for(t=0; t<numInRing[j]; t++)
	    {
		angle = (float)t / (float)(numInRing[j]) * 360.0;
		sinV = fsin(angle / 180.0 * M_PI);
		cosV = fcos(angle / 180.0 * M_PI);
	    
		dirs[i][0] = -cosH *sinV;
		dirs[i][1] = cosH *cosV;
		dirs[i][2] = -sinH;
		i++;
	    }
	}
    }
    else
    {
	/* read in the directions directly */
	READ_LINE();
	sscanf(glb->line_buf, "%d", &numDirs);

	dirs = pfMalloc(sizeof(pfVec3) * numDirs, glb->arena);
	
	for(i=0; i<numDirs; i++)
	{
	    READ_LINE();
	    sscanf(glb->line_buf, "%g %g %g", 
		   &dirs[i][0], &dirs[i][1], &dirs[i][2]);
	}
    }
    pfIBRtextureIBRdirections(tex, dirs, numDirs);


    READ_LINE();
    sscanf(glb->line_buf, "%d", &numTex);

    textures = pfMalloc(sizeof(pfTexture *) * numTex, glb->arena);

    for(i=0; i<numTex; i++)
    { 
	READ_LINE();
	sscanf(glb->line_buf, "%d", &n);
	textures[i] = glb->rl_list[L_TEX][n];
    }
    pfIBRtextureIBRTexture(tex, textures, numTex);

    return((pfObject *)tex);
}


static void *pfb_read_ibr_tex(pfa_global_t *glb)
{
    pfIBRtexture *tex;
    pfVec3       *dirs;
    pfTexture **textures;
    int       i, j, n, t, numTex, numDirs;
    int       buf_size, buf_pos;
    int       *buf;

    pfb_fread(&buf_size, sizeof(int), 1, glb->ifp);
    buf = GET_BUF(buf_size);
    pfb_fread(buf, sizeof(int), buf_size, glb->ifp);

    buf_pos = 0;

    tex = pfNewIBRtexture(glb->arena);

    i = buf[buf_pos++];
    pfIBRtextureFlags(tex, 0xffffffff, 0); /* set all to 0 */
    pfIBRtextureFlags(tex, i, 1); /* set those that are set in 'i' */

    pfIBRtextureDirection(tex, ((float *)buf)[buf_pos++]);

    n = buf[buf_pos++];
    if(n > 0)
    {
	int   numInRing[PFDD_MAX_RINGS];
	float horAngle[PFDD_MAX_RINGS];
	
	/* n rings specified */	
	numDirs = 0;
	for(i=0; i<n; i++)
	{
	    numInRing[i] = buf[buf_pos++];
	    horAngle[i] = ((float *)buf)[buf_pos++];
	    numDirs += numInRing[i];
	}

	dirs = pfMalloc(sizeof(pfVec3) * numDirs, glb->arena);

	/* compute the directions */
	i = 0;

	for(j=0; j<n; j++)
	{
	    float sinH, cosH, sinV, cosV, angle;

	    sinH = fsin(horAngle[j]/ 180.0 *M_PI);
	    cosH = fcos(horAngle[j]/ 180.0 *M_PI);
	
	    for(t=0; t<numInRing[j]; t++)
	    {
		angle = (float)t / (float)(numInRing[j]) * 360.0;
		sinV = fsin(angle / 180.0 * M_PI);
		cosV = fcos(angle / 180.0 * M_PI);
	    
		dirs[i][0] = -cosH *sinV;
		dirs[i][1] = cosH *cosV;
		dirs[i][2] = -sinH;
		i++;
	    }
	}
    }
    else
    {
	/* read in the directions directly */
	numDirs = buf[buf_pos++];

	dirs = pfMalloc(sizeof(pfVec3) * numDirs, glb->arena);
	
	for(i=0; i<numDirs; i++)
	{
	    dirs[i][0] = ((float *)buf)[buf_pos++];
	    dirs[i][1] = ((float *)buf)[buf_pos++];
	    dirs[i][2] = ((float *)buf)[buf_pos++];
	}
    }
    pfIBRtextureIBRdirections(tex, dirs, numDirs);


    numTex = buf[buf_pos++];

    textures = pfMalloc(sizeof(pfTexture *) * numTex, glb->arena);

    for(i=0; i<numTex; i++)
    { 
	textures[i] = glb->rl_list[L_TEX][buf[buf_pos++]];
    }
    pfIBRtextureIBRTexture(tex, textures, numTex);

    return((pfObject *)tex);
}

static void *pfa_read_mesh(pfa_global_t *glb)
{
    int    g, f, nf, v, vt, nv, nb, nnb, flags, part;
    int    lines, vl, vlines, nl, nlines, fl, flines, fvl, fvlines;
    int    grids[3];
    pfBox  bbox;
    pfSphere bsphere;
    pfMeshFace *face;
    pfMeshVertex *vert;
    pfMeshVertexNeighbor neighbor;
    pfVec2 texCoord;
    pfVec3 coord;
    pfMesh *mesh;

    mesh = pfNewMesh(glb->arena);

    READ_LINE();
    /* read the number of lines the mesh takes, together with
       length of vertex (without neighbors),
       length of vertex neighbor, length of face (without face vertices),
       length of face vertex */
    sscanf(glb->line_buf, "%d %d %d %d %d\n", 
	   &lines, &vlines, &nlines, &flines, &fvlines);
    lines--; 

    /* flags */
    READ_LINE(); lines--;
    flags = atoi(glb->line_buf);
    pfMeshFlags(mesh, 0xffffffff, 0); /* set all to 0 */
    pfMeshFlags(mesh, flags, 1);

    /* gsetlist left empty */

    /* bbox */
    READ_LINE(); lines--;
    sscanf(glb->line_buf, "%g %g %g  %g %g %g", 
	   &bbox.min[0], &bbox.min[1], &bbox.min[2],
	   &bbox.max[0], &bbox.max[1], &bbox.max[2]);
    pfMeshBBox(mesh, &bbox);

     /* grid: bsphere and size */
    READ_LINE(); lines--;
    sscanf(glb->line_buf, "%g %g %g  %g", 
	   &bsphere.center[0], &bsphere.center[1], &bsphere.center[2], 
	   &bsphere.radius);
    pfMeshGridBsphere(mesh, &bsphere);

    READ_LINE(); lines--;
    sscanf(glb->line_buf, "%d %d %d", 
	   &grids[0], &grids[1], &grids[2]);
    pfMeshVal(mesh, PFM_VERTEX_GRID_SIZE_X, grids[0]);
    pfMeshVal(mesh, PFM_VERTEX_GRID_SIZE_Y, grids[1]);
    pfMeshVal(mesh, PFM_VERTEX_GRID_SIZE_Z, grids[2]);

    /* epsilon */
    READ_LINE(); lines--;
    pfMeshVal(mesh, PFM_EPSILON, atof(glb->line_buf));

    /* vertices */
    READ_LINE(); lines--;
    nv = atoi(glb->line_buf);
    pfMeshNumVertices(mesh, nv);

    for(v = 0; v < nv; v++)
    {
	vert = pfGetMeshVertex(mesh, v);

	vl = vlines;

	/* flags */
	READ_LINE(); lines--; vl--;
	pfFlagsMeshVertex(vert,  atoi(glb->line_buf), 1);

	pfMeshVertexCoordPtr(vert, NULL);

	/* coords */
	READ_LINE(); lines--; vl--;
        sscanf(glb->line_buf, "%g %g %g", &coord[0], &coord[1], &coord[2]);
	pfMeshVertexCoord(vert, &coord);

	/* next */
	READ_LINE(); lines--; vl--;
	pfMeshVertexNextVertex(vert, atoi(glb->line_buf));

	/* neighbors */
	READ_LINE(); lines--; vl--;
	nnb = atoi(glb->line_buf);
	pfMeshVertexNumNeighbors(vert, nnb);
	
	for(nb = 0; nb < nnb; nb++)
	{
	    nl = nlines; 

	    READ_LINE(); lines--; nl--;
	    
	    sscanf(glb->line_buf, "%d %d %hd", 
		   &neighbor.vertex, &neighbor.face, &neighbor.edgeType);

	    if(nlines >= 2)
	    {
		READ_LINE(); lines--; nl--;
		sscanf(glb->line_buf, "%hd %hd", &neighbor.prev, &neighbor.next);
	    }
	    else
	    {
		neighbor.prev = nb == 0 ? nnb-1 : nb-1;
		neighbor.next = nb == nnb-1 ? 0 : nb+1;
	    }
	    pfMeshVertexSetNeighbor(vert, nb, &neighbor);

	    while(nl > 0)
	    {
		READ_LINE(); lines--; nl--;
	    }
	}

	while(vl > 0)
	{
	    READ_LINE(); lines--; vl--;
	}
    }

    /* faces */
    READ_LINE(); lines--;
    nf = atoi(glb->line_buf);
    pfMeshNumFaces(mesh, nf);

    for(f = 0; f < nf; f++)
    {
	face = pfGetMeshFace(mesh, f);

	fl = flines;

	/* flags and part */
	READ_LINE(); lines--; fl--;
	sscanf(glb->line_buf, "%d %d", &flags, &part);
	pfMeshFaceFlags(face, 0xffffffff, 0); /* set all to 0 */
	pfMeshFaceFlags(face, flags, 1);
	pfMeshFacePart(face, part);
	
	/* geostate */
	READ_LINE(); lines--; fl--;
	g = atoi(glb->line_buf);
	pfMeshFaceGState(face, g >= 0 ? glb->rl_list[L_GSTATE][g] : NULL);

	/* vertices and texCoords */
	READ_LINE(); lines--; fl--;
	nv = atoi(glb->line_buf);
	pfMeshFaceNumVerts(face, nv);

	for(v = 0; v < nv; v++)
	{
	    fvl = fvlines;

	    READ_LINE(); lines--; fvl--;
	    sscanf(glb->line_buf, "%d %g %g", &vt, &texCoord[0], &texCoord[1]);

	    pfMeshFaceVertex(face, v, vt);
	    pfMeshFaceTexCoord(face, v, &texCoord);

	    while(fvl > 0)
	    {
		READ_LINE(); lines--; fvl--;
	    }
	}

	while(fl > 0)
	{
	    READ_LINE(); lines--; fl--;
	}
    } 

    while(lines > 0)
    {
	READ_LINE(); lines--;
    }

    return (void *)mesh;
}


static void *pfb_read_mesh(pfa_global_t *glb)
{
    int    g, c, f, nf, v, nv, nb, nnb;
    int    size, vs, vsize, ns, nsize, fs, fsize, fvs, fvsize;
    int    grids[3];
    pfBox  bbox;
    pfSphere bsphere;
    pfMeshFace *face;
    pfMeshVertex *vert;
    pfMeshVertexNeighbor neighbor;
    pfVec2 texCoord;
    pfVec3 coord;
    pfMesh *mesh;
    int    buf_pos;
    int    *buf;

    pfb_fread(&size, sizeof(int), 1, glb->ifp);
    buf = GET_BUF(size);
    pfb_fread(buf, sizeof(int), size, glb->ifp);

    buf_pos = 0;

    mesh = pfNewMesh(glb->arena);

    /* read the length of vertex (without neighbors),
       length of vertex neighbor, length of face (without face vertices),
       length of face vertex */
    c = buf[buf_pos++]; 
    size--;
    vsize = c>>24;
    nsize = (c>>16) & 0xff;
    fsize = (c>>8) & 0xff;
    fvsize = c & 0xff;

    /*printf("size = %d, vsize = %d, nsize = %d, fsize = %d, fvsize = %d\n",
      size, vsize, nsize, fsize, fvsize);*/

    /* flags */
    pfMeshFlags(mesh, 0xffffffff, 0); /* set all to 0 */
    pfMeshFlags(mesh, buf[buf_pos++], 1);
    size--;

    /* gsetlist left empty */

    /* bbox */
    for(c=0; c<3; c++)
	bbox.min[c] = ((float *)buf)[buf_pos++];
    for(c=0; c<3; c++)
	bbox.max[c] = ((float *)buf)[buf_pos++];
    size -= 6;
 
    pfMeshBBox(mesh, &bbox);

    /* grid: bsphere and size */
    for(c=0; c<3; c++)
	bsphere.center[c] = ((float *)buf)[buf_pos++];
    bsphere.radius = ((float *)buf)[buf_pos++];
    size -= 4;

    pfMeshGridBsphere(mesh, &bsphere);

    for(c=0; c<3; c++)
	grids[c] = buf[buf_pos++];
    size -= 3;
    pfMeshVal(mesh, PFM_VERTEX_GRID_SIZE_X, grids[0]);
    pfMeshVal(mesh, PFM_VERTEX_GRID_SIZE_Y, grids[1]);
    pfMeshVal(mesh, PFM_VERTEX_GRID_SIZE_Z, grids[2]);

    /* epsilon */
    pfMeshVal(mesh, PFM_EPSILON, ((float *)buf)[buf_pos++]);
    size--;

    /* vertices */
    nv = buf[buf_pos++];
    size--;
    pfMeshNumVertices(mesh, nv);

    /*printf("nv = %d\n", nv);*/

    for(v = 0; v < nv; v++)
    {
	vert = pfGetMeshVertex(mesh, v);

	vs = vsize;

	/* flags */
	pfFlagsMeshVertex(vert, buf[buf_pos++] , 1);
	size--;  vs--;

	pfMeshVertexCoordPtr(vert, NULL);

	/* coords */
	for(c=0; c<3; c++)
	    coord[c] = ((float *)buf)[buf_pos++];
	size -= 3;  vs -= 3;
	pfMeshVertexCoord(vert, &coord);

	pfMeshVertexNextVertex(vert, buf[buf_pos++]);
	size--;  vs--;

	/* neighbors */
	nnb = buf[buf_pos++];
	size--;  vs--;
	pfMeshVertexNumNeighbors(vert, nnb);
	
	for(nb = 0; nb < nnb; nb++)
	{
	    ns = nsize;

	    neighbor.vertex = buf[buf_pos++];
	    neighbor.face = buf[buf_pos++];
	    neighbor.edgeType = buf[buf_pos++];

	    if(nsize >= 5)
	    {
		size -= 5;  ns -= 5;
		neighbor.prev = buf[buf_pos++];
		neighbor.next = buf[buf_pos++];
	    }
	    else
	    {
		size -= 3;  ns -= 3;
		neighbor.prev = nb == 0 ? nnb-1 : nb-1;
		neighbor.next = nb == nnb-1 ? 0 : nb+1;
	    }
	    pfMeshVertexSetNeighbor(vert, nb, &neighbor);

	    buf_pos += ns; size -= ns;
	}

	buf_pos += vs; size -= vs;
    }

    /* faces */
    nf = buf[buf_pos++];
    size--;
    pfMeshNumFaces(mesh, nf);

    /*printf("nf = %d\n", nf);*/

    for(f = 0; f < nf; f++)
    {
	face = pfGetMeshFace(mesh, f);

	fs = fsize;

	/* flags and part */
	pfMeshFaceFlags(face, 0xffffffff, 0); /* set all to 0 */
	pfMeshFaceFlags(face, buf[buf_pos++], 1);
	pfMeshFacePart(face, buf[buf_pos++]);
	
	/* geostate */
	g = buf[buf_pos++];
	pfMeshFaceGState(face, g >= 0 ? glb->rl_list[L_GSTATE][g] : NULL);

	size -= 3;  fs -= 3;

	/* vertices and texCoords */
	nv = buf[buf_pos++];
	pfMeshFaceNumVerts(face, nv);
	size--;  fs--;

	for(v = 0; v < nv; v++)
	{
	    fvs = fvsize;

	    pfMeshFaceVertex(face, v, buf[buf_pos++]);

	    texCoord[0] = ((float *)buf)[buf_pos++];
	    texCoord[1] = ((float *)buf)[buf_pos++];
	    size -= 3;  fvs -= 3;

	    pfMeshFaceTexCoord(face, v, &texCoord);

	    buf_pos += fvs; size -= fvs;
	}

	buf_pos += fs; size -= fs;
    } 

    return (void *)mesh;
}

static void *pfa_read_tenv(pfa_global_t *glb)
{
    pfTexEnv *tenv;
    tenv_t t;

    READ_LINE();
    t.mode = atoi(glb->line_buf);
    READ_LINE();
    t.component = atoi(glb->line_buf);
    READ_LINE();
    sscanf(glb->line_buf, "%f %f %f %f",
	   &t.color[0], &t.color[1], &t.color[2], &t.color[3]);
    READ_LINE();
    t.udata = atoi(glb->line_buf);

    tenv = pfNewTEnv(glb->arena);
    pfTEnvMode(tenv, tem_table[t.mode]);
    pfTEnvComponent(tenv, tec_table[t.component]);
    pfTEnvBlendColor(tenv, t.color[0], t.color[1], t.color[2], t.color[3]);
    if (t.udata != -1)
	set_udata((pfObject *)tenv, t.udata, glb);

    return(share_obj((pfObject *)tenv, glb));
}


static void *pfb_read_tenv(pfa_global_t *glb)
{
    pfTexEnv *tenv;
    tenv_t t;

    pfb_fread(&t, sizeof(tenv_t), 1, glb->ifp);

    tenv = pfNewTEnv(glb->arena);
    pfTEnvMode(tenv, tem_table[t.mode]);
    pfTEnvComponent(tenv, tec_table[t.component]);
    pfTEnvBlendColor(tenv, t.color[0], t.color[1], t.color[2], t.color[3]);
    if (t.udata != -1)
	set_udata((pfObject *)tenv, t.udata, glb);

    return(share_obj((pfObject *)tenv, glb));
}


static void *pfa_read_fog(pfa_global_t *glb)
{
    pfFog *fog;
    fog_t f;

    READ_LINE();
    f.type = atoi(glb->line_buf);
    READ_LINE();
    sscanf(glb->line_buf, "%g %g %g", &f.color[0], &f.color[1], &f.color[2]);
    READ_LINE();
    sscanf(glb->line_buf, "%g %g", &f.range[0], &f.range[1]);
    READ_LINE();
    sscanf(glb->line_buf, "%g %g", &f.offsets[0], &f.offsets[1]);
    READ_LINE();
    f.r_points = atoi(glb->line_buf);
    READ_LINE();
    sscanf(glb->line_buf, "%g %g %g %g %g %g %g %g %g %g %g %g",
	   &f.r_range[0], &f.r_range[1], &f.r_range[2], &f.r_range[3],
	   &f.r_range[4], &f.r_range[5], &f.r_range[6], &f.r_range[7],
	   &f.r_range[8], &f.r_range[9], &f.r_range[10], &f.r_range[11]);
    READ_LINE();
    sscanf(glb->line_buf, "%g %g %g %g %g %g %g %g %g %g %g %g",
	   &f.r_density[0], &f.r_density[1], &f.r_density[2],
	   &f.r_density[3], &f.r_density[4], &f.r_density[5],
	   &f.r_density[6], &f.r_density[7], &f.r_density[8],
	   &f.r_density[9], &f.r_density[10], &f.r_density[11]);
    READ_LINE();
    f.r_bias = atof(glb->line_buf);
    READ_LINE();
    f.udata = atoi(glb->line_buf);

    fog = pfNewFog(glb->arena);
    pfFogType(fog, fogt_table[f.type]);
    pfFogColor(fog, f.color[0], f.color[1], f.color[2]);
    pfFogRange(fog, f.range[0], f.range[1]);
    pfFogOffsets(fog, f.offsets[0], f.offsets[1]);
    pfFogRamp(fog, f.r_points, f.r_range, f.r_density, f.r_bias);
    if (f.udata != -1)
	set_udata((pfObject *)fog, f.udata, glb);

    return(share_obj((pfObject *)fog, glb));
}


static void *pfb_read_fog(pfa_global_t *glb)
{
    pfFog *fog;
    fog_t f;

    pfb_fread(&f, sizeof(fog_t), 1, glb->ifp);

    fog = pfNewFog(glb->arena);
    pfFogType(fog, fogt_table[f.type]);
    pfFogColor(fog, f.color[0], f.color[1], f.color[2]);
    pfFogRange(fog, f.range[0], f.range[1]);
    pfFogOffsets(fog, f.offsets[0], f.offsets[1]);
    pfFogRamp(fog, f.r_points, f.r_range, f.r_density, f.r_bias);
    if (f.udata != -1)
	set_udata((pfObject *)fog, f.udata, glb);

    return(share_obj((pfObject *)fog, glb));
}


static void *pfa_read_tgen(pfa_global_t *glb)
{
    pfTexGen *tgen;
    tgen_t t;

    READ_LINE();
    sscanf(glb->line_buf, "%d %d %d %d",
	   &t.mode_s, &t.mode_t, &t.mode_r, &t.mode_q);
    READ_LINE();
    sscanf(glb->line_buf, "%f %f %f %f",
	   &t.plane_s[0], &t.plane_s[1], &t.plane_s[2], &t.plane_s[3]);
    READ_LINE();
    sscanf(glb->line_buf, "%f %f %f %f",
	   &t.plane_t[0], &t.plane_t[1], &t.plane_t[2], &t.plane_t[3]);
    READ_LINE();
    sscanf(glb->line_buf, "%f %f %f %f",
	   &t.plane_r[0], &t.plane_r[1], &t.plane_r[2], &t.plane_r[3]);
    READ_LINE();
    sscanf(glb->line_buf, "%f %f %f %f",
	   &t.plane_q[0], &t.plane_q[1], &t.plane_q[2], &t.plane_q[3]);
    READ_LINE();
    t.udata = atoi(glb->line_buf);

    tgen = pfNewTGen(glb->arena);
    pfTGenMode(tgen, PF_S, tgen_table[t.mode_s]);
    pfTGenMode(tgen, PF_T, tgen_table[t.mode_t]);
    pfTGenMode(tgen, PF_R, tgen_table[t.mode_r]);
    pfTGenMode(tgen, PF_Q, tgen_table[t.mode_q]);
    pfTGenPlane(tgen, PF_S,
		t.plane_s[0], t.plane_s[1], t.plane_s[2], t.plane_s[3]);
    pfTGenPlane(tgen, PF_T,
		t.plane_t[0], t.plane_t[1], t.plane_t[2], t.plane_t[3]);
    pfTGenPlane(tgen, PF_R,
		t.plane_r[0], t.plane_r[1], t.plane_r[2], t.plane_r[3]);
    pfTGenPlane(tgen, PF_Q,
		t.plane_q[0], t.plane_q[1], t.plane_q[2], t.plane_q[3]);
    if (t.udata != -1)
	set_udata((pfObject *)tgen, t.udata, glb);

    return(share_obj((pfObject *)tgen, glb));
}


static void *pfb_read_tgen(pfa_global_t *glb)
{
    pfTexGen *tgen;
    tgen_t t;

    pfb_fread(&t, sizeof(tgen_t), 1, glb->ifp);

    tgen = pfNewTGen(glb->arena);
    pfTGenMode(tgen, PF_S, tgen_table[t.mode_s]);
    pfTGenMode(tgen, PF_T, tgen_table[t.mode_t]);
    pfTGenMode(tgen, PF_R, tgen_table[t.mode_r]);
    pfTGenMode(tgen, PF_Q, tgen_table[t.mode_q]);
    pfTGenPlane(tgen, PF_S,
		t.plane_s[0], t.plane_s[1], t.plane_s[2], t.plane_s[3]);
    pfTGenPlane(tgen, PF_T,
		t.plane_t[0], t.plane_t[1], t.plane_t[2], t.plane_t[3]);
    pfTGenPlane(tgen, PF_R,
		t.plane_r[0], t.plane_r[1], t.plane_r[2], t.plane_r[3]);
    pfTGenPlane(tgen, PF_Q,
		t.plane_q[0], t.plane_q[1], t.plane_q[2], t.plane_q[3]);
    if (t.udata != -1)
	set_udata((pfObject *)tgen, t.udata, glb);

    return(share_obj((pfObject *)tgen, glb));
}


static void *pfa_read_tlod(pfa_global_t *glb)
{
    pfTexLOD *tlod;
    tlod_t t;

/*
// bjk: it seems to be a bug in the pfb loader to read the texlod
// in binary AND ascii form....
//     pfb_fread(&t, sizeof(tlod_t), 1, glb->ifp);
*/

    READ_LINE();
    sscanf(glb->line_buf, "%f %f", &t.range[0], &t.range[1]);
    READ_LINE();
    sscanf(glb->line_buf, "%f %f %f", &t.bias[0], &t.bias[1], &t.bias[2]);
    READ_LINE();
    t.udata = atoi(glb->line_buf);

    tlod = pfNewTLOD(glb->arena);
    pfTLODRange(tlod, t.range[0], t.range[1]);
    pfTLODBias(tlod, t.bias[0], t.bias[1], t.bias[2]);
    if (t.udata != -1)
	set_udata((pfObject *)tlod, t.udata, glb);

    return(tlod);
}


static void *pfb_read_tlod(pfa_global_t *glb)
{
    pfTexLOD *tlod;
    tlod_t t;

    pfb_fread(&t, sizeof(tlod_t), 1, glb->ifp);

    tlod = pfNewTLOD(glb->arena);
    pfTLODRange(tlod, t.range[0], t.range[1]);
    pfTLODBias(tlod, t.bias[0], t.bias[1], t.bias[2]);
    if (t.udata != -1)
	set_udata((pfObject *)tlod, t.udata, glb);

    return(tlod);
}

static void *pfa_read_shader_object(pfa_global_t *glb)
{
  int shaderType, bufSize;
  pfShaderObject *so = NULL;

  READ_LINE();
  shaderType = atoi(glb->line_buf);
  READ_LINE();
  bufSize = atoi(glb->line_buf);

  so = pfNewShaderObject(glb->arena);
  pfShaderObjectShaderType(so, shaderType);

  if(bufSize == 0) { /* we're only storing the filename ... */
    READ_LINE();
    
    if(strchr(glb->line_buf,'\n'))
      *(strchr(glb->line_buf,'\n')) = '\0';
    
    pfShaderObjectName(so, glb->line_buf);
    pfShaderObjectLoad(so);
  } else {
    /* else shader code is inlined and the isInlined
     * variable will tell us how many lines of code
     * to read.
     */
    int readCount = 0;
    /* char *buf = (char *)pfCalloc(bufSize, sizeof(char), glb->arena); */
    char *buf = (char *)pfMalloc(sizeof(char) * (bufSize+1), glb->arena);
    
    memset(buf,'\0',bufSize+1);

    while(readCount < bufSize) {
      READ_LINE();
      if(buf)
	strcat(buf,glb->line_buf);
      /* else we weren't able to alloc enough memory to read shader code */

      readCount += strlen(glb->line_buf);
    }

    if(buf) {
      pfShaderObjectSource(so, buf);
      pfFree(buf);
    }
  }

  return so;
}

static void *pfb_read_shader_object(pfa_global_t *glb)
{
  int shaderType, len, isInlined;
  pfShaderObject *so = NULL;

  pfb_fread(&shaderType, sizeof(int), 1, glb->ifp);
  pfb_fread(&isInlined, sizeof(int), 1, glb->ifp);

  so = pfNewShaderObject(glb->arena);
  pfShaderObjectShaderType(so, shaderType);

  if(isInlined == 0) {
    pfb_fread(&len, sizeof(int), 1, glb->ifp);

    if(len>0) {
      char *str = pfMalloc(len+1, glb->arena);
      if(!str)
	return NULL;
      
      str[len] = '\0';
      pfb_fread(str, sizeof(char), len, glb->ifp);
      
      pfShaderObjectName(so, str);
      pfShaderObjectLoad(so);
      pfFree(str);
      return so;
    } else
      return NULL;
  } else {
    /* char *str = pfCalloc(isInlined, sizeof(char), glb->arena); */
    char *str = (char *)pfMalloc(sizeof(char) * (isInlined + 1), glb->arena);
    
    memset(str,'\0',isInlined+1);
    if(str) {
      /* str[isInlined-1] = '\0'; */
      pfb_fread(str, sizeof(char), isInlined, glb->ifp);
      pfShaderObjectSource(so, str);
      /* don't free str */
    }
  }

  return so;
}

static void* pfb_read_uniform_data(void *data,int uniType,int numVals,pfa_global_t *glb)
{
  switch(uniType) {
  case PFUNI_FLOAT1: 
    pfb_fread(data, sizeof(GLfloat), 1 * numVals, glb->ifp);
    break;
  case PFUNI_FLOAT2:
    pfb_fread(data, sizeof(GLfloat), 2 * numVals, glb->ifp);
    break;
  case PFUNI_FLOAT3:
    pfb_fread(data, sizeof(GLfloat), 3 * numVals, glb->ifp);
    break;
  case PFUNI_MAT2:
  case PFUNI_FLOAT4:
    pfb_fread(data, sizeof(GLfloat), 4 * numVals, glb->ifp);
    break;

  case PFUNI_BOOL1:
  case PFUNI_INT1: 
  case PFUNI_SAMP1D:
  case PFUNI_SAMP2D:
  case PFUNI_SAMP3D:
  case PFUNI_SAMPCUBE:
  case PFUNI_SAMP1DSHADOW:
  case PFUNI_SAMP2DSHADOW:
    pfb_fread(data, sizeof(GLint), 1 * numVals, glb->ifp);
    break;
  case PFUNI_BOOL2:
  case PFUNI_INT2:
    pfb_fread(data, sizeof(GLint), 2 * numVals, glb->ifp);
    break;
  case PFUNI_BOOL3:
  case PFUNI_INT3:
    pfb_fread(data, sizeof(GLint), 3 * numVals, glb->ifp);
    break;
  case PFUNI_BOOL4:
  case PFUNI_INT4:
    pfb_fread(data, sizeof(GLint), 4 * numVals, glb->ifp);
    break;

  case PFUNI_MAT3:
    pfb_fread(data, sizeof(GLfloat), 9 * numVals, glb->ifp);
    break;

  case PFUNI_MAT4:
    pfb_fread(data, sizeof(GLfloat), 16 * numVals, glb->ifp);
    break;
  }

  return NULL; /* ret value ignored */
}

static void* pfb_read_shader_program_uniform(pfShaderProgram *sp,pfa_global_t *glb)
{
  char *name;
  int uniType, numVals, len, uniLoc;
  char clampMode, normalize;
  int sizeForOne;
  void *data, *minData = NULL, *maxData = NULL;

  pfb_fread(&uniType, sizeof(int), 1, glb->ifp);
  pfb_fread(&numVals, sizeof(int), 1, glb->ifp);
  pfb_fread(&clampMode, sizeof(char), 1, glb->ifp);
  pfb_fread(&normalize, sizeof(char), 1, glb->ifp);

  pfb_fread(&len, sizeof(int), 1, glb->ifp);

  if(len > 0) {
    name = pfMalloc(len+1, glb->arena);
    if(name) {
      name[len] = '\0';
      pfb_fread(name, sizeof(char), len, glb->ifp);
    }
  }
 
  sizeForOne = pfGetSProgUniTypeSize(uniType);

  data = (void *)pfMalloc(sizeForOne * numVals, glb->arena);
  if(!data) {
    if(name) pfFree(name);
    return NULL;
  }

  pfb_read_uniform_data(data, uniType, numVals, glb);
  
  uniLoc = pfSProgAddUniform(sp, name, uniType, numVals, data);

  if(clampMode & PFUNI_CLAMP_MIN) {
    minData = (void *)pfMalloc(sizeForOne * numVals, glb->arena);
    if(minData) {
      pfb_read_uniform_data(minData, uniType, numVals, glb);
      pfSProgUniformMin(sp, uniLoc, minData);
      pfFree(minData); /* XXX Alex */
    }
  }
  if(clampMode & PFUNI_CLAMP_MAX) {
    maxData = (void *)pfMalloc(sizeForOne * numVals, glb->arena);
    if(maxData) {
      pfb_read_uniform_data(maxData, uniType, numVals, glb);
      pfSProgUniformMax(sp, uniLoc, maxData);
      pfFree(maxData); /* XXX Alex */
    }
  }

  pfSProgUniformClampMode(sp, uniLoc, clampMode);
  pfSProgUniformNormalizedFlag(sp, uniLoc, normalize);

  pfFree(data);

#if 0
  if(name)
    free(name);
#endif

  return sp; /* return value isn't important here, really ... */
}

static void* pfa_read_uniform_data(void *data,int uniType,int numVals,pfa_global_t *glb)
{
  int i;

  switch(uniType) {
  case PFUNI_FLOAT1: 
    {
      GLfloat *v = (GLfloat *)data;
      for(i=0; i<numVals; i++) {
	READ_LINE();
	sscanf(glb->line_buf,"%g",&(v[i]));
      }
    }
    break;
  case PFUNI_FLOAT2:
    {
      GLfloat *v = (GLfloat *)data;
      for(i=0; i<numVals; i++) {
	READ_LINE();
	sscanf(glb->line_buf,"%g %g",&(v[0]),&(v[1])), v+=2;
      }
    }
    break;
  case PFUNI_FLOAT3:
    {
      GLfloat *v = (GLfloat *)data;
      for(i=0; i<numVals; i++) {
	READ_LINE();
	sscanf(glb->line_buf,"%g %g %g\n",&(v[0]),&(v[1]),&(v[2])), v+= 3;
      }
    }
    break;

  case PFUNI_MAT2:
  case PFUNI_FLOAT4:
    {
      GLfloat *v = (GLfloat *)data;
      for(i=0; i<numVals; i++) {
	READ_LINE();
	sscanf(glb->line_buf,"%g %g %g %g\n",&(v[0]),&(v[1]),&(v[2]),&(v[3])), v+= 4;
      }
    }
    break;

  case PFUNI_BOOL1:
  case PFUNI_INT1: 
  case PFUNI_SAMP1D:
  case PFUNI_SAMP2D:
  case PFUNI_SAMP3D:
  case PFUNI_SAMPCUBE:
  case PFUNI_SAMP1DSHADOW:
  case PFUNI_SAMP2DSHADOW:
    {
      GLint *v = (GLint *)data;
      for(i=0; i<numVals; i++) {
	READ_LINE();
	sscanf(glb->line_buf,"%d\n",&(v[i]));
      }
    }
    break;
  case PFUNI_BOOL2:
  case PFUNI_INT2:
    {
      GLint *v = (GLint *)data;
      for(i=0; i<numVals; i++) {
	READ_LINE();
	sscanf(glb->line_buf,"%d %d\n",&(v[0]),&(v[1])), v+= 2;
      }
    }
    break;
  case PFUNI_BOOL3:
  case PFUNI_INT3:
    {
      GLint *v = (GLint *)data;
      for(i=0; i<numVals; i++) {
	READ_LINE();
	sscanf(glb->line_buf,"%d %d %d\n",&(v[0]),&(v[1]),&(v[2])), v+= 3;
      }
    }
    break;
  case PFUNI_BOOL4:
  case PFUNI_INT4:
    {
      GLint *v = (GLint *)data;
      for(i=0; i<numVals; i++) {
	READ_LINE();
	sscanf(glb->line_buf,"%d %d %d %d\n",&(v[0]),&(v[1]),&(v[2]),&(v[3])), v+= 4;
      }
    }
    break;

  case PFUNI_MAT3:
    {
      GLfloat *v = (GLfloat *)data;
      for(i=0; i<numVals; i++) {
	READ_LINE();
	sscanf(glb->line_buf,"%g %g %g %g %g %g %g %g %g\n",
		&(v[0]),&(v[1]),&(v[2]),&(v[3]),&(v[4]),&(v[5]),&(v[6]),&(v[7]),&(v[8])), v+= 9;
      }
    }
    break;

  case PFUNI_MAT4:
    {
      GLfloat *v = (GLfloat *)data;
      for(i=0; i<numVals; i++) {
	READ_LINE();
	sscanf(glb->line_buf,"%g %g %g %g %g %g %g %g %g %g %g %g %g %g %g %g\n",
		&(v[0]),&(v[1]),&(v[2]),&(v[3]),&(v[4]),&(v[5]),&(v[6]),&(v[7]),&(v[8]),&(v[9]),&(v[10]),&(v[11]),&(v[12]),&(v[13]),&(v[14]),&(v[15])), v+= 16;
      }
    }
    break;
  }

  return NULL; /* ret value ignored */
}

static void* pfa_read_shader_program_uniform(pfShaderProgram *sp,pfa_global_t *glb)
{
  char *name;
  int uniType, numVals, i, uniLoc;
  short clampMode, normalize;
  int sizeForOne;
  void *data, *minData = NULL, *maxData = NULL;

  READ_LINE();
  name = strdup(glb->line_buf);
  if(strchr(name,'\n')) {
    *(strchr(name,'\n')) = '\0';
  }

  READ_LINE();
  sscanf(glb->line_buf,"%d %d %hd %hd",&uniType,&numVals,&clampMode,&normalize);
 
  sizeForOne = pfGetSProgUniTypeSize(uniType);
  data = (void *)pfMalloc(sizeForOne * numVals, glb->arena);
  if(!data) {
    /* skip the lines ... but don't do anything with what we've just read in */
    for(i=0; i<numVals; i++)
      READ_LINE();
    return NULL;
  }
  
  pfa_read_uniform_data(data, uniType, numVals, glb);
  uniLoc = pfSProgAddUniform(sp, name, uniType, numVals, data);

  if(clampMode & PFUNI_CLAMP_MIN) {
    minData = (void *)pfMalloc(sizeForOne * numVals, glb->arena);
    if(minData) {
      pfa_read_uniform_data(minData, uniType, numVals, glb);
      pfSProgUniformMin(sp, uniLoc, minData);
      pfFree(minData); /* XXX Alex */
    }
  }
  if(clampMode & PFUNI_CLAMP_MAX) {
    maxData = (void *)pfMalloc(sizeForOne * numVals, glb->arena);
    if(maxData) {
      pfa_read_uniform_data(maxData, uniType, numVals, glb);
      pfSProgUniformMax(sp, uniLoc, maxData);
      pfFree(maxData); /* XXX Alex */
    }
  }

  pfSProgUniformClampMode(sp, uniLoc, (char)clampMode);
  pfSProgUniformNormalizedFlag(sp, uniLoc, (char)normalize);
  
  pfFree(data);
#if 0
  free(name);
#endif

  return sp; /* return value isn't important here, really ... */
}

static void *pfa_read_shader_program(pfa_global_t *glb)
{
  int i, numShaders, numUniforms, shaderIndex;
  pfShaderObject *obj;
  pfShaderProgram *sp;

  READ_LINE();
  numShaders = atoi(glb->line_buf);
  READ_LINE();
  numUniforms = atoi(glb->line_buf);

  sp = pfNewSProg(glb->arena);

  for(i=0; i<numShaders; i++) {
    READ_LINE();
    shaderIndex = atoi(glb->line_buf);
    if(shaderIndex != -1) {
      obj = glb->rl_list[L_SHADOBJ][shaderIndex];
      if(obj)
	pfSProgAddShader(sp, (pfShaderObject *)obj);
    }
  }

  /* Then read uniforms */
  for(i=0; i<numUniforms; i++)
    pfa_read_shader_program_uniform(sp, glb);

  return sp;
}

static void *pfb_read_shader_program(pfa_global_t *glb)
{
  int i, numShaders, numUniforms, shaderIndex;
  pfShaderObject *obj;
  pfShaderProgram *sp;

  pfb_fread(&numShaders, sizeof(int), 1, glb->ifp);
  pfb_fread(&numUniforms, sizeof(int), 1, glb->ifp);

  sp = pfNewSProg(glb->arena);

  for(i=0; i<numShaders; i++) {
    pfb_fread(&shaderIndex, sizeof(int), 1, glb->ifp);
    if(shaderIndex != -1) {
      obj = glb->rl_list[L_SHADOBJ][shaderIndex];
      if(obj)
	pfSProgAddShader(sp, (pfShaderObject *)obj);
    }
  }

  /* Then read uniforms */
  for(i=0; i<numUniforms; i++)
    pfb_read_shader_program_uniform(sp, glb);

  return sp;
}

static void *pfa_read_gprogram(pfa_global_t *glb)
{
    unsigned char *str;
    int i, j, size, type;
    pfGProgram *gp;

    READ_LINE();
    type = atoi(glb->line_buf);

    if(type == 0)
        gp = (pfGProgram *)pfNewVertexProgram(glb->arena);
    else
        gp = (pfGProgram *)pfNewFragmentProgram(glb->arena);

    READ_LINE();
    if ((size = atoi(glb->line_buf)) > 0);
    {
	str = pfMalloc(size, glb->arena);
	READ_LINE();
	for (i = 0; i < size; i++)
	{
	    j = (i % 39) * 2;

	    if (glb->line_buf[j] >= '0' && glb->line_buf[j] <= '9')
		str[i] = (glb->line_buf[j] - '0') << 4;
	    else
		str[i] = (glb->line_buf[j] - 'a' + 10) << 4;

	    if (glb->line_buf[j+1] >= '0' && glb->line_buf[j+1] <= '9')
		str[i] |= glb->line_buf[j+1] - '0';
	    else
		str[i] |= glb->line_buf[j+1] - 'a' + 10;

	    if (j == 76 && i < size - 1)
		READ_LINE();
	}
	pfGProgramProgramLen(gp, str, size);
    }

    return(gp);
}

static void *pfb_read_gprogram(pfa_global_t *glb)
{
    unsigned char *str;
    int size, type;
    pfGProgram *gp;

    pfb_fread(&type, sizeof(int), 1, glb->ifp);

    if(type == 0)
        gp = (pfGProgram *)pfNewVertexProgram(glb->arena);
    else
        gp = (pfGProgram *)pfNewFragmentProgram(glb->arena);

    pfb_fread(&size, sizeof(int), 1, glb->ifp);
    if (size > 0)
    {
	str = pfMalloc(size, glb->arena);
	pfb_fread(str, 1, size, glb->ifp);
	pfGProgramProgramLen(gp, str, size);
	
	pfNotify(PFNFY_DEBUG, PFNFY_PRINT, str);
    }

    return(gp);
}

static void *pfa_read_gprogramparms(pfa_global_t *glb)
{

    pfGProgramParms *gpp;
    unsigned char *s;
    int   i, j, num, ix, count, type;
    float *p;

    READ_LINE();
    sscanf(glb->line_buf, "%d %d", &type, &num);

    gpp = pfNewGPParams(type, glb->arena);

    for (i = 0; i < num; i++)
    {
	READ_LINE();	

	ix  = strtol(glb->line_buf, &s, 10);
	type = strtol(s, &s, 10);
	count = strtol(s, &s, 10);

	p = (float *)pfMalloc(sizeof(float) * count, glb->arena);

	for(j=0; j<count; j++)
#if defined(WIN32) || (defined(sgi) && defined(_ABIO32))
	    p[j] = strtod(s, &s);
#else
	    p[j] = strtold(s, &s);
#endif

        pfGPParamsParameters(gpp, ix, type, count, (void*)p);
    }

    return (void *)gpp;
}

static void *pfb_read_gprogramparms(pfa_global_t *glb)
{

    pfGProgramParms *gpp;
    int   i, num, ix, count, type;
    float *p;

    pfb_fread(&type, sizeof(int), 1, glb->ifp);

    gpp = pfNewGPParams(type, glb->arena);

    pfb_fread(&num, sizeof(int), 1, glb->ifp);

    for (i = 0; i < num; i++)
    {
	pfb_fread(&ix, sizeof(int), 1, glb->ifp);
	pfb_fread(&type, sizeof(int), 1, glb->ifp);
	pfb_fread(&count, sizeof(int), 1, glb->ifp);

	p = (float *)pfMalloc(sizeof(float) * count, glb->arena);

	pfb_fread(p, sizeof(float), count, glb->ifp);

        pfGPParamsParameters(gpp, ix, type, count, (void*)p);
    }

    return (void *)gpp;
}


static void *pfa_read_ctab(pfa_global_t *glb)
{
    pfColortable *ctab;
    int i, size;
    pfVec4 color;

    READ_LINE();
    size = atoi(glb->line_buf);
    ctab = pfNewCtab(size, glb->arena);

    for (i = 0; i < size; i++)
    {
	READ_LINE();
	sscanf(glb->line_buf, "%g %g %g %g",
	       &color[0], &color[1], &color[2], &color[3]);
	pfCtabColor(ctab, i, color);
    }

    READ_LINE();
    i = atoi(glb->line_buf);
    if (i != -1)
	set_udata((pfObject *)ctab, i, glb);

    return(share_obj((pfObject *)ctab, glb));
}


static void *pfb_read_ctab(pfa_global_t *glb)
{
    pfColortable *ctab;
    int i, j, size, count;
    pfVec4 colors[256];

    pfb_fread(&size, sizeof(int), 1, glb->ifp);
    ctab = pfNewCtab(size, glb->arena);

    for (i = 0; i < size;)
    {
	if (size - i > 256)
	    count = 256;
	else
	    count = size - i;
	pfb_fread(colors, sizeof(pfVec4), count, glb->ifp);
	for (j = 0; j < count; i++, j++)
	    pfCtabColor(ctab, i, colors[j]);
    }

    pfb_fread(&i, sizeof(int), 1, glb->ifp);
    if (i != -1)
	set_udata((pfObject *)ctab, i, glb);

    return(share_obj((pfObject *)ctab, glb));
}


static void *pfa_read_lpstate(pfa_global_t *glb)
{
    pfLPointState *lpstate;
    lpstate_t l;

    if (glb->version >= PFBV_LPSTATE_CALLIG)
    {
	READ_LINE();
	sscanf(glb->line_buf, "%d %d %d %d %d %d %d %d",
	       &l.size_mode, &l.transp_mode, &l.fog_mode, &l.dir_mode,
	       &l.shape_mode, &l.range_mode, &l.draw_mode, &l.quality_mode);
	READ_LINE();
	sscanf(glb->line_buf, "%f %f %f %f %f %f %f %f %f %f %f %f %f",
	       &l.size_min_pixel, &l.size_max_pixel, &l.size_actual,
	       &l.transp_pixel_size, &l.transp_exponent, &l.transp_scale,
	       &l.transp_clamp, &l.fog_scale, &l.intensity,
	       &l.size_diff_thresh,
	       &l.significance, &l.min_defocus, &l.max_defocus);
    }
    else
    {
	READ_LINE();
	sscanf(glb->line_buf, "%d %d %d %d %d %d",
	       &l.size_mode, &l.transp_mode, &l.fog_mode, &l.dir_mode,
	       &l.shape_mode, &l.range_mode);
	READ_LINE();
	sscanf(glb->line_buf, "%f %f %f %f %f %f %f %f %f %f",
	       &l.size_min_pixel, &l.size_max_pixel, &l.size_actual,
	       &l.transp_pixel_size, &l.transp_exponent, &l.transp_scale,
	       &l.transp_clamp, &l.fog_scale, &l.intensity,
	       &l.size_diff_thresh);
    }
    READ_LINE();
    sscanf(glb->line_buf, "%f %f %f %f %f",
	   &l.shape[0], &l.shape[1], &l.shape[2], &l.shape[3], &l.shape[4]);
    READ_LINE();
    sscanf(glb->line_buf, "%f %f %f %f", &l.back_color[0], &l.back_color[1],
	   &l.back_color[2], &l.back_color[3]);
    if (glb->version >= PFBV_UFUNC)
    {
	READ_LINE();
	sscanf(glb->line_buf, "%d %d %d %d",
	       &l.rfunc, &l.rfunc_data, &l.cfunc, &l.cfunc_data);
    }
    READ_LINE();
    l.udata = atoi(glb->line_buf);

    lpstate = pfNewLPState(glb->arena);

    pfLPStateMode(lpstate, PFLPS_SIZE_MODE, lpssm_table[l.size_mode]);
    pfLPStateMode(lpstate, PFLPS_TRANSP_MODE, lpstm_table[l.transp_mode]);
    pfLPStateMode(lpstate, PFLPS_FOG_MODE, lpsfm_table[l.fog_mode]);
    pfLPStateMode(lpstate, PFLPS_DIR_MODE, lpsdm_table[l.dir_mode]);
    pfLPStateMode(lpstate, PFLPS_SHAPE_MODE, lpsshm_table[l.shape_mode]);
    pfLPStateMode(lpstate, PFLPS_RANGE_MODE, lpsrm_table[l.range_mode]);
    pfLPStateVal(lpstate, PFLPS_SIZE_MIN_PIXEL, l.size_min_pixel);
    pfLPStateVal(lpstate, PFLPS_SIZE_MAX_PIXEL, l.size_max_pixel);
    pfLPStateVal(lpstate, PFLPS_SIZE_ACTUAL, l.size_actual);
    pfLPStateVal(lpstate, PFLPS_TRANSP_PIXEL_SIZE, l.transp_pixel_size);
    pfLPStateVal(lpstate, PFLPS_TRANSP_EXPONENT, l.transp_exponent);
    pfLPStateVal(lpstate, PFLPS_TRANSP_SCALE, l.transp_scale);
    pfLPStateVal(lpstate, PFLPS_TRANSP_CLAMP, l.transp_clamp);
    pfLPStateVal(lpstate, PFLPS_FOG_SCALE, l.fog_scale);
    pfLPStateVal(lpstate, PFLPS_INTENSITY, l.intensity);
    pfLPStateVal(lpstate, PFLPS_SIZE_DIFF_THRESH, l.size_diff_thresh);
    pfLPStateShape(lpstate, l.shape[0], l.shape[1], l.shape[2],
		   l.shape[3], l.shape[4]);
    pfLPStateBackColor(lpstate, l.back_color[0], l.back_color[1],
		       l.back_color[2], l.back_color[3]);
    if (l.udata != -1)
	set_udata((pfObject *)lpstate, l.udata, glb);
    if (glb->version >= PFBV_LPSTATE_CALLIG)
    {
	pfLPStateVal(lpstate, PFLPS_SIGNIFICANCE, l.significance);
	pfLPStateVal(lpstate, PFLPS_MIN_DEFOCUS, l.min_defocus);
	pfLPStateVal(lpstate, PFLPS_MAX_DEFOCUS, l.max_defocus);
	pfLPStateMode(lpstate, PFLPS_DRAW_MODE, lpsdrwm_table[l.draw_mode]);
	pfLPStateMode(lpstate, PFLPS_QUALITY_MODE, lpsqm_table[l.quality_mode]);
    }
    if (glb->version >= PFBV_UFUNC)
    {
	void *func;

	if (l.rfunc != -1)
	{
	    func = glb->rl_list[L_UFUNC][l.rfunc];
	    if (l.rfunc_data == -1)
		pfRasterFunc(lpstate, func, NULL);
	}
	else
	    func = NULL;
	if (l.rfunc_data != -1)
	    set_raster_func(lpstate, func, l.rfunc_data, glb);
	if (l.cfunc != -1)
	{
	    func = glb->rl_list[L_UFUNC][l.cfunc];
	    if (l.cfunc_data == -1)
		pfCalligFunc(lpstate, func, NULL);
	}
	else
	    func = NULL;
	if (l.cfunc_data != -1)
	    set_callig_func(lpstate, func, l.cfunc_data, glb);
    }

    return(lpstate);
}


static void *pfb_read_lpstate(pfa_global_t *glb)
{
    pfLPointState *lpstate;
    lpstate_t l;

    if (glb->version >= PFBV_UFUNC)
	pfb_fread(&l, sizeof(lpstate_t), 1, glb->ifp);
    else if (glb->version >= PFBV_LPSTATE_CALLIG)
	pfb_fread(&l, sizeof(lpstate_1_t), 1, glb->ifp);
    else
	pfb_fread(&l, sizeof(lpstate_0_t), 1, glb->ifp);

    lpstate = pfNewLPState(glb->arena);

    pfLPStateMode(lpstate, PFLPS_SIZE_MODE, lpssm_table[l.size_mode]);
    pfLPStateMode(lpstate, PFLPS_TRANSP_MODE, lpstm_table[l.transp_mode]);
    pfLPStateMode(lpstate, PFLPS_FOG_MODE, lpsfm_table[l.fog_mode]);
    pfLPStateMode(lpstate, PFLPS_DIR_MODE, lpsdm_table[l.dir_mode]);
    pfLPStateMode(lpstate, PFLPS_SHAPE_MODE, lpsshm_table[l.shape_mode]);
    pfLPStateMode(lpstate, PFLPS_RANGE_MODE, lpsrm_table[l.range_mode]);
    pfLPStateVal(lpstate, PFLPS_SIZE_MIN_PIXEL, l.size_min_pixel);
    pfLPStateVal(lpstate, PFLPS_SIZE_MAX_PIXEL, l.size_max_pixel);
    pfLPStateVal(lpstate, PFLPS_SIZE_ACTUAL, l.size_actual);
    pfLPStateVal(lpstate, PFLPS_TRANSP_PIXEL_SIZE, l.transp_pixel_size);
    pfLPStateVal(lpstate, PFLPS_TRANSP_EXPONENT, l.transp_exponent);
    pfLPStateVal(lpstate, PFLPS_TRANSP_SCALE, l.transp_scale);
    pfLPStateVal(lpstate, PFLPS_TRANSP_CLAMP, l.transp_clamp);
    pfLPStateVal(lpstate, PFLPS_FOG_SCALE, l.fog_scale);
    pfLPStateVal(lpstate, PFLPS_INTENSITY, l.intensity);
    pfLPStateVal(lpstate, PFLPS_SIZE_DIFF_THRESH, l.size_diff_thresh);
    pfLPStateShape(lpstate, l.shape[0], l.shape[1], l.shape[2],
		   l.shape[3], l.shape[4]);
    pfLPStateBackColor(lpstate, l.back_color[0], l.back_color[1],
		       l.back_color[2], l.back_color[3]);
    if (l.udata != -1)
	set_udata((pfObject *)lpstate, l.udata, glb);
    if (glb->version >= PFBV_LPSTATE_CALLIG)
    {
	pfLPStateVal(lpstate, PFLPS_SIGNIFICANCE, l.significance);
	pfLPStateVal(lpstate, PFLPS_MIN_DEFOCUS, l.min_defocus);
	pfLPStateVal(lpstate, PFLPS_MAX_DEFOCUS, l.max_defocus);
	pfLPStateMode(lpstate, PFLPS_DRAW_MODE, lpsdrwm_table[l.draw_mode]);
	pfLPStateMode(lpstate, PFLPS_QUALITY_MODE, lpsqm_table[l.quality_mode]);
    }
    if (glb->version >= PFBV_UFUNC)
    {
	void *func;

	if (l.rfunc != -1)
	{
	    func = glb->rl_list[L_UFUNC][l.rfunc];
	    if (l.rfunc_data == -1)
		pfRasterFunc(lpstate, func, NULL);
	}
	else
	    func = NULL;
	if (l.rfunc_data != -1)
	    set_raster_func(lpstate, func, l.rfunc_data, glb);
	if (l.cfunc != -1)
	{
	    func = glb->rl_list[L_UFUNC][l.cfunc];
	    if (l.cfunc_data == -1)
		pfCalligFunc(lpstate, func, NULL);
	}
	else
	    func = NULL;
	if (l.cfunc_data != -1)
	    set_callig_func(lpstate, func, l.cfunc_data, glb);
    }

    return(lpstate);
}


static void *pfa_read_hlight(int hl_num, pfa_global_t *glb)
{
    pfHighlight *hlight;
    hlight_t h;

    READ_LINE();
    h.mode = atoi(glb->line_buf);
    READ_LINE();
    sscanf(glb->line_buf, "%f %f %f %f %f %f %f",
	   &h.fg_color[0], &h.fg_color[1], &h.fg_color[2],
	   &h.bg_color[0], &h.bg_color[1], &h.bg_color[2], &h.alpha);
    READ_LINE();
    sscanf(glb->line_buf, "%f %f", &h.normal_length[0], &h.normal_length[1]);
    READ_LINE();
    scanf(glb->line_buf, "%f %f", &h.line_width, &h.point_size);
    READ_LINE();
    sscanf(glb->line_buf, "%hx %hx", &h.fg_line_pat, &h.bg_line_pat);
    READ_LINE();
    sscanf(glb->line_buf, "%x %x %x %x %x %x %x %x",
	   &h.fg_fill_pat[0], &h.fg_fill_pat[1], &h.fg_fill_pat[2],
	   &h.fg_fill_pat[3], &h.fg_fill_pat[4], &h.fg_fill_pat[5],
	   &h.fg_fill_pat[6], &h.fg_fill_pat[7]);
    READ_LINE();
    sscanf(glb->line_buf, "%x %x %x %x %x %x %x %x",
	   &h.fg_fill_pat[8], &h.fg_fill_pat[9], &h.fg_fill_pat[10],
	   &h.fg_fill_pat[11], &h.fg_fill_pat[12], &h.fg_fill_pat[13],
	   &h.fg_fill_pat[14], &h.fg_fill_pat[15]);
    READ_LINE();
    sscanf(glb->line_buf, "%x %x %x %x %x %x %x %x",
	   &h.fg_fill_pat[16], &h.fg_fill_pat[17], &h.fg_fill_pat[18],
	   &h.fg_fill_pat[19], &h.fg_fill_pat[20], &h.fg_fill_pat[21],
	   &h.fg_fill_pat[22], &h.fg_fill_pat[23]);
    READ_LINE();
    sscanf(glb->line_buf, "%x %x %x %x %x %x %x %x",
	   &h.fg_fill_pat[24], &h.fg_fill_pat[25], &h.fg_fill_pat[26],
	   &h.fg_fill_pat[27], &h.fg_fill_pat[28], &h.fg_fill_pat[29],
	   &h.fg_fill_pat[30], &h.fg_fill_pat[31]);
    READ_LINE();
    sscanf(glb->line_buf, "%x %x %x %x %x %x %x %x",
	   &h.bg_fill_pat[0], &h.bg_fill_pat[1], &h.bg_fill_pat[2],
	   &h.bg_fill_pat[3], &h.bg_fill_pat[4], &h.bg_fill_pat[5],
	   &h.bg_fill_pat[6], &h.bg_fill_pat[7]);
    READ_LINE();
    sscanf(glb->line_buf, "%x %x %x %x %x %x %x %x",
	   &h.bg_fill_pat[8], &h.bg_fill_pat[9], &h.bg_fill_pat[10],
	   &h.bg_fill_pat[11], &h.bg_fill_pat[12], &h.bg_fill_pat[13],
	   &h.bg_fill_pat[14], &h.bg_fill_pat[15]);
    READ_LINE();
    sscanf(glb->line_buf, "%x %x %x %x %x %x %x %x",
	   &h.bg_fill_pat[16], &h.bg_fill_pat[17], &h.bg_fill_pat[18],
	   &h.bg_fill_pat[19], &h.bg_fill_pat[20], &h.bg_fill_pat[21],
	   &h.bg_fill_pat[22], &h.bg_fill_pat[23]);
    READ_LINE();
    sscanf(glb->line_buf, "%x %x %x %x %x %x %x %x",
	   &h.bg_fill_pat[24], &h.bg_fill_pat[25], &h.bg_fill_pat[26],
	   &h.bg_fill_pat[27], &h.bg_fill_pat[28], &h.bg_fill_pat[29],
	   &h.bg_fill_pat[30], &h.bg_fill_pat[31]);
    READ_LINE();
    sscanf(glb->line_buf, "%d %d", &h.gstate[0], &h.gstate[1]);
    READ_LINE();
    sscanf(glb->line_buf, "%d %d %d", &h.tex, &h.tenv, &h.tgen);
    READ_LINE();
    h.udata = atoi(glb->line_buf);

    hlight = pfNewHlight(glb->arena);

    pfHlightMode(hlight, from_table(hlm_table, h.mode));
    pfHlightColor(hlight, PFHL_FGCOLOR,
		  h.fg_color[0], h.fg_color[1], h.fg_color[2]);
    pfHlightColor(hlight, PFHL_BGCOLOR,
		  h.bg_color[0], h.bg_color[1], h.bg_color[2]);
    pfHlightAlpha(hlight, h.alpha);
    pfHlightNormalLength(hlight, h.normal_length[0], h.normal_length[1]);
    pfHlightLineWidth(hlight, h.line_width);
    pfHlightPntSize(hlight, h.point_size);
    pfHlightLinePat(hlight, PFHL_FGPAT, h.fg_line_pat);
    pfHlightLinePat(hlight, PFHL_BGPAT, h.bg_line_pat);
    pfHlightFillPat(hlight, PFHL_FGPAT, h.fg_fill_pat);
    pfHlightFillPat(hlight, PFHL_BGPAT, h.fg_fill_pat);
    glb->hlight_gstates[hl_num] = h.gstate[0];
    if (h.gstate[1] != -1)
	pfHlightGStateIndex(hlight, h.gstate[1]);
    if (h.tex != -1)
	pfHlightTex(hlight, glb->rl_list[L_TEX][h.tex]);
    if (h.tenv != -1)
	pfHlightTEnv(hlight, glb->rl_list[L_TENV][h.tenv]);
    if (h.tgen != -1)
	pfHlightTGen(hlight, glb->rl_list[L_TGEN][h.tgen]);
    if (h.udata != -1)
	set_udata((pfObject *)hlight, h.udata, glb);

    return(share_obj((pfObject *)hlight, glb));
}


static void *pfb_read_hlight(int hl_num, pfa_global_t *glb)
{
    pfHighlight *hlight;
    hlight_t h;

    pfb_fread(&h, sizeof(hlight_t), 1, glb->ifp);
    hlight = pfNewHlight(glb->arena);

    pfHlightMode(hlight, from_table(hlm_table, h.mode));
    pfHlightColor(hlight, PFHL_FGCOLOR,
		  h.fg_color[0], h.fg_color[1], h.fg_color[2]);
    pfHlightColor(hlight, PFHL_BGCOLOR,
		  h.bg_color[0], h.bg_color[1], h.bg_color[2]);
    pfHlightAlpha(hlight, h.alpha);
    pfHlightNormalLength(hlight, h.normal_length[0], h.normal_length[1]);
    pfHlightLineWidth(hlight, h.line_width);
    pfHlightPntSize(hlight, h.point_size);
    pfHlightLinePat(hlight, PFHL_FGPAT, h.fg_line_pat);
    pfHlightLinePat(hlight, PFHL_BGPAT, h.bg_line_pat);
    pfHlightFillPat(hlight, PFHL_FGPAT, h.fg_fill_pat);
    pfHlightFillPat(hlight, PFHL_BGPAT, h.fg_fill_pat);
    glb->hlight_gstates[hl_num] = h.gstate[0];
    if (h.gstate[1] != -1)
	pfHlightGStateIndex(hlight, h.gstate[1]);
    if (h.tex != -1)
	pfHlightTex(hlight, glb->rl_list[L_TEX][h.tex]);
    if (h.tenv != -1)
	pfHlightTEnv(hlight, glb->rl_list[L_TENV][h.tenv]);
    if (h.tgen != -1)
	pfHlightTGen(hlight, glb->rl_list[L_TGEN][h.tgen]);
    if (h.udata != -1)
	set_udata((pfObject *)hlight, h.udata, glb);

    return(share_obj((pfObject *)hlight, glb));
}


static void *pfa_read_gstate(pfa_global_t *glb)
{
    pfGeoState *gstate;
    uint64_t imask;
    int i, j;
    int t1, t2, t3;
    char *s;
    int ids[PF_MAX_TEXTURES_31], whichAttr;

    gstate = pfNewGState(glb->arena);

#ifndef WIN32
    imask = 0xffffffffffffffffLL;
#else
    imask = 0xffffffffffffffffL;
#endif

    READ_LINE();
    while ((i = (int)strtol(glb->line_buf, &s, 10)) != STATE_END)
    {
	imask &= ~gst_table[i];

	switch (i)
	{
	    /*
	     *  Modes
	     */
	    case STATE_TRANSPARENCY:
		pfGStateMode(gstate, gst_table[i], tr_table[atoi(s)]);
		break;
	    case STATE_ANTIALIAS:
		pfGStateMode(gstate, gst_table[i], aa_table[atoi(s)]);
		break;
	    case STATE_DECAL:
		sscanf(s, "%d %d %d", &t1, &t2, &t3);
		pfGStateMode(gstate, gst_table[i],
			     decal_table[t1] |
			     (t2 << PFDECAL_LAYER_SHIFT) |
			     ((t3)? PFDECAL_LAYER_OFFSET : 0));
		break;
	    case STATE_ALPHAFUNC:
		pfGStateMode(gstate, gst_table[i], af_table[atoi(s)]);
		break;
	    case STATE_CULLFACE:
		pfGStateMode(gstate, gst_table[i], cf_table[atoi(s)]);
		break;

	    case STATE_ENTEXGEN:
	    case STATE_ENTEXLOD:
	    case STATE_ENTEXMAT:
	    case STATE_ENTEXTURE:
	      if(glb->version >= PFBV_MAX_TEXTURES_8) {
		sscanf(s,"%d %d %d %d %d %d %d %d",
		       &(ids[0]),&(ids[1]),&(ids[2]),&(ids[3]),
		       &(ids[4]),&(ids[5]),&(ids[6]),&(ids[7]));

		/* XXX Alex -- don't set more than n textures
		 * if h/w is not capable .. */
		for(j=0; j<glb->maxTextures; j++)
		  if(oo_table[ids[j]] == PF_ON)
		    pfGStateMultiMode(gstate, gst_table[i], j, PF_ON);
	      } else 
		pfGStateMode(gstate, gst_table[i], oo_table[atoi(s)]);
	      break;

	    case STATE_ENLIGHTING:
	    case STATE_ENFOG:
	    case STATE_ENWIREFRAME:
	    case STATE_ENCOLORTABLE:
	    case STATE_ENHIGHLIGHTING:
	    case STATE_ENLPOINTSTATE:
	    case STATE_ENVTXPROG:
	    case STATE_ENFRAGPROG:
	    case STATE_ENSHADPROG:
		pfGStateMode(gstate, gst_table[i], oo_table[atoi(s)]);
		break;

	    /*
	     *  Values
	     */
	    case STATE_ALPHAREF:
		pfGStateVal(gstate, gst_table[i], atof(s));
		break;

	    /*
	     *  Attributes
	     */
	    case STATE_FRONTMTL:
	    case STATE_BACKMTL:
		pfGStateAttr(gstate, gst_table[i],
			     glb->rl_list[L_MTL][atoi(s)]);
		break;
	    case STATE_FOG:
		pfGStateAttr(gstate, gst_table[i],
			     glb->rl_list[L_FOG][atoi(s)]);
		break;
	    case STATE_LIGHTMODEL:
		pfGStateAttr(gstate, gst_table[i],
			     glb->rl_list[L_LMODEL][atoi(s)]);
		break;
	    case STATE_LIGHTS:
		{
		    pfLight *lights[PF_MAX_LIGHTS];
		    int lcount, j;

		    lcount = (int)strtol(s, &s, 10);
		    for (j = 0; j < lcount; j++)
		    {
			if ((t1 = (int)strtol(s, &s, 10)) != -1)
			    lights[j] = glb->rl_list[L_LIGHT][t1];
			else
			    lights[j] = NULL;
		    }
		    for (; j < PF_MAX_LIGHTS; j++)
			lights[j] = NULL;

		    pfGStateAttr(gstate, gst_table[i], lights);
		}
		break;
	    case STATE_COLORTABLE:
		pfGStateAttr(gstate, gst_table[i],
			     glb->rl_list[L_CTAB][atoi(s)]);
		break;
	    case STATE_HIGHLIGHT:
		pfGStateAttr(gstate, gst_table[i],
			     glb->rl_list[L_HLIGHT][atoi(s)]);
		break;
	    case STATE_LPOINTSTATE:
		pfGStateAttr(gstate, gst_table[i],
			     glb->rl_list[L_LPSTATE][atoi(s)]);
		break;

	    case STATE_TEXTURE:
	    case STATE_TEXENV:
	    case STATE_TEXGEN:
	    case STATE_TEXLOD:
	      switch(i) {
	      case STATE_TEXTURE: whichAttr = L_TEX; break;
	      case STATE_TEXENV: whichAttr = L_TENV; break;
	      case STATE_TEXGEN: whichAttr = L_TGEN; break;
	      case STATE_TEXLOD: whichAttr = L_TLOD; break;
	      }

	      if(glb->version >= PFBV_MAX_TEXTURES_8) {
		sscanf(s,"%d %d %d %d %d %d %d %d",
		       &(ids[0]),&(ids[1]),&(ids[2]),&(ids[3]),
		       &(ids[4]),&(ids[5]),&(ids[6]),&(ids[7]));
		       
		for(j=0; j<glb->maxTextures; j++) {
		  if(ids[j] != -1)
		    pfGStateMultiAttr(gstate, gst_table[i], j,
				      glb->rl_list[whichAttr][ids[j]]);
		}
	      } else 
		pfGStateAttr(gstate, gst_table[i],
			     glb->rl_list[whichAttr][atoi(s)]);
		break;

	    case STATE_SHADPROG:
	        pfGStateAttr(gstate, gst_table[i],
			     glb->rl_list[L_SHADPROG][atoi(s)]);
		break;
	    case STATE_VERTEX_PROGRAM:
	    case STATE_FRAGMENT_PROGRAM:
		pfGStateAttr(gstate, gst_table[i],
			     glb->rl_list[L_GPROG][atoi(s)]);
		break;
	    case STATE_GPROGPARMS:
	        {
		    int count, index, pindex, j;

		    count = strtol(s, &s, 10);

		    for(j=0; j<count; j++)
		    {
		        index = strtol(s, &s, 10);
			pindex = strtol(s, &s, 10);
		        pfGStateMultiAttr(gstate, gst_table[i], index, 
					  glb->rl_list[L_GPROGPARM][pindex]);
		    }
		    break;
		}
	    case STATE_TEXMAT:
		{
		    pfMatrix *m;
		    int numIters;

		    if(glb->version >= PFBV_MAX_TEXTURES_8)
		      numIters = glb->maxTextures;
		    else
		      numIters = 1;

		    for(j=0; j<numIters; j++) {
		    m = (pfMatrix *)pfMalloc(sizeof(pfMatrix), glb->arena);
		    READ_LINE();
		    sscanf(glb->line_buf, "%g %g %g %g",
			   &(*m)[0][0], &(*m)[0][1], &(*m)[0][2], &(*m)[0][3]);
		    READ_LINE();
		    sscanf(glb->line_buf, "%g %g %g %g",
			   &(*m)[1][0], &(*m)[1][1], &(*m)[1][2], &(*m)[1][3]);
		    READ_LINE();
		    sscanf(glb->line_buf, "%g %g %g %g",
			   &(*m)[2][0], &(*m)[2][1], &(*m)[2][2], &(*m)[2][3]);
		    READ_LINE();
		    sscanf(glb->line_buf, "%g %g %g %g",
			   &(*m)[3][0], &(*m)[3][1], &(*m)[3][2], &(*m)[3][3]);
		    pfGStateMultiAttr(gstate, gst_table[i], j, m);
		    }
		}
		break;
	}

	READ_LINE();
    }

    READ_LINE();
    t1 = atoi(glb->line_buf);
    if (t1 != -1)
	set_udata((pfObject *)gstate, t1, glb);

    pfGStateInherit(gstate, imask);

    return(share_obj((pfObject *)gstate, glb));
}


static void *pfb_read_gstate(pfa_global_t *glb)
{
    pfGeoState *gstate;
    uint64_t imask;
    int i, j;
    int t1, t2, t3;
    int buf_size, buf_pos;
    int end_of_states;
    int *buf;

    pfb_fread(&buf_size, sizeof(int), 1, glb->ifp);
    buf = glb->buf;
	pfb_fread(buf, sizeof(int), buf_size, glb->ifp);

    gstate = pfNewGState(glb->arena);

#ifndef WIN32
    imask = 0xffffffffffffffffLL;
#else
    imask = 0xffffffffffffffffL;
#endif

    buf_pos = 0;
    end_of_states = buf_size - 1;
    while (buf_pos < end_of_states)
    {
	i = buf[buf_pos++];
	imask &= ~gst_table[i];

	switch (i)
	{
	    /*
	     *  Modes
	     */
	    case STATE_TRANSPARENCY:
		pfGStateMode(gstate, gst_table[i], tr_table[buf[buf_pos++]]);
		break;
	    case STATE_ANTIALIAS:
		pfGStateMode(gstate, gst_table[i], aa_table[buf[buf_pos++]]);
		break;
	    case STATE_DECAL:
		t1 = buf[buf_pos++];
		t2 = buf[buf_pos++];
		t3 = buf[buf_pos++];
		pfGStateMode(gstate, gst_table[i],
			     decal_table[t1] |
			     (t2 << PFDECAL_LAYER_SHIFT) |
			     ((t3)? PFDECAL_LAYER_OFFSET : 0));
		break;
	    case STATE_ALPHAFUNC:
		pfGStateMode(gstate, gst_table[i], af_table[buf[buf_pos++]]);
		break;
	    case STATE_CULLFACE:
		pfGStateMode(gstate, gst_table[i], cf_table[buf[buf_pos++]]);
		break;

	    case STATE_ENTEXTURE:
	    case STATE_ENTEXGEN:
	    case STATE_ENTEXLOD:
	    case STATE_ENTEXMAT:
		/*
		//
		// This skips a warning message from pfGStateMultiMode when
		// Hardware has less than PF_MAX_TEXTURES_19 texture units.
		*/
		if (oo_table[buf[buf_pos++]])
		    pfGStateMode(gstate, gst_table[i], 1);

		if (glb->version >= PFBV_MULTITEXTURE)
		{
		  for (j = 1 ; j < glb->maxTextures ; j ++)
			if (oo_table[buf[buf_pos++]] == PF_ON)
			    pfGStateMultiMode(gstate, gst_table[i], j, PF_ON);
		}
		break;

	    case STATE_ENLIGHTING:
	    case STATE_ENFOG:
	    case STATE_ENWIREFRAME:
	    case STATE_ENCOLORTABLE:
	    case STATE_ENHIGHLIGHTING:
	    case STATE_ENLPOINTSTATE:
	    case STATE_ENVTXPROG:
	    case STATE_ENFRAGPROG:
	    case STATE_ENSHADPROG:
		pfGStateMode(gstate, gst_table[i], oo_table[buf[buf_pos++]]);
		break;

	    /*
	     *  Values
	     */
	    case STATE_ALPHAREF:
		pfGStateVal(gstate, gst_table[i], ((float *)buf)[buf_pos++]);
		break;

	    /*
	     *  Attributes
	     */
	    case STATE_FRONTMTL:
	    case STATE_BACKMTL:
		pfGStateAttr(gstate, gst_table[i],
			     glb->rl_list[L_MTL][buf[buf_pos++]]);
		break;
	    case STATE_TEXTURE:
		pfGStateAttr(gstate, gst_table[i],
			     glb->rl_list[L_TEX][buf[buf_pos++]]);
		if (glb->version >= PFBV_MULTITEXTURE)
		{
		    for (j = 1 ; j < glb->maxTextures ; j ++)
			if (buf[buf_pos] >= 0)
			    pfGStateMultiAttr(gstate, gst_table[i], j, 
				 glb->rl_list[L_TEX][buf[buf_pos++]]);
			else 
			    buf_pos++;
		}
		break;
	    case STATE_TEXENV:
		pfGStateAttr(gstate, gst_table[i],
			     glb->rl_list[L_TENV][buf[buf_pos++]]);
		if (glb->version >= PFBV_MULTITEXTURE)
		{
		    for (j = 1 ; j < glb->maxTextures ; j ++)
			if (buf[buf_pos] >= 0)
			    pfGStateMultiAttr(gstate, gst_table[i], j, 
				     glb->rl_list[L_TENV][buf[buf_pos++]]);
			else
			    buf_pos++;
		}
		break;
	    case STATE_FOG:
		pfGStateAttr(gstate, gst_table[i],
			     glb->rl_list[L_FOG][buf[buf_pos++]]);
		break;
	    case STATE_LIGHTMODEL:
		pfGStateAttr(gstate, gst_table[i],
			     glb->rl_list[L_LMODEL][buf[buf_pos++]]);
		break;
	    case STATE_LIGHTS:
		{
		    pfLight *lights[PF_MAX_LIGHTS];
		    int lcount, j;

		    lcount = buf[buf_pos++];
		    for (j = 0; j < lcount; j++)
		    {
			if ((t1 = buf[buf_pos++]) != -1)
			    lights[j] = glb->rl_list[L_LIGHT][t1];
			else
			    lights[j] = NULL;
		    }
		    for (; j < PF_MAX_LIGHTS; j++)
			lights[j] = NULL;

		    pfGStateAttr(gstate, gst_table[i], lights);
		}
		break;
	    case STATE_COLORTABLE:
		pfGStateAttr(gstate, gst_table[i],
			     glb->rl_list[L_CTAB][buf[buf_pos++]]);
		break;
	    case STATE_HIGHLIGHT:
		pfGStateAttr(gstate, gst_table[i],
			     glb->rl_list[L_HLIGHT][buf[buf_pos++]]);
		break;
	    case STATE_LPOINTSTATE:
		pfGStateAttr(gstate, gst_table[i],
			     glb->rl_list[L_LPSTATE][buf[buf_pos++]]);
		break;
	    case STATE_TEXGEN:
		pfGStateAttr(gstate, gst_table[i],
			     glb->rl_list[L_TGEN][buf[buf_pos++]]);
		if (glb->version >= PFBV_MULTITEXTURE)
		{
		    for (j = 1 ; j < glb->maxTextures ; j ++)
			if (buf[buf_pos] >= 0)
			    pfGStateMultiAttr(gstate, gst_table[i], j,
				     glb->rl_list[L_TGEN][buf[buf_pos++]]);
			else
			    buf_pos++;
		}
		break;
	    case STATE_TEXLOD:
		pfGStateAttr(gstate, gst_table[i],
			     glb->rl_list[L_TLOD][buf[buf_pos++]]);
		if (glb->version >= PFBV_MULTITEXTURE)
		{
		    for (j = 1 ; j < glb->maxTextures ; j ++)
			if (buf[buf_pos] >= 0)
			    pfGStateMultiAttr(gstate, gst_table[i], j, 
				 glb->rl_list[L_TLOD][buf[buf_pos++]]);
			else
			    buf_pos++;
		}
		break;
	    case STATE_SHADPROG:
	        pfGStateAttr(gstate, gst_table[i],
			     glb->rl_list[L_SHADPROG][buf[buf_pos++]]);
		break;
	    case STATE_VERTEX_PROGRAM:
	    case STATE_FRAGMENT_PROGRAM:
	        /*pfNotify(PFNFY_ALWAYS, PFNFY_PRINT, "Pointer to prog:   0x%x",  
		  glb->rl_list[L_GPROG][buf[buf_pos]]);*/
		pfGStateAttr(gstate, gst_table[i],
			     glb->rl_list[L_GPROG][buf[buf_pos++]]);
		break;
	    case STATE_GPROGPARMS:
	        {
		    int count, j, ix, pindex;

		    count = buf[buf_pos++];
	      
		    for(j=0; j<count; j++)
		    {
			ix = buf[buf_pos++];
			pindex = buf[buf_pos++];
		      pfGStateMultiAttr(gstate, gst_table[i], ix, 
					glb->rl_list[L_GPROGPARM][pindex]);
		    }
		    break;
		}
	    case STATE_TEXMAT:
		{
		    pfMatrix *m;

		    m = (pfMatrix *)pfMalloc(sizeof(pfMatrix), glb->arena);

		    if (glb->version < PFBV_MULTITEXTURE)
		    {
			(*m)[0][0] = ((float *)buf)[buf_pos++];
			(*m)[0][1] = ((float *)buf)[buf_pos++];
			(*m)[0][2] = ((float *)buf)[buf_pos++];
			(*m)[0][3] = ((float *)buf)[buf_pos++];
			(*m)[1][0] = ((float *)buf)[buf_pos++];
			(*m)[1][1] = ((float *)buf)[buf_pos++];
			(*m)[1][2] = ((float *)buf)[buf_pos++];
			(*m)[1][3] = ((float *)buf)[buf_pos++];
			(*m)[2][0] = ((float *)buf)[buf_pos++];
			(*m)[2][1] = ((float *)buf)[buf_pos++];
			(*m)[2][2] = ((float *)buf)[buf_pos++];
			(*m)[2][3] = ((float *)buf)[buf_pos++];
			(*m)[3][0] = ((float *)buf)[buf_pos++];
			(*m)[3][1] = ((float *)buf)[buf_pos++];
			(*m)[3][2] = ((float *)buf)[buf_pos++];
			(*m)[3][3] = ((float *)buf)[buf_pos++];
			pfGStateAttr(gstate, gst_table[i], m);
		    }
		    else
		    {
			for (j = 0 ; j < glb->maxTextures ; j ++)
			{
			    if (buf[buf_pos++])
			    {
				(*m)[0][0] = ((float *)buf)[buf_pos++];
				(*m)[0][1] = ((float *)buf)[buf_pos++];
				(*m)[0][2] = ((float *)buf)[buf_pos++];
				(*m)[0][3] = ((float *)buf)[buf_pos++];
				(*m)[1][0] = ((float *)buf)[buf_pos++];
				(*m)[1][1] = ((float *)buf)[buf_pos++];
				(*m)[1][2] = ((float *)buf)[buf_pos++];
				(*m)[1][3] = ((float *)buf)[buf_pos++];
				(*m)[2][0] = ((float *)buf)[buf_pos++];
				(*m)[2][1] = ((float *)buf)[buf_pos++];
				(*m)[2][2] = ((float *)buf)[buf_pos++];
				(*m)[2][3] = ((float *)buf)[buf_pos++];
				(*m)[3][0] = ((float *)buf)[buf_pos++];
				(*m)[3][1] = ((float *)buf)[buf_pos++];
				(*m)[3][2] = ((float *)buf)[buf_pos++];
				(*m)[3][3] = ((float *)buf)[buf_pos++];
				pfGStateMultiAttr(gstate, gst_table[i], j, m);
			    }
			}
		    }
		}
		break;
	}
    }

    t1 = buf[buf_pos++];
    if (t1 != -1)
	set_udata((pfObject *)gstate, t1, glb);

    pfGStateInherit(gstate, imask);

    return(share_obj((pfObject *)gstate, glb));
}

static void *pfa_read_shader(pfa_global_t *glb)
{
  /*
    This version of shaders is no longer supported. This function reads and
    discards shader data for backward compatibility */
    
  pfNotify(PFNFY_WARN,PFNFY_WARN, "pfShader is deprecated. Ignoring shader");
  READ_LINE();
  READ_LINE();
  return NULL;
}

static void *pfb_read_shader(pfa_global_t *glb)
{   
  /*
    This version of shaders is no longer supported. This function reads and
    discards shader data for backward compatibility */
    int i, size, inlined;
    char *name = 0;
    
    pfb_fread(&inlined, sizeof(int), 1, glb->ifp);
    pfNotify(PFNFY_WARN,PFNFY_WARN, "pfShader is deprecated. Ignoring shader");
    
    if(inlined) {
    } else {
      pfb_fread(&size, sizeof(int), 1, glb->ifp);
      pfb_fread(name,sizeof(char),size,glb->ifp);
    }

    pfb_fread(&i, sizeof(int), 1, glb->ifp);
    return NULL;
}

static void *pfa_read_slist_header(void **data, int unit_size, int *size,
				   pfa_global_t *glb)
{
    int buf[3];

    READ_LINE();
    sscanf(glb->line_buf, "%d %d %d", &buf[0], &buf[1], &buf[2]);

    *size = buf[0];

    switch (buf[1])	/* Memory type */
    {
	case MEM_ARENA:
	    *data = pfMalloc(buf[0] * unit_size, glb->arena);
	    return(*data);
	case MEM_CBUF:
	    {
		pfCycleBuffer *cbuf;

		cbuf = pfNewCBuffer(buf[0] * unit_size, glb->arena);
		*data = pfGetCurCBufferData(cbuf);
		pfCBufferChanged(cbuf);
		if (buf[2] != -1)
		    set_udata((pfObject *)cbuf, buf[2], glb);
		return(cbuf);
	    }
	case MEM_FLUX:
	    return(glb->rl_list[L_FLUX][buf[2]]);
    }
    return NULL;
}


static void *pfb_read_slist_header(void **data, int unit_size, int *size,
				   pfa_global_t *glb)
{
    int buf[3];

    pfb_fread(&buf, sizeof(int), 3, glb->ifp);

    *size = buf[0];

    switch (buf[1])	/* Memory type */
    {
	case MEM_ARENA:
	    *data = pfMalloc(buf[0] * unit_size, glb->arena);
	    return(*data);
	case MEM_CBUF:
	    {
		pfCycleBuffer *cbuf;

		cbuf = pfNewCBuffer(buf[0] * unit_size, glb->arena);
		*data = pfGetCurCBufferData(cbuf);
		pfCBufferChanged(cbuf);
		if (buf[2] != -1)
		    set_udata((pfObject *)cbuf, buf[2], glb);
		return(cbuf);
	    }
	case MEM_FLUX:
	    return(glb->rl_list[L_FLUX][buf[2]]);
    }
    return NULL;
}


static void *pfa_read_llist(pfa_global_t *glb)
{
    void *rbuf;
    int i, size;
    int *llist;

    rbuf = pfa_read_slist_header((void **)(&llist), sizeof(int), &size, glb);
    for (i = 0; i < size; i++)
    {
	READ_LINE();
	llist[i] = atoi(glb->line_buf);
    }

    return(rbuf);
}


static void *pfb_read_llist(pfa_global_t *glb)
{
    void *rbuf;
    int size;
    int *llist;

    rbuf = pfb_read_slist_header((void **)(&llist), sizeof(int), &size, glb);
    if (size > 0)
	pfb_fread(llist, sizeof(int), size, glb->ifp);
    return(rbuf);
}


static void *pfa_read_vlist(pfa_global_t *glb)
{
    void *rbuf;
    int i, size;
    pfVec3 *vlist;

    rbuf = pfa_read_slist_header((void **)(&vlist), sizeof(pfVec3), &size, glb);
    for (i = 0; i < size; i++)
    {
	READ_LINE();
	sscanf(glb->line_buf, "%g %g %g",
	       &vlist[i][0], &vlist[i][1], &vlist[i][2]);
    }

    return(rbuf);
}


static void *pfb_read_vlist(pfa_global_t *glb)
{
    void *rbuf;
    int size;
    pfVec3 *vlist;

    rbuf = pfb_read_slist_header((void **)(&vlist), sizeof(pfVec3), &size, glb);
    if (size > 0)
    {
	pfb_fread(vlist, sizeof(pfVec3), size, glb->ifp);
	if (glb->crypt_key)
	    glb->decrypt_func((int)(size * sizeof(pfVec3)), glb->crypt_key,
			      vlist);
    }

    return(rbuf);
}


static void *pfa_read_clist(pfa_global_t *glb)
{
    void *rbuf;
    int i, size;
    pfVec4 *clist;

    rbuf = pfa_read_slist_header((void **)(&clist), sizeof(pfVec4), &size, glb);
    for (i = 0; i < size; i++)
    {
	READ_LINE();
	sscanf(glb->line_buf, "%g %g %g %g",
	       &clist[i][0], &clist[i][1], &clist[i][2], &clist[i][3]);
    }

    return(rbuf);
}


static void *pfb_read_clist(pfa_global_t *glb)
{
    void *rbuf;
    int size;
    pfVec4 *clist;

    rbuf = pfb_read_slist_header((void **)(&clist), sizeof(pfVec4), &size, glb);
    if (size > 0)
	pfb_fread(clist, sizeof(pfVec4), size, glb->ifp);

    return(rbuf);
}


static void *pfa_read_nlist(pfa_global_t *glb)
{
    void *rbuf;
    int i, size;
    pfVec3 *nlist;

    rbuf = pfa_read_slist_header((void **)(&nlist), sizeof(pfVec3), &size, glb);
    for (i = 0; i < size; i++)
    {
	READ_LINE();
	sscanf(glb->line_buf, "%g %g %g",
	       &nlist[i][0], &nlist[i][1], &nlist[i][2]);
    }

    return(rbuf);
}


static void *pfb_read_nlist(pfa_global_t *glb)
{
    void *rbuf;
    int size;
    pfVec3 *nlist;

    rbuf = pfb_read_slist_header((void **)(&nlist), sizeof(pfVec3), &size, glb);
    if (size > 0)
	pfb_fread(nlist, sizeof(pfVec3), size, glb->ifp);
    return(rbuf);
}


static void *pfa_read_tlist(pfa_global_t *glb)
{
    void *rbuf;
    int i, size;
    pfVec2 *tlist;

    rbuf = pfa_read_slist_header((void **)(&tlist), sizeof(pfVec2), &size, glb);
    for (i = 0; i < size; i++)
    {
	READ_LINE();
	sscanf(glb->line_buf, "%g %g", &tlist[i][0], &tlist[i][1]);
    }

    return(rbuf);
}


static void *pfb_read_tlist(pfa_global_t *glb)
{
    void *rbuf;
    int size;
    pfVec2 *tlist;

    rbuf = pfb_read_slist_header((void **)(&tlist), sizeof(pfVec2), &size, glb);
    if (size > 0)
	pfb_fread(tlist, sizeof(pfVec2), size, glb->ifp);
    return(rbuf);
}


static void *pfa_read_ilist(pfa_global_t *glb)
{
    void *rbuf;
    int i, size;
    ushort *ilist;

    rbuf = pfa_read_slist_header((void **)(&ilist), sizeof(ushort), &size, glb);
    for (i = 0; i < size; i++)
    {
	READ_LINE();
	ilist[i] = atoi(glb->line_buf);
    }

    return(rbuf);
}


static void *pfb_read_ilist(pfa_global_t *glb)
{
    void *rbuf;
    int size;
    ushort *ilist;

    rbuf = pfb_read_slist_header((void **)(&ilist), sizeof(ushort), &size, glb);
    if (size > 0)
	pfb_fread(ilist, sizeof(ushort), size, glb->ifp);
    return(rbuf);
}

static void *pfa_read_flist(pfa_global_t *glb)
{
    void *rbuf;
    int i, size;
    float *flist;

    rbuf = pfa_read_slist_header((void **)(&flist), sizeof(float), &size, glb);
    for (i = 0; i < size; i++)
    {
	READ_LINE();
	flist[i] = atof(glb->line_buf);
    }

    return(rbuf);
}


static void *pfb_read_flist(pfa_global_t *glb)
{
    void *rbuf;
    int size;
    float *flist;

    rbuf = pfb_read_slist_header((void **)(&flist), sizeof(float), &size, glb);
    if (size > 0)
	pfb_fread(flist, sizeof(float), size, glb->ifp);
    return(rbuf);
}

static void *pfa_read_varray_1(pfa_global_t *glb)
{
    void *rbuf;
    int i, size;
    char *list;

    rbuf = pfa_read_slist_header((void **)(&list), sizeof(char), &size, glb);
    for (i = 0; i < size; i++) {
	READ_LINE();
	list[i] = atoi(glb->line_buf);	/* one byte per line */
    }
    return(rbuf);
}


static void *pfb_read_varray_1(pfa_global_t *glb)
{
    void *rbuf;
    int size;
    char *list;

    rbuf = pfb_read_slist_header((void **)(&list), sizeof(char), &size, glb);
    if (size > 0) 
    {
	pfb_fread(list, sizeof(char), size, glb->ifp);
	if (glb->crypt_key && glb->version >= PFBV_GEOARRAY_STRIDE_SAVE)
	    glb->decrypt_func((int)(size * sizeof(char)), glb->crypt_key,
			      list);
    }
    return(rbuf);
}

static void *pfa_read_varray_2(pfa_global_t *glb)
{
    void *rbuf;
    int i, size;
    short *list;

    rbuf = pfa_read_slist_header((void **)(&list), sizeof(short), &size, glb);
    for (i = 0; i < size; i++) {
	READ_LINE();
	list[i] = atoi(glb->line_buf);	/* one short per line */
    }
    return(rbuf);
}

static void *pfb_read_varray_2(pfa_global_t *glb)
{
    void *rbuf;
    int size;
    short *list;

    rbuf = pfb_read_slist_header((void **)(&list), sizeof(short), &size, glb);
    if (size > 0)
    {
	pfb_fread(list, sizeof(short), size, glb->ifp);
	if (glb->crypt_key && glb->version >= PFBV_GEOARRAY_STRIDE_SAVE)
	    glb->decrypt_func((int)(size * sizeof(short)), glb->crypt_key,
			      list);
    }
    return(rbuf);
}

static void *pfa_read_varray_4(pfa_global_t *glb)
{
    void *rbuf;
    int i, size;
    int *list;

    rbuf = pfa_read_slist_header((void **)(&list), sizeof(int), &size, glb);
    for (i = 0; i < size; i++) {
	READ_LINE();
	sscanf(glb->line_buf, "%x", &list[i]);	/* one integer per line (cast to whatever) */
    }
    return(rbuf);
}


static void *pfb_read_varray_4(pfa_global_t *glb)
{
    void *rbuf;
    int size;
    int *list;

    rbuf = pfb_read_slist_header((void **)(&list), sizeof(int), &size, glb);
    if (size > 0)
    {
	pfb_fread(list, sizeof(int), size, glb->ifp);
	if (glb->crypt_key && glb->version >= PFBV_GEOARRAY_STRIDE_SAVE)
	    glb->decrypt_func((int)(size * sizeof(int)), glb->crypt_key,
			      list);
    }
    return(rbuf);
}

static void *pfa_read_asddata(pfa_global_t *glb)
{
    void *rbuf;
    int i, j, size;
    void *asddata;
    unsigned char *uc;

    rbuf = pfa_read_slist_header(&asddata, 1, &size, glb);
    uc = (unsigned char *)asddata;
    READ_LINE();
    for (i = 0; i < size; i++)
    {
	j = (i % 39) * 2;

	if (glb->line_buf[j] >= '0' && glb->line_buf[j] <= '9')
	    uc[i] = (glb->line_buf[j] - '0') << 4;
	else
	    uc[i] = (glb->line_buf[j] - 'a' + 10) << 4;

	if (glb->line_buf[j+1] >= '0' && glb->line_buf[j+1] <= '9')
	    uc[i] |= glb->line_buf[j+1] - '0';
	else
	    uc[i] |= glb->line_buf[j+1] - 'a' + 10;

	if (j == 76 && i < size - 1)
	    READ_LINE();
    }

    return(rbuf);
}


static void *pfb_read_asddata(pfa_global_t *glb)
{
    void *rbuf;
    int size;
    void *asddata;

    rbuf = pfb_read_slist_header(&asddata, 1, &size, glb);
    pfb_fread(asddata, 1, size, glb->ifp);

    return(rbuf);
}


static void *pfa_read_flux(pfa_global_t *glb)
{
    pfFlux *flux;
    flux_t f;
    unsigned char *uc;
    int i, j;

    READ_LINE();
    sscanf(glb->line_buf, "%d", &f.num_buffers);
    READ_LINE();
    sscanf(glb->line_buf, "%d", &f.data_size);
    READ_LINE();
    sscanf(glb->line_buf, "%x", &f.flags);
    READ_LINE();
    sscanf(glb->line_buf, "%x", &f.mask);
    READ_LINE();
    sscanf(glb->line_buf, "%d", &f.udata);
    if (glb->version >= PFBV_FLUX_SYNC_GROUP)
    {
	READ_LINE();
	sscanf(glb->line_buf, "%x", &f.sync_group);
    }
    else
	f.sync_group = 0;

    flux = pfNewFlux(f.data_size, f.num_buffers, glb->arena);
    if (f.flags & FF_PUSH)
	pfFluxMode(flux, PFFLUX_PUSH, PF_ON);
    if (f.flags & FF_ON_DEMAND)
	pfFluxMode(flux, PFFLUX_ON_DEMAND, PF_ON);
    if (f.flags & FF_COPY_LAST_DATA)
	pfFluxMode(flux, PFFLUX_COPY_LAST_DATA, PF_ON);
    pfFluxMask(flux, f.mask);
    if (glb->version >= PFBV_FLUX_SYNC_GROUP_NAMES)
	pfFluxSyncGroup(flux,
			*(uint*)&glb->rl_list[L_SG_NAME][f.sync_group]);
    else
	pfFluxSyncGroup(flux, f.sync_group);
    if (f.udata != -1)
	set_udata((pfObject *)flux, f.udata, glb);

    uc = (unsigned char *)pfGetFluxCurData(flux);
    READ_LINE();
    for (i = 0; i < f.data_size; i++)
    {
	j = (i % 39) * 2;

	if (glb->line_buf[j] >= '0' && glb->line_buf[j] <= '9')
	    uc[i] = (glb->line_buf[j] - '0') << 4;
	else
	    uc[i] = (glb->line_buf[j] - 'a' + 10) << 4;

	if (glb->line_buf[j+1] >= '0' && glb->line_buf[j+1] <= '9')
	    uc[i] |= glb->line_buf[j+1] - '0';
	else
	    uc[i] |= glb->line_buf[j+1] - 'a' + 10;

	if (j == 76 && i < f.data_size - 1)
	    READ_LINE();
    }

    pfFluxInitData(flux, uc);

    return(flux);
}


static void *pfb_read_flux(pfa_global_t *glb)
{
    pfFlux *flux;
    flux_t f;

    unsigned char *uc;
    if (glb->version >= PFBV_FLUX_SYNC_GROUP)
	pfb_fread(&f, sizeof(flux_t), 1, glb->ifp);
    else
    {
	pfb_fread(&f, sizeof(flux_0_t), 1, glb->ifp);
	f.sync_group = 0;
    }

    flux = pfNewFlux(f.data_size, f.num_buffers, glb->arena);
    if (f.flags & FF_PUSH)
	pfFluxMode(flux, PFFLUX_PUSH, PF_ON);
    if (f.flags & FF_ON_DEMAND)
	pfFluxMode(flux, PFFLUX_ON_DEMAND, PF_ON);
    if (f.flags & FF_COPY_LAST_DATA)
	pfFluxMode(flux, PFFLUX_COPY_LAST_DATA, PF_ON);
    pfFluxMask(flux, f.mask);
    if (glb->version >= PFBV_FLUX_SYNC_GROUP_NAMES)
	pfFluxSyncGroup(flux,
			*(uint*)&glb->rl_list[L_SG_NAME][f.sync_group]);
    else
	pfFluxSyncGroup(flux, f.sync_group);
    if (f.udata != -1)
	set_udata((pfObject *)flux, f.udata, glb);

    uc = (unsigned char *)pfGetFluxCurData(flux);
    pfb_fread(uc, f.data_size, 1, glb->ifp);
    pfFluxInitData(flux, uc);

    return(flux);
}


static void *pfa_read_sg_name(pfa_global_t *glb)
{
    READ_LINE();
    glb->line_buf[strlen(glb->line_buf)-1] = '\0';
    return (void*)pfGetFluxNamedSyncGroup(glb->line_buf);
}


static void *pfb_read_sg_name(pfa_global_t *glb)
{
    int length;
    char name[PF_MAXSTRING];

    pfb_fread(&length, sizeof(int), 1, glb->ifp);
    pfb_fread(name, 1, length, glb->ifp);
    name[length] = '\0';

    return (void*)pfGetFluxNamedSyncGroup(name);
}


static void *pfa_read_engine(pfa_global_t *glb)
{
    pfEngine *engine;
    engine_t e;
    engine_src_t *es;
    int i, es_size;
    ushort *ilist;

    READ_LINE();
    sscanf(glb->line_buf, "%d %d", &e.function, &e.user_function);
    READ_LINE();
    sscanf(glb->line_buf, "%d %d %d %d %d", &e.dst_data, &e.dst_data_list,
	    &e.dst_ilist, &e.dst_offset, &e.dst_stride);
    READ_LINE();
    sscanf(glb->line_buf, "%d", &e.num_srcs);
    READ_LINE();
    sscanf(glb->line_buf, "%d %d", &e.interations, &e.items);
    READ_LINE();
    sscanf(glb->line_buf, "%x", &e.mask);
    READ_LINE();
    sscanf(glb->line_buf, "%g %g %g %g %g",
	    &e.range[0], &e.range[1], &e.range[2], &e.range[3], &e.range[4]);
    READ_LINE();
    sscanf(glb->line_buf, "%x", &e.flags);
    READ_LINE();
    sscanf(glb->line_buf, "%d", &e.udata);
    es_size = e.num_srcs * (int)sizeof(engine_src_t);
    es = (engine_src_t *)GET_BUF(es_size);
    for (i = 0; i < e.num_srcs; i++)
    {
	READ_LINE();
	sscanf(glb->line_buf, "%d %d %d %d %d %d",
	       &es[i].data, &es[i].data_list, &es[i].ilist,
	       &es[i].icount, &es[i].offset, &es[i].stride);
    }

    engine = pfNewEngine(ef_table[e.function], glb->arena);
    if (e.user_function != -1)
	pfEngineUserFunction(engine, glb->rl_list[L_UFUNC][e.user_function]);
    if (e.dst_data != -1)
    {
	ilist = (e.dst_ilist != -1)? glb->rl_list[L_ILIST][e.dst_ilist] : NULL;
	pfEngineDst(engine, glb->rl_list[e.dst_data_list][e.dst_data],
		    ilist, e.dst_offset, e.dst_stride);
    }
    pfEngineIterations(engine, e.interations, e.items);
    pfEngineMask(engine, e.mask);
    pfEngineEvaluationRange(engine, e.range, e.range[3], e.range[4]);
    if (e.flags & EF_RANGE_CHECK)
	pfEngineMode(engine, PFENG_RANGE_CHECK, PF_ON);
    if (e.flags & EF_MATRIX_POST_MULT)
	pfEngineMode(engine, PFENG_MATRIX_MODE, PFENG_MATRIX_POST_MULT);
    if (e.flags & EF_TIME_SWING)
	pfEngineMode(engine, PFENG_TIME_MODE, PFENG_TIME_SWING);
    if (e.udata != -1)
	set_udata((pfObject *)engine, e.udata, glb);

    for (i = 0; i < e.num_srcs; i++)
    {
	ilist = (es[i].ilist != -1)? glb->rl_list[L_ILIST][es[i].ilist] : NULL;
	if (es[i].data >= 0)
	    pfEngineSrc(engine, i, glb->rl_list[es[i].data_list][es[i].data],
			ilist, es[i].icount, es[i].offset, es[i].stride);
	else if (es[i].data == FRAME_TIME_FLUX && es[i].data_list == L_FLUX)
	    pfEngineSrc(engine, i, pfGetFrameTimeFlux(),
			ilist, es[i].icount, es[i].offset, es[i].stride);
    }
    return engine;
}


static void *pfb_read_engine(pfa_global_t *glb)
{
    pfEngine *engine;
    engine_t e;
    engine_src_t *es;
    int i, es_size;
    ushort *ilist;

    pfb_fread(&e, sizeof(engine_t), 1, glb->ifp);
    es_size = e.num_srcs * (int)sizeof(engine_src_t);
    es = (engine_src_t *)GET_BUF(es_size);
    pfb_fread(es, es_size, 1, glb->ifp);

    engine = pfNewEngine(e.function, glb->arena);
    if (e.user_function != -1)
	pfEngineUserFunction(engine, glb->rl_list[L_UFUNC][e.user_function]);
    if (e.dst_data != -1)
    {
	ilist = (e.dst_ilist != -1)? glb->rl_list[L_ILIST][e.dst_ilist] : NULL;
	pfEngineDst(engine, glb->rl_list[e.dst_data_list][e.dst_data],
		    ilist, e.dst_offset, e.dst_stride);
    }
    pfEngineIterations(engine, e.interations, e.items);
    pfEngineMask(engine, e.mask);
    pfEngineEvaluationRange(engine, e.range, e.range[3], e.range[4]);
    if (e.flags & EF_RANGE_CHECK)
	pfEngineMode(engine, PFENG_RANGE_CHECK, PF_ON);
    if (e.flags & EF_MATRIX_POST_MULT)
	pfEngineMode(engine, PFENG_MATRIX_MODE, PFENG_MATRIX_POST_MULT);
    if (e.flags & EF_TIME_SWING)
	pfEngineMode(engine, PFENG_TIME_MODE, PFENG_TIME_SWING);
    if (e.udata != -1)
	set_udata((pfObject *)engine, e.udata, glb);

    for (i = 0; i < e.num_srcs; i++)
    {
	ilist = (es[i].ilist != -1)? glb->rl_list[L_ILIST][es[i].ilist] : NULL;
	if (es[i].data >= 0)
	    pfEngineSrc(engine, i, glb->rl_list[es[i].data_list][es[i].data],
			ilist, es[i].icount, es[i].offset, es[i].stride);
	else if (es[i].data == FRAME_TIME_FLUX && es[i].data_list == L_FLUX)
	    pfEngineSrc(engine, i, pfGetFrameTimeFlux(),
			ilist, es[i].icount, es[i].offset, es[i].stride);
    }
    return engine;
}


static void *pfa_read_gset(pfa_global_t *glb)
{
    pfGeoSet *gset;
    gset_t g;

    READ_LINE();
    sscanf(glb->line_buf, "%d %d", &g.ptype, &g.pcount);
    READ_LINE();
    g.llist = atoi(glb->line_buf);
    READ_LINE();
    sscanf(glb->line_buf, "%d %d %d", &g.vlist[0], &g.vlist[1], &g.vlist[2]);
    READ_LINE();
    sscanf(glb->line_buf, "%d %d %d", &g.clist[0], &g.clist[1], &g.clist[2]);
    READ_LINE();
    sscanf(glb->line_buf, "%d %d %d", &g.nlist[0], &g.nlist[1], &g.nlist[2]);
    READ_LINE();
    sscanf(glb->line_buf, "%d %d %d", &g.tlist[0], &g.tlist[1], &g.tlist[2]);
    READ_LINE();
    sscanf(glb->line_buf, "%d %d %d",
	   &g.draw_mode[0], &g.draw_mode[1], &g.draw_mode[2]);
    READ_LINE();
    sscanf(glb->line_buf, "%d %d", &g.gstate[0], &g.gstate[1]);
    READ_LINE();
    sscanf(glb->line_buf, "%f %f", &g.line_width, &g.point_size);
    READ_LINE();
    g.draw_bin = atoi(glb->line_buf);
    READ_LINE();
    g.isect_mask = (uint)strtoul(glb->line_buf, NULL, 16);
    READ_LINE();
    g.hlight = atoi(glb->line_buf);
    READ_LINE();
    sscanf(glb->line_buf, "%d %f %f %f %f %f %f", &g.bbox_mode,
	   &g.bbox.min[0], &g.bbox.min[1], &g.bbox.min[2],
	   &g.bbox.max[0], &g.bbox.max[1], &g.bbox.max[2]);
    READ_LINE();
    g.udata = atoi(glb->line_buf);

    if (glb->version >= PFBV_GSET_DO_DP)
    {
	READ_LINE();
	g.draw_order = atoi(glb->line_buf);
	READ_LINE();
	sscanf(glb->line_buf, "%d %g %g %g %g", &g.decal_plane,
	       &g.dplane_normal[0], &g.dplane_normal[1], &g.dplane_normal[2],
	       &g.dplane_offset);
    }
    if (glb->version >= PFBV_GSET_BBOX_FLUX)
    {
	READ_LINE();
	g.bbox_flux = atoi(glb->line_buf);
    }

    gset = pfNewGSet(glb->arena);

    pfGSetPrimType(gset, gspt_table[g.ptype]);
    pfGSetNumPrims(gset, g.pcount);
    if (g.llist != -1)
	pfGSetPrimLengths(gset, glb->rl_list[L_LLIST][g.llist]);
    pfGSetAttr(gset, PFGS_COORD3, gsb_table[g.vlist[0]],
	       (g.vlist[1] == -1)? NULL : glb->rl_list[L_VLIST][g.vlist[1]],
	       (g.vlist[2] == -1)? NULL : glb->rl_list[L_ILIST][g.vlist[2]]);
    pfGSetAttr(gset, PFGS_COLOR4, gsb_table[g.clist[0]],
	       (g.clist[1] == -1)? NULL : glb->rl_list[L_CLIST][g.clist[1]],
	       (g.clist[2] == -1)? NULL : glb->rl_list[L_ILIST][g.clist[2]]);
    pfGSetAttr(gset, PFGS_NORMAL3, gsb_table[g.nlist[0]],
	       (g.nlist[1] == -1)? NULL : glb->rl_list[L_NLIST][g.nlist[1]],
	       (g.nlist[2] == -1)? NULL : glb->rl_list[L_ILIST][g.nlist[2]]);
    pfGSetAttr(gset, PFGS_TEXCOORD2, gsb_table[g.tlist[0]],
	       (g.tlist[1] == -1)? NULL : glb->rl_list[L_TLIST][g.tlist[1]],
	       (g.tlist[2] == -1)? NULL : glb->rl_list[L_ILIST][g.tlist[2]]);
    pfGSetDrawMode(gset, PFGS_FLATSHADE, oo_table[g.draw_mode[0]]);
    pfGSetDrawMode(gset, PFGS_WIREFRAME, oo_table[g.draw_mode[1]]);
    if (glb->compile_gl == PFPFB_COMPILE_GL_ON ||
	(glb->compile_gl == PFPFB_COMPILE_GL_AS_SAVED &&
	 oo_table[g.draw_mode[2]] == PF_ON))
	pfGSetDrawMode(gset, PFGS_COMPILE_GL, PF_ON);
    if (g.gstate[0] != -1)
	pfGSetGState(gset, glb->rl_list[L_GSTATE][g.gstate[0]]);
    if (g.gstate[1] != -1)
	pfGSetGStateIndex(gset, g.gstate[1]);
    pfGSetLineWidth(gset, g.line_width);
    pfGSetPntSize(gset, g.point_size);
    pfGSetDrawBin(gset, g.draw_bin);
    pfGSetDrawOrder(gset, g.draw_order);
    pfGSetIsectMask(gset, g.isect_mask,
		    (PFTRAV_SELF | PFTRAV_IS_CACHE), PF_SET);
    if (g.hlight != -1)
	pfGSetHlight(gset, glb->rl_list[L_HLIGHT][g.hlight]);
    pfGSetBBox(gset, &g.bbox, bound_table[g.bbox_mode]);
    if (g.udata != -1)
	set_udata((pfObject *)gset, g.udata, glb);

    if (glb->version >= PFBV_GSET_DO_DP)
    {
	pfGSetDrawOrder(gset, g.draw_order);
	if (g.decal_plane != -1)
	{
	    pfPlane *plane;

	    plane = (pfPlane *)pfMalloc(sizeof(pfPlane), glb->arena);
	    PFCOPY_VEC3(plane->normal, g.dplane_normal);
	    plane->offset = g.dplane_offset;
	    pfGSetDecalPlane(gset, plane);
	}
    }
    if (glb->version >= PFBV_GSET_BBOX_FLUX)
    {
	if (g.bbox_flux != -1)
	    pfGSetBBoxFlux(gset, glb->rl_list[L_FLUX][g.bbox_flux]);
    }
    /* Read appearance */
    if(glb->version >= PFBV_SHADER_V2) {
      READ_LINE();
      sscanf(glb->line_buf, "%d", &g.appearance);
      if(g.appearance >= 0) {
	pfGSetAppearance(gset, glb->rl_list[L_APPEARANCE][g.appearance]);
      }
    }
    
    return gset;
}


static void *pfb_read_gset(pfa_global_t *glb)
{
    pfGeoSet 	*gset;
    gset_t 	g;
    int		i, j;
    
    if (glb->version >= PFBV_MAX_TEXTURES_8)
        pfb_fread(&g, sizeof(gset_t), 1, glb->ifp);
    else if (glb->version >= PFBV_SHADER_V2)
        pfb_fread(&g, sizeof(gset_4_t), 1, glb->ifp);
    else if(glb->version >= PFBV_MULTITEXTURE)
	pfb_fread(&g, sizeof(gset_3_t), 1, glb->ifp);
    else if (glb->version >= PFBV_GSET_BBOX_FLUX)
	pfb_fread(&g, sizeof(gset_2_t), 1, glb->ifp);
    else if (glb->version >= PFBV_GSET_DO_DP)
	pfb_fread(&g, sizeof(gset_1_t), 1, glb->ifp);
    else
	pfb_fread(&g, sizeof(gset_0_t), 1, glb->ifp);

    gset = pfNewGSet(glb->arena);

    pfGSetPrimType(gset, gspt_table[g.ptype]);
    pfGSetNumPrims(gset, g.pcount);
    if (g.llist != -1)
	pfGSetPrimLengths(gset, glb->rl_list[L_LLIST][g.llist]);
    pfGSetAttr(gset, PFGS_COORD3, gsb_table[g.vlist[0]],
	       (g.vlist[1] == -1)? NULL : glb->rl_list[L_VLIST][g.vlist[1]],
	       (g.vlist[2] == -1)? NULL : glb->rl_list[L_ILIST][g.vlist[2]]);
    pfGSetAttr(gset, PFGS_COLOR4, gsb_table[g.clist[0]],
	       (g.clist[1] == -1)? NULL : glb->rl_list[L_CLIST][g.clist[1]],
	       (g.clist[2] == -1)? NULL : glb->rl_list[L_ILIST][g.clist[2]]);
    pfGSetAttr(gset, PFGS_NORMAL3, gsb_table[g.nlist[0]],
	       (g.nlist[1] == -1)? NULL : glb->rl_list[L_NLIST][g.nlist[1]],
	       (g.nlist[2] == -1)? NULL : glb->rl_list[L_ILIST][g.nlist[2]]);
    pfGSetAttr(gset, PFGS_TEXCOORD2, gsb_table[g.tlist[0]],
	       (g.tlist[1] == -1)? NULL : glb->rl_list[L_TLIST][g.tlist[1]],
	       (g.tlist[2] == -1)? NULL : glb->rl_list[L_ILIST][g.tlist[2]]);
    pfGSetDrawMode(gset, PFGS_FLATSHADE, oo_table[g.draw_mode[0]]);
    pfGSetDrawMode(gset, PFGS_WIREFRAME, oo_table[g.draw_mode[1]]);
    if (glb->compile_gl == PFPFB_COMPILE_GL_ON ||
	(glb->compile_gl == PFPFB_COMPILE_GL_AS_SAVED &&
	 oo_table[g.draw_mode[2]] == PF_ON))
	pfGSetDrawMode(gset, PFGS_COMPILE_GL, PF_ON);
    if (g.gstate[0] != -1)
	pfGSetGState(gset, glb->rl_list[L_GSTATE][g.gstate[0]]);
    if (g.gstate[1] != -1)
	pfGSetGStateIndex(gset, g.gstate[1]);
    pfGSetLineWidth(gset, g.line_width);
    pfGSetPntSize(gset, g.point_size);
    pfGSetDrawBin(gset, g.draw_bin);
    pfGSetIsectMask(gset, g.isect_mask,
		    (PFTRAV_SELF | PFTRAV_IS_CACHE), PF_SET);
    if (g.hlight != -1)
	pfGSetHlight(gset, glb->rl_list[L_HLIGHT][g.hlight]);
    pfGSetBBox(gset, &g.bbox, bound_table[g.bbox_mode]);
    if (g.udata != -1)
	set_udata((pfObject *)gset, g.udata, glb);

    if (glb->version >= PFBV_GSET_DO_DP)
    {
	pfGSetDrawOrder(gset, g.draw_order);
	if (g.decal_plane != -1)
	{
	    pfPlane *plane;

	    plane = (pfPlane *)pfMalloc(sizeof(pfPlane), glb->arena);
	    PFCOPY_VEC3(plane->normal, g.dplane_normal);
	    plane->offset = g.dplane_offset;
	    pfGSetDecalPlane(gset, plane);
	}
    }
    if (glb->version >= PFBV_GSET_BBOX_FLUX)
    {
	if (g.bbox_flux != -1)
	    pfGSetBBoxFlux(gset, glb->rl_list[L_FLUX][g.bbox_flux]);
    }

    if (glb->version >= PFBV_MULTITEXTURE)
    {
	/*
	 *	To retain backwards compatibility, we store texture attr's
	 *	for texture unit # 0 in tlist, and texture attributes for 
	 *	texture units #1, #2, ... in multi_tlist.
	 *	
	 *	we process multi_tlist only if the pfb format version is 
	 *	late enough.
	 */

        int numIters = 
	  (glb->version >= PFBV_MAX_TEXTURES_8) ? glb->maxTextures : 4;
        int multi_tlist[(PF_MAX_TEXTURES_31-1) * 3];

	memcpy(multi_tlist,g.multi_tlist,sizeof(int)*3*3);
	memcpy(multi_tlist + 3*3, g.multi_tlist2, sizeof(int)*4*3);

	for (i = 1 ; i < numIters ; i ++)
	{
	    j = (i - 1) * 3;
	    if (gsb_table[multi_tlist[j]] != PFGS_OFF)
		pfGSetMultiAttr(gset, PFGS_TEXCOORD2, i, 
		   gsb_table[multi_tlist[j]],
		   (multi_tlist[j + 1] == -1) ? 
			NULL : glb->rl_list[L_TLIST][multi_tlist[j + 1]],
		   (multi_tlist[j + 2] == -1) ? 
			NULL : glb->rl_list[L_ILIST][multi_tlist[j + 2]]);
	}
    }
    
    if(glb->version >= PFBV_SHADER_V2) {
      if(g.appearance >= 0) {
	islAppearance *a = glb->rl_list[L_APPEARANCE][g.appearance];
	pfGSetAppearance(gset, a);
      }
    }

    return(gset);
}

static void *pfa_read_geoArray(pfa_global_t *glb)
{
    pfGeoSet *gset;
    pfGeoArray *ga;
    pfVertexAttr *va;
    int num_attrs,i,j,standard, gindex, glsize, gltype, elemSize, listIndex, stride;
    void *ptr;
    char strname[256];
    gset_t g;

    /* copy of geoset stuff begins here */
    READ_LINE();
    sscanf(glb->line_buf, "%d %d", &g.ptype, &g.pcount);
    READ_LINE();
    g.llist = atoi(glb->line_buf);
    READ_LINE();
    sscanf(glb->line_buf, "%d %d %d", &g.vlist[0], &g.vlist[1], &g.vlist[2]);
    READ_LINE();
    sscanf(glb->line_buf, "%d %d %d", &g.clist[0], &g.clist[1], &g.clist[2]);
    READ_LINE();
    sscanf(glb->line_buf, "%d %d %d", &g.nlist[0], &g.nlist[1], &g.nlist[2]);
    READ_LINE();
    sscanf(glb->line_buf, "%d %d %d", &g.tlist[0], &g.tlist[1], &g.tlist[2]);
    READ_LINE();
    sscanf(glb->line_buf, "%d %d %d",
	   &g.draw_mode[0], &g.draw_mode[1], &g.draw_mode[2]);
    READ_LINE();
    sscanf(glb->line_buf, "%d %d", &g.gstate[0], &g.gstate[1]);
    READ_LINE();
    sscanf(glb->line_buf, "%f %f", &g.line_width, &g.point_size);
    READ_LINE();
    g.draw_bin = atoi(glb->line_buf);
    READ_LINE();
    g.isect_mask = (uint)strtoul(glb->line_buf, NULL, 16);
    READ_LINE();
    g.hlight = atoi(glb->line_buf);
    READ_LINE();
    sscanf(glb->line_buf, "%d %f %f %f %f %f %f", &g.bbox_mode,
	   &g.bbox.min[0], &g.bbox.min[1], &g.bbox.min[2],
	   &g.bbox.max[0], &g.bbox.max[1], &g.bbox.max[2]);
    READ_LINE();
    g.udata = atoi(glb->line_buf);

    if (glb->version >= PFBV_GSET_DO_DP)
    {
	READ_LINE();
	g.draw_order = atoi(glb->line_buf);
	READ_LINE();
	sscanf(glb->line_buf, "%d %g %g %g %g", &g.decal_plane,
	       &g.dplane_normal[0], &g.dplane_normal[1], &g.dplane_normal[2],
	       &g.dplane_offset);
    }
    if (glb->version >= PFBV_GSET_BBOX_FLUX)
    {
	READ_LINE();
	g.bbox_flux = atoi(glb->line_buf);
    }

    ga = pfNewGArray(glb->arena);
    gset = (pfGeoSet*)ga;		/* casting for code below */

    pfGSetPrimType(gset, gspt_table[g.ptype]);
    pfGSetNumPrims(gset, g.pcount);
    if (g.llist != -1)
	pfGSetPrimLengths(gset, glb->rl_list[L_LLIST][g.llist]);
    pfGSetDrawMode(gset, PFGS_FLATSHADE, oo_table[g.draw_mode[0]]);
    pfGSetDrawMode(gset, PFGS_WIREFRAME, oo_table[g.draw_mode[1]]);
    if (glb->compile_gl == PFPFB_COMPILE_GL_ON ||
	(glb->compile_gl == PFPFB_COMPILE_GL_AS_SAVED &&
	 oo_table[g.draw_mode[2]] == PF_ON))
	pfGSetDrawMode(gset, PFGS_COMPILE_GL, PF_ON);
    if (g.gstate[0] != -1)
	pfGSetGState(gset, glb->rl_list[L_GSTATE][g.gstate[0]]);
    if (g.gstate[1] != -1)
	pfGSetGStateIndex(gset, g.gstate[1]);
    
    pfGSetLineWidth(gset, g.line_width);
    pfGSetPntSize(gset, g.point_size);
    pfGSetDrawBin(gset, g.draw_bin);
    pfGSetDrawOrder(gset, g.draw_order);
    if (g.hlight != -1)
	pfGSetHlight(gset, glb->rl_list[L_HLIGHT][g.hlight]);
    pfGSetBBox(gset, &g.bbox, bound_table[g.bbox_mode]);
    if (g.udata != -1)
	set_udata((pfObject *)gset, g.udata, glb);

    if (glb->version >= PFBV_GSET_DO_DP)
    {
	pfGSetDrawOrder(gset, g.draw_order);
	if (g.decal_plane != -1)
	{
	    pfPlane *plane;

	    plane = (pfPlane *)pfMalloc(sizeof(pfPlane), glb->arena);
	    PFCOPY_VEC3(plane->normal, g.dplane_normal);
	    plane->offset = g.dplane_offset;
	    pfGSetDecalPlane(gset, plane);
	}
    }
    if (glb->version >= PFBV_GSET_BBOX_FLUX)
    {
	if (g.bbox_flux != -1)
	    pfGSetBBoxFlux(gset, glb->rl_list[L_FLUX][g.bbox_flux]);
    }
    /* Read appearance */
    if(glb->version >= PFBV_SHADER_V2) {
      READ_LINE();
      sscanf(glb->line_buf, "%d", &g.appearance);
      if(g.appearance >= 0) {
	pfGSetAppearance(gset, glb->rl_list[L_APPEARANCE][g.appearance]);
      }
    }

    /* OK, now can read GeoArray data */
    READ_LINE();
    num_attrs = atoi(glb->line_buf);
    
    stride = 0;
    for(i=0; i < num_attrs; i++){
	READ_LINE();
	if(glb->version < PFBV_MAX_TEXTURES_8)
	  sscanf(glb->line_buf, "%d %d %d %d %d %d %s", 
		 &glsize,
		 &gltype,
		 &gindex,
		 &standard,
		 &elemSize,
		 &listIndex,
		 strname);
	else {
	  if(glb->version < PFBV_GEOARRAY_STRIDE_SAVE)
	    sscanf(glb->line_buf,"%d %d %d %d %d %d",
		   &glsize,
		   &gltype,
		   &gindex,
		   &standard,
		   &elemSize,
		   &listIndex);
	  else
	    sscanf(glb->line_buf,"%d %d %d %d %d %d %d",
		   &glsize,
		   &gltype,
		   &gindex,
		   &standard,
		   &elemSize,
		   &listIndex,
		   &stride);
	    
	  {
	    char *x = strchr(glb->line_buf,'*');
	    if(x) {
	      x+=2;
	      if(strchr(x,'\n'))
		*(strchr(x,'\n')) = '\0';
	      strcpy(strname,x);
	    }
	  }
	}
	ptr = (void*) glb->rl_list[(elemSize == 1 ? L_VARRAY_1:(elemSize == 2? L_VARRAY_2:L_VARRAY_4))][listIndex];

	va = pfGArraySetMultiAttr(ga, standard, gindex, glsize, gltype, stride, ptr);
	pfVAttrName(va, strname);
    }
    
    /* lastly, check for va Index Array. */
    READ_LINE();
    j = atoi (glb->line_buf);
    if(j >= 0){
	/* index defaults to 4 byte indexes (may want to allow 16 bit) */
	pfGArrayIndexArray(ga, glb->rl_list[L_VARRAY_4][j]);
    }

/* don't do this till after all vertex arrays are loaded. */
    pfGSetIsectMask(gset, g.isect_mask,
		    (PFTRAV_SELF | PFTRAV_IS_CACHE), PF_SET);
    
    return ga;
}



static void *pfb_read_geoArray(pfa_global_t *glb)
{
    pfGeoSet 	*gset;
    pfGeoArray  *ga;
    pfVertexAttr *va;
    int buf[10];
    int standard, gindex, glsize, gltype, elemSize, listIndex, stride;
    int n_attrs, vertAttrDataSize;
    
    void *ptr;
    char strname[256];
    
    gset_t 	g;
    int		i, j;
    
    if (glb->version >= PFBV_MAX_TEXTURES_8)
        pfb_fread(&g, sizeof(gset_t), 1, glb->ifp);
    else if (glb->version >= PFBV_SHADER_V2) 
        pfb_fread(&g, sizeof(gset_4_t), 1, glb->ifp);
    else if (glb->version >= PFBV_MULTITEXTURE)
        pfb_fread(&g, sizeof(gset_3_t), 1, glb->ifp);
    else if (glb->version >= PFBV_GSET_BBOX_FLUX)
	pfb_fread(&g, sizeof(gset_2_t), 1, glb->ifp);
    else if (glb->version >= PFBV_GSET_DO_DP)
	pfb_fread(&g, sizeof(gset_1_t), 1, glb->ifp);
    else
	pfb_fread(&g, sizeof(gset_0_t), 1, glb->ifp);

    ga = pfNewGArray(glb->arena);
    gset = (pfGeoSet*) ga;		/* cast to geoSet for access */
    

    pfGSetPrimType(gset, gspt_table[g.ptype]);
    pfGSetNumPrims(gset, g.pcount);
    if (g.llist != -1)
	pfGSetPrimLengths(gset, glb->rl_list[L_LLIST][g.llist]);
    pfGSetDrawMode(gset, PFGS_FLATSHADE, oo_table[g.draw_mode[0]]);
    pfGSetDrawMode(gset, PFGS_WIREFRAME, oo_table[g.draw_mode[1]]);
    if (glb->compile_gl == PFPFB_COMPILE_GL_ON ||
	(glb->compile_gl == PFPFB_COMPILE_GL_AS_SAVED &&
	 oo_table[g.draw_mode[2]] == PF_ON))
	pfGSetDrawMode(gset, PFGS_COMPILE_GL, PF_ON);
    if (g.gstate[0] != -1)
	pfGSetGState(gset, glb->rl_list[L_GSTATE][g.gstate[0]]);
    if (g.gstate[1] != -1)
	pfGSetGStateIndex(gset, g.gstate[1]);
    pfGSetLineWidth(gset, g.line_width);
    pfGSetPntSize(gset, g.point_size);
    pfGSetDrawBin(gset, g.draw_bin);
    if (g.hlight != -1)
	pfGSetHlight(gset, glb->rl_list[L_HLIGHT][g.hlight]);
    pfGSetBBox(gset, &g.bbox, bound_table[g.bbox_mode]);
    if (g.udata != -1)
	set_udata((pfObject *)gset, g.udata, glb);

    if (glb->version >= PFBV_GSET_DO_DP)
    {
	pfGSetDrawOrder(gset, g.draw_order);
	if (g.decal_plane != -1)
	{
	    pfPlane *plane;

	    plane = (pfPlane *)pfMalloc(sizeof(pfPlane), glb->arena);
	    PFCOPY_VEC3(plane->normal, g.dplane_normal);
	    plane->offset = g.dplane_offset;
	    pfGSetDecalPlane(gset, plane);
	}
    }
    if (glb->version >= PFBV_GSET_BBOX_FLUX)
    {
	if (g.bbox_flux != -1)
	    pfGSetBBoxFlux(gset, glb->rl_list[L_FLUX][g.bbox_flux]);
    }

    if (glb->version >= PFBV_MULTITEXTURE)
    {
	/*
	 *	To retain backwards compatibility, we store texture attr's
	 *	for texture unit # 0 in tlist, and texture attributes for 
	 *	texture units #1, #2, ... in multi_tlist.
	 *	
	 *	we process multi_tlist only if the pfb format version is 
	 *	late enough.
	 */

        int numIters = 
	  (glb->version >= PFBV_MAX_TEXTURES_8) ? glb->maxTextures : 4;
        int multi_tlist[(PF_MAX_TEXTURES_31-1) * 3];

	memcpy(multi_tlist,g.multi_tlist,sizeof(int)*3*3);
	memcpy(multi_tlist + 3*3, g.multi_tlist2, sizeof(int)*4*3);

	for (i = 1 ; i < numIters ; i ++)
	{
	    j = (i - 1) * 3;
	    if (gsb_table[multi_tlist[j]] != PFGS_OFF)
		pfGSetMultiAttr(gset, PFGS_TEXCOORD2, i, 
		   gsb_table[multi_tlist[j]],
		   (multi_tlist[j + 1] == -1) ? 
			NULL : glb->rl_list[L_TLIST][multi_tlist[j + 1]],
		   (multi_tlist[j + 2] == -1) ? 
			NULL : glb->rl_list[L_ILIST][multi_tlist[j + 2]]);
	}
    }
    
    if(glb->version >= PFBV_SHADER_V2) {
      if(g.appearance >= 0) {
	islAppearance *a = glb->rl_list[L_APPEARANCE][g.appearance];
	pfGSetAppearance(gset, a);
      }
    }
    /* --- read geoArray stuff here */

    pfb_fread(&buf, sizeof(int), 1, glb->ifp);
    n_attrs = buf[0];

    if(glb->version < PFBV_GEOARRAY_STRIDE_SAVE)
      vertAttrDataSize = 6;
    else
      vertAttrDataSize = 7;

    stride = 0;
    for(i = 0; i < n_attrs; i++){
	/* load info and names */
        pfb_fread(&buf, sizeof(int), vertAttrDataSize, glb->ifp);
	pfb_fread(strname, sizeof(char), 256, glb->ifp);
	
	glsize = buf[0];
	gltype = buf[1];
	gindex = buf[2];
	standard = buf[3];
	elemSize = buf[4];
	listIndex = buf[5];

	if(glb->version >= PFBV_GEOARRAY_STRIDE_SAVE)
	  stride = buf[6];

	ptr = (void*) glb->rl_list[(elemSize == 1 ? L_VARRAY_1:(elemSize == 2? L_VARRAY_2:L_VARRAY_4))][listIndex];
	    
	va = pfGArraySetMultiAttr(ga, standard, gindex, glsize, gltype, stride, ptr);
	pfVAttrName(va, strname);
    }

    /* check for Array Index (-1 if none)  */
    pfb_fread(&buf, sizeof(int), 1, glb->ifp);
    if(buf[0] >= 0){
	pfGArrayIndexArray(ga, glb->rl_list[L_VARRAY_4][buf[0]]);
    }
/* don't do this till after all vertex arrays are loaded. */
    pfGSetIsectMask(gset, g.isect_mask,
		    (PFTRAV_SELF | PFTRAV_IS_CACHE), PF_SET);

    return(ga);
}

static void *pfa_read_udata(int udata_num, pfa_global_t *glb)
{
    void *udata;
    int size, slot;
    int i, j;

    READ_LINE();
    if (glb->version >= PFBV_UDATA_SLOT_FUNCS)
    {
	sscanf(glb->line_buf, "%d %d", &size, &slot);
	if (glb->rl_list[L_UDATA_NAME] != NULL)
	    slot = *(int*)&glb->rl_list[L_UDATA_NAME][slot];
	else
	    slot = 0;
    }
    else
    {
	size = atoi(glb->line_buf);
	slot = 0;
    }

    if (size == -1)
    {
	READ_LINE();
	sscanf(glb->line_buf, "%d %d",
	       &glb->udi[udata_num].list, &glb->udi[udata_num].id);
	udata = NULL;
    }
    else if (glb->udfunc && glb->udfunc[slot].load)
    {
	int start;

	start = (int)ftell(glb->ifp);

	glb->data_total = 0;
	udata = glb->udfunc[slot].load((void *)glb);

	if (glb->data_total != size)
	{
	    /*
	     *  The wrong amount of data was loaded by the users function.
	     */
	    pfNotify(PFNFY_WARN, PFNFY_PRINT,
		     "%s  Custom user data load function failed.\n",
		     "pfdLoadFile_pfa:");
	    pfNotify(PFNFY_WARN, PFNFY_PRINT,
		     "%s  Loaded %d bytes of data.\n",
		     "pfdLoadFile_pfa:", glb->data_total);
	    pfNotify(PFNFY_WARN, PFNFY_PRINT,
		     "%s  Should have loaded %d bytes of data.\n",
		     "pfdLoadFile_pfa:", size);
	    fseek(glb->ifp, start, SEEK_SET);
	    for (i = 0; i < (size % 39) + 1; i++)
		READ_LINE();
	}
    }
    else
    {
	unsigned char *uc;

	udata = pfMalloc(size, glb->arena);
	uc = udata;

	READ_LINE();
	for (i = 0; i < size; i++)
	{
	    j = (i % 39) * 2;

	    if (glb->line_buf[j] >= '0' && glb->line_buf[j] <= '9')
		uc[i] = (glb->line_buf[j] - '0') << 4;
	    else
		uc[i] = (glb->line_buf[j] - 'a' + 10) << 4;

	    if (glb->line_buf[j+1] >= '0' && glb->line_buf[j+1] <= '9')
		uc[i] |= glb->line_buf[j+1] - '0';
	    else
		uc[i] |= glb->line_buf[j+1] - 'a' + 10;

	    if (j == 76 && i < size - 1)
		READ_LINE();
	}
    }

    return(udata);
}


static void *pfb_read_udata(int udata_num, pfa_global_t *glb)
{
    void *udata;
    int buf[2];
    int size, slot;

    if (glb->version >= PFBV_UDATA_SLOT_FUNCS)
    {
	pfb_fread(buf, sizeof(int), 2, glb->ifp);
	size = buf[0];
	if (glb->rl_list[L_UDATA_NAME] != NULL)
	    slot = *(int*)&glb->rl_list[L_UDATA_NAME][buf[1]];
	else
	    slot = 0;
    }
    else
    {
	pfb_fread(&size, sizeof(int), 1, glb->ifp);
	slot = 0;
    }

    if (size == -1)
    {
	pfb_fread(&buf, sizeof(int), 2, glb->ifp);
	glb->udi[udata_num].list = buf[0];
	glb->udi[udata_num].id = buf[1];
	udata = NULL;
    }
    else if (glb->udfunc && glb->udfunc[slot].load)
    {
	int start;

	start = (int)pfbFileReadFunction.ftell(glb->ifp);

	glb->data_total = 0;
	udata = glb->udfunc[slot].load((void *)glb);

	if (glb->data_total != size)
	{
	    /*
	     *  The wrong amount of data was loaded by the users function.
	     */
	    pfNotify(PFNFY_WARN, PFNFY_PRINT,
		     "%s  Custom user data load function failed.\n",
		     "pfdLoadFile_pfb:");
	    pfNotify(PFNFY_WARN, PFNFY_PRINT,
		     "%s  Loaded %d bytes of data.\n",
		     "pfdLoadFile_pfb:", glb->data_total);
	    pfNotify(PFNFY_WARN, PFNFY_PRINT,
		     "%s  Should have loaded %d bytes of data.\n",
		     "pfdLoadFile_pfb:", size);
	    pfbFileReadFunction.fseek(glb->ifp, start + size, SEEK_SET);
	}
    }
    else
    {
	udata = pfMalloc(size, glb->arena);
	pfb_fread(udata, 1, size, glb->ifp);
    }

    return(udata);
}


static void *pfa_read_udata_list(pfa_global_t *glb)
{
    int i, num, *buf;

    READ_LINE();
    num = atoi(glb->line_buf);
    buf = (int*)malloc(sizeof(int) * (num*2 + 1));
    buf[0] = num;

    for (i = 0; i < num; i++)
    {
	READ_LINE();
	sscanf(glb->line_buf, "%d %d", &buf[i*2+1], &buf[i*2+2]);
    }

    return buf;
}


static void *pfb_read_udata_list(pfa_global_t *glb)
{
    int num, *buf;

    pfb_fread(&num, sizeof(int), 1, glb->ifp);
    buf = (int*)malloc(sizeof(int) * (num*2 + 1));
    buf[0] = num;
    pfb_fread(&buf[1], sizeof(int)*2, num, glb->ifp);

    return buf;
}


static void *pfa_read_udata_name(pfa_global_t *glb)
{
    READ_LINE();
    glb->line_buf[strlen(glb->line_buf)-1] = '\0';
    return (void*)pfGetNamedUserDataSlot(glb->line_buf);
}


static void *pfb_read_udata_name(pfa_global_t *glb)
{
    int length;
    char name[PF_MAXSTRING];

    pfb_fread(&length, sizeof(int), 1, glb->ifp);
    pfb_fread(name, 1, length, glb->ifp);
    name[length] = '\0';

    return (void*)pfGetNamedUserDataSlot(name);
}


static void *pfa_read_ufunc(pfa_global_t *glb)
{
    char *name, name_buf[PF_MAXSTRING];
    char *dso_name, dso_name_buf[PF_MAXSTRING];
    int i;

    READ_LINE();
    strncpy(name_buf, glb->line_buf, PF_MAXSTRING);
    if (name_buf[0] == '"')
    {
	name = name_buf + 1;
	for (i = 0; name[i] != '"'; i++);
	name[i] = '\0';
    }
    else
	READ_ERROR();

    READ_LINE();
    strncpy(dso_name_buf, glb->line_buf, PF_MAXSTRING);
    if (strncmp(dso_name_buf, "NULL", 4) != 0)
    {
	if (dso_name_buf[0] == '"')
	{
	    dso_name = dso_name_buf + 1;
	    for (i = 0; dso_name[i] != '"'; i++);
	    dso_name[i] = '\0';
	}
	else
	    READ_ERROR();
    }
    else
	dso_name = NULL;

    if (glb->dont_load_user_funcs)
	return NULL;
    else
	return find_ufunc(name, dso_name, "pfdLoadFile_pfa");
}


static void *pfa_load_ufunc(FILE *ifp)
{
    char *name, name_buf[PF_MAXSTRING];
    char *dso_name, dso_name_buf[PF_MAXSTRING];
    int i;

    do
	fgets(name_buf, PF_MAXSTRING, ifp);
    while (name_buf[0] == '#');
    if (name_buf[0] == '"')
    {
	name = name_buf + 1;
	for (i = 0; name[i] != '"'; i++);
	name[i] = '\0';
    }
    else
	return(NULL);

    do
	fgets(dso_name_buf, PF_MAXSTRING, ifp);
    while (dso_name_buf[0] == '#');
    if (strncmp(dso_name_buf, "NULL", 4) != 0)
    {
	if (dso_name_buf[0] == '"')
	{
	    dso_name = dso_name_buf + 1;
	    for (i = 0; dso_name[i] != '"'; i++);
	    dso_name[i] = '\0';
	}
	else
	    return(NULL);
    }
    else
	dso_name = NULL;

    return find_ufunc(name, dso_name, "pfdLoadNeededDSOs_pfa");
}


static void *pfb_read_ufunc(pfa_global_t *glb)
{
    int length;
    char name[PF_MAXSTRING];
    char *dso_name, dso_name_buf[PF_MAXSTRING];

    pfb_fread(&length, sizeof(int), 1, glb->ifp);
    pfb_fread(name, 1, length, glb->ifp);
    name[length] = '\0';

    pfb_fread(&length, sizeof(int), 1, glb->ifp);
    if (length != -1)
    {
	dso_name = dso_name_buf;
	pfb_fread(dso_name, 1, length, glb->ifp);
	dso_name[length] = '\0';
    }
    else
	dso_name = NULL;

    if (glb->dont_load_user_funcs)
	return NULL;
    else
	return find_ufunc(name, dso_name, "pfdLoadFile_pfb");
}


static void *pfb_load_ufunc(FILE *ifp)
{
    int length;
    char name[PF_MAXSTRING];
    char *dso_name, dso_name_buf[PF_MAXSTRING];

    pfb_fread(&length, sizeof(int), 1, ifp);
    pfb_fread(name, 1, length, ifp);
    name[length] = '\0';

    pfb_fread(&length, sizeof(int), 1, ifp);
    if (length != -1)
    {
	dso_name = dso_name_buf;
	pfb_fread(dso_name, 1, length, ifp);
	dso_name[length] = '\0';
    }
    else
	dso_name = NULL;

    return find_ufunc(name, dso_name, "pfdLoadNeededDSOs_pfb");
}


static void *find_ufunc(char *name, char *dso_name, const char *from)
{
    void *dso;
    void *ufunc;

    /*
     *  Check to see if this function has already been registered.
     */
    if ((ufunc = pfdFindRegisteredUserFunc(name)) != NULL)
	return(ufunc);
#ifndef WIN32
    /*
     *  If not, we will need to find and load it.
     */

    /*
     *  First check to see if it is already loaded.
     */
    dso = dlopen(NULL, RTLD_LAZY);
    ufunc = dlsym(dso, name);

    /*
     *  If that did not work, try to load the dso and find it in it.
     */
    if (!ufunc && dso_name != NULL)
    {
	if (dso = dlopen(dso_name, RTLD_LAZY))
	{
	    ufunc = dlsym(dso, name);
	    if (pfIsConfiged())
	    {
		pfNotify(PFNFY_NOTICE, PFNFY_PRINT,
			 "%s: Loaded \"%s\" DSO", from, dso_name);
		pfNotify(PFNFY_NOTICE, PFNFY_MORE,
			 "after pfConfig().  This might cause");
		pfNotify(PFNFY_NOTICE, PFNFY_MORE,
			 "problems.  pfdInitConverter() should");
		pfNotify(PFNFY_NOTICE, PFNFY_MORE,
			 "have been called for this file before");
		pfNotify(PFNFY_NOTICE, PFNFY_MORE,
			 "pfConfig().");
	    }

	}
	else
	    pfNotify(PFNFY_WARN, PFNFY_PRINT,
		     "%s: Unable to open \"%s\" DSO.", from, dso_name);
    }

    if (ufunc)
    {
	/*
	 *  We have found the function, and we should register it.
	 */
	pfdRegisterUserFunc(ufunc, name, dso_name);
	return ufunc;
    }

    pfNotify(PFNFY_WARN, PFNFY_PRINT,
	     "%s: Unable to find", from);
    pfNotify(PFNFY_WARN, PFNFY_MORE,
	     "user callback function");
    pfNotify(PFNFY_WARN, PFNFY_MORE,
	     "\"%s\".", name);
#else /* WIN32 */
    pfNotify(PFNFY_FATAL, PFNFY_PRINT, "NYI: user functions in pfb loader. Fatal error\n");
#endif /* WIN32 */

    return NULL;
}


static void *pfa_read_image(pfa_global_t *glb)
{
    uint *image;
    int size;
    int num_comp, comp_size, comp_order, row_size;
    unsigned char *uc;
    int i, j;

    READ_LINE();
    sscanf(glb->line_buf, "%d %d %d %d %d",
	   &size, &num_comp, &comp_size, &comp_order, &row_size);
    image = pfMalloc(size, glb->arena);
    uc = (unsigned char *)image;

    READ_LINE();
    for (i = 0; i < size; i++)
    {
	j = (i % 39) * 2;

	if (glb->line_buf[j] >= '0' && glb->line_buf[j] <= '9')
	    uc[i] = (glb->line_buf[j] - '0') << 4;
	else
	    uc[i] = (glb->line_buf[j] - 'a' + 10) << 4;

	if (glb->line_buf[j+1] >= '0' && glb->line_buf[j+1] <= '9')
	    uc[i] |= glb->line_buf[j+1] - '0';
	else
	    uc[i] |= glb->line_buf[j+1] - 'a' + 10;

	if (j == 76 && i < size - 1)
	    READ_LINE();
    }

    if (comp_order != COMPONENT_ORDER)
	image = flip_components(image, size, num_comp, comp_size,
				row_size);

    return(image);
}


static void *pfb_read_image(pfa_global_t *glb)
{
    uint *image;
    int buf[5];
    int flipBeforeDecryptFlag = (r_swap && glb->crypt_key != NULL) ? 1 : 0;

    pfb_fread(buf, sizeof(int), 5, glb->ifp);
    image = pfMalloc(buf[0], glb->arena);
    pfb_fread(image, 1, buf[0], glb->ifp);

    if (glb->crypt_key) 
    {
      if(flipBeforeDecryptFlag)
	  image = flip_components(image, buf[0], 4, buf[2], buf[4]);

      glb->decrypt_func(buf[0], glb->crypt_key, image);
    }

    if(flipBeforeDecryptFlag)
      image = flip_components(image, buf[0], 4, buf[2], buf[4]);
    else if(buf[3] != COMPONENT_ORDER)
      image = flip_components(image, buf[0], buf[1], buf[2], buf[4]);

    return(image);
}


static uint *flip_components(uint *image, int size, int num_comp, int comp_size,
			     int row_size)
{
    int bpr;		/* bytes per row */
    int bpr_in;
    int bpr_out;
    int rows;
    int texels;
    int num_uints;
    int remain;
    uint *out_image;
    unsigned char *uc_image;
    unsigned char *uc_out_image;
    unsigned char *in_row, *out_row;
    unsigned char uc_tmp[8];
    uint ui_tmp;
    int i, j;

    bpr = row_size * num_comp * comp_size;
    remain = bpr % 4;
    if (remain)
    {
	remain = 4 - remain;
	bpr_in = bpr + remain;
	bpr_out = bpr;
	rows = size / bpr_in;
	out_image = pfMalloc(rows * bpr_out, pfGetArena(image));
	texels = rows * row_size;
	uc_image = (unsigned char *)image;
	uc_out_image = (unsigned char *)out_image;

	if (comp_size == 1)
	{
	    switch (num_comp)
	    {
		case 1:
		    for (j = 0; j < rows; j++)
		    {
			in_row = &uc_image[j*bpr_in];
			out_row = &uc_out_image[j*bpr_out];
			for (i = 0; i < bpr; i++)
			    out_row[i] = in_row[i];
		    }
		    break;
		case 2:
		    for (j = 0; j < rows; j++)
		    {
			in_row = &uc_image[j*bpr_in];
			out_row = &uc_out_image[j*bpr_out];
			for (i = 0; i < bpr; i += 2)
			{
			    out_row[i]   = in_row[i+1];
			    out_row[i+1] = in_row[i];
			}
		    }
		    break;
		case 3:
		    for (j = 0; j < rows; j++)
		    {
			in_row = &uc_image[j*bpr_in];
			out_row = &uc_out_image[j*bpr_out];
			for (i = 0; i < bpr; i += 3)
			{
			    out_row[i]   = in_row[i+2];
			    out_row[i+1] = in_row[i+1];
			    out_row[i+2] = in_row[i];
			}
		    }
		    break;
		case 4:
		    for (j = 0; j < rows; j++)
		    {
			in_row = &uc_image[j*bpr_in];
			out_row = &uc_out_image[j*bpr_out];
			for (i = 0; i < bpr; i += 4)
			{
			    out_row[i]   = in_row[i+3];
			    out_row[i+1] = in_row[i+2];
			    out_row[i+2] = in_row[i+1];
			    out_row[i+3] = in_row[i];
			}
		    }
		    break;
	    }
	}
	else
	{
	    switch (num_comp)
	    {
		case 1:
		    for (j = 0; j < rows; j++)
		    {
			in_row = &uc_image[j*bpr_in];
			out_row = &uc_out_image[j*bpr_out];
			for (i = 0; i < bpr; i += 2)
			{
			    out_row[i] = in_row[i];
			    out_row[i+1] = in_row[i+1];
			}
		    }
		    break;
		case 2:
		    for (j = 0; j < rows; j++)
		    {
			in_row = &uc_image[j*bpr_in];
			out_row = &uc_out_image[j*bpr_out];
			for (i = 0; i < bpr; i += 4)
			{
			    out_row[i]   = in_row[i+2];
			    out_row[i+1] = in_row[i+3];
			    out_row[i+2] = in_row[i];
			    out_row[i+3] = in_row[i+1];
			}
		    }
		    break;
		case 3:
		    for (j = 0; j < rows; j++)
		    {
			in_row = &uc_image[j*bpr_in];
			out_row = &uc_out_image[j*bpr_out];
			for (i = 0; i < bpr; i += 6)
			{
			    out_row[i]   = in_row[i+4];
			    out_row[i+1] = in_row[i+5];
			    out_row[i+2] = in_row[i+2];
			    out_row[i+3] = in_row[i+3];
			    out_row[i+4] = in_row[i];
			    out_row[i+5] = in_row[i+1];
			}
		    }
		    break;
		case 4:
		    for (j = 0; j < rows; j++)
		    {
			in_row = &uc_image[j*bpr_in];
			out_row = &uc_out_image[j*bpr_out];
			for (i = 0; i < bpr; i += 8)
			{
			    out_row[i]   = in_row[i+6];
			    out_row[i+1] = in_row[i+7];
			    out_row[i+2] = in_row[i+4];
			    out_row[i+3] = in_row[i+5];
			    out_row[i+4] = in_row[i+2];
			    out_row[i+5] = in_row[i+3];
			    out_row[i+6] = in_row[i];
			    out_row[i+7] = in_row[i+1];
			}
		    }
		    break;
	    }
	}

	pfFree(image);
    }
    else /* no remain */
    {
	rows = size / bpr;
	out_image = image;
	texels = rows * row_size;

	if (comp_size == 1)
	{
	    switch (num_comp)
	    {
		case 1:
		    break;
		case 2:
		    num_uints = texels / 2;
		    for (i = 0; i < num_uints; i++)
			image[i] = ((image[i] >> 8) & 0x00ff00ff) |
				   ((image[i] << 8) & 0xff00ff00);
		    break;
		case 3:
		    uc_image = (unsigned char *)image;
		    for (i = 0; i < size; i += 3)
		    {
			uc_tmp[0] = uc_image[i];
			uc_image[i] = uc_image[i+2];
			uc_image[i+2] = uc_tmp[0];
		    }
		    break;
		case 4:
		    for (i = 0; i < texels; i++)
			image[i] = ((image[i] >> 24) & 0x000000ff) |
				   ((image[i] >>  8) & 0x0000ff00) |
				   ((image[i] <<  8) & 0x00ff0000) |
				   ((image[i] << 24) & 0xff000000);
		    break;
	    }
	}
	else
	{
	    switch (num_comp)
	    {
		case 1:
		    break;
		case 2:
		    for (i = 0; i < texels; i++)
			image[i] = ((image[i] >> 16) & 0x0000ffff) |
				   ((image[i] << 16) & 0xffff0000);
		    break;
		case 3:
		    uc_image = (unsigned char *)image;
		    for (i = 0; i < size; i += 3)
		    {
			uc_tmp[0] = uc_image[i];
			uc_tmp[1] = uc_image[i+1];
			uc_image[i] = uc_image[i+4];
			uc_image[i+1] = uc_image[i+5];
			uc_image[i+4] = uc_tmp[0];
			uc_image[i+5] = uc_tmp[1];
		    }
		    break;
		case 4:
		    num_uints = texels * 2;
		    for (i = 0; i < num_uints; i+=2)
		    {
			ui_tmp = ((image[i] >> 16) & 0x0000ffff) |
				 ((image[i] << 16) & 0xffff0000);
			image[i] = ((image[i+1] >> 16) & 0x0000ffff) |
				   ((image[i+1] << 16) & 0xffff0000);
			image[i+1] = ui_tmp;
		    }
		    break;
	    }
	}
    }

    return(out_image);
}


static void *pfa_read_itile(pfa_global_t *glb)
{
/* XXX */
glb = glb;
return NULL;
}


static void *pfb_read_itile(pfa_global_t *glb)
{
    pfImageTile *itile;
    itile_t t;
    char *name;

    pfb_fread(&t, sizeof(itile_t), 1, glb->ifp);

    itile = pfNewImageTile(glb->arena);

    pfImageTileSize(itile, t.size[0], t.size[1], t.size[2]);
    pfImageTileOrigin(itile, t.origin[0], t.origin[1], t.origin[2]);
    pfImageTileMemImageFormat(itile, itif_table[t.mem_image_format]);
    pfImageTileMemImageType(itile, txef_table[t.mem_image_type]);
    pfImageTileMemInfo(itile, t.mem_info[0], t.mem_info[1]);
    pfImageTileFileImageFormat(itile, itif_table[t.file_image_format]);
    pfImageTileFileImageType(itile, txef_table[t.file_image_type]);
    pfImageTileFileTile(itile, t.file_tile[0], t.file_tile[1], t.file_tile[2]);
    pfImageTileNumFileTiles(itile, t.num_file_tiles[0],
			    t.num_file_tiles[1], t.num_file_tiles[2]);
    pfImageTileHeaderOffset(itile, t.header_offset);
    pfImageTilePriority(itile, t.priority);
    pfImageTileDefaultTileMode(itile, oo_table[t.default_tile_mode]);
    if (t.default_tile != -1)
	pfImageTileDefaultTile(itile, glb->rl_list[L_ITILE][t.default_tile]);
    if (t.read_func != -1)
	pfImageTileReadFunc(itile, glb->rl_list[L_UFUNC][t.read_func]);
    if (t.read_queue != -1)
	pfImageTileReadQueue(itile, ID_TO_QUEUE(t.read_queue));
    if (t.udata != -1)
	set_udata((pfObject *)itile, t.udata, glb);

    name = (char *)glb->buf;
    if (t.name_size != -1)
    {
	pfb_fread(name, sizeof(char), t.name_size, glb->ifp);
	name[t.name_size] = '\0';
	pfImageTileName(itile, name);
    }
    if (t.fname_size != -1)
    {
	pfb_fread(name, sizeof(char), t.fname_size, glb->ifp);
	name[t.fname_size] = '\0';
	pfImageTileFileName(itile, name);
    }

    return(itile);
}


static void *pfa_read_icache(pfa_global_t *glb)
{
/* XXX */
glb = glb;
return NULL;
}


static void *pfb_read_icache(pfa_global_t *glb)
{
    pfImageCache *icache;
    char *name;
    char *format_string;
    int *format_args;
    icache_t c;
    stream_server_t *ss;
    char *ss_names;
    int total_ss;
    int i, j;

    icache = pfNewImageCache(glb->arena);

    pfb_fread(&c, sizeof(icache_t), 1, glb->ifp);

    pfImageCacheImageSize(icache, c.image_size[0],
			  c.image_size[1], c.image_size[2]);
    if (c.proto_tile != -1)
	pfImageCacheProtoTile(icache, glb->rl_list[L_ITILE][c.proto_tile]);
    pfImageCacheMemRegionSize(icache, c.mem_region_size[0],
			      c.mem_region_size[1], c.mem_region_size[2]);
    pfImageCacheMemRegionOrg(icache, c.mem_region_org[0],
			     c.mem_region_org[1], c.mem_region_org[2]);
    pfImageCacheTexRegionSize(icache, c.tex_region_size[0],
			      c.tex_region_size[1], c.tex_region_size[2]);
    pfImageCacheTexRegionOrg(icache, c.tex_region_org[0],
			     c.tex_region_org[1], c.tex_region_org[2]);
    pfImageCacheTexSize(icache, c.tex_size[0], c.tex_size[1], c.tex_size[2]);
    if (c.read_queue_func != -1)
	pfImageCacheReadQueueFunc(icache,
				  glb->rl_list[L_UFUNC][c.read_queue_func]);
    if (c.tile_file_name_func != -1)
	pfImageCacheTileFileNameFunc(icache,
				     glb->rl_list[L_UFUNC]
						 [c.tile_file_name_func]);
    if (c.master != -1)
	pfImageCacheMaster(icache, glb->rl_list[L_ICACHE][c.master]);
    pfImageCacheMode(icache, PFIMAGECACHE_AUTOCENTER, c.auto_center);
    pfImageCacheMode(icache, PFIMAGECACHE_AUTOCREATE_STREAMSERVER_QUEUES,
		     c.auto_stream_server_queues);
    pfImageCacheMode(icache, PFIMAGECACHE_AUTOSET_TILE_FILENAME,
		     c.auto_tile_file_name);
    pfImageCacheMode(icache, PFIMAGECACHE_AUTOSET_TILE_READQUEUE,
		     c.auto_tile_read_queue);
    if (c.udata != -1)
	set_udata((pfObject *)icache, c.udata, glb);

    name = (char *)glb->buf;
    if (c.name_size != -1)
    {
	pfb_fread(name, sizeof(char), c.name_size, glb->ifp);
	name[c.name_size] = '\0';
	pfImageCacheName(icache, name);
    }

    format_args = glb->buf;
    if (c.num_format_args > 0)
    {
	pfb_fread(format_args, sizeof(int), c.num_format_args, glb->ifp);
	for (i = 0; i < c.num_format_args; i++)
	    format_args[i] = icfnf_table[format_args[i]];
    }
    format_string = (char *)(&format_args[c.num_format_args]);
    if (c.format_string_size != -1)
    {
	pfb_fread(format_string, sizeof(char), c.format_string_size, glb->ifp);
	pfImageCacheTileFileNameFormat(icache, format_string,
				       c.num_format_args, format_args);
    }

    if (c.stream_server_size > 0)
    {
	ss = (stream_server_t *)GET_BUF(c.stream_server_size);
	pfb_fread(ss, sizeof(int), c.stream_server_size, glb->ifp);
	total_ss = c.num_stream_servers[0] + c.num_stream_servers[1] +
		   c.num_stream_servers[2];
	ss_names = (char *)(&ss[total_ss]);
	j = 0;
	for (i = 0; i < c.num_stream_servers[0]; i++, j++)
	{
	    if (ss[j].queue != -1)
		pfImageCacheStreamServerQueue(icache, PFIMAGECACHE_S_DIMENSION,
					      i, ID_TO_QUEUE(ss[j].queue));
	    if (ss[j].name_length > 0)
	    {
		pfImageCacheFileStreamServer(icache, PFIMAGECACHE_S_DIMENSION,
					     i, ss_names);
		ss_names += ss[j].name_length;
	    }
	}
	for (i = 0; i < c.num_stream_servers[1]; i++, j++)
	{
	    if (ss[j].queue != -1)
		pfImageCacheStreamServerQueue(icache, PFIMAGECACHE_T_DIMENSION,
					      i, ID_TO_QUEUE(ss[j].queue));
	    if (ss[j].name_length > 0)
	    {
		pfImageCacheFileStreamServer(icache, PFIMAGECACHE_T_DIMENSION,
					     i, ss_names);
		ss_names += ss[j].name_length;
	    }
	}
	for (i = 0; i < c.num_stream_servers[2]; i++, j++)
	{
	    if (ss[j].queue != -1)
		pfImageCacheStreamServerQueue(icache, PFIMAGECACHE_R_DIMENSION,
					      i, ID_TO_QUEUE(ss[j].queue));
	    if (ss[j].name_length > 0)
	    {
		pfImageCacheFileStreamServer(icache, PFIMAGECACHE_R_DIMENSION,
					     i, ss_names);
		ss_names += ss[j].name_length;
	    }
	}
    }

    return(icache);
}


static void *pfa_read_queue(pfa_global_t *glb)
{
    pfQueue *queue;
    queue_t q;

    READ_LINE();
    sscanf(glb->line_buf, "%d %d %d", &q.elt_size, &q.num_elts, &q.udata);

    queue = pfNewQueue(q.elt_size, q.num_elts, glb->arena);

    if (q.udata != -1)
	set_udata((pfObject *)queue, q.udata, glb);

    return(queue);
}


static void *pfb_read_queue(pfa_global_t *glb)
{
    pfQueue *queue;
    queue_t q;

    pfb_fread(&q, sizeof(queue_t), 1, glb->ifp);

    queue = pfNewQueue(q.elt_size, q.num_elts, glb->arena);

    if (q.udata != -1)
	set_udata((pfObject *)queue, q.udata, glb);

    return(queue);
}


static void *pfa_read_morph(pfa_global_t *glb)
{
    pfMorph *morph;
    int num_attr;
    int num_src, max_src;
    int attr_type, list_id;
    int fpe, e, dst_id;
    int t1, t2;
    float **alist;
    ushort **ilist;
    int *nlist;
    float *weights;
    int indexed;
    void *dst;
    int i, j;

    max_src = 0;

    morph = pfNewMorph();
    READ_LINE();
    num_attr = atoi(glb->line_buf);

    for (i = 0; i < num_attr; i++)
    {
	READ_LINE();
	attr_type = atoi(glb->line_buf);
	if (attr_type != MORPH_ATTR_MISSING)
	{
	    switch(attr_type)
	    {
		case MORPH_ATTR_COLOR4:
		    list_id = L_CLIST;
		    break;
		case MORPH_ATTR_NORMAL3:
		    list_id = L_NLIST;
		    break;
		case MORPH_ATTR_TEXCOORD2:
		    list_id = L_TLIST;
		    break;
		case MORPH_ATTR_COORD3:
		    list_id = L_VLIST;
		    break;
		case MORPH_ATTR_OTHER:
		    list_id = L_FLIST;
		    break;
	    }

	    READ_LINE();
	    sscanf(glb->line_buf, "%d %d %d %d %d",
		   &fpe, &e, &num_src, &dst_id, &indexed);

	    dst = glb->rl_list[list_id][dst_id];
	    if (num_src > max_src)
	    {
		if (max_src > 0)
		{
		    free(alist);
		    free(ilist);
		    free(nlist);
		    free(weights);
		}
		alist = (float **)malloc(num_src * sizeof(float *));
		ilist = (ushort **)malloc(num_src * sizeof(ushort *));
		nlist = (int *)malloc(num_src * sizeof(int));
		weights = (float *)malloc(num_src * sizeof(float));
		max_src = num_src;
	    }
	    if (indexed)
	    {
		for (j = 0; j < num_src; j++)
		{
		    READ_LINE();
		    sscanf(glb->line_buf, "%d %d %d", &t1, &t2, &nlist[j]);
		    alist[j] = glb->rl_list[list_id][t1];
		    ilist[j] = glb->rl_list[L_ILIST][t2];
		}
		pfMorphAttr(morph, i, fpe, e, dst, num_src,
			    alist, ilist, nlist);
	    }
	    else
	    {
		for (j = 0; j < num_src; j++)
		{
		    READ_LINE();
		    alist[j] = glb->rl_list[list_id][atoi(glb->line_buf)];
		}
		pfMorphAttr(morph, i, fpe, e, dst, num_src, alist, NULL, NULL);
	    }

	    for (j = 0; j < num_src; j++)
	    {
		READ_LINE();
		weights[j] = atof(glb->line_buf);
	    }
	    pfMorphWeights(morph, i, weights);
	}
    }

    if (max_src > 0)
    {
	free(alist);
	free(ilist);
	free(nlist);
	free(weights);
    }

    return(morph);
}


static void *pfb_read_morph(pfa_global_t *glb)
{
    pfMorph *morph;
    int num_attr;
    int num_src, max_src;
    int attr_type, list_id;
    int fpe, e, dst_id;
    float **alist;
    ushort **ilist;
    int *nlist;
    float *weights;
    int indexed;
    void *dst;
    int i, j;
    int buf_size, buf_pos;
    int *buf;

    max_src = 0;

    pfb_fread(&buf_size, sizeof(int), 1, glb->ifp);
    buf = GET_BUF(buf_size);
	pfb_fread(buf, sizeof(int), buf_size, glb->ifp);

    buf_pos = 0;
    max_src = 0;

    morph = pfNewMorph();
    num_attr = buf[buf_pos++];

    for (i = 0; i < num_attr; i++)
    {
	attr_type = buf[buf_pos++];
	if (attr_type != MORPH_ATTR_MISSING)
	{
	    switch(attr_type)
	    {
		case MORPH_ATTR_COLOR4:
		    list_id = L_CLIST;
		    break;
		case MORPH_ATTR_NORMAL3:
		    list_id = L_NLIST;
		    break;
		case MORPH_ATTR_TEXCOORD2:
		    list_id = L_TLIST;
		    break;
		case MORPH_ATTR_COORD3:
		    list_id = L_VLIST;
		    break;
		case MORPH_ATTR_OTHER:
		    list_id = L_FLIST;
		    break;
	    }

	    fpe = buf[buf_pos++];
	    e = buf[buf_pos++];
	    num_src = buf[buf_pos++];
	    dst_id = buf[buf_pos++];
	    indexed = buf[buf_pos++];

	    dst = glb->rl_list[list_id][dst_id];
	    if (num_src > max_src)
	    {
		if (max_src > 0)
		{
		    free(alist);
		    free(ilist);
		    free(nlist);
		    free(weights);
		}
		alist = (float **)malloc(num_src * sizeof(float *));
		ilist = (ushort **)malloc(num_src * sizeof(ushort *));
		nlist = (int *)malloc(num_src * sizeof(int));
		weights = (float *)malloc(num_src * sizeof(float));
		max_src = num_src;
	    }
	    if (indexed)
	    {
		for (j = 0; j < num_src; j++)
		{
		    alist[j] = glb->rl_list[list_id][buf[buf_pos++]];
		    ilist[j] = glb->rl_list[L_ILIST][buf[buf_pos++]];
		    nlist[j] = buf[buf_pos++];
		}
		pfMorphAttr(morph, i, fpe, e, dst, num_src,
			    alist, ilist, nlist);
	    }
	    else
	    {
		for (j = 0; j < num_src; j++)
		    alist[j] = glb->rl_list[list_id][buf[buf_pos++]];
		pfMorphAttr(morph, i, fpe, e, dst, num_src, alist, NULL, NULL);
	    }

	    for (j = 0; j < num_src; j++)
		weights[j] = ((float *)buf)[buf_pos++];
	    pfMorphWeights(morph, i, weights);
	}
    }

    if (max_src > 0)
    {
	free(alist);
	free(ilist);
	free(nlist);
	free(weights);
    }

    return(morph);
}


static void *pfa_read_lodstate(pfa_global_t *glb)
{
    pfLODState *ls;
    char *s;
    int t1;
    int i;

    ls = pfNewLODState();
    READ_LINE();
    pfLODStateAttr(ls, PFLODSTATE_RANGE_RANGEOFFSET, strtod(glb->line_buf, &s));
    pfLODStateAttr(ls, PFLODSTATE_RANGE_RANGESCALE, strtod(s, &s));
    pfLODStateAttr(ls, PFLODSTATE_TRANSITION_RANGEOFFSET, strtod(s, &s));
    pfLODStateAttr(ls, PFLODSTATE_TRANSITION_RANGESCALE, strtod(s, &s));
    pfLODStateAttr(ls, PFLODSTATE_RANGE_STRESSOFFSET, strtod(s, &s));
    pfLODStateAttr(ls, PFLODSTATE_RANGE_STRESSSCALE, strtod(s, &s));
    pfLODStateAttr(ls, PFLODSTATE_TRANSITION_STRESSOFFSET, strtod(s, &s));
    pfLODStateAttr(ls, PFLODSTATE_TRANSITION_STRESSSCALE, strtod(s, &s));

    READ_LINE();
    if (strncmp(glb->line_buf, "NULL", 4))
    {
	if (glb->line_buf[0] == '"')
	{
	    for (i = 1; glb->line_buf[i] != '"' && glb->line_buf[i] != '\0';
		 i++);
	    glb->line_buf[i] = '\0';
	    pfLODStateName(ls, &glb->line_buf[1]);
	}
	else
	    READ_ERROR();
    }

    READ_LINE();
    t1 = atoi(glb->line_buf);
    if (t1 != -1)
	set_udata((pfObject *)ls, t1, glb);

    return(ls);
}


static void *pfb_read_lodstate(pfa_global_t *glb)
{
    pfLODState *ls;
    float attr[8];
    int buf_size;
    int t1;

    ls = pfNewLODState();
    pfb_fread(&attr, sizeof(float), 8, glb->ifp);
    pfLODStateAttr(ls, PFLODSTATE_RANGE_RANGEOFFSET, attr[0]);
    pfLODStateAttr(ls, PFLODSTATE_RANGE_RANGESCALE, attr[1]);
    pfLODStateAttr(ls, PFLODSTATE_TRANSITION_RANGEOFFSET, attr[2]);
    pfLODStateAttr(ls, PFLODSTATE_TRANSITION_RANGESCALE, attr[3]);
    pfLODStateAttr(ls, PFLODSTATE_RANGE_STRESSOFFSET, attr[4]);
    pfLODStateAttr(ls, PFLODSTATE_RANGE_STRESSSCALE, attr[5]);
    pfLODStateAttr(ls, PFLODSTATE_TRANSITION_STRESSOFFSET, attr[6]);
    pfLODStateAttr(ls, PFLODSTATE_TRANSITION_STRESSSCALE, attr[7]);

    pfb_fread(&buf_size, sizeof(int), 1, glb->ifp);
    if (buf_size != -1)
    {
	char name[PF_MAXSTRING];

	pfb_fread(name, 1, buf_size, glb->ifp);
	name[buf_size] = '\0';
	pfLODStateName(ls, name);
    }

    pfb_fread(&t1, sizeof(int), 1, glb->ifp);
    if (t1 != -1)
	set_udata((pfObject *)ls, t1, glb);

    return(ls);
}


static void *pfa_read_font(pfa_global_t *glb)
{
    pfFont *font;
    int num_chars;
    pfBox bbox;
    pfVec3 v1;
    int t1;
    int i;

    font = pfNewFont(glb->arena);

    READ_LINE();
    if (strncmp(glb->line_buf, "NULL", 4))
    {
	if (glb->line_buf[0] == '"')
	{
	    for (i = 1; glb->line_buf[i] != '"' && glb->line_buf[i] != '\0';
		 i++);
	    glb->line_buf[i] = '\0';
	    pfFontAttr(font, PFFONT_NAME, &glb->line_buf[1]);
	}
	else
	    READ_ERROR();
    }

    READ_LINE();
    num_chars = atoi(glb->line_buf);
    pfFontMode(font, PFFONT_NUM_CHARS, num_chars);
    READ_LINE();
#ifdef XXX_FIXED_FONT_CHAR_SPACING_BUG
    pfFontMode(font, PFFONT_CHAR_SPACING, fontcs_table[atoi(glb->line_buf)]);
#endif
    READ_LINE();
    pfFontMode(font, PFFONT_RETURN_CHAR, atoi(glb->line_buf));
    READ_LINE();
    pfFontVal(font, PFFONT_UNIT_SCALE, atof(glb->line_buf));
    READ_LINE();
    pfFontAttr(font, PFFONT_GSTATE,
	       glb->rl_list[L_GSTATE][atoi(glb->line_buf)]);
    READ_LINE();
    sscanf(glb->line_buf, "%g %g %g",
	   &bbox.min[0], &bbox.min[1], &bbox.min[2]);
    READ_LINE();
    sscanf(glb->line_buf, "%g %g %g",
	   &bbox.max[0], &bbox.max[1], &bbox.max[2]);
    READ_LINE();
    sscanf(glb->line_buf, "%g %g %g", &v1[0], &v1[1], &v1[2]);
    pfFontAttr(font, PFFONT_SPACING, v1);

    for (i = 0; i < num_chars; i++)
    {
	READ_LINE();
	sscanf(glb->line_buf, "%d %g %g %g", &t1, &v1[0], &v1[1], &v1[2]);
	if (t1 != -1){
	    if(glb->version >= PFBV_GEOARRAY){	/* Check for geoArray */
		pfFontCharGSet(font, i,
			       (t1 & PFB_GARRAY_IXBIT ?
				glb->rl_list[L_GARRAY][t1 ^ PFB_GARRAY_IXBIT]:
				glb->rl_list[L_GSET][t1]));
	    } else {
		pfFontCharGSet(font, i, glb->rl_list[L_GSET][t1]);
	    }
	    
	}
	pfFontCharSpacing(font, i, v1);
    }

    pfFontAttr(font, PFFONT_BBOX, &bbox);

    return(font);
}


static void *pfb_read_font(pfa_global_t *glb)
{
    pfFont *font;
    int num_chars;
    pfBox bbox;
    pfVec3 v1;
    int t1;
    int i;
    int buf_size, buf_pos;
    int *buf;

    font = pfNewFont(glb->arena);

    pfb_fread(&buf_size, sizeof(int), 1, glb->ifp);
    if (buf_size != -1)
    {
	char name[PF_MAXSTRING];

	pfb_fread(name, 1, buf_size, glb->ifp);
	name[buf_size] = '\0';
	pfFontAttr(font, PFFONT_NAME, name);
    }

    pfb_fread(&buf_size, sizeof(int), 1, glb->ifp);
    buf = GET_BUF(buf_size);
    pfb_fread(buf, sizeof(int), buf_size, glb->ifp);

    buf_pos = 0;
    num_chars = buf[buf_pos++];
    pfFontMode(font, PFFONT_NUM_CHARS, num_chars);
#ifdef XXX_FIXED_FONT_CHAR_SPACING_BUG
    pfFontMode(font, PFFONT_CHAR_SPACING, fontcs_table[buf[buf_pos++]]);
#else
    buf_pos++;
#endif
    pfFontMode(font, PFFONT_RETURN_CHAR, buf[buf_pos++]);
    pfFontVal(font, PFFONT_UNIT_SCALE, ((float *)buf)[buf_pos++]);
    pfFontAttr(font, PFFONT_GSTATE, glb->rl_list[L_GSTATE][buf[buf_pos++]]);
    bbox.min[0] = ((float *)buf)[buf_pos++];
    bbox.min[1] = ((float *)buf)[buf_pos++];
    bbox.min[2] = ((float *)buf)[buf_pos++];
    bbox.max[0] = ((float *)buf)[buf_pos++];
    bbox.max[1] = ((float *)buf)[buf_pos++];
    bbox.max[2] = ((float *)buf)[buf_pos++];
    v1[0] = ((float *)buf)[buf_pos++];
    v1[1] = ((float *)buf)[buf_pos++];
    v1[2] = ((float *)buf)[buf_pos++];
    pfFontAttr(font, PFFONT_SPACING, v1);

    for (i = 0; i < num_chars; i++)
    {
	t1 = buf[buf_pos++];
	v1[0] = ((float *)buf)[buf_pos++];
	v1[1] = ((float *)buf)[buf_pos++];
	v1[2] = ((float *)buf)[buf_pos++];
	if (t1 != -1){
	    if(glb->version >= PFBV_GEOARRAY){	/* Check for geoArray */
		pfFontCharGSet(font, i,
			       (t1 & PFB_GARRAY_IXBIT ?
				glb->rl_list[L_GARRAY][t1 ^ PFB_GARRAY_IXBIT]:
				glb->rl_list[L_GSET][t1]));
	    } else {
		pfFontCharGSet(font, i, glb->rl_list[L_GSET][t1]);
	    }
	}
	pfFontCharSpacing(font, i, v1);
    }

    pfFontAttr(font, PFFONT_BBOX, &bbox);

    return(font);
}


static void *pfa_read_string(pfa_global_t *glb)
{
    pfString *string;
    string_t s;
    void *str;
    int size;
    int i, j;
    unsigned char *uc_str;

    READ_LINE();
    sscanf(glb->line_buf, "%f %f %f %f",
	   &s.mat[0][0], &s.mat[0][1], &s.mat[0][2], &s.mat[0][3]);
    READ_LINE();
    sscanf(glb->line_buf, "%f %f %f %f",
	   &s.mat[1][0], &s.mat[1][1], &s.mat[1][2], &s.mat[1][3]);
    READ_LINE();
    sscanf(glb->line_buf, "%f %f %f %f",
	   &s.mat[2][0], &s.mat[2][1], &s.mat[2][2], &s.mat[2][3]);
    READ_LINE();
    sscanf(glb->line_buf, "%f %f %f %f",
	   &s.mat[3][0], &s.mat[3][1], &s.mat[3][2], &s.mat[3][3]);
    READ_LINE();
    s.justify = atoi(glb->line_buf);
    READ_LINE();
    s.char_size = atoi(glb->line_buf);
    READ_LINE();
    s.font = atoi(glb->line_buf);
    READ_LINE();
    sscanf(glb->line_buf, "%f %f %f",
	   &s.spacing_scale[0], &s.spacing_scale[1], &s.spacing_scale[2]);
    READ_LINE();
    sscanf(glb->line_buf, "%f %f %f %f",
	   &s.color[0], &s.color[1], &s.color[2], &s.color[3]);
    READ_LINE();
    sscanf(glb->line_buf, "%d %d", &s.gstate[0], &s.gstate[1]);
    READ_LINE();
    s.isect_mask = (uint)strtoul(glb->line_buf, NULL, 16);
    READ_LINE();
    sscanf(glb->line_buf, "%f %f %f",
	   &s.bbox.min[0], &s.bbox.min[1], &s.bbox.min[2]);
    READ_LINE();
    sscanf(glb->line_buf, "%f %f %f",
	   &s.bbox.max[0], &s.bbox.max[1], &s.bbox.max[2]);

    READ_LINE();
    if ((size = atoi(glb->line_buf)) != -1);
    {
	str = pfMalloc(size, glb->arena);
	uc_str = str;
	READ_LINE();
	for (i = 0; i < size; i++)
	{
	    j = (i % 39) * 2;

	    if (glb->line_buf[j] >= '0' && glb->line_buf[j] <= '9')
		uc_str[i] = (glb->line_buf[j] - '0') << 4;
	    else
		uc_str[i] = (glb->line_buf[j] - 'a' + 10) << 4;

	    if (glb->line_buf[j+1] >= '0' && glb->line_buf[j+1] <= '9')
		uc_str[i] |= glb->line_buf[j+1] - '0';
	    else
		uc_str[i] |= glb->line_buf[j+1] - 'a' + 10;

	    if (j == 76 && i < size - 1)
		READ_LINE();
	}
    }

    string = pfNewString(glb->arena);
    pfStringMat(string, s.mat);
    pfStringMode(string, PFSTR_JUSTIFY, strj_table[s.justify]);
    pfStringMode(string, PFSTR_CHAR_SIZE, strcs_table[s.char_size]);
    if (s.font != -1)
	pfStringFont(string, glb->rl_list[L_FONT][s.font]);
    pfStringSpacingScale(string, s.spacing_scale[0], s.spacing_scale[1],
			 s.spacing_scale[2]);
    pfStringColor(string, s.color[0], s.color[1], s.color[2], s.color[3]);
    if (s.gstate[0] != -1)
	pfStringGState(string, glb->rl_list[L_GSTATE][s.gstate[0]]);
    pfStringIsectMask(string, s.isect_mask,
		      (PFTRAV_SELF | PFTRAV_IS_CACHE), PF_SET);
    if (size != -1)
	pfStringString(string, str);
    pfStringBBox(string, &s.bbox);

    return(string);
}


static void *pfb_read_string(pfa_global_t *glb)
{
    pfString *string;
    string_t s;
    char *str;
    int size;

    pfb_fread(&s, sizeof(string_t), 1, glb->ifp);
    pfb_fread(&size, sizeof(int), 1, glb->ifp);
    if (size > 0)
    {
	str = pfMalloc(size, glb->arena);
	pfb_fread(str, 1, size, glb->ifp);
    }

    string = pfNewString(glb->arena);
    pfStringMat(string, s.mat);
    pfStringMode(string, PFSTR_JUSTIFY, strj_table[s.justify]);
    pfStringMode(string, PFSTR_CHAR_SIZE, strcs_table[s.char_size]);
    if (s.font != -1)
	pfStringFont(string, glb->rl_list[L_FONT][s.font]);
    pfStringSpacingScale(string, s.spacing_scale[0], s.spacing_scale[1],
			 s.spacing_scale[2]);
    pfStringColor(string, s.color[0], s.color[1], s.color[2], s.color[3]);
    if (s.gstate[0] != -1)
	pfStringGState(string, glb->rl_list[L_GSTATE][s.gstate[0]]);
    pfStringIsectMask(string, s.isect_mask,
		      (PFTRAV_SELF | PFTRAV_IS_CACHE), PF_SET);
    if (size != -1)
	pfStringString(string, str);
    pfStringBBox(string, &s.bbox);

    return(string);
}

static int pfb_read_rep(pfa_global_t *glb,pfRep *rep)
{
  int counter = 0;
  rep_t r;
  
  if( pfb_fread(&r, sizeof(rep_t), 1, glb->ifp) ) {
    counter += sizeof(rep_t) / sizeof(int);

    pfRepOrigin(rep, r.origin);
    pfRepOrient(rep, r.matrix);
  }

  return counter;
}

static void pfb_read_rep_from_buffer(pfa_global_t *glb,pfRep *rep,int *buf_pos)
{
  int i, x, y;
  rep_t r;
  
  for(i=0; i<3; i++)
    r.origin[i] = ((float *)glb->buf)[(*buf_pos)++];
		  
  for(y=0; y<4; y++)
    for(x=0; x<4; x++) 
      r.matrix[y][x] = ((float *)glb->buf)[(*buf_pos)++];
  
  pfRepOrigin(rep, r.origin);
  pfRepOrient(rep, r.matrix);
}


static void* pfa_read_rep(pfa_global_t *glb,pfRep *rep,int *numLinesLeft)
{
  pfVec3 vec;
  pfMatrix m;
  
  READ_LINE(); (*numLinesLeft)--;
  sscanf(glb->line_buf, "%g %g %g",
	 &vec[0], &vec[1], &vec[2]);
   
  pfRepOrigin(rep, vec);

  READ_LINE(); (*numLinesLeft)--;
  sscanf(glb->line_buf, "%g %g %g %g",
	 &m[0][0], &m[0][1], &m[0][2], &m[0][3]);
  READ_LINE(); (*numLinesLeft)--;
  sscanf(glb->line_buf, "%g %g %g %g",
	 &m[1][0], &m[1][1], &m[1][2], &m[1][3]);
  READ_LINE(); (*numLinesLeft)--;
  sscanf(glb->line_buf, "%g %g %g %g",
	 &m[2][0], &m[2][1], &m[2][2], &m[2][3]);
  READ_LINE(); (*numLinesLeft)--;
  sscanf(glb->line_buf, "%g %g %g %g",
	 &m[3][0], &m[3][1], &m[3][2], &m[3][3]);
  
  pfRepOrient(rep, m);

  return NULL;
}

static void add_new_object_to_list(pfObject *obj,int originalId,
				  pfList **list,void *arena)
{
  object_id_map_t *oim = NULL;

  if(*list == NULL)
    *list = pfNewList(sizeof(object_id_map_t *), 0, arena);

  if(!(*list)) {
    pfNotify(PFNFY_WARN,PFNFY_PRINT,
	     "Unable to add object to list of loaded topo/solids");
    pfNotify(PFNFY_WARN,PFNFY_MORE,
	     "may cause problems loading pfParaSurfaces.");
    return;
  }

  oim = (object_id_map_t *)pfMalloc(sizeof(object_id_map_t), arena);
  
  if(!oim) {
    pfNotify(PFNFY_WARN,PFNFY_PRINT,
	     "Unable to alloc memory for topo/solid -> id map element.");
    return;
  }

  oim->object = obj;
  oim->originalId = originalId;

  pfAdd(*list, oim);
}

static void add_new_topo_to_list(pfTopo *topo,int originalId,
				 pfa_global_t *glb)
{
  add_new_object_to_list((pfObject *)topo, originalId, &(glb->topo_map), glb->arena);
}

static void add_new_solid_to_list(pfSolid *solid,int originalId,
				  pfa_global_t *glb)
{
  add_new_object_to_list((pfObject *)solid, originalId, &(glb->solid_map), glb->arena);
}

static void add_boundary_to_list(pfa_global_t *glb,pfBoundary *b)
{
  if(glb->boundaryList == NULL)
    glb->boundaryList = pfNewList(sizeof(pfBoundary *), 0, glb->arena);

  pfAdd(glb->boundaryList, b);
}

static void* pfa_read_topo(pfa_global_t *glb)
{
  int i, j;
  int numBoundaries = 0;
  int wingCount;
  int surfaceId, loopNum, curveNum, originalTopoId;
  int originalSolidId, numSolids;
  pfReal tol;
  pfLengthUnits units;
  pfTopo *topo = NULL; 

  READ_LINE();
  originalTopoId = atoi(glb->line_buf);

  READ_LINE();
  sscanf(glb->line_buf,"%g %d",&tol,&units);

  READ_LINE();
  numBoundaries = atoi(glb->line_buf);

  topo = pfNewTopo(glb->arena);
  pfTopoDistanceTol(topo, tol, units);

  add_new_topo_to_list(topo, originalTopoId, glb);

  /* Don't add boundaries if they only have a single wing.
   * Also when storing edges check for this.
   */
  for(i=0; i<numBoundaries; i++) {
    pfBoundary *b = pfNewBoundary(pfGetSharedArena());

    if(glb->version >= PFBV_SURFACE_CLONING)
      add_boundary_to_list(glb,b);

    READ_LINE();
    wingCount = atoi(glb->line_buf);

    for(j=0; j<wingCount; j++) {
      READ_LINE();
      sscanf(glb->line_buf,"%d %d %d",&surfaceId, &loopNum, &curveNum);
      pfBoundarySurface   (b, j, surfaceId);
      pfBoundaryLoop      (b, j, loopNum);
      pfBoundaryTrimCurve (b, j, curveNum);
    }

    pfTopoAddBoundary(topo, b);
  }

  READ_LINE();
  numSolids = atoi(glb->line_buf);

  if(topo) {
    for(i=0; i<numSolids; i++) {
      pfSolid *solid = pfNewSolid(glb->arena);

      pfTopoAddSolid(topo, solid);
      
      READ_LINE();
      originalSolidId = atoi(glb->line_buf);

      add_new_solid_to_list(solid, originalSolidId, glb);
    }
  }

  return topo;
}

static void* pfb_read_topo(pfa_global_t *glb)
{
  pfTopo *topo = NULL;
  int count, i, listSize = 0, buf_size = 0, *buf = glb->buf;
  pfReal tol;
  pfLengthUnits lengthUnits;
  int j, wingCount, originalTopoId;
  int originalSolidId, numSolids;

  pfb_fread(&listSize, sizeof(int), 1, glb->ifp);
  buf = GET_BUF(listSize);
  pfb_fread(buf, sizeof(int), listSize, glb->ifp);

  originalTopoId = buf[buf_size++];
  tol = ((pfReal *)buf)[buf_size++];
  lengthUnits = buf[buf_size++];

  /* loop over surfaces */
  count = buf[buf_size++];

  topo = pfNewTopo(glb->arena);
  pfTopoDistanceTol(topo, tol, lengthUnits);
  add_new_topo_to_list(topo, originalTopoId, glb);

  for(i=0; i<count; i++) {
    pfBoundary *b = pfNewBoundary(glb->arena);

    if(glb->version >= PFBV_SURFACE_CLONING)
      add_boundary_to_list(glb,b);

    wingCount = buf[buf_size++];
    for(j=0; j<wingCount; j++) {
      pfBoundarySurface   (b, j, buf[buf_size++]);
      pfBoundaryLoop      (b, j, buf[buf_size++]);
      pfBoundaryTrimCurve (b, j, buf[buf_size++]);
    }

    pfTopoAddBoundary(topo, b);
  }

  numSolids = buf[buf_size++];
  if(topo) {
    for(i=0; i<numSolids; i++) {
      pfSolid *solid = pfNewSolid(glb->arena);

      pfTopoAddSolid(topo, solid);
      originalSolidId = buf[buf_size++];
      add_new_solid_to_list(solid, originalSolidId, glb);
    }
  }

  return topo; /* share_obj((pfObject *)topo, glb); */
}

static void* pfb_read_discurve2d(pfa_global_t *glb)
{
  int n, isClosed, spaceReqmnt, *buf = glb->buf, buf_size = 0;
  pfReal *points = NULL;

  pfDisCurve2d *curve = pfNewDisCurve2d();

  pfb_fread(&spaceReqmnt, sizeof(int), 1, glb->ifp);
  buf = GET_BUF(spaceReqmnt);

  pfb_fread(buf, sizeof(int), spaceReqmnt, glb->ifp);

  pfb_read_rep_from_buffer(glb,(pfRep *)curve,&buf_size);

  n = buf[buf_size++];
  isClosed = buf[buf_size++];

  pfDisCurve2dClosed(curve, isClosed);
  
  points = (pfReal *)pfMalloc(sizeof(pfReal) * n * 2, glb->arena);
  if(points) {
    memcpy(points, &buf[buf_size], sizeof(pfReal) * n * 2);
    pfDisCurve2dSet(curve, n, points);
    pfFree(points);
  }

  return curve;
}

static void* pfa_read_discurve2d(pfa_global_t *glb)
{
  int i, n, isClosed;
  pfVec2 v;
  pfDisCurve2d *curve = pfNewDisCurve2d();

  READ_LINE();
  sscanf(glb->line_buf,"%d %d",&n,&isClosed);

  pfDisCurve2dClosed(curve,isClosed);

  for(i=0; i<n; i++) {
    READ_LINE();
    sscanf(glb->line_buf,"%g %g",&(v[0]), &(v[1]));
    pfDisCurve2dPoint(curve, i, v);
  }

  return curve;
}

static int pfb_read_curve2d(pfa_global_t *glb,pfCurve2d *curve)
{
  curve2d_t c;
  int counter = 0;

  counter += pfb_read_rep(glb, (pfRep *)curve);
  if( pfb_fread(&c, sizeof(curve2d_t), 1, glb->ifp) ) {
    counter += sizeof(curve2d_t) / sizeof(int);
    pfCurve2dBeginT(curve,c.beginT);
    pfCurve2dEndT(curve,c.endT);
    pfCurve2dClosed(curve,c.closed);
    pfCurve2dClosedTol(curve,c.closedTol);
  }

  return counter;
}

static int pfb_read_curve3d(pfa_global_t *glb,pfCurve3d *curve,int *buf_pos)
{
  curve3d_t c;
  int counter = 0;

  pfb_read_rep_from_buffer(glb, (pfRep *)curve, buf_pos);

  c.beginT = ((float *)glb->buf)[(*buf_pos)++];
  c.endT = ((float *)glb->buf)[(*buf_pos)++];
  c.closed = glb->buf[(*buf_pos)++];
  c.closedTol = ((float *)glb->buf)[(*buf_pos)++];

  pfCurve3dBeginT(curve,c.beginT);
  pfCurve3dEndT(curve,c.endT);
  pfCurve3dClosed(curve,c.closed);
  pfCurve3dClosedTol(curve,c.closedTol);

  return counter;
}

static void* pfa_read_curve2d(pfa_global_t *glb,pfCurve2d *curve)
{
  int numLinesLeft;
  pfReal beginT, endT, closedTol;
  pfBool isClosed;

  pfa_read_rep(glb, (pfRep *)curve,&numLinesLeft);
  
  READ_LINE();
  sscanf(glb->line_buf,"%g %g %d %g",&beginT,&endT,&isClosed,&closedTol);

  pfCurve2dBeginT(curve,beginT);
  pfCurve2dEndT(curve,endT);
  pfCurve2dClosed(curve,isClosed);
  pfCurve2dClosedTol(curve,closedTol);

  return curve;
}

static void* pfa_read_curve3d(pfa_global_t *glb,pfCurve3d *curve)
{
  int numLinesLeft;
  pfReal beginT, endT, closedTol;
  pfBool isClosed;

  pfa_read_rep(glb, (pfRep *)curve,&numLinesLeft);
  
  READ_LINE();
  sscanf(glb->line_buf,"%g %g %d %g",&beginT,&endT,&isClosed,&closedTol);

  pfCurve3dBeginT(curve,beginT);
  pfCurve3dEndT(curve,endT);
  pfCurve3dClosed(curve,isClosed);
  pfCurve3dClosedTol(curve,closedTol);

  return curve;
}

static void* pfa_read_nurb_curve2d(pfa_global_t *glb,pfNurbCurve2d *nc)
{
  int ii, nn;
  pfVec3 v;
  pfReal beginT, endT;

  pfa_read_curve2d(glb, (pfCurve2d *)nc);

  READ_LINE();
  nn = atoi(glb->line_buf);

  beginT = pfGetCurve2dBeginT((pfCurve2d *)nc);
  endT   = pfGetCurve2dEndT((pfCurve2d *)nc);
  
  for(ii=0; ii<nn; ii++) {
    READ_LINE();
    sscanf(glb->line_buf,"%g %g %g",&(v[0]),&(v[1]),&(v[2]));
    pfNurbCurve2dControlHull((pfNurbCurve2d *)nc, ii, v);
  }
  
  READ_LINE();
  nn = atoi(glb->line_buf);
  for(ii=0; ii<nn; ii++) {
    READ_LINE();
    sscanf(glb->line_buf,"%g",&(v[0]));
    pfNurbCurve2dKnot((pfNurbCurve2d *)nc, ii, v[0]);
  }

  pfCurve2dBeginT((pfCurve2d *)nc,beginT);
  pfCurve2dEndT((pfCurve2d *)nc,endT);

  return nc;
}

static void* pfa_read_nurb_curve3d(pfa_global_t *glb,pfNurbCurve3d *nc)
{
  int ii, nn;
  pfVec4 v;
  pfReal beginT, endT;

  pfa_read_curve3d(glb, (pfCurve3d *)nc);

  beginT = pfGetCurve3dBeginT((pfCurve3d *)nc);
  endT   = pfGetCurve3dEndT  ((pfCurve3d *)nc);

  READ_LINE();
  nn = atoi(glb->line_buf);
  
  for(ii=0; ii<nn; ii++) {
    READ_LINE();
    sscanf(glb->line_buf,"%g %g %g %g",&(v[0]),&(v[1]),&(v[2]),&(v[3]));
    pfNurbCurve3dControlHull((pfNurbCurve3d *)nc, ii, v);
  }
  
  READ_LINE();
  nn = atoi(glb->line_buf);
  for(ii=0; ii<nn; ii++) {
    READ_LINE();
    sscanf(glb->line_buf,"%g",&(v[0]));
    pfNurbCurve3dKnot((pfNurbCurve3d *)nc, ii, v[0]);
  }

  pfCurve3dBeginT((pfCurve3d *)nc, beginT);
  pfCurve3dEndT  ((pfCurve3d *)nc, endT);

  return nc;
}

static int pfb_read_nurb_curve2d(pfa_global_t *glb,pfNurbCurve2d *nc)
{
  int ii, nn, counter = 0;
  pfVec3 *vecs = NULL;
  pfReal *knots = NULL;
  pfReal beginT, endT;

  counter += pfb_read_curve2d(glb, (pfCurve2d *)nc);

  beginT = pfGetCurve2dBeginT((pfCurve2d *)nc);
  endT   = pfGetCurve2dEndT((pfCurve2d *)nc);

  counter += pfb_fread(&nn, sizeof(int), 1, glb->ifp);

  if(nn > 0) {
    vecs = (pfVec3 *)pfMalloc(sizeof(pfVec3) * nn, glb->arena);
    if(vecs) {
      pfb_fread(vecs, sizeof(pfVec3), nn, glb->ifp);
      counter +=  nn * (sizeof(pfVec3) / sizeof(int));
      for(ii=0; ii<nn; ii++)
	pfNurbCurve2dControlHull((pfNurbCurve2d *)nc, ii, vecs[ii]);
      pfFree(vecs);
    }
  }
  
  counter += pfb_fread(&nn, sizeof(int), 1, glb->ifp);
  if(nn > 0) {
    knots = (pfReal *)pfMalloc(sizeof(pfReal) * nn, glb->arena);
    if(knots) {
      pfb_fread(knots, sizeof(pfReal), nn, glb->ifp);	
      counter += nn;
      for(ii=0; ii<nn; ii++)
	pfNurbCurve2dKnot((pfNurbCurve2d *)nc, ii, knots[ii]);
      pfFree(knots);
    }
  }

  pfCurve2dBeginT((pfCurve2d *)nc, beginT);
  pfCurve2dEndT((pfCurve2d *)nc, endT);

  return counter;
}


static int pfb_read_piecewisepolycurve2d(pfa_global_t *glb,
					 pfPieceWisePolyCurve2d *c)
{
  int patchCount, i, reverse, j, counter = 0, *orders = NULL, maxOrder;
  pfVec2 *vecs = NULL;

  counter += pfb_read_curve2d(glb, (pfCurve2d *)c);

  counter += pfb_fread(&patchCount, sizeof(int), 1, glb->ifp);
  counter += pfb_fread(&reverse, sizeof(int), 1, glb->ifp);

  pfPieceWisePolyCurve2dReverse(c, reverse);

  if(patchCount < 1) {
    return counter; /* display error message */
  } /* else : */

  orders = (int *)pfMalloc(sizeof(int) * patchCount, glb->arena);
  if(!orders) {
    return counter; /* display error message */
  }

  pfb_fread(orders, sizeof(int), patchCount, glb->ifp);
  counter += patchCount;

  maxOrder = orders[0];
  for(i=1; i<patchCount; i++)
    if(orders[i] > maxOrder)
      maxOrder = orders[i];
  
  if(maxOrder < 0) {
    if(orders)
      pfFree(orders);
    return counter; /* display error message */
  } /* else : */

  /* the maxOrder + 1 is because we want space to also read the
   * limit paras at the same time
   */
  vecs = (pfVec2 *)pfMalloc(sizeof(pfVec2) * (maxOrder + 1), glb->arena);
  if(!vecs) {
    if(orders)
      pfFree(orders);
    return counter; /* display error message */
  }

  for(i=0; i<patchCount; i++) {
    pfb_fread(vecs, sizeof(pfVec2), orders[i] + 1, glb->ifp);
    counter += (orders[i] + 1) * sizeof(pfVec2) / sizeof(int);
    pfPieceWisePolyCurve2dLimitParas(c,i, vecs[0][0], vecs[0][1]);
    for(j=0; j<orders[i]; j++)
      pfPieceWisePolyCurve2dControlHull(c,i,j, vecs[j+1]);
  }

  if(orders)
    pfFree(orders);
  if(vecs)
    pfFree(vecs);

  return counter;
}

static void* pfa_read_piecewisepolycurve2d(pfa_global_t *glb,
					   pfPieceWisePolyCurve2d *c)
{
  int patchCount, i, reverse, j, order;
  pfVec2 vec;

  pfa_read_curve2d(glb, (pfCurve2d *)c);

  READ_LINE();
  patchCount = atoi(glb->line_buf);
  READ_LINE();
  reverse = atoi(glb->line_buf);

  pfPieceWisePolyCurve2dReverse(c, reverse);

  for(i=0; i<patchCount; i++) {
    READ_LINE();
    sscanf(glb->line_buf,"%g %g",&(vec[0]), &(vec[1]));
    pfPieceWisePolyCurve2dLimitParas(c,i, vec[0], vec[1]);
    READ_LINE();
    order = atoi(glb->line_buf);
    for(j=0; j<order; j++) {
      READ_LINE();
      sscanf(glb->line_buf,"%g %g",&(vec[0]), &(vec[1]));
      pfPieceWisePolyCurve2dControlHull(c,i,j, vec);
    }
  }

  return c;
}


static void* pfa_read_curve2d_general(pfa_global_t *glb)
{
  pfObject *obj = NULL;
  int curveType;

  READ_LINE();

  curveType = atoi(glb->line_buf);
  switch(curveType) {
  case N_NURB_CURVE2D:
    if(obj == NULL)
      obj = (pfObject *)pfNewNurbCurve2d();
    pfa_read_nurb_curve2d(glb, (pfNurbCurve2d *)obj);
    break;
  case N_PIECEWISEPOLYCURVE2D:
    if(obj == NULL)
      obj = (pfObject *)pfNewPieceWisePolyCurve2d();
    pfa_read_piecewisepolycurve2d(glb, (pfPieceWisePolyCurve2d *)obj);
    break;
  case N_LINE2D:
    if(obj == NULL)
      obj = (pfObject *)pfNewLine2d();
    pfa_read_line2d(glb, (pfLine2d *)obj);
    break;
  case N_CIRCLE2D:
    if(obj == NULL)
      obj = (pfObject *)pfNewCircle2d();
    pfa_read_circle2d(glb, (pfCircle2d *)obj);
    break;
  case N_SUPERQUADCURVE2D:
    if(obj == NULL)
      obj = (pfObject *)pfNewSuperQuadCurve2d();
    pfa_read_super_quad_curve2d(glb, (pfSuperQuadCurve2d *)obj);
    break;
  }

  return obj; /* XXX Alex share_obj(obj, glb); */
}


static void* pfb_read_curve2d_general(pfa_global_t *glb)
{
  pfObject *obj = NULL;
  int curveType, spaceReqmnt;
  int counter = 0;

  pfb_fread(&spaceReqmnt, sizeof(int), 1, glb->ifp);

  glb->buf = GET_BUF(spaceReqmnt);
  pfb_fread(&curveType, sizeof(int), 1, glb->ifp);

  switch(curveType) {
  case N_NURB_CURVE2D:
    if(obj == NULL)
      obj = (pfObject *)pfNewNurbCurve2d();
    counter += pfb_read_nurb_curve2d(glb, (pfNurbCurve2d *)obj);
    break;
  case N_PIECEWISEPOLYCURVE2D:
    if(obj == NULL)
      obj = (pfObject *)pfNewPieceWisePolyCurve2d();
    counter += pfb_read_piecewisepolycurve2d(glb, (pfPieceWisePolyCurve2d *)obj);
    break;
  case N_LINE2D:
    if(obj == NULL)
      obj = (pfObject *)pfNewLine2d();
    counter += pfb_read_line2d(glb, (pfLine2d *)obj);
    break;
  case N_CIRCLE2D:
    if(obj == NULL)
      obj = (pfObject *)pfNewCircle2d();
    counter += pfb_read_circle2d(glb, (pfCircle2d *)obj);
    break;
  case N_SUPERQUADCURVE2D:
    if(obj == NULL)
      obj = (pfObject *)pfNewSuperQuadCurve2d();
    counter += pfb_read_super_quad_curve2d(glb, (pfSuperQuadCurve2d *)obj);
    break;
  default:
    pfNotify(PFNFY_WARN,PFNFY_PRINT,"Don't know how to read curve "
	     " of type %d requiring %d bytes. Skipping ...",
	     curveType, spaceReqmnt);
    pfb_fread(glb->buf, spaceReqmnt-1, 1, glb->ifp); /* skip this curve */
    counter = spaceReqmnt - 1;
    break;
  }

  PFASSERTDEBUG((counter + 1) == spaceReqmnt);

  return obj; /* share_obj(obj, glb); */
}

static void* pfa_read_composite_scalar(pfa_global_t *glb,pfCompositeScalar *scalar)
{
  pfScalar *outF = NULL, *inF = NULL;
  int outF_id = -1, inF_id = -1;

  READ_LINE();

  sscanf(glb->line_buf, "%d %d",&outF_id, &inF_id);

  if(outF_id >= 0) outF = glb->rl_list[L_SCALAR][outF_id];
  if(inF_id >= 0)  inF  = glb->rl_list[L_SCALAR][inF_id];

  if(outF) pfCompositeScalarOutF(scalar, outF);
  if(inF)  pfCompositeScalarInF (scalar, inF);

  return scalar; /* share_obj(scalar, glb); */
}

static void pfb_read_composite_scalar(pfa_global_t *glb,pfCompositeScalar *scalar,int *buf_pos)
{
  pfScalar *outF = NULL, *inF = NULL;
  int outF_id = -1, inF_id = -1;

  outF_id = glb->buf[(*buf_pos)++];
  inF_id  = glb->buf[(*buf_pos)++];
  
  if(outF_id >= 0) outF = glb->rl_list[L_SCALAR][outF_id];
  if(inF_id  >= 0)  inF = glb->rl_list[L_SCALAR][inF_id];

  if(outF) pfCompositeScalarOutF(scalar, outF);
  if(inF)  pfCompositeScalarInF (scalar, inF);
}

static void* pfa_read_poly_scalar(pfa_global_t *glb,pfPolyScalar *scalar)
{
  int degree, i;
  pfReal *coeffs = NULL;

  READ_LINE();
  degree = atoi(glb->line_buf);

  if(degree >= 0) 
    coeffs = (pfReal *)pfMalloc(sizeof(pfReal) * (degree+1), glb->arena);
  if(coeffs == NULL) {
    pfNotify(PFNFY_WARN,PFNFY_PRINT,"Coeffs for poly scalar NULL. (degree = %d)",degree);
    return NULL;
  }
  
  for(i=0; i<degree+1; i++) {
      READ_LINE();
      coeffs[i] = atof(glb->line_buf);
  }

  pfPolyScalarSet(scalar, degree, coeffs);
  pfFree(coeffs);

  return scalar; /* share_obj(obj, glb); */
}

static void pfb_read_poly_scalar(pfa_global_t *glb,pfPolyScalar *scalar,
				 int *buf_pos)
{
  int degree = glb->buf[(*buf_pos)++];
  pfPolyScalarSet(scalar, degree, &(((float *)glb->buf)[*buf_pos]));
  *buf_pos += degree+1;
}

static void* pfb_read_scalar(pfa_global_t *glb)
{
  pfObject *obj = NULL;
  int scalarType, spaceReqmnt;
  int buf_pos = 0;

  pfb_fread(&spaceReqmnt, sizeof(int), 1, glb->ifp);

  glb->buf = GET_BUF(spaceReqmnt-1);

  pfb_fread(glb->buf, sizeof(int), spaceReqmnt-1, glb->ifp);

  scalarType = glb->buf[buf_pos++];

  switch(scalarType) {
  case S_COMPOSITE:
    if(obj == NULL)
      obj = (pfObject *)pfNewCompositeScalar(glb->arena);
    pfb_read_composite_scalar(glb, (pfCompositeScalar *)obj, &buf_pos);
    break;

    /* 
     * There is no read function for pfSinScalar and pfCosScalar.
     * The type alone defines this entity sufficiently.
     */
  case S_COS:
    if(obj == NULL)
      obj = (pfObject *)pfNewCosScalar(glb->arena);
    break;

  case S_SIN:
    if(obj == NULL)
      obj = (pfObject *)pfNewSinScalar(glb->arena);
    break;

  case S_POLY:
    if(obj == NULL)
      obj = (pfObject *)pfNewPolyScalar(glb->arena);
    pfb_read_poly_scalar(glb, (pfPolyScalar *)obj, &buf_pos);
    break;

  default:
    pfNotify(PFNFY_WARN,PFNFY_PRINT,"Don't know how to read scalar "
	     " of type %d requiring %d bytes. Skipping ...",
	     scalarType, spaceReqmnt);
    /*skip this scalar */
    break;
  }

  return obj; /* share_obj(obj, glb); */
}

static void* pfa_read_scalar(pfa_global_t *glb)
{
  pfObject *obj = NULL;
  int scalarType, nLines, i;

  READ_LINE();
  sscanf(glb->line_buf,"%d %d",&scalarType, &nLines);

  switch(scalarType) {
  case S_COMPOSITE:
    if(obj == NULL)
      obj = (pfObject *)pfNewCompositeScalar(glb->arena);
    pfa_read_composite_scalar(glb, (pfCompositeScalar *)obj);
    break;

    /* 
     *  There is no read function for pfSinScalar and pfCosScalar.
     * The type alone defines this entity sufficiently.
     */
  case S_COS:
    if(obj == NULL)
      obj = (pfObject *)pfNewCosScalar(glb->arena);
    break;

  case S_SIN:
    if(obj == NULL)
      obj = (pfObject *)pfNewSinScalar(glb->arena);
    break;

  case S_POLY:
    if(obj == NULL)
      obj = (pfObject *)pfNewPolyScalar(glb->arena);
    pfa_read_poly_scalar(glb, (pfPolyScalar *)obj);
    break;

  default:
    pfNotify(PFNFY_WARN,PFNFY_PRINT,"Don't know how to read scalar "
	     " of type %d. Skipping %d lines...",
	     scalarType, nLines);
    for(i=0; i<nLines; i++) 
      READ_LINE();
    break;
  }

  return obj; /* share_obj(obj, glb); */
}

pfObject *find_map_object(int originalId,pfList *theList)
{
  object_id_map_t *oim = NULL;
  int i, n = theList ? pfGetNum(theList) : 0;

  for(i=0; i<n; i++) {
    oim = (object_id_map_t *)pfGet(theList, i);
    if(oim && oim->originalId == originalId)
      return oim->object;
  }

  /* if we get here it's because we didn't find what we were looking for */
  return NULL;
}

static pfTopo *find_topo_object(int originalId,pfa_global_t *glb)
{
  return (pfTopo *)find_map_object(originalId, glb->topo_map);
}

static pfSolid *find_solid_object(int originalId,pfa_global_t *glb)
{
  return (pfSolid *)find_map_object(originalId, glb->solid_map);
}

static void addEdge(edge_t* e,pfParaSurface *surf,int i,int j,
		    int topoId,int oldSurfaceId,int newSurfaceId,
		    pfa_global_t *glb)
{
  pfEdge *edge = NULL;
  pfCurve2d *contCurve = NULL;
  pfDisCurve2d *disCurve = NULL;

  if(e->contCurve >= 0) 
    contCurve = glb->rl_list[L_CURVE2D][e->contCurve];
  if(e->disCurve >= 0)
    disCurve = glb->rl_list[L_DISCURVE2D][e->disCurve];
  
  pfParaSurfaceAddTrimCurve(surf, i, contCurve, disCurve);
  
  edge = pfGetParaSurfaceTrimCurve(surf, i, j);

  /* 
   * If the old (original) surface id isn't the same
   * as the new surface id then we need to go and ammend
   * all the boundaries associated with it. We can do them
   * one at a time here.
   *
   */
  if(topoId > 0 && oldSurfaceId != newSurfaceId) {
    int k, wingCount;
    pfTopo *t = pfGetTopoGlobalTopo(topoId);
    pfBoundary *b = pfGetTopoBoundary(t, pfGetEdgeBoundary(edge));

    wingCount = b ? pfGetBoundaryWingCount(b) : 0;
    for(k=0; k<wingCount; k++) {
      int surfId = pfGetBoundarySurface(b, k);
      int loopNo = pfGetBoundaryLoop(b, k);
      int curveNo = pfGetBoundaryTrimCurve(b, k);
      
      if(surfId == oldSurfaceId && loopNo == i && curveNo == j) {
	pfBoundarySurface(b, k, newSurfaceId);
	break;
      }
    }
  }
  
  if(e->boundary > -1) {
    if(glb->version >= PFBV_SURFACE_CLONING)
      pfEdgeBoundary(edge, pfGetBoundaryBoundaryId(pfGet(glb->boundaryList, e->boundary)));
    else
      pfEdgeBoundary(edge, e->boundary);
  }
  pfEdgeBoundaryDir(edge, e->boundaryDir);
}

static void pfb_read_parasurface(pfa_global_t *glb,pfParaSurface *surf,
				 int *buf_pos)
{
  int i, j, newSurfaceId, *buf = glb->buf;
  surface_t header;

  /* first read pfRep information */
  pfb_read_rep_from_buffer(glb, (pfRep *)surf,buf_pos);

  (glb->surface_count)++;

  memcpy(&header, &(buf[*buf_pos]), sizeof(surface_t));
  *buf_pos += sizeof(surface_t)/sizeof(int);

  if(header.topoId > 0) {
    pfTopo *t = find_topo_object(header.topoId, glb);
    if(t)
      pfTopoAddSurface(t, surf);
  }

  newSurfaceId = pfGetParaSurfaceSurfaceId(surf);

  if(header.solidId != -1) {
    pfSolid *solid = find_solid_object(header.solidId, glb);
    if(solid)
      pfSolidAddSurface(solid, surf);
  }

  pfParaSurfaceBeginU(surf, header.beginU);
  pfParaSurfaceEndU(surf, header.endU);
  pfParaSurfaceBeginV(surf, header.beginV);
  pfParaSurfaceEndV(surf, header.endV);

  pfParaSurfaceHandednessHint(surf, header.handednessHint);

  if(header.geoStateIndex != -1)
    pfParaSurfaceGState(surf, glb->rl_list[L_GSTATE][header.geoStateIndex]);

  for(i=0; i<header.trimLoopCount; i++) {
    int numTrimCurves;

    numTrimCurves = buf[(*buf_pos)++];

    for(j=0; j<numTrimCurves; j++) {
      edge_t e;

      e.contCurve   = buf[(*buf_pos)++];
      e.disCurve    = buf[(*buf_pos)++];
      e.boundary    = buf[(*buf_pos)++];
      e.boundaryDir = buf[(*buf_pos)++];

      addEdge(&e,surf,i,j,header.topoId,header.surfaceId,newSurfaceId,glb);
    }
  }

  /* store closedness info of loops */
  for(i=0; i<header.trimLoopCount; i++)
    pfParaSurfaceTrimLoopClosed(surf, i, buf[(*buf_pos)++]);
}

static void* pfa_read_parasurface(pfa_global_t *glb,pfParaSurface *surf)
{
  int trimLoopCount, i, j, geoStateIndex;
  int topoId, solidId, surfaceId, handednessHint, closedFlag;
  float beginU, endU, beginV, endV;
  int numLinesLeft;
  int newSurfaceId;

  /* first read pfRep information */
  pfa_read_rep(glb, (pfRep *)surf,&numLinesLeft);

  (glb->surface_count)++;

  READ_LINE();
  sscanf(glb->line_buf, "%d %d %d",&topoId, &solidId, &surfaceId);
  READ_LINE();
  sscanf(glb->line_buf, "%g %g %g %g",&beginU, &endU, &beginV, &endV);
  READ_LINE();
  sscanf(glb->line_buf, "%d", &handednessHint);
  READ_LINE();
  trimLoopCount = atoi(glb->line_buf);

  if(topoId > 0) {
    pfTopo *t = find_topo_object(topoId, glb);
    if(t)
      pfTopoAddSurface(t, surf);
  }

  newSurfaceId = pfGetParaSurfaceSurfaceId(surf);

  if(solidId != -1) {
    pfSolid *solid = find_solid_object(solidId, glb);
    if(solid)
      pfSolidAddSurface(solid, surf);
  }

  pfParaSurfaceBeginU(surf, beginU);
  pfParaSurfaceEndU(surf, endU);
  pfParaSurfaceBeginV(surf, beginV);
  pfParaSurfaceEndV(surf, endV);

  pfParaSurfaceHandednessHint(surf, handednessHint);

  READ_LINE();
  geoStateIndex = atoi(glb->line_buf);
  if(geoStateIndex != -1)
    pfParaSurfaceGState(surf, glb->rl_list[L_GSTATE][geoStateIndex]);

  for(i=0; i<trimLoopCount; i++) {
    int numTrimCurves;

    READ_LINE();
    numTrimCurves = atoi(glb->line_buf);
    for(j=0; j<numTrimCurves; j++) {
      edge_t e;

      READ_LINE();

      sscanf(glb->line_buf,"%d %d %d %d",
	     &e.contCurve, &e.disCurve, &e.boundary, &e.boundaryDir);

      if(glb->version >= PFBV_SURFACE_CLONING && e.boundary != -1)
      {
	pfBoundary *listBoundary = (pfBoundary *)pfGet(glb->boundaryList,e.boundary);
	
	if(listBoundary != NULL)
	  e.boundary = pfGetBoundaryBoundaryId(listBoundary);
	else
	  e.boundary = -1; /* shouldn't happen ... */
      }

      addEdge(&e,surf,i,j,topoId,surfaceId,newSurfaceId,glb);
    }
  }

  /* store closedness info of loops */
  for(i=0; i<trimLoopCount; i++) {
    READ_LINE();
    closedFlag = atoi(glb->line_buf);
    pfParaSurfaceTrimLoopClosed(surf, i, closedFlag);
  }     

  return NULL;
}

static void *pfa_read_node(int node_num, pfa_global_t *glb)
{
    int i, count, lines;
    int t1, t2, t3;
    float f1;
    pfVec3 v1;
    void *node;
    int node_type, custom_id;
    custom_node_t *custom;
    char *s;

    READ_LINE();
    node_type = atoi(glb->line_buf);
    node = NULL;
    if (node_type & N_CUSTOM)
    {
	custom_id = (node_type & N_CUSTOM_MASK) >> N_CUSTOM_SHIFT;
	if (custom = glb->rl_list[L_CUSTOM][custom_id])
	{
	    if (custom->new_func == NULL)
	    {
		pfNotify(PFNFY_WARN, PFNFY_PRINT,
			 "%s Custom node type \"%s\" has no new func.",
			 "pfdLoadFile_pfa: ", custom->name);
		custom = NULL;
	    }
	    else if (custom->load_func == NULL)
	    {
		pfNotify(PFNFY_WARN, PFNFY_PRINT,
			 "%s Custom node type \"%s\" has no load func.",
			 "pfdLoadFile_pfa: ", custom->name);
		custom = NULL;
	    }
	    else
		node = custom->new_func();
	}
    }
    else
	custom_id = -1;
    node_type &= N_NOT_CUSTOM_MASK;

    switch (node_type)
    {
	case N_LIGHTPOINT:
	    /* Obsolete */
	    break;
	case N_TEXT:
	    if (node == NULL)
		node = pfNewText();
	    READ_LINE();
	    count = atoi(glb->line_buf);
	    for (i = 0; i < count; i++)
	    {
		READ_LINE();
		pfAddString((pfText *)node,
			    glb->rl_list[L_STRING][atoi(glb->line_buf)]);
	    }
	    break;
	case N_GEODE:
	    if (node == NULL)
		node = pfNewGeode();
	    READ_LINE();
	    count = atoi(glb->line_buf);
	    break;
        case N_SUBDIV_SURFACE:
	    if (node == NULL)
		node = pfNewSubdivSurface(glb->arena);
	    
	    READ_LINE();    /* number of lines to read */
	    lines = atoi(glb->line_buf);

	    pfa_read_rep(glb, (pfRep *)node, &lines);

	    /* flags */
	    READ_LINE(); lines--;
	    pfSubdivSurfaceFlags((pfSubdivSurface *)node, 0xffffffff, 0); /* set all to 0 */
	    pfSubdivSurfaceFlags((pfSubdivSurface *)node, 
				 atoi(glb->line_buf), 1);

	    READ_LINE(); lines--;
	    pfSubdivSurfaceVal((pfSubdivSurface *)node, 
			       PFSB_SUBDIVISION_METHOD, atoi(glb->line_buf));
	    READ_LINE(); lines--;
	    pfSubdivSurfaceVal((pfSubdivSurface *)node, 
			       PFSB_SUBDIVISION_LEVEL, atoi(glb->line_buf));

	    /* mesh */
	    READ_LINE(); lines--;
	    pfSubdivSurfaceMesh((pfSubdivSurface *)node, 
				glb->rl_list[L_MESH][atoi(glb->line_buf)]);

	    READ_LINE(); lines--;
	    count = atoi(glb->line_buf);
	    
	    while(lines > 0)
	    {
		READ_LINE(); lines--;
	    }
	    break;
        case N_TORUS:
	    if(node == NULL)
	      node = pfNewTorusSurface();

	    pfa_read_parasurface(glb,(pfParaSurface *)node);
	    
	    READ_LINE();
	    {
	      pfReal minorRadius, majorRadius;
	      sscanf(glb->line_buf,"%g %g",&minorRadius, &majorRadius);
	      pfTorusSurfaceMinorRadius((pfTorusSurface *)node,minorRadius);
	      pfTorusSurfaceMajorRadius((pfTorusSurface *)node,majorRadius);
	    }
	      
	    READ_LINE(); 
	    count = atoi(glb->line_buf);
	    break;

        case N_SPHERE_SURF:
            if(node == NULL)
	      node = pfNewSphereSurface();

	    pfa_read_parasurface(glb,(pfParaSurface *)node);

	    READ_LINE();
	    {
	      pfReal radius;
	      sscanf(glb->line_buf,"%g",&radius);
	      pfSphereSurfaceRadius((pfSphereSurface *)node, radius);
	    }

	    READ_LINE();
	    count = atoi(glb->line_buf);
	    break;

        case N_CONE:
            if(node == NULL)
	      node = pfNewConeSurface();

	    pfa_read_parasurface(glb,(pfParaSurface *)node);

	    READ_LINE();
	    {
	      pfReal radius, height;
	      sscanf(glb->line_buf,"%g %g",&radius,&height);
	      pfConeSurfaceRadius((pfConeSurface *)node, radius);
	      pfConeSurfaceHeight((pfConeSurface *)node, height);
	    }

	    READ_LINE();
	    count = atoi(glb->line_buf);
	    break;

        case N_CYLINDER:
            if(node == NULL)
	      node = pfNewCylinderSurface();

	    pfa_read_parasurface(glb,(pfParaSurface *)node);

	    READ_LINE();
	    {
	      pfReal radius, height;
	      sscanf(glb->line_buf,"%g %g",&radius,&height);
	      pfCylinderSurfaceRadius((pfCylinderSurface *)node, radius);
	      pfCylinderSurfaceHeight((pfCylinderSurface *)node, height);
	    }

	    READ_LINE();
	    count = atoi(glb->line_buf);
	    break;

        case N_PLANE_SURF:
	    if(node == NULL)
	      node = pfNewPlaneSurface();

	    pfa_read_parasurface(glb,(pfParaSurface *)node);
	    
	    {
	      pfReal vals[5];

	      READ_LINE();
	      sscanf(glb->line_buf,"%g %g %g %g %g",&(vals[0]),&(vals[1]),
		     &(vals[2]),&(vals[3]),&(vals[4]));
	      pfPlaneSurfacePoint1((pfPlaneSurface *)node,
				vals[0],vals[1],vals[2],vals[3],vals[4]);


	      READ_LINE();
	      sscanf(glb->line_buf,"%g %g %g %g",&(vals[0]),&(vals[1]),
		     &(vals[2]),&(vals[3]));
	      pfPlaneSurfacePoint2((pfPlaneSurface *)node,
				vals[0],vals[1],vals[2],vals[3]);

	      READ_LINE();
	      sscanf(glb->line_buf,"%g %g %g %g",&(vals[0]),&(vals[1]),
		     &(vals[2]),&(vals[3]));
	      pfPlaneSurfacePoint3((pfPlaneSurface *)node,
				vals[0],vals[1],vals[2],vals[3]);
	    }

	    READ_LINE();
	    count = atoi(glb->line_buf);
	    break;

        case N_NURB_CURVE2D:
	    /* read nurb curve 2d */
	    if(node == NULL)
	      node = pfNewNurbCurve2d();

	    pfa_read_nurb_curve2d(glb, (pfNurbCurve2d *)node);

	    READ_LINE(); 
	    count = atoi(glb->line_buf);
	    break;

        case N_NURB_SURFACE:
	    if(node == NULL)
	      node = pfNewNurbSurface();

	    pfa_read_parasurface(glb,(pfParaSurface *)node);

	    {
	      int uKnotCount, vKnotCount;
	      int uSize, vSize, ii, jj;
	      float knotValue;
	      pfVec4 pt;
	      pfNurbSurface *nurbs = (pfNurbSurface *)node;

	      READ_LINE();
	      sscanf(glb->line_buf,"%d %d %d %d",
		     &uKnotCount,&vKnotCount,&uSize,&vSize);

	      /* read ctrl pts */
	      for(ii=0; ii<vSize; ii++) {
		for(jj=0; jj<uSize; jj++) {
		  READ_LINE();
      
		  sscanf(glb->line_buf,"%g %g %g %g\n",
			 &(pt[0]), &(pt[1]), &(pt[2]), &(pt[3]));

		  pfNurbSurfaceControlHull4(nurbs,jj,ii,pt);
		}
	      }

	      /* read u knot vector */
	      for(ii=0; ii < uKnotCount; ii++) {
		READ_LINE();
		sscanf(glb->line_buf,"%g",&knotValue);
		pfNurbSurfaceUknot(nurbs,ii,knotValue);
	      }
	      
	      /* read v knot vector */
	      for(ii=0; ii < vKnotCount; ii++) {
		READ_LINE();
		sscanf(glb->line_buf,"%g",&knotValue);
		pfNurbSurfaceVknot(nurbs,ii,knotValue);
	      }
	    }
	   
	    READ_LINE(); 
	    count = atoi(glb->line_buf);
	    break;

        case N_HSPLINE_SURFACE:
	    if(node == NULL)
	      node = pfNewHsplineSurface();

	    pfa_read_parasurface(glb,(pfParaSurface *)node);

	    {
	      int uKnotCount, vKnotCount;
	      pfBool cylindrical;
	      pfHsplineSurface *hSurf = (pfHsplineSurface *)node;
	      pfReal *values, *p;
	      int size, stride;

	      READ_LINE();
	      sscanf(glb->line_buf,"%d %d %d",
		     &uKnotCount, &vKnotCount, &cylindrical);

	      pfHsplineSurfaceCylindrical(hSurf, cylindrical);

	      stride = uKnotCount * vKnotCount * 3;
	      size = (4*stride + uKnotCount + vKnotCount) * sizeof(pfReal);

	      values = (pfReal *)malloc(size);
	      p = values;

	      if(p) { /* else we should display an error ... */
		int i, n = uKnotCount * vKnotCount * 4;
		for(i = 0; i < n; i++) { /* read p, tu, tv, tuv values */
		  READ_LINE();
		  sscanf(glb->line_buf,"%g %g %g",p,p+1,p+2);
		  p+=3;
		}
		/* read u and v knot values */
		for(i = 0; i < (uKnotCount + vKnotCount); i++) {
		  READ_LINE();
		  sscanf(glb->line_buf,"%g",p++);
		}

		{
		  pfReal *tu, *tv, *tuv, *uu, *vv;

		  tu  = values + stride;
		  tv  = tu + stride;
		  tuv = tv + stride;
		  uu  = tuv + stride;
		  vv  = uu + uKnotCount;
 
		  pfHsplineSurfaceAll(hSurf, values, tu, tv, tuv, uu, vv,
				      uKnotCount, vKnotCount);
		}

		free(values);
	      }
	    }

	    READ_LINE(); 
	    count = atoi(glb->line_buf);
	    break;
	   
        case N_PIECEWISEPOLYSURFACE:
	    if(node == NULL)
	      node = pfNewPieceWisePolySurface();

	    pfa_read_parasurface(glb,(pfParaSurface *)node);

	    {
	      int uPatchCount, vPatchCount, u, v, uOrder, vOrder, uu, vv;
	      pfVec3 cHull;
	      pfPieceWisePolySurface *ppSurf = (pfPieceWisePolySurface *)node;

	      READ_LINE();
	      sscanf(glb->line_buf,"%d %d", &uPatchCount, &vPatchCount);

	      for(v=0; v<vPatchCount; v++) {
		for(u=0; u<uPatchCount; u++) {
		  READ_LINE();
		  sscanf(glb->line_buf,"%d %d", &uOrder, &vOrder);
		  for(vv=0; vv<vOrder; vv++) {
		    for(uu=0; uu<uOrder; uu++) {
		      READ_LINE();
		      sscanf(glb->line_buf,"%g %g %g",&(cHull[0]),
			     &(cHull[1]), &(cHull[2]));
		      pfPieceWisePolySurfaceControlHull(ppSurf,u,v,uu,vv,
							&cHull);
		    }
		  }
		}
	      }
	    }
	    READ_LINE(); 
	    count = atoi(glb->line_buf);
	    break;

        case N_FRENET_SWEPT_SURFACE:
	  if(node == NULL)
	    node = pfNewFrenetSweptSurface();

	  pfa_read_parasurface(glb,(pfParaSurface *)node);

	  {
	    int crossId, pathId, scalarId;
	    pfCurve3d *crossCurve = NULL, *pathCurve = NULL;
	    pfScalar *scalar = NULL;
	    pfFrenetSweptSurface *frenet = (pfFrenetSweptSurface *)node;

	    READ_LINE();
	    sscanf(glb->line_buf,"%d %d %d",&crossId, &pathId, &scalarId);
	    
	    if(crossId  > -1) crossCurve = glb->rl_list[L_NODE][crossId];
	    if(pathId   > -1) pathCurve  = glb->rl_list[L_NODE][pathId];
	    if(scalarId > -1) scalar     = glb->rl_list[L_SCALAR][scalarId];
	    
	    pfFrenetSweptSurfaceSet(frenet, crossCurve, pathCurve, scalar);
	  }
	  READ_LINE();
	  count = atoi(glb->line_buf);
	  break;

        case N_SWEPT_SURFACE:
	  if(node == NULL)
	    node = pfNewSweptSurface();

	  pfa_read_parasurface(glb,(pfParaSurface *)node);

	  {
	    int crossId, pathId, tId, bId, scalarId;
	    pfCurve3d *crossCurve = NULL, *pathCurve = NULL, *tCurve = NULL, *bCurve = NULL;
	    pfScalar *scalar = NULL;
	    pfSweptSurface *swept = (pfSweptSurface *)node;
	    
	    sscanf(glb->line_buf,"%d %d %d %d %d",&crossId, &pathId, &tId, &bId, &scalarId);
	    
	    if(crossId  > -1) crossCurve = glb->rl_list[L_NODE][crossId];
	    if(pathId   > -1) pathCurve  = glb->rl_list[L_NODE][pathId];
	    if(tId      > -1) tCurve     = glb->rl_list[L_NODE][tId];
	    if(bId      > -1) bCurve     = glb->rl_list[L_NODE][bId];
	    if(scalarId > -1) scalar     = glb->rl_list[L_SCALAR][scalarId];
	    
	    if(crossCurve) pfSweptSurfaceCrossSection(swept, crossCurve);
	    if(pathCurve)  pfSweptSurfacePath(swept, pathCurve);
	    if(tCurve)     pfSweptSurfaceT(swept, tCurve);
	    if(bCurve)     pfSweptSurfaceB(swept, bCurve);
	    if(scalar)     pfSweptSurfaceProf(swept, scalar);
	  }
	  READ_LINE();
	  count = atoi(glb->line_buf);
	  break;

        case N_COONS:
	  if(node == NULL)
	    node = pfNewCoonsSurface();

	  pfa_read_parasurface(glb,(pfParaSurface *)node);

	  {
	    int rId, lId, bId, tId;
	    pfCurve3d *rCurve = NULL, *lCurve = NULL, *bCurve = NULL, *tCurve = NULL;
	    pfCoonsSurface *coons = (pfCoonsSurface *)node;

	    READ_LINE();
	    sscanf(glb->line_buf,"%d %d %d %d",&rId, &lId, &bId, &tId);
	    
	    if(rId > -1) rCurve = glb->rl_list[L_NODE][rId];
	    if(lId > -1) lCurve = glb->rl_list[L_NODE][lId];
	    if(bId > -1) bCurve = glb->rl_list[L_NODE][bId];
	    if(tId > -1) tCurve = glb->rl_list[L_NODE][tId];
	    
	    if(rCurve) pfCoonsSurfaceRight(coons, rCurve);
	    if(lCurve) pfCoonsSurfaceLeft(coons, lCurve);
	    if(bCurve) pfCoonsSurfaceBottom(coons, bCurve);
	    if(tCurve) pfCoonsSurfaceTop(coons, tCurve);
	  }
	  READ_LINE();
	  count = atoi(glb->line_buf);
	  break;

        case N_RULED:
	  if(node == NULL)
	    node = pfNewRuledSurface();
	  pfa_read_parasurface(glb,(pfParaSurface *)node);
	  {
	    int curve1_id, curve2_id;
	    pfCurve3d *c1 = NULL, *c2 = NULL;

	    READ_LINE();
	    sscanf(glb->line_buf,"%d %d",&curve1_id, &curve2_id);
	    
	    if(curve1_id > -1) c1 = glb->rl_list[L_NODE][curve1_id];
	    if(curve2_id > -1) c2 = glb->rl_list[L_NODE][curve2_id];

	    if(c1) pfRuledSurfaceCurve1((pfRuledSurface *)node, c1);
	    if(c2) pfRuledSurfaceCurve2((pfRuledSurface *)node, c2);
	  }

	  READ_LINE();
	  count = atoi(glb->line_buf);
	  break;

        case N_LINE3D:
        case N_ORIENTEDLINE3D:
	  if(node == NULL)
	    if(node_type == N_LINE3D)
	      node = pfNewLine3d();
	    else
	      node = pfNewOrientedLine3d();

	  pfa_read_curve3d(glb,(pfCurve3d *)node);
	  {
	    pfReal x, y, z, t;

	    READ_LINE();
	    sscanf(glb->line_buf,"%g %g %g %g",&x,&y,&z,&t);
	    pfLine3dPoint1((pfLine3d *)node, x, y, z, t);

	    READ_LINE();
	    sscanf(glb->line_buf,"%g %g %g %g",&x,&y,&z,&t);
	    pfLine3dPoint2((pfLine3d *)node, x, y, z, t);

	    if(node_type == N_ORIENTEDLINE3D) {
	      READ_LINE();
	      sscanf(glb->line_buf,"%g %g %g",&x,&y,&z);
	      pfOrientedLine3dUpPoint((pfOrientedLine3d *)node,x,y,z);
	    }

	  }
	  READ_LINE();
	  count = atoi(glb->line_buf);
	  break;

        case N_CIRCLE3D:
	  if(node == NULL)
	    node = pfNewCircle3d();

	  pfa_read_curve3d(glb,(pfCurve3d *)node);
	  {
	    pfReal radius;

	    READ_LINE();
	    sscanf(glb->line_buf,"%g",&radius);
	    pfCircle3dRadius((pfCircle3d *)node, radius);
	  }
	  READ_LINE();
	  count = atoi(glb->line_buf);
	  break;

        case N_SUPERQUADCURVE3D:
	  if(node == NULL)
	    node = pfNewSuperQuadCurve3d();
	  pfa_read_curve3d(glb,(pfCurve3d *)node);
	  {
	    pfReal radius, exponent;
	    READ_LINE();
	    sscanf(glb->line_buf,"%g %g",&radius,&exponent);
	    pfSuperQuadCurve3dRadius((pfSuperQuadCurve3d *)node, radius);
	    pfSuperQuadCurve3dExponent((pfSuperQuadCurve3d *)node, exponent);
	  }
	  READ_LINE();
	  count = atoi(glb->line_buf);
	  break;

        case N_COMPOSITE_CURVE3D:
	  if(node == NULL)
	    node = pfNewCompCurve3d();
	  pfa_read_curve3d(glb,(pfCurve3d *)node);
	  {
	    int surfId = -1, curveId = -1;
	    pfParaSurface *surface = NULL;
	    pfCurve2d     *curve   = NULL;

	    READ_LINE();
	    sscanf(glb->line_buf,"%d %d",&surfId, &curveId);

	    if(surfId  > -1) surface = glb->rl_list[L_NODE][surfId];
	    if(curveId > -1) curve   = glb->rl_list[L_CURVE2D][curveId]; /* XXX Alex -- look in L_NODE instead?*/

	    pfCompCurve3dSet((pfCompositeCurve3d *)node, surface, curve);
	  }
	  READ_LINE();
	  count = atoi(glb->line_buf);
	  break;

        case N_NURB_CURVE3D:
	  if(node == NULL)
	    node = pfNewNurbCurve3d();

	  pfa_read_nurb_curve3d(glb, (pfNurbCurve3d *)node);

	  READ_LINE();
	  count = atoi(glb->line_buf);
	  break;


	case N_BILLBOARD:
        case N_IBR_NODE:
	    if (node == NULL)
		if (node_type == N_IBR_NODE)
		    node = pfNewIBRnode();
		else
		    node = pfNewBboard();
	    READ_LINE();
	    count = atoi(glb->line_buf);
	    if (count == BB_POS_FLUX)
	    {
		READ_LINE();
		pfBboardPosFlux((pfBillboard *)node,
			(pfFlux*)glb->rl_list[L_FLUX][atoi(glb->line_buf)]);
	    }
	    else
	    {
		for (i = 0; i < count; i++)
		{
		    READ_LINE();
		    sscanf(glb->line_buf, "%g %g %g", &v1[0], &v1[1], &v1[2]);
		    pfBboardPos((pfBillboard *)node, i, v1);
		}
	    }
	    READ_LINE();
	    pfBboardMode((pfBillboard *)node, PFBB_ROT,
			 bbr_table[atoi(glb->line_buf)]);
	    READ_LINE();
	    sscanf(glb->line_buf, "%g %g %g", &v1[0], &v1[1], &v1[2]);
	    pfBboardAxis((pfBillboard *)node, v1);

	    if (node_type == N_IBR_NODE)
	    {
		float a1, a2;
		int n;
		pfIBRtexture *IBRtex;

		READ_LINE();
		IBRtex = glb->rl_list[L_IBR_TEX][atoi(glb->line_buf)];
		pfIBRnodeIBRtexture((pfIBRnode *)node, IBRtex);

		READ_LINE();
		n = atoi(glb->line_buf);

		for(i=0; i<n; i++)
		{
		    READ_LINE();
		    sscanf(glb->line_buf, "%g %g", &a1, &a2);
		    pfIBRnodeAngles((pfIBRnode *)node, i, a1, a2);
		}


		if (glb->version >= PFBV_VERSION2_6)
		{ 
		  READ_LINE();
		  sscanf(glb->line_buf, "%d", &n);
		  pfIBRnodeFlags((pfIBRnode *)node, 0xffffffff, 0); /* set all to 0 */
		  pfIBRnodeFlags((pfIBRnode *)node, n, 1); /* set those that are set in 'n' */

		  if(pfGetIBRtextureFlags(IBRtex, PFIBR_USE_PROXY) ||
		     (n & PFIBRN_TEXCOORDS_DEFINED))
		  {
		    int       count, numGroups, viewsPerGroup;
		    int       j, t, v;
		    pfVec2    ***texCoords;

		    /* printf("reading proxy tex coords\n"); */

		    /* read the proxy texture coordinates */

		    /* read the number of geosets and the number of textures
		     */
		    READ_LINE();
		    sscanf(glb->line_buf, "%d %d %d", &count, &numGroups,
			   &viewsPerGroup);

		    texCoords = (pfVec2 ***)pfMalloc(sizeof(pfVec2**) * 
						     numGroups,
						     pfGetSharedArena());

		    /* for each view or group of views */
		    for(t=0; t<numGroups; t++) 
		    {
		      texCoords[t] = 
			  (pfVec2 **)pfMalloc(sizeof(pfVec2*) *
					      count * viewsPerGroup,
					      pfGetSharedArena());

		      /* for each geoset */
		      for(i=0; i<count; i++)
		      {

			/* read the number of vertices in each geoset */
			/* determine the number of vertices */
			READ_LINE();
			n = atoi(glb->line_buf);

			for(j=0; j<viewsPerGroup; j++) 
			{
			    if(n == 0)
			    {
				texCoords[t][i*viewsPerGroup + j] = 0;
				continue;
			    }

			    texCoords[t][i*viewsPerGroup + j] = 
				(pfVec2 *)pfMalloc(sizeof(pfVec2) * n,
						   pfGetSharedArena());

			    for(v=0; v<n; v++)
			    {
			        READ_LINE();
			        sscanf(glb->line_buf, "%g %g", 
				       &texCoords[t][i*viewsPerGroup + j][v][0], 
				       &texCoords[t][i*viewsPerGroup + j][v][1]);	
			    }
			}
		      }
		    }
		    
		    /* set proxy coordinates */
		    pfIBRnodeProxyTexCoords((pfIBRnode *)node, texCoords);
		  }
		}
	    }
	    break;
	case N_LIGHTSOURCE:
	    READ_LINE();
	    node = glb->rl_list[L_LSOURCE][atoi(glb->line_buf)];
	    break;
	case N_GROUP:
	    if (node == NULL)
		node = pfNewGroup();
	    break;
	case N_SCS:
	case N_DCS:
	    {
		pfMatrix m;
		uint m_type;

		if (node_type == N_DCS)
		{
		    READ_LINE();
		    m_type = (uint)strtoul(glb->line_buf, NULL, 16);
		    if (m_type == CS_UNCONSTRAINED)
			m_type = PFDCS_UNCONSTRAINED;
		    else
			m_type = from_table(mat_table, m_type);
		}

		READ_LINE();
		sscanf(glb->line_buf, "%g %g %g %g",
		       &m[0][0], &m[0][1], &m[0][2], &m[0][3]);
		READ_LINE();
		sscanf(glb->line_buf, "%g %g %g %g",
		       &m[1][0], &m[1][1], &m[1][2], &m[1][3]);
		READ_LINE();
		sscanf(glb->line_buf, "%g %g %g %g",
		       &m[2][0], &m[2][1], &m[2][2], &m[2][3]);
		READ_LINE();
		sscanf(glb->line_buf, "%g %g %g %g",
		       &m[3][0], &m[3][1], &m[3][2], &m[3][3]);

		if (node_type == N_SCS)
		    node = pfNewSCS(m);
		else
		{
		    if (node == NULL)
			node = pfNewDCS();
		    pfDCSMat((pfDCS *)node, m);
		    pfDCSMatType((pfDCS *)node, m_type);
		}
	    }
	    break;
	case N_FCS:
	    {
		int flux_id;
		uint m_type;

		READ_LINE();
		flux_id = atoi(glb->line_buf);
		READ_LINE();
		m_type = (uint)strtoul(glb->line_buf, NULL, 16);
		if (m_type == CS_UNCONSTRAINED)
		    m_type = PFFCS_UNCONSTRAINED;
		else
		    m_type = from_table(mat_table, m_type);

		if (node == NULL)
		    node = pfNewFCS(glb->rl_list[L_FLUX][flux_id]);
		pfFCSMatType((pfFCS *)node, m_type);
	    }
	    break;
	case N_PARTITION:
	    if (node == NULL)
		node = pfNewPart();
	    READ_LINE();
	    if (strncmp(glb->line_buf, "NULL", 4))
	    {
		sscanf(glb->line_buf, "%g %g %g", &v1[0], &v1[1], &v1[2]);
		pfPartAttr((pfPartition *)node, PFPART_MIN_SPACING, v1);
	    }
	    READ_LINE();
	    if (strncmp(glb->line_buf, "NULL", 4))
	    {
		sscanf(glb->line_buf, "%g %g %g", &v1[0], &v1[1], &v1[2]);
		pfPartAttr((pfPartition *)node, PFPART_MAX_SPACING, v1);
	    }
	    READ_LINE();
	    if (strncmp(glb->line_buf, "NULL", 4))
	    {
		sscanf(glb->line_buf, "%g %g %g", &v1[0], &v1[1], &v1[2]);
		pfPartAttr((pfPartition *)node, PFPART_ORIGIN, v1);
	    }
	    READ_LINE();
	    pfPartVal((pfPartition *)node, PFPART_FINENESS,
		      atof(glb->line_buf));
	    break;
	case N_SCENE:
	    if (node == NULL)
		node = pfNewScene();
	    READ_LINE();
	    sscanf(glb->line_buf, "%d %d", &t1, &t2);
	    if (t1 != -1)
		pfSceneGState((pfScene *)node, glb->rl_list[L_GSTATE][t1]);
	    if (t2 != -1)
		pfSceneGStateIndex((pfScene *)node, t2);
	    break;
	case N_SWITCH:
	    if (node == NULL)
		node = pfNewSwitch();
	    READ_LINE();
	    t1 = atoi(glb->line_buf);
	    if (t1 == PFB_SWITCH_OFF)
		pfSwitchVal((pfSwitch*)node, PFSWITCH_OFF);
	    else if (t1 == PFB_SWITCH_ON)
		pfSwitchVal((pfSwitch*)node, PFSWITCH_ON);
	    else
		pfSwitchVal((pfSwitch*)node, t1);
	    if (glb->version >= PFBV_SWITCH_VAL_FLUX)
	    {
		READ_LINE();
		t1 = atoi(glb->line_buf);
		if (t1 != -1)
		    pfSwitchValFlux((pfSwitch*)node, glb->rl_list[L_FLUX][t1]);
	    }
	    break;
	case N_LOD:
	    if (node == NULL)
		node = pfNewLOD();
	    READ_LINE();
	    count = (int)strtol(glb->line_buf, &s, 10);
	    for (i = 0; i <= count; i++)
		pfLODRange((pfLOD *)node, i, strtod(s, &s));
	    READ_LINE();
	    s = glb->line_buf;
	    for (i = 0; i <= count; i++)
		pfLODTransition((pfLOD *)node, i, strtod(s, &s));
	    READ_LINE();
	    sscanf(glb->line_buf, "%g %g %g", &v1[0], &v1[1], &v1[2]);
	    pfLODCenter((pfLOD *)node, v1);
	    READ_LINE();
	    sscanf(glb->line_buf, "%d %d", &t1, &t2);
	    if (t1 != -1)
		pfLODLODState((pfLOD *)node, glb->rl_list[L_LODSTATE][t1]);
	    if (t2 != -1)
		pfLODLODStateIndex((pfLOD *)node, t2);
	    break;
	case N_SEQUENCE:
	    if (node == NULL)
		node = pfNewSeq();
	    READ_LINE();
	    count = (int)strtol(glb->line_buf, &s, 10);
	    for (i = 0; i < count; i++)
		pfSeqTime((pfSequence *)node, i, strtod(s, &s));
	    READ_LINE();
	    sscanf(glb->line_buf, "%d %d %d", &t1, &t2, &t3);
	    pfSeqInterval((pfSequence *)node, sqi_table[t1], t2, t3);
	    READ_LINE();
	    sscanf(glb->line_buf, "%g %d", &f1, &t1);
	    pfSeqDuration((pfSequence *)node, f1, t1);
	    READ_LINE();
	    pfSeqMode((pfSequence *)node, sqm_table[atoi(glb->line_buf)]);
	    break;
	case N_LAYER:
	    if (node == NULL)
		node = pfNewLayer();
	    READ_LINE();
	    sscanf(glb->line_buf, "%d %d %d", &t1, &t2, &t3);
	    pfLayerMode((pfLayer *)node,
			layer_table[t1] |
			(t2 << PFDECAL_LAYER_SHIFT) |
			(t3 ? PFDECAL_LAYER_OFFSET : 0));
	    break;
	case N_MORPH:
	    READ_LINE();
	    node = glb->rl_list[L_MORPH][atoi(glb->line_buf)];
	    break;
	case N_ASD:
	    {
		int bind, size, which, attr_type, attr;

		if (node == NULL)
		    node = pfNewASD();

		READ_LINE();
		sscanf(glb->line_buf, "%d %d %d", &bind, &size, &attr);
		if (attr != -1)
		    pfASDAttr((pfASD *)node, PFASD_LODS, bind, size,
			      glb->rl_list[L_ASDDATA][attr]);

		READ_LINE();
		sscanf(glb->line_buf, "%d %d %d", &bind, &size, &attr);
		if (attr != -1)
		    pfASDAttr((pfASD *)node, PFASD_COORDS, bind, size,
			      glb->rl_list[L_ASDDATA][attr]);

		READ_LINE();
		sscanf(glb->line_buf, "%d %d %d", &bind, &size, &attr);
		if (attr != -1)
		    pfASDAttr((pfASD *)node, PFASD_FACES, bind, size,
			      glb->rl_list[L_ASDDATA][attr]);

		READ_LINE();
		sscanf(glb->line_buf, "%d %d %d", &attr_type, &size, &attr);
		if (attr != -1)
		{
		    which = 0;
		    if (attr_type & ASD_ATTR_NORMALS)
			which |= PFASD_NORMALS;
		    if (attr_type & ASD_ATTR_COLORS)
			which |= PFASD_COLORS;
		    if (attr_type & ASD_ATTR_TCOORDS)
			which |= PFASD_TCOORDS;
		    pfASDAttr((pfASD *)node, PFASD_OVERALL_ATTR, which, size,
			      glb->rl_list[L_ASDDATA][attr]);
		}

		READ_LINE();
		sscanf(glb->line_buf, "%d %d %d", &attr_type, &size, &attr);
		if (attr != -1)
		{
		    which = 0;
		    if (attr_type & ASD_ATTR_NORMALS)
			which |= PFASD_NORMALS;
		    if (attr_type & ASD_ATTR_COLORS)
			which |= PFASD_COLORS;
		    if (attr_type & ASD_ATTR_TCOORDS)
			which |= PFASD_TCOORDS;
		    pfASDAttr((pfASD *)node, PFASD_PER_VERTEX_ATTR, which, size,
			      glb->rl_list[L_ASDDATA][attr]);
		}

		READ_LINE();
		pfASDNumBaseFaces((pfASD *)node, atoi(glb->line_buf));
		READ_LINE();
		t1 = atoi(glb->line_buf);
		if (t1 != -1)
		{
		    t2 = 0;
		    if (t1 & ASD_ATTR_NORMALS)
			t2 |= PFASD_NORMALS;
		    if (t1 & ASD_ATTR_COLORS)
			t2 |= PFASD_COLORS;
		    if (t1 & ASD_ATTR_TCOORDS)
			t2 |= PFASD_TCOORDS;
		}
		pfASDMorphAttrs((pfASD *)node, t2);
		if (glb->version >= PFBV_ASD_GSTATES)
		{
		    READ_LINE();
		    count = atoi(glb->line_buf);
		    if (count > 0)
		    {
			pfGeoState **gsa = (pfGeoState**)
					    malloc(sizeof(pfGeoState*) * count);
			for (i = 0; i < count; i++)
			{
			    READ_LINE();
			    if ((t1 = atoi(glb->line_buf)) != -1)
				gsa[i] = glb->rl_list[L_GSTATE][t1];
			    else
				gsa[i] = NULL;
			}
			pfASDGStates((pfASD*)node, gsa, count);
			free(gsa);
		    }
		}
		else
		{
		    READ_LINE();
		    if ((t1 = atoi(glb->line_buf)) != -1)
		    {
			pfGeoState **gsa = (pfGeoState**)
					    malloc(sizeof(pfGeoState*));
			*gsa = glb->rl_list[L_GSTATE][t1];
			pfASDGStates((pfASD*)node, gsa, 1);
			free(gsa);
		    }
		}
		READ_LINE();
		pfASDMaxMorphDepth((pfASD *)node, atoi(glb->line_buf), 0);
		READ_LINE();
		pfASDEvalMethod((pfASD *)node,
				asd_em_table[atoi(glb->line_buf)]);
		READ_LINE();
		if ((t1 = atoi(glb->line_buf)) != -1)
		    pfASDEvalFunc((pfASD *)node, glb->rl_list[L_UFUNC][t1]);
#if 0
		if (glb->version >= PFBV_ASD_SYNC_GROUP)
		{
		    READ_LINE();
		    t1 = atoi(glb->line_buf);
		    pfASDSyncGroup((pfASD *)node,
				   *(uint*)&glb->rl_list[L_SG_NAME][t1]);
		}
#endif
		{
            	    char *e = getenv("_PFASD_CLIPRINGS");
            	    int _PFASD_CLIPRINGS = (e ? *e ? atoi(e) : -1 : 0);
pfNotify(PFNFY_INTERNAL_DEBUG, PFNFY_PRINT, "_PFASD_CLIPRINGS %d", _PFASD_CLIPRINGS);
            	    if(_PFASD_CLIPRINGS)
                	pfdASDClipring(node, _PFASD_CLIPRINGS);
        	}
		pfASDConfig((pfASD *)node);
		pfDelete(glb->rl_list[L_ASDDATA]);
	    }
	    break;
    }

    if (pfIsOfType(node, pfGetGroupClassType()))
    {
	READ_LINE();
	count = (int)strtol(glb->line_buf, &s, 10);
	glb->children[node_num] = (int *)malloc((count+1) * sizeof(int));
	glb->children[node_num][0] = count;
	for (i = 0; i < count; i++)
	{
	    glb->children[node_num][i+1] = (int)strtol(s, &s, 10);
	    if (i % 12 == 11 && i != count - 1)
	    {
		READ_LINE();
		s = glb->line_buf;
	    }
	}
    }
    else if (pfIsOfType(node, pfGetGeodeClassType()))
    {
	READ_LINE();
	s = glb->line_buf;
	for (i = 0; i < count; i++)
	{
	    long t;
	    t = strtol(s, &s, 10);

	    if(glb->version >= PFBV_GEOARRAY){	/* Check for geoArray index bit hack*/
	        pfAddGSet((pfGeode *)node,
		          (t & PFB_GARRAY_IXBIT ?
			   glb->rl_list[L_GARRAY][t ^ PFB_GARRAY_IXBIT]:
			   glb->rl_list[L_GSET][t]));
	    } else {
	        pfAddGSet((pfGeode *)node, glb->rl_list[L_GSET][t]);
	    }
		    
	    if (i % 12 == 11 && i != count - 1)
	    {
		READ_LINE();
		s = glb->line_buf;
	    }
	}
    }

    READ_LINE();
    pfNodeTravMask(node, PFTRAV_ISECT, (uint)strtoul(glb->line_buf, &s, 16),
		   PFTRAV_SELF, PF_SET);
    pfNodeTravMask(node, PFTRAV_APP, (uint)strtoul(s, &s, 16),
		   PFTRAV_SELF, PF_SET);
    pfNodeTravMask(node, PFTRAV_CULL, (uint)strtoul(s, &s, 16),
		   PFTRAV_SELF, PF_SET);
    pfNodeTravMask(node, PFTRAV_DRAW, (uint)strtoul(s, &s, 16),
		   PFTRAV_SELF, PF_SET);

    if (glb->version >= PFBV_UFUNC)
    {
	READ_LINE();
	if (strtol(glb->line_buf, &s, 10) == 1)
	{
	    /*
	     *  has trav funcs and/or trav data
	     */
	    void *pre, *post;

	    if ((t1 = (int)strtol(s, &s, 10)) != -1)
		pre = glb->rl_list[L_UFUNC][t1];
	    else
		pre = NULL;
	    if ((t1 = (int)strtol(s, &s, 10)) != -1)
		post = glb->rl_list[L_UFUNC][t1];
	    else
		post = NULL;
	    if (pre || post)
		pfNodeTravFuncs(node, PFTRAV_APP, pre, post);
	    if ((t1 = (int)strtol(s, &s, 10)) != -1)
		set_trav_data(node, PFTRAV_APP, t1, glb);

	    if ((t1 = (int)strtol(s, &s, 10)) != -1)
		pre = glb->rl_list[L_UFUNC][t1];
	    else
		pre = NULL;
	    if ((t1 = (int)strtol(s, &s, 10)) != -1)
		post = glb->rl_list[L_UFUNC][t1];
	    else
		post = NULL;
	    if (pre || post)
		pfNodeTravFuncs(node, PFTRAV_CULL, pre, post);
	    if ((t1 = (int)strtol(s, &s, 10)) != -1)
		set_trav_data(node, PFTRAV_CULL, t1, glb);

	    if ((t1 = (int)strtol(s, &s, 10)) != -1)
		pre = glb->rl_list[L_UFUNC][t1];
	    else
		pre = NULL;
	    if ((t1 = (int)strtol(s, &s, 10)) != -1)
		post = glb->rl_list[L_UFUNC][t1];
	    else
		post = NULL;
	    if (pre || post)
		pfNodeTravFuncs(node, PFTRAV_DRAW, pre, post);
	    if ((t1 = (int)strtol(s, &s, 10)) != -1)
		set_trav_data(node, PFTRAV_DRAW, t1, glb);

	    if ((t1 = (int)strtol(s, &s, 10)) != -1)
		pre = glb->rl_list[L_UFUNC][t1];
	    else
		pre = NULL;
	    if ((t1 = (int)strtol(s, &s, 10)) != -1)
		post = glb->rl_list[L_UFUNC][t1];
	    else
		post = NULL;
	    if (pre || post)
		pfNodeTravFuncs(node, PFTRAV_ISECT, pre, post);
	    if ((t1 = (int)strtol(s, &s, 10)) != -1)
		set_trav_data(node, PFTRAV_ISECT, t1, glb);
	}
    }

    READ_LINE();
    if ((i = atoi(glb->line_buf)) != -1)
    {
	/*
	 * i is an index into the list of udata lists.
	 * look up the appropriate udata list
	 * and assign it to this node.
	 */
	set_udata((pfObject *)node, i, glb);
    }

    if (glb->version >= PFBV_NODE_BSPHERE)
    {
	pfSphere sphere;

	READ_LINE();
	sscanf(glb->line_buf, "%d %g %g %g %g",
	       &t1, &sphere.center[0], &sphere.center[1], &sphere.center[2],
	       &sphere.radius);
	if ((t1 = bound_table[t1]) != PFBOUND_DYNAMIC)
	    pfNodeBSphere(node, &sphere, t1);
    }

    READ_LINE();
    if (strncmp(glb->line_buf, "NULL", 4))
    {
	if (glb->line_buf[0] == '"')
	{
	    for (i = 1; glb->line_buf[i] != '"' && glb->line_buf[i] != '\0';
		 i++);
	    glb->line_buf[i] = '\0';
	    pfNodeName(node, &glb->line_buf[1]);
	}
	else
	    READ_ERROR();
    }

    if (custom_id != -1)
    {
	int size;

	READ_LINE();
	size = atoi(glb->line_buf);

	if (custom)
	{
	    int start;

	    start = (int)ftell(glb->ifp);

	    glb->data_total = 0;
	    custom->load_func(node, glb);

	    if (glb->data_total != size)
	    {
		/*
		 *  The wrong amount of data was loaded by the users function.
		 */
		pfNotify(PFNFY_WARN, PFNFY_PRINT,
			 "%s  Custom node load function failed.\n",
			 "pfdLoadFile_pfa:");
		pfNotify(PFNFY_WARN, PFNFY_PRINT,
			 "%s  Loaded %d bytes of data.\n",
			 "pfdLoadFile_pfa:", glb->data_total);
		pfNotify(PFNFY_WARN, PFNFY_PRINT,
			 "%s  Should have loaded %d bytes of data.\n",
			 "pfdLoadFile_pfa:", size);
		fseek(glb->ifp, start + size, SEEK_SET);
	    }
	}
	else
	{
	    for (i = 0; i < size; i += 39)
	    {
		READ_LINE();
	    }
	    pfNotify(PFNFY_WARN, PFNFY_PRINT,
		     "%s Unable to load custom node.", "pfdLoadFile_pfa: ");
	}
    }

    return(node);
}


static void *pfb_read_node(int node_num, pfa_global_t *glb)
{
    int i, count, valuesLeft;
    int t1, t2, t3;
    float f1;
    pfVec3 v1;
    void *node;
    int node_type, custom_id;
    custom_node_t *custom;
    int buf_size, buf_pos;
    int *buf;

    pfb_fread(&buf_size, sizeof(int), 1, glb->ifp);
    buf = GET_BUF(buf_size);
    pfb_fread(buf, sizeof(int), buf_size, glb->ifp);

    buf_pos = 0;
    node_type = buf[buf_pos++];
    node = NULL;
    if (node_type & N_CUSTOM)
    {
	custom_id = (node_type & N_CUSTOM_MASK) >> N_CUSTOM_SHIFT;
	if (custom = glb->rl_list[L_CUSTOM][custom_id])
	{
	    if (custom->new_func == NULL)
	    {
		pfNotify(PFNFY_WARN, PFNFY_PRINT,
			 "%s Custom node type \"%s\" has no new func.",
			 "pfdLoadFile_pfb: ", custom->name);
		custom = NULL;
	    }
	    else if (custom->load_func == NULL)
	    {
		pfNotify(PFNFY_WARN, PFNFY_PRINT,
			 "%s Custom node type \"%s\" has no load func.",
			 "pfdLoadFile_pfb: ", custom->name);
		custom = NULL;
	    }
	    else
		node = custom->new_func();
	}
    }
    else
	custom_id = -1;
    node_type &= N_NOT_CUSTOM_MASK;

    switch (node_type)
    {
	case N_LIGHTPOINT:
	    /* Obsolete */
	    break;
	case N_TEXT:
	    if (node == NULL)
		node = pfNewText();
	    count = buf[buf_pos++];
	    for (i = 0; i < count; i++)
	    {
		pfAddString((pfText *)node,
			    glb->rl_list[L_STRING][buf[buf_pos++]]);
	    }
	    break;
	case N_GEODE:
	    if (node == NULL)
		node = pfNewGeode();
	    count = buf[buf_pos++];
	    break;
        case N_SUBDIV_SURFACE:
	    if (node == NULL)
		node = pfNewSubdivSurface(glb->arena);
	    
	    valuesLeft = buf[buf_pos++];

	    /* read pfRep stuff */
	    {
		pfVec3 vec;
		pfMatrix m;
		int     x,y;

		for(i=0; i<3; i++)
		    vec[i] = ((float *)buf)[buf_pos++];
   
		pfRepOrigin((pfRep *)node, vec);

		for(y=0; y<4; y++)
		    for(x=0; x<4; x++)
			m[y][x] = ((float *)buf)[buf_pos++];
		
		pfRepOrient((pfRep *)node, m);
		valuesLeft -= 3 + 16;
	    }

	    /* flags */
	    pfSubdivSurfaceFlags((pfSubdivSurface *)node, 0xffffffff, 0); /* set all to 0 */
	    pfSubdivSurfaceFlags((pfSubdivSurface *)node, buf[buf_pos++], 1);
	    valuesLeft--;

	    pfSubdivSurfaceVal((pfSubdivSurface *)node, 
			       PFSB_SUBDIVISION_METHOD, buf[buf_pos++]);
	    valuesLeft--;

	    pfSubdivSurfaceVal((pfSubdivSurface *)node, 
			       PFSB_SUBDIVISION_LEVEL, buf[buf_pos++]);
	    valuesLeft--;

	    /* mesh */
	    pfSubdivSurfaceMesh((pfSubdivSurface *)node, 
				glb->rl_list[L_MESH][buf[buf_pos++]]);
	    valuesLeft--;

	    count = buf[buf_pos++]; 
	    valuesLeft--;
	    
	    buf_pos += valuesLeft;
	    break;

        case N_TORUS:
	    if(node == NULL)
	      node = pfNewTorusSurface();

	    buf_pos++; /* ZZZ Alex -- BAD BAD BAD .. don't understand
			  why we have to skip one value here ... */

	    pfb_read_parasurface(glb,(pfParaSurface *)node,&buf_pos);

	    pfTorusSurfaceMinorRadius((pfTorusSurface *)node,((float *)buf)[buf_pos++]);
	    pfTorusSurfaceMajorRadius((pfTorusSurface *)node,((float *)buf)[buf_pos++]);
	      
	    count = buf[buf_pos++];
	    break;

        case N_SPHERE_SURF:
	    if(node == NULL)
	      node = pfNewSphereSurface();

	    buf_pos++; /* ZZZ Alex -- BAD BAD BAD .. don't understand
			  why we have to skip one value here ... */

	    pfb_read_parasurface(glb,(pfParaSurface *)node,&buf_pos);

	    pfSphereSurfaceRadius((pfSphereSurface *)node, ((float *)buf)[buf_pos++]);
	      
	    count = buf[buf_pos++];
	    break;

        case N_CONE:
	    if(node == NULL)
	      node = pfNewConeSurface();

	    buf_pos++; /* ZZZ Alex -- BAD BAD BAD .. don't understand
			  why we have to skip one value here ... */

	    pfb_read_parasurface(glb,(pfParaSurface *)node,&buf_pos);

	    pfConeSurfaceRadius((pfConeSurface *)node, ((float *)buf)[buf_pos++]);
	    pfConeSurfaceHeight((pfConeSurface *)node, ((float *)buf)[buf_pos++]);
	      
	    count = buf[buf_pos++];
	    break;

        case N_CYLINDER:
	    if(node == NULL)
	      node = pfNewCylinderSurface();

	    buf_pos++; /* ZZZ Alex -- BAD BAD BAD .. don't understand
			  why we have to skip one value here ... */

	    pfb_read_parasurface(glb,(pfParaSurface *)node,&buf_pos);

	    pfCylinderSurfaceRadius((pfCylinderSurface *)node, 
				    ((float *)buf)[buf_pos++]);
	    pfCylinderSurfaceHeight((pfCylinderSurface *)node, 
				    ((float *)buf)[buf_pos++]);
	      
	    count = buf[buf_pos++];
	    break;

        case N_NURB_CURVE2D:
	    /* read nurb curve 2d */
	    if(node == NULL)
	      node = pfNewNurbCurve2d();

	    buf_pos++; /* ZZZ Alex -- BAD BAD BAD .. don't understand
			  why we have to skip one value here ... */

	    buf_pos += pfb_read_nurb_curve2d(glb, (pfNurbCurve2d *)node);

	    count = buf[buf_pos++];
	    break;

        case N_NURB_SURFACE:
	    if(node == NULL)
	      node = pfNewNurbSurface();

	    buf_pos++; /* ZZZ Alex -- BAD BAD BAD .. don't understand
			  why we have to skip one value here ... */

	    pfb_read_parasurface(glb,(pfParaSurface *)node, &buf_pos);

	    {
	      int uKnotCount, vKnotCount;
	      int uSize, vSize, ii, jj;
	      pfVec4 pt;
	      pfNurbSurface *nurbs = (pfNurbSurface *)node;

	      uKnotCount = buf[buf_pos++];
	      vKnotCount = buf[buf_pos++];
	      uSize = buf[buf_pos++];
	      vSize = buf[buf_pos++];

	      /* read ctrl pts */
	      for(ii=0; ii<vSize; ii++) {
		for(jj=0; jj<uSize; jj++) {
		  pt[0] = ((float *)buf)[buf_pos++];
		  pt[1] = ((float *)buf)[buf_pos++];
		  pt[2] = ((float *)buf)[buf_pos++];
		  pt[3] = ((float *)buf)[buf_pos++];

		  pfNurbSurfaceControlHull4(nurbs,jj,ii,pt);
		}
	      }

	      /* read u knot vector */
	      for(ii=0; ii < uKnotCount; ii++)
		pfNurbSurfaceUknot(nurbs,ii,((float *)buf)[buf_pos++]);
	      
	      /* read v knot vector */
	      for(ii=0; ii < vKnotCount; ii++)
		pfNurbSurfaceVknot(nurbs,ii,((float *)buf)[buf_pos++]);
	    }
	    
	    count = buf[buf_pos++];
	    break;

        case N_HSPLINE_SURFACE:
	    if(node == NULL)
	      node = pfNewHsplineSurface();

	    buf_pos++; /* ZZZ Alex -- BAD BAD BAD .. don't understand
			  why we have to skip one value here ... */

	    pfb_read_parasurface(glb,(pfParaSurface *)node, &buf_pos);

	    {
	      int uKnotCount, vKnotCount;
	      pfBool cylindrical;
	      pfHsplineSurface *hSurf = (pfHsplineSurface *)node;
	      pfReal *p, *tu, *tv, *tuv, *uu, *vv;
	      int stride;

	      uKnotCount = buf[buf_pos++];
	      vKnotCount = buf[buf_pos++];
	      cylindrical = buf[buf_pos++];

	      pfHsplineSurfaceCylindrical(hSurf, cylindrical);

	      stride = uKnotCount * vKnotCount * 3;

	      p   = &((float *)buf)[buf_pos];
	      tu  = p + stride;
	      tv  = tu + stride;
	      tuv = tv + stride;
	      uu  = tuv + stride;
	      vv  = uu + uKnotCount;
	      
	      buf_pos += 4 * stride + uKnotCount + vKnotCount;
	      pfHsplineSurfaceAll(hSurf, p, tu, tv, tuv, uu, vv,
				  uKnotCount, vKnotCount);
	    }

	    count = buf[buf_pos++];

	    break;

        case N_PLANE_SURF:
	    if(node == NULL)
	      node = pfNewPlaneSurface();

	    buf_pos++; /* ZZZ Alex -- BAD BAD BAD .. don't understand
			  why we have to skip one value here ... */

	    pfb_read_parasurface(glb,(pfParaSurface *)node, &buf_pos);

	    {
	      pfReal vals[5];

	      vals[0] = ((float *)buf)[buf_pos++];
	      vals[1] = ((float *)buf)[buf_pos++];
	      vals[2] = ((float *)buf)[buf_pos++];
	      vals[3] = ((float *)buf)[buf_pos++];
	      vals[4] = ((float *)buf)[buf_pos++];

	      pfPlaneSurfacePoint1((pfPlaneSurface *)node,
				vals[0],vals[1],vals[2],vals[3],vals[4]);

	      vals[0] = ((float *)buf)[buf_pos++];
	      vals[1] = ((float *)buf)[buf_pos++];
	      vals[2] = ((float *)buf)[buf_pos++];
	      vals[3] = ((float *)buf)[buf_pos++];

	      pfPlaneSurfacePoint2((pfPlaneSurface *)node,
				vals[0],vals[1],vals[2],vals[3]);

	      vals[0] = ((float *)buf)[buf_pos++];
	      vals[1] = ((float *)buf)[buf_pos++];
	      vals[2] = ((float *)buf)[buf_pos++];
	      vals[3] = ((float *)buf)[buf_pos++];

	      pfPlaneSurfacePoint3((pfPlaneSurface *)node,
				vals[0],vals[1],vals[2],vals[3]);
	    }

	    count = buf[buf_pos++];
	    break;

        case N_PIECEWISEPOLYSURFACE:
	  /* ZZZ Alex */
	    if(node == NULL)
	      node = pfNewPieceWisePolySurface();

	    buf_pos++; /* ZZZ Alex -- BAD BAD BAD .. don't understand
			  why we have to skip one value here ... */

	    pfb_read_parasurface(glb,(pfParaSurface *)node, &buf_pos);

	    {
	      int uPatchCount, vPatchCount, uOrder, vOrder, u, v, uu, vv;
	      pfVec3 cHull;
	      pfPieceWisePolySurface *ppSurf = (pfPieceWisePolySurface *)node;

	      uPatchCount = buf[buf_pos++];
	      vPatchCount = buf[buf_pos++];

	      for(v=0; v<vPatchCount; v++) {
		for(u=0; u<uPatchCount; u++) {
		  uOrder = buf[buf_pos++];
		  vOrder = buf[buf_pos++];
		for(vv=0; vv<vOrder; vv++) {
		  for(uu=0; uu<uOrder; uu++) {
		    cHull[0] = ((float *)buf)[buf_pos++];
		    cHull[1] = ((float *)buf)[buf_pos++];
		    cHull[2] = ((float *)buf)[buf_pos++];

		    pfPieceWisePolySurfaceControlHull(ppSurf,
						      u, v, uu, vv, &cHull);
		  }
		}
		}
	      }
	    }

	    count = buf[buf_pos++];

	    break;

        case N_FRENET_SWEPT_SURFACE:
	  if(node == NULL)
	    node = pfNewFrenetSweptSurface();

	    buf_pos++; /* ZZZ Alex -- BAD BAD BAD .. don't understand
			  why we have to skip one value here ... */

	    pfb_read_parasurface(glb,(pfParaSurface *)node, &buf_pos);

	    {
	      int crossId, pathId, scalarId;
	      pfCurve3d *crossCurve = NULL, *pathCurve = NULL;
	      pfScalar *scalar = NULL;
	      pfFrenetSweptSurface *frenet = (pfFrenetSweptSurface *)node;

	      crossId = buf[buf_pos++];
	      pathId  = buf[buf_pos++];
	      scalarId= buf[buf_pos++];
	      
	      if(crossId  > -1) crossCurve = glb->rl_list[L_NODE][crossId];
	      if(pathId   > -1) pathCurve  = glb->rl_list[L_NODE][pathId];
	      if(scalarId > -1) scalar     = glb->rl_list[L_SCALAR][scalarId];
	    
	      pfFrenetSweptSurfaceSet(frenet, crossCurve, pathCurve, scalar);
	    }

	    count = buf[buf_pos++];

	    break;
        case N_SWEPT_SURFACE:
	  if(node == NULL)
	    node = pfNewSweptSurface();
	  buf_pos++; /* ZZZ Alex -- BAD BAD BAD .. don't understand
			why we have to skip one value here ... */

	  pfb_read_parasurface(glb,(pfParaSurface *)node, &buf_pos);
	  {
	    int crossId, pathId, tId, bId, scalarId;
	    pfCurve3d *crossCurve = NULL, *pathCurve = NULL, *tCurve = NULL, *bCurve = NULL;
	    pfScalar *scalar = NULL;
	    pfSweptSurface *swept = (pfSweptSurface *)node;
	    
	    crossId = buf[buf_pos++];
	    pathId  = buf[buf_pos++];
	    tId     = buf[buf_pos++];
	    bId     = buf[buf_pos++];
	    scalarId= buf[buf_pos++];
	    
	    if(crossId  > -1) crossCurve = glb->rl_list[L_NODE][crossId];
	    if(pathId   > -1) pathCurve  = glb->rl_list[L_NODE][pathId];
	    if(tId      > -1) tCurve     = glb->rl_list[L_NODE][tId];
	    if(bId      > -1) bCurve     = glb->rl_list[L_NODE][bId];
	    if(scalarId > -1) scalar     = glb->rl_list[L_SCALAR][scalarId];
	    
	    if(crossCurve) pfSweptSurfaceCrossSection(swept, crossCurve);
	    if(pathCurve)  pfSweptSurfacePath(swept, pathCurve);
	    if(tCurve)     pfSweptSurfaceT(swept, tCurve);
	    if(bCurve)     pfSweptSurfaceB(swept, bCurve);
	    if(scalar)     pfSweptSurfaceProf(swept, scalar);
	  }

	  count = buf[buf_pos++];

	  break;

        case N_COONS:
	  if(node == NULL)
	    node = pfNewCoonsSurface();
	  buf_pos++; /* ZZZ Alex -- BAD BAD BAD .. don't understand
			why we have to skip one value here ... */

	  pfb_read_parasurface(glb,(pfParaSurface *)node, &buf_pos);
	  {
	    int rId, lId, bId, tId;
	    pfCurve3d *rCurve = NULL, *lCurve = NULL, *bCurve = NULL, *tCurve = NULL;
	    pfCoonsSurface *coons = (pfCoonsSurface *)node;

	    rId = buf[buf_pos++];
	    lId = buf[buf_pos++];
	    bId = buf[buf_pos++];
	    tId = buf[buf_pos++];
	    
	    if(rId > -1) rCurve = glb->rl_list[L_NODE][rId];
	    if(lId > -1) lCurve = glb->rl_list[L_NODE][lId];
	    if(bId > -1) bCurve = glb->rl_list[L_NODE][bId];
	    if(tId > -1) tCurve = glb->rl_list[L_NODE][tId];

	    if(rCurve) pfCoonsSurfaceRight(coons, rCurve);
	    if(lCurve) pfCoonsSurfaceLeft(coons, lCurve);
	    if(bCurve) pfCoonsSurfaceBottom(coons, bCurve);
	    if(tCurve) pfCoonsSurfaceTop(coons, tCurve);
	  }
	  count = buf[buf_pos++];
	  break;

        case N_RULED:
	  if(node == NULL)
	    node = pfNewRuledSurface();
	  buf_pos++; /* ZZZ Alex -- BAD BAD BAD .. don't understand
			why we have to skip one value here ... */

	  pfb_read_parasurface(glb,(pfParaSurface *)node, &buf_pos);
	  {
	    int curve1_id, curve2_id;
	    pfCurve3d *c1 = NULL, *c2 = NULL;
	    pfRuledSurface *ruled = (pfRuledSurface *)node;

	    curve1_id = buf[buf_pos++];
	    curve2_id = buf[buf_pos++];
	    
	    if(curve1_id > -1) c1 = glb->rl_list[L_NODE][curve1_id];
	    if(curve2_id > -1) c2 = glb->rl_list[L_NODE][curve2_id];
	    
 	    if(c1) pfRuledSurfaceCurve1(ruled, c1);
 	    if(c2) pfRuledSurfaceCurve2(ruled, c2);
	  }
	  count = buf[buf_pos++];
	  break;

        case N_LINE3D:
        case N_ORIENTEDLINE3D:
	  if(node == NULL)
	    if(node_type == N_LINE3D)
	      node = pfNewLine3d();
	    else
	      node = pfNewOrientedLine3d();

	  buf_pos++; /* ZZZ Alex */
	  pfb_read_curve3d(glb,(pfCurve3d *)node, &buf_pos);
	  {
	    pfReal x, y, z, t;

	    x = ((float *)buf)[buf_pos++];
	    y = ((float *)buf)[buf_pos++];
	    z = ((float *)buf)[buf_pos++];
	    t = ((float *)buf)[buf_pos++];
	    pfLine3dPoint1((pfLine3d *)node, x, y, z, t);

	    x = ((float *)buf)[buf_pos++];
	    y = ((float *)buf)[buf_pos++];
	    z = ((float *)buf)[buf_pos++];
	    t = ((float *)buf)[buf_pos++];
	    pfLine3dPoint2((pfLine3d *)node, x, y, z, t);

	    if(node_type == N_ORIENTEDLINE3D) {
	      x = ((float *)buf)[buf_pos++];
	      y = ((float *)buf)[buf_pos++];
	      z = ((float *)buf)[buf_pos++];
	      pfOrientedLine3dUpPoint((pfOrientedLine3d *)node, 
				      x, y, z);
	    }
	    
	  }
	  count = buf[buf_pos++];
	  break;

        case N_CIRCLE3D:
	  if(node == NULL)
	    node = pfNewCircle3d();

	  buf_pos++; /* ZZZ Alex */
	  pfb_read_curve3d(glb,(pfCurve3d *)node, &buf_pos);
	  {
	    pfReal radius;

	    radius = ((float *)buf)[buf_pos++];
	    pfCircle3dRadius((pfCircle3d *)node, radius);
	  }
	  count = buf[buf_pos++];
	  break;

        case N_SUPERQUADCURVE3D:
	  if(node == NULL)
	    node = pfNewSuperQuadCurve3d();

	  buf_pos++; /* ZZZ Alex */
	  pfb_read_curve3d(glb,(pfCurve3d *)node, &buf_pos);
	  {
	    pfReal radius, exponent;

	    radius = ((float *)buf)[buf_pos++];
	    exponent = ((float *)buf)[buf_pos++];
	    pfSuperQuadCurve3dRadius((pfSuperQuadCurve3d *)node, radius);
	    pfSuperQuadCurve3dExponent((pfSuperQuadCurve3d *)node, exponent);
	  }
	  count = buf[buf_pos++];
	  break;

        case N_COMPOSITE_CURVE3D:
	  if(node == NULL)
	    node = pfNewCompCurve3d();
	  buf_pos++;
	  pfb_read_curve3d(glb,(pfCurve3d *)node, &buf_pos);
	  {
	    int surfId = -1, curveId = -1;
	    pfParaSurface *surface = NULL;
	    pfCurve2d     *curve   = NULL;

	    surfId = buf[buf_pos++];
	    curveId = buf[buf_pos++];

	    if(surfId  > -1) surface = glb->rl_list[L_NODE][surfId];
	    if(curveId > -1) curve   = glb->rl_list[L_CURVE2D][curveId]; /* XXX Alex -- look in L_NODE instead?*/

	    pfCompCurve3dSet((pfCompositeCurve3d *)node, surface, curve);
	  }
	  count = buf[buf_pos++];
	  break;

        case N_NURB_CURVE3D:
	    if(node == NULL)
	      node = pfNewNurbCurve3d();
	    buf_pos++;
	    pfb_read_curve3d(glb, (pfCurve3d *)node, &buf_pos);
	    {
	      pfRVec4 vec;
	      int nn, ii;
	      pfNurbCurve3d *nc = (pfNurbCurve3d *)node;
              pfReal beginT, endT;

	      beginT = pfGetCurve3dBeginT((pfCurve3d *)nc);
              endT   = pfGetCurve3dEndT  ((pfCurve3d *)nc);

	      nn = buf[buf_pos++];
	      if(nn > 0) {
		for(ii=0; ii<nn; ii++) {
		  vec[0] =  ((float *)buf)[buf_pos++];
		  vec[1] =  ((float *)buf)[buf_pos++];
		  vec[2] =  ((float *)buf)[buf_pos++];
		  vec[3] =  ((float *)buf)[buf_pos++];
		  pfNurbCurve3dControlHull((pfNurbCurve3d *)nc, ii, vec);
		}
	      }
  
	      nn = buf[buf_pos++];
	      if(nn > 0) {
		for(ii=0; ii<nn; ii++)
		  pfNurbCurve3dKnot((pfNurbCurve3d *)nc, ii,
				    ((float *)buf)[buf_pos++]);
	      }

	      pfCurve3dBeginT((pfCurve3d *)nc, beginT);
	      pfCurve3dEndT  ((pfCurve3d *)nc, endT);
	    }
	    count = buf[buf_pos++];
	    break;

	case N_BILLBOARD:
        case N_IBR_NODE:
	    if (node == NULL)
		if (node_type == N_IBR_NODE)
		    node = pfNewIBRnode();
		else
		    node = pfNewBboard();
	    count = buf[buf_pos++];
	    if (count == BB_POS_FLUX)
	    {
		pfBboardPosFlux((pfBillboard *)node,
				(pfFlux*)glb->rl_list[L_FLUX][buf[buf_pos++]]);
	    }
	    else
	    {
		for (i = 0; i < count; i++)
		{
		    v1[0] = ((float *)buf)[buf_pos++];
		    v1[1] = ((float *)buf)[buf_pos++];
		    v1[2] = ((float *)buf)[buf_pos++];
		    pfBboardPos((pfBillboard *)node, i, v1);
		}
	    }
	    pfBboardMode((pfBillboard *)node, PFBB_ROT,
			 bbr_table[buf[buf_pos++]]);
	    v1[0] = ((float *)buf)[buf_pos++];
	    v1[1] = ((float *)buf)[buf_pos++];
	    v1[2] = ((float *)buf)[buf_pos++];
	    pfBboardAxis((pfBillboard *)node, v1);
	    if (node_type == N_IBR_NODE)
	    {
		float a1, a2;
		int n, j;
		pfIBRtexture *IBRtex;

		IBRtex = glb->rl_list[L_IBR_TEX][buf[buf_pos++]];
		pfIBRnodeIBRtexture((pfIBRnode *)node, IBRtex);


		n = buf[buf_pos++];

		for(i=0; i<n; i++)
		{
		    a1 = ((float *)buf)[buf_pos++];
		    a2 = ((float *)buf)[buf_pos++];
		    pfIBRnodeAngles((pfIBRnode *)node, i, a1, a2);
		}

		if (glb->version >= PFBV_VERSION2_6)
		{
		  n = buf[buf_pos++];
		  
		  pfIBRnodeFlags((pfIBRnode *)node, 0xffffffff, 0); /* set all to 0 */
		  pfIBRnodeFlags((pfIBRnode *)node, n, 1); /* set those that are set in 'n' */

		  if(pfGetIBRtextureFlags(IBRtex, PFIBR_USE_PROXY) ||
		     (n & PFIBRN_TEXCOORDS_DEFINED))
		  {
		    int       count, numGroups, viewsPerGroup;
		    int       t, v;
		    pfVec2    ***texCoords;

		    /* printf("reading proxy tex coords\n"); */

		    /* read the proxy texture coordinates */

		    /* read the number of geosets and the number of textures
		     */
		    count = buf[buf_pos++];
		    numGroups = buf[buf_pos++];
		    viewsPerGroup = buf[buf_pos++];

		    texCoords = (pfVec2 ***)pfMalloc(sizeof(pfVec2**) * 
						     numGroups,
						     pfGetSharedArena());

		    /* for each view or groups of views */
		    for(t=0; t<numGroups; t++) 
		    {
		      texCoords[t] = 
			  (pfVec2 **)pfMalloc(sizeof(pfVec2*) *
					      count * viewsPerGroup,
					      pfGetSharedArena());
		      /* for each geoset */
		      for(i=0; i<count; i++)
		      {

			/* read the number of vertices in each geoset */
			/* determine the number of vertices */
			n = buf[buf_pos++];

			for(j=0; j<viewsPerGroup; j++) 
			{
			    if(n == 0)
			    {
				texCoords[t][i*viewsPerGroup + j] = 0;
				continue;
			    }

			    texCoords[t][i*viewsPerGroup + j] = 
				(pfVec2 *)pfMalloc(sizeof(pfVec2) * n,
						   pfGetSharedArena());

			    for(v=0; v<n; v++)
			    {
				texCoords[t][i*viewsPerGroup + j][v][0] = ((float *)buf)[buf_pos++];
				texCoords[t][i*viewsPerGroup + j][v][1] = ((float *)buf)[buf_pos++];	
			    }
			}
		      }
		    }
		    
		    /* set proxy coordinates */
		    pfIBRnodeProxyTexCoords((pfIBRnode *)node, texCoords);
		  }
		}
	    }
	    break;
	case N_LIGHTSOURCE:
	    node = glb->rl_list[L_LSOURCE][buf[buf_pos++]];
	    break;
	case N_GROUP:
	    if (node == NULL)
		node = pfNewGroup();
	    break;
	case N_SCS:
	case N_DCS:
	    {
		pfMatrix m;
		uint m_type;

		if (node_type == N_DCS)
		{
		    m_type = ((uint *)buf)[buf_pos++];
		    if (m_type == CS_UNCONSTRAINED)
			m_type = PFDCS_UNCONSTRAINED;
		    else
			m_type = from_table(mat_table, m_type);
		}

		m[0][0] = ((float *)buf)[buf_pos++];
		m[0][1] = ((float *)buf)[buf_pos++];
		m[0][2] = ((float *)buf)[buf_pos++];
		m[0][3] = ((float *)buf)[buf_pos++];
		m[1][0] = ((float *)buf)[buf_pos++];
		m[1][1] = ((float *)buf)[buf_pos++];
		m[1][2] = ((float *)buf)[buf_pos++];
		m[1][3] = ((float *)buf)[buf_pos++];
		m[2][0] = ((float *)buf)[buf_pos++];
		m[2][1] = ((float *)buf)[buf_pos++];
		m[2][2] = ((float *)buf)[buf_pos++];
		m[2][3] = ((float *)buf)[buf_pos++];
		m[3][0] = ((float *)buf)[buf_pos++];
		m[3][1] = ((float *)buf)[buf_pos++];
		m[3][2] = ((float *)buf)[buf_pos++];
		m[3][3] = ((float *)buf)[buf_pos++];

		if (node_type == N_SCS)
		    node = pfNewSCS(m);
		else
		{
		    if (node == NULL)
			node = pfNewDCS();
		    pfDCSMat((pfDCS *)node, m);
		    pfDCSMatType((pfDCS *)node, m_type);
		}
	    }
	    break;
	case N_FCS:
	    {
		int flux_id;
		uint m_type;

		flux_id = buf[buf_pos++];
		m_type = ((uint *)buf)[buf_pos++];
		if (m_type == CS_UNCONSTRAINED)
		    m_type = PFFCS_UNCONSTRAINED;
		else
		    m_type = from_table(mat_table, m_type);

		if (node == NULL)
		    node = pfNewFCS(glb->rl_list[L_FLUX][flux_id]);
		pfFCSMatType((pfFCS *)node, m_type);
	    }
	    break;
	case N_DOUBLE_SCS:
	case N_DOUBLE_DCS:
	    {
		pfMatrix4d m;
		double	dbuff[16];
		int	dbuff_index;
		uint m_type;

		if (node_type == N_DOUBLE_DCS)
		{
		    m_type = ((uint *)buf)[buf_pos++];
		    if (m_type == CS_UNCONSTRAINED)
			m_type = PFDCS_UNCONSTRAINED;
		    else
			m_type = from_table(mat_table, m_type);
		}
		
		/*
		 *	Must memcpy to temp buffer because buf[buf_pos] may 
		 *	not be on a double-word boundary.
	 	 */
		memcpy (dbuff, & buf[buf_pos], 16 * sizeof (double));
		buf_pos += 32; /* 16 double-words */

		dbuff_index = 0;
		m[0][0] = dbuff[dbuff_index ++];
		m[0][1] = dbuff[dbuff_index ++];
		m[0][2] = dbuff[dbuff_index ++];
		m[0][3] = dbuff[dbuff_index ++];
		m[1][0] = dbuff[dbuff_index ++];
		m[1][1] = dbuff[dbuff_index ++];
		m[1][2] = dbuff[dbuff_index ++];
		m[1][3] = dbuff[dbuff_index ++];
		m[2][0] = dbuff[dbuff_index ++];
		m[2][1] = dbuff[dbuff_index ++];
		m[2][2] = dbuff[dbuff_index ++];
		m[2][3] = dbuff[dbuff_index ++];
		m[3][0] = dbuff[dbuff_index ++];
		m[3][1] = dbuff[dbuff_index ++];
		m[3][2] = dbuff[dbuff_index ++];
		m[3][3] = dbuff[dbuff_index ++];

		if (node_type == N_DOUBLE_SCS)
		    node = pfNewDoubleSCS(m);
		else
		{
		    if (node == NULL)
			node = pfNewDoubleDCS();
		    pfDoubleDCSMat((pfDoubleDCS *)node, m);
		    pfDoubleDCSMatType((pfDoubleDCS *)node, m_type);
		}
	    }
	    break;
	case N_DOUBLE_FCS:
	    {
		int flux_id;
		uint m_type;

		flux_id = buf[buf_pos++];
		m_type = ((uint *)buf)[buf_pos++];
		if (m_type == CS_UNCONSTRAINED)
		    m_type = PFFCS_UNCONSTRAINED;
		else
		    m_type = from_table(mat_table, m_type);

		if (node == NULL)
		    node = pfNewDoubleFCS(glb->rl_list[L_FLUX][flux_id]);
		pfDoubleFCSMatType((pfDoubleFCS *)node, m_type);
	    }
	    break;
	case N_PARTITION:
	    if (node == NULL)
		node = pfNewPart();
	    if (buf[buf_pos++])
	    {
		v1[0] = ((float *)buf)[buf_pos++];
		v1[1] = ((float *)buf)[buf_pos++];
		v1[2] = ((float *)buf)[buf_pos++];
		pfPartAttr((pfPartition *)node, PFPART_MIN_SPACING, v1);
	    }
	    if (buf[buf_pos++])
	    {
		v1[0] = ((float *)buf)[buf_pos++];
		v1[1] = ((float *)buf)[buf_pos++];
		v1[2] = ((float *)buf)[buf_pos++];
		pfPartAttr((pfPartition *)node, PFPART_MAX_SPACING, v1);
	    }
	    if (buf[buf_pos++])
	    {
		v1[0] = ((float *)buf)[buf_pos++];
		v1[1] = ((float *)buf)[buf_pos++];
		v1[2] = ((float *)buf)[buf_pos++];
		pfPartAttr((pfPartition *)node, PFPART_ORIGIN, v1);
	    }
	    pfPartVal((pfPartition *)node, PFPART_FINENESS,
		      ((float *)buf)[buf_pos++]);
	    break;
	case N_SCENE:
	    if (node == NULL)
		node = pfNewScene();
	    t1 = buf[buf_pos++];
	    t2 = buf[buf_pos++];
	    if (t1 != -1)
		pfSceneGState((pfScene *)node, glb->rl_list[L_GSTATE][t1]);
	    if (t2 != -1)
		pfSceneGStateIndex((pfScene *)node, t2);
	    break;
	case N_SWITCH:
	    if (node == NULL)
		node = pfNewSwitch();
	    t1 = buf[buf_pos++];
	    if (t1 == PFB_SWITCH_OFF)
		pfSwitchVal((pfSwitch*)node, PFSWITCH_OFF);
	    else if (t1 == PFB_SWITCH_ON)
		pfSwitchVal((pfSwitch*)node, PFSWITCH_ON);
	    else
		pfSwitchVal((pfSwitch*)node, t1);
	    if (glb->version >= PFBV_SWITCH_VAL_FLUX)
	    {
		t1 = buf[buf_pos++];
		if (t1 != -1)
		    pfSwitchValFlux((pfSwitch*)node, glb->rl_list[L_FLUX][t1]);
	    }
	    break;
	case N_LOD:
	    if (node == NULL)
		node = pfNewLOD();
	    count = buf[buf_pos++];
	    for (i = 0; i <= count; i++)
		pfLODRange((pfLOD *)node, i, ((float *)buf)[buf_pos++]);
	    for (i = 0; i <= count; i++)
		pfLODTransition((pfLOD *)node, i, ((float *)buf)[buf_pos++]);
	    v1[0] = ((float *)buf)[buf_pos++];
	    v1[1] = ((float *)buf)[buf_pos++];
	    v1[2] = ((float *)buf)[buf_pos++];
	    pfLODCenter((pfLOD *)node, v1);
	    t1 = buf[buf_pos++];
	    t2 = buf[buf_pos++];
	    if (t1 != -1)
		pfLODLODState((pfLOD *)node, glb->rl_list[L_LODSTATE][t1]);
	    if (t2 != -1)
		pfLODLODStateIndex((pfLOD *)node, t2);
	    break;
	case N_SEQUENCE:
	    if (node == NULL)
		node = pfNewSeq();
	    count = buf[buf_pos++];
	    for (i = 0; i < count; i++)
		pfSeqTime((pfSequence *)node, i, ((float *)buf)[buf_pos++]);
	    t1 = buf[buf_pos++];
	    t2 = buf[buf_pos++];
	    t3 = buf[buf_pos++];
	    pfSeqInterval((pfSequence *)node, sqi_table[t1], t2, t3);
	    f1 = ((float *)buf)[buf_pos++];
	    t1 = buf[buf_pos++];
	    pfSeqDuration((pfSequence *)node, f1, t1);
	    pfSeqMode((pfSequence *)node, sqm_table[buf[buf_pos++]]);
	    break;
	case N_LAYER:
	    if (node == NULL)
		node = pfNewLayer();
	    t1 = buf[buf_pos++];
	    t2 = buf[buf_pos++];
	    t3 = buf[buf_pos++];
	    pfLayerMode((pfLayer *)node,
			layer_table[t1] |
			(t2 << PFDECAL_LAYER_SHIFT) |
			(t3 ? PFDECAL_LAYER_OFFSET : 0));
	    break;
	case N_MORPH:
	    node = glb->rl_list[L_MORPH][buf[buf_pos++]];
	    break;
	case N_ASD:
	    {
		int bind, size, which, attr_type, attr;
        int loop;
        int inner;
        /* blocksize assumed to be of floats */
        int blockSize = 0;
        asd_face_t *tmpFace;
        asd_lodrange_t *tmpRange;
        asd_vert_t *tmpVert;
        float *tmpAttr;

        printf ("Reading an ASD\n");

		if (node == NULL)
		    node = pfNewASD();

		bind = buf[buf_pos++];
		size = buf[buf_pos++];
		attr = buf[buf_pos++];
        if (r_swap)
        for (loop=0; loop < size; loop++)
        {
            tmpRange = (asd_lodrange_t *) &((pfASDLODRange*)glb->rl_list[L_ASDDATA][attr])[loop];
            P_32_SWAP(&tmpRange->switchin);
            P_32_SWAP(&tmpRange->morph);
        }
		if (attr != -1)
		    pfASDAttr((pfASD *)node, PFASD_LODS, bind, size,
			      glb->rl_list[L_ASDDATA][attr]);

		bind = buf[buf_pos++];
		size = buf[buf_pos++];
		attr = buf[buf_pos++];
        if (r_swap)
        for (loop=0; loop < size; loop++)
        {
            tmpVert = (asd_vert_t*) &((pfASDVert*)glb->rl_list[L_ASDDATA][attr])[loop];
            P_32_SWAP(&tmpVert->v0[0]);
            P_32_SWAP(&tmpVert->v0[1]);
            P_32_SWAP(&tmpVert->v0[2]);
            P_32_SWAP(&tmpVert->vd[0]);
            P_32_SWAP(&tmpVert->vd[1]);
            P_32_SWAP(&tmpVert->vd[2]);
            P_32_SWAP(&tmpVert->neighborid[0]);
            P_32_SWAP(&tmpVert->neighborid[1]);
            P_32_SWAP(&tmpVert->vertid);
        }
		if (attr != -1)
		    pfASDAttr((pfASD *)node, PFASD_COORDS, bind, size,
			      glb->rl_list[L_ASDDATA][attr]);

		bind = buf[buf_pos++];
		size = buf[buf_pos++];
		attr = buf[buf_pos++];
        if ((attr != -1) && (r_swap))
            for (loop=0; loop < size; loop++)
            {
                tmpFace = (asd_face_t*) &((pfASDFace*)glb->rl_list[L_ASDDATA][attr])[loop];
    
                /* int fields */
                P_32_SWAP(&tmpFace->level);
                P_32_SWAP(&tmpFace->tsid);
                P_32_SWAP(&tmpFace->vert[0]);
                P_32_SWAP(&tmpFace->vert[1]);
                P_32_SWAP(&tmpFace->vert[2]);
                P_32_SWAP(&tmpFace->attr[0]);
                P_32_SWAP(&tmpFace->attr[1]);
                P_32_SWAP(&tmpFace->attr[2]);
                P_32_SWAP(&tmpFace->refvert[0]);
                P_32_SWAP(&tmpFace->refvert[1]);
                P_32_SWAP(&tmpFace->refvert[2]);
                P_32_SWAP(&tmpFace->sattr[0]);
                P_32_SWAP(&tmpFace->sattr[1]);
                P_32_SWAP(&tmpFace->sattr[2]);
                P_32_SWAP(&tmpFace->child[0]);
                P_32_SWAP(&tmpFace->child[1]);
                P_32_SWAP(&tmpFace->child[2]);
                P_32_SWAP(&tmpFace->child[3]);
                /* ushort fields */
                P_16_SWAP(&tmpFace->gstateid);
                P_16_SWAP(&tmpFace->mask);
            }
		if (attr != -1)
		    pfASDAttr((pfASD *)node, PFASD_FACES, bind, size,
			      glb->rl_list[L_ASDDATA][attr]);

		attr_type = buf[buf_pos++];
		size = buf[buf_pos++];
		attr = buf[buf_pos++];
		if (attr != -1)
		{
		    which = 0;
		    if (attr_type & ASD_ATTR_NORMALS)
            {
			    which |= PFASD_NORMALS;
                blockSize += 6;
            }
		    if (attr_type & ASD_ATTR_COLORS)
            {
			    which |= PFASD_COLORS;
                blockSize += 8;
            }
		    if (attr_type & ASD_ATTR_TCOORDS)
            {
			    which |= PFASD_TCOORDS;
                blockSize += 4;
            }
            if (r_swap)
            {
                tmpAttr = (float*)glb->rl_list[L_ASDDATA][attr];
                for (loop=0; loop < size; loop++)
                    for (inner=0; inner < blockSize; inner++)
                    {
                        P_32_SWAP(tmpAttr);
                        tmpAttr++;
                    }
            }
		    pfASDAttr((pfASD *)node, PFASD_OVERALL_ATTR, which, size,
			      glb->rl_list[L_ASDDATA][attr]);
		}

		attr_type = buf[buf_pos++];
		size = buf[buf_pos++];
		attr = buf[buf_pos++];
		if (attr != -1)
		{
		    which = 0;
            blockSize = 0;
		    if (attr_type & ASD_ATTR_NORMALS)
            {
                blockSize += 6;
			    which |= PFASD_NORMALS;
            }
		    if (attr_type & ASD_ATTR_COLORS)
            {
			    which |= PFASD_COLORS;
                blockSize += 8;
            }
		    if (attr_type & ASD_ATTR_TCOORDS)
            {
			    which |= PFASD_TCOORDS;
                blockSize += 4;
            }
            if (r_swap)
            {
                tmpAttr = (float*)glb->rl_list[L_ASDDATA][attr];
                for (loop=0; loop < size; loop++)
                    for (inner=0; inner < blockSize; inner++)
                    {
                        P_32_SWAP(tmpAttr);
                        tmpAttr++;
                    }
            }
		    pfASDAttr((pfASD *)node, PFASD_PER_VERTEX_ATTR, which, size,
			      glb->rl_list[L_ASDDATA][attr]);
		}

		pfASDNumBaseFaces((pfASD *)node, buf[buf_pos++]);
		t1 = buf[buf_pos++];
		t2 = 0;
		if (t1 != -1)
		{
		    if (t1 & ASD_ATTR_NORMALS)
			t2 |= PFASD_NORMALS;
		    if (t1 & ASD_ATTR_COLORS)
			t2 |= PFASD_COLORS;
		    if (t1 & ASD_ATTR_TCOORDS)
			t2 |= PFASD_TCOORDS;
		}
		pfASDMorphAttrs((pfASD *)node, t2);
		if (glb->version >= PFBV_ASD_GSTATES)
		{
		    count = buf[buf_pos++];
		    if (count > 0)
		    {
			pfGeoState **gsa = (pfGeoState**)
					    malloc(sizeof(pfGeoState*) * count);
			for (i = 0; i < count; i++)
			{
			    if ((t1 = buf[buf_pos++]) != -1)
				gsa[i] = glb->rl_list[L_GSTATE][t1];
			    else
				gsa[i] = NULL;
			}
			pfASDGStates((pfASD*)node, gsa, count);
			free(gsa);
		    }
		}
		else
		{
		    if ((t1 = buf[buf_pos++]) != -1)
		    {
			pfGeoState **gsa = (pfGeoState**)
					    malloc(sizeof(pfGeoState*));
			*gsa = glb->rl_list[L_GSTATE][t1];
			pfASDGStates((pfASD*)node, gsa, 1);
			free(gsa);
		    }
		}
/* XXXX probably should add morphweightconstraint to buf */
		pfASDMaxMorphDepth((pfASD *)node, buf[buf_pos++], 0);
		pfASDEvalMethod((pfASD *)node, asd_em_table[buf[buf_pos++]]);
		if ((t1 = buf[buf_pos++]) != -1)
		    pfASDEvalFunc((pfASD *)node, glb->rl_list[L_UFUNC][t1]);
#if 0
		if (glb->version >= PFBV_ASD_SYNC_GROUP)
		{
		    t1 = buf[buf_pos++];
		    pfASDSyncGroup((pfASD *)node,
				   *(uint*)&glb->rl_list[L_SG_NAME][t1]);
		}
#endif
	   	{
                    char *e = getenv("_PFASD_CLIPRINGS");
                    int _PFASD_CLIPRINGS = (e ? *e ? atoi(e) : -1 : 0);
pfNotify(PFNFY_INTERNAL_DEBUG, PFNFY_PRINT, "_PFASD_CLIPRINGS %d", _PFASD_CLIPRINGS);
                    if(_PFASD_CLIPRINGS)
                        pfdASDClipring(node, _PFASD_CLIPRINGS);
                }

		pfASDConfig((pfASD *)node);
        /* pfPrint((pfASD*)node, NULL, PFPRINT_VB_DEBUG, NULL); */
	    }
	    break;
    }

    if (pfIsOfType(node, pfGetGroupClassType()))
    {
	count = buf[buf_pos++];
	glb->children[node_num] = (int *)malloc((count+1) * sizeof(int));
	glb->children[node_num][0] = count;
	for (i = 1; i <= count; i++)
	    glb->children[node_num][i] = buf[buf_pos++];
    }
    else if (pfIsOfType(node, pfGetGeodeClassType()))
    {
	for (i = 0; i < count; i++){
	    if(glb->version >= PFBV_GEOARRAY){	/* Check hack for geoArray */
		int t = buf[buf_pos++];
	        pfAddGSet((pfGeode *)node,
		          (t & PFB_GARRAY_IXBIT ?
			   glb->rl_list[L_GARRAY][t ^ PFB_GARRAY_IXBIT]:
			   glb->rl_list[L_GSET][t]));
	    } else {
		pfAddGSet((pfGeode *)node, glb->rl_list[L_GSET][buf[buf_pos++]]);
	    }
	}
    }

    pfNodeTravMask(node, PFTRAV_ISECT, ((uint *)buf)[buf_pos++],
		   PFTRAV_SELF, PF_SET);
    pfNodeTravMask(node, PFTRAV_APP, ((uint *)buf)[buf_pos++],
		   PFTRAV_SELF, PF_SET);
    pfNodeTravMask(node, PFTRAV_CULL, ((uint *)buf)[buf_pos++],
		   PFTRAV_SELF, PF_SET);
    pfNodeTravMask(node, PFTRAV_DRAW, ((uint *)buf)[buf_pos++],
		   PFTRAV_SELF, PF_SET);

    if (glb->version >= PFBV_UFUNC)
    {
	if (buf[buf_pos++] == 1)
	{
	    /*
	     *  has trav funcs and/or trav data
	     */
	    void *pre, *post;

	    if ((t1 = buf[buf_pos++]) != -1)
		pre = glb->rl_list[L_UFUNC][t1];
	    else
		pre = NULL;
	    if ((t1 = buf[buf_pos++]) != -1)
		post = glb->rl_list[L_UFUNC][t1];
	    else
		post = NULL;
	    if (pre || post)
		pfNodeTravFuncs(node, PFTRAV_APP, pre, post);
	    if ((t1 = buf[buf_pos++]) != -1)
		set_trav_data(node, PFTRAV_APP, t1, glb);

	    if ((t1 = buf[buf_pos++]) != -1)
		pre = glb->rl_list[L_UFUNC][t1];
	    else
		pre = NULL;
	    if ((t1 = buf[buf_pos++]) != -1)
		post = glb->rl_list[L_UFUNC][t1];
	    else
		post = NULL;
	    if (pre || post)
		pfNodeTravFuncs(node, PFTRAV_CULL, pre, post);
	    if ((t1 = buf[buf_pos++]) != -1)
		set_trav_data(node, PFTRAV_CULL, t1, glb);

	    if ((t1 = buf[buf_pos++]) != -1)
		pre = glb->rl_list[L_UFUNC][t1];
	    else
		pre = NULL;
	    if ((t1 = buf[buf_pos++]) != -1)
		post = glb->rl_list[L_UFUNC][t1];
	    else
		post = NULL;
	    if (pre || post)
		pfNodeTravFuncs(node, PFTRAV_DRAW, pre, post);
	    if ((t1 = buf[buf_pos++]) != -1)
		set_trav_data(node, PFTRAV_DRAW, t1, glb);

	    if ((t1 = buf[buf_pos++]) != -1)
		pre = glb->rl_list[L_UFUNC][t1];
	    else
		pre = NULL;
	    if ((t1 = buf[buf_pos++]) != -1)
		post = glb->rl_list[L_UFUNC][t1];
	    else
		post = NULL;
	    if (pre || post)
		pfNodeTravFuncs(node, PFTRAV_ISECT, pre, post);
	    if ((t1 = buf[buf_pos++]) != -1)
		set_trav_data(node, PFTRAV_ISECT, t1, glb);
	}
    }

    if ((i = buf[buf_pos++]) != -1)
	set_udata((pfObject *)node, i, glb);

    if (glb->version >= PFBV_NODE_BSPHERE)
    {
	if ((t1 = bound_table[buf[buf_pos++]]) != PFBOUND_DYNAMIC)
	{
	    pfSphere sphere;

	    sphere.center[0] = ((float *)buf)[buf_pos++];
	    sphere.center[1] = ((float *)buf)[buf_pos++];
	    sphere.center[2] = ((float *)buf)[buf_pos++];
	    sphere.radius = ((float *)buf)[buf_pos++];
	    pfNodeBSphere(node, &sphere, t1);
	}
    }

    if (glb->version >= PFBV_SHADER)
    {
      /* add this node to the list of nodes that have
       * shaders applied to them if necessary.
       */
      int numShadersApplied = buf[buf_pos++];

      /* add to list of nodes that have shaders applied to them */
      /*for(i=0; i<numShadersApplied; i++) {
	shaderID = buf[buf_pos++];
	shadedNodeListAdd(node, shaderID ,glb);
	}
      */

      buf_pos += numShadersApplied;
    }

    pfb_fread(&buf_size, sizeof(int), 1, glb->ifp);
    if (buf_size != -1)
    {
	char name[PF_MAXSTRING];

	pfb_fread(name, 1, buf_size, glb->ifp);
	name[buf_size] = '\0';
	pfNodeName(node, name);
    }

    if (custom_id != -1)
    {
	int size;

	pfb_fread(&size, sizeof(int), 1, glb->ifp);
	if (custom)
	{
	    int start;

	    start = (int)pfbFileReadFunction.ftell(glb->ifp);

	    glb->data_total = 0;
	    custom->load_func(node, glb);

	    if (glb->data_total != size)
	    {
		/*
		 *  The wrong amount of data was loaded by the users function.
		 */
		pfNotify(PFNFY_WARN, PFNFY_PRINT,
			 "%s  Custom node load function failed.\n",
			 "pfdLoadFile_pfb:");
		pfNotify(PFNFY_WARN, PFNFY_PRINT,
			 "%s  Loaded %d bytes of data.\n",
			 "pfdLoadFile_pfb:", glb->data_total);
		pfNotify(PFNFY_WARN, PFNFY_PRINT,
			 "%s  Should have loaded %d bytes of data.\n",
			 "pfdLoadFile_pfb:", size);
		pfbFileReadFunction.fseek(glb->ifp, start + size, SEEK_SET);
	    }
	}
	else
	{
	    pfbFileReadFunction.fseek(glb->ifp, size, SEEK_CUR);
	    pfNotify(PFNFY_WARN, PFNFY_PRINT,
		     "%s Unable to load custom node.", "pfdLoadFile_pfb: ");
	}
    }

    return(node);
}


static void *pfa_read_custom(pfa_global_t *glb)
{
    int used;
    int i;

    READ_LINE();
    used = atoi(glb->line_buf);
    READ_LINE();
    glb->line_buf[strlen(glb->line_buf)-1] = '\0';

    for (i = 0; i < glb->num_custom_nodes; i++)
	if (strcmp(glb->line_buf, glb->custom_nodes[i].name) == 0)
	    return(&glb->custom_nodes[i]);

    if (used == 1)
    {
	pfNotify(PFNFY_WARN, PFNFY_PRINT,
		 "%s File contains custom nodes of type \"%s\", and",
		 "pfdLoadFile_pfa: ", glb->line_buf);
	pfNotify(PFNFY_WARN, PFNFY_MORE,
		 "pfdAddCustomNode_pfa() has not been called for this type.\n");
    }

    return(NULL);
}


static void *pfb_read_custom(pfa_global_t *glb)
{
    char name[PF_MAXSTRING];
    int buf[2];			/* used, size */
    int i;

    pfb_fread(&buf, sizeof(int), 2, glb->ifp);
    pfb_fread(name, 1, buf[1], glb->ifp);
    name[buf[1]] = '\0';

    for (i = 0; i < glb->num_custom_nodes; i++)
	if (strcmp(name, glb->custom_nodes[i].name) == 0)
	    return(&glb->custom_nodes[i]);

    if (buf[0] == 1)		/* used */
    {
	pfNotify(PFNFY_WARN, PFNFY_PRINT,
		 "%s File contains custom nodes of type \"%s\", and",
		 "pfdLoadFile_pfb: ", name);
	pfNotify(PFNFY_WARN, PFNFY_MORE,
		 "pfdAddCustomNode_pfb() has not been called for this type.\n");
    }

    return(NULL);
}


static void connect_nodes(pfa_global_t *glb)
{
    int i, j;
    int partitions;

    partitions = 0;

    for (i = 0; i < glb->l_num[L_NODE]; i++)
    {
	if (pfIsOfType(glb->rl_list[L_NODE][i], pfGetGroupClassType()))
	{
	    for (j = 1; j <= glb->children[i][0]; j++)
		pfAddChild(glb->rl_list[L_NODE][i],
			   glb->rl_list[L_NODE][glb->children[i][j]]);
	    free(glb->children[i]);

	    if (pfIsOfType(glb->rl_list[L_NODE][i], pfGetPartClassType()))
		partitions = 1;
	}
    }
    free(glb->children);

    if (glb->hlight_gstates != NULL)
    {
	for (i = 0; i < glb->l_num[L_HLIGHT]; i++)
	    if (glb->hlight_gstates[i] != -1)
		pfHlightGState(glb->rl_list[L_HLIGHT][i],
			       glb->rl_list[L_GSTATE][glb->hlight_gstates[i]]);
	free(glb->hlight_gstates);
    }

    if (partitions)
	for (i = 0; i < glb->l_num[L_NODE]; i++)
	{
	    if (pfIsOfType(glb->rl_list[L_NODE][i], pfGetPartClassType()))
		pfBuildPart((pfPartition *)glb->rl_list[L_NODE][i]);
	}

    connect_udata(glb);
}


PFPFB_DLLEXPORT int pfdStoreFile_pfa(pfNode *root, const char *fileName)
{
    int i;
    pfa_global_t *glb;

    glb = (pfa_global_t *)malloc(sizeof(pfa_global_t));
    bzero(glb, sizeof(pfa_global_t));
    glb->add_comments = 1;

    glb->maxTextures = PF_MAX_TEXTURES_31; /* which is == 8 */

    if (crypt_key_pfb != NULL)
	pfNotify(PFNFY_WARN, PFNFY_PRINT,
		 "pfdStoreFile_pfa: .pfa format does not support encryption");

    /*
     *  open the output file for writing
     */
    if ((glb->ofp = fopen(fileName, "w")) == NULL)
    {
	pfNotify(PFNFY_WARN, PFNFY_RESOURCE,
		 "pfdStoreFile_pfa: Could not open \"%s\" for writing\n",
		 fileName);
	free(glb);
	return(FALSE);
    }

    pfNotify(PFNFY_NOTICE, PFNFY_PRINT, "pfdStoreFile_pfa: Storing \"%s\"",
	     fileName);

    glb->save_texture_image = save_texture_image;
    glb->save_texture_path = save_texture_path;
    glb->save_texture_pfi = save_texture_pfi;
    glb->disable_rep_save = disable_rep_save;

    {
	char *e;
	e = getenv("PFPFB_SUBDIVSURFACE_SAVE_GEOSETS");
	
	if(e)
	    subdivsurface_save_geosets = atoi(e);
    }
    glb->subdivsurface_save_geosets = subdivsurface_save_geosets;

	    

    glb->num_udata_slots = pfGetNumNamedUserDataSlots();
    glb->udfunc = copy_udfunc(udfunc_pfa);
    if (glb->num_custom_nodes = num_custom_nodes_pfa)
    {
	glb->custom_nodes = (custom_node_t *)malloc(num_custom_nodes_pfa *
						    sizeof(custom_node_t));
	bcopy(custom_nodes_pfa, glb->custom_nodes,
	      num_custom_nodes_pfa * sizeof(custom_node_t));
	for (i = 0; i < num_custom_nodes_pfa; i++)
	    glb->custom_nodes[i].name = strdup(custom_nodes_pfa[i].name);
    }
    set_buf_size(512, glb);

    /*
     *  Count the number of, and make lists of pfGeoStates, pfGeoSets, and
     *  pfNodes.
     */
    descend_node(root, glb);
    really_descend_morphs(glb);
    really_descend_unknown_datas(glb);

    /*
     *  write header
     */
    fprintf(glb->ofp, "0x%08x %d	# header\n",
	    	PFB_MAGIC_NUMBER, PFB_VERSION_NUMBER);


    /*
     *  write lists
     */
    PFA_WRITE_LIST(L_UFUNC, pfa_write_ufunc);
    pfa_write_custom_list(glb);
    PFA_WRITE_LIST(L_SG_NAME, pfa_write_sg_name);
    if (glb->max_udata_slot > 1)
    {
	PFA_WRITE_LIST(L_UDATA_NAME, pfa_write_udata_name);
	PFA_WRITE_LIST(L_UDATA_LIST, pfa_write_udata_list);
    }
    PFA_WRITE_LIST(L_UDATA, pfa_write_udata);
    PFA_WRITE_LIST(L_FLUX, pfa_write_flux);
    PFA_WRITE_SLIST(L_LLIST, pfa_write_llist);
    PFA_WRITE_SLIST(L_VLIST, pfa_write_vlist);
    PFA_WRITE_SLIST(L_CLIST, pfa_write_clist);
    PFA_WRITE_SLIST(L_NLIST, pfa_write_nlist);
    PFA_WRITE_SLIST(L_TLIST, pfa_write_tlist);
    PFA_WRITE_SLIST(L_ILIST, pfa_write_ilist);
    PFA_WRITE_SLIST(L_FLIST, pfa_write_flist);
    PFA_WRITE_LIST(L_ENGINE, pfa_write_engine);
    PFA_WRITE_SLIST(L_IMAGE, pfa_write_image);
    PFA_WRITE_LIST(L_QUEUE, pfa_write_queue);
    PFA_WRITE_LIST(L_ITILE, pfa_write_itile);
    PFA_WRITE_LIST(L_ICACHE, pfa_write_icache);
    PFA_WRITE_LIST(L_MTL, pfa_write_mtl);
    PFA_WRITE_LIST(L_LMODEL, pfa_write_lmodel);
    PFA_WRITE_LIST(L_LIGHT, pfa_write_light);
    PFA_WRITE_LIST(L_TLOD, pfa_write_tlod);
    PFA_WRITE_LIST(L_GPROG, pfa_write_gprogram);
    PFA_WRITE_LIST(L_GPROGPARM, pfa_write_gprogramparms);
    PFA_WRITE_LIST(L_TEX, pfa_write_tex);
    PFA_WRITE_LIST(L_IBR_TEX, pfa_write_ibr_tex);
    PFA_WRITE_LIST(L_TENV, pfa_write_tenv);
    PFA_WRITE_LIST(L_TGEN, pfa_write_tgen);
    PFA_WRITE_LIST(L_FOG, pfa_write_fog);
    PFA_WRITE_LIST(L_CTAB, pfa_write_ctab);
    PFA_WRITE_LIST(L_LPSTATE, pfa_write_lpstate);
    PFA_WRITE_LIST(L_HLIGHT, pfa_write_hlight);
    PFA_WRITE_LIST(L_SHADOBJ, pfa_write_shader_object);
    PFA_WRITE_LIST(L_SHADPROG, pfa_write_shader_program);
    PFA_WRITE_LIST(L_GSTATE, pfa_write_gstate);
    PFA_WRITE_SLIST(L_APPEARANCE, pfa_write_appearance);
    PFA_WRITE_SLIST(L_ASDDATA, pfa_write_asddata);
    PFA_WRITE_LIST(L_MORPH, pfa_write_morph);
    PFA_WRITE_LIST(L_FRUST, pfa_write_frust);
    PFA_WRITE_LIST(L_LSOURCE, pfa_write_lsource);
    PFA_WRITE_LIST(L_GSET, pfa_write_gset);
    PFA_WRITE_SLIST(L_VARRAY_1, pfa_write_varray_1);
    PFA_WRITE_SLIST(L_VARRAY_2, pfa_write_varray_2);
    PFA_WRITE_SLIST(L_VARRAY_4, pfa_write_varray_4);
    PFA_WRITE_LIST(L_GARRAY, pfa_write_geoArray);
    PFA_WRITE_LIST(L_LODSTATE, pfa_write_lodstate);
    PFA_WRITE_LIST(L_FONT, pfa_write_font);
    PFA_WRITE_LIST(L_STRING, pfa_write_string);
    PFA_WRITE_LIST(L_MESH, pfa_write_mesh);
    PFA_WRITE_LIST(L_CURVE2D, pfa_write_curve2d_general);
    PFA_WRITE_LIST(L_DISCURVE2D, pfa_write_discurve2d);
    PFA_WRITE_LIST(L_SCALAR, pfa_write_scalar);
    PFA_WRITE_LIST(L_TOPO, pfa_write_topo);
    PFA_WRITE_SLIST(L_NODE, pfa_write_node);
 
    /*
     *  close the file
     */
    fclose(glb->ofp);
    
	
    /*
     *  free temporary memory used in creating the file.
     */
    free_store_data(glb);

    return(TRUE);
}


PFPFB_DLLEXPORT int pfdStoreFile_pfb(pfNode *root, const char *fileName)
{
    int i;
    pfa_global_t *glb;
    int buf[4];

    glb = (pfa_global_t *)malloc(sizeof(pfa_global_t));
    bzero(glb, sizeof(pfa_global_t));

    glb->maxTextures = PF_MAX_TEXTURES_31; /* which is == 8 */

    /*
     *  open the output file for writing
     */
    if ((glb->ofp = fopen(fileName, "wb")) == NULL)
    {
	pfNotify(PFNFY_WARN, PFNFY_RESOURCE,
		 "pfdStoreFile_pfb:  Could not open \"%s\" for writing\n",
		 fileName);
	free(glb);
	return(FALSE);
    }

    if (crypt_key_pfb != NULL)
    {
	i = (int)(crypt_key_pfb[0] * sizeof(uint) + sizeof(uint));
	glb->crypt_key = (uint *)malloc(i);
	bcopy(crypt_key_pfb, glb->crypt_key, i);
	glb->encrypt_func = encrypt_func_pfb;
    }

    if (glb->crypt_key)
	pfNotify(PFNFY_NOTICE, PFNFY_PRINT,
		 "pfdStoreFile_pfb: Storing ENCRYPTED \"%s\"", fileName);
    else
	pfNotify(PFNFY_NOTICE, PFNFY_PRINT,
		 "pfdStoreFile_pfb: Storing \"%s\"", fileName);

    glb->save_texture_image = save_texture_image;
    glb->save_texture_path = save_texture_path;
    glb->save_texture_pfi = save_texture_pfi;
    glb->disable_rep_save = disable_rep_save;
    glb->subdivsurface_save_geosets = subdivsurface_save_geosets;
    glb->num_udata_slots = pfGetNumNamedUserDataSlots();
    glb->udfunc = copy_udfunc(udfunc_pfb);
    if (glb->num_custom_nodes = num_custom_nodes_pfb)
    {
	glb->custom_nodes = (custom_node_t *)malloc(num_custom_nodes_pfb *
						    sizeof(custom_node_t));
	bcopy(custom_nodes_pfb, glb->custom_nodes,
	      num_custom_nodes_pfb * sizeof(custom_node_t));
	for (i = 0; i < num_custom_nodes_pfb; i++)
	    glb->custom_nodes[i].name = strdup(custom_nodes_pfb[i].name);
    }
    set_buf_size(512, glb);

    /*
     *  Count the number of, and make lists of pfGeoStates, pfGeoSets, and
     *  pfNodes.
     */
    descend_node(root, glb);
    really_descend_morphs(glb);
    really_descend_unknown_datas(glb);

    /*
     *  write header
     */
	if (glb->crypt_key)
		buf[0] = PFB_MAGIC_ENCODED;
    else
		buf[0] = PFB_MAGIC_NUMBER;
    buf[1] = PFB_VERSION_NUMBER;
    buf[2] = 1;
    buf[3] = sizeof(int) * 4;
    fwrite(buf, sizeof(int), 4, glb->ofp);

    /*
     *  write lists
     */
    PFB_WRITE_LIST(L_UFUNC, pfb_write_ufunc);
    pfb_write_custom_list(glb);
    PFB_WRITE_LIST(L_SG_NAME, pfb_write_sg_name);
    if (glb->max_udata_slot > 1)
    {
	PFB_WRITE_LIST(L_UDATA_NAME, pfb_write_udata_name);
	PFB_WRITE_LIST(L_UDATA_LIST, pfb_write_udata_list);
    }
    PFB_WRITE_LIST(L_UDATA, pfb_write_udata);
    PFB_WRITE_LIST(L_FLUX, pfb_write_flux);
    PFB_WRITE_SLIST(L_LLIST, pfb_write_llist);
    PFB_WRITE_SLIST(L_VLIST, pfb_write_vlist);
    PFB_WRITE_SLIST(L_CLIST, pfb_write_clist);
    PFB_WRITE_SLIST(L_NLIST, pfb_write_nlist);
    PFB_WRITE_SLIST(L_TLIST, pfb_write_tlist);
    PFB_WRITE_SLIST(L_ILIST, pfb_write_ilist);
    PFB_WRITE_SLIST(L_FLIST, pfb_write_flist);
    PFB_WRITE_LIST(L_ENGINE, pfb_write_engine);
    PFB_WRITE_SLIST(L_IMAGE, pfb_write_image);
    PFB_WRITE_LIST(L_QUEUE, pfb_write_queue);
    PFB_WRITE_LIST(L_ITILE, pfb_write_itile);
    PFB_WRITE_LIST(L_ICACHE, pfb_write_icache);
    PFB_WRITE_LIST(L_MTL, pfb_write_mtl);
    PFB_WRITE_LIST(L_LMODEL, pfb_write_lmodel);
    PFB_WRITE_LIST(L_LIGHT, pfb_write_light);
    PFB_WRITE_LIST(L_TLOD, pfb_write_tlod);
    PFB_WRITE_LIST(L_GPROG, pfb_write_gprogram);
    PFB_WRITE_LIST(L_GPROGPARM, pfb_write_gprogramparms);
    PFB_WRITE_LIST(L_TEX, pfb_write_tex);
    PFB_WRITE_LIST(L_IBR_TEX, pfb_write_ibr_tex);
    PFB_WRITE_LIST(L_TENV, pfb_write_tenv);
    PFB_WRITE_LIST(L_TGEN, pfb_write_tgen);
    PFB_WRITE_LIST(L_FOG, pfb_write_fog);
    PFB_WRITE_LIST(L_CTAB, pfb_write_ctab);
    PFB_WRITE_LIST(L_LPSTATE, pfb_write_lpstate);
    PFB_WRITE_LIST(L_HLIGHT, pfb_write_hlight);
    PFB_WRITE_LIST(L_SHADOBJ, pfb_write_shader_object);
    PFB_WRITE_LIST(L_SHADPROG, pfb_write_shader_program);
    PFB_WRITE_LIST(L_GSTATE, pfb_write_gstate);
    PFB_WRITE_SLIST(L_APPEARANCE, pfb_write_appearance);
    PFB_WRITE_SLIST(L_ASDDATA, pfb_write_asddata);
    PFB_WRITE_LIST(L_MORPH, pfb_write_morph);
    PFB_WRITE_LIST(L_FRUST, pfb_write_frust);
    PFB_WRITE_LIST(L_LSOURCE, pfb_write_lsource);
    PFB_WRITE_LIST(L_GSET, pfb_write_gset);
    PFB_WRITE_SLIST(L_VARRAY_1, pfb_write_varray_1);
    PFB_WRITE_SLIST(L_VARRAY_2, pfb_write_varray_2);
    PFB_WRITE_SLIST(L_VARRAY_4, pfb_write_varray_4);
    PFB_WRITE_LIST(L_GARRAY, pfb_write_geoArray);
    PFB_WRITE_LIST(L_LODSTATE, pfb_write_lodstate);
    PFB_WRITE_LIST(L_FONT, pfb_write_font);
    PFB_WRITE_LIST(L_STRING, pfb_write_string);
    PFB_WRITE_LIST(L_MESH, pfb_write_mesh);
    PFB_WRITE_LIST(L_CURVE2D, pfb_write_curve2d_general);
    PFB_WRITE_LIST(L_DISCURVE2D, pfb_write_discurve2d);
    PFB_WRITE_LIST(L_SCALAR, pfb_write_scalar);
    PFB_WRITE_LIST(L_TOPO, pfb_write_topo);
    PFB_WRITE_SLIST(L_NODE, pfb_write_node);


    /*
     *  close the file
     */
    fclose(glb->ofp);
    /*
     *  free temporary memory used in creating the file.
     */
    free_store_data(glb);

    return(TRUE);
}


static void descend_node(pfNode *node, pfa_global_t *glb)
{
    int i, num;
    void *data;
    pfNodeTravFuncType pre, post;

    /* we need to descend the para surface before we add it to the list! */
    if(glb->disable_rep_save == 0 &&
       pfIsOfType(node, pfGetParaSurfaceClassType())) {
      if(find_in_list(L_NODE, node, glb) == -1)
	descend_parasurface((pfParaSurface *)node,glb);
    }

    if (add_to_list(L_NODE, node, glb))
	
    {
	if (pfIsOfType(node, pfGetGroupClassType()))
	{
	    if (pfIsOfType(node, pfGetLODClassType()))
	    {
		if (data = pfGetLODLODState((pfLOD *)node))
		    add_to_list(L_LODSTATE, data, glb);
	    }
	    else if (pfIsOfType(node, pfGetMorphClassType()))
		descend_morph((pfMorph *)node, glb);
	    else if (pfIsOfType(node, pfGetSceneClassType()))
	    {
		if (data = pfGetSceneGState((pfScene *)node))
		    descend_gstate(data, glb);
	    }
	    else if (pfIsOfType(node, pfGetFCSClassType()))
		descend_flux(pfGetFCSFlux((pfFCS *)node), glb);
	    else if (pfIsOfType(node, pfGetDoubleFCSClassType()))
		descend_flux(pfGetDoubleFCSFlux((pfDoubleFCS *)node), glb);

	    num = pfGetNumChildren((pfGroup *)node);

	    for (i = 0; i < num; i++)
		descend_node(pfGetChild((pfGroup *)node, i), glb);
	}
	else if (pfIsOfType(node, pfGetGeodeClassType()))
	{
	    if (pfIsOfType(node, pfGetBboardClassType()))
	    {
		if (data = pfGetBboardPosFlux((pfBillboard *)node))
		    descend_flux((pfFlux*)data, glb);

		if(pfIsOfType(node, pfGetIBRnodeClassType()))
		{
		    descend_ibr_tex(pfGetIBRnodeIBRtexture((pfIBRnode *)node),
				    glb);
		}

		num = pfGetNumGSets((pfGeode *)node);
	    }
	    else if (pfIsOfType(node, pfGetSubdivSurfaceClassType()))
	    {
		if(glb->subdivsurface_save_geosets)
		{
		    /* like a geode */
		    num = pfGetNumGSets((pfGeode *)node);
		}
		else
		{
		    descend_mesh(pfGetSubdivSurfaceMesh((pfSubdivSurface *)node),
				 glb);

		    num = 0; /* do not descent geosets */
		}
	    }
	    else if(glb->disable_rep_save == 0 && 
		    pfIsOfType(node, pfGetParaSurfaceClassType()))
	    {
	      /* don't descend here ... do it earlier so that
	       * all the elements which rely on it get placed
	       * in the list first.
	       */
	      num = pfGetNumGSets((pfGeode *)node);
	    }
	    else if(glb->disable_rep_save == 0 &&
		    pfIsOfType(node, pfGetCompCurve3dClassType()))
	    {
	      pfParaSurface *s = 
	        pfGetCompCurve3dParaSurface((pfCompositeCurve3d *)node);
	      pfCurve2d     *c = 
	        pfGetCompCurve3dCurve2d((pfCompositeCurve3d *)node);

	      if(s) add_to_list(L_NODE, s, glb);
	      /* XXX Alex -- or should we add 'em to L_NODE ? */
	      if(c) add_to_list(L_CURVE2D, s, glb);
	      num = pfGetNumGSets((pfGeode *)node);
	    }
	    else
		num = pfGetNumGSets((pfGeode *)node);

	    for (i = 0; i < num; i++)
		descend_gset(pfGetGSet((pfGeode *)node, i), glb);
	}
	else if (pfIsOfType(node, pfGetLPointClassType()))
	{
	    descend_gset(pfGetLPointGSet((pfLightPoint *)node), glb);
	}
	else if (pfIsOfType(node, pfGetLSourceClassType()))
	{
	    add_to_list(L_LSOURCE, node, glb);
	    if (data = pfGetLSourceAttr((pfLightSource *)node, PFLS_SHADOW_TEX))
		descend_tex(data, glb);
	    if (data = pfGetLSourceAttr((pfLightSource *)node, PFLS_PROJ_TEX))
		descend_tex(data, glb);
	    if (data = pfGetLSourceAttr((pfLightSource *)node, PFLS_PROJ_FOG))
		add_to_list(L_FOG, data, glb);
	    if (data = pfGetLSourceAttr((pfLightSource *)node, PFLS_PROJ_FRUST))
		add_to_list(L_FRUST, data, glb);
	}
	else if (pfIsOfType(node, pfGetTextClassType()))
	{
	    pfString *string;

	    num = pfGetNumStrings((pfText *)node);

	    for (i = 0; i < num; i++)
		if (string = pfGetString((pfText *)node, i))
		    descend_string(string, glb);
	}
	else if (pfIsOfType(node, pfGetASDClassType()))
	{
	    int bind, size, o_size, pv_size, o_sizeof, pv_sizeof;
	    void *attr, *o_attr, *pv_attr;
	    pfASDEvalFuncType efunc;

	    pfGetASDAttr((pfASD *)node, PFASD_LODS, &bind, &size, &attr);
	    if (attr)
		add_to_slist(L_ASDDATA, attr, size * (int)sizeof(pfASDLODRange),
			     glb);
	    pfGetASDAttr((pfASD *)node, PFASD_COORDS, &bind, &size, &attr);
	    if (attr)
		add_to_slist(L_ASDDATA, attr, size * (int)sizeof(pfASDVert), glb);
	    pfGetASDAttr((pfASD *)node, PFASD_FACES, &bind, &size, &attr);
	    if (attr)
		add_to_slist(L_ASDDATA, attr, size * (int)sizeof(pfASDFace), glb);

	    o_attr = pv_attr = NULL;
	    o_sizeof = 0;
	    pv_sizeof = 0;
	    pfGetASDAttr((pfASD *)node, PFASD_PER_VERTEX_ATTR, &bind, &size, &attr);
	    if (attr && size && bind)
	    {
		pv_attr = attr;
		pv_size = size;
		if (bind &PFASD_NORMALS)
		{
		    pv_sizeof += 6 * sizeof(float);
		}
		if(bind&PFASD_COLORS)
		{
		    pv_sizeof += 8 * sizeof(float);
		}
		if(bind&PFASD_TCOORDS)
		{
		    pv_sizeof += 4 * sizeof(float);
		}
	    }
	    pfGetASDAttr((pfASD *)node, PFASD_OVERALL_ATTR,  &bind, &size, &attr);
	    if (attr && size && bind)
	    {
		o_attr = attr;
		o_size = size;
		if (bind &PFASD_NORMALS)
		{
		    o_sizeof += 6 * sizeof(float);
		}
		if(bind&PFASD_COLORS)
		{
		    o_sizeof += 8 * sizeof(float);
		}
		if(bind&PFASD_TCOORDS)
		{
		    o_sizeof += 4 * sizeof(float);
		}
	    }

	    if (o_attr)
		add_to_slist(L_ASDDATA, o_attr, o_size * o_sizeof, glb);
	    if (pv_attr)
		add_to_slist(L_ASDDATA, pv_attr, pv_size * pv_sizeof, glb);

	    num = pfGetASDNumGStates((pfASD *)node);
	    for (i = 0; i < num; i++)
		if (data = pfGetASDGState((pfASD *)node, i))
		    descend_gstate(data, glb);

	    if (efunc = pfGetASDEvalFunc((pfASD *)node))
		descend_ufunc(efunc, "pfASD eval", glb);

#if 0
	    sg = pfGetASDSyncGroup((pfASD *)node);
	    add_to_list(L_SG_NAME, (void*)pfGetFluxSyncGroupName(sg), glb);
#endif
	}
	else
	{
	    /* XXX unknown node type */
	}

	pfGetNodeTravFuncs(node, PFTRAV_APP, &pre, &post);
	if (pre)
	    descend_ufunc(pre, "pfNode pre app trav", glb);
	if (post)
	    descend_ufunc(post, "pfNode post app trav", glb);
	pfGetNodeTravFuncs(node, PFTRAV_CULL, &pre, &post);
	if (pre)
	    descend_ufunc(pre, "pfNode pre cull trav", glb);
	if (post)
	    descend_ufunc(post, "pfNode post cull trav", glb);
	pfGetNodeTravFuncs(node, PFTRAV_DRAW, &pre, &post);
	if (pre)
	    descend_ufunc(pre, "pfNode pre draw trav", glb);
	if (post)
	    descend_ufunc(post, "pfNode post draw trav", glb);
	pfGetNodeTravFuncs(node, PFTRAV_ISECT, &pre, &post);
	if (pre)
	    descend_ufunc(pre, "pfNode pre isect trav", glb);
	if (post)
	    descend_ufunc(post, "pfNode post isect trav", glb);

	if (data = pfGetNodeTravData(node, PFTRAV_APP))
	    descend_udata(data, 0, node, glb);
	if (data = pfGetNodeTravData(node, PFTRAV_CULL))
	    descend_udata(data, 0, node, glb);
	if (data = pfGetNodeTravData(node, PFTRAV_DRAW))
	    descend_udata(data, 0, node, glb);
	if (data = pfGetNodeTravData(node, PFTRAV_ISECT))
	    descend_udata(data, 0, node, glb);
    }
}


static void descend_gset(pfGeoSet *gset, pfa_global_t *glb)
{
    /*
     * this could be a geoArray.
     */
    if(pfIsOfType(gset, pfGetGArrayClassType())){
	descend_geoArray((pfGeoArray*)gset, glb);
	return;
    }
       
	
    if (add_to_list(L_GSET, gset, glb))
    {
	pfGeoState *gstate;
	int lcount, *llist;
	int bind;
	int vcount, cncount;
	pfVec4 *clist;
	pfVec3 *nlist, *vlist;
	pfVec2 *tlist;
	ushort *ilist;
	int prim_type, pcount;
	int min, max;
	int i;
	pfHighlight *hlight;
	pfFlux *flux;
	islAppearance *appearance;

	prim_type = pfGetGSetPrimType(gset);
	pcount = pfGetGSetNumPrims(gset);

	if (llist = pfGetGSetPrimLengths(gset))
	{
	    for (i = 0, lcount = 0; i < pcount; i++)
		lcount += PF_ABS(llist[i]);
	    add_to_slist(L_LLIST, llist, pcount, glb);
	}

	switch (prim_type)
	{
	    case PFGS_POINTS:
		vcount = cncount = pcount;
		break;
	    case PFGS_LINES:
		vcount = cncount = pcount * 2;
		break;
	    case PFGS_TRIS:
		vcount = cncount = pcount * 3;
		break;
	    case PFGS_QUADS:
		vcount = cncount = pcount * 4;
		break;
	    case PFGS_LINESTRIPS:
	    case PFGS_TRISTRIPS:
	    case PFGS_TRIFANS:
	    case PFGS_POLYS:
		vcount = cncount = lcount;
		break;
	    case PFGS_FLAT_LINESTRIPS:
		vcount = lcount;
		cncount = lcount - pcount;
		break;
	    case PFGS_FLAT_TRISTRIPS:
	    case PFGS_FLAT_TRIFANS:
		vcount = lcount;
		cncount = lcount - pcount * 2;
		break;
	}

	/*
	 *  PFGS_COORD3
	 */
	pfGetGSetAttrLists(gset, PFGS_COORD3, (void **)&vlist, &ilist);
	if (vlist)
	{
	    if (ilist)
	    {
		pfGetGSetAttrRange(gset, PFGS_COORD3, &min, &max);
		add_to_slist(L_ILIST, ilist, vcount, glb);
		add_to_slist(L_VLIST, vlist, max+1, glb);
	    }
	    else
		add_to_slist(L_VLIST, vlist, vcount, glb);
	    if (flux = pfGetFlux(vlist))
		descend_flux(flux, glb);
	}

	/*
	 *  PFGS_COLOR4
	 */
	if ((bind = pfGetGSetAttrBind(gset, PFGS_COLOR4)) != PFGS_OFF)
	{
	    pfGetGSetAttrLists(gset, PFGS_COLOR4, (void **)&clist, &ilist);
	    if (ilist)
	    {
		pfGetGSetAttrRange(gset, PFGS_COLOR4, &min, &max);
		switch (bind)
		{
		    case PFGS_OVERALL:
			add_to_slist(L_ILIST, ilist, 1, glb);
			add_to_slist(L_CLIST, clist, max+1, glb);
			break;
		    case PFGS_PER_PRIM:
			add_to_slist(L_ILIST, ilist, pcount, glb);
			add_to_slist(L_CLIST, clist, max+1, glb);
			break;
		    case PFGS_PER_VERTEX:
			add_to_slist(L_ILIST, ilist, cncount, glb);
			add_to_slist(L_CLIST, clist, max+1, glb);
			break;
		}
	    }
	    else
	    {
		switch (bind)
		{
		    case PFGS_OVERALL:
			add_to_slist(L_CLIST, clist, 1, glb);
			break;
		    case PFGS_PER_PRIM:
			add_to_slist(L_CLIST, clist, pcount, glb);
			break;
		    case PFGS_PER_VERTEX:
			add_to_slist(L_CLIST, clist, cncount, glb);
			break;
		}
	    }
	    if (clist && (flux = pfGetFlux(clist)))
		descend_flux(flux, glb);
	}

	/*
	 *  PFGS_NORMAL3
	 */
	if ((bind = pfGetGSetAttrBind(gset, PFGS_NORMAL3)) != PFGS_OFF)
	{
	    pfGetGSetAttrLists(gset, PFGS_NORMAL3, (void **)&nlist, &ilist);
	    if (ilist)
	    {
		pfGetGSetAttrRange(gset, PFGS_NORMAL3, &min, &max);
		switch (bind)
		{
		    case PFGS_OVERALL:
			add_to_slist(L_ILIST, ilist, 1, glb);
			add_to_slist(L_NLIST, nlist, max+1, glb);
			break;
		    case PFGS_PER_PRIM:
			add_to_slist(L_ILIST, ilist, pcount, glb);
			add_to_slist(L_NLIST, nlist, max+1, glb);
			break;
		    case PFGS_PER_VERTEX:
			add_to_slist(L_ILIST, ilist, cncount, glb);
			add_to_slist(L_NLIST, nlist, max+1, glb);
			break;
		}
	    }
	    else
	    {
		switch (bind)
		{
		    case PFGS_OVERALL:
			add_to_slist(L_NLIST, nlist, 1, glb);
			break;
		    case PFGS_PER_PRIM:
			add_to_slist(L_NLIST, nlist, pcount, glb);
			break;
		    case PFGS_PER_VERTEX:
			add_to_slist(L_NLIST, nlist, cncount, glb);
			break;
		}
	    }
	    if (nlist && (flux = pfGetFlux(nlist)))
		descend_flux(flux, glb);
	}

	/*
	 *  PFGS_TEXCOORD2 - Do for all texture units.
	 */
        for (i = 0 ; i < glb->maxTextures ; i ++)
	{
	    if (pfGetGSetMultiAttrBind(gset, PFGS_TEXCOORD2, i) == 
							PFGS_PER_VERTEX) 
	    { 
		pfGetGSetMultiAttrLists(gset, PFGS_TEXCOORD2, i, 
					(void **)&tlist, &ilist);
		if (ilist)
		{
		    pfGetGSetMultiAttrRange(gset, PFGS_TEXCOORD2, i, 
						&min, &max);
		    add_to_slist(L_ILIST, ilist, vcount, glb);
		    add_to_slist(L_TLIST, tlist, max+1, glb);
		}
		else
		    add_to_slist(L_TLIST, tlist, vcount, glb);
		if (tlist && (flux = pfGetFlux(tlist)))
		    descend_flux(flux, glb);
	    }
	}

	if (gstate = pfGetGSetGState(gset))
	    descend_gstate(gstate, glb);

	if (hlight = pfGetGSetHlight(gset))
	    descend_hlight(hlight, glb);

	if (flux = pfGetGSetBBoxFlux(gset))
	    descend_flux(flux, glb);
	
	if(appearance = pfGetGSetAppearance(gset)) 
	  descend_appearance(appearance, glb);
    }
}

static void descend_geoArray(pfGeoArray *ga, pfa_global_t *glb)
{
	
    /* Handle geoArray much like a geoSet*/
    if (add_to_list(L_GARRAY, ga, glb))
    {
	pfGeoState *gstate;
	pfGeoSet *gset;
	
	int lcount, *llist;
	int vcount;
	int prim_type, pcount;
	int i,n;
	pfHighlight *hlight;
	pfFlux *flux;
	islAppearance *appearance;

	gset = (pfGeoSet*) ga;

	prim_type = pfGetGSetPrimType(gset);
	pcount = pfGetGSetNumPrims(gset);

	if (llist = pfGetGSetPrimLengths(gset))
	{
	    for (i = 0, lcount = 0; i < pcount; i++)
		lcount += PF_ABS(llist[i]);
	    add_to_slist(L_LLIST, llist, pcount, glb);
	}

	switch (prim_type)
	{
	    case PFGS_POINTS:
		vcount = pcount;
		break;
	    case PFGS_LINES:
		vcount = pcount * 2;
		break;
	    case PFGS_TRIS:
		vcount = pcount * 3;
		break;
	    case PFGS_QUADS:
		vcount = pcount * 4;
		break;
	    case PFGS_LINESTRIPS:
	    case PFGS_TRISTRIPS:
	    case PFGS_TRIFANS:
	    case PFGS_POLYS:
		vcount = lcount;
		break;
	    case PFGS_FLAT_LINESTRIPS:
		vcount = lcount;
		break;
	    case PFGS_FLAT_TRISTRIPS:
	    case PFGS_FLAT_TRIFANS:
		vcount = lcount;
		break;
	}


	/*
	 *  GeoSets dont use {vcnt}lists.
	 */

	{
	 int nva = pfGetGArrayNumAttrs(ga);

	 for(n=0; n<nva; n++){
	    int esize, msize, stride, acount;
	    unsigned int *ix;

	    char *ptr;
	    pfVertexAttr *va = pfGetGArrayNthAttr(ga, n);
	    
	    /* separate vertex arrays based on known element size so
	     * endian swizzles happen properly on loading.
	     */
	    msize = pfGetGArrayArrayMemSize(ga, va, &esize);
	    stride = pfGetVAttrStride(va);
	    /* compute actual count of arrays */
	    if(ix = pfGetGArrayIndexArray(ga)){
		acount = 0;
		for(i=0; i<vcount; i++) if(ix[i] > acount) acount = ix[i];
		acount++; /* count = max + 1 */
	    } else {
		acount = vcount;
	    }

	    ptr = (char *) pfGetVAttrPtr(va);
	    
	    if(esize == 1)
		add_to_slist(L_VARRAY_1, ptr, msize, glb);
	    else if(esize == 2)
		add_to_slist(L_VARRAY_2, ptr, msize/2, glb);
	    else
		add_to_slist(L_VARRAY_4, ptr, msize/4, glb);
	 }
	 if(pfGetGArrayIndexArray(ga))
	    add_to_slist(L_VARRAY_4, pfGetGArrayIndexArray(ga), vcount, glb);
	}
	
	/* ---- end vertex array dumper --- */

	if (gstate = pfGetGSetGState((pfGeoSet*)ga))
	    descend_gstate(gstate, glb);

	if (hlight = pfGetGSetHlight((pfGeoSet*)ga))
	    descend_hlight(hlight, glb);

	if (flux = pfGetGSetBBoxFlux((pfGeoSet*)ga))
	    descend_flux(flux, glb);
	
	if(appearance = pfGetGSetAppearance((pfGeoSet*)ga)) 
	  descend_appearance(appearance, glb);
    }
}

static void descend_gstate(pfGeoState *gstate, pfa_global_t *glb)
{
    int		i;

    if (add_to_list(L_GSTATE, gstate, glb))
    {
	uint64_t imask;
	void *data;

	imask = pfGetGStateInherit(gstate);

	if (!(imask & PFSTATE_FRONTMTL))
	{
	    data = pfGetGStateAttr(gstate, PFSTATE_FRONTMTL);
	    add_to_list(L_MTL, data, glb);
	}

	if (!(imask & PFSTATE_BACKMTL))
	{
	    data = pfGetGStateAttr(gstate, PFSTATE_BACKMTL);
	    add_to_list(L_MTL, data, glb);
	}

	if (!(imask & PFSTATE_LIGHTMODEL))
	{
	    data = pfGetGStateAttr(gstate, PFSTATE_LIGHTMODEL);
	    add_to_list(L_LMODEL, data, glb);
	}

	if (!(imask & PFSTATE_LIGHTS))
	{
	    int i;
	    pfLight **lights;

	    lights = pfGetGStateAttr(gstate, PFSTATE_LIGHTS);
	    for (i = 0; i < PF_MAX_LIGHTS; i++)
		if (lights[i])
		    add_to_list(L_LIGHT, lights[i], glb);
	}

	if (!(imask & PFSTATE_TEXTURE))
	{
	    for (i = 0 ; i < glb->maxTextures ; i ++)
	    {
	        data = pfGetGStateMultiAttr(gstate, PFSTATE_TEXTURE, i);
		if (data)
		    descend_tex(data, glb);
	    }
	}

	if (!(imask & PFSTATE_TEXENV))
	{
	    for (i = 0 ; i < glb->maxTextures ; i ++)
	    {
	        data = pfGetGStateMultiAttr(gstate, PFSTATE_TEXENV, i);
		if (data)
		    add_to_list(L_TENV, data, glb);
	    }
	}

	if (!(imask & PFSTATE_FOG))
	{
	    data = pfGetGStateAttr(gstate, PFSTATE_FOG);
	    add_to_list(L_FOG, data, glb);
	}

	if (!(imask & PFSTATE_TEXGEN))
	{
	    for (i = 0 ; i < glb->maxTextures ; i ++)
	    {
	        data = pfGetGStateMultiAttr(gstate, PFSTATE_TEXGEN, i);
		if (data)
		    add_to_list(L_TGEN, data, glb);
	    }
	}

	if (!(imask & PFSTATE_TEXLOD))
	{
	    for (i = 0 ; i < glb->maxTextures ; i ++)
	    {
	        data = pfGetGStateMultiAttr(gstate, PFSTATE_TEXLOD, i);
		if (data)
		    add_to_list(L_TLOD, data, glb);
	    }
	}

	if (!(imask & PFSTATE_VTXPROG))
	{
	    data = pfGetGStateAttr(gstate, PFSTATE_VTXPROG);
	    if (data)
	        add_to_list(L_GPROG, data, glb);
	}

	if (!(imask & PFSTATE_FRAGPROG))
	{
	    data = pfGetGStateAttr(gstate, PFSTATE_FRAGPROG);
	    if (data)
	        add_to_list(L_GPROG, data, glb);
	}

	if (!(imask & PFSTATE_GPROGPARMS))
	{
	    for(i=0; i<PF_N_GPPARMS; i++)
	        if(data = pfGetGStateMultiAttr(gstate, PFSTATE_GPROGPARMS, i))
		    add_to_list(L_GPROGPARM, data, glb);
	}

	if (!(imask & PFSTATE_COLORTABLE))
	{
	    data = pfGetGStateAttr(gstate, PFSTATE_COLORTABLE);
	    add_to_list(L_CTAB, data, glb);
	}

	if (!(imask & PFSTATE_HIGHLIGHT))
	{
	    descend_hlight(pfGetGStateAttr(gstate, PFSTATE_HIGHLIGHT), glb);
	}

	if (!(imask & PFSTATE_LPOINTSTATE))
	{
	    descend_lpstate(pfGetGStateAttr(gstate, PFSTATE_LPOINTSTATE), glb);
	}

	if (!(imask & PFSTATE_SHADPROG))
	{
	    void *obj = NULL;
	    int n;

	    data = pfGetGStateAttr(gstate, PFSTATE_SHADPROG);
	    if (data) {
	        add_to_list(L_SHADPROG, data, glb);
		n = pfGetSProgNumShaders((pfShaderProgram *)data);
		for(i=0; i<n; i++) {
		  obj = pfGetSProgShader((pfShaderProgram *)data, i);
		  if(obj)
		    add_to_list(L_SHADOBJ, obj, glb);
		}

	    }
	}

    }
}


static void descend_hlight(pfHighlight *hlight, pfa_global_t *glb)
{
    if (add_to_list(L_HLIGHT, hlight, glb))
    {
	void *data1;

	if (data1 = pfGetHlightGState(hlight))
	    descend_gstate(data1, glb);
	if (data1 = pfGetHlightTex(hlight))
	    descend_tex(data1, glb);
	if (data1 = pfGetHlightTEnv(hlight))
	    add_to_list(L_TENV, data1, glb);
	if (data1 = pfGetHlightTGen(hlight))
	    add_to_list(L_TGEN, data1, glb);
    }
}


static void descend_lpstate(pfLPointState *lpstate, pfa_global_t *glb)
{
    if (add_to_list(L_LPSTATE, lpstate, glb))
    {
	pfRasterFuncType rfunc;
	pfCalligFuncType cfunc;
	void *data;

	pfGetRasterFunc(lpstate, &rfunc, &data);
	if (rfunc)
	    descend_ufunc(rfunc, "pfLPointState raster", glb);
	if (data)
	    descend_udata(data, 0, lpstate, glb);
	pfGetCalligFunc(lpstate, &cfunc, &data);
	if (cfunc)
	    descend_ufunc(cfunc, "pfLPointState callig", glb);
	if (data)
	    descend_udata(data, 0, lpstate, glb);
    }
}


static void descend_tex(pfTexture *tex, pfa_global_t *glb)
{
    pfTexture *child_tex;
    const char *name;
    uint *image, *load_image;
    int comp, ns, nt, nr;
    pfList *tlist;
    int save_image, is_pfi;
    int save_image2[TEX_MAX_IMAGES];
    int i, count;
    int is_cliptexture;

    if (find_in_list(L_TEX, tex, glb) != -1)
	return;

    is_pfi = is_pfi_tex(tex);

    /*
     *  If we are not always saving images, see if we can find
     *  the texture file by it's name.
     */
    save_image = TRUE;
    name = pfGetTexName(tex);
    if (glb->save_texture_pfi == PF_ON)
    {
	if (name != NULL)
	    save_image = FALSE;
    }
    else if (glb->save_texture_image == PF_OFF)
    {
	if (name)
	{
	    char path[PF_MAXSTRING];

	    if (pfFindFile(name, path, R_OK))
		save_image = FALSE;	/* don't save the image */
	}
    }

    for(i=0; i<TEX_MAX_IMAGES; i++)
    {
	pfGetTexMultiImage(tex, &image, i, &comp, &ns, &nt, &nr);
	if(image)
	{
	    save_image2[i] = TRUE;

	    name = pfGetTexMultiName(tex, i);
	    if (glb->save_texture_pfi == PF_ON)
	    {
		if (name != NULL)
		    save_image2[i] = FALSE;
	    }
	    else if (glb->save_texture_image == PF_OFF)
	    {
		if (name)
		{
		    char path[PF_MAXSTRING];
		    
		    if (pfFindFile(name, path, R_OK))
			save_image2[i] = FALSE;	/* don't save the image */
		}
	    }
	}
    }

    if (is_cliptexture = pfIsOfType(tex, pfGetClipTextureClassType()))
    {
	child_tex = (pfTexture *)pfGetClipTextureMaster((pfClipTexture *)tex);
	if (child_tex)
	    descend_tex(child_tex, glb);
    }
    else
    {
	if (save_image)
	{
	    pfGetTexImage(tex, &image, &comp, &ns, &nt, &nr);
	    if (pfGetTexFormat(tex, PFTEX_EXTERNAL_FORMAT) == PFTEX_PACK_16)
		comp |= TWO_BYTE_COMPONENTS;
	    if (pfGetTexFormat(tex, PFTEX_EXTERNAL_FORMAT) == PFTEX_PACK_32)
		comp |= FOUR_BYTE_COMPONENTS;
	    comp |= ns << 8;

	    /*
	     *  save the texture load image if it exists
	     */
	    load_image = pfGetTexLoadImage(tex);
	    if (load_image)
		add_to_slist(L_IMAGE, load_image, comp, glb);

	    if (image)
		add_to_slist(L_IMAGE, image, comp, glb);
	}

	for(i=0; i<TEX_MAX_IMAGES; i++)
	    if (save_image2[i])
	    {
		pfGetTexMultiImage(tex, &image, i, &comp, &ns, &nt, &nr);
		if (pfGetTexFormat(tex, PFTEX_EXTERNAL_FORMAT) == 
		    PFTEX_PACK_16)
		    comp |= TWO_BYTE_COMPONENTS;
		if (pfGetTexFormat(tex, PFTEX_EXTERNAL_FORMAT) == 
		    PFTEX_PACK_32)
		    comp |= FOUR_BYTE_COMPONENTS;
		comp |= ns << 8;

		/*
		 *  no texture load image if exists for textures with multiple images
		 */
		
		if (image)
		    add_to_slist(L_IMAGE, image, comp, glb);

		save_image = TRUE; /* so that we descend texture levels */
	    }
	

	/*
	 *  Descend texture level textures.
	 */
	if (!(is_pfi || glb->save_texture_pfi == PF_ON) || save_image)
	    for (i = 1; i <= 32; i++)
	    {
		if (child_tex = pfGetTexLevel(tex, i))
		    descend_tex(child_tex, glb);
	    }
    }

    /*
     *  Descend detail texture.
     */
    if (child_tex = pfGetTexDetailTex(tex))
	descend_tex(child_tex, glb);

    /*
     *  Desend textures from pfTexList
     */
    if (tlist = pfGetTexList(tex))
    {
	/*
	 *  If the texture list is not from a multi image pfi, or
	 *  we are saving images.
	 */
	if (!(is_pfi & IS_PFI_MULTI) || save_image)
	{
	    count = pfGetNum(tlist);
	    for (i = 0; i < count; i++)
		descend_tex(pfGet(tlist, i), glb);
	}
    }

    /*
     *  Now that we have added all sub textures of this texture, add it
     *  to the texture list.
     */
    add_to_list(L_TEX, tex, glb);

    if (is_cliptexture)
    {
	pfObject *level_obj;

	/*
	 *  Descend cliptexture level objects.
	 */
	for (i = -32; i <= 32; i++)
	{
	    if (level_obj = pfGetClipTextureLevel((pfClipTexture *)tex, i))
	    {
		if (pfIsOfType(level_obj, pfGetImageCacheClassType()))
		    descend_icache((pfImageCache *)level_obj, glb);
		else
		    descend_itile((pfImageTile *)level_obj, glb);
	    }
	}
    }
}

static void descend_ibr_tex(pfIBRtexture *ibrtex, pfa_global_t *glb)
{
    int i, n;
    pfTexture **textures;

    if (find_in_list(L_IBR_TEX, ibrtex, glb) != -1)
	return;

    pfGetIBRtextureIBRTextures(ibrtex, &textures, &n);

    for(i=0; i<n; i++)
	descend_tex(textures[i], glb);

    add_to_list(L_IBR_TEX, ibrtex, glb);
}

/* recursive function because the composite scalar's components
 * may be composite scalars themselves!
 */
static void descend_scalar(pfScalar *scalar,pfa_global_t *glb)
{
  if(!scalar)
    return;

  if(pfIsOfType(scalar, pfGetCompositeScalarClassType())) {
    pfScalar *outF = NULL, *inF = NULL;
    
    outF = pfGetCompositeScalarOutF((pfCompositeScalar *)scalar);
    inF  = pfGetCompositeScalarInF ((pfCompositeScalar *)scalar);
    
    /* recurse ... */
    if(outF) descend_scalar(outF, glb);
    if(inF ) descend_scalar(inF , glb);
  }
  add_to_list(L_SCALAR, scalar, glb);
}

/* find all the pfScalars assoc with this surface */
static void descend_swept_surface(pfSweptSurface *surf,pfa_global_t *glb)
{
  pfCurve3d *crossSection = NULL, *path = NULL, *t = NULL, *b = NULL;
  pfScalar *s = (pfScalar *)pfGetSweptSurfaceProf(surf);

  crossSection = (pfCurve3d *)pfGetMemory(pfGetSweptSurfaceCrossSection(surf));
  path         = (pfCurve3d *)pfGetMemory(pfGetSweptSurfacePath(surf));
  t            = (pfCurve3d *)pfGetMemory(pfGetSweptSurfaceT(surf));
  b            = (pfCurve3d *)pfGetMemory(pfGetSweptSurfaceB(surf));

  if(s) 
    descend_scalar(s,glb);

  if(crossSection) descend_node((pfNode *)crossSection, glb);
  if(path)         descend_node((pfNode *)path, glb);

  /* don't store ptrs to t & b if pfFrenetSweptSurface */
  if(!pfIsOfType(surf, pfGetFrenetSweptSurfaceClassType())) {
    if(t)            descend_node((pfNode *)t, glb);
    if(b)            descend_node((pfNode *)b, glb);
  }
}

static void descend_coons_surface(pfCoonsSurface *coons,pfa_global_t *glb)
{
  pfCurve3d *r = NULL, *l = NULL, *b = NULL, *t = NULL;

  r = pfGetCoonsSurfaceRight(coons);
  l = pfGetCoonsSurfaceLeft(coons);
  b = pfGetCoonsSurfaceBottom(coons);
  t = pfGetCoonsSurfaceTop(coons);

  if(r) descend_node((pfNode *)r, glb);
  if(l) descend_node((pfNode *)l, glb);
  if(b) descend_node((pfNode *)b, glb);
  if(t) descend_node((pfNode *)t, glb);
}

static void descend_ruled_surface(pfRuledSurface *ruled,pfa_global_t *glb)
{
  pfCurve3d *c1 = NULL, *c2 = NULL;
  
  c1 = pfGetRuledSurfaceCurve1(ruled);
  c2 = pfGetRuledSurfaceCurve2(ruled);

  if(c1) descend_node((pfNode *)c1, glb);
  if(c2) descend_node((pfNode *)c2, glb);
}

/* Descend all the edges of a surface and all the topos.
 * Also take care of all scalars associated with swept surfaces,
 * curves for a coon surface, if applicable.
 */
static void descend_parasurface(pfParaSurface *surf,pfa_global_t *glb)
{	
  int i, j, n, m, solidId;
  pfTopo *topo;
  pfGeoState *geoState;

  n = pfGetParaSurfaceTrimLoopCount(surf);
  topo = (pfTopo *)pfGetMemory(pfGetParaSurfaceTopo(surf));

  for(i=0; i<n; i++) { /* for each trim loop */
    m = pfGetParaSurfaceTrimCurveCount(surf, i);

    for(j=0; j<m; j++) { /* for each trim curve in this trim loop */
      pfCurve2d *cc;
      pfDisCurve2d *dc;
      pfEdge *edge = pfGetParaSurfaceTrimCurve(surf, i, j);

      cc = pfGetEdgeContCurve(edge);
      dc = pfGetEdgeDisCurve(edge);

      if(cc)
	if(find_in_list(L_CURVE2D, cc, glb) == -1)
	  add_to_list(L_CURVE2D, cc, glb);

      if(dc && cc == NULL)
	if(find_in_list(L_DISCURVE2D, dc, glb) == -1)
	  add_to_list(L_DISCURVE2D, dc, glb);
    }
  }

  if(topo) {
    /* XXX Alex -- should we bother storing surfaces with topoId == 0 ? */
    if(/* pfGetTopoTopoId(topo) > 0 && */find_in_list(L_TOPO, topo, glb) == -1)
      add_to_list(L_TOPO, topo, glb);
    solidId = pfGetParaSurfaceSolidId(surf);
    if(solidId != -1) {
      pfSolid *solid = (pfSolid *)pfGetMemory(pfGetTopoSolid(topo, solidId));

      if(solid && find_in_list(L_SOLID, solid, glb) == -1)
	add_to_list(L_SOLID, solid, glb);
    }
  }

  geoState = pfGetParaSurfaceGState(surf);
  if(geoState)
    if(find_in_list(L_GSTATE, geoState, glb) == -1) {
      descend_gstate(geoState, glb);
      add_to_list(L_GSTATE, geoState, glb);
    }

  /* deal with pfScalars if needed */
  if(pfIsOfType(surf, pfGetSweptSurfaceClassType()))
    descend_swept_surface((pfSweptSurface *)surf, glb);
  else if(pfIsOfType(surf, pfGetCoonsSurfaceClassType())) /* or pfCoonsSurface surfaces */
    descend_coons_surface((pfCoonsSurface *)surf, glb);
  else if(pfIsOfType(surf, pfGetRuledSurfaceClassType()))
    descend_ruled_surface((pfRuledSurface *)surf, glb);
}

/* descend all geostates of a mesh */
static void descend_mesh(pfMesh *mesh, pfa_global_t *glb)
{
    int f, n;
    pfGeoState *lastGstate, *gstate;

    if (find_in_list(L_MESH, mesh, glb) != -1)
	return;

    n = pfGetMeshNumFaces(mesh);

    if(n == 0)
	return;

    lastGstate = pfGetMeshFaceGState( pfGetMeshFace(mesh, 0) );

    for(f = 1; f < n; f++)
    {
	gstate = pfGetMeshFaceGState( pfGetMeshFace(mesh, f) );
	
	if(gstate != lastGstate || f == n-1)
	{
	    descend_gstate(lastGstate, glb);
	    lastGstate = gstate;
	}
    }

    add_to_list(L_MESH, mesh, glb);
}

static void descend_icache(pfImageCache *icache, pfa_global_t *glb)
{
    void *data;
    pfQueue *queue;
    int i, count;
    pfImageCacheReadQueueFuncType rqfunc;
    pfTileFileNameFuncType tfnfunc;

    if (find_in_list(L_ICACHE, icache, glb) != -1)
	return;

    if (data = pfGetImageCacheMaster(icache))
	descend_icache(data, glb);

    if (data = pfGetImageCacheProtoTile(icache))
	descend_itile(data, glb);

    count = pfGetImageCacheNumStreamServers(icache, PFIMAGECACHE_S_DIMENSION);
    for (i = 0; i < count; i++)
    {
	queue = pfGetImageCacheStreamServerQueue(icache,
						 PFIMAGECACHE_S_DIMENSION, i);
	if (queue && queue != pfGetGlobalReadQueue())
	    add_to_list(L_QUEUE, queue, glb);
    }

    count = pfGetImageCacheNumStreamServers(icache, PFIMAGECACHE_T_DIMENSION);
    for (i = 0; i < count; i++)
    {
	queue = pfGetImageCacheStreamServerQueue(icache,
						 PFIMAGECACHE_T_DIMENSION, i);
	if (queue && queue != pfGetGlobalReadQueue())
	    add_to_list(L_QUEUE, queue, glb);
    }

    count = pfGetImageCacheNumStreamServers(icache, PFIMAGECACHE_R_DIMENSION);
    for (i = 0; i < count; i++)
    {
	queue = pfGetImageCacheStreamServerQueue(icache,
						 PFIMAGECACHE_R_DIMENSION, i);
	if (queue && queue != pfGetGlobalReadQueue())
	    add_to_list(L_QUEUE, queue, glb);
    }

    if (rqfunc = pfGetImageCacheReadQueueFunc(icache))
	descend_ufunc(rqfunc, "pfImageCache read queue", glb);

    if (tfnfunc = pfGetImageCacheTileFileNameFunc(icache))
	descend_ufunc(tfnfunc, "pfImageCache tile file name", glb);

    add_to_list(L_ICACHE, icache, glb);
}


static void descend_itile(pfImageTile *itile, pfa_global_t *glb)
{
    pfImageTile *default_itile;
    pfQueue *queue;
    pfReadImageTileFuncType rfunc;

    if (find_in_list(L_ITILE, itile, glb) != -1)
	return;

    if (default_itile = pfGetImageTileDefaultTile(itile))
	descend_itile(default_itile, glb);

    if ((queue = pfGetImageTileReadQueue(itile)) &&
	queue != pfGetGlobalReadQueue())
	add_to_list(L_QUEUE, queue, glb);

    if (rfunc = pfGetImageTileReadFunc(itile))
	descend_ufunc(rfunc, "pfImageTile read", glb);

    add_to_list(L_ITILE, itile, glb);
}


static void descend_string(pfString *string, pfa_global_t *glb)
{
    void *data;

    if (add_to_list(L_STRING, string, glb))
    {
	if (data = (void *)pfGetStringGState(string))
	    descend_gstate(data, glb);
	if (data = pfGetStringFont(string))
	    descend_font(data, glb);
    }
}


static void descend_font(pfFont *font, pfa_global_t *glb)
{
    pfGeoSet *gset;
    void *data;
    int i, num_chars;

    if (add_to_list(L_FONT, font, glb))
    {
	if (data = pfGetFontAttr(font, PFFONT_GSTATE))
	    descend_gstate(data, glb);
	num_chars = pfGetFontMode(font, PFFONT_NUM_CHARS);
	for (i = 0; i < num_chars; i++)
	    if (gset = pfGetFontCharGSet(font, i))
		descend_gset(gset, glb);
    }
}


static void descend_flux(pfFlux *flux, pfa_global_t *glb)
{
    if (add_to_list(L_FLUX, flux, glb))
    {
	int i, num;

	num = pfGetFluxNumSrcEngines(flux);
	for (i = 0; i < num; i++)
	    descend_engine(pfGetFluxSrcEngine(flux, i), glb);

	num = pfGetFluxNumClientEngines(flux);
	for (i = 0; i < num; i++)
	    descend_engine(pfGetFluxClientEngine(flux, i), glb);

	add_to_list(L_SG_NAME,
		    (void*)pfGetFluxSyncGroupName(pfGetFluxSyncGroup(flux)),
		    glb);
    }
}


static void descend_engine(pfEngine *engine, pfa_global_t *glb)
{
    if (add_to_list(L_ENGINE, engine, glb))
    {
	int i, num;
	void *data;
	ushort *ilist;
	int icount, offset, stride;
	pfEngineFuncType ufunc;

	num = pfGetEngineNumSrcs(engine);
	for (i = 0; i < num; i++)
	{
	    pfGetEngineSrc(engine, i, &data, &ilist, &icount, &offset, &stride);
	    if (data)
	    {
		if (pfIsOfType(data, pfGetFluxClassType()))
		{
		    if ((pfFlux*)data != pfGetFrameTimeFlux())
			descend_flux((pfFlux*)data, glb);
		}
		else
		    descend_unknown_data(data, glb);

		if (ilist)
		    add_to_slist(L_ILIST, ilist, icount, glb);
	    }
	}

	pfGetEngineDst(engine, &data, &ilist, &offset, &stride);
	if (data)
	{
	    if (pfIsOfType(data, pfGetFluxClassType()))
		descend_flux((pfFlux*)data, glb);
	    else
		descend_unknown_data(data, glb);

	    if (ilist)
	    {
		pfGetEngineIterations(engine, &icount, &num);
		add_to_slist(L_ILIST, ilist, icount, glb);
	    }
	}

	if (ufunc = pfGetEngineUserFunction(engine))
	    descend_ufunc(ufunc, "pfEngine user", glb);
    }
}


static void descend_ufunc(void *ufunc, const char *what, pfa_global_t *glb)
{
    if (pfdIsRegisteredUserFunc(ufunc))
	add_to_list(L_UFUNC, ufunc, glb);
    else
    {
	pfNotify(PFNFY_WARN, PFNFY_PRINT,
		 "%sFound unregistered %s function 0x%08x.",
		 "pfdStoreFile_pfb: ", what, ufunc);
	pfNotify(PFNFY_WARN, PFNFY_MORE,
		 "This function will not be saved.");
	pfNotify(PFNFY_WARN, PFNFY_MORE,
		 "To have user callback functions saved,");
	pfNotify(PFNFY_WARN, PFNFY_MORE,
		 "register them with pfdRegisterUserFunc.");
    }


    /*
     *  XXX This only saves user functions if they have been registered
     *      with pfdRegisterUserFunc().  It may be possible to
     *      get from a function pointer to a symbol name and dso.  If
     *      we can we should do that and auto register user functions
     *      that have not been registered.  This can be a future enhancement.
     */
}


static void descend_unknown_data(void *data, pfa_global_t *glb)
{
    if (glb->unknown_count == glb->unknown_size)
    {
	if (glb->unknown == NULL)
	{
	    glb->unknown_count = 0;
	    glb->unknown_size = 64;
	    glb->unknown = (void**)malloc(sizeof(void*) * 64);
	}
	else
	{
	    glb->unknown_size <<= 1;
	    glb->unknown = (void**)realloc(glb->unknown,
					 sizeof(void*) * glb->unknown_size);
	}
    }

    glb->unknown[glb->unknown_count] = data;
    glb->unknown_count++;
}


static void really_descend_unknown_datas(pfa_global_t *glb)
{
    int i;

    for (i = 0; i < glb->unknown_count; i++)
	if (find_data_list(glb->unknown[i], glb) == -1)
	    add_to_slist(L_FLIST, glb->unknown[i],
			 (int)pfGetSize(glb->unknown[i]) / (int)sizeof(float),
			 glb);

    if (glb->unknown)
	free(glb->unknown);
}


static void descend_udata_list(int count, void *parent, pfa_global_t *glb)
{
    void *udata;
    int i;

    if (glb->num_udata_slots > 1)
    {
	if (add_to_list(L_UDATA_LIST, parent, glb))
	{
	    if (count > glb->max_udata_slot)
		glb->max_udata_slot = count;

	    for (i = 0; i < count; i++)
		if (udata = pfGetUserDataSlot(parent, i))
		{
		    descend_udata(udata, i, parent, glb);
		    add_to_list(L_UDATA_NAME, (void*)pfGetUserDataSlotName(i),
				glb);
		}
	}
    }
    else
    {
	if (udata = pfGetUserDataSlot(parent, 0))
	    descend_udata(udata, 0, parent, glb);
    }
}


static void descend_udata(void *udata, int slot, void *parent,
			  pfa_global_t *glb)
{
    pfType *udata_type;
    int list;

    if (find_in_list(L_UDATA, udata, glb) != -1)
	return;

    list = -1;

    if ((udata_type = pfGetType(udata)) == NULL ||
	udata_type == pfGetMemoryClassType() ||
	(list = descend_unknown(udata, udata_type, glb)) == -1)
    {
	if (glb->udfunc && glb->udfunc[slot].descend)	/* Can user save it. */
	{
	    if (glb->udfunc[slot].descend(udata, parent, (void *)glb) != -1)
	    {
		add_to_list(L_UDATA, udata, glb);
		set_udata_info(UDT_USER_DEFINED, list, slot, parent, glb);
	    }
	    return;
	}

	if (udata_type == pfGetMemoryClassType())
	{
	    add_to_list(L_UDATA, udata, glb);
	    set_udata_info(UDT_MEMORY, list, slot, parent, glb);
	}

	return;
    }

    add_to_list(L_UDATA, udata, glb);
    set_udata_info(UDT_PF_OBJECT, list, slot, parent, glb);
}


static void set_udata_info(int type, int list, int slot, void *parent,
			   pfa_global_t *glb)
{
    udata_info_t *udi;

    if (glb->udi_size < glb->l_num[L_UDATA])
    {
	if (glb->udi_size == 0)
	    glb->udi_size = 16;
	else
	    glb->udi_size <<= 1;
	glb->udi = (udata_info_t *)realloc(glb->udi, glb->udi_size *
						     sizeof(udata_info_t));
    }

    udi = &glb->udi[glb->l_num[L_UDATA]-1];
    udi->type = type;
    udi->list = list;
    udi->slot = slot;
    udi->parent = parent;
}


static int descend_unknown(void *data, pfType *type, pfa_global_t *glb)
{
    if (pfIsDerivedFrom(type, pfGetNodeClassType()))
    {
	descend_node(data, glb);
	return(L_NODE);
    }
    else if (pfIsDerivedFrom(type, pfGetGSetClassType()))
    {
	descend_gset(data, glb);
	return(L_GSET);
    }
    else if (pfIsDerivedFrom(type, pfGetGStateClassType()))
    {
	descend_gstate(data, glb);
	return(L_GSTATE);
    }
    else if (pfIsDerivedFrom(type, pfGetTexClassType()))
    {
	descend_tex(data, glb);
	return(L_TEX);
    }
    else if (pfIsDerivedFrom(type, pfGetHlightClassType()))
    {
	descend_hlight(data, glb);
	return(L_HLIGHT);
    }
    else if (pfIsDerivedFrom(type, pfGetMorphClassType()))
    {
	descend_morph(data, glb);
	return(L_MORPH);
    }
    else if (pfIsDerivedFrom(type, pfGetStringClassType()))
    {
	descend_string(data, glb);
	return(L_STRING);
    }
    else if (pfIsDerivedFrom(type, pfGetFontClassType()))
    {
	descend_font(data, glb);
	return(L_FONT);
    }
    else if (pfIsDerivedFrom(type, pfGetMtlClassType()))
    {
	add_to_list(L_MTL, data, glb);
	return(L_MTL);
    }
    else if (pfIsDerivedFrom(type, pfGetTEnvClassType()))
    {
	add_to_list(L_TENV, data, glb);
	return(L_TENV);
    }
    else if (pfIsDerivedFrom(type, pfGetLODStateClassType()))
    {
	add_to_list(L_LODSTATE, data, glb);
	return(L_LODSTATE);
    }
    else if (pfIsDerivedFrom(type, pfGetFogClassType()))
    {
	add_to_list(L_FOG, data, glb);
	return(L_FOG);
    }
    else if (pfIsDerivedFrom(type, pfGetTGenClassType()))
    {
	add_to_list(L_TGEN, data, glb);
	return(L_TGEN);
    }
    else if (pfIsDerivedFrom(type, pfGetLModelClassType()))
    {
	add_to_list(L_LMODEL, data, glb);
	return(L_LMODEL);
    }
    else if (pfIsDerivedFrom(type, pfGetLightClassType()))
    {
	add_to_list(L_LIGHT, data, glb);
	return(L_LIGHT);
    }
    else if (pfIsDerivedFrom(type, pfGetCtabClassType()))
    {
	add_to_list(L_CTAB, data, glb);
	return(L_CTAB);
    }
    else if (pfIsDerivedFrom(type, pfGetLPStateClassType()))
    {
	add_to_list(L_LPSTATE, data, glb);
	return(L_LPSTATE);
    }
    else if (pfIsDerivedFrom(type, pfGetFrustClassType()))
    {
	add_to_list(L_FRUST, data, glb);
	return(L_FRUST);
    }
    else
	return(-1);		/* Whatever this is, we can not save it. */
}


static void descend_morph(pfMorph *morph, pfa_global_t *glb)
{
    morph_list_t *tmp;

    add_to_list(L_MORPH, morph, glb);

    /*
     *  add morph to the special morph list
     */
    tmp = (morph_list_t *)malloc(sizeof(morph_list_t));
    tmp->morph = morph;
    tmp->next = NULL;

    if (glb->morph_tail)
	glb->morph_tail->next = tmp;
    else
	glb->morph_head = tmp;

    glb->morph_tail = tmp;
}


static void really_descend_morphs(pfa_global_t *glb)
{
    morph_list_t *tmp;
    pfMorph *morph;
    list_t *ltmp;
    int i, j;
    int attr_count, src_count;
    void *dst;
    float *alist;
    ushort *ilist;
    int nlist;

    tmp = glb->morph_head;
    while (tmp)
    {
	morph = tmp->morph;
	attr_count = pfGetMorphNumAttrs(morph);
	tmp->attr_type = (int *)malloc(attr_count * sizeof(int));
	tmp->list_id = (int *)malloc(attr_count * sizeof(int));
	tmp->fpe = (int *)malloc(attr_count * sizeof(int));
	tmp->e = (int *)malloc(attr_count * sizeof(int));
	tmp->id = (int *)malloc(attr_count * sizeof(int));

	for (i = 0; i < attr_count; i++)
	{
	    dst = pfGetMorphDst(morph, i);
	    if (dst != NULL)
	    {
		if (pfIsOfType(dst, pfGetCBufferClassType()))
		    dst = pfGetCurCBufferData(dst);

		if (ltmp = find_list_item(L_VLIST, dst, glb))
		{
		    tmp->attr_type[i] = MORPH_ATTR_COORD3;
		    tmp->list_id[i] = L_VLIST;
		    tmp->fpe[i] = 3;
		    tmp->id[i] = ltmp->id;
		}
		else if (ltmp = find_list_item(L_NLIST, dst, glb))
		{
		    tmp->attr_type[i] = MORPH_ATTR_NORMAL3;
		    tmp->list_id[i] = L_NLIST;
		    tmp->fpe[i] = 3;
		    tmp->id[i] = ltmp->id;
		}
		else if (ltmp = find_list_item(L_TLIST, dst, glb))
		{
		    tmp->attr_type[i] = MORPH_ATTR_TEXCOORD2;
		    tmp->list_id[i] = L_TLIST;
		    tmp->fpe[i] = 2;
		    tmp->id[i] = ltmp->id;
		}
		else if (ltmp = find_list_item(L_CLIST, dst, glb))
		{
		    tmp->attr_type[i] = MORPH_ATTR_COLOR4;
		    tmp->list_id[i] = L_CLIST;
		    tmp->fpe[i] = 4;
		    tmp->id[i] = ltmp->id;
		}
		else
		{
		    tmp->attr_type[i] = MORPH_ATTR_OTHER;
		    tmp->list_id[i] = L_FLIST;
		    tmp->fpe[i] = 1;
		    tmp->e[i] = (int)(pfGetSize(alist) / sizeof(float));
		    if (ltmp = find_list_item(L_FLIST, dst, glb))
			tmp->id[i] = ltmp->id;
		    else
			tmp->id[i] = add_to_slist(L_FLIST, dst, tmp->e[i], glb);
		}

		src_count = pfGetMorphNumSrcs(morph, i);
		for (j = 0; j < src_count; j++)
		{
		    pfGetMorphSrc(morph, i, j, &alist, &ilist, &nlist);
		    tmp->e[i] = (int)(pfGetSize(alist) / sizeof(float) /
				      tmp->fpe[i]);
		    add_to_slist(tmp->list_id[i], alist, tmp->e[i], glb);
		    if (ilist)
			add_to_slist(L_ILIST, ilist, tmp->e[i], glb);
		}
	    }
	    else
		tmp->attr_type[i] = MORPH_ATTR_MISSING;
	}

	tmp = tmp->next;
    }
}


static morph_list_t *find_morph(pfMorph *morph, pfa_global_t *glb)
{
    morph_list_t *tmp;

    tmp = glb->morph_head;
    while (tmp)
    {
	if (tmp->morph == morph)
	    return(tmp);

	tmp = tmp->next;
    }

    return(NULL);
}


static void free_morph_list(pfa_global_t *glb)
{
    morph_list_t *tmp;

    while (glb->morph_head)
    {
	tmp = glb->morph_head;
	glb->morph_head = glb->morph_head->next;;
	free(tmp->attr_type);
	free(tmp->list_id);
	free(tmp->fpe);
	free(tmp->e);
	free(tmp);
    }
    glb->morph_tail = NULL;
}


static void pfa_write_mtl(pfMaterial *mtl, pfa_global_t *glb)
{
    int f_cm, b_cm;
    mtl_t m;

    m.side = find_in_table(mtls_table, pfGetMtlSide(mtl));
    m.alpha =  pfGetMtlAlpha(mtl);
    m.shininess =  pfGetMtlShininess(mtl);
    pfGetMtlColor(mtl, PFMTL_AMBIENT,
		  &m.ambient[0], &m.ambient[1], &m.ambient[2]);
    pfGetMtlColor(mtl, PFMTL_DIFFUSE,
		  &m.diffuse[0], &m.diffuse[1], &m.diffuse[2]);
    pfGetMtlColor(mtl, PFMTL_SPECULAR,
		  &m.specular[0], &m.specular[1], &m.specular[2]);
    pfGetMtlColor(mtl, PFMTL_EMISSION,
		  &m.emission[0], &m.emission[1], &m.emission[2]);
    f_cm = pfGetMtlColorMode(mtl, PFMTL_FRONT);
    b_cm = pfGetMtlColorMode(mtl, PFMTL_BACK);
    if (f_cm == b_cm)
    {
	m.cmode[0] = find_in_table(mtls_table, PFMTL_BOTH);
	m.cmode[1] = find_in_table(cmode_table, f_cm);
    }
    else
    {
	m.cmode[0] = find_in_table(mtls_table, PFMTL_FRONT);
	m.cmode[1] = find_in_table(cmode_table, f_cm);
    }
    m.udata = get_udata((pfObject*)mtl, glb);

    fprintf(glb->ofp, "%d\n", m.side);
    fprintf(glb->ofp, "%.9g\n", m.alpha);
    fprintf(glb->ofp, "%.9g\n", m.shininess);
    fprintf(glb->ofp, "%.9g %.9g %.9g\n",
	    m.ambient[0], m.ambient[1], m.ambient[2]);
    fprintf(glb->ofp, "%.9g %.9g %.9g\n",
	    m.diffuse[0], m.diffuse[1], m.diffuse[2]);
    fprintf(glb->ofp, "%.9g %.9g %.9g\n",
	    m.specular[0], m.specular[1], m.specular[2]);
    fprintf(glb->ofp, "%.9g %.9g %.9g\n",
	    m.emission[0], m.emission[1], m.emission[2]);
    fprintf(glb->ofp, "%d %d\n", m.cmode[0], m.cmode[1]);
    fprintf(glb->ofp, "%d\n", m.udata);
}


static void pfb_write_mtl(pfMaterial *mtl, pfa_global_t *glb)
{
    int f_cm, b_cm;
    mtl_t m;

    m.side = find_in_table(mtls_table, pfGetMtlSide(mtl));
    m.alpha =  pfGetMtlAlpha(mtl);
    m.shininess =  pfGetMtlShininess(mtl);
    pfGetMtlColor(mtl, PFMTL_AMBIENT,
		  &m.ambient[0], &m.ambient[1], &m.ambient[2]);
    pfGetMtlColor(mtl, PFMTL_DIFFUSE,
		  &m.diffuse[0], &m.diffuse[1], &m.diffuse[2]);
    pfGetMtlColor(mtl, PFMTL_SPECULAR,
		  &m.specular[0], &m.specular[1], &m.specular[2]);
    pfGetMtlColor(mtl, PFMTL_EMISSION,
		  &m.emission[0], &m.emission[1], &m.emission[2]);
    f_cm = pfGetMtlColorMode(mtl, PFMTL_FRONT);
    b_cm = pfGetMtlColorMode(mtl, PFMTL_BACK);
    if (f_cm == b_cm)
    {
	m.cmode[0] = find_in_table(mtls_table, PFMTL_BOTH);
	m.cmode[1] = find_in_table(cmode_table, f_cm);
    }
    else
    {
	m.cmode[0] = find_in_table(mtls_table, PFMTL_FRONT);
	m.cmode[1] = find_in_table(cmode_table, f_cm);
    }
    m.udata = get_udata((pfObject*)mtl, glb);

    fwrite(&m, sizeof(mtl_t), 1, glb->ofp);
}


static void pfa_write_lmodel(pfLightModel *lmodel, pfa_global_t *glb)
{
    lmodel_t lm;

    pfGetLModelAtten(lmodel, &lm.atten[0], &lm.atten[1], &lm.atten[2]);
    lm.local = find_in_table(oo_table, pfGetLModelLocal(lmodel));
    lm.twoside = find_in_table(oo_table, pfGetLModelTwoSide(lmodel));
    pfGetLModelAmbient(lmodel, &lm.ambient[0], &lm.ambient[1], &lm.ambient[2]);
    lm.udata = get_udata((pfObject*)lmodel, glb);

    fprintf(glb->ofp, "%.9g %.9g %.9g\n",
	    lm.atten[0], lm.atten[1], lm.atten[2]);
    fprintf(glb->ofp, "%d\n", lm.local);
    fprintf(glb->ofp, "%d\n", lm.twoside);
    fprintf(glb->ofp, "%.9g %.9g %.9g\n",
	    lm.ambient[0], lm.ambient[1], lm.ambient[2]);
    fprintf(glb->ofp, "%d\n", lm.udata);
}


static void pfb_write_lmodel(pfLightModel *lmodel, pfa_global_t *glb)
{
    lmodel_t lm;

    pfGetLModelAtten(lmodel, &lm.atten[0], &lm.atten[1], &lm.atten[2]);
    lm.local = find_in_table(oo_table, pfGetLModelLocal(lmodel));
    lm.twoside = find_in_table(oo_table, pfGetLModelTwoSide(lmodel));
    pfGetLModelAmbient(lmodel, &lm.ambient[0], &lm.ambient[1], &lm.ambient[2]);
    lm.udata = get_udata((pfObject*)lmodel, glb);

    fwrite(&lm, sizeof(lmodel_t), 1, glb->ofp);
}


static void pfa_write_light(pfLight *light, pfa_global_t *glb)
{
    light_t l;

    pfGetLightColor(light, PFLT_AMBIENT,
		    &l.ambient[0], &l.ambient[1], &l.ambient[2]);
    pfGetLightColor(light, PFLT_DIFFUSE,
		    &l.diffuse[0], &l.diffuse[1], &l.diffuse[2]);
    pfGetLightColor(light, PFLT_SPECULAR,
		    &l.specular[0], &l.specular[1], &l.specular[2]);
    pfGetLightAtten(light, &l.atten[0], &l.atten[1], &l.atten[2]);
    pfGetLightPos(light, &l.pos[0], &l.pos[1], &l.pos[2], &l.pos[3]);
    pfGetSpotLightDir(light, &l.sl_dir[0], &l.sl_dir[1], &l.sl_dir[2]);
    pfGetSpotLightCone(light, &l.sl_cone[0], &l.sl_cone[1]);
    l.on = pfIsLightOn(light);
    l.udata = get_udata((pfObject*)light, glb);

    fprintf(glb->ofp, "%.9g %.9g %.9g\n",
	    l.ambient[0], l.ambient[1], l.ambient[2]);
    fprintf(glb->ofp, "%.9g %.9g %.9g\n",
	    l.diffuse[0], l.diffuse[1], l.diffuse[2]);
    fprintf(glb->ofp, "%.9g %.9g %.9g\n",
	    l.specular[0], l.specular[1], l.specular[2]);
    fprintf(glb->ofp, "%.9g %.9g %.9g\n", l.atten[0], l.atten[1], l.atten[2]);
    fprintf(glb->ofp, "%.9g %.9g %.9g %.9g\n",
	    l.pos[0], l.pos[1], l.pos[2], l.pos[3]);
    fprintf(glb->ofp, "%.9g %.9g %.9g\n",
	    l.sl_dir[0], l.sl_dir[1], l.sl_dir[2]);
    fprintf(glb->ofp, "%.9g %.9g\n", l.sl_cone[0], l.sl_cone[1]);
    fprintf(glb->ofp, "%d\n", l.on);
    fprintf(glb->ofp, "%d\n", l.udata);
}


static void pfb_write_light(pfLight *light, pfa_global_t *glb)
{
    light_t l;

    pfGetLightColor(light, PFLT_AMBIENT,
		    &l.ambient[0], &l.ambient[1], &l.ambient[2]);
    pfGetLightColor(light, PFLT_DIFFUSE,
		    &l.diffuse[0], &l.diffuse[1], &l.diffuse[2]);
    pfGetLightColor(light, PFLT_SPECULAR,
		    &l.specular[0], &l.specular[1], &l.specular[2]);
    pfGetLightAtten(light, &l.atten[0], &l.atten[1], &l.atten[2]);
    pfGetLightPos(light, &l.pos[0], &l.pos[1], &l.pos[2], &l.pos[3]);
    pfGetSpotLightDir(light, &l.sl_dir[0], &l.sl_dir[1], &l.sl_dir[2]);
    pfGetSpotLightCone(light, &l.sl_cone[0], &l.sl_cone[1]);
    l.on = pfIsLightOn(light);
    l.udata = get_udata((pfObject*)light, glb);

    fwrite(&l, sizeof(light_t), 1, glb->ofp);
}


static void pfa_write_lsource(pfLightSource *lsource, pfa_global_t *glb)
{
    lsource_t l;
    int t1;
    void *data;

    pfGetLSourceColor(lsource, PFLT_AMBIENT,
		    &l.ambient[0], &l.ambient[1], &l.ambient[2]);
    pfGetLSourceColor(lsource, PFLT_DIFFUSE,
		    &l.diffuse[0], &l.diffuse[1], &l.diffuse[2]);
    pfGetLSourceColor(lsource, PFLT_SPECULAR,
		    &l.specular[0], &l.specular[1], &l.specular[2]);
    pfGetLSourceAtten(lsource, &l.atten[0], &l.atten[1], &l.atten[2]);
    pfGetLSourcePos(lsource, &l.pos[0], &l.pos[1], &l.pos[2], &l.pos[3]);
    pfGetSpotLSourceDir(lsource, &l.sl_dir[0], &l.sl_dir[1], &l.sl_dir[2]);
    pfGetSpotLSourceCone(lsource, &l.sl_cone[0], &l.sl_cone[1]);
    l.on = pfIsLSourceOn(lsource);
    t1 = pfGetLSourceMode(lsource, PFLS_SHADOW_ENABLE);
    l.shadow_enable = find_in_table(oo_table, t1);
    t1 = pfGetLSourceMode(lsource, PFLS_PROJTEX_ENABLE);
    l.projtex_enable = find_in_table(oo_table, t1);
    t1 = pfGetLSourceMode(lsource, PFLS_FREEZE_SHADOWS);
    l.freeze_shadows = find_in_table(oo_table, t1);
    t1 = pfGetLSourceMode(lsource, PFLS_FOG_ENABLE);
    l.fog_enable = find_in_table(oo_table, t1);
    l.shadow_displace_scale = pfGetLSourceVal(lsource,
					      PFLS_SHADOW_DISPLACE_SCALE);
    l.shadow_displace_offset = pfGetLSourceVal(lsource,
					       PFLS_SHADOW_DISPLACE_OFFSET);
    l.shadow_size = pfGetLSourceVal(lsource, PFLS_SHADOW_SIZE);
    l.intensity = pfGetLSourceVal(lsource, PFLS_INTENSITY);
    data = pfGetLSourceAttr(lsource, PFLS_SHADOW_TEX);
    l.shadow_tex = find_in_list(L_TEX, data, glb);
    data = pfGetLSourceAttr(lsource, PFLS_PROJ_TEX);
    l.proj_tex = find_in_list(L_TEX, data, glb);
    data = pfGetLSourceAttr(lsource, PFLS_PROJ_FOG);
    l.proj_fog = find_in_list(L_FOG, data, glb);
    data = pfGetLSourceAttr(lsource, PFLS_PROJ_FRUST);
    l.proj_frust = find_in_list(L_FRUST, data, glb);

    fprintf(glb->ofp, "%.9g %.9g %.9g\n",
	    l.ambient[0], l.ambient[1], l.ambient[2]);
    fprintf(glb->ofp, "%.9g %.9g %.9g\n",
	    l.diffuse[0], l.diffuse[1], l.diffuse[2]);
    fprintf(glb->ofp, "%.9g %.9g %.9g\n",
	    l.specular[0], l.specular[1], l.specular[2]);
    fprintf(glb->ofp, "%.9g %.9g %.9g\n", l.atten[0], l.atten[1], l.atten[2]);
    fprintf(glb->ofp, "%.9g %.9g %.9g %.9g\n",
	    l.pos[0], l.pos[1], l.pos[2], l.pos[3]);
    fprintf(glb->ofp, "%.9g %.9g %.9g\n",
	    l.sl_dir[0], l.sl_dir[1], l.sl_dir[2]);
    fprintf(glb->ofp, "%.9g %.9g\n", l.sl_cone[0], l.sl_cone[1]);
    fprintf(glb->ofp, "%d\n", l.on);
    fprintf(glb->ofp, "%d\n", l.shadow_enable);
    fprintf(glb->ofp, "%d\n", l.projtex_enable);
    fprintf(glb->ofp, "%d\n", l.freeze_shadows);
    fprintf(glb->ofp, "%d\n", l.fog_enable);
    fprintf(glb->ofp, "%.9g\n", l.shadow_displace_scale);
    fprintf(glb->ofp, "%.9g\n", l.shadow_displace_offset);
    fprintf(glb->ofp, "%.9g\n", l.shadow_size);
    fprintf(glb->ofp, "%.9g\n", l.intensity);
    fprintf(glb->ofp, "%d\n", l.shadow_tex);
    fprintf(glb->ofp, "%d\n", l.proj_tex);
    fprintf(glb->ofp, "%d\n", l.proj_fog);
    fprintf(glb->ofp, "%d\n", l.proj_frust);
}


static void pfb_write_lsource(pfLightSource *lsource, pfa_global_t *glb)
{
    lsource_t l;
    int t1;
    void *data;

    pfGetLSourceColor(lsource, PFLT_AMBIENT,
		    &l.ambient[0], &l.ambient[1], &l.ambient[2]);
    pfGetLSourceColor(lsource, PFLT_DIFFUSE,
		    &l.diffuse[0], &l.diffuse[1], &l.diffuse[2]);
    pfGetLSourceColor(lsource, PFLT_SPECULAR,
		    &l.specular[0], &l.specular[1], &l.specular[2]);
    pfGetLSourceAtten(lsource, &l.atten[0], &l.atten[1], &l.atten[2]);
    pfGetLSourcePos(lsource, &l.pos[0], &l.pos[1], &l.pos[2], &l.pos[3]);
    pfGetSpotLSourceDir(lsource, &l.sl_dir[0], &l.sl_dir[1], &l.sl_dir[2]);
    pfGetSpotLSourceCone(lsource, &l.sl_cone[0], &l.sl_cone[1]);
    l.on = pfIsLSourceOn(lsource);
    t1 = pfGetLSourceMode(lsource, PFLS_SHADOW_ENABLE);
    l.shadow_enable = find_in_table(oo_table, t1);
    t1 = pfGetLSourceMode(lsource, PFLS_PROJTEX_ENABLE);
    l.projtex_enable = find_in_table(oo_table, t1);
    t1 = pfGetLSourceMode(lsource, PFLS_FREEZE_SHADOWS);
    l.freeze_shadows = find_in_table(oo_table, t1);
    t1 = pfGetLSourceMode(lsource, PFLS_FOG_ENABLE);
    l.fog_enable = find_in_table(oo_table, t1);
    l.shadow_displace_scale = pfGetLSourceVal(lsource,
					      PFLS_SHADOW_DISPLACE_SCALE);
    l.shadow_displace_offset = pfGetLSourceVal(lsource,
					       PFLS_SHADOW_DISPLACE_OFFSET);
    l.shadow_size = pfGetLSourceVal(lsource, PFLS_SHADOW_SIZE);
    l.intensity = pfGetLSourceVal(lsource, PFLS_INTENSITY);
    data = pfGetLSourceAttr(lsource, PFLS_SHADOW_TEX);
    l.shadow_tex = find_in_list(L_TEX, data, glb);
    data = pfGetLSourceAttr(lsource, PFLS_PROJ_TEX);
    l.proj_tex = find_in_list(L_TEX, data, glb);
    data = pfGetLSourceAttr(lsource, PFLS_PROJ_FOG);
    l.proj_fog = find_in_list(L_FOG, data, glb);
    data = pfGetLSourceAttr(lsource, PFLS_PROJ_FRUST);
    l.proj_frust = find_in_list(L_FRUST, data, glb);

    fwrite(&l, sizeof(lsource_t), 1, glb->ofp);
}


static void pfa_write_frust(pfFrustum *frust, pfa_global_t *glb)
{
    frust_t f;
    pfVec3 ll, lr, ul, ur;

    pfGetFrustNearFar(frust, &f.nearPlane, &f.farPlane);
    pfGetFrustNear(frust, ll, lr, ul, ur);
    f.left = ll[0];
    f.right = ur[0];
    f.bottum = ll[2];
    f.top = ur[2];
    /*
     *  XXX Note:  Curently the .pfa/.pfb format does not support saving
     *  a frustum that has been transformed by pfOrthoXformFrust().  This
     *  is for the following reasons.
     *    - The Performer api does not provide a way to get back transform.
     *    - It is theoretically feasable to figure out the tansform from
     *      the eye point and the corners of the frustum, but is a lot of
     *      work.
     *    - Frustum currently only appear in the scene graph as part of
     *      a pfLightsource and in that usage pfOrthoXformFrust() does
     *      not make sense.
     */
    pfMakeIdentMat(f.mat);
    f.udata = get_udata((pfObject*)frust, glb);

    fprintf(glb->ofp, "%d\n", f.type);
    fprintf(glb->ofp, "%.9g %.9g %.9g %.9g %.9g %.9g\n",
	   f.left, f.right, f.bottum, f.top, f.nearPlane, f.farPlane);
    fprintf(glb->ofp, "%.9g %.9g %.9g %.9g\n",
	   f.mat[0][0], f.mat[0][1], f.mat[0][2], f.mat[0][3]);
    fprintf(glb->ofp, "%.9g %.9g %.9g %.9g\n",
	   f.mat[1][0], f.mat[1][1], f.mat[1][2], f.mat[1][3]);
    fprintf(glb->ofp, "%.9g %.9g %.9g %.9g\n",
	   f.mat[2][0], f.mat[2][1], f.mat[2][2], f.mat[2][3]);
    fprintf(glb->ofp, "%.9g %.9g %.9g %.9g\n",
	   f.mat[3][0], f.mat[3][1], f.mat[3][2], f.mat[3][3]);
    fprintf(glb->ofp, "%d\n", f.udata);
}


static void pfb_write_frust(pfFrustum *frust, pfa_global_t *glb)
{
    frust_t f;
    pfVec3 ll, lr, ul, ur;

    pfGetFrustNearFar(frust, &f.nearPlane, &f.farPlane);
    pfGetFrustNear(frust, ll, lr, ul, ur);
    f.left = ll[0];
    f.right = ur[0];
    f.bottum = ll[2];
    f.top = ur[2];
    /*
     *  XXX Note:  Curently the .pfa/.pfb format does not support saving
     *  a frustum that has been transformed by pfOrthoXformFrust().  This
     *  is for the following reasons.
     *    - The Performer api does not provide a way to get back transform.
     *    - It is theoretically feasable to figure out the tansform from
     *      the eye point and the corners of the frustum, but is a lot of
     *      work.
     *    - Frustum currently only appear in the scene graph as part of
     *      a pfLightsource and in that usage pfOrthoXformFrust() does
     *      not make sense.
     */
    pfMakeIdentMat(f.mat);
    f.udata = get_udata((pfObject*)frust, glb);

    fwrite(&f, sizeof(frust_t), 1, glb->ofp);
}


static int save_tex_image(pfTexture *tex, const char **name_ptr,
			  pfa_global_t *glb)
{
    const char *name;
    char *s, *buf;
    int found, save_image, save_pfi;

    /*
     *  First see if we can find a texture file by it's name.
     */
    if (name = pfGetTexName(tex))
    {
	buf = (char *)GET_BUF(PF_MAXSTRING);

	found = pfFindFile(name, buf, R_OK);

	if (glb->save_texture_pfi == PF_ON)
	{
	    save_image = 0;

	    if ((s = strrchr(name, '.')) == NULL)
	    {
		strcpy(buf, name);
		strcat(buf, ".pfi");
		save_pfi = TRUE;
	    }
	    else if (strcmp(s, ".pfi"))
	    {
		strcpy(buf, name);
		s = strrchr(buf, '.');
		strcpy(s, ".pfi");
		save_pfi = TRUE;
	    }
	    else if (!found)
	    {
		strcpy(buf, name);
		save_pfi = TRUE;
	    }
	    else
		save_pfi = FALSE;

	    if (glb->save_texture_path == PF_ON)
		name = buf;
	    else
	    {
		if (name = strrchr(buf, '/'))
		    name++;
		else
		    name = buf;
	    }

	    *name_ptr = name;

	    if (save_pfi)
		pfSaveTexFile(tex, name);
	}
	else if (glb->save_texture_image == PF_OFF && found)
	{
	    save_image = 0;

	    if (glb->save_texture_path == PF_ON)
		*name_ptr = name;
	    else
	    {
		if (s = strrchr(name, '/'))
		    s++;
		else
		    s = (char *)name;
		*name_ptr = s;
	    }
	}
	else
	{
	    save_image = 1;
	    *name_ptr = name;
	}
    }
    else
    {
	save_image = 1;
	*name_ptr = NULL;
    }

    return(save_image);
}


static int save_tex_multi_image(pfTexture *tex, const char **name_ptr,
				int imageIndex, pfa_global_t *glb)
{
    const char *name;
    char *s, *buf;
    int found, save_image, save_pfi;

    /*
     *  First see if we can find a texture file by it's name.
     */
    if (name = pfGetTexMultiName(tex, imageIndex))
    {
	buf = (char *)GET_BUF(PF_MAXSTRING);

	found = pfFindFile(name, buf, R_OK);

	if (glb->save_texture_pfi == PF_ON)
	{
	    save_image = 0;

	    if ((s = strrchr(name, '.')) == NULL)
	    {
		strcpy(buf, name);
		strcat(buf, ".pfi");
		save_pfi = TRUE;
	    }
	    else if (strcmp(s, ".pfi"))
	    {
		strcpy(buf, name);
		s = strrchr(buf, '.');
		strcpy(s, ".pfi");
		save_pfi = TRUE;
	    }
	    else if (!found)
	    {
		strcpy(buf, name);
		save_pfi = TRUE;
	    }
	    else
		save_pfi = FALSE;

	    if (glb->save_texture_path == PF_ON)
		name = buf;
	    else
	    {
		if (name = strrchr(buf, '/'))
		    name++;
		else
		    name = buf;
	    }

	    *name_ptr = name;

	    if (save_pfi)
		pfSaveMultiTexFile(tex, name, imageIndex);
	}
	else if (glb->save_texture_image == PF_OFF && found)
	{
	    save_image = 0;

	    if (glb->save_texture_path == PF_ON)
		*name_ptr = name;
	    else
	    {
		if (s = strrchr(name, '/'))
		    s++;
		else
		    s = (char *)name;
		*name_ptr = s;
	    }
	}
	else
	{
	    save_image = 1;
	    *name_ptr = name;
	}
    }
    else
    {
	save_image = 1;
	*name_ptr = NULL;
    }

    return(save_image);
}


static void pfa_write_tex(pfTexture *tex, pfa_global_t *glb)
{
    pfTexture *child_tex, *base_tex;
    tex_t t;
    const char *name;
    pfList *tlist;
    int *itlist;
    int ilevels[63][2];
    int is_pfi, save_image;
    int i, im;

    if (pfIsOfType(tex, pfGetClipTextureClassType()))
	t.type = TEXTYPE_CLIPTEXTURE;
    else
	t.type = TEXTYPE_TEXTURE;

    is_pfi = is_pfi_tex(tex);

    base_tex = tex;

    if (save_image = save_tex_image(tex, &name, glb))
    {
	uint *image;

	pfGetTexImage(tex, &image, &t.comp, &t.xsize, &t.ysize, &t.zsize);
	t.image = find_in_list(L_IMAGE, image, glb);
    }

    if(pfGetTexFormat(tex, PFTEX_CUBE_MAP))
    {
	save_image = TRUE;
	for(i=0; i<TEX_MAX_IMAGES; i++)
	    if (!save_tex_multi_image(tex, &name, i, glb))
		save_image = FALSE;
    }
    if(!save_image)
    {
	t.image = t.comp = t.xsize = t.ysize = t.zsize = -1;

	if (is_pfi & IS_PFI_MULTI)
	{
	    /*
	     *  If this texture is from a multi image pfi file,
	     *  we want to save the state of a texture in the texture list.
	     */
	    tlist = pfGetTexList(tex);
	    tex = pfGet(tlist, 0);
	}
    }

    pfa_write_name(name, glb);

    t.format[0] = find_in_table(txif_table,
				pfGetTexFormat(tex, PFTEX_INTERNAL_FORMAT));
    t.format[1] = find_in_table(txef_table,
				pfGetTexFormat(tex, PFTEX_EXTERNAL_FORMAT));
    t.format[2] = find_in_table(oo_table,
				pfGetTexFormat(tex, PFTEX_DETAIL_TEXTURE));
    t.format[3] = find_in_table(oo_table,
				pfGetTexFormat(tex, PFTEX_SUBLOAD_FORMAT));
    t.format[4] = find_in_table(oo_table,
				pfGetTexFormat(tex, PFTEX_FAST_DEFINE))
	| ((pfGetTexFormat(tex, PFTEX_CUBE_MAP) ? 1 : 0) << 2);
    t.filter[0] = to_table(txf_table, pfGetTexFilter(tex, PFTEX_MINFILTER));
    t.filter[1] = to_table(txf_table, pfGetTexFilter(tex, PFTEX_MAGFILTER));
    t.filter[2] = to_table(txf_table,
			   pfGetTexFilter(tex, PFTEX_MAGFILTER_ALPHA));
    t.filter[3] = to_table(txf_table,
			   pfGetTexFilter(tex, PFTEX_MAGFILTER_COLOR));
    t.wrap[0] = find_in_table(txr_table, pfGetTexRepeat(tex, PFTEX_WRAP_R));
    t.wrap[1] = find_in_table(txr_table, pfGetTexRepeat(tex, PFTEX_WRAP_S));
    t.wrap[2] = find_in_table(txr_table, pfGetTexRepeat(tex, PFTEX_WRAP_T));
    pfGetTexBorderColor(tex, &t.bcolor);
    t.btype = find_in_table(txb_table, pfGetTexBorderType(tex));
    pfGetTexSpline(tex, PFTEX_SHARPEN_SPLINE, t.ssp, &t.ssc);
    pfGetTexSpline(tex, PFTEX_DETAIL_SPLINE, t.dsp, &t.dsc);
#ifndef XXX_FIXED_TEX_SPLINE_BUG
    t.ssp[0][0] = 0.0f; t.ssp[0][1] = 0.0f;
    t.ssp[1][0] = 0.0f; t.ssp[1][1] = 0.0f;
    t.ssp[2][0] = 0.0f; t.ssp[2][1] = 0.0f;
    t.ssp[3][0] = 0.0f; t.ssp[3][1] = 0.0f;
    t.ssc = BROKEN_SPLINE;
    t.dsp[0][0] = 0.0f; t.dsp[0][1] = 0.0f;
    t.dsp[1][0] = 0.0f; t.dsp[1][1] = 0.0f;
    t.dsp[2][0] = 0.0f; t.dsp[2][1] = 0.0f;
    t.dsp[3][0] = 0.0f; t.dsp[3][1] = 0.0f;
    t.dsc = BROKEN_SPLINE;
#endif
    pfGetTexDetail(tex, &i, &child_tex);
    if (child_tex == NULL)
    {
	t.tdetail[0] = 0;
	t.tdetail[1] = -1;
    }
    else
    {
	t.tdetail[0] = i;
	t.tdetail[1] = find_in_list(L_TEX, child_tex, glb);
    }
    t.lmode[0] = find_in_table(txls_table,
			       pfGetTexLoadMode(tex, PFTEX_LOAD_SOURCE));
    t.lmode[1] = find_in_table(txlb_table,
			       pfGetTexLoadMode(tex, PFTEX_LOAD_BASE));
    t.lmode[2] = find_in_table(txll_table,
			       pfGetTexLoadMode(tex, PFTEX_LOAD_LIST));
    pfGetTexLoadOrigin(tex, PFTEX_ORIGIN_SOURCE,
		       &t.losource[0], &t.losource[1]);
    pfGetTexLoadOrigin(tex, PFTEX_ORIGIN_DEST, &t.lodest[0], &t.lodest[1]);
    pfGetTexLoadSize(tex, &t.lsize[0], &t.lsize[1]);
    t.load_image = find_in_list(L_IMAGE, pfGetTexLoadImage(tex), glb);
    t.frame = pfGetTexFrame(base_tex);
    if (tlist = pfGetTexList(tex))
    {
	t.list_size = pfGetNum(tlist);
	itlist = malloc(t.list_size * sizeof(int));
	for (i = 0; i < t.list_size; i++)
	    itlist[i] = find_in_list(L_TEX, pfGet(tlist, i), glb);
    }
    else
	t.list_size = 0;
    t.num_levels = 0;
    if (!pfGetTexFormat(tex, PFTEX_GEN_MIPMAP_FORMAT))
    {
	if (!(is_pfi || glb->save_texture_pfi == PF_ON) || save_image)
	    for (i = 1; i <= 32; i++)
	    {
		if (child_tex = pfGetTexLevel(tex, i))
		{
		    ilevels[t.num_levels][0] = i;
		    ilevels[t.num_levels][1] = find_in_list(L_TEX, child_tex,
							    glb);
		    t.num_levels++;
		}
	    }
    }
    t.udata = get_udata((pfObject*)base_tex, glb);
    t.aniso_degree = pfGetTexAnisotropy(tex);

    fprintf(glb->ofp, "%d\n", t.type);
    fprintf(glb->ofp, "%d %d %d %d %d\n",
	    t.format[0], t.format[1], t.format[2], t.format[3], t.format[4]);
    fprintf(glb->ofp, "0x%x 0x%x 0x%x 0x%x\n",
	    t.filter[0], t.filter[1], t.filter[2], t.filter[3]);
    fprintf(glb->ofp, "%d %d %d\n", t.wrap[0], t.wrap[1], t.wrap[2]);
    fprintf(glb->ofp, "%.9g %.9g %.9g %.9g\n",
	    t.bcolor[0], t.bcolor[1], t.bcolor[2], t.bcolor[3]);
    fprintf(glb->ofp, "%d\n", t.btype);
    fprintf(glb->ofp, "%.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g\n",
	    t.ssp[0][0], t.ssp[0][1], t.ssp[1][0], t.ssp[1][1],
	    t.ssp[2][0], t.ssp[2][1], t.ssp[3][0], t.ssp[3][1], t.ssc);
    fprintf(glb->ofp, "%.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g\n",
	    t.dsp[0][0], t.dsp[0][1], t.dsp[1][0], t.dsp[1][1],
	    t.dsp[2][0], t.dsp[2][1], t.dsp[3][0], t.dsp[3][1], t.dsc);
    fprintf(glb->ofp, "%d %d\n", t.tdetail[0], t.tdetail[1]);
    fprintf(glb->ofp, "%d %d %d\n", t.lmode[0], t.lmode[1], t.lmode[2]);
    fprintf(glb->ofp, "%d %d\n", t.losource[0], t.losource[1]);
    fprintf(glb->ofp, "%d %d\n", t.lodest[0], t.lodest[1]);
    fprintf(glb->ofp, "%d %d\n", t.lsize[0], t.lsize[1]);
    fprintf(glb->ofp, "%d %d %d %d %d\n",
	    t.image, t.comp, t.xsize, t.ysize, t.zsize);
    fprintf(glb->ofp, "%d\n", t.load_image);
    fprintf(glb->ofp, "%d %g\n", t.list_size, t.frame);
    if (t.list_size > 0)
    {
	for (i = 0; i < t.list_size; i++)
	    fprintf(glb->ofp, "%d\n", itlist[i]);
	free(itlist);
    }
    fprintf(glb->ofp, "%d\n", t.num_levels);
    for (i = 0; i < t.num_levels; i++)
	fprintf(glb->ofp, "%d %d\n", ilevels[i][0], ilevels[i][1]);
    fprintf(glb->ofp, "%d\n", t.aniso_degree);
    fprintf(glb->ofp, "%d\n", t.udata);

    if((t.format[4] >> 2) == 1)
    {
	i = TEX_MAX_IMAGES;
	fprintf(glb->ofp, "%d\n", i);

	for(i=0; i<TEX_MAX_IMAGES; i++)
	{
	    if (save_tex_multi_image(tex, &name, i, glb))
	    {
		uint *image;
	    
		pfGetTexMultiImage(tex, &image, i, 
				   &t.comp, &t.xsize, &t.ysize, &t.zsize);
		im = find_in_list(L_IMAGE, image, glb);
	    }
	    else
		im = -1;
	    fprintf(glb->ofp, "%d\n", im);
	    
	    pfa_write_name(name, glb);
	}
    }
}

static void pfb_write_tex(pfTexture *tex, pfa_global_t *glb)
{
    pfTexture *child_tex, *base_tex;
    tex_t t;
    const char *name;
    pfList *tlist;
    int *itlist;
    int ilevels[63][2];
    int is_pfi, save_image;
    int i;

    if (pfIsOfType(tex, pfGetClipTextureClassType()))
	t.type = TEXTYPE_CLIPTEXTURE;
    else
	t.type = TEXTYPE_TEXTURE;

    is_pfi = is_pfi_tex(tex);

    base_tex = tex;

    if (save_image = save_tex_image(tex, &name, glb))
    {
	uint *image;

	pfGetTexImage(tex, &image, &t.comp, &t.xsize, &t.ysize, &t.zsize);
	t.image = find_in_list(L_IMAGE, image, glb);
    }
    if(pfGetTexFormat(tex, PFTEX_CUBE_MAP))
    {
	save_image = TRUE;
	for(i=0; i<TEX_MAX_IMAGES; i++)
	    if (!save_tex_multi_image(tex, &name, i, glb))
		save_image = FALSE;
    }
    if(!save_image)
    {
	t.image = t.comp = t.xsize = t.ysize = t.zsize = -1;

	if (is_pfi & IS_PFI_MULTI)
	{
	    /*
	     *  If this texture is from a multi image pfi file,
	     *  we want to save the state of a texture in the texture list.
	     */
	    tlist = pfGetTexList(tex);
	    tex = pfGet(tlist, 0);
	}
    }

    pfb_write_name(name, glb);

    t.format[0] = find_in_table(txif_table,
				pfGetTexFormat(tex, PFTEX_INTERNAL_FORMAT));
    t.format[1] = find_in_table(txef_table,
				pfGetTexFormat(tex, PFTEX_EXTERNAL_FORMAT));
    t.format[2] = find_in_table(oo_table,
				pfGetTexFormat(tex, PFTEX_DETAIL_TEXTURE));
    t.format[3] = find_in_table(oo_table,
				pfGetTexFormat(tex, PFTEX_SUBLOAD_FORMAT));
    t.format[4] = find_in_table(oo_table,
				pfGetTexFormat(tex, PFTEX_FAST_DEFINE))
	| ((pfGetTexFormat(tex, PFTEX_CUBE_MAP) ? 1 : 0) << 2);
    t.filter[0] = to_table(txf_table, pfGetTexFilter(tex, PFTEX_MINFILTER));
    t.filter[1] = to_table(txf_table, pfGetTexFilter(tex, PFTEX_MAGFILTER));
    t.filter[2] = to_table(txf_table,
			   pfGetTexFilter(tex, PFTEX_MAGFILTER_ALPHA));
    t.filter[3] = to_table(txf_table,
			   pfGetTexFilter(tex, PFTEX_MAGFILTER_COLOR));
    t.wrap[0] = find_in_table(txr_table, pfGetTexRepeat(tex, PFTEX_WRAP_R));
    t.wrap[1] = find_in_table(txr_table, pfGetTexRepeat(tex, PFTEX_WRAP_S));
    t.wrap[2] = find_in_table(txr_table, pfGetTexRepeat(tex, PFTEX_WRAP_T));
    pfGetTexBorderColor(tex, &t.bcolor);
    t.btype = find_in_table(txb_table, pfGetTexBorderType(tex));
    pfGetTexSpline(tex, PFTEX_SHARPEN_SPLINE, t.ssp, &t.ssc);
    pfGetTexSpline(tex, PFTEX_DETAIL_SPLINE, t.dsp, &t.dsc);
#ifndef XXX_FIXED_TEX_SPLINE_BUG
    t.ssp[0][0] = 0.0f; t.ssp[0][1] = 0.0f;
    t.ssp[1][0] = 0.0f; t.ssp[1][1] = 0.0f;
    t.ssp[2][0] = 0.0f; t.ssp[2][1] = 0.0f;
    t.ssp[3][0] = 0.0f; t.ssp[3][1] = 0.0f;
    t.ssc = BROKEN_SPLINE;
    t.dsp[0][0] = 0.0f; t.dsp[0][1] = 0.0f;
    t.dsp[1][0] = 0.0f; t.dsp[1][1] = 0.0f;
    t.dsp[2][0] = 0.0f; t.dsp[2][1] = 0.0f;
    t.dsp[3][0] = 0.0f; t.dsp[3][1] = 0.0f;
    t.dsc = BROKEN_SPLINE;
#endif
    pfGetTexDetail(tex, &i, &child_tex);
    if (child_tex == NULL)
    {
	t.tdetail[0] = 0;
	t.tdetail[1] = -1;
    }
    else
    {
	t.tdetail[0] = i;
	t.tdetail[1] = find_in_list(L_TEX, child_tex, glb);
    }
    t.lmode[0] = find_in_table(txls_table,
			       pfGetTexLoadMode(tex, PFTEX_LOAD_SOURCE));
    t.lmode[1] = find_in_table(txlb_table,
			       pfGetTexLoadMode(tex, PFTEX_LOAD_BASE));
    t.lmode[2] = find_in_table(txll_table,
			       pfGetTexLoadMode(tex, PFTEX_LOAD_LIST));
    pfGetTexLoadOrigin(tex, PFTEX_ORIGIN_SOURCE,
		       &t.losource[0], &t.losource[1]);
    pfGetTexLoadOrigin(tex, PFTEX_ORIGIN_DEST, &t.lodest[0], &t.lodest[1]);
    pfGetTexLoadSize(tex, &t.lsize[0], &t.lsize[1]);
    t.load_image = find_in_list(L_IMAGE, pfGetTexLoadImage(tex), glb);
    t.frame = pfGetTexFrame(base_tex);
    if (tlist = pfGetTexList(tex))
    {
	t.list_size = pfGetNum(tlist);
	itlist = malloc(t.list_size * sizeof(int));
	for (i = 0; i < t.list_size; i++)
	    itlist[i] = find_in_list(L_TEX, pfGet(tlist, i), glb);
    }
    else
	t.list_size = 0;
    t.num_levels = 0;
    if (!pfGetTexFormat(tex, PFTEX_GEN_MIPMAP_FORMAT))
    {
	if (!(is_pfi || glb->save_texture_pfi == PF_ON) || save_image)
	    for (i = 1; i <= 32; i++)
	    {
		if (child_tex = pfGetTexLevel(tex, i))
		{
		    ilevels[t.num_levels][0] = i;
		    ilevels[t.num_levels][1] = find_in_list(L_TEX, child_tex,
							    glb);
		    t.num_levels++;
		}
	    }
    }
    t.udata = get_udata((pfObject*)base_tex, glb);
    t.aniso_degree = pfGetTexAnisotropy(tex);

    fwrite(&t, sizeof(tex_t), 1, glb->ofp);
    if (t.list_size > 0)
    {
	fwrite(itlist, sizeof(int), t.list_size, glb->ofp);
	free(itlist);
    }

	if (t.type == TEXTYPE_TEXTURE)
	{
		if (t.num_levels > 0)
		fwrite(ilevels, sizeof(int) * 2, t.num_levels, glb->ofp);
	}
	else
	if (t.type == TEXTYPE_CLIPTEXTURE)
	{
		cliptex_t	ct;
		pfTexture	*mtex;

		pfGetClipTextureCenter ((pfClipTexture*)tex, (int*)&(ct.center[0]), (int*)&(ct.center[1]), (int*)&(ct.center[2]));
		ct.clip_size = pfGetClipTextureClipSize ((pfClipTexture*)tex );
		pfGetClipTextureVirtualSize ((pfClipTexture*)tex, ct.virtual_size, ct.virtual_size + 1, ct.virtual_size + 2);
		ct.invalid_border = pfGetClipTextureInvalidBorder ((pfClipTexture*)tex);
		ct.virtual_LOD_offset = pfGetClipTextureVirtualLODOffset ((pfClipTexture*)tex);
		ct.effective_levels = pfGetClipTextureNumEffectiveLevels ((pfClipTexture*)tex);
		mtex = (pfTexture*)pfGetClipTextureMaster ((pfClipTexture*)tex);
		if (!mtex)
			ct.master = -1;
		else
			ct.master = find_in_list (L_TEX, mtex, glb);

		ct.DTR_mode = pfGetClipTextureDTRMode ((pfClipTexture*)tex);
		ct.DTR_max_i_border = 0;
		pfGetClipTextureLODRange ((pfClipTexture*)tex, ct.LOD_range, ct.LOD_range + 1);

		fwrite(&ct, sizeof ct, 1, glb->ofp);

		if (t.num_levels > 0)
		{
			cliplevel_t		cliplevels[100];
			pfObject		*obj;

			for (i = 0; i < t.num_levels; i ++)
			{
				cliplevels[i].level = i;
				obj = pfGetClipTextureLevel ((pfClipTexture*)tex, i);
				if (pfIsOfType (obj, pfGetImageCacheClassType()))
				{
					cliplevels[i].obj = find_in_list (L_ICACHE, obj, glb);
					cliplevels[i].type = L_ICACHE;
				}
				else
				{
					cliplevels[i].obj = find_in_list (L_ITILE, obj, glb);
					cliplevels[i].type = L_ITILE;
				}

				cliplevels[i].phase_margin = pfGetClipTextureLevelPhaseMargin ((pfClipTexture*)tex, i);
				pfGetClipTextureLevelPhaseShift ((pfClipTexture*)tex, i, cliplevels[i].phase_shift, cliplevels[i].phase_shift + 1, cliplevels[i].phase_shift + 2);

			}

			fwrite (cliplevels, sizeof (cliplevel_t), t.num_levels, glb->ofp);
		}
	}

    if((t.format[4] >> 2) == 1)
    {
	int  im;

	i = TEX_MAX_IMAGES;
	fwrite (&i, sizeof (int), 1, glb->ofp);

	for(i=0; i<TEX_MAX_IMAGES; i++)
	{
	    if (save_tex_multi_image(tex, &name, i, glb))
	    {
		uint *image;
	    
		pfGetTexMultiImage(tex, &image, i, 
				   &t.comp, &t.xsize, &t.ysize, &t.zsize);
		im = find_in_list(L_IMAGE, image, glb);
	    }
	    else
		im = -1;

	    fwrite (&im, sizeof (int), 1, glb->ofp);

	    pfb_write_name(name, glb);
	}
    }
}


static void pfa_write_ibr_tex(pfIBRtexture *tex, pfa_global_t *glb)
{
    pfTexture **textures;
    pfVec3    *dirs;
    int       i, numTex, numDirs;

    fprintf(glb->ofp, "%d\n", pfGetIBRtextureFlags(tex, 0xffffffff));
    fprintf(glb->ofp, "%g\n", pfGetIBRtextureDirection(tex));

    fprintf(glb->ofp, "0\n"); /* zero rings, we are storing directions */

    pfGetIBRtextureIBRdirections(tex, &dirs, &numDirs);
    fprintf(glb->ofp, "%d\n", numDirs); 

    for(i=0; i<numDirs; i++)
    {
        fprintf(glb->ofp, "%g %g %g\n", dirs[i][0], dirs[i][1], dirs[i][2]);
    }

    pfGetIBRtextureIBRTextures(tex, &textures, &numTex);
    fprintf(glb->ofp, "%d\n", numTex);
    for(i=0; i<numTex; i++)
        fprintf(glb->ofp, "%d\n", find_in_list(L_TEX, textures[i], glb));
}


static void pfb_write_ibr_tex(pfIBRtexture *tex, pfa_global_t *glb)
{
    pfTexture **textures;
    pfVec3    *dirs;
    int       i, numTex, numDirs;
    int buf_size;
    int *buf;

    pfGetIBRtextureIBRdirections(tex, &dirs, &numDirs);
    pfGetIBRtextureIBRTextures(tex, &textures, &numTex);
    buf_size = 4 + 3*numDirs + 1 + numTex;

    buf = GET_BUF(buf_size);
    buf_size = 0;

    buf[buf_size++] = pfGetIBRtextureFlags(tex, 0xffffffff);
    ((float *)buf)[buf_size++] = pfGetIBRtextureDirection(tex);

    buf[buf_size++] = 0; /* zero rings, we are storing directions */

    buf[buf_size++] = numDirs;

    for(i=0; i<numDirs; i++)
    {
	((float *)buf)[buf_size++] = dirs[i][0];
	((float *)buf)[buf_size++] = dirs[i][1];
	((float *)buf)[buf_size++] = dirs[i][2];
    }

    buf[buf_size++] = numTex;
    for(i=0; i<numTex; i++)
        buf[buf_size++] = find_in_list(L_TEX, textures[i], glb);

    fwrite(&buf_size, sizeof(int), 1, glb->ofp);
    fwrite(buf, sizeof(int), buf_size, glb->ofp);    
}

static void determineLengthOfMesh(pfMesh *mesh, int *lines, int *numbers)
{
    int    f, nf, v, nv, nnb;
    pfMeshFace *face;
    pfMeshVertex *vert;

    *lines = 0;
    *numbers = 0;

    /* length of mesh, length of vertex (without neighbors),
       length of vertex neighbor, length of face (without face vertices),
       length of face vertex */
    *lines += 1;
    *numbers += 1; /* length of mesh is stored anyway - as size of buffer */

    /* flags + bbox + bpshere + grid sizes + epsilon*/
    *lines += 1 + 1 + 1 + 1 + 1;
    *numbers += 1 + 6 + 4 + 3 + 1;

    /* number of vertices */
    *lines += 1;
    *numbers += 1;

    nv = pfGetMeshNumVertices(mesh);

    for(v = 0; v < nv; v++)
    {
	vert = pfGetMeshVertex(mesh, v);
	
	/* flags, coords, and next */
	*lines += 1 + 1 + 1;
	*numbers += 1 + 3 + 1;

	/* neighbors */
	*lines += 1;
	*numbers += 1;

	nnb = pfGetMeshVertexNumNeighbors(vert);

	*lines += nnb * 2; /* 3 values on 1-st line, 2 values on 2-nd line */
	*numbers += nnb * 5; /* 5 values */
    }

    /* number of faces */
    *lines += 1;
    *numbers += 1;

    nf = pfGetMeshNumFaces(mesh);

    for(f = 0; f < nf; f++)
    {
	face = pfGetMeshFace(mesh, f);

	/* flags, and part */
	*lines += 1;
	*numbers += 2;

	/* geostate */
	*lines += 1;
	*numbers += 1;

	/* vertices and texCoords */
	*lines += 1;
	*numbers += 1;

	nv = pfGetMeshFaceNumVerts(face);
	
	/* vertex, and texCoords times nv */
	*lines += nv;
	*numbers += nv * 3;
    }
}


static void pfb_write_curve2d(pfCurve2d *curve,pfa_global_t *glb,int *oldPos)
{
  pfb_write_rep((pfRep *)curve,glb,oldPos);

  ((float *)(glb->buf))[(*oldPos)++] = pfGetCurve2dBeginT(curve);
  ((float *)(glb->buf))[(*oldPos)++] = pfGetCurve2dEndT(curve);
  glb->buf[(*oldPos)++] = pfGetCurve2dClosed(curve);
  ((float *)(glb->buf))[(*oldPos)++] = pfGetCurve2dClosedTol(curve);
}

static void pfb_write_curve3d(pfCurve3d *curve,pfa_global_t *glb,int *oldPos)
{
  pfb_write_rep((pfRep *)curve,glb,oldPos);

  ((float *)(glb->buf))[(*oldPos)++] = pfGetCurve3dBeginT(curve);
  ((float *)(glb->buf))[(*oldPos)++] = pfGetCurve3dEndT(curve);
  glb->buf[(*oldPos)++] = pfGetCurve3dClosed(curve);
  ((float *)(glb->buf))[(*oldPos)++] = pfGetCurve3dClosedTol(curve);
}

static void pfa_write_curve2d(pfCurve2d *curve,pfa_global_t *glb)
{
  /* spew out rep related stuff first */
  pfa_write_rep((pfRep *)curve,glb);

  /* now spew out stuff specific to curve2d */
  fprintf(glb->ofp,"%g %g %d %g\n",
	  pfGetCurve2dBeginT(curve),
	  pfGetCurve2dEndT(curve),
	  pfGetCurve2dClosed(curve),
	  pfGetCurve2dClosedTol(curve));
}	

static void pfa_write_curve3d(pfCurve3d *curve,pfa_global_t *glb)
{
  /* spew out rep related stuff first */
  pfa_write_rep((pfRep *)curve,glb);

  /* now spew out stuff specific to curve3d */
  fprintf(glb->ofp,"%g %g %d %g\n",
	  pfGetCurve3dBeginT(curve),
	  pfGetCurve3dEndT(curve),
	  pfGetCurve3dClosed(curve),
	  pfGetCurve3dClosedTol(curve));
}	

static void pfb_write_nurb_curve2d(pfNurbCurve2d *nc,pfa_global_t *glb,
				   int *oldPos)
{
  int i,n, m;
  int *buf = glb->buf;

  /* write out curve info first: */
  pfb_write_curve2d((pfCurve2d *)nc,glb,oldPos);

  n = pfGetNurbCurve2dControlHullSize(nc);
  m = pfGetNurbCurve2dKnotCount(nc);

  buf[(*oldPos)++] = n;
  for(i=0; i<n; i++) {
    pfVec2 *v = pfGetNurbCurve2dControlHull(nc,i);
    pfReal  w = pfGetNurbCurve2dWeight(nc,i);

    ((float *)buf)[(*oldPos)++] = (*v)[0];
    ((float *)buf)[(*oldPos)++] = (*v)[1];
    ((float *)buf)[(*oldPos)++] = w;
  }
  
  buf[(*oldPos)++] = m;

  for(i=0; i<m; i++) 
    ((float *)buf)[(*oldPos)++] = pfGetNurbCurve2dKnot(nc,i);
}

static void pfb_write_nurb_curve3d(pfNurbCurve3d *nc,pfa_global_t *glb,
				   int *oldPos)
{
  int i,n, m;
  int *buf = glb->buf;

  /* write out curve info first: */
  pfb_write_curve3d((pfCurve3d *)nc,glb,oldPos);

  n = pfGetNurbCurve3dControlHullSize(nc);
  m = pfGetNurbCurve3dKnotCount(nc);

  buf[(*oldPos)++] = n;
  for(i=0; i<n; i++) {
    pfVec3 *v = pfGetNurbCurve3dControlHull(nc,i);
    pfReal  w = pfGetNurbCurve3dWeight(nc,i);

    ((float *)buf)[(*oldPos)++] = (*v)[0];
    ((float *)buf)[(*oldPos)++] = (*v)[1];
    ((float *)buf)[(*oldPos)++] = (*v)[2];
    ((float *)buf)[(*oldPos)++] = w;
  }
  
  buf[(*oldPos)++] = m;

  for(i=0; i<m; i++) 
    ((float *)buf)[(*oldPos)++] = pfGetNurbCurve3dKnot(nc,i);
}

static void pfa_write_nurb_curve2d(pfNurbCurve2d *nc,pfa_global_t *glb)
{
  int i,n;

  /* write out curve info first: */
  pfa_write_curve2d((pfCurve2d *)nc,glb);

  n = pfGetNurbCurve2dControlHullSize(nc);
  fprintf(glb->ofp,"%d # size of ctrl hull\n",n);
  for(i=0; i<n; i++) {
    pfVec2 *v = pfGetNurbCurve2dControlHull(nc,i);
    pfReal  w = pfGetNurbCurve2dWeight(nc,i);
    fprintf(glb->ofp,"%g %g %g\n",(*v)[0],(*v)[1],w);
  }
  
  n = pfGetNurbCurve2dKnotCount(nc);
  fprintf(glb->ofp,"%d # num knots\n",n);
  for(i=0; i<n; i++) 
    fprintf(glb->ofp,"%g\n", pfGetNurbCurve2dKnot(nc,i));
}

static void pfa_write_nurb_curve3d(pfNurbCurve3d *nc,pfa_global_t *glb)
{
  int i,n;

  /* write out curve info first: */
  pfa_write_curve3d((pfCurve3d *)nc,glb);

  n = pfGetNurbCurve3dControlHullSize(nc);
  fprintf(glb->ofp,"%d # size of ctrl hull\n",n);
  for(i=0; i<n; i++) {
    pfVec3 *v = pfGetNurbCurve3dControlHull(nc,i);
    pfReal  w = pfGetNurbCurve3dWeight(nc,i);
    fprintf(glb->ofp,"%g %g %g %g\n",(*v)[0],(*v)[1],(*v)[2],w);
  }
  
  n = pfGetNurbCurve3dKnotCount(nc);
  fprintf(glb->ofp,"%d # num knots\n",n);
  for(i=0; i<n; i++) 
    fprintf(glb->ofp,"%g\n", pfGetNurbCurve3dKnot(nc,i));
}

static void pfb_write_line2d(pfLine2d *line,pfa_global_t *glb,int *oldPos)
{
  line_t l;

  pfb_write_curve2d((pfCurve2d *)line, glb,oldPos);

  pfGetLine2dPoint1(line, &l.x0, &l.y0, &l.t0);
  pfGetLine2dPoint2(line, &l.x1, &l.y1, &l.t1);

  /* XXX Alex: or .. do a memcpy() and increment the oldPos counter by sizeof(line_t)/sizeof(int) */
  ((pfReal *)glb->buf)[(*oldPos)++] = l.x0;
  ((pfReal *)glb->buf)[(*oldPos)++] = l.y0;
  ((pfReal *)glb->buf)[(*oldPos)++] = l.t0;

  ((pfReal *)glb->buf)[(*oldPos)++] = l.x1;
  ((pfReal *)glb->buf)[(*oldPos)++] = l.y1;
  ((pfReal *)glb->buf)[(*oldPos)++] = l.t1;
}

static void pfa_write_line2d(pfLine2d *line,pfa_global_t *glb)
{
  pfReal x,y,t;

  pfa_write_curve2d((pfCurve2d *)line, glb);
  
  pfGetLine2dPoint1(line, &x, &y, &t);
  fprintf(glb->ofp,"%g %g %g\n",x,y,t);

  pfGetLine2dPoint2(line, &x, &y, &t);
  fprintf(glb->ofp,"%g %g %g\n",x,y,t);
}

static int pfb_read_line2d(pfa_global_t *glb,pfLine2d *line)
{
  int counter = 0;
  line_t l;

  counter += pfb_read_curve2d(glb, (pfCurve2d *)line);

  if( pfb_fread(&l, sizeof(line_t), 1, glb->ifp) ) {
    counter += sizeof(line_t) / sizeof(int);
    pfLine2dPoint1(line, l.x0, l.y0, l.t0);
    pfLine2dPoint2(line, l.x1, l.y1, l.t1);
  }

  return counter;
}

static void* pfa_read_line2d(pfa_global_t *glb,pfLine2d *line)
{
  pfReal x,y,t;

  pfa_read_curve2d(glb, (pfCurve2d *)line);

  READ_LINE();
  sscanf(glb->line_buf,"%g %g %g",&x,&y,&t);
  pfLine2dPoint1(line, x, y, t);

  READ_LINE();
  sscanf(glb->line_buf,"%g %g %g",&x,&y,&t);
  pfLine2dPoint2(line, x, y, t);
  
  return line;
}

static void pfa_write_super_quad_curve2d(pfSuperQuadCurve2d *c,
					 pfa_global_t *glb)
{
  pfReal radius, exponent;

  pfa_write_curve2d((pfCurve2d *)c, glb);

  radius = pfGetSuperQuadCurve2dRadius(c);
  exponent = pfGetSuperQuadCurve2dExponent(c);
  fprintf(glb->ofp,"%g %g # radius, exponent\n",radius,exponent);
}

static void pfb_write_super_quad_curve2d(pfSuperQuadCurve2d *c,
					 pfa_global_t *glb,
					 int *oldPos)
{
  pfb_write_curve2d((pfCurve2d *)c, glb, oldPos);

  ((float *)glb->buf)[(*oldPos)++] = pfGetSuperQuadCurve2dRadius(c);
  ((float *)glb->buf)[(*oldPos)++] = pfGetSuperQuadCurve2dExponent(c);
}

static int pfb_read_super_quad_curve2d(pfa_global_t *glb,
				       pfSuperQuadCurve2d *c)
{
  int counter = 0;
  pfReal radius, exponent;

  counter += pfb_read_curve2d(glb, (pfCurve2d *)c);
  counter += pfb_fread(&radius, sizeof(pfReal), 1, glb->ifp);
  counter += pfb_fread(&exponent, sizeof(pfReal), 1, glb->ifp);

  pfSuperQuadCurve2dRadius(c, radius);
  pfSuperQuadCurve2dExponent(c, exponent);

  return counter;
}

static void* pfa_read_super_quad_curve2d(pfa_global_t *glb,
					 pfSuperQuadCurve2d *c)
{
  pfReal radius, exponent;

  pfa_read_curve2d(glb, (pfCurve2d *)c);

  READ_LINE();
  sscanf(glb->line_buf,"%g %g",&radius,&exponent);

  pfSuperQuadCurve2dRadius(c, radius);
  pfSuperQuadCurve2dExponent(c, exponent);
  
  return c;
}

static void pfa_write_circle2d(pfCircle2d *circle,pfa_global_t *glb)
{
  pfReal radius;

  pfa_write_curve2d((pfCurve2d *)circle, glb);

  radius = pfGetCircle2dRadius(circle);
  fprintf(glb->ofp,"%g # radius\n",radius);
}

static void pfb_write_circle2d(pfCircle2d *circle,pfa_global_t *glb,
			       int *oldPos)
{
  pfb_write_curve2d((pfCurve2d *)circle, glb, oldPos);

  ((float *)glb->buf)[(*oldPos)++] = pfGetCircle2dRadius(circle);
}

static int pfb_read_circle2d(pfa_global_t *glb,pfCircle2d *circle)
{
  int counter = 0;
  float radius;

  counter += pfb_read_curve2d(glb, (pfCurve2d *)circle);
  if(pfb_fread(&radius, sizeof(float), 1, glb->ifp))
    counter++;

  pfCircle2dRadius(circle, radius);

  return counter;
}

static void* pfa_read_circle2d(pfa_global_t *glb,pfCircle2d *circle)
{
  pfReal radius;

  pfa_read_curve2d(glb, (pfCurve2d *)circle);

  READ_LINE();
  sscanf(glb->line_buf,"%g",&radius);
  pfCircle2dRadius(circle, radius);
  
  return circle;
}

static void pfa_write_piecewisepolycurve2d(pfPieceWisePolyCurve2d *curve,
					   pfa_global_t *glb)
{
  int patchCount, i, reverse, j, order;
  pfVec2 *vec;

  pfa_write_curve2d((pfCurve2d *)curve, glb);

  patchCount = pfGetPieceWisePolyCurve2dPatchCount(curve);
  fprintf(glb->ofp,"%d # patch count\n",patchCount);

  reverse = pfGetPieceWisePolyCurve2dReverse(curve);
  fprintf(glb->ofp,"%d # reverse flag\n",reverse);

  for(i=0; i<patchCount; i++) {
    vec = pfGetPieceWisePolyCurve2dLimitParasByAddr(curve,i);
    fprintf(glb->ofp,"%g %g # limit pars for patch %d\n",
	    (*vec)[0], (*vec)[1],i);
    order = pfGetPieceWisePolyCurve2dOrder(curve,i);
    fprintf(glb->ofp,"%d # order for patch %d\n",order,i);
    for(j=0; j<order; j++) {
      vec = pfGetPieceWisePolyCurve2dControlHullByAddr(curve,i,j);
      fprintf(glb->ofp,"%g %g\n",(*vec)[0],(*vec)[1]);
    }
  }
}

static void pfb_write_piecewisepolycurve2d(pfPieceWisePolyCurve2d *curve,
					   pfa_global_t *glb,int *oldPos)
{
  int patchCount, i, reverse, j, order, *buf = glb->buf;
  pfVec2 *vec;

  pfb_write_curve2d((pfCurve2d *)curve, glb, oldPos);

  patchCount = pfGetPieceWisePolyCurve2dPatchCount(curve);
  buf[(*oldPos)++] = patchCount;

  reverse = pfGetPieceWisePolyCurve2dReverse(curve);
  buf[(*oldPos)++] = reverse;

  for(i=0; i<patchCount; i++)
    buf[(*oldPos)++] = pfGetPieceWisePolyCurve2dOrder(curve,i);

  for(i=0; i<patchCount; i++) {
    vec = pfGetPieceWisePolyCurve2dLimitParasByAddr(curve,i);
    ((float *)buf)[(*oldPos)++] = (*vec)[0];
    ((float *)buf)[(*oldPos)++] = (*vec)[1];

    order = pfGetPieceWisePolyCurve2dOrder(curve,i);

    for(j=0; j<order; j++) {
      vec = pfGetPieceWisePolyCurve2dControlHullByAddr(curve,i,j);
      ((float *)buf)[(*oldPos)++] = (*vec)[0];
      ((float *)buf)[(*oldPos)++] = (*vec)[1];
    }
  }
}

static void pfb_write_curve2d_general(pfCurve2d *curve,pfa_global_t *glb)
{
  int buf_size = 0, *buf = glb->buf;
  int spaceNeededForCurve = sizeof(rep_t) / sizeof(int) + 
                            sizeof(curve2d_t) / sizeof(int) + 
                            1 + 1; /* id + size */
  
  if(pfIsOfType(curve,pfGetNurbCurve2dClassType())) {
    pfNurbCurve2d *nc = (pfNurbCurve2d *)curve;

    spaceNeededForCurve +=
      3 * pfGetNurbCurve2dControlHullSize(nc) +
          pfGetNurbCurve2dKnotCount(nc) + 
      2; /* for hullsize + knot count */

    buf = GET_BUF(spaceNeededForCurve);

    buf[buf_size++] = N_NURB_CURVE2D; /* | custom; */
    pfb_write_nurb_curve2d(nc,glb,&buf_size);
  } else if(pfIsOfType(curve,pfGetLine2dClassType())) {
    spaceNeededForCurve += 6;
    buf = GET_BUF(spaceNeededForCurve);
    buf[buf_size++] = N_LINE2D /*| custom */ ;
    pfb_write_line2d((pfLine2d *)curve,glb,&buf_size);
  } else if(pfIsOfType(curve,pfGetCircle2dClassType())) {
    spaceNeededForCurve += 1;
    buf = GET_BUF(spaceNeededForCurve);
    buf[buf_size++] = N_CIRCLE2D /*|custom */ ;
    pfb_write_circle2d((pfCircle2d *)curve,glb,&buf_size);
  } else if(pfIsOfType(curve,pfGetSuperQuadCurve2dClassType())) {
    spaceNeededForCurve += 2;
    buf = GET_BUF(spaceNeededForCurve);
    buf[buf_size++] = N_SUPERQUADCURVE2D /*|custom */;
    pfb_write_super_quad_curve2d((pfSuperQuadCurve2d *)curve,glb,&buf_size);
  } else if(pfIsOfType(curve,pfGetPieceWisePolyCurve2dClassType())) {
    int patchCount, order, i;
    pfPieceWisePolyCurve2d *ppCurve = (pfPieceWisePolyCurve2d *)curve;

    /* determine space needed for this curve */
    spaceNeededForCurve += 2; /* patch count + reverse flag */

    patchCount = pfGetPieceWisePolyCurve2dPatchCount(ppCurve);
    for(i=0; i<patchCount; i++) {
      order = pfGetPieceWisePolyCurve2dOrder(ppCurve,i);
      /* two additional spaces here for limit paras(2) and order */
      spaceNeededForCurve += 1 + 2 + order * 2;
    }

    buf = GET_BUF(spaceNeededForCurve);
    glb->buf[buf_size++] = N_PIECEWISEPOLYCURVE2D /* | custom; */ ;
    pfb_write_piecewisepolycurve2d(ppCurve,glb, &buf_size);
  } else {
    pfNotify(PFNFY_WARN,PFNFY_PRINT,
	     "PFB FILE WILL BE BOGUS: Unknown 2d curve type: %s\n",
	     pfGetTypeName(pfGetType(curve)));
  }

  PFASSERTDEBUG((spaceNeededForCurve-1) == buf_size);

  fwrite(&buf_size, sizeof(int), 1, glb->ofp);
  fwrite(buf, sizeof(int), buf_size, glb->ofp);
}

static void pfa_write_curve2d_general(pfCurve2d *curve,pfa_global_t *glb)
{
#if 0
  int node_type;

  READ_LINE();
  node_type = atoi(glb->line_buf);
  
  if (node_type & N_CUSTOM)
    node_type &= N_NOT_CUSTOM_MASK;
#endif

  if(pfIsOfType(curve,pfGetNurbCurve2dClassType())) {
    PFA_WRITE_ID(N_NURB_CURVE2D /* | custom*/, node_names);
    pfa_write_nurb_curve2d((pfNurbCurve2d *)curve,glb);
  } else if(pfIsOfType(curve,pfGetLine2dClassType())) {
    PFA_WRITE_ID(N_LINE2D, node_names);
    pfa_write_line2d((pfLine2d *)curve,glb);
  } else if(pfIsOfType(curve,pfGetCircle2dClassType())) {
    PFA_WRITE_ID(N_CIRCLE2D, node_names);
    pfa_write_circle2d((pfCircle2d *)curve,glb);
  } else if(pfIsOfType(curve,pfGetPieceWisePolyCurve2dClassType())) {
    PFA_WRITE_ID(N_PIECEWISEPOLYCURVE2D, node_names);
    pfa_write_piecewisepolycurve2d((pfPieceWisePolyCurve2d *)curve,glb);
  } else if(pfIsOfType(curve,pfGetSuperQuadCurve2dClassType())) {
    PFA_WRITE_ID(N_SUPERQUADCURVE2D, node_names);
    pfa_write_super_quad_curve2d((pfSuperQuadCurve2d *)curve,glb);
  } else {
    pfNotify(PFNFY_WARN,PFNFY_PRINT,
	     "PFA FILE WILL BE BOGUS: unknown curve type %s",
	     pfGetTypeName(pfGetType(curve)));
  }
}

static void pfb_write_discurve2d(pfDisCurve2d *curve,pfa_global_t *glb)
{
  pfVec2 *v;
  int i, n , closed, *buf = glb->buf, buf_size = 0, spaceNeededForCurve;

  n = pfGetDisCurve2dPointCount(curve),
  closed = pfGetDisCurve2dClosed(curve);

  spaceNeededForCurve = sizeof(rep_t)/sizeof(int) + 2 + 2*n;

  buf = GET_BUF(spaceNeededForCurve + 1);

  buf[buf_size++] = spaceNeededForCurve;
  pfb_write_rep((pfRep *)curve, glb, &buf_size);  


  buf[buf_size++] = n;
  buf[buf_size++] = closed;

  for(i=0; i<n; i++) {
    v = pfGetDisCurve2dPointByAddr(curve,i);
    ((float *)buf)[buf_size++] = (*v)[0];
    ((float *)buf)[buf_size++] = (*v)[1];
  }

  PFASSERTDEBUG(buf_size == (spaceNeededForCurve+1));
  
  fwrite(buf, sizeof(int), buf_size, glb->ofp);
}

static void pfa_write_discurve2d(pfDisCurve2d *curve,pfa_global_t *glb)
{
  int i, n = pfGetDisCurve2dPointCount(curve);
  
  fprintf(glb->ofp,"%d %d# num pts in dis curve 2d, closed flag\n",n,
	  pfGetDisCurve2dClosed(curve));
  for(i=0; i<n; i++) {
    pfVec2 *v = pfGetDisCurve2dPointByAddr(curve,i);
    fprintf(glb->ofp,"%g %g\n",(*v)[0], (*v)[1]);
  }
}

static void pfb_write_scalar(pfScalar *scalar,pfa_global_t *glb)
{
  int *buff = glb->buf, buf_size = 0;
  int size = 2;

  if(pfIsOfType(scalar, pfGetPolyScalarClassType())) {
    size += pfGetPolyScalarDegree((pfPolyScalar *)scalar) + 1 + 1; 
  } else if(pfIsOfType(scalar, pfGetCompositeScalarClassType())) {
    size += 2;
  }

  buff = GET_BUF(size);

  buff[buf_size++] = size;

  if(pfIsOfType(scalar, pfGetCompositeScalarClassType())) {
    pfCompositeScalar *comp = (pfCompositeScalar *)scalar;

    buff[buf_size++] = S_COMPOSITE;
    buff[buf_size++] = find_in_list(L_SCALAR, pfGetCompositeScalarOutF(comp), glb);
    buff[buf_size++] = find_in_list(L_SCALAR, pfGetCompositeScalarInF(comp), glb);
  } else if(pfIsOfType(scalar, pfGetCosScalarClassType())) {
    buff[buf_size++] = S_COS;
  } else if(pfIsOfType(scalar, pfGetSinScalarClassType())) {
    buff[buf_size++] = S_SIN;
  } else if(pfIsOfType(scalar, pfGetPolyScalarClassType())) {
    pfPolyScalar *poly = (pfPolyScalar *)scalar;
    int i, degree = pfGetPolyScalarDegree(poly);

    buff[buf_size++] = S_POLY;
    buff[buf_size++] = degree;
    for(i = 0; i<degree+1; i++) 
      ((float *)buff)[buf_size++] = pfGetPolyScalarCoef(poly, i);
  } else {
    pfNotify(PFNFY_WARN,PFNFY_PRINT,
	     "Don't know how to save scalar of type %s\n",
	     pfGetTypeName(pfGetType(scalar)));
  }

  fwrite(buff, sizeof(int), buf_size, glb->ofp);
}

static void pfa_write_scalar(pfScalar *scalar,pfa_global_t *glb)
{
  int nLines = 1; /* we can write out scalar type and size in one line
		   * and often enough, that's all we need ... */

  if(pfIsOfType(scalar, pfGetPolyScalarClassType())) {
    nLines += pfGetPolyScalarDegree((pfPolyScalar *)scalar) + 1;
  } else if(pfIsOfType(scalar, pfGetCompositeScalarClassType())) {
    nLines += 1;
  }

  if(pfIsOfType(scalar, pfGetCompositeScalarClassType())) {
    pfCompositeScalar *comp = (pfCompositeScalar *)scalar;

    fprintf(glb->ofp,"%d %d # %s\n%d %d # outF and inF\n",
	    S_COMPOSITE, nLines, scalar_names[S_COMPOSITE],
	    find_in_list(L_SCALAR, pfGetCompositeScalarOutF(comp), glb),
	    find_in_list(L_SCALAR, pfGetCompositeScalarInF(comp), glb));
  } else if(pfIsOfType(scalar, pfGetCosScalarClassType())) {
    fprintf(glb->ofp,"%d %d # %s\n",S_COS, nLines, scalar_names[S_COS]);
  } else if(pfIsOfType(scalar, pfGetSinScalarClassType())) {
    fprintf(glb->ofp,"%d %d # %s\n",S_SIN, nLines, scalar_names[S_SIN]);
  } else if(pfIsOfType(scalar, pfGetPolyScalarClassType())) {
    pfPolyScalar *poly = (pfPolyScalar *)scalar;
    int i, degree = pfGetPolyScalarDegree(poly);

    fprintf(glb->ofp,"%d %d # %s\n%d # degree\n", 
	    S_POLY, nLines, scalar_names[S_POLY], degree);
    for(i = 0; i<degree+1; i++)
      fprintf(glb->ofp,"%g\n", pfGetPolyScalarCoef(poly, i));
  } else {
    /* else we don't know how to write this scalar type ... */
    pfNotify(PFNFY_WARN,PFNFY_PRINT,
	     "Don't know how to save scalar of type %s\n",
	     pfGetTypeName(pfGetType(scalar)));
  }
}

static void pfb_write_topo(pfTopo *topo,pfa_global_t *glb)
{
  int buf_size = 0;
  int i, j, numRealBoundaries = 0;
  int numBoundaries = pfGetTopoBoundaryCount(topo);
  int totWingCount = 0;
  int* wingCounts = (int *)malloc(sizeof(int) * numBoundaries);
  pfBoundary **boundaries = (pfBoundary **)malloc(sizeof(pfBoundary*) *
						  numBoundaries);
  int solidCount = 0;
  list_t *lp = NULL;

  if(boundaries == NULL || wingCounts == NULL) {
    if(boundaries) free(boundaries);
    if(wingCounts) free(wingCounts);
    numBoundaries = 0; /* and spew warning ... */
  }

  for(i=0; i<numBoundaries; i++) {
    int effectiveWingCount = 0;
    pfBoundary *b =  pfGetTopoBoundary(topo, i);
    int wingCount = b ? pfGetBoundaryWingCount(b) : 1;

    if(wingCount == 1)
      continue; /* don't worry about this free boundary */

    /* only store boundary if at least two surfaces in wing are being stored */
    for(j=0; j<wingCount; j++) {
      int surfId = pfGetBoundarySurface(b, j);
      pfParaSurface *surf = pfGetTopoSurface(topo, surfId);
      if(!surf)
	if(wingCount > 2)
	  continue;
	else /* one of two surfaces in boundary not found */
	  break;
      if(find_in_list(L_NODE, surf, glb) != -1)
	effectiveWingCount++;
    }

    if(effectiveWingCount > 1) {
      boundaries[numRealBoundaries] = b;
      wingCounts[numRealBoundaries] = effectiveWingCount;
      numRealBoundaries++;
      totWingCount += effectiveWingCount;
      /* see comment about why this is necessary in pfa_write_topo() */
      if(glb->version >= PFBV_SURFACE_CLONING)
	add_boundary_to_list(glb, b);
      else
	add_to_list(L_BOUNDARY, b, glb);
    }
    /* else don't store this boundary */
  }

  /* now write solids */
  lp = glb->l_list[L_SOLID];
  while(lp) {
    solidCount++;
    lp = lp->next;
  }

  glb->buf = GET_BUF(5 + totWingCount * 3 + numRealBoundaries + solidCount);
  glb->buf[buf_size++] = pfGetTopoTopoId(topo);
  ((pfReal *)glb->buf)[buf_size++] = pfGetTopoDistanceTol(topo);
  glb->buf[buf_size++] = pfGetTopoLengthUnits(topo);
  glb->buf[buf_size++] = numRealBoundaries;

  if(numRealBoundaries > 0) {
    for(i=0; i<numRealBoundaries; i++) {
      int wingCount;
      pfBoundary *b = boundaries[i];
      glb->buf[buf_size++] = wingCounts[i];

      wingCount = pfGetBoundaryWingCount(b);
      if(wingCount == wingCounts[i]) {
	/* simple case, store all the surfaces assoc with this boundary. */
	for(j=0; j<wingCount; j++) {
	  glb->buf[buf_size++] = pfGetBoundarySurface(b, j);
	  glb->buf[buf_size++] = pfGetBoundaryLoop(b, j);
	  glb->buf[buf_size++] = pfGetBoundaryTrimCurve(b, j);
	}
      } else {
	/* not storing all surfaces specified in this boundary
	 * but are storing at least two
	 */
	int effectiveWingCount = 0;
	for(j=0; j<wingCount || effectiveWingCount == wingCounts[i]; j++) {
	  int surfId = pfGetBoundarySurface(b, j);
	  pfParaSurface *surf = pfGetTopoSurface(topo, surfId);
	  if(!surf)
	    continue;
	  if(find_in_list(L_NODE, surf, glb) != -1) {
	    glb->buf[buf_size++] = surfId;
	    glb->buf[buf_size++] = pfGetBoundaryLoop(b, j);
	    glb->buf[buf_size++] = pfGetBoundaryTrimCurve(b, j);
	    effectiveWingCount++;
	  }
	}
      }
    }
  }

  if(boundaries) free(boundaries);
  if(wingCounts) free(wingCounts);

  glb->buf[buf_size++] = solidCount;

  lp = glb->l_list[L_SOLID];

  solidCount = 0;
  while(lp) {
    pfSolid *solid = (pfSolid *)lp->data;
    glb->buf[buf_size++] = pfGetSolidSolidId(solid);
    solidCount++;
    lp = lp->next;
  }

  fwrite(&buf_size, sizeof(int), 1, glb->ofp);
  fwrite(glb->buf, sizeof(int), buf_size, glb->ofp);
}

static void pfa_write_topo(pfTopo *topo,pfa_global_t *glb)
{
  int i, j, numRealBoundaries = 0;
  int numBoundaries = pfGetTopoBoundaryCount(topo);
  int* wingCounts = (int *)malloc(sizeof(int) * numBoundaries);
  pfBoundary **boundaries = (pfBoundary **)malloc(sizeof(pfBoundary*) *
						  numBoundaries);
  int solidCount = 0;
  list_t *lp = NULL;

  if(boundaries == NULL || wingCounts == NULL) {
    if(boundaries) free(boundaries);
    if(wingCounts) free(wingCounts);
    numBoundaries = 0; /* and spew warning ... */
  }

  for(i=0; i<numBoundaries; i++) {
    int effectiveWingCount = 0;
    pfBoundary *b =  pfGetTopoBoundary(topo, i);
    int wingCount = pfGetBoundaryWingCount(b);

    if(wingCount == 1)
      continue; /* don't worry about this free boundary */

    /* only store boundary if at least two surfaces in wing are being stored */
    for(j=0; j<wingCount; j++) {
      int surfId = pfGetBoundarySurface(b, j);
      pfParaSurface *surf = pfGetTopoSurface(topo, surfId);
      if(!surf)
	if(wingCount > 2)
	  continue;
	else /* one of two surfaces in boundary not found */
	  break;
      if(find_in_list(L_NODE, surf, glb) != -1)
	effectiveWingCount++;
    }

    if(effectiveWingCount > 1) {
      boundaries[numRealBoundaries] = b;
      wingCounts[numRealBoundaries] = effectiveWingCount;
      /* 
       * adding the boundary to the list effectively helps
       * use set the new boundary id for this boundary and
       * is used by getRealBoundaryId() function above.
       *
       */
      if(glb->version >= PFBV_SURFACE_CLONING)
	add_boundary_to_list(glb, b);
      else
	add_to_list(L_BOUNDARY, b, glb);
      numRealBoundaries++;
    }
    /* else don't store this boundary */
  }

  fprintf(glb->ofp,"%d # topo id\n",pfGetTopoTopoId(topo));
  fprintf(glb->ofp,"%g %d # distance tol, length units\n",
	  pfGetTopoDistanceTol(topo), pfGetTopoLengthUnits(topo));
  fprintf(glb->ofp,"%d # num boundaries\n",numRealBoundaries);

  if(numRealBoundaries > 0) {
    for(i=0; i<numRealBoundaries; i++) {
      int wingCount;
      pfBoundary *b = boundaries[i];
      fprintf(glb->ofp,"%d\n",wingCounts[i]);

      wingCount = b ? pfGetBoundaryWingCount(b) : 0;
      if(wingCount == wingCounts[i]) {
	/* simple case, store all the surfaces assoc with this boundary. */
	for(j=0; j<wingCount; j++) {
	  fprintf(glb->ofp,"%d %d %d\n",
		  pfGetBoundarySurface(b, j),
		  pfGetBoundaryLoop(b, j),
		  pfGetBoundaryTrimCurve(b, j));
	}
      } else {
	/* not storing all surfaces specified in this boundary
	 * but are storing at least two
	 */
	int effectiveWingCount = 0;
	for(j=0; j<wingCount || effectiveWingCount == wingCounts[i]; j++) {
	  int surfId = pfGetBoundarySurface(b, j);
	  pfParaSurface *surf = pfGetTopoSurface(topo, surfId);
	  if(!surf)
	    continue;
	  if(find_in_list(L_NODE, surf, glb) != -1) {
	    fprintf(glb->ofp,"%d %d %d\n", surfId,
		    pfGetBoundaryLoop(b, j),
		    pfGetBoundaryTrimCurve(b, j));
	    effectiveWingCount++;
	  }
	}
      }
    }
  }

  if(boundaries) free(boundaries);
  if(wingCounts) free(wingCounts);

  /* now write solids */
  lp = glb->l_list[L_SOLID];
  while(lp) {
    solidCount++;
    lp = lp->next;
  }
  fprintf(glb->ofp,"%d # num solids\n", solidCount);

  lp = glb->l_list[L_SOLID];

  solidCount = 0;
  while(lp) {
    pfSolid *solid = (pfSolid *)lp->data;
    fprintf(glb->ofp,"%d\n",pfGetSolidSolidId(solid));
    solidCount++;
    lp = lp->next;
  }
}

static void pfa_write_mesh(pfMesh *mesh, pfa_global_t *glb)
{
    int    f, nf, v, nv, nb, nnb, lines;
    float  grids[3];
    pfBox  *bbox;
    pfSphere *bsphere;
    float  epsilon;
    pfMeshFace *face;
    pfMeshVertex *vert;
    pfMeshVertexNeighbor *neighbor;
    pfVec2 *texCoord;
    pfVec3 *coord;

    /* to be easily upgradeable */
    determineLengthOfMesh(mesh, &lines, &f);

    /* save number of lines the mesh takes, together with
       length of vertex (without neighbors),
       length of vertex neighbor, length of face (without face vertices),
       length of face vertex */
    fprintf(glb->ofp, "%d 4 2 3 1\n", lines);

    /* flags */
    fprintf(glb->ofp, "%d\n", pfGetMeshFlags(mesh, 0xffffffff));
    
    /* gsetlist ignored - no dynamic updates via gsets after loading */

    /* bbox */
    bbox = pfGetMeshBBox(mesh);
    fprintf(glb->ofp, "%g %g %g  %g %g %g\n", 
	    bbox->min[0], bbox->min[1], bbox->min[2],
	    bbox->max[0], bbox->max[1], bbox->max[2]);

    /* grid: bsphere and sizes */
    bsphere = pfGetMeshGridBsphere(mesh);
    fprintf(glb->ofp, "%g %g %g  %g\n", 
	    bsphere->center[0], bsphere->center[1], bsphere->center[2], 
	    bsphere->radius);

    pfGetMeshVal(mesh, PFM_VERTEX_GRID_SIZE_X, &grids[0]);
    pfGetMeshVal(mesh, PFM_VERTEX_GRID_SIZE_Y, &grids[1]);
    pfGetMeshVal(mesh, PFM_VERTEX_GRID_SIZE_Z, &grids[2]);

    fprintf(glb->ofp, "%d %d %d\n", 
	    (int)grids[0], (int)grids[1], (int)grids[2]);

    /* epsilon */
    pfGetMeshVal(mesh, PFM_EPSILON, &epsilon);
    fprintf(glb->ofp, "%g\n", epsilon);

    /* vertices */
    nv = pfGetMeshNumVertices(mesh);
    fprintf(glb->ofp, "%d\n", nv);

    for(v = 0; v < nv; v++)
    {
	vert = pfGetMeshVertex(mesh, v);

	/* flags */
	fprintf(glb->ofp, "%d\n", pfGetMeshVertexFlags(vert, 0xffffffff));

	/* coords (coords ptr is not saved, it is NULL after loading) */
	coord = pfGetMeshVertexCoord(vert);
        fprintf(glb->ofp, "%g %g %g\n", coord[0][0], coord[0][1], coord[0][2]);

	fprintf(glb->ofp, "%d\n", pfGetMeshVertexNextVertex(vert));

	/* neighbors */
	nnb = pfGetMeshVertexNumNeighbors(vert);	
	fprintf(glb->ofp, "%d\n", nnb);
	
	for(nb = 0; nb < nnb; nb++)
	{
	    neighbor = pfGetMeshVertexNeighbor(vert, nb);
	    
	    fprintf(glb->ofp, "%d %d %d\n", 
		    neighbor->vertex, neighbor->face, neighbor->edgeType);
	    fprintf(glb->ofp, "%d %d\n", neighbor->prev, neighbor->next);
	}
    }


    /* faces */
    nf = pfGetMeshNumFaces(mesh);
    fprintf(glb->ofp, "%d\n", nf);

    for(f = 0; f < nf; f++)
    {
	face = pfGetMeshFace(mesh, f);

	/* flags and part */
	fprintf(glb->ofp, "%d %d\n", 
		pfGetMeshFaceFlags(face, 0xffffffff),
		pfGetMeshFacePart(face));
	
	/* geostate */
        fprintf(glb->ofp, "%d\n", 
		find_in_list(L_GSTATE, pfGetMeshFaceGState(face), glb));

	/* vertices and texCoords */
	nv = pfGetMeshFaceNumVerts(face);
	fprintf(glb->ofp, "%d\n", nv);

	for(v = 0; v < nv; v++)
	{
	    fprintf(glb->ofp, "%d ", pfGetMeshFaceVertex(face, v));

	    texCoord = pfGetMeshFaceTexCoord(face, v);
	    fprintf(glb->ofp, "%g %g\n", texCoord[0][0], texCoord[0][1]);
	}
    }
}


static void pfb_write_mesh(pfMesh *mesh, pfa_global_t *glb)
{
    int    c, f, nf, v, nv, nb, nnb, lines;
    float  grids[3];
    pfBox  *bbox;
    pfSphere *bsphere;
    float  epsilon;
    pfMeshFace *face;
    pfMeshVertex *vert;
    pfMeshVertexNeighbor *neighbor;
    pfVec2 *texCoord;
    pfVec3 *coord;
    int    buf_size;
    int    *buf;

    /* to be easily upgradeable */
    determineLengthOfMesh(mesh, &lines, &buf_size);

    buf = GET_BUF(buf_size);
    buf_size = 0;

    /* length of vertex (without neighbors),
       length of vertex neighbor, length of face (without face vertices),
       length of face vertex */ 
    buf[buf_size++] = (6<<24) + (5<<16) + (4<<8) + 3;

    /* flags */
    buf[buf_size++] = pfGetMeshFlags(mesh, 0xffffffff);
    
    /* gsetlist ignored - no dynamic updates via gsets after loading */

    /* bbox */
    bbox = pfGetMeshBBox(mesh);
    for(c=0; c<3; c++)
	((float *)buf)[buf_size++] = bbox->min[c];
    for(c=0; c<3; c++)
	((float *)buf)[buf_size++] = bbox->max[c];

    /* grid: bsphere and sizes */
    bsphere = pfGetMeshGridBsphere(mesh);
    for(c=0; c<3; c++)
	((float *)buf)[buf_size++] = bsphere->center[c];
    ((float *)buf)[buf_size++] = bsphere->radius;

    pfGetMeshVal(mesh, PFM_VERTEX_GRID_SIZE_X, &grids[0]);
    pfGetMeshVal(mesh, PFM_VERTEX_GRID_SIZE_Y, &grids[1]);
    pfGetMeshVal(mesh, PFM_VERTEX_GRID_SIZE_Z, &grids[2]);

    for(c=0; c<3; c++)
	buf[buf_size++] = (int)grids[c];

    /* epsilon */
    pfGetMeshVal(mesh, PFM_EPSILON, &epsilon);
    ((float *)buf)[buf_size++] = epsilon;

    /* vertices */
    nv = pfGetMeshNumVertices(mesh);
    buf[buf_size++] = nv;

    for(v = 0; v < nv; v++)
    {
	vert = pfGetMeshVertex(mesh, v);

	/* flags */
	buf[buf_size++] = pfGetMeshVertexFlags(vert, 0xffffffff);

	/* coords (coords ptr is not saved, it is NULL after loading) */
	coord = pfGetMeshVertexCoord(vert);
	for(c=0; c<3; c++)
	    ((float *)buf)[buf_size++] = coord[0][c];

	/* next vertex */
	buf[buf_size++] = pfGetMeshVertexNextVertex(vert);

	/* neighbors */
	nnb = pfGetMeshVertexNumNeighbors(vert);
	buf[buf_size++] = nnb;

	for(nb = 0; nb < nnb; nb++)
	{	    
	    neighbor = pfGetMeshVertexNeighbor(vert, nb);

	    buf[buf_size++] = neighbor->vertex;
	    buf[buf_size++] = neighbor->face;
	    buf[buf_size++] = neighbor->edgeType;
	    buf[buf_size++] = neighbor->prev;
	    buf[buf_size++] = neighbor->next;
	}
    }


    /* faces */
    nf = pfGetMeshNumFaces(mesh);
    buf[buf_size++] = nf;

    for(f = 0; f < nf; f++)
    {
	face = pfGetMeshFace(mesh, f);

	/* flags and part */
	buf[buf_size++] = pfGetMeshFaceFlags(face, 0xffffffff);
	buf[buf_size++] = pfGetMeshFacePart(face);
	
	/* geostate */
        buf[buf_size++] = find_in_list(L_GSTATE, pfGetMeshFaceGState(face), 
				       glb);

	/* vertices and texCoords */
	nv = pfGetMeshFaceNumVerts(face);
	buf[buf_size++] = nv;

	for(v = 0; v < nv; v++)
	{
	    buf[buf_size++] = pfGetMeshFaceVertex(face, v);

	    texCoord = pfGetMeshFaceTexCoord(face, v);
	    ((float *)buf)[buf_size++] = texCoord[0][0];
	    ((float *)buf)[buf_size++] = texCoord[0][1];
	}
    }

    fwrite(&buf_size, sizeof(int), 1, glb->ofp);
    fwrite(buf, sizeof(int), buf_size, glb->ofp);    
}


static void pfa_write_tenv(pfTexEnv *tenv, pfa_global_t *glb)
{
    float r, g, b, a;

    fprintf(glb->ofp, "%d\n", find_in_table(tem_table, pfGetTEnvMode(tenv)));
    fprintf(glb->ofp, "%d\n",
	    find_in_table(tec_table, pfGetTEnvComponent(tenv)));
    pfGetTEnvBlendColor(tenv, &r, &g, &b, &a);
    fprintf(glb->ofp, "%.9g %.9g %.9g %.9g\n", r, g, b, a);
    fprintf(glb->ofp, "%d\n", get_udata((pfObject*)tenv, glb));
}


static void pfb_write_tenv(pfTexEnv *tenv, pfa_global_t *glb)
{
    tenv_t t;

    t.mode = find_in_table(tem_table, pfGetTEnvMode(tenv));
    t.component = find_in_table(tec_table, pfGetTEnvComponent(tenv));
    pfGetTEnvBlendColor(tenv,
			&t.color[0], &t.color[1], &t.color[2], &t.color[3]);
    t.udata = get_udata((pfObject*)tenv, glb);

    fwrite(&t, sizeof(tenv_t), 1, glb->ofp);
}


static void pfa_write_fog(pfFog *fog, pfa_global_t *glb)
{
    fog_t f;

    f.type = find_in_table(fogt_table, pfGetFogType(fog));
    pfGetFogColor(fog, &f.color[0], &f.color[1], &f.color[2]);
    pfGetFogRange(fog, &f.range[0], &f.range[1]);
    pfGetFogOffsets(fog, &f.offsets[0], &f.offsets[1]);
    pfGetFogRamp(fog, &f.r_points, f.r_range, f.r_density, &f.r_bias);
    f.udata = get_udata((pfObject*)fog, glb);

    fprintf(glb->ofp, "%d\n", f.type);
    fprintf(glb->ofp, "%g %g %g\n", f.color[0], f.color[1], f.color[2]);
    fprintf(glb->ofp, "%g %g\n", f.range[0], f.range[1]);
    fprintf(glb->ofp, "%g %g\n", f.offsets[0], f.offsets[1]);
    fprintf(glb->ofp, "%d\n", f.r_points);
    fprintf(glb->ofp, "%g %g %g %g %g %g %g %g %g %g %g %g\n",
	    f.r_range[0], f.r_range[1], f.r_range[2], f.r_range[3],
	    f.r_range[4], f.r_range[5], f.r_range[6], f.r_range[7],
	    f.r_range[8], f.r_range[9], f.r_range[10], f.r_range[11]);
    fprintf(glb->ofp, "%g %g %g %g %g %g %g %g %g %g %g %g\n",
	    f.r_density[0], f.r_density[1], f.r_density[2], f.r_density[3],
	    f.r_density[4], f.r_density[5], f.r_density[6], f.r_density[7],
	    f.r_density[8], f.r_density[9], f.r_density[10], f.r_density[11]);
    fprintf(glb->ofp, "%.9g\n", f.r_bias);
    fprintf(glb->ofp, "%d\n", f.udata);
}


static void pfb_write_fog(pfFog *fog, pfa_global_t *glb)
{
    fog_t f;

    f.type = find_in_table(fogt_table, pfGetFogType(fog));
    pfGetFogColor(fog, &f.color[0], &f.color[1], &f.color[2]);
    pfGetFogRange(fog, &f.range[0], &f.range[1]);
    pfGetFogOffsets(fog, &f.offsets[0], &f.offsets[1]);
    pfGetFogRamp(fog, &f.r_points, f.r_range, f.r_density, &f.r_bias);
    f.udata = get_udata((pfObject*)fog, glb);

    fwrite(&f, sizeof(fog_t), 1, glb->ofp);
}


static void pfa_write_tgen(pfTexGen *tgen, pfa_global_t *glb)
{
    tgen_t t;

    t.mode_s = find_in_table(tgen_table, pfGetTGenMode(tgen, PF_S));
    t.mode_t = find_in_table(tgen_table, pfGetTGenMode(tgen, PF_T));
    t.mode_r = find_in_table(tgen_table, pfGetTGenMode(tgen, PF_R));
    t.mode_q = find_in_table(tgen_table, pfGetTGenMode(tgen, PF_Q));
    pfGetTGenPlane(tgen, PF_S,
		   &t.plane_s[0], &t.plane_s[1], &t.plane_s[2], &t.plane_s[3]);
    pfGetTGenPlane(tgen, PF_T,
		   &t.plane_t[0], &t.plane_t[1], &t.plane_t[2], &t.plane_t[3]);
    pfGetTGenPlane(tgen, PF_R,
		   &t.plane_r[0], &t.plane_r[1], &t.plane_r[2], &t.plane_r[3]);
    pfGetTGenPlane(tgen, PF_Q,
		   &t.plane_q[0], &t.plane_q[1], &t.plane_q[2], &t.plane_q[3]);
    t.udata = get_udata((pfObject*)tgen, glb);

    fprintf(glb->ofp, "%d %d %d %d\n", t.mode_s, t.mode_t, t.mode_r, t.mode_q);
    fprintf(glb->ofp, "%.9g %.9g %.9g %.9g\n",
	    t.plane_s[0], t.plane_s[1], t.plane_s[2], t.plane_s[3]);
    fprintf(glb->ofp, "%.9g %.9g %.9g %.9g\n",
	    t.plane_t[0], t.plane_t[1], t.plane_t[2], t.plane_t[3]);
    fprintf(glb->ofp, "%.9g %.9g %.9g %.9g\n",
	    t.plane_r[0], t.plane_r[1], t.plane_r[2], t.plane_r[3]);
    fprintf(glb->ofp, "%.9g %.9g %.9g %.9g\n",
	    t.plane_q[0], t.plane_q[1], t.plane_q[2], t.plane_q[3]);
    fprintf(glb->ofp, "%d\n", t.udata);
}


static void pfb_write_tgen(pfTexGen *tgen, pfa_global_t *glb)
{
    tgen_t t;

    t.mode_s = find_in_table(tgen_table, pfGetTGenMode(tgen, PF_S));
    t.mode_t = find_in_table(tgen_table, pfGetTGenMode(tgen, PF_T));
    t.mode_r = find_in_table(tgen_table, pfGetTGenMode(tgen, PF_R));
    t.mode_q = find_in_table(tgen_table, pfGetTGenMode(tgen, PF_Q));
    pfGetTGenPlane(tgen, PF_S,
		   &t.plane_s[0], &t.plane_s[1], &t.plane_s[2], &t.plane_s[3]);
    pfGetTGenPlane(tgen, PF_T,
		   &t.plane_t[0], &t.plane_t[1], &t.plane_t[2], &t.plane_t[3]);
    pfGetTGenPlane(tgen, PF_R,
		   &t.plane_r[0], &t.plane_r[1], &t.plane_r[2], &t.plane_r[3]);
    pfGetTGenPlane(tgen, PF_Q,
		   &t.plane_q[0], &t.plane_q[1], &t.plane_q[2], &t.plane_q[3]);
    t.udata = get_udata((pfObject*)tgen, glb);

    fwrite(&t, sizeof(tgen_t), 1, glb->ofp);
}


static void pfa_write_tlod(pfTexLOD *tlod, pfa_global_t *glb)
{
    tlod_t t;

    pfGetTLODRange(tlod, &t.range[0], &t.range[1]);
    pfGetTLODBias(tlod, &t.bias[0], &t.bias[1], &t.bias[2]);
    t.udata = get_udata((pfObject*)tlod, glb);

    fprintf(glb->ofp, "%.9g %.9g\n", t.range[0], t.range[1]);
    fprintf(glb->ofp, "%.9g %.9g %.9g\n", t.bias[0], t.bias[1], t.bias[2]);
    fprintf(glb->ofp, "%d\n", t.udata);
}


static void pfb_write_tlod(pfTexLOD *tlod, pfa_global_t *glb)
{
    tlod_t t;

    pfGetTLODRange(tlod, &t.range[0], &t.range[1]);
    pfGetTLODBias(tlod, &t.bias[0], &t.bias[1], &t.bias[2]);
    t.udata = get_udata((pfObject*)tlod, glb);

    fwrite(&t, sizeof(tlod_t), 1, glb->ofp);
}

static void pfb_write_shader_object(pfShaderObject *so, pfa_global_t *glb)
{
  int shaderType, len, isInlined = 0;
  const char *shaderName = NULL;
  char *source = pfGetShaderObjectSource(so);

  if(pfGetShaderObjectName(so) == NULL)
    isInlined = source ? strlen(source) : 0;

  shaderType = pfGetShaderObjectShaderType(so);
  PFASSERTALWAYS(shaderType != -1);

  fwrite(&shaderType, sizeof(int), 1, glb->ofp);
  fwrite(&isInlined, sizeof(int), 1, glb->ofp);

  if(isInlined == 0) {
    shaderName = pfGetShaderObjectName(so);
    len = shaderName ? strlen(shaderName) : 0;
    fwrite(&len, sizeof(int), 1, glb->ofp);
    if(len>0)
      fwrite(shaderName, sizeof(char), len, glb->ofp);
  } else {
    if(source)
      fwrite(source, sizeof(char), isInlined, glb->ofp);
  }
}

static void pfa_write_shader_object(pfShaderObject *so, pfa_global_t *glb)
{
  int shaderType, isInlined = 0;
  const char *shaderName = NULL;
  char *source = pfGetShaderObjectSource(so);

  if(pfGetShaderObjectName(so) == NULL)
    isInlined = source ? strlen(source) : 0;

  shaderType = pfGetShaderObjectShaderType(so);
  PFASSERTALWAYS(shaderType != -1);

  fprintf(glb->ofp,"%d # shader type\n", shaderType);
  fprintf(glb->ofp,"%d # is inlined\n", isInlined);

  if(isInlined == 0) {
    shaderName = pfGetShaderObjectName(so);
    fprintf(glb->ofp,"%s\n", shaderName?shaderName:"");
  } else {
    /* inlined version of shader source */
    if(source) {
      fprintf(glb->ofp,"%s",source);
      if(source[isInlined-1] != '\n')
	fprintf(glb->ofp,"\n");
    }
  }
}

static 
void pfb_write_shader_program_uniform_data(void *data,int uniType,int numVals,
					   pfa_global_t *glb)
{
  switch(uniType) {
  case PFUNI_FLOAT1: 
    fwrite(data, sizeof(GLfloat), numVals, glb->ofp);
    break;
  case PFUNI_FLOAT2:
    fwrite(data, sizeof(GLfloat), numVals*2, glb->ofp);
    break;
  case PFUNI_FLOAT3:
    fwrite(data, sizeof(GLfloat), numVals*3, glb->ofp);
    break;

  case PFUNI_MAT2:
  case PFUNI_FLOAT4:
    fwrite(data, sizeof(GLfloat), numVals*4, glb->ofp);
    break;

  case PFUNI_BOOL1:
  case PFUNI_INT1: 
  case PFUNI_SAMP1D:
  case PFUNI_SAMP2D:
  case PFUNI_SAMP3D:
  case PFUNI_SAMPCUBE:
  case PFUNI_SAMP1DSHADOW:
  case PFUNI_SAMP2DSHADOW:
    fwrite(data, sizeof(GLint), numVals, glb->ofp);
    break;
  case PFUNI_BOOL2:
  case PFUNI_INT2:
    fwrite(data, sizeof(GLint), numVals*2, glb->ofp);
    break;
  case PFUNI_BOOL3:
  case PFUNI_INT3:
    fwrite(data, sizeof(GLint), numVals*3, glb->ofp);
    break;
  case PFUNI_BOOL4:
  case PFUNI_INT4:
    fwrite(data, sizeof(GLint), numVals*4, glb->ofp);
    break;

  case PFUNI_MAT3:
    fwrite(data, sizeof(GLfloat), numVals*9, glb->ofp);
    break;

  case PFUNI_MAT4:
    fwrite(data, sizeof(GLfloat), numVals*16, glb->ofp);
    break;
  }
}

static void pfb_write_shader_program_uniform(pfShaderProgram *sp,int j,pfa_global_t *glb)
{
  char *name;
  int numVals, len, uniType;
  char clampMode, normalized;
  void *data, *minVal, *maxVal;

  uniType = pfGetSProgUniformType(sp, j);
  numVals = pfGetSProgUniformVals(sp, j);
  name = pfGetSProgUniformName(sp, j);
  data = pfGetSProgUniformData(sp, j);
  clampMode = pfGetSProgUniformClampMode(sp, j);
  normalized = pfGetSProgUniformNormalizedFlag(sp, j);
  minVal = pfGetSProgUniformMin(sp, j);
  maxVal = pfGetSProgUniformMax(sp, j);
  
  fwrite(&uniType,sizeof(int), 1, glb->ofp);
  fwrite(&numVals,sizeof(int), 1, glb->ofp);
  fwrite(&clampMode,sizeof(char), 1, glb->ofp);
  fwrite(&normalized,sizeof(char), 1, glb->ofp);

  len = strlen(name);
  fwrite(&len,sizeof(int), 1, glb->ofp);
  fwrite(name,1, len, glb->ofp);

  pfb_write_shader_program_uniform_data(data, uniType, numVals, glb);
  if(clampMode & PFUNI_CLAMP_MIN)
    pfb_write_shader_program_uniform_data(minVal, uniType, numVals, glb);
  if(clampMode & PFUNI_CLAMP_MAX)
    pfb_write_shader_program_uniform_data(maxVal, uniType, numVals, glb);
}

static 
void pfa_write_shader_program_uniform_data(void *data,int uniType,int numVals,
					   pfa_global_t *glb)
{
  int i;

  switch(uniType) {
  case PFUNI_FLOAT1: 
    {
      GLfloat *v = (GLfloat *)data;
      for(i=0; i<numVals; i++)
	fprintf(glb->ofp,"%g\n",v[i]);
    }
    break;
  case PFUNI_FLOAT2:
    {
      GLfloat *v = (GLfloat *)data;
      for(i=0; i<numVals; i++)
	fprintf(glb->ofp,"%g %g\n",v[0],v[1]), v+= 2;
    }
    break;
  case PFUNI_FLOAT3:
    {
      GLfloat *v = (GLfloat *)data;
      for(i=0; i<numVals; i++)
	fprintf(glb->ofp,"%g %g %g\n",v[0],v[1],v[2]), v+= 3;
    }
    break;

  case PFUNI_MAT2:
  case PFUNI_FLOAT4:
    {
      GLfloat *v = (GLfloat *)data;
      for(i=0; i<numVals; i++)
	fprintf(glb->ofp,"%g %g %g %g\n",v[0],v[1],v[2],v[3]), v+= 4;
    }
    break;

  case PFUNI_BOOL1:
  case PFUNI_INT1: 
  case PFUNI_SAMP1D:
  case PFUNI_SAMP2D:
  case PFUNI_SAMP3D:
  case PFUNI_SAMPCUBE:
  case PFUNI_SAMP1DSHADOW:
  case PFUNI_SAMP2DSHADOW:
    {
      GLint *v = (GLint *)data;
      for(i=0; i<numVals; i++)
	fprintf(glb->ofp,"%d\n",v[i]);
    }
    break;
  case PFUNI_BOOL2:
  case PFUNI_INT2:
    {
      GLint *v = (GLint *)data;
      for(i=0; i<numVals; i++)
	fprintf(glb->ofp,"%d %d\n",v[0],v[1]), v+= 2;
    }
    break;
  case PFUNI_BOOL3:
  case PFUNI_INT3:
    {
      GLint *v = (GLint *)data;
      for(i=0; i<numVals; i++)
	fprintf(glb->ofp,"%d %d %d\n",v[0],v[1],v[2]), v+= 3;
    }
    break;
  case PFUNI_BOOL4:
  case PFUNI_INT4:
    {
      GLint *v = (GLint *)data;
      for(i=0; i<numVals; i++)
	fprintf(glb->ofp,"%d %d %d %d\n",v[0],v[1],v[2],v[3]), v+= 4;
    }
    break;

  case PFUNI_MAT3:
    {
      GLfloat *v = (GLfloat *)data;
      for(i=0; i<numVals; i++)
	fprintf(glb->ofp,"%g %g %g %g %g %g %g %g %g\n",
		v[0],v[1],v[2],v[3],v[4],v[5],v[6],v[7],v[8]), v+= 9;
    }
    break;

  case PFUNI_MAT4:
    {
      GLfloat *v = (GLfloat *)data;
      for(i=0; i<numVals; i++)
	fprintf(glb->ofp,"%g %g %g %g %g %g %g %g %g %g %g %g %g %g %g %g\n",
		v[0],v[1],v[2],v[3],v[4],v[5],v[6],v[7],v[8],v[9],v[10],v[11],v[12],v[13],v[14],v[15]), v+= 16;
    }
    break;
  }
}

static void pfa_write_shader_program_uniform(pfShaderProgram *sp,int j,pfa_global_t *glb)
{
  char *name;
  int numVals;
  unsigned short uniType;
  char clampMode, normalized;
  void *data, *minVal, *maxVal;

  uniType = pfGetSProgUniformType(sp, j);
  numVals = pfGetSProgUniformVals(sp, j);
  name = pfGetSProgUniformName(sp, j);
  data = pfGetSProgUniformData(sp, j);
  clampMode = pfGetSProgUniformClampMode(sp, j);
  normalized = pfGetSProgUniformNormalizedFlag(sp, j);
  minVal = pfGetSProgUniformMin(sp, j);
  maxVal = pfGetSProgUniformMax(sp, j);

  fprintf(glb->ofp,"%s\n",name);
  fprintf(glb->ofp,"%d %d %d %d\n",uniType,numVals,clampMode,normalized);

  pfa_write_shader_program_uniform_data(data, uniType, numVals, glb);
  if(clampMode & PFUNI_CLAMP_MIN)
    pfa_write_shader_program_uniform_data(minVal, uniType, numVals, glb);
  if(clampMode & PFUNI_CLAMP_MAX)
    pfa_write_shader_program_uniform_data(maxVal, uniType, numVals, glb);
}

static void pfa_write_shader_program(pfShaderProgram *sp, pfa_global_t *glb)
{
  int i, numShaders, numUniforms;
  pfShaderObject *obj;

  numShaders = pfGetSProgNumShaders(sp);
  numUniforms = pfGetSProgNumUniforms(sp);

  fprintf(glb->ofp,"%d # num shader objects\n", numShaders);
  fprintf(glb->ofp,"%d # num uniforms\n", numUniforms);

  /* First write out shader objects */
  for(i=0; i<numShaders; i++) {
    obj = pfGetSProgShader(sp, i);
    if(obj)
      fprintf(glb->ofp,"%d\n",find_in_list(L_SHADOBJ, obj, glb));
    else
      fprintf(glb->ofp,"-1\n");
  }

  /* Then write out uniforms */
  for(i=0; i<numUniforms; i++)
    pfa_write_shader_program_uniform(sp, i, glb);
}

static void pfb_write_shader_program(pfShaderProgram *sp, pfa_global_t *glb)
{
  int i, numShaders, numUniforms, index;
  pfShaderObject *obj;

  numShaders = pfGetSProgNumShaders(sp);
  numUniforms = pfGetSProgNumUniforms(sp);

  fwrite(&numShaders, sizeof(int), 1, glb->ofp);
  fwrite(&numUniforms, sizeof(int), 1, glb->ofp);

  /* First write out shader objects */
  for(i=0; i<numShaders; i++) {
    obj = pfGetSProgShader(sp, i);
    if(obj)
      index = find_in_list(L_SHADOBJ, obj, glb);
    else
      index = -1;
    fwrite(&index, sizeof(int), 1, glb->ofp);
  }

  /* Then write out uniforms */
  for(i=0; i<numUniforms; i++)
    pfb_write_shader_program_uniform(sp, i, glb);
}

static void pfa_write_gprogram(pfGProgram *gp, pfa_global_t *glb)
{
    unsigned char *str;
    int  i, type, size;

    pfGetGProgramProgram(gp, (char **)&str);
    if (pfIsOfType(gp, pfGetVertexProgramClassType()))
        type = 0;
    else
        type = 1;
      
   
    fprintf(glb->ofp, "%d\n", type);

    if(str)
        size = strlen(str);
    else
        size = 0;
    fprintf(glb->ofp, "%d\n", size);

    for (i = 0; i < size; i++)
    {
	fprintf(glb->ofp, "%c%c",
		hex_char[str[i] >> 4], hex_char[str[i] & 0xf]);
	if (i % 39 == 38)
	    fprintf(glb->ofp, "\n");
    }
    if (i % 39 != 0)
	fprintf(glb->ofp, "\n");
}


static void pfb_write_gprogram(pfGProgram *gp, pfa_global_t *glb)
{
    unsigned char *str;
    int  type, size;

    pfGetGProgramProgram(gp, (char **)&str);
    if (pfIsOfType(gp, pfGetVertexProgramClassType()))
        type = 0;
    else
        type = 1;

    fwrite(&type, sizeof(int), 1, glb->ofp);

    if(str)
        size = strlen(str);
    else
        size = 0;
    fwrite(&size, sizeof(int), 1, glb->ofp);

    if (size > 0)
	fwrite(str, 1, size, glb->ofp);
}


static void pfa_write_gprogramparms(pfGProgramParms *gpp, pfa_global_t *glb)
{
    int   i, j, num, ix, type, count;
    float *p;

    num = pfGetGPParamsNumParameters(gpp);

    fprintf(glb->ofp, "%d %d\n", pfGetGPParamsType(gpp), num);

    for (i = 0; i < num; i++)
    {
        pfGetGPParamsParameters(gpp, i, &ix, &type, &count, (void**)&p);
	
	fprintf(glb->ofp, "%d %d %d ", ix, type, count); 

	for(j=0; j<count; j++)
	    fprintf(glb->ofp, "%g ", *(p++));

	fprintf(glb->ofp, "\n");
    }
}


static void pfb_write_gprogramparms(pfGProgramParms *gpp, pfa_global_t *glb)
{
    int   i, num, ix, count, type;
    float *p;

    type = pfGetGPParamsType(gpp);
    fwrite(&type, sizeof(int), 1, glb->ofp);

    num = pfGetGPParamsNumParameters(gpp);
    fwrite(&num, sizeof(int), 1, glb->ofp);

    for (i = 0; i < num; i++)
    {
        pfGetGPParamsParameters(gpp, i, &ix, &type, &count, (void**)&p);
	
	fwrite(&ix, sizeof(int), 1, glb->ofp);
	fwrite(&type, sizeof(int), 1, glb->ofp);
	fwrite(&count, sizeof(int), 1, glb->ofp);

	fwrite(p, sizeof(float), count, glb->ofp);
    }
}


static void pfa_write_ctab(pfColortable *ctab, pfa_global_t *glb)
{
    int size;
    int i;
    int udata;
    pfVec4 *colors;

    size = pfGetCtabSize(ctab);
    colors = pfGetCtabColors(ctab);
    udata = get_udata((pfObject*)ctab, glb);

    fprintf(glb->ofp, "%d\n", size);
    for (i = 0; i < size; i++)
	fprintf(glb->ofp, "%g %g %g %g\n",
		colors[i][0], colors[i][1], colors[i][2], colors[i][3]);
    fprintf(glb->ofp, "%d\n", udata);
}


static void pfb_write_ctab(pfColortable *ctab, pfa_global_t *glb)
{
    int size;
    pfVec4 *colors;
    int udata;

    size = pfGetCtabSize(ctab);
    colors = pfGetCtabColors(ctab);
    udata = get_udata((pfObject*)ctab, glb);

    fwrite(&size, sizeof(int), 1, glb->ofp);
    fwrite(colors, sizeof(pfVec4), size, glb->ofp);
    fwrite(&udata, sizeof(int), 1, glb->ofp);
}


static void pfa_write_lpstate(pfLPointState *lpstate, pfa_global_t *glb)
{
    lpstate_t l;
    pfRasterFuncType rfunc;
    pfCalligFuncType cfunc;
    void *data;

    l.size_mode = find_in_table(lpssm_table,
				pfGetLPStateMode(lpstate, PFLPS_SIZE_MODE));
    l.transp_mode = find_in_table(lpstm_table,
				  pfGetLPStateMode(lpstate, PFLPS_TRANSP_MODE));
    l.fog_mode = find_in_table(lpsfm_table,
			       pfGetLPStateMode(lpstate, PFLPS_FOG_MODE));
    l.dir_mode = find_in_table(lpsdm_table,
			       pfGetLPStateMode(lpstate, PFLPS_DIR_MODE));
    l.shape_mode = find_in_table(lpsshm_table,
				 pfGetLPStateMode(lpstate, PFLPS_SHAPE_MODE));
    l.range_mode = find_in_table(lpsrm_table,
				 pfGetLPStateMode(lpstate, PFLPS_RANGE_MODE));
    l.draw_mode = find_in_table(lpsdrwm_table,
				pfGetLPStateMode(lpstate, PFLPS_DRAW_MODE));
    l.quality_mode = find_in_table(lpsqm_table,
				 pfGetLPStateMode(lpstate, PFLPS_QUALITY_MODE));
    l.size_min_pixel = pfGetLPStateVal(lpstate, PFLPS_SIZE_MIN_PIXEL);
    l.size_max_pixel = pfGetLPStateVal(lpstate, PFLPS_SIZE_MAX_PIXEL);
    l.size_actual = pfGetLPStateVal(lpstate, PFLPS_SIZE_ACTUAL);
    l.transp_pixel_size = pfGetLPStateVal(lpstate, PFLPS_TRANSP_PIXEL_SIZE);
    l.transp_exponent = pfGetLPStateVal(lpstate, PFLPS_TRANSP_EXPONENT);
    l.transp_scale = pfGetLPStateVal(lpstate, PFLPS_TRANSP_SCALE);
    l.transp_clamp = pfGetLPStateVal(lpstate, PFLPS_TRANSP_CLAMP);
    l.fog_scale = pfGetLPStateVal(lpstate, PFLPS_FOG_SCALE);
    l.intensity = pfGetLPStateVal(lpstate, PFLPS_INTENSITY);
    l.size_diff_thresh = pfGetLPStateVal(lpstate, PFLPS_SIZE_DIFF_THRESH);
    l.significance = pfGetLPStateVal(lpstate, PFLPS_SIGNIFICANCE);
    l.min_defocus = pfGetLPStateVal(lpstate, PFLPS_MIN_DEFOCUS);
    l.max_defocus = pfGetLPStateVal(lpstate, PFLPS_MAX_DEFOCUS);
    pfGetLPStateShape(lpstate, &l.shape[0], &l.shape[1], &l.shape[2],
		      &l.shape[3], &l.shape[4]);
    pfGetLPStateBackColor(lpstate, &l.back_color[0], &l.back_color[1],
			  &l.back_color[2], &l.back_color[3]);
    pfGetRasterFunc(lpstate, &rfunc, &data);
    l.rfunc = find_in_list(L_UFUNC, rfunc, glb);
    l.rfunc_data = find_in_list(L_UDATA, data, glb);
    pfGetCalligFunc(lpstate, &cfunc, &data);
    l.cfunc = find_in_list(L_UFUNC, cfunc, glb);
    l.cfunc_data = find_in_list(L_UDATA, data, glb);
    l.udata = get_udata((pfObject*)lpstate, glb);

    fprintf(glb->ofp, "%d %d %d %d %d %d %d %d\n",
	    l.size_mode, l.transp_mode, l.fog_mode, l.dir_mode,
	    l.shape_mode, l.range_mode, l.draw_mode, l.quality_mode);
    fprintf(glb->ofp,
	   "%.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g\n",
	    l.size_min_pixel, l.size_max_pixel, l.size_actual,
	    l.transp_pixel_size, l.transp_exponent, l.transp_scale,
	    l.transp_clamp, l.fog_scale, l.intensity, l.size_diff_thresh,
	    l.significance, l.min_defocus, l.max_defocus);
    fprintf(glb->ofp, "%.9g %.9g %.9g %.9g %.9g\n",
	    l.shape[0], l.shape[1], l.shape[2], l.shape[3], l.shape[4]);
    fprintf(glb->ofp, "%.9g %.9g %.9g %.9g\n",
	    l.back_color[0], l.back_color[1], l.back_color[2], l.back_color[3]);
    fprintf(glb->ofp, "%d %d %d %d\n",
	    l.rfunc, l.rfunc_data, l.cfunc, l.cfunc_data);
    fprintf(glb->ofp, "%d\n", l.udata);
}


static void pfb_write_lpstate(pfLPointState *lpstate, pfa_global_t *glb)
{
    lpstate_t l;
    pfRasterFuncType rfunc;
    pfCalligFuncType cfunc;
    void *data;

    l.size_mode = find_in_table(lpssm_table,
				pfGetLPStateMode(lpstate, PFLPS_SIZE_MODE));
    l.transp_mode = find_in_table(lpstm_table,
				  pfGetLPStateMode(lpstate, PFLPS_TRANSP_MODE));
    l.fog_mode = find_in_table(lpsfm_table,
			       pfGetLPStateMode(lpstate, PFLPS_FOG_MODE));
    l.dir_mode = find_in_table(lpsdm_table,
			       pfGetLPStateMode(lpstate, PFLPS_DIR_MODE));
    l.shape_mode = find_in_table(lpsshm_table,
				 pfGetLPStateMode(lpstate, PFLPS_SHAPE_MODE));
    l.range_mode = find_in_table(lpsrm_table,
				 pfGetLPStateMode(lpstate, PFLPS_RANGE_MODE));
    l.draw_mode = find_in_table(lpsdrwm_table,
				pfGetLPStateMode(lpstate, PFLPS_DRAW_MODE));
    l.quality_mode = find_in_table(lpsqm_table,
				 pfGetLPStateMode(lpstate, PFLPS_QUALITY_MODE));
    l.size_min_pixel = pfGetLPStateVal(lpstate, PFLPS_SIZE_MIN_PIXEL);
    l.size_max_pixel = pfGetLPStateVal(lpstate, PFLPS_SIZE_MAX_PIXEL);
    l.size_actual = pfGetLPStateVal(lpstate, PFLPS_SIZE_ACTUAL);
    l.transp_pixel_size = pfGetLPStateVal(lpstate, PFLPS_TRANSP_PIXEL_SIZE);
    l.transp_exponent = pfGetLPStateVal(lpstate, PFLPS_TRANSP_EXPONENT);
    l.transp_scale = pfGetLPStateVal(lpstate, PFLPS_TRANSP_SCALE);
    l.transp_clamp = pfGetLPStateVal(lpstate, PFLPS_TRANSP_CLAMP);
    l.fog_scale = pfGetLPStateVal(lpstate, PFLPS_FOG_SCALE);
    l.intensity = pfGetLPStateVal(lpstate, PFLPS_INTENSITY);
    l.size_diff_thresh = pfGetLPStateVal(lpstate, PFLPS_SIZE_DIFF_THRESH);
    l.significance = pfGetLPStateVal(lpstate, PFLPS_SIGNIFICANCE);
    l.min_defocus = pfGetLPStateVal(lpstate, PFLPS_MIN_DEFOCUS);
    l.max_defocus = pfGetLPStateVal(lpstate, PFLPS_MAX_DEFOCUS);
    pfGetLPStateShape(lpstate, &l.shape[0], &l.shape[1], &l.shape[2],
		      &l.shape[3], &l.shape[4]);
    pfGetLPStateBackColor(lpstate, &l.back_color[0], &l.back_color[1],
			  &l.back_color[2], &l.back_color[3]);
    pfGetRasterFunc(lpstate, &rfunc, &data);
    l.rfunc = find_in_list(L_UFUNC, rfunc, glb);
    l.rfunc_data = find_in_list(L_UDATA, data, glb);
    pfGetCalligFunc(lpstate, &cfunc, &data);
    l.cfunc = find_in_list(L_UFUNC, cfunc, glb);
    l.cfunc_data = find_in_list(L_UDATA, data, glb);
    l.udata = get_udata((pfObject*)lpstate, glb);

    fwrite(&l, sizeof(lpstate_t), 1, glb->ofp);
}


static void pfa_write_hlight(pfHighlight *hlight, pfa_global_t *glb)
{
    hlight_t h;

    h.mode = to_table(hlm_table, pfGetHlightMode(hlight));
    pfGetHlightColor(hlight, PFHL_FGCOLOR,
		     &h.fg_color[0], &h.fg_color[1], &h.fg_color[2]);
    pfGetHlightColor(hlight, PFHL_BGCOLOR,
		     &h.bg_color[0], &h.bg_color[1], &h.bg_color[2]);
    h.alpha = pfGetHlightAlpha(hlight);
    pfGetHlightNormalLength(hlight, &h.normal_length[0], &h.normal_length[1]);
    h.line_width = pfGetHlightLineWidth(hlight);
    h.point_size = pfGetHlightPntSize(hlight);
    h.fg_line_pat = pfGetHlightLinePat(hlight, PFHL_FGPAT);
    h.bg_line_pat = pfGetHlightLinePat(hlight, PFHL_BGPAT);
    pfGetHlightFillPat(hlight, PFHL_FGPAT, h.fg_fill_pat);
    pfGetHlightFillPat(hlight, PFHL_BGPAT, h.fg_fill_pat);
    h.gstate[0] = find_in_list(L_GSTATE, pfGetHlightGState(hlight), glb);
    h.gstate[1] = pfGetHlightGStateIndex(hlight);
    h.tex = find_in_list(L_TEX, pfGetHlightTex(hlight), glb);
    h.tenv = find_in_list(L_TENV, pfGetHlightTEnv(hlight), glb);
    h.tgen = find_in_list(L_TGEN, pfGetHlightTGen(hlight), glb);
    h.udata = get_udata((pfObject*)hlight, glb);

    fprintf(glb->ofp, "%d\n", h.mode);
    fprintf(glb->ofp, "%.9g %.9g %.9g %.9g %.9g %.9g %.9g\n",
	    h.fg_color[0], h.fg_color[1], h.fg_color[2],
	    h.bg_color[0], h.bg_color[1], h.bg_color[2], h.alpha);
    fprintf(glb->ofp, "%.9g %.9g\n", h.normal_length[0], h.normal_length[1]);
    fprintf(glb->ofp, "%.9g %.9g\n", h.line_width, h.point_size);
    fprintf(glb->ofp, "%x %x", h.fg_line_pat, h.bg_line_pat);
    fprintf(glb->ofp, "%x %x %x %x %x %x %x %x\n",
	    h.fg_fill_pat[0], h.fg_fill_pat[1], h.fg_fill_pat[2],
	    h.fg_fill_pat[3], h.fg_fill_pat[4], h.fg_fill_pat[5],
	    h.fg_fill_pat[6], h.fg_fill_pat[7]);
    fprintf(glb->ofp, "%x %x %x %x %x %x %x %x\n",
	    h.fg_fill_pat[8], h.fg_fill_pat[9], h.fg_fill_pat[10],
	    h.fg_fill_pat[11], h.fg_fill_pat[12], h.fg_fill_pat[13],
	    h.fg_fill_pat[14], h.fg_fill_pat[15]);
    fprintf(glb->ofp, "%x %x %x %x %x %x %x %x\n",
	    h.fg_fill_pat[16], h.fg_fill_pat[17], h.fg_fill_pat[18],
	    h.fg_fill_pat[19], h.fg_fill_pat[20], h.fg_fill_pat[21],
	    h.fg_fill_pat[22], h.fg_fill_pat[23]);
    fprintf(glb->ofp, "%x %x %x %x %x %x %x %x\n",
	    h.fg_fill_pat[24], h.fg_fill_pat[25], h.fg_fill_pat[26],
	    h.fg_fill_pat[27], h.fg_fill_pat[28], h.fg_fill_pat[29],
	    h.fg_fill_pat[30], h.fg_fill_pat[31]);
    fprintf(glb->ofp, "%x %x %x %x %x %x %x %x\n",
	    h.bg_fill_pat[0], h.bg_fill_pat[1], h.bg_fill_pat[2],
	    h.bg_fill_pat[3], h.bg_fill_pat[4], h.bg_fill_pat[5],
	    h.bg_fill_pat[6], h.bg_fill_pat[7]);
    fprintf(glb->ofp, "%x %x %x %x %x %x %x %x\n",
	    h.bg_fill_pat[8], h.bg_fill_pat[9], h.bg_fill_pat[10],
	    h.bg_fill_pat[11], h.bg_fill_pat[12], h.bg_fill_pat[13],
	    h.bg_fill_pat[14], h.bg_fill_pat[15]);
    fprintf(glb->ofp, "%x %x %x %x %x %x %x %x\n",
	    h.bg_fill_pat[16], h.bg_fill_pat[17], h.bg_fill_pat[18],
	    h.bg_fill_pat[19], h.bg_fill_pat[20], h.bg_fill_pat[21],
	    h.bg_fill_pat[22], h.bg_fill_pat[23]);
    fprintf(glb->ofp, "%x %x %x %x %x %x %x %x\n",
	    h.bg_fill_pat[24], h.bg_fill_pat[25], h.bg_fill_pat[26],
	    h.bg_fill_pat[27], h.bg_fill_pat[28], h.bg_fill_pat[29],
	    h.bg_fill_pat[30], h.bg_fill_pat[31]);
    fprintf(glb->ofp, "%d %d\n", h.gstate[0], h.gstate[1]);
    fprintf(glb->ofp, "%d %d %d\n", h.tex, h.tenv, h.tgen);
    fprintf(glb->ofp, "%d\n", h.udata);
}


static void pfb_write_hlight(pfHighlight *hlight, pfa_global_t *glb)
{
    hlight_t h;

    h.mode = to_table(hlm_table, pfGetHlightMode(hlight));
    pfGetHlightColor(hlight, PFHL_FGCOLOR,
		     &h.fg_color[0], &h.fg_color[1], &h.fg_color[2]);
    pfGetHlightColor(hlight, PFHL_BGCOLOR,
		     &h.bg_color[0], &h.bg_color[1], &h.bg_color[2]);
    h.alpha = pfGetHlightAlpha(hlight);
    pfGetHlightNormalLength(hlight, &h.normal_length[0], &h.normal_length[1]);
    h.line_width = pfGetHlightLineWidth(hlight);
    h.point_size = pfGetHlightPntSize(hlight);
    h.fg_line_pat = pfGetHlightLinePat(hlight, PFHL_FGPAT);
    h.bg_line_pat = pfGetHlightLinePat(hlight, PFHL_BGPAT);
    pfGetHlightFillPat(hlight, PFHL_FGPAT, h.fg_fill_pat);
    pfGetHlightFillPat(hlight, PFHL_BGPAT, h.fg_fill_pat);
    h.gstate[0] = find_in_list(L_GSTATE, pfGetHlightGState(hlight), glb);
    h.gstate[1] = pfGetHlightGStateIndex(hlight);
    h.tex = find_in_list(L_TEX, pfGetHlightTex(hlight), glb);
    h.tenv = find_in_list(L_TENV, pfGetHlightTEnv(hlight), glb);
    h.tgen = find_in_list(L_TGEN, pfGetHlightTGen(hlight), glb);
    h.udata = get_udata((pfObject*)hlight, glb);

    fwrite(&h, sizeof(hlight_t), 1, glb->ofp);
}


static void pfa_write_gstate(pfGeoState *gstate, pfa_global_t *glb)
{
    uint64_t imask, state;
    int i, j;
    int mode;
    void *data;
    int *ids = NULL, whichAttr;

    imask = pfGetGStateInherit(gstate);

    ids = (int *)malloc(glb->maxTextures * sizeof(int));
    PFASSERTALWAYS(ids);

    for (i = 1; i <= gst_table[0]; i++)
    {
	if (!(imask & (state = gst_table[i])))
	{
	    switch (i)
	    {
		/*
		 *  Modes
		 */
		case STATE_TRANSPARENCY:
		    mode = pfGetGStateMode(gstate, state);
		    fprintf(glb->ofp, "%d %d",
			    i, find_in_table(tr_table, mode));
		    break;
		case STATE_ANTIALIAS:
		    mode = pfGetGStateMode(gstate, state);
		    fprintf(glb->ofp, "%d %d",
			    i, find_in_table(aa_table, mode));
		    break;
		case STATE_DECAL:
		    mode = pfGetGStateMode(gstate, state);
		    fprintf(glb->ofp, "%d %d %d %d\n", i,
			    find_in_table(decal_table,
					  mode & PFDECAL_MODE_MASK),
			    (mode & PFDECAL_LAYER_MASK) >> PFDECAL_LAYER_SHIFT,
			    (mode & PFDECAL_LAYER_OFFSET)? 1 : 0);
		    break;
		case STATE_ALPHAFUNC:
		    mode = pfGetGStateMode(gstate, state);
		    fprintf(glb->ofp, "%d %d",
			    i, find_in_table(af_table, mode));
		    break;
		case STATE_CULLFACE:
		    mode = pfGetGStateMode(gstate, state);
		    fprintf(glb->ofp, "%d %d",
			    i, find_in_table(cf_table, mode));
		    break;
		case STATE_ENTEXTURE:
		case STATE_ENTEXGEN:
		case STATE_ENTEXMAT:
		case STATE_ENTEXLOD:
		  for (j = 0 ; j < glb->maxTextures ; j ++)
		  {
		    if (j < glb->maxTextures)
		      mode = pfGetGStateMultiMode(gstate, state, j);
		    else
		      mode = 0;
		    ids[j] = find_in_table(oo_table, mode);
		  }
		  
		  fprintf(glb->ofp,"%d",i);
		  for (j=0; j<glb->maxTextures; j++)
		    fprintf(glb->ofp," %d",ids[j]);
		  break;
		case STATE_ENLIGHTING:
		case STATE_ENFOG:
		case STATE_ENWIREFRAME:
		case STATE_ENCOLORTABLE:
		case STATE_ENHIGHLIGHTING:
		case STATE_ENLPOINTSTATE:
		case STATE_ENVTXPROG:
		case STATE_ENFRAGPROG:
	        case STATE_ENSHADPROG:
		    mode = pfGetGStateMode(gstate, state);
		    fprintf(glb->ofp, "%d %d",
			    i, find_in_table(oo_table, mode));
		    break;

		/*
		 *  Values
		 */
		case STATE_ALPHAREF:
		    fprintf(glb->ofp, "%d %.9g\n", i,
			    pfGetGStateVal(gstate, state));
		    break;

		/*
		 *  Attributes
		 */
		case STATE_FRONTMTL:
		    data = pfGetGStateAttr(gstate, state);
		    fprintf(glb->ofp, "%d %d",
			    i, find_in_list(L_MTL, data, glb));
		    break;
		case STATE_BACKMTL:
		    data = pfGetGStateAttr(gstate, state);
		    fprintf(glb->ofp, "%d %d",
			    i, find_in_list(L_MTL, data, glb));
		    break;
		case STATE_FOG:
		    data = pfGetGStateAttr(gstate, state);
		    fprintf(glb->ofp, "%d %d",
			    i, find_in_list(L_FOG, data, glb));
		    break;
		case STATE_LIGHTMODEL:
		    data = pfGetGStateAttr(gstate, state);
		    fprintf(glb->ofp, "%d %d",
			    i, find_in_list(L_LMODEL, data, glb));
		    break;
		case STATE_LIGHTS:
		    {
			pfLight **lights;
			int j;

			fprintf(glb->ofp, "%d %d", i, PF_MAX_LIGHTS);

			lights = pfGetGStateAttr(gstate, state);
			for (j = 0; j < PF_MAX_LIGHTS; j++)
			    fprintf(glb->ofp, " %d",
				    find_in_list(L_LIGHT, lights[j], glb));
		    }
		    break;
		case STATE_COLORTABLE:
		    data = pfGetGStateAttr(gstate, state);
		    fprintf(glb->ofp, "%d %d",
			    i, find_in_list(L_CTAB, data, glb));
		    break;
		case STATE_HIGHLIGHT:
		    data = pfGetGStateAttr(gstate, state);
		    fprintf(glb->ofp, "%d %d",
			    i, find_in_list(L_HLIGHT, data, glb));
		    break;
		case STATE_LPOINTSTATE:
		    data = pfGetGStateAttr(gstate, state);
		    fprintf(glb->ofp, "%d %d",
			    i, find_in_list(L_LPSTATE, data, glb));
		    break;
		case STATE_TEXENV:
	        case STATE_TEXGEN:
		case STATE_TEXLOD:
	        case STATE_TEXTURE:
		  switch(i) { /* gross ... */
		  case STATE_TEXENV: whichAttr = L_TENV; break;
		  case STATE_TEXGEN: whichAttr = L_TGEN; break;
		  case STATE_TEXLOD: whichAttr = L_TLOD; break;
		  case STATE_TEXTURE: whichAttr = L_TEX; break;
		  default:
		    break;
		  }

		  for (j = 0 ; j < glb->maxTextures ; j ++)
		  {
		    data = pfGetGStateMultiAttr(gstate, state, j);
		    if (data)
		      ids[j] = find_in_list(whichAttr, data, glb);
		    else
		      ids[j] = (-1);
		  }
		  fprintf(glb->ofp,"%d", i);
		  for(j=0; j<glb->maxTextures; j++)
		    fprintf(glb->ofp," %d",ids[j]);
		  break;
	        case STATE_SHADPROG:
		    data = pfGetGStateAttr(gstate, state);
		    fprintf(glb->ofp,"%d %d",i,find_in_list(L_SHADPROG, data, glb));
		    break;	        
		case STATE_VERTEX_PROGRAM:
		case STATE_FRAGMENT_PROGRAM:
		    data = pfGetGStateAttr(gstate, state);
		    fprintf(glb->ofp, "%d %d",
			    i, find_in_list(L_GPROG, data, glb));
		    break;
		case STATE_GPROGPARMS:
		    {
		        int count, j;
			
			count = 0;
			for(j=0; j<PF_N_GPPARMS; j++)
			    if(pfGetGStateMultiAttr(gstate, state, j))
			      count++;
			
			fprintf(glb->ofp, "%d %d ", i, count);

			for(j=0; j<PF_N_GPPARMS; j++)
			    if(data = pfGetGStateMultiAttr(gstate, state, j))
			        fprintf(glb->ofp, "%d %d ",
					j, find_in_list(L_GPROGPARM, data, glb));
			break;
		    }
		case STATE_TEXMAT:
		  for (j = 0 ; j < glb->maxTextures ; j ++)
		  {
			pfMatrix *m;

			m = (pfMatrix *)pfGetGStateMultiAttr(gstate, state, j);
			fprintf(glb->ofp, "\n");
			fprintf(glb->ofp, "%g %g %g %g\n",
			       (*m)[0][0], (*m)[0][1], (*m)[0][2], (*m)[0][3]);
			fprintf(glb->ofp, "%g %g %g %g\n",
			       (*m)[1][0], (*m)[1][1], (*m)[1][2], (*m)[1][3]);
			fprintf(glb->ofp, "%g %g %g %g\n",
			       (*m)[2][0], (*m)[2][1], (*m)[2][2], (*m)[2][3]);
			fprintf(glb->ofp, "%g %g %g %g",
			       (*m)[3][0], (*m)[3][1], (*m)[3][2], (*m)[3][3]);
		  }
		  break;
	    }

	    if (glb->add_comments)
		fprintf(glb->ofp, "\t\t# %s\n", gst_names[i]);
	    else
		fprintf(glb->ofp, "\n");
	}
    }
    fprintf(glb->ofp, "%d\n", STATE_END);

    fprintf(glb->ofp, "%d\n", get_udata((pfObject*)gstate, glb));

    free(ids);
}


static void pfb_write_gstate(pfGeoState *gstate, pfa_global_t *glb)
{
    uint64_t imask, state;
    int i, j;
    int mode;
    void *data;
    int buf_size;
    int *buf;

    buf_size = 0;
    buf = glb->buf;
    imask = pfGetGStateInherit(gstate);

    for (i = 1; i <= gst_table[0]; i++)
    {
	if (!(imask & (state = gst_table[i])))
	{
	    buf[buf_size++] = i;
	    switch (i)
	    {
		/*
		 *  Modes
		 */
		case STATE_TRANSPARENCY:
		    mode = pfGetGStateMode(gstate, state);
		    buf[buf_size++] = find_in_table(tr_table, mode);
		    break;
		case STATE_ANTIALIAS:
		    mode = pfGetGStateMode(gstate, state);
		    buf[buf_size++] = find_in_table(aa_table, mode);
		    break;
		case STATE_DECAL:
		    mode = pfGetGStateMode(gstate, state);
		    buf[buf_size++] = find_in_table(decal_table,
						    mode & PFDECAL_MODE_MASK);
		    buf[buf_size++] = (mode & PFDECAL_LAYER_MASK) >>
				      PFDECAL_LAYER_SHIFT;
		    buf[buf_size++] = (mode & PFDECAL_LAYER_OFFSET)? 1 : 0;
		    break;
		case STATE_ALPHAFUNC:
		    mode = pfGetGStateMode(gstate, state);
		    buf[buf_size++] = find_in_table(af_table, mode);
		    break;
		case STATE_CULLFACE:
		    mode = pfGetGStateMode(gstate, state);
		    buf[buf_size++] = find_in_table(cf_table, mode);
		    break;

		case STATE_ENTEXTURE:
		case STATE_ENTEXGEN:
		case STATE_ENTEXLOD:
		case STATE_ENTEXMAT:
		    for (j = 0 ; j < glb->maxTextures ; j ++)
		    {
		        mode = pfGetGStateMultiMode(gstate, state, j);
			buf[buf_size++] = find_in_table(oo_table, mode);
		    }
		    break;
		case STATE_ENLIGHTING:
		case STATE_ENFOG:
		case STATE_ENWIREFRAME:
		case STATE_ENCOLORTABLE:
		case STATE_ENHIGHLIGHTING:
		case STATE_ENLPOINTSTATE:
		case STATE_ENVTXPROG:
		case STATE_ENFRAGPROG:
	        case STATE_ENSHADPROG:
		    mode = pfGetGStateMode(gstate, state);
		    buf[buf_size++] = find_in_table(oo_table, mode);
		    break;

		/*
		 *  Values
		 */
		case STATE_ALPHAREF:
		    ((float *)buf)[buf_size++] = pfGetGStateVal(gstate, state);
		    break;

		/*
		 *  Attributes
		 */
		case STATE_FRONTMTL:
		    data = pfGetGStateAttr(gstate, state);
		    buf[buf_size++] = find_in_list(L_MTL, data, glb);
		    break;
		case STATE_BACKMTL:
		    data = pfGetGStateAttr(gstate, state);
		    buf[buf_size++] = find_in_list(L_MTL, data, glb);
		    break;
		case STATE_TEXTURE:
		  for (j = 0 ; j < glb->maxTextures ; j ++)
		    {
		        data = pfGetGStateMultiAttr(gstate, state, j);
		        if (data)
			  buf[buf_size++] = find_in_list(L_TEX, data, glb);
		        else
			  buf[buf_size++] = (-1);
		    }
		    break;
		case STATE_TEXENV:
		  for (j = 0 ; j < glb->maxTextures ; j ++)
		    {
		        data = pfGetGStateMultiAttr(gstate, state, j);
			if (data)
			  buf[buf_size++] = find_in_list(L_TENV, data, glb);
			else
			  buf[buf_size++] = (-1);
		    }
		    break;
		case STATE_FOG:
		    data = pfGetGStateAttr(gstate, state);
		    buf[buf_size++] = find_in_list(L_FOG, data, glb);
		    break;
		case STATE_LIGHTMODEL:
		    data = pfGetGStateAttr(gstate, state);
		    buf[buf_size++] = find_in_list(L_LMODEL, data, glb);
		    break;
		case STATE_LIGHTS:
		    {
			pfLight **lights;
			int j;

			buf[buf_size++] = PF_MAX_LIGHTS;

			lights = pfGetGStateAttr(gstate, state);
			for (j = 0; j < PF_MAX_LIGHTS; j++)
			    buf[buf_size++] = find_in_list(L_LIGHT, lights[j],
							   glb);
		    }
		    break;
		case STATE_COLORTABLE:
		    data = pfGetGStateAttr(gstate, state);
		    buf[buf_size++] = find_in_list(L_CTAB, data, glb);
		    break;
		case STATE_HIGHLIGHT:
		    data = pfGetGStateAttr(gstate, state);
		    buf[buf_size++] = find_in_list(L_HLIGHT, data, glb);
		    break;
		case STATE_LPOINTSTATE:
		    data = pfGetGStateAttr(gstate, state);
		    buf[buf_size++] = find_in_list(L_LPSTATE, data, glb);
		    break;
		case STATE_TEXGEN:
		  for (j = 0 ; j < glb->maxTextures ; j ++)
		    {
		        data = pfGetGStateMultiAttr(gstate, state, j);
			if (data)
			    buf[buf_size++] = find_in_list(L_TGEN, data, glb);
			else
			    buf[buf_size++] = (-1);
		    }
		    break;
		case STATE_TEXLOD:
		  for (j = 0 ; j < glb->maxTextures ; j ++)
		    {
		        data = pfGetGStateMultiAttr(gstate, state, j);
			if (data)
			    buf[buf_size++] = find_in_list(L_TLOD, data, glb);
			else
			    buf[buf_size++] = (-1);
		    }
		    break;
	        case STATE_SHADPROG:
		    data = pfGetGStateAttr(gstate, state);
		    buf[buf_size++] = find_in_list(L_SHADPROG, data, glb);
		    break;
		case STATE_VERTEX_PROGRAM:
		case STATE_FRAGMENT_PROGRAM:
		    data = pfGetGStateAttr(gstate, state);
		    buf[buf_size++] = find_in_list(L_GPROG, data, glb);
		    break;
		case STATE_GPROGPARMS:
		    {
		        int count, j, pos;
			
			count = 0;
			pos = buf_size++; /* store count later */

			for(j=0; j<PF_N_GPPARMS; j++)
			    if(data = pfGetGStateMultiAttr(gstate, state, j))
			    {
			        count++;

				buf[buf_size++] = j;
				buf[buf_size++] = find_in_list(L_GPROGPARM, 
							       data, glb);
			    }

			buf[pos] = count;
			break;
		    }
		case STATE_TEXMAT:
		    {
			pfMatrix *m;

			/* The first word in the coding of texture matrix
			// if a flag: '1' if we have a matrix for texture 
			// unit # i. (New for perf 2.4) */

			  for (j = 0 ; j < glb->maxTextures ; j ++)
			{
			    m = (pfMatrix *)pfGetGStateMultiAttr(gstate, 
								 state, j);

			    if (m)
			    {
				buf[buf_size++] = 1;
				((float *)buf)[buf_size++] = (*m)[0][0]; 
				((float *)buf)[buf_size++] = (*m)[0][1]; 
				((float *)buf)[buf_size++] = (*m)[0][2];
				((float *)buf)[buf_size++] = (*m)[0][3];
				((float *)buf)[buf_size++] = (*m)[1][0];
				((float *)buf)[buf_size++] = (*m)[1][1];
				((float *)buf)[buf_size++] = (*m)[1][2];
				((float *)buf)[buf_size++] = (*m)[1][3];
				((float *)buf)[buf_size++] = (*m)[2][0];
				((float *)buf)[buf_size++] = (*m)[2][1];
				((float *)buf)[buf_size++] = (*m)[2][2];
				((float *)buf)[buf_size++] = (*m)[2][3];
				((float *)buf)[buf_size++] = (*m)[3][0];
				((float *)buf)[buf_size++] = (*m)[3][1];
				((float *)buf)[buf_size++] = (*m)[3][2];
				((float *)buf)[buf_size++] = (*m)[3][3];
			    }
			    else
				buf[buf_size++] = 0;
			}
		    }
		    break;
	    }
	}
    }

    buf[buf_size++] = get_udata((pfObject*)gstate, glb);

    fwrite(&buf_size, sizeof(int), 1, glb->ofp);
    fwrite(buf, sizeof(int), buf_size, glb->ofp);
}

static int pfa_write_slist_header(void *data, int size, pfa_global_t *glb)
{
    int buf[3];
    pfFlux *flux;
    pfCycleBuffer *cbuf;

    if (data && (flux = pfGetFlux(data)))
    {
	buf[0] = 0;
	buf[1] = MEM_FLUX;
	buf[2] = find_in_list(L_FLUX, flux, glb);
    }
    else if (cbuf = pfGetCBuffer(data))
    {
	buf[0] = size;
	buf[1] = MEM_CBUF;
	buf[2] = get_udata((pfObject*)cbuf, glb);
    }
    else
    {
	buf[0] = size;
	buf[1] = MEM_ARENA;
	buf[2] = -1;
    }

    fprintf(glb->ofp, "%d %d %d\n", buf[0], buf[1], buf[2]);

    if (buf[1] == MEM_FLUX)
	return 0;
    else
	return 1;
}


static int pfb_write_slist_header(void *data, int size, pfa_global_t *glb)
{
    int buf[3];
    pfFlux *flux;
    pfCycleBuffer *cbuf;

    if (data && (flux = pfGetFlux(data)))
    {
	buf[0] = 0;
	buf[1] = MEM_FLUX;
	buf[2] = find_in_list(L_FLUX, flux, glb);
    }
    else if (cbuf = pfGetCBuffer(data))
    {
	buf[0] = size;
	buf[1] = MEM_CBUF;
	buf[2] = get_udata((pfObject*)cbuf, glb);
    }
    else
    {
	buf[0] = size;
	buf[1] = MEM_ARENA;
	buf[2] = -1;
    }

    fwrite(&buf, sizeof(int), 3, glb->ofp);

    if (buf[1] == MEM_FLUX)
	return 0;
    else
	return 1;
}


static void pfa_write_llist(int *llist, int size, pfa_global_t *glb)
{
    int i;

    if (pfa_write_slist_header(llist, size, glb))
	for (i = 0; i < size; i++)
	    fprintf(glb->ofp, "%d\n", llist[i]);
}


static void pfb_write_llist(int *llist, int size, pfa_global_t *glb)
{
    if (pfb_write_slist_header(llist, size, glb))
	fwrite(llist, sizeof(int), size, glb->ofp);
}


static void pfa_write_vlist(pfVec3 *vlist, int size, pfa_global_t *glb)
{
    int i;

    if (pfa_write_slist_header(vlist, size, glb))
	for (i = 0; i < size; i++)
	    fprintf(glb->ofp, "%g %g %g\n",
		    vlist[i][0], vlist[i][1], vlist[i][2]);
}


static void pfb_write_vlist(pfVec3 *vlist, int size, pfa_global_t *glb)
{
    if (pfb_write_slist_header(vlist, size, glb))
    {
	if (glb->crypt_key)
	{
	    pfVec3 *tmp_vlist;

	    tmp_vlist = (pfVec3 *)malloc(size * sizeof(pfVec3));
	    glb->encrypt_func(size * (int)sizeof(pfVec3), glb->crypt_key,
			  vlist, tmp_vlist);
	    fwrite(tmp_vlist, sizeof(pfVec3), size, glb->ofp);
	    free(tmp_vlist);
	}
	else
	    fwrite(vlist, sizeof(pfVec3), size, glb->ofp);
    }
}


static void pfa_write_clist(pfVec4 *clist, int size, pfa_global_t *glb)
{
    int i;

    if (pfa_write_slist_header(clist, size, glb))
	for (i = 0; i < size; i++)
	    fprintf(glb->ofp, "%g %g %g %g\n",
		    clist[i][0], clist[i][1], clist[i][2], clist[i][3]);
}


static void pfb_write_clist(pfVec4 *clist, int size, pfa_global_t *glb)
{
    if (pfb_write_slist_header(clist, size, glb))
	fwrite(clist, sizeof(pfVec4), size, glb->ofp);
}


static void pfa_write_nlist(pfVec3 *nlist, int size, pfa_global_t *glb)
{
    int i;

    if (pfa_write_slist_header(nlist, size, glb))
	for (i = 0; i < size; i++)
	    fprintf(glb->ofp, "%g %g %g\n",
		    nlist[i][0], nlist[i][1], nlist[i][2]);
}


static void pfb_write_nlist(pfVec3 *nlist, int size, pfa_global_t *glb)
{
    if (pfb_write_slist_header(nlist, size, glb))
	fwrite(nlist, sizeof(pfVec3), size, glb->ofp);
}


static void pfa_write_tlist(pfVec2 *tlist, int size, pfa_global_t *glb)
{
    int i;

    if (pfa_write_slist_header(tlist, size, glb))
	for (i = 0; i < size; i++)
	    fprintf(glb->ofp, "%g %g\n", tlist[i][0], tlist[i][1]);
}


static void pfb_write_tlist(pfVec2 *tlist, int size, pfa_global_t *glb)
{
    if (pfb_write_slist_header(tlist, size, glb))
	fwrite(tlist, sizeof(pfVec2), size, glb->ofp);
}


static void pfa_write_ilist(ushort *ilist, int size, pfa_global_t *glb)
{
    int i;

    if (pfa_write_slist_header(ilist, size, glb))
	for (i = 0; i < size; i++)
	    fprintf(glb->ofp, "%d\n", ilist[i]);
}


static void pfb_write_ilist(ushort *ilist, int size, pfa_global_t *glb)
{
    if (pfb_write_slist_header(ilist, size, glb))
	fwrite(ilist, sizeof(ushort), size, glb->ofp);
}


static void pfa_write_flist(float *flist, int size, pfa_global_t *glb)
{
    int i;

    if (pfa_write_slist_header(flist, size, glb))
    {
	for (i = 0; i < size; i++)
	    fprintf(glb->ofp, "%g\n", flist[i]);
    }
}


static void pfb_write_flist(float *flist, int size, pfa_global_t *glb)
{
    if (pfb_write_slist_header(flist, size, glb))
	fwrite(flist, sizeof(float), size, glb->ofp);
}

static void pfa_write_varray_1(char *va, int size, pfa_global_t *glb)
{
    int i;
    if (pfa_write_slist_header(va, size, glb))
	for(i=0; i<size; i++)
	    fprintf(glb->ofp, "%d\n", va[i]);
}

static void pfb_write_varray_1(char *va, int size, pfa_global_t *glb)
{
    if (pfb_write_slist_header(va, size, glb))
      if(glb->crypt_key)
	{
	  char *tmp_va = (char *)malloc(size*sizeof(char));

	  glb->encrypt_func(size * (int)sizeof(char), glb->crypt_key, va, tmp_va);
	  fwrite(tmp_va, sizeof(char), size, glb->ofp);
	  free(tmp_va);
	}
      else
	fwrite(va, sizeof(char), size, glb->ofp);
}

static void pfa_write_varray_2(short *va, int size, pfa_global_t *glb)
{
    int i;
    if (pfa_write_slist_header(va, size, glb))
	for(i=0; i<size; i++)
	    fprintf(glb->ofp, "%d\n", va[i]);
}

static void pfb_write_varray_2(short *va, int size, pfa_global_t *glb)
{
    if (pfb_write_slist_header(va, size, glb))
      if(glb->crypt_key)
	{
	  short *tmp_va = (short *)malloc(size*sizeof(short));

	  glb->encrypt_func(size * (int)sizeof(short), glb->crypt_key, va, tmp_va);
	  fwrite(tmp_va, sizeof(short), size, glb->ofp);
	  free(tmp_va);
	}
      else
	fwrite(va, sizeof(short), size, glb->ofp);
}

static void pfa_write_varray_4(int *va, int size, pfa_global_t *glb)
{
    int i;
    if (pfa_write_slist_header(va, size, glb))
 	for(i=0; i<size; i++)
	    fprintf(glb->ofp, "%x\n", va[i]);	/* cast as ints */
}

static void pfb_write_varray_4(int *va, int size, pfa_global_t *glb)
{
    if (pfb_write_slist_header(va, size, glb))
      if(glb->crypt_key)
	{
	  int *tmp_va = (int *)malloc(size*sizeof(int));

	  glb->encrypt_func(size * (int)sizeof(int), glb->crypt_key, va, tmp_va);
	  fwrite(tmp_va, sizeof(int), size, glb->ofp);
	  free(tmp_va);
	}
      else
	fwrite(va, sizeof(int), size, glb->ofp);
}

static void pfa_write_asddata(void *asddata, int size, pfa_global_t *glb)
{
    int i;
    unsigned char *uc;

    pfa_write_slist_header(asddata, size, glb);
    uc = (unsigned char *)asddata;
    for (i = 0; i < size; i++)
    {
	fprintf(glb->ofp, "%c%c", hex_char[uc[i] >> 4], hex_char[uc[i] & 0xf]);
	if (i % 39 == 38)
	    fprintf(glb->ofp, "\n");
    }
    if (i % 39 != 0)
	fprintf(glb->ofp, "\n");
}


static void pfb_write_asddata(void *asddata, int size, pfa_global_t *glb)
{
    pfb_write_slist_header(asddata, size, glb);
    fwrite(asddata, 1, size, glb->ofp);
}


static void pfa_write_flux(pfFlux *flux, pfa_global_t *glb)
{
    flux_t f;
    uint sg;
    unsigned char *uc;
    int i;

    f.num_buffers = pfGetFluxNumBuffers(flux, PFFLUX_BUFFERS_SPECIFIED);
    f.data_size = (int)pfGetFluxDataSize(flux);
    f.flags = ((pfGetFluxMode(flux, PFFLUX_PUSH) == PF_ON) ?
	       FF_PUSH : 0) |
	      ((pfGetFluxMode(flux, PFFLUX_ON_DEMAND) == PF_ON) ?
	       FF_ON_DEMAND : 0) |
	      ((pfGetFluxMode(flux, PFFLUX_COPY_LAST_DATA) == PF_ON) ?
	       FF_COPY_LAST_DATA : 0);
    f.mask = pfGetFluxMask(flux);
    sg = pfGetFluxSyncGroup(flux);
    f.sync_group = find_in_list(L_SG_NAME, (void*)pfGetFluxSyncGroupName(sg),
				glb);
    f.udata = get_udata((pfObject*)flux, glb);
    uc = (unsigned char *)pfGetFluxCurData(flux);

    fprintf(glb->ofp, "%d\n", f.num_buffers);
    fprintf(glb->ofp, "%d\n", f.data_size);
    fprintf(glb->ofp, "0x%08x\n", f.flags);
    fprintf(glb->ofp, "0x%08x\n", f.mask);
    fprintf(glb->ofp, "%d\n", f.udata);
    fprintf(glb->ofp, "0x%08x\n", f.sync_group);

    for (i = 0; i < f.data_size; i++)
    {
	fprintf(glb->ofp, "%c%c", hex_char[uc[i] >> 4], hex_char[uc[i] & 0xf]);
	if (i % 39 == 38)
	    fprintf(glb->ofp, "\n");
    }
    if (i % 39 != 0)
	fprintf(glb->ofp, "\n");
}


static void pfb_write_flux(pfFlux *flux, pfa_global_t *glb)
{
    flux_t f;
    uint sg;
    unsigned char *uc;

    f.num_buffers = pfGetFluxNumBuffers(flux, PFFLUX_BUFFERS_SPECIFIED);
    f.data_size = (int)pfGetFluxDataSize(flux);
    f.flags = ((pfGetFluxMode(flux, PFFLUX_PUSH) == PF_ON) ?
	       FF_PUSH : 0) |
	      ((pfGetFluxMode(flux, PFFLUX_ON_DEMAND) == PF_ON) ?
	       FF_ON_DEMAND : 0) |
	      ((pfGetFluxMode(flux, PFFLUX_COPY_LAST_DATA) == PF_ON) ?
	       FF_COPY_LAST_DATA : 0);
    f.mask = pfGetFluxMask(flux);
    sg = pfGetFluxSyncGroup(flux);
    f.sync_group = find_in_list(L_SG_NAME, (void*)pfGetFluxSyncGroupName(sg),
				glb);
    f.udata = get_udata((pfObject*)flux, glb);
    uc = (unsigned char *)pfGetFluxCurData(flux);

    fwrite(&f, 1, sizeof(flux_t), glb->ofp);
    fwrite(uc, 1, f.data_size, glb->ofp);
}


static void pfa_write_sg_name(char *name, pfa_global_t *glb)
{
    fprintf(glb->ofp, "%s\n", name);
}


static void pfb_write_sg_name(char *name, pfa_global_t *glb)
{
    pfb_write_name(name, glb);
}


static void pfa_write_engine(pfEngine *engine, pfa_global_t *glb)
{
    engine_t e;
    engine_src_t *es;
    int i, es_size;
    void *data;
    ushort *ilist;

    e.function = find_in_table(ef_table, pfGetEngineFunction(engine));
    e.user_function = find_in_list(L_UFUNC, pfGetEngineUserFunction(engine),
				   glb);
    pfGetEngineDst(engine, &data, &ilist, &e.dst_offset, &e.dst_stride);
    if (pfIsOfType(data, pfGetFluxClassType()))
	e.dst_data_list = L_FLUX;
    else
	e.dst_data_list = find_data_list(data, glb);
    e.dst_data = find_in_list(e.dst_data_list, data, glb);
    e.dst_ilist = find_in_list(L_ILIST, ilist, glb);
    e.num_srcs = pfGetEngineNumSrcs(engine);
    pfGetEngineIterations(engine, &e.interations, &e.items);
    e.mask = pfGetEngineMask(engine);
    pfGetEngineEvaluationRange(engine, e.range, &e.range[3], &e.range[4]);
    e.flags = ((pfGetEngineMode(engine, PFENG_RANGE_CHECK) ==
	       PF_ON) ? EF_RANGE_CHECK : 0) |
	      ((pfGetEngineMode(engine, PFENG_MATRIX_MODE) ==
		PFENG_MATRIX_POST_MULT) ? EF_MATRIX_POST_MULT : 0) |
	      ((pfGetEngineMode(engine, PFENG_TIME_MODE) ==
		PFENG_TIME_SWING) ? EF_TIME_SWING : 0);
    e.udata = get_udata((pfObject*)engine, glb);

    es_size = e.num_srcs * (int)sizeof(engine_src_t);
    es = (engine_src_t *)GET_BUF(es_size);
    for (i = 0; i < e.num_srcs; i++)
    {
	pfGetEngineSrc(engine, i, &data, &ilist, &es[i].icount, &es[i].offset,
		       &es[i].stride);
	if (data != NULL)
	{
	    if (pfIsOfType(data, pfGetFluxClassType()))
	    {
		es[i].data_list = L_FLUX;
		if ((pfFlux*)data != pfGetFrameTimeFlux())
		    es[i].data = find_in_list(L_FLUX, data, glb);
		else
		    es[i].data = FRAME_TIME_FLUX;
	    }
	    else
	    {
		es[i].data_list = find_data_list(data, glb);
		es[i].data = find_in_list(es[i].data_list, data, glb);
	    }
	}
	else
	{
	    es[i].data = -1;
	    es[i].data_list = -1;
	}
	es[i].ilist = find_in_list(L_ILIST, ilist, glb);
    }

    fprintf(glb->ofp, "%d %d\n", e.function, e.user_function);
    fprintf(glb->ofp, "%d %d %d %d %d\n", e.dst_data, e.dst_data_list,
	    e.dst_ilist, e.dst_offset, e.dst_stride);
    fprintf(glb->ofp, "%d\n", e.num_srcs);
    fprintf(glb->ofp, "%d %d\n", e.interations, e.items);
    fprintf(glb->ofp, "0x%08x\n", e.mask);
    fprintf(glb->ofp, "%g %g %g %g %g\n",
	    e.range[0], e.range[1], e.range[2], e.range[3], e.range[4]);
    fprintf(glb->ofp, "0x%08x\n", e.flags);
    fprintf(glb->ofp, "%d\n", e.udata);
    for (i = 0; i < e.num_srcs; i++)
	fprintf(glb->ofp, "%d %d %d %d %d %d\n", es[i].data, es[i].data_list,
		es[i].ilist, es[i].icount, es[i].offset, es[i].stride);
}


static void pfb_write_engine(pfEngine *engine, pfa_global_t *glb)
{
    engine_t e;
    engine_src_t *es;
    int i, es_size;
    void *data;
    ushort *ilist;

    e.function = find_in_table(ef_table, pfGetEngineFunction(engine));
    e.user_function = find_in_list(L_UFUNC, pfGetEngineUserFunction(engine),
				   glb);
    pfGetEngineDst(engine, &data, &ilist, &e.dst_offset, &e.dst_stride);
    if (pfIsOfType(data, pfGetFluxClassType()))
	e.dst_data_list = L_FLUX;
    else
	e.dst_data_list = find_data_list(data, glb);
    e.dst_data = find_in_list(e.dst_data_list, data, glb);
    e.dst_ilist = find_in_list(L_ILIST, ilist, glb);
    e.num_srcs = pfGetEngineNumSrcs(engine);
    pfGetEngineIterations(engine, &e.interations, &e.items);
    e.mask = pfGetEngineMask(engine);
    pfGetEngineEvaluationRange(engine, e.range, &e.range[3], &e.range[4]);
    e.flags = ((pfGetEngineMode(engine, PFENG_RANGE_CHECK) ==
	       PF_ON) ? EF_RANGE_CHECK : 0) |
	      ((pfGetEngineMode(engine, PFENG_MATRIX_MODE) ==
		PFENG_MATRIX_POST_MULT) ? EF_MATRIX_POST_MULT : 0) |
	      ((pfGetEngineMode(engine, PFENG_TIME_MODE) ==
		PFENG_TIME_SWING) ? EF_TIME_SWING : 0);
    e.udata = get_udata((pfObject*)engine, glb);

    es_size = e.num_srcs * (int)sizeof(engine_src_t);
    es = (engine_src_t *)GET_BUF(es_size);
    for (i = 0; i < e.num_srcs; i++)
    {
	pfGetEngineSrc(engine, i, &data, &ilist, &es[i].icount, &es[i].offset,
		       &es[i].stride);
	if (data != NULL)
	{
	    if (pfIsOfType(data, pfGetFluxClassType()))
	    {
		es[i].data_list = L_FLUX;
		if ((pfFlux*)data != pfGetFrameTimeFlux())
		    es[i].data = find_in_list(L_FLUX, data, glb);
		else
		    es[i].data = FRAME_TIME_FLUX;
	    }
	    else
	    {
		es[i].data_list = find_data_list(data, glb);
		es[i].data = find_in_list(es[i].data_list, data, glb);
	    }
	}
	else
	{
	    es[i].data = -1;
	    es[i].data_list = -1;
	}
	es[i].ilist = find_in_list(L_ILIST, ilist, glb);
    }

    fwrite(&e, 1, sizeof(engine_t), glb->ofp);
    fwrite(es, 1, es_size, glb->ofp);
}


static void pfa_write_gset(pfGeoSet *gset, pfa_global_t *glb)
{
    pfVec4 *clist;
    pfVec3 *nlist, *vlist;
    pfVec2 *tlist;
    ushort *ilist;
    pfPlane *plane;
    gset_t g;

    g.ptype = find_in_table(gspt_table, pfGetGSetPrimType(gset));
    g.pcount = pfGetGSetNumPrims(gset);
    g.llist = find_in_list(L_LLIST, pfGetGSetPrimLengths(gset), glb);
    pfGetGSetAttrLists(gset, PFGS_COORD3, (void **)&vlist, &ilist);
    g.vlist[0] = find_in_table(gsb_table, pfGetGSetAttrBind(gset, PFGS_COORD3));
    g.vlist[1] = find_in_list(L_VLIST, vlist, glb);
    g.vlist[2] = find_in_list(L_ILIST, ilist, glb);
    pfGetGSetAttrLists(gset, PFGS_COLOR4, (void **)&clist, &ilist);
    g.clist[0] = find_in_table(gsb_table, pfGetGSetAttrBind(gset, PFGS_COLOR4));
    g.clist[1] = find_in_list(L_CLIST, clist, glb);
    g.clist[2] = find_in_list(L_ILIST, ilist, glb);
    pfGetGSetAttrLists(gset, PFGS_NORMAL3, (void **)&nlist, &ilist);
    g.nlist[0] = find_in_table(gsb_table,
			       pfGetGSetAttrBind(gset, PFGS_NORMAL3));
    g.nlist[1] = find_in_list(L_NLIST, nlist, glb);
    g.nlist[2] = find_in_list(L_ILIST, ilist, glb);
    pfGetGSetAttrLists(gset, PFGS_TEXCOORD2, (void **)&tlist, &ilist);
    g.tlist[0] = find_in_table(gsb_table,
			       pfGetGSetAttrBind(gset, PFGS_TEXCOORD2));
    g.tlist[1] = find_in_list(L_TLIST, tlist, glb);
    g.tlist[2] = find_in_list(L_ILIST, ilist, glb);
    g.draw_mode[0] = find_in_table(oo_table,
				   pfGetGSetDrawMode(gset, PFGS_FLATSHADE));
    g.draw_mode[1] = find_in_table(oo_table,
				   pfGetGSetDrawMode(gset, PFGS_WIREFRAME));
    g.draw_mode[2] = find_in_table(oo_table,
				   pfGetGSetDrawMode(gset, PFGS_COMPILE_GL));
    g.gstate[0] = find_in_list(L_GSTATE, pfGetGSetGState(gset), glb);
    g.gstate[1] = pfGetGSetGStateIndex(gset);
    g.line_width = pfGetGSetLineWidth(gset);
    g.point_size = pfGetGSetPntSize(gset);
    g.draw_bin = pfGetGSetDrawBin(gset);
    g.isect_mask = pfGetGSetIsectMask(gset);
    g.hlight = find_in_list(L_HLIGHT, pfGetGSetHlight(gset), glb);
    g.bbox_mode = find_in_table(bound_table, pfGetGSetBBox(gset, &g.bbox));
    g.udata = get_udata((pfObject*)gset, glb);
    g.draw_order = pfGetGSetDrawOrder(gset);
    plane = pfGetGSetDecalPlane(gset);
    g.appearance = find_in_list(L_APPEARANCE, pfGetGSetAppearance(gset), glb);
    if (plane != NULL)
    {
	g.decal_plane = 0;
	PFCOPY_VEC3(g.dplane_normal, plane->normal);
	g.dplane_offset = plane->offset;
    }
    else
    {
	g.decal_plane = -1;
	PFSET_VEC3(g.dplane_normal, 0.0f, 0.0f, 0.0f);
	g.dplane_offset = 0.0f;
    }
    g.bbox_flux = find_in_list(L_FLUX, pfGetGSetBBoxFlux(gset), glb);

    fprintf(glb->ofp, "%d %d\n", g.ptype, g.pcount);
    fprintf(glb->ofp, "%d\n", g.llist);
    fprintf(glb->ofp, "%d %d %d\n", g.vlist[0], g.vlist[1], g.vlist[2]);
    fprintf(glb->ofp, "%d %d %d\n", g.clist[0], g.clist[1], g.clist[2]);
    fprintf(glb->ofp, "%d %d %d\n", g.nlist[0], g.nlist[1], g.nlist[2]);
    fprintf(glb->ofp, "%d %d %d\n", g.tlist[0], g.tlist[1], g.tlist[2]);
    fprintf(glb->ofp, "%d %d %d\n",
	    g.draw_mode[0], g.draw_mode[1], g.draw_mode[2]);
    fprintf(glb->ofp, "%d %d\n", g.gstate[0], g.gstate[1]);
    fprintf(glb->ofp, "%.9g %.9g\n", g.line_width, g.point_size);
    fprintf(glb->ofp, "%d\n", g.draw_bin);
    fprintf(glb->ofp, "0x%08x\n", g.isect_mask);
    fprintf(glb->ofp, "%d\n", g.hlight);
    fprintf(glb->ofp, "%d %.9g %.9g %.9g %.9g %.9g %.9g\n", g.bbox_mode,
	    g.bbox.min[0], g.bbox.min[1], g.bbox.min[2],
	    g.bbox.max[0], g.bbox.max[1], g.bbox.max[2]);
    fprintf(glb->ofp, "%d\n", g.udata);
    fprintf(glb->ofp, "%d\n", g.draw_order);
    fprintf(glb->ofp, "%d %g %g %g %g\n", g.decal_plane,
	    g.dplane_normal[0], g.dplane_normal[1], g.dplane_normal[2],
	    g.dplane_offset);
    fprintf(glb->ofp, "%d\n", g.bbox_flux);
    fprintf(glb->ofp, "%d\n", g.appearance);
}


static void pfb_write_gset(pfGeoSet *gset, pfa_global_t *glb)
{
    pfVec4 	*clist;
    pfVec3 	*nlist, *vlist;
    pfVec2 	*tlist;
    ushort 	*ilist;
    pfPlane 	*plane;
    gset_t 	g;
    int		i;

    g.ptype = find_in_table(gspt_table, pfGetGSetPrimType(gset));
    g.pcount = pfGetGSetNumPrims(gset);
    g.llist = find_in_list(L_LLIST, pfGetGSetPrimLengths(gset), glb);
    pfGetGSetAttrLists(gset, PFGS_COORD3, (void **)&vlist, &ilist);
    g.vlist[0] = find_in_table(gsb_table, pfGetGSetAttrBind(gset, PFGS_COORD3));
    g.vlist[1] = find_in_list(L_VLIST, vlist, glb);
    g.vlist[2] = find_in_list(L_ILIST, ilist, glb);
    pfGetGSetAttrLists(gset, PFGS_COLOR4, (void **)&clist, &ilist);
    g.clist[0] = find_in_table(gsb_table, pfGetGSetAttrBind(gset, PFGS_COLOR4));
    g.clist[1] = find_in_list(L_CLIST, clist, glb);
    g.clist[2] = find_in_list(L_ILIST, ilist, glb);
    pfGetGSetAttrLists(gset, PFGS_NORMAL3, (void **)&nlist, &ilist);
    g.nlist[0] = find_in_table(gsb_table,
			       pfGetGSetAttrBind(gset, PFGS_NORMAL3));
    g.nlist[1] = find_in_list(L_NLIST, nlist, glb);
    g.nlist[2] = find_in_list(L_ILIST, ilist, glb);
    pfGetGSetAttrLists(gset, PFGS_TEXCOORD2, (void **)&tlist, &ilist);
    g.tlist[0] = find_in_table(gsb_table,
			       pfGetGSetAttrBind(gset, PFGS_TEXCOORD2));
    g.tlist[1] = find_in_list(L_TLIST, tlist, glb);
    g.tlist[2] = find_in_list(L_ILIST, ilist, glb);
    g.draw_mode[0] = find_in_table(oo_table,
				   pfGetGSetDrawMode(gset, PFGS_FLATSHADE));
    g.draw_mode[1] = find_in_table(oo_table,
				   pfGetGSetDrawMode(gset, PFGS_WIREFRAME));
    g.draw_mode[2] = find_in_table(oo_table,
				   pfGetGSetDrawMode(gset, PFGS_COMPILE_GL));
    g.gstate[0] = find_in_list(L_GSTATE, pfGetGSetGState(gset), glb);
    g.gstate[1] = pfGetGSetGStateIndex(gset);
    g.line_width = pfGetGSetLineWidth(gset);
    g.point_size = pfGetGSetPntSize(gset);
    g.draw_bin = pfGetGSetDrawBin(gset);
    g.isect_mask = pfGetGSetIsectMask(gset);
    g.hlight = find_in_list(L_HLIGHT, pfGetGSetHlight(gset), glb);
    g.bbox_mode = find_in_table(bound_table, pfGetGSetBBox(gset, &g.bbox));
    g.udata = get_udata((pfObject*)gset, glb);
    g.draw_order = pfGetGSetDrawOrder(gset);
    plane = pfGetGSetDecalPlane(gset);
    if (plane != NULL)
    {
	g.decal_plane = 0;
	PFCOPY_VEC3(g.dplane_normal, plane->normal);
	g.dplane_offset = plane->offset;
    }
    else
    {
	g.decal_plane = -1;
	PFSET_VEC3(g.dplane_normal, 0.0f, 0.0f, 0.0f);
	g.dplane_offset = 0.0f;
    }
    g.bbox_flux = find_in_list(L_FLUX, pfGetGSetBBoxFlux(gset), glb);

    /*
     * Fill in multi-texture coordinates - for texture units #1 and up.
     * (unit #0 alredy taken care of above).
     */
    for (i = 1 ; i < 4 ; i ++)
    {
	pfGetGSetMultiAttrLists
		(gset, PFGS_TEXCOORD2, i, (void **)&tlist, &ilist);
	g.multi_tlist[3 * (i - 1)] = find_in_table(gsb_table,
			       pfGetGSetMultiAttrBind(gset, PFGS_TEXCOORD2, i));

	g.multi_tlist[3 * (i - 1) + 1] = find_in_list(L_TLIST, tlist, glb);
	g.multi_tlist[3 * (i - 1) + 2] = find_in_list(L_ILIST, ilist, glb);
    }

    for (i = 4 ; i < glb->maxTextures ; i ++)
    {
	pfGetGSetMultiAttrLists
		(gset, PFGS_TEXCOORD2, i, (void **)&tlist, &ilist);
	g.multi_tlist2[3 * (i - 4)] = find_in_table(gsb_table,
			       pfGetGSetMultiAttrBind(gset, PFGS_TEXCOORD2, i));

	g.multi_tlist2[3 * (i - 4) + 1] = find_in_list(L_TLIST, tlist, glb);
	g.multi_tlist2[3 * (i - 4) + 2] = find_in_list(L_ILIST, ilist, glb);
    }
    
    {
      islAppearance *app = pfGetGSetAppearance(gset);
      if(app) 
	g.appearance = find_in_list(L_APPEARANCE, app, glb);
      else
	g.appearance = -1;
    }
    
    fwrite(&g, sizeof(gset_t), 1, glb->ofp);
}

static void pfa_write_geoArray(pfGeoArray *ga, pfa_global_t *glb)
{
    int nva, n, esize, std, gindex, ix, stride;
    void *ptr;
    
    /* cast as geoset, and write that part first */
    pfGeoSet *gset = (pfGeoSet*)ga;
    pfa_write_gset(gset, glb);

    /* line 1: number of varrays */
    nva = pfGetGArrayNumAttrs(ga);
    fprintf(glb->ofp, "%d\n", nva);

    /* foreach va: (glsize, gltype, gindex, standardm elemsize, listIndex, strname) */
    for(n=0; n<nva; n++){
	pfVertexAttr *va = pfGetGArrayNthAttr(ga, n);
	pfGetGArrayArrayMemSize(ga, va, &esize);
        ptr = pfGetVAttrPtr(va);
	stride = pfGetVAttrStride(va);

	if     (esize == 1) ix = find_in_list(L_VARRAY_1, ptr, glb);
	else if(esize == 2) ix = find_in_list(L_VARRAY_2, ptr, glb);
	else if(esize == 4) ix = find_in_list(L_VARRAY_4, ptr, glb);

	pfGArrayIsStandardArray(ga, va, &std, &gindex);
	
	fprintf(glb->ofp, "%d %d %d %d %d %d %d * %s\n",
		pfGetVAttrSize(va),	 /* glsize */
		pfGetVAttrDataType(va),	 /* glType (glFloat, etc) */
		gindex,   		/* gindex (texstage or generic index) */
		std,			/* if std gl array */
		esize,			/* element size (which VA list data lives) */
		ix,			/* L_VARRAY_X array index */
		stride,
	        pfGetVAttrName(va));	/* va string label */
    }
    /* index array, if any */
    if(ptr = pfGetGArrayIndexArray(ga)){
	fprintf(glb->ofp, "%d\n", find_in_list(L_VARRAY_4, ptr, glb));
    } else {
	fprintf(glb->ofp, "-1\n");
    }
}

    
static void pfb_write_geoArray(pfGeoArray *ga, pfa_global_t *glb)
{
    int nva, n, esize, std,gindex, ix, stride;
    int buf[7];
    char strbuf[256];
    void *ptr;
    

    /* cast as geoset, to write that part first */
    pfGeoSet *gset = (pfGeoSet*)ga;
    pfb_write_gset(gset, glb);


    /* line 1: number of varrays */
    nva = pfGetGArrayNumAttrs(ga);
    fwrite(&nva, sizeof(int), 1, glb->ofp);

    /* foreach va: (glsize, gltype, gindex, standardm elemsize, listIndex, strname) */
    for(n=0; n<nva; n++){
        pfVertexAttr *va = pfGetGArrayNthAttr(ga, n);
	pfGetGArrayArrayMemSize(ga, va, &esize);
        ptr = pfGetVAttrPtr(va);
	stride = pfGetVAttrStride(va);

	if     (esize == 1) ix = find_in_list(L_VARRAY_1, ptr, glb);
	else if(esize == 2) ix = find_in_list(L_VARRAY_2, ptr, glb);
	else if(esize == 4) ix = find_in_list(L_VARRAY_4, ptr, glb);

	pfGArrayIsStandardArray(ga, va, &std, &gindex);
	
	buf[0] = pfGetVAttrSize(va);	 /* glsize */
	buf[1] = pfGetVAttrDataType(va);	 /* glType (glFloat, etc) */
	buf[2] = gindex;   		/* gindex (texstage or generic index) */
	buf[3] = std;			/* if std gl array */
	buf[4] = esize;			/* element size (which VA list data lives) */
	buf[5] = ix;			/* L_VARRAY_X array index */
	buf[6] = stride;
	fwrite(buf, sizeof(int), 7, glb->ofp);
	strcpy(strbuf, pfGetVAttrName(va));	/* va string label */
	fwrite(strbuf, sizeof(char), 256, glb->ofp);
    }
    /* index array, if any */
    if(ptr = pfGetGArrayIndexArray(ga)){
	n = find_in_list(L_VARRAY_4, ptr, glb);
    } else {
	n = -1;
    }
    fwrite(&n, sizeof(int), 1, glb->ofp);
}

static void pfa_write_udata(void *udata, pfa_global_t *glb)
{
    udata_info_t *udi;
    int slot;

    udi = &glb->udi[find_in_list(L_UDATA, udata, glb)];

    if (glb->max_udata_slot > 1)
	slot = find_in_list(L_UDATA_NAME,
			    (void*)pfGetUserDataSlotName(udi->slot), glb);
    else
	slot = 0;

    switch (udi->type)
    {
	case UDT_MEMORY:
	    {
		int size;
		unsigned char *uc;
		int i;

		size = (int)pfGetSize(udata);
		fprintf(glb->ofp, "%d %d\n", size, slot);

		uc = udata;
		for (i = 0; i < size; i++)
		{
		    fprintf(glb->ofp, "%c%c",
			    hex_char[uc[i] >> 4], hex_char[uc[i] & 0xf]);
		    if (i % 39 == 38)
			fprintf(glb->ofp, "\n");
		}
		if (i % 39 != 0)
		    fprintf(glb->ofp, "\n");
	    }
	    break;
	case UDT_PF_OBJECT:
	    fprintf(glb->ofp, "-1 %d\n", slot);
	    fprintf(glb->ofp, "%d %d\n",
		    udi->list, find_in_list(udi->list, udata, glb));
	    break;
	case UDT_USER_DEFINED:
	    {
		int size_location, end;

		size_location = (int)ftell(glb->ofp);
		/* fake size */
		fprintf(glb->ofp, "%010d %d\n", 0, slot);

		glb->data_total = 0;
		glb->udfunc[udi->slot].store(udata, udi->parent, (void *)glb);
		if (glb->data_total % 39 != 0)
		    fprintf(glb->ofp, "\n");

		end = (int)ftell(glb->ofp);
		fseek(glb->ofp, size_location, SEEK_SET);
		/* real size */
		fprintf(glb->ofp, "%010d %d\n", glb->data_total, slot);
		fseek(glb->ofp, end, SEEK_SET);
	    }
	    break;
    }
}


static void pfb_write_udata(void *udata, pfa_global_t *glb)
{
    udata_info_t *udi;
    int slot;
    int buf[4];

    udi = &glb->udi[find_in_list(L_UDATA, udata, glb)];

    if (glb->max_udata_slot > 1)
	slot = find_in_list(L_UDATA_NAME,
			    (void*)pfGetUserDataSlotName(udi->slot), glb);
    else
	slot = 0;

    switch (udi->type)
    {
	case UDT_MEMORY:
	    {
		buf[0] = (int)pfGetSize(udata);
		buf[1] = slot;
		fwrite(buf, sizeof(int), 2, glb->ofp);
		fwrite(udata, 1, buf[0], glb->ofp);
	    }
	    break;
	case UDT_PF_OBJECT:
	    {
		buf[0] = -1;
		buf[1] = slot;
		buf[2] = udi->list;
		buf[3] = find_in_list(udi->list, udata, glb);
		fwrite(buf, sizeof(int), 4, glb->ofp);
	    }
	    break;
	case UDT_USER_DEFINED:
	    {
		int size_location, end;

		size_location = (int)ftell(glb->ofp);
		fseek(glb->ofp, sizeof(int) * 2, SEEK_CUR);

		glb->data_total = 0;
		glb->udfunc[udi->slot].store(udata, udi->parent, (void *)glb);

		end = (int)ftell(glb->ofp);
		fseek(glb->ofp, size_location, SEEK_SET);
		buf[0] = glb->data_total;
		buf[1] = slot;
		fwrite(buf, sizeof(int), 2, glb->ofp);
		fseek(glb->ofp, end, SEEK_SET);
	    }
	    break;
    }
}


static void pfa_write_udata_list(pfObject *parent, pfa_global_t *glb)
{
    int i, num;
    void *udata;
    int buf_size;
    int *buf;

    num = pfGetNumUserData(parent);
    buf = GET_BUF(num*2);
    buf_size = 0;

    for (i = 0; i < num; i++)
    {
	if (udata = pfGetUserDataSlot(parent, i))
	{
	    buf[buf_size++] = find_in_list(L_UDATA_NAME,
					   (void*)pfGetUserDataSlotName(i),
					   glb);
	    buf[buf_size++] = find_in_list(L_UDATA, udata, glb);
	    if (buf[buf_size-1] == -1)
	    {
		pfNotify(PFNFY_DEBUG, PFNFY_INTERNAL,
			 "pfdStoreFile_pfa: Can't find user data 0x%p in internal list\n",
			 udata);
		pfNotify(PFNFY_DEBUG, PFNFY_MORE,
			 "    parent type = %s\n",
			 pfGetTypeName(pfGetType(parent)));
		pfNotify(PFNFY_DEBUG, PFNFY_MORE,
			 "    slot = %d (%s)\n",
			 i, pfGetUserDataSlotName(i));
		pfNotify(PFNFY_DEBUG, PFNFY_MORE,
			 "    user data type = %s\n",
			 pfGetType(udata) == NULL ? "(unknown)"
			  : pfGetTypeName(pfGetType(udata)));
		buf_size -= 2;
	    }
	}
    }

    num = buf_size / 2;
    fprintf(glb->ofp, "%d\n", num);
    for (i = 0; i < buf_size; i += 2)
	fprintf(glb->ofp, "%d %d\n", buf[i], buf[i+1]);
}


static void pfb_write_udata_list(pfObject *parent, pfa_global_t *glb)
{
    int i, num;
    void *udata;
    int buf_size;
    int *buf;

    num = pfGetNumUserData(parent);
    buf = GET_BUF(num*2);
    buf_size = 0;

    for (i = 0; i < num; i++)
    {
	if (udata = pfGetUserDataSlot(parent, i))
	{
	    buf[buf_size++] = find_in_list(L_UDATA_NAME,
					   (void*)pfGetUserDataSlotName(i),
					   glb);
	    buf[buf_size++] = find_in_list(L_UDATA, udata, glb);
	    if (buf[buf_size-1] == -1)
	    {
		pfNotify(PFNFY_DEBUG, PFNFY_INTERNAL,
			 "pfdStoreFile_pfb: Can't find user data 0x%p in internal list\n",
			 udata);
		pfNotify(PFNFY_DEBUG, PFNFY_MORE,
			 "    parent type = %s\n",
			 pfGetTypeName(pfGetType(parent)));
		pfNotify(PFNFY_DEBUG, PFNFY_MORE,
			 "    slot = %d (%s)\n",
			 i, pfGetUserDataSlotName(i));
		pfNotify(PFNFY_DEBUG, PFNFY_MORE,
			 "    user data type = %s\n",
			 pfGetType(udata) == NULL ? "(unknown)"
			  : pfGetTypeName(pfGetType(udata)));
		buf_size -= 2;
	    }
	}
    }

    num = buf_size / 2;
    fwrite(&num, sizeof(int), 1, glb->ofp);
    fwrite(buf, sizeof(int), buf_size, glb->ofp);
}


static void pfa_write_udata_name(char *name, pfa_global_t *glb)
{
    fprintf(glb->ofp, "%s\n", name);
}


static void pfb_write_udata_name(char *name, pfa_global_t *glb)
{
    pfb_write_name(name, glb);
}


static void pfa_write_ufunc(void *ufunc, pfa_global_t *glb)
{
    char *name, *dso_name;

    /*
     *  Find the function is the function user registry.
     */
    pfdGetRegisteredUserFunc(ufunc, &name, &dso_name);

    pfa_write_name(name, glb);
    pfa_write_name(dso_name, glb);
}


static void pfb_write_ufunc(void *ufunc, pfa_global_t *glb)
{
    char *name, *dso_name;

    /*
     *  Find the function is the function user registry.
     */
    pfdGetRegisteredUserFunc(ufunc, &name, &dso_name);

    pfb_write_name(name, glb);
    pfb_write_name(dso_name, glb);
}


static void pfa_write_image(uint *image, int comp, pfa_global_t *glb)
{
    int size;
    unsigned char *uc;
    int i;

    size = (int)pfGetSize(image);
    fprintf(glb->ofp, "%d %d %d %d %d\n", size, comp & COMPONMENT_MASK,
	    (comp & FOUR_BYTE_COMPONENTS)? 4 : 
	    ((comp & TWO_BYTE_COMPONENTS)? 2 : 1), COMPONENT_ORDER,
	    (comp & ROW_SIZE_MASK) >> ROW_SIZE_SHIFT);

    uc = (unsigned char *)image;
    for (i = 0; i < size; i++)
    {
	fprintf(glb->ofp, "%c%c", hex_char[uc[i] >> 4], hex_char[uc[i] & 0xf]);
	if (i % 39 == 38)
	    fprintf(glb->ofp, "\n");
    }
    if (i % 39 != 0)
	fprintf(glb->ofp, "\n");
}


static void pfb_write_image(uint *image, int comp, pfa_global_t *glb)
{
    int buf[5];

    buf[0] = (int)pfGetSize(image);
    buf[1] = comp & COMPONMENT_MASK;
    buf[2] = (comp & FOUR_BYTE_COMPONENTS)? 4 : 
	((comp & TWO_BYTE_COMPONENTS)? 2 : 1);
    buf[3] = COMPONENT_ORDER;
    buf[4] = (comp & ROW_SIZE_MASK) >> ROW_SIZE_SHIFT;
    fwrite(buf, sizeof(int), 5, glb->ofp);

    if (glb->crypt_key)
    {
	uint *tmp_image;

	tmp_image = (uint *)malloc(buf[0]);
	glb->encrypt_func(buf[0], glb->crypt_key, image, tmp_image);
	fwrite(tmp_image, 1, buf[0], glb->ofp);
	free(tmp_image);
    }
    else
	fwrite(image, 1, buf[0], glb->ofp);
}


static void pfa_write_itile(pfImageTile *itile, pfa_global_t *glb)
{
/* XXX */
itile = itile;
glb = glb;
}


static void pfb_write_itile(pfImageTile *itile, pfa_global_t *glb)
{
    itile_t t;
    pfQueue *queue;
    const char *name, *fname;

    pfGetImageTileSize(itile, &t.size[0], &t.size[1], &t.size[2]);
    pfGetImageTileOrigin(itile, &t.origin[0], &t.origin[1], &t.origin[2]);
    t.mem_image_format = find_in_table(itif_table,
				       pfGetImageTileMemImageFormat(itile));
    t.mem_image_type = find_in_table(txef_table,
				     pfGetImageTileMemImageType(itile));
    pfGetImageTileMemInfo(itile, &t.mem_info[0], &t.mem_info[1]);
    if (name = pfGetImageTileName(itile))
	t.name_size = (int)strlen(name);
    else
	t.name_size = -1;
    if (fname = pfGetImageTileFileName(itile))
	t.fname_size = (int)strlen(fname);
    else
	t.fname_size = -1;
    t.file_image_format = find_in_table(itif_table,
					pfGetImageTileFileImageFormat(itile));
    t.file_image_type = find_in_table(txef_table,
				      pfGetImageTileFileImageType(itile));
    pfGetImageTileFileTile(itile, &t.file_tile[0],
			   &t.file_tile[1], &t.file_tile[2]);
    pfGetImageTileNumFileTiles(itile, &t.num_file_tiles[0],
			       &t.num_file_tiles[1], &t.num_file_tiles[2]);
    t.header_offset = pfGetImageTileHeaderOffset(itile);
    t.priority = pfGetImageTilePriority(itile);
    t.default_tile_mode = find_in_table(oo_table,
					pfGetImageTileDefaultTileMode(itile));
    t.default_tile = find_in_list(L_ITILE,
				  pfGetImageTileDefaultTile(itile), glb);
    t.read_func = find_in_list(L_UFUNC, pfGetImageTileReadFunc(itile), glb);
    queue = pfGetImageTileReadQueue(itile);
    t.read_queue = QUEUE_TO_ID(queue);
    t.udata = get_udata((pfObject*)itile, glb);

    fwrite(&t, sizeof(itile_t), 1, glb->ofp);
    if (t.name_size > 0)
	fwrite(name, sizeof(char), t.name_size, glb->ofp);
    if (t.fname_size > 0)
	fwrite(fname, sizeof(char), t.fname_size, glb->ofp);
}


static void pfa_write_icache(pfImageCache *icache, pfa_global_t *glb)
{
/* XXX */
icache = icache;
glb = glb;
}


static void pfb_write_icache(pfImageCache *icache, pfa_global_t *glb)
{
    const char *name;
    const char *format_string;
    const int *format_args;
    int *format_args_save;
    void *data;
    icache_t c;
    stream_server_t *ss;
    char *ss_names;
    int total_ss;
    int l, total_l;
    int i, j;

    name = pfGetImageCacheName(icache);

    /* Paolo on Oct 13 2003 - this can crash if name is NULL */
    c.name_size = name ? (int)strlen(name) : 0;

    pfGetImageCacheImageSize(icache, &c.image_size[0],
			     &c.image_size[1], &c.image_size[2]);
    c.proto_tile = find_in_list(L_ITILE, pfGetImageCacheProtoTile(icache), glb);
    pfGetImageCacheMemRegionSize(icache, &c.mem_region_size[0],
				 &c.mem_region_size[1], &c.mem_region_size[2]);
    pfGetImageCacheMemRegionOrg(icache, &c.mem_region_org[0],
				&c.mem_region_org[1], &c.mem_region_org[2]);
    pfGetImageCacheTexRegionSize(icache, &c.tex_region_size[0],
				 &c.tex_region_size[1], &c.tex_region_size[2]);
    pfGetImageCacheTexRegionOrg(icache, &c.tex_region_org[0],
				&c.tex_region_org[1], &c.tex_region_org[2]);
    pfGetImageCacheTex(icache, &data, &c.tex_level, &i);
    c.tex = find_in_list(L_TEX, data, glb);
    c.tex_type = find_in_table(ictt_table, i);
    pfGetImageCacheTexSize(icache, &c.tex_size[0],
			   &c.tex_size[1], &c.tex_size[2]);
    c.read_queue_func =
	    find_in_list(L_UFUNC, pfGetImageCacheReadQueueFunc(icache), glb);
    c.tile_file_name_func =
	    find_in_list(L_UFUNC, pfGetImageCacheTileFileNameFunc(icache), glb);
    pfGetImageCacheTileFileNameFormat(icache, &format_string,
				      &c.num_format_args, &format_args);
    format_args_save = GET_BUF(c.num_format_args);
    for (i = 0; i < c.num_format_args; i++)
	format_args_save[i] = find_in_table(icfnf_table, format_args[i]);
    c.format_string_size = (int)strlen(format_string);
    c.num_stream_servers[0] = pfGetImageCacheNumStreamServers(icache,
						PFIMAGECACHE_S_DIMENSION);
    c.num_stream_servers[1] = pfGetImageCacheNumStreamServers(icache,
						PFIMAGECACHE_T_DIMENSION);
    c.num_stream_servers[2] = pfGetImageCacheNumStreamServers(icache,
						PFIMAGECACHE_R_DIMENSION);
    data = pfGetImageCacheMaster(icache);
    c.master = find_in_list(L_ICACHE, data, glb);
    c.auto_center = pfGetImageCacheMode(icache, PFIMAGECACHE_AUTOCENTER);
    c.auto_stream_server_queues = pfGetImageCacheMode(icache,
				PFIMAGECACHE_AUTOCREATE_STREAMSERVER_QUEUES);
    c.auto_tile_file_name = pfGetImageCacheMode(icache,
				PFIMAGECACHE_AUTOSET_TILE_FILENAME);
    c.auto_tile_read_queue = pfGetImageCacheMode(icache,
				PFIMAGECACHE_AUTOSET_TILE_READQUEUE);
    c.udata = get_udata((pfObject*)icache, glb);

    total_l = 0;
    for (i = 0; i < c.num_stream_servers[0]; i++)
    {
	name = pfGetImageCacheFileStreamServer(icache,
					       PFIMAGECACHE_S_DIMENSION, i);
	l = (name != NULL) ? (int)strlen(name)+1 : 0;
	total_l += l;
    }
    for (i = 0; i < c.num_stream_servers[1]; i++)
    {
	name = pfGetImageCacheFileStreamServer(icache,
					       PFIMAGECACHE_T_DIMENSION, i);
	l = (name != NULL) ? (int)strlen(name)+1 : 0;
	total_l += l;
    }
    for (i = 0; i < c.num_stream_servers[2]; i++)
    {
	name = pfGetImageCacheFileStreamServer(icache,
					       PFIMAGECACHE_R_DIMENSION, i);
	l = (name != NULL) ? (int)strlen(name)+1 : 0;
	total_l += l;
    }
    total_ss = c.num_stream_servers[0] + c.num_stream_servers[1] +
	       c.num_stream_servers[2];
    c.stream_server_size = (2 * total_ss) +
			   (total_l >> 2) +
			   ((total_l & 0x3) ? 1 : 0);

    ss = (stream_server_t *)GET_BUF(c.stream_server_size);
    ss_names = (char *)(&ss[total_ss]);
    j = 0;
    for (i = 0; i < c.num_stream_servers[0]; i++, j++)
    {
	data = pfGetImageCacheStreamServerQueue(icache,
						PFIMAGECACHE_S_DIMENSION, i);
	ss[j].queue = QUEUE_TO_ID(data);
	name = pfGetImageCacheFileStreamServer(icache,
					       PFIMAGECACHE_S_DIMENSION, i);
	if (name != NULL)
	{
	    ss[j].name_length = (int)strlen(name) + 1;
	    strncpy(ss_names, name, ss[j].name_length);
	    ss_names += ss[j].name_length;
	}
	else
	    ss[j].name_length = 0;
    }
    for (i = 0; i < c.num_stream_servers[1]; i++, j++)
    {
	data = pfGetImageCacheStreamServerQueue(icache,
						PFIMAGECACHE_T_DIMENSION, i);
	ss[j].queue = QUEUE_TO_ID(data);
	name = pfGetImageCacheFileStreamServer(icache,
					       PFIMAGECACHE_T_DIMENSION, i);
	if (name != NULL)
	{
	    ss[j].name_length = (int)strlen(name) + 1;
	    strncpy(ss_names, name, ss[j].name_length);
	    ss_names += ss[j].name_length;
	}
	else
	    ss[j].name_length = 0;
    }
    for (i = 0; i < c.num_stream_servers[2]; i++, j++)
    {
	data = pfGetImageCacheStreamServerQueue(icache,
						PFIMAGECACHE_R_DIMENSION, i);
	ss[j].queue = QUEUE_TO_ID(data);
	name = pfGetImageCacheFileStreamServer(icache,
					       PFIMAGECACHE_R_DIMENSION, i);
	if (name != NULL)
	{
	    ss[j].name_length = (int)strlen(name) + 1;
	    strncpy(ss_names, name, ss[j].name_length);
	    ss_names += ss[j].name_length;
	}
	else
	    ss[j].name_length = 0;
    }

    fwrite(&c, sizeof(icache_t), 1, glb->ofp);
    if (c.name_size > 0)
	fwrite(name, sizeof(char), c.name_size, glb->ofp);
    if (c.num_format_args > 0)
	fwrite(format_args_save, sizeof(int), c.num_format_args, glb->ofp);
    if (c.format_string_size > 0)
	fwrite(format_string, sizeof(char), c.format_string_size, glb->ofp);
    if (c.stream_server_size)
	fwrite(ss, sizeof(int), c.stream_server_size, glb->ofp);
}


static void pfa_write_queue(pfQueue *queue, pfa_global_t *glb)
{
    queue_t q;

    q.elt_size = 4;	/* XXX */
    q.num_elts = pfGetQueueArrayLen(queue);
    q.udata = get_udata((pfObject*)queue, glb);

    fprintf(glb->ofp, "%d %d %d\n", q.elt_size, q.num_elts, q.udata);
}


static void pfb_write_queue(pfQueue *queue, pfa_global_t *glb)
{
    queue_t q;

    q.elt_size = 4;	/* XXX */
    q.num_elts = pfGetQueueArrayLen(queue);
    q.udata = get_udata((pfObject*)queue, glb);

    fwrite(&q, sizeof(queue_t), 1, glb->ofp);
}


static void pfa_write_morph(pfMorph *morph, pfa_global_t *glb)
{
    morph_list_t *ml;
    int num_attr;
    int num_src;
    float *alist;
    ushort *ilist;
    int nlist;
    float *weights;
    int indexed;
    int i, j;

    ml = find_morph(morph, glb);
    num_attr = pfGetMorphNumAttrs(morph);
    fprintf(glb->ofp, "%d\n", num_attr);
    for (i = 0; i < num_attr; i++)
    {
	fprintf(glb->ofp, "%d\n", ml->attr_type[i]);
	if (ml->attr_type[i] != MORPH_ATTR_MISSING)
	{
	    num_src = pfGetMorphNumSrcs(morph, i);
	    pfGetMorphSrc(morph, i, 0, &alist, &ilist, &nlist);
	    indexed = ilist ? 1 : 0;
	    fprintf(glb->ofp, "%d %d %d %d %d\n",
		    ml->fpe[i], ml->e[i], num_src, ml->id[i], indexed);
	    for (j = 0; j < num_src; j++)
	    {
		pfGetMorphSrc(morph, i, j, &alist, &ilist, &nlist);
		if (indexed)
		    fprintf(glb->ofp, "%d %d %d\n",
			    find_in_list(ml->list_id[i], alist, glb),
			    find_in_list(L_ILIST, ilist, glb), nlist);
		else
		    fprintf(glb->ofp, "%d\n",
			    find_in_list(ml->list_id[i], alist, glb));
	    }

	    weights = (float *)malloc(num_src * sizeof(float));
	    pfGetMorphWeights(morph, i, weights);
	    for (j = 0; j < num_src; j++)
		fprintf(glb->ofp, "%g\n", weights[j]);
	    free(weights);
	}
    }
}


static void pfb_write_morph(pfMorph *morph, pfa_global_t *glb)
{
    morph_list_t *ml;
    int num_attr;
    int num_src;
    float *alist;
    ushort *ilist;
    int nlist;
    float *weights;
    int indexed;
    int i, j;
    int buf_size;
    int *buf;

    ml = find_morph(morph, glb);
    num_attr = pfGetMorphNumAttrs(morph);

    for (i = 0, buf_size = 1; i < num_attr; i++)
	buf_size += (ml->attr_type[i] != MORPH_ATTR_MISSING)
		    ? (6 + 4 * pfGetMorphNumSrcs(morph, i))
		    : 1;
    buf = GET_BUF(buf_size);

    buf_size = 0;
    buf[buf_size++] = num_attr;
    for (i = 0; i < num_attr; i++)
    {
	buf[buf_size++] = ml->attr_type[i];
	if (ml->attr_type[i] != MORPH_ATTR_MISSING)
	{
	    num_src = pfGetMorphNumSrcs(morph, i);
	    pfGetMorphSrc(morph, i, 0, &alist, &ilist, &nlist);
	    indexed = ilist ? 1 : 0;
	    buf[buf_size++] = ml->fpe[i];
	    buf[buf_size++] = ml->e[i];
	    buf[buf_size++] = num_src;
	    buf[buf_size++] = ml->id[i];
	    buf[buf_size++] = indexed;
	    for (j = 0; j < num_src; j++)
	    {
		pfGetMorphSrc(morph, i, j, &alist, &ilist, &nlist);
		buf[buf_size++] = find_in_list(ml->list_id[i], alist, glb);
		if (indexed)
		{
		    buf[buf_size++] = find_in_list(L_ILIST, ilist, glb);
		    buf[buf_size++] = nlist;
		}
	    }

	    weights = (float *)malloc(num_src * sizeof(float));
	    pfGetMorphWeights(morph, i, weights);
	    for (j = 0; j < num_src; j++)
		((float *)buf)[buf_size++] = weights[j];
	    free(weights);
	}
    }

    fwrite(&buf_size, sizeof(int), 1, glb->ofp);
    fwrite(buf, sizeof(int), buf_size, glb->ofp);
}


static void pfa_write_lodstate(pfLODState *ls, pfa_global_t *glb)
{
    fprintf(glb->ofp, "%g %g %g %g %g %g %g %g\n",
	    pfGetLODStateAttr(ls, PFLODSTATE_RANGE_RANGEOFFSET),
	    pfGetLODStateAttr(ls, PFLODSTATE_RANGE_RANGESCALE),
	    pfGetLODStateAttr(ls, PFLODSTATE_TRANSITION_RANGEOFFSET),
	    pfGetLODStateAttr(ls, PFLODSTATE_TRANSITION_RANGESCALE),
	    pfGetLODStateAttr(ls, PFLODSTATE_RANGE_STRESSOFFSET),
	    pfGetLODStateAttr(ls, PFLODSTATE_RANGE_STRESSSCALE),
	    pfGetLODStateAttr(ls, PFLODSTATE_TRANSITION_STRESSOFFSET),
	    pfGetLODStateAttr(ls, PFLODSTATE_TRANSITION_STRESSSCALE));

    pfa_write_name(pfGetLODStateName(ls), glb);

    fprintf(glb->ofp, "%d\n", get_udata((pfObject*)ls, glb));
}


static void pfb_write_lodstate(pfLODState *ls, pfa_global_t *glb)
{
    float attr[8];
    int t1;

    attr[0] = pfGetLODStateAttr(ls, PFLODSTATE_RANGE_RANGEOFFSET);
    attr[1] = pfGetLODStateAttr(ls, PFLODSTATE_RANGE_RANGESCALE);
    attr[2] = pfGetLODStateAttr(ls, PFLODSTATE_TRANSITION_RANGEOFFSET);
    attr[3] = pfGetLODStateAttr(ls, PFLODSTATE_TRANSITION_RANGESCALE);
    attr[4] = pfGetLODStateAttr(ls, PFLODSTATE_RANGE_STRESSOFFSET);
    attr[5] = pfGetLODStateAttr(ls, PFLODSTATE_RANGE_STRESSSCALE);
    attr[6] = pfGetLODStateAttr(ls, PFLODSTATE_TRANSITION_STRESSOFFSET);
    attr[7] = pfGetLODStateAttr(ls, PFLODSTATE_TRANSITION_STRESSSCALE);
    fwrite(&attr, sizeof(float), 8, glb->ofp);

    pfb_write_name(pfGetLODStateName(ls), glb);

    t1 = get_udata((pfObject*)ls, glb);
    fwrite(&t1, sizeof(int), 1, glb->ofp);
}


static void pfa_write_font(pfFont *font, pfa_global_t *glb)
{
    int num_chars;
    pfBox *bbox;
    pfGeoSet *gset;
    const pfVec3 *v1;
    int t1;
    int i;

    pfa_write_name(pfGetFontAttr(font, PFFONT_NAME), glb);
    num_chars = pfGetFontMode(font, PFFONT_NUM_CHARS);
    fprintf(glb->ofp, "%d\n", num_chars);
    t1 = find_in_table(fontcs_table, pfGetFontMode(font, PFFONT_CHAR_SPACING));
    fprintf(glb->ofp, "%d\n", t1);
    fprintf(glb->ofp, "%d\n", pfGetFontMode(font, PFFONT_RETURN_CHAR));
    fprintf(glb->ofp, "%g\n", pfGetFontVal(font, PFFONT_UNIT_SCALE));
    t1 = find_in_list(L_GSTATE, pfGetFontAttr(font, PFFONT_GSTATE), glb);
    fprintf(glb->ofp, "%d\n", t1);
    bbox = pfGetFontAttr(font, PFFONT_BBOX);
    fprintf(glb->ofp, "%g %g %g\n", bbox->min[0], bbox->min[1], bbox->min[2]);
    fprintf(glb->ofp, "%g %g %g\n", bbox->max[0], bbox->max[1], bbox->max[2]);
    v1 = pfGetFontAttr(font, PFFONT_SPACING);
    fprintf(glb->ofp, "%g %g %g\n", (*v1)[0], (*v1)[1], (*v1)[2]);

    for (i = 0; i < num_chars; i++)
    {
	gset = pfGetFontCharGSet(font, i);
	if (v1 = pfGetFontCharSpacing(font, i))
	    fprintf(glb->ofp, "%d %g %g %g\n",
		    find_in_list(L_GSET, gset, glb),
		    (*v1)[0], (*v1)[1], (*v1)[2]);
	else
	    fprintf(glb->ofp, "%d 0 0 0\n", find_in_list(L_GSET, gset, glb));
    }
}


static void pfb_write_font(pfFont *font, pfa_global_t *glb)
{
    int num_chars;
    pfBox *bbox;
    pfGeoSet *gset;
    const pfVec3 *v1;
    int i;
    int buf_size;
    int *buf;

    pfb_write_name(pfGetFontAttr(font, PFFONT_NAME), glb);
    num_chars = pfGetFontMode(font, PFFONT_NUM_CHARS);
    buf_size = 0;
    buf = GET_BUF(num_chars * 5 + 64);
    buf[buf_size++] = num_chars;
    buf[buf_size++] = find_in_table(fontcs_table,
				    pfGetFontMode(font, PFFONT_CHAR_SPACING));
    buf[buf_size++] = pfGetFontMode(font, PFFONT_RETURN_CHAR);
    ((float *)buf)[buf_size++] = pfGetFontVal(font, PFFONT_UNIT_SCALE);
    buf[buf_size++] = find_in_list(L_GSTATE,
				   pfGetFontAttr(font, PFFONT_GSTATE), glb);
    bbox = pfGetFontAttr(font, PFFONT_BBOX);
    ((float *)buf)[buf_size++] = bbox->min[0];
    ((float *)buf)[buf_size++] = bbox->min[1];
    ((float *)buf)[buf_size++] = bbox->min[2];
    ((float *)buf)[buf_size++] = bbox->max[0];
    ((float *)buf)[buf_size++] = bbox->max[1];
    ((float *)buf)[buf_size++] = bbox->max[2];
    v1 = pfGetFontAttr(font, PFFONT_SPACING);
    ((float *)buf)[buf_size++] = (*v1)[0];
    ((float *)buf)[buf_size++] = (*v1)[1];
    ((float *)buf)[buf_size++] = (*v1)[2];

    for (i = 0; i < num_chars; i++)
    {
	gset = pfGetFontCharGSet(font, i);
	buf[buf_size++] = find_in_list(L_GSET, gset, glb);
	if (v1 = pfGetFontCharSpacing(font, i))
	{
	    ((float *)buf)[buf_size++] = (*v1)[0];
	    ((float *)buf)[buf_size++] = (*v1)[1];
	    ((float *)buf)[buf_size++] = (*v1)[2];
	}
	else
	{
	    ((float *)buf)[buf_size++] = 0.0f;
	    ((float *)buf)[buf_size++] = 0.0f;
	    ((float *)buf)[buf_size++] = 0.0f;
	}
    }

    fwrite(&buf_size, sizeof(int), 1, glb->ofp);
    fwrite(buf, sizeof(int), buf_size, glb->ofp);
}


static void pfa_write_string(pfString *string, pfa_global_t *glb)
{
    string_t s;
    const pfBox *bbox;
    const void *str;
    int i, size;
    const unsigned char *uc_str;

    pfGetStringMat(string, s.mat);
    s.justify = find_in_table(strj_table,
			      pfGetStringMode(string, PFSTR_JUSTIFY));
    s.char_size = find_in_table(strcs_table,
				pfGetStringMode(string, PFSTR_CHAR_SIZE));
    s.font = find_in_list(L_FONT, pfGetStringFont(string), glb);
    pfGetStringSpacingScale(string, &s.spacing_scale[0], &s.spacing_scale[1],
			    &s.spacing_scale[2]);
    pfGetStringColor(string,
		     &s.color[0], &s.color[1], &s.color[2], &s.color[3]);
    s.gstate[0] = find_in_list(L_FONT, (void *)pfGetStringGState(string), glb);
    s.gstate[1] = -1;
    s.isect_mask = pfGetStringIsectMask(string);
    bbox = pfGetStringBBox(string);
    PFCOPY_VEC3(s.bbox.min, bbox->min);
    PFCOPY_VEC3(s.bbox.max, bbox->max);

    if (str = pfGetStringString(string))
    {
	switch (pfGetStringMode(string, PFSTR_CHAR_SIZE))
	{
	    case PFSTR_CHAR:
		{
		    const char *c_str;

		    c_str = str;
		    size = (int)strlen(c_str);
		    size = (size + 1) * (int)sizeof(char);
		}
		break;
	    case PFSTR_SHORT:
		{
		    const short *s_str;

		    s_str = str;
		    for (size = 0; s_str[size] != 0; size++);
		    size = (size + 1) * (int)sizeof(short);
		}
		break;
	    case PFSTR_INT:
		{
		    const int *i_str;

		    i_str = str;
		    for (size = 0; i_str[size] != 0; size++);
		    size = (size + 1) * (int)sizeof(int);
		}
		break;
	}
    }
    else
	size = -1;

    fprintf(glb->ofp, "%.9g %.9g %.9g %.9g\n",
	   s.mat[0][0], s.mat[0][1], s.mat[0][2], s.mat[0][3]);
    fprintf(glb->ofp, "%.9g %.9g %.9g %.9g\n",
	   s.mat[1][0], s.mat[1][1], s.mat[1][2], s.mat[1][3]);
    fprintf(glb->ofp, "%.9g %.9g %.9g %.9g\n",
	   s.mat[2][0], s.mat[2][1], s.mat[2][2], s.mat[2][3]);
    fprintf(glb->ofp, "%.9g %.9g %.9g %.9g\n",
	   s.mat[3][0], s.mat[3][1], s.mat[3][2], s.mat[3][3]);
    fprintf(glb->ofp, "%d\n", s.justify);
    fprintf(glb->ofp, "%d\n", s.char_size);
    fprintf(glb->ofp, "%d\n", s.font);
    fprintf(glb->ofp, "%.9g %.9g %.9g\n",
	    s.spacing_scale[0], s.spacing_scale[1], s.spacing_scale[2]);
    fprintf(glb->ofp, "%.9g %.9g %.9g %.9g\n",
	    s.color[0], s.color[1], s.color[2], s.color[3]);
    fprintf(glb->ofp, "%d %d\n", s.gstate[0], s.gstate[1]);
    fprintf(glb->ofp, "0x%x\n", s.isect_mask);
    fprintf(glb->ofp, "%.9g %.9g %.9g\n",
	    s.bbox.min[0], s.bbox.min[1], s.bbox.min[2]);
    fprintf(glb->ofp, "%.9g %.9g %.9g\n",
	    s.bbox.max[0], s.bbox.max[1], s.bbox.max[2]);

    fprintf(glb->ofp, "%d\n", size);

    uc_str = str;
    for (i = 0; i < size; i++)
    {
	fprintf(glb->ofp, "%c%c",
		hex_char[uc_str[i] >> 4], hex_char[uc_str[i] & 0xf]);
	if (i % 39 == 38)
	    fprintf(glb->ofp, "\n");
    }
    if (i % 39 != 0)
	fprintf(glb->ofp, "\n");
}


static void pfb_write_string(pfString *string, pfa_global_t *glb)
{
    string_t s;
    const pfBox *bbox;
    const void *str;
    int size;

    pfGetStringMat(string, s.mat);
    s.justify = find_in_table(strj_table,
			      pfGetStringMode(string, PFSTR_JUSTIFY));
    s.char_size = find_in_table(strcs_table,
				pfGetStringMode(string, PFSTR_CHAR_SIZE));
    s.font = find_in_list(L_FONT, pfGetStringFont(string), glb);
    pfGetStringSpacingScale(string, &s.spacing_scale[0], &s.spacing_scale[1],
			    &s.spacing_scale[2]);
    pfGetStringColor(string,
		     &s.color[0], &s.color[1], &s.color[2], &s.color[3]);
    s.gstate[0] = find_in_list(L_FONT, (void *)pfGetStringGState(string), glb);
    s.gstate[1] = -1;
    s.isect_mask = pfGetStringIsectMask(string);
    bbox = pfGetStringBBox(string);
    PFCOPY_VEC3(s.bbox.min, bbox->min);
    PFCOPY_VEC3(s.bbox.max, bbox->max);

    if (str = pfGetStringString(string))
    {
	switch (pfGetStringMode(string, PFSTR_CHAR_SIZE))
	{
	    case PFSTR_CHAR:
		{
		    const char *c_str;

		    c_str = str;
		    size = (int)strlen(c_str);
		    size = (size + 1) * (int)sizeof(char);
		}
		break;
	    case PFSTR_SHORT:
		{
		    const short *s_str;

		    s_str = str;
		    for (size = 0; s_str[size] != 0; size++);
		    size = (size + 1) * (int)sizeof(short);
		}
		break;
	    case PFSTR_INT:
		{
		    const int *i_str;

		    i_str = str;
		    for (size = 0; i_str[size] != 0; size++);
		    size = (size + 1) * (int)sizeof(int);
		}
		break;
	}
    }
    else
	size = -1;

    fwrite(&s, sizeof(string_t), 1, glb->ofp);
    fwrite(&size, sizeof(int), 1, glb->ofp);
    if (size > 0)
	fwrite(str, 1, size, glb->ofp);
}

static void pfb_write_rep(pfRep *rep,pfa_global_t *glb,int *oldPos)
{
  int i, x, y;
  rep_t r;

  pfGetRepOrigin(rep, r.origin);
  pfGetRepOrient(rep, r.matrix);

  glb->buf = GET_BUF(*oldPos + 3 + 4*4); /* at minimum .. */

  for(i=0; i<3; i++)
    ((float *)(glb->buf))[(*oldPos)++] = r.origin[i];
   
  for(y=0; y<4; y++)
    for(x=0; x<4; x++)
      ((float *)(glb->buf))[(*oldPos)++] = r.matrix[y][x];
}

static void pfa_write_rep(pfRep *rep,pfa_global_t *glb)
{
  /* output pfRep stuff */
  pfVec3 vec;
  pfMatrix m;
  
  pfGetRepOrigin(rep, vec);
  
  fprintf(glb->ofp, "%g %g %g\n", vec[0], vec[1], vec[2]);
  
  pfGetRepOrient(rep, m);
	    
  fprintf(glb->ofp, "%g %g %g %g\n",
	  m[0][0], m[0][1], m[0][2], m[0][3]);
  fprintf(glb->ofp, "%g %g %g %g\n",
	  m[1][0], m[1][1], m[1][2], m[1][3]);
  fprintf(glb->ofp, "%g %g %g %g\n",
	  m[2][0], m[2][1], m[2][2], m[2][3]);
  fprintf(glb->ofp, "%g %g %g %g\n",
	  m[3][0], m[3][1], m[3][2], m[3][3]);
}

static int calculateSpaceForSurface(pfParaSurface *surf)
{
  int i, cntr = 0;
  int numTrimLoops = pfGetParaSurfaceTrimLoopCount(surf);

  cntr += sizeof(rep_t) / sizeof(int); /* space for pfRep information */
  cntr += 4; /* for beginU,endU,beginV,endV */
  
  for(i=0; i<numTrimLoops; i++)
    cntr += 2 + pfGetParaSurfaceTrimCurveCount(surf, i) * 4;

  return cntr;
}

static int getRealBoundaryId(pfParaSurface *surf,int boundary,pfTopo *t,
			     pfa_global_t *glb)
{
  pfBoundary *b = NULL;
  int wingCount, effectiveWingCount = 1;

  if(boundary == -1)
    return -1; /* no need to go any further processing this boundary */

  if(!t)
    return -1;

  b = pfGetTopoBoundary(t, boundary);

  if(!b)
    return -1;

  /* 
   * XXX Alex -- we could skip everything between here
   * and the find_in_list() part of this routine. If this
   * boundary isn't in the L_BOUNDARY list then it's already
   * been determined that this boundary doesn't need to be saved.
   * But traversing the entire boundary list is slower than
   * simply looking at the wing information for a single boundary.
   * So ... we do both. If we determine that the boundary
   * needs to be store then we eventually look it up in the
   * boundary list. Otherwise we can throw it away and use
   * boundaryId == -1.
   *
   */
  wingCount = pfGetBoundaryWingCount(b);
  if(wingCount < 2)
    return -1;
  else {
    /* 
     * let's make sure we're storing at least two of surfaces for this
     * boundary else don't store it.
     *
     */
    int k;
    for(k=0; k<wingCount; k++) {
      int surfId = pfGetBoundarySurface(b, k);
      pfParaSurface *s = pfGetTopoSurface(t, surfId);
      if(s && s != surf) {
	if(find_in_list(L_NODE, s, glb) != -1)
	  effectiveWingCount++;
      }
    }
  }

  if(effectiveWingCount > 1) {
    if(glb->version >= PFBV_SURFACE_CLONING)
      return pfSearch(glb->boundaryList, b);
    else
      return find_in_list(L_BOUNDARY, b, glb);
  } else
    return -1;
}

static void pfb_write_parasurface(pfParaSurface *surf,pfa_global_t *glb,
				  int *buf_pos)
{
  int i, j, numTrimCurves, *buf = glb->buf;
  pfGeoState *gState;
  surface_t s;
  edge_t e;
  pfTopo *t = NULL;

  /* write pfRep info */
  pfb_write_rep((pfRep *)surf,glb,buf_pos);

  gState = pfGetParaSurfaceGState(surf);
  if(gState != NULL)
    s.geoStateIndex = find_in_list(L_GSTATE, gState, glb);
  else
    s.geoStateIndex = -1;

  s.trimLoopCount = pfGetParaSurfaceTrimLoopCount(surf);

  /* output para surface info */
  s.topoId = pfGetParaSurfaceTopoId(surf);
  s.solidId = pfGetParaSurfaceSolidId(surf);
  s.surfaceId = pfGetParaSurfaceSurfaceId(surf);
  s.beginU = pfGetParaSurfaceBeginU(surf);
  s.endU = pfGetParaSurfaceEndU(surf);
  s.beginV = pfGetParaSurfaceBeginV(surf);
  s.endV = pfGetParaSurfaceEndV(surf);
  s.handednessHint = pfGetParaSurfaceHandednessHint(surf);

  if(s.surfaceId == -1)
    s.topoId = -1;

  t = pfGetTopoGlobalTopo(s.topoId);

  memcpy(&(buf[*buf_pos]), &s, sizeof(surface_t));
  (*buf_pos) += sizeof(surface_t) / sizeof(int);
  
  if(s.trimLoopCount) {
    for(i=0; i<s.trimLoopCount; i++) {
      numTrimCurves = pfGetParaSurfaceTrimCurveCount(surf,i);

      buf[(*buf_pos)++] = numTrimCurves;
      for(j=0; j<numTrimCurves; j++) {
	pfEdge *edge = pfGetParaSurfaceTrimCurve(surf,i,j);

	e.contCurve = find_in_list(L_CURVE2D,pfGetEdgeContCurve(edge),glb);
	e.disCurve  = find_in_list(L_DISCURVE2D,pfGetEdgeDisCurve(edge),glb);
	e.boundary = getRealBoundaryId(surf, pfGetEdgeBoundary(edge), t, glb);
	e.boundaryDir = pfGetEdgeBoundaryDir(edge);

	buf[(*buf_pos)++] = e.contCurve;
	buf[(*buf_pos)++] = e.disCurve;
	buf[(*buf_pos)++] = e.boundary;
	buf[(*buf_pos)++] = e.boundaryDir;
      }
    }
  }
  
  /* store closedness info of loops */
  for(i=0; i<s.trimLoopCount; i++)
    buf[(*buf_pos)++] = pfGetParaSurfaceTrimLoopClosed(surf,i);
}

static void pfa_write_parasurface(pfParaSurface *surf,pfa_global_t *glb)
{
  int trimLoopCount, i, gStateIndex, topoId, surfaceId;
  pfGeoState *gState;
  pfTopo *t = NULL;

  /* write pfRep info */
  pfa_write_rep((pfRep *)surf,glb);

  gState = pfGetParaSurfaceGState(surf);
  if(gState != NULL)
    gStateIndex = find_in_list(L_GSTATE, gState, glb);
  else
    gStateIndex = -1;

  trimLoopCount = pfGetParaSurfaceTrimLoopCount(surf);

  surfaceId = pfGetParaSurfaceSurfaceId(surf);
  if(surfaceId == -1)
    topoId = -1;
  else
    topoId = pfGetParaSurfaceTopoId(surf);

  t = pfGetTopoGlobalTopo(topoId);

  /* output para surface info */
  fprintf(glb->ofp,
	  "%d %d %d #topo,solid,surface ids\n"
	  "%g %g %g %g #begin/end UV\n"
	  "%d # handedness\n"
	  "%d # trim loops\n"
	  "%d # geostate\n",
	  topoId,
	  pfGetParaSurfaceSolidId(surf),
	  surfaceId,
	  pfGetParaSurfaceBeginU(surf),
	  pfGetParaSurfaceEndU(surf),
	  pfGetParaSurfaceBeginV(surf),
	  pfGetParaSurfaceEndV(surf),
	  pfGetParaSurfaceHandednessHint(surf),
	  trimLoopCount,
	  gStateIndex);
 	    
  for(i=0; i<trimLoopCount; i++) {
    int j, numTrimCurves = pfGetParaSurfaceTrimCurveCount(surf,i);

    fprintf(glb->ofp, "%d # num trim curves in loop %d\n", numTrimCurves,i);
    
    for(j=0; j<numTrimCurves; j++) {
      edge_t e;
      pfEdge *edge = pfGetParaSurfaceTrimCurve(surf,i,j);

      e.contCurve = find_in_list(L_CURVE2D,pfGetEdgeContCurve(edge),glb);
      e.disCurve  = find_in_list(L_DISCURVE2D,pfGetEdgeDisCurve(edge),glb);
      e.boundary = getRealBoundaryId(surf, pfGetEdgeBoundary(edge), t, glb);
      e.boundaryDir = pfGetEdgeBoundaryDir(edge);

      fprintf(glb->ofp,"%d %d %d %d # cont crv, dis crv, bnd index, bnd dir\n",
	      e.contCurve, e.disCurve, e.boundary, e.boundaryDir);
    }
  }
  
  /* store closedness info of loops */
  for(i=0; i<trimLoopCount; i++)
    fprintf(glb->ofp, "%d # is closed for loop %d\n", 
	    pfGetParaSurfaceTrimLoopClosed(surf,i),i);
}

static void pfa_write_node(pfNode *node, int custom, pfa_global_t *glb)
{
    int i, count;
    int t1, t2, t3;
    float f1;
    pfVec3 v1;
    float *vp;
    pfSphere sphere;
    node_trav_t nt;

    if (pfIsOfType(node, pfGetGroupClassType()))
    {
	if (pfIsOfType(node, pfGetSwitchClassType()))
	{
	    PFA_WRITE_ID(N_SWITCH | custom, node_names);
	    t1 = pfGetSwitchVal((pfSwitch*)node);
	    if (t1 == PFSWITCH_OFF)
		t1 = PFB_SWITCH_OFF;
	    else if (t1 == PFSWITCH_ON)
		t1 = PFB_SWITCH_ON;
	    fprintf(glb->ofp, "%d\n", t1);
	    fprintf(glb->ofp, "%d\n",
		    find_in_list(L_FLUX, pfGetSwitchValFlux((pfSwitch*)node),
				 glb));
	}
	else if (pfIsOfType(node, pfGetSCSClassType()))
	{
	    pfMatrix m;
	    uint m_type;

	    if (pfIsOfType(node, pfGetDCSClassType()))
	    {
		PFA_WRITE_ID(N_DCS | custom, node_names);
		m_type = pfGetDCSMatType((pfDCS *)node);
		if (m_type == PFDCS_UNCONSTRAINED)
		    fprintf(glb->ofp, "0x%x\n", CS_UNCONSTRAINED);
		else
		    fprintf(glb->ofp, "0x%x\n", to_table(mat_table, m_type));
		pfGetDCSMat((pfDCS *)node, m);
		fprintf(glb->ofp, "%g %g %g %g\n",
			m[0][0], m[0][1], m[0][2], m[0][3]);
		fprintf(glb->ofp, "%g %g %g %g\n",
			m[1][0], m[1][1], m[1][2], m[1][3]);
		fprintf(glb->ofp, "%g %g %g %g\n",
			m[2][0], m[2][1], m[2][2], m[2][3]);
		fprintf(glb->ofp, "%g %g %g %g\n",
			m[3][0], m[3][1], m[3][2], m[3][3]);
	    }
	    else if (pfIsOfType(node, pfGetFCSClassType()))
	    {
		PFA_WRITE_ID(N_FCS | custom, node_names);
		fprintf(glb->ofp, "%d\n",
			find_in_list(L_FLUX, pfGetFCSFlux((pfFCS *)node), glb));
		m_type = pfGetFCSMatType((pfFCS *)node);
		if (m_type == PFFCS_UNCONSTRAINED)
		    fprintf(glb->ofp, "0x%x\n", CS_UNCONSTRAINED);
		else
		    fprintf(glb->ofp, "0x%x\n", to_table(mat_table, m_type));
	    }
	    else /* SCS */
	    {
		PFA_WRITE_ID(N_SCS | custom, node_names);
		pfGetSCSMat((pfSCS *)node, m);
		fprintf(glb->ofp, "%g %g %g %g\n",
			m[0][0], m[0][1], m[0][2], m[0][3]);
		fprintf(glb->ofp, "%g %g %g %g\n",
			m[1][0], m[1][1], m[1][2], m[1][3]);
		fprintf(glb->ofp, "%g %g %g %g\n",
			m[2][0], m[2][1], m[2][2], m[2][3]);
		fprintf(glb->ofp, "%g %g %g %g\n",
			m[3][0], m[3][1], m[3][2], m[3][3]);
	    }
	}
	else if (pfIsOfType(node, pfGetLODClassType()))
	{
	    PFA_WRITE_ID(N_LOD | custom, node_names);
	    count = pfGetNumChildren((pfGroup *)node);
	    fprintf(glb->ofp, "%d", count);
	    for (i = 0; i <= count; i++)
		fprintf(glb->ofp, " %g", pfGetLODRange((pfLOD *)node, i));
	    fprintf(glb->ofp, "\n");
	    for (i = 0; i <= count; i++)
#ifdef XXX_FIXED_GET_LOD_TRANSITION_BUG
		fprintf(glb->ofp, " %g", pfGetLODTransition((pfLOD *)node, i));
#else
	    {
		f1 = pfGetLODTransition((pfLOD *)node, i);
		if (f1 == -1)
		    f1 = pfGetLODTransition((pfLOD *)node, 0);
		fprintf(glb->ofp, " %g", f1);
	    }
#endif
	    fprintf(glb->ofp, "\n");
	    pfGetLODCenter((pfLOD *)node, v1);
	    fprintf(glb->ofp, "%g %g %g\n", v1[0], v1[1], v1[2]);
	    fprintf(glb->ofp, "%d %d\n",
		    find_in_list(L_LODSTATE, pfGetLODLODState((pfLOD *)node),
				 glb),
		    pfGetLODLODStateIndex((pfLOD *)node));
	}
	else if (pfIsOfType(node, pfGetSeqClassType()))
	{
	    PFA_WRITE_ID(N_SEQUENCE | custom, node_names);
	    count = pfGetNumChildren((pfGroup *)node);
	    fprintf(glb->ofp, "%d", count);
	    for (i = 0; i < count; i++)
		fprintf(glb->ofp, " %g", pfGetSeqTime((pfSequence *)node, i));
	    fprintf(glb->ofp, "\n");
	    pfGetSeqInterval((pfSequence *)node, &t1, &t2, &t3);
	    fprintf(glb->ofp, "%d %d %d\n",
		    find_in_table(sqi_table, t1), t2, t3);
	    pfGetSeqDuration((pfSequence *)node, &f1, &t1);
	    fprintf(glb->ofp, "%g %d\n", f1, t1);
	    fprintf(glb->ofp, "%d\n",
		    find_in_table(sqm_table, pfGetSeqMode((pfSequence *)node)));
	}
	else if (pfIsOfType(node, pfGetLayerClassType()))
	{
	    PFA_WRITE_ID(N_LAYER | custom, node_names);
	    t1 = pfGetLayerMode((pfLayer *)node);
	    fprintf(glb->ofp, "%d %d %d\n",
		    find_in_table(layer_table, t1 & PFDECAL_MODE_MASK),
		    (t1 & PFDECAL_LAYER_MASK) >> PFDECAL_LAYER_SHIFT,
		    (t1 & PFDECAL_LAYER_OFFSET)? 1 : 0);
	}
	else if (pfIsOfType(node, pfGetMorphClassType()))
	{
	    PFA_WRITE_ID(N_MORPH | custom, node_names);
	    fprintf(glb->ofp, "%d\n", find_in_list(L_MORPH, node, glb));
	}
	else if (pfIsOfType(node, pfGetPartClassType()))
	{
	    PFA_WRITE_ID(N_PARTITION | custom, node_names);
	    if (vp = pfGetPartAttr((pfPartition *)node, PFPART_MIN_SPACING))
		fprintf(glb->ofp, "%g %g %g\n", vp[0], vp[1], vp[2]);
	    else
		fprintf(glb->ofp, "NULL\n");
	    if (vp = pfGetPartAttr((pfPartition *)node, PFPART_MAX_SPACING))
		fprintf(glb->ofp, "%g %g %g\n", vp[0], vp[1], vp[2]);
	    else
		fprintf(glb->ofp, "NULL\n");
	    if (vp = pfGetPartAttr((pfPartition *)node, PFPART_ORIGIN))
		fprintf(glb->ofp, "%g %g %g\n", vp[0], vp[1], vp[2]);
	    else
		fprintf(glb->ofp, "NULL\n");
	    fprintf(glb->ofp, "%g\n",
		    pfGetPartVal((pfPartition *)node, PFPART_FINENESS));
	}
	else if (pfIsOfType(node, pfGetSceneClassType()))
	{
	    PFA_WRITE_ID(N_SCENE | custom, node_names);
	    fprintf(glb->ofp, "%d %d\n",
		    find_in_list(L_GSTATE, pfGetSceneGState((pfScene *)node),
				 glb),
		    pfGetSceneGStateIndex((pfScene *)node));
	}
	else /* basic pfGroup */
	    PFA_WRITE_ID(N_GROUP | custom, node_names);

	count = pfGetNumChildren((pfGroup *)node);
	fprintf(glb->ofp, "%d", count);
	for (i = 0; i < count; i++)
	{
	    fprintf(glb->ofp, " %d",
		    find_in_list(L_NODE, pfGetChild((pfGroup *)node, i), glb));
	    if (i % 12 == 11 && i != count - 1)
		fprintf(glb->ofp, "\n");
	}
	if (glb->add_comments)
	    fprintf(glb->ofp, "\t# children\n");
	else
	    fprintf(glb->ofp, "\n");
    }
    else if (pfIsOfType(node, pfGetGeodeClassType()))
    {
	count = pfGetNumGSets((pfGeode *)node);
	if (pfIsOfType(node, pfGetBboardClassType()))
	{
	    pfFlux *flux;
	    int isIBRnode, isIBRproxy = 0, storeTexCoords = 0;

	    isIBRnode = pfIsOfType(node, pfGetIBRnodeClassType());

	    if (isIBRnode)
	    {
		isIBRproxy = 
		    pfGetIBRtextureFlags(
				 pfGetIBRnodeIBRtexture((pfIBRnode *)node),
				 PFIBR_USE_PROXY);
		storeTexCoords = isIBRproxy || 
		    pfGetIBRnodeFlags((pfIBRnode *)node,
				      PFIBRN_TEXCOORDS_DEFINED);
		
		PFA_WRITE_ID(N_IBR_NODE | custom, node_names);
	    }
	    else
		PFA_WRITE_ID(N_BILLBOARD | custom, node_names);

	    if (flux = pfGetBboardPosFlux((pfBillboard *)node))
	    {
		fprintf(glb->ofp, "%d\n", BB_POS_FLUX);
		fprintf(glb->ofp, "%d\n", find_in_list(L_FLUX, flux, glb));
	    }
	    else
	    {
		fprintf(glb->ofp, "%d\n", count);
		
		if(isIBRproxy)
		{
		    /* only one position is used */
		    pfGetBboardPos((pfBillboard *)node, 0, v1);
		    for (i = 0; i < count; i++)
			fprintf(glb->ofp, "%g %g %g\n", v1[0], v1[1], v1[2]);
		}
		else
		{
		    for (i = 0; i < count; i++)
		    {
			pfGetBboardPos((pfBillboard *)node, i, v1);
			fprintf(glb->ofp, "%g %g %g\n", v1[0], v1[1], v1[2]);
		    }
		}
	    }
	    t1 = pfGetBboardMode((pfBillboard *)node, PFBB_ROT);
	    fprintf(glb->ofp, "%d\n", find_in_table(bbr_table, t1));
	    pfGetBboardAxis((pfBillboard *)node, v1);
	    fprintf(glb->ofp, "%g %g %g\n", v1[0], v1[1], v1[2]);
	    
	    if (isIBRnode)
	    {
		float a1, a2;
		int n, flags;
		pfVec3 *dummy;

		fprintf(glb->ofp, "%d\n", 
			find_in_list(L_IBR_TEX, 
				     pfGetIBRnodeIBRtexture((pfIBRnode *)node),
				     glb));

		n = pfGetIBRnodeNumAngles((pfIBRnode *)node);
		fprintf(glb->ofp, "%d\n", n);

		for(i=0; i<n; i++)
		{
		    pfGetIBRnodeAngles((pfIBRnode *)node, i, &a1, &a2);
		    fprintf(glb->ofp, "%g %g\n", a1, a2);
		}

		flags = pfGetIBRnodeFlags((pfIBRnode *)node, 0xffffffff);
		fprintf(glb->ofp, "%d\n", flags);

		if(storeTexCoords)
		{
		    int       tcount, numGroups, viewsPerGroup;
		    int       j, t, v, vary;
		    pfVec2    ***texCoords;
		    pfIBRtexture *IBRtex;
		    pfDirData *viewData;

		    /* printf("writing proxy tex coords\n"); */

		    /* write the proxy texture coordinates */
		    texCoords = pfGetIBRnodeProxyTexCoords((pfIBRnode *)node);

		    IBRtex = pfGetIBRnodeIBRtexture((pfIBRnode *)node);
		    viewData = pfGetIBRtextureDirData(IBRtex);

		    numGroups = pfGetDirDataNumGroups(viewData, &viewsPerGroup);
		    /* store the number of geosets and the number of textures */
		    if(flags & PFIBRN_VARY_PROXY_GEOSETS)
			/* there is tcount geosets per group */
		    {
			if(pfGetIBRtextureFlags(IBRtex, PFIBR_NEAREST))
			{
			    pfGetDirDataDirections(viewData, &numGroups,
						   &dummy);
			    viewsPerGroup = 1;
			}

			vary = 1;
			tcount = count / numGroups;
		    }
		    else
		    {
			/* each geoset is the same for all groups */
			tcount = count;

			pfGetDirDataDirections(viewData, &numGroups, &dummy);
			viewsPerGroup = 1;
			vary = 0;
		    }

		    fprintf(glb->ofp, "%d %d %d\n", 
			    tcount, numGroups, viewsPerGroup);

		    /*printf("geosets %d, numtex %d\n", tcount, numTex);*/

		    /* for all views or grups of views */
		    for(t=0; t<numGroups; t++)
		    {
			/* for each geoset */
			for(i=0; i<tcount; i++)
			{

			    if(texCoords[t][i*viewsPerGroup] == NULL)
			    {
				fprintf(glb->ofp, "0\n");
				continue;
			    }

			    /* not varying geosets */
			    /* all views use the same geosets */
			    n = getNumGSetVertices(pfGetGSet((pfGeode *)node, 
							     i + t*tcount*vary));
			
			    /* store the number of vertices */
			    fprintf(glb->ofp, "%d\n", n);

			    for(j=0; j<viewsPerGroup; j++)
			    {
				/*printf("geoset %d, texture %d\n", i, t);*/
				for(v=0; v<n; v++)
				    fprintf(glb->ofp, "%g %g\n", 
					    texCoords[t][i*viewsPerGroup + j][v][0],
					    texCoords[t][i*viewsPerGroup + j][v][1]);
			    }
			}
		    }
		}
	    }
	
        }
	else if (pfIsOfType(node, pfGetSubdivSurfaceClassType()))
	{
	    if(glb->subdivsurface_save_geosets)
	    {
		/* save as simple geode if this flag is set */
		PFA_WRITE_ID(N_GEODE | custom, node_names);
		count = pfGetNumGSets((pfGeode *)node);
		fprintf(glb->ofp, "%d\n", count);
	    }
	    else
	    {
		/* subdivision surface */
		pfSubdivSurface *surf = (pfSubdivSurface *)node;
		float val;
		
		PFA_WRITE_ID(N_SUBDIV_SURFACE | custom, node_names);
		
		fprintf(glb->ofp, "10\n");  /* 10 lines */
		
		pfa_write_rep((pfRep *)node, glb);
		
		fprintf(glb->ofp, "%d\n", 
			pfGetSubdivSurfaceFlags(surf, 0xffffffff));
		
		pfGetSubdivSurfaceVal(surf, PFSB_SUBDIVISION_METHOD, &val);
		fprintf(glb->ofp, "%d\n", (int)val);
		
		pfGetSubdivSurfaceVal(surf, PFSB_SUBDIVISION_LEVEL, &val);
		fprintf(glb->ofp, "%d\n", (int)val);
		
		fprintf(glb->ofp, "%d\n",
			find_in_list(L_MESH, pfGetSubdivSurfaceMesh(surf),  glb));
		
		count = 0; /* no geosets XXX decide based on a flag? - later */
		fprintf(glb->ofp, "%d\n", count);
	    }
	}
	else if(pfIsOfType(node, pfGetRepClassType())) {
	  if(glb->disable_rep_save) {
	    /* save as simple geode if this flag is set */
	    PFA_WRITE_ID(N_GEODE | custom, node_names);
	    count = pfGetNumGSets((pfGeode *)node);
	    fprintf(glb->ofp, "%d\n", count);
	  } else {
	    if (pfIsOfType(node, pfGetNurbSurfaceClassType())) 
	    {
	      /* nurbs surface */
	      pfNurbSurface *nurbs = (pfNurbSurface *)node;
	      int uKnotCount, vKnotCount;
	      int uSize, vSize, ii, jj;
	      float weight;

	      PFA_WRITE_ID(N_NURB_SURFACE | custom, node_names);

	      pfa_write_parasurface((pfParaSurface *)node,glb);

	      uKnotCount = pfGetNurbSurfaceUknotCount(nurbs);
	      vKnotCount = pfGetNurbSurfaceVknotCount(nurbs);
	      uSize = pfGetNurbSurfaceControlHullUSize(nurbs);
	      vSize = pfGetNurbSurfaceControlHullVSize(nurbs);

	      fprintf(glb->ofp,"%d %d %d %d # knot counts, hull size(u,v)\n",
		      uKnotCount, vKnotCount, uSize, vSize);

	      /* write out ctrl points */
	      for(ii=0; ii<vSize; ii++) {
		for(jj=0; jj<uSize; jj++) {
		  const pfVec3 *cHull =pfGetNurbSurfaceControlHull(nurbs,jj,ii);
		  weight = pfGetNurbSurfaceWeight(nurbs,jj,ii);
		  fprintf(glb->ofp,"%g %g %g %g\n",
			  (*cHull)[0], (*cHull)[1], (*cHull)[2], weight);
		}
	      }
	    
	      /* write out u and v knots */
	      for(ii=0; ii<uKnotCount; ii++)
		fprintf(glb->ofp,"%g\n",pfGetNurbSurfaceUknot(nurbs,ii));
	      for(ii=0; ii<vKnotCount; ii++)
		fprintf(glb->ofp,"%g\n",pfGetNurbSurfaceVknot(nurbs,ii));
	      
	      count = pfGetNumGSets((pfGeode *)node);
	      fprintf(glb->ofp, "%d # nurbs geoset count\n", count);
	    }
	    else if (pfIsOfType(node, pfGetTorusSurfaceClassType()))
	    {
	      /* torus surface */
	      pfTorusSurface *torus = (pfTorusSurface *)node;
	      PFA_WRITE_ID(N_TORUS | custom, node_names);

	      pfa_write_parasurface((pfParaSurface *)node,glb);

	      fprintf(glb->ofp,"%g %g\n",
		      pfGetTorusSurfaceMinorRadius(torus),
		      pfGetTorusSurfaceMajorRadius(torus));

	      count = pfGetNumGSets((pfGeode *)node);
	      fprintf(glb->ofp, "%d\n", count);
	    }
	    else if (pfIsOfType(node, pfGetSphereSurfaceClassType()))
	    {
	      /* sphere surface */
	      pfSphereSurface *sphere = (pfSphereSurface *)node;
	      PFA_WRITE_ID(N_SPHERE_SURF | custom, node_names);

	      pfa_write_parasurface((pfParaSurface *)node,glb);

	      fprintf(glb->ofp,"%g\n", pfGetSphereSurfaceRadius(sphere));

	      count = pfGetNumGSets((pfGeode *)node);
	      fprintf(glb->ofp, "%d\n", count);
	    }
	    else if (pfIsOfType(node, pfGetConeSurfaceClassType()))
	    {
	      /* cone surface */
	      pfConeSurface *cone = (pfConeSurface *)node;
	      PFA_WRITE_ID(N_CONE | custom, node_names);

	      pfa_write_parasurface((pfParaSurface *)node,glb);

	      fprintf(glb->ofp,"%g %g\n", 
		      pfGetConeSurfaceRadius(cone), pfGetConeSurfaceHeight(cone));

	      count = pfGetNumGSets((pfGeode *)node);
	      fprintf(glb->ofp, "%d\n", count);
	    }
	    else if (pfIsOfType(node, pfGetCylinderSurfaceClassType()))
	    {
	      /* cylinder surface */
	      pfCylinderSurface *cylinder = (pfCylinderSurface *)node;
	      PFA_WRITE_ID(N_CYLINDER | custom, node_names);

	      pfa_write_parasurface((pfParaSurface *)node,glb);

	      fprintf(glb->ofp,"%g %g\n", 
		      pfGetCylinderSurfaceRadius(cylinder), 
		      pfGetCylinderSurfaceHeight(cylinder));

	      count = pfGetNumGSets((pfGeode *)node);
	      fprintf(glb->ofp, "%d\n", count);
	    }
	    else if (pfIsOfType(node, pfGetPlaneSurfaceClassType()))
	    {
	      pfPlaneSurface *plane = (pfPlaneSurface *)node;
	      float vals[5];

	      PFA_WRITE_ID(N_PLANE_SURF | custom, node_names);

	      pfa_write_parasurface((pfParaSurface *)node, glb);
	  
	      pfGetPlaneSurfacePoint1(plane, &(vals[0]), &(vals[1]), &(vals[2]),
				   &(vals[3]), &(vals[4]));
	      fprintf(glb->ofp,"%g %g %g %g %g # pt 1\n",
		      vals[0],vals[1],vals[2],vals[3],vals[4]);

	      pfGetPlaneSurfacePoint2(plane, &(vals[0]), &(vals[1]), &(vals[2]),
				   &(vals[3]));
	      fprintf(glb->ofp,"%g %g %g %g # pt 2\n",
		      vals[0],vals[1],vals[2],vals[3]);
	  
	      pfGetPlaneSurfacePoint3(plane, &(vals[0]), &(vals[1]), &(vals[2]),
				   &(vals[3]));

	      fprintf(glb->ofp,"%g %g %g %g # pt 3\n",
		      vals[0],vals[1],vals[2],vals[3]);

	      count = pfGetNumGSets((pfGeode *)node);
	      fprintf(glb->ofp, "%d\n", count);
	    }
	    else if (pfIsOfType(node, pfGetPieceWisePolySurfaceClassType())) {
	      pfPieceWisePolySurface *ppSurf = (pfPieceWisePolySurface *)node;
	      int uPatchCount, vPatchCount, uOrder, vOrder, u, v, uu, vv;
	      pfVec3 *cHull = NULL;

	      PFA_WRITE_ID(N_PIECEWISEPOLYSURFACE | custom, node_names);

	      pfa_write_parasurface((pfParaSurface *)node, glb);

	      uPatchCount = pfGetPieceWisePolySurfaceUpatchCount(ppSurf);
	      vPatchCount = pfGetPieceWisePolySurfaceVpatchCount(ppSurf);

	      fprintf(glb->ofp,"%d %d # u,v patch count\n",
		      uPatchCount,vPatchCount);

	      for(v=0; v<vPatchCount; v++) {
		for(u=0; u<uPatchCount; u++) {
		  uOrder = pfGetPieceWisePolySurfaceUorder(ppSurf, u, v);
		  vOrder = pfGetPieceWisePolySurfaceVorder(ppSurf, u, v);
		  fprintf(glb->ofp,"%d %d # uOrder,vOrder for patch %d,%d\n",
			  uOrder,vOrder,u,v);
		  for(vv=0; vv<vOrder; vv++) {
		    for(uu=0; uu<uOrder; uu++) {
		      cHull = 
		        pfGetPieceWisePolySurfaceControlHull(ppSurf,u,v,uu,vv);

		      fprintf(glb->ofp,"%g %g %g\n",(*cHull)[0],
			      (*cHull)[1], (*cHull)[2]);
		    }
		  }
		}
	      }

	      count = pfGetNumGSets((pfGeode *)node);
	      fprintf(glb->ofp, "%d\n", count);
	    }
	    else if (pfIsOfType(node, pfGetSweptSurfaceClassType())) {
	      pfSweptSurface *ss = (pfSweptSurface *)node;
	      pfCurve3d *_crossSection = NULL, *_path = NULL, *_t = NULL, *_b = NULL;
	      pfScalar *_scalar = NULL;


	      _crossSection = (pfCurve3d *)pfGetMemory(pfGetSweptSurfaceCrossSection(ss));
	      _path         = (pfCurve3d *)pfGetMemory(pfGetSweptSurfacePath(ss));
	      _t            = (pfCurve3d *)pfGetMemory(pfGetSweptSurfaceT(ss));
	      _b            = (pfCurve3d *)pfGetMemory(pfGetSweptSurfaceB(ss));
	      _scalar       = (pfScalar  *)pfGetMemory(pfGetSweptSurfaceProf(ss));

	      if(pfIsOfType(node, pfGetFrenetSweptSurfaceClassType())) {
		PFA_WRITE_ID(N_FRENET_SWEPT_SURFACE | custom, node_names);

		pfa_write_parasurface((pfParaSurface *)node, glb);
		fprintf(glb->ofp,"%d %d %d # cross section, path, scalar\n",
			find_in_list(L_NODE, _crossSection, glb),
			find_in_list(L_NODE, _path, glb),
			find_in_list(L_SCALAR, _scalar, glb));
	      } else {
		PFA_WRITE_ID(N_SWEPT_SURFACE | custom, node_names);

		pfa_write_parasurface((pfParaSurface *)node, glb);
		fprintf(glb->ofp,"%d %d %d %d %d # cross section, path, t, b, scalar\n",
			find_in_list(L_NODE, _crossSection, glb),
			find_in_list(L_NODE, _path, glb),
			find_in_list(L_NODE, _t, glb),
			find_in_list(L_NODE, _b, glb),
			find_in_list(L_SCALAR, _scalar, glb));
	      }

	      count = pfGetNumGSets((pfGeode *)node);
	      fprintf(glb->ofp, "%d\n", count);
	    }
	    else if (pfIsOfType(node, pfGetCoonsSurfaceClassType())) {
	      pfCoonsSurface *coons = (pfCoonsSurface *)node;
	      pfCurve3d *_r = NULL, *_l = NULL, *_b = NULL, *_t = NULL;

	      PFA_WRITE_ID(N_COONS | custom, node_names);
	      
	      pfa_write_parasurface((pfParaSurface *)node, glb);

	      _r = pfGetCoonsSurfaceRight (coons);
	      _l = pfGetCoonsSurfaceLeft  (coons);
	      _b = pfGetCoonsSurfaceBottom(coons);
	      _t = pfGetCoonsSurfaceTop   (coons);

	      fprintf(glb->ofp,"%d %d %d %d # right,left,bottom,top curves\n",
			find_in_list(L_NODE, _r, glb),
			find_in_list(L_NODE, _l, glb),
			find_in_list(L_NODE, _t, glb),
			find_in_list(L_NODE, _b, glb));

	      count = pfGetNumGSets((pfGeode *)node);
	      fprintf(glb->ofp, "%d\n", count);
	    }

	    else if (pfIsOfType(node, pfGetRuledSurfaceClassType())) {
	      pfRuledSurface *ruled = (pfRuledSurface *)node;
	      pfCurve3d *_c1 = NULL, *_c2 = NULL;

	      PFA_WRITE_ID(N_RULED | custom, node_names);
	      
	      pfa_write_parasurface((pfParaSurface *)node, glb);

	      _c1 = (pfCurve3d *)pfGetMemory(pfGetRuledSurfaceCurve1 (ruled));
	      _c2 = (pfCurve3d *)pfGetMemory(pfGetRuledSurfaceCurve2 (ruled));

	      fprintf(glb->ofp,"%d %d # curve1 & curve2\n",
		      find_in_list(L_NODE, _c1, glb),
		      find_in_list(L_NODE, _c2, glb));

	      count = pfGetNumGSets((pfGeode *)node);
	      fprintf(glb->ofp, "%d\n", count);
	    }

	    else if (pfIsOfType(node, pfGetNurbCurve3dClassType()))
	    {
	      PFA_WRITE_ID(N_NURB_CURVE3D | custom, node_names);
	      pfa_write_nurb_curve3d((pfNurbCurve3d *)node, glb);
	      count = pfGetNumGSets((pfGeode *)node);
	      fprintf(glb->ofp,"%d\n",count);
	    }
	    else if (pfIsOfType(node, pfGetLine3dClassType()))
	    {
	      if(pfIsOfType(node, pfGetOrientedLine3dClassType()))
		PFA_WRITE_ID(N_ORIENTEDLINE3D | custom, node_names);
	      else
		PFA_WRITE_ID(N_LINE3D | custom, node_names);

	      pfa_write_curve3d((pfCurve3d *)node, glb);
	      {
		pfReal x,y,z,t;

		pfGetLine3dPoint1((pfLine3d *)node, &x, &y, &z, &t);
		fprintf(glb->ofp,"%g %g %g %g\n",x,y,z,t);
		
		pfGetLine3dPoint2((pfLine3d *)node, &x, &y, &z, &t);
		fprintf(glb->ofp,"%g %g %g %g\n",x,y,z,t);
		
		if(pfIsOfType(node, pfGetOrientedLine3dClassType())) {
		  pfGetOrientedLine3dUpPoint((pfOrientedLine3d *)node,
					     &x, &y, &z);
		  fprintf(glb->ofp,"%g %g %g\n",x,y,z);
		}
	      }

	      count = pfGetNumGSets((pfGeode *)node);
	      fprintf(glb->ofp,"%d\n",count);
	    }
	    else if (pfIsOfType(node, pfGetCircle3dClassType()))
	    {
	      PFA_WRITE_ID(N_CIRCLE3D | custom, node_names);

	      pfa_write_curve3d((pfCurve3d *)node, glb);

	      fprintf(glb->ofp,"%g # radius\n", 
		      pfGetCircle3dRadius((pfCircle3d *)node));

	      count = pfGetNumGSets((pfGeode *)node);
	      fprintf(glb->ofp,"%d\n",count);
	    }
	    else if (pfIsOfType(node, pfGetSuperQuadCurve3dClassType()))
	    {
	      PFA_WRITE_ID(N_SUPERQUADCURVE3D | custom, node_names);

	      pfa_write_curve3d((pfCurve3d *)node, glb);

	      fprintf(glb->ofp,"%g %g # radius, exponent\n",
		      pfGetSuperQuadCurve3dRadius((pfSuperQuadCurve3d *)node),
		      pfGetSuperQuadCurve3dExponent((pfSuperQuadCurve3d *)node));

	      count = pfGetNumGSets((pfGeode *)node);
	      fprintf(glb->ofp,"%d\n",count);
	    }
	    else if (pfIsOfType(node, pfGetCompCurve3dClassType()))
	    {
	      PFA_WRITE_ID(N_COMPOSITE_CURVE3D | custom, node_names);
	      pfa_write_curve3d((pfCurve3d *)node, glb);
	      {
		int sId, cId;
		pfParaSurface *s = pfGetCompCurve3dParaSurface((pfCompositeCurve3d *)node);
		pfCurve2d *c = pfGetCompCurve3dCurve2d((pfCompositeCurve3d *)node);
		
		sId = find_in_list(L_NODE, s, glb);
		cId = find_in_list(L_CURVE2D, c, glb); /* XXX Alex -- or should we look in L_NODE? */

		fprintf(glb->ofp,"%d %d # surface id, curve id\n",sId, cId);
	      }
	      count = pfGetNumGSets((pfGeode *)node);
	      fprintf(glb->ofp, "%d\n",count);
	    }
	    else if (pfIsOfType(node, pfGetHsplineSurfaceClassType()))
	    {
	      int uKnotCount, vKnotCount;
	      pfHsplineSurface *hSurf = (pfHsplineSurface *)node;
	      int _i, _j;
	      pfVec3 v;

	      PFA_WRITE_ID(N_HSPLINE_SURFACE | custom, node_names);

	      pfa_write_parasurface((pfParaSurface *)node, glb);
	      
	      uKnotCount = pfGetHsplineSurfaceUknotCount(hSurf);
	      vKnotCount = pfGetHsplineSurfaceVknotCount(hSurf);

	      fprintf(glb->ofp,
		      "%d %d %d # uKnotCount, vKnotCount, is cylindrical\n",
		      uKnotCount, vKnotCount,
		      pfGetHsplineSurfaceCylindrical(hSurf));

	      for(_i=0; _i<uKnotCount; _i++) {
		for(_j=0; _j<vKnotCount; _j++) {
		  pfGetHsplineSurfaceP(hSurf, _i, _j, v);
		  fprintf(glb->ofp,"%g %g %g\n",v[0],v[1],v[2]);
		}
	      }

	      for(_i=0; _i<uKnotCount; _i++) {
		for(_j=0; _j<vKnotCount; _j++) {
		  pfGetHsplineSurfaceTu(hSurf, _i, _j, v);
		  fprintf(glb->ofp,"%g %g %g\n",v[0],v[1],v[2]);
		}
	      }

	      for(_i=0; _i<uKnotCount; _i++) {
		for(_j=0; _j<vKnotCount; _j++) {
		  pfGetHsplineSurfaceTv(hSurf, _i, _j, v);
		  fprintf(glb->ofp,"%g %g %g\n",v[0],v[1],v[2]);
		}
	      }

	      for(_i=0; _i<uKnotCount; _i++) {
		for(_j=0; _j<vKnotCount; _j++) {
		  pfGetHsplineSurfaceTuv(hSurf, _i, _j, v);
		  fprintf(glb->ofp,"%g %g %g\n",v[0],v[1],v[2]);
		}
	      }

	      for(_i=0; _i<uKnotCount; _i++)
		fprintf(glb->ofp,"%g\n",pfGetHsplineSurfaceUknot(hSurf, _i));

	      for(_i=0; _i<vKnotCount; _i++)
		fprintf(glb->ofp,"%g\n",pfGetHsplineSurfaceVknot(hSurf, _i));

	      count = pfGetNumGSets((pfGeode *)node);
	      fprintf(glb->ofp, "%d\n", count);
	    }
	    else if (pfIsOfType(node, pfGetNurbCurve2dClassType()))
	    {
	      PFA_WRITE_ID(N_NURB_CURVE2D | custom, node_names);	  
	      pfa_write_nurb_curve2d((pfNurbCurve2d *)node,glb);
	      count = pfGetNumGSets((pfGeode *)node);
	      fprintf(glb->ofp, "%d\n", count);
	    }
	  }
	}
	else /* basic pfGeode */
	{
	    PFA_WRITE_ID(N_GEODE | custom, node_names);
	    fprintf(glb->ofp, "%d\n", count);
	}

	for (i = 0; i < count; i++)
	{
	    fprintf(glb->ofp, " %d",
		    find_in_list(L_GSET, pfGetGSet((pfGeode *)node, i), glb));
	    if (i % 12 == 11 && i != count - 1)
		fprintf(glb->ofp, "\n");
	}
	if (glb->add_comments)
	    fprintf(glb->ofp, "\t# gsets\n");
	else
	    fprintf(glb->ofp, "\n");
    }
    else if (pfIsOfType(node, pfGetLPointClassType()))
    {
	/*
	 *  Turn Light Point Node into a Geode
	 */
	PFA_WRITE_ID(N_GEODE | custom, node_names);
	fprintf(glb->ofp, "1 %d\n",
		find_in_list(L_GSET, pfGetLPointGSet((pfLightPoint *)node),
			     glb));
    }
    else if (pfIsOfType(node, pfGetTextClassType()))
    {
	PFA_WRITE_ID(N_TEXT | custom, node_names);
	count = pfGetNumStrings((pfText *)node);
	fprintf(glb->ofp, "%d\n", count);
	for (i = 0; i < count; i++)
	    fprintf(glb->ofp, "%d\n",
		    find_in_list(L_STRING, pfGetString((pfText *)node, i),
				 glb));
    }
    else if (pfIsOfType(node, pfGetLSourceClassType()))
    {
	PFA_WRITE_ID(N_LIGHTSOURCE | custom, node_names);
	fprintf(glb->ofp, "%d\n", find_in_list(L_LSOURCE, node, glb));
    }
    else if (pfIsOfType(node, pfGetASDClassType()))
    {
	int bind, size, o_size, pv_size, o_type, pv_type;
	void *attr, *o_attr, *pv_attr;

	PFA_WRITE_ID(N_ASD | custom, node_names);

	pfGetASDAttr((pfASD *)node, PFASD_LODS, &bind, &size, &attr);
	fprintf(glb->ofp, "%d %d %d\n",
		bind, size, find_in_list(L_ASDDATA, attr, glb));

	pfGetASDAttr((pfASD *)node, PFASD_COORDS, &bind, &size, &attr);
	fprintf(glb->ofp, "%d %d %d\n",
		bind, size, find_in_list(L_ASDDATA, attr, glb));

	pfGetASDAttr((pfASD *)node, PFASD_FACES, &bind, &size, &attr);
	fprintf(glb->ofp, "%d %d %d\n",
		bind, size, find_in_list(L_ASDDATA, attr, glb));

	o_attr = pv_attr = NULL;
	o_size = pv_size = 0;
	o_type = pv_type = 0;
	pfGetASDAttr((pfASD *)node, PFASD_PER_VERTEX_ATTR, &bind, &size, &attr);
	if (attr && size && bind )
	{
	    pv_attr = attr;
	    pv_size = size;
	    pv_type = (bind&PFASD_NORMALS ? ASD_ATTR_NORMALS:0) |
		      (bind&PFASD_COLORS ? ASD_ATTR_COLORS:0) |
		      (bind&PFASD_TCOORDS ? ASD_ATTR_TCOORDS:0);
	}
	pfGetASDAttr((pfASD *)node, PFASD_OVERALL_ATTR, &bind, &size, &attr);
	if (attr && size && bind )
	{
	    o_attr = attr;
	    o_size = size;
	    o_type = (bind&PFASD_NORMALS ? ASD_ATTR_NORMALS:0) |
		     (bind&PFASD_COLORS ? ASD_ATTR_COLORS:0) |
		     (bind&PFASD_TCOORDS ? ASD_ATTR_TCOORDS:0);
	}
	fprintf(glb->ofp, "%d %d %d\n",
		o_type, o_size, find_in_list(L_ASDDATA, o_attr, glb));
	fprintf(glb->ofp, "%d %d %d\n",
		pv_type, pv_size, find_in_list(L_ASDDATA, pv_attr, glb));

	fprintf(glb->ofp, "%d\n", pfGetASDNumBaseFaces((pfASD *)node));
	t1 = pfGetASDMorphAttrs((pfASD *)node);
	if (t1 == -1)
	    t2 = -1;
	else
	{
	    t2 = 0;
	    if (t1 & PFASD_NORMALS)
		t2 |= ASD_ATTR_NORMALS;
	    if (t1 & PFASD_COLORS)
		t2 |= ASD_ATTR_COLORS;
	    if (t1 & PFASD_TCOORDS)
		t2 |= ASD_ATTR_TCOORDS;
	}
	fprintf(glb->ofp, "%d\n", t2);
	count = pfGetASDNumGStates((pfASD *)node);
	fprintf(glb->ofp, "%d\n", count);
	for (i = 0; i < count; i++)
	    fprintf(glb->ofp, "%d\n",
		    find_in_list(L_GSTATE, pfGetASDGState((pfASD *)node, i), glb));
	{
	    int m;
	    float w;
	    pfGetASDMaxMorphDepth((pfASD *)node, &m, &w);
	    fprintf(glb->ofp, "%d\n", m);
	}
#if 0
	fprintf(glb->ofp, "%d\n", pfGetASDMaxMorphDepth((pfASD *)node));
#endif
	fprintf(glb->ofp, "%d\n",
		find_in_table(asd_em_table, pfGetASDEvalMethod((pfASD *)node)));
	fprintf(glb->ofp, "%d\n",
		find_in_list(L_UFUNC, pfGetASDEvalFunc((pfASD *)node), glb));
#if 0
	t1 = pfGetASDSyncGroup((pfASD *)node);
	fprintf(glb->ofp, "%d\n",
		find_in_list(L_SG_NAME,
			     (void*)pfGetFluxSyncGroupName((uint)t1), glb));
#endif
    }
    else
    {
	/* XXX unsuported node type */
    }

    fprintf(glb->ofp, "0x%x 0x%x 0x%x 0x%x\n",
	    pfGetNodeTravMask(node, PFTRAV_ISECT),
	    pfGetNodeTravMask(node, PFTRAV_APP),
	    pfGetNodeTravMask(node, PFTRAV_CULL),
	    pfGetNodeTravMask(node, PFTRAV_DRAW));

    pfGetNodeTravFuncs(node, PFTRAV_APP, &nt.app_pre, &nt.app_post);
    pfGetNodeTravFuncs(node, PFTRAV_CULL, &nt.cull_pre, &nt.cull_post);
    pfGetNodeTravFuncs(node, PFTRAV_DRAW, &nt.draw_pre, &nt.draw_post);
    pfGetNodeTravFuncs(node, PFTRAV_ISECT, &nt.isect_pre, &nt.isect_post);
    nt.app_data = pfGetNodeTravData(node, PFTRAV_APP);
    nt.cull_data = pfGetNodeTravData(node, PFTRAV_CULL);
    nt.draw_data = pfGetNodeTravData(node, PFTRAV_DRAW);
    nt.isect_data = pfGetNodeTravData(node, PFTRAV_ISECT);

    if (nt.app_pre != NULL || nt.app_post != NULL || nt.app_data != NULL ||
	nt.cull_pre != NULL || nt.cull_post != NULL || nt.cull_data != NULL ||
	nt.draw_pre != NULL || nt.draw_post != NULL || nt.draw_data != NULL ||
	nt.isect_pre != NULL || nt.isect_post != NULL || nt.isect_data != NULL)
    {
	/*
	 *  has trav funcs and/or trav data
	 */
	fprintf(glb->ofp, "1 %x %x %x %x %x %x %x %x %x %x %x %x\n",
		find_in_list(L_UFUNC, nt.app_pre, glb),
		find_in_list(L_UFUNC, nt.app_post, glb),
		find_in_list(L_UDATA, nt.app_data, glb),
		find_in_list(L_UFUNC, nt.cull_pre, glb),
		find_in_list(L_UFUNC, nt.cull_post, glb),
		find_in_list(L_UDATA, nt.cull_data, glb),
		find_in_list(L_UFUNC, nt.draw_pre, glb),
		find_in_list(L_UFUNC, nt.draw_post, glb),
		find_in_list(L_UDATA, nt.draw_data, glb),
		find_in_list(L_UFUNC, nt.isect_pre, glb),
		find_in_list(L_UFUNC, nt.isect_post, glb),
		find_in_list(L_UDATA, nt.isect_data, glb));
    }
    else
	/*
	 *  does not have trav funcs or trav data
	 */
	fprintf(glb->ofp, "0 -1 -1 -1 -1 -1 -1 -1 -1 -1 -1 -1 -1\n");

    fprintf(glb->ofp, "%d", get_udata((pfObject*)node, glb));
    if (glb->add_comments)
	fprintf(glb->ofp, "\t# user data\n");
    else
	fprintf(glb->ofp, "\n");

    t1 = find_in_table(bound_table, pfGetNodeBSphere(node, &sphere));
    fprintf(glb->ofp, "%d %g %g %g %g\n",
	    t1, sphere.center[0], sphere.center[1], sphere.center[2],
	    sphere.radius);

    pfa_write_name(pfGetNodeName(node), glb);

    if (custom & N_CUSTOM)
    {
	int size_location, end;

	size_location = (int)ftell(glb->ofp);
	fprintf(glb->ofp, "%010d\n", 0);		/* fake size */

	glb->data_total = 0;
	i = (custom & N_CUSTOM_MASK) >> N_CUSTOM_SHIFT;
	if (glb->custom_nodes[i].store_func)
	    glb->custom_nodes[i].store_func(node, glb);
	else
	    pfNotify(PFNFY_WARN, PFNFY_PRINT,
		     "%s Found \"%s\" custom node but custom store is NULL\n.",
		     "pfdStoreFile_pfb: ", glb->custom_nodes[i].name);
	if (glb->data_total % 39 != 0)
	    fprintf(glb->ofp, "\n");

	end = (int)ftell(glb->ofp);
	fseek(glb->ofp, size_location, SEEK_SET);
	fprintf(glb->ofp, "%010d\n", glb->data_total);	/* real size */
	fseek(glb->ofp, end, SEEK_SET);
    }
}


static void pfb_write_node(pfNode *node, int custom, pfa_global_t *glb)
{
    int i, count, vary;
    int t1, t2, t3;
    float f1;
    pfVec3 v1;
    float *vp;
    pfSphere sphere;
    int buf_size, spaceReqmnt = -1;
    int *buf;
    node_trav_t nt;

    buf_size = 0;
    buf = glb->buf;

    if (pfIsOfType(node, pfGetGroupClassType()))
    {
	count = pfGetNumChildren((pfGroup *)node);
	buf = GET_BUF(count * 3 + 128);

	if (pfIsOfType(node, pfGetSwitchClassType()))
	{
	    buf[buf_size++] = N_SWITCH | custom;
	    t1 = pfGetSwitchVal((pfSwitch*)node);
	    if (t1 == PFSWITCH_OFF)
		t1 = PFB_SWITCH_OFF;
	    else if (t1 == PFSWITCH_ON)
		t1 = PFB_SWITCH_ON;
	    buf[buf_size++] = t1;
	    buf[buf_size++] = find_in_list(L_FLUX,
					   pfGetSwitchValFlux((pfSwitch*)node),
					   glb);
	}
	else if (pfIsOfType(node, pfGetSCSClassType()))
	{
	    pfMatrix m;
	    uint m_type;

	    if (pfIsOfType(node, pfGetDCSClassType()))
	    {
		buf[buf_size++] = N_DCS | custom;
		m_type = pfGetDCSMatType((pfDCS *)node);
		if (m_type == PFDCS_UNCONSTRAINED)
		    ((uint*)buf)[buf_size++] = CS_UNCONSTRAINED;
		else
		    ((uint*)buf)[buf_size++] = to_table(mat_table, m_type);
		pfGetDCSMat((pfDCS *)node, m);
		((float *)buf)[buf_size++] = m[0][0];
		((float *)buf)[buf_size++] = m[0][1];
		((float *)buf)[buf_size++] = m[0][2];
		((float *)buf)[buf_size++] = m[0][3];
		((float *)buf)[buf_size++] = m[1][0];
		((float *)buf)[buf_size++] = m[1][1];
		((float *)buf)[buf_size++] = m[1][2];
		((float *)buf)[buf_size++] = m[1][3];
		((float *)buf)[buf_size++] = m[2][0];
		((float *)buf)[buf_size++] = m[2][1];
		((float *)buf)[buf_size++] = m[2][2];
		((float *)buf)[buf_size++] = m[2][3];
		((float *)buf)[buf_size++] = m[3][0];
		((float *)buf)[buf_size++] = m[3][1];
		((float *)buf)[buf_size++] = m[3][2];
		((float *)buf)[buf_size++] = m[3][3];
	    }
	    else if (pfIsOfType(node, pfGetFCSClassType()))
	    {
		buf[buf_size++] = N_FCS | custom;
		buf[buf_size++] = find_in_list(L_FLUX,
					       pfGetFCSFlux((pfFCS *)node),
					       glb);
		m_type = pfGetFCSMatType((pfFCS *)node);
		if (m_type == PFFCS_UNCONSTRAINED)
		    ((uint*)buf)[buf_size++] = CS_UNCONSTRAINED;
		else
		    ((uint *)buf)[buf_size++] = to_table(mat_table, m_type);
	    }
	    else /* SCS */
	    {
		buf[buf_size++] = N_SCS | custom;
		pfGetSCSMat((pfSCS *)node, m);
		((float *)buf)[buf_size++] = m[0][0];
		((float *)buf)[buf_size++] = m[0][1];
		((float *)buf)[buf_size++] = m[0][2];
		((float *)buf)[buf_size++] = m[0][3];
		((float *)buf)[buf_size++] = m[1][0];
		((float *)buf)[buf_size++] = m[1][1];
		((float *)buf)[buf_size++] = m[1][2];
		((float *)buf)[buf_size++] = m[1][3];
		((float *)buf)[buf_size++] = m[2][0];
		((float *)buf)[buf_size++] = m[2][1];
		((float *)buf)[buf_size++] = m[2][2];
		((float *)buf)[buf_size++] = m[2][3];
		((float *)buf)[buf_size++] = m[3][0];
		((float *)buf)[buf_size++] = m[3][1];
		((float *)buf)[buf_size++] = m[3][2];
		((float *)buf)[buf_size++] = m[3][3];
	    }
	}
	else if (pfIsOfType(node, pfGetDoubleSCSClassType()))
	{
	    pfMatrix4d 	m;
	    uint 	m_type;
	    double	dbuff[16];
	    int		dbuff_index;

	    if (pfIsOfType(node, pfGetDoubleDCSClassType()))
	    {
		buf[buf_size++] = N_DOUBLE_DCS | custom;
		m_type = pfGetDoubleDCSMatType((pfDoubleDCS *)node);
		if (m_type == PFDCS_UNCONSTRAINED)
		    ((uint*)buf)[buf_size++] = CS_UNCONSTRAINED;
		else
		    ((uint*)buf)[buf_size++] = to_table(mat_table, m_type);
		pfGetDoubleDCSMat((pfDoubleDCS *)node, m);

		/*
		 *	Must do double-copy because & buf[buf_size] may not 
		 *	be a double-word address.
		 */
		dbuff_index = 0;
		dbuff[dbuff_index ++] = m[0][0];
		dbuff[dbuff_index ++] = m[0][1];
		dbuff[dbuff_index ++] = m[0][2];
		dbuff[dbuff_index ++] = m[0][3];
		dbuff[dbuff_index ++] = m[1][0];
		dbuff[dbuff_index ++] = m[1][1];
		dbuff[dbuff_index ++] = m[1][2];
		dbuff[dbuff_index ++] = m[1][3];
		dbuff[dbuff_index ++] = m[2][0];
		dbuff[dbuff_index ++] = m[2][1];
		dbuff[dbuff_index ++] = m[2][2];
		dbuff[dbuff_index ++] = m[2][3];
		dbuff[dbuff_index ++] = m[3][0];
		dbuff[dbuff_index ++] = m[3][1];
		dbuff[dbuff_index ++] = m[3][2];
		dbuff[dbuff_index ++] = m[3][3];

		memcpy (& buf[buf_size], dbuff, 16 * sizeof (double));
		buf_size += 32;
	    }
	    else if (pfIsOfType(node, pfGetDoubleFCSClassType()))
	    {
		buf[buf_size++] = N_DOUBLE_FCS | custom;
		buf[buf_size++] = find_in_list(L_FLUX,
					       pfGetDoubleFCSFlux
							((pfDoubleFCS *)node),
					       glb);
		m_type = pfGetDoubleFCSMatType((pfDoubleFCS *)node);
		if (m_type == PFFCS_UNCONSTRAINED)
		    ((uint*)buf)[buf_size++] = CS_UNCONSTRAINED;
		else
		    ((uint *)buf)[buf_size++] = to_table(mat_table, m_type);
	    }
	    else /* DoubleSCS */
	    {
		buf[buf_size++] = N_DOUBLE_SCS | custom;
		pfGetDoubleSCSMat((pfDoubleSCS *)node, m);

                /*
                 *      Must do double-copy because & buf[buf_size] may not 
                 *      be a double-word address.
                 */
                dbuff_index = 0;
                dbuff[dbuff_index ++] = m[0][0];
                dbuff[dbuff_index ++] = m[0][1];
                dbuff[dbuff_index ++] = m[0][2];
                dbuff[dbuff_index ++] = m[0][3];
                dbuff[dbuff_index ++] = m[1][0];
                dbuff[dbuff_index ++] = m[1][1];
                dbuff[dbuff_index ++] = m[1][2];
                dbuff[dbuff_index ++] = m[1][3];
                dbuff[dbuff_index ++] = m[2][0];
                dbuff[dbuff_index ++] = m[2][1];
                dbuff[dbuff_index ++] = m[2][2];
                dbuff[dbuff_index ++] = m[2][3];
                dbuff[dbuff_index ++] = m[3][0];
                dbuff[dbuff_index ++] = m[3][1];
                dbuff[dbuff_index ++] = m[3][2];
                dbuff[dbuff_index ++] = m[3][3];

                memcpy (& buf[buf_size], dbuff, 16 * sizeof (double));
                buf_size += 32;
	    }
	}
	else if (pfIsOfType(node, pfGetLODClassType()))
	{
	    buf[buf_size++] = N_LOD | custom;
	    buf[buf_size++] = count;
	    for (i = 0; i <= count; i++)
		((float *)buf)[buf_size++] = pfGetLODRange((pfLOD *)node, i);
	    for (i = 0; i <= count; i++)
#ifdef XXX_FIXED_GET_LOD_TRANSITION_BUG
		((float *)buf)[buf_size++] = pfGetLODTransition((pfLOD*)node,i);
#else
	    {
		f1 = pfGetLODTransition((pfLOD*)node,i);
		if (f1 == -1)
		    f1 = pfGetLODTransition((pfLOD*)node, 0);
		((float *)buf)[buf_size++] = f1;
	    }
#endif
	    pfGetLODCenter((pfLOD *)node, v1);
	    ((float *)buf)[buf_size++] = v1[0];
	    ((float *)buf)[buf_size++] = v1[1];
	    ((float *)buf)[buf_size++] = v1[2];
	    buf[buf_size++] = find_in_list(L_LODSTATE,
					   pfGetLODLODState((pfLOD *)node),
					   glb);
	    buf[buf_size++] = pfGetLODLODStateIndex((pfLOD *)node);
	}
	else if (pfIsOfType(node, pfGetSeqClassType()))
	{
	    buf[buf_size++] = N_SEQUENCE | custom;
	    buf[buf_size++] = count;
	    for (i = 0; i < count; i++)
		((float *)buf)[buf_size++] = pfGetSeqTime((pfSequence*)node, i);
	    pfGetSeqInterval((pfSequence *)node, &t1, &t2, &t3);
	    buf[buf_size++] = find_in_table(sqi_table, t1);
	    buf[buf_size++] = t2;
	    buf[buf_size++] = t3;
	    pfGetSeqDuration((pfSequence *)node, &f1, &t1);
	    ((float *)buf)[buf_size++] = f1;
	    buf[buf_size++] = t1;
	    buf[buf_size++] = find_in_table(sqm_table,
					    pfGetSeqMode((pfSequence *)node));
	}
	else if (pfIsOfType(node, pfGetLayerClassType()))
	{
	    buf[buf_size++] = N_LAYER | custom;
	    t1 = pfGetLayerMode((pfLayer *)node);
	    buf[buf_size++] = find_in_table(layer_table,
					    t1 & PFDECAL_MODE_MASK);
	    buf[buf_size++] = (t1 & PFDECAL_LAYER_MASK) >> PFDECAL_LAYER_SHIFT;
	    buf[buf_size++] = (t1 & PFDECAL_LAYER_OFFSET)? 1 : 0;
	}
	else if (pfIsOfType(node, pfGetMorphClassType()))
	{
	    buf[buf_size++] = N_MORPH | custom;
	    buf[buf_size++] = find_in_list(L_MORPH, node, glb);
	}
	else if (pfIsOfType(node, pfGetPartClassType()))
	{
	    buf[buf_size++] = N_PARTITION | custom;
	    if (vp = pfGetPartAttr((pfPartition *)node, PFPART_MIN_SPACING))
	    {
		buf[buf_size++] = 1;
		((float *)buf)[buf_size++] = vp[0];
		((float *)buf)[buf_size++] = vp[1];
		((float *)buf)[buf_size++] = vp[2];
	    }
	    else
		buf[buf_size++] = 0;
	    if (vp = pfGetPartAttr((pfPartition *)node, PFPART_MAX_SPACING))
	    {
		buf[buf_size++] = 1;
		((float *)buf)[buf_size++] = vp[0];
		((float *)buf)[buf_size++] = vp[1];
		((float *)buf)[buf_size++] = vp[2];
	    }
	    else
		buf[buf_size++] = 0;
	    if (vp = pfGetPartAttr((pfPartition *)node, PFPART_ORIGIN))
	    {
		buf[buf_size++] = 1;
		((float *)buf)[buf_size++] = vp[0];
		((float *)buf)[buf_size++] = vp[1];
		((float *)buf)[buf_size++] = vp[2];
	    }
	    else
		buf[buf_size++] = 0;
	    ((float *)buf)[buf_size++] = pfGetPartVal((pfPartition *)node,
						      PFPART_FINENESS);
	}
	else if (pfIsOfType(node, pfGetSceneClassType()))
	{
	    buf[buf_size++] = N_SCENE | custom;
	    buf[buf_size++] = find_in_list(L_GSTATE,
					   pfGetSceneGState((pfScene *)node),
					   glb);
	    buf[buf_size++] = pfGetSceneGStateIndex((pfScene *)node);
	}
	else /* basic pfGroup */
	    buf[buf_size++] = N_GROUP | custom;

	buf[buf_size++] = count;
	for (i = 0; i < count; i++)
	    buf[buf_size++] = find_in_list(L_NODE, pfGetChild((pfGroup *)node, i), glb);
    }
    else if (!glb->subdivsurface_save_geosets && 
	     pfIsOfType(node, pfGetGeodeClassType()))
    {
	int tcount, isIBRnode, isIBRproxy = 0, storeTexCoords = 0, flags;
	int numGroups, viewsPerGroup;

	count = pfGetNumGSets((pfGeode *)node);

	isIBRnode = pfIsOfType(node, pfGetIBRnodeClassType());

	if (isIBRnode)
	{
	    int       size;
	    int       n, t;
	    pfIBRtexture *IBRtex;
	    pfDirData *viewData;

	    IBRtex = pfGetIBRnodeIBRtexture((pfIBRnode *)node);

	    isIBRproxy = pfGetIBRtextureFlags(IBRtex, PFIBR_USE_PROXY);
	    storeTexCoords = isIBRproxy || 
		pfGetIBRnodeFlags((pfIBRnode *)node, PFIBRN_TEXCOORDS_DEFINED);
	    
	    size = 1; /* 1 for IBR texture */

	    /* size of angles */
	    size += 1 + 2 * pfGetIBRnodeNumAngles((pfIBRnode *)node);

	    flags = pfGetIBRnodeFlags((pfIBRnode *)node, 0xffffffff);

	    if(storeTexCoords)
	    {
		pfVec2    ***texCoords;
		pfVec3    *dummy;

		texCoords = pfGetIBRnodeProxyTexCoords((pfIBRnode *)node);

		viewData = pfGetIBRtextureDirData(IBRtex);

		numGroups = pfGetDirDataNumGroups(viewData, &viewsPerGroup);

		/* determine the size of the array of texture coordinates 
		   in case of a pfIBRnode with proxy */
		size += 3;

		if(flags & PFIBRN_VARY_PROXY_GEOSETS)
		{
		    if(pfGetIBRtextureFlags(IBRtex, PFIBR_NEAREST))
		    {
			pfGetDirDataDirections(viewData, &numGroups,
					       &dummy);
			viewsPerGroup = 1;
		    }

		    /* varying geosets */
		    tcount = count / numGroups;
		    vary = 1;
		}
		else
		{
		    /* not varying geosets */
		    tcount = count;

		    pfGetDirDataDirections(viewData, &numGroups, &dummy);
		    viewsPerGroup = 1;
		    vary = 0;
		}

		for(t=0; t<numGroups; t++)
		{
		    /* for each geoset */
		    for(i=0; i<tcount; i++)
		    {
			size++; /* to store n */

			if(texCoords[t][i*viewsPerGroup] == NULL)
			    n = 0;
			else
			    n = getNumGSetVertices(pfGetGSet((pfGeode *)node, 
						       i + t*tcount*vary));

			size += viewsPerGroup * n * 2;
		    }
		} 
	    }
	    
	    buf = GET_BUF(count * 4 + 128 + size);
	}
	else if (pfIsOfType(node, pfGetSubdivSurfaceClassType()))
	{
	    /* subdivision surface */
	    buf = GET_BUF(7+3+16); /* id, size, and 5 values + origin + matrix */
	} else if(glb->disable_rep_save == 0 && 
		  pfIsOfType(node, pfGetParaSurfaceClassType())) {
	  /* parametric surface */
	  spaceReqmnt =
	    calculateSpaceForSurface((pfParaSurface *)node);

	  if(pfIsOfType(node, pfGetNurbSurfaceClassType())) {
	    pfNurbSurface *nurbs = (pfNurbSurface *)node;
	    int uKnotCount, vKnotCount, uOrder, vOrder, uSize, vSize;

	    uKnotCount = pfGetNurbSurfaceUknotCount(nurbs);
	    vKnotCount = pfGetNurbSurfaceVknotCount(nurbs);
	    uOrder = pfGetNurbSurfaceUorder(nurbs);
	    vOrder = pfGetNurbSurfaceVorder(nurbs);
	    
	    uSize = uKnotCount - uOrder;
	    vSize = vKnotCount - vOrder;

	    spaceReqmnt += (vSize+1)*(uSize+1)*4 + uKnotCount + vKnotCount + 4;
	  } else if(pfIsOfType(node, pfGetTorusSurfaceClassType())) {
	    spaceReqmnt += 2; /* minor radius + major radius */
	  } else if(pfIsOfType(node, pfGetSphereSurfaceClassType())) {
	    spaceReqmnt += 1; /* radius */
	  } else if(pfIsOfType(node, pfGetConeSurfaceClassType()) ||
		    pfIsOfType(node, pfGetCylinderSurfaceClassType())) {
	    spaceReqmnt += 2; /* radius + height */
	  } else if(pfIsOfType(node, pfGetPlaneSurfaceClassType())) {
	    spaceReqmnt += 3*3 + 2 + 1 + 1; /*3 pts + u1,v1,u2,v3 */
	  } else if(pfIsOfType(node, pfGetPieceWisePolySurfaceClassType())) {
	    int uPatchCount, vPatchCount, uOrder, vOrder, u, v;
	    pfPieceWisePolySurface *ppSurf = 
	      (pfPieceWisePolySurface *)node;

	    uPatchCount = pfGetPieceWisePolySurfaceUpatchCount(ppSurf);
	    vPatchCount = pfGetPieceWisePolySurfaceVpatchCount(ppSurf);

	    spaceReqmnt += (uPatchCount * vPatchCount) * 2 + 2;

	    for(v=0; v<vPatchCount; v++) {
	      for(u=0; u<uPatchCount; u++) {
		uOrder = pfGetPieceWisePolySurfaceUorder(ppSurf,u,v);
		vOrder = pfGetPieceWisePolySurfaceVorder(ppSurf,u,v);
		spaceReqmnt += (uOrder+1) * (vOrder+1) * 3;
	      }
	    }
	  } else if(pfIsOfType(node, pfGetSweptSurfaceClassType())) {
	    spaceReqmnt += 5; /* 4 curves + scalar */
	    if(pfIsOfType(node, pfGetFrenetSweptSurfaceClassType()))
	      spaceReqmnt -= 2; /* don't store t & b curves */
	  } else if(pfIsOfType(node, pfGetCoonsSurfaceClassType())) {
	    spaceReqmnt += 4; /* four bounding curves */
	  } else if(pfIsOfType(node, pfGetRuledSurfaceClassType())) {
	    spaceReqmnt += 2; /* two bounding curves */
	  } else if(pfIsOfType(node, pfGetHsplineSurfaceClassType())) {
	    int uKnotCount, vKnotCount;
	    pfHsplineSurface *hSurf = (pfHsplineSurface *)node;

	    uKnotCount = pfGetHsplineSurfaceUknotCount(hSurf);
	    vKnotCount = pfGetHsplineSurfaceVknotCount(hSurf);

	    spaceReqmnt += uKnotCount * vKnotCount * 3 * 4 + uKnotCount +
	                   vKnotCount + 1 + 1 + 1;
	    /* last three are for holding uKnotCount, vKnotCount and
	     * cylindircal flag */
	  } else {
	    pfNotify(PFNFY_WARN,PFNFY_NOTICE,
		     "Unable to calculate length of buffer required to store "
		     "parametric surface of type %s. This is bad ...",
		     pfGetTypeName(pfGetType(node)));
	  }

	  spaceReqmnt += 3; /* id + size + geoSet-count values */

	  /* now allocate buffer for surface */

	  buf = GET_BUF(spaceReqmnt);
	} else if(pfIsOfType(node, pfGetRepClassType())) {	
	  /* id + size + geoSet-count values and also rep_t data */
	  spaceReqmnt += 3 + sizeof(rep_t)/sizeof(int);
	  if(pfIsOfType(node, pfGetCurve3dClassType())) {

	    spaceReqmnt += sizeof(curve3d_t)/sizeof(int);

	    if(pfIsOfType(node, pfGetLine3dClassType())) {
	      spaceReqmnt += 8; /* x,y,z,t values twice */
	      if(pfIsOfType(node, pfGetOrientedLine3dClassType()))
		spaceReqmnt += 3; /* and three more vals for up pnt. */
	    } else if(pfIsOfType(node, pfGetNurbCurve3dClassType())) {
	      pfNurbCurve3d *nc = (pfNurbCurve3d *)node;
	      int knotCount, order, size;

	      knotCount = pfGetNurbCurve3dKnotCount(nc);
	      order     = pfGetNurbCurve3dOrder(nc);
	      size      = knotCount - order;

	      spaceReqmnt += (size+1) * 3 + knotCount + 4; /* XXX Alex what's the 4 for? */
	    } else if(pfIsOfType(node, pfGetPieceWisePolyCurve3dClassType())) {
	      pfPieceWisePolyCurve3d *c = (pfPieceWisePolyCurve3d *)node;
	      int patchCount, u;

	      patchCount = pfGetPieceWisePolyCurve3dPatchCount(c);
	      spaceReqmnt += 2 * patchCount + 2; /* limit pars for each patch, reverse flag 
						   for entire curve and patch count */

	      for(u=0; u<patchCount; u++)
		spaceReqmnt += (pfGetPieceWisePolyCurve3dOrder(c,u) +1) * 3;
	    } else if(pfIsOfType(node, pfGetCompCurve3dClassType())) {
	      spaceReqmnt += 2; /* surface + 2d curve ids. */
	    } else if(pfIsOfType(node, pfGetCircle3dClassType())) {
	      spaceReqmnt += 1; /* radius */
	    } else if(pfIsOfType(node, pfGetSuperQuadCurve3dClassType())) {
	      spaceReqmnt += 2; /* radius + exponent */
	    } else {
	      pfNotify(PFNFY_WARN,PFNFY_PRINT,
		     "Unable to calculate length of buffer required to store"
		     "3d parametric curve of type %s. This is bad ...",
		       pfGetTypeName(pfGetType(node)));
	    }
	  }
	  /* some other (non parametric surface or 3d-curve) class type */
	  /* ZZZ Alex */
	}
	else
	    buf = GET_BUF(count * 4 + 128);

	if (pfIsOfType(node, pfGetBboardClassType()))
	{
	    pfFlux *flux;

	    if (pfIsOfType(node, pfGetIBRnodeClassType()))
		buf[buf_size++] = N_IBR_NODE | custom;
	    else
		buf[buf_size++] = N_BILLBOARD | custom;

	    if (flux = pfGetBboardPosFlux((pfBillboard *)node))
	    {
		buf[buf_size++] = BB_POS_FLUX;
		buf[buf_size++] = find_in_list(L_FLUX, flux, glb);
	    }
	    else
	    {
		buf[buf_size++] = count;
		if(isIBRproxy)
		{
		    /* only one position is used */
		    pfGetBboardPos((pfBillboard *)node, 0, v1);
		    for (i = 0; i < count; i++)
		    {
			((float *)buf)[buf_size++] = v1[0];
			((float *)buf)[buf_size++] = v1[1];
			((float *)buf)[buf_size++] = v1[2];
		    }
		}
		else
		{
		    for (i = 0; i < count; i++)
		    {
			pfGetBboardPos((pfBillboard *)node, i, v1);
			((float *)buf)[buf_size++] = v1[0];
			((float *)buf)[buf_size++] = v1[1];
			((float *)buf)[buf_size++] = v1[2];
		    }
		}
	    }
	    t1 = pfGetBboardMode((pfBillboard *)node, PFBB_ROT);
	    buf[buf_size++] = find_in_table(bbr_table, t1);
	    pfGetBboardAxis((pfBillboard *)node, v1);
	    ((float *)buf)[buf_size++] = v1[0];
	    ((float *)buf)[buf_size++] = v1[1];
	    ((float *)buf)[buf_size++] = v1[2];

	    if (isIBRnode)
	    {
		float a1, a2;
		int n;

		buf[buf_size++] = 
		    find_in_list(L_IBR_TEX, 
				 pfGetIBRnodeIBRtexture((pfIBRnode *)node),
				 glb);

		n = pfGetIBRnodeNumAngles((pfIBRnode *)node);
		buf[buf_size++] = n;

		for(i=0; i<n; i++)
		{
		    pfGetIBRnodeAngles((pfIBRnode *)node, i, &a1, &a2);
		    ((float *)buf)[buf_size++] = a1;
		    ((float *)buf)[buf_size++] = a2;
		}

		buf[buf_size++] = flags;

		if(storeTexCoords)
		{
		    int       j, t, v;
		    pfVec2    ***texCoords;

		    /* printf("writing proxy tex coords\n"); */

		    /* write the proxy texture coordinates */
		    texCoords = pfGetIBRnodeProxyTexCoords((pfIBRnode *)node);

		    /* store the number of geosets and the number of textures */
		    buf[buf_size++] = tcount;
		    buf[buf_size++] = numGroups;
		    buf[buf_size++] = viewsPerGroup;

		    /*printf("geosets %d, numtex %d\n", count, numTex);*/

		    for(t=0; t<numGroups; t++)
		    {
			/* for each geoset */
			for(i=0; i<tcount; i++)
			{
			    if(texCoords[t][i*viewsPerGroup] == NULL)
			    {
				/* store the number of vertices */
				buf[buf_size++] = 0;
				continue;
			    }

			    /* determine the number of vertices */
			    n = getNumGSetVertices(pfGetGSet((pfGeode *)node, 
						       i + t*tcount*vary));
			
			    /* store the number of vertices */
			    buf[buf_size++] = n;

			    for(j=0; j<viewsPerGroup; j++)
			    {
				/*printf("geoset %d, texture %d\n", i, t);*/
				for(v=0; v<n; v++)
				{
				    ((float *)buf)[buf_size++] = 
					texCoords[t][i*viewsPerGroup+j][v][0];
				    ((float *)buf)[buf_size++] = 
					texCoords[t][i*viewsPerGroup+j][v][1];
				}
			    }
			}
		    }
		}
	    }
	}
	else if (pfIsOfType(node, pfGetSubdivSurfaceClassType()))
	{
	    if(glb->subdivsurface_save_geosets)
	    {
		/* save as simple geode if this flag is set */
		buf[buf_size++] = N_GEODE | custom;
		buf[buf_size++] = count;
	    } 
	    else 
	    {
		/* subdivision surface */
		pfSubdivSurface *surf = (pfSubdivSurface *)node;
		float val;
		
		buf[buf_size++] = N_SUBDIV_SURFACE | custom;
		
		buf[buf_size++] = 5 + 3 + 16;  /* 24 values */
		
		/* store pfRep stuff */
		{
		    pfVec3 vec;
		    pfMatrix m;
		    int     x,y;
		    
		    pfGetRepOrigin((pfRep *)node, vec);
		    
		    for(i=0; i<3; i++)
			((float *)buf)[buf_size++] = vec[i];
		    
		    
		    pfGetRepOrient((pfRep *)node, m);
		    
		    for(y=0; y<4; y++)
			for(x=0; x<4; x++)
			    ((float *)buf)[buf_size++] = m[y][x];
		}
		
		buf[buf_size++] = pfGetSubdivSurfaceFlags(surf, 0xffffffff);
		
		pfGetSubdivSurfaceVal(surf, PFSB_SUBDIVISION_METHOD, &val);
		buf[buf_size++] = (int)val;
		
		pfGetSubdivSurfaceVal(surf, PFSB_SUBDIVISION_LEVEL, &val);
		buf[buf_size++] = (int)val;
		
		buf[buf_size++] =
		    find_in_list(L_MESH, pfGetSubdivSurfaceMesh(surf),  glb);
		
		count = 0; /* no geosets XXX decide based on a flag? - later */
		buf[buf_size++] = count;
	    }
	}
	else if(pfIsOfType(node, pfGetRepClassType())) {
	  if(glb->disable_rep_save) {
	    /* save as simple geode if this flag is set */
	    buf[buf_size++] = N_GEODE | custom;
	    buf[buf_size++] = count;
	  } else {
	    if (pfIsOfType(node, pfGetNurbSurfaceClassType())) 
	    {
	      /* nurbs surface */
	      pfNurbSurface *nurbs = (pfNurbSurface *)node;
	      int uKnotCount, vKnotCount;
	      int uSize, vSize, ii, jj;
	      float weight;

	      buf[buf_size++] = N_NURB_SURFACE | custom;
	      buf[buf_size++] = spaceReqmnt;

	      pfb_write_parasurface((pfParaSurface *)node,glb,&buf_size);

	      uKnotCount = pfGetNurbSurfaceUknotCount(nurbs);
	      vKnotCount = pfGetNurbSurfaceVknotCount(nurbs);
	    
	      uSize = pfGetNurbSurfaceControlHullUSize(nurbs);
	      vSize = pfGetNurbSurfaceControlHullVSize(nurbs);

	      buf[buf_size++] = uKnotCount;
	      buf[buf_size++] = vKnotCount;
	      buf[buf_size++] = uSize;
	      buf[buf_size++] = vSize;

	      /* write out ctrl points */
	      for(ii=0; ii<vSize; ii++) {
		for(jj=0; jj<uSize; jj++) {
		  const pfVec3 *cHull =pfGetNurbSurfaceControlHull(nurbs,jj,ii);
		  weight = pfGetNurbSurfaceWeight(nurbs,jj,ii);

		  ((float *)buf)[buf_size++] = (*cHull)[0];
		  ((float *)buf)[buf_size++] = (*cHull)[1];
		  ((float *)buf)[buf_size++] = (*cHull)[2];
		  ((float *)buf)[buf_size++] = weight;
		}
	      }
	    
	      /* write out u and v knots */
	      for(ii=0; ii<uKnotCount; ii++)
		((float *)buf)[buf_size++] = pfGetNurbSurfaceUknot(nurbs,ii);

	      for(ii=0; ii<vKnotCount; ii++)
		((float *)buf)[buf_size++] = pfGetNurbSurfaceVknot(nurbs,ii);

	      buf[buf_size++] = count;
	    }
	    else if (pfIsOfType(node, pfGetTorusSurfaceClassType()))
	    {
	      /* torus surface */
	      pfTorusSurface *torus = (pfTorusSurface *)node;

	      buf[buf_size++] = N_TORUS | custom;
	      buf[buf_size++] = spaceReqmnt;

	      pfb_write_parasurface((pfParaSurface *)node,glb, &buf_size);

	      ((float *)buf)[buf_size++] = pfGetTorusSurfaceMinorRadius(torus);
	      ((float *)buf)[buf_size++] = pfGetTorusSurfaceMajorRadius(torus);

	      buf[buf_size++] = count;
	    }
	    else if (pfIsOfType(node, pfGetSphereSurfaceClassType()))
	    {
	      /* sphere surface */
	      pfSphereSurface *sphere = (pfSphereSurface *)node;

	      buf[buf_size++] = N_SPHERE_SURF | custom;
	      buf[buf_size++] = spaceReqmnt;

	      pfb_write_parasurface((pfParaSurface *)node,glb, &buf_size);

	      ((float *)buf)[buf_size++] = pfGetSphereSurfaceRadius(sphere);

	      buf[buf_size++] = count;
	    }
	    else if (pfIsOfType(node, pfGetConeSurfaceClassType()))
	    {
	      /* cone surface */
	      pfConeSurface *cone = (pfConeSurface *)node;

	      buf[buf_size++] = N_CONE | custom;
	      buf[buf_size++] = spaceReqmnt;

	      pfb_write_parasurface((pfParaSurface *)node,glb, &buf_size);

	      ((float *)buf)[buf_size++] = pfGetConeSurfaceRadius(cone);
	      ((float *)buf)[buf_size++] = pfGetConeSurfaceHeight(cone);

	      buf[buf_size++] = count;
	    }
	    else if (pfIsOfType(node, pfGetCylinderSurfaceClassType()))
	    {
	      /* cylinder surface */
	      pfCylinderSurface *cylinder = (pfCylinderSurface *)node;

	      buf[buf_size++] = N_CYLINDER | custom;
	      buf[buf_size++] = spaceReqmnt;

	      pfb_write_parasurface((pfParaSurface *)node,glb, &buf_size);

	      ((float *)buf)[buf_size++] = pfGetCylinderSurfaceRadius(cylinder);
	      ((float *)buf)[buf_size++] = pfGetCylinderSurfaceHeight(cylinder);

	      buf[buf_size++] = count;
	    }
	    else if (pfIsOfType(node, pfGetPlaneSurfaceClassType()))
	    {
	      /* planar surface */
	      pfPlaneSurface *plane = (pfPlaneSurface *)node;
	      float vals[5];

	      buf[buf_size++] = N_PLANE_SURF | custom;
	      buf[buf_size++] = spaceReqmnt;

	      pfb_write_parasurface((pfParaSurface *)node,glb, &buf_size);
	  
	      pfGetPlaneSurfacePoint1(plane, &(vals[0]), &(vals[1]), &(vals[2]),
				   &(vals[3]), &(vals[4]));
	      ((float *)buf)[buf_size++] = vals[0];
	      ((float *)buf)[buf_size++] = vals[1];
	      ((float *)buf)[buf_size++] = vals[2];
	      ((float *)buf)[buf_size++] = vals[3];
	      ((float *)buf)[buf_size++] = vals[4];

	      pfGetPlaneSurfacePoint2(plane, &(vals[0]), &(vals[1]), &(vals[2]),
				   &(vals[3]));
	      ((float *)buf)[buf_size++] = vals[0];
	      ((float *)buf)[buf_size++] = vals[1];
	      ((float *)buf)[buf_size++] = vals[2];
	      ((float *)buf)[buf_size++] = vals[3];
	  
	      pfGetPlaneSurfacePoint3(plane, &(vals[0]), &(vals[1]), &(vals[2]),
				   &(vals[3]));
	      ((float *)buf)[buf_size++] = vals[0];
	      ((float *)buf)[buf_size++] = vals[1];
	      ((float *)buf)[buf_size++] = vals[2];
	      ((float *)buf)[buf_size++] = vals[3];

	      buf[buf_size++] = count;
	    }
	    else if (pfIsOfType(node, pfGetPieceWisePolySurfaceClassType()))
	    {
	      pfPieceWisePolySurface *ppSurf = (pfPieceWisePolySurface *)node;

	      buf[buf_size++] = N_PIECEWISEPOLYSURFACE | custom;
	      buf[buf_size++] = spaceReqmnt;
	  
	      pfb_write_parasurface((pfParaSurface *)node,glb, &buf_size);

	      {
		int uPatchCount, vPatchCount, uOrder, vOrder, u, v, uu, vv;
		pfVec3 *cHull = NULL;

		uPatchCount = pfGetPieceWisePolySurfaceUpatchCount(ppSurf);
		vPatchCount = pfGetPieceWisePolySurfaceVpatchCount(ppSurf);

		buf[buf_size++] = uPatchCount;
		buf[buf_size++] = vPatchCount;

		for(v=0; v<vPatchCount; v++) {
		  for(u=0; u<uPatchCount; u++) {
		    uOrder = pfGetPieceWisePolySurfaceUorder(ppSurf, u, v);
		    vOrder = pfGetPieceWisePolySurfaceVorder(ppSurf, u, v);

		    buf[buf_size++] = uOrder;
		    buf[buf_size++] = vOrder;

		    for(vv=0; vv<vOrder; vv++) {
		      for(uu=0; uu<uOrder; uu++) {
			cHull = 
		      pfGetPieceWisePolySurfaceControlHull(ppSurf,u,v,uu,vv);

			((float *)buf)[buf_size++] = (*cHull)[0];
			((float *)buf)[buf_size++] = (*cHull)[1];
			((float *)buf)[buf_size++] = (*cHull)[2];
		      }
		    }
		  }
		}
	      }

	      buf[buf_size++] = count;
	    }
	    else if (pfIsOfType(node, pfGetSweptSurfaceClassType())) {
	      int isFrenet = 0;
	      pfSweptSurface *ss = (pfSweptSurface *)node;
	      pfCurve3d *_crossSection = NULL, *_path = NULL, *_t = NULL, *_b = NULL;
	      pfScalar *_scalar = NULL;

	      isFrenet = pfIsOfType(node, pfGetFrenetSweptSurfaceClassType());

	      if(isFrenet)
		buf[buf_size++] = N_FRENET_SWEPT_SURFACE | custom;
	      else
		buf[buf_size++] = N_SWEPT_SURFACE | custom;
	      buf[buf_size++] = spaceReqmnt;
	  
	      pfb_write_parasurface((pfParaSurface *)node,glb, &buf_size);

	      _crossSection = (pfCurve3d *)pfGetMemory(pfGetSweptSurfaceCrossSection(ss));
	      _path         = (pfCurve3d *)pfGetMemory(pfGetSweptSurfacePath(ss));
	      if(!isFrenet) {
		_t          = (pfCurve3d *)pfGetMemory(pfGetSweptSurfaceT(ss));
		_b          = (pfCurve3d *)pfGetMemory(pfGetSweptSurfaceB(ss));
	      }
	      _scalar       = (pfScalar  *)pfGetSweptSurfaceProf(ss);

	      buf[buf_size++] = find_in_list(L_NODE, _crossSection, glb);
	      buf[buf_size++] = find_in_list(L_NODE, _path, glb);

	      if(!isFrenet) {
		buf[buf_size++] = find_in_list(L_NODE, _t, glb);
		buf[buf_size++] = find_in_list(L_NODE, _b, glb);
	      }
	      buf[buf_size++] = find_in_list(L_SCALAR, _scalar, glb);

	      buf[buf_size++] = count;
	    }
	    else if(pfIsOfType(node, pfGetCoonsSurfaceClassType())) {
	      pfCoonsSurface *coons = (pfCoonsSurface *)node;
	      pfCurve3d *_r = NULL, *_l = NULL, *_b = NULL, *_t = NULL;

	      buf[buf_size++] = N_COONS | custom;
	      buf[buf_size++] = spaceReqmnt;

	      pfb_write_parasurface((pfParaSurface *)node, glb, &buf_size);

	      _r = pfGetCoonsSurfaceRight (coons);
	      _l = pfGetCoonsSurfaceLeft  (coons);
	      _b = pfGetCoonsSurfaceBottom(coons);
	      _t = pfGetCoonsSurfaceTop   (coons);

	      buf[buf_size++] = find_in_list(L_NODE, _r, glb);
	      buf[buf_size++] = find_in_list(L_NODE, _l, glb);
	      buf[buf_size++] = find_in_list(L_NODE, _b, glb);
	      buf[buf_size++] = find_in_list(L_NODE, _t, glb);

	      buf[buf_size++] = count;
	    }
	    else if(pfIsOfType(node, pfGetHsplineSurfaceClassType())) {
	      pfHsplineSurface *hSurf = (pfHsplineSurface *)node;
	      int uKnotCount, vKnotCount, _i, _j;
	      pfVec3 v;
	      
	      buf[buf_size++] = N_HSPLINE_SURFACE | custom;
	      buf[buf_size++] = spaceReqmnt;

	      pfb_write_parasurface((pfParaSurface *)node, glb, &buf_size);

	      uKnotCount = pfGetHsplineSurfaceUknotCount(hSurf);
	      vKnotCount = pfGetHsplineSurfaceVknotCount(hSurf);

	      buf[buf_size++] = uKnotCount;
	      buf[buf_size++] = vKnotCount;
	      buf[buf_size++] = pfGetHsplineSurfaceCylindrical(hSurf);

	      for(_i=0; _i<uKnotCount; _i++) {
		for(_j=0; _j<vKnotCount; _j++) {
		  pfGetHsplineSurfaceP(hSurf, _i, _j, v);
		  ((float *)buf)[buf_size++] = v[0];
		  ((float *)buf)[buf_size++] = v[1];
		  ((float *)buf)[buf_size++] = v[2];
		}
	      }

	      for(_i=0; _i<uKnotCount; _i++) {
		for(_j=0; _j<vKnotCount; _j++) {
		  pfGetHsplineSurfaceTu(hSurf, _i, _j, v);
		  ((float *)buf)[buf_size++] = v[0];
		  ((float *)buf)[buf_size++] = v[1];
		  ((float *)buf)[buf_size++] = v[2];
		}
	      }

	      for(_i=0; _i<uKnotCount; _i++) {
		for(_j=0; _j<vKnotCount; _j++) {
		  pfGetHsplineSurfaceTv(hSurf, _i, _j, v);
		  ((float *)buf)[buf_size++] = v[0];
		  ((float *)buf)[buf_size++] = v[1];
		  ((float *)buf)[buf_size++] = v[2];
		}
	      }

	      for(_i=0; _i<uKnotCount; _i++) {
		for(_j=0; _j<vKnotCount; _j++) {
		  pfGetHsplineSurfaceTuv(hSurf, _i, _j, v);
		  ((float *)buf)[buf_size++] = v[0];
		  ((float *)buf)[buf_size++] = v[1];
		  ((float *)buf)[buf_size++] = v[2];
		}
	      }

	      for(_i=0; _i<uKnotCount; _i++)
		((float *)buf)[buf_size++] = 
	          pfGetHsplineSurfaceUknot(hSurf, _i);

	      for(_i=0; _i<vKnotCount; _i++)
		((float *)buf)[buf_size++] = 
	          pfGetHsplineSurfaceVknot(hSurf, _i);

	      buf[buf_size++] = count;	      
	    }
	    else if(pfIsOfType(node, pfGetRuledSurfaceClassType())) {
	      pfRuledSurface *ruled = (pfRuledSurface *)node;
	      pfCurve3d *_c1 = NULL, *_c2 = NULL;

	      buf[buf_size++] = N_RULED | custom;
	      buf[buf_size++] = spaceReqmnt;

	      pfb_write_parasurface((pfParaSurface *)node, glb, &buf_size);

	      _c1 = (pfCurve3d *)pfGetMemory(pfGetRuledSurfaceCurve1(ruled));
	      _c2 = (pfCurve3d *)pfGetMemory(pfGetRuledSurfaceCurve2(ruled));

	      buf[buf_size++] = find_in_list(L_NODE, _c1, glb);
	      buf[buf_size++] = find_in_list(L_NODE, _c2, glb);

	      count = pfGetNumGSets((pfGeode *)node);
	      buf[buf_size++] = count;
	    }
	    else if (pfIsOfType(node, pfGetNurbCurve3dClassType()))
	    {
	      /* pfNurbCurve3d */
	      pfNurbCurve3d *nc = (pfNurbCurve3d *)node;
	  
	      buf[buf_size++] = N_NURB_CURVE3D | custom;
	      buf[buf_size++] = spaceReqmnt;
	  
	      pfb_write_nurb_curve3d(nc,glb,&buf_size);

	      count = pfGetNumGSets((pfGeode *)node);
	      buf[buf_size++] = count;
	    }
	    else if (pfIsOfType(node, pfGetLine3dClassType())) 
	    {
	      pfLine3d *line = (pfLine3d *)node;

	      if(pfIsOfType(node, pfGetOrientedLine3dClassType()))
		buf[buf_size++] = N_ORIENTEDLINE3D | custom;
	      else
		buf[buf_size++] = N_LINE3D | custom;
	      buf[buf_size++] = spaceReqmnt;

	      pfb_write_curve3d((pfCurve3d *)line, glb, &buf_size);

	      {
		pfReal x,y,z,t;

		pfGetLine3dPoint1(line, &x, &y, &z, &t);
		((float *)buf)[buf_size++] = x;
		((float *)buf)[buf_size++] = y;
		((float *)buf)[buf_size++] = z;
		((float *)buf)[buf_size++] = t;
		
		pfGetLine3dPoint2(line, &x, &y, &z, &t);
		((float *)buf)[buf_size++] = x;
		((float *)buf)[buf_size++] = y;
		((float *)buf)[buf_size++] = z;
		((float *)buf)[buf_size++] = t;

		if(pfIsOfType(node, pfGetOrientedLine3dClassType())) {
		  pfGetOrientedLine3dUpPoint((pfOrientedLine3d *)line,
					     &x,&y,&z);
		  ((float *)buf)[buf_size++] = x;
		  ((float *)buf)[buf_size++] = y;
		  ((float *)buf)[buf_size++] = z;
		}
	      }

	      count = pfGetNumGSets((pfGeode *)node);
	      buf[buf_size++] = count;
	    }
	    else if (pfIsOfType(node, pfGetCircle3dClassType())) 
	    {
	      pfCircle3d *circle = (pfCircle3d *)node;

	      buf[buf_size++] = N_CIRCLE3D | custom;
	      buf[buf_size++] = spaceReqmnt;

	      pfb_write_curve3d((pfCurve3d *)circle, glb, &buf_size);

	      ((float *)buf)[buf_size++] = pfGetCircle3dRadius(circle);

	      count = pfGetNumGSets((pfGeode *)node);
	      buf[buf_size++] = count;
	    }
	    else if (pfIsOfType(node, pfGetSuperQuadCurve3dClassType())) 
	    {
	      pfSuperQuadCurve3d *curve = (pfSuperQuadCurve3d *)node;

	      buf[buf_size++] = N_SUPERQUADCURVE3D | custom;
	      buf[buf_size++] = spaceReqmnt;

	      pfb_write_curve3d((pfCurve3d *)curve, glb, &buf_size);

	      ((float *)buf)[buf_size++] = pfGetSuperQuadCurve3dRadius(curve);
	      ((float *)buf)[buf_size++] = pfGetSuperQuadCurve3dExponent(curve);

	      count = pfGetNumGSets((pfGeode *)node);
	      buf[buf_size++] = count;
	    }

	    else if (pfIsOfType(node, pfGetCompCurve3dClassType())) 
	    {
	      pfCompositeCurve3d *cc = (pfCompositeCurve3d *)node;

	      buf[buf_size++] = N_COMPOSITE_CURVE3D | custom;
	      buf[buf_size++] = spaceReqmnt;

	      pfb_write_curve3d((pfCurve3d *)cc, glb, &buf_size);

	      {
		int sId, cId;
		pfParaSurface *s = pfGetCompCurve3dParaSurface(cc);
		pfCurve2d *c = pfGetCompCurve3dCurve2d(cc);
		
		sId = find_in_list(L_NODE, s, glb);
		cId = find_in_list(L_CURVE2D, c, glb); /* XXX Alex -- or should we look in L_NODE? */
		
		buf[buf_size++] = sId;
		buf[buf_size++] = cId;
	      }

	      count = pfGetNumGSets((pfGeode *)node);
	      buf[buf_size++] = count;
	    }
	    else if (pfIsOfType(node, pfGetNurbCurve2dClassType()))
	    {
	      /* pfNurbCurve2d */
	      pfNurbCurve2d *nc = (pfNurbCurve2d *)node;
	  
	      buf[buf_size++] = N_NURB_CURVE2D | custom;
	      buf[buf_size++] = spaceReqmnt;
	  
	      pfb_write_nurb_curve2d(nc,glb,&buf_size);

	      count = pfGetNumGSets((pfGeode *)node);
	      buf[buf_size++] = count;
	    }
	  }
	}
	else /* basic pfGeode */
	{
	    buf[buf_size++] = N_GEODE | custom;
	    buf[buf_size++] = count;
	}

	for (i = 0; i < count; i++)
	    buf[buf_size++] = find_in_list(L_GSET, pfGetGSet((pfGeode *)node, i), glb);
    }
    else if (pfIsOfType(node, pfGetLPointClassType()))
    {
	/*
	 *  Turn Light Point Node into a Geode
	 */
	buf[buf_size++] = N_GEODE | custom;
	buf[buf_size++] = 1;
	buf[buf_size++] = find_in_list(L_GSET,
				       pfGetLPointGSet((pfLightPoint *)node),
				       glb);
    }
    else if (pfIsOfType(node, pfGetTextClassType()))
    {
	count = pfGetNumStrings((pfText *)node);
	buf = GET_BUF(count + 128);

	buf[buf_size++] = N_TEXT | custom;
	buf[buf_size++] = count;
	for (i = 0; i < count; i++)
	    buf[buf_size++] = find_in_list(L_STRING,
					   pfGetString((pfText *)node, i), glb);
    }
    else if (pfIsOfType(node, pfGetLSourceClassType()))
    {
	buf[buf_size++] = N_LIGHTSOURCE | custom;
	buf[buf_size++] = find_in_list(L_LSOURCE, node, glb);
    }
    else if (pfIsOfType(node, pfGetASDClassType()))
    {
	int bind, size, o_size, pv_size, o_type, pv_type;
	void *attr, *o_attr, *pv_attr;

	buf[buf_size++] = N_ASD | custom;
	pfGetASDAttr((pfASD *)node, PFASD_LODS, &bind, &size, &attr);
	buf[buf_size++] = bind;
	buf[buf_size++] = size;
	buf[buf_size++] = find_in_list(L_ASDDATA, attr, glb);
	pfGetASDAttr((pfASD *)node, PFASD_COORDS, &bind, &size, &attr);
	buf[buf_size++] = bind;
	buf[buf_size++] = size;
	buf[buf_size++] = find_in_list(L_ASDDATA, attr, glb);
	pfGetASDAttr((pfASD *)node, PFASD_FACES, &bind, &size, &attr);
	buf[buf_size++] = bind;
	buf[buf_size++] = size;
	buf[buf_size++] = find_in_list(L_ASDDATA, attr, glb);

	o_attr = pv_attr = NULL;
	o_size = pv_size = 0;
	o_type = pv_type = 0;
	pfGetASDAttr((pfASD *)node, PFASD_PER_VERTEX_ATTR, &bind, &size, &attr);
	if (attr && size && bind )
	{
		pv_attr = attr;
		pv_size = size;
		pv_type = (bind&PFASD_NORMALS ? ASD_ATTR_NORMALS:0) |
			    (bind&PFASD_COLORS ? ASD_ATTR_COLORS:0) |
			    (bind&PFASD_TCOORDS ? ASD_ATTR_TCOORDS:0);
	}
	pfGetASDAttr((pfASD *)node, PFASD_OVERALL_ATTR, &bind, &size, &attr);
	if (attr && size && bind )
	{
		o_attr = attr;
		o_size = size;
		o_type = (bind&PFASD_NORMALS ? ASD_ATTR_NORMALS:0) |
			   (bind&PFASD_COLORS ? ASD_ATTR_COLORS:0) |
			   (bind&PFASD_TCOORDS ? ASD_ATTR_TCOORDS:0);
	}
	buf[buf_size++] = o_type;
	buf[buf_size++] = o_size;
	buf[buf_size++] = find_in_list(L_ASDDATA, o_attr, glb);
	buf[buf_size++] = pv_type;
	buf[buf_size++] = pv_size;
	buf[buf_size++] = find_in_list(L_ASDDATA, pv_attr, glb);

	buf[buf_size++] = pfGetASDNumBaseFaces((pfASD *)node);
	t1 = pfGetASDMorphAttrs((pfASD *)node);
	if (t1 == -1)
	    t2 = -1;
	else
	{
	    t2 = 0;
	    if (t1 & PFASD_NORMALS)
		t2 |= ASD_ATTR_NORMALS;
	    if (t1 & PFASD_COLORS)
		t2 |= ASD_ATTR_COLORS;
	    if (t1 & PFASD_TCOORDS)
		t2 |= ASD_ATTR_TCOORDS;
	}
	buf[buf_size++] = t2;
	count = pfGetASDNumGStates((pfASD *)node);
	buf[buf_size++] = count;
	for (i = 0; i < count; i++)
	    buf[buf_size++] = find_in_list(L_GSTATE,
					   pfGetASDGState((pfASD *)node, i),
					   glb);
        {
            int m;
            float w;
            pfGetASDMaxMorphDepth((pfASD *)node, &m, &w);
	    buf[buf_size++] = m;
        }
#if 0
	buf[buf_size++] = pfGetASDMaxMorphDepth((pfASD *)node);
#endif
	buf[buf_size++] = find_in_table(asd_em_table,
					pfGetASDEvalMethod((pfASD *)node));
	buf[buf_size++] = find_in_list(L_UFUNC,
				       pfGetASDEvalFunc((pfASD *)node), glb);
#if 0
	t1 = pfGetASDSyncGroup((pfASD *)node);
	buf[buf_size++] = find_in_list(L_SG_NAME,
				       (void*)pfGetFluxSyncGroupName((uint)t1),
				       glb);
#endif
    }
    else
    {
	/* XXX unsuported node type */
    }

    ((uint *)buf)[buf_size++] = pfGetNodeTravMask(node, PFTRAV_ISECT);
    ((uint *)buf)[buf_size++] = pfGetNodeTravMask(node, PFTRAV_APP);
    ((uint *)buf)[buf_size++] = pfGetNodeTravMask(node, PFTRAV_CULL);
    ((uint *)buf)[buf_size++] = pfGetNodeTravMask(node, PFTRAV_DRAW);

    pfGetNodeTravFuncs(node, PFTRAV_APP, &nt.app_pre, &nt.app_post);
    pfGetNodeTravFuncs(node, PFTRAV_CULL, &nt.cull_pre, &nt.cull_post);
    pfGetNodeTravFuncs(node, PFTRAV_DRAW, &nt.draw_pre, &nt.draw_post);
    pfGetNodeTravFuncs(node, PFTRAV_ISECT, &nt.isect_pre, &nt.isect_post);
    nt.app_data = pfGetNodeTravData(node, PFTRAV_APP);
    nt.cull_data = pfGetNodeTravData(node, PFTRAV_CULL);
    nt.draw_data = pfGetNodeTravData(node, PFTRAV_DRAW);
    nt.isect_data = pfGetNodeTravData(node, PFTRAV_ISECT);

    if (nt.app_pre != NULL || nt.app_post != NULL || nt.app_data != NULL ||
	nt.cull_pre != NULL || nt.cull_post != NULL || nt.cull_data != NULL ||
	nt.draw_pre != NULL || nt.draw_post != NULL || nt.draw_data != NULL ||
	nt.isect_pre != NULL || nt.isect_post != NULL || nt.isect_data != NULL)
    {
	/*
	 *  has trav funcs and/or trav data
	 */
	buf[buf_size++] = 1;
	buf[buf_size++] = find_in_list(L_UFUNC, nt.app_pre, glb);
	buf[buf_size++] = find_in_list(L_UFUNC, nt.app_post, glb);
	buf[buf_size++] = find_in_list(L_UDATA, nt.app_data, glb);
	buf[buf_size++] = find_in_list(L_UFUNC, nt.cull_pre, glb);
	buf[buf_size++] = find_in_list(L_UFUNC, nt.cull_post, glb);
	buf[buf_size++] = find_in_list(L_UDATA, nt.cull_data, glb);
	buf[buf_size++] = find_in_list(L_UFUNC, nt.draw_pre, glb);
	buf[buf_size++] = find_in_list(L_UFUNC, nt.draw_post, glb);
	buf[buf_size++] = find_in_list(L_UDATA, nt.draw_data, glb);
	buf[buf_size++] = find_in_list(L_UFUNC, nt.isect_pre, glb);
	buf[buf_size++] = find_in_list(L_UFUNC, nt.isect_post, glb);
	buf[buf_size++] = find_in_list(L_UDATA, nt.isect_data, glb);
    }
    else
	buf[buf_size++] = 0;	/* does not have trav funcs or trav data */

    buf[buf_size++] = get_udata((pfObject*)node, glb);

    t1 = pfGetNodeBSphere(node, &sphere);
    buf[buf_size++] = find_in_table(bound_table, t1);
    if (t1 != PFBOUND_DYNAMIC)
    {
	((float *)buf)[buf_size++] = sphere.center[0];
	((float *)buf)[buf_size++] = sphere.center[1];
	((float *)buf)[buf_size++] = sphere.center[2];
	((float *)buf)[buf_size++] = sphere.radius;
    }


    fwrite(&buf_size, sizeof(int), 1, glb->ofp);
    fwrite(buf, sizeof(int), buf_size, glb->ofp);

    pfb_write_name(pfGetNodeName(node), glb);

    if (custom & N_CUSTOM)
    {
	int size_location, end;

	size_location = (int)ftell(glb->ofp);
	fseek(glb->ofp, sizeof(int), SEEK_CUR);

	glb->data_total = 0;
	i = (custom & N_CUSTOM_MASK) >> N_CUSTOM_SHIFT;
	if (glb->custom_nodes[i].store_func)
	    glb->custom_nodes[i].store_func(node, glb);
	else
	    pfNotify(PFNFY_WARN, PFNFY_PRINT,
		     "%s Found \"%s\" custom node but custom store is NULL\n.",
		     "pfdStoreFile_pfb: ", glb->custom_nodes[i].name);

	end = (int)ftell(glb->ofp);
	fseek(glb->ofp, size_location, SEEK_SET);
	fwrite(&glb->data_total, sizeof(int), 1, glb->ofp);
	fseek(glb->ofp, end, SEEK_SET);
    }
    

}


static void pfa_write_custom_list(pfa_global_t *glb)
{
    int i;

    if (glb->num_custom_nodes == 0)
	return;

    if (glb->add_comments)
	fprintf(glb->ofp, "#---------------------- Customs\n");
    fprintf(glb->ofp, "%d %d\n", L_CUSTOM, glb->num_custom_nodes);
    for (i = 0; i < glb->num_custom_nodes; i++)
    {
	if (glb->add_comments)
	    fprintf(glb->ofp, "#---------------------- Custom %d\n", i);
	fprintf(glb->ofp, "%d\n", glb->custom_nodes[i].used);
	fprintf(glb->ofp, "%s\n", glb->custom_nodes[i].name);
    }
}


static void pfb_write_custom_list(pfa_global_t *glb)
{
    int i;
    int buf[2];
    int start, end;

    if (glb->num_custom_nodes == 0)
	return;

    buf[0] = L_CUSTOM;
    buf[1] = glb->num_custom_nodes;
    fwrite(buf, sizeof(int), 2, glb->ofp);
    fseek(glb->ofp, sizeof(int), SEEK_CUR);
    start = (int)ftell(glb->ofp);
    for (i = 0; i < glb->num_custom_nodes; i++)
    {
	buf[0] = glb->custom_nodes[i].used;
	buf[1] = (int)strlen(glb->custom_nodes[i].name);
	fwrite(buf, sizeof(int), 2, glb->ofp);
	fwrite(glb->custom_nodes[i].name, sizeof(char), buf[1], glb->ofp);
    }
    end = (int)ftell(glb->ofp);
    fseek(glb->ofp, start - (int)sizeof(int), SEEK_SET);
    buf[0] = end - start;
    fwrite(buf, sizeof(int), 1, glb->ofp);
    fseek(glb->ofp, end, SEEK_SET);
}


static void pfa_write_name(const char *s, pfa_global_t *glb)
{
    if (s)
    {
	/* bjk: replace any '\n' characters from the given name by ' ' */
	char *nl = s;
	
	while (nl = strchr(nl, '\n'))
	     *nl = ' ';

	fprintf(glb->ofp, "\"%s\"\n", s);
    }
    else
	fprintf(glb->ofp, "NULL\n");
}


static void pfb_write_name(const char *s, pfa_global_t *glb)
{
    int size;

    if (s)
    {
	size = (int)strlen(s);
	fwrite(&size, sizeof(int), 1, glb->ofp);
	fwrite(s, sizeof(char), size, glb->ofp);
    }
    else
    {
	size = -1;
	fwrite(&size, sizeof(int), 1, glb->ofp);
    }
}


static void free_load_data(pfa_global_t *glb)
{
    int i, n;

    /*
     *  Free any unreferenced images.  This could happen because textures
     *  have been able to be shared.
     */
    for (i = 0; i < glb->l_num[L_IMAGE]; i++)
	if (pfGetRef(glb->rl_list[L_IMAGE][i]) == 0)
	    pfFree(glb->rl_list[L_IMAGE][i]);

    /*
     *  Free udata lists
     */
    if (glb->rl_list[L_UDATA_LIST])
	for (i = 0; i < glb->l_num[L_UDATA_LIST]; i++)
	    free(glb->rl_list[L_UDATA_LIST][i]);

    /*
     *  free lists
     */
    for (i = 0; i < L_COUNT; i++)
	if (glb->rl_list[i])
	    free(glb->rl_list[i]);

    if (glb->crypt_key)
	free(glb->crypt_key);

    if (glb->line_buf)
	free(glb->line_buf);

    if (glb->buf)
	free(glb->buf);

    if (glb->udfunc)
	free(glb->udfunc);

    if (glb->topo_map) {
      n = pfGetNum(glb->topo_map);
      for(i=0; i<n; i++) 
	pfFree((object_id_map_t *)pfGet(glb->topo_map, i));
      pfFree(glb->topo_map);
    }

    if (glb->solid_map) {
      n = pfGetNum(glb->solid_map);
      for(i=0; i<n; i++) 
	pfFree((object_id_map_t *)pfGet(glb->solid_map, i));
      pfFree(glb->solid_map);
    }

    if (glb->boundaryList)
      pfFree(glb->boundaryList);

    free(glb);
}


static void free_store_data(pfa_global_t *glb)
{
    int i;
    list_t *tmp1, *tmp2;

    /*
     *  free lists
     */
    for (i = 0; i < L_COUNT; i++)
    {
	tmp1 = glb->l_list[i];
	glb->l_list[i] = NULL;

	while (tmp1)
	{
	    tmp2 = tmp1;
	    tmp1 = tmp1->next;
	    free(tmp2);
	}
	glb->l_num[i] = 0;

	if (glb->buckets[i])
	{
	    free(glb->buckets[i]);
	    glb->buckets[i] = NULL;
	}
    }
    if (glb->udi)
	free(glb->udi);

    free_morph_list(glb);

    if (glb->crypt_key)
	free(glb->crypt_key);

    if (glb->buf)
	free(glb->buf);

    if (glb->udfunc)
	free(glb->udfunc);

    free(glb);
}


static int find_in_list(int id, void *data, pfa_global_t *glb)
{
    list_t *tmp;

    if (data == NULL ) return(-1);
    
    if (glb->l_num[id] != 0){

	tmp = glb->buckets[id][((ulong)data & glb->bucket_mask[id]) >> 4];

	while (tmp)
	{
	    if (tmp->data == data)
		return(tmp->id);
	    tmp = tmp->bnext;
	}
    }

    /* for geoArrays, node may think it has a geoSet instead,
     * here we hack the index id to flag that indeed it is a geoArray.
     */
    if(id == L_GSET){
	if(glb->l_num[L_GARRAY] == 0) return (-1);
	
        tmp = glb->buckets[L_GARRAY][((ulong)data & glb->bucket_mask[L_GARRAY]) >> 4];
	while (tmp)
	{
	    if (tmp->data == data){
		return(tmp->id | PFB_GARRAY_IXBIT);
	    }
	    tmp = tmp->bnext;
	    
	}
    }
    return(-1);
}


static list_t *find_list_item(int id, void *data, pfa_global_t *glb)
{
    list_t *tmp;

    if (data == NULL || glb->l_num[id] == 0)
	return(NULL);

    tmp = glb->buckets[id][((ulong)data & glb->bucket_mask[id]) >> 4];

    while (tmp)
    {
	if (tmp->data == data)
	    return(tmp);
	tmp = tmp->bnext;
    }

    return(NULL);
}


static int find_in_unknown_list(void *data, int *list, pfa_global_t *glb)
{
    pfType *type;

    type = pfGetType(data);

    if (pfIsDerivedFrom(type, pfGetNodeClassType()))
	*list = L_NODE;
    else if (pfIsDerivedFrom(type, pfGetGSetClassType()))
	*list = L_GSET;
    else if (pfIsDerivedFrom(type, pfGetGStateClassType()))
	*list = L_GSTATE;
    else if (pfIsDerivedFrom(type, pfGetTexClassType()))
	*list = L_TEX;
    else if (pfIsDerivedFrom(type, pfGetHlightClassType()))
	*list = L_HLIGHT;
    else if (pfIsDerivedFrom(type, pfGetMorphClassType()))
	*list = L_MORPH;
    else if (pfIsDerivedFrom(type, pfGetStringClassType()))
	*list = L_STRING;
    else if (pfIsDerivedFrom(type, pfGetFontClassType()))
	*list = L_FONT;
    else if (pfIsDerivedFrom(type, pfGetMtlClassType()))
	*list = L_MTL;
    else if (pfIsDerivedFrom(type, pfGetTEnvClassType()))
	*list = L_TENV;
    else if (pfIsDerivedFrom(type, pfGetLODStateClassType()))
	*list = L_LODSTATE;
    else if (pfIsDerivedFrom(type, pfGetFogClassType()))
	*list = L_FOG;
    else if (pfIsDerivedFrom(type, pfGetTGenClassType()))
	*list = L_TGEN;
    else if (pfIsDerivedFrom(type, pfGetLModelClassType()))
	*list = L_LMODEL;
    else if (pfIsDerivedFrom(type, pfGetLightClassType()))
	*list = L_LIGHT;
    else if (pfIsDerivedFrom(type, pfGetCtabClassType()))
	*list = L_CTAB;
    else if (pfIsDerivedFrom(type, pfGetLPStateClassType()))
	*list = L_LPSTATE;
    else if (pfIsDerivedFrom(type, pfGetFrustClassType()))
	*list = L_FRUST;
    else
    {
	*list = -1;
	return(-1);		/* Whatever this is, we can not find it. */
    }

    return(find_in_list(*list, data, glb));
}


static int find_data_list(void *data, pfa_global_t *glb)
{
    if (find_in_list(L_VLIST, data, glb) != -1)
	return L_VLIST;
    else if (find_in_list(L_CLIST, data, glb) != -1)
	return L_CLIST;
    else if (find_in_list(L_NLIST, data, glb) != -1)
	return L_NLIST;
    else if (find_in_list(L_TLIST, data, glb) != -1)
	return L_TLIST;
    else if (find_in_list(L_FLIST, data, glb) != -1)
	return L_FLIST;
    else
	return -1;
}


static int add_to_list(int id, void *data, pfa_global_t *glb)
{
    list_t *tmp;
    int bucket;
    int num;

    if (data == NULL)
	return(0);

    if (glb->buckets[id])
    {
	/*
	 *  Search to see if data is already in the list.
	 */
	bucket = (int)((ulong)data & glb->bucket_mask[id]) >> 4;
	tmp = glb->buckets[id][bucket];
	while (tmp)
	{
	    if (tmp->data == data)
		return(0);
	    tmp = tmp->bnext;
	}

	/*
	 *  We must not have been able to find data in the list.
	 */
	tmp = (list_t *)malloc(sizeof(list_t));
	glb->l_list_end[id]->next = tmp;
    }
    else
    {
	/*
	 *  This is the first item in the list.
	 */
	tmp = (list_t *)malloc(sizeof(list_t));
	glb->l_list[id] = tmp;
	glb->bucket_mask[id] = 0xff;
	bucket = (int)((ulong)data & 0xff) >> 4;
	glb->buckets[id] = (list_t **)malloc(sizeof(list_t *) * 16);
	bzero(glb->buckets[id], sizeof(list_t *) * 16);
    }

    tmp->data = data;
    tmp->id = glb->l_num[id]++;
    tmp->next = NULL;
    tmp->bnext = glb->buckets[id][bucket];
    glb->buckets[id][bucket] = tmp;
    glb->l_list_end[id] = tmp;

    /*
     *  If there are more then 16 items on average per bucket then
     *  double the number of buckets.
     */
    if (glb->l_num[id] > glb->bucket_mask[id])
	double_buckets(id, glb);

    /*
     *  check for custom nodes
     */
    if (id == L_NODE)
    {
	int custom_id;

	if (glb->custom_nodes && (custom_id = is_custom_node(data, glb)) != -1)
	{
	    tmp->size = N_CUSTOM | (custom_id << N_CUSTOM_SHIFT);
	    glb->custom_nodes[custom_id].used = 1;
	    if (glb->custom_nodes[custom_id].descend_func)
		glb->custom_nodes[custom_id].descend_func(data, glb);
	}
	else
	    tmp->size = 0;
    }

    /*
     *  check for user data
     */
    if (id != L_UDATA && id != L_UDATA_LIST && id != L_UDATA_NAME &&
	id != L_IMAGE && id != L_UFUNC && id != L_SG_NAME)
    {
	if (pfIsOfType(data, pfGetObjectClassType()))
	{
	    if (num = pfGetNumUserData(data))
		descend_udata_list(num, data, glb);
	}
	else
	    pfNotify(PFNFY_WARN, PFNFY_INTERNAL,
		    "%s: add_to_list(id=%d): expected a pfObject",
		    "pfdStoreFile_pfb", id);
    }

    return(1);
}

/*
 * returns the position of the element in the array
 */
static int add_to_slist(int id, void *data, int size, pfa_global_t *glb)
{
    list_t *tmp;
    int bucket;
    pfCycleBuffer *cbuf;
    int num;

    if (glb->buckets[id])
    {
	/*
	 *  Search to see if data is already in the list.
	 */
	bucket = (int)((ulong)data & glb->bucket_mask[id]) >> 4;
	tmp = glb->buckets[id][bucket];
	while (tmp)
	{
	    if (tmp->data == data)
	    {
		if (tmp->size < size)
		    tmp->size = size;
		return tmp->id;
	    }
	    tmp = tmp->bnext;
	}

	/*
	 *  We must not have been able to find data in the list.
	 */
	tmp = (list_t *)malloc(sizeof(list_t));
	glb->l_list_end[id]->next = tmp;
    }
    else
    {
	/*
	 *  This is the first item in the list.
	 */
	tmp = (list_t *)malloc(sizeof(list_t));
	glb->l_list[id] = tmp;
	glb->bucket_mask[id] = 0xff;
	bucket = (int)((ulong)data & 0xff) >> 4;
	glb->buckets[id] = (list_t **)malloc(sizeof(list_t *) * 16);
	bzero(glb->buckets[id], sizeof(list_t *) * 16);
    }

    tmp->data = data;
    tmp->size = size;
    tmp->id = glb->l_num[id]++;
    tmp->next = NULL;
    tmp->bnext = glb->buckets[id][bucket];
    glb->buckets[id][bucket] = tmp;
    glb->l_list_end[id] = tmp;

    /*
     *  If there are more then 16 items on average per bucket then
     *  double the number of buckets.
     */
    if (glb->l_num[id] > glb->bucket_mask[id])
	double_buckets(id, glb);

    /*
     *  check for user data
     */
    if (cbuf = pfGetCBuffer(data))
	if (num = pfGetNumUserData((pfObject *)cbuf))
	    descend_udata_list(num, cbuf, glb);

    return tmp->id;
}


static void double_buckets(int id, pfa_global_t *glb)
{
    int bucket, num_buckets;
    list_t *tmp;

    /*
     *  remake the buckets
     */
    free(glb->buckets[id]);
    glb->bucket_mask[id] = (glb->bucket_mask[id] << 1) | 0x1;
    num_buckets = (glb->bucket_mask[id] >> 4) + 1;
    glb->buckets[id] = (list_t **)malloc(sizeof(list_t *) * num_buckets);
    bzero(glb->buckets[id], sizeof(list_t *) * num_buckets);

    /*
     *  resort the list into the buckets
     */
    for (tmp = glb->l_list[id]; tmp != NULL; tmp = tmp->next)
    {
	bucket = (int)((ulong)tmp->data & glb->bucket_mask[id]) >> 4;
	tmp->bnext = glb->buckets[id][bucket];
	glb->buckets[id][bucket] = tmp;
    }
}


static int find_in_table(const int *table, int item)
{
    int i;

    for (i = 1; i <= table[0]; i++)
	if (table[i] == item)
	    return(i);

    for (i = 0; table_names[i].addr != table && table_names[i].addr != NULL;
	 i++);
    pfNotify(PFNFY_WARN, PFNFY_INTERNAL,
		 "%s: find_in_table() could not find %d (0x%x) in %s table.\n",
		 "pfdStoreFile_pfb", item, item, table_names[i].name);

	return(1);
    }


    static uint to_table(const uint *table, uint src)
    {
	uint res, all_table;
	int i;

	res = 0x0;
	all_table = 0x0;
	for (i = 1, res = 0x0; i <= table[0]; i++)
	{
	    if ((src & table[i]) == table[i])
		res |= 0x1 << (i - 1);
	    all_table |= table[i];
	}

	if ((~all_table) & src)
	{
	    for (i = 0; table_names[i].addr != table && table_names[i].addr != NULL;
		 i++);
	    pfNotify(PFNFY_WARN, PFNFY_INTERNAL,
		     "%s:  to_table() src has bits not in %s table (0x%08x).\n",
		     "pfdStoreFile_pfb", table_names[i].name, (~all_table) & src);
	}

	return(res);
    }


    static uint from_table(const uint *table, uint src)
    {
	uint res;
	int i;

	res = 0x0;
	for (i = 1, res = 0x0; i <= table[0]; i++)
	    if (src & (0x1 << (i - 1)))
		res |= table[i];

	return(res);
    }


    static pfObject *share_obj(pfObject *obj, pfa_global_t *glb)
    {
	pfObject *match;

	if (glb->share_gs_objects == PF_OFF)
	    return obj;

	if (match = pfdFindSharedObject(glb->share, obj))
	{
	    pfDelete(obj);
	    return(match);
	}

	pfdAddSharedObject(glb->share, obj);
	return(obj);
    }


    static pfTexture *share_tex(tex_list_t *tl, pfa_global_t *glb)
    {
	tex_t *t;
	pfList *list;
	int i, differ;

	t = tl->t;

	if (t->tdetail[1] != -1)
	{
	    tl->dtex = glb->rl_list[L_TEX][t->tdetail[1]];
	    t->tdetail[1] = 0;
	}
	else
	    tl->dtex = NULL;

	if (t->list_size > 0)
	{
	    tl->ptlist = (pfTexture **)malloc(t->list_size * sizeof(pfTexture *));
	    for (i = 0; i < t->list_size; i++)
		tl->ptlist[i] = glb->rl_list[L_TEX][tl->itlist[i]];
	}
	else
	    tl->ptlist = NULL;

	if (t->num_levels > 0)
	{
	    tl->level_objs = (pfObject **)malloc(t->num_levels *
						 sizeof(pfObject *));
	    if (t->type == TEXTYPE_TEXTURE)
		for (i = 0; i < t->num_levels; i++)
		{
		    tl->level_objs[i] = glb->rl_list[L_TEX][tl->levels[i].obj];
		    tl->levels[i].obj = 0;
		}
	    else /* (t->type == TEXTYPE_CLIPTEXTURE) */
		for (i = 0; i < t->num_levels; i++)
		{
		    tl->level_objs[i] = glb->rl_list[tl->cliplevels[i].type]
						    [tl->cliplevels[i].obj];
		    tl->cliplevels[i].obj = 0;
		}
	}
	else
	    tl->level_objs = NULL;

	if (t->image != -1)
	{
	    tl->image = glb->rl_list[L_IMAGE][t->image];
	    t->image = 0;
	}
	else
	    tl->image = NULL;

	for(i=0; i<tl->num_images; i++)
	{
	    if(i >= TEX_MAX_IMAGES)
		break;
	    
	    if (tl->iimages[i] != -1)
	    {
		tl->images[i] = glb->rl_list[L_IMAGE][tl->iimages[i]];
		tl->iimages[i] = 0;
	    }
	    else
		tl->images[i] = NULL;
	}

	if (t->load_image != -1)
	{
	    tl->load_image = glb->rl_list[L_IMAGE][t->load_image];
	    t->load_image = 0;
	}
	else
	    tl->load_image = NULL;

	if (t->type == TEXTYPE_CLIPTEXTURE && tl->ct->master != -1)
	{
	    tl->master = glb->rl_list[L_TEX][tl->ct->master];
	    tl->ct->master = 0;
	}
	else
	    tl->master = NULL;

	if (glb->share_gs_objects == PF_ON)
	{
	    tex_list_t *tmp;

	    for (tmp = tex_list; tmp != NULL; tmp = tmp->next)
	    {
		if ((tl->name == NULL && tmp->name != NULL) ||
		    (tl->name != NULL && tmp->name == NULL))
		    continue;
		if (tl->name != NULL && tmp->name != NULL &&
		    strcmp(tl->name, tmp->name))
		    continue;
		if(tl->num_images != tmp->num_images)
		    continue;

		differ = 0;

		for(i=0; i<tl->num_images; i++)
		{
		    if(i >= TEX_MAX_IMAGES)
			break;
		    
		    if (tl->images[i] != tmp->images[i] ||
			tl->names[i] != tmp->names[i])
			{
			    differ = 1;
			    break;
			}
		}
		
		if(differ)
		    continue;

		if (mem_diff((uint *)t, (uint *)tmp->t, sizeof(tex_t)/sizeof(int)))
		    continue;
		if (tl->dtex != tmp->dtex)
		    continue;
		if ((!tl->image && tmp->image) ||
		    (tl->image && !tmp->image))
		    continue;
		if ((tl->image && tmp->image) &&
		    (pfGetSize(tl->image) != pfGetSize(tmp->image) ||
		     mem_diff(tl->image, tmp->image,
			      pfGetSize(tl->image)/sizeof(int))))
		    continue;
		if ((!tl->load_image && tmp->load_image) ||
		    (tl->load_image && !tmp->load_image))
		    continue;
		if ((tl->load_image && tmp->load_image) &&
		    (pfGetSize(tl->load_image) != pfGetSize(tmp->load_image) ||
		     mem_diff(tl->load_image, tmp->load_image,
			      pfGetSize(tl->load_image)/sizeof(int))))
		    continue;
		if ((!tl->ptlist && tmp->ptlist) ||
		    (tl->ptlist && !tmp->ptlist))
		    continue;
		if ((tl->ptlist && tmp->ptlist) &&
		    mem_diff((uint *)tl->ptlist, (uint *)tmp->ptlist, t->list_size))
		    continue;
		if (t->num_levels &&
		    mem_diff((uint *)tl->level_objs, (uint *)tmp->level_objs,
			     t->num_levels * sizeof(pfObject *)))
		    continue;
		if (t->type == TEXTYPE_TEXTURE)
		{
		    if ((!tl->levels && tmp->levels) ||
			(tl->levels && !tmp->levels))
			continue;
		    if ((tl->levels && tmp->levels) &&
			mem_diff((uint *)tl->levels, (uint *)tmp->levels,
				  t->num_levels * sizeof(level_t)))
			continue;
		}
		else /* (t->type == TEXTYPE_CLIPTEXTURE) */
		{
		    if (mem_diff((uint *)tl->ct, (uint *)tmp->ct,
				 sizeof(cliptex_t)/sizeof(int)))
			continue;
		    if ((!tl->cliplevels && tmp->cliplevels) ||
			(tl->cliplevels && !tmp->cliplevels))
			continue;
		    if ((tl->cliplevels && tmp->cliplevels) &&
			mem_diff((uint *)tl->cliplevels, (uint *)tmp->cliplevels,
				  t->num_levels * sizeof(cliplevel_t)))
			continue;
		}

		/*
		 *  found a matching texture
		 */
		free_tex_list_t(tl);
		return(tmp->tex);
	    }

	    /*
	     *  Add to pfb's internal texture list
	     */
	    if (tl->tex = make_tex(tl, glb))
	    {
		pfRef(tl->tex);
		tl->next = tex_list;
		tex_list = tl;

		/*
		 *  Do a psudo add to the pfdShare's texture list
		 */
		list = pfdGetSharedList(glb->share, pfGetTexClassType());
		pfAdd(list, tl->tex);
		pfRef(tl->tex);
	    }

	    return(tl->tex);
	}
	else /* not sharing textures */
	{
	    pfTexture *tex;

	    tex = make_tex(tl, glb);

	    free_tex_list_t(tl);
	    return(tex);
	}
    }


    static void free_tex_list_t(tex_list_t *tl)
    {
	int i;

	if (tl->name)
	    free(tl->name);
	if (tl->num_images)
	    for(i=0; i<tl->num_images; i++)
		free(tl->names[i]);
	if (tl->itlist)
	    free(tl->itlist);
	if (tl->ptlist)
	    free(tl->ptlist);
	if (tl->level_objs)
	    free(tl->level_objs);
	if (tl->t->type == TEXTYPE_TEXTURE)
	{
	    if (tl->levels)
		free(tl->levels);
	}
	else /* (tl->t->type == TEXTYPE_CLIPTEXTURE) */
	{
	    free(tl->ct);
	    if (tl->cliplevels)
		free(tl->cliplevels);
	}
	free(tl->t);
	free(tl);
    }


    static pfTexture *make_tex(tex_list_t *tl, pfa_global_t *glb)
    {
	pfTexture *tex;
	pfList *tlist;
	tex_t *t;
	int i, count;
	int file_loaded;

	t = tl->t;

	file_loaded = FALSE;

	if (t->type == TEXTYPE_TEXTURE)
	{
	    tex = pfNewTex(glb->arena);

	    if(tl->num_images == 0) 
	    {
	      if (tl->image)
		{
		  pfTexImage(tex, tl->image, t->comp, t->xsize, t->ysize, t->zsize);
		  if (tl->name)
		    pfTexName(tex, tl->name);
		}
	      else if (tl->name)
		{
		  file_loaded = pfLoadTexFile(tex, tl->name);

		  if (tlist = pfGetTexList(tex))
		    {
		      /*
		       *  This has to be a multi image pfi file.
		       */
		      pfTexFrame(tex, t->frame);
		      if (t->udata != -1)
			set_udata((pfObject*)tex, t->udata, glb);
		      
		      count = pfGetNum(tlist);
		      for (i = 0; i < count; i++)
			set_pfi_multi((pfTexture*)pfGet(tlist, i), tl);
		      
		      return(tex);
		    }
		  if (!file_loaded)
		    {
		      pfDelete (tex);
		      return NULL;
		    }
		}
	    } else {
	      for(i=0; i<tl->num_images; i++)
		{
		  if (tl->images[i])
		    {
		      pfTexMultiImage(tex, tl->images[i], i, 
				      t->comp, t->xsize, t->ysize, t->zsize);
		      if (tl->names[i])
			pfTexMultiName(tex, tl->names[i], i);
		    }
		  else if (tl->names[i])
		    {
		      file_loaded = pfLoadMultiTexFile(tex, tl->names[i], i);
		      
		      if (tlist = pfGetTexList(tex))
			{
			  /*
			   *  This has to be a multi image pfi file.
			   */
			  pfTexFrame(tex, t->frame);
			  if (t->udata != -1)
			    set_udata((pfObject*)tex, t->udata, glb);
			  
			  count = pfGetNum(tlist);
			  for (i = 0; i < count; i++)
			    set_pfi_multi((pfTexture*)pfGet(tlist, i), tl);
			  
			  return(tex);
			}
		      if (!file_loaded)
			{
			  pfDelete (tex);
			  return NULL;
			}
		    }
		}
	    }
	    if (t -> aniso_degree > 1)
		    pfTexAnisotropy (tex, t -> aniso_degree);
	}
	else /* (t->type == TEXTYPE_CLIPTEXTURE) */
	{
	    pfClipTexture *ctex;
	    cliptex_t *ct;
	    char *s;

	    ct = tl->ct;
	    if (tl->name && t->num_levels == 0 &&
		(s = strrchr(tl->name, '.')) && strcmp(s, ".ct") == 0)
		ctex = pfdLoadClipTexture(tl->name);
	    else
		ctex = pfNewClipTexture(glb->arena);

	    pfClipTextureCenter(ctex, ct->center[0], ct->center[1], ct->center[2]);
	    pfClipTextureClipSize(ctex, ct->clip_size);
	    pfClipTextureVirtualSize(ctex, ct->virtual_size[0],
				     ct->virtual_size[1], ct->virtual_size[2]);
	    pfClipTextureInvalidBorder(ctex, ct->invalid_border);
	    pfClipTextureVirtualLODOffset(ctex, ct->virtual_LOD_offset);
	    pfClipTextureNumEffectiveLevels(ctex, ct->effective_levels);
	    pfClipTextureLODRange(ctex, ct->LOD_range[0], ct->LOD_range[1]);

	    for (i = 0; i < t->num_levels; i++)
	    {
		pfClipTextureLevel(ctex, tl->cliplevels[i].level,
				   tl->level_objs[i]);
		if (tl->cliplevels[i].type == L_ICACHE)
		    pfImageCacheTex((pfImageCache *)tl->level_objs[i],
				    (void *)ctex, tl->cliplevels[i].level,
				    PFTLOAD_DEST_TEXTURE);
		pfClipTextureLevelPhaseMargin(ctex, tl->cliplevels[i].level,
					      tl->cliplevels[i].phase_margin);
		pfClipTextureLevelPhaseShift(ctex, tl->cliplevels[i].level,
					     tl->cliplevels[i].phase_shift[0],
					     tl->cliplevels[i].phase_shift[1],
					     tl->cliplevels[i].phase_shift[2]);
	    }

	    tex = (pfTexture *)ctex;
	}

	pfTexFormat(tex, PFTEX_INTERNAL_FORMAT, txif_table[t->format[0]]);
	if (!file_loaded)
	    pfTexFormat(tex, PFTEX_EXTERNAL_FORMAT, txef_table[t->format[1]]);
	pfTexFormat(tex, PFTEX_DETAIL_TEXTURE, oo_table[t->format[2]]);
	pfTexFormat(tex, PFTEX_SUBLOAD_FORMAT, oo_table[t->format[3]]);
	pfTexFormat(tex, PFTEX_FAST_DEFINE, oo_table[t->format[4] & 0x3]);
	pfTexFormat(tex, PFTEX_CUBE_MAP, (t->format[4] >> 2));

	    if (!pfIsOfType (tex, pfGetClipTextureClassType()))
		    pfTexFilter(tex, PFTEX_MINFILTER, from_table(txf_table, t->filter[0]));
	pfTexFilter(tex, PFTEX_MAGFILTER, from_table(txf_table, t->filter[1]));
	pfTexFilter(tex, PFTEX_MAGFILTER_ALPHA,
		    from_table(txf_table, t->filter[2]));
	pfTexFilter(tex, PFTEX_MAGFILTER_COLOR,
		    from_table(txf_table, t->filter[3]));

	if (t->wrap[0] == t->wrap[1] && t->wrap[0] == t->wrap[2])
	    pfTexRepeat(tex, PFTEX_WRAP, txr_table[t->wrap[0]]);
	else
	{
	    pfTexRepeat(tex, PFTEX_WRAP_R, txr_table[t->wrap[0]]);
	    pfTexRepeat(tex, PFTEX_WRAP_S, txr_table[t->wrap[1]]);
	    pfTexRepeat(tex, PFTEX_WRAP_T, txr_table[t->wrap[2]]);
	}

	pfTexBorderColor(tex, t->bcolor);
	pfTexBorderType(tex, txb_table[t->btype]);

	if (t->ssc != BROKEN_SPLINE)
	    pfTexSpline(tex, PFTEX_SHARPEN_SPLINE, t->ssp, t->ssc);

	if (t->dsc != BROKEN_SPLINE)
	    pfTexSpline(tex, PFTEX_DETAIL_SPLINE, t->dsp, t->dsc);

	pfTexLoadMode(tex, PFTEX_LOAD_SOURCE, txls_table[t->lmode[0]]);
	pfTexLoadMode(tex, PFTEX_LOAD_BASE, txlb_table[t->lmode[1]]);
	pfTexLoadMode(tex, PFTEX_LOAD_LIST, txll_table[t->lmode[2]]);
	pfTexLoadOrigin(tex, PFTEX_ORIGIN_SOURCE, t->losource[0], t->losource[1]);
	pfTexLoadOrigin(tex, PFTEX_ORIGIN_DEST, t->lodest[0], t->lodest[1]);
	pfTexLoadSize(tex, t->lsize[0], t->lsize[1]);
	if (tl->dtex)
	    pfTexDetail(tex, t->tdetail[0], tl->dtex);
	if (tl->ptlist)
	{
	    tlist = pfNewList(sizeof(pfTexture *), t->list_size, glb->arena);
	    for (i = 0; i < t->list_size; i++)
		pfAdd(tlist, tl->ptlist[i]);
	    pfTexList(tex, tlist);
	}
	pfTexFrame(tex, t->frame);
	if (t->type == TEXTYPE_TEXTURE)
	{
	    if (t->num_levels != 0)
	    {
		pfTexFormat(tex, PFTEX_GEN_MIPMAP_FORMAT, 0);
		for (i = 0; i < t->num_levels; i++)
		    pfTexLevel(tex, tl->levels[i].level,
			       (pfTexture *)tl->level_objs[i]);
	    }
	}
	if (tl->load_image)
	    pfTexLoadImage(tex, tl->load_image);

	if (t->udata != -1)
	    set_udata((pfObject *)tex, t->udata, glb);

	return(tex);
    }


    static void set_pfi_multi(pfTexture *tex, tex_list_t *tl)
    {
	tex_t *t;
	int i;

	t = tl->t;

	pfTexFormat(tex, PFTEX_INTERNAL_FORMAT, txif_table[t->format[0]]);
	pfTexFormat(tex, PFTEX_DETAIL_TEXTURE, oo_table[t->format[2]]);
	pfTexFormat(tex, PFTEX_SUBLOAD_FORMAT, oo_table[t->format[3]]);
	pfTexFormat(tex, PFTEX_FAST_DEFINE, oo_table[t->format[4] & 0x3]);
	pfTexFormat(tex, PFTEX_CUBE_MAP, (t->format[4] >> 2));

	pfTexFilter(tex, PFTEX_MINFILTER, from_table(txf_table, t->filter[0]));
	pfTexFilter(tex, PFTEX_MAGFILTER, from_table(txf_table, t->filter[1]));
	pfTexFilter(tex, PFTEX_MAGFILTER_ALPHA,
		    from_table(txf_table, t->filter[2]));
	pfTexFilter(tex, PFTEX_MAGFILTER_COLOR,
		    from_table(txf_table, t->filter[3]));

	if (t->wrap[0] == t->wrap[1] && t->wrap[0] == t->wrap[2])
	    pfTexRepeat(tex, PFTEX_WRAP, txr_table[t->wrap[0]]);
	else
	{
	    pfTexRepeat(tex, PFTEX_WRAP_R, txr_table[t->wrap[0]]);
	    pfTexRepeat(tex, PFTEX_WRAP_S, txr_table[t->wrap[1]]);
	    pfTexRepeat(tex, PFTEX_WRAP_T, txr_table[t->wrap[2]]);
	}

	pfTexBorderColor(tex, t->bcolor);
	pfTexBorderType(tex, txb_table[t->btype]);

	if (t->ssc != BROKEN_SPLINE)
	    pfTexSpline(tex, PFTEX_SHARPEN_SPLINE, t->ssp, t->ssc);

	if (t->dsc != BROKEN_SPLINE)
	    pfTexSpline(tex, PFTEX_DETAIL_SPLINE, t->dsp, t->dsc);

	pfTexLoadMode(tex, PFTEX_LOAD_SOURCE, txls_table[t->lmode[0]]);
	pfTexLoadMode(tex, PFTEX_LOAD_BASE, txlb_table[t->lmode[1]]);
	pfTexLoadMode(tex, PFTEX_LOAD_LIST, txll_table[t->lmode[2]]);
	pfTexLoadOrigin(tex, PFTEX_ORIGIN_SOURCE, t->losource[0], t->losource[1]);
	pfTexLoadOrigin(tex, PFTEX_ORIGIN_DEST, t->lodest[0], t->lodest[1]);
	pfTexLoadSize(tex, t->lsize[0], t->lsize[1]);
	if (tl->dtex)
	    pfTexDetail(tex, t->tdetail[0], tl->dtex);
	if (t->num_levels != 0)
	{
	    pfTexFormat(tex, PFTEX_GEN_MIPMAP_FORMAT, 0);
	    for (i = 0; i < t->num_levels; i++)
		pfTexLevel(tex, tl->levels[i].level,
			   (pfTexture *)tl->level_objs[i]);
	}
    }


    static int mem_diff(uint *p1, uint *p2, size_t size)
    {
	int i;

	for (i = 0; i < size; i++)
	    if (p1[i] != p2[i])
		return(1);

	return(0);
    }


PFPFB_DLLEXPORT     void pfdConverterMode_pfb(int mode, int value)
    {
	switch (mode)
	{
	    case PFPFB_SAVE_TEXTURE_IMAGE:
		if (value == PF_ON || value == PF_OFF)
		    save_texture_image = value;
		else
		    pfNotify(PFNFY_WARN, PFNFY_USAGE,
			     "%s:  Unknown value %d for PFPFB_SAVE_TEXTURE_IMAGE",
			     "pfdConverterMode_pfb", value);
		break;
	    case PFPFB_SAVE_TEXTURE_PATH:
		if (value == PF_ON || value == PF_OFF)
		    save_texture_path = value;
		else
		    pfNotify(PFNFY_WARN, PFNFY_USAGE,
			     "%s:  Unknown value %d for PFPFB_SAVE_TEXTURE_PATH",
			     "pfdConverterMode_pfb", value);
		break;
	    case PFPFB_SHARE_GS_OBJECTS:
		if (value == PF_ON || value == PF_OFF)
		    share_gs_objects = value;
		else
		    pfNotify(PFNFY_WARN, PFNFY_USAGE,
			     "%s:  Unknown value %d for PFPFB_SHARE_GS_OBJECTS",
			     "pfdConverterMode_pfb", value);
		break;
	    case PFPFB_COMPILE_GL:
		if (value == PFPFB_COMPILE_GL_OFF || value == PFPFB_COMPILE_GL_ON ||
		    value == PFPFB_COMPILE_GL_AS_SAVED)
		    compile_gl = value;
		else
		    pfNotify(PFNFY_WARN, PFNFY_USAGE,
			     "%s:  Unknown value %d for PFPFB_COMPILE_GL",
			     "pfdConverterMode_pfb", value);
		break;
	    case PFPFB_SAVE_TEXTURE_PFI:
		if (value == PF_ON || value == PF_OFF)
		    save_texture_pfi = value;
		else
		    pfNotify(PFNFY_WARN, PFNFY_USAGE,
			     "%s:  Unknown value %d for PFPFB_SAVE_TEXTURE_PFI",
			     "pfdConverterMode_pfb", value);
		break;
	    case PFPFB_DONT_LOAD_USER_FUNCS:
		if (value == PF_ON || value == PF_OFF)
		    dont_load_user_funcs = value;
		else
		    pfNotify(PFNFY_WARN, PFNFY_USAGE,
			     "%s:  Unknown value %d for PFPFB_DONT_LOAD_USER_FUNCS",
			     "pfdConverterMode_pfb", value);
		break;
            case PFPFB_DISABLE_REP_SAVE:
		if (value == PF_ON || value == PF_OFF)
		    disable_rep_save = value;
		else
		    pfNotify(PFNFY_WARN, PFNFY_USAGE,
			     "%s: Unknown value %d for PFPFB_DISABLE_REP_SAVE",
			     "pfdConverterMode_pfb", value);
		break;
            case PFPFB_SUBDIVSURFACE_SAVE_GEOSETS:
		if (value == PF_ON || value == PF_OFF)
		    subdivsurface_save_geosets = value;
		else
		    pfNotify(PFNFY_WARN, PFNFY_USAGE,
			     "%s: Unknown value %d for PFPFB_SUBDIVSURFACE_SAVE_GEOSETS",
			     "pfdConverterMode_pfb", value);
		break;
	    default:
		pfNotify(PFNFY_WARN, PFNFY_USAGE,
			 "pfdConverterMode_pfb: Unknown mode %d", mode);
	}
    }


PFPFB_DLLEXPORT     int pfdGetConverterMode_pfb(int mode)
    {
	switch (mode)
	{
	    case PFPFB_SAVE_TEXTURE_IMAGE:
		return(save_texture_image);
	    case PFPFB_SAVE_TEXTURE_PATH:
		return(save_texture_path);
	    case PFPFB_SHARE_GS_OBJECTS:
		return(share_gs_objects);
	    case PFPFB_COMPILE_GL:
		return(compile_gl);
	    case PFPFB_SAVE_TEXTURE_PFI:
		return(save_texture_pfi);
	    case PFPFB_DONT_LOAD_USER_FUNCS:
		return(dont_load_user_funcs);
	    case PFPFB_DISABLE_REP_SAVE:
		return(disable_rep_save);
	    case PFPFB_SUBDIVSURFACE_SAVE_GEOSETS:
		return(subdivsurface_save_geosets);
	    default:
		pfNotify(PFNFY_WARN, PFNFY_USAGE,
			 "pfdGetConverterMode_pfb: Unknown mode %d", mode);
		return(-1);
	}
    }


PFPFB_DLLEXPORT     void pfdConverterVal_pfb(int which, float val)
    {
	val = val;		/* HACK to avoid "never referenced" compile warning */

	switch (which)
	{
	    default:
		pfNotify(PFNFY_WARN, PFNFY_USAGE,
			 "pfdConverterVal_pfb: Unknown which %d", which);
	}
    }


PFPFB_DLLEXPORT     float pfdGetConverterVal_pfb(int which)
    {
	switch (which)
	{
	    default:
		pfNotify(PFNFY_WARN, PFNFY_USAGE,
			 "pfdGetConverterVal_pfb: Unknown which %d", which);
		return(-1.0f);
	}
    }


PFPFB_DLLEXPORT     void pfdConverterAttr_pfb(int which, void *attr)
    {
	attr = attr;	/* HACK to avoid "never referenced" compile warning */

	switch (which)
	{
	    default:
		pfNotify(PFNFY_WARN, PFNFY_USAGE,
			 "pfdConverterAttr_pfb: Unknown which %d", which);
	}
    }


PFPFB_DLLEXPORT     void *pfdGetConverterAttr_pfb(int which)
    {
	switch (which)
	{
	    default:
		pfNotify(PFNFY_WARN, PFNFY_USAGE,
			 "pfdGetConverterAttr_pfb: Unknown which %d", which);
		return(NULL);
	}
    }


PFPFB_DLLEXPORT     void pfdConverterMode_pfa(int mode, int value)
    {
	pfdConverterMode_pfb(mode, value);
    }


PFPFB_DLLEXPORT     int pfdGetConverterMode_pfa(int mode)
    {
	return(pfdGetConverterMode_pfb(mode));
    }


PFPFB_DLLEXPORT     void pfdConverterVal_pfa(int which, float val)
    {
	pfdConverterVal_pfb(which, val);
    }


PFPFB_DLLEXPORT     float pfdGetConverterVal_pfa(int which)
    {
	return(pfdGetConverterVal_pfb(which));
    }


PFPFB_DLLEXPORT     void pfdConverterAttr_pfa(int which, void *attr)
    {
	pfdConverterAttr_pfb(which, attr);
    }


PFPFB_DLLEXPORT     void *pfdGetConverterAttr_pfa(int which)
    {
	return(pfdGetConverterAttr_pfb(which));
    }


    static void pfb_decrypt(int size, uint *key, void *data)
    {
	int i;
	uint *ui_data;
	uint ui_size;
	int keylength;

	ui_data = data;
	ui_size = size >> 2;
	keylength = key[0];

	for(i = 0; i < ui_size; i++)
	    ui_data[i] ^= key[(i % keylength) + 1];
    }


    static void pfb_encrypt(int size, uint *key, void *src, void *dst)
    {
	int i;
	uint *ui_src, *ui_dst;
	uint ui_size;
	int keylength;

	ui_src = src;
	ui_dst = dst;
	ui_size = size >> 2;
	keylength = key[0];

	for(i = 0; i < ui_size; i++)
	    ui_dst[i] = ui_src[i] ^ key[(i % keylength) + 1];
    }


    static int get_udata(pfObject *obj, pfa_global_t *glb)
    {
	if (glb->max_udata_slot > 1)
	    return find_in_list(L_UDATA_LIST, obj, glb);
	else
	    return find_in_list(L_UDATA, pfGetUserData(obj), glb);
    }

    /*
     * Set all of the user data on obj.
     * udata_id is an index into the list of udata lists.
     * The resulting list is in pairs: name (i.e. index into the udata name list),
     * item, name, item, etc.
     * Calls set_udata_slot for each of these pairs.
     */
    static void set_udata(pfObject *obj, int udata_id, pfa_global_t *glb)
    {
	if (glb->rl_list[L_UDATA_LIST] != NULL)
	{
	    int i, *list;

	    list = glb->rl_list[L_UDATA_LIST][udata_id];

	    for (i = 0; i < list[0]; i++)
		set_udata_slot(obj, (int)*(size_t*)&glb->rl_list[L_UDATA_NAME][list[i*2+1]],
			       list[i*2+2], glb);
	}
	else
	    set_udata_slot(obj, 0, udata_id, glb);
    }

    /*
     * Set the user data in a particular slot on obj.
     * slot is the actual libpf slot number.
     * udata_id is an index into glb->rl_list[L_UDATA]. (rl == relocation list? -dh)
     */
    static void set_udata_slot(pfObject *obj, int slot, int udata_id,
			       pfa_global_t *glb)
    {
	PFASSERTDEBUG(udata_id != -1);
	if (glb->rl_list[L_UDATA][udata_id])
	    pfUserDataSlot(obj, slot, glb->rl_list[L_UDATA][udata_id]);
	else
	{
	    udata_fix_t *udf;

	    udf = (udata_fix_t *)malloc(sizeof(udata_fix_t));
	    udf->type = UDF_UDATA;
	    udf->obj = obj;
	    udf->which = slot;
	    udf->list = glb->udi[udata_id].list;
	    udf->id = glb->udi[udata_id].id;
	    udf->next = glb->udf;
	    glb->udf = udf;
	}
    }


    static void set_trav_data(pfNode *node, int which, int udata_id,
			      pfa_global_t *glb)
    {
	if (glb->rl_list[L_UDATA][udata_id])
	    pfNodeTravData(node, which, glb->rl_list[L_UDATA][udata_id]);
	else
	{
	    udata_fix_t *udf;

	    udf = (udata_fix_t *)malloc(sizeof(udata_fix_t));
	    udf->type = UDF_TRAV_DATA;
	    udf->which = which;
	    udf->obj = (pfObject*)node;
	    udf->list = glb->udi[udata_id].list;
	    udf->id = glb->udi[udata_id].id;
	    udf->next = glb->udf;
	    glb->udf = udf;
	}
    }


    static void set_raster_func(pfLPointState *lpstate, void *func, int udata_id,
				pfa_global_t *glb)
    {
	if (glb->rl_list[L_UDATA][udata_id])
	    pfRasterFunc(lpstate, func, glb->rl_list[L_UDATA][udata_id]);
	else
	{
	    udata_fix_t *udf;

	    udf = (udata_fix_t *)malloc(sizeof(udata_fix_t));
	    udf->type = UDF_RASTER_FUNC;
	    udf->func = func;
	    udf->obj = (pfObject*)lpstate;
	    udf->list = glb->udi[udata_id].list;
	    udf->id = glb->udi[udata_id].id;
	    udf->next = glb->udf;
	    glb->udf = udf;
	}
    }


    static void set_callig_func(pfLPointState *lpstate, void *func, int udata_id,
				pfa_global_t *glb)
    {
	if (glb->rl_list[L_UDATA][udata_id])
	    pfCalligFunc(lpstate, func, glb->rl_list[L_UDATA][udata_id]);
	else
	{
	    udata_fix_t *udf;

	    udf = (udata_fix_t *)malloc(sizeof(udata_fix_t));
	    udf->type = UDF_CALLIG_FUNC;
	    udf->func = func;
	    udf->obj = (pfObject*)lpstate;
	    udf->list = glb->udi[udata_id].list;
	    udf->id = glb->udi[udata_id].id;
	    udf->next = glb->udf;
	    glb->udf = udf;
	}
    }


    static void connect_udata(pfa_global_t *glb)
    {
	udata_fix_t *udf;

	while (glb->udf)
	{
	    udf = glb->udf;
	    switch (udf->type)
	    {
		case UDF_UDATA:
		    pfUserDataSlot(udf->obj, udf->which,
				   glb->rl_list[udf->list][udf->id]);
		    break;
		case UDF_PATCH:
		    *udf->patch = glb->rl_list[udf->list][udf->id];
		    break;
		case UDF_TRAV_DATA:
		    pfNodeTravData((pfNode*)udf->obj, udf->which,
				   glb->rl_list[udf->list][udf->id]);
		    break;
		case UDF_RASTER_FUNC:
		    pfRasterFunc((pfLPointState*)udf->obj, udf->func,
				 glb->rl_list[udf->list][udf->id]);
		    break;
		case UDF_CALLIG_FUNC:
		    pfCalligFunc((pfLPointState*)udf->obj, udf->func,
				 glb->rl_list[udf->list][udf->id]);
		    break;
	    }
	    glb->udf = udf->next;
	    free(udf);
	}

	if (glb->udi)
	    free(glb->udi);
    }


PFPFB_DLLEXPORT     size_t pfdLoadData_pfa(void *data, int size, void *handle)
    {
	pfa_global_t *glb;
	unsigned char *uc;
	int i, j;

	glb = (pfa_global_t *)handle;
	uc = data;

	if (size && (glb->data_total % 39) == 0)
	    READ_LINE();

	for (i = 0; i < size; i++)
	{
	    j = ((i + glb->data_total) % 39) * 2;

	    if (glb->line_buf[j] >= '0' && glb->line_buf[j] <= '9')
		uc[i] = (glb->line_buf[j] - '0') << 4;
	    else
		uc[i] = (glb->line_buf[j] - 'a' + 10) << 4;

	    if (glb->line_buf[j+1] >= '0' && glb->line_buf[j+1] <= '9')
		uc[i] |= glb->line_buf[j+1] - '0';
	    else
		uc[i] |= glb->line_buf[j+1] - 'a' + 10;

	    if (j == 76 && i < size - 1)
		READ_LINE();
	}
	glb->data_total += size;

	return(size);
    }


PFPFB_DLLEXPORT     size_t pfdLoadData_pfb(void *data, int size, void *handle)
    {
	((pfa_global_t *)handle)->data_total += size;
	return(pfb_fread(data, 1, size, ((pfa_global_t *)handle)->ifp));
    }


PFPFB_DLLEXPORT     int pfdLoadObjectRef_pfa(pfObject **obj, void *handle)
    {
	pfa_global_t *glb;
	int buf[2];
	udata_fix_t *udf;

	glb = (pfa_global_t *)handle;

	pfdLoadData_pfa(buf, sizeof(int) * 2, handle);

	udf = (udata_fix_t *)malloc(sizeof(udata_fix_t));
	udf->type = UDF_PATCH;
	udf->patch = obj;
	udf->list = buf[0];
	udf->id = buf[1];
	udf->next = glb->udf;
	glb->udf = udf;

	return(0);
    }


PFPFB_DLLEXPORT     int pfdLoadObjectRef_pfb(pfObject **obj, void *handle)
    {
	pfa_global_t *glb;
	int buf[2];
	udata_fix_t *udf;

	glb = (pfa_global_t *)handle;

	glb->data_total += sizeof(int) * 2;
	pfb_fread(buf, sizeof(int), 2, glb->ifp);
	udf = (udata_fix_t *)malloc(sizeof(udata_fix_t));
	udf->type = UDF_PATCH;
	udf->patch = obj;
	udf->list = buf[0];
	udf->id = buf[1];
	udf->next = glb->udf;
	glb->udf = udf;

	return(0);
    }


PFPFB_DLLEXPORT     size_t pfdStoreData_pfa(void *data, int size, void *handle)
    {
	pfa_global_t *glb;
	unsigned char *uc;
	int i;

	glb = (pfa_global_t *)handle;
	uc = data;

	for (i = 0; i < size; i++)
	{
	    fprintf(glb->ofp, "%c%c", hex_char[uc[i] >> 4], hex_char[uc[i] & 0xf]);
	    if ((i + glb->data_total) % 39 == 38)
		fprintf(glb->ofp, "\n");
	}
	glb->data_total += size;

	return(size);
    }


PFPFB_DLLEXPORT     size_t pfdStoreData_pfb(void *data, int size, void *handle)
    {
	((pfa_global_t *)handle)->data_total += size;
	return(fwrite(data, 1, size, ((pfa_global_t *)handle)->ofp));
    }


PFPFB_DLLEXPORT     int pfdStoreObjectRef_pfa(pfObject *obj, void *handle)
    {
	pfa_global_t *glb;
	int buf[2];

	glb = (pfa_global_t *)handle;

	if ((buf[1] = find_in_unknown_list(obj, &buf[0], glb)) == -1)
	    return(-1);

	pfdStoreData_pfa(buf, sizeof(int) * 2, handle);

	return(0);
    }


PFPFB_DLLEXPORT     int pfdStoreObjectRef_pfb(pfObject *obj, void *handle)
    {
	pfa_global_t *glb;
	int buf[2];

	glb = (pfa_global_t *)handle;

	if ((buf[1] = find_in_unknown_list(obj, &buf[0], glb)) == -1)
	    return(-1);

	glb->data_total += sizeof(int) * 2;
	fwrite(buf, sizeof(int), 2, glb->ofp);

	return(0);
    }


PFPFB_DLLEXPORT     int pfdDescendObject_pfa(pfObject *obj, void *handle)
    {
	return(pfdDescendObject_pfb(obj, handle));
    }


PFPFB_DLLEXPORT     int pfdDescendObject_pfb(pfObject *obj, void *handle)
    {
	pfType *type;

	if ((type = pfGetType(obj)) == NULL)
	    return(-1);

	return(descend_unknown(obj, type, (pfa_global_t *)handle));
    }


PFPFB_DLLEXPORT     int pfdCrypt_pfb(uint *key,
		     pfdEncryptFuncType_pfb encrypt_func,
		     pfdDecryptFuncType_pfb decrypt_func)
    {
	if (crypt_key_pfb != NULL)
	    free(crypt_key_pfb);
	if (key != NULL)
	{
	    crypt_key_pfb = (uint *)malloc(key[0]*sizeof(int)+sizeof(int));
	    bcopy(key, crypt_key_pfb, key[0]*sizeof(int)+sizeof(int));
	}
	else
	    crypt_key_pfb = NULL;

	if (encrypt_func == NULL)
	    encrypt_func_pfb = pfb_encrypt;
	else
	    encrypt_func_pfb = encrypt_func;

	if (decrypt_func == NULL)
	    decrypt_func_pfb = pfb_decrypt;
	else
	    decrypt_func_pfb = decrypt_func;

	return(0);
    }


PFPFB_DLLEXPORT     int pfdUserDataSlot_pfa(int slot,
			    pfdDescendUserDataFuncType_pfa descend_func,
			    pfdStoreUserDataFuncType_pfa store_func,
			    pfdLoadUserDataFuncType_pfa load_func)
    {
	int num, old_num;

	num = pfGetNumNamedUserDataSlots();

	if (slot >= num)
	{
	    pfNotify(PFNFY_WARN, PFNFY_USAGE,
		     "%s  Slot %d is not a named user data slot.",
		     "pfdUserDataSlot_pfa:",slot);
	    return(-1);
	}

	if (descend_func != NULL && store_func == NULL)
	{
	    pfNotify(PFNFY_WARN, PFNFY_USAGE,
		     "%s  \"%s\" must also be set for \"%s\" to have effect.\n",
		     "pfdUserData_pfa:",
		     "store_func", "descend_func");
	    return(-1);
	}
	else if (store_func != NULL && descend_func == NULL)
	{
	    pfNotify(PFNFY_WARN, PFNFY_USAGE,
		     "%s  \"%s\" must also be set for \"%s\" to have effect.\n",
		     "pfdUserData_pfa:",
		     "descend_func", "store_func");
	    return(-1);
	}

	if (udfunc_pfa == NULL)
	{
	    udfunc_pfa = (udata_func_t*)pfMalloc(sizeof(udata_func_t) * num,
						 pfGetSharedArena());
	    bzero(udfunc_pfa, sizeof(udata_func_t) * num);
	}
	else
	{
	    old_num = (int)(pfGetSize(udfunc_pfa) / sizeof(udata_func_t));
	    if (old_num < num)
		pfRealloc(udfunc_pfa, sizeof(udata_func_t) * num);
	    bzero(&udfunc_pfa[old_num], sizeof(udata_func_t) * (num - old_num));
	}

	udfunc_pfa[slot].descend = descend_func;
	udfunc_pfa[slot].store = store_func;
	udfunc_pfa[slot].load = load_func;

	return(0);
    }


PFPFB_DLLEXPORT     int pfdUserData_pfa(pfdDescendUserDataFuncType_pfa descend_func,
			pfdStoreUserDataFuncType_pfa store_func,
			pfdLoadUserDataFuncType_pfa load_func)
    {
	return pfdUserDataSlot_pfa(0, descend_func, store_func, load_func);
    }


PFPFB_DLLEXPORT     int pfdUserDataSlot_pfb(int slot,
			    pfdDescendUserDataFuncType_pfb descend_func,
			    pfdStoreUserDataFuncType_pfb store_func,
			    pfdLoadUserDataFuncType_pfb load_func)
    {
	int num, old_num;

	num = pfGetNumNamedUserDataSlots();

	if (slot >= num)
	{
	    pfNotify(PFNFY_WARN, PFNFY_USAGE,
		     "%s  Slot %d is not a named user data slot.",
		     "pfdUserDataSlot_pfb:",slot);
	    return(-1);
	}

	if (descend_func != NULL && store_func == NULL)
	{
	    pfNotify(PFNFY_WARN, PFNFY_USAGE,
		     "%s  \"%s\" must also be set for \"%s\" to have effect.\n",
		     "pfdUserData_pfb:",
		     "store_func", "descend_func");
	    return(-1);
	}
	else if (store_func != NULL && descend_func == NULL)
	{
	    pfNotify(PFNFY_WARN, PFNFY_USAGE,
		     "%s  \"%s\" must also be set for \"%s\" to have effect.\n",
		     "pfdUserData_pfb:",
		     "descend_func", "store_func");
	    return(-1);
	}

	if (udfunc_pfb == NULL)
	{
	    udfunc_pfb = (udata_func_t*)pfMalloc(sizeof(udata_func_t) * num,
						 pfGetSharedArena());
	    bzero(udfunc_pfb, sizeof(udata_func_t) * num);
	}
	else
	{
	    old_num = (int)(pfGetSize(udfunc_pfb) / sizeof(udata_func_t));
	    if (old_num < num)
	    {
		pfRealloc(udfunc_pfb, sizeof(udata_func_t) * num);
		bzero(&udfunc_pfb[old_num], sizeof(udata_func_t) * (num - old_num));
	    }
	}

	udfunc_pfb[slot].descend = descend_func;
	udfunc_pfb[slot].store = store_func;
	udfunc_pfb[slot].load = load_func;

	return(0);
    }


PFPFB_DLLEXPORT     int pfdUserData_pfb(pfdDescendUserDataFuncType_pfb descend_func,
			pfdStoreUserDataFuncType_pfb store_func,
			pfdLoadUserDataFuncType_pfb load_func)
    {
	return pfdUserDataSlot_pfb(0, descend_func, store_func, load_func);
    }


    static udata_func_t *copy_udfunc(udata_func_t *udfunc)
    {
	udata_func_t *new_udfunc;
	int num, old_num;

	if (udfunc == NULL)
	    return NULL;

	num = pfGetNumNamedUserDataSlots();
	old_num = (int)(pfGetSize(udfunc_pfb) / sizeof(udata_func_t));

	new_udfunc = (udata_func_t*)malloc(sizeof(udata_func_t) * num);

	bcopy(udfunc, new_udfunc, sizeof(udata_func_t) * old_num);

	if (old_num < num)
	    bzero(&new_udfunc[old_num], sizeof(udata_func_t) * (num - old_num));

	return new_udfunc;
    }


PFPFB_DLLEXPORT     int pfdAddCustomNode_pfa(pfType *type, const char *name,
			     pfdDescendCustomNodeFuncType_pfa descend_func,
			     pfdStoreCustomNodeFuncType_pfa store_func,
			     pfdNewCustomNodeFuncType_pfa new_func,
			     pfdLoadCustomNodeFuncType_pfa load_func)
    {
	int i;

	for (i = 0; i < num_custom_nodes_pfa; i++)
	    if (type == custom_nodes_pfa[i].type)
	    {
		if (name)
		{
		    free(custom_nodes_pfa[i].name);
		    custom_nodes_pfa[i].name = strdup(name);
		}
		custom_nodes_pfa[i].descend_func = descend_func;
		custom_nodes_pfa[i].store_func = store_func;
		custom_nodes_pfa[i].new_func = new_func;
		custom_nodes_pfa[i].load_func = load_func;
		return(0);
	    }

	if (name == NULL)
	{
	    pfNotify(PFNFY_WARN, PFNFY_USAGE,
		     "pfdAddCustomNode_pfa:  Name must not be NULL.");
	    return(-1);
	}

	if (num_custom_nodes_pfa == N_CUSTOM_MAX)
	{
	    pfNotify(PFNFY_WARN, PFNFY_PRINT,
		     "pfdAddCustomNode_pfa:  No more then %d custom nodes allowed.",
		     N_CUSTOM_MAX);
	    return(-1);
	}

	if ((num_custom_nodes_pfa & (num_custom_nodes_pfa - 1)) == 0)
	{							/* if power of 2 */
	    if (num_custom_nodes_pfa == 0)
		i = 1;
	    else
		i = num_custom_nodes_pfa << 1;

	    custom_nodes_pfa = (custom_node_t *)realloc(custom_nodes_pfa,
							i * sizeof(custom_node_t));
	}

	custom_nodes_pfa[num_custom_nodes_pfa].type = type;
	custom_nodes_pfa[num_custom_nodes_pfa].name = strdup(name);
	custom_nodes_pfa[num_custom_nodes_pfa].used = 0;
	custom_nodes_pfa[num_custom_nodes_pfa].descend_func = descend_func;
	custom_nodes_pfa[num_custom_nodes_pfa].store_func = store_func;
	custom_nodes_pfa[num_custom_nodes_pfa].new_func = new_func;
	custom_nodes_pfa[num_custom_nodes_pfa].load_func = load_func;
	num_custom_nodes_pfa++;
	return(0);
    }


PFPFB_DLLEXPORT     int pfdDeleteCustomNode_pfa(pfType *type)
    {
	int i;

	for (i = 0; i < num_custom_nodes_pfa; i++)
	    if (type == custom_nodes_pfa[i].type)
	    {
		num_custom_nodes_pfa--;
		free(custom_nodes_pfa[i].name);
		for (; i < num_custom_nodes_pfa; i++)
		    bcopy(&custom_nodes_pfa[i], &custom_nodes_pfa[i+1],
			  sizeof(custom_node_t));
		if ((num_custom_nodes_pfa &
		     (num_custom_nodes_pfa - 1)) == 0)	/* if power of 2 */
		    custom_nodes_pfa = (custom_node_t *)realloc(custom_nodes_pfa,
							    num_custom_nodes_pfa *
							    sizeof(custom_node_t));
		return(0);
	    }

	pfNotify(PFNFY_WARN, PFNFY_PRINT,
		 "pfdDeleteCustomNode_pfa:  Could not find custom node.");
	return(-1);
    }


PFPFB_DLLEXPORT     int pfdAddCustomNode_pfb(pfType *type, const char *name,
			      pfdDescendCustomNodeFuncType_pfb descend_func,
			      pfdStoreCustomNodeFuncType_pfb store_func,
			      pfdNewCustomNodeFuncType_pfb new_func,
			      pfdLoadCustomNodeFuncType_pfb load_func)
    {
	int i;

	for (i = 0; i < num_custom_nodes_pfb; i++)
	    if (type == custom_nodes_pfb[i].type)
	    {
		if (name)
		{
		    free(custom_nodes_pfb[i].name);
		    custom_nodes_pfb[i].name = strdup(name);
		}
		custom_nodes_pfb[i].descend_func = descend_func;
		custom_nodes_pfb[i].store_func = store_func;
		custom_nodes_pfb[i].new_func = new_func;
		custom_nodes_pfb[i].load_func = load_func;
		return(0);
	    }

	if (name == NULL)
	{
	    pfNotify(PFNFY_WARN, PFNFY_USAGE,
		     "pfdAddCustomNode_pfb:  Name must not be NULL.");
	    return(-1);
	}

	if (num_custom_nodes_pfb == N_CUSTOM_MAX)
	{
	    pfNotify(PFNFY_WARN, PFNFY_PRINT,
		     "pfdAddCustomNode_pfb:  No more then %d custom nodes allowed.",
		     N_CUSTOM_MAX);
	    return(-1);
	}

	if ((num_custom_nodes_pfb & (num_custom_nodes_pfb - 1)) == 0)
	{							/* if power of 2 */
	    if (num_custom_nodes_pfb == 0)
		i = 1;
	    else
		i = num_custom_nodes_pfb << 1;

	    custom_nodes_pfb = (custom_node_t *)realloc(custom_nodes_pfb,
							i * sizeof(custom_node_t));
	}

	custom_nodes_pfb[num_custom_nodes_pfb].type = type;
	custom_nodes_pfb[num_custom_nodes_pfb].name = strdup(name);
	custom_nodes_pfb[num_custom_nodes_pfb].used = 0;
	custom_nodes_pfb[num_custom_nodes_pfb].descend_func = descend_func;
	custom_nodes_pfb[num_custom_nodes_pfb].store_func = store_func;
	custom_nodes_pfb[num_custom_nodes_pfb].new_func = new_func;
	custom_nodes_pfb[num_custom_nodes_pfb].load_func = load_func;
	num_custom_nodes_pfb++;
	return(0);
    }


PFPFB_DLLEXPORT     int pfdDeleteCustomNode_pfb(pfType *type)
    {
	int i;

	for (i = 0; i < num_custom_nodes_pfb; i++)
	    if (type == custom_nodes_pfb[i].type)
	    {
		num_custom_nodes_pfb--;
		free(custom_nodes_pfb[i].name);
		for (; i < num_custom_nodes_pfb; i++)
		    bcopy(&custom_nodes_pfb[i], &custom_nodes_pfb[i+1],
			  sizeof(custom_node_t));
		if ((num_custom_nodes_pfb &
		     (num_custom_nodes_pfb - 1)) == 0)	/* if power of 2 */
		    custom_nodes_pfb = (custom_node_t *)realloc(custom_nodes_pfb,
							    num_custom_nodes_pfb *
							    sizeof(custom_node_t));
		return(0);
	    }

	pfNotify(PFNFY_WARN, PFNFY_PRINT,
		 "pfdDeleteCustomNode_pfb:  Could not find custom node.");
	return(-1);
    }


    static int is_custom_node(pfNode *node, pfa_global_t *glb)
    {
	pfType *type;
	int i;

	type = pfGetType(node);

	for (i = 0; i < glb->num_custom_nodes; i++)
	    if (type == glb->custom_nodes[i].type)
		return(i);

	return(-1);
    }


    static int is_pfi_tex(pfTexture *tex)
    {
	int is_pfi;
	pfList *tlist;
	const char *name;
	char *s;
	pfTexture *child_tex;

	if ((name = pfGetTexName(tex)) != NULL &&
	    (s = strrchr(name, '.')) != NULL &&
	    strcmp(s, ".pfi") == 0)
	    is_pfi = IS_PFI;
	else
	    is_pfi = 0;

	if (is_pfi &&
	    (tlist = pfGetTexList(tex)) &&
	    pfGetNum(tlist) &&
	    (child_tex = (pfTexture*)pfGet(tlist, 0)) &&
	    strncmp(name, pfGetTexName(child_tex), strlen(name)) == 0)
	    is_pfi |= IS_PFI_MULTI;

	return is_pfi;
    }


    PFPFB_DLLEXPORT int
    pfdCleanShare_pfb(void)
    {
	tex_list_t *last, *cur, *next;

	cur = tex_list;
	last = NULL;

	while (cur)
	{
	    next = cur->next;
	    /*
	     *  Note < 3 because one ref by pfb's texture list and one ref by
	     *  the pfdShare list.
	     */
	    if (pfGetRef(cur->tex) < 3)
	    {
		pfUnref(cur->tex);
		if (last)
		    last->next = next;
		else
		    tex_list = next;
		free_tex_list_t(cur);
		cur = next;
	    }
	    else
	    {
		last = cur;
		cur = next;
	    }
	}

	return pfdCleanShare(pfdGetGlobalShare());
    }


    int pfdCleanShare_pfa(void)
    {
	return pfdCleanShare_pfb();
    }


PFPFB_DLLEXPORT     void pfdLoadNeededDSOs_pfa(const char *fileName)
    {
	int i;
	char path[PF_MAXSTRING];
	char buf[512];
	int list_type, list_size;
	char *s;
	FILE *ifp;

	/*
	 *  find the input file
	 */
	if (!pfFindFile(fileName, path, R_OK))
	{
	    /*
	     *  Do not give any type of failure message.  This will often
	     *  get called by pfdInitConverter() with names of non files,
	     *  or files that do not exist yet.
	     */
	    return;
	}

	/*
	 *  open the input file for reading
	 */
	if ((ifp = fopen(path, "r")) == NULL)
	{
	    pfNotify(PFNFY_WARN, PFNFY_RESOURCE,
		     "pfdLoadNeededDSOs_pfa:  Could not open \"%s\" for reading.\n",
		     path);
	    return;
	}

	/*
	 *  read header
	 */
	do
	    fgets(buf, PF_MAXSTRING, ifp);
	while (buf[0] == '#');
	if(strtoul(buf, &s, 16) == PFB_MAGIC_NUMBER)
	{		
		    int stuff = atoi(s);
		    if(r_swap)
		    {		
			    /* swap stuff */
			    P_32_SWAP(&stuff);
		    }

		    if (stuff > PFB_VERSION_NUMBER)
		    {
		pfNotify(PFNFY_WARN, PFNFY_PRINT,
			 "%s:  \"%s\" has a newer pfa version number.\n",
			 "pfdLoadNeededDSOs_pfa", path);
		pfNotify(PFNFY_WARN, PFNFY_PRINT,
			     "It may not load correctly.\n");
		    }
	}
	else if(strtoul(buf, &s, 16) == PFB_MAGIC_NUMBER_LE)
	{
		    int stuff = atoi(s);
		    if(r_swap)
		    {		
			    P_32_SWAP(&stuff);
		    }

		    if (stuff > PFB_VERSION_NUMBER)
		    {
		pfNotify(PFNFY_WARN, PFNFY_PRINT,
			 "%s:  \"%s\" has a newer pfa version number.\n",
			 "pfdLoadNeededDSOs_pfa", path);
		pfNotify(PFNFY_WARN, PFNFY_PRINT,
			     "It may not load correctly.\n");
		    }

	}
	    else
		    return;

	/*
	 *  Read the user function list, if it exists.
	 */
	do
	    fgets(buf, PF_MAXSTRING, ifp);
	while (buf[0] == '#');
	sscanf(buf, "%d %d", &list_type, &list_size);
	if (list_type == L_UFUNC)
	{
	    pfNotify(PFNFY_NOTICE, PFNFY_PRINT,
		    "pfdLoadNeededDSOs_pfa: Loading DSOs for \"%s\"", path);
	    for (i = 0; i < list_size; i++)
		pfa_load_ufunc(ifp);
	}

	/*
	 *  close the file
	 */
	fclose(ifp);
    }


PFPFB_DLLEXPORT     void pfdLoadNeededDSOs_pfb(const char *fileName)
{
    int i;
    char path[PF_MAXSTRING];
    int buf[4];
    FILE *ifp;
    
    pfbFileReadFunction.fread = fread;
    pfbFileReadFunction.fseek = fseek;
    pfbFileReadFunction.ftell = ftell;    

    /*
     *  find the input file
     */
    if (!pfFindFile(fileName, path, R_OK))
    {
	/*
	 *  Do not give any type of failure message.  This will often
	 *  get called by pfdInitConverter() with names of non files,
	 *  or files that do not exist yet.
	 */
	return;
    }

    /*
     *  open the input file for reading
     */
    if ((ifp = fopen(path, "rb")) == NULL)
    {
	pfNotify(PFNFY_WARN, PFNFY_RESOURCE,
		 "pfdLoadNeededDSOs_pfb:  Could not open \"%s\" for reading.\n",
		 path);
	return;
    }

    /*
     *  read header
     */
    pfb_fread(buf, sizeof(int), 4, ifp);
    if((buf[0] != PFB_MAGIC_NUMBER) && (buf[0] != PFB_MAGIC_ENCODED) &&
       (buf[0] != PFB_MAGIC_NUMBER_LE) && (buf[0] != PFB_MAGIC_ENCODED_LE))
    {
	pfNotify(PFNFY_WARN, PFNFY_PRINT,
		 "pfdLoadNeededDSOs_pfb:  buf[0] = %x\n", buf[0]);
	pfNotify(PFNFY_WARN, PFNFY_PRINT,
		 "pfdLoadNeededDSOs_pfb:  \"%s\" Is not a .pfb file.\n",
		 path);
	return;
    }

    fseek(ifp, buf[3], SEEK_SET);

    /*
     *  Read the user function list, if it exists.
     */
    if(pfb_fread(buf, sizeof(int), 3, ifp) == 3 && buf[0] == L_UFUNC)
    {
	pfNotify(PFNFY_NOTICE, PFNFY_PRINT,
		"pfdLoadNeededDSOs_pfb: Loading DSOs for \"%s\"", path);
	for (i = 0; i < buf[1]; i++)
	    pfb_load_ufunc(ifp);
    }

    /*
     *  close the file
     */
    fclose(ifp);
}

static void descend_appearance(islAppearance *a, pfa_global_t *glb) {
  if( find_in_list(L_APPEARANCE, a, glb) != -1) {
    return;
  }
  
  add_to_slist(L_APPEARANCE, a, sizeof(a), glb);
}

/* ARGSUSED */
static void pfa_write_appearance(void *va, size_t size, pfa_global_t *glb) {
  islAppearance *a = (islAppearance *)va;
  pfa_write_name(pfdGetAppearanceFilename(a), glb);
}

/* ARGSUSED */
static void pfb_write_appearance(void *va, size_t size, pfa_global_t *glb) {
  islAppearance *a = (islAppearance *)va;
  pfb_write_name(pfdGetAppearanceFilename(a), glb);
}

static void *pfa_read_appearance(pfa_global_t *glb) {
  islAppearance *appearance;
  int i;
  READ_LINE();
  if (strncmp(glb->line_buf, "NULL", 4) != 0)
    {
      if (glb->line_buf[0] == '"')
	{
	  for (i = 1; glb->line_buf[i] != '"' && glb->line_buf[i] != '\0';
	       i++);
	  glb->line_buf[i] = '\0';
	}
      else
	READ_ERROR();
    }
  else
    return NULL;
  
  appearance = pfdLoadAppearance(&glb->line_buf[1]);
  return appearance;
}

static void *pfb_read_appearance(pfa_global_t *glb) {
  int len;

  pfb_fread(&len, sizeof(len), 1, glb->ifp);
  if(len == -1)
    return NULL;
  else {
    islAppearance *app;
    char *name = (char *)malloc(len+1);
    pfb_fread(name, 1, len, glb->ifp);
    name[len] = '\0';

    app = pfdLoadAppearance(name);
    free(name);
    return app;
  }
}


