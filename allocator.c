#include <sys/mman.h>
#include <stdint.h>
#include <stddef.h>


#define PAGE_SIZE 4096
#define ALIGN 8

// Power of 2 sizes: 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024
#define NUM_SIZE_CLASSES 10

// Header for each page managing small blocks
typedef struct page_metadata {
    size_t block_size;              // Size of blocks in this page
    struct page_metadata *next;     // Next page with same block size
    void *free_list;                // Head of free list for this page
} page_metadata_t;

// Free block node (lives inside free blocks)
typedef struct free_block {
    struct free_block *next;
} free_block_t;

// Header for large allocations (>1024 bytes)
typedef struct large_block_header {
    size_t total_size;              // Total size including header
    struct large_block_header *next;
    struct large_block_header *prev;
} large_block_header_t;

// Global segregated free lists - one for each size class
// Index 0 = size 2, Index 1 = size 4, ..., Index 9 = size 1024
static page_metadata_t *size_class_pages[NUM_SIZE_CLASSES] = {NULL};

// List of large allocations
static large_block_header_t *large_blocks_head = NULL;

// Convert size to size class index (0-9)
// Returns -1 if size > 1024 (large allocation)
static int size_to_class(size_t size) {
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
    return -1; // Large allocation
}

// Convert size class index to actual block size
static size_t class_to_size(int class_index) {
    return (size_t)(1 << (class_index + 1)); // 2^(index+1)
}

// Create a new page for a specific size class
static page_metadata_t* create_page_for_size(size_t block_size) {
    // Request a 4K page from OS
    void *page = mmap(NULL, PAGE_SIZE,
                     PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    
    if (page == MAP_FAILED) {
        return NULL;
    }
    
    // Place metadata at the start of the page
    page_metadata_t *meta = (page_metadata_t*)page;
    meta->block_size = block_size;
    meta->next = NULL;
    meta->free_list = NULL;
    
    // Calculate usable space and number of blocks
    size_t metadata_size = sizeof(page_metadata_t);
    // Align metadata end to 8-byte boundary
    metadata_size = (metadata_size + ALIGN - 1) & ~(ALIGN - 1);
    
    size_t usable_space = PAGE_SIZE - metadata_size;
    size_t num_blocks = usable_space / block_size;
    
    // Initialize free list - link all blocks in this page
    char *block_area = (char*)page + metadata_size;
    for (size_t i = 0; i < num_blocks; i++) {
        free_block_t *block = (free_block_t*)(block_area + (i * block_size));
        block->next = meta->free_list;
        meta->free_list = block;
    }
    
    return meta;
}

// Find which page contains a given pointer (for small allocations)
static page_metadata_t* find_page_containing_ptr(void *ptr, int class_index) {
    page_metadata_t *page = size_class_pages[class_index];
    uintptr_t ptr_addr = (uintptr_t)ptr;
    
    while (page) {
        uintptr_t page_start = (uintptr_t)page;
        uintptr_t page_end = page_start + PAGE_SIZE;
        
        if (ptr_addr >= page_start && ptr_addr < page_end) {
            return page;
        }
        page = page->next;
    }
    
    return NULL;
}

// Allocate memory for small blocks
static void* allocate_small(size_t size) {
    int class_index = size_to_class(size);
    size_t block_size = class_to_size(class_index);
    
    // Find a page with free blocks in this size class
    page_metadata_t *page = size_class_pages[class_index];
    while (page && page->free_list == NULL) {
        page = page->next;
    }
    
    // No page with free blocks, create a new one
    if (page == NULL) {
        page = create_page_for_size(block_size);
        if (page == NULL) {
            return NULL;
        }
        
        // Add to front of list for this size class
        page->next = size_class_pages[class_index];
        size_class_pages[class_index] = page;
    }
    
    // Allocate from free list
    free_block_t *block = (free_block_t*)page->free_list;
    page->free_list = block->next;
    
    return (void*)block;
}

// Allocate memory for large blocks
static void* allocate_large(size_t size) {
    // Calculate total size including header
    size_t total_size = size + sizeof(large_block_header_t);
    
    // Round up to page size
    size_t num_pages = (total_size + PAGE_SIZE - 1) / PAGE_SIZE;
    size_t alloc_size = num_pages * PAGE_SIZE;
    
    // Allocate using mmap
    void *mem = mmap(NULL, alloc_size,
                    PROT_READ | PROT_WRITE,
                    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    
    if (mem == MAP_FAILED) {
        return NULL;
    }
    
    // Initialize header
    large_block_header_t *header = (large_block_header_t*)mem;
    header->total_size = alloc_size;
    header->next = large_blocks_head;
    header->prev = NULL;
    
    if (large_blocks_head) {
        large_blocks_head->prev = header;
    }
    large_blocks_head = header;
    
    // Return pointer after header
    return (void*)((char*)mem + sizeof(large_block_header_t));
}

// Free small block
static void free_small(void *ptr) {
    // Find which size class
    for (int i = 0; i < NUM_SIZE_CLASSES; i++) {
        page_metadata_t *page = find_page_containing_ptr(ptr, i);
        
        if (page) {
            // Add back to free list
            free_block_t *block = (free_block_t*)ptr;
            block->next = page->free_list;
            page->free_list = block;
            return;
        }
    }
}

// Free large block - returns 1 if found and freed, 0 otherwise
static int free_large(void *ptr) {
    large_block_header_t *large = large_blocks_head;
    
    while (large) {
        void *user_ptr = (void*)((char*)large + sizeof(large_block_header_t));
        
        if (user_ptr == ptr) {
            // Remove from linked list
            if (large->prev) {
                large->prev->next = large->next;
            } else {
                large_blocks_head = large->next;
            }
            
            if (large->next) {
                large->next->prev = large->prev;
            }
            
            // Unmap the memory
            munmap(large, large->total_size);
            return 1; // Found and freed
        }
        
        large = large->next;
    }
    
    return 0; // Not found
}

// Get the size of an allocation
static size_t get_allocation_size(void *ptr) {
    if (ptr == NULL) {
        return 0;
    }
    
    // Check if large allocation
    large_block_header_t *large = large_blocks_head;
    while (large) {
        void *user_ptr = (void*)((char*)large + sizeof(large_block_header_t));
        if (user_ptr == ptr) {
            return large->total_size - sizeof(large_block_header_t);
        }
        large = large->next;
    }
    
    // Check small allocations
    for (int i = 0; i < NUM_SIZE_CLASSES; i++) {
        page_metadata_t *page = find_page_containing_ptr(ptr, i);
        if (page) {
            return page->block_size;
        }
    }
    
    return 0;
}