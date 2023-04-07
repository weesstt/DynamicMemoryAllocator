/**
 * Do not submit your assignment with a main function in this file.
 * If you submit with a main function in this file, you will get a zero.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "debug.h"
#include "sfmm.h"
#include "sfmm_util.h"
#include <errno.h>
#include <inttypes.h>

#define TRUE (1)
#define FALSE (0)
#define WORD_SIZE 2 //word size = 2 bytes
#define MEM_ROW_SIZE 8 //memory row size = 8 bytes
#define HEADER_SIZE 8 //header size = 8 bytes
#define MIN_BLOCK_SIZE 32 //min block size = 32 bytes

static int mallocInit = FALSE; //Indicates whether a first malloc call has been made to initalize 
static sf_block *heapProPtr = NULL; //this will be a pointer to the prologue block 
static sf_block *heapEpiPtr = NULL; //this will be a pointer to the epilogue block 

/*
* Simple function that satisfies malloc error requirements with one line.
* 
* @return NULL, also sets sf_errno to ENOMEM
*/
static void *malloc_err(){
    sf_errno = ENOMEM;
    return NULL;
}

size_t maskInfoBits(size_t size){
    size = size >> 3;
    size = size << 3;
    return size; 
}

//Increment a pointer by a specificed number of bytes
static void *incrementPointer(size_t bytes, void *ptr){
    return ((void *) ptr) + bytes; 
}

//Given a pointer to a free block header, return a pointer to the footer location of that block
static void *getFooterPointer(void *ptr){
    size_t blockSize = maskInfoBits(((sf_block *) ptr) -> header); 
    return incrementPointer(blockSize - sizeof(sf_header), ptr);
}

static sf_block *getNextBlock(sf_block *ptr){
    return (sf_block *) incrementPointer(maskInfoBits(ptr -> header), ptr);
}

static sf_block *getPrevBlock(sf_block *ptr){
    sf_block *prevFooter = incrementPointer(-sizeof(sf_footer), ptr);
    size_t size = maskInfoBits(prevFooter -> header);
    return (sf_block *) incrementPointer(-size, ptr);
}

static size_t power(size_t base, int power){
    if(power == 0){
        return 1;
    }else if(power == 1){
        return base;
    }

    size_t result = base;
    for(int i = 0; i < power - 1; i++){
        result = result * base; 
    }
    return result;
} 

//Given the size of a free block, return the correct index of the free list to insert this block in
static int getFreeListIndex(size_t size){
    size = maskInfoBits(size);
    if(size < MIN_BLOCK_SIZE){
        return -1;
    }else if(size == MIN_BLOCK_SIZE){
        return 0;
    }else if(size > (power(2, NUM_FREE_LISTS - 2)) * MIN_BLOCK_SIZE){
        return NUM_FREE_LISTS - 1;
    }
    for(int i = 1; i < NUM_FREE_LISTS - 1; i++){
        size_t min = MIN_BLOCK_SIZE * power(2, i - 1);
        size_t max = MIN_BLOCK_SIZE * power(2, i);
        if(size > min && size <= max){
            return i;
        }
    }
    return NUM_FREE_LISTS - 1;
}

//Given the size of a requested block, return the index of the quick list to check. Return -1 if size is too big for a quick list
static int getQuickListIndex(size_t size){
    size = maskInfoBits(size); 
    for(int i = 0; i < NUM_QUICK_LISTS; i++){
        size_t quickSize = MIN_BLOCK_SIZE + (i * 8);
        if(quickSize == size){
            return i;
        }
    }
    return -1;
}

//given a pointer to a block, determine what the current coalesce state is for that block
enum CoalesceBlockStates {bothFree, prevFree, nextFree, bothAlloc}; 
static int getCoalesceSituation(sf_block *ptr){
    if(!mallocInit){
        return bothAlloc; //just for when we init malloc
    }
    if(((ptr -> header) & 0x2) > 0){//prev block is alloc
        if((((sf_block *) incrementPointer(maskInfoBits(ptr -> header), ptr)) -> header & 0x1) > 0){//next block is alloc
            return bothAlloc;
        }else{//prev block is alloc but next block is free
            return nextFree;
        }
    }else{//prev block is free
        if((((sf_block *) incrementPointer(maskInfoBits(ptr -> header), ptr)) -> header & 0x1) > 0){//next block is alloc
            return prevFree;
        }else{
            return bothFree;
        }
    }
}

