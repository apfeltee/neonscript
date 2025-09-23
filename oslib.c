
#include "oslib.h"

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
    size_t dirlen;
    char * dirpart;
    const char *p;
    const char *slash;
    p = fname;
    slash = NULL;
    if(fname)
    {
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
            strncpy(dirpart, fname, dirlen);
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
    while(*strend)
    {
        strend++;
    }
    while(strend > cpath && strend[-1] == '/')
    {
        strend--;
    }
    strbeg = strend;
    while(strbeg > cpath && strbeg[-1] != '/')
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

int osfn_rmdir(const char* path)
{
    #if defined(OSFN_ISUNIXLIKE)
        return rmdir(path);
    #else
        return -1;
    #endif
}

int osfn_unlink(const char* path)
{
    #if defined(OSFN_ISUNIXLIKE)
        return unlink(path);
    #else
        return -1;
    #endif
}


const char* osfn_getenv(const char* key)
{
    return getenv(key);
}

bool osfn_setenv(const char* key, const char* value, bool replace)
{
    #if defined(OSFN_ISUNIXLIKE)
        return (setenv(key, value, replace) == 0);
    #else
        int errcode;
        size_t envsize;
        errcode = 0;
        if(!replace)
        {
            envsize = 0;
            errcode = getenv_s(&envsize, NULL, 0, key);
            if(errcode || envsize) return errcode;
        }
        if(_putenv_s(key, value) == -1)
        {
            return false;
        }
        return true;
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

bool nn_util_fsfileexists(NNState* state, const char* filepath)
{
    (void)state;
    #if !defined(NEON_PLAT_ISWINDOWS)
        return access(filepath, F_OK) == 0;
    #else
        struct stat st;
        if(stat(filepath, &st) == -1)
        {
            return false;
        }
        return true;
    #endif
}

bool nn_util_fsfileistype(NNState* state, const char* filepath, int typ)
{
    struct stat st;
    (void)state;
    (void)filepath;
    if(stat(filepath, &st) == -1)
    {
        return false;
    }
    if(typ == 'f')
    {
        return S_ISREG(st.st_mode);
    }
    else if(typ == 'd')
    {
        return S_ISDIR(st.st_mode);
    }
    return false;
}

bool nn_util_fsfileisfile(NNState* state, const char* filepath)
{
    return nn_util_fsfileistype(state, filepath, 'f');
}

bool nn_util_fsfileisdirectory(NNState* state, const char* filepath)
{
    return nn_util_fsfileistype(state, filepath, 'd');
}

char* nn_util_fsgetbasename(NNState* state, char* path)
{
    (void)state;
    return osfn_basename(path);
}

static const char* nn_filestat_internmodetoname(int t)
{
    switch(t)
    {
        #if defined(S_IFBLK)
            case S_IFBLK:
                return "blockdevice";
                break;
        #endif
        #if defined(S_IFCHR)
            case S_IFCHR:
                return "characterdevice";
                break;
        #endif
        #if defined(S_IFDIR)
            case S_IFDIR:
                return "directory";
                break;
        #endif
        #if defined(S_IFIFO)
            case S_IFIFO:
                return "pipe";
                break;
        #endif
        #if defined(S_IFLNK)
            case S_IFLNK:
                return "symlink";
                break;
        #endif
        #if defined(S_IFREG)
            case S_IFREG:
                return "file";
                break;
        #endif
        #if defined(S_IFSOCK)
            case S_IFSOCK:
                return "socket";
                break;
        #endif
            default:
                break;
    }
    return "unknown";
}

const char* nn_filestat_ctimetostring(const time_t *timep)
{
    size_t len;
    char* r;
    r = ctime(timep);
    len = strlen(r);
    if(r[len - 1] == '\n')
    {
        r[len - 1] = 0;
    }
    return r;
}


bool nn_filestat_initempty(NNFSStat* nfs)
{
    memset(nfs, 0, sizeof(NNFSStat));
    return true;
}

bool nn_filestat_setup(NNFSStat* nfs)
{
    nfs->inode = nfs->rawstbuf.st_ino;
    nfs->mode = (nfs->rawstbuf.st_mode & S_IFMT);
    nfs->numlinks = nfs->rawstbuf.st_nlink;
    nfs->owneruid = nfs->rawstbuf.st_uid;
    nfs->ownergid = nfs->rawstbuf.st_gid;
    #if !defined(_WIN32) && !defined(_WIN64)
        nfs->blocksize = nfs->rawstbuf.st_blksize;
        nfs->blockcount = nfs->rawstbuf.st_blocks;
    #else
        nfs->blocksize = 8;
        nfs->blockcount = (1024 * 4);
    #endif
    nfs->filesize = nfs->rawstbuf.st_size;
    nfs->modename = nn_filestat_internmodetoname(nfs->mode);
    nfs->tmlastchanged = (&nfs->rawstbuf.st_ctime);
    nfs->tmlastaccessed = (&nfs->rawstbuf.st_atime);
    nfs->tmlastmodified = (&nfs->rawstbuf.st_mtime);
    return true;
}


bool nn_filestat_initfrompath(NNFSStat* nfs, const char* path)
{
    if(!nn_filestat_initempty(nfs))
    {
        return false;
    }
    if(stat(path, &nfs->rawstbuf) == -1)
    {
        return false;
    }
    return nn_filestat_setup(nfs);
}

