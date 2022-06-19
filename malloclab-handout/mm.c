/*
 * mm-naive.c - The fastest, least memory-efficient malloc package.
 *
 * In this naive approach, a block is allocated by simply incrementing
 * the brk pointer.  A block is pure payload. There are no headers or
 * footers.  Blocks are never coalesced or reused. Realloc is
 * implemented directly using mm_malloc and mm_free.
 *
 * NOTE TO STUDENTS: Replace this header comment with your own header
 * comment that gives a high level description of your solution.
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "memlib.h"
#include "mm.h"

/*********************************************************
 * NOTE TO STUDENTS: Before you do anything else, please
 * provide your team information in the following struct.
 ********************************************************/
team_t team = {
        /* Team name */
        "ateam",
        /* First member's full name */
        "Harry Bovik",
        /* First member's email address */
        "bovik@cs.cmu.edu",
        /* Second member's full name (leave blank if none) */
        "",
        /* Second member's email address (leave blank if none) */
        ""};

/* single word (4) or double word (8) alignment */
#define ALIGNMENT 8

/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(size) (((size) + (ALIGNMENT - 1)) & ~0x7)

#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))

#define WSIZE 4
#define DSIZE 8
#define CHUNKSIZE (1 << 12)
#define PACK(size, alloc) ((size) | (alloc))
#define GET(p) (*(u_int64_t *) (p))
#define PUT(p, val) (*(u_int64_t *) (p) = (u_int64_t) (val))
#define GET_SIZE(p) (GET(p) & ~0x7)
#define GET_ALLOC(p) (GET(p) & 0x1)
#define FTR_P(p) (p + GET_SIZE(p) / ALIGNMENT)
#define NEXT_BLOCK(p) (p + GET_SIZE(p) / ALIGNMENT + 1)

/*
 * mm_init - initialize the malloc package.
 */
int mm_init(void) {
	u_int64_t *start = (u_int64_t *) mem_sbrk(ALIGNMENT * 6);
	(*start)         = ALIGNMENT | 1;
	(*(start + 1))   = ALIGNMENT | 1;
	(*(start + 2))   = (ALIGNMENT * 2) | 0;
	(*(start + 4))   = (ALIGNMENT * 2) | 0;
	(*(start + 5))   = 0 | 1;
	return 0;
}

/*
 * mm_malloc - Allocate a block by incrementing the brk pointer.
 *     Always allocate a block whose size is a multiple of the alignment.
 */
void *mm_malloc(size_t size) {
	//printf("malloc called\n");
	size = ALIGN(size) + ALIGNMENT;
	//printf("size = %zu\n", size);
	u_int64_t *p = (u_int64_t *) mem_heap_lo();
	while(p < (u_int64_t *) ((char *) mem_heap_hi() - 7)) {
		//printf("p = %p\n",p);
		if(GET_ALLOC(p)) {                               // 该块已经被分配
			p = NEXT_BLOCK(p);                           // 前进到下一块
		} else {                                         // 该块是空闲块
			if(GET_SIZE(p) >= size) {                    // 该块的空间足够
				if(GET_SIZE(p) - size >= 3 * ALIGNMENT) {// 可以分割
					u_int64_t *this_ftr = p + size / ALIGNMENT;
					u_int64_t *next_hdr = p + size / ALIGNMENT + 1;
					u_int64_t *next_ftr = FTR_P(this_ftr);
					PUT(next_hdr, PACK(GET_SIZE(p) - size - 1, 0));
					PUT(next_ftr, PACK(GET_SIZE(p) - size - 1, 0));
					PUT(p, PACK(size, 1));
					PUT(this_ftr, PACK(size, 1));
					//printf("1 malloc return %p\n", p + 1);
					return (void *) (p + 1);
				} else {//不能分割
					PUT(p, PACK(GET_SIZE(p), 1));
					PUT(FTR_P(p), PACK(GET_SIZE(p), 1));
					//printf("2 malloc return %p\n", p + 1);
					return (void *) (p + 1);
				}
			} else {// 该块的空间不够
				p = NEXT_BLOCK(p);
			}
		}
	}
	//printf("malloc: current end of heap: %p\n", mem_heap_hi());
	void *sbrk_p = mem_sbrk(size + ALIGNMENT);
	//printf("malloc: sbrk_p: %p\n", sbrk_p);
	p = ((u_int64_t *) sbrk_p) - 1;
	//printf("malloc: p: %p\n", p);
	//printf("malloc: tail content: %p\n", *p);
	PUT(p, PACK(size, 1));
	//printf("malloc: header content: %p\n", (*(u_int64_t *) p));
	//printf("malloc: footer: %p\n", FTR_P(p));
	//printf("malloc: next block: %p\n", NEXT_BLOCK(p));
	PUT(FTR_P(p), PACK(size, 1));
	PUT(NEXT_BLOCK(p), PACK(0, 1));
	//printf("malloc: current end of heap: %p\n", mem_heap_hi());
	//printf("malloc: new tail: %p\n", NEXT_BLOCK(p));
	//printf("malloc: new tail content: %p\n", *NEXT_BLOCK(p));
	//printf("3 malloc return %p\n", p + 1);
	return (void *) (p + 1);
}

