/**
 * All functions you make for the assignment must be implemented in this file.
 * Do not submit your assignment with a main function in this file.
 * If you submit with a main function in this file, you will get a zero.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "debug.h"
#include "sfmm.h"

#define MIN_BLOCK_SIZE 64
#define PROLOGUE_BLOCK_SIZE 64
#define EPILOGUE_BLOCK_SIZE 8
#define HEADER_SIZE 8
#define FOOTER_SIZE 8

/* Finds index of first free list from 0 to NUM_FREE_LISTS */
int first_free_list(size_t size)
{
    /* First check if size = M, 2M, or 3M (return 0, 1, 2, respectively). */
    if (size == MIN_BLOCK_SIZE)
    {
        return 0;
    }

    if (size == MIN_BLOCK_SIZE * 2)
    {
        return 1;
    }

    if (size == MIN_BLOCK_SIZE * 3)
    {
         return 2;
    }

    int lower_range = 3;
    int upper_range = 5;

    /* Otherwise, iterate like Fibonacci Sequence (3, 5, 8, 13, 21,...). If in range, return i. Otherwise, update ranges. */
    for (int i = 3; i < NUM_FREE_LISTS - 2; i++)
    {
        if (size > lower_range * MIN_BLOCK_SIZE && size <= upper_range * MIN_BLOCK_SIZE)
        {
            return i;
        }
        else
        {
            int old_lower = lower_range;
            lower_range = upper_range;
            upper_range += old_lower;
        }
    }

    /* Comes here if size is > (NUM_FREE_LISTS - 2)th element of Fibonacci * M bytes. If so, return NUM_FREE_LISTS -2.
    Else, return NUM_FREE_LISTS - 1 for Wilderness Block index. (Implemented outside function for wilderness). */
    if (size > lower_range * MIN_BLOCK_SIZE)
    {
        return NUM_FREE_LISTS - 2;
    }
    else
    {
        return NUM_FREE_LISTS - 1;
    }
}

/* Finds a free block to allocate the request. */
void *find_free_block(size_t index, size_t block_size)
{
    void *curr_b;
    void *next_b;

    for (int i = index; i < NUM_FREE_LISTS; i++)
    {
        curr_b = &sf_free_list_heads[i];
        next_b = sf_free_list_heads[i].body.links.next;

        /* Keep iterating through the current free list until you hit the sentinel again. */
        while (curr_b != next_b)
        {
            sf_block *free_block = ((sf_block *)next_b);

            /* If free block is not the sentinel AND it's block size satisfies the given block size, return that block. */
            if (curr_b != free_block && ((free_block -> header) & BLOCK_SIZE_MASK) >= block_size)
            {
                return free_block;
            }

            next_b = ((sf_block *)next_b) -> body.links.next;
        }
    }

    return NULL;
}

/* Adds new block into free list. ADDS TO THE FRONT OF THE LINKED LIST. Set new block's next to sentinel's next and
new block's prev to sentinel itself. Then, set sentinel's old next's prev to new block and sentinel's next now
to new block. */
void add_block_into_free_list(sf_block *new_block, int sentinel_index)
{
    sf_block *sentinel_ptr = &sf_free_list_heads[sentinel_index];
    new_block -> body.links.next = sentinel_ptr -> body.links.next;
    new_block -> body.links.prev = sentinel_ptr;

    (sentinel_ptr -> body.links.next) -> body.links.prev = new_block;
    sentinel_ptr -> body.links.next = new_block;
}

/* Removes a block from the free list. */
void remove_block_from_free_list(void *given_block)
{
    /* sf_block ptr to given block. */
    sf_block *block = (sf_block *)given_block;

    /* This helps to find if given block exists in the free lists. block_exists is the final boolean. */
    sf_block *curr_b;
    sf_block *next_b;
    int block_exists = 0;

    /* Loop through entire list of free lists. While curr block ptr != next block ptr (circular linked list), check if
    next block ptr = given block ptr. If it is, then block exists = 1 and break out, else it will remain 0. */
    for (int i = 0; i < NUM_FREE_LISTS; i++)
    {
        curr_b = &sf_free_list_heads[i];
        next_b = sf_free_list_heads[i].body.links.next;

        while (curr_b != next_b)
        {
            /* If found the block, break out of while loop. */
            if (next_b == block)
            {
                block_exists = 1;
                break;
            }

            next_b = next_b -> body.links.next;
        }

        if (block_exists)
        {
            break;
        }
    }

    /* If block exists remains 0, just return. Otherwise, update prev block's next and next block's prev accordingly. */
    if (block_exists == 0)
    {
        return;
    }

    (block -> body.links.prev) -> body.links.next = block -> body.links.next;
    (block -> body.links.next) -> body.links.prev = block -> body.links.prev;
}

