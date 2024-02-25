#pragma once

/*
* this file contains platform-specific functions.
* unless target platform is unix-like, they are all non-functional stubs, that default to
* returning an error value.
*/

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <fcntl.h>

#if defined(__unix__) || defined(__linux__)
    #define OSFN_ISUNIXY 1
#endif

#define OSFN_PATHSIZE 1024
#if defined(__unix__) || defined(__linux__)
    #define OSFN_ISUNIX
#elif defined(_WIN32) || defined(_WIN64)
    #define OSFN_ISWINDOWS
#endif

#if defined(OSFN_ISUNIX)
    #include <unistd.h>
    #include <dirent.h>
    #include <libgen.h>
    #include <sys/time.h>
#else
    #if defined(OSFN_ISWINDOWS)
        #include <windows.h>
        #include <io.h>
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

typedef struct FSDirReader FSDirReader;
typedef struct FSDirItem FSDirItem;

struct FSDirReader
{
    void* handle;
};

struct FSDirItem
{
    char name[OSFN_PATHSIZE + 1];
    bool isdir;
    bool isfile;
};

static char* osfn_utilstrndup(const char* src, size_t len)
{
    char* buf;
    buf = (char*)malloc(len+1);
    if(buf == NULL)
    {
        return NULL;
    }
    memset(buf, 0, len+1);
    memcpy(buf, src, len);
    return buf;
}

static char* osfn_utilstrdup(const char* src)
{
    return osfn_utilstrndup(src, strlen(src));
}

bool fslib_diropen(FSDirReader* rd, const char* path)
{
    #if defined(OSFN_ISUNIX)
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
    memset(itm->name, 0, OSFN_PATHSIZE);
    #if defined(OSFN_ISUNIX)
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
    #if defined(OSFN_ISUNIX)
        closedir((DIR*)(rd->handle));
    #endif
    return false;
}

/*
int open(const char *pathname, int flags);
int open(const char *pathname, int flags, mode_t mode);
int creat(const char *pathname, mode_t mode);
int openat(int dirfd, const char *pathname, int flags);
int openat(int dirfd, const char *pathname, int flags, mode_t mode);
*/

int osfn_fdopen(const char *path, int flags, int mode)
{
    return open(path, flags, mode);
}

int osfn_fdcreat(const char* path, int mode)
{
    return creat(path, mode);
}

int osfn_fdclose(int fd)
{
    return close(fd);
}

size_t osfn_fdread(int fd, void* buf, size_t count)
{
    return read(fd, buf, count);
}

//ssize_t write(int fd, const void *buf, size_t count);
size_t osfn_fdwrite(int fd, const void* buf, size_t count)
{
    return write(fd, buf, count);
}

// alias to osfn_fdwrite -- my 'w' is still broken.
size_t osfn_fdput(int fd, const void* buf, size_t count)
{
    return osfn_fdwrite(fd, buf, count);
}

int osfn_fdsyncfs(int fd)
{
    #if defined(OSFN_ISUNIX) && !defined(__CYGWIN__)
        return syncfs(fd);
    #endif
    return -1;
}

int osfn_fdselect(int nfds, fd_set* readfds, fd_set* writefds, fd_set* exceptfds, struct timeval* timeout)
{
    #if defined(OSFN_ISUNIX)
        return select(nfds, readfds, writefds, exceptfds, timeout);
    #endif
    return -1;
}

FILE* osfn_popen(const char* cmd, const char* type)
{
    #if defined(OSFN_ISUNIX)
        return popen(cmd, type);
    #endif
    return NULL;
}

void osfn_pclose(FILE* fh)
{
    #if defined(OSFN_ISUNIX)
        pclose(fh);
    #endif
}

int osfn_chmod(const char* path, int mode)
{
    #if defined(OSFN_ISUNIXY)
        return chmod(path, mode);
    #else
        return -1;
    #endif
}

char* osfn_realpath(const char* path, char* respath)
{
    char* copy;
    #if defined(OSFN_ISUNIXY)
        char* rt;
        rt = realpath(path, respath);
        if(rt != NULL)
        {
            return rt;
        }
    #else
    #endif
    copy = osfn_utilstrdup(path);
    respath = copy;
    return copy;

}

