
#include "talloc.h"

#if defined(TALLOC_CONFIG_ATOMICSWINDOWS)
    #include <Windows.h>
#elif defined(TALLOC_CONFIG_ATOMICSTDATOMICS)
    #include <stdatomic.h>
    #include <stdalign.h>
#endif


#if defined(TALLOC_CONFIG_ATOMICSWINDOWS)
    #define tatomic_exchange(ex, val) InterlockedExchange((LONG*)(ex), (val))
    #define tatomic_store(st, val) ((*st) = (val))
#elif defined(TALLOC_CONFIG_ATOMICSTDATOMICS)
    #define tatomic_exchange(ex, val) atomic_exchange((ex), (val))
    #define tatomic_store(st, val) atomic_store((st), (val))
#endif

#if defined(TALLOC_CONFIG_ATOMICSWINDOWS)
typedef volatile bool talatomicbool_t;
#elif defined(TALLOC_CONFIG_ATOMICSTDATOMICS)
typedef atomic_bool talatomicbool_t;
#endif

typedef unsigned char talbyte_t;

#define ABORT(msg)  \
    do              \
    {               \
        state->err_f(msg); \
        abort();    \
    } while(0);

//#define ASSERT(exp, msg) assert((exp) && (msg))
#define ASSERT(exp, msg)


#define TALLOC_LOCK(flag)                         \
    while(tatomic_exchange(&(flag), true)) \
    {                                      \
        ;                                  \
    }
#define TALLOC_UNLOCK(flag) tatomic_store(&(flag), false)
#define VECTOR_INIT_CAPACITY 128


struct talfreemeta_t
{
    talfreemeta_t* next;
    talfreemeta_t* prev;
    bool used;
    uintptr_t check;
    int64_t size;
    // additional data for free blocks
    talfreemeta_t* left;
    talfreemeta_t* right;
    int height;
}; 

struct talallocmeta_t
{
    talfreemeta_t* next;
    talfreemeta_t* prev;
    bool used;
#if TALLOC_MEM_CHECKING
    uintptr_t check;
#endif
    int64_t size;
};


struct talpoolmeta_t
{
    talpoolmeta_t* next;
};

struct talfreecellmeta_t
{
    talfreecellmeta_t* next;
};

struct talalloccellmeta_t
{
#if TALLOC_MEM_CHECKING
    uintptr_t check;
#endif
    int64_t size;
};

struct talcategory_t
{
    talfreecellmeta_t* head;
    talpoolmeta_t* next_pool;
    talatomicbool_t flag;
    int64_t used;
};

struct taluniversalmeta_t
{
#if TALLOC_MEM_CHECKING
    uintptr_t check;
#endif
    int64_t size;
};

struct talvector_t
{
    int64_t size;
    int64_t capacity;
    void** data;
};

struct talstate_t
{
    talonerrorfunc_t err_f;
    talatomicbool_t heap_flag;
    talfreemeta_t list_head;
    talfreemeta_t* free_tree_head;
    talvector_t sys_alloc_buffer;
    int64_t allocated;
    int64_t used;
    talcategory_t categories[CATEGORY_COUNT];
};


#define FREE_META_SIZE sizeof(talfreemeta_t)
#define ALLOC_META_SIZE sizeof(talallocmeta_t)
#define GET_ALLOC_META_PTR(ptr) ((talallocmeta_t*)(ptr) - 1)
#define MOVE_FREE_META_PTR(ptr, bytes) (talfreemeta_t*)((talbyte_t*)(ptr) + (bytes))
#define ADJUST_SIZE(size)                                         \
    {                                                             \
        (size) += TALLOC_ALIGNMENT - ((size) % TALLOC_ALIGNMENT); \
    }


#define GET_UNI_META_PTR(ptr) (((taluniversalmeta_t*)(ptr)) - 1)
#define MOVE_FREE_CELL_META_PTR(cell, n) (talfreecellmeta_t*)((talbyte_t*)(cell) + (n))
#define GET_ALLOC_CELL_META(ptr) ((talalloccellmeta_t*)(ptr) - 1);

