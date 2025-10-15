
#include <time.h>

#include "neon.h"
#include "oslib.h"

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
    dirn = nn_string_getdata(os);
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

#define putorgetkey(md, name, value) \
    { \
        if(havekey && (keywanted != NULL)) \
        { \
            if(strcmp(name, nn_string_getdata(keywanted)) == 0) \
            { \
                return value; \
            } \
        } \
        else \
        { \
           nn_dict_addentrycstr(md, name, value); \
        } \
    }


NNValue nn_modfn_os_stat(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    bool havekey;
    NNObjString* keywanted;
    NNObjString* path;
    NNArgCheck check;
    NNObjDict* md;
    NNFSStat nfs;
    const char* strp;
    (void)thisval;
    havekey = false;
    keywanted = NULL;
    md = NULL;
    nn_argcheck_init(state, &check, "stat", argv, argc);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
    path = nn_value_asstring(argv[0]);
    if(argc > 1)
    {
        NEON_ARGS_CHECKTYPE(&check, 1, nn_value_isstring);
        keywanted = nn_value_asstring(argv[1]);
        havekey = true;
    }
    strp = nn_string_getdata(path);
    if(!nn_filestat_initfrompath(&nfs, strp))
    {
        nn_except_throwclass(state, state->exceptions.ioerror, "%s: %s", strp, strerror(errno));
        return nn_value_makenull();
    }
    md = NULL;
    if(!havekey)
    {
        md = nn_object_makedict(state);
    }
    putorgetkey(md, "path", nn_value_fromobject(path));
    putorgetkey(md, "mode", nn_value_makenumber(nfs.mode));
    putorgetkey(md, "modename", nn_value_fromobject(nn_string_copycstr(state, nfs.modename)));
    putorgetkey(md, "inode", nn_value_makenumber(nfs.inode));
    putorgetkey(md, "links", nn_value_makenumber(nfs.numlinks));
    putorgetkey(md, "uid", nn_value_makenumber(nfs.owneruid));
    putorgetkey(md, "gid", nn_value_makenumber(nfs.ownergid));
    putorgetkey(md, "blocksize", nn_value_makenumber(nfs.blocksize));
    putorgetkey(md, "blocks", nn_value_makenumber(nfs.blockcount));
    putorgetkey(md, "filesize", nn_value_makenumber(nfs.filesize));
    putorgetkey(md, "lastchanged", nn_value_fromobject(nn_string_copycstr(state, nn_filestat_ctimetostring(nfs.tmlastchanged))));
    putorgetkey(md, "lastaccess", nn_value_fromobject(nn_string_copycstr(state, nn_filestat_ctimetostring(nfs.tmlastaccessed))));
    putorgetkey(md, "lastmodified", nn_value_fromobject(nn_string_copycstr(state, nn_filestat_ctimetostring(nfs.tmlastmodified))));
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