/* Coalesce two blocks that are ASSUMED to be FREE. */
sf_block *coalesce_blocks(sf_block *block_one, sf_block *block_two)
{
    /* Remove both blocks from the free list so can put new one in once done. */
    remove_block_from_free_list((void *)block_one);
    remove_block_from_free_list((void *)block_two);

    size_t total_size = ((block_one -> header) & BLOCK_SIZE_MASK) + ((block_two -> header) & BLOCK_SIZE_MASK);

    /* Rewrite block one's information. */

    /* First get block one's pal bit. This way the new block can have it for its header. This new block
    is free by default so its alloc bit is 0. */

    size_t block_one_pal_bit = (block_one -> header) & PREV_BLOCK_ALLOCATED;

    /* Overwrite block one as the new coalesced block. Update the header AS WELL AS the footer b/c its a free block. */
    block_one -> header = total_size | block_one_pal_bit;

    sf_block *coalesced_next_block = (sf_block *)(((void *)block_one) + total_size);
    coalesced_next_block -> prev_footer = block_one -> header;

    /* Now add new coalesced block into the free list. */
    int free_list_index = first_free_list(total_size);

    /* Check if next block is epilogue (block size == 0). If it is, coalesced block is new wilderness block.
    Index becomes NUM_FREE_LISTS - 1. */
    if (((coalesced_next_block -> header) & BLOCK_SIZE_MASK) == 0)
    {
        free_list_index = NUM_FREE_LISTS - 1;
    }

    add_block_into_free_list(block_one, free_list_index);

    return block_one;
}

/* Given pointer to block, splits it based on block size (smaller size) to avoid internal fragmentation. */
void *block_split(void *curr_block, size_t block_size)
{
    sf_block *first_block = (sf_block *)curr_block;

    /* prev block allocated boolean by & with 0x2. */
    size_t pal_bit = (first_block -> header) & PREV_BLOCK_ALLOCATED;

    /* find current size by masking the alloc bits. */
    size_t curr_block_size = (first_block -> header) & BLOCK_SIZE_MASK;

    /* If given block size is smaller than actual block's size, can go on to splitting it. */
    if (block_size < curr_block_size)
    {
        /* If block size diff is less than MIN_BLOCK_SIZE (64), have a splinter. Return a ptr to curr block's payload. */
        if (curr_block_size - block_size < MIN_BLOCK_SIZE)
        {
            return (void *)(first_block -> body.payload);
        }

        /* This is when we can split AKA when diff is >= MIN_BLOCK_SIZE. Split into 2 blocks. */

        /* Header becomes the new block size | THIS_BLOCK_ALLOCATED | w/e the pal bit was before. */
        first_block -> header = block_size | THIS_BLOCK_ALLOCATED | pal_bit;

        /* Remove old block from free list. */
        remove_block_from_free_list(curr_block);

        /* Initialize new block's header, and new block ptr for the split process. */
        size_t second_block_size = curr_block_size - block_size;

        sf_block *second_block = curr_block + block_size;

        /* Now, the second block is free. */

        /* Set new block's header and split's next block's prev footer. Also set second block's prev footer to 0 b/c
        first block is now allocated. */
        second_block -> header = second_block_size | PREV_BLOCK_ALLOCATED;
        second_block -> prev_footer = 0;

        /* Find new index for second block size and then add the second block at that new index. */
        int second_block_free_list_index = first_free_list(second_block_size);

        /* Block after split block. */
        sf_block *splits_next_block = curr_block + block_size + second_block_size;
        size_t splits_next_block_size = (splits_next_block -> header) & BLOCK_SIZE_MASK;

        /* Check if new split block is the new wilderness block AKA split's next block is the EPILOGUE! */
        if (splits_next_block_size == 0)
        {
            second_block_free_list_index = NUM_FREE_LISTS - 1;
        }

        add_block_into_free_list(second_block, second_block_free_list_index);

        /* Split's next block's prev footer is split block's header. */
        splits_next_block -> prev_footer = second_block -> header;

        /* Check if block is just an epilogue (block size = 0). If so, erase pal bit and set equal to alloc bit. */
        if (splits_next_block_size == 0)
        {
            splits_next_block -> header = THIS_BLOCK_ALLOCATED;
        }
        else
        {
            /* Have to set next block's header to erase the pal bit. */
            size_t splits_next_block_allocated_bit = (splits_next_block -> header) & THIS_BLOCK_ALLOCATED;

            splits_next_block -> header = splits_next_block_size | splits_next_block_allocated_bit;

            /* Now check if split's next block is allocated. If the alloc bit is NOT 1, then it is free. Change footer too.
            Then coalesce the 2 free blocks together. */
            if (splits_next_block_allocated_bit != 1)
            {
                sf_block *next_next_block = (sf_block *)(((void *)splits_next_block) + splits_next_block_size);
                next_next_block -> prev_footer = splits_next_block -> header;

                second_block = coalesce_blocks(second_block, splits_next_block);
            }
        }

        /* No need to check for coalescing for first block and second block b/c first block is allocated by default. */

        return (void *)(first_block -> body.payload);
    }
    else if (block_size > curr_block_size)
    {
        /* Block too small to split into 2. */
        return NULL;
    }
    else
    {
        /* The given block is already the right size. Just set it to allocated. */
        remove_block_from_free_list(curr_block);

        /* Set header of current block to allocated. */
        sf_block *curr_block_ptr = (sf_block *)curr_block;
        curr_block_ptr -> header |= THIS_BLOCK_ALLOCATED;

        /* No need to set footer b/c now its allocated so no footer in allocated blocks. */
        size_t curr_block_size = (curr_block_ptr -> header) & BLOCK_SIZE_MASK;

        /* Set next block's prev-allocated bit. */
        sf_block *next_block = curr_block + curr_block_size;
        next_block -> header |= PREV_BLOCK_ALLOCATED;

        /* Also 0 out the prev footer since curr block is now ALLOCATED. */
        next_block -> prev_footer = 0;

        return (void *)(curr_block_ptr -> body.payload);
    }
}

