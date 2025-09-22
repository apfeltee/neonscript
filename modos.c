
#include <time.h>
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
    NEON_ARGS_CHECKTYPE(&check, 1, nn_value_iscallable);

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
            nn_nestcall_callfunction(state, callable, thisval, nestargs, 1, &res, false);
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

/*
NNValue nn_modfn_os_$template(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    int64_t r;
    int64_t mod;
    NNObjString* path;
    NNArgCheck check;
    (void)thisval;
    nn_argcheck_init(state, &check, "chmod", argv, argc);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
    NEON_ARGS_CHECKTYPE(&check, 1, nn_value_isnumber);
    path = nn_value_asstring(argv[0]);
    mod = nn_value_asnumber(argv[1]);
    r = osfn_chmod(nn_string_getdata(path), mod);
    return nn_value_makenumber(r);
}
*/

NNValue nn_modfn_os_chmod(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    int64_t r;
    int64_t mod;
    NNObjString* path;
    NNArgCheck check;
    (void)thisval;
    nn_argcheck_init(state, &check, "chmod", argv, argc);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
    NEON_ARGS_CHECKTYPE(&check, 1, nn_value_isnumber);
    path = nn_value_asstring(argv[0]);
    mod = nn_value_asnumber(argv[1]);
    r = osfn_chmod(nn_string_getdata(path), mod);
    return nn_value_makenumber(r);
}

NNValue nn_modfn_os_mkdir(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    int64_t r;
    int64_t mod;
    NNObjString* path;
    NNArgCheck check;
    (void)thisval;
    nn_argcheck_init(state, &check, "mkdir", argv, argc);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
    NEON_ARGS_CHECKTYPE(&check, 1, nn_value_isnumber);
    path = nn_value_asstring(argv[0]);
    mod = nn_value_asnumber(argv[1]);
    r = osfn_mkdir(nn_string_getdata(path), mod);
    return nn_value_makenumber(r);
}


NNValue nn_modfn_os_chdir(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    int64_t r;
    NNObjString* path;
    NNArgCheck check;
    (void)thisval;
    nn_argcheck_init(state, &check, "chdir", argv, argc);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
    path = nn_value_asstring(argv[0]);
    r = osfn_chdir(nn_string_getdata(path));
    return nn_value_makenumber(r);
}

NNValue nn_modfn_os_rmdir(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    int64_t r;
    NNObjString* path;
    NNArgCheck check;
    (void)thisval;
    nn_argcheck_init(state, &check, "rmdir", argv, argc);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
    NEON_ARGS_CHECKTYPE(&check, 1, nn_value_isnumber);
    path = nn_value_asstring(argv[0]);
    r = osfn_rmdir(nn_string_getdata(path));
    return nn_value_makenumber(r);
}

NNValue nn_modfn_os_unlink(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    int64_t r;
    NNObjString* path;
    NNArgCheck check;
    (void)thisval;
    nn_argcheck_init(state, &check, "unlink", argv, argc);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
    path = nn_value_asstring(argv[0]);
    r = osfn_unlink(nn_string_getdata(path));
    return nn_value_makenumber(r);
}

NNValue nn_modfn_os_getenv(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    const char* r;
    NNObjString* key;
    NNArgCheck check;
    (void)thisval;
    nn_argcheck_init(state, &check, "getenv", argv, argc);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
    key = nn_value_asstring(argv[0]);
    r = osfn_getenv(nn_string_getdata(key));
    if(r == NULL)
    {
        return nn_value_makenull();
    }
    return nn_value_fromobject(nn_string_copycstr(state, r));
}

NNValue nn_modfn_os_setenv(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNObjString* key;
    NNObjString* value;
    NNArgCheck check;
    (void)thisval;
    nn_argcheck_init(state, &check, "setenv", argv, argc);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
    NEON_ARGS_CHECKTYPE(&check, 1, nn_value_isstring);
    key = nn_value_asstring(argv[0]);
    value = nn_value_asstring(argv[1]);
    return nn_value_makebool(osfn_setenv(nn_string_getdata(key), nn_string_getdata(value), true));
}

NNValue nn_modfn_os_cwdhelper(NNState* state, NNValue thisval, NNValue* argv, size_t argc, const char* name)
{
    enum { kMaxBufSz = 1024 };
    NNArgCheck check;
    char* r;
    char buf[kMaxBufSz];
    (void)thisval;
    nn_argcheck_init(state, &check, name, argv, argc);
    r = osfn_getcwd(buf, kMaxBufSz);
    if(r == NULL)
    {
        return nn_value_makenull();
    }
    return nn_value_fromobject(nn_string_copycstr(state, r));
}

NNValue nn_modfn_os_cwd(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    return nn_modfn_os_cwdhelper(state, thisval, argv, argc, "cwd");
}

NNValue nn_modfn_os_pwd(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    return nn_modfn_os_cwdhelper(state, thisval, argv, argc, "pwd");
}

NNValue nn_modfn_os_basename(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    const char* r;
    NNObjString* path;
    NNArgCheck check;
    (void)thisval;
    nn_argcheck_init(state, &check, "basename", argv, argc);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
    path = nn_value_asstring(argv[0]);
    r = osfn_basename(nn_string_getdata(path));
    if(r == NULL)
    {
        return nn_value_makenull();
    }
    return nn_value_fromobject(nn_string_copycstr(state, r));
}

