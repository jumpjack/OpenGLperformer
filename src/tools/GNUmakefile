#-------------------------------------------------------------------#
#-- Makefile for Performer/src/sample directory                   --#
#-------------------------------------------------------------------#
#-- RCS version information                                       --#
#--   $Revision: 1.8 $                                           --#
#--   $Date: 2005/06/18 00:24:04 $                                --#
#-------------------------------------------------------------------#
#

PFROOT ?= $(ROOT)

include $(PFROOT)/usr/share/Performer/src/pfmakedefs.gmk

CMDS = \
	izoom \
	assemble \
	subimg \
	to5551 \
	to888 \
	toL16 \
	rgb2raw \
	rsets \
	shrink \
	viewtile \
	generatePhongMap

all dso ddso debug: $(CMDS)

assemble: assemble.c
	$(CXX) -o $@ assemble.c -L$(PFROOT)/usr/lib -lpf 

izoom: izoom.c
	$(CXX) -o $@ izoom.c -L$(PFROOT)/usr/lib -lpf 

subimg: subimg.c
	$(CXX) -o $@ subimg.c -L$(PFROOT)/usr/lib -lpf 

to5551: to5551.c
	$(CC) -o $@ to5551.c -L$(PFROOT)/usr/lib -lpf 

to888: to888.c
	$(CC) -o $@ to888.c -L$(PFROOT)/usr/lib -lpf 

to8888: to8888.c
	$(CC) -o $@ to8888.c -L$(PFROOT)/usr/lib -lpf 

toL16: toL16.c
	$(CC) -o $@ toL16.c -L$(PFROOT)/usr/lib -lpf 

rgb2raw: rgb2raw.c
	$(CC) -o $@ rgb2raw.c

rsets: izoom subimg assemble rsets.c
	$(CC) -o $@ rsets.c

shrink: shrink.c
	$(CC) -o $@ shrink.c

viewtile: viewtile.c
	$(CC) -o $@ viewtile.c -lGL -L/usr/X11R6/lib -lX11 -lm

generatePhongMap: generatePhongMap.C
	$(CXX) -o $@ -I$(PFROOT)/usr/include generatePhongMap.C -L$(PFROOT)/usr/lib -lpf -lpfdu -lpfutil -lGL

.c:
	$(CC) -o $@ $< -limage
.C:
	$(CXX) -o $@ $< 

clean:
	rm -f $(CMDS)
