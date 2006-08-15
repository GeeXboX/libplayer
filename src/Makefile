include ../config.mak

LIBNAME = libplayer.a

SRCS =  player.c \
	mrl.c \
	logs.c \
	wrapper_dummy.c

ifeq ($(WRAPPER_XINE),yes)
  SRCS += wrapper_xine.c
  CFLAGS += -DHAVE_XINE
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
	$(AR) r $(LIBNAME) $(OBJS)
	$(RANLIB) $(LIBNAME)

clean:
	rm -f *.o *.a
	rm -f .depend

depend:
	$(CC) -MM $(CFLAGS) $(SRCS) 1>.depend

.phony: clean depend

#
# include dependency files if they exist
#
ifneq ($(wildcard .depend),)
include .depend
endif