/* Code used to allocate a page if wilderness block is not there or not big enough. */
void *allocate_page(size_t block_size)
{
    sf_block *last_block = (sf_block *)(sf_mem_end() - PAGE_SZ - HEADER_SIZE - FOOTER_SIZE);
    size_t wilderness_size = (last_block -> prev_footer) & BLOCK_SIZE_MASK;

    /* Old wilderness block */
    sf_block *wilderness_block = (sf_block *)(sf_mem_end() - PAGE_SZ - HEADER_SIZE - FOOTER_SIZE - wilderness_size);
    size_t wilderness_size_from_header = (wilderness_block -> header) & BLOCK_SIZE_MASK;

    /* Check if wilderness header and footer show same block size. */
    if (wilderness_size_from_header != wilderness_size)
    {
        return NULL;
    }

    /* Update old wilderness block's header size. */
    size_t wilderness_prev_allocated_bit = (wilderness_block -> header) & PREV_BLOCK_ALLOCATED;
    wilderness_block -> header = (wilderness_size_from_header + PAGE_SZ) | wilderness_prev_allocated_bit;

    /* Set up new wilderness block's footer in new epilogue's prev footer. */
    sf_block *epilogue = (sf_block *)(sf_mem_end() - HEADER_SIZE - FOOTER_SIZE);
    epilogue -> prev_footer = wilderness_block -> header;

    /* New epilogue header just equals THIS_BLOCK_ALLOCATED (0x1). */
    epilogue -> header = THIS_BLOCK_ALLOCATED;

    /* Update new size. */
    wilderness_size_from_header = (wilderness_block -> header) & BLOCK_SIZE_MASK;

    /* Now add new wilderness into the correct list in free lists. Index is last one for wilderness block. */
    remove_block_from_free_list(wilderness_block);
    int new_index = NUM_FREE_LISTS - 1;
    add_block_into_free_list(wilderness_block, new_index);

    /* Now check if new wilderness block size satisfies given block size. If it does, then proceed to split.
    Otherwise, recall this function again to add another page. */
    if (block_size <= wilderness_size_from_header)
    {
        return block_split(wilderness_block, block_size);
    }

    /* If it reaches this point, then the block size still isnt enough. Grow another page and repeat the process. */
    void *page_attempt = sf_mem_grow();

    /* If can't add another page, just return NULL. */
    if (page_attempt == NULL)
    {
        return NULL;
    }
    else
    {
        return allocate_page(block_size);
    }
}

