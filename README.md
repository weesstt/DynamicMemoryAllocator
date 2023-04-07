# Dynamic Memory Allocator - Systems Fundamentals - Spring 2023
#### Professor Eugene Stark

**NOTE:** In this document, we refer to a word as 2 bytes (16 bits) and a memory
row as 4 words (64 bits). We consider a page of memory to be 4096 bytes (4 KB)

## Objectives

After completing this project, you will have a better understanding of:
* The inner workings of a dynamic memory allocator
* Memory padding and alignment
* Structs and linked lists in C
* [errno](https://linux.die.net/man/3/errno) numbers in C
* Unit testing in C

# Overview

You will create an allocator for the x86-64 architecture with the following features:

- Free lists segregated by size class, using first-fit policy within each size class,
  augmented with a set of "quick lists" holding small blocks segregated by size.
- Immediate coalescing of large blocks on free with adjacent free blocks;
  delayed coalescing on free of small blocks.
- Boundary tags to support efficient coalescing, with footer optimization that allows
    footers to be omitted from allocated blocks.
- Block splitting without creating splinters.
- Allocated blocks aligned to "single memory row" (8-byte) boundaries.
- Free lists maintained using **last in first out (LIFO)** discipline.
- Use of a prologue and epilogue to achieve required alignment and avoid edge cases
    at the end of the heap.

You will implement your own versions of the **malloc**, **realloc**,
**free**, and **memalign** functions.

## Free List Management Policy

Your allocator **MUST** use the following scheme to manage free blocks:
Free blocks will be stored in a fixed array of `NUM_FREE_LISTS` free lists,
segregated by size class (see **Chapter 9.9.14 Page 863** for a discussion
of segregated free lists).
Each individual free list will be organized as a **circular, doubly linked list**
(more information below).
The size classes are based on a power-of-two geometric sequence (1, 2, 4, 8, 16, ...),
according to the following scheme:
The first free list (at index 0) holds blocks of the minimum size `M`
(where `M = 32` for this assignment).
The second list (at index 1) holds blocks of size `(M, 2M]`.
The third list (at index 2) holds blocks of size `(2M, 4M]`.
The fourth list holds blocks whose size is in the interval `(4M, 8M]`.
The fifth list holds blocks whose size is in the interval `(8M, 16M]`,
and so on.  This pattern continues up to the interval `(128M, 256M]`,
and then the last list (at index `NUM_FREE_LISTS-1`; *i.e.* 9)
holds blocks of size greater than `256M`.
Allocation requests will be satisfied by searching the free lists in increasing
order of size class.

## Quick Lists

Besides the main free lists, you are also to use additional "quick lists" as a temporary
repository for recently freed small blocks.  There are a fixed number of quick lists,
which are organized as singly linked lists accessed in LIFO fashion.  Each quick lists
holds small blocks of one particular size.  The first quick list holds blocks of the
minimum size (32 bytes).
The second quick list holds blocks of the minimum size plus the alignment size
(32+8 = 40 bytes).  This third quick list holds blocks of size 32+8+8 = 48 bytes,
and so on.  When a small block is freed, it is inserted at the front of the corresponding
quick list, where it can quickly be found to satisfy a subsequent request for a block
of that same size.  The capacity of each quick list is limited; if insertion of a block
would exceed the capacity of the quick list, then the list is "flushed" and the existing
blocks in the quick list are removed from the quick list and added to the main free list,
after coalescing, if possible.

## Block Placement Policy

When allocating memory, use a **segregated fits policy**, modified by the use of quick lists
as follows.  When an allocation request is received, the quick list containing blocks of the
appropriate size is first checked to try to quickly obtain a block of exactly the right size.
If there is no quick list of that size (quick lists are only maintained for a fixed set of
the smallest block sizes), or if there is a quick list but it is empty, then the request will
be satisfied from the main free lists.

Satisfying a request from the main free lists is accomplished as follows:
First, the smallest size class that is sufficiently large to satisfy the request
is determined.  The free lists are then searched, starting from the list for the
determined size class and continuing in increasing order of size, until a nonempty
list is found.  The request is then satisfied by the first block in that list
that is sufficiently large; *i.e.* a **first-fit policy**
(discussed in **Chapter 9.9.7 Page 849**) is applied within each individual free list.

If there is no exact match for an allocation request in the quick lists, and there
is no block in the main free lists that is large enough to satisfy the allocation request,
`sf_mem_grow` should be called to extend the heap by an additional page of memory.
After coalescing this page with any free block that immediately precedes it, you should
attempt to use the resulting block of memory to satisfy the allocation request;
splitting it if it is too large and no splinter would result.  If the block of
memory is still not large enough, another call to `sf_mem_grow` should be made;
continuing to grow the heap until either a large enough block is obtained or the return
value from `sf_mem_grow` indicates that there is no more memory.

As discussed in the book, segregated free lists allow the allocator to approximate a
best-fit policy, with lower overhead than would be the case if an exact best-fit policy
were implemented.  The rationale for the use of quick lists is that when a small block
are freed, it is likely that there will soon be another allocation request for a block
of that same size.  By putting the block in a quick list, it can be re-used for such
a request without the overhead of coalescing and/or splitting that would be required
if the block were inserted back into the main pool.

## Splitting Blocks & Splinters

Your allocator must split blocks at allocation time to reduce the amount of
internal fragmentation.
Due to alignment and overhead constraints, there will be a minimum useful block size
that the allocator can support.  **For this assignment, pointers returned by the allocator
in response to allocation requests are required to be aligned to 8-byte boundaries**;
*i.e.* the pointers returned will be addresses that are multiples of 2^3.
The 8-byte alignment requirement implies that the minimum block size for your allocator
will be 32 bytes.  No "splinters" of smaller size than this are ever to be created.
If splitting a block to be allocated would result in a splinter, then the block should
not be split; rather, the block should be used as-is to satisfy the allocation request
(*i.e.*, you will "over-allocate" by issuing a block slightly larger than that required).

## Freeing a Block

When a block is freed, if it is a small block it is inserted at the front of the quick list of the
appropriate size.  Blocks in the quick lists are free, but the allocation bit remains set in
the header to prevent them from being coalesced with adjacent blocks.  In addition, there is a
separate "in quick list" bit in the block header that is set for blocks in the quick lists,
to allow them to be readily distinguished from blocks that are actually allocated.
To avoid arbitrary growth of the quick lists, the capacity of each is limited to `QUICK_LIST_MAX` blocks.
If an attempt is made to insert a block into a quick list that is already at capacity,
the quick list is *flushed* by removing each of the blocks it currently contains and adding
them back into the main free lists, coalescing them with any adjacent free blocks as described
below.  After flushing the quick list, the block currently being freed is inserted into the
now-empty list, leaving just one block in that list.

When a block is freed and added into the main free lists, an attempt should first be made to
**coalesce** the block with any free block that immediately precedes or follows it in the heap.
Once the block has been coalesced, it should be inserted at the **front** of the free
list for the appropriate size class (based on the size after coalescing).
The reason for performing coalescing is to combat the external fragmentation
that would otherwise result due to the splitting of blocks upon allocation.
Note that blocks inserted into quick lists are not immediately coalesced; they are only
coalesced at such later time as the quick list is flushed and the blocks are moved into the
main free lists.  This is an example of a "deferred coalescing" strategy.

## Block Headers & Footers

In this assignment, the header will be 4 words (i.e. 64 bits or 1 memory row). The header fields will be similar
to those in the textbook but you will maintain an extra bit for recording whether
or not the previous block is allocated, and an extra bit for recording whether or not
the block is currently in a quick list.
Each free block will also have a footer, which occupies the last memory row of the block.
The footer of a free block contains exactly the same information as the header.
In an allocated block, the footer is not present, and the space that it would otherwise
occupy may be used for payload.

**Block Header Format:**
```c
    +------------------------------------------------------------+--------+---------+---------+ <- header
    |                                       block_size           |in qklst|prv alloc|  alloc  |
    |                                  (3 LSB's implicitly 0)    | (0/1)  |  (0/1)  |  (0/1)  |
    |                                        (1 row)             | 1 bit  |  1 bit  |  1 bit  |
    +------------------------------------------------------------+--------+---------+---------+ <- (aligned)
```

- The `block_size` field gives the number of bytes for the **entire** block (including header/footer,
  payload, and padding).  It occupies the entire 64 bits of the block header or footer,
  except that the three least-significant bits of the block size, which would normally always
  be zero due to alignment requirements, are used to store additional information.
  This means that these bits have to be masked when retrieving the block size from the header and
  when the block size is stored in the header the previously existing values of these bits have
  to be preserved.
- The `alloc` bit (bit 0, mask 0x1) is a boolean. It is 1 if the block is allocated and 0 if it is free.
- The `prev_alloc` (bit 1, mask 0x2) is also a boolean. It is 1 if the **immediately preceding** block
  in the heap is allocated and 0 if it is not.
- The `in_qklst` (bit 2, mask 0x4) is also a boolean. It is 1 if the block is currently in a quick list,
  and 0 if it is not.  Note that if this bit is a 1, then the `alloc` bit will also be a 1.

Each free block will also have a footer, which occupies the last memory row of the block.
The footer of a free block (including a block in a quick list) must contain exactly the
same information as the header.  In an allocated block, the footer will not be present,
and the space that it would otherwise occupy may be used for payload.

# Allocation Functions

You will implement the four functions (`sf_malloc`, `sf_realloc`, `sf_free`,
and `sf_memalign`)
in the file `src/sfmm.c`.  The file `include/sfmm.h` contains the prototypes and
documentation shown below.

**Note:** Standard C library functions set `errno` when there is an error.
To avoid conflicts with these functions, your allocation functions will set `sf_errno`,
a variable declared as `extern` in `sfmm.h`.

```c
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
void *sf_malloc(size_t size);

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
void* sf_realloc(void *ptr, size_t size);

/*
 * Marks a dynamically allocated region as no longer in use.
 * Adds the newly freed block to the free list.
 *
 * @param ptr Address of memory returned by the function sf_malloc.
 *
 * If ptr is invalid, the function calls abort() to exit the program.
 */
void sf_free(void *ptr);

/*
 * Allocates a block of memory with a specified alignment.
 *
 * @param align The alignment required of the returned pointer.
 * @param size The number of bytes requested to be allocated.
 *
 * @return If align is not a power of two or is less than the default alignment (8),
 * then NULL is returned and sf_errno is set to EINVAL.
 * If size is 0, then NULL is returned without setting sf_errno.
 * Otherwise, if the allocation is successful a pointer to a valid region of memory
 * of the requested size and with the requested alignment is returned.
 * If the allocation is not successful, then NULL is returned and sf_errno is set
 * to ENOMEM.
 */
void *sf_memalign(size_t size, size_t align);
```

> :scream: <font color="red">Make sure these functions have these exact names
> and arguments. They must also appear in the correct file. If you do not name
> the functions correctly with the correct arguments, your program will not
> compile when we test it. **YOU WILL GET A ZERO**</font>

> Any functions other than `sf_malloc`, `sf_free`, `sf_realloc`, and `sf_memalign`
> **WILL NOT** be graded.

# Initialization Functions

In the `lib` directory, we have provided you with the `sfutil.o` object file.
When linked with your program, this object file allows you to access the
`sfutil` library, which contains the following functions:

```c
/*
 * @return The starting address of the heap for your allocator.
 */
void *sf_mem_start();

/*
 * @return The ending address of the heap for your allocator.
 */
void *sf_mem_end();

/*
 * This function increases the size of your heap by adding one page of
 * memory to the end.
 *
 * @return On success, this function returns a pointer to the start of the
 * additional page, which is the same as the value that would have been returned
 * by sf_mem_end() before the size increase.  On error, NULL is returned
 * and sf_errno is set to ENOMEM.
 */
void *sf_mem_grow();

/* The size of a page of memory returned by sf_mem_grow(). */
#define PAGE_SZ 4096
```

> :scream: As these functions are provided in a pre-built .o file, the source
> is not available to you. You will not be able to debug these using gdb.
> You must treat them as black boxes.

# sf_mem_grow

The function `sf_mem_grow` is to be invoked by `sf_malloc`, at the time of the
first allocation request to obtain an initial free block, and on subsequent allocations
when a large enough block to satisfy the request is not found.
For this assignment, your implementation **MUST ONLY** use `sf_mem_grow` to
extend the heap.  **DO NOT** use any system calls such as **brk** or **sbrk**
to do this.

Function `sf_mem_grow` returns memory to your allocator in pages.
Each page is 4096 bytes (4 KB) and there are a limited, small number of pages
available (the actual number may vary, so do not hard-code any particular limit
into your program).  Each call to `sf_mem_grow` extends the heap by one page and
returns a pointer to the new page (this will be the same pointer as would have
been obtained from `sf_mem_end` before the call to `sf_mem_grow`.

The `sf_mem_grow` function also keeps track of the starting and ending addresses
of the heap for you. You can get these addresses through the `sf_mem_start` and
`sf_mem_end` functions.

> :smile: A real allocator would typically use the **brk**/**sbrk** system calls
> calls for small memory allocations and the **mmap**/**munmap** system calls
> for large allocations.  To allow your program to use other functions provided by
> glibc, which rely on glibc's allocator (*i.e.* `malloc`), we have provided
> `sf_mem_grow` as a safe wrapper around **sbrk**.  This makes it so your heap and
> the one managed by glibc do not interfere with each other.

# Implementation Details

## Memory Row Size

The table below lists the sizes of data types (following Intel standard terminlogy)
on x86-64 Linux Mint:

| C declaration | Data type | x86-64 Size (Bytes) |
| :--------------: | :----------------: | :----------------------: |
| char  | Byte | 1 |
| short | Word | 2 |
| int   | Double word | 4 |
| long int | Quadword | 8 |
| unsigned long | Quadword | 8 |
| pointer | Quadword | 8 |
| float | Single precision | 4 |
| double | Double precision | 8 |
| long double | Extended precision | 16

In this assignment we will assume that each "memory row" is 8 bytes (64 bits) in size.
All pointers returned by your `sf_malloc` are to be 8-byte aligned; that is, they will be
addresses that are multiples of 8.  This requirement permits such pointers to be used to
store any of the basic machine data types up to 8 bytes in width in a "naturally aligned" fashion.
A value stored in memory is said to be *naturally aligned* if the address at which it
is stored is a multiple of the size of the value.  For example, an `int` value is
naturally aligned when stored at an address that is a multiple of 4.  A `double` value
is naturally aligned when stored at an address that is a multiple of 8.
Keeping values naturally aligned in memory is a hardware-imposed requirement for some
architectures, and improves the efficiency of memory access in other architectures.

## Block Header & Footer Fields

The various header and footer formats are specified in `include/sfmm.h`:

```

                                 Format of an allocated memory block
    +-----------------------------------------------------------------------------------------+
    |                                    64-bit-wide row                                      |
    +-----------------------------------------------------------------------------------------+

    +------------------------------------------------------------+--------+---------+---------+ <- header
    |                                       block_size           |in qklst|prv alloc|  alloc  |
    |                                  (3 LSB's implicitly 0)    |  (0)   |  (0/1)  |   (1)   |
    |                                        (1 row)             | 1 bit  |  1 bit  |  1 bit  |
    +------------------------------------------------------------+--------+---------+---------+ <- (aligned)
    |                                                                                         |
    |                                   Payload and Padding                                   |
    |                                        (N rows)                                         |
    |                                                                                         |
    |                                                                                         |
    +-----------------------------------------------------------------------------------------+

    NOTE: For an allocated block, there is no footer (it is used for payload).
```

```
                                Format of a memory block in a quick list
    +------------------------------------------------------------+--------+---------+---------+ <- header
    |                                       block_size           |in qklst|prv alloc|  alloc  |
    |                                  (3 LSB's implicitly 0)    |  (1)   |  (0/1)  |   (1)   |
    |                                        (1 row)             | 1 bit  |  1 bit  |  1 bit  |
    +------------------------------------------------------------+--------+---------+---------+ <- (aligned)
    |                                                                                         |
    |                                Pointer to next free block                               |
    |                                        (1 row)                                          |
    +-----------------------------------------------------------------------------------------+
    |                                                                                         |
    |                                         Unused                                          |
    |                                        (N rows)                                         |
    |                                                                                         |
    |                                                                                         |
    +-----------------------------------------------------------------------------------------+

    NOTE: For a block in a quick list, there is no footer.
```

```
                                     Format of a free memory block
    +------------------------------------------------------------+--------+---------+---------+ <- header
    |                                       block_size           |in qklst|prv alloc|  alloc  |
    |                                  (3 LSB's implicitly 0)    |  (0)   |  (0/1)  |   (0)   |
    |                                        (1 row)             | 1 bit  |  1 bit  |  1 bit  |
    +------------------------------------------------------------+--------+---------+---------+ <- (aligned)
    |                                                                                         |
    |                                Pointer to next free block                               |
    |                                        (1 row)                                          |
    +-----------------------------------------------------------------------------------------+
    |                                                                                         |
    |                               Pointer to previous free block                            |
    |                                        (1 row)                                          |
    +-----------------------------------------------------------------------------------------+
    |                                                                                         |
    |                                         Unused                                          |
    |                                        (N rows)                                         |
    |                                                                                         |
    |                                                                                         |
    +------------------------------------------------------------+--------+---------+---------+ <- footer
    |                                       block_size           |in qklst|prv alloc|  alloc  |
    |                                  (3 LSB's implicitly 0)    |  (0)   |  (0/1)  |    0    |
    |                                        (1 row)             | 1 bit  |  1 bit  |  1 bit  |
    +------------------------------------------------------------+--------+---------+---------+

    NOTE: For a free block, footer contents must always be identical to header contents.
```

The `sfmm.h` header file contains C structure definitions corresponding to the above diagrams:

```c
#define THIS_BLOCK_ALLOCATED  0x1
#define PREV_BLOCK_ALLOCATED  0x2
#define IN_QUICK_LIST         0x4

typedef size_t sf_header;
typedef size_t sf_footer;

/*
 * Structure of a block.
 */
typedef struct sf_block {
    sf_header header;
    union {
        /* A free block contains links to other blocks in a free list. */
        struct {
            struct sf_block *next;
            struct sf_block *prev;
        } links;
        /* An allocated block contains a payload (aligned), starting here. */
        char payload[0];   // Length varies according to block size.
    } body;
    // Depending on whether the block is allocated or free, and on whether footer optimization
    // is in use, a block might have a footer at the end, either overlapping the payload area
    // or in addition to it.  Since the payload size is not known at compile-time, we can't
    // declare the footer here as a field of the struct but instead have to compute its location
    // at run time.
} sf_block;
```

For `sf_block`, the `body` field is a `union`, which has been used to emphasize
the difference between the information contained in a free block and that contained
in an allocated block.  If the block is free, then its `body` has a `links` field,
which is a `struct` containing `next` and `prev` pointers.  If the block is
allocated, then its `body` does not have a `links` field, but rather has a `payload`,
which starts at the same address that the `links` field would have started if the
block were free.  The size of the `payload` is obviously not zero, but as it is
variable and only determined at run time, the `payload` field has been declared
to be an array of length 0 just to enable the use of `bp->body.payload` to obtain
a pointer to the payload area, if `bp` is a pointer to `sf_block`.

When a block is free, it must have a valid footer whose contents are identical to the
header contents.  We will use a "footer optimization" technique that permits a footer
to be omitted from allocated blocks; thereby making the space that would otherwise
be occupied by the footer available for use by payload.  The footer optimization
technique involves maintaining a bit in the header of each block that can be checked
to find out if the immediately preceding block is allocated or free.
If the preceding block is free, then its footer can be examined to find out its
size and then the size can be used to calculate the block's starting address for the
purpose of performing coalescing.
If the preceding block is **not** free, then it has no footer, but as we can only
coalesce with a free block there is no need for the information that we would have
found in the footer, anyway.

## Quick List and Free List Heads

In the file `include/sfmm.h`, you will see the following declarations:

```c
#define NUM_QUICK_LISTS 20  /* Number of quick lists. */
#define QUICK_LIST_MAX   5  /* Maximum number of blocks permitted on a single quick list. */

struct {
    int length;             // Number of blocks currently in the list.
    struct sf_block *first; // Pointer to first block in the list.
} sf_quick_lists[NUM_QUICK_LISTS];

#define NUM_FREE_LISTS 10
struct sf_block sf_free_list_heads[NUM_FREE_LISTS];
```

The array `sf_quick_lists` gives the heads of the quick lists, of which there
are a total of `NUM_QUICK_LISTS`.   At index 0 in this array is the head of the
quick list for blocks of the minimum block size `MIN_BLOCK_SIZE`, and for each
successive entry in the array the block size increases by the alignment granularity of 8.
The `sf_quick_lists` array therefore has space for the heads of `NUM_QUICK_LISTS`
quick lists, with sizes ranging from `MIN_BLOCK_SIZE` to
`MIN_BLOCK_SIZE + (NUM_QUICK_LISTS-1) * 8`, in increments of 8.
Besides giving a pointer `first` to the first block in a quick list,
each entry of `sf_quick_lists` contains a `length` field that is to be kept
updated with the current length of the list headed by that entry.
In contrast to the main free list, the quick lists are maintained as
**singly linked lists** accessed in LIFO fashion (*i.e.* like stacks).
When a block is in a quick list, only its `next` pointer is used.  Double linking
is not needed, because entries are only ever added or removed at the front of a list.
The capacity of each quick list is limited to a maximum of `QUICK_LIST_MAX` blocks.
Inserting into a quick list that is at capacity causes the quick list to be flushed
as discussed elsewhere.

The array `sf_free_list_heads` contains the heads of the main free lists,
of which there are a total of `NUM_FREE_LISTS`.
These lists are maintained as **circular, doubly linked lists**.
Each node in a free list contains a `next` pointer that points to the next
node in the list, and a `prev` pointer that points the previous node.
For each index `i` with `0 <= i < NUM_FREE_LISTS` the variable `sf_free_list_head[i]`
is a dummy, "sentinel" node, which is used to connect the beginning and the end of
the list at index `i`.  This sentinel node is always present and (aside from its `next`
and `free` pointers) does **not** contain any other data.  If the list is empty,
then the fields `sf_freelist_heads[i].body.links.next` and `sf_freelist_heads[i].body.links.prev`
both contain `&sf_freelist_heads[i]` (*i.e.* the sentinel node points back to itself).
If the list is nonempty, then `sf_freelist_heads[i].body.links.next` points to the
first node in the list and `sf_freelist_heads[i].body.links.prev` points to the
last node in the list.
Inserting into and deleting from a circular doubly linked list is done
in the usual way, except that, owing to the use of the sentinel, there
are no edge cases for inserting or removing at the beginning or the end
of the list.
If you need a further introduction to this data structure, you can readily
find information on it by googling ("circular doubly linked lists with sentinel").

## Overall Structure of the Heap

The overall structure of the allocatable area of your heap will be a sequence of allocated
and free blocks.
Your heap should also contain a prologue and epilogue (as described in the book, **page 855**)
to arrange for the proper block alignment and to avoid edge cases when coalescing blocks.
The overall organization of the heap is as shown below:

```c
                                         Format of the heap
    +-----------------------------------------------------------------------------------------+
    |                                    64-bit-wide row                                      |
    +-----------------------------------------------------------------------------------------+

    +-----------------------------------------------------------------------------------------+ <- heap start
    |                                                                                         |
    |                                        Padding                                          |
    |                                    (0 or more rows)                                     |
    +------------------------------------------------------------+--------+---------+---------+ <- header
    |                                       block_size           |in qklst|prv alloc|  alloc  |
    |                                  (3 LSB's implicitly 0)    |  (0/1) |  (0/1)  |  (1)    | prologue
    |                                        (1 row)             | 1 bit  |  1 bit  |  1 bit  |
    +------------------------------------------------------------+--------+---------+---------+ <- (aligned)
    |                                                                                         |
    |                                   Payload and Padding                                   |
    |                                        (N rows)                                         |
    |                                                                                         |
    |                                                                                         |
    +--------------------------------------------+------------------------+---------+---------+
    |                                                                                         |
    |                                                                                         |
    |                                                                                         |
    |                                                                                         |
    |                             Additional allocated and free blocks                        |
    |                                                                                         |
    |                                                                                         |
    |                                                                                         |
    +-----------------------------------------------------------------------------------------+
    |                                                                                         |
    |                       Unused (will become header when heap grows)                       |
    |                                        (1 row)                                          |
    +-----------------------------------------------------------------------------------------+ <- heap end
                                                                                                   (aligned)
```

The heap begins with unused "padding", so that the header of each block will start
`sizeof(sf_header)` bytes before an alignment boundary.
The first block of the heap is the "prologue", which is an allocated block of minimum
size with an unused payload area.

At the end of the heap is an "epilogue", which consists only of an allocated header,
with block size set to 0.
The prologue and epilogue are never used to satisfy allocation requests and they
are never freed.
Whenever the heap is extended, a new epilogue is created at the end of the
newly added region and the old epilogue becomes the header of the new block.
This is as described in the book.

We do not make any separate C structure definitions for the prologue and epilogue.
They can be manipulated using the existing `sf_block` structure, though care must be taken
not to access fields that are not valid for these special blocks
(*i.e.* anything other than `header` for the epilogue).

As your heap is initially empty, at the time of the first call to `sf_malloc`
you will need to make one call to `sf_mem_grow` to obtain a page of memory
within which to set up the prologue and initial epilogue.
The remainder of the memory in this first page should then be inserted into
the free list as a single block.