static void tal_defaulterrfunc(const char* msg);

void tal_heap_forcereset(talstate_t* state)
{
    for(int64_t i = 0; i < state->sys_alloc_buffer.size; i++)
    {
        free(tal_vector_at(&state->sys_alloc_buffer, i));
    }
    tal_vector_free(&state->sys_alloc_buffer);
    state->list_head = (const talfreemeta_t){ 0 };
    state->free_tree_head = NULL;
    state->allocated = 0;
    state->used = 0;
}

talstate_t* tal_state_init()
{
    talstate_t* state;
    state = (talstate_t*)malloc(sizeof(talstate_t));
    memset(state, 0, sizeof(talstate_t));
    state->err_f = tal_defaulterrfunc;
    return state;
}

void tal_state_destroy(talstate_t* state)
{
    tal_heap_forcereset(state);
    free(state);
}

static void tal_defaulterrfunc(const char* msg)
{
    fprintf(stderr, "%s\n", msg);
}

void tal_state_seterrfunc(talstate_t* state, talonerrorfunc_t func)
{
    state->err_f = func;
}

//*****************************************************************************
// TREE
//*****************************************************************************

static inline int tal_heap_height(talstate_t* state, const talfreemeta_t* node)
{
    (void)state;
    if(!node)
        return 0;
    return node->height;
}

static inline int tal_heap_balance(talstate_t* state, const talfreemeta_t* node)
{
    int ileft;
    int iright;
    if(!node)
    {
        return 0;
    }
    ileft = tal_heap_height(state, node->left);
    iright = tal_heap_height(state, node->right);
    return ileft - iright;
}

static inline int tal_heap_maxnode(int a, int b)
{
    if(a > b)
    {
        return a;
    }
    return b;
}

talfreemeta_t* tal_heap_rrotate(talstate_t* state, talfreemeta_t* pivot)
{
    talfreemeta_t* new_root;
    new_root = pivot->left;
    if(!new_root)
    {
        return pivot;
    }
    talfreemeta_t* stree;
    stree = new_root->right;
    new_root->right = pivot;
    pivot->left = stree;
    pivot->height = tal_heap_maxnode(tal_heap_height(state, pivot->left), tal_heap_height(state, pivot->right)) + 1;
    new_root->height = tal_heap_maxnode(tal_heap_height(state, new_root->left), tal_heap_height(state, new_root->right)) + 1;
    return new_root;
}

talfreemeta_t* tal_heap_lrotate(talstate_t* state, talfreemeta_t* pivot)
{
    talfreemeta_t* stree;
    talfreemeta_t* new_root;
    new_root = pivot->right;
    if(!new_root)
    {
        return pivot;
    }
    stree = new_root->left;
    new_root->left = pivot;
    pivot->right = stree;
    pivot->height = tal_heap_maxnode(tal_heap_height(state, pivot->left), tal_heap_height(state, pivot->right)) + 1;
    new_root->height = tal_heap_maxnode(tal_heap_height(state, new_root->left), tal_heap_height(state, new_root->right)) + 1;
    return new_root;
}

talfreemeta_t* tal_heap_minnode(talstate_t* state, talfreemeta_t* node)
{
    (void)state;
    talfreemeta_t* current;
    current = node;
    /* loop down to find the leftmost leaf */
    while(current->left != NULL)
    {
        current = current->left;
    }
    return current;
}

static inline void tal_heap_initnode(talstate_t* state, talfreemeta_t* node)
{
    (void)state;
    node->left = NULL;
    node->right = NULL;
    node->height = 1;
}

