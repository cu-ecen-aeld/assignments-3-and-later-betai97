# File: makefile
# Author: Ben Tait

CC      ?= $(CROSS_COMPILE)gcc
CFLAGS  ?= -g3 -Wall -Werror
RM		?= rm -f
LDFLAGS ?= -pthread  -lrt
TARGET ?= aesdsocket

all: aesdsocket

default: aesdsocket

aesdsocket: aesdsocket.c
	$(CC) $(CFLAGS) -o aesdsocket aesdsocket.c $(LDFLAGS)

clean:
	$(RM) aesdsocket
	$(RM) *.o