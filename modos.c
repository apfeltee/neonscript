
#include <signal.h>
#include "neon.h"

#if defined(__unix__) || defined(__linux__)
    #define OSFN_ISUNIXLIKE 1
#endif

#if defined(__unix__) || defined(__linux__)
    #define OSFN_ISLINUX
#elif defined(_WIN32) || defined(_WIN64)
    #define OSFN_ISWINNT
#endif

#if defined(OSFN_ISLINUX)
    #include <unistd.h>
    #include <dirent.h>
    #include <libgen.h>
    #include <sys/time.h>
#else
    #if defined(OSFN_ISWINNT)
        #include <windows.h>
    #endif
#endif

#ifndef S_IREAD
    #define S_IREAD     0400
#endif /* S_IREAD */
#ifndef S_IWRITE
    #define S_IWRITE    0200
#endif /* S_IWRITE */
#ifndef S_IEXEC
    #define S_IEXEC     0100
#endif /* S_IEXEC */


#if !defined(S_IRUSR)
    #define S_IRUSR (S_IREAD)
#endif
#if !defined(S_IWUSR)
    #define S_IWUSR (S_IWRITE)
#endif
#if !defined(S_IXUSR)
    #define S_IXUSR (S_IEXEC)
#endif
#if !defined(S_IRGRP)
    #define S_IRGRP (S_IRUSR >> 3)
#endif
#if !defined(S_IWGRP)
    #define S_IWGRP (S_IWUSR >> 3)
#endif
#if !defined(S_IXGRP)
    #define S_IXGRP (S_IXUSR >> 3)
#endif
#if !defined(S_IROTH)
    #define S_IROTH (S_IRUSR >> 6)
#endif
#if !defined(S_IWOTH)
    #define S_IWOTH (S_IWUSR >> 6)
#endif
#if !defined(S_IXOTH)
    #define S_IXOTH (S_IXUSR >> 6)
#endif
#if !defined(S_IRWXU)
    #define S_IRWXU (S_IRUSR|S_IWUSR|S_IXUSR)
#endif
#if !defined(S_IRWXG)
    #define S_IRWXG (S_IRGRP|S_IWGRP|S_IXGRP)
#endif
#if !defined(S_IRWXO)
    #define S_IRWXO (S_IROTH|S_IWOTH|S_IXOTH)
#endif

#if !defined(S_IFLNK)
    #define S_IFLNK 0120000
#endif

#if !defined (S_ISDIR)
    #define	S_ISDIR(m)	(((m)&S_IFMT) == S_IFDIR)	/* directory */
#endif
#if !defined (S_ISREG)
    #define	S_ISREG(m)	(((m)&S_IFMT) == S_IFREG)	/* file */
#endif
#if !defined(S_ISLNK)
    #define S_ISLNK(m)    (((m) & S_IFMT) == S_IFLNK)
#endif

#if !defined(DT_DIR)
    #define DT_DIR 4
#endif
#if !defined(DT_REG)
    #define DT_REG 8
#endif

#if !defined(PATH_MAX)
    #define PATH_MAX 1024
#endif

char* osfn_utilstrndup(const char* src, size_t len)
{
    char* buf;
    buf = (char*)nn_memory_malloc(len+1);
    if(buf == NULL)
    {
        return NULL;
    }
    memset(buf, 0, len+1);
    memcpy(buf, src, len);
    return buf;
}

char* osfn_utilstrdup(const char* src)
{
    return osfn_utilstrndup(src, strlen(src));
}

bool fslib_diropen(FSDirReader* rd, const char* path)
{
    #if defined(OSFN_ISLINUX)
        if((rd->handle = opendir(path)) == NULL)
        {
            return false;
        }
        return true;
    #endif
    return false;
}

bool fslib_dirread(FSDirReader* rd, FSDirItem* itm)
{
    itm->isdir = false;
    itm->isfile = false;
    memset(itm->name, 0, NEON_CONF_OSPATHSIZE);
    #if defined(OSFN_ISLINUX)
        struct dirent* ent;
        if((ent = readdir((DIR*)(rd->handle))) == NULL)
        {
            return false;
        }
        if(ent->d_type == DT_DIR)
        {
            itm->isdir = true;
        }
        if(ent->d_type == DT_REG)
        {
            itm->isfile = true;
        }
        strcpy(itm->name, ent->d_name);
        return true;
    #endif
    return false;
}

bool fslib_dirclose(FSDirReader* rd)
{
    #if defined(OSFN_ISLINUX)
        closedir((DIR*)(rd->handle));
    #endif
    return false;
}

FILE* osfn_popen(const char* cmd, const char* type)
{
    #if defined(OSFN_ISLINUX)
        return popen(cmd, type);
    #endif
    return NULL;
}

void osfn_pclose(FILE* fh)
{
    #if defined(OSFN_ISLINUX)
        pclose(fh);
    #endif
}

int osfn_chmod(const char* path, int mode)
{
    #if defined(OSFN_ISUNIXLIKE)
        return chmod(path, mode);
    #else
        return -1;
    #endif
}

char* osfn_realpath(const char* path, char* respath)
{
    char* copy;
    #if defined(OSFN_ISUNIXLIKE)
        char* rt;
        rt = realpath(path, respath);
        if(rt != NULL)
        {
            copy = osfn_utilstrdup(rt);
            free(rt);
            return copy;
        }
    #else
    #endif
    copy = osfn_utilstrdup(path);
    respath = copy;
    return copy;

}

