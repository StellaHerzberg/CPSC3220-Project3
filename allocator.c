#include <sys/mman.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>
#include <stdint.h>

#define PAGE_SIZE 4096
#define MAX_SMALL_SIZE 1024
#define MIN_SIZE 2
#define NUM_SIZE_CLASSES 10  // 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024

// Page header structure (stored at beginning of each page)
typedef struct page_header {
    size_t object_size;           // Size of objects on this page
    size_t num_objects;           // Total objects that fit on page
    size_t free_count;            // Number of free objects
    struct page_header *next;     // Next page with same object size
    void *free_list;              // Free list for this page
} page_header_t;

// Large allocation header
typedef struct large_header {
    size_t size;                  // Total size including header
    uint32_t magic;               // Magic number to identify large allocs
} large_header_t;

#define LARGE_ALLOC_MAGIC 0xDEADBEEF

// Free block structure (overlays freed memory)
typedef struct free_block {
    struct free_block *next;
} free_block_t;

// Global segregated free lists (one for each size class)
static page_header_t *size_class_pages[NUM_SIZE_CLASSES] = {NULL};

// Get size class index for a given size
// Returns index for: 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024
static int get_size_class(size_t size) {
    if (size <= 2) return 0;
    if (size <= 4) return 1;
    if (size <= 8) return 2;
    if (size <= 16) return 3;
    if (size <= 32) return 4;
    if (size <= 64) return 5;
    if (size <= 128) return 6;
    if (size <= 256) return 7;
    if (size <= 512) return 8;
    if (size <= 1024) return 9;
    return -1;  // Large allocation
}

// Get actual object size for a size class
// Returns: 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024
static size_t get_object_size(int size_class) {
    if (size_class == 0) return 2;
    if (size_class == 1) return 4;
    return 1 << (size_class + 1);  // 8, 16, 32, 64, 128, 256, 512, 1024
}

