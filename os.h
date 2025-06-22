#pragma once

/*
* this file contains platform-specific functions.
* unless target platform is unix-like, they are all non-functional stubs, that default to
* returning an error value.
*/

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>

#if defined(__unix__) || defined(__linux__)
    #define OSFN_ISUNIXLIKE 1
#endif

#define TINDIR_PATHSIZE 1024
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

typedef struct FSDirReader FSDirReader;
typedef struct FSDirItem FSDirItem;

struct FSDirReader
{
    void* handle;
};

struct FSDirItem
{
    char name[TINDIR_PATHSIZE + 1];
    bool isdir;
    bool isfile;
};

bool fslib_diropen(FSDirReader* rd, const char* path);
bool fslib_dirread(FSDirReader* rd, FSDirItem* itm);
bool fslib_dirclose(FSDirReader* rd);

FILE* osfn_popen(const char* cmd, const char* type);
void osfn_pclose(FILE* fh);
int osfn_chmod(const char *path, int mode);
char* osfn_realpath(const char *path, char *resolved_path);
char* osfn_dirname(const char *path);
char* osfn_basename(const char *path);
int osfn_isatty(int fd);
int osfn_symlink(const char *path1, const char *path2);
int osfn_symlinkat(const char *path1, int fd, const char *path2);
char* osfn_getcwd(char *buf, size_t size);
int osfn_lstat(const char* path, struct stat* buf);
int osfn_truncate(const char *path, size_t length);
unsigned int osfn_sleep(unsigned int seconds);
int osfn_gettimeofday(struct timeval* tp, void* tzp);
int osfn_mkdir(const char *path, size_t mode);
int osfn_chdir(const char *path);
int osfn_getpid();
int osfn_kill(int pid, int code);