char* osfn_dirname(const char *fname)
{
    const char *p  = fname;
    const char *slash = NULL;
    if(fname)
    {
        size_t dirlen;
        char * dirpart;
        if(*fname && fname[1] == ':')
        {
            slash = fname + 1;
            p += 2;
        }
        /* Find the rightmost slash.  */
        while (*p)
        {
            if (*p == '/' || *p == '\\')
            {
                slash = p;
            }
            p++;
        }
        if(slash == NULL)
        {
            fname = ".";
            dirlen = 1;
        }
        else
        {
            /* Remove any trailing slashes.  */
            while(slash > fname && (slash[-1] == '/' || slash[-1] == '\\'))
            {
                slash--;
            }
            /* How long is the directory we will return?  */
            dirlen = slash - fname + (slash == fname || slash[-1] == ':');
            if (*slash == ':' && dirlen == 1)
            {
                dirlen += 2;
            }
        }
        dirpart = (char *)nn_memory_malloc(dirlen + 1);
        if(dirpart != NULL)
        {
            strncpy (dirpart, fname, dirlen);
            if (slash && *slash == ':' && dirlen == 3)
            {
                dirpart[2] = '.';	/* for "x:foo" return "x:." */
            }
            dirpart[dirlen] = '\0';
        }
        return dirpart;
    }
    return NULL;
}

char* osfn_fallbackbasename(const char* opath)
{
    char* strbeg;
    char* strend;
    char* cpath;
    strend = cpath = (char*)opath;
    while (*strend)
    {
        strend++;
    }
    while (strend > cpath && strend[-1] == '/')
    {
        strend--;
    }
    strbeg = strend;
    while (strbeg > cpath && strbeg[-1] != '/')
    {
        strbeg--;
    }
    /* len = (strend - strbeg) */
    cpath = strbeg;
    cpath[(strend - strbeg)] = 0;
    return strbeg;
}

char* osfn_basename(const char* path)
{
    #if defined(OSFN_ISUNIXLIKE)
        char* rt;
        rt = basename((char*)path);
        if(rt != NULL)
        {
            return rt;
        }
    #else
    #endif
    return osfn_fallbackbasename(path);
}


int osfn_isatty(int fd)
{
    #if defined(OSFN_ISUNIXLIKE)
        return isatty(fd);
    #else
        return 0;
    #endif
}

int osfn_symlink(const char* path1, const char* path2)
{
    #if defined(OSFN_ISUNIXLIKE)
        return symlink(path1, path2);
    #else
        return -1;
    #endif
}

int osfn_symlinkat(const char* path1, int fd, const char* path2)
{
    #if defined(OSFN_ISUNIXLIKE)
        return symlinkat(path1, fd, path2);
    #else
        return -1;
    #endif
}

char* osfn_getcwd(char* buf, size_t size)
{
    #if defined(OSFN_ISUNIXLIKE)
        return getcwd(buf, size);
    #else
        #if defined(OSFN_ISWINNT)
            GetCurrentDirectory(size, buf);
            return buf;
        #endif
    #endif
    return NULL;

}

int osfn_lstat(const char* path, struct stat* buf)
{
    #if defined(OSFN_ISUNIXLIKE)
        return lstat(path, buf);
    #else
        return stat(path, buf);
    #endif
}


int osfn_truncate(const char* path, size_t length)
{
    #if defined(OSFN_ISUNIXLIKE)
        return truncate(path, length);
    #else
        return -1;
    #endif
}

unsigned int osfn_sleep(unsigned int seconds)
{
    #if defined(OSFN_ISUNIXLIKE)
        return sleep(seconds);
    #else
        return 0;
    #endif
}

int osfn_gettimeofday(struct timeval* tp, void* tzp)
{
    #if defined(OSFN_ISUNIXLIKE)
        return gettimeofday(tp, tzp);
    #else
        return 0;
    #endif
}

int osfn_mkdir(const char* path, size_t mode)
{
    #if defined(OSFN_ISUNIXLIKE)
        return mkdir(path, mode);
    #else
        return -1;
    #endif
}


int osfn_chdir(const char* path)
{
    #if defined(OSFN_ISUNIXLIKE)
        return chdir(path);
    #else
        return -1;
    #endif
}

int osfn_getpid()
{
    #if defined(OSFN_ISUNIXLIKE)
        return getpid();
    #endif
    return -1;
}

int osfn_kill(int pid, int code)
{
    #if defined(OSFN_ISUNIXLIKE)
        return kill(pid, code);
    #else
        return -1;
    #endif
}

void nn_modfn_os_preloader(NNState* state)
{
    (void)state;
}

NNValue nn_modfn_os_readdir(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    const char* dirn;
    FSDirReader rd;
    FSDirItem itm;
    NNValue res;
    NNValue itemval;
    NNValue callable;
    NNObjString* os;
    NNObjString* itemstr;
    NNArgCheck check;
    NNValue nestargs[2];
    nn_argcheck_init(state, &check, "readdir", argv, argc);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
    os = nn_value_asstring(argv[0]);
    callable = argv[1];
    dirn = os->sbuf.data;
    if(fslib_diropen(&rd, dirn))
    {
        while(fslib_dirread(&rd, &itm))
        {
            #if 0
                itemstr = nn_string_intern(state, itm.name);
            #else
                itemstr = nn_string_copycstr(state, itm.name);
            #endif
            itemval = nn_value_fromobject(itemstr);
            nestargs[0] = itemval;
            nn_nestcall_callfunction(state, callable, thisval, nestargs, 1, &res);
        }
        fslib_dirclose(&rd);
        return nn_value_makenull();
    }
    else
    {
        nn_except_throw(state, "cannot open directory '%s'", dirn);
    }
    return nn_value_makenull();
}



