#-------------------------------------------------------------------------#
#-- Makefile for "volfog" the demonstration program			--#
#-------------------------------------------------------------------------#
#-- RCS version information						--#
#--   $Revision: 1.1 $							--#
#--   $Date: 2001/10/11 13:53:38 $					--#
#-------------------------------------------------------------------------#

ifndef PFROOT
PFROOT = $(ROOT)
endif

include $(PFROOT)/usr/share/Performer/src/pfmakedefs.gmk

#-- provide a list of alternate locations for file searches
UNIQUE	= ..

ifndef PFROOT
PFROOT = $(ROOT)
endif


#-- alternate locatins for included files
LCINCS += \
	-I${UNIQUE}


#-- base name of program
TARGET	= volfog

#-- files from which the target is built {some are in the common directory}
CFILES = \
	draw.c \
	smoke.c \
	fog.c


include $(PFROOT)/usr/share/Performer/src/pfmakerules.gmk

#-- objects are built from either unique or common files
VPATH=${UNIQUE}
