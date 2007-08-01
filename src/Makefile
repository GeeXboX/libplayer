include ../config.mak

LIBNAME = libplayer.so

SRCS =  player.c \
	mrl.c \
	logs.c \
	x11_common.c \
	wrapper_dummy.c

CFLAGS += -fPIC

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

all: depend $(LIBNAME)

.c.o:
	$(CC) -c $(CFLAGS) $(OPTFLAGS) -o $@ $<

$(LIBNAME): $(OBJS)
	$(CC) -shared $(OBJS) $(LDFLAGS) $(EXTRALIBS) -o $(LIBNAME)

clean:
	rm -f *.o *.a *.so
	rm -f .depend

install: $(LIBNAME)
	$(INSTALL) -c $(LIBNAME) $(libdir)
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
