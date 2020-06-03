#include <criterion/criterion.h>
#include <errno.h>
#include <signal.h>
#include "debug.h"
#include "sfmm.h"

void assert_free_block_count(size_t size, int count);
void assert_free_list_block_count(size_t size, int count);

/*
 * Assert the total number of free blocks of a specified size.
 * If size == 0, then assert the total number of all free blocks.
 */
void assert_free_block_count(size_t size, int count) {
    int cnt = 0;
    for(int i = 0; i < NUM_FREE_LISTS; i++) {
	sf_block *bp = sf_free_list_heads[i].body.links.next;
	while(bp != &sf_free_list_heads[i]) {
	    if(size == 0 || size == (bp->header & BLOCK_SIZE_MASK))
		cnt++;
	    bp = bp->body.links.next;
	}
    }
    if(size == 0) {
	cr_assert_eq(cnt, count, "Wrong number of free blocks (exp=%d, found=%d)",
		     count, cnt);
    } else {
	cr_assert_eq(cnt, count, "Wrong number of free blocks of size %ld (exp=%d, found=%d)",
		     size, count, cnt);
    }
}

/*
 * Assert that the free list with a specified index has the specified number of
 * blocks in it.
 */
void assert_free_list_size(int index, int size) {
    int cnt = 0;
    sf_block *bp = sf_free_list_heads[index].body.links.next;
    while(bp != &sf_free_list_heads[index]) {
	cnt++;
	bp = bp->body.links.next;
    }
    cr_assert_eq(cnt, size, "Free list %d has wrong number of free blocks (exp=%d, found=%d)",
		 index, size, cnt);
}

Test(sf_memsuite_student, malloc_an_int, .init = sf_mem_init, .fini = sf_mem_fini) {
	sf_errno = 0;
	int *x = sf_malloc(sizeof(int));

	cr_assert_not_null(x, "x is NULL!");

	*x = 4;

	cr_assert(*x == 4, "sf_malloc failed to give proper space for an int!");

	assert_free_block_count(0, 1);
	assert_free_block_count(3904, 1);
	assert_free_list_size(NUM_FREE_LISTS-1, 1);

	cr_assert(sf_errno == 0, "sf_errno is not zero!");
	cr_assert(sf_mem_start() + PAGE_SZ == sf_mem_end(), "Allocated more than necessary!");
}

Test(sf_memsuite_student, malloc_three_pages, .init = sf_mem_init, .fini = sf_mem_fini) {
	sf_errno = 0;
	// We want to allocate up to exactly three pages.
	void *x = sf_malloc(3 * PAGE_SZ - ((1 << 6) - sizeof(sf_header)) - 64 - 2*sizeof(sf_header));

	cr_assert_not_null(x, "x is NULL!");
	assert_free_block_count(0, 0);
	cr_assert(sf_errno == 0, "sf_errno is not 0!");
}

Test(sf_memsuite_student, malloc_too_large, .init = sf_mem_init, .fini = sf_mem_fini) {
	sf_errno = 0;
	void *x = sf_malloc(PAGE_SZ << 16);

	cr_assert_null(x, "x is not NULL!");
	assert_free_block_count(0, 1);
	assert_free_block_count(65408, 1);
	cr_assert(sf_errno == ENOMEM, "sf_errno is not ENOMEM!");
}

Test(sf_memsuite_student, free_quick, .init = sf_mem_init, .fini = sf_mem_fini) {
	sf_errno = 0;
	/* void *x = */ sf_malloc(8);
	void *y = sf_malloc(32);
	/* void *z = */ sf_malloc(1);

	sf_free(y);

	assert_free_block_count(0, 2);
	assert_free_block_count(64, 1);
	assert_free_block_count(3776, 1);
	cr_assert(sf_errno == 0, "sf_errno is not zero!");
}

Test(sf_memsuite_student, free_no_coalesce, .init = sf_mem_init, .fini = sf_mem_fini) {
	sf_errno = 0;
	/* void *x = */ sf_malloc(8);
	void *y = sf_malloc(200);
	/* void *z = */ sf_malloc(1);

	sf_free(y);

	assert_free_block_count(0, 2);
	assert_free_block_count(256, 1);
	assert_free_block_count(3584, 1);
	cr_assert(sf_errno == 0, "sf_errno is not zero!");
}

Test(sf_memsuite_student, free_coalesce, .init = sf_mem_init, .fini = sf_mem_fini) {
	sf_errno = 0;
	/* void *w = */ sf_malloc(8);
	void *x = sf_malloc(200);
	void *y = sf_malloc(300);
	/* void *z = */ sf_malloc(4);

	sf_free(y);
	sf_free(x);

	assert_free_block_count(0, 2);
	assert_free_block_count(576, 1);
	assert_free_block_count(3264, 1);
	cr_assert(sf_errno == 0, "sf_errno is not zero!");
}

