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
#define INIT_SIZE  (1<<6)
#define MAX(x, y)  ((x) > (y) ? (x) : (y))
#define MIN(x, y)  ((x) < (y) ? (x) : (y))
void *first_free = NULL;

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
  printf("insert_node called\n - Block size: %ld\n", size);
  // Don't insert a nonexistent block
  if (ptr == NULL)
    return;
  // If there are existing free blocks, adjust the list
  if (first_free != NULL) {
    printf(" - Non-empty list, prepending.\n");
    F_SET_PTR(F_PREV_PTR(first_free), ptr);
    F_SET_PTR(F_NEXT_PTR(ptr), first_free);
  }
  // Otherwise, start populating the list
  else {
    printf(" - Empty list, prepending.\n");
    F_SET_PTR(F_NEXT_PTR(ptr), NULL);
  }
  F_SET_PTR(F_PREV_PTR(ptr), NULL);
  first_free = ptr;
  return;
}

/*
 * Deletes a node from the explicit free list (remove linked-list pointers)
 * Case 1: Has a previous and a next free block
 * Case 2: Has a previous free block only
 * Case 3: Has a next free block only
 * Case 4: No previous or next free blocks
 */
static void delete_node(void *ptr) {
  printf("delete_node called\n - Block size: %ld\n", GET_SIZE(HDRP(ptr)));
  // Don't delete a nonexistent node
  if (ptr == NULL)
    return;
  if (F_PREV(ptr) != NULL) {
    if (F_NEXT(ptr) != NULL) {  // Case 1
      printf(" - Case 1, middle of the list\n");
      F_SET_PTR(F_NEXT_PTR(F_PREV(ptr)), F_NEXT(ptr));
      F_SET_PTR(F_PREV_PTR(F_NEXT(ptr)), F_PREV(ptr));
    }
    else {                      // Case 2
      printf(" - Case 2, bottom of the list\n");
      F_SET_PTR(F_NEXT_PTR(F_PREV(ptr)), NULL);
    }
  }
  else {
    if (F_NEXT(ptr) != NULL) {    // Case 3
      printf(" - Case 3, top of the list\n");
      F_SET_PTR(F_PREV_PTR(F_NEXT(ptr)), NULL);
      first_free = F_NEXT(ptr);
    }
    else {                      // Case 4
      printf(" - Case 4, only item in list\n");
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
  printf("set_allocated called\n");
  // Don't allocate a nonexistent block
  if (ptr == NULL)
    return NULL;
  size_t free_size = GET_SIZE(HDRP(ptr));
  size_t remain = free_size - asize;
  printf(" - Block size: %ld, Needed: %ld, Remainder: %ld\n",
         free_size, asize, remain);

  // Remove the block from the free list
  delete_node(ptr);

  if (remain <= OVERHEAD * 2) {  // Remainder too small for splitting
    printf(" - Too small for splitting, allocate whole block.\n");
    PUT(HDRP(ptr), PACK(free_size, 1));
    PUT(FTRP(ptr), PACK(free_size, 1));
  }
  else {  // Split block and add the remainder back to the free list
    printf(" - Large enough for splitting.\n");
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
  printf("extend called\n - Requesting %ld bytes\n", asize);
  void *ptr;

  if ((long)(ptr = mem_map(asize)) == -1)
    return NULL;

  printf(" - Base address of new chunk: %p\n", ptr);

  // After 8 bytes of padding, set sentinel block of size OVERHEAD as allocated
  PUT(HDRP(ptr+OVERHEAD), PACK(OVERHEAD, 1));
  PUT(FTRP(ptr+OVERHEAD), PACK(OVERHEAD, 1));
  // Add terminator at end of page
  PUT(HDRP(ptr+asize), PACK(0, 1));
  // Add a free block spanning the middle of the page
  ptr += OVERHEAD * 2;
  asize -= OVERHEAD * 2;
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
static void *check_chunk(void *ptr) {
  printf("check_chunk called\n");
  if (ptr == NULL)
    return NULL;
  size_t prev_size = GET_SIZE(HDRP(PREV_BLKP(ptr)));
  size_t next_size = GET_SIZE(HDRP(NEXT_BLKP(ptr)));
  printf(" - Previous block: %ld bytes, Next block: %ld bytes\n",
         prev_size, next_size);

  if (prev_size == OVERHEAD && next_size == 0) {  // Free the chunk
    delete_node(ptr);
    size_t size = GET_SIZE(HDRP(ptr)) + OVERHEAD * 2;
    ptr -= OVERHEAD * 2;
    printf(" - Freeing a chunk of %ld bytes at %p\n", size, ptr);
    mem_unmap(ptr, size);
    ptr = NULL;
  }

  return ptr;
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
  printf("coalesce called\n");
  if (ptr == NULL)
    return NULL;
  size_t prev_alloc = GET_ALLOC(HDRP(PREV_BLKP(ptr)));
  size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(ptr)));
  size_t size = GET_SIZE(HDRP(ptr));
  printf(" - Original size: %ld bytes\n", size);

  if (prev_alloc && next_alloc) {        // Case 1 (return as-is)
    printf(" - Case 1, no adjacent free blocks.\n");
    return ptr;
  }
  else if (prev_alloc && !next_alloc) {  // Case 2 (coalesce with right)
    printf(" - Case 2, right block is free.\n");
    delete_node(ptr);
    delete_node(NEXT_BLKP(ptr));
    size += GET_SIZE(HDRP(NEXT_BLKP(ptr)));
    PUT(HDRP(ptr), PACK(size, 0));
    PUT(FTRP(ptr), PACK(size, 0));
  }
  else if (!prev_alloc && next_alloc) {  // Case 3 (coalesce with left)
    printf(" - Case 3, left block is free.\n");
    delete_node(ptr);
    delete_node(PREV_BLKP(ptr));
    size += GET_SIZE(HDRP(PREV_BLKP(ptr)));
    PUT(FTRP(ptr), PACK(size, 0));
    PUT(HDRP(PREV_BLKP(ptr)), PACK(size, 0));
    ptr = PREV_BLKP(ptr);
  }
  else {                                 // Case 4 (coalesce with both sides)
    printf(" - Case 4, both right and left blocks are free.\n");
    delete_node(ptr);
    delete_node(PREV_BLKP(ptr));
    delete_node(NEXT_BLKP(ptr));
    size += GET_SIZE(HDRP(PREV_BLKP(ptr))) + GET_SIZE(HDRP(NEXT_BLKP(ptr)));
    PUT(HDRP(PREV_BLKP(ptr)), PACK(size, 0));
    PUT(FTRP(PREV_BLKP(ptr)), PACK(size, 0));
    ptr = PREV_BLKP(ptr);
  }

  printf(" - New size: %ld bytes\n", size);

  insert_node(ptr, size);
  return ptr;
}
/********** End of helper functions **********/

/* =
 * mm_init - initialize the malloc package.
 */
int mm_init(void)
{
  printf("\nmm_init called\n");
  first_free = NULL;
  return 0;
}

/*
 * mm_malloc - Allocate a block by using bytes from current_avail,
 *     grabbing a new page if necessary.
 */
void *mm_malloc(size_t size)
{
  printf("\nmm_malloc called\n - Requesting %ld bytes\n", size);
  // Ignore size 0 cases
  if (size == 0)
    return NULL;

  // Align block size
  size_t asize = ALIGN(size + OVERHEAD);
  void *ptr = first_free;
  printf(" - Aligned size: %ld bytes\n", asize);

  // Search for a free block of adequate size
  while (ptr != NULL) {
    size_t block_size = GET_SIZE(HDRP(ptr));
    if (asize > block_size) {  // Too small, check next free block
      printf(" - Free block of size %ld, too small\n", block_size);
      ptr = F_NEXT(ptr);
      continue;
    }
    else {  // Size is adequate. Proceed to allocation
      printf(" - Free block of size %ld, big enough\n", block_size);
      break;
    }
  }

  // If a free block that fits isn't found, extend the heap
  if (ptr == NULL) {
    printf(" - No free blocks of adequate size.\n");
    size_t extendsize = PAGE_ALIGN(asize);
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
  printf("\nmm_free called\n");
  if (ptr == NULL)
    return;
  // Set the header allocated bit to 0
  block_header* hdr = (block_header *)HDRP(ptr);
  size_t size = GET_SIZE(hdr);
  PUT(hdr, PACK(size, 0));

  // Similar for the footer
  block_footer* ftr = (block_footer *)FTRP(ptr);
  PUT(ftr, PACK(size, 0));

  printf(" - Freeing block of size %ld\n", size);

  // Coalesce, if applicable
  insert_node(ptr, size);
  ptr = coalesce(ptr);
  ptr = check_chunk(ptr);

  return;
}
