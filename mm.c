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
#define OVERHEAD  (sizeof(block_header) + sizeof(block_footer))
#define LISTLIMIT 20 // Maximum number of segregated free lists
#define MAX(x, y)      ((x) > (y) ? (x) : (y))
#define MIN(x, y)      ((x) < (y) ? (x) : (y))
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
#define NEXT_BLKP(ptr)  ((char *)(ptr) + GET_SIZE(HDRP(ptr))
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
 * Set a block to allocated
 * Update block headers/footers as needed
 * Update free list if applicable
 * Split block if applicable
 */
static void set_allocated(void *b, size_t size) {
  return;
}

/*
 * Request more memory by calling mem_map
 * Initialize the new chunk of memory as applicable
 *  - 8 bytes of padding needed at the start of every page
 *  - full_size = ALIGN(size + OVERHEAD)
 * Update free list if applicable
 */
static void extend(size_t s) {
  return;
}

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
 * Coalesce a free block if applicable
 * Returns pointer to new coalesced block
 */
static void* coalesce(void *bp) {
  return NULL;
}
/********** End of helper functions **********/

void *current_avail = NULL;
int current_avail_size = 0;

/* =
 * mm_init - initialize the malloc package.
 */
int mm_init(void)
{
  current_avail = NULL;
  current_avail_size = 0;

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

  if (current_avail_size < newsize) {
    current_avail_size = PAGE_ALIGN(newsize);
    current_avail = mem_map(current_avail_size);
    if (current_avail == NULL)
      return NULL;
  }

  p = current_avail;
  current_avail += newsize;
  current_avail_size -= newsize;

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

  return;
}