Test(sf_memsuite_student, freelist, .init = sf_mem_init, .fini = sf_mem_fini) {
	void *u = sf_malloc(200);
	/* void *v = */ sf_malloc(300);
	void *w = sf_malloc(200);
	/* void *x = */ sf_malloc(500);
	void *y = sf_malloc(200);
	/* void *z = */ sf_malloc(700);

	sf_free(u);
	sf_free(w);
	sf_free(y);

	assert_free_block_count(0, 4);
	assert_free_block_count(256, 3);
	assert_free_block_count(1600, 1);
	assert_free_list_size(3, 3);
	assert_free_list_size(NUM_FREE_LISTS-1, 1);

	// First block in list should be the most recently freed block.
	int i = 3;
	sf_block *bp = sf_free_list_heads[i].body.links.next;
	cr_assert_eq(bp, (char *)y - 2*sizeof(sf_header),
		     "Wrong first block in free list %d: (found=%p, exp=%p)",
                     i, bp, (char *)y - 2*sizeof(sf_header));
}

Test(sf_memsuite_student, realloc_larger_block, .init = sf_mem_init, .fini = sf_mem_fini) {
	void *x = sf_malloc(sizeof(int));
	/* void *y = */ sf_malloc(10);
	x = sf_realloc(x, sizeof(int) * 20);

	cr_assert_not_null(x, "x is NULL!");
	sf_block *bp = (sf_block *)((char *)x - 2*sizeof(sf_header));
	cr_assert(bp->header & THIS_BLOCK_ALLOCATED, "Allocated bit is not set!");
	cr_assert((bp->header & BLOCK_SIZE_MASK) == 128, "Realloc'ed block size not what was expected!");

	assert_free_block_count(0, 2);
	assert_free_block_count(64, 1);
	assert_free_block_count(3712, 1);
}

Test(sf_memsuite_student, realloc_smaller_block_splinter, .init = sf_mem_init, .fini = sf_mem_fini) {
	void *x = sf_malloc(sizeof(int) * 20);
	void *y = sf_realloc(x, sizeof(int) * 16);

	cr_assert_not_null(y, "y is NULL!");
	cr_assert(x == y, "Payload addresses are different!");

	sf_block *bp = (sf_block *)((char*)y - 2*sizeof(sf_header));
	cr_assert(bp->header & THIS_BLOCK_ALLOCATED, "Allocated bit is not set!");
	cr_assert((bp->header & BLOCK_SIZE_MASK) == 128, "Block size not what was expected!");

	// There should be only one free block of size 3840.
	assert_free_block_count(0, 1);
	assert_free_block_count(3840, 1);
}

Test(sf_memsuite_student, realloc_smaller_block_free_block, .init = sf_mem_init, .fini = sf_mem_fini) {
	void *x = sf_malloc(sizeof(double) * 8);
	void *y = sf_realloc(x, sizeof(int));

	cr_assert_not_null(y, "y is NULL!");

	sf_block *bp = (sf_block *)((char*)y - 2*sizeof(sf_header));
	cr_assert(bp->header & THIS_BLOCK_ALLOCATED, "Allocated bit is not set!");
	cr_assert((bp->header & BLOCK_SIZE_MASK) == 64, "Realloc'ed block size not what was expected!");

	// After realloc'ing x, we can return a block of size 64 to the freelist.
	// This block will go into the main freelist and be coalesced.
	assert_free_block_count(0, 1);
	assert_free_block_count(3904, 1);
}

//############################################
//STUDENT UNIT TESTS SHOULD BE WRITTEN BELOW
//DO NOT DELETE THESE COMMENTS
//############################################

/* My own assert function that checks if a FREE block has the same header and footer. */
void assert_free_block_data(sf_block *free_block)
{
    size_t block_size = (free_block -> header) & BLOCK_SIZE_MASK;

    if (block_size == 0)
    {
        return;
    }

    sf_block *next_block = (sf_block *)(((void *)free_block) + block_size);

    cr_assert((free_block -> header) == (next_block -> prev_footer), "Free block header and footer MISMATCH!");
}

/* Test to assert free block's header and footer are equal after using malloc / free / realloc. */
Test(sf_memsuite_student, free_block_header_footer_equality, .init = sf_mem_init, .fini = sf_mem_fini)
{
	sf_errno = 0;

	void *w = sf_malloc(2700);
	size_t w_block_size = (((sf_block *)(w - 16)) -> header) & BLOCK_SIZE_MASK;
	sf_block *x = (sf_block *)(w - 16 + w_block_size);

	assert_free_block_data(x);
	assert_free_block_count(0, 1);
	assert_free_block_count(1216, 1);
	assert_free_list_size(NUM_FREE_LISTS - 1, 1);

	void *y = sf_realloc(w, 2358);
	size_t y_block_size = (((sf_block *)(y - 16)) -> header) & BLOCK_SIZE_MASK;
	sf_block *z = (sf_block *)(y - 16 + y_block_size);

	assert_free_block_data(z);
	assert_free_block_count(0, 1);
	assert_free_block_count(1600, 1);
	assert_free_list_size(NUM_FREE_LISTS - 1, 1);

	sf_free(y);

	assert_free_block_data(y - 16);
	assert_free_block_count(0, 1);
	assert_free_block_count(3968, 1);
	assert_free_list_size(NUM_FREE_LISTS - 1, 1);
	cr_assert(sf_errno == 0, "sf_errno is not zero!");
}

