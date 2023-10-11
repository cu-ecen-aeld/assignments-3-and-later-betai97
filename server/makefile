# File: makefile
# Author: Ben Tait

CC      = $(CROSS_COMPILE)gcc
CFLAGS  = -g3 -Wall -Werror
RM		= rm -f

all: aesdsocket

default: aesdsocket

aesdsocket: aesdsocket.c
	$(CC) $(CFLAGS) -o aesdsocket aesdsocket.c

clean:
	$(RM) aesdsocket
	$(RM) *.o