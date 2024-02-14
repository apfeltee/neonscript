
#include <stdbool.h>
#include "wrapdlfcn.h"

#if defined(LIBWRAPDL_ISWINDOWS)
static struct
{
    char* lasterror;
    const char* originfuncname;
} wdlfcn_errinfo = { 0, NULL };


char* dlwrap_wingetlasterrorstr()
{
    size_t size;
    char* ebuf;
    DWORD emsgid;
    LPSTR msgbuf;
    emsgid = GetLastError();
    if(emsgid == 0)
    {
        return NULL;//No error message has been recorded
    }
    msgbuf = NULL;

    //Ask Win32 to give us the string version of that message ID.
    //The parameters we pass in, tell Win32 to create the buffer that holds the message for us (because we don't yet know how long the message string will be).
    size = FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL, emsgid,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&msgbuf, 0, NULL
    );
    if(size > 0)
    {
        //Copy the error message into a std::string.
        ebuf = (char*)calloc(size, sizeof(char));
        memcpy(ebuf, msgbuf, size - 1);
        ebuf[size - 1] = '\0';
        //Free the Win32's string's buffer.
        LocalFree(msgbuf);
        return ebuf;
    }
    return NULL;
}
#endif

WrapDL* dlwrap_dlopen(const char* filename, int flags)
{
    WrapDL* dlw;
    #if defined(LIBWRAPDL_ISWINDOWS)
    HINSTANCE hinst;
    #endif
    dlw = (WrapDL*)malloc(sizeof(WrapDL));
    if(dlw == NULL)
    {
        return NULL;
    }
    #if defined(LIBWRAPDL_ISLINUX)
        dlw->handle = dlopen(filename, flags);
    #elif defined(LIBWRAPDL_ISWINDOWS)
        hinst = LoadLibrary(filename);
        if(hinst == NULL)
        {
            wdlfcn_errinfo.lasterror = dlwrap_wingetlasterrorstr();
            wdlfcn_errinfo.originfuncname = "dlopen";
        }
        dlw->handle = hinst;
    #endif
    return dlw;
}

int dlwrap_dlclose(WrapDL* dlw)
{
    int rc;
    bool ok;
    (void)ok;
    rc = 0;
    #if defined(LIBWRAPDL_ISLINUX)
        dlclose(dlw->handle);
    #elif defined(LIBWRAPDL_ISWINDOWS)
        ok = FreeLibrary((HINSTANCE)dlw->handle);
        if(!ok)
        {
            wdlfcn_errinfo.lasterror = dlwrap_wingetlasterrorstr();
            wdlfcn_errinfo.originfuncname = "dlclose";
            rc = -1;
        }
    #endif
    free(dlw);
    return rc;
}

void* dlwrap_dlsym(WrapDL* dlw, const char* name)
{
    #if defined(LIBWRAPDL_ISLINUX)
        return dlsym(dlw->handle, name);
    #elif defined(LIBWRAPDL_ISWINDOWS)
        FARPROC fp;
        fp = GetProcAddress((HINSTANCE)dlw->handle, name);
        if(!fp)
        {
            wdlfcn_errinfo.lasterror = dlwrap_wingetlasterrorstr();
            wdlfcn_errinfo.originfuncname = "dlsym";
        }
        return (void*)(intptr_t)fp;
    #endif
    return NULL;
}

const char* dlwrap_dlerror()
{
    #if defined(LIBWRAPDL_ISLINUX)
        return dlerror();
    #elif defined(LIBWRAPDL_ISWINDOWS)
        return (const char*)wdlfcn_errinfo.lasterror;
    #endif
    return "no error";
}



