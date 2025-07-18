TOPDIR		=	../
VDRDIR		=	$(TOPDIR)../../..

LIST		=	xlist vob2vdr stripps cutter genindex
OBJS		=	xlist.o vob2vdr.o stripps.o cutter.o
CXXARCH		?=	$(shell make -sf $(TOPDIR)Make.arch|grep -v 'make') -funroll-loops
CXX		?=	g++
CXXFLAGS	?=	-O2 $(CXXARCH) -Wall -Woverloaded-virtual -g
CFLAGS		?=	-O2 $(CXXARCH) -Wall -g
CC		?=	gcc
DEFINES		+=	-D_GNU_SOURCE
INCLUDES	+=	-I$(VDRDIR)/include
INCLUDES	+=	-I$(TOPDIR)

all: $(LIST)

%.o: %.c
	$(CXX) $(CXXFLAGS) -fPIC -DPIC -c $(DEFINES) $(INCLUDES) $<

MAKEDEP = g++ -MM -MG
DEPFILE = .dependencies
$(DEPFILE): Makefile
	@$(MAKEDEP) $(DEFINES) $(INCLUDES) $(OBJS:%.o=%.c) > $@

-include $(DEPFILE)

xlist: xlist.o
	$(CXX) $(CXXFLAGS) -fPIC -DPIC $(DEFINES) $(INCLUDES) -o $@ $<

vob2vdr: vob2vdr.o
	$(CXX) $(CXXFLAGS) -fPIC -DPIC $(DEFINES) $(INCLUDES) -o $@ $<

stripps: stripps.o
	$(CXX) $(CXXFLAGS) -fPIC -DPIC $(DEFINES) $(INCLUDES) -o $@ $<

cutter: cutter.o handle.o
	$(CXX) $(CXXFLAGS) -fPIC -DPIC $(DEFINES) $(INCLUDES) -o $@ $^

genindex: genindex.c
	$(CC) $(CFLAGS) -fPIC -DPIC $(DEFINES) $(INCLUDES) -o $@ $^

clean:
	@-rm -f $(OBJS) $(LIST) $(DEPFILE) *.o *.so *.tar.bz2 core* *~ testt

handle.o:: handle.h
