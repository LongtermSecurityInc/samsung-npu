#include "npu.h"


/*
 * heap_init - Address: 0x200c0
 * 
 * Wrapper for the heap initialization function _heap_init.
 */
void heap_init() {
    /*
     * Main heap initialization function.
     * Creates a heap at address 0x80000 and ending at 0xE0000.
     */
    _heap_init(HEAP_START_ADDR, HEAP_END_ADDR);

    // [...]
}


/*
 * _heap_init - Address: 0x20dfc
 * 
 * Main heap initialization function.
 */
struct heap_chunk* __heap_init(u32 heap_start_addr, u32 heap_end_addr) {
    struct heap_chunk **freelist_ptr;

    /* Checks that the heap is larger than 16 bytes */
    if (heap_start_addr + 16 > heap_end_addr)
        out_of_heap_memory_abort(0);

    /* Gets the freelist pointer from heap_state */
    freelist_ptr = get_heap_freelist_ptr();

    /* Makes the freelist point to the heap start address */
    *freelist_ptr = (struct heap_chunk*) heap_start_addr;

    /* Initializes the first chunk */
    freelist_chunk_init(*freelist_ptr);
    struct heap_chunk *first_heap_chunk = (struct heap_chunk *)heap_start_addr;
    first_heap_chunk->size = 0;
    first_heap_chunk->next = 0;
    *(u32 *)(first_heap_chunk + 8) = first_heap_chunk

    /* Initializes the freelist */
    if (heap_end_addr != heap_start_addr + 16) {
        u32 heap_size = heap_end_addr - (heap_addr + 16);
        u32 heap_start = heap_start_addr + 16;
        freelist_init(*freelist_ptr, heap_start, heap_size);
    }
}


/*
 * freelist_init - Address: 0x21520
 * 
 * Freelist initialization function creating the first chunk which can then be
 * reused by functions such as malloc/free.
 */
void freelist_init(struct heap_chunk *freelist, struct heap_chunk *first_chunk,
    u32 size) {
    struct heap_chunk *next_chunk;
    struct heap_chunk *prev_chunk = freelist;

    /*
     * This loop will be skipped because `freelist->next` is null.
     * Note: `init_freelist_heap` is supposed to be called in another function
     *       but the function always return 0 before reaching the call to it.
     *       During the initialization process this loop is essentially dead
     *       code.
     */
    for (next_chunk = freelist->next;; next_chunk = next_chunk->next) {
        if (!next_chunk || next_chunk >= heap_addr)
            break;
        prev_chunk = next_chunk;
    }

    /*
     * Realigns the heap.
     * Here `chunk->size` is zero and `chunk` is equal to `freelist`.
     * The result should be:
     *  - first_chunk = 0x80014
     *  - size = 0x5ffec
     */
    if (prev_chunk + prev_chunk->size != heap_addr) {
        size -= ((first_chunk->size + 3) & 0xFFFFFFF8) + 4 - first_chunk;
        first_chunk = ((first_chunk + 3) & 0xFFFFFFF8) + 4;
    }

    /*
     * The previous instructions don't do much, but now the size of the first
     * chunk is set to the size of the whole heap. By freeing this chunk, even
     * though it hasn't been really allocated yet, it will be added to the
     * freelist and it will then be possible to split it into smaller chunks
     * when calling malloc.
     */
    first_chunk->size = size;
    free(first_chunk->next);
}


/*
 * malloc - Address: 0x20138
 * 
 * Dynamically allocates a memory region of `size` bytes.
 * The return value is a pointer to the `next` field of the chunk which also
 * serves as the location for the payload.
 */
struct heap_chunk* malloc(u32 size_arg) {
    struct heap_chunk *freelist;
    u32 size;
    struct heap_chunk *result;
    struct heap_chunk *prev_chunk;
    struct heap_chunk *chunk_to_alloc;
    struct heap_chunk *next_chunk;

    freelist = *get_heap_freelist_ptr();

    /* Rounds the size up and returns if it overflows */
    size = (size_arg + 11) & 0xFFFFFFF8;
    if (size <= size_arg)
        return 0;

    /*
     * Checks that there is at least one chunk in the freelist that we can
     * allocate.
     */
    chunk_to_alloc = freelist->next;
    if (!chunk_to_alloc)
        return 0

    /*
     * Iterates through the chunks in the freelist to find one that is big
     * enough for the allocation.
     */
    while (chunk_to_alloc->size < size) {
        prev_chunk = chunk_to_alloc;
        chunk_to_alloc = chunk_to_alloc->next;
        if (!chunk_to_alloc)
            return 0;
    }

    /* Checks if the selected chunk can be split in two. */
    if (chunk_to_alloc->size >= size + MINIMUM_CHUNK_SIZE) {
        next_chunk = chunk_to_alloc + size;
        next_chunk->next = chunk_to_alloc->next;
        next_chunk->size = chunk_to_alloc->size - size;
        prev_chunk->next = next_chunk;
        chunk_to_alloc->size = size;
    } else {
        prev_chunk->next = chunk_to_alloc->next;
    }
    return chunk_to_alloc->next;
}


/*
 * free - Address: 0x201dc
 * 
 * Frees a dynamically allocated chunk.
 */
void free(struct heap_chunk *chunk_next_ptr) {
    struct heap_chunk *chunk_to_free;
    struct heap_chunk *prev_chunk;
    struct heap_chunk *next_chunk;
    u32 chunk_to_free_size;

    chunk_to_free = (struct heap_chunk *)(chunk_next_ptr - 4);
    prev_chunk = *get_heap_freelist_ptr();

    if (!chunk_next_ptr)
        return 0;

    /*
     * Freelist chunks are sorted by address. The function iterates over the
     * freelist to find after which chunk to insert the chunk we want to free.
     */
    for (next_chunk = prev_chunk->next;; next_chunk = next_chunk->next) {
        if (!next_chunk || next_chunk >= chunk_to_free)
            break;
        prev_chunk = next_chunk;
    }

    /*
     * If the previous chunk and the chunk we want to free are adjacent in
     * memory, coalesce them.
     */
    if (prev_chunk + prev_chunk->size == chunk_to_free) {
        chunk_to_free_size = chunk_to_free->size;
        chunk_to_free = prev_chunk;
        prev_chunk->size += chunk_to_free_size;
    }
    /* Otherwise just make the previous chunk point to the one to free. */
    else {
        prev_chunk->next = chunk_to_free;
    }

    /*
     * If the next chunk and the chunk we want to free are adjacent in
     * memory, coalesce them.
     */
    if (chunk_to_free + chunk_to_free->size == next_chunk)
    {
        chunk_to_free->next = next_chunk->next;
        chunk_to_free->size += next_chunk->size;
    }
    /* Otherwise just make the chunk to free point to the next one. */
    else {
        chunk_to_free->next = next_chunk;
    }
}