/* Test to assert the heap mem_grows when the whole wilderness block is occupied and there are no more free blocks. */
Test(sf_memsuite_student, mem_grow_wilderness_block_occupied, .init = sf_mem_init, .fini = sf_mem_fini)
{
	sf_errno = 0;

	void *x = sf_malloc(3960);

	assert_free_block_count(0, 0);

	sf_malloc(12);

	assert_free_block_count(0, 1);
	assert_free_block_count(4032, 1);
	assert_free_list_size(NUM_FREE_LISTS - 1, 1);

	cr_assert_not_null(x, "x is NULL!");
	cr_assert(sf_errno == 0, "sf_errno is not zero!");
}

/* Test to assert the alloc and pal bits of the block after a series of malloc / free calls. */
Test(sf_memsuite_student, block_alloc_pal_bits, .init = sf_mem_init, .fini = sf_mem_fini)
{
	void *x = sf_malloc(PAGE_SZ);
	size_t x_alloc_bit = (((sf_block *)(x - 16)) -> header) & THIS_BLOCK_ALLOCATED;

	cr_assert(x_alloc_bit == THIS_BLOCK_ALLOCATED, "Block alloc bit is incorrect!");

	size_t x_block_size = (((sf_block *)(x - 16)) -> header) & BLOCK_SIZE_MASK;
	sf_block *y = (sf_block *)(x - 16 + x_block_size);
	size_t y_pal_bit = (y -> header) & PREV_BLOCK_ALLOCATED;

	cr_assert(y_pal_bit == PREV_BLOCK_ALLOCATED, "Next block pal bit is incorrect!");

	sf_malloc(497);
	sf_free(x);
	x_alloc_bit = (((sf_block *)(x - 16)) -> header) & THIS_BLOCK_ALLOCATED;

	cr_assert(x_alloc_bit == 0, "Block alloc bit is incorrect!");

	y = (sf_block *)(x - 16 + x_block_size);
	y_pal_bit = (y -> header) & PREV_BLOCK_ALLOCATED;

	cr_assert(y_pal_bit == 0, "Next block pal bit is incorrect!");

	sf_free(((void *)(y)) + 16);
	size_t y_alloc_bit = (y -> header) & THIS_BLOCK_ALLOCATED;

	cr_assert(y_alloc_bit == 0, "Block alloc bit is incorrect!");

	sf_block *z = (sf_block *)(((void *)(y)) + ((y -> header) & BLOCK_SIZE_MASK));
	size_t z_pal_bit = (z -> header) & PREV_BLOCK_ALLOCATED;

	cr_assert(z_pal_bit == 0, "Next block pal bit is incorrect!");
}

/* Test to check if free aborts after trying to free a block within the prologue or the epilogue. */
Test(sf_memsuite_student, free_block_outside_heap_abort, .signal = SIGABRT, .init = sf_mem_init, .fini = sf_mem_fini)
{
	sf_malloc(1);
	sf_block *bad_block = (sf_block *)(sf_mem_start() + 48);
	bad_block -> header = 64 | THIS_BLOCK_ALLOCATED | PREV_BLOCK_ALLOCATED;
	sf_free((void *)(bad_block -> body.payload));
}

/* Test to check results for sf_memalign. */
Test(sf_memsuite_student, mem_align_with_offset, .init = sf_mem_init, .fini = sf_mem_fini)
{
	int alignment = 4096;
	void *x = sf_memalign(606, alignment);

	cr_assert_not_null(x, "x is NULL!");

	sf_block *initial_block = sf_mem_start() + 112;
	void *initial_block_payload = ((void *)initial_block) + 16;

	if (((size_t)initial_block_payload) % alignment == 0)
	{
		cr_assert((((size_t)x) % alignment) == 0, "Payload is not aligned to given alignment!");
		assert_free_block_count(0, 1);
		assert_free_block_count(7424, 1);
		assert_free_list_size(NUM_FREE_LISTS - 1, 1);
		cr_assert(((initial_block -> header) & BLOCK_SIZE_MASK) == 640, "Block size not what was expected!");
	}
	else
	{
		int left_size_offset = 64;
		initial_block = ((void *)initial_block) + 64;

		while((((size_t)(((void *)(initial_block)) + 16)) % alignment) != 0)
		{
			left_size_offset += 64;
			initial_block = ((void *)initial_block) + 64;
		}

		assert_free_block_count(0, 2);
		assert_free_block_count(left_size_offset, 1);
		assert_free_block_count((PAGE_SZ * 2) - 128 - left_size_offset - 640, 1);
		cr_assert(((initial_block -> header) & BLOCK_SIZE_MASK) == 640, "Block size not what was expected!");
	}
}