#-------------------------------------------------------------------#
#-- Makefile for Performer/src/conv directory                     --#
#-------------------------------------------------------------------#
#-- RCS version information                                       --#
#--   $Revision: 1.3 $						  --#
#--   $Date: 2001/10/10 00:48:04 $				  --#
#-------------------------------------------------------------------#

ifndef PFROOT
PFROOT = $(ROOT)
endif

include $(PFROOT)/usr/share/Performer/src/pfmakedefs.gmk


#-- targets are the executables
TARGETS	= \
	pfconv \
	pficonv \
	pfpalettize \
	makeIBRimages

OBJECTS = $(TARGETS:=.o)


include $(PFROOT)/usr/share/Performer/src/pfmakerules.gmk
