#ifndef ALLOCATOR.H
#define ALLOCATOR.H

#include <sys/mman.h>
#include <unistd.h>
#include <string.h>
#include <stdint.h>

static int size_to_class(size_t size);
static size_t class_to_size(int class_index);
static void* allocate_small(size_t size);
static void* allocate_large(size_t size);
static void free_small(void *ptr);
static int free_large(void *ptr);
static size_t get_allocation_size(void *ptr);

void* malloc(size_t size);
void free(void *ptr);
void* calloc(size_t nmemb, size_t size);
void* realloc(void *ptr, size_t size);

#endif