
#pragma once


#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32) || defined(_WIN64)
    #include <windows.h>
    #define LIBWRAPDL_ISWINDOWS
#else
    #include <dlfcn.h>
    #if defined(__wasi__) || defined(__wasm__)
        #define LIBWRAPDL_ISWASM
    #else
        #define LIBWRAPDL_ISLINUX
    #endif
#endif

/*
// Based on Stack Overflow answer at https://stackoverflow.com/a/53532799/5125586
// @TODO: Make implementation thread safe...
*/
#if defined(LIBWRAPDL_ISWINDOWS)
    #define WRAPDL_RTLD_GLOBAL 0x100 /* do not hide entries in this module */
    #define WRAPDL_RTLD_LOCAL 0x000 /* hide entries in this module */
    #define WRAPDL_RTLD_LAZY 0x000 /* accept unresolved externs */
    #define WRAPDL_RTLD_NOW 0x001 /* abort if module has unresolved externs */
#else
    #define WRAPDL_RTLD_GLOBAL RTLD_GLOBAL
    #define WRAPDL_RTLD_LOCAL RTLD_LOCAL
    #define WRAPDL_RTLD_LAZY RTLD_LAZY
    #define WRAPDL_RTLD_NOW RTLD_NOW  
#endif

#if !defined(WRAPDL_RTLD_GLOBAL)
    #define WRAPDL_RTLD_GLOBAL 0x100 
    #define WRAPDL_RTLD_LOCAL 0x000 
    #define WRAPDL_RTLD_LAZY 0x000 
    #define WRAPDL_RTLD_NOW 0x001
#endif

typedef struct WrapDL WrapDL;

struct WrapDL
{
    void* handle;
};

WrapDL* dlwrap_dlopen(const char* filename, int flags);
int dlwrap_dlclose(WrapDL* dlw);
void* dlwrap_dlsym(WrapDL* dlw, const char* name);
const char* dlwrap_dlerror();
