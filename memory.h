
#pragma once

#include <new>

namespace neon
{
    class Memory
    {
        public:
            static void* osRealloc(void* ptr, size_t size)
            {
                return realloc(ptr, size);
            }

            static void* osMalloc(size_t size)
            {
                return malloc(size);
            }

            static void* osCalloc(size_t count, size_t size)
            {
                return calloc(count, size);
            }

            static void osFree(void* ptr)
            {
                free(ptr);
            }


            template<typename ValType>
            static inline ValType* growArray(ValType* pointer, size_t oldcount, size_t newcount)
            {
                size_t tsz;
                (void)oldcount;
                (void)tsz;
                tsz = sizeof(ValType);
                return (ValType*)Memory::osRealloc(pointer, tsz*newcount);
            }

            template<typename ValType>
            static inline void freeArray(ValType* pointer, size_t oldcount)
            {
                size_t tsz;
                (void)oldcount;
                (void)tsz;
                tsz = sizeof(ValType);
                osFree(pointer);
            }

            template<typename Type, typename... ArgsT>
            static inline Type* create(ArgsT&&... args)
            {
                Type* rt;
                Type* buf;
                buf = (Type*)osMalloc(sizeof(Type));
                rt = new(buf) Type(args...);
                return rt;
            }

            template<typename Type>
            static inline void destroy(Type* ptr)
            {
                ptr->~Type();
                osFree(ptr);
            }
    };

}