/* When first call of malloc, must initialize prologue, epilogue, and free block with sf_mem_grow. */
void *first_sf_mem_grow(void *first_free_list_head)
{
    void *first_page_head = sf_mem_grow();

    /* Possible errors. After growing a page, if ptr still NULL or start of heap = end of heap, then return NULL. */
    if (first_page_head == NULL || sf_mem_start() == sf_mem_end())
    {
        return NULL;
    }

    /* Setting up prologue (Starts 56 bytes after the heap. Start with header. Will set up prologue's footer in
    free block's sf_block. */
    sf_block *first_prologue = (sf_block *)(sf_mem_start() + 48);
    first_prologue -> header = PROLOGUE_BLOCK_SIZE | THIS_BLOCK_ALLOCATED | PREV_BLOCK_ALLOCATED;

    /* First free block actually starts at 112 bytes from the heap b/c want to start at prologue's footer to set prev_footer.
    112 comes from 7 rows padding + 1 row prologue header + 6 rows padding. (14 * 8) = 112. */
    /* FIRST FREE BLOCK = WILDERNESS BLOCK!! */
    sf_block *first_block = (sf_block *)(sf_mem_start() + 56 + PROLOGUE_BLOCK_SIZE - FOOTER_SIZE);

    /* Set up first block's prev footer, which is prologue footer. */
    first_block -> prev_footer = PROLOGUE_BLOCK_SIZE | THIS_BLOCK_ALLOCATED | PREV_BLOCK_ALLOCATED;

    /* First block's header */
    first_block -> header = PAGE_SZ - 56 - PROLOGUE_BLOCK_SIZE - HEADER_SIZE;
    first_block -> header |= PREV_BLOCK_ALLOCATED;

    /* Set up first block's prev and next ptrs TO POINT TO FREE LIST SENTINEL (WILDERNESS BLOCK SENTINEL). */
    first_block -> body.links.next = first_free_list_head;
    first_block -> body.links.prev = first_free_list_head;

    /* Set up first block's footer in epilogue's prev footer.  */
    sf_block *first_epilogue = (sf_block *)(sf_mem_end() - HEADER_SIZE - FOOTER_SIZE);
    first_epilogue -> prev_footer = first_block -> header;

    /* First epilogue header just equals THIS_BLOCK_ALLOCATED (0x1) b/c block size = 0. */
    first_epilogue -> header = THIS_BLOCK_ALLOCATED;

    /* Return where wilderness block is initially. */
    return sf_mem_start() + 56 + PROLOGUE_BLOCK_SIZE - FOOTER_SIZE;
}