//Remove pointer in a free list
static void removeBlockFromFreeList(sf_block *ptr){
    sf_block *freeListHead = (sf_block *) &(sf_free_list_heads[getFreeListIndex(ptr -> header)]);
    sf_block *cursor = freeListHead -> body.links.next; 
    while(cursor != freeListHead){
        if(cursor == ptr){
            sf_block *prev = cursor -> body.links.prev; 
            sf_block *next = cursor -> body.links.next; 
            prev -> body.links.next = next; 
            next -> body.links.prev = prev;
            break;
        }
        cursor = cursor -> body.links.next; 
    }
}

//Insert free block into list, assume that the header and info bits as well as footer have already been set
static void insertBlockIntoFreeList(sf_block *ptr){
    //coalesce block with other free blocks
    switch(getCoalesceSituation(ptr)){
        case bothAlloc:
            break; //no coalescing possible
        case nextFree: //next block is free but prev block is alloc
            sf_block *nextBlock = getNextBlock(ptr);
            removeBlockFromFreeList(nextBlock);
            size_t nextSize = maskInfoBits(nextBlock -> header); 
            ptr -> header = (ptr -> header) + nextSize;
            (nextBlock -> header) &= 0x0; //clear header to make space for payload
            sf_block *footer = getFooterPointer(ptr);
            footer -> header = ptr -> header; 
            break;
        case prevFree://prev block is free but next block is alloc 
            sf_block *prevBlock = getPrevBlock(ptr);
            removeBlockFromFreeList(prevBlock);
            prevBlock -> header = (prevBlock -> header) + maskInfoBits(ptr -> header); 
            (ptr -> header) &= 0x0; //clear header 
            sf_block *prevFreeFooter = getFooterPointer(prevBlock);
            prevFreeFooter -> header = prevBlock -> header; 
            ptr = prevBlock;
            break;
        case bothFree:
            sf_block *bothFreePrevBlock = getPrevBlock(ptr);
            removeBlockFromFreeList(bothFreePrevBlock);
            sf_block *bothFreeNextBlock = getNextBlock(ptr);
            removeBlockFromFreeList(bothFreeNextBlock);
            bothFreePrevBlock -> header = (bothFreePrevBlock -> header) + maskInfoBits(ptr -> header) + maskInfoBits(bothFreeNextBlock -> header); 
            (bothFreeNextBlock -> header) &= 0x0;
            (ptr -> header) &= 0x0; 
            sf_block *bothFreeFooter = getFooterPointer(bothFreePrevBlock);
            bothFreeFooter -> header = bothFreePrevBlock -> header; 
            ptr = bothFreePrevBlock;
            break; 
    }
    //set the prev alloc bit of the next block to 0
    sf_block *nextBlock = getNextBlock(ptr);
    size_t nextHeader = nextBlock -> header;
    int quickList = nextHeader & 0x4;
    int alloc = nextHeader & 0x1;
    nextBlock -> header = (maskInfoBits(nextHeader) | quickList) | alloc; 
    if(alloc == FALSE){
        sf_block *footer = getFooterPointer(nextBlock);
        footer -> header = nextBlock -> header;
    }

    sf_block *freeHeaderPointer = (sf_block *) &(sf_free_list_heads[getFreeListIndex(ptr -> header)]);
    ptr -> body.links.next = freeHeaderPointer -> body.links.next; //set new pointer next link to the prev first node. 
    (freeHeaderPointer -> body.links.next) -> body.links.prev = ptr;
    (freeHeaderPointer -> body.links.next) = ptr; 
    ptr -> body.links.prev = freeHeaderPointer;
}

