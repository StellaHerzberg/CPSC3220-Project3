#include <sys/mman.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>
#include <stdint.h>
#include <dlfcn.h>

#define PAGE_SIZE 4096
#define MAX_SMALL 1024

typedef struct PageHeader {
    size_t block_size;
    void *free_list;
    struct PageHeader *next;
} PageHeader;

typedef struct LargeHeader {
    size_t size;
    size_t mmap_size;
} LargeHeader;

static void *allFreeLists[11] = {NULL};
static PageHeader *pageLists[11] = {NULL};
static int initialized = 0;

static size_t roundPageSize(size_t size) {
    size_t min = sizeof(void *);

    if (size < min){
        size = min;
    }
    size_t p = 1;
    while (p < size) {
        p <<= 1;
    }
    return p;
}

static int sizeToIndex(size_t size) {
    int index = 0;
    size_t min = sizeof(void *);

    while (min < size && index < 10) {
        min <<= 1;
        index++;
    }
    return index;
}

static void allocatePage(size_t block_size, int index) {
    
    // Calling mmap - from GitHub example
    void *page = mmap(NULL, PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

    if (page == MAP_FAILED){
        return;
    }

    PageHeader *header = (PageHeader *)page;
    header -> block_size = block_size;
    header -> next = pageLists[index];
    pageLists[index] = header;

    size_t header_size = sizeof(PageHeader);
    size_t usable_bytes = PAGE_SIZE - header_size;
    int blocks = usable_bytes / block_size;

    if (blocks <= 0) {
        return;
    }

    uint8_t *base = (uint8_t *)page + header_size;

    header -> free_list = base;

    for (int i = 0; i < (blocks - 1); i++) {
        void **current = (void **)(base + i * block_size);
        *current = base + (i + 1) * block_size;
    }

    void **last = (void **)(base + (blocks - 1) * block_size);
    *last = NULL;

    void *old_list_head = allFreeLists[index];
    *last = old_list_head;
    allFreeLists[index] = header -> free_list;
}


void *malloc(size_t size) {

    if (!initialized) {
        initialized = 1;
    }

    if (size == 0){
        size = 1;
    }

    if (size <= MAX_SMALL) {
        size_t block_size = roundPageSize(size);
        int index = sizeToIndex(block_size);
    

        if (allFreeLists[index] == NULL) {
            allocatePage(block_size, index);

            if (allFreeLists[index] == NULL) {
                return NULL;
            }
        }
        void *block = allFreeLists[index];
        allFreeLists[index] = *(void **)block;
        return block;
    }
    size_t total_size = sizeof(LargeHeader) + size;
    size_t mmap_size = (total_size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

    void *mem = mmap(NULL, mmap_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mem == MAP_FAILED) {
        return NULL;
    }

    LargeHeader *header = (LargeHeader *)mem;
    header -> size = size;
    header -> mmap_size = mmap_size;

    return (void *)((char *)mem + sizeof(LargeHeader));
}

void free(void *ptr) {
    if (ptr == NULL) {
        return;
    }

    uintptr_t address_page = (uintptr_t)ptr & ~(PAGE_SIZE - 1);
    PageHeader *page_header = (PageHeader *)address_page;

    if ((page_header -> block_size > 0) && (page_header -> block_size <= MAX_SMALL)) {
        size_t block_size = page_header -> block_size;
        int index = sizeToIndex(block_size);

        *(void **)ptr = allFreeLists[index];
        allFreeLists[index] = ptr;
        return;
    }

    LargeHeader *large_block = (LargeHeader *)((char *)ptr - sizeof(LargeHeader));
    if (large_block -> mmap_size > 0){
        munmap((void *)large_block, large_block -> mmap_size);
    }
}

void *calloc(size_t mem_block, size_t size) {
    size_t total = mem_block * size;
    void *p = malloc(total);
    if (p) {
        memset(p, 0, total);
    }
    return p;
}

void *realloc(void *ptr, size_t size) {
    if (ptr == NULL) {
        return malloc(size);
    }
    if (size == 0) { 
        free(ptr); return NULL; 
    }

    void *newptr = malloc(size);
    if (!newptr) {
        return NULL;
    }

    // Copy old content safely
    uintptr_t page_addr = (uintptr_t)ptr & ~(PAGE_SIZE - 1);
    PageHeader *page_header = (PageHeader *)page_addr;

    size_t old_size; 
    if (page_header -> block_size > 0 && page_header -> block_size <= MAX_SMALL) {
        old_size = page_header -> block_size;
    
    } else {
        LargeHeader *large_header = (LargeHeader *)((char *)ptr - sizeof(LargeHeader));
        old_size = large_header ->size;
    }

    if (size < old_size) {
        old_size = size;
    }
    memcpy(newptr, ptr, old_size);
    free(ptr);
    return newptr;
}