// Initialize a new page for a given size class
static page_header_t *create_page(size_t object_size) {
    void *page = mmap(NULL, PAGE_SIZE, 
                     PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    
    if (page == MAP_FAILED) {
        return NULL;
    }
    
    page_header_t *header = (page_header_t *)page;
    header->object_size = object_size;
    
    // Calculate available space and number of objects
    size_t available = PAGE_SIZE - sizeof(page_header_t);
    header->num_objects = available / object_size;
    header->free_count = header->num_objects;
    header->next = NULL;
    header->free_list = NULL;
    
    // Initialize free list - build linked list of free blocks
    char *obj_start = (char *)page + sizeof(page_header_t);
    free_block_t *prev = NULL;
    
    for (size_t i = 0; i < header->num_objects; i++) {
        free_block_t *block = (free_block_t *)(obj_start + i * object_size);
        block->next = prev;
        prev = block;
    }
    header->free_list = prev;
    
    return header;
}

// Find the page header for a given pointer
static page_header_t *get_page_header(void *ptr) {
    uintptr_t addr = (uintptr_t)ptr;
    uintptr_t page_start = addr & ~(PAGE_SIZE - 1);
    return (page_header_t *)page_start;
}

// Check if pointer is in a small allocation page
static int is_small_allocation(void *ptr) {
    if (ptr == NULL) return 0;
    
    page_header_t *header = get_page_header(ptr);
    
    // Verify this looks like a valid page header
    if (header->object_size >= MIN_SIZE && 
        header->object_size <= MAX_SMALL_SIZE &&
        header->num_objects > 0 &&
        header->num_objects <= PAGE_SIZE &&
        header->free_count <= header->num_objects) {
        
        // Additional check: verify object_size is a power of 2 between 2 and 1024
        size_t size = header->object_size;
        if (size == 2 || size == 4 || 
            (size >= 8 && size <= 1024 && (size & (size - 1)) == 0)) {
            return 1;
        }
    }
    return 0;
}


void *malloc(size_t size) {
    if (size == 0) {
        return NULL;
    }
    
    // Large allocation (>1024 bytes)
    if (size > MAX_SMALL_SIZE) {
        size_t total_size = size + sizeof(large_header_t);
        size_t num_pages = (total_size + PAGE_SIZE - 1) / PAGE_SIZE;
        size_t alloc_size = num_pages * PAGE_SIZE;
        
        void *mem = mmap(NULL, alloc_size,
                        PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        
        if (mem == MAP_FAILED) {
            return NULL;
        }
        
        large_header_t *header = (large_header_t *)mem;
        header->size = alloc_size;
        header->magic = LARGE_ALLOC_MAGIC;
        
        return (char *)mem + sizeof(large_header_t);
    }
    
    // Small allocation - segregated free list (2, 4, 8, ..., 1024)
    int size_class = get_size_class(size);
    size_t object_size = get_object_size(size_class);
    
    // Find a page with free space
    page_header_t *page = size_class_pages[size_class];
    
    while (page != NULL && page->free_count == 0) {
        page = page->next;
    }
    
    // No page with free space, create new one
    if (page == NULL) {
        page = create_page(object_size);
        if (page == NULL) {
            return NULL;
        }
        page->next = size_class_pages[size_class];
        size_class_pages[size_class] = page;
    }
    
    // Allocate from free list
    free_block_t *block = (free_block_t *)page->free_list;
    page->free_list = block->next;
    page->free_count--;
    
    return (void *)block;
}


void free(void *ptr) {
    if (ptr == NULL) {
        return;
    }
    
    // Check if small allocation
    if (is_small_allocation(ptr)) {
        page_header_t *page = get_page_header(ptr);
        
        // Add to free list
        free_block_t *block = (free_block_t *)ptr;
        block->next = (free_block_t *)page->free_list;
        page->free_list = block;
        page->free_count++;
    } else {
        // Large allocation - unmap it
        large_header_t *header = (large_header_t *)((char *)ptr - sizeof(large_header_t));
        
        // Verify it's actually a large allocation before unmapping
        if (header->magic == LARGE_ALLOC_MAGIC) {
            munmap(header, header->size);
        }
    }
}

void *calloc(size_t nmemb, size_t size) {
    if (nmemb == 0 || size == 0) {
        return NULL;
    }
    
    // Check for overflow
    if (nmemb > SIZE_MAX / size) {
        return NULL;  // Overflow
    }
    
    size_t total_size = nmemb * size;
    
    void *ptr = malloc(total_size);
    if (ptr != NULL) {
        memset(ptr, 0, total_size);
    }
    
    return ptr;
}


void *realloc(void *ptr, size_t size) {
    if (ptr == NULL) {
        return malloc(size);
    }
    
    if (size == 0) {
        free(ptr);
        return NULL;
    }
    
    size_t old_size;
    
    // Determine old size
    if (is_small_allocation(ptr)) {
        page_header_t *page = get_page_header(ptr);
        old_size = page->object_size;
    } else {
        large_header_t *header = (large_header_t *)((char *)ptr - sizeof(large_header_t));
        if (header->magic == LARGE_ALLOC_MAGIC) {
            old_size = header->size - sizeof(large_header_t);
        } else {
            // Invalid pointer, but try to handle gracefully
            return malloc(size);
        }
    }
    
    // If new size fits in same size class, return same pointer
    if (size <= MAX_SMALL_SIZE && old_size <= MAX_SMALL_SIZE) {
        int old_class = get_size_class(old_size);
        int new_class = get_size_class(size);
        if (old_class == new_class) {
            return ptr;
        }
    } else if (size > MAX_SMALL_SIZE && old_size > MAX_SMALL_SIZE) {
        // Both large, check if same allocation would work
        if (size <= old_size) {
            return ptr;
        }
    }
    
    // Allocate new block
    void *new_ptr = malloc(size);
    if (new_ptr == NULL) {
        return NULL;
    }
    
    // Copy old data
    size_t copy_size = (size < old_size) ? size : old_size;
    memcpy(new_ptr, ptr, copy_size);
    
    // Free old block
    free(ptr);
    
    return new_ptr;
}