//search quick lists for a block of correct size, LIFO like a stack
//returns null if no quick list block is found
static sf_block *searchQuickLists(size_t size){
    int quickIndex = getQuickListIndex(size);
    if(quickIndex != -1){//if quickIndex = -1 then requested block is too big to be on a quick list
        int quickLength = sf_quick_lists[quickIndex].length; 
        if(quickLength != 0){
            sf_block *ptr = (sf_block *) sf_quick_lists[quickIndex].first;
            sf_quick_lists[quickIndex].length = quickLength - 1;
            sf_quick_lists[quickIndex].first = ptr -> body.links.next; 
            return ptr; 
        }
    }
    return NULL;
}

static void *splitBlock(size_t freeBlockSize, size_t size, sf_block *ptr){
    if(freeBlockSize - size >= 32){
        //proceed to split block
        ptr -> header = size | ((ptr -> header & 0x7) | 0x1); //info bits (quickList = 0) (prevAlloc = 1 or 0 depending on orig header) (alloc = 1)
        sf_block *remainder = incrementPointer(size, ptr); 
        remainder -> header = (freeBlockSize - size) | 0x2; //info bits (quickList = 0) (prevAlloc = 1) (alloc = 0) 
        insertBlockIntoFreeList(remainder);
        sf_block *footer = getFooterPointer(remainder); 
        footer -> header = remainder -> header; 
        return ptr; //return original pointer
    }else{//otherwise we do not want to split the block and will just allocate a much larger block
        return ptr; 
    }
}

//search free list for a big enough block, returns a new allocated block
//will split block and do neccessary things for that.
//will return null if there is no block found big enough
static sf_block *searchFreeLists(size_t size){
    sf_block *ptr = NULL;
    for(int i = getFreeListIndex(size); i < NUM_FREE_LISTS; i++){
        sf_block *head = &(sf_free_list_heads[i]);
        sf_block *cursor = head;
        if(cursor -> body.links.next != head){
            cursor = cursor -> body.links.next;
            while(cursor != head){
                size_t cursorSize = maskInfoBits(cursor -> header);
                if(cursorSize >= size){
                    ptr = cursor; 
                    //break links in free list for block we are returning
                    (cursor -> body.links.prev) -> body.links.next = cursor -> body.links.next; 
                    (cursor -> body.links.next) -> body.links.prev = cursor -> body.links.prev; 
                    break;
                }else{
                    cursor = cursor -> body.links.next; 
                }
            }
        }
        if(ptr != NULL){
            break;
        }
    }

    if(ptr != NULL){
        size_t freeBlockSize = maskInfoBits(ptr -> header);
        return splitBlock(freeBlockSize, size, ptr);
    }else{
        return NULL;
    }
}



static int extendHeap(){
    if(sf_mem_grow() == NULL){
        return FALSE;
    }

    int prevAlloc = (heapEpiPtr -> header) & 0x2;
    size_t size = PAGE_SZ;
    heapEpiPtr -> header = size | prevAlloc;

    sf_block *footer = getFooterPointer(heapEpiPtr);
    footer -> header = heapEpiPtr -> header; 

    heapEpiPtr = (sf_block *) incrementPointer(-sizeof(sf_header), sf_mem_end());
    heapEpiPtr -> header = 0x1; //allocated block and prev alloc is always gonna be 0

    insertBlockIntoFreeList(incrementPointer(-size, heapEpiPtr));
    return TRUE;
}

/*
 * This is your implementation of sf_malloc. It acquires uninitialized memory that
 * is aligned and padded properly for the underlying system.
 *
 * @param size The number of bytes requested to be allocated.
 *
 * @return If size is 0, then NULL is returned without setting sf_errno.
 * If size is nonzero, then if the allocation is successful a pointer to a valid region of
 * memory of the requested size is returned.  If the allocation is not successful, then
 * NULL is returned and sf_errno is set to ENOMEM.
 */