/*
 * mm_free - Freeing a block does nothing.
 */
void mm_free(void *ptr) {
	u_int64_t *p = ((u_int64_t *) ptr) - 1;
	//printf("free called with %p\n", ptr);
	if(!GET_ALLOC(p)) {
		fprintf(stderr, "free: not allocated\n");
		return;
	}
	u_int64_t new_size;
	PUT(p, PACK(GET_SIZE(p), 0));
	PUT(FTR_P(p), PACK(GET_SIZE(p), 0));
	u_int64_t *p_next = NEXT_BLOCK(p);
	if(p_next < (u_int64_t *) ((char *) mem_heap_hi() - 7) && !GET_ALLOC(p_next)) {
		//printf("free: next block is empty\n");
		new_size = GET_SIZE(p) + GET_SIZE(p_next) + ALIGNMENT;
		PUT(p_next, PACK(GET_SIZE(p_next), 0));
		u_int64_t *p_next_ftr = FTR_P(p_next);
		PUT(p_next_ftr, PACK(new_size, 0));
		PUT(p, PACK(new_size, 0));
	}
	u_int64_t *p_prev_ftr = p - 1;
	//printf("free: previous block footer: %p\n", p_prev_ftr);
	//printf("free: previous block footer content %p\n", (*(u_int64_t *) p_prev_ftr));
	if(!GET_ALLOC(p_prev_ftr)) {
		//printf("free: previous block is empty\n");
		new_size          = GET_SIZE(p_prev_ftr) + GET_SIZE(p) + 1;
		u_int64_t *prev_p = p_prev_ftr - GET_SIZE(p_prev_ftr) / ALIGNMENT;
		u_int64_t *ftr    = FTR_P(p);
		PUT(prev_p, PACK(new_size, 0));
		PUT(ftr, PACK(new_size, 0));
	}
}

/*
 * mm_realloc - Implemented simply in terms of mm_malloc and mm_free
 */
void *mm_realloc(void *ptr, size_t size) {
	printf("realloc called");
	if(ptr == NULL) {
		return mm_malloc(size);
	}
	if(size == 0) {
		mm_free(ptr);
		return NULL;
	}
	size_t aligned_size = ALIGN(size) + ALIGNMENT;
	u_int64_t *p        = ((u_int64_t *) ptr) - 1;
	if(GET_SIZE(p) == aligned_size) {
		return ptr;
	}
	if(GET_SIZE(p) < aligned_size + 3 * ALIGNMENT) {
		char *new_ptr = (char *) mm_malloc(size);
		for(int i = 0; i < size; i++) {
			new_ptr[i] = ((char *) ptr)[i];
		}
		mm_free(ptr);
		return (void *) (p + 1);
	} else {
		u_int64_t *new_ftr    = p + aligned_size;
		u_int64_t *next_block = new_ftr + 1;
		u_int64_t *old_ftr    = FTR_P(p);
		PUT(old_ftr, PACK(GET_SIZE(p) - aligned_size - 1, 0));
		PUT(next_block, PACK(GET_SIZE(p) - aligned_size - 1, 0));
		PUT(p, PACK(aligned_size, 1));
		PUT(new_ftr, PACK(aligned_size, 1));
		return ptr;
	}
	return NULL;
}