void *first_fit(size_t allocated_size)
{
    /* First find the index of free list for the given size. */
    int first_index = first_free_list(allocated_size);

    void *free_block = NULL;
    void *allocated_block_ptr = NULL;
    void *initial_block = NULL;

    /* Check if start of heap equals end of heap. If it does, then heap empty. Build prologue and epilogue. */
    if (sf_mem_start() == sf_mem_end())
    {
        /* Set all sentinels of free blocks list to have their next and prev point to itself. */
        for (int i = 0; i < NUM_FREE_LISTS; i++)
        {
            sf_free_list_heads[i].body.links.next = &sf_free_list_heads[i];
            sf_free_list_heads[i].body.links.prev = &sf_free_list_heads[i];
        }

        /* Since heap is empty, free block is the wilderness block at the LAST INDEX of the free lists. */
        first_index = NUM_FREE_LISTS - 1;
        initial_block = first_sf_mem_grow(&sf_free_list_heads[first_index]);

        /* Check if initial block is NULL. */
        if (initial_block == NULL)
        {
            return NULL;
        }
        else
        {
            /* Set sentinel of current free list's next and prev to initial block (wilderness block). */
            sf_free_list_heads[first_index].body.links.next = initial_block;
            sf_free_list_heads[first_index].body.links.prev = initial_block;

            /* Check if size given is 0 or negative AFTER you set up the heap and free lists! */
            if (allocated_size == 0)
            {
                return NULL;
            }

            /* Check if need to split the big block (allocated size <= WILDERNESS BLOCK SIZE). */
            if (allocated_size <= (PAGE_SZ - 56 - PROLOGUE_BLOCK_SIZE - HEADER_SIZE))
            {
                allocated_block_ptr = block_split(initial_block, allocated_size);
            }
            else
            {
                /* Otherwise, need to add on to the big block. */
                void *mem_grow_attempt = sf_mem_grow();

                /* Can't add another page. Return NULL. */
                if (mem_grow_attempt == NULL)
                {
                    return NULL;
                }
                else
                {
                    allocated_block_ptr = allocate_page(allocated_size);
                }
            }

            return allocated_block_ptr;
        }
    }
    else
    {
        /* Try to find a free block to use. */
        free_block = find_free_block(first_index, allocated_size);

        /* If there is none, go on to growing a page. */
        if (free_block == NULL)
        {
            free_block = sf_mem_grow();

            /* If can't grow, return NULL. */
            if (free_block == NULL)
            {
                return NULL;
            }
            else
            {
                /* Coalesce with the prev empty block if there is one. */

                sf_block *curr_epilogue = (sf_block *)(sf_mem_end() - PAGE_SZ - HEADER_SIZE - FOOTER_SIZE);

                /* Make old epilogue new header of block. Check if prev block was allocated. */
                if (curr_epilogue -> header == (THIS_BLOCK_ALLOCATED | PREV_BLOCK_ALLOCATED))
                {
                    /* New block is new WILDERNESS BLOCK, b/c no free blocks are available.
                    *** REMEMBER TO RESET PREV FOOTER b/c THE WILDERNESS BLOCK IS NO MORE! *** */
                    sf_block *new_block = curr_epilogue;
                    new_block -> prev_footer = 0;
                    new_block -> header = PAGE_SZ | PREV_BLOCK_ALLOCATED;

                    /* Set new block's footer in FUTURE epilogue. */

                    /* Add new wilderness block into the free list. */
                    int new_block_index = NUM_FREE_LISTS - 1;
                    add_block_into_free_list(new_block, new_block_index);

                    /* Now make the future epilogue and set prev footer (new block's footer) and new header. */
                    sf_block *new_epilogue = (sf_block *)(sf_mem_end() - HEADER_SIZE - FOOTER_SIZE);
                    new_epilogue -> prev_footer = new_block -> header;
                    new_epilogue -> header = THIS_BLOCK_ALLOCATED;

                    /* Check if the allocated block size is <= the page just allocated or not. If it is,
                    go on to splitting. Otherwise have to allocate another page. */
                    if (allocated_size <= PAGE_SZ)
                    {
                        allocated_block_ptr = block_split(new_block, allocated_size);
                        return allocated_block_ptr;
                    }
                    else
                    {
                        /* If it isn't enough, have to go on to allocating another page. */
                        void *mem_grow_attempt = sf_mem_grow();

                        /* Can't add another page. Return NULL. */
                        if (mem_grow_attempt == NULL)
                        {
                            return NULL;
                        }
                        else
                        {
                            allocated_block_ptr = allocate_page(allocated_size);
                        }

                        return allocated_block_ptr;
                    }
                }
                else if (curr_epilogue -> header) /* Epilogue is just allocated itself, no prev-allocated. */
                {
                    /* If epilogue just allocated itself (header == 1), allocate another page.
                    NOTE: This should NEVER HAPPEN, b/c if epilogue had no prev-allocated, then wilderness block should
                    STILL be in the free list. This mean this conditional should never of happened. Instead the else
                    statement should happen if that wilderness wasn't big enough when the free block was found initially. */
                    allocated_block_ptr = allocate_page(allocated_size);
                    return allocated_block_ptr;
                }
                else
                {
                    /* Something wrong with epilogue. */
                    return NULL;
                }
            }
        }
        else
        {
            /* Found a free block in the free list! */
            size_t free_block_size = (((sf_block *)free_block) -> header) & BLOCK_SIZE_MASK;

            /* If the size you need is <= free block's size, split down the free block. */
            if (allocated_size <= free_block_size)
            {
                allocated_block_ptr = block_split(free_block, allocated_size);
            }
            else
            {
                /* Otherwise, need to add on to the big block. */
                void *mem_grow_attempt = sf_mem_grow();

                /* Can't add another page. Return NULL. */
                if (mem_grow_attempt == NULL)
                {
                    return NULL;
                }
                else
                {
                    allocated_block_ptr = allocate_page(allocated_size);
                }
            }

            return allocated_block_ptr;
        }
    }
}

void *sf_malloc(size_t size) {
    /* Block allocation size is header size + size of block + padding + footer size (part of payload for allocated) */
    size_t allocated_block_size = HEADER_SIZE + size;

    /* If allocated_block_size is NOT 64 byte aligned, add the necessary PADDING to make it aligned. */
    if (allocated_block_size % MIN_BLOCK_SIZE != 0)
    {
        allocated_block_size += (MIN_BLOCK_SIZE - (allocated_block_size % MIN_BLOCK_SIZE));
    }

    /* If given size is <= 0, then set allocated block size to 0 so can STILL BUILD HEAP but then break out AFTERWARDS. */
    if (size <= 0)
    {
        allocated_block_size = 0;
    }

    void *mem_pointer = first_fit(allocated_block_size);

    /* If allocated block size is 0, then return NULL and don't mess w/ sf_errno */
    if (allocated_block_size == 0)
    {
        return NULL;
    }

    /* If ultimately, the malloc request can't be done, set sf_errno to ENOMEM and return NULL. */
    if (mem_pointer == NULL)
    {
        sf_errno = ENOMEM;
        return NULL;
    }

    /* Otherwise, return the mem ptr. */
    return mem_pointer;
}

