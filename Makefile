# Makefile to compile and clean the program

CC = clang
CFLAGS = -Wall -g -fPIC -shared -ldl

all: libmyalloc.so

libmyalloc.so: allocator.c
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -f libmyalloc.so *.o
