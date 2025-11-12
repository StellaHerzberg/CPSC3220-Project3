#include <sys/mman.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>
#include <stdint.h>
#include <dlfcn.h>

#define PAGE_SIZE 4096
#define MAX_SMALL 1024

// Struct to hold the page headers that will have the size of the memory blocks
// and the list of free memory that can be used
typedef struct PageHeader {
    size_t block_size;
    void *free_list;
    struct PageHeader *next;    //Pointer to next free memory block of a certain size
} PageHeader;

// Struct to hold the headers for the really large (>1024) memory blocks
// holds the size of the requested memory and the amount of blocks we allocated 
typedef struct LargeHeader {
    size_t size;
    size_t mmap_size;
} LargeHeader;

// Global lists & an initializer variable to help with an LD_PRELOAD issue
static void *allFreeLists[11] = {NULL};
static PageHeader *pageLists[11] = {NULL};
static int initialized = 0;


// HELPER FUNCTIONS
// Function to help get a page size from the amount requested, will also
// make sure that the sizes are in correct bounds 
static size_t roundPageSize(size_t size) {
    
    // Smallest we can allocate is void pointer 
    size_t min = sizeof(void *);

    // Fix the inputted size if it is less than that
    if (size < min){
        size = min;
    }
    // Fixing the size of input here using binary operations instead of if statements
    // idea from office hours
    size_t p = 1;
    while (p < size) {
        p <<= 1;
    }
    return p;
}

// Same thing as rounding the page size except for finding size blocks in linked list
static int sizeToIndex(size_t size) {
    int index = 0;

    // Loop until the index reaches one that can fit the size inputted
    // avoiding slow if-statements by using binary operations
    size_t min = sizeof(void *);
    while (min < size && index < 10) {
        min <<= 1;
        index++;
    }
    return index;
}

