
#include "neon.h"
#
NNObjFile* nn_object_makefile(NNState* state, FILE* handle, bool isstd, const char* path, const char* mode)
{
    NNObjFile* file;
    file = (NNObjFile*)nn_object_allocobject(state, sizeof(NNObjFile), NEON_OBJTYPE_FILE, false);
    file->isopen = false;
    file->mode = nn_string_copycstr(state, mode);
    file->path = nn_string_copycstr(state, path);
    file->isstd = isstd;
    file->handle = handle;
    file->istty = false;
    file->number = -1;
    if(file->handle != NULL)
    {
        file->isopen = true;
    }
    return file;
}

void nn_file_destroy(NNObjFile* file)
{
    NNState* state;
    state = ((NNObject*)file)->pstate;
    nn_fileobject_close(file);
    nn_gcmem_release(state, file, sizeof(NNObjFile));
}

void nn_file_mark(NNObjFile* file)
{
    NNState* state;
    state = ((NNObject*)file)->pstate;
    nn_gcmem_markobject(state, (NNObject*)file->mode);
    nn_gcmem_markobject(state, (NNObject*)file->path);
}

bool nn_file_read(NNObjFile* file, size_t readhowmuch, NNIOResult* dest)
{
    NNState* state;
    size_t filesizereal;
    struct stat stats;
    state = ((NNObject*)file)->pstate;
    filesizereal = -1;
    dest->success = false;
    dest->length = 0;
    dest->data = NULL;
    if(!file->isstd)
    {
        if(!nn_util_fsfileexists(state, file->path->sbuf.data))
        {
            return false;
        }
        /* file is in write only mode */
        /*
        else if(strstr(file->mode->sbuf.data, "w") != NULL && strstr(file->mode->sbuf.data, "+") == NULL)
        {
            FILE_ERROR(Unsupported, "cannot read file in write mode");
        }
        */
        if(!file->isopen)
        {
            /* open the file if it isn't open */
            nn_fileobject_open(file);
        }
        else if(file->handle == NULL)
        {
            return false;
        }
        if(osfn_lstat(file->path->sbuf.data, &stats) == 0)
        {
            filesizereal = (size_t)stats.st_size;
        }
        else
        {
            /* fallback */
            fseek(file->handle, 0L, SEEK_END);
            filesizereal = ftell(file->handle);
            rewind(file->handle);
        }
        if(readhowmuch == (size_t)-1 || readhowmuch > filesizereal)
        {
            readhowmuch = filesizereal;
        }
    }
    else
    {
        /*
        // for non-file objects such as stdin
        // minimum read bytes should be 1
        */
        if(readhowmuch == (size_t)-1)
        {
            readhowmuch = 1;
        }
    }
    /* +1 for terminator '\0' */
    dest->data = (char*)nn_memory_malloc(sizeof(char) * (readhowmuch + 1));
    if(dest->data == NULL && readhowmuch != 0)
    {
        return false;
    }
    dest->length = fread(dest->data, sizeof(char), readhowmuch, file->handle);
    if(dest->length == 0 && readhowmuch != 0 && readhowmuch == filesizereal)
    {
        return false;
    }
    /* we made use of +1 so we can terminate the string. */
    if(dest->data != NULL)
    {
        dest->data[dest->length] = '\0';
    }
    return true;
}