NNValue nn_modfn_os_dirname(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    const char* r;
    NNObjString* path;
    NNArgCheck check;
    (void)thisval;
    nn_argcheck_init(state, &check, "dirname", argv, argc);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
    path = nn_value_asstring(argv[0]);
    r = osfn_dirname(nn_string_getdata(path));
    if(r == NULL)
    {
        return nn_value_makenull();
    }
    return nn_value_fromobject(nn_string_copycstr(state, r));
}

NNValue nn_modfn_os_touch(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    FILE* fh;
    NNObjString* path;
    NNArgCheck check;
    (void)thisval;
    nn_argcheck_init(state, &check, "touch", argv, argc);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
    path = nn_value_asstring(argv[0]);
    fh = fopen(nn_string_getdata(path), "rb");
    if(fh == NULL)
    {
        return nn_value_makebool(false);
    }
    fclose(fh);
    return nn_value_makebool(true);
}


static const char* modetoname(int t)
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

static const char* ctime_helper(const time_t *timep)
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


NNValue nn_modfn_os_stat(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    int imode;
    int blksize;
    int blocks;
    NNObjString* path;
    NNArgCheck check;
    NNObjDict* md;

    const char* strp;
    struct stat st;
    (void)thisval;
    nn_argcheck_init(state, &check, "stat", argv, argc);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
    path = nn_value_asstring(argv[0]);
    strp = nn_string_getdata(path);
    if(stat(nn_string_getdata(path), &st) == -1)
    {
        nn_except_throwclass(state, state->exceptions.ioerror, "%s: %s", strp, strerror(errno));
        return nn_value_makenull();
    }
    md = nn_object_makedict(state);
    nn_dict_addentrycstr(md, "path", nn_value_fromobject(path));
    imode = (st.st_mode & S_IFMT);
    #if !defined(_WIN32) && !defined(_WIN64)
        blksize = st.st_blksize;
        blocks = st.st_blocks;
    #else
        blksize = 8;
        blocks = (1024 * 4);
    #endif
    #if 0
        nn_dict_addentrycstr(md, "id", major(st.st_dev) & minor(st.st_dev));
    #endif

    nn_dict_addentrycstr(md, "type", nn_value_fromobject(nn_string_copycstr(state, modetoname(imode))));
    nn_dict_addentrycstr(md, "inode", nn_value_makenumber((uintmax_t) st.st_ino));
    nn_dict_addentrycstr(md, "mode", nn_value_makenumber((uintmax_t) st.st_mode));
    nn_dict_addentrycstr(md, "links", nn_value_makenumber((uintmax_t) st.st_nlink));
    nn_dict_addentrycstr(md, "uid", nn_value_makenumber((uintmax_t) st.st_uid)),
    nn_dict_addentrycstr(md, "gid", nn_value_makenumber((uintmax_t) st.st_gid));
    nn_dict_addentrycstr(md, "blocksize", nn_value_makenumber(blksize));
    nn_dict_addentrycstr(md, "blocks", nn_value_makenumber(blocks));

    nn_dict_addentrycstr(md, "filesize", nn_value_makenumber((intmax_t) st.st_size));
    nn_dict_addentrycstr(md, "lastchanged", nn_value_fromobject(nn_string_copycstr(state, ctime_helper(&st.st_ctime))));
    nn_dict_addentrycstr(md, "lastaccess", nn_value_fromobject(nn_string_copycstr(state, ctime_helper(&st.st_atime))));
    nn_dict_addentrycstr(md, "lastmodified", nn_value_fromobject(nn_string_copycstr(state, ctime_helper(&st.st_mtime))));
    return nn_value_fromobject(md);
}

NNDefModule* nn_natmodule_load_os(NNState* state)
{
    static NNDefFunc modfuncs[] =
    {
        {"readdir",   true,  nn_modfn_os_readdir},
        {"chmod",   true,  nn_modfn_os_chmod},
        {"chdir",   true,  nn_modfn_os_chdir},
        {"mkdir",   true,  nn_modfn_os_mkdir},
        {"unlink",   true,  nn_modfn_os_unlink},
        {"getenv",   true,  nn_modfn_os_getenv},
        {"setenv",   true,  nn_modfn_os_setenv},
        {"rmdir",   true,  nn_modfn_os_rmdir},
        {"pwd",   true,  nn_modfn_os_pwd},
        {"pwd",   true,  nn_modfn_os_cwd},
        {"basename",   true,  nn_modfn_os_basename},
        {"dirname",   true,  nn_modfn_os_dirname},
        {"touch",   true,  nn_modfn_os_touch},
        {"stat",   true,  nn_modfn_os_stat},

        /* todo: implement these! */
        #if 0
        /* shell-like directory state - might be trickier */
        #endif
        {NULL,     false, NULL},
    };
    static NNDefField modfields[] =
    {
        /*{"platform", true, get_os_platform},*/
        {NULL,       false, NULL},
    };
    static NNDefModule module;
    (void)state;
    module.name = "os";
    module.definedfields = modfields;
    module.definedfunctions = modfuncs;
    module.definedclasses = NULL;
    module.fnpreloaderfunc = &nn_modfn_os_preloader;
    module.fnunloaderfunc = NULL;
    return &module;
}