// Will allocate a page of memory for a request. Creates its own free list which has
// all of the free space for the block and updates global variables accordingly
static void allocatePage(size_t block_size, int index) {
    
    // Calling mmap - from GitHub example
    // Requests page of memory from OS
    void *page = mmap(NULL, PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

    if (page == MAP_FAILED){        // Shouldn't happen, but just validation
        return;
    }

    // Initialize a header for a page to be allocated w characteristics
    PageHeader *header = (PageHeader *)page;
    header -> block_size = block_size;

    // Put header in pageList for tracking & put it in its size list
    header -> next = pageLists[index];
    pageLists[index] = header;

    // Figure out how much data can be put into block by subtracting header from total space
    size_t header_size = sizeof(PageHeader);
    size_t usable_bytes = PAGE_SIZE - header_size;
    int blocks = usable_bytes / block_size;

    // Validation - probably dont need
    if (blocks <= 0) {
        return;
    }

    // Pointer to first place info can start after header, put this in free_list
    uint8_t *base = (uint8_t *)page + header_size;
    header -> free_list = base;

    // Loop through all blocks in the free list and link together (free list for this page)
    for (int i = 0; i < (blocks - 1); i++) {
        void **current = (void **)(base + i * block_size);  //Getting address of current block
        *current = base + (i + 1) * block_size;     //put address of next block
    }

    // Calculates the address of last block and sets that to point to NULL to show end of list
    void **last = (void **)(base + (blocks - 1) * block_size);
    *last = NULL;

    //merges the new list with the old one to access more easily
    void *old_list_head = allFreeLists[index];
    *last = old_list_head;
    //Add this new page's free list to the array of all of them
    allFreeLists[index] = header -> free_list;
}


// ACTUAL LIBRARY FUNCTIONS
// Function to allocate memory for a requested amount, should be able to handle
// any size of block requested
void *malloc(size_t size) {

    // Trying to fix an LD_PRELOAD issue
    // Looked up libc documentation for idea
    if (!initialized) {
        initialized = 1;
    }

    // Round up size if size is 0
    if (size == 0){
        size = 1;
    }

    // Handle small block requests:
    if (size <= MAX_SMALL) {
        size_t block_size = roundPageSize(size);
        int index = sizeToIndex(block_size);
        
        // If there is nothing on the free list to hold new allocation, allocate a new page
        if (allFreeLists[index] == NULL) {
            allocatePage(block_size, index);
            // Make sure allocaation worked before moving on
            if (allFreeLists[index] == NULL) {
                return NULL;
            }
        } 

        // If there is space (or once space is allocated)
        // Get the address, update list with new address
        void *block = allFreeLists[index];
        allFreeLists[index] = *(void **)block;
            
        return block; //Return memory block
    }

    // Large block requests:
    // Get the needed total size and how much is needed 
    size_t total_size = sizeof(LargeHeader) + size;
    size_t mmap_size = (total_size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);    //More bitwise math to get size needed

    // Call mmap to get memory from OS & check it 
    void *mem = mmap(NULL, mmap_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mem == MAP_FAILED) {
        return NULL;
    }

    // Same as allocatePage(), but large blocks get entire pages instead of part of one
    LargeHeader *header = (LargeHeader *)mem;
    header -> size = size;
    header -> mmap_size = mmap_size;

    // Find where the data can be put considering header size
    char *data_start = (char *)mem + sizeof(LargeHeader);
    
    return (void *)data_start;   //Return where data should begin
}

// Function to free allocated memory
// Will only free large blocks, not small ones
void free(void *ptr) {
    if (ptr == NULL) {  //Just to save myself stress lol
        return;
    }
    // Get the page that memory is on with more binary operations then get header info to store
    uintptr_t address_page = (uintptr_t)ptr & ~(PAGE_SIZE - 1);     //0s out lower bits for base (office hours suggestion)
    PageHeader *page_header = (PageHeader *)address_page;

    // Handle small blocks stored inside a page:
    if ((page_header -> block_size > 0) && (page_header -> block_size <= MAX_SMALL)) {
        
        //Get the index of the memory in the list
        size_t block_size = page_header -> block_size;
        int index = sizeToIndex(block_size);

        //put freed block on the global free list by treating it as ptr, then dereferencing, then changing pointer location
        *(void **)ptr = allFreeLists[index];
        allFreeLists[index] = ptr;

    // Handle large blocks / entire pages
    } else {
        // get the header of large block which is the ptr minus size of header
        LargeHeader *large_block = (LargeHeader *)((char *)ptr - sizeof(LargeHeader));
    
        //Unmap the memory assuming that the size is valid
        if (large_block -> mmap_size > 0){
            munmap((void *)large_block, large_block -> mmap_size);
        }
    }
}

// Function to allocate memory
// Calls malloc for actual allocation but will initialize all the memory to 0 like calloc usually does
void *calloc(size_t mem_block, size_t size) {
    
    // Get the total amount of space needed and malloc memory for it
    size_t total = mem_block * size;
    void *p = malloc(total);
    
    // Assuming it worked, initialize the memory to 0 using memset
    if (p) {
        memset(p, 0, total);
    }
    return p;
}

// Function to reallocate memory that is already allocated
// Will call malloc we already created
void *realloc(void *ptr, size_t size) {
    
    // Making sure memory actually needs to be reallocated
    // If nothing in pointer, can just act like malloc, so just call malloc
    if (ptr == NULL) {
        return malloc(size);
    }
    // If the size is 0, then no memory needs to be allocated so just free the old memory
    if (size == 0) { 
        free(ptr); 
        return NULL; 
    }

    // Get pointer for location to hold new memory by calling malloc
    void *newptr = malloc(size);
    if (!newptr) {     //check it just in case because im anxious
        return NULL;
    }

    // Same math as before to get the page size of old memory 
    uintptr_t page_addr = (uintptr_t)ptr & ~(PAGE_SIZE - 1);

    // Get the header for that page
    PageHeader *page_header = (PageHeader *)page_addr;

    // If it was a small page:
    size_t old_size; 
    if (page_header -> block_size > 0 && page_header -> block_size <= MAX_SMALL) {
        old_size = page_header -> block_size;  //Actual allocated size
    
    // If it was a large:
    } else {
        //Same offset logic as in free(), only needed for large blocks
        LargeHeader *large_header = (LargeHeader *)((char *)ptr - sizeof(LargeHeader));
        old_size = large_header ->size;  //Actual allocated size
    }

    // Can only write as much as we initially allocated to avoid writing over other pages 
    // check that before moving on and change if needed
    if (size < old_size) {
        old_size = size;
    }
    // Finally, we have new size, so copy the memory over to new location (reallocate it)
    memcpy(newptr, ptr, old_size);
    
    // Delete pointer to old memory since not needed anymore and return new address
    free(ptr);
    return newptr;
}