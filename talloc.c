
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

/*#define ASSERT(exp, msg) assert((exp) && (msg))*/
#define ASSERT(exp, msg)


#define TALLOC_LOCK(flag)                         \
    while(tatomic_exchange(&(flag), true)) \
    {                                      \
        ;                                  \
    }
#define TALLOC_UNLOCK(flag) tatomic_store(&(flag), false)
#define VECTOR_INIT_CAPACITY 128


/*
struct talfreemeta_t
{
    talfreemeta_t* next;
    talfreemeta_t* prev;
    bool used;
    uintptr_t check;
    int64_t size;
    talfreemeta_t* left;
    talfreemeta_t* right;
    int height;
}; 
*/


struct talmetamemory_t
{
    talmetamemory_t* next;
    talmetamemory_t* prev;
    bool used;
    uintptr_t check;
    int64_t size;
    talmetamemory_t* left;
    talmetamemory_t* right;
    int height;
};

/*
struct talallocmeta_t
{
    talfreemeta_t* next;
    talfreemeta_t* prev;
    bool used;
    uintptr_t check;
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
    uintptr_t check;
    int64_t size;
};
*/

struct talcategory_t
{
    talmetamemory_t* head;
    talmetamemory_t* next_pool;
    talatomicbool_t flag;
    int64_t used;
};

/*
struct taluniversalmeta_t
{
    uintptr_t check;
    int64_t size;
};
*/

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
    talmetamemory_t list_head;
    talmetamemory_t* free_tree_head;
    talvector_t sys_alloc_buffer;
    int64_t allocated;
    int64_t used;
    talcategory_t categories[CATEGORY_COUNT];
};


#define FREE_META_SIZE sizeof(talmetamemory_t)
#define ALLOC_META_SIZE sizeof(talmetamemory_t)
#define GET_ALLOC_META_PTR(ptr) ((talmetamemory_t*)(ptr) - 1)
#define MOVE_FREE_META_PTR(ptr, bytes) (talmetamemory_t*)((talbyte_t*)(ptr) + (bytes))
#define ADJUST_SIZE(size)                                         \
    {                                                             \
        (size) += TALLOC_ALIGNMENT - ((size) % TALLOC_ALIGNMENT); \
    }


#define GET_UNI_META_PTR(ptr) (((talmetamemory_t*)(ptr)) - 1)
#define MOVE_FREE_CELL_META_PTR(cell, n) (talmetamemory_t*)((talbyte_t*)(cell) + (n))
#define GET_ALLOC_CELL_META(ptr) ((talmetamemory_t*)(ptr) - 1);

static void tal_defaulterrfunc(const char* msg);