void sf_free(void *pp) {
    /* First, check if the pointer is NULL. If it is, abort(). */
    if (pp == NULL)
    {
        abort();
    }

    /* Check if the pointer currently at the payload is 64 byte aligned. *It is suppose to be aligned up to this point!* */
    if ((size_t)pp % MIN_BLOCK_SIZE != 0)
    {
        abort();
    }

    /* pp currently at payload! */
    sf_block *allocated_block = (sf_block *)(pp - HEADER_SIZE - FOOTER_SIZE);

    /* Check if current block is allocated. If it isn't abort(). */
    if (((allocated_block -> header) & THIS_BLOCK_ALLOCATED) == 0)
    {
        abort();
    }

    /* Check if header of block is before the end of prologue. AKA Check if the header of the block crosses before
    the end of the prologue's footer. */
    if ((void *)(&(allocated_block -> header)) < (sf_mem_start() + 56 + PROLOGUE_BLOCK_SIZE))
    {
        abort();
    }

    size_t block_size = (allocated_block -> header) & BLOCK_SIZE_MASK;

    /* Check if footer of block is after the beginning of the epilogue. AKA Check if the **END** of the footer
    is after the beginning of the epilogue. AKA Check if the end of the footer crosses the header of the epilogue. */
    if (((void *)(&(allocated_block -> header)) + block_size) > (sf_mem_end() - HEADER_SIZE))
    {
        abort();
    }

    /* If prev allocated bit is 0, check if the alloc bit of the previous block is 0. If it is not, then abort(). */

    /* Get prev block's size from prev block. */
    sf_block *prev_next_block = (sf_block *)(pp - HEADER_SIZE - FOOTER_SIZE);
    size_t prev_block_size_from_footer = (prev_next_block -> prev_footer) & BLOCK_SIZE_MASK;

    /* Prev Block */
    sf_block *prev_block = (sf_block *)(pp - HEADER_SIZE - FOOTER_SIZE - prev_block_size_from_footer);

    if (((allocated_block -> header) & PREV_BLOCK_ALLOCATED) == 0)
    {
        /* Check if the alloc bit of prev block is true. If it is, abort(). */
        if (((prev_block -> header) & THIS_BLOCK_ALLOCATED) != 0)
        {
            abort();
        }
    }

    /* After passing all these checks, this block is currently ALLOCATED and we must FREE it. */

    /* First change allocated block's header to ERASE the alloc bit, just keeping the pal bit. */
    size_t prev_allocated_bit = (allocated_block -> header) & PREV_BLOCK_ALLOCATED;
    allocated_block -> header = block_size | prev_allocated_bit;

    /* Now a free block, so set its footer in the next block to equal its header. */
    sf_block *next_block = (sf_block *)(((void *)allocated_block) + block_size);
    size_t next_block_size = (next_block -> header) & BLOCK_SIZE_MASK;

    next_block -> prev_footer = allocated_block -> header;

    /* Now change the next block's prev-allocated bit. */

    /* If block size is 0, just the epilogue. Set header to alloc and thats it. */
    if (next_block_size == 0)
    {
        next_block -> header = THIS_BLOCK_ALLOCATED;
    }
    else
    {
        /* Next block is a normal block we have to change the header to ERASE the pal bit. */
        size_t next_block_allocated_bit = (next_block -> header) & THIS_BLOCK_ALLOCATED;
        next_block -> header = next_block_size | next_block_allocated_bit;

        /* Now check if next block is allocated. If the alloc bit is NOT 1, then it is free. Have to change footer too. */
        if (next_block_allocated_bit != 1)
        {
            sf_block *next_next_block = (sf_block *)(((void *)next_block) + next_block_size);
            next_next_block -> prev_footer = next_block -> header;
        }
    }

    /* Now add the new free block into the free list. Check if its a new wilderness block by checking if the next block
     was an epilogue. If it was, index is NUM_FREE_LISTS - 1. */
    int free_list_index = first_free_list(block_size);

    if (next_block_size == 0)
    {
        free_list_index = NUM_FREE_LISTS - 1;
    }

    add_block_into_free_list(allocated_block, free_list_index);

    /* Check if the previous block is free. If it is, gotta coalesce the prev block w/ this new free block. Check the pal
    bit == 0. If it's 0, then prev block is a free block. */
    if (prev_allocated_bit == 0)
    {
        allocated_block = coalesce_blocks(prev_block, allocated_block);
    }

    /* Get next block's alloc bit. If it is 0, then it is a free block, and you have to coalesce curr block with next block
    together. */
    block_size = (allocated_block -> header) & BLOCK_SIZE_MASK;
    next_block = (sf_block *)(((void *)allocated_block) + block_size);
    size_t next_block_alloc_bit = (next_block -> header) & THIS_BLOCK_ALLOCATED;

    if (next_block_alloc_bit == 0)
    {
        allocated_block = coalesce_blocks(allocated_block, next_block);
    }

    return;
}

