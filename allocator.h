#ifndef ALLOCATOR.H
#define ALLOCATOR.H

#include <sys/mman.h>
#include <unistd.h>
#include <string.h>
#include <stdint.h>

static size_t roundPageSize(size_t size);


void* malloc(size_t size);
void free(void *ptr);
void* calloc(size_t nmemb, size_t size);
void* realloc(void *ptr, size_t size);

#endif