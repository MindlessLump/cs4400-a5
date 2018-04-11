/*
 * mm-naive.c - The least memory-efficient malloc package.
 *
 * Erik Martin (00915261)
 *
 * Each block has a header and footer. The header contains size, relocation
 * info, and allocation info. The footer contains size and allocation info.
 * Free blocks are tagged to a segregated list, so all free blocks contain
 * pointers to the predecessor and successor blocks in that list.
 *
 */
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>

#include "mm.h"
#include "memlib.h"

/* always use 16-byte alignment */
#define ALIGNMENT 16

/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(size) (((size) + (ALIGNMENT-1)) & ~(ALIGNMENT-1))

/* rounds up to the nearest multiple of mem_pagesize() */
#define PAGE_ALIGN(size) (((size) + (mem_pagesize()-1)) & ~(mem_pagesize()-1))

/********** My macros and variables **********/
typedef size_t block_header;
typedef size_t block_footer;
#define OVERHEAD   (sizeof(block_header) + sizeof(block_footer))
#define MAX(x, y)  ((x) > (y) ? (x) : (y))
#define MIN(x, y)  ((x) < (y) ? (x) : (y))
void *first_free;

// Combine a size and alloc bit
#define PACK(size, alloc)  ((size) | (alloc))

// Get address of header/footer of ptr block
#define HDRP(ptr)  ((char *)(ptr) - sizeof(block_header))
#define FTRP(ptr)  ((char *)(ptr) + GET_SIZE(HDRP(ptr)) - OVERHEAD)

// Given a pointer to a header, get or set its value
#define GET(ptr)       (*(size_t *)(ptr))
#define PUT(ptr, val)  (*(size_t *)(ptr) = (val))

// Get size and allocation bit of ptr block
#define GET_SIZE(ptr)   (GET(ptr) & ~0xF)
#define GET_ALLOC(ptr)  (GET(ptr) & 0x1)

// Address of adjacent blocks
#define NEXT_BLKP(ptr)  ((char *)(ptr) + GET_SIZE(HDRP(ptr)))
#define PREV_BLKP(ptr)  ((char *)(ptr) - GET_SIZE((char *)(ptr) - OVERHEAD))

// Address of free block's predecessor and successor entries
#define F_PREV(ptr)      (*(char **)(ptr))
#define F_NEXT(ptr)      (*(char **)(F_NEXT_PTR(ptr)))
#define F_PREV_PTR(ptr)  ((char *)(ptr))
#define F_NEXT_PTR(ptr)  ((char *)(ptr) + sizeof(block_header))
#define F_SET_PTR(p, ptr)  (*(size_t *)(p) = (size_t)(ptr))
/********** End of my macros and variables **********/


/********** Helper functions **********/

/*
 * Inserts a free block into the explicit free list (prepend)
 */
static void insert_node(void *ptr, size_t size) {
  F_SET_PTR(F_PREV_PTR(first_free), ptr);
  F_SET_PTR(F_NEXT_PTR(ptr), first_free);
  F_SET_PTR(F_PREV_PTR(ptr), NULL);
  first_free = ptr;
  return;
}

/*
 * Deletes a node from the segregated free lists
 * Case 1: Has a previous and a next free block
 * Case 2: Has a previous free block only
 * Case 3: Has a next free block only
 * Case 4: No previous or next free blocks
 */
static void delete_node(void *ptr) {
  if (F_PREV(ptr) != NULL) {
    if (F_NEXT(ptr) != NULL) {  // Case 1
      F_SET_PTR(F_NEXT_PTR(F_PREV(ptr)), F_NEXT(ptr));
      F_SET_PTR(F_PREV_PTR(F_NEXT(ptr)), F_PREV(ptr));
    }
    else {                      // Case 2
      F_SET_PTR(F_NEXT_PTR(F_PREV(ptr)), NULL);
    }
  }
  else {
    if (F_NEXT(ptr) != NULL) {    // Case 3
      F_SET_PTR(F_PREV_PTR(F_NEXT(ptr)), NULL);
      first_free = F_NEXT(ptr);
    }
    else {                      // Case 4
      first_free = NULL;
    }
  }

  return;
}

