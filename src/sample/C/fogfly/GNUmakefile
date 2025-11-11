#-------------------------------------------------------------------------#
#-- Makefile for "fogfly" the basic demonstration program		--#
#-------------------------------------------------------------------------#
#-- RCS version information						--#
#--   $Revision: 1.2 $							--#
#--   $Date: 2001/10/10 00:48:04 $					--#
#-------------------------------------------------------------------------#

ifndef PFROOT
PFROOT = $(ROOT)
endif

include $(PFROOT)/usr/share/Performer/src/pfmakedefs.gmk

#-- provide a list of alternate locations for file searches
UNIQUE	= ..
COMMON	= ../../common

ifndef PFROOT
PFROOT = $(ROOT)
endif


#-- alternate locatins for included files
LCINCS += \
	-I${UNIQUE} \
	-I${COMMON}


#-- base name of program
TARGET	= fogfly

#-- files from which the target is built {some are in the common directory}
CFILES = \
	cmdline.c \
	configfile.c \
	generic.c \
	env.c \
	gui.c \
	keybd.c \
	perfly.c \
        fog.c \
	main.c


include $(PFROOT)/usr/share/Performer/src/pfmakerules.gmk

#-- objects are built from either unique or common files
VPATH=${UNIQUE}:${COMMON}