void *sf_malloc(size_t size) {
    if(size == 0)
        return NULL;

    if(!mallocInit){//first time calling malloc so we will want to initalize.
        //call sf_mem_grow to obtain a page of memory, initalize the prologue and inital epilogue
        //then remainder of free memory should be inserted into the free list as one block
        heapProPtr = sf_mem_grow();//returns a pointer to the start of new memory page
        if(heapProPtr == NULL){
            return malloc_err();
        }

        //init free lists
        for(int i = 0; i < NUM_FREE_LISTS; i++){//set up dummy heads
            sf_block *dummy = &(sf_free_list_heads[i]);
            dummy -> body.links.next = dummy;
            dummy -> body.links.prev = dummy;
        }  

        //Create the prologue block
        sf_block *prologue = (heapProPtr);
        prologue -> header = MIN_BLOCK_SIZE | 0x1; 
        *(prologue -> body.payload) = 0x0;
        
        //Create the epilogue header
        sf_block *epilogue = (sf_block *) (incrementPointer(-sizeof(sf_header), sf_mem_end()));
        epilogue -> header = 0x1; //size 0 but we have an allocated block so 0x1
        heapEpiPtr = epilogue;

        //Create the free block
        sf_block *freeBlock = (sf_block *) incrementPointer(MIN_BLOCK_SIZE, heapProPtr);
        size_t freeBlockSize = (PAGE_SZ - MIN_BLOCK_SIZE - sizeof(sf_header)) | 0x2;//4096 - 32 (prologue) - 8 (epilogue) | (qlist = 1) (prev alloc = 1) (alloc = 0)
        freeBlock -> header = freeBlockSize;

        //insert newly created free block into free list
        insertBlockIntoFreeList(freeBlock);

        //footer of free block
        sf_block *footer = (sf_block *) getFooterPointer(freeBlock);
        footer -> header = (freeBlock -> header);

        mallocInit = TRUE; //we have initalized malloc
    }

    //calculate required size of free block needed
    size = size + sizeof(sf_header); //allocated blocks consists of header and payload
    if(size < MIN_BLOCK_SIZE){
        size = MIN_BLOCK_SIZE;
    }else{
        while((size & 0x7) > 0){ //make size a multiple 8 if not
            size++;
        }
    }

    sf_block *ptr = searchQuickLists(size);
    if(ptr == NULL){//if we did not find a ptr to a free block in the quick lists, proceed to search free list
        ptr = searchFreeLists(size);
        while(ptr == NULL){//Request new page of memory and create free block from it if size is bigger than any avail free block 
            if(extendHeap() == FALSE){//extend heap was not successful
                return malloc_err();
            }
            ptr = searchFreeLists(size);
        }
    }
    sf_block *next = getNextBlock(ptr);
    next -> header = (next -> header) | 0x2; //set prev alloc bit of next block
    ptr -> header = (ptr -> header) | 0x1; //set alloc field if split block did not do it
    return ptr -> body.payload;
}

//returns true if the block was put into a quick list and returns false if it was not inserted into a quick list
static int insertBlockIntoQuickList(sf_block *ptr){
    int index = getQuickListIndex(ptr -> header); 
    if(index != -1){
        int quickLength = sf_quick_lists[index].length;
        if(quickLength == QUICK_LIST_MAX){//flush quick list
            sf_block *cursor = sf_quick_lists[index].first; 
            while(cursor != NULL){//cursor -> body.links.next != NULL
                int prevAlloc = (cursor -> header) & 0x2; //extract prev alloc bit
                size_t size = maskInfoBits(cursor -> header); //mask info bits so that we can make the header a free block not in quicklist
                size = (size | (prevAlloc));//set the prev alloc bit if it was set in the header before
                cursor -> header = size; 
                sf_block *footer = getFooterPointer(cursor); 
                footer -> header = cursor -> header; 
                sf_quick_lists[index].first = cursor -> body.links.next; //remove block from quick list
                insertBlockIntoFreeList(cursor); 
                cursor = sf_quick_lists[index].first; 
            }
            sf_quick_lists[index].first = NULL;
            quickLength = 0; 
        }
        quickLength++;
        sf_quick_lists[index].length = quickLength;
        if(sf_quick_lists[index].first != NULL){
            ptr -> header = (ptr -> header) | 0x4; //set the in quick list bit
            ptr -> body.links.next = sf_quick_lists[index].first;
            sf_quick_lists[index].first = ptr; 
        }else{
            ptr -> header = (ptr -> header) | 0x4; //set the in quick list bit
            ptr -> body.links.next = NULL;
            sf_quick_lists[index].first = ptr; 
        }
        return TRUE;
    }else{
        return FALSE; 
    }
}

