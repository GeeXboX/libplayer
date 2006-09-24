CC = gcc
INSTALL = install

PREFIX = /usr

CFLAGS = -Wall -g -D_LARGEFILE_SOURCE -D_FILE_OFFSET_BITS=64 -D_REENTRANT
LDFLAGS = 

# Enable debug messages
DEBUG = yes

# Built-in wrappers
WRAPPER_XINE = yes