talfreemeta_t* tal_heap_removenode(talstate_t* state, talfreemeta_t* root, talfreemeta_t* key)
{
    if(root == NULL)
    {
        return root;
    }
    if(key->size < root->size)
    {
        root->left = tal_heap_removenode(state, root->left, key);
    }
    else if(key->size > root->size)
    {
        root->right = tal_heap_removenode(state, root->right, key);
    }
    else
    {
        // cases with same size -> found node is not key node only has same
        // size
        if(root != key)
        {
            if(root->left)
            {
                root->left = tal_heap_removenode(state, root->left, key);
            }
            if(root->right)
            {
                root->right = tal_heap_removenode(state, root->right, key);
            }
        }
        else
        {
            // root == key -> node to be deleted
            if(!root->right || !root->left)
            {
                talfreemeta_t* tmp = root->left ? root->left : root->right;
                if(!tmp)
                {
                    // no child
                    root = NULL;
                }
                else
                {
                    // one child
                    root = tmp;
                }
            }
            else
            {
                // two children
                talfreemeta_t* tmp = tal_heap_minnode(state, root->right);
                root->right = tal_heap_removenode(state, root->right, tmp);
                tmp->right = root->right;
                tmp->left = root->left;
                tmp->height = root->height;
                root = tmp;
            }
        }
    }
    if(root == NULL)
    {
        return root;
    }
    root->height = 1 + tal_heap_maxnode(tal_heap_height(state, root->left), tal_heap_height(state, root->right));
    int bl = tal_heap_balance(state, root);
    // Left Left Case
    if(bl > 1 && tal_heap_balance(state, root->left) >= 0)
    {
        return tal_heap_rrotate(state, root);
    }
    // Left Right Case
    if(bl > 1 && tal_heap_balance(state, root->left) < 0)
    {
        root->left = tal_heap_lrotate(state, root->left);
        return tal_heap_rrotate(state, root);
    }
    // Right Right Case
    if(bl < -1 && tal_heap_balance(state, root->right) <= 0)
    {
        return tal_heap_lrotate(state, root);
    }
    // Right Left Case
    if(bl < -1 && tal_heap_balance(state, root->right) > 0)
    {
        root->right = tal_heap_rrotate(state, root->right);
        return tal_heap_lrotate(state, root);
    }
    return root;
}

talfreemeta_t* tal_heap_insertnode(talstate_t* state, talfreemeta_t* node, talfreemeta_t* new_node)
{
    ASSERT(new_node != node, "trying to insert already inserted node into heap tree");
    if(!node)
    {
        tal_heap_initnode(state, new_node);
        return new_node;
    }

    const int64_t size = new_node->size;
    if(size < node->size)
    {
        // left
        node->left = tal_heap_insertnode(state, node->left, new_node);
    }
    else
    {
        node->right = tal_heap_insertnode(state, node->right, new_node);
    }

    // AVL tree tal_heap_balance
    node->height = 1 + tal_heap_maxnode(tal_heap_height(state, node->left), tal_heap_height(state, node->right));
    const int bl = tal_heap_balance(state, node);

    // Left Left Case
    if(bl > 1 && size < node->left->size)
    {
        return tal_heap_rrotate(state, node);
    }

    // Right Right Case
    if(bl < -1 && size > node->right->size)
    {
        return tal_heap_lrotate(state, node);
    }

    // Left Right Case
    if(bl > 1 && size > node->left->size)
    {
        node->left = tal_heap_lrotate(state, node->left);
        return tal_heap_rrotate(state, node);
    }

    // Right Left Case
    if(bl < -1 && size < node->right->size)
    {
        node->right = tal_heap_rrotate(state, node->right);
        return tal_heap_lrotate(state, node);
    }

    return node;
}

talfreemeta_t* tal_heap_findfreenode(talstate_t* state, talfreemeta_t* node, int64_t size)
{
    (void)state;
    if(!node)
        return NULL;

    talfreemeta_t* current = node;
    talfreemeta_t* best_fit = NULL;
    int64_t node_size = 0;

    while(current)
    {
        node_size = current->size;
        if(size <= node_size)
        {
            // can fit
            best_fit = current;
            current = current->left;
        }
        if(size > node_size)
            current = current->right;
    }
    return best_fit;
}

// void print_tree(FILE *file, talfreemeta_t *node)
//{
//    if (!node)
//        return;
//    fprintf(file, "\nNode: %p Size: %zu\n", node, node->size);
//    fprintf(file, "   left: %p\n", node->left);
//    fprintf(file, "   right: %p\n", node->right);
//    fprintf(file, "   height: %i\n", node->height);
//
//    print_tree(file, node->left);
//    print_tree(file, node->right);
//}