/*
 * Set a block to allocated
 * Update block headers/footers as needed
 * Update free list if applicable
 * Split block if applicable
 */
static void *set_allocated(void *ptr, size_t asize) {
  size_t free_size = GET_SIZE(HDRP(ptr));
  size_t remain = free_size - asize;

  delete_node(ptr);

  if (remain <= OVERHEAD * 2) {  // Remainder too small to split
    PUT(HDRP(ptr), PACK(free_size, 1));
    PUT(FTRP(ptr), PACK(free_size, 1));
  }
  else {  // Split block
    PUT(HDRP(ptr), PACK(asize, 1));
    PUT(FTRP(ptr), PACK(asize, 1));
    PUT(HDRP(NEXT_BLKP(ptr)), PACK(remain, 0));
    PUT(FTRP(NEXT_BLKP(ptr)), PACK(remain, 0));
    insert_node(NEXT_BLKP(ptr), remain);
  }

  return ptr;
}

/*
 * Request more memory by calling mem_map
 * Initialize the new chunk of memory as applicable
 *  - 8 bytes of padding needed at the start of every page
 *  - Use a sentinel block (header+footer) at the start of every page
 *  - Add a terminator block (header) at the end of every page
 * Update free list if applicable
 */
static void *extend(size_t asize) {
  void *ptr;

  if ((ptr = mem_map(asize)) == (void *)-1)
    return NULL;

  // After 8 bytes of padding, set sentinel block of size OVERHEAD as allocated
  PUT(HDRP(ptr+OVERHEAD), PACK(OVERHEAD, 1));
  PUT(FTRP(ptr+OVERHEAD), PACK(OVERHEAD, 1));
  // Add terminator at end of page
  PUT(HDRP(ptr+asize), PACK(0, 1));
  // Add a free block spanning the middle of the page
  ptr += OVERHEAD << 1;
  asize -= OVERHEAD + sizeof(block_header);
  PUT(HDRP(ptr), PACK(asize, 0));
  PUT(FTRP(ptr), PACK(asize, 0));

  insert_node(ptr, asize);
  return ptr;
}

/*
 * Check to see if a whole chunk is now free.
 * Should be called after coalesce() does its work.
 * The chunk is empty if the left and right neighbors are sentinels.
 */
static void check_chunk(void *ptr) {
  size_t prev_size = GET_SIZE(HDRP(PREV_BLKP(ptr)));
  size_t next_size = GET_SIZE(HDRP(NEXT_BLKP(ptr)));

  if (prev_size == OVERHEAD && next_size == 0) {  // Free the chunk
    size_t size = GET_SIZE(HDRP(ptr)) + OVERHEAD * 2;
    ptr -= OVERHEAD * 2;
    delete_node(ptr);
    mem_unmap(ptr, size);
  }
}

/*
 * Coalesce a free block if applicable
 * Returns pointer to new coalesced block
 * Case 1: No adjacent free blocks.
 * Case 2: Next block is free.
 * Case 3: Previous block is free.
 * Case 4: Previous and next blocks are free.
 */