void tal_heap_forcereset(talstate_t* state)
{
    int64_t i;
    for(i = 0; i < state->sys_alloc_buffer.size; i++)
    {
        free(tal_vector_at(&state->sys_alloc_buffer, i));
    }
    tal_vector_free(&state->sys_alloc_buffer);
    state->list_head = (talmetamemory_t){ 0 };
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

static inline int tal_heap_height(talstate_t* state, talmetamemory_t* node)
{
    (void)state;
    if(!node)
    {
        return 0;
    }
    return node->height;
}

static inline int tal_heap_balance(talstate_t* state, talmetamemory_t* node)
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

talmetamemory_t* tal_heap_rrotate(talstate_t* state, talmetamemory_t* pivot)
{
    talmetamemory_t* stree;
    talmetamemory_t* newroot;
    newroot = pivot->left;
    if(!newroot)
    {
        return pivot;
    }
    stree = newroot->right;
    newroot->right = pivot;
    pivot->left = stree;
    pivot->height = tal_heap_maxnode(tal_heap_height(state, pivot->left), tal_heap_height(state, pivot->right)) + 1;
    newroot->height = tal_heap_maxnode(tal_heap_height(state, newroot->left), tal_heap_height(state, newroot->right)) + 1;
    return newroot;
}

talmetamemory_t* tal_heap_lrotate(talstate_t* state, talmetamemory_t* pivot)
{
    talmetamemory_t* stree;
    talmetamemory_t* newroot;
    newroot = pivot->right;
    if(!newroot)
    {
        return pivot;
    }
    stree = newroot->left;
    newroot->left = pivot;
    pivot->right = stree;
    pivot->height = tal_heap_maxnode(tal_heap_height(state, pivot->left), tal_heap_height(state, pivot->right)) + 1;
    newroot->height = tal_heap_maxnode(tal_heap_height(state, newroot->left), tal_heap_height(state, newroot->right)) + 1;
    return newroot;
}

talmetamemory_t* tal_heap_minnode(talstate_t* state, talmetamemory_t* node)
{
    talmetamemory_t* current;
    (void)state;
    current = node;
    /* loop down to find the leftmost leaf */
    while(current->left != NULL)
    {
        current = current->left;
    }
    return current;
}

static inline void tal_heap_initnode(talstate_t* state, talmetamemory_t* node)
{
    (void)state;
    node->left = NULL;
    node->right = NULL;
    node->height = 1;
}

talmetamemory_t* tal_heap_removenode(talstate_t* state, talmetamemory_t* root, talmetamemory_t* key)
{
    int bl;
    talmetamemory_t* tmp;
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
        /* cases with same size -> found node is not key node only has same size*/
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
            /* root == key -> node to be deleted*/
            if(!root->right || !root->left)
            {
                tmp = root->left ? root->left : root->right;
                if(!tmp)
                {
                    /* no child*/
                    root = NULL;
                }
                else
                {
                    /* one child*/
                    root = tmp;
                }
            }
            else
            {
                /* two children*/
                tmp = tal_heap_minnode(state, root->right);
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
    bl = tal_heap_balance(state, root);
    /* Left Left Case*/
    if(bl > 1 && tal_heap_balance(state, root->left) >= 0)
    {
        return tal_heap_rrotate(state, root);
    }
    /* Left Right Case*/
    if(bl > 1 && tal_heap_balance(state, root->left) < 0)
    {
        root->left = tal_heap_lrotate(state, root->left);
        return tal_heap_rrotate(state, root);
    }
    /* Right Right Case*/
    if(bl < -1 && tal_heap_balance(state, root->right) <= 0)
    {
        return tal_heap_lrotate(state, root);
    }
    /* Right Left Case*/
    if(bl < -1 && tal_heap_balance(state, root->right) > 0)
    {
        root->right = tal_heap_rrotate(state, root->right);
        return tal_heap_lrotate(state, root);
    }
    return root;
}

talmetamemory_t* tal_heap_insertnode(talstate_t* state, talmetamemory_t* node, talmetamemory_t* new_node)
{
    int bl;
    int64_t size;
    ASSERT(new_node != node, "trying to insert already inserted node into heap tree");
    if(!node)
    {
        tal_heap_initnode(state, new_node);
        return new_node;
    }
    size = new_node->size;
    if(size < node->size)
    {
        /* left*/
        node->left = tal_heap_insertnode(state, node->left, new_node);
    }
    else
    {
        node->right = tal_heap_insertnode(state, node->right, new_node);
    }
    /* AVL tree tal_heap_balance*/
    node->height = 1 + tal_heap_maxnode(tal_heap_height(state, node->left), tal_heap_height(state, node->right));
    bl = tal_heap_balance(state, node);
    /* Left Left Case*/
    if(bl > 1 && size < node->left->size)
    {
        return tal_heap_rrotate(state, node);
    }
    /* Right Right Case*/
    if(bl < -1 && size > node->right->size)
    {
        return tal_heap_lrotate(state, node);
    }
    /* Left Right Case*/
    if(bl > 1 && size > node->left->size)
    {
        node->left = tal_heap_lrotate(state, node->left);
        return tal_heap_rrotate(state, node);
    }
    /* Right Left Case*/
    if(bl < -1 && size < node->right->size)
    {
        node->right = tal_heap_rrotate(state, node->right);
        return tal_heap_lrotate(state, node);
    }
    return node;
}

talmetamemory_t* tal_heap_findfreenode(talstate_t* state, talmetamemory_t* node, int64_t size)
{
    int64_t nodesize;
    talmetamemory_t* current;
    talmetamemory_t* bestfit;
    (void)state;
    if(!node)
    {
        return NULL;
    }
    current = node;
    bestfit = NULL;
    nodesize = 0;
    while(current)
    {
        nodesize = current->size;
        if(size <= nodesize)
        {
            /* can fit*/
            bestfit = current;
            current = current->left;
        }
        if(size > nodesize)
        {
            current = current->right;
        }
    }
    return bestfit;
}

void tal_heap_insertblock(talstate_t* state, talmetamemory_t* prev, talmetamemory_t* next, talmetamemory_t* block)
{
    (void)state;
    if(prev)
    {
        prev->next = block;
    }
    if(next)
    {
        next->prev = block;
    }
    block->prev = prev;
    block->next = next;
}

void tal_heap_insertblocksorted(talstate_t* state, talmetamemory_t* block)
{
    talmetamemory_t* prev;
    talmetamemory_t* current;
    current = state->list_head.next;
    prev = &state->list_head;
    while(current && (block > current))
    {
        ASSERT(current != current->next, "memory corrupted");
        prev = current;
        current = current->next;
    }
    tal_heap_insertblock(state, prev, current, block);
}

void tal_heap_removeblock(talstate_t* state, talmetamemory_t* block)
{
    talmetamemory_t* prev;
    talmetamemory_t* next;
    (void)state;
    prev = block->prev;
    next = block->next;
    if(prev)
    {
        prev->next = next;
    }
    if(next)
    {
        next->prev = prev;
    }
}

talmetamemory_t* tal_heap_newspace(talstate_t* state, int64_t size)
{
    talmetamemory_t* newblock;
    if(size < TALLOC_BLOCK_SIZE)
    {
        size = TALLOC_BLOCK_SIZE;
    }
    if(size < (int64_t)sizeof(talmetamemory_t))
    {
        size += sizeof(talmetamemory_t);
    }
    newblock = (talmetamemory_t*)malloc(size);
    if(!newblock)
    {
        ABORT("bad allocation");
    }
    memset(newblock, 0, size);
    newblock->size = size;
    newblock->used = false;
    tal_heap_insertblocksorted(state, newblock);
    state->free_tree_head = tal_heap_insertnode(state, state->free_tree_head, newblock);
#if TALLOC_FORCE_RESET
    /* store for future free*/
    if(state->sys_alloc_buffer.data == NULL)
    {
        tal_vector_init(&state->sys_alloc_buffer);
    }
    tal_vector_push_back(&state->sys_alloc_buffer, newblock);
#endif
    state->allocated += size;
    return newblock;
}

/* determinates if block can be merged from right with another block*/
talmetamemory_t* tal_heap_canmergenext(talstate_t* state, talmetamemory_t* block)
{
    talmetamemory_t* last;
    (void)state;
    if(!block->next || block->next->used)
    {
        return NULL;
    }
    last = MOVE_FREE_META_PTR(block, block->size);
    if(last == block->next)
    {
        return block->next;
    }
    return NULL;
}

/* determinates if block can be merged from left with another block*/
talmetamemory_t* tal_heap_canmergeprev(talstate_t* state, talmetamemory_t* block)
{
    int64_t size;
    talmetamemory_t* prevlast;
    (void)state;
    if(!block->prev || block->prev->used)
    {
        return NULL;
    }
    size = block->prev->size;
    prevlast = MOVE_FREE_META_PTR(block->prev, size);
    if(prevlast == block)
    {
        return block->prev;
    }
    return NULL;
}

/* allocate block with proper alignment and save allocation meta data*/
void* tal_heap_allocate(talstate_t* state, talmetamemory_t* block, int64_t size)
{
    int64_t remspace;
    talmetamemory_t* newblock;
    talmetamemory_t* allocblock;
    remspace = block->size - size;
    state->free_tree_head = tal_heap_removenode(state, state->free_tree_head, block);
    if(remspace > (int64_t)FREE_META_SIZE)
    {
        newblock = MOVE_FREE_META_PTR(block, size);
        newblock->size = remspace;
        newblock->used = false;
        tal_heap_insertblock(state, block, block->next, newblock);
        state->free_tree_head = tal_heap_insertnode(state, state->free_tree_head, newblock);
    }
    else
    {
        size += remspace;
    }
    allocblock = (talmetamemory_t*)block;
    allocblock->size = size;
    allocblock->used = true;
    allocblock->check = (uintptr_t)(allocblock + 1);
    return (allocblock + 1);
}

void tal_heap_deallocate(talstate_t* state, talmetamemory_t* block)
{
    talmetamemory_t* neighbour;
    talmetamemory_t* newblock;
    newblock = block;
    block->used = false;
    neighbour= tal_heap_canmergenext(state, block);
    if(neighbour)
    {
        /* remove from tree*/
        state->free_tree_head = tal_heap_removenode(state, state->free_tree_head, neighbour);
        /* remove from list*/
        tal_heap_removeblock(state, neighbour);
        block->size = block->size + neighbour->size;
    }
    neighbour = tal_heap_canmergeprev(state, block);
    if(neighbour)
    {
        /* remove from tree*/
        state->free_tree_head = tal_heap_removenode(state, state->free_tree_head, neighbour);
        /* remove from list*/
        tal_heap_removeblock(state, block);
        neighbour->size = block->size + neighbour->size;
        newblock = neighbour;
    }
    state->free_tree_head = tal_heap_insertnode(state, state->free_tree_head, newblock);
}

void* tal_heap_malloc(talstate_t* state, int64_t count)
{
    void* ret;
    talmetamemory_t* block;
    count = count + ALLOC_META_SIZE;
    if(count < (int64_t)FREE_META_SIZE)
    {
        count = FREE_META_SIZE;
    }
    ADJUST_SIZE(count);
    TALLOC_LOCK(state->heap_flag);
    block = tal_heap_findfreenode(state, state->free_tree_head, count);
    if(!block)
    {
        /* no free block with requested size -> allocate new one*/
        block = tal_heap_newspace(state, count);
    }
    ASSERT(block->size >= count, "not enough space");
    ret = tal_heap_allocate(state, block, count);
    state->used += count;
    TALLOC_UNLOCK(state->heap_flag);
    return ret;
}

void tal_heap_free(talstate_t* state, void* ptr)
{
    talmetamemory_t* block;
    if(!ptr)
    {
        return;
    }
    block = (talmetamemory_t*)GET_ALLOC_META_PTR(ptr);
    TALLOC_LOCK(state->heap_flag);
    state->used -= block->size;
    tal_heap_deallocate(state, block);
    TALLOC_UNLOCK(state->heap_flag);
}

void tal_heap_expand(talstate_t* state, int64_t count)
{
    if(count < TALLOC_BLOCK_SIZE)
    {
        count = TALLOC_BLOCK_SIZE;
    }
    tal_heap_newspace(state, count);
}

void tal_heap_printblocks(talstate_t* state, FILE* file)
{
    talmetamemory_t* current;
    fprintf(file, "\n");
    fprintf(file, "||| address          |||   size   ||| previous         ||| next             |||\n");
    current = &state->list_head;
    TALLOC_LOCK(state->heap_flag);
    while(current)
    {
        if(!current->used)
        {
            fprintf(file, "||| %16p ||| %8zu ||| %16p ||| %16p |||\n", current, current->size, current->prev, current->next);
        }
        if(current == current->next)
        {
            break;
        }
        current = current->next;
    }
    TALLOC_UNLOCK(state->heap_flag);
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
    int64_t i;
    void* newhead;
    talmetamemory_t* buf;
    talmetamemory_t* iter;
    talmetamemory_t* newpool;
    /* allocate space for n objects of size on global heap*/
    newhead = tal_heap_malloc(state, (TALLOC_INIT_POOL_SIZE * size) + sizeof(talmetamemory_t) + sizeof(talmetamemory_t));
    newpool = (talmetamemory_t*)newhead;
    /* store linked list of pools in category (for future freeing)*/
    newpool->next = category->next_pool;
    category->next_pool = newpool;
    newhead = (talbyte_t*)newhead + sizeof(talmetamemory_t) + sizeof(talmetamemory_t);
    iter = (talmetamemory_t*)newhead;
    buf = NULL;
    i = 0;
    while(i < TALLOC_INIT_POOL_SIZE - 1)
    {
        iter->next = MOVE_FREE_CELL_META_PTR(iter, size);
        buf = GET_ALLOC_CELL_META(iter);
        buf->size = size;
        buf->check = (uintptr_t)iter;
        i++;
        iter = MOVE_FREE_CELL_META_PTR(iter, size);
    }
    iter->next = NULL;
    buf = GET_ALLOC_CELL_META(iter);
    buf->size = size;
    buf->check = (uintptr_t)iter;
    category->head = newhead;
}

talmetamemory_t* tal_pool_allocate(talstate_t* state, talcategory_t* category, int64_t size)
{
    talmetamemory_t* ret;
    talmetamemory_t* meta;
    (void)meta;
    TALLOC_LOCK(category->flag);
    if(category->head == NULL)
    {
        tal_pool_newcategory(state, category, size);
    }
    ret = category->head;
    meta = GET_ALLOC_CELL_META(ret);
    ASSERT(meta->check == (uintptr_t)ret, "pool corrupted");
    category->head = category->head->next;
    category->used++;
    TALLOC_UNLOCK(category->flag);
    return ret;
}

void tal_pool_deallocate(talstate_t* state, talcategory_t* category, talmetamemory_t* freecell)
{
    (void)state;
    TALLOC_LOCK(category->flag);
    freecell->next = category->head;
    category->head = freecell;
    category->used--;
    TALLOC_UNLOCK(category->flag);
}

void* tal_pool_malloc(talstate_t* state, int64_t count)
{
    int64_t categoryid;
    talcategory_t* category;
    count = tal_pool_cellsize(state, count);
    categoryid = SIZE_TO_CATEGORY(count);
    ASSERT(categoryid < CATEGORY_COUNT, "pool category overflow");
    category = &state->categories[categoryid];
    return tal_pool_allocate(state, category, count);
}

void tal_pool_free(talstate_t* state, void* ptr)
{
    int64_t size;
    int64_t categoryid;
    talmetamemory_t* cell;
    talmetamemory_t* freecell;
    talcategory_t* category;
    cell = GET_ALLOC_CELL_META(ptr);
    size = cell->size;
    ASSERT(cell->check == (uintptr_t)ptr, "pool corrupted");
    categoryid = SIZE_TO_CATEGORY(size);
    /*fprintf(stderr, "categoryid=%ld CATEGORY_COUNT=%ld\n", categoryid, CATEGORY_COUNT);*/
    ASSERT(categoryid < CATEGORY_COUNT, "pool category overflow");
    category = &state->categories[categoryid];
    freecell = (talmetamemory_t*)ptr;
    tal_pool_deallocate(state, category, freecell);
}

int64_t tal_pool_cellsize(talstate_t* state, int64_t size)
{
    (void)state;
    if(size < (int64_t)sizeof(talmetamemory_t))
    {
        size = sizeof(talmetamemory_t);
    }
    size += sizeof(talmetamemory_t);
    return NEXT_MULT_OF(size, TALLOC_POOL_GROUP_MULT);
}

void tal_pool_optimize(talstate_t* state)
{
    int64_t i;
    talcategory_t* c;
    talmetamemory_t* prev;
    talmetamemory_t* current;
    c = NULL;
    for(i = 0; i < CATEGORY_COUNT; i++)
    {
        c = &state->categories[i];
        TALLOC_LOCK(c->flag);
        /* check category, when its not used -> clean up all its allocated pools*/
        if(!c->used)
        {
            current = c->next_pool;
            prev = NULL;
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
    size_t mask;
    uintptr_t iunaligned;
    uintptr_t misalignment;
    if(tal_util_isaligned(state, *p, alignment))
    {
        *adjustment = 0;
        return;
    }
    mask = alignment - 1;
    ASSERT((alignment & mask) == 0, "wrong alignemet");/* pwr of 2*/
    iunaligned = (uintptr_t)(*p);
    misalignment = iunaligned & mask;
    *adjustment = alignment - misalignment;
    *p = (void*)(iunaligned + *adjustment);
}

void tal_util_alignptrwithheader(talstate_t* state, void** p, size_t alignment, int64_t header_size, ptrdiff_t* adjustment)
{
    unsigned char* chptr;
    chptr = (unsigned char*)(*p);
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
    int64_t vs;
    vs = (sizeof(void*) * VECTOR_INIT_CAPACITY);
    vec->size = 0;
    vec->capacity = VECTOR_INIT_CAPACITY;
    vec->data = malloc(vs);
    memset(vec->data, 0, vs);
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
    {
        return NULL;
    }
    if(tal_pool_cellsize(state, count) <= TALLOC_SMALL_TO)
    {
        return tal_pool_malloc(state, count);
    }
    return tal_heap_malloc(state, count);
}

void* tal_state_realloc(talstate_t* state, void* from, int64_t size)
{
    int64_t cpysize;
    int64_t bmsize;
    void* dest;
    bmsize = 0;
    cpysize = size;
    if(!from)
    {
        return tal_state_malloc(state, size);
    }
    #if 1
        talmetamemory_t* block = GET_UNI_META_PTR(from);
        if(block != NULL)
        {
            if(block->check == (uintptr_t)from)
            {
                bmsize = block->size;
                if(bmsize > 0)
                {
                    #if 0
                        fprintf(stderr, "realloc: block: size=%ld reallocsize=%ld\n", block->size, size);
                    #endif
                    cpysize = bmsize;
                }
            }
        }
    #endif
    dest = tal_state_malloc(state, size);
    memcpy(dest, from, cpysize);
    tal_state_free(state, from);
    return dest;
}


void* tal_state_calloc(talstate_t* state, int64_t nelem, int64_t elsize)
{
    int64_t size;
    void* dest;
    size = nelem * elsize;
    dest = tal_state_malloc(state, size);
    memset(dest, 0, size);
    return dest;
}

void tal_state_free(talstate_t* state, void* ptr)
{
    talmetamemory_t* block;
    if(!ptr)
    {
        return;
    }
    block = GET_UNI_META_PTR(ptr);
    #if 0
    if(block->check != (uintptr_t)ptr)
    {
        ABORT("pointer being freed was not allocated");
    }
    #endif
    if(block->size <= TALLOC_SMALL_TO)
    {
        tal_pool_free(state, ptr);
        return;
    }
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
    tal_pool_optimize(state);
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
