# File: makefile
# Author: Ben Tait

CC      = $(CROSS_COMPILE)gcc
CFLAGS  = -g
RM		= rm -f

default: writer

writer: writer.c
	$(CC) $(CFLAGS) -o writer writer.c

clean:
	$(RM) writer
	$(RM) *.o