#!smake
#
# Makefile for C++ rotorwash
#
# $Revision: 1.1 $
# $Date: 2001/10/11 13:53:38 $
#

ifndef PFROOT
PFROOT = $(ROOT)
endif

PFDEPTH = ../../../..

include $(PFROOT)/usr/share/Performer/src/pfmakedefs.gmk

ifndef PFROOT
PFROOT = $(ROOT)
endif

# override default SRCDIR in pfcommondefs
SRCDIR = $(PFDEPTH)/sample/C/rotorwash

# for regular make

TARGET = framework

UNIQUE = ..

LCINCS += -O -I$(PFDEPTH)/include -I$(PFROOT)/usr/include -I$(UNIQUE) -I$(SRCDIR)

# look in SRCDIR first for C files. If file is not found, look
# in the common code directory
.PATH.C : $(SRCDIR)

INSTCFILES = framework.c sea.c

CFILES = framework.c sea.c

HFILES = sea.h

DEFCMDS=$(TARGETCMDS)
DEFOBJS=$(OBJECTS)

#OBJECTS = $(CFILES:.C=.o)

#default: $(OBJECTS)
#	$(CC) $(OBJECTS) -o $(TARGETCMDS) -L$(PFROOT)/usr/lib -lpf -lpfdu -lpfui -lpfutil -lGL -lGLU -L/usr/X11R6/lib -lX11 -ldl -lm 

include $(PFROOT)/usr/share/Performer/src/pfmakerules.gmk

#-- objects are built from either unique or common files
VPATH=${UNIQUE}:${SRCDIR}


