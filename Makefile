# Makefile to compile and clean the program

CC = clang
CFLAGS = -Wall -g -fPIC

all: allocator libmyalloc.so

allocator: allocator.c
	$(CC) $(CFLAGS) -o $@ $^

libmyalloc.so: libmyalloc.c
	$(CC) $(CFLAGS) -shared -o $@ $^

clean:
	rm -f allocator libmyalloc.so *.o