//*****************************************************************************

//*****************************************************************************
// DOUBLE-LINKED LIST
//*****************************************************************************

void tal_heap_insertblock(talstate_t* state, talfreemeta_t* prev, talfreemeta_t* next, talfreemeta_t* block)
{
    (void)state;
    if(prev)
        prev->next = block;
    if(next)
        next->prev = block;
    block->prev = prev;
    block->next = next;
}

void tal_heap_insertblocksorted(talstate_t* state, talfreemeta_t* block)
{
    talfreemeta_t* current = state->list_head.next;
    talfreemeta_t* prev = &state->list_head;

    while(current && (block > current))
    {
        ASSERT(current != current->next, "memory corrupted");
        prev = current;
        current = current->next;
    }
    tal_heap_insertblock(state, prev, current, block);
}

void tal_heap_removeblock(talstate_t* state, talfreemeta_t* block)
{
    (void)state;
    talfreemeta_t* prev = block->prev;
    talfreemeta_t* next = block->next;
    if(prev)
        prev->next = next;
    if(next)
        next->prev = prev;
}
//*****************************************************************************

talfreemeta_t* tal_heap_newspace(talstate_t* state, int64_t size)
{
    if(size < TALLOC_BLOCK_SIZE)
    {
        size = TALLOC_BLOCK_SIZE;
    }
    if(size < (int64_t)sizeof(talfreemeta_t))
    {
        size += sizeof(talfreemeta_t);
    }
    talfreemeta_t* new_block = (talfreemeta_t*)malloc(size);
    if(!new_block)
        ABORT("bad allocation");

    new_block->size = size;
    new_block->used = false;
    tal_heap_insertblocksorted(state, new_block);
    state->free_tree_head = tal_heap_insertnode(state, state->free_tree_head, new_block);

#if TALLOC_FORCE_RESET
    // store for future free
    if(state->sys_alloc_buffer.data == NULL)
        tal_vector_init(&state->sys_alloc_buffer);
    tal_vector_push_back(&state->sys_alloc_buffer, new_block);
#endif

    state->allocated += size;
    return new_block;
}

// determinates if block can be merged from right with another block
talfreemeta_t* tal_heap_canmergenext(talstate_t* state, talfreemeta_t* block)
{
    (void)state;
    if(!block->next || block->next->used)
        return NULL;

    talfreemeta_t* last = MOVE_FREE_META_PTR(block, block->size);
    if(last == block->next)
        return block->next;
    return NULL;
}

// determinates if block can be merged from left with another block
talfreemeta_t* tal_heap_canmergeprev(talstate_t* state, talfreemeta_t* block)
{
    (void)state;
    if(!block->prev || block->prev->used)
        return NULL;

    int64_t size = block->prev->size;
    talfreemeta_t* prev_last = MOVE_FREE_META_PTR(block->prev, size);
    if(prev_last == block)
        return block->prev;
    return NULL;
}

// allocate block with proper alignment and save allocation meta data
void* tal_heap_allocate(talstate_t* state, talfreemeta_t* block, int64_t size)
{
    const int64_t rem_space = block->size - size;
    state->free_tree_head = tal_heap_removenode(state, state->free_tree_head, block);
    if(rem_space > (int64_t)FREE_META_SIZE)
    {
        talfreemeta_t* new_block = MOVE_FREE_META_PTR(block, size);
        new_block->size = rem_space;
        new_block->used = false;
        tal_heap_insertblock(state, block, block->next, new_block);
        state->free_tree_head = tal_heap_insertnode(state, state->free_tree_head, new_block);
    }
    else
    {
        size += rem_space;
    }

    talallocmeta_t* alloc_block = (talallocmeta_t*)block;
    alloc_block->size = size;
    alloc_block->used = true;
#if TALLOC_MEM_CHECKING
    alloc_block->check = (uintptr_t)(alloc_block + 1);
#endif
    return (alloc_block + 1);
}