#define FILE_ERROR(type, message) \
    NEON_RETURNERROR(#type " -> %s", message, file->path->sbuf.data);

#define RETURN_STATUS(status) \
    if((status) == 0) \
    { \
        return nn_value_makebool(true); \
    } \
    else \
    { \
        FILE_ERROR(File, strerror(errno)); \
    }

#define DENY_STD() \
    if(file->isstd) \
    NEON_RETURNERROR("method not supported for std files");

int nn_fileobject_close(NNObjFile* file)
{
    int result;
    if(file->handle != NULL && !file->isstd)
    {
        fflush(file->handle);
        result = fclose(file->handle);
        file->handle = NULL;
        file->isopen = false;
        file->number = -1;
        file->istty = false;
        return result;
    }
    return -1;
}

bool nn_fileobject_open(NNObjFile* file)
{
    if(file->handle != NULL)
    {
        return true;
    }
    if(file->handle == NULL && !file->isstd)
    {
        file->handle = fopen(file->path->sbuf.data, file->mode->sbuf.data);
        if(file->handle != NULL)
        {
            file->isopen = true;
            file->number = fileno(file->handle);
            file->istty = osfn_isatty(file->number);
            return true;
        }
        else
        {
            file->number = -1;
            file->istty = false;
        }
        return false;
    }
    return false;
}

NNValue nn_objfnfile_constructor(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    FILE* hnd;
    const char* path;
    const char* mode;
    NNObjString* opath;
    NNObjFile* file;
    (void)hnd;
    (void)thisval;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "constructor", argv, argc);
    NEON_ARGS_CHECKCOUNTRANGE(&check, 1, 2);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
    opath = nn_value_asstring(argv[0]);
    if(opath->sbuf.length == 0)
    {
        NEON_RETURNERROR("file path cannot be empty");
    }
    mode = "r";
    if(argc == 2)
    {
        NEON_ARGS_CHECKTYPE(&check, 1, nn_value_isstring);
        mode = nn_value_asstring(argv[1])->sbuf.data;
    }
    path = opath->sbuf.data;
    file = (NNObjFile*)nn_gcmem_protect(state, (NNObject*)nn_object_makefile(state, NULL, false, path, mode));
    nn_fileobject_open(file);
    return nn_value_fromobject(file);
}