void *sf_realloc(void *pp, size_t rsize) {
    /* NOTE: In realloc, the block must be already ALLOCATED! */
    /* Realloc grows or shrinks a block. Invalid ptr: sf_errno = EINVAL; No memory: sf_errno = ENOMEM */

    /* Check if valid pointer BUT rsize is 0. If so, free the block and return NULL! */
    if (rsize == 0)
    {
        sf_free(pp);
        return NULL;
    }

    /* First check if the rsize was negative. */
    if (rsize < 0)
    {
        return NULL;
    }

    /* Now check for valid pointer the same way you checked in sf_free(). */

    /* First, check if the pointer is NULL. If it is, return NULL. */
    if (pp == NULL)
    {
        sf_errno = EINVAL;
        return NULL;
    }

    /* Check if the pointer currently at the payload is 64 byte aligned. *It is suppose to be aligned up to this point!* */
    if ((size_t)pp % MIN_BLOCK_SIZE != 0)
    {
        sf_errno = EINVAL;
        return NULL;
    }

    /* pp currently at payload! */
    sf_block *allocated_block = (sf_block *)(pp - HEADER_SIZE - FOOTER_SIZE);

    /* Check if current block is allocated. If it isn't return NULL. */
    if (((allocated_block -> header) & THIS_BLOCK_ALLOCATED) == 0)
    {
        sf_errno = EINVAL;
        return NULL;
    }

    /* Check if header of block is before the end of prologue. AKA Check if the header of the block crosses before
    the end of the prologue's footer. */
    if ((void *)(&(allocated_block -> header)) < (sf_mem_start() + 56 + PROLOGUE_BLOCK_SIZE))
    {
        sf_errno = EINVAL;
        return NULL;
    }

    size_t block_size = (allocated_block -> header) & BLOCK_SIZE_MASK;

    /* Check if footer of block is after the beginning of the epilogue. AKA Check if the **END** of the footer
    is after the beginning of the epilogue. AKA Check if the end of the footer crosses the header of the epilogue. */
    if (((void *)(&(allocated_block -> header)) + block_size) > (sf_mem_end() - HEADER_SIZE))
    {
        sf_errno = EINVAL;
        return NULL;
    }

    /* If prev allocated bit is 0, check if the alloc bit of the previous block is 0. If it is not, then return NULL. */

    /* Get prev block's size from prev block's footer. */
    sf_block *prev_next_block = (sf_block *)(pp - HEADER_SIZE - FOOTER_SIZE);
    size_t prev_block_size_from_footer = (prev_next_block -> prev_footer) & BLOCK_SIZE_MASK;

    /* Prev Block */
    sf_block *prev_block = (sf_block *)(pp - HEADER_SIZE - FOOTER_SIZE - prev_block_size_from_footer);

    if (((allocated_block -> header) & PREV_BLOCK_ALLOCATED) == 0)
    {

        /* Check if the alloc bit of prev block is true. If it is, return NULL. */
        if (((prev_block -> header) & THIS_BLOCK_ALLOCATED) != 0)
        {
            sf_errno = EINVAL;
            return NULL;
        }
    }

    /* After passing all these checks, this block is currently ALLOCATED and we must REALLOC it. */

    /* 2 Cases: Case 1 - Realloc from smaller block size to LARGER block size.
                Case 2 - Realloc from larger block size to SMALLER block size. */

    /* Case 1: realloc smaller -> larger size. */
    if (block_size < rsize)
    {
        void *larger_block = sf_malloc(rsize);

        /* If for some reason, can't malloc a block of larger size, then realloc returns NULL and set errno to ENOMEM. */
        if (larger_block == NULL)
        {
            sf_errno = ENOMEM;
            return NULL;
        }

        /* REMEMBER: larger_block now points to the larger allocated block's PAYLOAD! */

        /* Now memcpy the allocated block into the block by sf_malloc().
        memcpy takes in parameters: dest, origin, # of bytes. */
        memcpy(larger_block, allocated_block -> body.payload, block_size);

        /* Now we free the smaller allocated block. */
        /* If the block's smaller size was not 0, then go on to free the block. */
        if (block_size != 0)
        {
            sf_free(allocated_block -> body.payload);
        }

        /* Return the larger block. */
        return larger_block;
    }
    else if (block_size > rsize)
    {
        /* Case 2: realloc larger -> smaller size. */

        /* Check for splinter by simply adding HEADER SIZE (8) to the smaller realloc block size and see if the difference
        between the current block size and now bumped up realloc block size is < MIN_BLOCK_SIZE of 64. */
        /* No adding footer b/c allocated blocks have no footer. */
        rsize += HEADER_SIZE;

        /* If it leads to a splinter, simply return current allocated block's payload. */
        if (block_size - rsize < MIN_BLOCK_SIZE)
        {
            return (void *)(allocated_block -> body.payload);
        }
        else
        {
            /* If no splinter, simply use our block split function to split the allocated block's payload into
            realloc size. */

            /* If the_rsize is NOT 64 byte aligned, add the necessary PADDING to make it aligned. */
            if (rsize % MIN_BLOCK_SIZE != 0)
            {
                rsize += (MIN_BLOCK_SIZE - (rsize % MIN_BLOCK_SIZE));
            }

            /* Now attempt splitting. */
            return block_split((void *)allocated_block, rsize);
        }
    }
    else
    {
        /* Case where realloc size is the same as the block size. Simply return the payload. */
        return (void *)(allocated_block -> body.payload);
    }
}