void tal_heap_deallocate(talstate_t* state, talfreemeta_t* block)
{
    talfreemeta_t* new_block = block;
    block->used = false;

    talfreemeta_t* neighbour = tal_heap_canmergenext(state, block);
    if(neighbour)
    {
        // remove from tree
        state->free_tree_head = tal_heap_removenode(state, state->free_tree_head, neighbour);
        // remove from list
        tal_heap_removeblock(state, neighbour);
        block->size = block->size + neighbour->size;
    }

    neighbour = tal_heap_canmergeprev(state, block);
    if(neighbour)
    {
        // remove from tree
        state->free_tree_head = tal_heap_removenode(state, state->free_tree_head, neighbour);
        // remove from list
        tal_heap_removeblock(state, block);
        neighbour->size = block->size + neighbour->size;
        new_block = neighbour;
    }

    state->free_tree_head = tal_heap_insertnode(state, state->free_tree_head, new_block);
}

void* tal_heap_malloc(talstate_t* state, int64_t count)
{
    count = count + ALLOC_META_SIZE;
    if(count < (int64_t)FREE_META_SIZE)
        count = FREE_META_SIZE;
    ADJUST_SIZE(count);

    TALLOC_LOCK(state->heap_flag);
    talfreemeta_t* block = tal_heap_findfreenode(state, state->free_tree_head, count);
    if(!block)
    {
        // no free block with requested size -> allocate new one
        block = tal_heap_newspace(state, count);
    }

    ASSERT(block->size >= count, "not enough space");
    void* ret = tal_heap_allocate(state, block, count);
    state->used += count;
    TALLOC_UNLOCK(state->heap_flag);

    return ret;
}

void tal_heap_free(talstate_t* state, void* ptr)
{
    if(!ptr)
        return;
    talfreemeta_t* block = (talfreemeta_t*)GET_ALLOC_META_PTR(ptr);

    TALLOC_LOCK(state->heap_flag);
    state->used -= block->size;
    tal_heap_deallocate(state, block);
    TALLOC_UNLOCK(state->heap_flag);
}

void tal_heap_expand(talstate_t* state, int64_t count)
{
    if(count < TALLOC_BLOCK_SIZE)
        count = TALLOC_BLOCK_SIZE;

    tal_heap_newspace(state, count);
}

void tal_heap_printblocks(talstate_t* state, FILE* file)
{
    fprintf(file, "\n");
    fprintf(file, "???????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????\n");
    fprintf(file, "??? address          ???   size   ??? previous         ??? next             ???\n");
    fprintf(file, "???????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????\n");
    talfreemeta_t* current = &state->list_head;
    TALLOC_LOCK(state->heap_flag);
    while(current)
    {
        if(!current->used)
            fprintf(file, "??? %16p ??? %8zu ??? %16p ??? %16p ???\n", current, current->size, current->prev,
                    current->next);
        if(current == current->next)
            break;
        current = current->next;
    }
    TALLOC_UNLOCK(state->heap_flag);
    fprintf(file, "???????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????\n\n");

    //    print_tree(file, state->free_tree_head);
}

int64_t tal_heap_allocated(talstate_t* state)
{
    return state->allocated;
}

int64_t tal_heap_used(talstate_t* state)
{
    return state->used;
}

void tal_pool_newcategory(talstate_t* state, talcategory_t* category, int64_t size)
{
    // allocate space for n objects of size on global heap
    void* new_head = tal_heap_malloc(state, (TALLOC_INIT_POOL_SIZE * size) + sizeof(talpoolmeta_t) + sizeof(talalloccellmeta_t));
    talpoolmeta_t* new_pool = (talpoolmeta_t*)new_head;

    // store linked list of pools in category (for future freeing)
    new_pool->next = category->next_pool;
    category->next_pool = new_pool;

    new_head = (talbyte_t*)new_head + sizeof(talpoolmeta_t) + sizeof(talalloccellmeta_t);
    talfreecellmeta_t* iter = (talfreecellmeta_t*)new_head;
    talalloccellmeta_t* buf = NULL;
    for(int64_t i = 0; i < TALLOC_INIT_POOL_SIZE - 1; ++i, iter = MOVE_FREE_CELL_META_PTR(iter, size))
    {
        iter->next = MOVE_FREE_CELL_META_PTR(iter, size);
        buf = GET_ALLOC_CELL_META(iter);
        buf->size = size;
#if TALLOC_MEM_CHECKING
        buf->check = (uintptr_t)iter;
#endif
    }
    iter->next = NULL;
    buf = GET_ALLOC_CELL_META(iter);
    buf->size = size;
#if TALLOC_MEM_CHECKING
    buf->check = (uintptr_t)iter;
#endif

    category->head = new_head;
}

