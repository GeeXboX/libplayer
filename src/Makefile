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

SRCS =  player.c \
	mrl.c \
	playlist.c \
	logs.c \
	fifo_queue.c \
	wrapper_dummy.c

ifeq ($(WRAPPER_MPLAYER),yes)
  SRCS += wrapper_mplayer.c
endif

ifeq ($(WRAPPER_VLC),yes)
  SRCS += wrapper_vlc.c
endif

ifeq ($(WRAPPER_XINE),yes)
  SRCS += wrapper_xine.c
endif

ifeq ($(WRAPPER_GSTREAMER),yes)
  SRCS += wrapper_gstreamer.c
endif

ifeq ($(X11),yes)
  SRCS += x11_common.c
endif

OBJS = $(SRCS:.c=.o)

.SUFFIXES: .c .o

all: depend $(BUILD_RULES)

.c.o:
	$(CC) -c $(CFLAGS) $(OPTFLAGS) -o $@ $<

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
	[ $(BUILD_STATIC) = yes ] && $(INSTALL) -c $(STATIC_LIBNAME) $(libdir); \
	if [ $(BUILD_SHARED) = yes ]; then \
	  $(INSTALL) -c $(SHARED_LIBNAME_VERSION) $(libdir); \
	  $(LN) -sf $(SHARED_LIBNAME_VERSION) $(libdir)/$(SHARED_LIBNAME_MAJOR); \
	  $(LN) -sf $(SHARED_LIBNAME_MAJOR) $(libdir)/$(SHARED_LIBNAME); \
	fi
	$(INSTALL) -c -m 644 player.h $(includedir)

uninstall:
	rm -f $(libdir)/$(STATIC_LIBNAME)
	rm -f $(libdir)/$(SHARED_LIBNAME)*
	rm -f $(includedir)/player.h

depend:
	$(CC) -MM $(CFLAGS) $(SRCS) 1>.depend

.PHONY: clean depend
.PHONY: uninstall

#
# include dependency files if they exist
#
ifneq ($(wildcard .depend),)
include .depend
endif
