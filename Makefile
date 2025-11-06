# Makefile to compile and clean the program

CC = clang
CFLAGS = -Wall -g -fPIC

all: libmyalloc.so

libmyalloc.so: allocator.c
	$(CC) $(CFLAGS) -shared -o $@ $^

clean:
	rm -f libmyalloc.so *.o