talfreecellmeta_t* tal_pool_allocate(talstate_t* state, talcategory_t* category, int64_t size)
{
    talfreecellmeta_t* ret;
    talalloccellmeta_t* meta;
    (void)meta;
    TALLOC_LOCK(category->flag);

    if(category->head == NULL)
        tal_pool_newcategory(state, category, size);
    ret = category->head;
    meta = GET_ALLOC_CELL_META(ret);
    #if TALLOC_MEM_CHECKING
        ASSERT(meta->check == (uintptr_t)ret, "pool corrupted");
    #endif
    category->head = category->head->next;
    category->used++;
    TALLOC_UNLOCK(category->flag);

    return ret;
}

void tal_pool_deallocate(talstate_t* state, talcategory_t* category, talfreecellmeta_t* free_cell)
{
    (void)state;
    TALLOC_LOCK(category->flag);
    free_cell->next = category->head;
    category->head = free_cell;
    category->used--;
    TALLOC_UNLOCK(category->flag);
}

void* tal_pool_malloc(talstate_t* state, int64_t count)
{
    count = tal_pool_cellsize(state, count);
    const int64_t category_id = SIZE_TO_CATEGORY(count);
    ASSERT(category_id < CATEGORY_COUNT, "pool category overflow");
    talcategory_t* category = &state->categories[category_id];

    return tal_pool_allocate(state, category, count);
}

void tal_pool_free(talstate_t* state, void* ptr)
{
    talalloccellmeta_t* cell = GET_ALLOC_CELL_META(ptr);
    const int64_t size = cell->size;
    #if TALLOC_MEM_CHECKING
    ASSERT(cell->check == (uintptr_t)ptr, "pool corrupted");
    #endif
    int64_t category_id = SIZE_TO_CATEGORY(size);
    //fprintf(stderr, "category_id=%ld CATEGORY_COUNT=%ld\n", category_id, CATEGORY_COUNT);
    ASSERT(category_id < CATEGORY_COUNT, "pool category overflow");
    talcategory_t* category = &state->categories[category_id];
    talfreecellmeta_t* free_cell = (talfreecellmeta_t*)ptr;
    tal_pool_deallocate(state, category, free_cell);
}

int64_t tal_pool_cellsize(talstate_t* state, int64_t size)
{
    (void)state;
    if(size < (int64_t)sizeof(talfreecellmeta_t))
        size = sizeof(talfreecellmeta_t);
    size += sizeof(talalloccellmeta_t);
    return NEXT_MULT_OF(size, TALLOC_POOL_GROUP_MULT);
}

void tal_pool_optimize(talstate_t* state)
{
    talcategory_t* c = NULL;
    for(int64_t i = 0; i < CATEGORY_COUNT; i++)
    {
        c = &state->categories[i];
        TALLOC_LOCK(c->flag);
        // check category, when its not used -> clean up all its allocated pools
        if(!c->used)
        {
            talpoolmeta_t* current = c->next_pool;
            talpoolmeta_t* prev = NULL;
            while(current)
            {
                prev = current;
                current = current->next;
                tal_heap_free(state, prev);
            }
            c->next_pool = NULL;
            c->head = NULL;
        }
        TALLOC_UNLOCK(c->flag);
    }
}

bool tal_util_isaligned(talstate_t* state, const void* p, size_t alignment)
{
    (void)state;
    return (uintptr_t)p % alignment == 0;
}

