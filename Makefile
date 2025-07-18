#
# Makefile for the Video Disk Recorder plugin bitstreamout
#

#
# The bitstreamout stream plugin.
#
PLUGIN = bitstreamout
ALSAIN =

### The version number of this plugin (taken from the main source file):

VERSION = 0.90

### The C++ compiler and options:

CXX      ?= g++
CXXARCH  ?= $(shell set +evx; make -sf Make.arch CC=$(CXX) INCLUDES="$(INCLUDES) $(ALSAIN)")
CXXFLAGS ?= -fPIC -g -O2 $(CXXARCH) -Wall -Woverloaded-virtual -pthread

### The directory environment:

DVBDIR   = ../../../../DVB
VDRDIR   = ../../..
LIBDIR   = $(VDRDIR)/PLUGINS/lib
TMPDIR   = /tmp
VIDEODIR = /video
VIDEOLIB = /usr/local/lib/vdr
MANDIR   = /usr/local/share/man

ifeq ($(wildcard $(VDRDIR)/include/),)
  VDRDIR = $(PWD)
endif

### Allow user defined options to overwrite defaults:

-include $(VDRDIR)/Make.config

### The version number of VDR (taken from VDR's "config.h"):

APIVERSION = $(shell sed -rn '/define APIVERSION/s/^.*"(.*)".*$$/\1/p' $(VDRDIR)/config.h)

### The name of the distribution archive:

ARCHIVE = $(PLUGIN)-$(VERSION)
PACKAGE = vdr-$(ARCHIVE)

### Includes and Defines (add further entries here):

cc-include = $(shell $(CC) $(INCLUDES) -include $(1) -S -o /dev/null -xc /dev/null > /dev/null 2>&1 && echo "-D$(2)")
cc-library = $(shell echo 'int main () { return 0; }' |$(CC) -l$(1:lib%=%) -o /dev/null -xc - > /dev/null 2>&1 && echo yes)

INCLUDES += $(EXTRA_INCLUDES)
ifneq ($(wildcard $(VDRDIR)/include/),)
  INCLUDES += -I$(VDRDIR)/include
else
  $(error No VDR in include path or no `make include-dir' executed!)
endif
ifneq ($(wildcard $(DVBDIR)/),)
  INCLUDES += -I$(DVBDIR)/include
endif
ifneq ($(ALSAIN),)
  INCLUDES += -I$(ALSAIN)
endif

DEFINES += -DPIC
DEFINES += -DPLUGIN_NAME_I18N='"$(PLUGIN)"'
DEFINES += -D_GNU_SOURCE
DEFINES += -DVERSION=\"$(VERSION)\"
DEFINES += $(call cc-include,alsa/asoundlib.h,HAS_ASOUNDLIB_H)
DEFINES += $(call cc-include,sys/cdefs.h,HAS_CDEFS_H)
DEFINES += $(call cc-include,mad.h,HAS_MAD_H)

ifeq ($(call cc-library,libasound),yes)
  LIBS += -lasound -lrt
else
  $(error No libasound in linkage path please install alsa and alsa-devel packages)
endif
#ifeq ($(call cc-library,libmad),yes)
#  LIBS += -lmad
#else
#  $(error No libmad in linkage path please install mad and mad-devel packages)
#endif

### The object files (add further files here):

OBJS = $(PLUGIN).o iec60958.o ac3.o dts.o lpcm.o channel.o replay.o spdif.o \
	shm_memory_tool.o mp2.o

### Data files like manual page and sample configuration

MANPAGE = vdr-bitstreamout.5

# Start

all: echo libvdr-$(PLUGIN).so

### Implicit rules:

%.o: %.c
	$(CXX) $(CXXFLAGS) $(DEFINES) $(INCLUDES) -c $<

# Dependencies:

MAKEDEP = g++ -MM -MG
DEPFILE = .dependencies
$(DEPFILE): Makefile
	@$(MAKEDEP) $(CXXFLAGS) $(DEFINES) $(INCLUDES) $(OBJS:%.o=%.c) > $@

-include $(DEPFILE)

### Targets:
echo:
	@echo $(PLUGIN) Version $(VERSION)

libvdr-$(PLUGIN).so: $(OBJS)
	$(CXX) $(CXXFLAGS) -shared $(OBJS) -Wl,-soname -Wl,$@.$(APIVERSION) -o $@ -lasound -lrt -lmad
	@cp -p --remove-destination $@ $(LIBDIR)/$@.$(APIVERSION)
	@-strip --strip-unneeded       $(LIBDIR)/$@.$(APIVERSION)

install:  $(LIBDIR)/libvdr-$(PLUGIN).so.$(APIVERSION)
	install -d -m 0755 $(DESTDIR)$(VIDEOLIB)/PLUGINS/lib
	install -m 0755 $< $(DESTDIR)$(VIDEOLIB)/PLUGINS/lib/
	install -d -m 0755 $(DESTDIR)$(MANDIR)/man5
	sed 's|@@VIDEODIR@@|$(VIDEODIR)|' < $(MANPAGE) > $(DESTDIR)$(MANDIR)/man5/$(MANPAGE)

dest: dist
dist: clean
	@-rm -rf $(TMPDIR)/$(ARCHIVE)
	@mkdir $(TMPDIR)/$(ARCHIVE)
	@cp -a * $(TMPDIR)/$(ARCHIVE)
	@-rm -rf $(TMPDIR)/$(PLUGIN)
	@ln -sf $(ARCHIVE) $(TMPDIR)/$(PLUGIN)
	@tar cjf $(PACKAGE).tar.bz2 -C $(TMPDIR) $(PLUGIN) $(ARCHIVE)
	@-rm -rf $(TMPDIR)/$(ARCHIVE) $(TMPDIR)/$(PLUGIN)
	@echo Distribution package created as $(PACKAGE).tar.bz2

clean:
	make -C tools clean
	@-rm -f $(OBJS) $(DEPFILE) *.so *.tar.bz2 core* *~ testt

vdrobj	=	$(shell ls $(VDRDIR)/*.o| grep -v vdr.o)
vdrlib  =	$(wildcard $(VDRDIR)/libsi/*.a $(VDRDIR)/libdtv/*/*.a)
testt:  CXXFLAGS += -DSPDIF_TEST=1 -g3
testt:	$(OBJS) testt.c shm_memory_tool.o spdif.o
	g++ $(CXXFLAGS) $(DEFINES) -o testt testt.c shm_memory_tool.o spdif.o iec60958.o $(vdrobj) \
	-I$(VDRDIR)/include \
	-lasound -ljpeg \
	$(vdrlib) -lrt
