
#include <stdlib.h>
#include <limits.h>
#include "os.h"


char* osfn_utilstrndup(const char* src, size_t len)
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
    memset(itm->name, 0, TINDIR_PATHSIZE);
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
            return rt;
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
        dirpart = (char *)malloc (dirlen + 1);
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
    return getpid();
}

int osfn_kill(int pid, int code)
{
    #if defined(OSFN_ISUNIXLIKE)
        return kill(pid, code);
    #else
        return -1;
    #endif
}

