include config.mak

LIBTEST = libtest
SRCS = libtest.c

CFLAGS += -Isrc
LDFLAGS += -Lsrc -lplayer

all: lib test

lib:
	$(MAKE) -C src

test:
	$(CC) $(SRCS) $(CFLAGS) $(LDFLAGS) -o $(LIBTEST)

clean:
	$(MAKE) -C src clean
	rm -f $(LIBTEST)

install:
	$(MAKE) -C src install

.phony: clean
