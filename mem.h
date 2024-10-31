
#ifndef __libmc_mem_h__
#define __libmc_mem_h__

void* mc_memory_malloc(size_t sz);
void* mc_memory_realloc(void* p, size_t nsz);
void* mc_memory_calloc(size_t count, size_t typsize);
void mc_memory_free(void* ptr);

#endif /* __libmc_mem_h__ */
