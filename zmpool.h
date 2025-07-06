
#pragma once

#include <stdlib.h>
#include <stdint.h>

#define POOL_COUNT (1024 * 64)
#define POOL_BLOCKSIZE (1024 * 64)

#define ZERO (0)

static uint32_t zm_stat(void);

static void zm_init(void);
static void *zm_malloc(size_t size);
static void zm_free(void *ptr);


static unsigned char zm_pool[POOL_BLOCKSIZE * POOL_COUNT];

static uint32_t used = 0;

typedef struct zm_block {
    uint32_t block_size;
    struct zm_block *next;
} zm_block;

static zm_block zm_blocks[POOL_COUNT];
static zm_block *_free_block_ptr;
static zm_block *_free_block_ptr_end;

static void zm_init(void)
{
    uint32_t used = 0;

    for(uint32_t i=0; i<POOL_COUNT; i++)
    {
        zm_block *block = &zm_blocks[i];

        if(i != 0)
        {
	    zm_blocks[i-1].next = block;
        }

        block->block_size = 0;
    }
    zm_blocks[POOL_COUNT-1].next = 0;

    _free_block_ptr = &zm_blocks[0];
    _free_block_ptr_end = &zm_blocks[POOL_COUNT-1];
}

static void *zm_malloc(size_t size)
{
    void *p = NULL;

    if(   size == 0
       || size > POOL_BLOCKSIZE
       || used >= POOL_COUNT)
        return (void *)NULL;

    if(!_free_block_ptr)
        return (void *)NULL;

    zm_block *should_return = _free_block_ptr;
    _free_block_ptr = _free_block_ptr->next;

    if(_free_block_ptr == _free_block_ptr_end)
    {
        _free_block_ptr_end = 0;
    }

    used++;
    should_return->block_size = size;

    return (void *) &zm_pool[POOL_BLOCKSIZE * (should_return - zm_blocks)];
}

static void zm_free(void *ptr)
{
    uint64_t i;

    if( ptr < (void *) zm_pool || (void *) &zm_pool[POOL_COUNT*POOL_BLOCKSIZE -1] < ptr)
        return;

    i = ((uint64_t) ptr - (uint64_t) zm_pool) / POOL_BLOCKSIZE;

    if(i < POOL_COUNT &&
       zm_blocks[i].block_size)
    {
        zm_blocks[i].block_size = ZERO;
        used--;

        if(_free_block_ptr && _free_block_ptr_end)
        {
            _free_block_ptr_end->next = &zm_blocks[i];
            _free_block_ptr_end = &zm_blocks[i];
        }
        else
        {
            _free_block_ptr = _free_block_ptr_end = &zm_blocks[i];
        }
    }
    return;
}

static uint32_t zm_stat(void)
{
     return used;
}
