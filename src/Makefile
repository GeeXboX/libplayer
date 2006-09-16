include ../config.mak

LIBNAME = libplayer.so

SRCS =  player.c \
	mrl.c \
	logs.c \
	wrapper_dummy.c

LDFLAGS += -fPIC

ifeq ($(WRAPPER_XINE),yes)
  SRCS += wrapper_xine.c
  CFLAGS += -DHAVE_XINE
  LDFLAGS += -lxine -lpthread
endif

ifeq ($(DEBUG),yes)
  CFLAGS += -DHAVE_DEBUG
endif

OBJS = $(SRCS:.c=.o)

.SUFFIXES: .c .o

all: depend $(LIBNAME)

.c.o:
	$(CC) -c $(CFLAGS) -o $@ $<

$(LIBNAME): $(OBJS)
	$(CC) -shared $(OBJS) $(LDFLAGS) -o $(LIBNAME)

clean:
	rm -f *.o *.a *.so
	rm -f .depend

install: $(LIBNAME)
	$(INSTALL) -c $(LIBNAME) $(PREFIX)/lib
	$(INSTALL) -c -m 644 player.h $(PREFIX)/include

depend:
	$(CC) -MM $(CFLAGS) $(SRCS) 1>.depend

.phony: clean depend

#
# include dependency files if they exist
#
ifneq ($(wildcard .depend),)
include .depend
endif