int validatePointer(void *pp){
    if(pp == NULL){
        return FALSE;
    }

    sf_block *block = (sf_block *) pp; 
    block = incrementPointer(-sizeof(sf_header), block);
    size_t size = maskInfoBits(block -> header); 

    if(pp < (((void *) heapProPtr) + 32) 
        || ((uintptr_t) pp & 0x7) > 0 
        || size < 32 
        || (size & 0x7) > 0
        || pp >= ((void *) heapEpiPtr) 
        || getFooterPointer(block) >= ((void *) heapEpiPtr) 
        || ((block -> header) & 0x4) > 0
        || ((block -> header) & 0x1) == 0
        || !mallocInit){
            return FALSE;
    }

    if(((block -> header) & 0x2) == 0){ //get prev alloc bit
        sf_block *prevHeader = getPrevBlock(pp);
        if(((prevHeader -> header) & 0x1) < 0){
            return FALSE;
        }
    }

    return TRUE;
}

/*
 * Marks a dynamically allocated region as no longer in use.
 * Adds the newly freed block to the free list.
 *
 * @param ptr Address of memory returned by the function sf_malloc.
 *
 * If ptr is invalid, the function calls abort() to exit the program.
 */
void sf_free(void *pp) {
    sf_block *block = (sf_block *) pp; 
    block = incrementPointer(-sizeof(sf_header), block);

    if(!validatePointer(pp)){
        abort();
    }

    //insert into quick list, flushing if neccessary first but done by function
    if(insertBlockIntoQuickList(block) == FALSE){
        int prevAlloc = (block -> header) & 0x2; //extract prev alloc bit
        size_t size = maskInfoBits(block -> header); //mask info bits so that we can make the header a free block not in quicklist
        size = (size | (prevAlloc));//set the prev alloc bit if it was set in the header before
        block -> header = size; 
        sf_block *footer = getFooterPointer(block);
        footer -> header = block -> header;  
        insertBlockIntoFreeList(block);
    }
}

/*
 * Resizes the memory pointed to by ptr to size bytes.
 *
 * @param ptr Address of the memory region to resize.
 * @param size The minimum size to resize the memory to.
 *
 * @return If successful, the pointer to a valid region of memory is
 * returned, else NULL is returned and sf_errno is set appropriately.
 *
 *   If sf_realloc is called with an invalid pointer sf_errno should be set to EINVAL.
 *   If there is no memory available sf_realloc should set sf_errno to ENOMEM.
 *
 * If sf_realloc is called with a valid pointer and a size of 0 it should free
 * the allocated block and return NULL without setting sf_errno.
 */

void *sf_realloc(void *pp, size_t rsize) {
    sf_block *block = (sf_block *) incrementPointer(-sizeof(sf_header), pp);

    if(!validatePointer(pp)){
        sf_errno = EINVAL;
        return NULL;
    }

    if(rsize == 0){//free pointer and return NULL
        int prevAlloc = (block -> header) & 0x2;
        size_t size = maskInfoBits(block -> header) | prevAlloc; 
        block -> header = size;
        sf_block *footer = getFooterPointer(block);
        footer -> header = size;
        insertBlockIntoFreeList(block);
        return NULL; 
    }

    size_t size = maskInfoBits(block -> header);
    if(size < rsize){//realloc to a larger size
        void *largerBlock = sf_malloc(rsize);
        if(largerBlock == NULL){ //sf_errno is set sf_malloc
            return NULL;
        }
        size_t payloadSize = maskInfoBits(block -> header) - sizeof(sf_header);
        memcpy(largerBlock, pp, payloadSize);
        //free prev block
        sf_free(pp);
        return largerBlock;
    }else if(size == rsize){//realloc of same size so just return current pointer
        return pp;
    }else{//realloc to a smaller size
        size_t newSize = rsize + sizeof(sf_header); //allocated blocks consists of header and payload
        if(newSize < MIN_BLOCK_SIZE){
            newSize = MIN_BLOCK_SIZE;
        }else{
            while((newSize & 0x7) > 0){ //make size a multiple 8 if not
                newSize++;
            }
        }

        if(maskInfoBits(block -> header) - newSize >= MIN_BLOCK_SIZE){//only split if not creating splinter
            sf_block *newBlock = incrementPointer(newSize, block);
            newBlock -> header = (maskInfoBits(block -> header) - newSize) | 0x2; //prev alloc bit is true
            sf_block *footer = getFooterPointer(newBlock);
            footer -> header = newBlock -> header;
            block -> header = (block -> header) - maskInfoBits(newBlock -> header);
            insertBlockIntoFreeList(newBlock); //insert new free block into free list
            return block -> body.payload;
        }else{//cannot split block so just return orig pointer
            return pp;
        }
    }
}

