ifeq (,$(wildcard ../config.mak))
$(error "../config.mak is not present, run configure !")
endif
include ../config.mak

ifeq ($(BUILD_STATIC),yes)
  BUILD_RULES += lib_static
endif
ifeq ($(BUILD_SHARED),yes)
  BUILD_RULES += lib_shared
endif

LIBNAME = libplayer
STATIC_LIBNAME = ${LIBNAME}.a
SHARED_LIBNAME = ${LIBNAME}.so
SHARED_LIBNAME_VERSION = ${SHARED_LIBNAME}.${LIBPLAYER_VERSION}
SHARED_LIBNAME_MAJOR = $(SHARED_LIBNAME).$(shell echo $(LIBPLAYER_VERSION) | cut -f1 -d.)

SRCS = \
	player.c \
	player_internal.c \
	mrl.c \
	mrl_internal.c \
	playlist.c \
	logs.c \
	fifo_queue.c \
	fs_utils.c \
	parse_utils.c \
	event.c \
	event_handler.c \
	supervisor.c \
	wrapper_dummy.c

EXTRADIST = \
	event.h \
	event_handler.h \
	fifo_queue.h \
	fs_utils.h \
	logs.h \
	parse_utils.h \
	player.h \
	player_internals.h \
	playlist.h \
	supervisor.h \
	wrapper_dummy.h \
	wrapper_gstreamer.h \
	wrapper_mplayer.h \
	wrapper_vlc.h \
	wrapper_xine.h \
	x11_common.h \

SRCS-$(X11)			+= x11_common.c
SRCS-$(WRAPPER_MPLAYER)		+= wrapper_mplayer.c
SRCS-$(WRAPPER_VLC)		+= wrapper_vlc.c
SRCS-$(WRAPPER_XINE)		+= wrapper_xine.c
SRCS-$(WRAPPER_GSTREAMER)	+= wrapper_gstreamer.c

SRCS += $(SRCS-yes)

OBJS = $(SRCS:.c=.o)

.SUFFIXES: .c .o

all: depend $(BUILD_RULES)

.c.o:
	$(CC) -c $(CFLAGS) $(EXTRACFLAGS) $(OPTFLAGS) -o $@ $<

lib_static: $(OBJS)
	$(AR) r $(STATIC_LIBNAME) $(OBJS)
	$(RANLIB) $(STATIC_LIBNAME)

lib_shared: $(OBJS)
	$(CC) -shared -Wl,-soname,$(SHARED_LIBNAME_MAJOR) \
	  $(OBJS) $(LDFLAGS) $(EXTRALIBS) -o $(SHARED_LIBNAME_VERSION)
	$(LN) -sf $(SHARED_LIBNAME_VERSION) $(SHARED_LIBNAME_MAJOR)
	$(LN) -sf $(SHARED_LIBNAME_MAJOR) $(SHARED_LIBNAME)

clean:
	rm -f *.o *.a *.so*
	rm -f .depend

install: $(BUILD_RULES)
	$(INSTALL) -d $(libdir)
	[ $(BUILD_STATIC) = yes ] && $(INSTALL) -c $(STATIC_LIBNAME) $(libdir); \
	if [ $(BUILD_SHARED) = yes ]; then \
	  $(INSTALL) -c $(SHARED_LIBNAME_VERSION) $(libdir); \
	  $(LN) -sf $(SHARED_LIBNAME_VERSION) $(libdir)/$(SHARED_LIBNAME_MAJOR); \
	  $(LN) -sf $(SHARED_LIBNAME_MAJOR) $(libdir)/$(SHARED_LIBNAME); \
	fi
	$(INSTALL) -d $(includedir)
	$(INSTALL) -c -m 644 player.h $(includedir)

uninstall:
	rm -f $(libdir)/$(STATIC_LIBNAME)
	rm -f $(libdir)/$(SHARED_LIBNAME)*
	rm -f $(includedir)/player.h

depend:
	$(CC) -MM $(CFLAGS) $(EXTRACFLAGS) $(SRCS) 1>.depend

.PHONY: clean depend
.PHONY: uninstall

dist-all:
	cp $(EXTRADIST) $(SRCS) Makefile $(DIST)

.PHONY: dist-all

#
# include dependency files if they exist
#
ifneq ($(wildcard .depend),)
include .depend
endif
