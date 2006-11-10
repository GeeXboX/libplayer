include config.mak

LIBTEST = libtest
SRCS = libtest.c

CFLAGS += -Isrc
LDFLAGS += -Lsrc -lplayer -lpthread

all: lib test

lib:
	$(MAKE) -C src

test:
	$(CC) $(SRCS) $(OPTFLAGS) $(CFLAGS) $(LDFLAGS) -o $(LIBTEST)

clean:
	$(MAKE) -C src clean
	rm -f $(LIBTEST)

distclean: clean
	rm -f config.log
	rm -f config.mak

install:
	$(MAKE) -C src install

.phony: clean distclean