static void *coalesce(void *ptr) {
  size_t prev_alloc = GET_ALLOC(HDRP(PREV_BLKP(ptr)));
  size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(ptr)));
  size_t size = GET_SIZE(HDRP(ptr));

  if (prev_alloc && next_alloc) {        // Case 1 (return as-is)
    return ptr;
  }
  else if (prev_alloc && !next_alloc) {  // Case 2 (coalesce with right)
    delete_node(ptr);
    delete_node(NEXT_BLKP(ptr));
    size += GET_SIZE(HDRP(NEXT_BLKP(ptr)));
    PUT(HDRP(ptr), PACK(size, 0));
    PUT(FTRP(ptr), PACK(size, 0));
  }
  else if (!prev_alloc && next_alloc) {  // Case 3 (coalesce with left)
    delete_node(ptr);
    delete_node(PREV_BLKP(ptr));
    size += GET_SIZE(HDRP(PREV_BLKP(ptr)));
    PUT(FTRP(ptr), PACK(size, 0));
    PUT(HDRP(PREV_BLKP(ptr)), PACK(size, 0));
    ptr = PREV_BLKP(ptr);
  }
  else {                                 // Case 4 (coalesce with both sides)
    delete_node(ptr);
    delete_node(PREV_BLKP(ptr));
    delete_node(NEXT_BLKP(ptr));
    size += GET_SIZE(HDRP(PREV_BLKP(ptr))) + GET_SIZE(HDRP(NEXT_BLKP(ptr)));
    PUT(HDRP(PREV_BLKP(ptr)), PACK(size, 0));
    PUT(FTRP(NEXT_BLKP(ptr)), PACK(size, 0));
    ptr = PREV_BLKP(ptr);
  }

  insert_node(ptr, size);
  return ptr;
}
/********** End of helper functions **********/

/* =
 * mm_init - initialize the malloc package.
 */
int mm_init(void)
{
  // Initialize segregated free lists
  first_free = NULL;

  // Initialize an empty heap
  char *heap_start;
  if ((long)(heap_start = mem_map(mem_pagesize())) == -1)
    return -1;

  // After 8 bytes of padding, set sentinel block of size OVERHEAD as allocated
  PUT(HDRP(heap_start+OVERHEAD), PACK(OVERHEAD, 1));
  PUT(FTRP(heap_start+OVERHEAD), PACK(OVERHEAD, 1));
  // Add terminator at end of page
  PUT(HDRP(heap_start+mem_pagesize()), PACK(0, 1));
  // Add a free block spanning the middle of the page
  heap_start += OVERHEAD << 1;
  size_t size = mem_pagesize() - (OVERHEAD + sizeof(block_header));
  PUT(HDRP(heap_start), PACK(mem_pagesize(), 0));
  PUT(FTRP(heap_start), PACK(mem_pagesize(), 0));

  insert_node(heap_start, size);

  return 0;
}

/*
 * mm_malloc - Allocate a block by using bytes from current_avail,
 *     grabbing a new page if necessary.
 */
void *mm_malloc(size_t size)
{
  // Ignore size 0 cases
  if (size == 0)
    return NULL;

  // Align block size
  size_t asize = ALIGN(size + OVERHEAD);
  void *ptr = first_free;

  // Search for a free block of adequate size
  while (ptr != NULL) {
    if (asize > GET_SIZE(HDRP(ptr))) {  // Too small, check next free block
      ptr = F_NEXT(ptr);
      continue;
    }
    else {  // Size is adequate. Proceed to allocation
      break;
    }
  }

  // If a free block that fits isn't found, extend the heap
  if (ptr == NULL) {
    size_t extendsize = MAX(asize, mem_pagesize());
    if ((ptr = extend(extendsize)) == NULL)
      return NULL;
  }

  // Allocate the block
  ptr = set_allocated(ptr, asize);

  return ptr;
}

/*
 * mm_free - Frees the block pointed to by ptr, coalescing if applicable.
 * Returns nothing.
 */
void mm_free(void *ptr)
{
  // Set the header allocated bit to 0
  block_header* hdr = (block_header *)HDRP(ptr);
  size_t size = GET_SIZE(hdr);
  PUT(hdr, PACK(size, 0));

  // Similar for the footer
  block_footer* ftr = (block_footer *)FTRP(ptr);
  PUT(ftr, PACK(size, 0));

  // Coalesce, if applicable
  insert_node(ptr, size);
  coalesce(ptr);
  check_chunk(ptr);

  return;
}