static int isPowerOf2(int n){
    if(n < 1){
        return FALSE; 
    }

    while(n != 1){
        if(n % 2 != 0){
            return FALSE;
        }
        n = n / 2;
    }

    return TRUE;
}

/*
 * Allocates a block of memory with a specified alignment.
 *
 * @param align The alignment required of the returned pointer.
 * @param size The number of bytes requested to be allocated.
 *
 * @return If align is not a power of two or is less than the minimum block size,
 * then NULL is returned and sf_errno is set to EINVAL.
 * If size is 0, then NULL is returned without setting sf_errno.
 * Otherwise, if the allocation is successful a pointer to a valid region of memory
 * of the requested size and with the requested alignment is returned.
 * If the allocation is not successful, then NULL is returned and sf_errno is set
 * to ENOMEM.
 */

void *sf_memalign(size_t size, size_t align) {
    if(align < 8 || !isPowerOf2(align)){
        sf_errno = EINVAL;
        return NULL;
    }

    if(size == 0){
        return NULL;
    }

    size_t mallocSize = size + align + MIN_BLOCK_SIZE + sizeof(sf_header);
    void *ptr = sf_malloc(mallocSize);
    if(ptr == NULL){
        sf_errno = ENOMEM;
        return NULL;
    }

    ptr = incrementPointer(-sizeof(sf_header), ptr);
    mallocSize = maskInfoBits(((sf_block *) ptr) -> header);

    //check if normal payload address is aligned
    char *payload = (((sf_block *) ptr) -> body.payload);
    if((uintptr_t) payload % align == 0){
        printf("Aligned! %p", payload);
    }else{
        sf_block *block = incrementPointer(MIN_BLOCK_SIZE, ptr);
        size_t offset = MIN_BLOCK_SIZE;
        payload = block -> body.payload; 
        while((uintptr_t) payload % align != 0){
            offset++;
            block = incrementPointer(1, block);
            payload = block -> body.payload; 
            if(mallocSize - offset < MIN_BLOCK_SIZE || mallocSize - offset < size + sizeof(sf_header)){
                sf_free(ptr); 
                sf_errno = ENOMEM;
                return NULL;
            }
        }

        sf_block *front = (sf_block *) ptr;
        int prevAlloc = (front -> header) & 0x2; 
        size_t frontSize = (offset) | prevAlloc;
        front -> header = frontSize;
        sf_block *frontFooter = getFooterPointer(front);
        frontFooter -> header = frontSize;
        block -> header = (mallocSize - offset) | 0x1;
        insertBlockIntoFreeList(front);
        size = size + sizeof(sf_header); //allocated blocks consists of header and payload
        if(size < MIN_BLOCK_SIZE){
            size = MIN_BLOCK_SIZE;
        }else{
            while((size & 0x7) > 0){ //make size a multiple 8 if not
                size++;
            }
        }
        return ((sf_block *) splitBlock(maskInfoBits(block -> header), size, block)) -> body.payload;
    }
    return NULL;
}
