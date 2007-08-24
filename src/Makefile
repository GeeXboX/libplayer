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

SRCS =  player.c \
	mrl.c \
	logs.c \
	x11_common.c \
	wrapper_dummy.c

ifeq ($(WRAPPER_XINE),yes)
  SRCS += wrapper_xine.c
endif

ifeq ($(WRAPPER_VLC),yes)
  SRCS += wrapper_vlc.c
endif

ifeq ($(WRAPPER_MPLAYER),yes)
  SRCS += wrapper_mplayer.c
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
	$(CC) -shared -Wl,-soname,$(SHARED_LIBNAME) \
	  $(OBJS) $(LDFLAGS) $(EXTRALIBS) -o $(SHARED_LIBNAME)

clean:
	rm -f *.o *.a *.so
	rm -f .depend

install: $(BUILD_RULES)
	[ $(BUILD_STATIC) = yes ] && $(INSTALL) -c $(STATIC_LIBNAME) $(libdir)
	[ $(BUILD_STATIC) = yes ] && $(INSTALL) -c $(SHARED_LIBNAME) $(libdir)
	$(INSTALL) -c -m 644 player.h $(includedir)

depend:
	$(CC) -MM $(CFLAGS) $(SRCS) 1>.depend

.phony: clean depend

#
# include dependency files if they exist
#
ifneq ($(wildcard .depend),)
include .depend
endif