NNValue nn_objfnfile_exists(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNObjString* file;
    NNArgCheck check;
    (void)thisval;
    nn_argcheck_init(state, &check, "exists", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
    file = nn_value_asstring(argv[0]);
    return nn_value_makebool(nn_util_fsfileexists(state, file->sbuf.data));
}

NNValue nn_objfnfile_isfile(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNObjString* file;
    NNArgCheck check;
    (void)thisval;
    nn_argcheck_init(state, &check, "isfile", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
    file = nn_value_asstring(argv[0]);
    return nn_value_makebool(nn_util_fsfileisfile(state, file->sbuf.data));
}

NNValue nn_objfnfile_isdirectory(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNObjString* file;
    NNArgCheck check;
    (void)thisval;
    nn_argcheck_init(state, &check, "isdirectory", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
    file = nn_value_asstring(argv[0]);
    return nn_value_makebool(nn_util_fsfileisdirectory(state, file->sbuf.data));
}


NNValue nn_objfnfile_readstatic(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    char* buf;
    size_t thismuch;
    size_t actualsz;
    NNObjString* filepath;
    NNArgCheck check;
    (void)thisval;
    nn_argcheck_init(state, &check, "read", argv, argc);
    thismuch = -1;
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
    if(argc > 1)
    {
        NEON_ARGS_CHECKTYPE(&check, 1, nn_value_isnumber);
        thismuch = (size_t)nn_value_asnumber(argv[1]);
    }
    filepath = nn_value_asstring(argv[0]);
    buf = nn_util_filereadfile(state, filepath->sbuf.data, &actualsz, true, thismuch);
    if(buf == NULL)
    {
        nn_except_throwclass(state, state->exceptions.ioerror, "%s: %s", filepath->sbuf.data, strerror(errno));
        return nn_value_makenull();
    }
    return nn_value_fromobject(nn_string_takelen(state, buf, actualsz));
}


NNValue nn_objfnfile_writestatic(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    bool appending;
    size_t rt;
    FILE* fh;
    const char* mode;
    NNObjString* filepath;
    NNObjString* data;
    NNArgCheck check;
    (void)thisval;
    appending = false;
    mode = "wb";
    nn_argcheck_init(state, &check, "write", argv, argc);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
    NEON_ARGS_CHECKTYPE(&check, 1, nn_value_isstring);
    if(argc > 2)
    {
        NEON_ARGS_CHECKTYPE(&check, 2, nn_value_isbool);
        appending = nn_value_asbool(argv[2]);
    }
    if(appending)
    {
        mode = "ab";
    }
    filepath = nn_value_asstring(argv[0]);
    data = nn_value_asstring(argv[1]);
    fh = fopen(filepath->sbuf.data, mode);
    if(fh == NULL)
    {
        nn_except_throwclass(state, state->exceptions.ioerror, strerror(errno));
        return nn_value_makenull();
    }
    rt = fwrite(data->sbuf.data, sizeof(char), data->sbuf.length, fh);
    fclose(fh);
    return nn_value_makenumber(rt);
}


NNValue nn_objfnfile_statstatic(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNObjString* file;
    NNArgCheck check;
    NNObjDict* dict;
    struct stat st;
    (void)thisval;
    nn_argcheck_init(state, &check, "stat", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
    dict = (NNObjDict*)nn_gcmem_protect(state, (NNObject*)nn_object_makedict(state));
    file = nn_value_asstring(argv[0]);
    if(osfn_lstat(file->sbuf.data, &st) == 0)
    {
        nn_util_statfilldictphysfile(dict, &st);
        return nn_value_fromobject(dict);
    }
    return nn_value_makenull();
}

NNValue nn_objfnfile_close(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNArgCheck check;
    nn_argcheck_init(state, &check, "close", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    nn_fileobject_close(nn_value_asfile(thisval));
    return nn_value_makenull();
}

NNValue nn_objfnfile_open(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNArgCheck check;
    nn_argcheck_init(state, &check, "open", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    nn_fileobject_open(nn_value_asfile(thisval));
    return nn_value_makenull();
}

NNValue nn_objfnfile_isopen(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNObjFile* file;
    (void)state;
    (void)argv;
    (void)argc;
    file = nn_value_asfile(thisval);
    return nn_value_makebool(file->isstd || file->isopen);
}

NNValue nn_objfnfile_isclosed(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNObjFile* file;
    (void)state;
    (void)argv;
    (void)argc;
    file = nn_value_asfile(thisval);
    return nn_value_makebool(!file->isstd && !file->isopen);
}

NNValue nn_objfnfile_readmethod(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    size_t readhowmuch;
    NNIOResult res;
    NNObjFile* file;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "read", argv, argc);
    NEON_ARGS_CHECKCOUNTRANGE(&check, 0, 1);
    readhowmuch = -1;
    if(argc == 1)
    {
        NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isnumber);
        readhowmuch = (size_t)nn_value_asnumber(argv[0]);
    }
    file = nn_value_asfile(thisval);
    if(!nn_file_read(file, readhowmuch, &res))
    {
        FILE_ERROR(NotFound, strerror(errno));
    }
    return nn_value_fromobject(nn_string_takelen(state, res.data, res.length));
}


NNValue nn_objfnfile_readline(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    long rdline;
    size_t linelen;
    char* strline;
    NNObjFile* file;
    NNArgCheck check;
    NNObjString* nos;
    nn_argcheck_init(state, &check, "readLine", argv, argc);
    NEON_ARGS_CHECKCOUNTRANGE(&check, 0, 1);
    file = nn_value_asfile(thisval);
    linelen = 0;
    strline = NULL;
    rdline = nn_util_filegetlinehandle(&strline, &linelen, file->handle);
    if(rdline == -1)
    {
        return nn_value_makenull();
    }
    nos = nn_string_takelen(state, strline, rdline);
    return nn_value_fromobject(nos);
}

NNValue nn_objfnfile_get(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    int ch;
    NNObjFile* file;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "get", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    file = nn_value_asfile(thisval);
    ch = fgetc(file->handle);
    if(ch == EOF)
    {
        return nn_value_makenull();
    }
    return nn_value_makenumber(ch);
}

NNValue nn_objfnfile_gets(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    long end;
    long length;
    long currentpos;
    size_t bytesread;
    char* buffer;
    NNObjFile* file;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "gets", argv, argc);
    NEON_ARGS_CHECKCOUNTRANGE(&check, 0, 1);
    length = -1;
    if(argc == 1)
    {
        NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isnumber);
        length = (size_t)nn_value_asnumber(argv[0]);
    }
    file = nn_value_asfile(thisval);
    if(!file->isstd)
    {
        if(!nn_util_fsfileexists(state, file->path->sbuf.data))
        {
            FILE_ERROR(NotFound, "no such file or directory");
        }
        else if(strstr(file->mode->sbuf.data, "w") != NULL && strstr(file->mode->sbuf.data, "+") == NULL)
        {
            FILE_ERROR(Unsupported, "cannot read file in write mode");
        }
        if(!file->isopen)
        {
            FILE_ERROR(Read, "file not open");
        }
        else if(file->handle == NULL)
        {
            FILE_ERROR(Read, "could not read file");
        }
        if(length == -1)
        {
            currentpos = ftell(file->handle);
            fseek(file->handle, 0L, SEEK_END);
            end = ftell(file->handle);
            fseek(file->handle, currentpos, SEEK_SET);
            length = end - currentpos;
        }
    }
    else
    {
        if(fileno(stdout) == file->number || fileno(stderr) == file->number)
        {
            FILE_ERROR(Unsupported, "cannot read from output file");
        }
        /*
        // for non-file objects such as stdin
        // minimum read bytes should be 1
        */
        if(length == -1)
        {
            length = 1;
        }
    }
    buffer = (char*)nn_memory_malloc(sizeof(char) * (length + 1));
    if(buffer == NULL && length != 0)
    {
        FILE_ERROR(Buffer, "not enough memory to read file");
    }
    bytesread = fread(buffer, sizeof(char), length, file->handle);
    if(bytesread == 0 && length != 0)
    {
        FILE_ERROR(Read, "could not read file contents");
    }
    if(buffer != NULL)
    {
        buffer[bytesread] = '\0';
    }
    return nn_value_fromobject(nn_string_takelen(state, buffer, bytesread));
}

NNValue nn_objfnfile_write(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    size_t count;
    int length;
    unsigned char* data;
    NNObjFile* file;
    NNObjString* string;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "write", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    file = nn_value_asfile(thisval);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
    string = nn_value_asstring(argv[0]);
    data = (unsigned char*)string->sbuf.data;
    length = string->sbuf.length;
    if(!file->isstd)
    {
        if(strstr(file->mode->sbuf.data, "r") != NULL && strstr(file->mode->sbuf.data, "+") == NULL)
        {
            FILE_ERROR(Unsupported, "cannot write into non-writable file");
        }
        else if(length == 0)
        {
            FILE_ERROR(Write, "cannot write empty buffer to file");
        }
        else if(file->handle == NULL || !file->isopen)
        {
            nn_fileobject_open(file);
        }
        else if(file->handle == NULL)
        {
            FILE_ERROR(Write, "could not write to file");
        }
    }
    else
    {
        if(fileno(stdin) == file->number)
        {
            FILE_ERROR(Unsupported, "cannot write to input file");
        }
    }
    count = fwrite(data, sizeof(unsigned char), length, file->handle);
    fflush(file->handle);
    if(count > (size_t)0)
    {
        return nn_value_makebool(true);
    }
    return nn_value_makebool(false);
}

NNValue nn_objfnfile_puts(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    size_t count;
    int length;
    unsigned char* data;
    NNObjFile* file;
    NNObjString* string;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "puts", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    file = nn_value_asfile(thisval);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
    string = nn_value_asstring(argv[0]);
    data = (unsigned char*)string->sbuf.data;
    length = string->sbuf.length;
    if(!file->isstd)
    {
        if(strstr(file->mode->sbuf.data, "r") != NULL && strstr(file->mode->sbuf.data, "+") == NULL)
        {
            FILE_ERROR(Unsupported, "cannot write into non-writable file");
        }
        else if(length == 0)
        {
            FILE_ERROR(Write, "cannot write empty buffer to file");
        }
        else if(!file->isopen)
        {
            FILE_ERROR(Write, "file not open");
        }
        else if(file->handle == NULL)
        {
            FILE_ERROR(Write, "could not write to file");
        }
    }
    else
    {
        if(fileno(stdin) == file->number)
        {
            FILE_ERROR(Unsupported, "cannot write to input file");
        }
    }
    count = fwrite(data, sizeof(unsigned char), length, file->handle);
    if(count > (size_t)0 || length == 0)
    {
        return nn_value_makebool(true);
    }
    return nn_value_makebool(false);
}

NNValue nn_objfnfile_printf(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNObjFile* file;
    NNFormatInfo nfi;
    NNPrinter pr;
    NNObjString* ofmt;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "printf", argv, argc);
    file = nn_value_asfile(thisval);
    NEON_ARGS_CHECKMINARG(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
    ofmt = nn_value_asstring(argv[0]);
    nn_printer_makestackio(state, &pr, file->handle, false);
    nn_strformat_init(state, &nfi, &pr, nn_string_getcstr(ofmt), nn_string_getlength(ofmt));
    if(!nn_strformat_format(&nfi, argc, 1, argv))
    {
    }
    return nn_value_makenull();
}

NNValue nn_objfnfile_number(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNArgCheck check;
    nn_argcheck_init(state, &check, "number", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    return nn_value_makenumber(nn_value_asfile(thisval)->number);
}

NNValue nn_objfnfile_istty(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNObjFile* file;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "istty", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    file = nn_value_asfile(thisval);
    return nn_value_makebool(file->istty);
}

NNValue nn_objfnfile_flush(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNObjFile* file;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "flush", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    file = nn_value_asfile(thisval);
    if(!file->isopen)
    {
        FILE_ERROR(Unsupported, "I/O operation on closed file");
    }
    #if defined(NEON_PLAT_ISLINUX)
    if(fileno(stdin) == file->number)
    {
        while((getchar()) != '\n')
        {
        }
    }
    else
    {
        fflush(file->handle);
    }
    #else
    fflush(file->handle);
    #endif
    return nn_value_makenull();
}

void nn_util_statfilldictphysfile(NNObjDict* dict, struct stat* st)
{
    #if !defined(NEON_PLAT_ISWINDOWS)
    nn_dict_addentrycstr(dict, "isreadable", nn_value_makebool(((st->st_mode & S_IRUSR) != 0)));
    nn_dict_addentrycstr(dict, "iswritable", nn_value_makebool(((st->st_mode & S_IWUSR) != 0)));
    nn_dict_addentrycstr(dict, "isexecutable", nn_value_makebool(((st->st_mode & S_IXUSR) != 0)));
    nn_dict_addentrycstr(dict, "issymbolic", nn_value_makebool((S_ISLNK(st->st_mode) != 0)));
    #else
    nn_dict_addentrycstr(dict, "isreadable", nn_value_makebool(((st->st_mode & S_IREAD) != 0)));
    nn_dict_addentrycstr(dict, "iswritable", nn_value_makebool(((st->st_mode & S_IWRITE) != 0)));
    nn_dict_addentrycstr(dict, "isexecutable", nn_value_makebool(((st->st_mode & S_IEXEC) != 0)));
    nn_dict_addentrycstr(dict, "issymbolic", nn_value_makebool(false));
    #endif
    nn_dict_addentrycstr(dict, "size", nn_value_makenumber(st->st_size));
    nn_dict_addentrycstr(dict, "mode", nn_value_makenumber(st->st_mode));
    nn_dict_addentrycstr(dict, "dev", nn_value_makenumber(st->st_dev));
    nn_dict_addentrycstr(dict, "ino", nn_value_makenumber(st->st_ino));
    nn_dict_addentrycstr(dict, "nlink", nn_value_makenumber(st->st_nlink));
    nn_dict_addentrycstr(dict, "uid", nn_value_makenumber(st->st_uid));
    nn_dict_addentrycstr(dict, "gid", nn_value_makenumber(st->st_gid));
    nn_dict_addentrycstr(dict, "mtime", nn_value_makenumber(st->st_mtime));
    nn_dict_addentrycstr(dict, "atime", nn_value_makenumber(st->st_atime));
    nn_dict_addentrycstr(dict, "ctime", nn_value_makenumber(st->st_ctime));
    nn_dict_addentrycstr(dict, "blocks", nn_value_makenumber(0));
    nn_dict_addentrycstr(dict, "blksize", nn_value_makenumber(0));
}

NNValue nn_objfnfile_statmethod(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    struct stat stats;
    NNObjFile* file;
    NNObjDict* dict;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "stat", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    file = nn_value_asfile(thisval);
    dict = (NNObjDict*)nn_gcmem_protect(state, (NNObject*)nn_object_makedict(state));
    if(!file->isstd)
    {
        if(nn_util_fsfileexists(state, file->path->sbuf.data))
        {
            if(osfn_lstat(file->path->sbuf.data, &stats) == 0)
            {
                nn_util_statfilldictphysfile(dict, &stats);
            }
        }
        else
        {
            NEON_RETURNERROR("cannot get stats for non-existing file");
        }
    }
    else
    {
        if(fileno(stdin) == file->number)
        {
            nn_dict_addentrycstr(dict, "isreadable", nn_value_makebool(true));
            nn_dict_addentrycstr(dict, "iswritable", nn_value_makebool(false));
        }
        else
        {
            nn_dict_addentrycstr(dict, "isreadable", nn_value_makebool(false));
            nn_dict_addentrycstr(dict, "iswritable", nn_value_makebool(true));
        }
        nn_dict_addentrycstr(dict, "isexecutable", nn_value_makebool(false));
        nn_dict_addentrycstr(dict, "size", nn_value_makenumber(1));
    }
    return nn_value_fromobject(dict);
}

NNValue nn_objfnfile_path(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNObjFile* file;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "path", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    file = nn_value_asfile(thisval);
    DENY_STD();
    return nn_value_fromobject(file->path);
}

NNValue nn_objfnfile_mode(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNObjFile* file;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "mode", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    file = nn_value_asfile(thisval);
    return nn_value_fromobject(file->mode);
}

NNValue nn_objfnfile_name(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    char* name;
    NNObjFile* file;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "name", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    file = nn_value_asfile(thisval);
    if(!file->isstd)
    {
        name = nn_util_fsgetbasename(state, file->path->sbuf.data);
        return nn_value_fromobject(nn_string_copycstr(state, name));
    }
    else if(file->istty)
    {
        return nn_value_fromobject(nn_string_copycstr(state, "<tty>"));
    }
    return nn_value_makenull();
}

NNValue nn_objfnfile_seek(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    long position;
    int seektype;
    NNObjFile* file;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "seek", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 2);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isnumber);
    NEON_ARGS_CHECKTYPE(&check, 1, nn_value_isnumber);
    file = nn_value_asfile(thisval);
    DENY_STD();
    position = (long)nn_value_asnumber(argv[0]);
    seektype = nn_value_asnumber(argv[1]);
    RETURN_STATUS(fseek(file->handle, position, seektype));
}

NNValue nn_objfnfile_tell(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNObjFile* file;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "tell", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    file = nn_value_asfile(thisval);
    DENY_STD();
    return nn_value_makenumber(ftell(file->handle));
}

#undef FILE_ERROR
#undef RETURN_STATUS
#undef DENY_STD



