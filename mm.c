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
#define LISTLIMIT 20 // Maximum number of segregated free lists
#define MAX(x, y)  ((x) > (y) ? (x) : (y))
#define MIN(x, y)  ((x) < (y) ? (x) : (y))
void *segregated_free_lists[LISTLIMIT];

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
#define PRED(ptr)      (*(char **)(ptr))
#define SUCC(ptr)      (*(char **)(SUCC_PTR(ptr)))
#define PRED_PTR(ptr)  ((char *)(ptr))
#define SUCC_PTR(ptr)  ((char *)(ptr) + sizeof(block_header))
#define SET_PTR(p, ptr)  (*(size_t *)(p) = (size_t)(ptr))
/********** End of my macros and variables **********/


/********** Helper functions **********/

/*
 * Inserts a free block into the segregated free lists
 */
static void insert_node(void *ptr, size_t size) {
  int list = 0;
  void *search_ptr = ptr;
  void *insert_ptr = NULL;

  // Select segregated list based on size
  while ((list < LISTLIMIT - 1) && (size > 1)) {
    size >>= 1;
    list++;
  }

  // Keep size-ascending order and search
  search_ptr = segregated_free_lists[list];
  while ((search_ptr != NULL) && (size > GET_SIZE(HDRP(search_ptr)))) {
    insert_ptr = search_ptr;
    search_ptr = PRED(search_ptr);
  }

  // Set predecessor and successor
  if (search_ptr != NULL) {
    if (insert_ptr != NULL) {
      SET_PTR(PRED_PTR(ptr), search_ptr);
      SET_PTR(SUCC_PTR(search_ptr), ptr);
      SET_PTR(SUCC_PTR(ptr), insert_ptr);
      SET_PTR(PRED_PTR(insert_ptr), ptr);
    }
    else {
      SET_PTR(PRED_PTR(ptr), search_ptr);
      SET_PTR(SUCC_PTR(search_ptr), ptr);
      SET_PTR(SUCC_PTR(ptr), NULL);
      segregated_free_lists[list] = ptr;
    }
  }
  else {
    if (insert_ptr != NULL) {
      SET_PTR(PRED_PTR(ptr), NULL);
      SET_PTR(SUCC_PTR(ptr), insert_ptr);
      SET_PTR(PRED_PTR(insert_ptr), ptr);
    }
    else {
      SET_PTR(PRED_PTR(ptr), NULL);
      SET_PTR(SUCC_PTR(ptr), NULL);
      segregated_free_lists[list] = ptr;
    }
  }

  return;
}

/*
 * Deletes a node from the segregated free lists
 */
static void delete_node(void *ptr) {
  int list = 0;
  size_t size = GET_SIZE(HDRP(ptr));

  // Select the segregated list based on size
  while ((list < LISTLIMIT - 1) && (size > 1)) {
    size >>= 1;
    list++;
  }

  if (PRED(ptr) != NULL) {
    if (SUCC(ptr) != NULL) {
      SET_PTR(SUCC_PTR(PRED(ptr)), SUCC(ptr));
      SET_PTR(PRED_PTR(SUCC(ptr)), PRED(ptr));
    }
    else {
      SET_PTR(SUCC_PTR(PRED(ptr)), NULL);
      segregated_free_lists[list] = PRED(ptr);
    }
  }
  else {
    if (SUCC(ptr) != NULL) {
      SET_PTR(PRED_PTR(SUCC(ptr)), NULL);
    }
    else {
      segregated_free_lists[list] = NULL;
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
static void set_allocated(void *ptr, size_t size) {
  size_t asize = ALIGN(size + OVERHEAD);
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
}

/*
 * Request more memory by calling mem_map
 * Initialize the new chunk of memory as applicable
 *  - 8 bytes of padding needed at the start of every page
 *  - Use a sentinel block (header+footer) at the start of every page
 *  - Add a terminator block (header) at the end of every page
 * Update free list if applicable
 */
static void extend(size_t s) {
  void *ptr;
  size_t asize = ALIGN(s + OVERHEAD);
  asize = MAX(asize, mem_pagesize());

  if ((ptr = mem_map(asize)) == (void *)-1)
    return;

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
  return;
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
static void* coalesce(void *ptr) {
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
  int list;
  for (list = 0; list < LISTLIMIT; list++) {
    segregated_free_lists[list] = NULL;
  }

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
  size_t newsize = ALIGN(size);
  void *p;

  return p;
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