void tal_util_alignptrup(talstate_t* state, void** p, size_t alignment, ptrdiff_t* adjustment)
{
    if(tal_util_isaligned(state, *p, alignment))
    {
        *adjustment = 0;
        return;
    }

    const size_t mask = alignment - 1;
    ASSERT((alignment & mask) == 0, "wrong alignemet");// pwr of 2
    const uintptr_t i_unaligned = (uintptr_t)(*p);
    const uintptr_t misalignment = i_unaligned & mask;
    *adjustment = alignment - misalignment;
    *p = (void*)(i_unaligned + *adjustment);
}

void tal_util_alignptrwithheader(talstate_t* state, void** p, size_t alignment, int64_t header_size, ptrdiff_t* adjustment)
{
    unsigned char* chptr = (unsigned char*)(*p);
    chptr += header_size;
    *p = (void*)chptr;
    tal_util_alignptrup(state, p, alignment, adjustment);
    *adjustment += header_size;
}


void tal_vector_double_if_needed(talvector_t* vec)
{
    if(vec->size >= vec->capacity)
    {
        vec->capacity *= 2;
        vec->data = realloc(vec->data, sizeof(void*) * vec->capacity);
    }
}

void tal_vector_init(talvector_t* vec)
{
    vec->size = 0;
    vec->capacity = VECTOR_INIT_CAPACITY;
    vec->data = malloc(sizeof(void*) * VECTOR_INIT_CAPACITY);
}

void tal_vector_free(talvector_t* vec)
{
    free(vec->data);
    vec->data = NULL;
    vec->size = 0;
    vec->capacity = 0;
}

void tal_vector_push_back(talvector_t* vec, void* data)
{
    tal_vector_double_if_needed(vec);
    vec->data[vec->size++] = data;
}

void* tal_vector_at(talvector_t* vec, int64_t index)
{
    ASSERT(index < vec->size && index >= 0, "vector index out of range!!!");
    return vec->data[index];
}

void* tal_state_malloc(talstate_t* state, int64_t count)
{
    if(count == 0)
        return NULL;

#if TALLOC_USE_POOLS
    if(tal_pool_cellsize(state, count) <= TALLOC_SMALL_TO)
        return tal_pool_malloc(state, count);
#endif
    return tal_heap_malloc(state, count);
}

void* tal_state_realloc(talstate_t* state, void* ptr, int64_t size)
{
    if(!ptr)
        return tal_state_malloc(state, size);
    void* mem = tal_state_malloc(state, size);
    memcpy(mem, ptr, size);
    tal_state_free(state, ptr);
    return mem;
}

void* tal_state_calloc(talstate_t* state, const int64_t nelem, const int64_t elsize)
{
    const int64_t size = nelem * elsize;
    void* mem = tal_state_malloc(state, size);
    memset(mem, 0, size);
    return mem;
}

void tal_state_free(talstate_t* state, void* ptr)
{
    if(!ptr)
        return;
#if TALLOC_USE_POOLS
    const taluniversalmeta_t* block = GET_UNI_META_PTR(ptr);

    #if TALLOC_MEM_CHECKING
        #if 0
        if(block->check != (uintptr_t)ptr)
        {
            ABORT("pointer being freed was not allocated");
        }
        #endif
    #endif
    if(block->size <= TALLOC_SMALL_TO)
    {
        tal_pool_free(state, ptr);
        return;
    }
#endif
    tal_heap_free(state, ptr);
}

void tal_state_expand(talstate_t* state, int64_t count)
{
    tal_heap_expand(state, count);
}

void tal_state_printblocks(talstate_t* state, FILE* file)
{
    tal_heap_printblocks(state, file);
}

void tal_state_optimize(talstate_t* state)
{
#if TALLOC_USE_POOLS
    tal_pool_optimize(state);
#endif
}

int64_t tal_state_getallocatedsystemmemory(talstate_t* state)
{
    return tal_heap_allocated(state);
}

int64_t tal_state_getusedmemory(talstate_t* state)
{
    return tal_heap_used(state);
}

void tal_state_forcereset(talstate_t* state)
{
    tal_heap_forcereset(state);
}