char* osfn_dirname(const char* path)
{
    char* copy;
    #if defined(OSFN_ISUNIXY)
        char* rt;
        rt = dirname((char*)path);
        if(rt != NULL)
        {
            return rt;
        }
    #else
    #endif
    copy = osfn_utilstrdup(path);
    return copy;
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
    #if defined(OSFN_ISUNIXY)
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
    #if defined(OSFN_ISUNIXY)
        return isatty(fd);
    #else
        return 0;
    #endif
}

int osfn_symlink(const char* path1, const char* path2)
{
    #if defined(OSFN_ISUNIXY)
        return symlink(path1, path2);
    #else
        return -1;
    #endif
}

int osfn_symlinkat(const char* path1, int fd, const char* path2)
{
    #if defined(OSFN_ISUNIXY)
        return symlinkat(path1, fd, path2);
    #else
        return -1;
    #endif
}

char* osfn_getcwd(char* buf, size_t size)
{
    #if defined(OSFN_ISUNIXY)
        return getcwd(buf, size);
    #else
        return NULL;
    #endif
}

int osfn_lstat(const char* path, struct stat* buf)
{
    #if defined(OSFN_ISUNIXY)
        return lstat(path, buf);
    #else
        return stat(path, buf);
    #endif
}


int osfn_truncate(const char* path, size_t length)
{
    #if defined(OSFN_ISUNIXY)
        return truncate(path, length);
    #else
        return -1;
    #endif
}

unsigned int osfn_sleep(unsigned int seconds)
{
    #if defined(OSFN_ISUNIXY)
        return sleep(seconds);
    #else
        return 0;
    #endif
}

int osfn_gettimeofday(struct timeval* tp, void* tzp)
{
    #if defined(OSFN_ISUNIXY)
        return gettimeofday(tp, tzp);
    #else
        return 0;
    #endif
}

int osfn_mkdir(const char* path, size_t mode)
{
    #if defined(OSFN_ISUNIXY)
        return mkdir(path, mode);
    #else
        return -1;
    #endif
}


int osfn_chdir(const char* path)
{
    #if defined(OSFN_ISUNIXY)
        return chdir(path);
    #else
        return -1;
    #endif
}

bool osfn_fileexists(const char* filepath)
{
    #if defined(OSFN_ISUNIXY)
        return access(filepath, F_OK) == 0;
    #else
        struct stat sb;
        if(stat(filepath, &sb) == -1)
        {
            return false;
        }
        return true;
    #endif
}

bool osfn_stat(const char* path, struct stat* st)
{
    if(stat(path, st) == -1)
    {
        return false;
    }
    return true;
}

bool osfn_statisa(const char* path, struct stat* st, char kind)
{
    int fm;
    int need;
    (void)path;
    if(st == NULL)
    {
        return false;
    }
    need = -1;
    #define caseflag(c, r) case c: need=r; break;
    switch(kind)
    {
        #if defined(S_IFBLK)
            caseflag('b', S_IFBLK);
        #endif
        #if defined(S_IFIFO)
            caseflag('i', S_IFIFO);
        #endif
        #if defined(S_IFSOCK)
            caseflag('s', S_IFSOCK);
        #endif
        caseflag('c', S_IFCHR);
        caseflag('d', S_IFDIR);
        caseflag('f', S_IFREG);
    }
    #undef caseflag
    if(need == -1)
    {
        return false;
    }
    fm = (st->st_mode & S_IFMT);
    return (fm == need);
}

bool osfn_pathcheck(const char* path, char mode)
{
    struct stat st;
    if(!osfn_stat(path, &st))
    {
        return false;
    }
    return osfn_statisa(path, &st, mode);
}

bool osfn_pathisfile(const char* path)
{
    return osfn_pathcheck(path, 'f');
}

bool osfn_pathisdirectory(const char* path)
{
    return osfn_pathcheck(path, 'd');
}

