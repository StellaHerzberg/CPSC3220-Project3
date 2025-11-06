#include <string.h>

// Include the allocator implementation
#include "allocator.c"


void* malloc(size_t size) {
    if (size == 0) {
        return NULL;
    }
    
    int class_index = size_to_class(size);
    
    // Large allocation (>1024 bytes)
    if (class_index == -1) {
        return allocate_large(size);
    }
    
    // Small allocation (≤1024 bytes)
    return allocate_small(size);
}

void free(void *ptr) {
    if (ptr == NULL) {
        return;
    }
    
    // Try to free as large block first
    if (free_large(ptr)) {
        return;
    }
    
    // Otherwise free as small block
    free_small(ptr);
}

void* calloc(size_t nmemb, size_t size) {
    if (nmemb == 0 || size == 0) {
        return NULL;
    }
    
    // Check for overflow
    size_t total_size = nmemb * size;
    if (total_size / nmemb != size) {
        return NULL;
    }
    
    void *ptr = malloc(total_size);
    if (ptr) {
        memset(ptr, 0, total_size);
    }
    
    return ptr;
}

void* realloc(void *ptr, size_t size) {
    // If ptr is NULL, equivalent to malloc
    if (ptr == NULL) {
        return malloc(size);
    }
    
    // If size is 0, equivalent to free
    if (size == 0) {
        free(ptr);
        return NULL;
    }
    
    // Get old size
    size_t old_size = get_allocation_size(ptr);
    if (old_size == 0) {
        // Invalid pointer, just try to allocate new memory
        return malloc(size);
    }
    
    // Allocate new block
    void *new_ptr = malloc(size);
    if (new_ptr == NULL) {
        return NULL;
    }
    
    // Copy data (minimum of old and new size)
    size_t copy_size = (old_size < size) ? old_size : size;
    memcpy(new_ptr, ptr, copy_size);
    
    // Free old block
    free(ptr);
    
    return new_ptr;
}