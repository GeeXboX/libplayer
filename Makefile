include config.mak

LIBTEST = libtest
SRCS = libtest.c

CFLAGS += -Isrc
LDFLAGS += -Lsrc -lplayer

ifeq ($(WRAPPER_XINE),yes)
  LDFLAGS += -lxine -lpthread
endif

all: lib test

lib:
	$(MAKE) -C src

test:
	$(CC) $(SRCS) $(CFLAGS) $(LDFLAGS) -o $(LIBTEST)

clean:
	$(MAKE) -C src clean
	rm $(LIBTEST)

.phony: clean