void *sf_memalign(size_t size, size_t align) {

    /* First we must check if the alignment size is a POWER of 2 and >= MIN_BLOCK_SIZE. If it breaks any of these
    initial conditions, set sf_errno to EINVAL and return NULL. */

    /* Simple bitwise manipulation tells us if align is a power of 2. Powers of 2 always have a 1 bit at the beginning
    followed by 0's. Therefore, align - 1 would be a 0 bit followed by 1's. If we bitwise & them, then if it is a
    power of 2, then the result should be 0. */
    size_t is_power_of_two = align & (align - 1);

    if (is_power_of_two != 0  || align < MIN_BLOCK_SIZE)
    {
        sf_errno = EINVAL;
        return NULL;
    }

    /* If block size is 0, then just return NULL without setting sf_errno. */
    if (size == 0)
    {
        return NULL;
    }

    /* Initial size of the block is AT LEAST the requested size, plus the alignment size, plus the minimum block size,
    plus the size required for a block header. */
    size_t initial_allocation_size = size + align + MIN_BLOCK_SIZE + HEADER_SIZE;

    /* Now 64 byte align this initial allocation size by passing it into sf_malloc and then getting a ptr to the INITIAL
    allocation size. (Will be sliced down later.) */

    void *initial_allocated_block_payload = sf_malloc(initial_allocation_size);

    /* If for some reason, can't malloc a block of initial size, then memalign returns NULL and set errno to ENOMEM. */
    if (initial_allocated_block_payload == NULL)
    {
        sf_errno = ENOMEM;
        return NULL;
    }

    /* If this size is properly aligned with the new alignment, then just go on to allocate a block with this size.
    MAY STILL HAVE TO FREE EXTRA SPACE IN THE END OF THIS BLOCK. */
    if (((size_t)initial_allocated_block_payload) % align == 0)
    {
        /* Epiphany: If the payload is aligned to the new alignment, to cut off the right excess of the block, simply
        call REALLOC to REDUCE the initial allocated block size to the size passed into mem align. */
        return sf_realloc(initial_allocated_block_payload, size);
    }
    else
    {
        /* If this size is not properly aligned, then align it, and go on to freeing any block space BEFORE the alignment
        and AFTER the alignment. This way the block only allocates the amount of space needed. */

        /* Get initial block and its size. */
        sf_block *initial_allocated_block = (sf_block *)(initial_allocated_block_payload - HEADER_SIZE - FOOTER_SIZE);
        size_t initial_allocated_block_size = (initial_allocated_block -> header) & BLOCK_SIZE_MASK;

        /* Move the payload address forward to the new alignment address by CONSTANTLY adding 64 until address is aligned. */
        void *new_allocated_block_payload_addr = initial_allocated_block_payload;

        while ((size_t)new_allocated_block_payload_addr % align != 0)
        {
            new_allocated_block_payload_addr += MIN_BLOCK_SIZE;
        }

        /* Find the alignment diff and the NEW allocation size. */
        size_t alignment_diff = new_allocated_block_payload_addr - initial_allocated_block_payload;
        size_t new_allocation_size = initial_allocated_block_size - alignment_diff;

        /* New aligned allocated block with new, smaller size. */
        sf_block *new_allocated_block = (sf_block *)(((void *)initial_allocated_block) + alignment_diff);

        /* Update the initial allocated block to the alignment difference as the block size. */
        size_t initial_allocated_block_pal_bit = (initial_allocated_block -> header) & PREV_BLOCK_ALLOCATED;
        initial_allocated_block -> header = alignment_diff | THIS_BLOCK_ALLOCATED | initial_allocated_block_pal_bit;

        /* Update the new allocated block to the new allocation size. */
        new_allocated_block -> header = new_allocation_size | THIS_BLOCK_ALLOCATED | PREV_BLOCK_ALLOCATED;

        /* Now, we must free the initial block, which is excess space to the LEFT of the new allocated block. */
        sf_free(initial_allocated_block_payload);


        /* Now we freed the excess left, free the excess space to the RIGHT of the new allocated block the same way as if
        the address were aligned in the first place using sf_realloc. */
        return sf_realloc((void *)(new_allocated_block -> body.payload), size);
    }
}