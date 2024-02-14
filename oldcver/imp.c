

#if defined(_WIN32)
    #include <fcntl.h>
    #include <io.h>
#endif

#include "neon.h"
#include "utf8.h"

#if defined(__STRICT_ANSI__)
    #define va_copy(...)
#endif

#if defined(__GNUC__)
    #define NEON_LIKELY(x) \
        __builtin_expect(!!(x), 1)

    #define NEON_UNLIKELY(x) \
        __builtin_expect(!!(x), 0)
#else
    #define NEON_LIKELY(x) x
    #define NEON_UNLIKELY(x) x
#endif


void* nn_util_rawrealloc(void* userptr, void* ptr, size_t size)
{
    (void)userptr;
    return realloc(ptr, size);
}

void* nn_util_rawmalloc(void* userptr, size_t size)
{
    (void)userptr;
    return malloc(size);
}

void* nn_util_rawcalloc(void* userptr, size_t count, size_t size)
{
    (void)userptr;
    return calloc(count, size);
}

void nn_util_rawfree(void* userptr, void* ptr)
{
    (void)userptr;
    free(ptr);
}

void* nn_util_memrealloc(NeonState* state, void* ptr, size_t size)
{
    return nn_util_rawrealloc(state->memuserptr, ptr, size);
}

void* nn_util_memmalloc(NeonState* state, size_t size)
{
    return nn_util_rawmalloc(state->memuserptr, size);
}

void* nn_util_memcalloc(NeonState* state, size_t count, size_t size)
{
    return nn_util_rawcalloc(state->memuserptr, count, size);
}

void nn_util_memfree(NeonState* state, void* ptr)
{
    nn_util_rawfree(state->memuserptr, ptr);
}

NeonObject* nn_gcmem_protect(NeonState* state, NeonObject* object)
{
    size_t frpos;
    nn_vm_stackpush(state, nn_value_fromobject(object));
    frpos = 0;
    if(state->vmstate.framecount > 0)
    {
        frpos = state->vmstate.framecount - 1;
    }
    state->vmstate.framevalues[frpos].gcprotcount++;
    return object;
}

void nn_gcmem_clearprotect(NeonState* state)
{
    size_t frpos;
    NeonCallFrame* frame;
    frpos = 0;
    if(state->vmstate.framecount > 0)
    {
        frpos = state->vmstate.framecount - 1;
    }
    frame = &state->vmstate.framevalues[frpos];
    if(frame->gcprotcount > 0)
    {
        state->vmstate.stackidx -= frame->gcprotcount;
    }
    frame->gcprotcount = 0;
}

const char* nn_util_color(NeonColor tc)
{
    #if !defined(NEON_CFG_FORCEDISABLECOLOR)
        bool istty;
        int fdstdout;
        int fdstderr;
        fdstdout = fileno(stdout);
        fdstderr = fileno(stderr);
        istty = (osfn_isatty(fdstderr) && osfn_isatty(fdstdout));
        if(istty)
        {
            switch(tc)
            {
                case NEON_COLOR_RESET:
                    return "\x1B[0m";
                case NEON_COLOR_RED:
                    return "\x1B[31m";
                case NEON_COLOR_GREEN:
                    return "\x1B[32m";
                case NEON_COLOR_YELLOW:
                    return "\x1B[33m";
                case NEON_COLOR_BLUE:
                    return "\x1B[34m";
                case NEON_COLOR_MAGENTA:
                    return "\x1B[35m";
                case NEON_COLOR_CYAN:
                    return "\x1B[36m";
            }
        }
    #else
        (void)tc;
    #endif
    return "";
}

char* nn_util_strndup(NeonState* state, const char* src, size_t len)
{
    char* buf;
    buf = (char*)nn_util_memmalloc(state, sizeof(char) * (len+1));
    if(buf == NULL)
    {
        return NULL;
    }
    memset(buf, 0, len+1);
    memcpy(buf, src, len);
    return buf;
}

char* nn_util_strdup(NeonState* state, const char* src)
{
    return nn_util_strndup(state, src, strlen(src));
}

char* nn_util_readhandle(NeonState* state, FILE* hnd, size_t* dlen)
{
    long rawtold;
    /*
    * the value returned by ftell() may not necessarily be the same as
    * the amount that can be read.
    * since we only ever read a maximum of $toldlen, there will
    * be no memory trashing.
    */
    size_t toldlen;
    size_t actuallen;
    char* buf;
    if(fseek(hnd, 0, SEEK_END) == -1)
    {
        return NULL;
    }
    if((rawtold = ftell(hnd)) == -1)
    {
        return NULL;
    }
    toldlen = rawtold;
    if(fseek(hnd, 0, SEEK_SET) == -1)
    {
        return NULL;
    }
    buf = (char*)nn_gcmem_allocate(state, sizeof(char), toldlen + 1);
    memset(buf, 0, toldlen+1);
    if(buf != NULL)
    {
        actuallen = fread(buf, sizeof(char), toldlen, hnd);
        /*
        // optionally, read remainder:
        size_t tmplen;
        if(actuallen < toldlen)
        {
            tmplen = actuallen;
            actuallen += fread(buf+tmplen, sizeof(char), actuallen-toldlen, hnd);
            ...
        }
        // unlikely to be necessary, so not implemented.
        */
        if(dlen != NULL)
        {
            *dlen = actuallen;
        }
        return buf;
    }
    return NULL;
}

char* nn_util_readfile(NeonState* state, const char* filename, size_t* dlen)
{
    char* b;
    FILE* fh;
    fh = fopen(filename, "rb");
    if(fh == NULL)
    {
        return NULL;
    }
    #if defined(NEON_PLAT_ISWINDOWS)
        _setmode(fileno(fh), _O_BINARY);
    #endif
    b = nn_util_readhandle(state, fh, dlen);
    fclose(fh);
    return b;
}

/* returns the number of bytes contained in a unicode character */
int nn_util_utf8numbytes(int value)
{
    if(value < 0)
    {
        return -1;
    }
    if(value <= 0x7f)
    {
        return 1;
    }
    if(value <= 0x7ff)
    {
        return 2;
    }
    if(value <= 0xffff)
    {
        return 3;
    }
    if(value <= 0x10ffff)
    {
        return 4;
    }
    return 0;
}

char* nn_util_utf8encode(NeonState* state, unsigned int code, size_t* dlen)
{
    int count;
    char* chars;
    *dlen = 0;
    count = nn_util_utf8numbytes((int)code);
    if(count > 0)
    {
        *dlen = count;
        chars = (char*)nn_gcmem_allocate(state, sizeof(char), (size_t)count + 1);
        if(chars != NULL)
        {
            if(code <= 0x7F)
            {
                chars[0] = (char)(code & 0x7F);
                chars[1] = '\0';
            }
            else if(code <= 0x7FF)
            {
                /* one continuation byte */
                chars[1] = (char)(0x80 | (code & 0x3F));
                code = (code >> 6);
                chars[0] = (char)(0xC0 | (code & 0x1F));
            }
            else if(code <= 0xFFFF)
            {
                /* two continuation bytes */
                chars[2] = (char)(0x80 | (code & 0x3F));
                code = (code >> 6);
                chars[1] = (char)(0x80 | (code & 0x3F));
                code = (code >> 6);
                chars[0] = (char)(0xE0 | (code & 0xF));
            }
            else if(code <= 0x10FFFF)
            {
                /* three continuation bytes */
                chars[3] = (char)(0x80 | (code & 0x3F));
                code = (code >> 6);
                chars[2] = (char)(0x80 | (code & 0x3F));
                code = (code >> 6);
                chars[1] = (char)(0x80 | (code & 0x3F));
                code = (code >> 6);
                chars[0] = (char)(0xF0 | (code & 0x7));
            }
            else
            {
                /* unicode replacement character */
                chars[2] = (char)0xEF;
                chars[1] = (char)0xBF;
                chars[0] = (char)0xBD;
            }
            return chars;
        }
    }
    return NULL;
}

int nn_util_utf8decode(const uint8_t* bytes, uint32_t length)
{
    int value;
    uint32_t remainingbytes;
    /* Single byte (i.e. fits in ASCII). */
    if(*bytes <= 0x7f)
    {
        return *bytes;
    }
    if((*bytes & 0xe0) == 0xc0)
    {
        /* Two byte sequence: 110xxxxx 10xxxxxx. */
        value = *bytes & 0x1f;
        remainingbytes = 1;
    }
    else if((*bytes & 0xf0) == 0xe0)
    {
        /* Three byte sequence: 1110xxxx	 10xxxxxx 10xxxxxx. */
        value = *bytes & 0x0f;
        remainingbytes = 2;
    }
    else if((*bytes & 0xf8) == 0xf0)
    {
        /* Four byte sequence: 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx. */
        value = *bytes & 0x07;
        remainingbytes = 3;
    }
    else
    {
        /* Invalid UTF-8 sequence. */
        return -1;
    }
    /* Don't read past the end of the buffer on truncated UTF-8. */
    if(remainingbytes > length - 1)
    {
        return -1;
    }
    while(remainingbytes > 0)
    {
        bytes++;
        remainingbytes--;
        /* Remaining bytes must be of form 10xxxxxx. */
        if((*bytes & 0xc0) != 0x80)
        {
            return -1;
        }
        value = value << 6 | (*bytes & 0x3f);
    }
    return value;
}

char* nn_util_utf8codepoint(const char* str, char* outcodepoint)
{
    if(0xf0 == (0xf8 & str[0]))
    {
        /* 4 byte utf8 codepoint */
        *outcodepoint = (
            ((0x07 & str[0]) << 18) |
            ((0x3f & str[1]) << 12) |
            ((0x3f & str[2]) << 6) |
            (0x3f & str[3])
        );
        str += 4;
    }
    else if(0xe0 == (0xf0 & str[0]))
    {
        /* 3 byte utf8 codepoint */
        *outcodepoint = (
            ((0x0f & str[0]) << 12) |
            ((0x3f & str[1]) << 6) |
            (0x3f & str[2])
        );
        str += 3;
    }
    else if(0xc0 == (0xe0 & str[0]))
    {
        /* 2 byte utf8 codepoint */
        *outcodepoint = (
            ((0x1f & str[0]) << 6) |
            (0x3f & str[1])
        );
        str += 2;
    }
    else
    {
        /* 1 byte utf8 codepoint otherwise */
        *outcodepoint = str[0];
        str += 1;
    }
    return (char*)str;
}

char* nn_util_utf8strstr(const char* haystack, const char* needle)
{
    char throwawaycodepoint;
    const char* n;
    const char* maybematch;
    throwawaycodepoint = 0;
    /* if needle has no utf8 codepoints before the null terminating
     * byte then return haystack */
    if('\0' == *needle)
    {
        return (char*)haystack;
    }
    while('\0' != *haystack)
    {
        maybematch = haystack;
        n = needle;
        while(*haystack == *n && (*haystack != '\0' && *n != '\0'))
        {
            n++;
            haystack++;
        }
        if('\0' == *n)
        {
            /* we found the whole utf8 string for needle in haystack at
             * maybematch, so return it */
            return (char*)maybematch;
        }
        else
        {
            /* h could be in the middle of an unmatching utf8 codepoint,
             * so we need to march it on to the next character beginning
             * starting from the current character */
            haystack = nn_util_utf8codepoint(maybematch, &throwawaycodepoint);
        }
    }
    /* no match */
    return NULL;
}

/*
// returns a pointer to the beginning of the pos'th utf8 codepoint
// in the buffer at s
*/
char* nn_util_utf8index(char* s, int pos)
{
    ++pos;
    for(; *s; ++s)
    {
        if((*s & 0xC0) != 0x80)
        {
            --pos;
        }
        if(pos == 0)
        {
            return s;
        }
    }
    return NULL;
}

/*
// converts codepoint indexes start and end to byte offsets in the buffer at s
*/
void nn_util_utf8slice(char* s, int* start, int* end)
{
    char* p;
    p = nn_util_utf8index(s, *start);
    if(p != NULL)
    {
        *start = (int)(p - s);
    }
    else
    {
        *start = -1;
    }
    p = nn_util_utf8index(s, *end);
    if(p != NULL)
    {
        *end = (int)(p - s);
    }
    else
    {
        *end = (int)strlen(s);
    }
}

char* nn_util_strtoupper(char* str, size_t length)
{
    int c;
    size_t i;
    for(i=0; i<length; i++)
    {
        c = str[i];
        str[i] = toupper(c);
    }
    return str;
}

char* nn_util_strtolower(char* str, size_t length)
{
    int c;
    size_t i;
    for(i=0; i<length; i++)
    {
        c = str[i];
        str[i] = toupper(c);
    }
    return str;
}

#define NEON_APIDEBUG(state, ...) \
    if((NEON_UNLIKELY((state)->conf.enableapidebug))) \
    { \
        nn_state_apidebug(state, __FUNCTION__, __VA_ARGS__); \
    }


#define NEON_ASTDEBUG(state, ...) \
    if((NEON_UNLIKELY((state)->conf.enableastdebug))) \
    { \
        nn_state_astdebug(state, __FUNCTION__, __VA_ARGS__); \
    }


static NEON_FORCEINLINE void nn_state_apidebugv(NeonState* state, const char* funcname, const char* format, va_list va)
{
    (void)state;
    fprintf(stderr, "API CALL: to '%s': ", funcname);
    vfprintf(stderr, format, va);
    fprintf(stderr, "\n");
}

static NEON_FORCEINLINE void nn_state_apidebug(NeonState* state, const char* funcname, const char* format, ...)
{
    va_list va;
    va_start(va, format);
    nn_state_apidebugv(state, funcname, format, va);
    va_end(va);
}

static NEON_FORCEINLINE void nn_state_astdebugv(NeonState* state, const char* funcname, const char* format, va_list va)
{
    (void)state;
    fprintf(stderr, "AST CALL: to '%s': ", funcname);
    vfprintf(stderr, format, va);
    fprintf(stderr, "\n");
}

static NEON_FORCEINLINE void nn_state_astdebug(NeonState* state, const char* funcname, const char* format, ...)
{
    va_list va;
    va_start(va, format);
    nn_state_astdebugv(state, funcname, format, va);
    va_end(va);
}

void nn_gcmem_maybecollect(NeonState* state, int addsize, bool wasnew)
{
    state->gcstate.bytesallocated += addsize;
    if(state->gcstate.nextgc > 0)
    {
        if(wasnew && state->gcstate.bytesallocated > state->gcstate.nextgc)
        {
            if(state->vmstate.currentframe && state->vmstate.currentframe->gcprotcount == 0)
            {
                nn_gcmem_collectgarbage(state);
            }
        }
    }
}

void* nn_gcmem_reallocate(NeonState* state, void* pointer, size_t oldsize, size_t newsize)
{
    void* result;
    nn_gcmem_maybecollect(state, newsize - oldsize, newsize > oldsize);
    result = nn_util_memrealloc(state, pointer, newsize);
    /*
    // just in case reallocation fails... computers ain't infinite!
    */
    if(result == NULL)
    {
        fprintf(stderr, "fatal error: failed to allocate %ld bytes\n", newsize);
        abort();
    }
    return result;
}

void nn_gcmem_release(NeonState* state, void* pointer, size_t oldsize)
{
    nn_gcmem_maybecollect(state, -oldsize, false);
    if(oldsize > 0)
    {
        memset(pointer, 0, oldsize);
    }
    nn_util_memfree(state, pointer);
    pointer = NULL;
}

void* nn_gcmem_allocate(NeonState* state, size_t size, size_t amount)
{
    return nn_gcmem_reallocate(state, NULL, 0, size * amount);
}

void nn_gcmem_markobject(NeonState* state, NeonObject* object)
{
    if(object == NULL)
    {
        return;
    }
    if(object->mark == state->markvalue)
    {
        return;
    }
    #if defined(DEBUG_GC) && DEBUG_GC
    nn_printer_writefmt(state->debugwriter, "GC: marking object at <%p> ", (void*)object);
    nn_printer_printvalue(state->debugwriter, nn_value_fromobject(object), false);
    nn_printer_writefmt(state->debugwriter, "\n");
    #endif
    object->mark = state->markvalue;
    if(state->gcstate.graycapacity < state->gcstate.graycount + 1)
    {
        state->gcstate.graycapacity = GROW_CAPACITY(state->gcstate.graycapacity);
        state->gcstate.graystack = (NeonObject**)nn_util_memrealloc(state, state->gcstate.graystack, sizeof(NeonObject*) * state->gcstate.graycapacity);
        if(state->gcstate.graystack == NULL)
        {
            fflush(stdout);
            fprintf(stderr, "GC encountered an error");
            abort();
        }
    }
    state->gcstate.graystack[state->gcstate.graycount++] = object;
}

void nn_gcmem_markvalue(NeonState* state, NeonValue value)
{
    if(nn_value_isobject(value))
    {
        nn_gcmem_markobject(state, nn_value_asobject(value));
    }
}

void nn_gcmem_blackenobject(NeonState* state, NeonObject* object)
{
    #if defined(DEBUG_GC) && DEBUG_GC
    nn_printer_writefmt(state->debugwriter, "GC: blacken object at <%p> ", (void*)object);
    nn_printer_printvalue(state->debugwriter, nn_value_fromobject(object), false);
    nn_printer_writefmt(state->debugwriter, "\n");
    #endif
    switch(object->type)
    {
        case NEON_OBJTYPE_MODULE:
            {
                NeonObjModule* module;
                module = (NeonObjModule*)object;
                nn_table_mark(state, module->deftable);
            }
            break;
        case NEON_OBJTYPE_SWITCH:
            {
                NeonObjSwitch* sw;
                sw = (NeonObjSwitch*)object;
                nn_table_mark(state, sw->table);
            }
            break;
        case NEON_OBJTYPE_FILE:
            {
                NeonObjFile* file;
                file = (NeonObjFile*)object;
                nn_file_mark(state, file);
            }
            break;
        case NEON_OBJTYPE_DICT:
            {
                NeonObjDict* dict;
                dict = (NeonObjDict*)object;
                nn_valarray_mark(dict->names);
                nn_table_mark(state, dict->htab);
            }
            break;
        case NEON_OBJTYPE_ARRAY:
            {
                NeonObjArray* list;
                list = (NeonObjArray*)object;
                nn_valarray_mark(list->varray);
            }
            break;
        case NEON_OBJTYPE_FUNCBOUND:
            {
                NeonObjFuncBound* bound;
                bound = (NeonObjFuncBound*)object;
                nn_gcmem_markvalue(state, bound->receiver);
                nn_gcmem_markobject(state, (NeonObject*)bound->method);
            }
            break;
        case NEON_OBJTYPE_CLASS:
            {
                NeonObjClass* klass;
                klass = (NeonObjClass*)object;
                nn_gcmem_markobject(state, (NeonObject*)klass->name);
                nn_table_mark(state, klass->methods);
                nn_table_mark(state, klass->staticmethods);
                nn_table_mark(state, klass->staticproperties);
                nn_gcmem_markvalue(state, klass->constructor);
                nn_gcmem_markvalue(state, klass->destructor);
                if(klass->superclass != NULL)
                {
                    nn_gcmem_markobject(state, (NeonObject*)klass->superclass);
                }
            }
            break;
        case NEON_OBJTYPE_FUNCCLOSURE:
            {
                int i;
                NeonObjFuncClosure* closure;
                closure = (NeonObjFuncClosure*)object;
                nn_gcmem_markobject(state, (NeonObject*)closure->scriptfunc);
                for(i = 0; i < closure->upvalcount; i++)
                {
                    nn_gcmem_markobject(state, (NeonObject*)closure->upvalues[i]);
                }
            }
            break;
        case NEON_OBJTYPE_FUNCSCRIPT:
            {
                NeonObjFuncScript* function;
                function = (NeonObjFuncScript*)object;
                nn_gcmem_markobject(state, (NeonObject*)function->name);
                nn_gcmem_markobject(state, (NeonObject*)function->module);
                nn_valarray_mark(function->blob.constants);
            }
            break;
        case NEON_OBJTYPE_INSTANCE:
            {
                NeonObjInstance* instance;
                instance = (NeonObjInstance*)object;
                nn_instance_mark(state, instance);
            }
            break;
        case NEON_OBJTYPE_UPVALUE:
            {
                nn_gcmem_markvalue(state, ((NeonObjUpvalue*)object)->closed);
            }
            break;
        case NEON_OBJTYPE_RANGE:
        case NEON_OBJTYPE_FUNCNATIVE:
        case NEON_OBJTYPE_USERDATA:
        case NEON_OBJTYPE_STRING:
            break;
    }
}

void nn_gcmem_destroyobject(NeonState* state, NeonObject* object)
{
    #if defined(DEBUG_GC) && DEBUG_GC
    nn_printer_writefmt(state->debugwriter, "GC: freeing at <%p> of type %d\n", (void*)object, object->type);
    #endif
    if(object->stale)
    {
        return;
    }
    switch(object->type)
    {
        case NEON_OBJTYPE_MODULE:
            {
                NeonObjModule* module;
                module = (NeonObjModule*)object;
                nn_module_destroy(state, module);
                nn_gcmem_release(state, object, sizeof(NeonObjModule));
            }
            break;
        case NEON_OBJTYPE_FILE:
            {
                NeonObjFile* file;
                file = (NeonObjFile*)object;
                nn_file_destroy(state, file);
            }
            break;
        case NEON_OBJTYPE_DICT:
            {
                NeonObjDict* dict;
                dict = (NeonObjDict*)object;
                nn_valarray_destroy(dict->names);
                nn_table_destroy(dict->htab);
                nn_gcmem_release(state, object, sizeof(NeonObjDict));
            }
            break;
        case NEON_OBJTYPE_ARRAY:
            {
                NeonObjArray* list;
                list = (NeonObjArray*)object;
                nn_valarray_destroy(list->varray);
                nn_gcmem_release(state, object, sizeof(NeonObjArray));
            }
            break;
        case NEON_OBJTYPE_FUNCBOUND:
            {
                /*
                // a closure may be bound to multiple instances
                // for this reason, we do not free closures when freeing bound methods
                */
                nn_gcmem_release(state, object, sizeof(NeonObjFuncBound));
            }
            break;
        case NEON_OBJTYPE_CLASS:
            {
                NeonObjClass* klass;
                klass = (NeonObjClass*)object;
                nn_class_destroy(state, klass);
            }
            break;
        case NEON_OBJTYPE_FUNCCLOSURE:
            {
                NeonObjFuncClosure* closure;
                closure = (NeonObjFuncClosure*)object;
                nn_gcmem_freearray(state, sizeof(NeonObjUpvalue*), closure->upvalues, closure->upvalcount);
                /*
                // there may be multiple closures that all reference the same function
                // for this reason, we do not free functions when freeing closures
                */
                nn_gcmem_release(state, object, sizeof(NeonObjFuncClosure));
            }
            break;
        case NEON_OBJTYPE_FUNCSCRIPT:
            {
                NeonObjFuncScript* function;
                function = (NeonObjFuncScript*)object;
                nn_funcscript_destroy(state, function);
                nn_gcmem_release(state, function, sizeof(NeonObjFuncScript));
            }
            break;
        case NEON_OBJTYPE_INSTANCE:
            {
                NeonObjInstance* instance;
                instance = (NeonObjInstance*)object;
                nn_instance_destroy(state, instance);
            }
            break;
        case NEON_OBJTYPE_FUNCNATIVE:
            {
                nn_gcmem_release(state, object, sizeof(NeonObjFuncNative));
            }
            break;
        case NEON_OBJTYPE_UPVALUE:
            {
                nn_gcmem_release(state, object, sizeof(NeonObjUpvalue));
            }
            break;
        case NEON_OBJTYPE_RANGE:
            {
                nn_gcmem_release(state, object, sizeof(NeonObjRange));
            }
            break;
        case NEON_OBJTYPE_STRING:
            {
                NeonObjString* string;
                string = (NeonObjString*)object;
                nn_string_destroy(state, string);
            }
            break;
        case NEON_OBJTYPE_SWITCH:
            {
                NeonObjSwitch* sw;
                sw = (NeonObjSwitch*)object;
                nn_table_destroy(sw->table);
                nn_gcmem_release(state, object, sizeof(NeonObjSwitch));
            }
            break;
        case NEON_OBJTYPE_USERDATA:
            {
                NeonObjUserdata* ptr;
                ptr = (NeonObjUserdata*)object;
                if(ptr->ondestroyfn)
                {
                    ptr->ondestroyfn(ptr->pointer);
                }
                nn_gcmem_release(state, object, sizeof(NeonObjUserdata));
            }
            break;
        default:
            break;
    }
    
}

void nn_gcmem_markroots(NeonState* state)
{
    int i;
    int j;
    NeonValue* slot;
    NeonObjUpvalue* upvalue;
    NeonExceptionFrame* handler;
    for(slot = state->vmstate.stackvalues; slot < &state->vmstate.stackvalues[state->vmstate.stackidx]; slot++)
    {
        nn_gcmem_markvalue(state, *slot);
    }
    for(i = 0; i < (int)state->vmstate.framecount; i++)
    {
        nn_gcmem_markobject(state, (NeonObject*)state->vmstate.framevalues[i].closure);
        for(j = 0; j < (int)state->vmstate.framevalues[i].handlercount; j++)
        {
            handler = &state->vmstate.framevalues[i].handlers[j];
            nn_gcmem_markobject(state, (NeonObject*)handler->klass);
        }
    }
    for(upvalue = state->vmstate.openupvalues; upvalue != NULL; upvalue = upvalue->next)
    {
        nn_gcmem_markobject(state, (NeonObject*)upvalue);
    }
    nn_table_mark(state, state->globals);
    nn_table_mark(state, state->modules);
    nn_gcmem_markobject(state, (NeonObject*)state->exceptions.stdexception);
    nn_gcmem_markcompilerroots(state);
}

void nn_gcmem_tracerefs(NeonState* state)
{
    NeonObject* object;
    while(state->gcstate.graycount > 0)
    {
        object = state->gcstate.graystack[--state->gcstate.graycount];
        nn_gcmem_blackenobject(state, object);
    }
}

void nn_gcmem_sweep(NeonState* state)
{
    NeonObject* object;
    NeonObject* previous;
    NeonObject* unreached;
    previous = NULL;
    object = state->vmstate.linkedobjects;
    while(object != NULL)
    {
        if(object->mark == state->markvalue)
        {
            previous = object;
            object = object->next;
        }
        else
        {
            unreached = object;
            object = object->next;
            if(previous != NULL)
            {
                previous->next = object;
            }
            else
            {
                state->vmstate.linkedobjects = object;
            }
            nn_gcmem_destroyobject(state, unreached);
        }
    }
}

void nn_gcmem_destroylinkedobjects(NeonState* state)
{
    NeonObject* next;
    NeonObject* object;
    object = state->vmstate.linkedobjects;
    while(object != NULL)
    {
        next = object->next;
        nn_gcmem_destroyobject(state, object);
        object = next;
    }
    nn_util_memfree(state, state->gcstate.graystack);
    state->gcstate.graystack = NULL;
}

void nn_gcmem_collectgarbage(NeonState* state)
{
    size_t before;
    (void)before;
    #if defined(DEBUG_GC) && DEBUG_GC
    nn_printer_writefmt(state->debugwriter, "GC: gc begins\n");
    before = state->gcstate.bytesallocated;
    #endif
    /*
    //  REMOVE THE NEXT LINE TO DISABLE NESTED nn_gcmem_collectgarbage() POSSIBILITY!
    */
    #if 1
    state->gcstate.nextgc = state->gcstate.bytesallocated;
    #endif
    nn_gcmem_markroots(state);
    nn_gcmem_tracerefs(state);
    nn_table_removewhites(state, state->strings);
    nn_table_removewhites(state, state->modules);
    nn_gcmem_sweep(state);
    state->gcstate.nextgc = state->gcstate.bytesallocated * NEON_CFG_GCHEAPGROWTHFACTOR;
    state->markvalue = !state->markvalue;
    #if defined(DEBUG_GC) && DEBUG_GC
    nn_printer_writefmt(state->debugwriter, "GC: gc ends\n");
    nn_printer_writefmt(state->debugwriter, "GC: collected %zu bytes (from %zu to %zu), next at %zu\n", before - state->gcstate.bytesallocated, before, state->gcstate.bytesallocated, state->gcstate.nextgc);
    #endif
}

NeonValue nn_argcheck_vfail(NeonArgCheck* ch, const char* srcfile, int srcline, const char* fmt, va_list va)
{
    nn_vm_stackpopn(ch->pvm, ch->argc);
    nn_exceptions_vthrowwithclass(ch->pvm, ch->pvm->exceptions.argumenterror, srcfile, srcline, fmt, va);
    return nn_value_makebool(false);
}

NeonValue nn_argcheck_fail(NeonArgCheck* ch, const char* srcfile, int srcline, const char* fmt, ...)
{
    NeonValue v;
    va_list va;
    va_start(va, fmt);
    v = nn_argcheck_vfail(ch, srcfile, srcline, fmt, va);
    va_end(va);
    return v;
}

void nn_argcheck_init(NeonState* state, NeonArgCheck* ch, NeonArguments* args)
{
    ch->pvm = state;
    ch->name = args->name;
    ch->argc = args->count;
    ch->argv = args->args;
}

void nn_dbg_disasmblob(NeonPrinter* pr, NeonBlob* blob, const char* name)
{
    int offset;
    nn_printer_writefmt(pr, "== compiled '%s' [[\n", name);
    for(offset = 0; offset < blob->count;)
    {
        offset = nn_dbg_printinstructionat(pr, blob, offset);
    }
    nn_printer_writefmt(pr, "]]\n");
}

void nn_dbg_printinstrname(NeonPrinter* pr, const char* name)
{
    nn_printer_writefmt(pr, "%s%-16s%s ", nn_util_color(NEON_COLOR_RED), name, nn_util_color(NEON_COLOR_RESET));
}

int nn_dbg_printsimpleinstr(NeonPrinter* pr, const char* name, int offset)
{
    nn_dbg_printinstrname(pr, name);
    nn_printer_writefmt(pr, "\n");
    return offset + 1;
}

int nn_dbg_printconstinstr(NeonPrinter* pr, const char* name, NeonBlob* blob, int offset)
{
    uint16_t constant;
    constant = (blob->instrucs[offset + 1].code << 8) | blob->instrucs[offset + 2].code;
    nn_dbg_printinstrname(pr, name);
    nn_printer_writefmt(pr, "%8d ", constant);
    nn_printer_printvalue(pr, blob->constants->values[constant], true, false);
    nn_printer_writefmt(pr, "\n");
    return offset + 3;
}

int nn_dbg_printpropertyinstr(NeonPrinter* pr, const char* name, NeonBlob* blob, int offset)
{
    const char* proptn;
    uint16_t constant;
    constant = (blob->instrucs[offset + 1].code << 8) | blob->instrucs[offset + 2].code;
    nn_dbg_printinstrname(pr, name);
    nn_printer_writefmt(pr, "%8d ", constant);
    nn_printer_printvalue(pr, blob->constants->values[constant], true, false);
    proptn = "";
    if(blob->instrucs[offset + 3].code == 1)
    {
        proptn = "static";
    }
    nn_printer_writefmt(pr, " (%s)", proptn);
    nn_printer_writefmt(pr, "\n");
    return offset + 4;
}

int nn_dbg_printshortinstr(NeonPrinter* pr, const char* name, NeonBlob* blob, int offset)
{
    uint16_t slot;
    slot = (blob->instrucs[offset + 1].code << 8) | blob->instrucs[offset + 2].code;
    nn_dbg_printinstrname(pr, name);
    nn_printer_writefmt(pr, "%8d\n", slot);
    return offset + 3;
}

int nn_dbg_printbyteinstr(NeonPrinter* pr, const char* name, NeonBlob* blob, int offset)
{
    uint8_t slot;
    slot = blob->instrucs[offset + 1].code;
    nn_dbg_printinstrname(pr, name);
    nn_printer_writefmt(pr, "%8d\n", slot);
    return offset + 2;
}

int nn_dbg_printjumpinstr(NeonPrinter* pr, const char* name, int sign, NeonBlob* blob, int offset)
{
    uint16_t jump;
    jump = (uint16_t)(blob->instrucs[offset + 1].code << 8);
    jump |= blob->instrucs[offset + 2].code;
    nn_dbg_printinstrname(pr, name);
    nn_printer_writefmt(pr, "%8d -> %d\n", offset, offset + 3 + sign * jump);
    return offset + 3;
}

int nn_dbg_printtryinstr(NeonPrinter* pr, const char* name, NeonBlob* blob, int offset)
{
    uint16_t finally;
    uint16_t type;
    uint16_t address;
    type = (uint16_t)(blob->instrucs[offset + 1].code << 8);
    type |= blob->instrucs[offset + 2].code;
    address = (uint16_t)(blob->instrucs[offset + 3].code << 8);
    address |= blob->instrucs[offset + 4].code;
    finally = (uint16_t)(blob->instrucs[offset + 5].code << 8);
    finally |= blob->instrucs[offset + 6].code;
    nn_dbg_printinstrname(pr, name);
    nn_printer_writefmt(pr, "%8d -> %d, %d\n", type, address, finally);
    return offset + 7;
}

int nn_dbg_printinvokeinstr(NeonPrinter* pr, const char* name, NeonBlob* blob, int offset)
{
    uint16_t constant;
    uint8_t argcount;
    constant = (uint16_t)(blob->instrucs[offset + 1].code << 8);
    constant |= blob->instrucs[offset + 2].code;
    argcount = blob->instrucs[offset + 3].code;
    nn_dbg_printinstrname(pr, name);
    nn_printer_writefmt(pr, "(%d args) %8d ", argcount, constant);
    nn_printer_printvalue(pr, blob->constants->values[constant], true, false);
    nn_printer_writefmt(pr, "\n");
    return offset + 4;
}

const char* nn_dbg_op2str(uint8_t instruc)
{
    switch(instruc)
    {
        case NEON_OP_GLOBALDEFINE: return "NEON_OP_GLOBALDEFINE";
        case NEON_OP_GLOBALGET: return "NEON_OP_GLOBALGET";
        case NEON_OP_GLOBALSET: return "NEON_OP_GLOBALSET";
        case NEON_OP_LOCALGET: return "NEON_OP_LOCALGET";
        case NEON_OP_LOCALSET: return "NEON_OP_LOCALSET";
        case NEON_OP_FUNCARGSET: return "NEON_OP_FUNCARGSET";
        case NEON_OP_FUNCARGGET: return "NEON_OP_FUNCARGGET";
        case NEON_OP_UPVALUEGET: return "NEON_OP_UPVALUEGET";
        case NEON_OP_UPVALUESET: return "NEON_OP_UPVALUESET";
        case NEON_OP_UPVALUECLOSE: return "NEON_OP_UPVALUECLOSE";
        case NEON_OP_PROPERTYGET: return "NEON_OP_PROPERTYGET";
        case NEON_OP_PROPERTYGETSELF: return "NEON_OP_PROPERTYGETSELF";
        case NEON_OP_PROPERTYSET: return "NEON_OP_PROPERTYSET";
        case NEON_OP_JUMPIFFALSE: return "NEON_OP_JUMPIFFALSE";
        case NEON_OP_JUMPNOW: return "NEON_OP_JUMPNOW";
        case NEON_OP_LOOP: return "NEON_OP_LOOP";
        case NEON_OP_EQUAL: return "NEON_OP_EQUAL";
        case NEON_OP_PRIMGREATER: return "NEON_OP_PRIMGREATER";
        case NEON_OP_PRIMLESSTHAN: return "NEON_OP_PRIMLESSTHAN";
        case NEON_OP_PUSHEMPTY: return "NEON_OP_PUSHEMPTY";
        case NEON_OP_PUSHNULL: return "NEON_OP_PUSHNULL";
        case NEON_OP_PUSHTRUE: return "NEON_OP_PUSHTRUE";
        case NEON_OP_PUSHFALSE: return "NEON_OP_PUSHFALSE";
        case NEON_OP_PRIMADD: return "NEON_OP_PRIMADD";
        case NEON_OP_PRIMSUBTRACT: return "NEON_OP_PRIMSUBTRACT";
        case NEON_OP_PRIMMULTIPLY: return "NEON_OP_PRIMMULTIPLY";
        case NEON_OP_PRIMDIVIDE: return "NEON_OP_PRIMDIVIDE";
        case NEON_OP_PRIMFLOORDIVIDE: return "NEON_OP_PRIMFLOORDIVIDE";
        case NEON_OP_PRIMMODULO: return "NEON_OP_PRIMMODULO";
        case NEON_OP_PRIMPOW: return "NEON_OP_PRIMPOW";
        case NEON_OP_PRIMNEGATE: return "NEON_OP_PRIMNEGATE";
        case NEON_OP_PRIMNOT: return "NEON_OP_PRIMNOT";
        case NEON_OP_PRIMBITNOT: return "NEON_OP_PRIMBITNOT";
        case NEON_OP_PRIMAND: return "NEON_OP_PRIMAND";
        case NEON_OP_PRIMOR: return "NEON_OP_PRIMOR";
        case NEON_OP_PRIMBITXOR: return "NEON_OP_PRIMBITXOR";
        case NEON_OP_PRIMSHIFTLEFT: return "NEON_OP_PRIMSHIFTLEFT";
        case NEON_OP_PRIMSHIFTRIGHT: return "NEON_OP_PRIMSHIFTRIGHT";
        case NEON_OP_PUSHONE: return "NEON_OP_PUSHONE";
        case NEON_OP_PUSHCONSTANT: return "NEON_OP_PUSHCONSTANT";
        case NEON_OP_ECHO: return "NEON_OP_ECHO";
        case NEON_OP_POPONE: return "NEON_OP_POPONE";
        case NEON_OP_DUPONE: return "NEON_OP_DUPONE";
        case NEON_OP_POPN: return "NEON_OP_POPN";
        case NEON_OP_ASSERT: return "NEON_OP_ASSERT";
        case NEON_OP_EXTHROW: return "NEON_OP_EXTHROW";
        case NEON_OP_MAKECLOSURE: return "NEON_OP_MAKECLOSURE";
        case NEON_OP_CALLFUNCTION: return "NEON_OP_CALLFUNCTION";
        case NEON_OP_CALLMETHOD: return "NEON_OP_CALLMETHOD";
        case NEON_OP_CLASSINVOKETHIS: return "NEON_OP_CLASSINVOKETHIS";
        case NEON_OP_CLASSGETTHIS: return "NEON_OP_CLASSGETTHIS";
        case NEON_OP_RETURN: return "NEON_OP_RETURN";
        case NEON_OP_MAKECLASS: return "NEON_OP_MAKECLASS";
        case NEON_OP_MAKEMETHOD: return "NEON_OP_MAKEMETHOD";
        case NEON_OP_CLASSPROPERTYDEFINE: return "NEON_OP_CLASSPROPERTYDEFINE";
        case NEON_OP_CLASSINHERIT: return "NEON_OP_CLASSINHERIT";
        case NEON_OP_CLASSGETSUPER: return "NEON_OP_CLASSGETSUPER";
        case NEON_OP_CLASSINVOKESUPER: return "NEON_OP_CLASSINVOKESUPER";
        case NEON_OP_CLASSINVOKESUPERSELF: return "NEON_OP_CLASSINVOKESUPERSELF";
        case NEON_OP_MAKERANGE: return "NEON_OP_MAKERANGE";
        case NEON_OP_MAKEARRAY: return "NEON_OP_MAKEARRAY";
        case NEON_OP_MAKEDICT: return "NEON_OP_MAKEDICT";
        case NEON_OP_INDEXGET: return "NEON_OP_INDEXGET";
        case NEON_OP_INDEXGETRANGED: return "NEON_OP_INDEXGETRANGED";
        case NEON_OP_INDEXSET: return "NEON_OP_INDEXSET";
        case NEON_OP_IMPORTIMPORT: return "NEON_OP_IMPORTIMPORT";
        case NEON_OP_EXTRY: return "NEON_OP_EXTRY";
        case NEON_OP_EXPOPTRY: return "NEON_OP_EXPOPTRY";
        case NEON_OP_EXPUBLISHTRY: return "NEON_OP_EXPUBLISHTRY";
        case NEON_OP_STRINGIFY: return "NEON_OP_STRINGIFY";
        case NEON_OP_SWITCH: return "NEON_OP_SWITCH";
        case NEON_OP_TYPEOF: return "NEON_OP_TYPEOF";
        case NEON_OP_BREAK_PL: return "NEON_OP_BREAK_PL";
        default:
            break;
    }
    return "<?unknown?>";
}

int nn_dbg_printclosureinstr(NeonPrinter* pr, const char* name, NeonBlob* blob, int offset)
{
    int j;
    int islocal;
    uint16_t index;
    uint16_t constant;
    const char* locn;
    NeonObjFuncScript* function;
    offset++;
    constant = blob->instrucs[offset++].code << 8;
    constant |= blob->instrucs[offset++].code;
    nn_printer_writefmt(pr, "%-16s %8d ", name, constant);
    nn_printer_printvalue(pr, blob->constants->values[constant], true, false);
    nn_printer_writefmt(pr, "\n");
    function = nn_value_asfuncscript(blob->constants->values[constant]);
    for(j = 0; j < function->upvalcount; j++)
    {
        islocal = blob->instrucs[offset++].code;
        index = blob->instrucs[offset++].code << 8;
        index |= blob->instrucs[offset++].code;
        locn = "upvalue";
        if(islocal)
        {
            locn = "local";
        }
        nn_printer_writefmt(pr, "%04d      |                     %s %d\n", offset - 3, locn, (int)index);
    }
    return offset;
}

int nn_dbg_printinstructionat(NeonPrinter* pr, NeonBlob* blob, int offset)
{
    uint8_t instruction;
    const char* opname;
    nn_printer_writefmt(pr, "%08d ", offset);
    if(offset > 0 && blob->instrucs[offset].srcline == blob->instrucs[offset - 1].srcline)
    {
        nn_printer_writefmt(pr, "       | ");
    }
    else
    {
        nn_printer_writefmt(pr, "%8d ", blob->instrucs[offset].srcline);
    }
    instruction = blob->instrucs[offset].code;
    opname = nn_dbg_op2str(instruction);
    switch(instruction)
    {
        case NEON_OP_JUMPIFFALSE:
            return nn_dbg_printjumpinstr(pr, opname, 1, blob, offset);
        case NEON_OP_JUMPNOW:
            return nn_dbg_printjumpinstr(pr, opname, 1, blob, offset);
        case NEON_OP_EXTRY:
            return nn_dbg_printtryinstr(pr, opname, blob, offset);
        case NEON_OP_LOOP:
            return nn_dbg_printjumpinstr(pr, opname, -1, blob, offset);
        case NEON_OP_GLOBALDEFINE:
            return nn_dbg_printconstinstr(pr, opname, blob, offset);
        case NEON_OP_GLOBALGET:
            return nn_dbg_printconstinstr(pr, opname, blob, offset);
        case NEON_OP_GLOBALSET:
            return nn_dbg_printconstinstr(pr, opname, blob, offset);
        case NEON_OP_LOCALGET:
            return nn_dbg_printshortinstr(pr, opname, blob, offset);
        case NEON_OP_LOCALSET:
            return nn_dbg_printshortinstr(pr, opname, blob, offset);
        case NEON_OP_FUNCARGGET:
            return nn_dbg_printshortinstr(pr, opname, blob, offset);
        case NEON_OP_FUNCARGSET:
            return nn_dbg_printshortinstr(pr, opname, blob, offset);
        case NEON_OP_PROPERTYGET:
            return nn_dbg_printconstinstr(pr, opname, blob, offset);
        case NEON_OP_PROPERTYGETSELF:
            return nn_dbg_printconstinstr(pr, opname, blob, offset);
        case NEON_OP_PROPERTYSET:
            return nn_dbg_printconstinstr(pr, opname, blob, offset);
        case NEON_OP_UPVALUEGET:
            return nn_dbg_printshortinstr(pr, opname, blob, offset);
        case NEON_OP_UPVALUESET:
            return nn_dbg_printshortinstr(pr, opname, blob, offset);
        case NEON_OP_EXPOPTRY:
            return nn_dbg_printsimpleinstr(pr, opname, offset);
        case NEON_OP_EXPUBLISHTRY:
            return nn_dbg_printsimpleinstr(pr, opname, offset);
        case NEON_OP_PUSHCONSTANT:
            return nn_dbg_printconstinstr(pr, opname, blob, offset);
        case NEON_OP_EQUAL:
            return nn_dbg_printsimpleinstr(pr, opname, offset);
        case NEON_OP_PRIMGREATER:
            return nn_dbg_printsimpleinstr(pr, opname, offset);
        case NEON_OP_PRIMLESSTHAN:
            return nn_dbg_printsimpleinstr(pr, opname, offset);
        case NEON_OP_PUSHEMPTY:
            return nn_dbg_printsimpleinstr(pr, opname, offset);
        case NEON_OP_PUSHNULL:
            return nn_dbg_printsimpleinstr(pr, opname, offset);
        case NEON_OP_PUSHTRUE:
            return nn_dbg_printsimpleinstr(pr, opname, offset);
        case NEON_OP_PUSHFALSE:
            return nn_dbg_printsimpleinstr(pr, opname, offset);
        case NEON_OP_PRIMADD:
            return nn_dbg_printsimpleinstr(pr, opname, offset);
        case NEON_OP_PRIMSUBTRACT:
            return nn_dbg_printsimpleinstr(pr, opname, offset);
        case NEON_OP_PRIMMULTIPLY:
            return nn_dbg_printsimpleinstr(pr, opname, offset);
        case NEON_OP_PRIMDIVIDE:
            return nn_dbg_printsimpleinstr(pr, opname, offset);
        case NEON_OP_PRIMFLOORDIVIDE:
            return nn_dbg_printsimpleinstr(pr, opname, offset);
        case NEON_OP_PRIMMODULO:
            return nn_dbg_printsimpleinstr(pr, opname, offset);
        case NEON_OP_PRIMPOW:
            return nn_dbg_printsimpleinstr(pr, opname, offset);
        case NEON_OP_PRIMNEGATE:
            return nn_dbg_printsimpleinstr(pr, opname, offset);
        case NEON_OP_PRIMNOT:
            return nn_dbg_printsimpleinstr(pr, opname, offset);
        case NEON_OP_PRIMBITNOT:
            return nn_dbg_printsimpleinstr(pr, opname, offset);
        case NEON_OP_PRIMAND:
            return nn_dbg_printsimpleinstr(pr, opname, offset);
        case NEON_OP_PRIMOR:
            return nn_dbg_printsimpleinstr(pr, opname, offset);
        case NEON_OP_PRIMBITXOR:
            return nn_dbg_printsimpleinstr(pr, opname, offset);
        case NEON_OP_PRIMSHIFTLEFT:
            return nn_dbg_printsimpleinstr(pr, opname, offset);
        case NEON_OP_PRIMSHIFTRIGHT:
            return nn_dbg_printsimpleinstr(pr, opname, offset);
        case NEON_OP_PUSHONE:
            return nn_dbg_printsimpleinstr(pr, opname, offset);
        case NEON_OP_IMPORTIMPORT:
            return nn_dbg_printsimpleinstr(pr, opname, offset);
        case NEON_OP_TYPEOF:
            return nn_dbg_printsimpleinstr(pr, opname, offset);
        case NEON_OP_ECHO:
            return nn_dbg_printsimpleinstr(pr, opname, offset);
        case NEON_OP_STRINGIFY:
            return nn_dbg_printsimpleinstr(pr, opname, offset);
        case NEON_OP_EXTHROW:
            return nn_dbg_printsimpleinstr(pr, opname, offset);
        case NEON_OP_POPONE:
            return nn_dbg_printsimpleinstr(pr, opname, offset);
        case NEON_OP_UPVALUECLOSE:
            return nn_dbg_printsimpleinstr(pr, opname, offset);
        case NEON_OP_DUPONE:
            return nn_dbg_printsimpleinstr(pr, opname, offset);
        case NEON_OP_ASSERT:
            return nn_dbg_printsimpleinstr(pr, opname, offset);
        case NEON_OP_POPN:
            return nn_dbg_printshortinstr(pr, opname, blob, offset);
            /* non-user objects... */
        case NEON_OP_SWITCH:
            return nn_dbg_printshortinstr(pr, opname, blob, offset);
            /* data container manipulators */
        case NEON_OP_MAKERANGE:
            return nn_dbg_printshortinstr(pr, opname, blob, offset);
        case NEON_OP_MAKEARRAY:
            return nn_dbg_printshortinstr(pr, opname, blob, offset);
        case NEON_OP_MAKEDICT:
            return nn_dbg_printshortinstr(pr, opname, blob, offset);
        case NEON_OP_INDEXGET:
            return nn_dbg_printbyteinstr(pr, opname, blob, offset);
        case NEON_OP_INDEXGETRANGED:
            return nn_dbg_printbyteinstr(pr, opname, blob, offset);
        case NEON_OP_INDEXSET:
            return nn_dbg_printsimpleinstr(pr, opname, offset);
        case NEON_OP_MAKECLOSURE:
            return nn_dbg_printclosureinstr(pr, opname, blob, offset);
        case NEON_OP_CALLFUNCTION:
            return nn_dbg_printbyteinstr(pr, opname, blob, offset);
        case NEON_OP_CALLMETHOD:
            return nn_dbg_printinvokeinstr(pr, opname, blob, offset);
        case NEON_OP_CLASSINVOKETHIS:
            return nn_dbg_printinvokeinstr(pr, opname, blob, offset);
        case NEON_OP_RETURN:
            return nn_dbg_printsimpleinstr(pr, opname, offset);
        case NEON_OP_CLASSGETTHIS:
            return nn_dbg_printsimpleinstr(pr, opname, offset);
        case NEON_OP_MAKECLASS:
            return nn_dbg_printconstinstr(pr, opname, blob, offset);
        case NEON_OP_MAKEMETHOD:
            return nn_dbg_printconstinstr(pr, opname, blob, offset);
        case NEON_OP_CLASSPROPERTYDEFINE:
            return nn_dbg_printpropertyinstr(pr, opname, blob, offset);
        case NEON_OP_CLASSGETSUPER:
            return nn_dbg_printconstinstr(pr, opname, blob, offset);
        case NEON_OP_CLASSINHERIT:
            return nn_dbg_printsimpleinstr(pr, opname, offset);
        case NEON_OP_CLASSINVOKESUPER:
            return nn_dbg_printinvokeinstr(pr, opname, blob, offset);
        case NEON_OP_CLASSINVOKESUPERSELF:
            return nn_dbg_printbyteinstr(pr, opname, blob, offset);
        default:
            {
                nn_printer_writefmt(pr, "unknown opcode %d\n", instruction);
            }
            break;
    }
    return offset + 1;
}

void nn_blob_init(NeonState* state, NeonBlob* blob)
{
    blob->count = 0;
    blob->capacity = 0;
    blob->instrucs = NULL;
    blob->constants = nn_valarray_make(state);
    blob->argdefvals = nn_valarray_make(state);
}

void nn_blob_push(NeonState* state, NeonBlob* blob, NeonInstruction ins)
{
    int oldcapacity;
    if(blob->capacity < blob->count + 1)
    {
        oldcapacity = blob->capacity;
        blob->capacity = GROW_CAPACITY(oldcapacity);
        blob->instrucs = (NeonInstruction*)nn_gcmem_growarray(state, sizeof(NeonInstruction), blob->instrucs, oldcapacity, blob->capacity);
    }
    blob->instrucs[blob->count] = ins;
    blob->count++;
}

void nn_blob_destroy(NeonState* state, NeonBlob* blob)
{
    if(blob->instrucs != NULL)
    {
        nn_gcmem_freearray(state, sizeof(NeonInstruction), blob->instrucs, blob->capacity);
    }
    nn_valarray_destroy(blob->constants);
    nn_valarray_destroy(blob->argdefvals);
}

int nn_blob_pushconst(NeonState* state, NeonBlob* blob, NeonValue value)
{
    (void)state;
    nn_valarray_push(blob->constants, value);
    return blob->constants->count - 1;
}


int nn_blob_pushargdefval(NeonState* state, NeonBlob* blob, NeonValue value)
{
    (void)state;
    nn_valarray_push(blob->argdefvals, value);
    return blob->argdefvals->count - 1;
}

NeonProperty nn_property_makewithpointer(NeonState* state, NeonValue val, NeonFieldType type)
{
    NeonProperty vf;
    (void)state;
    memset(&vf, 0, sizeof(NeonProperty));
    vf.type = type;
    vf.value = val;
    vf.havegetset = false;
    return vf;
}

NeonProperty nn_property_makewithgetset(NeonState* state, NeonValue val, NeonValue getter, NeonValue setter, NeonFieldType type)
{
    bool getisfn;
    bool setisfn;
    NeonProperty np;
    np = nn_property_makewithpointer(state, val, type);
    setisfn = nn_value_iscallable(setter);
    getisfn = nn_value_iscallable(getter);
    if(getisfn || setisfn)
    {
        np.getset.setter = setter;
        np.getset.getter = getter;
    }
    return np;
}

NeonProperty nn_property_make(NeonState* state, NeonValue val, NeonFieldType type)
{
    return nn_property_makewithpointer(state, val, type);
}

NeonHashTable* nn_table_make(NeonState* state)
{
    NeonHashTable* table;
    table = (NeonHashTable*)nn_gcmem_allocate(state, sizeof(NeonHashTable), 1);
    if(table == NULL)
    {
        return NULL;
    }
    table->pvm = state;
    table->active = true;
    table->count = 0;
    table->capacity = 0;
    table->entries = NULL;
    return table;
}

void nn_table_destroy(NeonHashTable* table)
{
    NeonState* state;
    if(table != NULL)
    {
        state = table->pvm;
        nn_gcmem_freearray(state, sizeof(NeonHashEntry), table->entries, table->capacity);
        memset(table, 0, sizeof(NeonHashTable));
        nn_gcmem_release(state, table, sizeof(NeonHashTable));
    }
}

NeonHashEntry* nn_table_findentrybyvalue(NeonHashTable* table, NeonHashEntry* entries, int capacity, NeonValue key)
{
    uint32_t hash;
    uint32_t index;
    NeonState* state;
    NeonHashEntry* entry;
    NeonHashEntry* tombstone;
    state = table->pvm;
    hash = nn_value_hashvalue(key);
    #if defined(DEBUG_TABLE) && DEBUG_TABLE
    fprintf(stderr, "looking for key ");
    nn_printer_printvalue(state->debugwriter, key, true, false);
    fprintf(stderr, " with hash %u in table...\n", hash);
    #endif
    index = hash & (capacity - 1);
    tombstone = NULL;
    while(true)
    {
        entry = &entries[index];
        if(nn_value_isempty(entry->key))
        {
            if(nn_value_isnull(entry->value.value))
            {
                /* empty entry */
                if(tombstone != NULL)
                {
                    return tombstone;
                }
                else
                {
                    return entry;
                }
            }
            else
            {
                /* we found a tombstone. */
                if(tombstone == NULL)
                {
                    tombstone = entry;
                }
            }
        }
        else if(nn_value_compare(state, key, entry->key))
        {
            return entry;
        }
        index = (index + 1) & (capacity - 1);
    }
    return NULL;
}

NeonHashEntry* nn_table_findentrybystr(NeonHashTable* table, NeonHashEntry* entries, int capacity, NeonValue valkey, const char* kstr, size_t klen, uint32_t hash)
{
    bool havevalhash;
    uint32_t index;
    uint32_t valhash;
    NeonObjString* entoskey;
    NeonHashEntry* entry;
    NeonHashEntry* tombstone;
    NeonState* state;
    state = table->pvm;
    (void)valhash;
    (void)havevalhash;
    #if defined(DEBUG_TABLE) && DEBUG_TABLE
    fprintf(stderr, "looking for key ");
    nn_printer_printvalue(state->debugwriter, key, true, false);
    fprintf(stderr, " with hash %u in table...\n", hash);
    #endif
    valhash = 0;
    havevalhash = false;
    index = hash & (capacity - 1);
    tombstone = NULL;
    while(true)
    {
        entry = &entries[index];
        if(nn_value_isempty(entry->key))
        {
            if(nn_value_isnull(entry->value.value))
            {
                /* empty entry */
                if(tombstone != NULL)
                {
                    return tombstone;
                }
                else
                {
                    return entry;
                }
            }
            else
            {
                /* we found a tombstone. */
                if(tombstone == NULL)
                {
                    tombstone = entry;
                }
            }
        }
        if(nn_value_isstring(entry->key))
        {
            entoskey = nn_value_asstring(entry->key);
            if(entoskey->sbuf->length == klen)
            {
                if(memcmp(kstr, entoskey->sbuf->data, klen) == 0)
                {
                    return entry;
                }
            }
        }
        else
        {
            if(!nn_value_isempty(valkey))
            {
                if(nn_value_compare(state, valkey, entry->key))
                {
                    return entry;
                }
            }
        }
        index = (index + 1) & (capacity - 1);
    }
    return NULL;
}

NeonProperty* nn_table_getfieldbyvalue(NeonHashTable* table, NeonValue key)
{
    NeonState* state;
    NeonHashEntry* entry;
    (void)state;
    state = table->pvm;
    if(table->count == 0 || table->entries == NULL)
    {
        return NULL;
    }
    #if defined(DEBUG_TABLE) && DEBUG_TABLE
    fprintf(stderr, "getting entry with hash %u...\n", nn_value_hashvalue(key));
    #endif
    entry = nn_table_findentrybyvalue(table, table->entries, table->capacity, key);
    if(nn_value_isempty(entry->key) || nn_value_isnull(entry->key))
    {
        return NULL;
    }
    #if defined(DEBUG_TABLE) && DEBUG_TABLE
    fprintf(stderr, "found entry for hash %u == ", nn_value_hashvalue(entry->key));
    nn_printer_printvalue(state->debugwriter, entry->value.value, true, false);
    fprintf(stderr, "\n");
    #endif
    return &entry->value;
}

NeonProperty* nn_table_getfieldbystr(NeonHashTable* table, NeonValue valkey, const char* kstr, size_t klen, uint32_t hash)
{
    NeonState* state;
    NeonHashEntry* entry;
    (void)state;
    state = table->pvm;
    if(table->count == 0 || table->entries == NULL)
    {
        return NULL;
    }
    #if defined(DEBUG_TABLE) && DEBUG_TABLE
    fprintf(stderr, "getting entry with hash %u...\n", nn_value_hashvalue(key));
    #endif
    entry = nn_table_findentrybystr(table, table->entries, table->capacity, valkey, kstr, klen, hash);
    if(nn_value_isempty(entry->key) || nn_value_isnull(entry->key))
    {
        return NULL;
    }
    #if defined(DEBUG_TABLE) && DEBUG_TABLE
    fprintf(stderr, "found entry for hash %u == ", nn_value_hashvalue(entry->key));
    nn_printer_printvalue(state->debugwriter, entry->value.value, true, false);
    fprintf(stderr, "\n");
    #endif
    return &entry->value;
}

NeonProperty* nn_table_getfieldbyostr(NeonHashTable* table, NeonObjString* str)
{
    return nn_table_getfieldbystr(table, nn_value_makeempty(), str->sbuf->data, str->sbuf->length, str->hash);
}

NeonProperty* nn_table_getfieldbycstr(NeonHashTable* table, const char* kstr)
{
    size_t klen;
    uint32_t hash;
    klen = strlen(kstr);
    hash = nn_util_hashstring(kstr, klen);
    return nn_table_getfieldbystr(table, nn_value_makeempty(), kstr, klen, hash);
}

NeonProperty* nn_table_getfield(NeonHashTable* table, NeonValue key)
{
    NeonObjString* oskey;
    if(nn_value_isstring(key))
    {
        oskey = nn_value_asstring(key);
        return nn_table_getfieldbystr(table, key, oskey->sbuf->data, oskey->sbuf->length, oskey->hash);
    }
    return nn_table_getfieldbyvalue(table, key);
}

bool nn_table_get(NeonHashTable* table, NeonValue key, NeonValue* value)
{
    NeonProperty* field;
    field = nn_table_getfield(table, key);
    if(field != NULL)
    {
        *value = field->value;
        return true;
    }
    return false;
}

void nn_table_adjustcapacity(NeonHashTable* table, int capacity)
{
    int i;
    NeonState* state;
    NeonHashEntry* dest;
    NeonHashEntry* entry;
    NeonHashEntry* entries;
    state = table->pvm;
    entries = (NeonHashEntry*)nn_gcmem_allocate(state, sizeof(NeonHashEntry), capacity);
    for(i = 0; i < capacity; i++)
    {
        entries[i].key = nn_value_makeempty();
        entries[i].value = nn_property_make(state, nn_value_makenull(), NEON_PROPTYPE_VALUE);
    }
    /* repopulate buckets */
    table->count = 0;
    for(i = 0; i < table->capacity; i++)
    {
        entry = &table->entries[i];
        if(nn_value_isempty(entry->key))
        {
            continue;
        }
        dest = nn_table_findentrybyvalue(table, entries, capacity, entry->key);
        dest->key = entry->key;
        dest->value = entry->value;
        table->count++;
    }
    /* free the old entries... */
    nn_gcmem_freearray(state, sizeof(NeonHashEntry), table->entries, table->capacity);
    table->entries = entries;
    table->capacity = capacity;
}

bool nn_table_setwithtype(NeonHashTable* table, NeonValue key, NeonValue value, NeonFieldType ftyp, bool keyisstring)
{
    bool isnew;
    int capacity;
    NeonState* state;
    NeonHashEntry* entry;
    (void)keyisstring;
    state = table->pvm;
    if(table->count + 1 > table->capacity * NEON_CFG_MAXTABLELOAD)
    {
        capacity = GROW_CAPACITY(table->capacity);
        nn_table_adjustcapacity(table, capacity);
    }
    entry = nn_table_findentrybyvalue(table, table->entries, table->capacity, key);
    isnew = nn_value_isempty(entry->key);
    if(isnew && nn_value_isnull(entry->value.value))
    {
        table->count++;
    }
    /* overwrites existing entries. */
    entry->key = key;
    entry->value = nn_property_make(state, value, ftyp);
    return isnew;
}

bool nn_table_set(NeonHashTable* table, NeonValue key, NeonValue value)
{
    return nn_table_setwithtype(table, key, value, NEON_PROPTYPE_VALUE, nn_value_isstring(key));
}

bool nn_table_setcstrwithtype(NeonHashTable* table, const char* cstrkey, NeonValue value, NeonFieldType ftype)
{
    NeonObjString* os;
    NeonState* state;
    state = table->pvm;
    os = nn_string_copycstr(state, cstrkey);
    return nn_table_setwithtype(table, nn_value_fromobject(os), value, ftype, true);
}

bool nn_table_setcstr(NeonHashTable* table, const char* cstrkey, NeonValue value)
{
    return nn_table_setcstrwithtype(table, cstrkey, value, NEON_PROPTYPE_VALUE);
}

bool nn_table_delete(NeonHashTable* table, NeonValue key)
{
    NeonHashEntry* entry;
    if(table->count == 0)
    {
        return false;
    }
    /* find the entry */
    entry = nn_table_findentrybyvalue(table, table->entries, table->capacity, key);
    if(nn_value_isempty(entry->key))
    {
        return false;
    }
    /* place a tombstone in the entry. */
    entry->key = nn_value_makeempty();
    entry->value = nn_property_make(table->pvm, nn_value_makebool(true), NEON_PROPTYPE_VALUE);
    return true;
}

void nn_table_addall(NeonHashTable* from, NeonHashTable* to)
{
    int i;
    NeonHashEntry* entry;
    for(i = 0; i < from->capacity; i++)
    {
        entry = &from->entries[i];
        if(!nn_value_isempty(entry->key))
        {
            nn_table_setwithtype(to, entry->key, entry->value.value, entry->value.type, false);
        }
    }
}

void nn_table_importall(NeonHashTable* from, NeonHashTable* to)
{
    int i;
    NeonHashEntry* entry;
    for(i = 0; i < (int)from->capacity; i++)
    {
        entry = &from->entries[i];
        if(!nn_value_isempty(entry->key) && !nn_value_ismodule(entry->value.value))
        {
            /* Don't import private values */
            if(nn_value_isstring(entry->key) && nn_value_asstring(entry->key)->sbuf->data[0] == '_')
            {
                continue;
            }
            nn_table_setwithtype(to, entry->key, entry->value.value, entry->value.type, false);
        }
    }
}

void nn_table_copy(NeonHashTable* from, NeonHashTable* to)
{
    int i;
    NeonState* state;
    NeonHashEntry* entry;
    state = from->pvm;
    for(i = 0; i < (int)from->capacity; i++)
    {
        entry = &from->entries[i];
        if(!nn_value_isempty(entry->key))
        {
            nn_table_setwithtype(to, entry->key, nn_value_copyvalue(state, entry->value.value), entry->value.type, false);
        }
    }
}

NeonObjString* nn_table_findstring(NeonHashTable* table, const char* chars, size_t length, uint32_t hash)
{
    size_t slen;
    uint32_t index;
    const char* sdata;
    NeonHashEntry* entry;
    NeonObjString* string;
    if(table->count == 0)
    {
        return NULL;
    }
    index = hash & (table->capacity - 1);
    while(true)
    {
        entry = &table->entries[index];
        if(nn_value_isempty(entry->key))
        {
            /*
            // stop if we find an empty non-tombstone entry
            //if (nn_value_isnull(entry->value))
            */
            {
                return NULL;
            }
        }
        string = nn_value_asstring(entry->key);
        slen = string->sbuf->length;
        sdata = string->sbuf->data;
        if((slen == length) && (string->hash == hash) && memcmp(sdata, chars, length) == 0)
        {
            /* we found it */
            return string;
        }
        index = (index + 1) & (table->capacity - 1);
    }
}

NeonValue nn_table_findkey(NeonHashTable* table, NeonValue value)
{
    int i;
    NeonHashEntry* entry;
    for(i = 0; i < (int)table->capacity; i++)
    {
        entry = &table->entries[i];
        if(!nn_value_isnull(entry->key) && !nn_value_isempty(entry->key))
        {
            if(nn_value_compare(table->pvm, entry->value.value, value))
            {
                return entry->key;
            }
        }
    }
    return nn_value_makenull();
}

NeonObjArray* nn_table_getkeys(NeonHashTable* table)
{
    int i;
    NeonState* state;
    NeonObjArray* list;
    NeonHashEntry* entry;
    state = table->pvm;
    list = (NeonObjArray*)nn_gcmem_protect(state, (NeonObject*)nn_object_makearray(state));
    for(i = 0; i < table->capacity; i++)
    {
        entry = &table->entries[i];
        if(!nn_value_isnull(entry->key) && !nn_value_isempty(entry->key))
        {
            nn_valarray_push(list->varray, entry->key);
        }
    }
    return list;
}

void nn_table_print(NeonState* state, NeonPrinter* pr, NeonHashTable* table, const char* name)
{
    int i;
    NeonHashEntry* entry;
    (void)state;
    nn_printer_writefmt(pr, "<HashTable of %s : {\n", name);
    for(i = 0; i < table->capacity; i++)
    {
        entry = &table->entries[i];
        if(!nn_value_isempty(entry->key))
        {
            nn_printer_printvalue(pr, entry->key, true, true);
            nn_printer_writefmt(pr, ": ");
            nn_printer_printvalue(pr, entry->value.value, true, true);
            if(i != table->capacity - 1)
            {
                nn_printer_writefmt(pr, ",\n");
            }
        }
    }
    nn_printer_writefmt(pr, "}>\n");
}

void nn_table_mark(NeonState* state, NeonHashTable* table)
{
    int i;
    NeonHashEntry* entry;
    if(table == NULL)
    {
        return;
    }
    if(!table->active)
    {
        nn_state_warn(state, "trying to mark inactive hashtable <%p>!", table);
        return;
    }
    for(i = 0; i < table->capacity; i++)
    {
        entry = &table->entries[i];
        if(entry != NULL)
        {
            nn_gcmem_markvalue(state, entry->key);
            nn_gcmem_markvalue(state, entry->value.value);
        }
    }
}

void nn_table_removewhites(NeonState* state, NeonHashTable* table)
{
    int i;
    NeonHashEntry* entry;
    for(i = 0; i < table->capacity; i++)
    {
        entry = &table->entries[i];
        if(nn_value_isobject(entry->key) && nn_value_asobject(entry->key)->mark != state->markvalue)
        {
            nn_table_delete(table, entry->key);
        }
    }
}

NeonValArray* nn_valarray_makewithsize(NeonState* state, size_t size)
{
    NeonValArray* arr;
    arr = (NeonValArray*)nn_gcmem_allocate(state, sizeof(NeonValArray), 1);
    if(arr == NULL)
    {
        return NULL;
    }
    arr->pvm = state;
    arr->tsize = size;
    arr->capacity = 0;
    arr->count = 0;
    arr->values = NULL;
    return arr;
}

NeonValArray* nn_valarray_make(NeonState* state)
{
    return nn_valarray_makewithsize(state, sizeof(NeonValue));
}

void nn_valarray_destroy(NeonValArray* array)
{
    NeonState* state;
    state = array->pvm;
    nn_gcmem_freearray(state, array->tsize, array->values, array->capacity);
    nn_gcmem_release(state, array, sizeof(NeonValArray));
}

void nn_valarray_mark(NeonValArray* array)
{
    int i;
    NeonState* state;
    state = array->pvm;
    for(i = 0; i < array->count; i++)
    {
        nn_gcmem_markvalue(state, array->values[i]);
    }
}

void nn_valarray_push(NeonValArray* array, NeonValue value)
{
    int oldcapacity;
    NeonState* state;
    state = array->pvm;
    if(array->capacity < array->count + 1)
    {
        oldcapacity = array->capacity;
        array->capacity = GROW_CAPACITY(oldcapacity);
        array->values = (NeonValue*)nn_gcmem_growarray(state, array->tsize, array->values, oldcapacity, array->capacity);
    }
    array->values[array->count] = value;
    array->count++;
}

void nn_valarray_insert(NeonValArray* array, NeonValue value, int index)
{
    int i;
    int capacity;
    NeonState* state;
    state = array->pvm;
    if(array->capacity <= index)
    {
        array->capacity = GROW_CAPACITY(index);
        array->values = (NeonValue*)nn_gcmem_growarray(state, array->tsize, array->values, array->count, array->capacity);
    }
    else if(array->capacity < array->count + 2)
    {
        capacity = array->capacity;
        array->capacity = GROW_CAPACITY(capacity);
        array->values = (NeonValue*)nn_gcmem_growarray(state, array->tsize, array->values, capacity, array->capacity);
    }
    if(index <= array->count)
    {
        for(i = array->count - 1; i >= index; i--)
        {
            array->values[i + 1] = array->values[i];
        }
    }
    else
    {
        for(i = array->count; i < index; i++)
        {
            /* null out overflow indices */
            array->values[i] = nn_value_makenull();
            array->count++;
        }
    }
    array->values[index] = value;
    array->count++;
}

void nn_printer_initvars(NeonState* state, NeonPrinter* pr, NeonPrMode mode)
{
    pr->pvm = state;
    pr->fromstack = false;
    pr->wrmode = NEON_PRMODE_UNDEFINED;
    pr->shouldclose = false;
    pr->shouldflush = false;
    pr->stringtaken = false;
    pr->shortenvalues = false;
    pr->maxvallength = 15;
    pr->strbuf = NULL;
    pr->handle = NULL;
    pr->wrmode = mode;
}

NeonPrinter* nn_printer_makeundefined(NeonState* state, NeonPrMode mode)
{
    NeonPrinter* pr;
    (void)state;
    pr = (NeonPrinter*)nn_gcmem_allocate(state, sizeof(NeonPrinter), 1);
    if(!pr)
    {
        fprintf(stderr, "cannot allocate NeonPrinter\n");
        return NULL;
    }
    nn_printer_initvars(state, pr, mode);
    return pr;
}

NeonPrinter* nn_printer_makeio(NeonState* state, FILE* fh, bool shouldclose)
{
    NeonPrinter* pr;
    pr = nn_printer_makeundefined(state, NEON_PRMODE_FILE);
    pr->handle = fh;
    pr->shouldclose = shouldclose;
    return pr;
}

NeonPrinter* nn_printer_makestring(NeonState* state)
{
    NeonPrinter* pr;
    pr = nn_printer_makeundefined(state, NEON_PRMODE_STRING);
    pr->strbuf = dyn_strbuf_makeempty(0);
    return pr;
}

void nn_printer_makestackio(NeonState* state, NeonPrinter* pr, FILE* fh, bool shouldclose)
{
    nn_printer_initvars(state, pr, NEON_PRMODE_FILE);
    pr->fromstack = true;
    pr->handle = fh;
    pr->shouldclose = shouldclose;
}

void nn_printer_makestackstring(NeonState* state, NeonPrinter* pr)
{
    nn_printer_initvars(state, pr, NEON_PRMODE_STRING);
    pr->fromstack = true;
    pr->wrmode = NEON_PRMODE_STRING;
    pr->strbuf = dyn_strbuf_makeempty(0);
}

void nn_printer_destroy(NeonPrinter* pr)
{
    NeonState* state;
    (void)state;
    if(pr == NULL)
    {
        return;
    }
    if(pr->wrmode == NEON_PRMODE_UNDEFINED)
    {
        return;
    }
    /*fprintf(stderr, "nn_printer_destroy: pr->wrmode=%d\n", pr->wrmode);*/
    state = pr->pvm;
    if(pr->wrmode == NEON_PRMODE_STRING)
    {
        if(!pr->stringtaken)
        {
            dyn_strbuf_destroy(pr->strbuf);
        }
    }
    else if(pr->wrmode == NEON_PRMODE_FILE)
    {
        if(pr->shouldclose)
        {
            //fclose(pr->handle);
        }
    }
    if(!pr->fromstack)
    {
        nn_util_memfree(state, pr);
        pr = NULL;
    }
}

NeonObjString* nn_printer_takestring(NeonPrinter* pr)
{
    uint32_t hash;
    NeonState* state;
    NeonObjString* os;
    state = pr->pvm;
    hash = nn_util_hashstring(pr->strbuf->data, pr->strbuf->length);
    os = nn_string_makefromstrbuf(state, pr->strbuf, hash);
    pr->stringtaken = true;
    return os;
}

bool nn_printer_writestringl(NeonPrinter* pr, const char* estr, size_t elen)
{
    if(pr->wrmode == NEON_PRMODE_FILE)
    {
        fwrite(estr, sizeof(char), elen, pr->handle);
        if(pr->shouldflush)
        {
            fflush(pr->handle);
        }
    }
    else if(pr->wrmode == NEON_PRMODE_STRING)
    {
        dyn_strbuf_appendstrn(pr->strbuf, estr, elen);
    }
    else
    {
        return false;
    }
    return true;
}

bool nn_printer_writestring(NeonPrinter* pr, const char* estr)
{
    return nn_printer_writestringl(pr, estr, strlen(estr));
}

bool nn_printer_writechar(NeonPrinter* pr, int b)
{
    char ch;
    if(pr->wrmode == NEON_PRMODE_STRING)
    {
        ch = b;
        nn_printer_writestringl(pr, &ch, 1);
    }
    else if(pr->wrmode == NEON_PRMODE_FILE)
    {
        fputc(b, pr->handle);
        if(pr->shouldflush)
        {
            fflush(pr->handle);
        }
    }
    return true;
}

bool nn_printer_writeescapedchar(NeonPrinter* pr, int ch)
{
    switch(ch)
    {
        case '\'':
            {
                nn_printer_writestring(pr, "\\\'");
            }
            break;
        case '\"':
            {
                nn_printer_writestring(pr, "\\\"");
            }
            break;
        case '\\':
            {
                nn_printer_writestring(pr, "\\\\");
            }
            break;
        case '\b':
            {
                nn_printer_writestring(pr, "\\b");
            }
            break;
        case '\f':
            {
                nn_printer_writestring(pr, "\\f");
            }
            break;
        case '\n':
            {
                nn_printer_writestring(pr, "\\n");
            }
            break;
        case '\r':
            {
                nn_printer_writestring(pr, "\\r");
            }
            break;
        case '\t':
            {
                nn_printer_writestring(pr, "\\t");
            }
            break;
        case 0:
            {
                nn_printer_writestring(pr, "\\0");
            }
            break;
        default:
            {
                nn_printer_writefmt(pr, "\\x%02x", (unsigned char)ch);
            }
            break;
    }
    return true;
}

bool nn_printer_writequotedstring(NeonPrinter* pr, const char* str, size_t len, bool withquot)
{
    int bch;
    size_t i;
    bch = 0;
    if(withquot)
    {
        nn_printer_writechar(pr, '"');
    }
    for(i = 0; i < len; i++)
    {
        bch = str[i];
        if((bch < 32) || (bch > 127) || (bch == '\"') || (bch == '\\'))
        {
            nn_printer_writeescapedchar(pr, bch);
        }
        else
        {
            nn_printer_writechar(pr, bch);
        }
    }
    if(withquot)
    {
        nn_printer_writechar(pr, '"');
    }
    return true;
}

bool nn_printer_vwritefmttostring(NeonPrinter* pr, const char* fmt, va_list va)
{
    #if 0
        size_t wsz;
        size_t needed;
        char* buf;
        va_list copy;
        va_copy(copy, va);
        needed = 1 + vsnprintf(NULL, 0, fmt, copy);
        va_end(copy);
        buf = (char*)nn_gcmem_allocate(pr->pvm, sizeof(char), needed + 1);
        if(!buf)
        {
            return false;
        }
        memset(buf, 0, needed + 1);
        wsz = vsnprintf(buf, needed, fmt, va);
        nn_printer_writestringl(pr, buf, wsz);
        nn_util_memfree(pr->pvm, buf);
    #else
        dyn_strbuf_appendformatv(pr->strbuf, pr->strbuf->length, fmt, va);
    #endif
    return true;
}

bool nn_printer_vwritefmt(NeonPrinter* pr, const char* fmt, va_list va)
{
    if(pr->wrmode == NEON_PRMODE_STRING)
    {
        return nn_printer_vwritefmttostring(pr, fmt, va);
    }
    else if(pr->wrmode == NEON_PRMODE_FILE)
    {
        vfprintf(pr->handle, fmt, va);
        if(pr->shouldflush)
        {
            fflush(pr->handle);
        }
    }
    return true;
}

bool nn_printer_writefmt(NeonPrinter* pr, const char* fmt, ...) NEON_ATTR_PRINTFLIKE(2, 3);

bool nn_printer_writefmt(NeonPrinter* pr, const char* fmt, ...)
{
    bool b;
    va_list va;
    va_start(va, fmt);
    b = nn_printer_vwritefmt(pr, fmt, va);
    va_end(va);
    return b;
}

void nn_printer_printfunction(NeonPrinter* pr, NeonObjFuncScript* func)
{
    if(func->name == NULL)
    {
        nn_printer_writefmt(pr, "<script at %p>", (void*)func);
    }
    else
    {
        if(func->isvariadic)
        {
            nn_printer_writefmt(pr, "<function %s(%d...) at %p>", func->name->sbuf->data, func->arity, (void*)func);
        }
        else
        {
            nn_printer_writefmt(pr, "<function %s(%d) at %p>", func->name->sbuf->data, func->arity, (void*)func);
        }
    }
}

void nn_printer_printarray(NeonPrinter* pr, NeonObjArray* list)
{
    size_t i;
    size_t vsz;
    bool isrecur;
    NeonValue val;
    NeonObjArray* subarr;
    vsz = list->varray->count;
    nn_printer_writefmt(pr, "[");
    for(i = 0; i < vsz; i++)
    {
        isrecur = false;
        val = list->varray->values[i];
        if(nn_value_isarray(val))
        {
            subarr = nn_value_asarray(val);
            if(subarr == list)
            {
                isrecur = true;
            }
        }
        if(isrecur)
        {
            nn_printer_writefmt(pr, "<recursion>");
        }
        else
        {
            nn_printer_printvalue(pr, val, true, true);
        }
        if(i != vsz - 1)
        {
            nn_printer_writefmt(pr, ", ");
        }
        if(pr->shortenvalues && (i >= pr->maxvallength))
        {
            nn_printer_writefmt(pr, " [%ld items]", vsz);
            break;
        }
    }
    nn_printer_writefmt(pr, "]");
}

void nn_printer_printdict(NeonPrinter* pr, NeonObjDict* dict)
{
    size_t i;
    size_t dsz;
    bool keyisrecur;
    bool valisrecur;
    NeonValue val;
    NeonObjDict* subdict;
    NeonProperty* field;
    dsz = dict->names->count;
    nn_printer_writefmt(pr, "{");
    for(i = 0; i < dsz; i++)
    {
        valisrecur = false;
        keyisrecur = false;
        val = dict->names->values[i];
        if(nn_value_isdict(val))
        {
            subdict = nn_value_asdict(val);
            if(subdict == dict)
            {
                valisrecur = true;
            }
        }
        if(valisrecur)
        {
            nn_printer_writefmt(pr, "<recursion>");
        }
        else
        {
            nn_printer_printvalue(pr, val, true, true);
        }
        nn_printer_writefmt(pr, ": ");
        field = nn_table_getfield(dict->htab, dict->names->values[i]);
        if(field != NULL)
        {
            if(nn_value_isdict(field->value))
            {
                subdict = nn_value_asdict(field->value);
                if(subdict == dict)
                {
                    keyisrecur = true;
                }
            }
            if(keyisrecur)
            {
                nn_printer_writefmt(pr, "<recursion>");
            }
            else
            {
                nn_printer_printvalue(pr, field->value, true, true);
            }
        }
        if(i != dsz - 1)
        {
            nn_printer_writefmt(pr, ", ");
        }
        if(pr->shortenvalues && (pr->maxvallength >= i))
        {
            nn_printer_writefmt(pr, " [%ld items]", dsz);
            break;
        }
    }
    nn_printer_writefmt(pr, "}");
}

void nn_printer_printfile(NeonPrinter* pr, NeonObjFile* file)
{
    nn_printer_writefmt(pr, "<file at %s in mode %s>", file->path->sbuf->data, file->mode->sbuf->data);
}

void nn_printer_printinstance(NeonPrinter* pr, NeonObjInstance* instance, bool invmethod)
{
    (void)invmethod;
    #if 0
    int arity;
    NeonPrinter subw;
    NeonValue resv;
    NeonValue thisval;
    NeonProperty* field;
    NeonState* state;
    NeonObjString* os;
    NeonObjArray* args;
    state = pr->pvm;
    if(invmethod)
    {
        field = nn_table_getfieldbycstr(instance->klass->methods, "toString");
        if(field != NULL)
        {
            args = nn_object_makearray(state);
            thisval = nn_value_fromobject(instance);
            arity = nn_nestcall_prepare(state, field->value, thisval, args);
            fprintf(stderr, "arity = %d\n", arity);
            nn_vm_stackpop(state);
            nn_vm_stackpush(state, thisval);
            if(nn_nestcall_callfunction(state, field->value, thisval, args, &resv))
            {
                nn_printer_makestackstring(state, &subw);
                nn_printer_printvalue(&subw, resv, false, false);
                os = nn_printer_takestring(&subw);
                nn_printer_writestringl(pr, os->sbuf->data, os->sbuf->length);
                //nn_vm_stackpop(state);
                return;
            }
        }
    }
    #endif
    nn_printer_writefmt(pr, "<instance of %s at %p>", instance->klass->name->sbuf->data, (void*)instance);
}

void nn_printer_printobject(NeonPrinter* pr, NeonValue value, bool fixstring, bool invmethod)
{
    NeonObject* obj;
    obj = nn_value_asobject(value);
    switch(obj->type)
    {
        case NEON_OBJTYPE_SWITCH:
            {
                nn_printer_writestring(pr, "<switch>");
            }
            break;
        case NEON_OBJTYPE_USERDATA:
            {
                nn_printer_writefmt(pr, "<userdata %s>", nn_value_asuserdata(value)->name);
            }
            break;
        case NEON_OBJTYPE_RANGE:
            {
                NeonObjRange* range;
                range = nn_value_asrange(value);
                nn_printer_writefmt(pr, "<range %d .. %d>", range->lower, range->upper);
            }
            break;
        case NEON_OBJTYPE_FILE:
            {
                nn_printer_printfile(pr, nn_value_asfile(value));
            }
            break;
        case NEON_OBJTYPE_DICT:
            {
                nn_printer_printdict(pr, nn_value_asdict(value));
            }
            break;
        case NEON_OBJTYPE_ARRAY:
            {
                nn_printer_printarray(pr, nn_value_asarray(value));
            }
            break;
        case NEON_OBJTYPE_FUNCBOUND:
            {
                NeonObjFuncBound* bn;
                bn = nn_value_asfuncbound(value);
                nn_printer_printfunction(pr, bn->method->scriptfunc);
            }
            break;
        case NEON_OBJTYPE_MODULE:
            {
                NeonObjModule* mod;
                mod = nn_value_asmodule(value);
                nn_printer_writefmt(pr, "<module '%s' at '%s'>", mod->name->sbuf->data, mod->physicalpath->sbuf->data);
            }
            break;
        case NEON_OBJTYPE_CLASS:
            {
                NeonObjClass* klass;
                klass = nn_value_asclass(value);
                nn_printer_writefmt(pr, "<class %s at %p>", klass->name->sbuf->data, (void*)klass);
            }
            break;
        case NEON_OBJTYPE_FUNCCLOSURE:
            {
                NeonObjFuncClosure* cls;
                cls = nn_value_asfuncclosure(value);
                nn_printer_printfunction(pr, cls->scriptfunc);
            }
            break;
        case NEON_OBJTYPE_FUNCSCRIPT:
            {
                NeonObjFuncScript* fn;
                fn = nn_value_asfuncscript(value);
                nn_printer_printfunction(pr, fn);
            }
            break;
        case NEON_OBJTYPE_INSTANCE:
            {
                /* @TODO: support the toString() override */
                NeonObjInstance* instance;
                instance = nn_value_asinstance(value);
                nn_printer_printinstance(pr, instance, invmethod);
            }
            break;
        case NEON_OBJTYPE_FUNCNATIVE:
            {
                NeonObjFuncNative* native;
                native = nn_value_asfuncnative(value);
                nn_printer_writefmt(pr, "<function %s(native) at %p>", native->name, (void*)native);
            }
            break;
        case NEON_OBJTYPE_UPVALUE:
            {
                nn_printer_writefmt(pr, "<upvalue>");
            }
            break;
        case NEON_OBJTYPE_STRING:
            {
                NeonObjString* string;
                string = nn_value_asstring(value);
                if(fixstring)
                {
                    nn_printer_writequotedstring(pr, string->sbuf->data, string->sbuf->length, true);
                }
                else
                {
                    nn_printer_writestringl(pr, string->sbuf->data, string->sbuf->length);
                }
            }
            break;
    }
}

void nn_printer_printvalue(NeonPrinter* pr, NeonValue value, bool fixstring, bool invmethod)
{
    switch(value.type)
    {
        case NEON_VALTYPE_EMPTY:
            {
                nn_printer_writestring(pr, "<empty>");
            }
            break;
        case NEON_VALTYPE_NULL:
            {
                nn_printer_writestring(pr, "null");
            }
            break;
        case NEON_VALTYPE_BOOL:
            {
                nn_printer_writestring(pr, nn_value_asbool(value) ? "true" : "false");
            }
            break;
        case NEON_VALTYPE_NUMBER:
            {
                nn_printer_writefmt(pr, "%.16g", nn_value_asnumber(value));
            }
            break;
        case NEON_VALTYPE_OBJ:
            {
                nn_printer_printobject(pr, value, fixstring, invmethod);
            }
            break;
        default:
            break;
    }
}

NeonObjString* nn_value_tostring(NeonState* state, NeonValue value)
{
    NeonPrinter pr;
    NeonObjString* s;
    nn_printer_makestackstring(state, &pr);
    nn_printer_printvalue(&pr, value, false, true);
    s = nn_printer_takestring(&pr);
    return s;
}

const char* nn_value_objecttypename(NeonObject* object)
{
    switch(object->type)
    {
        case NEON_OBJTYPE_MODULE:
            return "module";
        case NEON_OBJTYPE_RANGE:
            return "range";
        case NEON_OBJTYPE_FILE:
            return "file";
        case NEON_OBJTYPE_DICT:
            return "dictionary";
        case NEON_OBJTYPE_ARRAY:
            return "array";
        case NEON_OBJTYPE_CLASS:
            return "class";
        case NEON_OBJTYPE_FUNCSCRIPT:
        case NEON_OBJTYPE_FUNCNATIVE:
        case NEON_OBJTYPE_FUNCCLOSURE:
        case NEON_OBJTYPE_FUNCBOUND:
            return "function";
        case NEON_OBJTYPE_INSTANCE:
            return ((NeonObjInstance*)object)->klass->name->sbuf->data;
        case NEON_OBJTYPE_STRING:
            return "string";
        case NEON_OBJTYPE_USERDATA:
            return "userdata";
        case NEON_OBJTYPE_SWITCH:
            return "switch";
        default:
            break;
    }
    return "unknown";
}

const char* nn_value_typename(NeonValue value)
{
    if(nn_value_isempty(value))
    {
        return "empty";
    }
    if(nn_value_isnull(value))
    {
        return "null";
    }
    else if(nn_value_isbool(value))
    {
        return "boolean";
    }
    else if(nn_value_isnumber(value))
    {
        return "number";
    }
    else if(nn_value_isobject(value))
    {
        return nn_value_objecttypename(nn_value_asobject(value));
    }
    return "unknown";
}

bool nn_value_compobject(NeonState* state, NeonValue a, NeonValue b)
{
    size_t i;
    NeonObjType ta;
    NeonObjType tb;
    NeonObject* oa;
    NeonObject* ob;
    NeonObjString* stra;
    NeonObjString* strb;
    NeonObjArray* arra;
    NeonObjArray* arrb;
    oa = nn_value_asobject(a);
    ob = nn_value_asobject(b);
    ta = oa->type;
    tb = ob->type;
    if(ta == tb)
    {
        if(ta == NEON_OBJTYPE_STRING)
        {
            stra = (NeonObjString*)oa;
            strb = (NeonObjString*)ob;
            if(stra->sbuf->length == strb->sbuf->length)
            {
                if(memcmp(stra->sbuf->data, strb->sbuf->data, stra->sbuf->length) == 0)
                {
                    return true;
                }
                return false;
            }
        }
        if(ta == NEON_OBJTYPE_ARRAY)
        {
            arra = (NeonObjArray*)oa;
            arrb = (NeonObjArray*)ob;
            if(arra->varray->count == arrb->varray->count)
            {
                for(i=0; i<(size_t)arra->varray->count; i++)
                {
                    if(!nn_value_compare(state, arra->varray->values[i], arrb->varray->values[i]))
                    {
                        return false;
                    }
                }
                return true;
            }
        }
    }
    return false;
}

bool nn_value_compare_actual(NeonState* state, NeonValue a, NeonValue b)
{
    if(a.type != b.type)
    {
        return false;
    }
    switch(a.type)
    {
        case NEON_VALTYPE_NULL:
        case NEON_VALTYPE_EMPTY:
            {
                return true;
            }
            break;
        case NEON_VALTYPE_BOOL:
            {
                return nn_value_asbool(a) == nn_value_asbool(b);
            }
            break;
        case NEON_VALTYPE_NUMBER:
            {
                return (nn_value_asnumber(a) == nn_value_asnumber(b));
            }
            break;
        case NEON_VALTYPE_OBJ:
            {
                if(nn_value_asobject(a) == nn_value_asobject(b))
                {
                    return true;
                }
                return nn_value_compobject(state, a, b);
            }
            break;
        default:
            break;
    }
    return false;
}


bool nn_value_compare(NeonState* state, NeonValue a, NeonValue b)
{
    bool r;
    r = nn_value_compare_actual(state, a, b);
    return r;
}

uint32_t nn_util_hashbits(uint64_t hash)
{
    /*
    // From v8's ComputeLongHash() which in turn cites:
    // Thomas Wang, Integer Hash Functions.
    // http://www.concentric.net/~Ttwang/tech/inthash.htm
    // hash = (hash << 18) - hash - 1;
    */
    hash = ~hash + (hash << 18);
    hash = hash ^ (hash >> 31);
    /* hash = (hash + (hash << 2)) + (hash << 4); */
    hash = hash * 21;
    hash = hash ^ (hash >> 11);
    hash = hash + (hash << 6);
    hash = hash ^ (hash >> 22);
    return (uint32_t)(hash & 0x3fffffff);
}

uint32_t nn_util_hashdouble(double value)
{
    NeonDoubleHashUnion bits;
    bits.num = value;
    return nn_util_hashbits(bits.bits);
}

uint32_t nn_util_hashstring(const char* key, int length)
{
    uint32_t hash;
    const char* be;
    hash = 2166136261u;
    be = key + length;
    while(key < be)
    {
        hash = (hash ^ *key++) * 16777619;
    }
    return hash;
    /* return siphash24(127, 255, key, length); */
}

uint32_t nn_object_hashobject(NeonObject* object)
{
    switch(object->type)
    {
        case NEON_OBJTYPE_CLASS:
            {
                /* Classes just use their name. */
                return ((NeonObjClass*)object)->name->hash;
            }
            break;
        case NEON_OBJTYPE_FUNCSCRIPT:
            {
                /*
                // Allow bare (non-closure) functions so that we can use a map to find
                // existing constants in a function's constant table. This is only used
                // internally. Since user code never sees a non-closure function, they
                // cannot use them as map keys.
                */
                NeonObjFuncScript* fn;
                fn = (NeonObjFuncScript*)object;
                return nn_util_hashdouble(fn->arity) ^ nn_util_hashdouble(fn->blob.count);
            }
            break;
        case NEON_OBJTYPE_STRING:
            {
                return ((NeonObjString*)object)->hash;
            }
            break;
        default:
            break;
    }
    return 0;
}

uint32_t nn_value_hashvalue(NeonValue value)
{
    switch(value.type)
    {
        case NEON_VALTYPE_BOOL:
            return nn_value_asbool(value) ? 3 : 5;
        case NEON_VALTYPE_NULL:
            return 7;
        case NEON_VALTYPE_NUMBER:
            return nn_util_hashdouble(nn_value_asnumber(value));
        case NEON_VALTYPE_OBJ:
            return nn_object_hashobject(nn_value_asobject(value));
        default:
            /* NEON_VALTYPE_EMPTY */
            break;
    }
    return 0;
}


/**
 * returns the greater of the two values.
 * this function encapsulates the object hierarchy
 */
NeonValue nn_value_findgreater(NeonValue a, NeonValue b)
{
    NeonObjString* osa;
    NeonObjString* osb;    
    if(nn_value_isnull(a))
    {
        return b;
    }
    else if(nn_value_isbool(a))
    {
        if(nn_value_isnull(b) || (nn_value_isbool(b) && nn_value_asbool(b) == false))
        {
            /* only null, false and false are lower than numbers */
            return a;
        }
        else
        {
            return b;
        }
    }
    else if(nn_value_isnumber(a))
    {
        if(nn_value_isnull(b) || nn_value_isbool(b))
        {
            return a;
        }
        else if(nn_value_isnumber(b))
        {
            if(nn_value_asnumber(a) >= nn_value_asnumber(b))
            {
                return a;
            }
            return b;
        }
        else
        {
            /* every other thing is greater than a number */
            return b;
        }
    }
    else if(nn_value_isobject(a))
    {
        if(nn_value_isstring(a) && nn_value_isstring(b))
        {
            osa = nn_value_asstring(a);
            osb = nn_value_asstring(b);
            if(strncmp(osa->sbuf->data, osb->sbuf->data, osa->sbuf->length) >= 0)
            {
                return a;
            }
            return b;
        }
        else if(nn_value_isfuncscript(a) && nn_value_isfuncscript(b))
        {
            if(nn_value_asfuncscript(a)->arity >= nn_value_asfuncscript(b)->arity)
            {
                return a;
            }
            return b;
        }
        else if(nn_value_isfuncclosure(a) && nn_value_isfuncclosure(b))
        {
            if(nn_value_asfuncclosure(a)->scriptfunc->arity >= nn_value_asfuncclosure(b)->scriptfunc->arity)
            {
                return a;
            }
            return b;
        }
        else if(nn_value_isrange(a) && nn_value_isrange(b))
        {
            if(nn_value_asrange(a)->lower >= nn_value_asrange(b)->lower)
            {
                return a;
            }
            return b;
        }
        else if(nn_value_isclass(a) && nn_value_isclass(b))
        {
            if(nn_value_asclass(a)->methods->count >= nn_value_asclass(b)->methods->count)
            {
                return a;
            }
            return b;
        }
        else if(nn_value_isarray(a) && nn_value_isarray(b))
        {
            if(nn_value_asarray(a)->varray->count >= nn_value_asarray(b)->varray->count)
            {
                return a;
            }
            return b;
        }
        else if(nn_value_isdict(a) && nn_value_isdict(b))
        {
            if(nn_value_asdict(a)->names->count >= nn_value_asdict(b)->names->count)
            {
                return a;
            }
            return b;
        }
        else if(nn_value_isfile(a) && nn_value_isfile(b))
        {
            if(strcmp(nn_value_asfile(a)->path->sbuf->data, nn_value_asfile(b)->path->sbuf->data) >= 0)
            {
                return a;
            }
            return b;
        }
        else if(nn_value_isobject(b))
        {
            if(nn_value_asobject(a)->type >= nn_value_asobject(b)->type)
            {
                return a;
            }
            return b;
        }
        else
        {
            return a;
        }
    }
    return a;
}

/**
 * sorts values in an array using the bubble-sort algorithm
 */
void nn_value_sortvalues(NeonState* state, NeonValue* values, int count)
{
    int i;
    int j;
    NeonValue temp;
    for(i = 0; i < count; i++)
    {
        for(j = 0; j < count; j++)
        {
            if(nn_value_compare(state, values[j], nn_value_findgreater(values[i], values[j])))
            {
                temp = values[i];
                values[i] = values[j];
                values[j] = temp;
                if(nn_value_isarray(values[i]))
                {
                    nn_value_sortvalues(state, nn_value_asarray(values[i])->varray->values, nn_value_asarray(values[i])->varray->count);
                }

                if(nn_value_isarray(values[j]))
                {
                    nn_value_sortvalues(state, nn_value_asarray(values[j])->varray->values, nn_value_asarray(values[j])->varray->count);
                }
            }
        }
    }
}

NeonValue nn_value_copyvalue(NeonState* state, NeonValue value)
{
    if(nn_value_isobject(value))
    {
        switch(nn_value_asobject(value)->type)
        {
            case NEON_OBJTYPE_STRING:
                {
                    NeonObjString* string;
                    string = nn_value_asstring(value);
                    return nn_value_fromobject(nn_string_copylen(state, string->sbuf->data, string->sbuf->length));
                }
                break;
            case NEON_OBJTYPE_ARRAY:
            {
                int i;
                NeonObjArray* list;
                NeonObjArray* newlist;
                list = nn_value_asarray(value);
                newlist = nn_object_makearray(state);
                nn_vm_stackpush(state, nn_value_fromobject(newlist));
                for(i = 0; i < list->varray->count; i++)
                {
                    nn_valarray_push(newlist->varray, list->varray->values[i]);
                }
                nn_vm_stackpop(state);
                return nn_value_fromobject(newlist);
            }
            /*
            case NEON_OBJTYPE_DICT:
                {
                    NeonObjDict *dict;
                    NeonObjDict *newdict;
                    dict = nn_value_asdict(value);
                    newdict = nn_object_makedict(state);
                    // @TODO: Figure out how to handle dictionary values correctly
                    // remember that copying keys is redundant and unnecessary
                }
                break;
            */
            default:
                break;
        }
    }
    return value;
}

NeonObject* nn_object_allocobject(NeonState* state, size_t size, NeonObjType type)
{
    NeonObject* object;
    object = (NeonObject*)nn_gcmem_allocate(state, size, 1);
    object->type = type;
    object->mark = !state->markvalue;
    object->stale = false;
    object->pvm = state;
    object->next = state->vmstate.linkedobjects;
    state->vmstate.linkedobjects = object;
    #if defined(DEBUG_GC) && DEBUG_GC
    nn_printer_writefmt(state->debugwriter, "%p allocate %ld for %d\n", (void*)object, size, type);
    #endif
    return object;
}

NeonObjUserdata* nn_object_makeuserdata(NeonState* state, void* pointer, const char* name)
{
    NeonObjUserdata* ptr;
    ptr = (NeonObjUserdata*)nn_object_allocobject(state, sizeof(NeonObjUserdata), NEON_OBJTYPE_USERDATA);
    ptr->pointer = pointer;
    ptr->name = nn_util_strdup(state, name);
    ptr->ondestroyfn = NULL;
    return ptr;
}

NeonObjModule* nn_module_make(NeonState* state, const char* name, const char* file, bool imported)
{
    NeonObjModule* module;
    module = (NeonObjModule*)nn_object_allocobject(state, sizeof(NeonObjModule), NEON_OBJTYPE_MODULE);
    module->deftable = nn_table_make(state);
    module->name = nn_string_copycstr(state, name);
    module->physicalpath = nn_string_copycstr(state, file);
    module->unloader = NULL;
    module->preloader = NULL;
    module->handle = NULL;
    module->imported = imported;
    return module;
}

void nn_module_destroy(NeonState* state, NeonObjModule* module)
{
    nn_table_destroy(module->deftable);
    /*
    nn_util_memfree(state, module->name);
    nn_util_memfree(state, module->physicalpath);
    */
    if(module->unloader != NULL && module->imported)
    {
        ((NeonModLoaderFN)module->unloader)(state);
    }
    if(module->handle != NULL)
    {
        nn_import_closemodule(module->handle);
    }
}

void nn_module_setfilefield(NeonState* state, NeonObjModule* module)
{
    return;
    nn_table_setcstr(module->deftable, "__file__", nn_value_fromobject(nn_string_copyobjstr(state, module->physicalpath)));
}

NeonObjSwitch* nn_object_makeswitch(NeonState* state)
{
    NeonObjSwitch* sw;
    sw = (NeonObjSwitch*)nn_object_allocobject(state, sizeof(NeonObjSwitch), NEON_OBJTYPE_SWITCH);
    sw->table = nn_table_make(state);
    sw->defaultjump = -1;
    sw->exitjump = -1;
    return sw;
}

NeonObjArray* nn_object_makearray(NeonState* state)
{
    return nn_array_make(state);
}

NeonObjRange* nn_object_makerange(NeonState* state, int lower, int upper)
{
    NeonObjRange* range;
    range = (NeonObjRange*)nn_object_allocobject(state, sizeof(NeonObjRange), NEON_OBJTYPE_RANGE);
    range->lower = lower;
    range->upper = upper;
    if(upper > lower)
    {
        range->range = upper - lower;
    }
    else
    {
        range->range = lower - upper;
    }
    return range;
}

NeonObjDict* nn_object_makedict(NeonState* state)
{
    NeonObjDict* dict;
    dict = (NeonObjDict*)nn_object_allocobject(state, sizeof(NeonObjDict), NEON_OBJTYPE_DICT);
    dict->names = nn_valarray_make(state);
    dict->htab = nn_table_make(state);
    return dict;
}

NeonObjFile* nn_object_makefile(NeonState* state, FILE* handle, bool isstd, const char* path, const char* mode)
{
    NeonObjFile* file;
    file = (NeonObjFile*)nn_object_allocobject(state, sizeof(NeonObjFile), NEON_OBJTYPE_FILE);
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

void nn_file_destroy(NeonState* state, NeonObjFile* file)
{
    nn_fileobject_close(file);
    nn_gcmem_release(state, file, sizeof(NeonObjFile));
}

void nn_file_mark(NeonState* state, NeonObjFile* file)
{
    nn_gcmem_markobject(state, (NeonObject*)file->mode);
    nn_gcmem_markobject(state, (NeonObject*)file->path);
}

NeonObjFuncBound* nn_object_makefuncbound(NeonState* state, NeonValue receiver, NeonObjFuncClosure* method)
{
    NeonObjFuncBound* bound;
    bound = (NeonObjFuncBound*)nn_object_allocobject(state, sizeof(NeonObjFuncBound), NEON_OBJTYPE_FUNCBOUND);
    bound->receiver = receiver;
    bound->method = method;
    return bound;
}

NeonObjClass* nn_object_makeclass(NeonState* state, NeonObjString* name)
{
    NeonObjClass* klass;
    klass = (NeonObjClass*)nn_object_allocobject(state, sizeof(NeonObjClass), NEON_OBJTYPE_CLASS);
    klass->name = name;
    klass->instprops = nn_table_make(state);
    klass->staticproperties = nn_table_make(state);
    klass->methods = nn_table_make(state);
    klass->staticmethods = nn_table_make(state);
    klass->constructor = nn_value_makeempty();
    klass->destructor = nn_value_makeempty();
    klass->superclass = NULL;
    return klass;
}

void nn_class_destroy(NeonState* state, NeonObjClass* klass)
{
    nn_table_destroy(klass->methods);
    nn_table_destroy(klass->staticmethods);
    nn_table_destroy(klass->instprops);
    nn_table_destroy(klass->staticproperties);
    /*
    // We are not freeing the initializer because it's a closure and will still be freed accordingly later.
    */
    memset(klass, 0, sizeof(NeonObjClass));
    nn_gcmem_release(state, klass, sizeof(NeonObjClass));   
}

void nn_class_inheritfrom(NeonObjClass* subclass, NeonObjClass* superclass)
{
    nn_table_addall(superclass->instprops, subclass->instprops);
    nn_table_addall(superclass->methods, subclass->methods);
    subclass->superclass = superclass;
}

void nn_class_defproperty(NeonObjClass* klass, const char* cstrname, NeonValue val)
{
    nn_table_setcstr(klass->instprops, cstrname, val);
}

void nn_class_defcallablefieldptr(NeonState* state, NeonObjClass* klass, const char* cstrname, NeonNativeFN function, void* uptr)
{
    NeonObjString* oname;
    NeonObjFuncNative* ofn;
    oname = nn_string_copycstr(state, cstrname);
    ofn = nn_object_makefuncnative(state, function, cstrname, uptr);
    nn_table_setwithtype(klass->instprops, nn_value_fromobject(oname), nn_value_fromobject(ofn), NEON_PROPTYPE_FUNCTION, true);
}

void nn_class_defcallablefield(NeonState* state, NeonObjClass* klass, const char* cstrname, NeonNativeFN function)
{
    return nn_class_defcallablefieldptr(state, klass, cstrname, function, NULL);
}

void nn_class_setstaticpropertycstr(NeonObjClass* klass, const char* cstrname, NeonValue val)
{
    nn_table_setcstr(klass->staticproperties, cstrname, val);
}

void nn_class_setstaticproperty(NeonObjClass* klass, NeonObjString* name, NeonValue val)
{
    nn_class_setstaticpropertycstr(klass, name->sbuf->data, val);
}

void nn_class_defnativeconstructorptr(NeonState* state, NeonObjClass* klass, NeonNativeFN function, void* uptr)
{
    const char* cname;
    NeonObjFuncNative* ofn;
    cname = "constructor";
    ofn = nn_object_makefuncnative(state, function, cname, uptr);
    klass->constructor = nn_value_fromobject(ofn);
}

void nn_class_defnativeconstructor(NeonState* state, NeonObjClass* klass, NeonNativeFN function)
{
    return nn_class_defnativeconstructorptr(state, klass, function, NULL);
}

void nn_class_defmethod(NeonState* state, NeonObjClass* klass, const char* name, NeonValue val)
{
    (void)state;
    nn_table_setcstr(klass->methods, name, val);
}

void nn_class_defnativemethodptr(NeonState* state, NeonObjClass* klass, const char* name, NeonNativeFN function, void* ptr)
{
    NeonObjFuncNative* ofn;
    ofn = nn_object_makefuncnative(state, function, name, ptr);
    nn_class_defmethod(state, klass, name, nn_value_fromobject(ofn));
}

void nn_class_defnativemethod(NeonState* state, NeonObjClass* klass, const char* name, NeonNativeFN function)
{
    return nn_class_defnativemethodptr(state, klass, name, function, NULL);
}

void nn_class_defstaticnativemethodptr(NeonState* state, NeonObjClass* klass, const char* name, NeonNativeFN function, void* uptr)
{
    NeonObjFuncNative* ofn;
    ofn = nn_object_makefuncnative(state, function, name, uptr);
    nn_table_setcstr(klass->staticmethods, name, nn_value_fromobject(ofn));
}

void nn_class_defstaticnativemethod(NeonState* state, NeonObjClass* klass, const char* name, NeonNativeFN function)
{
    return nn_class_defstaticnativemethodptr(state, klass, name, function, NULL);
}

NeonProperty* nn_class_getmethodfield(NeonObjClass* klass, NeonObjString* name)
{
    NeonProperty* field;
    field = nn_table_getfield(klass->methods, nn_value_fromobject(name));
    if(field != NULL)
    {
        return field;
    }
    if(klass->superclass != NULL)
    {
        return nn_class_getmethodfield(klass->superclass, name);
    }
    return NULL;
}

NeonProperty* nn_class_getpropertyfield(NeonObjClass* klass, NeonObjString* name)
{
    NeonProperty* field;
    field = nn_table_getfield(klass->instprops, nn_value_fromobject(name));
    return field;
}

NeonProperty* nn_class_getstaticproperty(NeonObjClass* klass, NeonObjString* name)
{
    return nn_table_getfieldbyostr(klass->staticproperties, name);
}

NeonProperty* nn_class_getstaticmethodfield(NeonObjClass* klass, NeonObjString* name)
{
    NeonProperty* field;
    field = nn_table_getfield(klass->staticmethods, nn_value_fromobject(name));
    return field;
}

NeonObjInstance* nn_object_makeinstance(NeonState* state, NeonObjClass* klass)
{
    NeonObjInstance* instance;
    instance = (NeonObjInstance*)nn_object_allocobject(state, sizeof(NeonObjInstance), NEON_OBJTYPE_INSTANCE);
    /* gc fix */
    nn_vm_stackpush(state, nn_value_fromobject(instance));
    instance->active = true;
    instance->klass = klass;
    instance->properties = nn_table_make(state);
    if(klass->instprops->count > 0)
    {
        nn_table_copy(klass->instprops, instance->properties);
    }
    /* gc fix */
    nn_vm_stackpop(state);
    return instance;
}

void nn_instance_mark(NeonState* state, NeonObjInstance* instance)
{
    if(instance->active == false)
    {
        nn_state_warn(state, "trying to mark inactive instance <%p>!", instance);
        return;
    }
    nn_gcmem_markobject(state, (NeonObject*)instance->klass);
    nn_table_mark(state, instance->properties);
}

void nn_instance_destroy(NeonState* state, NeonObjInstance* instance)
{
    if(!nn_value_isempty(instance->klass->destructor))
    {
        if(!nn_vm_callvaluewithobject(state, instance->klass->constructor, nn_value_fromobject(instance), 0))
        {
            
        }
    }
    nn_table_destroy(instance->properties);
    instance->properties = NULL;
    instance->active = false;
    nn_gcmem_release(state, instance, sizeof(NeonObjInstance));
}

void nn_instance_defproperty(NeonObjInstance* instance, const char *cstrname, NeonValue val)
{
    nn_table_setcstr(instance->properties, cstrname, val);
}

NeonObjFuncScript* nn_object_makefuncscript(NeonState* state, NeonObjModule* module, NeonFuncType type)
{
    NeonObjFuncScript* function;
    function = (NeonObjFuncScript*)nn_object_allocobject(state, sizeof(NeonObjFuncScript), NEON_OBJTYPE_FUNCSCRIPT);
    function->arity = 0;
    function->upvalcount = 0;
    function->isvariadic = false;
    function->name = NULL;
    function->type = type;
    function->module = module;
    nn_blob_init(state, &function->blob);
    return function;
}

void nn_funcscript_destroy(NeonState* state, NeonObjFuncScript* function)
{
    nn_blob_destroy(state, &function->blob);
}

NeonObjFuncNative* nn_object_makefuncnative(NeonState* state, NeonNativeFN function, const char* name, void* uptr)
{
    NeonObjFuncNative* native;
    native = (NeonObjFuncNative*)nn_object_allocobject(state, sizeof(NeonObjFuncNative), NEON_OBJTYPE_FUNCNATIVE);
    native->natfunc = function;
    native->name = name;
    native->type = NEON_FUNCTYPE_FUNCTION;
    native->userptr = uptr;
    return native;
}

NeonObjFuncClosure* nn_object_makefuncclosure(NeonState* state, NeonObjFuncScript* function)
{
    int i;
    NeonObjUpvalue** upvals;
    NeonObjFuncClosure* closure;
    upvals = (NeonObjUpvalue**)nn_gcmem_allocate(state, sizeof(NeonObjUpvalue*), function->upvalcount);
    for(i = 0; i < function->upvalcount; i++)
    {
        upvals[i] = NULL;
    }
    closure = (NeonObjFuncClosure*)nn_object_allocobject(state, sizeof(NeonObjFuncClosure), NEON_OBJTYPE_FUNCCLOSURE);
    closure->scriptfunc = function;
    closure->upvalues = upvals;
    closure->upvalcount = function->upvalcount;
    return closure;
}

NeonObjString* nn_string_makefromstrbuf(NeonState* state, StringBuffer* sbuf, uint32_t hash)
{
    NeonObjString* rs;
    rs = (NeonObjString*)nn_object_allocobject(state, sizeof(NeonObjString), NEON_OBJTYPE_STRING);
    rs->sbuf = sbuf;
    rs->hash = hash;
    nn_vm_stackpush(state, nn_value_fromobject(rs));
    nn_table_set(state->strings, nn_value_fromobject(rs), nn_value_makenull());
    nn_vm_stackpop(state);
    return rs;
}

NeonObjString* nn_string_allocstring(NeonState* state, const char* estr, size_t elen, uint32_t hash, bool istaking)
{
    StringBuffer* sbuf;
    (void)istaking;
    sbuf = dyn_strbuf_makeempty(elen);
    dyn_strbuf_appendstrn(sbuf, estr, elen);
    return nn_string_makefromstrbuf(state, sbuf, hash);
}

/*
NeonObjString* nn_string_borrow(NeonState* state, const char* estr, size_t elen)
{
    uint32_t hash;
    StringBuffer* sbuf;
    hash = nn_util_hashstring(estr, length);
    sbuf = dyn_strbuf_makeborrowed(estr, elen);
    return nn_string_makefromstrbuf(state, sbuf, hash);
}
*/

size_t nn_string_getlength(NeonObjString* os)
{
    return os->sbuf->length;
}

const char* nn_string_getdata(NeonObjString* os)
{
    return os->sbuf->data;
}

const char* nn_string_getcstr(NeonObjString* os)
{
    return nn_string_getdata(os);
}

void nn_string_destroy(NeonState* state, NeonObjString* str)
{
    dyn_strbuf_destroy(str->sbuf);
    nn_gcmem_release(state, str, sizeof(NeonObjString));
}

NeonObjString* nn_string_takelen(NeonState* state, char* chars, int length)
{
    uint32_t hash;
    NeonObjString* rs;
    hash = nn_util_hashstring(chars, length);
    rs = nn_table_findstring(state->strings, chars, length, hash);
    if(rs == NULL)
    {
        rs = nn_string_allocstring(state, chars, length, hash, true);
    }
    nn_gcmem_freearray(state, sizeof(char), chars, (size_t)length + 1);
    return rs;
}

NeonObjString* nn_string_copylen(NeonState* state, const char* chars, int length)
{
    uint32_t hash;
    NeonObjString* rs;
    hash = nn_util_hashstring(chars, length);
    rs = nn_table_findstring(state->strings, chars, length, hash);
    if(rs != NULL)
    {
        return rs;
    }
    rs = nn_string_allocstring(state, chars, length, hash, false);
    return rs;
}

NeonObjString* nn_string_takecstr(NeonState* state, char* chars)
{
    return nn_string_takelen(state, chars, strlen(chars));
}

NeonObjString* nn_string_copycstr(NeonState* state, const char* chars)
{
    return nn_string_copylen(state, chars, strlen(chars));
}

NeonObjString* nn_string_copyobjstr(NeonState* state, NeonObjString* os)
{
    return nn_string_copylen(state, os->sbuf->data, os->sbuf->length);
}

NeonObjUpvalue* nn_object_makeupvalue(NeonState* state, NeonValue* slot, int stackpos)
{
    NeonObjUpvalue* upvalue;
    upvalue = (NeonObjUpvalue*)nn_object_allocobject(state, sizeof(NeonObjUpvalue), NEON_OBJTYPE_UPVALUE);
    upvalue->closed = nn_value_makenull();
    upvalue->location = *slot;
    upvalue->next = NULL;
    upvalue->stackpos = stackpos;
    return upvalue;
}

static const char* g_strthis = "this";
static const char* g_strsuper = "super";

NeonAstLexer* nn_astlex_init(NeonState* state, const char* source)
{
    NeonAstLexer* lex;
    NEON_ASTDEBUG(state, "");
    lex = (NeonAstLexer*)nn_gcmem_allocate(state, sizeof(NeonAstLexer), 1);
    lex->pvm = state;
    lex->sourceptr = source;
    lex->start = source;
    lex->line = 1;
    lex->tplstringcount = -1;
    return lex;
}

void nn_astlex_destroy(NeonState* state, NeonAstLexer* lex)
{
    NEON_ASTDEBUG(state, "");
    nn_gcmem_release(state, lex, sizeof(NeonAstLexer));
}

bool nn_astlex_isatend(NeonAstLexer* lex)
{
    return *lex->sourceptr == '\0';
}

NeonAstToken nn_astlex_maketoken(NeonAstLexer* lex, NeonAstTokType type)
{
    NeonAstToken t;
    t.isglobal = false;
    t.type = type;
    t.start = lex->start;
    t.length = (int)(lex->sourceptr - lex->start);
    t.line = lex->line;
    return t;
}

NeonAstToken nn_astlex_errortoken(NeonAstLexer* lex, const char* fmt, ...)
{
    int length;
    char* buf;
    va_list va;
    NeonAstToken t;
    va_start(va, fmt);
    buf = (char*)nn_gcmem_allocate(lex->pvm, sizeof(char), 1024);
    /* TODO: used to be vasprintf. need to check how much to actually allocate! */
    length = vsprintf(buf, fmt, va);
    va_end(va);
    t.type = NEON_ASTTOK_ERROR;
    t.start = buf;
    t.isglobal = false;
    if(buf != NULL)
    {
        t.length = length;
    }
    else
    {
        t.length = 0;
    }
    t.line = lex->line;
    return t;
}

bool nn_astutil_isdigit(char c)
{
    return c >= '0' && c <= '9';
}

bool nn_astutil_isbinary(char c)
{
    return c == '0' || c == '1';
}

bool nn_astutil_isalpha(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

bool nn_astutil_isoctal(char c)
{
    return c >= '0' && c <= '7';
}

bool nn_astutil_ishexadecimal(char c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

const char* nn_astutil_toktype2str(int t)
{
    switch(t)
    {
        case NEON_ASTTOK_NEWLINE: return "NEON_ASTTOK_NEWLINE";
        case NEON_ASTTOK_PARENOPEN: return "NEON_ASTTOK_PARENOPEN";
        case NEON_ASTTOK_PARENCLOSE: return "NEON_ASTTOK_PARENCLOSE";
        case NEON_ASTTOK_BRACKETOPEN: return "NEON_ASTTOK_BRACKETOPEN";
        case NEON_ASTTOK_BRACKETCLOSE: return "NEON_ASTTOK_BRACKETCLOSE";
        case NEON_ASTTOK_BRACEOPEN: return "NEON_ASTTOK_BRACEOPEN";
        case NEON_ASTTOK_BRACECLOSE: return "NEON_ASTTOK_BRACECLOSE";
        case NEON_ASTTOK_SEMICOLON: return "NEON_ASTTOK_SEMICOLON";
        case NEON_ASTTOK_COMMA: return "NEON_ASTTOK_COMMA";
        case NEON_ASTTOK_BACKSLASH: return "NEON_ASTTOK_BACKSLASH";
        case NEON_ASTTOK_EXCLMARK: return "NEON_ASTTOK_EXCLMARK";
        case NEON_ASTTOK_NOTEQUAL: return "NEON_ASTTOK_NOTEQUAL";
        case NEON_ASTTOK_COLON: return "NEON_ASTTOK_COLON";
        case NEON_ASTTOK_AT: return "NEON_ASTTOK_AT";
        case NEON_ASTTOK_DOT: return "NEON_ASTTOK_DOT";
        case NEON_ASTTOK_DOUBLEDOT: return "NEON_ASTTOK_DOUBLEDOT";
        case NEON_ASTTOK_TRIPLEDOT: return "NEON_ASTTOK_TRIPLEDOT";
        case NEON_ASTTOK_PLUS: return "NEON_ASTTOK_PLUS";
        case NEON_ASTTOK_PLUSASSIGN: return "NEON_ASTTOK_PLUSASSIGN";
        case NEON_ASTTOK_INCREMENT: return "NEON_ASTTOK_INCREMENT";
        case NEON_ASTTOK_MINUS: return "NEON_ASTTOK_MINUS";
        case NEON_ASTTOK_MINUSASSIGN: return "NEON_ASTTOK_MINUSASSIGN";
        case NEON_ASTTOK_DECREMENT: return "NEON_ASTTOK_DECREMENT";
        case NEON_ASTTOK_MULTIPLY: return "NEON_ASTTOK_MULTIPLY";
        case NEON_ASTTOK_MULTASSIGN: return "NEON_ASTTOK_MULTASSIGN";
        case NEON_ASTTOK_POWEROF: return "NEON_ASTTOK_POWEROF";
        case NEON_ASTTOK_POWASSIGN: return "NEON_ASTTOK_POWASSIGN";
        case NEON_ASTTOK_DIVIDE: return "NEON_ASTTOK_DIVIDE";
        case NEON_ASTTOK_DIVASSIGN: return "NEON_ASTTOK_DIVASSIGN";
        case NEON_ASTTOK_FLOOR: return "NEON_ASTTOK_FLOOR";
        case NEON_ASTTOK_ASSIGN: return "NEON_ASTTOK_ASSIGN";
        case NEON_ASTTOK_EQUAL: return "NEON_ASTTOK_EQUAL";
        case NEON_ASTTOK_LESSTHAN: return "NEON_ASTTOK_LESSTHAN";
        case NEON_ASTTOK_LESSEQUAL: return "NEON_ASTTOK_LESSEQUAL";
        case NEON_ASTTOK_LEFTSHIFT: return "NEON_ASTTOK_LEFTSHIFT";
        case NEON_ASTTOK_LEFTSHIFTASSIGN: return "NEON_ASTTOK_LEFTSHIFTASSIGN";
        case NEON_ASTTOK_GREATERTHAN: return "NEON_ASTTOK_GREATERTHAN";
        case NEON_ASTTOK_GREATER_EQ: return "NEON_ASTTOK_GREATER_EQ";
        case NEON_ASTTOK_RIGHTSHIFT: return "NEON_ASTTOK_RIGHTSHIFT";
        case NEON_ASTTOK_RIGHTSHIFTASSIGN: return "NEON_ASTTOK_RIGHTSHIFTASSIGN";
        case NEON_ASTTOK_MODULO: return "NEON_ASTTOK_MODULO";
        case NEON_ASTTOK_PERCENT_EQ: return "NEON_ASTTOK_PERCENT_EQ";
        case NEON_ASTTOK_AMP: return "NEON_ASTTOK_AMP";
        case NEON_ASTTOK_AMP_EQ: return "NEON_ASTTOK_AMP_EQ";
        case NEON_ASTTOK_BAR: return "NEON_ASTTOK_BAR";
        case NEON_ASTTOK_BAR_EQ: return "NEON_ASTTOK_BAR_EQ";
        case NEON_ASTTOK_TILDE: return "NEON_ASTTOK_TILDE";
        case NEON_ASTTOK_TILDE_EQ: return "NEON_ASTTOK_TILDE_EQ";
        case NEON_ASTTOK_XOR: return "NEON_ASTTOK_XOR";
        case NEON_ASTTOK_XOR_EQ: return "NEON_ASTTOK_XOR_EQ";
        case NEON_ASTTOK_QUESTION: return "NEON_ASTTOK_QUESTION";
        case NEON_ASTTOK_KWAND: return "NEON_ASTTOK_KWAND";
        case NEON_ASTTOK_KWAS: return "NEON_ASTTOK_KWAS";
        case NEON_ASTTOK_KWASSERT: return "NEON_ASTTOK_KWASSERT";
        case NEON_ASTTOK_KWBREAK: return "NEON_ASTTOK_KWBREAK";
        case NEON_ASTTOK_KWCATCH: return "NEON_ASTTOK_KWCATCH";
        case NEON_ASTTOK_KWCLASS: return "NEON_ASTTOK_KWCLASS";
        case NEON_ASTTOK_KWCONTINUE: return "NEON_ASTTOK_KWCONTINUE";
        case NEON_ASTTOK_KWFUNCTION: return "NEON_ASTTOK_KWFUNCTION";
        case NEON_ASTTOK_KWDEFAULT: return "NEON_ASTTOK_KWDEFAULT";
        case NEON_ASTTOK_KWTHROW: return "NEON_ASTTOK_KWTHROW";
        case NEON_ASTTOK_KWDO: return "NEON_ASTTOK_KWDO";
        case NEON_ASTTOK_KWECHO: return "NEON_ASTTOK_KWECHO";
        case NEON_ASTTOK_KWELSE: return "NEON_ASTTOK_KWELSE";
        case NEON_ASTTOK_KWFALSE: return "NEON_ASTTOK_KWFALSE";
        case NEON_ASTTOK_KWFINALLY: return "NEON_ASTTOK_KWFINALLY";
        case NEON_ASTTOK_KWFOREACH: return "NEON_ASTTOK_KWFOREACH";
        case NEON_ASTTOK_KWIF: return "NEON_ASTTOK_KWIF";
        case NEON_ASTTOK_KWIMPORT: return "NEON_ASTTOK_KWIMPORT";
        case NEON_ASTTOK_KWIN: return "NEON_ASTTOK_KWIN";
        case NEON_ASTTOK_KWFOR: return "NEON_ASTTOK_KWFOR";
        case NEON_ASTTOK_KWNULL: return "NEON_ASTTOK_KWNULL";
        case NEON_ASTTOK_KWNEW: return "NEON_ASTTOK_KWNEW";
        case NEON_ASTTOK_KWOR: return "NEON_ASTTOK_KWOR";
        case NEON_ASTTOK_KWSUPER: return "NEON_ASTTOK_KWSUPER";
        case NEON_ASTTOK_KWRETURN: return "NEON_ASTTOK_KWRETURN";
        case NEON_ASTTOK_KWTHIS: return "NEON_ASTTOK_KWTHIS";
        case NEON_ASTTOK_KWSTATIC: return "NEON_ASTTOK_KWSTATIC";
        case NEON_ASTTOK_KWTRUE: return "NEON_ASTTOK_KWTRUE";
        case NEON_ASTTOK_KWTRY: return "NEON_ASTTOK_KWTRY";
        case NEON_ASTTOK_KWSWITCH: return "NEON_ASTTOK_KWSWITCH";
        case NEON_ASTTOK_KWVAR: return "NEON_ASTTOK_KWVAR";
        case NEON_ASTTOK_KWCASE: return "NEON_ASTTOK_KWCASE";
        case NEON_ASTTOK_KWWHILE: return "NEON_ASTTOK_KWWHILE";
        case NEON_ASTTOK_LITERAL: return "NEON_ASTTOK_LITERAL";
        case NEON_ASTTOK_LITNUMREG: return "NEON_ASTTOK_LITNUMREG";
        case NEON_ASTTOK_LITNUMBIN: return "NEON_ASTTOK_LITNUMBIN";
        case NEON_ASTTOK_LITNUMOCT: return "NEON_ASTTOK_LITNUMOCT";
        case NEON_ASTTOK_LITNUMHEX: return "NEON_ASTTOK_LITNUMHEX";
        case NEON_ASTTOK_IDENTNORMAL: return "NEON_ASTTOK_IDENTNORMAL";
        case NEON_ASTTOK_DECORATOR: return "NEON_ASTTOK_DECORATOR";
        case NEON_ASTTOK_INTERPOLATION: return "NEON_ASTTOK_INTERPOLATION";
        case NEON_ASTTOK_EOF: return "NEON_ASTTOK_EOF";
        case NEON_ASTTOK_ERROR: return "NEON_ASTTOK_ERROR";
        case NEON_ASTTOK_KWEMPTY: return "NEON_ASTTOK_KWEMPTY";
        case NEON_ASTTOK_UNDEFINED: return "NEON_ASTTOK_UNDEFINED";
        case NEON_ASTTOK_TOKCOUNT: return "NEON_ASTTOK_TOKCOUNT";
    }
    return "?invalid?";
}

char nn_astlex_advance(NeonAstLexer* lex)
{
    lex->sourceptr++;
    if(lex->sourceptr[-1] == '\n')
    {
        lex->line++;
    }
    return lex->sourceptr[-1];
}

bool nn_astlex_match(NeonAstLexer* lex, char expected)
{
    if(nn_astlex_isatend(lex))
    {
        return false;
    }
    if(*lex->sourceptr != expected)
    {
        return false;
    }
    lex->sourceptr++;
    if(lex->sourceptr[-1] == '\n')
    {
        lex->line++;
    }
    return true;
}

char nn_astlex_peekcurr(NeonAstLexer* lex)
{
    return *lex->sourceptr;
}

char nn_astlex_peekprev(NeonAstLexer* lex)
{
    return lex->sourceptr[-1];
}

char nn_astlex_peeknext(NeonAstLexer* lex)
{
    if(nn_astlex_isatend(lex))
    {
        return '\0';
    }
    return lex->sourceptr[1];
}

NeonAstToken nn_astlex_skipblockcomments(NeonAstLexer* lex)
{
    int nesting;
    nesting = 1;
    while(nesting > 0)
    {
        if(nn_astlex_isatend(lex))
        {
            return nn_astlex_errortoken(lex, "unclosed block comment");
        }
        /* internal comment open */
        if(nn_astlex_peekcurr(lex) == '/' && nn_astlex_peeknext(lex) == '*')
        {
            nn_astlex_advance(lex);
            nn_astlex_advance(lex);
            nesting++;
            continue;
        }
        /* comment close */
        if(nn_astlex_peekcurr(lex) == '*' && nn_astlex_peeknext(lex) == '/')
        {
            nn_astlex_advance(lex);
            nn_astlex_advance(lex);
            nesting--;
            continue;
        }
        /* regular comment body */
        nn_astlex_advance(lex);
    }
    #if defined(NEON_PLAT_ISWINDOWS)
    //nn_astlex_advance(lex);
    #endif
    return nn_astlex_maketoken(lex, NEON_ASTTOK_UNDEFINED);
}

NeonAstToken nn_astlex_skipspace(NeonAstLexer* lex)
{
    char c;
    NeonAstToken result;
    result.isglobal = false;
    for(;;)
    {
        c = nn_astlex_peekcurr(lex);
        switch(c)
        {
            case ' ':
            case '\r':
            case '\t':
            {
                nn_astlex_advance(lex);
            }
            break;
            /*
            case '\n':
                {
                    lex->line++;
                    nn_astlex_advance(lex);
                }
                break;
            */
            /*
            case '#':
                // single line comment
                {
                    while(nn_astlex_peekcurr(lex) != '\n' && !nn_astlex_isatend(lex))
                        nn_astlex_advance(lex);

                }
                break;
            */
            case '/':
            {
                if(nn_astlex_peeknext(lex) == '/')
                {
                    while(nn_astlex_peekcurr(lex) != '\n' && !nn_astlex_isatend(lex))
                    {
                        nn_astlex_advance(lex);
                    }
                    return nn_astlex_maketoken(lex, NEON_ASTTOK_UNDEFINED);
                }
                else if(nn_astlex_peeknext(lex) == '*')
                {
                    nn_astlex_advance(lex);
                    nn_astlex_advance(lex);
                    result = nn_astlex_skipblockcomments(lex);
                    if(result.type != NEON_ASTTOK_UNDEFINED)
                    {
                        return result;
                    }
                    break;
                }
                else
                {
                    return nn_astlex_maketoken(lex, NEON_ASTTOK_UNDEFINED);
                }
            }
            break;
            /* exit as soon as we see a non-whitespace... */
            default:
                goto finished;
                break;
        }
    }
    finished:
    return nn_astlex_maketoken(lex, NEON_ASTTOK_UNDEFINED);
}

NeonAstToken nn_astlex_scanstring(NeonAstLexer* lex, char quote, bool withtemplate)
{
    NeonAstToken tkn;
    NEON_ASTDEBUG(lex->pvm, "quote=[%c] withtemplate=%d", quote, withtemplate);
    while(nn_astlex_peekcurr(lex) != quote && !nn_astlex_isatend(lex))
    {
        if(withtemplate)
        {
            /* interpolation started */
            if(nn_astlex_peekcurr(lex) == '$' && nn_astlex_peeknext(lex) == '{' && nn_astlex_peekprev(lex) != '\\')
            {
                if(lex->tplstringcount - 1 < NEON_CFG_ASTMAXSTRTPLDEPTH)
                {
                    lex->tplstringcount++;
                    lex->tplstringbuffer[lex->tplstringcount] = (int)quote;
                    lex->sourceptr++;
                    tkn = nn_astlex_maketoken(lex, NEON_ASTTOK_INTERPOLATION);
                    lex->sourceptr++;
                    return tkn;
                }
                return nn_astlex_errortoken(lex, "maximum interpolation nesting of %d exceeded by %d", NEON_CFG_ASTMAXSTRTPLDEPTH,
                    NEON_CFG_ASTMAXSTRTPLDEPTH - lex->tplstringcount + 1);
            }
        }
        if(nn_astlex_peekcurr(lex) == '\\' && (nn_astlex_peeknext(lex) == quote || nn_astlex_peeknext(lex) == '\\'))
        {
            nn_astlex_advance(lex);
        }
        nn_astlex_advance(lex);
    }
    if(nn_astlex_isatend(lex))
    {
        return nn_astlex_errortoken(lex, "unterminated string (opening quote not matched)");
    }
    /* the closing quote */
    nn_astlex_match(lex, quote);
    return nn_astlex_maketoken(lex, NEON_ASTTOK_LITERAL);
}

NeonAstToken nn_astlex_scannumber(NeonAstLexer* lex)
{
    NEON_ASTDEBUG(lex->pvm, "");
    /* handle binary, octal and hexadecimals */
    if(nn_astlex_peekprev(lex) == '0')
    {
        /* binary number */
        if(nn_astlex_match(lex, 'b'))
        {
            while(nn_astutil_isbinary(nn_astlex_peekcurr(lex)))
            {
                nn_astlex_advance(lex);
            }
            return nn_astlex_maketoken(lex, NEON_ASTTOK_LITNUMBIN);
        }
        else if(nn_astlex_match(lex, 'c'))
        {
            while(nn_astutil_isoctal(nn_astlex_peekcurr(lex)))
            {
                nn_astlex_advance(lex);
            }
            return nn_astlex_maketoken(lex, NEON_ASTTOK_LITNUMOCT);
        }
        else if(nn_astlex_match(lex, 'x'))
        {
            while(nn_astutil_ishexadecimal(nn_astlex_peekcurr(lex)))
            {
                nn_astlex_advance(lex);
            }
            return nn_astlex_maketoken(lex, NEON_ASTTOK_LITNUMHEX);
        }
    }
    while(nn_astutil_isdigit(nn_astlex_peekcurr(lex)))
    {
        nn_astlex_advance(lex);
    }
    /* dots(.) are only valid here when followed by a digit */
    if(nn_astlex_peekcurr(lex) == '.' && nn_astutil_isdigit(nn_astlex_peeknext(lex)))
    {
        nn_astlex_advance(lex);
        while(nn_astutil_isdigit(nn_astlex_peekcurr(lex)))
        {
            nn_astlex_advance(lex);
        }
        /*
        // E or e are only valid here when followed by a digit and occurring after a dot
        */
        if((nn_astlex_peekcurr(lex) == 'e' || nn_astlex_peekcurr(lex) == 'E') && (nn_astlex_peeknext(lex) == '+' || nn_astlex_peeknext(lex) == '-'))
        {
            nn_astlex_advance(lex);
            nn_astlex_advance(lex);
            while(nn_astutil_isdigit(nn_astlex_peekcurr(lex)))
            {
                nn_astlex_advance(lex);
            }
        }
    }
    return nn_astlex_maketoken(lex, NEON_ASTTOK_LITNUMREG);
}

NeonAstTokType nn_astlex_getidenttype(NeonAstLexer* lex)
{
    static const struct
    {
        const char* str;
        int tokid;
    }
    keywords[] =
    {
        { "and", NEON_ASTTOK_KWAND },
        { "assert", NEON_ASTTOK_KWASSERT },
        { "as", NEON_ASTTOK_KWAS },
        { "break", NEON_ASTTOK_KWBREAK },
        { "catch", NEON_ASTTOK_KWCATCH },
        { "class", NEON_ASTTOK_KWCLASS },
        { "continue", NEON_ASTTOK_KWCONTINUE },
        { "default", NEON_ASTTOK_KWDEFAULT },
        { "def", NEON_ASTTOK_KWFUNCTION },
        { "function", NEON_ASTTOK_KWFUNCTION },
        { "throw", NEON_ASTTOK_KWTHROW },
        { "do", NEON_ASTTOK_KWDO },
        { "echo", NEON_ASTTOK_KWECHO },
        { "else", NEON_ASTTOK_KWELSE },
        { "empty", NEON_ASTTOK_KWEMPTY },
        { "false", NEON_ASTTOK_KWFALSE },
        { "finally", NEON_ASTTOK_KWFINALLY },
        { "foreach", NEON_ASTTOK_KWFOREACH },
        { "if", NEON_ASTTOK_KWIF },
        { "import", NEON_ASTTOK_KWIMPORT },
        { "in", NEON_ASTTOK_KWIN },
        { "for", NEON_ASTTOK_KWFOR },
        { "null", NEON_ASTTOK_KWNULL },
        { "new", NEON_ASTTOK_KWNEW },
        { "or", NEON_ASTTOK_KWOR },
        { "super", NEON_ASTTOK_KWSUPER },
        { "return", NEON_ASTTOK_KWRETURN },
        { "this", NEON_ASTTOK_KWTHIS },
        { "static", NEON_ASTTOK_KWSTATIC },
        { "true", NEON_ASTTOK_KWTRUE },
        { "try", NEON_ASTTOK_KWTRY },
        { "typeof", NEON_ASTTOK_KWTYPEOF },
        { "switch", NEON_ASTTOK_KWSWITCH },
        { "case", NEON_ASTTOK_KWCASE },
        { "var", NEON_ASTTOK_KWVAR },
        { "while", NEON_ASTTOK_KWWHILE },
        { NULL, (NeonAstTokType)0 }
    };
    size_t i;
    size_t kwlen;
    size_t ofs;
    const char* kwtext;
    for(i = 0; keywords[i].str != NULL; i++)
    {
        kwtext = keywords[i].str;
        kwlen = strlen(kwtext);
        ofs = (lex->sourceptr - lex->start);
        if(ofs == kwlen)
        {
            if(memcmp(lex->start, kwtext, kwlen) == 0)
            {
                return (NeonAstTokType)keywords[i].tokid;
            }
        }
    }
    return NEON_ASTTOK_IDENTNORMAL;
}

NeonAstToken nn_astlex_scanident(NeonAstLexer* lex, bool isdollar)
{
    int cur;
    NeonAstToken tok;
    cur = nn_astlex_peekcurr(lex);
    if(cur == '$')
    {
        nn_astlex_advance(lex);
    }
    while(true)
    {
        cur = nn_astlex_peekcurr(lex);
        if(nn_astutil_isalpha(cur) || nn_astutil_isdigit(cur))
        {
            nn_astlex_advance(lex);
        }
        else
        {
            break;
        }
    }
    tok = nn_astlex_maketoken(lex, nn_astlex_getidenttype(lex));
    tok.isglobal = isdollar;
    return tok;
}

NeonAstToken nn_astlex_scandecorator(NeonAstLexer* lex)
{
    while(nn_astutil_isalpha(nn_astlex_peekcurr(lex)) || nn_astutil_isdigit(nn_astlex_peekcurr(lex)))
    {
        nn_astlex_advance(lex);
    }
    return nn_astlex_maketoken(lex, NEON_ASTTOK_DECORATOR);
}

NeonAstToken nn_astlex_scantoken(NeonAstLexer* lex)
{
    char c;
    bool isdollar;
    NeonAstToken tk;
    NeonAstToken token;
    tk = nn_astlex_skipspace(lex);
    if(tk.type != NEON_ASTTOK_UNDEFINED)
    {
        return tk;
    }
    lex->start = lex->sourceptr;
    if(nn_astlex_isatend(lex))
    {
        return nn_astlex_maketoken(lex, NEON_ASTTOK_EOF);
    }
    c = nn_astlex_advance(lex);
    if(nn_astutil_isdigit(c))
    {
        return nn_astlex_scannumber(lex);
    }
    else if(nn_astutil_isalpha(c) || (c == '$'))
    {
        isdollar = (c == '$');
        return nn_astlex_scanident(lex, isdollar);
    }
    switch(c)
    {
        case '(':
            {
                return nn_astlex_maketoken(lex, NEON_ASTTOK_PARENOPEN);
            }
            break;
        case ')':
            {
                return nn_astlex_maketoken(lex, NEON_ASTTOK_PARENCLOSE);
            }
            break;
        case '[':
            {
                return nn_astlex_maketoken(lex, NEON_ASTTOK_BRACKETOPEN);
            }
            break;
        case ']':
            {
                return nn_astlex_maketoken(lex, NEON_ASTTOK_BRACKETCLOSE);
            }
            break;
        case '{':
            {
                return nn_astlex_maketoken(lex, NEON_ASTTOK_BRACEOPEN);
            }
            break;
        case '}':
            {
                if(lex->tplstringcount > -1)
                {
                    token = nn_astlex_scanstring(lex, (char)lex->tplstringbuffer[lex->tplstringcount], true);
                    lex->tplstringcount--;
                    return token;
                }
                return nn_astlex_maketoken(lex, NEON_ASTTOK_BRACECLOSE);
            }
            break;
        case ';':
            {
                return nn_astlex_maketoken(lex, NEON_ASTTOK_SEMICOLON);
            }
            break;
        case '\\':
            {
                return nn_astlex_maketoken(lex, NEON_ASTTOK_BACKSLASH);
            }
            break;
        case ':':
            {
                return nn_astlex_maketoken(lex, NEON_ASTTOK_COLON);
            }
            break;
        case ',':
            {
                return nn_astlex_maketoken(lex, NEON_ASTTOK_COMMA);
            }
            break;
        case '@':
            {
                if(!nn_astutil_isalpha(nn_astlex_peekcurr(lex)))
                {
                    return nn_astlex_maketoken(lex, NEON_ASTTOK_AT);
                }
                return nn_astlex_scandecorator(lex);
            }
            break;
        case '!':
            {
                if(nn_astlex_match(lex, '='))
                {
                    return nn_astlex_maketoken(lex, NEON_ASTTOK_NOTEQUAL);
                }
                return nn_astlex_maketoken(lex, NEON_ASTTOK_EXCLMARK);

            }
            break;
        case '.':
            {
                if(nn_astlex_match(lex, '.'))
                {
                    if(nn_astlex_match(lex, '.'))
                    {
                        return nn_astlex_maketoken(lex, NEON_ASTTOK_TRIPLEDOT);
                    }
                    return nn_astlex_maketoken(lex, NEON_ASTTOK_DOUBLEDOT);
                }
                return nn_astlex_maketoken(lex, NEON_ASTTOK_DOT);
            }
            break;
        case '+':
        {
            if(nn_astlex_match(lex, '+'))
            {
                return nn_astlex_maketoken(lex, NEON_ASTTOK_INCREMENT);
            }
            if(nn_astlex_match(lex, '='))
            {
                return nn_astlex_maketoken(lex, NEON_ASTTOK_PLUSASSIGN);
            }
            else
            {
                return nn_astlex_maketoken(lex, NEON_ASTTOK_PLUS);
            }
        }
        break;
        case '-':
            {
                if(nn_astlex_match(lex, '-'))
                {
                    return nn_astlex_maketoken(lex, NEON_ASTTOK_DECREMENT);
                }
                if(nn_astlex_match(lex, '='))
                {
                    return nn_astlex_maketoken(lex, NEON_ASTTOK_MINUSASSIGN);
                }
                else
                {
                    return nn_astlex_maketoken(lex, NEON_ASTTOK_MINUS);
                }
            }
            break;
        case '*':
            {
                if(nn_astlex_match(lex, '*'))
                {
                    if(nn_astlex_match(lex, '='))
                    {
                        return nn_astlex_maketoken(lex, NEON_ASTTOK_POWASSIGN);
                    }
                    return nn_astlex_maketoken(lex, NEON_ASTTOK_POWEROF);
                }
                else
                {
                    if(nn_astlex_match(lex, '='))
                    {
                        return nn_astlex_maketoken(lex, NEON_ASTTOK_MULTASSIGN);
                    }
                    return nn_astlex_maketoken(lex, NEON_ASTTOK_MULTIPLY);
                }
            }
            break;
        case '/':
            {
                if(nn_astlex_match(lex, '='))
                {
                    return nn_astlex_maketoken(lex, NEON_ASTTOK_DIVASSIGN);
                }
                return nn_astlex_maketoken(lex, NEON_ASTTOK_DIVIDE);
            }
            break;
        case '=':
            {
                if(nn_astlex_match(lex, '='))
                {
                    return nn_astlex_maketoken(lex, NEON_ASTTOK_EQUAL);
                }
                return nn_astlex_maketoken(lex, NEON_ASTTOK_ASSIGN);
            }        
            break;
        case '<':
            {
                if(nn_astlex_match(lex, '<'))
                {
                    if(nn_astlex_match(lex, '='))
                    {
                        return nn_astlex_maketoken(lex, NEON_ASTTOK_LEFTSHIFTASSIGN);
                    }
                    return nn_astlex_maketoken(lex, NEON_ASTTOK_LEFTSHIFT);
                }
                else
                {
                    if(nn_astlex_match(lex, '='))
                    {
                        return nn_astlex_maketoken(lex, NEON_ASTTOK_LESSEQUAL);
                    }
                    return nn_astlex_maketoken(lex, NEON_ASTTOK_LESSTHAN);

                }
            }
            break;
        case '>':
            {
                if(nn_astlex_match(lex, '>'))
                {
                    if(nn_astlex_match(lex, '='))
                    {
                        return nn_astlex_maketoken(lex, NEON_ASTTOK_RIGHTSHIFTASSIGN);
                    }
                    return nn_astlex_maketoken(lex, NEON_ASTTOK_RIGHTSHIFT);
                }
                else
                {
                    if(nn_astlex_match(lex, '='))
                    {
                        return nn_astlex_maketoken(lex, NEON_ASTTOK_GREATER_EQ);
                    }
                    return nn_astlex_maketoken(lex, NEON_ASTTOK_GREATERTHAN);
                }
            }
            break;
        case '%':
            {
                if(nn_astlex_match(lex, '='))
                {
                    return nn_astlex_maketoken(lex, NEON_ASTTOK_PERCENT_EQ);
                }
                return nn_astlex_maketoken(lex, NEON_ASTTOK_MODULO);
            }
            break;
        case '&':
            {
                if(nn_astlex_match(lex, '&'))
                {
                    return nn_astlex_maketoken(lex, NEON_ASTTOK_KWAND);
                }
                else if(nn_astlex_match(lex, '='))
                {
                    return nn_astlex_maketoken(lex, NEON_ASTTOK_AMP_EQ);
                }
                return nn_astlex_maketoken(lex, NEON_ASTTOK_AMP);
            }
            break;
        case '|':
            {
                if(nn_astlex_match(lex, '|'))
                {
                    return nn_astlex_maketoken(lex, NEON_ASTTOK_KWOR);
                }
                else if(nn_astlex_match(lex, '='))
                {
                    return nn_astlex_maketoken(lex, NEON_ASTTOK_BAR_EQ);
                }
                return nn_astlex_maketoken(lex, NEON_ASTTOK_BAR);
            }
            break;
        case '~':
            {
                if(nn_astlex_match(lex, '='))
                {
                    return nn_astlex_maketoken(lex, NEON_ASTTOK_TILDE_EQ);
                }
                return nn_astlex_maketoken(lex, NEON_ASTTOK_TILDE);
            }
            break;
        case '^':
            {
                if(nn_astlex_match(lex, '='))
                {
                    return nn_astlex_maketoken(lex, NEON_ASTTOK_XOR_EQ);
                }
                return nn_astlex_maketoken(lex, NEON_ASTTOK_XOR);
            }
            break;
        case '\n':
            {
                return nn_astlex_maketoken(lex, NEON_ASTTOK_NEWLINE);
            }
            break;
        case '"':
            {
                return nn_astlex_scanstring(lex, '"', true);
            }
            break;
        case '\'':
            {
                return nn_astlex_scanstring(lex, '\'', false);
            }
            break;
        case '?':
            {
                return nn_astlex_maketoken(lex, NEON_ASTTOK_QUESTION);
            }
            break;
        /*
        // --- DO NOT MOVE ABOVE OR BELOW THE DEFAULT CASE ---
        // fall-through tokens goes here... this tokens are only valid
        // when the carry another token with them...
        // be careful not to add break after them so that they may use the default
        // case.
        */
        default:
            break;
    }
    return nn_astlex_errortoken(lex, "unexpected character %c", c);
}

NeonAstParser* nn_astparser_make(NeonState* state, NeonAstLexer* lexer, NeonObjModule* module, bool keeplast)
{
    NeonAstParser* parser;
    NEON_ASTDEBUG(state, "");
    parser = (NeonAstParser*)nn_gcmem_allocate(state, sizeof(NeonAstParser), 1);
    parser->pvm = state;
    parser->lexer = lexer;
    parser->currentfunccompiler = NULL;
    parser->haderror = false;
    parser->panicmode = false;
    parser->blockcount = 0;
    parser->replcanecho = false;
    parser->isreturning = false;
    parser->istrying = false;
    parser->compcontext = NEON_COMPCONTEXT_NONE;
    parser->innermostloopstart = -1;
    parser->innermostloopscopedepth = 0;
    parser->currentclasscompiler = NULL;
    parser->currentmodule = module;
    parser->keeplastvalue = keeplast;
    parser->lastwasstatement = false;
    parser->infunction = false;
    parser->currentfile = parser->currentmodule->physicalpath->sbuf->data;
    return parser;
}

void nn_astparser_destroy(NeonState* state, NeonAstParser* parser)
{
    nn_gcmem_release(state, parser, sizeof(NeonAstParser));
}

NeonBlob* nn_astparser_currentblob(NeonAstParser* prs)
{
    return &prs->currentfunccompiler->targetfunc->blob;
}

bool nn_astparser_raiseerroratv(NeonAstParser* prs, NeonAstToken* t, const char* message, va_list args)
{
    fflush(stdout);
    /*
    // do not cascade error
    // suppress error if already in panic mode
    */
    if(prs->panicmode)
    {
        return false;
    }
    prs->panicmode = true;
    fprintf(stderr, "SyntaxError");
    if(t->type == NEON_ASTTOK_EOF)
    {
        fprintf(stderr, " at end");
    }
    else if(t->type == NEON_ASTTOK_ERROR)
    {
        /* do nothing */
    }
    else
    {
        if(t->length == 1 && *t->start == '\n')
        {
            fprintf(stderr, " at newline");
        }
        else
        {
            fprintf(stderr, " at '%.*s'", t->length, t->start);
        }
    }
    fprintf(stderr, ": ");
    vfprintf(stderr, message, args);
    fputs("\n", stderr);
    fprintf(stderr, "  %s:%d\n", prs->currentmodule->physicalpath->sbuf->data, t->line);
    prs->haderror = true;
    return false;
}

bool nn_astparser_raiseerror(NeonAstParser* prs, const char* message, ...)
{
    va_list args;
    va_start(args, message);
    nn_astparser_raiseerroratv(prs, &prs->prevtoken, message, args);
    va_end(args);
    return false;
}

bool nn_astparser_raiseerroratcurrent(NeonAstParser* prs, const char* message, ...)
{
    va_list args;
    va_start(args, message);
    nn_astparser_raiseerroratv(prs, &prs->currtoken, message, args);
    va_end(args);
    return false;
}

void nn_astparser_advance(NeonAstParser* prs)
{
    prs->prevtoken = prs->currtoken;
    while(true)
    {
        prs->currtoken = nn_astlex_scantoken(prs->lexer);
        if(prs->currtoken.type != NEON_ASTTOK_ERROR)
        {
            break;
        }
        nn_astparser_raiseerroratcurrent(prs, prs->currtoken.start);
    }
}

bool nn_astparser_consume(NeonAstParser* prs, NeonAstTokType t, const char* message)
{
    if(prs->currtoken.type == t)
    {
        nn_astparser_advance(prs);
        return true;
    }
    return nn_astparser_raiseerroratcurrent(prs, message);
}

void nn_astparser_consumeor(NeonAstParser* prs, const char* message, const NeonAstTokType* ts, int count)
{
    int i;
    for(i = 0; i < count; i++)
    {
        if(prs->currtoken.type == ts[i])
        {
            nn_astparser_advance(prs);
            return;
        }
    }
    nn_astparser_raiseerroratcurrent(prs, message);
}

bool nn_astparser_checknumber(NeonAstParser* prs)
{
    NeonAstTokType t;
    t = prs->prevtoken.type;
    if(t == NEON_ASTTOK_LITNUMREG || t == NEON_ASTTOK_LITNUMOCT || t == NEON_ASTTOK_LITNUMBIN || t == NEON_ASTTOK_LITNUMHEX)
    {
        return true;
    }
    return false;
}

bool nn_astparser_check(NeonAstParser* prs, NeonAstTokType t)
{
    return prs->currtoken.type == t;
}

bool nn_astparser_match(NeonAstParser* prs, NeonAstTokType t)
{
    if(!nn_astparser_check(prs, t))
    {
        return false;
    }
    nn_astparser_advance(prs);
    return true;
}

void nn_astparser_runparser(NeonAstParser* parser)
{
    nn_astparser_advance(parser);
    nn_astparser_ignorewhitespace(parser);
    while(!nn_astparser_match(parser, NEON_ASTTOK_EOF))
    {
        nn_astparser_parsedeclaration(parser);
    }
}

void nn_astparser_parsedeclaration(NeonAstParser* prs)
{
    nn_astparser_ignorewhitespace(prs);
    if(nn_astparser_match(prs, NEON_ASTTOK_KWCLASS))
    {
        nn_astparser_parseclassdeclaration(prs);
    }
    else if(nn_astparser_match(prs, NEON_ASTTOK_KWFUNCTION))
    {
        nn_astparser_parsefuncdecl(prs);
    }
    else if(nn_astparser_match(prs, NEON_ASTTOK_KWVAR))
    {
        nn_astparser_parsevardecl(prs, false);
    }
    else if(nn_astparser_match(prs, NEON_ASTTOK_BRACEOPEN))
    {
        if(!nn_astparser_check(prs, NEON_ASTTOK_NEWLINE) && prs->currentfunccompiler->scopedepth == 0)
        {
            nn_astparser_parseexprstmt(prs, false, true);
        }
        else
        {
            nn_astparser_scopebegin(prs);
            nn_astparser_parseblock(prs);
            nn_astparser_scopeend(prs);
        }
    }
    else
    {
        nn_astparser_parsestmt(prs);
    }
    nn_astparser_ignorewhitespace(prs);
    if(prs->panicmode)
    {
        nn_astparser_synchronize(prs);
    }
    nn_astparser_ignorewhitespace(prs);
}

void nn_astparser_parsestmt(NeonAstParser* prs)
{
    prs->replcanecho = false;
    nn_astparser_ignorewhitespace(prs);
    if(nn_astparser_match(prs, NEON_ASTTOK_KWECHO))
    {
        nn_astparser_parseechostmt(prs);
    }
    else if(nn_astparser_match(prs, NEON_ASTTOK_KWIF))
    {
        nn_astparser_parseifstmt(prs);
    }
    else if(nn_astparser_match(prs, NEON_ASTTOK_KWDO))
    {
        nn_astparser_parsedo_whilestmt(prs);
    }
    else if(nn_astparser_match(prs, NEON_ASTTOK_KWWHILE))
    {
        nn_astparser_parsewhilestmt(prs);
    }
    else if(nn_astparser_match(prs, NEON_ASTTOK_KWFOR))
    {
        nn_astparser_parseforstmt(prs);
    }
    else if(nn_astparser_match(prs, NEON_ASTTOK_KWFOREACH))
    {
        nn_astparser_parseforeachstmt(prs);
    }
    else if(nn_astparser_match(prs, NEON_ASTTOK_KWSWITCH))
    {
        nn_astparser_parseswitchstmt(prs);
    }
    else if(nn_astparser_match(prs, NEON_ASTTOK_KWCONTINUE))
    {
        nn_astparser_parsecontinuestmt(prs);
    }
    else if(nn_astparser_match(prs, NEON_ASTTOK_KWBREAK))
    {
        nn_astparser_parsebreakstmt(prs);
    }
    else if(nn_astparser_match(prs, NEON_ASTTOK_KWRETURN))
    {
        nn_astparser_parsereturnstmt(prs);
    }
    else if(nn_astparser_match(prs, NEON_ASTTOK_KWASSERT))
    {
        nn_astparser_parseassertstmt(prs);
    }
    else if(nn_astparser_match(prs, NEON_ASTTOK_KWTHROW))
    {
        nn_astparser_parsethrowstmt(prs);
    }
    else if(nn_astparser_match(prs, NEON_ASTTOK_BRACEOPEN))
    {
        nn_astparser_scopebegin(prs);
        nn_astparser_parseblock(prs);
        nn_astparser_scopeend(prs);
    }
    else if(nn_astparser_match(prs, NEON_ASTTOK_KWTRY))
    {
        nn_astparser_parsetrystmt(prs);
    }
    else
    {
        nn_astparser_parseexprstmt(prs, false, false);
    }
    nn_astparser_ignorewhitespace(prs);
}

void nn_astparser_consumestmtend(NeonAstParser* prs)
{
    /* allow block last statement to omit statement end */
    if(prs->blockcount > 0 && nn_astparser_check(prs, NEON_ASTTOK_BRACECLOSE))
    {
        return;
    }
    if(nn_astparser_match(prs, NEON_ASTTOK_SEMICOLON))
    {
        while(nn_astparser_match(prs, NEON_ASTTOK_SEMICOLON) || nn_astparser_match(prs, NEON_ASTTOK_NEWLINE))
        {
        }
        return;
    }
    if(nn_astparser_match(prs, NEON_ASTTOK_EOF) || prs->prevtoken.type == NEON_ASTTOK_EOF)
    {
        return;
    }
    /* nn_astparser_consume(prs, NEON_ASTTOK_NEWLINE, "end of statement expected"); */
    while(nn_astparser_match(prs, NEON_ASTTOK_SEMICOLON) || nn_astparser_match(prs, NEON_ASTTOK_NEWLINE))
    {
    }
}

void nn_astparser_ignorewhitespace(NeonAstParser* prs)
{
    while(true)
    {
        if(nn_astparser_check(prs, NEON_ASTTOK_NEWLINE))
        {
            nn_astparser_advance(prs);
        }
        else
        {
            break;
        }
    }
}

int nn_astparser_getcodeargscount(const NeonInstruction* bytecode, const NeonValue* constants, int ip)
{
    int constant;
    NeonOpCode code;
    NeonObjFuncScript* fn;
    code = (NeonOpCode)bytecode[ip].code;
    switch(code)
    {
        case NEON_OP_EQUAL:
        case NEON_OP_PRIMGREATER:
        case NEON_OP_PRIMLESSTHAN:
        case NEON_OP_PUSHNULL:
        case NEON_OP_PUSHTRUE:
        case NEON_OP_PUSHFALSE:
        case NEON_OP_PRIMADD:
        case NEON_OP_PRIMSUBTRACT:
        case NEON_OP_PRIMMULTIPLY:
        case NEON_OP_PRIMDIVIDE:
        case NEON_OP_PRIMFLOORDIVIDE:
        case NEON_OP_PRIMMODULO:
        case NEON_OP_PRIMPOW:
        case NEON_OP_PRIMNEGATE:
        case NEON_OP_PRIMNOT:
        case NEON_OP_ECHO:
        case NEON_OP_TYPEOF:
        case NEON_OP_POPONE:
        case NEON_OP_UPVALUECLOSE:
        case NEON_OP_DUPONE:
        case NEON_OP_RETURN:
        case NEON_OP_CLASSINHERIT:
        case NEON_OP_CLASSGETSUPER:
        case NEON_OP_PRIMAND:
        case NEON_OP_PRIMOR:
        case NEON_OP_PRIMBITXOR:
        case NEON_OP_PRIMSHIFTLEFT:
        case NEON_OP_PRIMSHIFTRIGHT:
        case NEON_OP_PRIMBITNOT:
        case NEON_OP_PUSHONE:
        case NEON_OP_INDEXSET:
        case NEON_OP_ASSERT:
        case NEON_OP_EXTHROW:
        case NEON_OP_EXPOPTRY:
        case NEON_OP_MAKERANGE:
        case NEON_OP_STRINGIFY:
        case NEON_OP_PUSHEMPTY:
        case NEON_OP_EXPUBLISHTRY:
        case NEON_OP_CLASSGETTHIS:
            return 0;
        case NEON_OP_CALLFUNCTION:
        case NEON_OP_CLASSINVOKESUPERSELF:
        case NEON_OP_INDEXGET:
        case NEON_OP_INDEXGETRANGED:
            return 1;
        case NEON_OP_GLOBALDEFINE:
        case NEON_OP_GLOBALGET:
        case NEON_OP_GLOBALSET:
        case NEON_OP_LOCALGET:
        case NEON_OP_LOCALSET:
        case NEON_OP_FUNCARGSET:
        case NEON_OP_FUNCARGGET:
        case NEON_OP_UPVALUEGET:
        case NEON_OP_UPVALUESET:
        case NEON_OP_JUMPIFFALSE:
        case NEON_OP_JUMPNOW:
        case NEON_OP_BREAK_PL:
        case NEON_OP_LOOP:
        case NEON_OP_PUSHCONSTANT:
        case NEON_OP_POPN:
        case NEON_OP_MAKECLASS:
        case NEON_OP_PROPERTYGET:
        case NEON_OP_PROPERTYGETSELF:
        case NEON_OP_PROPERTYSET:
        case NEON_OP_MAKEARRAY:
        case NEON_OP_MAKEDICT:
        case NEON_OP_IMPORTIMPORT:
        case NEON_OP_SWITCH:
        case NEON_OP_MAKEMETHOD:
        //case NEON_OP_FUNCOPTARG:
            return 2;
        case NEON_OP_CALLMETHOD:
        case NEON_OP_CLASSINVOKETHIS:
        case NEON_OP_CLASSINVOKESUPER:
        case NEON_OP_CLASSPROPERTYDEFINE:
            return 3;
        case NEON_OP_EXTRY:
            return 6;
        case NEON_OP_MAKECLOSURE:
            {
                constant = (bytecode[ip + 1].code << 8) | bytecode[ip + 2].code;
                fn = nn_value_asfuncscript(constants[constant]);
                /* There is two byte for the constant, then three for each up value. */
                return 2 + (fn->upvalcount * 3);
            }
            break;
        default:
            break;
    }
    return 0;
}

void nn_astemit_emit(NeonAstParser* prs, uint8_t byte, int line, bool isop)
{
    NeonInstruction ins;
    ins.code = byte;
    ins.srcline = line;
    ins.isop = isop;
    nn_blob_push(prs->pvm, nn_astparser_currentblob(prs), ins);
}

void nn_astemit_patchat(NeonAstParser* prs, size_t idx, uint8_t byte)
{
    nn_astparser_currentblob(prs)->instrucs[idx].code = byte;
}

void nn_astemit_emitinstruc(NeonAstParser* prs, uint8_t byte)
{
    nn_astemit_emit(prs, byte, prs->prevtoken.line, true);
}

void nn_astemit_emit1byte(NeonAstParser* prs, uint8_t byte)
{
    nn_astemit_emit(prs, byte, prs->prevtoken.line, false);
}

void nn_astemit_emit1short(NeonAstParser* prs, uint16_t byte)
{
    nn_astemit_emit(prs, (byte >> 8) & 0xff, prs->prevtoken.line, false);
    nn_astemit_emit(prs, byte & 0xff, prs->prevtoken.line, false);
}

void nn_astemit_emit2byte(NeonAstParser* prs, uint8_t byte, uint8_t byte2)
{
    nn_astemit_emit(prs, byte, prs->prevtoken.line, false);
    nn_astemit_emit(prs, byte2, prs->prevtoken.line, false);
}

void nn_astemit_emitbyteandshort(NeonAstParser* prs, uint8_t byte, uint16_t byte2)
{
    nn_astemit_emit(prs, byte, prs->prevtoken.line, false);
    nn_astemit_emit(prs, (byte2 >> 8) & 0xff, prs->prevtoken.line, false);
    nn_astemit_emit(prs, byte2 & 0xff, prs->prevtoken.line, false);
}

void nn_astemit_emitloop(NeonAstParser* prs, int loopstart)
{
    int offset;
    nn_astemit_emitinstruc(prs, NEON_OP_LOOP);
    offset = nn_astparser_currentblob(prs)->count - loopstart + 2;
    if(offset > UINT16_MAX)
    {
        nn_astparser_raiseerror(prs, "loop body too large");
    }
    nn_astemit_emit1byte(prs, (offset >> 8) & 0xff);
    nn_astemit_emit1byte(prs, offset & 0xff);
}

void nn_astemit_emitreturn(NeonAstParser* prs)
{
    if(prs->istrying)
    {
        nn_astemit_emitinstruc(prs, NEON_OP_EXPOPTRY);
    }
    if(prs->currentfunccompiler->type == NEON_FUNCTYPE_INITIALIZER)
    {
        nn_astemit_emitbyteandshort(prs, NEON_OP_LOCALGET, 0);
    }
    else
    {
        if(!prs->keeplastvalue || prs->lastwasstatement)
        {
            if(prs->currentfunccompiler->fromimport)
            {
                nn_astemit_emitinstruc(prs, NEON_OP_PUSHNULL);
            }
            else
            {
                nn_astemit_emitinstruc(prs, NEON_OP_PUSHEMPTY);
            }
        }
    }
    nn_astemit_emitinstruc(prs, NEON_OP_RETURN);
}

int nn_astparser_pushconst(NeonAstParser* prs, NeonValue value)
{
    int constant;
    constant = nn_blob_pushconst(prs->pvm, nn_astparser_currentblob(prs), value);
    if(constant >= UINT16_MAX)
    {
        nn_astparser_raiseerror(prs, "too many constants in current scope");
        return 0;
    }
    return constant;
}

void nn_astemit_emitconst(NeonAstParser* prs, NeonValue value)
{
    int constant;
    constant = nn_astparser_pushconst(prs, value);
    nn_astemit_emitbyteandshort(prs, NEON_OP_PUSHCONSTANT, (uint16_t)constant);
}

int nn_astemit_emitjump(NeonAstParser* prs, uint8_t instruction)
{
    nn_astemit_emitinstruc(prs, instruction);
    /* placeholders */
    nn_astemit_emit1byte(prs, 0xff);
    nn_astemit_emit1byte(prs, 0xff);
    return nn_astparser_currentblob(prs)->count - 2;
}

int nn_astemit_emitswitch(NeonAstParser* prs)
{
    nn_astemit_emitinstruc(prs, NEON_OP_SWITCH);
    /* placeholders */
    nn_astemit_emit1byte(prs, 0xff);
    nn_astemit_emit1byte(prs, 0xff);
    return nn_astparser_currentblob(prs)->count - 2;
}

int nn_astemit_emittry(NeonAstParser* prs)
{
    nn_astemit_emitinstruc(prs, NEON_OP_EXTRY);
    /* type placeholders */
    nn_astemit_emit1byte(prs, 0xff);
    nn_astemit_emit1byte(prs, 0xff);
    /* handler placeholders */
    nn_astemit_emit1byte(prs, 0xff);
    nn_astemit_emit1byte(prs, 0xff);
    /* finally placeholders */
    nn_astemit_emit1byte(prs, 0xff);
    nn_astemit_emit1byte(prs, 0xff);
    return nn_astparser_currentblob(prs)->count - 6;
}

void nn_astemit_patchswitch(NeonAstParser* prs, int offset, int constant)
{
    nn_astemit_patchat(prs, offset, (constant >> 8) & 0xff);
    nn_astemit_patchat(prs, offset + 1, constant & 0xff);
}

void nn_astemit_patchtry(NeonAstParser* prs, int offset, int type, int address, int finally)
{
    /* patch type */
    nn_astemit_patchat(prs, offset, (type >> 8) & 0xff);
    nn_astemit_patchat(prs, offset + 1, type & 0xff);
    /* patch address */
    nn_astemit_patchat(prs, offset + 2, (address >> 8) & 0xff);
    nn_astemit_patchat(prs, offset + 3, address & 0xff);
    /* patch finally */
    nn_astemit_patchat(prs, offset + 4, (finally >> 8) & 0xff);
    nn_astemit_patchat(prs, offset + 5, finally & 0xff);
}

void nn_astemit_patchjump(NeonAstParser* prs, int offset)
{
    /* -2 to adjust the bytecode for the offset itself */
    int jump;
    jump = nn_astparser_currentblob(prs)->count - offset - 2;
    if(jump > UINT16_MAX)
    {
        nn_astparser_raiseerror(prs, "body of conditional block too large");
    }
    nn_astemit_patchat(prs, offset, (jump >> 8) & 0xff);
    nn_astemit_patchat(prs, offset + 1, jump & 0xff);
}

void nn_astfunccompiler_init(NeonAstParser* prs, NeonAstFuncCompiler* compiler, NeonFuncType type, bool isanon)
{
    bool candeclthis;
    NeonPrinter wtmp;
    NeonAstLocal* local;
    NeonObjString* fname;
    compiler->enclosing = prs->currentfunccompiler;
    compiler->targetfunc = NULL;
    compiler->type = type;
    compiler->localcount = 0;
    compiler->scopedepth = 0;
    compiler->handlercount = 0;
    compiler->fromimport = false;
    compiler->targetfunc = nn_object_makefuncscript(prs->pvm, prs->currentmodule, type);
    prs->currentfunccompiler = compiler;
    if(type != NEON_FUNCTYPE_SCRIPT)
    {
        nn_vm_stackpush(prs->pvm, nn_value_fromobject(compiler->targetfunc));
        if(isanon)
        {
            nn_printer_makestackstring(prs->pvm, &wtmp);
            nn_printer_writefmt(&wtmp, "anonymous@[%s:%d]", prs->currentfile, prs->prevtoken.line);
            fname = nn_printer_takestring(&wtmp);
        }
        else
        {
            fname = nn_string_copylen(prs->pvm, prs->prevtoken.start, prs->prevtoken.length);
        }
        prs->currentfunccompiler->targetfunc->name = fname;
        nn_vm_stackpop(prs->pvm);
    }
    /* claiming slot zero for use in class methods */
    local = &prs->currentfunccompiler->locals[0];
    prs->currentfunccompiler->localcount++;
    local->depth = 0;
    local->iscaptured = false;
    candeclthis = (
        (type != NEON_FUNCTYPE_FUNCTION) &&
        (prs->compcontext == NEON_COMPCONTEXT_CLASS)
    );
    if(candeclthis || (/*(type == NEON_FUNCTYPE_ANONYMOUS) &&*/ (prs->compcontext != NEON_COMPCONTEXT_CLASS)))
    {
        local->name.start = g_strthis;
        local->name.length = 4;
    }
    else
    {
        local->name.start = "";
        local->name.length = 0;
    }
}

int nn_astparser_makeidentconst(NeonAstParser* prs, NeonAstToken* name)
{
    int rawlen;
    const char* rawstr;
    NeonObjString* str;
    rawstr = name->start;
    rawlen = name->length;
    if(name->isglobal)
    {
        rawstr++;
        rawlen--;
    }
    str = nn_string_copylen(prs->pvm, rawstr, rawlen);
    return nn_astparser_pushconst(prs, nn_value_fromobject(str));
}

bool nn_astparser_identsequal(NeonAstToken* a, NeonAstToken* b)
{
    return a->length == b->length && memcmp(a->start, b->start, a->length) == 0;
}

int nn_astfunccompiler_resolvelocal(NeonAstParser* prs, NeonAstFuncCompiler* compiler, NeonAstToken* name)
{
    int i;
    NeonAstLocal* local;
    for(i = compiler->localcount - 1; i >= 0; i--)
    {
        local = &compiler->locals[i];
        if(nn_astparser_identsequal(&local->name, name))
        {
            if(local->depth == -1)
            {
                nn_astparser_raiseerror(prs, "cannot read local variable in it's own initializer");
            }
            return i;
        }
    }
    return -1;
}

int nn_astfunccompiler_addupvalue(NeonAstParser* prs, NeonAstFuncCompiler* compiler, uint16_t index, bool islocal)
{
    int i;
    int upcnt;
    NeonAstUpvalue* upvalue;
    upcnt = compiler->targetfunc->upvalcount;
    for(i = 0; i < upcnt; i++)
    {
        upvalue = &compiler->upvalues[i];
        if(upvalue->index == index && upvalue->islocal == islocal)
        {
            return i;
        }
    }
    if(upcnt == NEON_CFG_ASTMAXUPVALS)
    {
        nn_astparser_raiseerror(prs, "too many closure variables in function");
        return 0;
    }
    compiler->upvalues[upcnt].islocal = islocal;
    compiler->upvalues[upcnt].index = index;
    return compiler->targetfunc->upvalcount++;
}

int nn_astfunccompiler_resolveupvalue(NeonAstParser* prs, NeonAstFuncCompiler* compiler, NeonAstToken* name)
{
    int local;
    int upvalue;
    if(compiler->enclosing == NULL)
    {
        return -1;
    }
    local = nn_astfunccompiler_resolvelocal(prs, compiler->enclosing, name);
    if(local != -1)
    {
        compiler->enclosing->locals[local].iscaptured = true;
        return nn_astfunccompiler_addupvalue(prs, compiler, (uint16_t)local, true);
    }
    upvalue = nn_astfunccompiler_resolveupvalue(prs, compiler->enclosing, name);
    if(upvalue != -1)
    {
        return nn_astfunccompiler_addupvalue(prs, compiler, (uint16_t)upvalue, false);
    }
    return -1;
}

int nn_astparser_addlocal(NeonAstParser* prs, NeonAstToken name)
{
    NeonAstLocal* local;
    if(prs->currentfunccompiler->localcount == NEON_CFG_ASTMAXLOCALS)
    {
        /* we've reached maximum local variables per scope */
        nn_astparser_raiseerror(prs, "too many local variables in scope");
        return -1;
    }
    local = &prs->currentfunccompiler->locals[prs->currentfunccompiler->localcount++];
    local->name = name;
    local->depth = -1;
    local->iscaptured = false;
    return prs->currentfunccompiler->localcount;
}

void nn_astparser_declarevariable(NeonAstParser* prs)
{
    int i;
    NeonAstToken* name;
    NeonAstLocal* local;
    /* global variables are implicitly declared... */
    if(prs->currentfunccompiler->scopedepth == 0)
    {
        return;
    }
    name = &prs->prevtoken;
    for(i = prs->currentfunccompiler->localcount - 1; i >= 0; i--)
    {
        local = &prs->currentfunccompiler->locals[i];
        if(local->depth != -1 && local->depth < prs->currentfunccompiler->scopedepth)
        {
            break;
        }
        if(nn_astparser_identsequal(name, &local->name))
        {
            nn_astparser_raiseerror(prs, "%.*s already declared in current scope", name->length, name->start);
        }
    }
    nn_astparser_addlocal(prs, *name);
}

int nn_astparser_parsevariable(NeonAstParser* prs, const char* message)
{
    if(!nn_astparser_consume(prs, NEON_ASTTOK_IDENTNORMAL, message))
    {
        /* what to do here? */
    }
    nn_astparser_declarevariable(prs);
    /* we are in a local scope... */
    if(prs->currentfunccompiler->scopedepth > 0)
    {
        return 0;
    }
    return nn_astparser_makeidentconst(prs, &prs->prevtoken);
}

void nn_astparser_markinitialized(NeonAstParser* prs)
{
    if(prs->currentfunccompiler->scopedepth == 0)
    {
        return;
    }
    prs->currentfunccompiler->locals[prs->currentfunccompiler->localcount - 1].depth = prs->currentfunccompiler->scopedepth;
}

void nn_astparser_definevariable(NeonAstParser* prs, int global)
{
    /* we are in a local scope... */
    if(prs->currentfunccompiler->scopedepth > 0)
    {
        nn_astparser_markinitialized(prs);
        return;
    }
    nn_astemit_emitbyteandshort(prs, NEON_OP_GLOBALDEFINE, global);
}

NeonAstToken nn_astparser_synthtoken(const char* name)
{
    NeonAstToken token;
    token.isglobal = false;
    token.line = 0;
    token.type = (NeonAstTokType)0;
    token.start = name;
    token.length = (int)strlen(name);
    return token;
}

NeonObjFuncScript* nn_astparser_endcompiler(NeonAstParser* prs)
{
    const char* fname;
    NeonObjFuncScript* function;
    nn_astemit_emitreturn(prs);
    function = prs->currentfunccompiler->targetfunc;
    fname = NULL;
    if(function->name == NULL)
    {
        fname = prs->currentmodule->physicalpath->sbuf->data;
    }
    else
    {
        fname = function->name->sbuf->data;
    }
    if(!prs->haderror && prs->pvm->conf.dumpbytecode)
    {
        nn_dbg_disasmblob(prs->pvm->debugwriter, nn_astparser_currentblob(prs), fname);
    }
    NEON_ASTDEBUG(prs->pvm, "for function '%s'", fname);
    prs->currentfunccompiler = prs->currentfunccompiler->enclosing;
    return function;
}

void nn_astparser_scopebegin(NeonAstParser* prs)
{
    NEON_ASTDEBUG(prs->pvm, "current depth=%d", prs->currentfunccompiler->scopedepth);
    prs->currentfunccompiler->scopedepth++;
}

bool nn_astutil_scopeendcancontinue(NeonAstParser* prs)
{
    int lopos;
    int locount;
    int lodepth;
    int scodepth;
    NEON_ASTDEBUG(prs->pvm, "");
    locount = prs->currentfunccompiler->localcount;
    lopos = prs->currentfunccompiler->localcount - 1;
    lodepth = prs->currentfunccompiler->locals[lopos].depth;
    scodepth = prs->currentfunccompiler->scopedepth;
    if(locount > 0 && lodepth > scodepth)
    {
        return true;
    }
    return false;
}

void nn_astparser_scopeend(NeonAstParser* prs)
{
    NEON_ASTDEBUG(prs->pvm, "current scope depth=%d", prs->currentfunccompiler->scopedepth);
    prs->currentfunccompiler->scopedepth--;
    /*
    // remove all variables declared in scope while exiting...
    */
    if(prs->keeplastvalue)
    {
        //return;
    }
    while(nn_astutil_scopeendcancontinue(prs))
    {
        if(prs->currentfunccompiler->locals[prs->currentfunccompiler->localcount - 1].iscaptured)
        {
            nn_astemit_emitinstruc(prs, NEON_OP_UPVALUECLOSE);
        }
        else
        {
            nn_astemit_emitinstruc(prs, NEON_OP_POPONE);
        }
        prs->currentfunccompiler->localcount--;
    }
}

int nn_astparser_discardlocals(NeonAstParser* prs, int depth)
{
    int local;
    NEON_ASTDEBUG(prs->pvm, "");
    if(prs->keeplastvalue)
    {
        //return 0;
    }
    if(prs->currentfunccompiler->scopedepth == -1)
    {
        nn_astparser_raiseerror(prs, "cannot exit top-level scope");
    }
    local = prs->currentfunccompiler->localcount - 1;
    while(local >= 0 && prs->currentfunccompiler->locals[local].depth >= depth)
    {
        if(prs->currentfunccompiler->locals[local].iscaptured)
        {
            nn_astemit_emitinstruc(prs, NEON_OP_UPVALUECLOSE);
        }
        else
        {
            nn_astemit_emitinstruc(prs, NEON_OP_POPONE);
        }
        local--;
    }
    return prs->currentfunccompiler->localcount - local - 1;
}

void nn_astparser_endloop(NeonAstParser* prs)
{
    int i;
    NeonInstruction* bcode;
    NeonValue* cvals;
    NEON_ASTDEBUG(prs->pvm, "");
    /*
    // find all NEON_OP_BREAK_PL placeholder and replace with the appropriate jump...
    */
    i = prs->innermostloopstart;
    while(i < prs->currentfunccompiler->targetfunc->blob.count)
    {
        if(prs->currentfunccompiler->targetfunc->blob.instrucs[i].code == NEON_OP_BREAK_PL)
        {
            prs->currentfunccompiler->targetfunc->blob.instrucs[i].code = NEON_OP_JUMPNOW;
            nn_astemit_patchjump(prs, i + 1);
            i += 3;
        }
        else
        {
            bcode = prs->currentfunccompiler->targetfunc->blob.instrucs;
            cvals = prs->currentfunccompiler->targetfunc->blob.constants->values;
            i += 1 + nn_astparser_getcodeargscount(bcode, cvals, i);
        }
    }
}

bool nn_astparser_rulebinary(NeonAstParser* prs, NeonAstToken previous, bool canassign)
{
    NeonAstTokType op;
    NeonAstRule* rule;
    (void)previous;
    (void)canassign;
    NEON_ASTDEBUG(prs->pvm, "");
    op = prs->prevtoken.type;
    /* compile the right operand */
    rule = nn_astparser_getrule(op);
    nn_astparser_parseprecedence(prs, (NeonAstPrecedence)(rule->precedence + 1));
    /* emit the operator instruction */
    switch(op)
    {
        case NEON_ASTTOK_PLUS:
            nn_astemit_emitinstruc(prs, NEON_OP_PRIMADD);
            break;
        case NEON_ASTTOK_MINUS:
            nn_astemit_emitinstruc(prs, NEON_OP_PRIMSUBTRACT);
            break;
        case NEON_ASTTOK_MULTIPLY:
            nn_astemit_emitinstruc(prs, NEON_OP_PRIMMULTIPLY);
            break;
        case NEON_ASTTOK_DIVIDE:
            nn_astemit_emitinstruc(prs, NEON_OP_PRIMDIVIDE);
            break;
        case NEON_ASTTOK_MODULO:
            nn_astemit_emitinstruc(prs, NEON_OP_PRIMMODULO);
            break;
        case NEON_ASTTOK_POWEROF:
            nn_astemit_emitinstruc(prs, NEON_OP_PRIMPOW);
            break;
        case NEON_ASTTOK_FLOOR:
            nn_astemit_emitinstruc(prs, NEON_OP_PRIMFLOORDIVIDE);
            break;
            /* equality */
        case NEON_ASTTOK_EQUAL:
            nn_astemit_emitinstruc(prs, NEON_OP_EQUAL);
            break;
        case NEON_ASTTOK_NOTEQUAL:
            nn_astemit_emit2byte(prs, NEON_OP_EQUAL, NEON_OP_PRIMNOT);
            break;
        case NEON_ASTTOK_GREATERTHAN:
            nn_astemit_emitinstruc(prs, NEON_OP_PRIMGREATER);
            break;
        case NEON_ASTTOK_GREATER_EQ:
            nn_astemit_emit2byte(prs, NEON_OP_PRIMLESSTHAN, NEON_OP_PRIMNOT);
            break;
        case NEON_ASTTOK_LESSTHAN:
            nn_astemit_emitinstruc(prs, NEON_OP_PRIMLESSTHAN);
            break;
        case NEON_ASTTOK_LESSEQUAL:
            nn_astemit_emit2byte(prs, NEON_OP_PRIMGREATER, NEON_OP_PRIMNOT);
            break;
            /* bitwise */
        case NEON_ASTTOK_AMP:
            nn_astemit_emitinstruc(prs, NEON_OP_PRIMAND);
            break;
        case NEON_ASTTOK_BAR:
            nn_astemit_emitinstruc(prs, NEON_OP_PRIMOR);
            break;
        case NEON_ASTTOK_XOR:
            nn_astemit_emitinstruc(prs, NEON_OP_PRIMBITXOR);
            break;
        case NEON_ASTTOK_LEFTSHIFT:
            nn_astemit_emitinstruc(prs, NEON_OP_PRIMSHIFTLEFT);
            break;
        case NEON_ASTTOK_RIGHTSHIFT:
            nn_astemit_emitinstruc(prs, NEON_OP_PRIMSHIFTRIGHT);
            break;
            /* range */
        case NEON_ASTTOK_DOUBLEDOT:
            nn_astemit_emitinstruc(prs, NEON_OP_MAKERANGE);
            break;
        default:
            break;
    }
    return true;
}

bool nn_astparser_rulecall(NeonAstParser* prs, NeonAstToken previous, bool canassign)
{
    uint8_t argcount;
    (void)previous;
    (void)canassign;
    NEON_ASTDEBUG(prs->pvm, "");
    argcount = nn_astparser_parsefunccallargs(prs);
    nn_astemit_emit2byte(prs, NEON_OP_CALLFUNCTION, argcount);
    return true;
}

bool nn_astparser_ruleliteral(NeonAstParser* prs, bool canassign)
{
    (void)canassign;
    NEON_ASTDEBUG(prs->pvm, "");
    switch(prs->prevtoken.type)
    {
        case NEON_ASTTOK_KWNULL:
            nn_astemit_emitinstruc(prs, NEON_OP_PUSHNULL);
            break;
        case NEON_ASTTOK_KWTRUE:
            nn_astemit_emitinstruc(prs, NEON_OP_PUSHTRUE);
            break;
        case NEON_ASTTOK_KWFALSE:
            nn_astemit_emitinstruc(prs, NEON_OP_PUSHFALSE);
            break;
        default:
            /* TODO: assuming this is correct behaviour ... */
            return false;
    }
    return true;
}

void nn_astparser_parseassign(NeonAstParser* prs, uint8_t realop, uint8_t getop, uint8_t setop, int arg)
{
    NEON_ASTDEBUG(prs->pvm, "");
    prs->replcanecho = false;
    if(getop == NEON_OP_PROPERTYGET || getop == NEON_OP_PROPERTYGETSELF)
    {
        nn_astemit_emitinstruc(prs, NEON_OP_DUPONE);
    }
    if(arg != -1)
    {
        nn_astemit_emitbyteandshort(prs, getop, arg);
    }
    else
    {
        nn_astemit_emit2byte(prs, getop, 1);
    }
    nn_astparser_parseexpression(prs);
    nn_astemit_emitinstruc(prs, realop);
    if(arg != -1)
    {
        nn_astemit_emitbyteandshort(prs, setop, (uint16_t)arg);
    }
    else
    {
        nn_astemit_emitinstruc(prs, setop);
    }
}

void nn_astparser_assignment(NeonAstParser* prs, uint8_t getop, uint8_t setop, int arg, bool canassign)
{
    NEON_ASTDEBUG(prs->pvm, "");
    if(canassign && nn_astparser_match(prs, NEON_ASTTOK_ASSIGN))
    {
        prs->replcanecho = false;
        nn_astparser_parseexpression(prs);
        if(arg != -1)
        {
            nn_astemit_emitbyteandshort(prs, setop, (uint16_t)arg);
        }
        else
        {
            nn_astemit_emitinstruc(prs, setop);
        }
    }
    else if(canassign && nn_astparser_match(prs, NEON_ASTTOK_PLUSASSIGN))
    {
        nn_astparser_parseassign(prs, NEON_OP_PRIMADD, getop, setop, arg);
    }
    else if(canassign && nn_astparser_match(prs, NEON_ASTTOK_MINUSASSIGN))
    {
        nn_astparser_parseassign(prs, NEON_OP_PRIMSUBTRACT, getop, setop, arg);
    }
    else if(canassign && nn_astparser_match(prs, NEON_ASTTOK_MULTASSIGN))
    {
        nn_astparser_parseassign(prs, NEON_OP_PRIMMULTIPLY, getop, setop, arg);
    }
    else if(canassign && nn_astparser_match(prs, NEON_ASTTOK_DIVASSIGN))
    {
        nn_astparser_parseassign(prs, NEON_OP_PRIMDIVIDE, getop, setop, arg);
    }
    else if(canassign && nn_astparser_match(prs, NEON_ASTTOK_POWASSIGN))
    {
        nn_astparser_parseassign(prs, NEON_OP_PRIMPOW, getop, setop, arg);
    }
    else if(canassign && nn_astparser_match(prs, NEON_ASTTOK_PERCENT_EQ))
    {
        nn_astparser_parseassign(prs, NEON_OP_PRIMMODULO, getop, setop, arg);
    }
    else if(canassign && nn_astparser_match(prs, NEON_ASTTOK_AMP_EQ))
    {
        nn_astparser_parseassign(prs, NEON_OP_PRIMAND, getop, setop, arg);
    }
    else if(canassign && nn_astparser_match(prs, NEON_ASTTOK_BAR_EQ))
    {
        nn_astparser_parseassign(prs, NEON_OP_PRIMOR, getop, setop, arg);
    }
    else if(canassign && nn_astparser_match(prs, NEON_ASTTOK_TILDE_EQ))
    {
        nn_astparser_parseassign(prs, NEON_OP_PRIMBITNOT, getop, setop, arg);
    }
    else if(canassign && nn_astparser_match(prs, NEON_ASTTOK_XOR_EQ))
    {
        nn_astparser_parseassign(prs, NEON_OP_PRIMBITXOR, getop, setop, arg);
    }
    else if(canassign && nn_astparser_match(prs, NEON_ASTTOK_LEFTSHIFTASSIGN))
    {
        nn_astparser_parseassign(prs, NEON_OP_PRIMSHIFTLEFT, getop, setop, arg);
    }
    else if(canassign && nn_astparser_match(prs, NEON_ASTTOK_RIGHTSHIFTASSIGN))
    {
        nn_astparser_parseassign(prs, NEON_OP_PRIMSHIFTRIGHT, getop, setop, arg);
    }
    else if(canassign && nn_astparser_match(prs, NEON_ASTTOK_INCREMENT))
    {
        prs->replcanecho = false;
        if(getop == NEON_OP_PROPERTYGET || getop == NEON_OP_PROPERTYGETSELF)
        {
            nn_astemit_emitinstruc(prs, NEON_OP_DUPONE);
        }

        if(arg != -1)
        {
            nn_astemit_emitbyteandshort(prs, getop, arg);
        }
        else
        {
            nn_astemit_emit2byte(prs, getop, 1);
        }

        nn_astemit_emit2byte(prs, NEON_OP_PUSHONE, NEON_OP_PRIMADD);
        nn_astemit_emitbyteandshort(prs, setop, (uint16_t)arg);
    }
    else if(canassign && nn_astparser_match(prs, NEON_ASTTOK_DECREMENT))
    {
        prs->replcanecho = false;
        if(getop == NEON_OP_PROPERTYGET || getop == NEON_OP_PROPERTYGETSELF)
        {
            nn_astemit_emitinstruc(prs, NEON_OP_DUPONE);
        }

        if(arg != -1)
        {
            nn_astemit_emitbyteandshort(prs, getop, arg);
        }
        else
        {
            nn_astemit_emit2byte(prs, getop, 1);
        }

        nn_astemit_emit2byte(prs, NEON_OP_PUSHONE, NEON_OP_PRIMSUBTRACT);
        nn_astemit_emitbyteandshort(prs, setop, (uint16_t)arg);
    }
    else
    {
        if(arg != -1)
        {
            if(getop == NEON_OP_INDEXGET || getop == NEON_OP_INDEXGETRANGED)
            {
                nn_astemit_emit2byte(prs, getop, (uint8_t)0);
            }
            else
            {
                nn_astemit_emitbyteandshort(prs, getop, (uint16_t)arg);
            }
        }
        else
        {
            nn_astemit_emit2byte(prs, getop, (uint8_t)0);
        }
    }
}

bool nn_astparser_ruledot(NeonAstParser* prs, NeonAstToken previous, bool canassign)
{
    int name;
    bool caninvoke;
    uint8_t argcount;
    NeonOpCode getop;
    NeonOpCode setop;
    NEON_ASTDEBUG(prs->pvm, "");
    nn_astparser_ignorewhitespace(prs);
    if(!nn_astparser_consume(prs, NEON_ASTTOK_IDENTNORMAL, "expected property name after '.'"))
    {
        return false;
    }
    name = nn_astparser_makeidentconst(prs, &prs->prevtoken);
    if(nn_astparser_match(prs, NEON_ASTTOK_PARENOPEN))
    {
        argcount = nn_astparser_parsefunccallargs(prs);
        caninvoke = (
            (prs->currentclasscompiler != NULL) &&
            (
                (previous.type == NEON_ASTTOK_KWTHIS) ||
                (nn_astparser_identsequal(&prs->prevtoken, &prs->currentclasscompiler->name))
            )
        );
        if(caninvoke)
        {
            nn_astemit_emitbyteandshort(prs, NEON_OP_CLASSINVOKETHIS, name);
        }
        else
        {
            nn_astemit_emitbyteandshort(prs, NEON_OP_CALLMETHOD, name);
        }
        nn_astemit_emit1byte(prs, argcount);
    }
    else
    {
        getop = NEON_OP_PROPERTYGET;
        setop = NEON_OP_PROPERTYSET;
        if(prs->currentclasscompiler != NULL && (previous.type == NEON_ASTTOK_KWTHIS || nn_astparser_identsequal(&prs->prevtoken, &prs->currentclasscompiler->name)))
        {
            getop = NEON_OP_PROPERTYGETSELF;
        }
        nn_astparser_assignment(prs, getop, setop, name, canassign);
    }
    return true;
}

void nn_astparser_namedvar(NeonAstParser* prs, NeonAstToken name, bool canassign)
{
    bool fromclass;
    uint8_t getop;
    uint8_t setop;
    int arg;
    (void)fromclass;
    NEON_ASTDEBUG(prs->pvm, " name=%.*s", name.length, name.start);
    fromclass = prs->currentclasscompiler != NULL;
    arg = nn_astfunccompiler_resolvelocal(prs, prs->currentfunccompiler, &name);
    if(arg != -1)
    {
        if(prs->infunction)
        {
            getop = NEON_OP_FUNCARGGET;
            setop = NEON_OP_FUNCARGSET;
        }
        else
        {
            getop = NEON_OP_LOCALGET;
            setop = NEON_OP_LOCALSET;
        }
    }
    else
    {
        arg = nn_astfunccompiler_resolveupvalue(prs, prs->currentfunccompiler, &name);
        if((arg != -1) && (name.isglobal == false))
        {
            getop = NEON_OP_UPVALUEGET;
            setop = NEON_OP_UPVALUESET;
        }
        else
        {
            arg = nn_astparser_makeidentconst(prs, &name);
            getop = NEON_OP_GLOBALGET;
            setop = NEON_OP_GLOBALSET;
        }
    }
    nn_astparser_assignment(prs, getop, setop, arg, canassign);
}

void nn_astparser_createdvar(NeonAstParser* prs, NeonAstToken name)
{
    int local;
    NEON_ASTDEBUG(prs->pvm, "name=%.*s", name.length, name.start);
    if(prs->currentfunccompiler->targetfunc->name != NULL)
    {
        local = nn_astparser_addlocal(prs, name) - 1;
        nn_astparser_markinitialized(prs);
        nn_astemit_emitbyteandshort(prs, NEON_OP_LOCALSET, (uint16_t)local);
    }
    else
    {
        nn_astemit_emitbyteandshort(prs, NEON_OP_GLOBALDEFINE, (uint16_t)nn_astparser_makeidentconst(prs, &name));
    }
}

bool nn_astparser_rulearray(NeonAstParser* prs, bool canassign)
{
    int count;
    (void)canassign;
    NEON_ASTDEBUG(prs->pvm, "");
    /* placeholder for the list */
    nn_astemit_emitinstruc(prs, NEON_OP_PUSHNULL);
    count = 0;
    nn_astparser_ignorewhitespace(prs);
    if(!nn_astparser_check(prs, NEON_ASTTOK_BRACKETCLOSE))
    {
        do
        {
            nn_astparser_ignorewhitespace(prs);
            if(!nn_astparser_check(prs, NEON_ASTTOK_BRACKETCLOSE))
            {
                /* allow comma to end lists */
                nn_astparser_parseexpression(prs);
                nn_astparser_ignorewhitespace(prs);
                count++;
            }
            nn_astparser_ignorewhitespace(prs);
        } while(nn_astparser_match(prs, NEON_ASTTOK_COMMA));
    }
    nn_astparser_ignorewhitespace(prs);
    nn_astparser_consume(prs, NEON_ASTTOK_BRACKETCLOSE, "expected ']' at end of list");
    nn_astemit_emitbyteandshort(prs, NEON_OP_MAKEARRAY, count);
    return true;
}

bool nn_astparser_ruledictionary(NeonAstParser* prs, bool canassign)
{
    bool usedexpression;
    int itemcount;
    NeonAstCompContext oldctx;
    (void)canassign;
    NEON_ASTDEBUG(prs->pvm, "");
    /* placeholder for the dictionary */
    nn_astemit_emitinstruc(prs, NEON_OP_PUSHNULL);
    itemcount = 0;
    nn_astparser_ignorewhitespace(prs);
    if(!nn_astparser_check(prs, NEON_ASTTOK_BRACECLOSE))
    {
        do
        {
            nn_astparser_ignorewhitespace(prs);
            if(!nn_astparser_check(prs, NEON_ASTTOK_BRACECLOSE))
            {
                /* allow last pair to end with a comma */
                usedexpression = false;
                if(nn_astparser_check(prs, NEON_ASTTOK_IDENTNORMAL))
                {
                    nn_astparser_consume(prs, NEON_ASTTOK_IDENTNORMAL, "");
                    nn_astemit_emitconst(prs, nn_value_fromobject(nn_string_copylen(prs->pvm, prs->prevtoken.start, prs->prevtoken.length)));
                }
                else
                {
                    nn_astparser_parseexpression(prs);
                    usedexpression = true;
                }
                nn_astparser_ignorewhitespace(prs);
                if(!nn_astparser_check(prs, NEON_ASTTOK_COMMA) && !nn_astparser_check(prs, NEON_ASTTOK_BRACECLOSE))
                {
                    nn_astparser_consume(prs, NEON_ASTTOK_COLON, "expected ':' after dictionary key");
                    nn_astparser_ignorewhitespace(prs);

                    nn_astparser_parseexpression(prs);
                }
                else
                {
                    if(usedexpression)
                    {
                        nn_astparser_raiseerror(prs, "cannot infer dictionary values from expressions");
                        return false;
                    }
                    else
                    {
                        nn_astparser_namedvar(prs, prs->prevtoken, false);
                    }
                }
                itemcount++;
            }
        } while(nn_astparser_match(prs, NEON_ASTTOK_COMMA));
    }
    nn_astparser_ignorewhitespace(prs);
    nn_astparser_consume(prs, NEON_ASTTOK_BRACECLOSE, "expected '}' after dictionary");
    nn_astemit_emitbyteandshort(prs, NEON_OP_MAKEDICT, itemcount);
    return true;
}

bool nn_astparser_ruleindexing(NeonAstParser* prs, NeonAstToken previous, bool canassign)
{
    bool assignable;
    bool commamatch;
    uint8_t getop;
    (void)previous;
    (void)canassign;
    NEON_ASTDEBUG(prs->pvm, "");
    assignable = true;
    commamatch = false;
    getop = NEON_OP_INDEXGET;
    if(nn_astparser_match(prs, NEON_ASTTOK_COMMA))
    {
        nn_astemit_emitinstruc(prs, NEON_OP_PUSHNULL);
        commamatch = true;
        getop = NEON_OP_INDEXGETRANGED;
    }
    else
    {
        nn_astparser_parseexpression(prs);
    }
    if(!nn_astparser_match(prs, NEON_ASTTOK_BRACKETCLOSE))
    {
        getop = NEON_OP_INDEXGETRANGED;
        if(!commamatch)
        {
            nn_astparser_consume(prs, NEON_ASTTOK_COMMA, "expecting ',' or ']'");
        }
        if(nn_astparser_match(prs, NEON_ASTTOK_BRACKETCLOSE))
        {
            nn_astemit_emitinstruc(prs, NEON_OP_PUSHNULL);
        }
        else
        {
            nn_astparser_parseexpression(prs);
            nn_astparser_consume(prs, NEON_ASTTOK_BRACKETCLOSE, "expected ']' after indexing");
        }
        assignable = false;
    }
    else
    {
        if(commamatch)
        {
            nn_astemit_emitinstruc(prs, NEON_OP_PUSHNULL);
        }
    }
    nn_astparser_assignment(prs, getop, NEON_OP_INDEXSET, -1, assignable);
    return true;
}

bool nn_astparser_rulevarnormal(NeonAstParser* prs, bool canassign)
{
    NEON_ASTDEBUG(prs->pvm, "");
    nn_astparser_namedvar(prs, prs->prevtoken, canassign);
    return true;
}

bool nn_astparser_rulevarglobal(NeonAstParser* prs, bool canassign)
{
    NEON_ASTDEBUG(prs->pvm, "");
    nn_astparser_namedvar(prs, prs->prevtoken, canassign);
    return true;
}

bool nn_astparser_rulethis(NeonAstParser* prs, bool canassign)
{
    (void)canassign;
    NEON_ASTDEBUG(prs->pvm, "");
    #if 0
    if(prs->currentclasscompiler == NULL)
    {
        nn_astparser_raiseerror(prs, "cannot use keyword 'this' outside of a class");
        return false;
    }
    #endif
    //if(prs->currentclasscompiler != NULL)
    {
        nn_astparser_namedvar(prs, prs->prevtoken, false);
        //nn_astparser_namedvar(prs, nn_astparser_synthtoken(g_strthis), false);
    }
    //nn_astemit_emitinstruc(prs, NEON_OP_CLASSGETTHIS);
    return true;
}

bool nn_astparser_rulesuper(NeonAstParser* prs, bool canassign)
{
    int name;
    bool invokeself;
    uint8_t argcount;
    NEON_ASTDEBUG(prs->pvm, "");
    (void)canassign;
    if(prs->currentclasscompiler == NULL)
    {
        nn_astparser_raiseerror(prs, "cannot use keyword 'super' outside of a class");
        return false;
    }
    else if(!prs->currentclasscompiler->hassuperclass)
    {
        nn_astparser_raiseerror(prs, "cannot use keyword 'super' in a class without a superclass");
        return false;
    }
    name = -1;
    invokeself = false;
    if(!nn_astparser_check(prs, NEON_ASTTOK_PARENOPEN))
    {
        nn_astparser_consume(prs, NEON_ASTTOK_DOT, "expected '.' or '(' after super");
        nn_astparser_consume(prs, NEON_ASTTOK_IDENTNORMAL, "expected super class method name after .");
        name = nn_astparser_makeidentconst(prs, &prs->prevtoken);
    }
    else
    {
        invokeself = true;
    }
    nn_astparser_namedvar(prs, nn_astparser_synthtoken(g_strthis), false);
    if(nn_astparser_match(prs, NEON_ASTTOK_PARENOPEN))
    {
        argcount = nn_astparser_parsefunccallargs(prs);
        nn_astparser_namedvar(prs, nn_astparser_synthtoken(g_strsuper), false);
        if(!invokeself)
        {
            nn_astemit_emitbyteandshort(prs, NEON_OP_CLASSINVOKESUPER, name);
            nn_astemit_emit1byte(prs, argcount);
        }
        else
        {
            nn_astemit_emit2byte(prs, NEON_OP_CLASSINVOKESUPERSELF, argcount);
        }
    }
    else
    {
        nn_astparser_namedvar(prs, nn_astparser_synthtoken(g_strsuper), false);
        nn_astemit_emitbyteandshort(prs, NEON_OP_CLASSGETSUPER, name);
    }
    return true;
}

bool nn_astparser_rulegrouping(NeonAstParser* prs, bool canassign)
{
    (void)canassign;
    NEON_ASTDEBUG(prs->pvm, "");
    nn_astparser_ignorewhitespace(prs);
    nn_astparser_parseexpression(prs);
    while(nn_astparser_match(prs, NEON_ASTTOK_COMMA))
    {
        nn_astparser_parseexpression(prs);
    }
    nn_astparser_ignorewhitespace(prs);
    nn_astparser_consume(prs, NEON_ASTTOK_PARENCLOSE, "expected ')' after grouped expression");
    return true;
}

NeonValue nn_astparser_compilenumber(NeonAstParser* prs)
{
    double dbval;
    long longval;
    long long llval;
    NEON_ASTDEBUG(prs->pvm, "");
    if(prs->prevtoken.type == NEON_ASTTOK_LITNUMBIN)
    {
        llval = strtoll(prs->prevtoken.start + 2, NULL, 2);
        return nn_value_makenumber(llval);
    }
    else if(prs->prevtoken.type == NEON_ASTTOK_LITNUMOCT)
    {
        longval = strtol(prs->prevtoken.start + 2, NULL, 8);
        return nn_value_makenumber(longval);
    }
    else if(prs->prevtoken.type == NEON_ASTTOK_LITNUMHEX)
    {
        longval = strtol(prs->prevtoken.start, NULL, 16);
        return nn_value_makenumber(longval);
    }
    else
    {
        dbval = strtod(prs->prevtoken.start, NULL);
        return nn_value_makenumber(dbval);
    }
}

bool nn_astparser_rulenumber(NeonAstParser* prs, bool canassign)
{
    (void)canassign;
    NEON_ASTDEBUG(prs->pvm, "");
    nn_astemit_emitconst(prs, nn_astparser_compilenumber(prs));
    return true;
}

/*
// Reads the next character, which should be a hex digit (0-9, a-f, or A-F) and
// returns its numeric value. If the character isn't a hex digit, returns -1.
*/
int nn_astparser_readhexdigit(char c)
{
    if((c >= '0') && (c <= '9'))
    {
        return (c - '0');
    }
    if((c >= 'a') && (c <= 'f'))
    {
        return ((c - 'a') + 10);
    }
    if((c >= 'A') && (c <= 'F'))
    {
        return ((c - 'A') + 10);
    }
    return -1;
}

/*
// Reads [digits] hex digits in a string literal and returns their number value.
*/
int nn_astparser_readhexescape(NeonAstParser* prs, const char* str, int index, int count)
{
    size_t pos;
    int i;
    int cval;
    int digit;
    int value;
    value = 0;
    i = 0;
    digit = 0;
    for(; i < count; i++)
    {
        pos = (index + i + 2);
        cval = str[pos];
        digit = nn_astparser_readhexdigit(cval);
        if(digit == -1)
        {
            nn_astparser_raiseerror(prs, "invalid hex escape sequence at #%d of \"%s\": '%c' (%d)", pos, str, cval, cval);
        }
        value = (value * 16) | digit;
    }
    if(count == 4 && (digit = nn_astparser_readhexdigit(str[index + i + 2])) != -1)
    {
        value = (value * 16) | digit;
    }
    return value;
}

int nn_astparser_readunicodeescape(NeonAstParser* prs, char* string, const char* realstring, int numberbytes, int realindex, int index)
{
    int value;
    int count;
    size_t len;
    char* chr;
    NEON_ASTDEBUG(prs->pvm, "");
    value = nn_astparser_readhexescape(prs, realstring, realindex, numberbytes);
    count = nn_util_utf8numbytes(value);
    if(count == -1)
    {
        nn_astparser_raiseerror(prs, "cannot encode a negative unicode value");
    }
    /* check for greater that \uffff */
    if(value > 65535)
    {
        count++;
    }
    if(count != 0)
    {
        chr = nn_util_utf8encode(prs->pvm, value, &len);
        if(chr)
        {
            memcpy(string + index, chr, (size_t)count + 1);
            nn_util_memfree(prs->pvm, chr);
        }
        else
        {
            nn_astparser_raiseerror(prs, "cannot decode unicode escape at index %d", realindex);
        }
    }
    /* but greater than \uffff doesn't occupy any extra byte */
    /*
    if(value > 65535)
    {
        count--;
    }
    */
    return count;
}

char* nn_astparser_compilestring(NeonAstParser* prs, int* length)
{
    int k;
    int i;
    int count;
    int reallength;
    int rawlen;
    char c;
    char quote;
    char* deststr;
    char* realstr;
    rawlen = (((size_t)prs->prevtoken.length - 2) + 1);
    NEON_ASTDEBUG(prs->pvm, "raw length=%d", rawlen);
    deststr = (char*)nn_gcmem_allocate(prs->pvm, sizeof(char), rawlen);
    quote = prs->prevtoken.start[0];
    realstr = (char*)prs->prevtoken.start + 1;
    reallength = prs->prevtoken.length - 2;
    k = 0;
    for(i = 0; i < reallength; i++, k++)
    {
        c = realstr[i];
        if(c == '\\' && i < reallength - 1)
        {
            switch(realstr[i + 1])
            {
                case '0':
                    {
                        c = '\0';
                    }
                    break;
                case '$':
                    {
                        c = '$';
                    }
                    break;
                case '\'':
                    {
                        if(quote == '\'' || quote == '}')
                        {
                            /* } handle closing of interpolation. */
                            c = '\'';
                        }
                        else
                        {
                            i--;
                        }
                    }
                    break;
                case '"':
                    {
                        if(quote == '"' || quote == '}')
                        {
                            c = '"';
                        }
                        else
                        {
                            i--;
                        }
                    }
                    break;
                case 'a':
                    {
                        c = '\a';
                    }
                    break;
                case 'b':
                    {
                        c = '\b';
                    }
                    break;
                case 'f':
                    {
                        c = '\f';
                    }
                    break;
                case 'n':
                    {
                        c = '\n';
                    }
                    break;
                case 'r':
                    {
                        c = '\r';
                    }
                    break;
                case 't':
                    {
                        c = '\t';
                    }
                    break;
                case 'e':
                    {
                        c = 27;
                    }
                    break;
                case '\\':
                    {
                        c = '\\';
                    }
                    break;
                case 'v':
                    {
                        c = '\v';
                    }
                    break;
                case 'x':
                    {
                        //int nn_astparser_readunicodeescape(NeonAstParser* prs, char* string, char* realstring, int numberbytes, int realindex, int index)
                        //int nn_astparser_readhexescape(NeonAstParser* prs, const char* str, int index, int count)
                        //k += nn_astparser_readunicodeescape(prs, deststr, realstr, 2, i, k) - 1;
                        //k += nn_astparser_readhexescape(prs, deststr, i, 2) - 0;
                        c = nn_astparser_readhexescape(prs, realstr, i, 2) - 0;
                        i += 2;
                        //continue;
                    }
                    break;
                case 'u':
                    {
                        count = nn_astparser_readunicodeescape(prs, deststr, realstr, 4, i, k);
                        if(count > 4)
                        {
                            k += count - 2;
                        }
                        else
                        {
                            k += count - 1;
                        }
                        if(count > 4)
                        {
                            i += 6;
                        }
                        else
                        {
                            i += 5;
                        }
                        continue;
                    }
                case 'U':
                    {
                        count = nn_astparser_readunicodeescape(prs, deststr, realstr, 8, i, k);
                        if(count > 4)
                        {
                            k += count - 2;
                        }
                        else
                        {
                            k += count - 1;
                        }
                        i += 9;
                        continue;
                    }
                default:
                    {
                        i--;
                    }
                    break;
            }
            i++;
        }
        memcpy(deststr + k, &c, 1);
    }
    *length = k;
    deststr[k] = '\0';
    return deststr;
}

bool nn_astparser_rulestring(NeonAstParser* prs, bool canassign)
{
    int length;
    char* str;
    (void)canassign;
    NEON_ASTDEBUG(prs->pvm, "canassign=%d", canassign);
    str = nn_astparser_compilestring(prs, &length);
    nn_astemit_emitconst(prs, nn_value_fromobject(nn_string_takelen(prs->pvm, str, length)));
    return true;
}

bool nn_astparser_ruleinterpolstring(NeonAstParser* prs, bool canassign)
{
    int count;
    bool doadd;
    bool stringmatched;
    NEON_ASTDEBUG(prs->pvm, "canassign=%d", canassign);
    count = 0;
    do
    {
        doadd = false;
        stringmatched = false;
        if(prs->prevtoken.length - 2 > 0)
        {
            nn_astparser_rulestring(prs, canassign);
            doadd = true;
            stringmatched = true;
            if(count > 0)
            {
                nn_astemit_emitinstruc(prs, NEON_OP_PRIMADD);
            }
        }
        nn_astparser_parseexpression(prs);
        nn_astemit_emitinstruc(prs, NEON_OP_STRINGIFY);
        if(doadd || (count >= 1 && stringmatched == false))
        {
            nn_astemit_emitinstruc(prs, NEON_OP_PRIMADD);
        }
        count++;
    } while(nn_astparser_match(prs, NEON_ASTTOK_INTERPOLATION));
    nn_astparser_consume(prs, NEON_ASTTOK_LITERAL, "unterminated string interpolation");
    if(prs->prevtoken.length - 2 > 0)
    {
        nn_astparser_rulestring(prs, canassign);
        nn_astemit_emitinstruc(prs, NEON_OP_PRIMADD);
    }
    return true;
}

bool nn_astparser_ruleunary(NeonAstParser* prs, bool canassign)
{
    NeonAstTokType op;
    (void)canassign;
    NEON_ASTDEBUG(prs->pvm, "");
    op = prs->prevtoken.type;
    /* compile the expression */
    nn_astparser_parseprecedence(prs, NEON_ASTPREC_UNARY);
    /* emit instruction */
    switch(op)
    {
        case NEON_ASTTOK_MINUS:
            nn_astemit_emitinstruc(prs, NEON_OP_PRIMNEGATE);
            break;
        case NEON_ASTTOK_EXCLMARK:
            nn_astemit_emitinstruc(prs, NEON_OP_PRIMNOT);
            break;
        case NEON_ASTTOK_TILDE:
            nn_astemit_emitinstruc(prs, NEON_OP_PRIMBITNOT);
            break;
        default:
            break;
    }
    return true;
}

bool nn_astparser_ruleand(NeonAstParser* prs, NeonAstToken previous, bool canassign)
{
    int endjump;
    (void)previous;
    (void)canassign;
    NEON_ASTDEBUG(prs->pvm, "");
    endjump = nn_astemit_emitjump(prs, NEON_OP_JUMPIFFALSE);
    nn_astemit_emitinstruc(prs, NEON_OP_POPONE);
    nn_astparser_parseprecedence(prs, NEON_ASTPREC_AND);
    nn_astemit_patchjump(prs, endjump);
    return true;
}

bool nn_astparser_ruleor(NeonAstParser* prs, NeonAstToken previous, bool canassign)
{
    int endjump;
    int elsejump;
    (void)previous;
    (void)canassign;
    NEON_ASTDEBUG(prs->pvm, "");
    elsejump = nn_astemit_emitjump(prs, NEON_OP_JUMPIFFALSE);
    endjump = nn_astemit_emitjump(prs, NEON_OP_JUMPNOW);
    nn_astemit_patchjump(prs, elsejump);
    nn_astemit_emitinstruc(prs, NEON_OP_POPONE);
    nn_astparser_parseprecedence(prs, NEON_ASTPREC_OR);
    nn_astemit_patchjump(prs, endjump);
    return true;
}

bool nn_astparser_ruleconditional(NeonAstParser* prs, NeonAstToken previous, bool canassign)
{
    int thenjump;
    int elsejump;
    (void)previous;
    (void)canassign;
    NEON_ASTDEBUG(prs->pvm, "");
    thenjump = nn_astemit_emitjump(prs, NEON_OP_JUMPIFFALSE);
    nn_astemit_emitinstruc(prs, NEON_OP_POPONE);
    nn_astparser_ignorewhitespace(prs);
    /* compile the then expression */
    nn_astparser_parseprecedence(prs, NEON_ASTPREC_CONDITIONAL);
    nn_astparser_ignorewhitespace(prs);
    elsejump = nn_astemit_emitjump(prs, NEON_OP_JUMPNOW);
    nn_astemit_patchjump(prs, thenjump);
    nn_astemit_emitinstruc(prs, NEON_OP_POPONE);
    nn_astparser_consume(prs, NEON_ASTTOK_COLON, "expected matching ':' after '?' conditional");
    nn_astparser_ignorewhitespace(prs);
    /*
    // compile the else expression
    // here we parse at NEON_ASTPREC_ASSIGNMENT precedence as
    // linear conditionals can be nested.
    */
    nn_astparser_parseprecedence(prs, NEON_ASTPREC_ASSIGNMENT);
    nn_astemit_patchjump(prs, elsejump);
    return true;
}

bool nn_astparser_ruleimport(NeonAstParser* prs, bool canassign)
{
    (void)canassign;
    NEON_ASTDEBUG(prs->pvm, "");
    nn_astparser_parseexpression(prs);
    nn_astemit_emitinstruc(prs, NEON_OP_IMPORTIMPORT);
    return true;
}

bool nn_astparser_rulenew(NeonAstParser* prs, bool canassign)
{
    NEON_ASTDEBUG(prs->pvm, "");
    nn_astparser_consume(prs, NEON_ASTTOK_IDENTNORMAL, "class name after 'new'");
    return nn_astparser_rulevarnormal(prs, canassign);
    //return nn_astparser_rulecall(prs, prs->prevtoken, canassign);
}

bool nn_astparser_ruletypeof(NeonAstParser* prs, bool canassign)
{
    (void)canassign;
    NEON_ASTDEBUG(prs->pvm, "");
    nn_astparser_consume(prs, NEON_ASTTOK_PARENOPEN, "expected '(' after 'typeof'");
    nn_astparser_parseexpression(prs);
    nn_astparser_consume(prs, NEON_ASTTOK_PARENCLOSE, "expected ')' after 'typeof'");
    nn_astemit_emitinstruc(prs, NEON_OP_TYPEOF);
    return true;
}

bool nn_astparser_rulenothingprefix(NeonAstParser* prs, bool canassign)
{
    (void)prs;
    (void)canassign;
    NEON_ASTDEBUG(prs->pvm, "");
    return true;
}

bool nn_astparser_rulenothinginfix(NeonAstParser* prs, NeonAstToken previous, bool canassign)
{
    (void)prs;
    (void)previous;
    (void)canassign;
    return true;
}

NeonAstRule* nn_astparser_putrule(NeonAstRule* dest, NeonAstParsePrefixFN prefix, NeonAstParseInfixFN infix, NeonAstPrecedence precedence)
{
    dest->prefix = prefix;
    dest->infix = infix;
    dest->precedence = precedence;
    return dest;
}

#define dorule(tok, prefix, infix, precedence) \
    case tok: return nn_astparser_putrule(&dest, prefix, infix, precedence);

NeonAstRule* nn_astparser_getrule(NeonAstTokType type)
{
    static NeonAstRule dest;
    switch(type)
    {
        dorule(NEON_ASTTOK_NEWLINE, nn_astparser_rulenothingprefix, nn_astparser_rulenothinginfix, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_PARENOPEN, nn_astparser_rulegrouping, nn_astparser_rulecall, NEON_ASTPREC_CALL );
        dorule(NEON_ASTTOK_PARENCLOSE, NULL, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_BRACKETOPEN, nn_astparser_rulearray, nn_astparser_ruleindexing, NEON_ASTPREC_CALL );
        dorule(NEON_ASTTOK_BRACKETCLOSE, NULL, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_BRACEOPEN, nn_astparser_ruledictionary, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_BRACECLOSE, NULL, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_SEMICOLON, nn_astparser_rulenothingprefix, nn_astparser_rulenothinginfix, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_COMMA, NULL, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_BACKSLASH, NULL, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_EXCLMARK, nn_astparser_ruleunary, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_NOTEQUAL, NULL, nn_astparser_rulebinary, NEON_ASTPREC_EQUALITY );
        dorule(NEON_ASTTOK_COLON, NULL, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_AT, nn_astparser_ruleanonfunc, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_DOT, NULL, nn_astparser_ruledot, NEON_ASTPREC_CALL );
        dorule(NEON_ASTTOK_DOUBLEDOT, NULL, nn_astparser_rulebinary, NEON_ASTPREC_RANGE );
        dorule(NEON_ASTTOK_TRIPLEDOT, NULL, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_PLUS, nn_astparser_ruleunary, nn_astparser_rulebinary, NEON_ASTPREC_TERM );
        dorule(NEON_ASTTOK_PLUSASSIGN, NULL, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_INCREMENT, NULL, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_MINUS, nn_astparser_ruleunary, nn_astparser_rulebinary, NEON_ASTPREC_TERM );
        dorule(NEON_ASTTOK_MINUSASSIGN, NULL, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_DECREMENT, NULL, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_MULTIPLY, NULL, nn_astparser_rulebinary, NEON_ASTPREC_FACTOR );
        dorule(NEON_ASTTOK_MULTASSIGN, NULL, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_POWEROF, NULL, nn_astparser_rulebinary, NEON_ASTPREC_FACTOR );
        dorule(NEON_ASTTOK_POWASSIGN, NULL, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_DIVIDE, NULL, nn_astparser_rulebinary, NEON_ASTPREC_FACTOR );
        dorule(NEON_ASTTOK_DIVASSIGN, NULL, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_FLOOR, NULL, nn_astparser_rulebinary, NEON_ASTPREC_FACTOR );
        dorule(NEON_ASTTOK_ASSIGN, NULL, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_EQUAL, NULL, nn_astparser_rulebinary, NEON_ASTPREC_EQUALITY );
        dorule(NEON_ASTTOK_LESSTHAN, NULL, nn_astparser_rulebinary, NEON_ASTPREC_COMPARISON );
        dorule(NEON_ASTTOK_LESSEQUAL, NULL, nn_astparser_rulebinary, NEON_ASTPREC_COMPARISON );
        dorule(NEON_ASTTOK_LEFTSHIFT, NULL, nn_astparser_rulebinary, NEON_ASTPREC_SHIFT );
        dorule(NEON_ASTTOK_LEFTSHIFTASSIGN, NULL, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_GREATERTHAN, NULL, nn_astparser_rulebinary, NEON_ASTPREC_COMPARISON );
        dorule(NEON_ASTTOK_GREATER_EQ, NULL, nn_astparser_rulebinary, NEON_ASTPREC_COMPARISON );
        dorule(NEON_ASTTOK_RIGHTSHIFT, NULL, nn_astparser_rulebinary, NEON_ASTPREC_SHIFT );
        dorule(NEON_ASTTOK_RIGHTSHIFTASSIGN, NULL, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_MODULO, NULL, nn_astparser_rulebinary, NEON_ASTPREC_FACTOR );
        dorule(NEON_ASTTOK_PERCENT_EQ, NULL, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_AMP, NULL, nn_astparser_rulebinary, NEON_ASTPREC_BITAND );
        dorule(NEON_ASTTOK_AMP_EQ, NULL, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_BAR, /*nn_astparser_ruleanoncompat*/ NULL, nn_astparser_rulebinary, NEON_ASTPREC_BITOR );
        dorule(NEON_ASTTOK_BAR_EQ, NULL, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_TILDE, nn_astparser_ruleunary, NULL, NEON_ASTPREC_UNARY );
        dorule(NEON_ASTTOK_TILDE_EQ, NULL, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_XOR, NULL, nn_astparser_rulebinary, NEON_ASTPREC_BITXOR );
        dorule(NEON_ASTTOK_XOR_EQ, NULL, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_QUESTION, NULL, nn_astparser_ruleconditional, NEON_ASTPREC_CONDITIONAL );
        dorule(NEON_ASTTOK_KWAND, NULL, nn_astparser_ruleand, NEON_ASTPREC_AND );
        dorule(NEON_ASTTOK_KWAS, NULL, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_KWASSERT, NULL, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_KWBREAK, NULL, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_KWCLASS, NULL, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_KWCONTINUE, NULL, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_KWFUNCTION, nn_astparser_ruleanonfunc, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_KWDEFAULT, NULL, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_KWTHROW, NULL, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_KWDO, NULL, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_KWECHO, NULL, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_KWELSE, NULL, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_KWFALSE, nn_astparser_ruleliteral, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_KWFOREACH, NULL, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_KWIF, NULL, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_KWIMPORT, nn_astparser_ruleimport, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_KWIN, NULL, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_KWFOR, NULL, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_KWVAR, NULL, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_KWNULL, nn_astparser_ruleliteral, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_KWNEW, nn_astparser_rulenew, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_KWTYPEOF, nn_astparser_ruletypeof, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_KWOR, NULL, nn_astparser_ruleor, NEON_ASTPREC_OR );
        dorule(NEON_ASTTOK_KWSUPER, nn_astparser_rulesuper, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_KWRETURN, NULL, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_KWTHIS, nn_astparser_rulethis, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_KWSTATIC, NULL, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_KWTRUE, nn_astparser_ruleliteral, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_KWSWITCH, NULL, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_KWCASE, NULL, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_KWWHILE, NULL, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_KWTRY, NULL, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_KWCATCH, NULL, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_KWFINALLY, NULL, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_LITERAL, nn_astparser_rulestring, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_LITNUMREG, nn_astparser_rulenumber, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_LITNUMBIN, nn_astparser_rulenumber, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_LITNUMOCT, nn_astparser_rulenumber, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_LITNUMHEX, nn_astparser_rulenumber, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_IDENTNORMAL, nn_astparser_rulevarnormal, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_INTERPOLATION, nn_astparser_ruleinterpolstring, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_EOF, NULL, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_ERROR, NULL, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_KWEMPTY, nn_astparser_ruleliteral, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_UNDEFINED, NULL, NULL, NEON_ASTPREC_NONE );
        default:
            fprintf(stderr, "missing rule?\n");
            break;
    }
    return NULL;
}
#undef dorule

bool nn_astparser_doparseprecedence(NeonAstParser* prs, NeonAstPrecedence precedence/*, NeonAstExpression* dest*/)
{
    bool canassign;
    NeonAstToken previous;
    NeonAstParseInfixFN infixrule;
    NeonAstParsePrefixFN prefixrule;
    prefixrule = nn_astparser_getrule(prs->prevtoken.type)->prefix;
    if(prefixrule == NULL)
    {
        nn_astparser_raiseerror(prs, "expected expression");
        return false;
    }
    canassign = precedence <= NEON_ASTPREC_ASSIGNMENT;
    prefixrule(prs, canassign);
    while(precedence <= nn_astparser_getrule(prs->currtoken.type)->precedence)
    {
        previous = prs->prevtoken;
        nn_astparser_ignorewhitespace(prs);
        nn_astparser_advance(prs);
        infixrule = nn_astparser_getrule(prs->prevtoken.type)->infix;
        infixrule(prs, previous, canassign);
    }
    if(canassign && nn_astparser_match(prs, NEON_ASTTOK_ASSIGN))
    {
        nn_astparser_raiseerror(prs, "invalid assignment target");
        return false;
    }
    return true;
}

bool nn_astparser_parseprecedence(NeonAstParser* prs, NeonAstPrecedence precedence)
{
    if(nn_astlex_isatend(prs->lexer) && prs->pvm->isrepl)
    {
        return false;
    }
    nn_astparser_ignorewhitespace(prs);
    if(nn_astlex_isatend(prs->lexer) && prs->pvm->isrepl)
    {
        return false;
    }
    nn_astparser_advance(prs);
    return nn_astparser_doparseprecedence(prs, precedence);
}

bool nn_astparser_parseprecnoadvance(NeonAstParser* prs, NeonAstPrecedence precedence)
{
    if(nn_astlex_isatend(prs->lexer) && prs->pvm->isrepl)
    {
        return false;
    }
    nn_astparser_ignorewhitespace(prs);
    if(nn_astlex_isatend(prs->lexer) && prs->pvm->isrepl)
    {
        return false;
    }
    return nn_astparser_doparseprecedence(prs, precedence);
}

bool nn_astparser_parseexpression(NeonAstParser* prs)
{
    return nn_astparser_parseprecedence(prs, NEON_ASTPREC_ASSIGNMENT);
}

bool nn_astparser_parseblock(NeonAstParser* prs)
{
    prs->blockcount++;
    nn_astparser_ignorewhitespace(prs);
    while(!nn_astparser_check(prs, NEON_ASTTOK_BRACECLOSE) && !nn_astparser_check(prs, NEON_ASTTOK_EOF))
    {
        nn_astparser_parsedeclaration(prs);
    }
    prs->blockcount--;
    if(!nn_astparser_consume(prs, NEON_ASTTOK_BRACECLOSE, "expected '}' after block"))
    {
        return false;
    }
    if(nn_astparser_match(prs, NEON_ASTTOK_SEMICOLON))
    {
    }
    return true;
}

void nn_astparser_declarefuncargvar(NeonAstParser* prs)
{
    int i;
    NeonAstToken* name;
    NeonAstLocal* local;
    /* global variables are implicitly declared... */
    if(prs->currentfunccompiler->scopedepth == 0)
    {
        return;
    }
    name = &prs->prevtoken;
    for(i = prs->currentfunccompiler->localcount - 1; i >= 0; i--)
    {
        local = &prs->currentfunccompiler->locals[i];
        if(local->depth != -1 && local->depth < prs->currentfunccompiler->scopedepth)
        {
            break;
        }
        if(nn_astparser_identsequal(name, &local->name))
        {
            nn_astparser_raiseerror(prs, "%.*s already declared in current scope", name->length, name->start);
        }
    }
    nn_astparser_addlocal(prs, *name);
}


int nn_astparser_parsefuncparamvar(NeonAstParser* prs, const char* message)
{
    if(!nn_astparser_consume(prs, NEON_ASTTOK_IDENTNORMAL, message))
    {
        /* what to do here? */
    }
    nn_astparser_declarefuncargvar(prs);
    /* we are in a local scope... */
    if(prs->currentfunccompiler->scopedepth > 0)
    {
        return 0;
    }
    return nn_astparser_makeidentconst(prs, &prs->prevtoken);
}

uint8_t nn_astparser_parsefunccallargs(NeonAstParser* prs)
{
    uint8_t argcount;
    argcount = 0;
    if(!nn_astparser_check(prs, NEON_ASTTOK_PARENCLOSE))
    {
        do
        {
            nn_astparser_ignorewhitespace(prs);
            nn_astparser_parseexpression(prs);
            if(argcount == NEON_CFG_ASTMAXFUNCPARAMS)
            {
                nn_astparser_raiseerror(prs, "cannot have more than %d arguments to a function", NEON_CFG_ASTMAXFUNCPARAMS);
            }
            argcount++;
        } while(nn_astparser_match(prs, NEON_ASTTOK_COMMA));
    }
    nn_astparser_ignorewhitespace(prs);
    if(!nn_astparser_consume(prs, NEON_ASTTOK_PARENCLOSE, "expected ')' after argument list"))
    {
        /* TODO: handle this, somehow. */
    }
    return argcount;
}

void nn_astparser_parsefuncparamlist(NeonAstParser* prs)
{
    int paramconstant;
    /* compile argument list... */
    do
    {
        nn_astparser_ignorewhitespace(prs);
        prs->currentfunccompiler->targetfunc->arity++;
        if(prs->currentfunccompiler->targetfunc->arity > NEON_CFG_ASTMAXFUNCPARAMS)
        {
            nn_astparser_raiseerroratcurrent(prs, "cannot have more than %d function parameters", NEON_CFG_ASTMAXFUNCPARAMS);
        }
        if(nn_astparser_match(prs, NEON_ASTTOK_TRIPLEDOT))
        {
            prs->currentfunccompiler->targetfunc->isvariadic = true;
            nn_astparser_addlocal(prs, nn_astparser_synthtoken("__args__"));
            nn_astparser_definevariable(prs, 0);
            break;
        }
        paramconstant = nn_astparser_parsefuncparamvar(prs, "expected parameter name");
        nn_astparser_definevariable(prs, paramconstant);
        nn_astparser_ignorewhitespace(prs);
    } while(nn_astparser_match(prs, NEON_ASTTOK_COMMA));
}

void nn_astfunccompiler_compilebody(NeonAstParser* prs, NeonAstFuncCompiler* compiler, bool closescope, bool isanon)
{
    int i;
    NeonObjFuncScript* function;
    (void)isanon;
    /* compile the body */
    nn_astparser_ignorewhitespace(prs);
    nn_astparser_consume(prs, NEON_ASTTOK_BRACEOPEN, "expected '{' before function body");
    nn_astparser_parseblock(prs);
    /* create the function object */
    if(closescope)
    {
        nn_astparser_scopeend(prs);
    }
    function = nn_astparser_endcompiler(prs);
    nn_vm_stackpush(prs->pvm, nn_value_fromobject(function));
    nn_astemit_emitbyteandshort(prs, NEON_OP_MAKECLOSURE, nn_astparser_pushconst(prs, nn_value_fromobject(function)));
    for(i = 0; i < function->upvalcount; i++)
    {
        nn_astemit_emit1byte(prs, compiler->upvalues[i].islocal ? 1 : 0);
        nn_astemit_emit1short(prs, compiler->upvalues[i].index);
    }
    nn_vm_stackpop(prs->pvm);
}

void nn_astparser_parsefuncfull(NeonAstParser* prs, NeonFuncType type, bool isanon)
{
    NeonAstFuncCompiler compiler;
    prs->infunction = true;
    nn_astfunccompiler_init(prs, &compiler, type, isanon);
    nn_astparser_scopebegin(prs);
    /* compile parameter list */
    nn_astparser_consume(prs, NEON_ASTTOK_PARENOPEN, "expected '(' after function name");
    if(!nn_astparser_check(prs, NEON_ASTTOK_PARENCLOSE))
    {
        nn_astparser_parsefuncparamlist(prs);
    }
    nn_astparser_consume(prs, NEON_ASTTOK_PARENCLOSE, "expected ')' after function parameters");
    nn_astfunccompiler_compilebody(prs, &compiler, false, isanon);
    prs->infunction = false;
}

void nn_astparser_parsemethod(NeonAstParser* prs, NeonAstToken classname, bool isstatic)
{
    size_t sn;
    int constant;
    const char* sc;
    NeonFuncType type;
    static NeonAstTokType tkns[] = { NEON_ASTTOK_IDENTNORMAL, NEON_ASTTOK_DECORATOR };
    (void)classname;
    (void)isstatic;
    sc = "constructor";
    sn = strlen(sc);
    nn_astparser_consumeor(prs, "method name expected", tkns, 2);
    constant = nn_astparser_makeidentconst(prs, &prs->prevtoken);
    type = NEON_FUNCTYPE_METHOD;
    if((prs->prevtoken.length == (int)sn) && (memcmp(prs->prevtoken.start, sc, sn) == 0))
    {
        type = NEON_FUNCTYPE_INITIALIZER;
    }
    else if((prs->prevtoken.length > 0) && (prs->prevtoken.start[0] == '_'))
    {
        type = NEON_FUNCTYPE_PRIVATE;
    }
    nn_astparser_parsefuncfull(prs, type, false);
    nn_astemit_emitbyteandshort(prs, NEON_OP_MAKEMETHOD, constant);
}

bool nn_astparser_ruleanonfunc(NeonAstParser* prs, bool canassign)
{
    NeonAstFuncCompiler compiler;
    (void)canassign;
    nn_astfunccompiler_init(prs, &compiler, NEON_FUNCTYPE_FUNCTION, true);
    nn_astparser_scopebegin(prs);
    /* compile parameter list */
    nn_astparser_consume(prs, NEON_ASTTOK_PARENOPEN, "expected '(' at start of anonymous function");
    if(!nn_astparser_check(prs, NEON_ASTTOK_PARENCLOSE))
    {
        nn_astparser_parsefuncparamlist(prs);
    }
    nn_astparser_consume(prs, NEON_ASTTOK_PARENCLOSE, "expected ')' after anonymous function parameters");
    nn_astfunccompiler_compilebody(prs, &compiler, true, true);
    return true;
}

void nn_astparser_parsefield(NeonAstParser* prs, bool isstatic)
{
    int fieldconstant;
    nn_astparser_consume(prs, NEON_ASTTOK_IDENTNORMAL, "class property name expected");
    fieldconstant = nn_astparser_makeidentconst(prs, &prs->prevtoken);
    if(nn_astparser_match(prs, NEON_ASTTOK_ASSIGN))
    {
        nn_astparser_parseexpression(prs);
    }
    else
    {
        nn_astemit_emitinstruc(prs, NEON_OP_PUSHNULL);
    }
    nn_astemit_emitbyteandshort(prs, NEON_OP_CLASSPROPERTYDEFINE, fieldconstant);
    nn_astemit_emit1byte(prs, isstatic ? 1 : 0);
    nn_astparser_consumestmtend(prs);
    nn_astparser_ignorewhitespace(prs);
}

void nn_astparser_parsefuncdecl(NeonAstParser* prs)
{
    int global;
    global = nn_astparser_parsevariable(prs, "function name expected");
    nn_astparser_markinitialized(prs);
    nn_astparser_parsefuncfull(prs, NEON_FUNCTYPE_FUNCTION, false);
    nn_astparser_definevariable(prs, global);
}

void nn_astparser_parseclassdeclaration(NeonAstParser* prs)
{
    bool isstatic;
    int nameconst;
    NeonAstCompContext oldctx;
    NeonAstToken classname;
    NeonAstClassCompiler classcompiler;
    nn_astparser_consume(prs, NEON_ASTTOK_IDENTNORMAL, "class name expected");
    nameconst = nn_astparser_makeidentconst(prs, &prs->prevtoken);
    classname = prs->prevtoken;
    nn_astparser_declarevariable(prs);
    nn_astemit_emitbyteandshort(prs, NEON_OP_MAKECLASS, nameconst);
    nn_astparser_definevariable(prs, nameconst);
    classcompiler.name = prs->prevtoken;
    classcompiler.hassuperclass = false;
    classcompiler.enclosing = prs->currentclasscompiler;
    prs->currentclasscompiler = &classcompiler;
    oldctx = prs->compcontext;
    prs->compcontext = NEON_COMPCONTEXT_CLASS;
    if(nn_astparser_match(prs, NEON_ASTTOK_LESSTHAN))
    {
        nn_astparser_consume(prs, NEON_ASTTOK_IDENTNORMAL, "name of superclass expected");
        nn_astparser_rulevarnormal(prs, false);
        if(nn_astparser_identsequal(&classname, &prs->prevtoken))
        {
            nn_astparser_raiseerror(prs, "class %.*s cannot inherit from itself", classname.length, classname.start);
        }
        nn_astparser_scopebegin(prs);
        nn_astparser_addlocal(prs, nn_astparser_synthtoken(g_strsuper));
        nn_astparser_definevariable(prs, 0);
        nn_astparser_namedvar(prs, classname, false);
        nn_astemit_emitinstruc(prs, NEON_OP_CLASSINHERIT);
        classcompiler.hassuperclass = true;
    }
    nn_astparser_namedvar(prs, classname, false);
    nn_astparser_ignorewhitespace(prs);
    nn_astparser_consume(prs, NEON_ASTTOK_BRACEOPEN, "expected '{' before class body");
    nn_astparser_ignorewhitespace(prs);
    while(!nn_astparser_check(prs, NEON_ASTTOK_BRACECLOSE) && !nn_astparser_check(prs, NEON_ASTTOK_EOF))
    {
        isstatic = false;
        if(nn_astparser_match(prs, NEON_ASTTOK_KWSTATIC))
        {
            isstatic = true;
        }

        if(nn_astparser_match(prs, NEON_ASTTOK_KWVAR))
        {
            nn_astparser_parsefield(prs, isstatic);
        }
        else
        {
            nn_astparser_parsemethod(prs, classname, isstatic);
            nn_astparser_ignorewhitespace(prs);
        }
    }
    nn_astparser_consume(prs, NEON_ASTTOK_BRACECLOSE, "expected '}' after class body");
    if(nn_astparser_match(prs, NEON_ASTTOK_SEMICOLON))
    {
    }
    nn_astemit_emitinstruc(prs, NEON_OP_POPONE);
    if(classcompiler.hassuperclass)
    {
        nn_astparser_scopeend(prs);
    }
    prs->currentclasscompiler = prs->currentclasscompiler->enclosing;
    prs->compcontext = oldctx;
}

void nn_astparser_parsevardecl(NeonAstParser* prs, bool isinitializer)
{
    int global;
    int totalparsed;
    totalparsed = 0;
    do
    {
        if(totalparsed > 0)
        {
            nn_astparser_ignorewhitespace(prs);
        }
        global = nn_astparser_parsevariable(prs, "variable name expected");
        if(nn_astparser_match(prs, NEON_ASTTOK_ASSIGN))
        {
            nn_astparser_parseexpression(prs);
        }
        else
        {
            nn_astemit_emitinstruc(prs, NEON_OP_PUSHNULL);
        }
        nn_astparser_definevariable(prs, global);
        totalparsed++;
    } while(nn_astparser_match(prs, NEON_ASTTOK_COMMA));

    if(!isinitializer)
    {
        nn_astparser_consumestmtend(prs);
    }
    else
    {
        nn_astparser_consume(prs, NEON_ASTTOK_SEMICOLON, "expected ';' after initializer");
        nn_astparser_ignorewhitespace(prs);
    }
}

void nn_astparser_parseexprstmt(NeonAstParser* prs, bool isinitializer, bool semi)
{
    if(prs->pvm->isrepl && prs->currentfunccompiler->scopedepth == 0)
    {
        prs->replcanecho = true;
    }
    if(!semi)
    {
        nn_astparser_parseexpression(prs);
    }
    else
    {
        nn_astparser_parseprecnoadvance(prs, NEON_ASTPREC_ASSIGNMENT);
    }
    if(!isinitializer)
    {
        if(prs->replcanecho && prs->pvm->isrepl)
        {
            nn_astemit_emitinstruc(prs, NEON_OP_ECHO);
            prs->replcanecho = false;
        }
        else
        {
            //if(!prs->keeplastvalue)
            {
                nn_astemit_emitinstruc(prs, NEON_OP_POPONE);
            }
        }
        nn_astparser_consumestmtend(prs);
    }
    else
    {
        nn_astparser_consume(prs, NEON_ASTTOK_SEMICOLON, "expected ';' after initializer");
        nn_astparser_ignorewhitespace(prs);
        nn_astemit_emitinstruc(prs, NEON_OP_POPONE);
    }
}

/**
 * iter statements are like for loops in c...
 * they are desugared into a while loop
 *
 * i.e.
 *
 * iter i = 0; i < 10; i++ {
 *    ...
 * }
 *
 * desugars into:
 *
 * var i = 0
 * while i < 10 {
 *    ...
 *    i = i + 1
 * }
 */
void nn_astparser_parseforstmt(NeonAstParser* prs)
{
    int exitjump;
    int bodyjump;
    int incrstart;
    int surroundingloopstart;
    int surroundingscopedepth;
    nn_astparser_scopebegin(prs);
    nn_astparser_consume(prs, NEON_ASTTOK_PARENOPEN, "expected '(' after 'for'");
    /* parse initializer... */
    if(nn_astparser_match(prs, NEON_ASTTOK_SEMICOLON))
    {
        /* no initializer */
    }
    else if(nn_astparser_match(prs, NEON_ASTTOK_KWVAR))
    {
        nn_astparser_parsevardecl(prs, true);
    }
    else
    {
        nn_astparser_parseexprstmt(prs, true, false);
    }
    /* keep a copy of the surrounding loop's start and depth */
    surroundingloopstart = prs->innermostloopstart;
    surroundingscopedepth = prs->innermostloopscopedepth;
    /* update the parser's loop start and depth to the current */
    prs->innermostloopstart = nn_astparser_currentblob(prs)->count;
    prs->innermostloopscopedepth = prs->currentfunccompiler->scopedepth;
    exitjump = -1;
    if(!nn_astparser_match(prs, NEON_ASTTOK_SEMICOLON))
    {
        /* the condition is optional */
        nn_astparser_parseexpression(prs);
        nn_astparser_consume(prs, NEON_ASTTOK_SEMICOLON, "expected ';' after condition");
        nn_astparser_ignorewhitespace(prs);
        /* jump out of the loop if the condition is false... */
        exitjump = nn_astemit_emitjump(prs, NEON_OP_JUMPIFFALSE);
        nn_astemit_emitinstruc(prs, NEON_OP_POPONE);
        /* pop the condition */
    }
    /* the iterator... */
    if(!nn_astparser_check(prs, NEON_ASTTOK_BRACEOPEN))
    {
        bodyjump = nn_astemit_emitjump(prs, NEON_OP_JUMPNOW);
        incrstart = nn_astparser_currentblob(prs)->count;
        nn_astparser_parseexpression(prs);
        nn_astparser_ignorewhitespace(prs);
        nn_astemit_emitinstruc(prs, NEON_OP_POPONE);
        nn_astemit_emitloop(prs, prs->innermostloopstart);
        prs->innermostloopstart = incrstart;
        nn_astemit_patchjump(prs, bodyjump);
    }
    nn_astparser_consume(prs, NEON_ASTTOK_PARENCLOSE, "expected ')' after 'for'");
    nn_astparser_parsestmt(prs);
    nn_astemit_emitloop(prs, prs->innermostloopstart);
    if(exitjump != -1)
    {
        nn_astemit_patchjump(prs, exitjump);
        nn_astemit_emitinstruc(prs, NEON_OP_POPONE);
    }
    nn_astparser_endloop(prs);
    /* reset the loop start and scope depth to the surrounding value */
    prs->innermostloopstart = surroundingloopstart;
    prs->innermostloopscopedepth = surroundingscopedepth;
    nn_astparser_scopeend(prs);
}

/**
 * for x in iterable {
 *    ...
 * }
 *
 * ==
 *
 * {
 *    var iterable = expression()
 *    var _
 *
 *    while _ = iterable.@itern() {
 *      var x = iterable.@iter()
 *      ...
 *    }
 * }
 *
 * ---------------------------------
 *
 * foreach x, y in iterable {
 *    ...
 * }
 *
 * ==
 *
 * {
 *    var iterable = expression()
 *    var x
 *
 *    while x = iterable.@itern() {
 *      var y = iterable.@iter()
 *      ...
 *    }
 * }
 *
 * Every iterable Object must implement the @iter(x) and the @itern(x)
 * function.
 *
 * to make instances of a user created class iterable,
 * the class must implement the @iter(x) and the @itern(x) function.
 * the @itern(x) must return the current iterating index of the object and
 * the
 * @iter(x) function must return the value at that index.
 * _NOTE_: the @iter(x) function will no longer be called after the
 * @itern(x) function returns a false value. so the @iter(x) never needs
 * to return a false value
 */
void nn_astparser_parseforeachstmt(NeonAstParser* prs)
{
    int citer;
    int citern;
    int falsejump;
    int keyslot;
    int valueslot;
    int iteratorslot;
    int surroundingloopstart;
    int surroundingscopedepth;
    NeonAstToken iteratortoken;
    NeonAstToken keytoken;
    NeonAstToken valuetoken;
    nn_astparser_scopebegin(prs);
    /* define @iter and @itern constant */
    citer = nn_astparser_pushconst(prs, nn_value_fromobject(nn_string_copycstr(prs->pvm, "@iter")));
    citern = nn_astparser_pushconst(prs, nn_value_fromobject(nn_string_copycstr(prs->pvm, "@itern")));
    nn_astparser_consume(prs, NEON_ASTTOK_PARENOPEN, "expected '(' after 'foreach'");
    nn_astparser_consume(prs, NEON_ASTTOK_IDENTNORMAL, "expected variable name after 'foreach'");
    if(!nn_astparser_check(prs, NEON_ASTTOK_COMMA))
    {
        keytoken = nn_astparser_synthtoken(" _ ");
        valuetoken = prs->prevtoken;
    }
    else
    {
        keytoken = prs->prevtoken;
        nn_astparser_consume(prs, NEON_ASTTOK_COMMA, "");
        nn_astparser_consume(prs, NEON_ASTTOK_IDENTNORMAL, "expected variable name after ','");
        valuetoken = prs->prevtoken;
    }
    nn_astparser_consume(prs, NEON_ASTTOK_KWIN, "expected 'in' after for loop variable(s)");
    nn_astparser_ignorewhitespace(prs);
    /*
    // The space in the variable name ensures it won't collide with a user-defined
    // variable.
    */
    iteratortoken = nn_astparser_synthtoken(" iterator ");
    /* Evaluate the sequence expression and store it in a hidden local variable. */
    nn_astparser_parseexpression(prs);
    nn_astparser_consume(prs, NEON_ASTTOK_PARENCLOSE, "expected ')' after 'foreach'");
    if(prs->currentfunccompiler->localcount + 3 > NEON_CFG_ASTMAXLOCALS)
    {
        nn_astparser_raiseerror(prs, "cannot declare more than %d variables in one scope", NEON_CFG_ASTMAXLOCALS);
        return;
    }
    /* add the iterator to the local scope */
    iteratorslot = nn_astparser_addlocal(prs, iteratortoken) - 1;
    nn_astparser_definevariable(prs, 0);
    /* Create the key local variable. */
    nn_astemit_emitinstruc(prs, NEON_OP_PUSHNULL);
    keyslot = nn_astparser_addlocal(prs, keytoken) - 1;
    nn_astparser_definevariable(prs, keyslot);
    /* create the local value slot */
    nn_astemit_emitinstruc(prs, NEON_OP_PUSHNULL);
    valueslot = nn_astparser_addlocal(prs, valuetoken) - 1;
    nn_astparser_definevariable(prs, 0);
    surroundingloopstart = prs->innermostloopstart;
    surroundingscopedepth = prs->innermostloopscopedepth;
    /*
    // we'll be jumping back to right before the
    // expression after the loop body
    */
    prs->innermostloopstart = nn_astparser_currentblob(prs)->count;
    prs->innermostloopscopedepth = prs->currentfunccompiler->scopedepth;
    /* key = iterable.iter_n__(key) */
    nn_astemit_emitbyteandshort(prs, NEON_OP_LOCALGET, iteratorslot);
    nn_astemit_emitbyteandshort(prs, NEON_OP_LOCALGET, keyslot);
    nn_astemit_emitbyteandshort(prs, NEON_OP_CALLMETHOD, citern);
    nn_astemit_emit1byte(prs, 1);
    nn_astemit_emitbyteandshort(prs, NEON_OP_LOCALSET, keyslot);
    falsejump = nn_astemit_emitjump(prs, NEON_OP_JUMPIFFALSE);
    nn_astemit_emitinstruc(prs, NEON_OP_POPONE);
    /* value = iterable.iter__(key) */
    nn_astemit_emitbyteandshort(prs, NEON_OP_LOCALGET, iteratorslot);
    nn_astemit_emitbyteandshort(prs, NEON_OP_LOCALGET, keyslot);
    nn_astemit_emitbyteandshort(prs, NEON_OP_CALLMETHOD, citer);
    nn_astemit_emit1byte(prs, 1);
    /*
    // Bind the loop value in its own scope. This ensures we get a fresh
    // variable each iteration so that closures for it don't all see the same one.
    */
    nn_astparser_scopebegin(prs);
    /* update the value */
    nn_astemit_emitbyteandshort(prs, NEON_OP_LOCALSET, valueslot);
    nn_astemit_emitinstruc(prs, NEON_OP_POPONE);
    nn_astparser_parsestmt(prs);
    nn_astparser_scopeend(prs);
    nn_astemit_emitloop(prs, prs->innermostloopstart);
    nn_astemit_patchjump(prs, falsejump);
    nn_astemit_emitinstruc(prs, NEON_OP_POPONE);
    nn_astparser_endloop(prs);
    prs->innermostloopstart = surroundingloopstart;
    prs->innermostloopscopedepth = surroundingscopedepth;
    nn_astparser_scopeend(prs);
}

/**
 * switch expression {
 *    case expression {
 *      ...
 *    }
 *    case expression {
 *      ...
 *    }
 *    ...
 * }
 */
void nn_astparser_parseswitchstmt(NeonAstParser* prs)
{
    int i;
    int length;
    int swstate;
    int casecount;
    int switchcode;
    int startoffset;
    int caseends[NEON_CFG_ASTMAXSWITCHCASES];
    char* str;
    NeonValue jump;
    NeonAstTokType casetype;
    NeonObjSwitch* sw;
    NeonObjString* string;
    /* the expression */
    nn_astparser_parseexpression(prs);
    nn_astparser_consume(prs, NEON_ASTTOK_BRACEOPEN, "expected '{' after 'switch' expression");
    nn_astparser_ignorewhitespace(prs);
    /* 0: before all cases, 1: before default, 2: after default */
    swstate = 0;
    casecount = 0;
    sw = nn_object_makeswitch(prs->pvm);
    nn_vm_stackpush(prs->pvm, nn_value_fromobject(sw));
    switchcode = nn_astemit_emitswitch(prs);
    /* nn_astemit_emitbyteandshort(prs, NEON_OP_SWITCH, nn_astparser_pushconst(prs, nn_value_fromobject(sw))); */
    startoffset = nn_astparser_currentblob(prs)->count;
    while(!nn_astparser_match(prs, NEON_ASTTOK_BRACECLOSE) && !nn_astparser_check(prs, NEON_ASTTOK_EOF))
    {
        if(nn_astparser_match(prs, NEON_ASTTOK_KWCASE) || nn_astparser_match(prs, NEON_ASTTOK_KWDEFAULT))
        {
            casetype = prs->prevtoken.type;
            if(swstate == 2)
            {
                nn_astparser_raiseerror(prs, "cannot have another case after a default case");
            }
            if(swstate == 1)
            {
                /* at the end of the previous case, jump over the others... */
                caseends[casecount++] = nn_astemit_emitjump(prs, NEON_OP_JUMPNOW);
            }
            if(casetype == NEON_ASTTOK_KWCASE)
            {
                swstate = 1;
                do
                {
                    nn_astparser_ignorewhitespace(prs);
                    nn_astparser_advance(prs);
                    jump = nn_value_makenumber((double)nn_astparser_currentblob(prs)->count - (double)startoffset);
                    if(prs->prevtoken.type == NEON_ASTTOK_KWTRUE)
                    {
                        nn_table_set(sw->table, nn_value_makebool(true), jump);
                    }
                    else if(prs->prevtoken.type == NEON_ASTTOK_KWFALSE)
                    {
                        nn_table_set(sw->table, nn_value_makebool(false), jump);
                    }
                    else if(prs->prevtoken.type == NEON_ASTTOK_LITERAL)
                    {
                        str = nn_astparser_compilestring(prs, &length);
                        string = nn_string_takelen(prs->pvm, str, length);
                        /* gc fix */
                        nn_vm_stackpush(prs->pvm, nn_value_fromobject(string));
                        nn_table_set(sw->table, nn_value_fromobject(string), jump);
                        /* gc fix */
                        nn_vm_stackpop(prs->pvm);
                    }
                    else if(nn_astparser_checknumber(prs))
                    {
                        nn_table_set(sw->table, nn_astparser_compilenumber(prs), jump);
                    }
                    else
                    {
                        /* pop the switch */
                        nn_vm_stackpop(prs->pvm);
                        nn_astparser_raiseerror(prs, "only constants can be used in 'when' expressions");
                        return;
                    }
                } while(nn_astparser_match(prs, NEON_ASTTOK_COMMA));
            }
            else
            {
                swstate = 2;
                sw->defaultjump = nn_astparser_currentblob(prs)->count - startoffset;
            }
        }
        else
        {
            /* otherwise, it's a statement inside the current case */
            if(swstate == 0)
            {
                nn_astparser_raiseerror(prs, "cannot have statements before any case");
            }
            nn_astparser_parsestmt(prs);
        }
    }
    /* if we ended without a default case, patch its condition jump */
    if(swstate == 1)
    {
        caseends[casecount++] = nn_astemit_emitjump(prs, NEON_OP_JUMPNOW);
    }
    /* patch all the case jumps to the end */
    for(i = 0; i < casecount; i++)
    {
        nn_astemit_patchjump(prs, caseends[i]);
    }
    sw->exitjump = nn_astparser_currentblob(prs)->count - startoffset;
    nn_astemit_patchswitch(prs, switchcode, nn_astparser_pushconst(prs, nn_value_fromobject(sw)));
    /* pop the switch */
    nn_vm_stackpop(prs->pvm);
}

void nn_astparser_parseifstmt(NeonAstParser* prs)
{
    int elsejump;
    int thenjump;
    nn_astparser_parseexpression(prs);
    thenjump = nn_astemit_emitjump(prs, NEON_OP_JUMPIFFALSE);
    nn_astemit_emitinstruc(prs, NEON_OP_POPONE);
    nn_astparser_parsestmt(prs);
    elsejump = nn_astemit_emitjump(prs, NEON_OP_JUMPNOW);
    nn_astemit_patchjump(prs, thenjump);
    nn_astemit_emitinstruc(prs, NEON_OP_POPONE);
    if(nn_astparser_match(prs, NEON_ASTTOK_KWELSE))
    {
        nn_astparser_parsestmt(prs);
    }
    nn_astemit_patchjump(prs, elsejump);
}

void nn_astparser_parseechostmt(NeonAstParser* prs)
{
    nn_astparser_parseexpression(prs);
    nn_astemit_emitinstruc(prs, NEON_OP_ECHO);
    nn_astparser_consumestmtend(prs);
}

void nn_astparser_parsethrowstmt(NeonAstParser* prs)
{
    nn_astparser_parseexpression(prs);
    nn_astemit_emitinstruc(prs, NEON_OP_EXTHROW);
    nn_astparser_discardlocals(prs, prs->currentfunccompiler->scopedepth - 1);
    nn_astparser_consumestmtend(prs);
}

void nn_astparser_parseassertstmt(NeonAstParser* prs)
{
    nn_astparser_consume(prs, NEON_ASTTOK_PARENOPEN, "expected '(' after 'assert'");
    nn_astparser_parseexpression(prs);
    if(nn_astparser_match(prs, NEON_ASTTOK_COMMA))
    {
        nn_astparser_ignorewhitespace(prs);
        nn_astparser_parseexpression(prs);
    }
    else
    {
        nn_astemit_emitinstruc(prs, NEON_OP_PUSHNULL);
    }
    nn_astemit_emitinstruc(prs, NEON_OP_ASSERT);
    nn_astparser_consume(prs, NEON_ASTTOK_PARENCLOSE, "expected ')' after 'assert'");
    nn_astparser_consumestmtend(prs);
}

void nn_astparser_parsetrystmt(NeonAstParser* prs)
{
    int address;
    int type;
    int finally;
    int trybegins;
    int exitjump;
    int continueexecutionaddress;
    bool catchexists;
    bool finalexists;
    if(prs->currentfunccompiler->handlercount == NEON_CFG_MAXEXCEPTHANDLERS)
    {
        nn_astparser_raiseerror(prs, "maximum exception handler in scope exceeded");
    }
    prs->currentfunccompiler->handlercount++;
    prs->istrying = true;
    nn_astparser_ignorewhitespace(prs);
    trybegins = nn_astemit_emittry(prs);
    /* compile the try body */
    nn_astparser_parsestmt(prs);
    nn_astemit_emitinstruc(prs, NEON_OP_EXPOPTRY);
    exitjump = nn_astemit_emitjump(prs, NEON_OP_JUMPNOW);
    prs->istrying = false;
    /*
    // we can safely use 0 because a program cannot start with a
    // catch or finally block
    */
    address = 0;
    type = -1;
    finally = 0;
    catchexists = false;
    finalexists= false;
    /* catch body must maintain its own scope */
    if(nn_astparser_match(prs, NEON_ASTTOK_KWCATCH))
    {
        catchexists = true;
        nn_astparser_scopebegin(prs);
        nn_astparser_consume(prs, NEON_ASTTOK_PARENOPEN, "expected '(' after 'catch'");
        nn_astparser_consume(prs, NEON_ASTTOK_IDENTNORMAL, "missing exception class name");
        type = nn_astparser_makeidentconst(prs, &prs->prevtoken);
        address = nn_astparser_currentblob(prs)->count;
        if(nn_astparser_match(prs, NEON_ASTTOK_IDENTNORMAL))
        {
            nn_astparser_createdvar(prs, prs->prevtoken);
        }
        else
        {
            nn_astemit_emitinstruc(prs, NEON_OP_POPONE);
        }
          nn_astparser_consume(prs, NEON_ASTTOK_PARENCLOSE, "expected ')' after 'catch'");
        nn_astemit_emitinstruc(prs, NEON_OP_EXPOPTRY);
        nn_astparser_ignorewhitespace(prs);
        nn_astparser_parsestmt(prs);
        nn_astparser_scopeend(prs);
    }
    else
    {
        type = nn_astparser_pushconst(prs, nn_value_fromobject(nn_string_copycstr(prs->pvm, "Exception")));
    }
    nn_astemit_patchjump(prs, exitjump);
    if(nn_astparser_match(prs, NEON_ASTTOK_KWFINALLY))
    {
        finalexists = true;
        /*
        // if we arrived here from either the try or handler block,
        // we don't want to continue propagating the exception
        */
        nn_astemit_emitinstruc(prs, NEON_OP_PUSHFALSE);
        finally = nn_astparser_currentblob(prs)->count;
        nn_astparser_ignorewhitespace(prs);
        nn_astparser_parsestmt(prs);
        continueexecutionaddress = nn_astemit_emitjump(prs, NEON_OP_JUMPIFFALSE);
        /* pop the bool off the stack */
        nn_astemit_emitinstruc(prs, NEON_OP_POPONE);
        nn_astemit_emitinstruc(prs, NEON_OP_EXPUBLISHTRY);
        nn_astemit_patchjump(prs, continueexecutionaddress);
        nn_astemit_emitinstruc(prs, NEON_OP_POPONE);
    }
    if(!finalexists && !catchexists)
    {
        nn_astparser_raiseerror(prs, "try block must contain at least one of catch or finally");
    }
    nn_astemit_patchtry(prs, trybegins, type, address, finally);
}

void nn_astparser_parsereturnstmt(NeonAstParser* prs)
{
    prs->isreturning = true;
    /*
    if(prs->currentfunccompiler->type == NEON_FUNCTYPE_SCRIPT)
    {
        nn_astparser_raiseerror(prs, "cannot return from top-level code");
    }
    */
    if(nn_astparser_match(prs, NEON_ASTTOK_SEMICOLON) || nn_astparser_match(prs, NEON_ASTTOK_NEWLINE))
    {
        nn_astemit_emitreturn(prs);
    }
    else
    {
        if(prs->currentfunccompiler->type == NEON_FUNCTYPE_INITIALIZER)
        {
            nn_astparser_raiseerror(prs, "cannot return value from constructor");
        }
        if(prs->istrying)
        {
            nn_astemit_emitinstruc(prs, NEON_OP_EXPOPTRY);
        }
        nn_astparser_parseexpression(prs);
        nn_astemit_emitinstruc(prs, NEON_OP_RETURN);
        nn_astparser_consumestmtend(prs);
    }
    prs->isreturning = false;
}

void nn_astparser_parsewhilestmt(NeonAstParser* prs)
{
    int exitjump;
    int surroundingloopstart;
    int surroundingscopedepth;
    surroundingloopstart = prs->innermostloopstart;
    surroundingscopedepth = prs->innermostloopscopedepth;
    /*
    // we'll be jumping back to right before the
    // expression after the loop body
    */
    prs->innermostloopstart = nn_astparser_currentblob(prs)->count;
    prs->innermostloopscopedepth = prs->currentfunccompiler->scopedepth;
    nn_astparser_parseexpression(prs);
    exitjump = nn_astemit_emitjump(prs, NEON_OP_JUMPIFFALSE);
    nn_astemit_emitinstruc(prs, NEON_OP_POPONE);
    nn_astparser_parsestmt(prs);
    nn_astemit_emitloop(prs, prs->innermostloopstart);
    nn_astemit_patchjump(prs, exitjump);
    nn_astemit_emitinstruc(prs, NEON_OP_POPONE);
    nn_astparser_endloop(prs);
    prs->innermostloopstart = surroundingloopstart;
    prs->innermostloopscopedepth = surroundingscopedepth;
}

void nn_astparser_parsedo_whilestmt(NeonAstParser* prs)
{
    int exitjump;
    int surroundingloopstart;
    int surroundingscopedepth;
    surroundingloopstart = prs->innermostloopstart;
    surroundingscopedepth = prs->innermostloopscopedepth;
    /*
    // we'll be jumping back to right before the
    // statements after the loop body
    */
    prs->innermostloopstart = nn_astparser_currentblob(prs)->count;
    prs->innermostloopscopedepth = prs->currentfunccompiler->scopedepth;
    nn_astparser_parsestmt(prs);
    nn_astparser_consume(prs, NEON_ASTTOK_KWWHILE, "expecting 'while' statement");
    nn_astparser_parseexpression(prs);
    exitjump = nn_astemit_emitjump(prs, NEON_OP_JUMPIFFALSE);
    nn_astemit_emitinstruc(prs, NEON_OP_POPONE);
    nn_astemit_emitloop(prs, prs->innermostloopstart);
    nn_astemit_patchjump(prs, exitjump);
    nn_astemit_emitinstruc(prs, NEON_OP_POPONE);
    nn_astparser_endloop(prs);
    prs->innermostloopstart = surroundingloopstart;
    prs->innermostloopscopedepth = surroundingscopedepth;
}

void nn_astparser_parsecontinuestmt(NeonAstParser* prs)
{
    if(prs->innermostloopstart == -1)
    {
        nn_astparser_raiseerror(prs, "'continue' can only be used in a loop");
    }
    /*
    // discard local variables created in the loop
    //  discard_local(prs, prs->innermostloopscopedepth);
    */
    nn_astparser_discardlocals(prs, prs->innermostloopscopedepth + 1);
    /* go back to the top of the loop */
    nn_astemit_emitloop(prs, prs->innermostloopstart);
    nn_astparser_consumestmtend(prs);
}

void nn_astparser_parsebreakstmt(NeonAstParser* prs)
{
    if(prs->innermostloopstart == -1)
    {
        nn_astparser_raiseerror(prs, "'break' can only be used in a loop");
    }
    /* discard local variables created in the loop */
    /*
    int i;
    for(i = prs->currentfunccompiler->localcount - 1; i >= 0 && prs->currentfunccompiler->locals[i].depth >= prs->currentfunccompiler->scopedepth; i--)
    {
        if (prs->currentfunccompiler->locals[i].iscaptured)
        {
            nn_astemit_emitinstruc(prs, NEON_OP_UPVALUECLOSE);
        }
        else
        {
            nn_astemit_emitinstruc(prs, NEON_OP_POPONE);
        }
    }
    */
    nn_astparser_discardlocals(prs, prs->innermostloopscopedepth + 1);
    nn_astemit_emitjump(prs, NEON_OP_BREAK_PL);
    nn_astparser_consumestmtend(prs);
}

void nn_astparser_synchronize(NeonAstParser* prs)
{
    prs->panicmode = false;
    while(prs->currtoken.type != NEON_ASTTOK_EOF)
    {
        if(prs->currtoken.type == NEON_ASTTOK_NEWLINE || prs->currtoken.type == NEON_ASTTOK_SEMICOLON)
        {
            return;
        }
        switch(prs->currtoken.type)
        {
            case NEON_ASTTOK_KWCLASS:
            case NEON_ASTTOK_KWFUNCTION:
            case NEON_ASTTOK_KWVAR:
            case NEON_ASTTOK_KWFOREACH:
            case NEON_ASTTOK_KWIF:
            case NEON_ASTTOK_KWSWITCH:
            case NEON_ASTTOK_KWCASE:
            case NEON_ASTTOK_KWFOR:
            case NEON_ASTTOK_KWDO:
            case NEON_ASTTOK_KWWHILE:
            case NEON_ASTTOK_KWECHO:
            case NEON_ASTTOK_KWASSERT:
            case NEON_ASTTOK_KWTRY:
            case NEON_ASTTOK_KWCATCH:
            case NEON_ASTTOK_KWTHROW:
            case NEON_ASTTOK_KWRETURN:
            case NEON_ASTTOK_KWSTATIC:
            case NEON_ASTTOK_KWTHIS:
            case NEON_ASTTOK_KWSUPER:
            case NEON_ASTTOK_KWFINALLY:
            case NEON_ASTTOK_KWIN:
            case NEON_ASTTOK_KWIMPORT:
            case NEON_ASTTOK_KWAS:
                return;
            default:
                /* do nothing */
            ;
        }
        nn_astparser_advance(prs);
    }
}

/*
* $keeplast: whether to emit code that retains or discards the value of the last statement/expression.
* SHOULD NOT BE USED FOR ORDINARY SCRIPTS as it will almost definitely result in the stack containing invalid values.
*/
NeonObjFuncScript* nn_astparser_compilesource(NeonState* state, NeonObjModule* module, const char* source, NeonBlob* blob, bool fromimport, bool keeplast)
{
    NeonAstFuncCompiler compiler;
    NeonAstLexer* lexer;
    NeonAstParser* parser;
    NeonObjFuncScript* function;
    (void)blob;
    NEON_ASTDEBUG(state, "module=%p source=[...] blob=[...] fromimport=%d keeplast=%d", module, fromimport, keeplast);
    lexer = nn_astlex_init(state, source);
    parser = nn_astparser_make(state, lexer, module, keeplast);
    nn_astfunccompiler_init(parser, &compiler, NEON_FUNCTYPE_SCRIPT, true);
    compiler.fromimport = fromimport;
    nn_astparser_runparser(parser);
    function = nn_astparser_endcompiler(parser);
    if(parser->haderror)
    {
        function = NULL;
    }
    nn_astlex_destroy(state, lexer);
    nn_astparser_destroy(state, parser);
    return function;
}

void nn_gcmem_markcompilerroots(NeonState* state)
{
    /*
    NeonAstFuncCompiler* compiler;
    compiler = state->compiler;
    while(compiler != NULL)
    {
        nn_gcmem_markobject(state, (NeonObject*)compiler->targetfunc);
        compiler = compiler->enclosing;
    }
    */
}

NeonRegModule* nn_natmodule_load_null(NeonState* state)
{
    (void)state;
    static NeonRegFunc modfuncs[] =
    {
        /* {"somefunc",   true,  myfancymodulefunction},*/
        {NULL, false, NULL},
    };

    static NeonRegField modfields[] =
    {
        /*{"somefield", true, the_function_that_gets_called},*/
        {NULL, false, NULL},
    };

    static NeonRegModule module;
    module.name = "null";
    module.fields = modfields;
    module.functions = modfuncs;
    module.classes = NULL;
    module.preloader= NULL;
    module.unloader = NULL;
    return &module;
}

void nn_modfn_os_preloader(NeonState* state)
{
    (void)state;
}

NeonValue nn_modfn_os_readdir(NeonState* state, NeonArguments* args)
{
    const char* dirn;
    FSDirReader rd;
    FSDirItem itm;
    NeonObjString* os;
    NeonObjString* aval;
    NeonObjArray* res;
    NeonArgCheck check;
    nn_argcheck_init(state, &check, args);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
    os = nn_value_asstring(args->args[0]);
    dirn = os->sbuf->data;
    if(fslib_diropen(&rd, dirn))
    {
        res = nn_array_make(state);
        while(fslib_dirread(&rd, &itm))
        {
            aval = nn_string_copycstr(state, itm.name);
            nn_array_push(res, nn_value_fromobject(aval));
        }
        fslib_dirclose(&rd);
        return nn_value_fromobject(res);
    }
    else
    {
        nn_exceptions_throw(state, "cannot open directory '%s'", dirn);
    }
    return nn_value_makeempty();
}

NeonRegModule* nn_natmodule_load_os(NeonState* state)
{
    (void)state;
    static NeonRegFunc modfuncs[] =
    {
        {"readdir",   true,  nn_modfn_os_readdir},
        {NULL,     false, NULL},
    };
    static NeonRegField modfields[] =
    {
        /*{"platform", true, get_os_platform},*/
        {NULL,       false, NULL},
    };
    static NeonRegModule module;
    module.name = "os";
    module.fields = modfields;
    module.functions = modfuncs;
    module.classes = NULL;
    module.preloader= &nn_modfn_os_preloader;
    module.unloader = NULL;
    return &module;
}

NeonValue nn_modfn_astscan_scan(NeonState* state, NeonArguments* args)
{
    const char* cstr;
    NeonObjString* insrc;
    NeonAstLexer* scn;
    NeonObjArray* arr;
    NeonObjDict* itm;
    NeonAstToken token;
    NeonArgCheck check;
    nn_argcheck_init(state, &check, args);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
    insrc = nn_value_asstring(args->args[0]);
    scn = nn_astlex_init(state, insrc->sbuf->data);
    arr = nn_array_make(state);
    while(!nn_astlex_isatend(scn))
    {
        itm = nn_object_makedict(state);
        token = nn_astlex_scantoken(scn);
        nn_dict_addentrycstr(itm, "line", nn_value_makenumber(token.line));
        cstr = nn_astutil_toktype2str(token.type);
        nn_dict_addentrycstr(itm, "type", nn_value_fromobject(nn_string_copycstr(state, cstr + 12)));
        nn_dict_addentrycstr(itm, "source", nn_value_fromobject(nn_string_copylen(state, token.start, token.length)));
        nn_array_push(arr, nn_value_fromobject(itm));
    }
    nn_astlex_destroy(state, scn);
    return nn_value_fromobject(arr);
}

NeonRegModule* nn_natmodule_load_astscan(NeonState* state)
{
    NeonRegModule* ret;
    (void)state;
    static NeonRegFunc modfuncs[] =
    {
        {"scan",   true,  nn_modfn_astscan_scan},
        {NULL,     false, NULL},
    };
    static NeonRegField modfields[] =
    {
        {NULL,       false, NULL},
    };
    static NeonRegModule module;
    module.name = "astscan";
    module.fields = modfields;
    module.functions = modfuncs;
    module.classes = NULL;
    module.preloader= NULL;
    module.unloader = NULL;
    ret = &module;
    return ret;
}

NeonModInitFN g_builtinmodules[] =
{
    nn_natmodule_load_null,
    nn_natmodule_load_os,
    nn_natmodule_load_astscan,
    NULL,
};

bool nn_import_loadnativemodule(NeonState* state, NeonModInitFN init_fn, char* importname, const char* source, WrapDL* dlw)
{
    int j;
    int k;
    NeonValue v;
    NeonValue fieldname;
    NeonValue funcname;
    NeonValue funcrealvalue;
    NeonRegFunc func;
    NeonRegField field;
    NeonRegModule* module;
    NeonObjModule* themodule;
    NeonRegClass klassreg;
    NeonObjString* classname;
    NeonObjFuncNative* native;
    NeonObjClass* klass;
    NeonHashTable* tabdest;
    module = init_fn(state);
    if(module != NULL)
    {
        themodule = (NeonObjModule*)nn_gcmem_protect(state, (NeonObject*)nn_module_make(state, (char*)module->name, source, false));
        themodule->preloader = (void*)module->preloader;
        themodule->unloader = (void*)module->unloader;
        if(module->fields != NULL)
        {
            for(j = 0; module->fields[j].name != NULL; j++)
            {
                field = module->fields[j];
                fieldname = nn_value_fromobject(nn_gcmem_protect(state, (NeonObject*)nn_string_copycstr(state, field.name)));
                v = field.fieldvalfn(state);
                nn_vm_stackpush(state, v);
                nn_table_set(themodule->deftable, fieldname, v);
                nn_vm_stackpop(state);
            }
        }
        if(module->functions != NULL)
        {
            for(j = 0; module->functions[j].name != NULL; j++)
            {
                func = module->functions[j];
                funcname = nn_value_fromobject(nn_gcmem_protect(state, (NeonObject*)nn_string_copycstr(state, func.name)));
                funcrealvalue = nn_value_fromobject(nn_gcmem_protect(state, (NeonObject*)nn_object_makefuncnative(state, func.function, func.name, NULL)));
                nn_vm_stackpush(state, funcrealvalue);
                nn_table_set(themodule->deftable, funcname, funcrealvalue);
                nn_vm_stackpop(state);
            }
        }
        if(module->classes != NULL)
        {
            for(j = 0; module->classes[j].name != NULL; j++)
            {
                klassreg = module->classes[j];
                classname = (NeonObjString*)nn_gcmem_protect(state, (NeonObject*)nn_string_copycstr(state, klassreg.name));
                klass = (NeonObjClass*)nn_gcmem_protect(state, (NeonObject*)nn_object_makeclass(state, classname));
                if(klassreg.functions != NULL)
                {
                    for(k = 0; klassreg.functions[k].name != NULL; k++)
                    {
                        func = klassreg.functions[k];
                        funcname = nn_value_fromobject(nn_gcmem_protect(state, (NeonObject*)nn_string_copycstr(state, func.name)));
                        native = (NeonObjFuncNative*)nn_gcmem_protect(state, (NeonObject*)nn_object_makefuncnative(state, func.function, func.name, NULL));
                        if(func.isstatic)
                        {
                            native->type = NEON_FUNCTYPE_STATIC;
                        }
                        else if(strlen(func.name) > 0 && func.name[0] == '_')
                        {
                            native->type = NEON_FUNCTYPE_PRIVATE;
                        }
                        nn_table_set(klass->methods, funcname, nn_value_fromobject(native));
                    }
                }
                if(klassreg.fields != NULL)
                {
                    for(k = 0; klassreg.fields[k].name != NULL; k++)
                    {
                        field = klassreg.fields[k];
                        fieldname = nn_value_fromobject(nn_gcmem_protect(state, (NeonObject*)nn_string_copycstr(state, field.name)));
                        v = field.fieldvalfn(state);
                        nn_vm_stackpush(state, v);
                        tabdest = klass->instprops;
                        if(field.isstatic)
                        {
                            tabdest = klass->staticproperties;
                        }
                        nn_table_set(tabdest, fieldname, v);
                        nn_vm_stackpop(state);
                    }
                }
                nn_table_set(themodule->deftable, nn_value_fromobject(classname), nn_value_fromobject(klass));
            }
        }
        if(dlw != NULL)
        {
            themodule->handle = dlw;
        }
        nn_import_addnativemodule(state, themodule, themodule->name->sbuf->data);
        nn_gcmem_clearprotect(state);
        return true;
    }
    else
    {
        nn_state_warn(state, "Error loading module: %s\n", importname);
    }
    return false;
}

void nn_import_addnativemodule(NeonState* state, NeonObjModule* module, const char* as)
{
    NeonValue name;
    if(as != NULL)
    {
        module->name = nn_string_copycstr(state, as);
    }
    name = nn_value_fromobject(nn_string_copyobjstr(state, module->name));
    nn_vm_stackpush(state, name);
    nn_vm_stackpush(state, nn_value_fromobject(module));
    nn_table_set(state->modules, name, nn_value_fromobject(module));
    nn_vm_stackpopn(state, 2);
}

void nn_import_loadbuiltinmodules(NeonState* state)
{
    int i;
    for(i = 0; g_builtinmodules[i] != NULL; i++)
    {
        nn_import_loadnativemodule(state, g_builtinmodules[i], NULL, "<__native__>", NULL);
    }
}

void nn_import_closemodule(WrapDL* dlw)
{
    dlwrap_dlclose(dlw);
}


bool nn_util_fsfileexists(NeonState* state, const char* filepath)
{
    (void)state;
    #if !defined(NEON_PLAT_ISWINDOWS)
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

bool nn_util_fsfileisfile(NeonState* state, const char* filepath)
{
    (void)state;
    (void)filepath;
    return false;
}

bool nn_util_fsfileisdirectory(NeonState* state, const char* filepath)
{
    (void)state;
    (void)filepath;
    return false;
}

NeonObjModule* nn_import_loadmodulescript(NeonState* state, NeonObjModule* intomodule, NeonObjString* modulename)
{
    int argc;
    size_t fsz;
    char* source;
    char* physpath;
    NeonBlob blob;
    NeonValue retv;
    NeonValue callable;
    NeonProperty* field;
    NeonObjArray* args;
    NeonObjString* os;
    NeonObjModule* module;
    NeonObjFuncClosure* closure;
    NeonObjFuncScript* function;
    (void)os;
    (void)argc;
    (void)intomodule;
    field = nn_table_getfieldbyostr(state->modules, modulename);
    if(field != NULL)
    {
        return nn_value_asmodule(field->value);
    }
    physpath = nn_import_resolvepath(state, modulename->sbuf->data, intomodule->physicalpath->sbuf->data, NULL, false);
    if(physpath == NULL)
    {
        nn_exceptions_throw(state, "module not found: '%s'\n", modulename->sbuf->data);
        return NULL;
    }
    fprintf(stderr, "loading module from '%s'\n", physpath);
    source = nn_util_readfile(state, physpath, &fsz);
    if(source == NULL)
    {
        nn_exceptions_throw(state, "could not read import file %s", physpath);
        return NULL;
    }
    nn_blob_init(state, &blob);
    module = nn_module_make(state, modulename->sbuf->data, physpath, true);
    nn_util_memfree(state, physpath);
    function = nn_astparser_compilesource(state, module, source, &blob, true, false);
    nn_util_memfree(state, source);
    closure = nn_object_makefuncclosure(state, function);
    callable = nn_value_fromobject(closure);
    args = nn_object_makearray(state);
    argc = nn_nestcall_prepare(state, callable, nn_value_makenull(), args);
    if(!nn_nestcall_callfunction(state, callable, nn_value_makenull(), args, &retv))
    {
        nn_blob_destroy(state, &blob);
        nn_exceptions_throw(state, "failed to call compiled import closure");
        return NULL;
    }
    nn_blob_destroy(state, &blob);
    return module;
}

char* nn_import_resolvepath(NeonState* state, char* modulename, const char* currentfile, char* rootfile, bool isrelative)
{
    size_t i;
    size_t mlen;
    size_t splen;
    char* path1;
    char* path2;
    char* retme;
    const char* cstrpath;
    struct stat stroot;
    struct stat stmod;
    NeonObjString* pitem;
    StringBuffer* pathbuf;
    (void)rootfile;
    (void)isrelative;
    (void)stroot;
    (void)stmod;
    pathbuf = NULL;
    mlen = strlen(modulename);
    splen = state->importpath->count;
    for(i=0; i<splen; i++)
    {
        pitem = nn_value_asstring(state->importpath->values[i]);
        if(pathbuf == NULL)
        {
            pathbuf = dyn_strbuf_makeempty(pitem->sbuf->length + mlen + 5);
        }
        else
        {
            dyn_strbuf_reset(pathbuf);
        }
        dyn_strbuf_appendstrn(pathbuf, pitem->sbuf->data, pitem->sbuf->length);
        if(dyn_strbuf_containschar(pathbuf, '@'))
        {
            dyn_strbuf_charreplace(pathbuf, '@', modulename, mlen);
        }
        else
        {
            dyn_strbuf_appendstr(pathbuf, "/");
            dyn_strbuf_appendstr(pathbuf, modulename);
            dyn_strbuf_appendstr(pathbuf, NEON_CFG_FILEEXT);
        }
        cstrpath = pathbuf->data; 
        fprintf(stderr, "import: trying '%s' ... ", cstrpath);
        if(nn_util_fsfileexists(state, cstrpath))
        {
            fprintf(stderr, "found!\n");
            /* stop a core library from importing itself */
            #if 0
            if(stat(currentfile, &stroot) == -1)
            {
                fprintf(stderr, "resolvepath: failed to stat current file '%s'\n", currentfile);
                return NULL;
            }
            if(stat(cstrpath, &stmod) == -1)
            {
                fprintf(stderr, "resolvepath: failed to stat module file '%s'\n", cstrpath);
                return NULL;
            }
            if(stroot.st_ino == stmod.st_ino)
            {
                fprintf(stderr, "resolvepath: refusing to import itself\n");
                return NULL;
            }
            #endif
            path1 = osfn_realpath(cstrpath, NULL);
            path2 = osfn_realpath(currentfile, NULL);
            if(path1 != NULL && path2 != NULL)
            {
                if(memcmp(path1, path2, (int)strlen(path2)) == 0)
                {
                    nn_util_memfree(state, path1);
                    nn_util_memfree(state, path2);
                    fprintf(stderr, "resolvepath: refusing to import itself\n");
                    return NULL;
                }
                nn_util_memfree(state, path2);
                dyn_strbuf_destroy(pathbuf);
                pathbuf = NULL;
                retme = nn_util_strdup(state, path1);
                nn_util_memfree(state, path1);
                return retme;
            }
        }
        else
        {
            fprintf(stderr, "does not exist\n");
        }
    }
    if(pathbuf != NULL)
    {
        dyn_strbuf_destroy(pathbuf);
    }
    return NULL;
}

char* nn_util_fsgetbasename(NeonState* state, char* path)
{
    (void)state;
    return osfn_basename(path);
}

#define ENFORCE_VALID_DICT_KEY(chp, index) \
    NEON_ARGS_REJECTTYPE(chp, nn_value_isarray, index); \
    NEON_ARGS_REJECTTYPE(chp, nn_value_isdict, index); \
    NEON_ARGS_REJECTTYPE(chp, nn_value_isfile, index);

NeonValue nn_memberfunc_dict_length(NeonState* state, NeonArguments* args)
{
    NeonArgCheck check;
    nn_argcheck_init(state, &check, args);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    return nn_value_makenumber(nn_value_asdict(args->thisval)->names->count);
}

NeonValue nn_memberfunc_dict_add(NeonState* state, NeonArguments* args)
{
    NeonValue tempvalue;
    NeonObjDict* dict;
    NeonArgCheck check;
    nn_argcheck_init(state, &check, args);
    NEON_ARGS_CHECKCOUNT(&check, 2);
    ENFORCE_VALID_DICT_KEY(&check, 0);
    dict = nn_value_asdict(args->thisval);
    if(nn_table_get(dict->htab, args->args[0], &tempvalue))
    {
        NEON_RETURNERROR("duplicate key %s at add()", nn_value_tostring(state, args->args[0])->sbuf->data);
    }
    nn_dict_addentry(dict, args->args[0], args->args[1]);
    return nn_value_makeempty();
}

NeonValue nn_memberfunc_dict_set(NeonState* state, NeonArguments* args)
{
    NeonValue value;
    NeonObjDict* dict;
    NeonArgCheck check;
    nn_argcheck_init(state, &check, args);
    NEON_ARGS_CHECKCOUNT(&check, 2);
    ENFORCE_VALID_DICT_KEY(&check, 0);
    dict = nn_value_asdict(args->thisval);
    if(!nn_table_get(dict->htab, args->args[0], &value))
    {
        nn_dict_addentry(dict, args->args[0], args->args[1]);
    }
    else
    {
        nn_dict_setentry(dict, args->args[0], args->args[1]);
    }
    return nn_value_makeempty();
}

NeonValue nn_memberfunc_dict_clear(NeonState* state, NeonArguments* args)
{
    NeonObjDict* dict;
    NeonArgCheck check;
    nn_argcheck_init(state, &check, args);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    dict = nn_value_asdict(args->thisval);
    nn_valarray_destroy(dict->names);
    nn_table_destroy(dict->htab);
    return nn_value_makeempty();
}

NeonValue nn_memberfunc_dict_clone(NeonState* state, NeonArguments* args)
{
    int i;
    NeonObjDict* dict;
    NeonObjDict* newdict;
    NeonArgCheck check;
    nn_argcheck_init(state, &check, args);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    dict = nn_value_asdict(args->thisval);
    newdict = (NeonObjDict*)nn_gcmem_protect(state, (NeonObject*)nn_object_makedict(state));
    nn_table_addall(dict->htab, newdict->htab);
    for(i = 0; i < dict->names->count; i++)
    {
        nn_valarray_push(newdict->names, dict->names->values[i]);
    }
    return nn_value_fromobject(newdict);
}

NeonValue nn_memberfunc_dict_compact(NeonState* state, NeonArguments* args)
{
    int i;
    NeonObjDict* dict;
    NeonObjDict* newdict;
    NeonValue tmpvalue;
    NeonArgCheck check;
    nn_argcheck_init(state, &check, args);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    dict = nn_value_asdict(args->thisval);
    newdict = (NeonObjDict*)nn_gcmem_protect(state, (NeonObject*)nn_object_makedict(state));
    for(i = 0; i < dict->names->count; i++)
    {
        nn_table_get(dict->htab, dict->names->values[i], &tmpvalue);
        if(!nn_value_compare(state, tmpvalue, nn_value_makenull()))
        {
            nn_dict_addentry(newdict, dict->names->values[i], tmpvalue);
        }
    }
    return nn_value_fromobject(newdict);
}

NeonValue nn_memberfunc_dict_contains(NeonState* state, NeonArguments* args)
{
    NeonValue value;
    NeonObjDict* dict;
    NeonArgCheck check;
    nn_argcheck_init(state, &check, args);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    ENFORCE_VALID_DICT_KEY(&check, 0);
    dict = nn_value_asdict(args->thisval);
    return nn_value_makebool(nn_table_get(dict->htab, args->args[0], &value));
}

NeonValue nn_memberfunc_dict_extend(NeonState* state, NeonArguments* args)
{
    int i;
    NeonValue tmp;
    NeonObjDict* dict;
    NeonObjDict* dictcpy;
    NeonArgCheck check;
    nn_argcheck_init(state, &check, args);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isdict);
    dict = nn_value_asdict(args->thisval);
    dictcpy = nn_value_asdict(args->args[0]);
    for(i = 0; i < dictcpy->names->count; i++)
    {
        if(!nn_table_get(dict->htab, dictcpy->names->values[i], &tmp))
        {
            nn_valarray_push(dict->names, dictcpy->names->values[i]);
        }
    }
    nn_table_addall(dictcpy->htab, dict->htab);
    return nn_value_makeempty();
}

NeonValue nn_memberfunc_dict_get(NeonState* state, NeonArguments* args)
{
    NeonObjDict* dict;
    NeonProperty* field;
    NeonArgCheck check;
    nn_argcheck_init(state, &check, args);
    NEON_ARGS_CHECKCOUNTRANGE(&check, 1, 2);
    ENFORCE_VALID_DICT_KEY(&check, 0);
    dict = nn_value_asdict(args->thisval);
    field = nn_dict_getentry(dict, args->args[0]);
    if(field == NULL)
    {
        if(args->count == 1)
        {
            return nn_value_makenull();
        }
        else
        {
            return args->args[1];
        }
    }
    return field->value;
}

NeonValue nn_memberfunc_dict_keys(NeonState* state, NeonArguments* args)
{
    int i;
    NeonObjDict* dict;
    NeonObjArray* list;
    NeonArgCheck check;
    nn_argcheck_init(state, &check, args);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    dict = nn_value_asdict(args->thisval);
    list = (NeonObjArray*)nn_gcmem_protect(state, (NeonObject*)nn_object_makearray(state));
    for(i = 0; i < dict->names->count; i++)
    {
        nn_array_push(list, dict->names->values[i]);
    }
    return nn_value_fromobject(list);
}

NeonValue nn_memberfunc_dict_values(NeonState* state, NeonArguments* args)
{
    int i;
    NeonObjDict* dict;
    NeonObjArray* list;
    NeonProperty* field;
    NeonArgCheck check;
    nn_argcheck_init(state, &check, args);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    dict = nn_value_asdict(args->thisval);
    list = (NeonObjArray*)nn_gcmem_protect(state, (NeonObject*)nn_object_makearray(state));
    for(i = 0; i < dict->names->count; i++)
    {
        field = nn_dict_getentry(dict, dict->names->values[i]);
        nn_array_push(list, field->value);
    }
    return nn_value_fromobject(list);
}

NeonValue nn_memberfunc_dict_remove(NeonState* state, NeonArguments* args)
{
    int i;
    int index;
    NeonValue value;
    NeonObjDict* dict;
    NeonArgCheck check;
    nn_argcheck_init(state, &check, args);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    ENFORCE_VALID_DICT_KEY(&check, 0);
    dict = nn_value_asdict(args->thisval);
    if(nn_table_get(dict->htab, args->args[0], &value))
    {
        nn_table_delete(dict->htab, args->args[0]);
        index = -1;
        for(i = 0; i < dict->names->count; i++)
        {
            if(nn_value_compare(state, dict->names->values[i], args->args[0]))
            {
                index = i;
                break;
            }
        }
        for(i = index; i < dict->names->count; i++)
        {
            dict->names->values[i] = dict->names->values[i + 1];
        }
        dict->names->count--;
        return value;
    }
    return nn_value_makenull();
}

NeonValue nn_memberfunc_dict_isempty(NeonState* state, NeonArguments* args)
{
    NeonArgCheck check;
    nn_argcheck_init(state, &check, args);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    return nn_value_makebool(nn_value_asdict(args->thisval)->names->count == 0);
}

NeonValue nn_memberfunc_dict_findkey(NeonState* state, NeonArguments* args)
{
    NeonArgCheck check;
    nn_argcheck_init(state, &check, args);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    return nn_table_findkey(nn_value_asdict(args->thisval)->htab, args->args[0]);
}

NeonValue nn_memberfunc_dict_tolist(NeonState* state, NeonArguments* args)
{
    int i;
    NeonObjArray* list;
    NeonObjDict* dict;
    NeonObjArray* namelist;
    NeonObjArray* valuelist;
    NeonArgCheck check;
    nn_argcheck_init(state, &check, args);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    dict = nn_value_asdict(args->thisval);
    namelist = (NeonObjArray*)nn_gcmem_protect(state, (NeonObject*)nn_object_makearray(state));
    valuelist = (NeonObjArray*)nn_gcmem_protect(state, (NeonObject*)nn_object_makearray(state));
    for(i = 0; i < dict->names->count; i++)
    {
        nn_array_push(namelist, dict->names->values[i]);
        NeonValue value;
        if(nn_table_get(dict->htab, dict->names->values[i], &value))
        {
            nn_array_push(valuelist, value);
        }
        else
        {
            /* theoretically impossible */
            nn_array_push(valuelist, nn_value_makenull());
        }
    }
    list = (NeonObjArray*)nn_gcmem_protect(state, (NeonObject*)nn_object_makearray(state));
    nn_array_push(list, nn_value_fromobject(namelist));
    nn_array_push(list, nn_value_fromobject(valuelist));
    return nn_value_fromobject(list);
}

NeonValue nn_memberfunc_dict_iter(NeonState* state, NeonArguments* args)
{
    NeonValue result;
    NeonObjDict* dict;
    NeonArgCheck check;
    nn_argcheck_init(state, &check,  args);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    dict = nn_value_asdict(args->thisval);
    if(nn_table_get(dict->htab, args->args[0], &result))
    {
        return result;
    }
    return nn_value_makenull();
}

NeonValue nn_memberfunc_dict_itern(NeonState* state, NeonArguments* args)
{
    int i;
    NeonObjDict* dict;
    NeonArgCheck check;
    nn_argcheck_init(state, &check, args);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    dict = nn_value_asdict(args->thisval);
    if(nn_value_isnull(args->args[0]))
    {
        if(dict->names->count == 0)
        {
            return nn_value_makebool(false);
        }
        return dict->names->values[0];
    }
    for(i = 0; i < dict->names->count; i++)
    {
        if(nn_value_compare(state, args->args[0], dict->names->values[i]) && (i + 1) < dict->names->count)
        {
            return dict->names->values[i + 1];
        }
    }
    return nn_value_makenull();
}

NeonValue nn_memberfunc_dict_each(NeonState* state, NeonArguments* args)
{
    int i;
    int arity;
    NeonValue value;
    NeonValue callable;
    NeonValue unused;
    NeonObjDict* dict;
    NeonObjArray* nestargs;
    NeonArgCheck check;
    nn_argcheck_init(state, &check, args);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_iscallable);
    dict = nn_value_asdict(args->thisval);
    callable = args->args[0];
    nestargs = nn_object_makearray(state);
    nn_vm_stackpush(state, nn_value_fromobject(nestargs));
    arity = nn_nestcall_prepare(state, callable, args->thisval, nestargs);
    for(i = 0; i < dict->names->count; i++)
    {
        if(arity > 0)
        {
            nn_table_get(dict->htab, dict->names->values[i], &value);
            nestargs->varray->values[0] = value;
            if(arity > 1)
            {
                nestargs->varray->values[1] = dict->names->values[i];
            }
        }
        nn_nestcall_callfunction(state, callable, args->thisval, nestargs, &unused);
    }
    /* pop the argument list */
    nn_vm_stackpop(state);
    return nn_value_makeempty();
}

NeonValue nn_memberfunc_dict_filter(NeonState* state, NeonArguments* args)
{
    int i;
    int arity;
    NeonValue value;
    NeonValue callable;
    NeonValue result;
    NeonObjDict* dict;
    NeonObjArray* nestargs;
    NeonObjDict* resultdict;
    NeonArgCheck check;
    nn_argcheck_init(state, &check, args);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_iscallable);
    dict = nn_value_asdict(args->thisval);
    callable = args->args[0];
    nestargs = nn_object_makearray(state);
    nn_vm_stackpush(state, nn_value_fromobject(nestargs));
    arity = nn_nestcall_prepare(state, callable, args->thisval, nestargs);
    resultdict = (NeonObjDict*)nn_gcmem_protect(state, (NeonObject*)nn_object_makedict(state));
    for(i = 0; i < dict->names->count; i++)
    {
        nn_table_get(dict->htab, dict->names->values[i], &value);
        if(arity > 0)
        {
            nestargs->varray->values[0] = value;
            if(arity > 1)
            {
                nestargs->varray->values[1] = dict->names->values[i];
            }
        }
        nn_nestcall_callfunction(state, callable, args->thisval, nestargs, &result);
        if(!nn_value_isfalse(result))
        {
            nn_dict_addentry(resultdict, dict->names->values[i], value);
        }
    }
    /* pop the call list */
    nn_vm_stackpop(state);
    return nn_value_fromobject(resultdict);
}

NeonValue nn_memberfunc_dict_some(NeonState* state, NeonArguments* args)
{
    int i;
    int arity;
    NeonValue result;
    NeonValue value;
    NeonValue callable;
    NeonObjDict* dict;
    NeonObjArray* nestargs;
    NeonArgCheck check;
    nn_argcheck_init(state, &check, args);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_iscallable);
    dict = nn_value_asdict(args->thisval);
    callable = args->args[0];
    nestargs = nn_object_makearray(state);
    nn_vm_stackpush(state, nn_value_fromobject(nestargs));
    arity = nn_nestcall_prepare(state, callable, args->thisval, nestargs);
    for(i = 0; i < dict->names->count; i++)
    {
        if(arity > 0)
        {
            nn_table_get(dict->htab, dict->names->values[i], &value);
            nestargs->varray->values[0] = value;
            if(arity > 1)
            {
                nestargs->varray->values[1] = dict->names->values[i];
            }
        }
        nn_nestcall_callfunction(state, callable, args->thisval, nestargs, &result);
        if(!nn_value_isfalse(result))
        {
            /* pop the call list */
            nn_vm_stackpop(state);
            return nn_value_makebool(true);
        }
    }
    /* pop the call list */
    nn_vm_stackpop(state);
    return nn_value_makebool(false);
}


NeonValue nn_memberfunc_dict_every(NeonState* state, NeonArguments* args)
{
    int i;
    int arity;
    NeonValue value;
    NeonValue callable;  
    NeonValue result;
    NeonObjDict* dict;
    NeonObjArray* nestargs;
    NeonArgCheck check;
    nn_argcheck_init(state, &check, args);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_iscallable);
    dict = nn_value_asdict(args->thisval);
    callable = args->args[0];
    nestargs = nn_object_makearray(state);
    nn_vm_stackpush(state, nn_value_fromobject(nestargs));
    arity = nn_nestcall_prepare(state, callable, args->thisval, nestargs);
    for(i = 0; i < dict->names->count; i++)
    {
        if(arity > 0)
        {
            nn_table_get(dict->htab, dict->names->values[i], &value);
            nestargs->varray->values[0] = value;
            if(arity > 1)
            {
                nestargs->varray->values[1] = dict->names->values[i];
            }
        }
        nn_nestcall_callfunction(state, callable, args->thisval, nestargs, &result);
        if(nn_value_isfalse(result))
        {
            /* pop the call list */
            nn_vm_stackpop(state);
            return nn_value_makebool(false);
        }
    }
    /* pop the call list */
    nn_vm_stackpop(state);
    return nn_value_makebool(true);
}

NeonValue nn_memberfunc_dict_reduce(NeonState* state, NeonArguments* args)
{
    int i;
    int arity;
    int startindex;
    NeonValue value;
    NeonValue callable;
    NeonValue accumulator;
    NeonObjDict* dict;
    NeonObjArray* nestargs;
    NeonArgCheck check;
    nn_argcheck_init(state, &check, args);
    NEON_ARGS_CHECKCOUNTRANGE(&check, 1, 2);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_iscallable);
    dict = nn_value_asdict(args->thisval);
    callable = args->args[0];
    startindex = 0;
    accumulator = nn_value_makenull();
    if(args->count == 2)
    {
        accumulator = args->args[1];
    }
    if(nn_value_isnull(accumulator) && dict->names->count > 0)
    {
        nn_table_get(dict->htab, dict->names->values[0], &accumulator);
        startindex = 1;
    }
    nestargs = nn_object_makearray(state);
    nn_vm_stackpush(state, nn_value_fromobject(nestargs));
    arity = nn_nestcall_prepare(state, callable, args->thisval, nestargs);
    for(i = startindex; i < dict->names->count; i++)
    {
        /* only call map for non-empty values in a list. */
        if(!nn_value_isnull(dict->names->values[i]) && !nn_value_isempty(dict->names->values[i]))
        {
            if(arity > 0)
            {
                nestargs->varray->values[0] = accumulator;
                if(arity > 1)
                {
                    nn_table_get(dict->htab, dict->names->values[i], &value);
                    nestargs->varray->values[1] = value;
                    if(arity > 2)
                    {
                        nestargs->varray->values[2] = dict->names->values[i];
                        if(arity > 4)
                        {
                            nestargs->varray->values[3] = args->thisval;
                        }
                    }
                }
            }
            nn_nestcall_callfunction(state, callable, args->thisval, nestargs, &accumulator);
        }
    }
    /* pop the call list */
    nn_vm_stackpop(state);
    return accumulator;
}


#undef ENFORCE_VALID_DICT_KEY

#define FILE_ERROR(type, message) \
    NEON_RETURNERROR(#type " -> %s", message, file->path->sbuf->data);

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

int nn_fileobject_close(NeonObjFile* file)
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

bool nn_fileobject_open(NeonObjFile* file)
{
    if(file->handle != NULL)
    {
        return true;
    }
    if(file->handle == NULL && !file->isstd)
    {
        file->handle = fopen(file->path->sbuf->data, file->mode->sbuf->data);
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

NeonValue nn_memberfunc_file_constructor(NeonState* state, NeonArguments* args)
{
    FILE* hnd;
    const char* path;
    const char* mode;
    NeonObjString* opath;
    NeonObjFile* file;
    (void)hnd;
    NeonArgCheck check;
    nn_argcheck_init(state, &check, args);
    NEON_ARGS_CHECKCOUNTRANGE(&check, 1, 2);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
    opath = nn_value_asstring(args->args[0]);
    if(opath->sbuf->length == 0)
    {
        NEON_RETURNERROR("file path cannot be empty");
    }
    mode = "r";
    if(args->count == 2)
    {
        NEON_ARGS_CHECKTYPE(&check, 1, nn_value_isstring);
        mode = nn_value_asstring(args->args[1])->sbuf->data;
    }
    path = opath->sbuf->data;
    file = (NeonObjFile*)nn_gcmem_protect(state, (NeonObject*)nn_object_makefile(state, NULL, false, path, mode));
    nn_fileobject_open(file);
    return nn_value_fromobject(file);
}

NeonValue nn_memberfunc_file_exists(NeonState* state, NeonArguments* args)
{
    NeonObjString* file;
    NeonArgCheck check;
    nn_argcheck_init(state, &check, args);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
    file = nn_value_asstring(args->args[0]);
    return nn_value_makebool(nn_util_fsfileexists(state, file->sbuf->data));
}


NeonValue nn_memberfunc_file_isfile(NeonState* state, NeonArguments* args)
{
    NeonObjString* file;
    NeonArgCheck check;
    nn_argcheck_init(state, &check, args);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
    file = nn_value_asstring(args->args[0]);
    return nn_value_makebool(nn_util_fsfileisfile(state, file->sbuf->data));
}

NeonValue nn_memberfunc_file_isdirectory(NeonState* state, NeonArguments* args)
{
    NeonObjString* file;
    NeonArgCheck check;
    nn_argcheck_init(state, &check, args);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
    file = nn_value_asstring(args->args[0]);
    return nn_value_makebool(nn_util_fsfileisdirectory(state, file->sbuf->data));
}

NeonValue nn_memberfunc_file_close(NeonState* state, NeonArguments* args)
{
    NeonArgCheck check;
    nn_argcheck_init(state, &check, args);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    nn_fileobject_close(nn_value_asfile(args->thisval));
    return nn_value_makeempty();
}

NeonValue nn_memberfunc_file_open(NeonState* state, NeonArguments* args)
{
    NeonArgCheck check;
    nn_argcheck_init(state, &check, args);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    nn_fileobject_open(nn_value_asfile(args->thisval));
    return nn_value_makeempty();
}

NeonValue nn_memberfunc_file_isopen(NeonState* state, NeonArguments* args)
{
    NeonObjFile* file;
    (void)state;
    file = nn_value_asfile(args->thisval);
    return nn_value_makebool(file->isstd || file->isopen);
}

NeonValue nn_memberfunc_file_isclosed(NeonState* state, NeonArguments* args)
{
    NeonObjFile* file;
    (void)state;
    file = nn_value_asfile(args->thisval);
    return nn_value_makebool(!file->isstd && !file->isopen);
}

bool nn_file_read(NeonState* state, NeonObjFile* file, size_t readhowmuch, NeonIOResult* dest)
{
    size_t filesizereal;
    struct stat stats;
    filesizereal = -1;
    dest->success = false;
    dest->length = 0;
    dest->data = NULL;
    if(!file->isstd)
    {
        if(!nn_util_fsfileexists(state, file->path->sbuf->data))
        {
            return false;
        }
        /* file is in write only mode */
        /*
        else if(strstr(file->mode->sbuf->data, "w") != NULL && strstr(file->mode->sbuf->data, "+") == NULL)
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
        if(osfn_lstat(file->path->sbuf->data, &stats) == 0)
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
    dest->data = (char*)nn_gcmem_allocate(state, sizeof(char), readhowmuch + 1);
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

NeonValue nn_memberfunc_file_read(NeonState* state, NeonArguments* args)
{
    size_t readhowmuch;
    NeonIOResult res;
    NeonObjFile* file;
    NeonArgCheck check;
    nn_argcheck_init(state, &check, args);
    NEON_ARGS_CHECKCOUNTRANGE(&check, 0, 1);
    readhowmuch = -1;
    if(args->count == 1)
    {
        NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isnumber);
        readhowmuch = (size_t)nn_value_asnumber(args->args[0]);
    }
    file = nn_value_asfile(args->thisval);
    if(!nn_file_read(state, file, readhowmuch, &res))
    {
        FILE_ERROR(NotFound, strerror(errno));
    }
    return nn_value_fromobject(nn_string_takelen(state, res.data, res.length));
}

NeonValue nn_memberfunc_file_get(NeonState* state, NeonArguments* args)
{
    int ch;
    NeonObjFile* file;
    NeonArgCheck check;
    nn_argcheck_init(state, &check, args);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    file = nn_value_asfile(args->thisval);
    ch = fgetc(file->handle);
    if(ch == EOF)
    {
        return nn_value_makenull();
    }
    return nn_value_makenumber(ch);
}

NeonValue nn_memberfunc_file_gets(NeonState* state, NeonArguments* args)
{
    long end;
    long length;
    long currentpos;
    size_t bytesread;
    char* buffer;
    NeonObjFile* file;
    NeonArgCheck check;
    nn_argcheck_init(state, &check, args);
    NEON_ARGS_CHECKCOUNTRANGE(&check, 0, 1);
    length = -1;
    if(args->count == 1)
    {
        NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isnumber);
        length = (size_t)nn_value_asnumber(args->args[0]);
    }
    file = nn_value_asfile(args->thisval);
    if(!file->isstd)
    {
        if(!nn_util_fsfileexists(state, file->path->sbuf->data))
        {
            FILE_ERROR(NotFound, "no such file or directory");
        }
        else if(strstr(file->mode->sbuf->data, "w") != NULL && strstr(file->mode->sbuf->data, "+") == NULL)
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
    buffer = (char*)nn_gcmem_allocate(state, sizeof(char), length + 1);
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

NeonValue nn_memberfunc_file_write(NeonState* state, NeonArguments* args)
{
    size_t count;
    int length;
    unsigned char* data;
    NeonObjFile* file;
    NeonObjString* string;
    NeonArgCheck check;
    nn_argcheck_init(state, &check, args);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    file = nn_value_asfile(args->thisval);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
    string = nn_value_asstring(args->args[0]);
    data = (unsigned char*)string->sbuf->data;
    length = string->sbuf->length;
    if(!file->isstd)
    {
        if(strstr(file->mode->sbuf->data, "r") != NULL && strstr(file->mode->sbuf->data, "+") == NULL)
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

NeonValue nn_memberfunc_file_puts(NeonState* state, NeonArguments* args)
{
    size_t count;
    int length;
    unsigned char* data;
    NeonObjFile* file;
    NeonObjString* string;
    NeonArgCheck check;
    nn_argcheck_init(state, &check, args);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    file = nn_value_asfile(args->thisval);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
    string = nn_value_asstring(args->args[0]);
    data = (unsigned char*)string->sbuf->data;
    length = string->sbuf->length;
    if(!file->isstd)
    {
        if(strstr(file->mode->sbuf->data, "r") != NULL && strstr(file->mode->sbuf->data, "+") == NULL)
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

NeonValue nn_memberfunc_file_printf(NeonState* state, NeonArguments* args)
{
    NeonObjFile* file;
    NeonFormatInfo nfi;
    NeonPrinter pr;
    NeonObjString* ofmt;
    NeonArgCheck check;
    nn_argcheck_init(state, &check, args);
    file = nn_value_asfile(args->thisval);
    NEON_ARGS_CHECKMINARG(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
    ofmt = nn_value_asstring(args->args[0]);
    nn_printer_makestackio(state, &pr, file->handle, false);
    nn_strformat_init(state, &nfi, &pr, nn_string_getcstr(ofmt), nn_string_getlength(ofmt));
    if(!nn_strformat_format(&nfi, args->count, 1, args->args))
    {
    }
    return nn_value_makenull();
}

NeonValue nn_memberfunc_file_number(NeonState* state, NeonArguments* args)
{
    NeonArgCheck check;
    nn_argcheck_init(state, &check, args);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    return nn_value_makenumber(nn_value_asfile(args->thisval)->number);
}

NeonValue nn_memberfunc_file_istty(NeonState* state, NeonArguments* args)
{
    NeonObjFile* file;
    NeonArgCheck check;
    nn_argcheck_init(state, &check, args);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    file = nn_value_asfile(args->thisval);
    return nn_value_makebool(file->istty);
}

NeonValue nn_memberfunc_file_flush(NeonState* state, NeonArguments* args)
{
    NeonObjFile* file;
    NeonArgCheck check;
    nn_argcheck_init(state, &check, args);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    file = nn_value_asfile(args->thisval);
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
    return nn_value_makeempty();
}

NeonValue nn_memberfunc_file_stats(NeonState* state, NeonArguments* args)
{
    struct stat stats;
    NeonObjFile* file;
    NeonObjDict* dict;
    NeonArgCheck check;
    nn_argcheck_init(state, &check, args);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    file = nn_value_asfile(args->thisval);
    dict = (NeonObjDict*)nn_gcmem_protect(state, (NeonObject*)nn_object_makedict(state));
    if(!file->isstd)
    {
        if(nn_util_fsfileexists(state, file->path->sbuf->data))
        {
            if(osfn_lstat(file->path->sbuf->data, &stats) == 0)
            {
                #if !defined(NEON_PLAT_ISWINDOWS)
                nn_dict_addentrycstr(dict, "isreadable", nn_value_makebool(((stats.st_mode & S_IRUSR) != 0)));
                nn_dict_addentrycstr(dict, "iswritable", nn_value_makebool(((stats.st_mode & S_IWUSR) != 0)));
                nn_dict_addentrycstr(dict, "isexecutable", nn_value_makebool(((stats.st_mode & S_IXUSR) != 0)));
                nn_dict_addentrycstr(dict, "issymbolic", nn_value_makebool((S_ISLNK(stats.st_mode) != 0)));
                #else
                nn_dict_addentrycstr(dict, "isreadable", nn_value_makebool(((stats.st_mode & S_IREAD) != 0)));
                nn_dict_addentrycstr(dict, "iswritable", nn_value_makebool(((stats.st_mode & S_IWRITE) != 0)));
                nn_dict_addentrycstr(dict, "isexecutable", nn_value_makebool(((stats.st_mode & S_IEXEC) != 0)));
                nn_dict_addentrycstr(dict, "issymbolic", nn_value_makebool(false));
                #endif
                nn_dict_addentrycstr(dict, "size", nn_value_makenumber(stats.st_size));
                nn_dict_addentrycstr(dict, "mode", nn_value_makenumber(stats.st_mode));
                nn_dict_addentrycstr(dict, "dev", nn_value_makenumber(stats.st_dev));
                nn_dict_addentrycstr(dict, "ino", nn_value_makenumber(stats.st_ino));
                nn_dict_addentrycstr(dict, "nlink", nn_value_makenumber(stats.st_nlink));
                nn_dict_addentrycstr(dict, "uid", nn_value_makenumber(stats.st_uid));
                nn_dict_addentrycstr(dict, "gid", nn_value_makenumber(stats.st_gid));
                nn_dict_addentrycstr(dict, "mtime", nn_value_makenumber(stats.st_mtime));
                nn_dict_addentrycstr(dict, "atime", nn_value_makenumber(stats.st_atime));
                nn_dict_addentrycstr(dict, "ctime", nn_value_makenumber(stats.st_ctime));
                nn_dict_addentrycstr(dict, "blocks", nn_value_makenumber(0));
                nn_dict_addentrycstr(dict, "blksize", nn_value_makenumber(0));
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

NeonValue nn_memberfunc_file_path(NeonState* state, NeonArguments* args)
{
    NeonObjFile* file;
    NeonArgCheck check;
    nn_argcheck_init(state, &check, args);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    file = nn_value_asfile(args->thisval);
    DENY_STD();
    return nn_value_fromobject(file->path);
}

NeonValue nn_memberfunc_file_mode(NeonState* state, NeonArguments* args)
{
    NeonObjFile* file;
    NeonArgCheck check;
    nn_argcheck_init(state, &check, args);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    file = nn_value_asfile(args->thisval);
    return nn_value_fromobject(file->mode);
}

NeonValue nn_memberfunc_file_name(NeonState* state, NeonArguments* args)
{
    char* name;
    NeonObjFile* file;
    NeonArgCheck check;
    nn_argcheck_init(state, &check, args);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    file = nn_value_asfile(args->thisval);
    if(!file->isstd)
    {
        name = nn_util_fsgetbasename(state, file->path->sbuf->data);
        return nn_value_fromobject(nn_string_copycstr(state, name));
    }
    else if(file->istty)
    {
        /*name = ttyname(file->number);*/
        name = nn_util_strdup(state, "<tty>");
        if(name)
        {
            return nn_value_fromobject(nn_string_copycstr(state, name));
        }
    }
    return nn_value_makenull();
}

NeonValue nn_memberfunc_file_seek(NeonState* state, NeonArguments* args)
{
    long position;
    int seektype;
    NeonObjFile* file;
    NeonArgCheck check;
    nn_argcheck_init(state, &check, args);
    NEON_ARGS_CHECKCOUNT(&check, 2);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isnumber);
    NEON_ARGS_CHECKTYPE(&check, 1, nn_value_isnumber);
    file = nn_value_asfile(args->thisval);
    DENY_STD();
    position = (long)nn_value_asnumber(args->args[0]);
    seektype = nn_value_asnumber(args->args[1]);
    RETURN_STATUS(fseek(file->handle, position, seektype));
}

NeonValue nn_memberfunc_file_tell(NeonState* state, NeonArguments* args)
{
    NeonObjFile* file;
    NeonArgCheck check;
    nn_argcheck_init(state, &check, args);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    file = nn_value_asfile(args->thisval);
    DENY_STD();
    return nn_value_makenumber(ftell(file->handle));
}

#undef FILE_ERROR
#undef RETURN_STATUS
#undef DENY_STD

NeonObjArray* nn_array_makefilled(NeonState* state, size_t cnt, NeonValue filler)
{
    size_t i;
    NeonObjArray* list;
    list = (NeonObjArray*)nn_object_allocobject(state, sizeof(NeonObjArray), NEON_OBJTYPE_ARRAY);
    list->varray = nn_valarray_make(state);
    if(cnt > 0)
    {
        for(i=0; i<cnt; i++)
        {
            nn_valarray_push(list->varray, filler);
        }
    }
    return list;
}

NeonObjArray* nn_array_make(NeonState* state)
{
    return nn_array_makefilled(state, 0, nn_value_makeempty());
}

void nn_array_push(NeonObjArray* list, NeonValue value)
{
    NeonState* state;
    (void)state;
    state = ((NeonObject*)list)->pvm;
    /*nn_vm_stackpush(state, value);*/
    nn_valarray_push(list->varray, value);
    /*nn_vm_stackpop(state); */
}

NeonObjArray* nn_array_copy(NeonObjArray* list, int start, int length)
{
    int i;
    NeonState* state;
    NeonObjArray* newlist;
    state = ((NeonObject*)list)->pvm;
    newlist = (NeonObjArray*)nn_gcmem_protect(state, (NeonObject*)nn_object_makearray(state));
    if(start == -1)
    {
        start = 0;
    }
    if(length == -1)
    {
        length = list->varray->count - start;
    }
    for(i = start; i < start + length; i++)
    {
        nn_array_push(newlist, list->varray->values[i]);
    }
    return newlist;
}

NeonValue nn_memberfunc_array_length(NeonState* state, NeonArguments* args)
{
    NeonObjArray* selfarr;
    (void)state;
    NeonArgCheck check;
    nn_argcheck_init(state, &check, args);
    selfarr = nn_value_asarray(args->thisval);
    return nn_value_makenumber(selfarr->varray->count);
}

NeonValue nn_memberfunc_array_append(NeonState* state, NeonArguments* args)
{
    int i;
    (void)state;
    for(i = 0; i < args->count; i++)
    {
        nn_array_push(nn_value_asarray(args->thisval), args->args[i]);
    }
    return nn_value_makeempty();
}

NeonValue nn_memberfunc_array_clear(NeonState* state, NeonArguments* args)
{
    NeonArgCheck check;
    nn_argcheck_init(state, &check, args);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    nn_valarray_destroy(nn_value_asarray(args->thisval)->varray);
    return nn_value_makeempty();
}

NeonValue nn_memberfunc_array_clone(NeonState* state, NeonArguments* args)
{
    NeonObjArray* list;
    NeonArgCheck check;
    nn_argcheck_init(state, &check, args);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    list = nn_value_asarray(args->thisval);
    return nn_value_fromobject(nn_array_copy(list, 0, list->varray->count));
}

NeonValue nn_memberfunc_array_count(NeonState* state, NeonArguments* args)
{
    int i;
    int count;
    NeonObjArray* list;
    NeonArgCheck check;
    nn_argcheck_init(state, &check, args);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    list = nn_value_asarray(args->thisval);
    count = 0;
    for(i = 0; i < list->varray->count; i++)
    {
        if(nn_value_compare(state, list->varray->values[i], args->args[0]))
        {
            count++;
        }
    }
    return nn_value_makenumber(count);
}

NeonValue nn_memberfunc_array_extend(NeonState* state, NeonArguments* args)
{
    int i;
    NeonObjArray* list;
    NeonObjArray* list2;
    NeonArgCheck check;
    nn_argcheck_init(state, &check, args);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isarray);
    list = nn_value_asarray(args->thisval);
    list2 = nn_value_asarray(args->args[0]);
    for(i = 0; i < list2->varray->count; i++)
    {
        nn_array_push(list, list2->varray->values[i]);
    }
    return nn_value_makeempty();
}

NeonValue nn_memberfunc_array_indexof(NeonState* state, NeonArguments* args)
{
    int i;
    NeonObjArray* list;
    NeonArgCheck check;
    nn_argcheck_init(state, &check, args);
    NEON_ARGS_CHECKCOUNTRANGE(&check, 1, 2);
    list = nn_value_asarray(args->thisval);
    i = 0;
    if(args->count == 2)
    {
        NEON_ARGS_CHECKTYPE(&check, 1, nn_value_isnumber);
        i = nn_value_asnumber(args->args[1]);
    }
    for(; i < list->varray->count; i++)
    {
        if(nn_value_compare(state, list->varray->values[i], args->args[0]))
        {
            return nn_value_makenumber(i);
        }
    }
    return nn_value_makenumber(-1);
}

NeonValue nn_memberfunc_array_insert(NeonState* state, NeonArguments* args)
{
    int index;
    NeonObjArray* list;
    NeonArgCheck check;
    nn_argcheck_init(state, &check, args);
    NEON_ARGS_CHECKCOUNT(&check, 2);
    NEON_ARGS_CHECKTYPE(&check, 1, nn_value_isnumber);
    list = nn_value_asarray(args->thisval);
    index = (int)nn_value_asnumber(args->args[1]);
    nn_valarray_insert(list->varray, args->args[0], index);
    return nn_value_makeempty();
}


NeonValue nn_memberfunc_array_pop(NeonState* state, NeonArguments* args)
{
    NeonValue value;
    NeonObjArray* list;
    NeonArgCheck check;
    nn_argcheck_init(state, &check, args);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    list = nn_value_asarray(args->thisval);
    if(list->varray->count > 0)
    {
        value = list->varray->values[list->varray->count - 1];
        list->varray->count--;
        return value;
    }
    return nn_value_makenull();
}

NeonValue nn_memberfunc_array_shift(NeonState* state, NeonArguments* args)
{
    int i;
    int j;
    int count;
    NeonObjArray* list;
    NeonObjArray* newlist;
    NeonArgCheck check;
    nn_argcheck_init(state, &check, args);
    NEON_ARGS_CHECKCOUNTRANGE(&check, 0, 1);
    count = 1;
    if(args->count == 1)
    {
        NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isnumber);
        count = nn_value_asnumber(args->args[0]);
    }
    list = nn_value_asarray(args->thisval);
    if(count >= list->varray->count || list->varray->count == 1)
    {
        list->varray->count = 0;
        return nn_value_makenull();
    }
    else if(count > 0)
    {
        newlist = (NeonObjArray*)nn_gcmem_protect(state, (NeonObject*)nn_object_makearray(state));
        for(i = 0; i < count; i++)
        {
            nn_array_push(newlist, list->varray->values[0]);
            for(j = 0; j < list->varray->count; j++)
            {
                list->varray->values[j] = list->varray->values[j + 1];
            }
            list->varray->count -= 1;
        }
        if(count == 1)
        {
            return newlist->varray->values[0];
        }
        else
        {
            return nn_value_fromobject(newlist);
        }
    }
    return nn_value_makenull();
}

NeonValue nn_memberfunc_array_removeat(NeonState* state, NeonArguments* args)
{
    int i;
    int index;
    NeonValue value;
    NeonObjArray* list;
    NeonArgCheck check;
    nn_argcheck_init(state, &check, args);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isnumber);
    list = nn_value_asarray(args->thisval);
    index = nn_value_asnumber(args->args[0]);
    if(index < 0 || index >= list->varray->count)
    {
        NEON_RETURNERROR("list index %d out of range at remove_at()", index);
    }
    value = list->varray->values[index];
    for(i = index; i < list->varray->count - 1; i++)
    {
        list->varray->values[i] = list->varray->values[i + 1];
    }
    list->varray->count--;
    return value;
}

NeonValue nn_memberfunc_array_remove(NeonState* state, NeonArguments* args)
{
    int i;
    int index;
    NeonObjArray* list;
    NeonArgCheck check;
    nn_argcheck_init(state, &check, args);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    list = nn_value_asarray(args->thisval);
    index = -1;
    for(i = 0; i < list->varray->count; i++)
    {
        if(nn_value_compare(state, list->varray->values[i], args->args[0]))
        {
            index = i;
            break;
        }
    }
    if(index != -1)
    {
        for(i = index; i < list->varray->count; i++)
        {
            list->varray->values[i] = list->varray->values[i + 1];
        }
        list->varray->count--;
    }
    return nn_value_makeempty();
}

NeonValue nn_memberfunc_array_reverse(NeonState* state, NeonArguments* args)
{
    int fromtop;
    NeonObjArray* list;
    NeonObjArray* nlist;
    NeonArgCheck check;
    nn_argcheck_init(state, &check, args);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    list = nn_value_asarray(args->thisval);
    nlist = (NeonObjArray*)nn_gcmem_protect(state, (NeonObject*)nn_object_makearray(state));
    /* in-place reversal:*/
    /*
    int start = 0;
    int end = list->varray->count - 1;
    while (start < end)
    {
        NeonValue temp = list->varray->values[start];
        list->varray->values[start] = list->varray->values[end];
        list->varray->values[end] = temp;
        start++;
        end--;
    }
    */
    for(fromtop = list->varray->count - 1; fromtop >= 0; fromtop--)
    {
        nn_array_push(nlist, list->varray->values[fromtop]);
    }
    return nn_value_fromobject(nlist);
}

NeonValue nn_memberfunc_array_sort(NeonState* state, NeonArguments* args)
{
    NeonObjArray* list;
    NeonArgCheck check;
    nn_argcheck_init(state, &check, args);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    list = nn_value_asarray(args->thisval);
    nn_value_sortvalues(state, list->varray->values, list->varray->count);
    return nn_value_makeempty();
}

NeonValue nn_memberfunc_array_contains(NeonState* state, NeonArguments* args)
{
    int i;
    NeonObjArray* list;
    NeonArgCheck check;
    nn_argcheck_init(state, &check, args);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    list = nn_value_asarray(args->thisval);
    for(i = 0; i < list->varray->count; i++)
    {
        if(nn_value_compare(state, args->args[0], list->varray->values[i]))
        {
            return nn_value_makebool(true);
        }
    }
    return nn_value_makebool(false);
}

NeonValue nn_memberfunc_array_delete(NeonState* state, NeonArguments* args)
{
    int i;
    int idxupper;
    int idxlower;
    NeonObjArray* list;
    NeonArgCheck check;
    nn_argcheck_init(state, &check, args);
    NEON_ARGS_CHECKCOUNTRANGE(&check, 1, 2);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isnumber);
    idxlower = nn_value_asnumber(args->args[0]);
    idxupper = idxlower;
    if(args->count == 2)
    {
        NEON_ARGS_CHECKTYPE(&check, 1, nn_value_isnumber);
        idxupper = nn_value_asnumber(args->args[1]);
    }
    list = nn_value_asarray(args->thisval);
    if(idxlower < 0 || idxlower >= list->varray->count)
    {
        NEON_RETURNERROR("list index %d out of range at delete()", idxlower);
    }
    else if(idxupper < idxlower || idxupper >= list->varray->count)
    {
        NEON_RETURNERROR("invalid upper limit %d at delete()", idxupper);
    }
    for(i = 0; i < list->varray->count - idxupper; i++)
    {
        list->varray->values[idxlower + i] = list->varray->values[i + idxupper + 1];
    }
    list->varray->count -= idxupper - idxlower + 1;
    return nn_value_makenumber((double)idxupper - (double)idxlower + 1);
}

NeonValue nn_memberfunc_array_first(NeonState* state, NeonArguments* args)
{
    NeonObjArray* list;
    NeonArgCheck check;
    nn_argcheck_init(state, &check, args);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    list = nn_value_asarray(args->thisval);
    if(list->varray->count > 0)
    {
        return list->varray->values[0];
    }
    return nn_value_makenull();
}

NeonValue nn_memberfunc_array_last(NeonState* state, NeonArguments* args)
{
    NeonObjArray* list;
    NeonArgCheck check;
    nn_argcheck_init(state, &check, args);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    list = nn_value_asarray(args->thisval);
    if(list->varray->count > 0)
    {
        return list->varray->values[list->varray->count - 1];
    }
    return nn_value_makenull();
}

NeonValue nn_memberfunc_array_isempty(NeonState* state, NeonArguments* args)
{
    NeonArgCheck check;
    nn_argcheck_init(state, &check, args);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    return nn_value_makebool(nn_value_asarray(args->thisval)->varray->count == 0);
}


NeonValue nn_memberfunc_array_take(NeonState* state, NeonArguments* args)
{
    int count;
    NeonObjArray* list;
    NeonArgCheck check;
    nn_argcheck_init(state, &check, args);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isnumber);
    list = nn_value_asarray(args->thisval);
    count = nn_value_asnumber(args->args[0]);
    if(count < 0)
    {
        count = list->varray->count + count;
    }
    if(list->varray->count < count)
    {
        return nn_value_fromobject(nn_array_copy(list, 0, list->varray->count));
    }
    return nn_value_fromobject(nn_array_copy(list, 0, count));
}

NeonValue nn_memberfunc_array_get(NeonState* state, NeonArguments* args)
{
    int index;
    NeonObjArray* list;
    NeonArgCheck check;
    nn_argcheck_init(state, &check, args);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isnumber);
    list = nn_value_asarray(args->thisval);
    index = nn_value_asnumber(args->args[0]);
    if(index < 0 || index >= list->varray->count)
    {
        return nn_value_makenull();
    }
    return list->varray->values[index];
}

NeonValue nn_memberfunc_array_compact(NeonState* state, NeonArguments* args)
{
    int i;
    NeonObjArray* list;
    NeonObjArray* newlist;
    NeonArgCheck check;
    nn_argcheck_init(state, &check, args);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    list = nn_value_asarray(args->thisval);
    newlist = (NeonObjArray*)nn_gcmem_protect(state, (NeonObject*)nn_object_makearray(state));
    for(i = 0; i < list->varray->count; i++)
    {
        if(!nn_value_compare(state, list->varray->values[i], nn_value_makenull()))
        {
            nn_array_push(newlist, list->varray->values[i]);
        }
    }
    return nn_value_fromobject(newlist);
}


NeonValue nn_memberfunc_array_unique(NeonState* state, NeonArguments* args)
{
    int i;
    int j;
    bool found;
    NeonObjArray* list;
    NeonObjArray* newlist;
    NeonArgCheck check;
    nn_argcheck_init(state, &check, args);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    list = nn_value_asarray(args->thisval);
    newlist = (NeonObjArray*)nn_gcmem_protect(state, (NeonObject*)nn_object_makearray(state));
    for(i = 0; i < list->varray->count; i++)
    {
        found = false;
        for(j = 0; j < newlist->varray->count; j++)
        {
            if(nn_value_compare(state, newlist->varray->values[j], list->varray->values[i]))
            {
                found = true;
                continue;
            }
        }
        if(!found)
        {
            nn_array_push(newlist, list->varray->values[i]);
        }
    }
    return nn_value_fromobject(newlist);
}

NeonValue nn_memberfunc_array_zip(NeonState* state, NeonArguments* args)
{
    int i;
    int j;
    NeonObjArray* list;
    NeonObjArray* newlist;
    NeonObjArray* alist;
    NeonObjArray** arglist;
    NeonArgCheck check;
    nn_argcheck_init(state, &check, args);
    list = nn_value_asarray(args->thisval);
    newlist = (NeonObjArray*)nn_gcmem_protect(state, (NeonObject*)nn_object_makearray(state));
    arglist = (NeonObjArray**)nn_gcmem_allocate(state, sizeof(NeonObjArray*), args->count);
    for(i = 0; i < args->count; i++)
    {
        NEON_ARGS_CHECKTYPE(&check, i, nn_value_isarray);
        arglist[i] = nn_value_asarray(args->args[i]);
    }
    for(i = 0; i < list->varray->count; i++)
    {
        alist = (NeonObjArray*)nn_gcmem_protect(state, (NeonObject*)nn_object_makearray(state));
        /* item of main list*/
        nn_array_push(alist, list->varray->values[i]);
        for(j = 0; j < args->count; j++)
        {
            if(i < arglist[j]->varray->count)
            {
                nn_array_push(alist, arglist[j]->varray->values[i]);
            }
            else
            {
                nn_array_push(alist, nn_value_makenull());
            }
        }
        nn_array_push(newlist, nn_value_fromobject(alist));
    }
    return nn_value_fromobject(newlist);
}


NeonValue nn_memberfunc_array_zipfrom(NeonState* state, NeonArguments* args)
{
    int i;
    int j;
    NeonObjArray* list;
    NeonObjArray* newlist;
    NeonObjArray* alist;
    NeonObjArray* arglist;
    NeonArgCheck check;
    nn_argcheck_init(state, &check, args);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isarray);
    list = nn_value_asarray(args->thisval);
    newlist = (NeonObjArray*)nn_gcmem_protect(state, (NeonObject*)nn_object_makearray(state));
    arglist = nn_value_asarray(args->args[0]);
    for(i = 0; i < arglist->varray->count; i++)
    {
        if(!nn_value_isarray(arglist->varray->values[i]))
        {
            NEON_RETURNERROR("invalid list in zip entries");
        }
    }
    for(i = 0; i < list->varray->count; i++)
    {
        alist = (NeonObjArray*)nn_gcmem_protect(state, (NeonObject*)nn_object_makearray(state));
        nn_array_push(alist, list->varray->values[i]);
        for(j = 0; j < arglist->varray->count; j++)
        {
            if(i < nn_value_asarray(arglist->varray->values[j])->varray->count)
            {
                nn_array_push(alist, nn_value_asarray(arglist->varray->values[j])->varray->values[i]);
            }
            else
            {
                nn_array_push(alist, nn_value_makenull());
            }
        }
        nn_array_push(newlist, nn_value_fromobject(alist));
    }
    return nn_value_fromobject(newlist);
}

NeonValue nn_memberfunc_array_todict(NeonState* state, NeonArguments* args)
{
    int i;
    NeonObjDict* dict;
    NeonObjArray* list;
    NeonArgCheck check;
    nn_argcheck_init(state, &check, args);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    dict = (NeonObjDict*)nn_gcmem_protect(state, (NeonObject*)nn_object_makedict(state));
    list = nn_value_asarray(args->thisval);
    for(i = 0; i < list->varray->count; i++)
    {
        nn_dict_setentry(dict, nn_value_makenumber(i), list->varray->values[i]);
    }
    return nn_value_fromobject(dict);
}

NeonValue nn_memberfunc_array_iter(NeonState* state, NeonArguments* args)
{
    int index;
    NeonObjArray* list;
    NeonArgCheck check;
    nn_argcheck_init(state, &check, args);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isnumber);
    list = nn_value_asarray(args->thisval);
    index = nn_value_asnumber(args->args[0]);
    if(index > -1 && index < list->varray->count)
    {
        return list->varray->values[index];
    }
    return nn_value_makenull();
}

NeonValue nn_memberfunc_array_itern(NeonState* state, NeonArguments* args)
{
    int index;
    NeonObjArray* list;
    NeonArgCheck check;
    nn_argcheck_init(state, &check, args);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    list = nn_value_asarray(args->thisval);
    if(nn_value_isnull(args->args[0]))
    {
        if(list->varray->count == 0)
        {
            return nn_value_makebool(false);
        }
        return nn_value_makenumber(0);
    }
    if(!nn_value_isnumber(args->args[0]))
    {
        NEON_RETURNERROR("lists are numerically indexed");
    }
    index = nn_value_asnumber(args->args[0]);
    if(index < list->varray->count - 1)
    {
        return nn_value_makenumber((double)index + 1);
    }
    return nn_value_makenull();
}

NeonValue nn_memberfunc_array_each(NeonState* state, NeonArguments* args)
{
    int i;
    int arity;
    NeonValue callable;
    NeonValue unused;
    NeonObjArray* list;
    NeonObjArray* nestargs;
    NeonArgCheck check;
    nn_argcheck_init(state, &check, args);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_iscallable);
    list = nn_value_asarray(args->thisval);
    callable = args->args[0];
    nestargs = nn_object_makearray(state);
    nn_vm_stackpush(state, nn_value_fromobject(nestargs));
    arity = nn_nestcall_prepare(state, callable, args->thisval, nestargs);
    for(i = 0; i < list->varray->count; i++)
    {
        if(arity > 0)
        {
            nestargs->varray->values[0] = list->varray->values[i];
            if(arity > 1)
            {
                nestargs->varray->values[1] = nn_value_makenumber(i);
            }
        }
        nn_nestcall_callfunction(state, callable, args->thisval, nestargs, &unused);
    }
    nn_vm_stackpop(state);
    return nn_value_makeempty();
}


NeonValue nn_memberfunc_array_map(NeonState* state, NeonArguments* args)
{
    int i;
    int arity;
    NeonValue res;
    NeonValue callable;
    NeonObjArray* list;
    NeonObjArray* nestargs;
    NeonObjArray* resultlist;
    NeonArgCheck check;
    nn_argcheck_init(state, &check, args);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_iscallable);
    list = nn_value_asarray(args->thisval);
    callable = args->args[0];
    nestargs = nn_object_makearray(state);
    nn_vm_stackpush(state, nn_value_fromobject(nestargs));
    arity = nn_nestcall_prepare(state, callable, args->thisval, nestargs);
    resultlist = (NeonObjArray*)nn_gcmem_protect(state, (NeonObject*)nn_object_makearray(state));
    for(i = 0; i < list->varray->count; i++)
    {
        if(!nn_value_isempty(list->varray->values[i]))
        {
            if(arity > 0)
            {
                nestargs->varray->values[0] = list->varray->values[i];
                if(arity > 1)
                {
                    nestargs->varray->values[1] = nn_value_makenumber(i);
                }
            }
            nn_nestcall_callfunction(state, callable, args->thisval, nestargs, &res);
            nn_array_push(resultlist, res);
        }
        else
        {
            nn_array_push(resultlist, nn_value_makeempty());
        }
    }
    nn_vm_stackpop(state);
    return nn_value_fromobject(resultlist);
}


NeonValue nn_memberfunc_array_filter(NeonState* state, NeonArguments* args)
{
    int i;
    int arity;
    NeonValue callable;
    NeonValue result;
    NeonObjArray* list;
    NeonObjArray* nestargs;
    NeonObjArray* resultlist;
    NeonArgCheck check;
    nn_argcheck_init(state, &check, args);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_iscallable);
    list = nn_value_asarray(args->thisval);
    callable = args->args[0];
    nestargs = nn_object_makearray(state);
    nn_vm_stackpush(state, nn_value_fromobject(nestargs));
    arity = nn_nestcall_prepare(state, callable, args->thisval, nestargs);
    resultlist = (NeonObjArray*)nn_gcmem_protect(state, (NeonObject*)nn_object_makearray(state));
    for(i = 0; i < list->varray->count; i++)
    {
        if(!nn_value_isempty(list->varray->values[i]))
        {
            if(arity > 0)
            {
                nestargs->varray->values[0] = list->varray->values[i];
                if(arity > 1)
                {
                    nestargs->varray->values[1] = nn_value_makenumber(i);
                }
            }
            nn_nestcall_callfunction(state, callable, args->thisval, nestargs, &result);
            if(!nn_value_isfalse(result))
            {
                nn_array_push(resultlist, list->varray->values[i]);
            }
        }
    }
    nn_vm_stackpop(state);
    return nn_value_fromobject(resultlist);
}

NeonValue nn_memberfunc_array_some(NeonState* state, NeonArguments* args)
{
    int i;
    int arity;
    NeonValue callable;
    NeonValue result;
    NeonObjArray* list;
    NeonObjArray* nestargs;
    NeonArgCheck check;
    nn_argcheck_init(state, &check, args);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_iscallable);
    list = nn_value_asarray(args->thisval);
    callable = args->args[0];
    nestargs = nn_object_makearray(state);
    nn_vm_stackpush(state, nn_value_fromobject(nestargs));
    arity = nn_nestcall_prepare(state, callable, args->thisval, nestargs);
    for(i = 0; i < list->varray->count; i++)
    {
        if(!nn_value_isempty(list->varray->values[i]))
        {
            if(arity > 0)
            {
                nestargs->varray->values[0] = list->varray->values[i];
                if(arity > 1)
                {
                    nestargs->varray->values[1] = nn_value_makenumber(i);
                }
            }
            nn_nestcall_callfunction(state, callable, args->thisval, nestargs, &result);
            if(!nn_value_isfalse(result))
            {
                nn_vm_stackpop(state);
                return nn_value_makebool(true);
            }
        }
    }
    nn_vm_stackpop(state);
    return nn_value_makebool(false);
}


NeonValue nn_memberfunc_array_every(NeonState* state, NeonArguments* args)
{
    int i;
    int arity;
    NeonValue result;
    NeonValue callable;
    NeonObjArray* list;
    NeonObjArray* nestargs;
    NeonArgCheck check;
    nn_argcheck_init(state, &check, args);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_iscallable);
    list = nn_value_asarray(args->thisval);
    callable = args->args[0];
    nestargs = nn_object_makearray(state);
    nn_vm_stackpush(state, nn_value_fromobject(nestargs));
    arity = nn_nestcall_prepare(state, callable, args->thisval, nestargs);
    for(i = 0; i < list->varray->count; i++)
    {
        if(!nn_value_isempty(list->varray->values[i]))
        {
            if(arity > 0)
            {
                nestargs->varray->values[0] = list->varray->values[i];
                if(arity > 1)
                {
                    nestargs->varray->values[1] = nn_value_makenumber(i);
                }
            }
            nn_nestcall_callfunction(state, callable, args->thisval, nestargs, &result);
            if(nn_value_isfalse(result))
            {
                nn_vm_stackpop(state);
                return nn_value_makebool(false);
            }
        }
    }
    nn_vm_stackpop(state);
    return nn_value_makebool(true);
}

NeonValue nn_memberfunc_array_reduce(NeonState* state, NeonArguments* args)
{
    int i;
    int arity;
    int startindex;
    NeonValue callable;
    NeonValue accumulator;
    NeonObjArray* list;
    NeonObjArray* nestargs;
    NeonArgCheck check;
    nn_argcheck_init(state, &check, args);
    NEON_ARGS_CHECKCOUNTRANGE(&check, 1, 2);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_iscallable);
    list = nn_value_asarray(args->thisval);
    callable = args->args[0];
    startindex = 0;
    accumulator = nn_value_makenull();
    if(args->count == 2)
    {
        accumulator = args->args[1];
    }
    if(nn_value_isnull(accumulator) && list->varray->count > 0)
    {
        accumulator = list->varray->values[0];
        startindex = 1;
    }
    nestargs = nn_object_makearray(state);
    nn_vm_stackpush(state, nn_value_fromobject(nestargs));
    arity = nn_nestcall_prepare(state, callable, args->thisval, nestargs);
    for(i = startindex; i < list->varray->count; i++)
    {
        if(!nn_value_isnull(list->varray->values[i]) && !nn_value_isempty(list->varray->values[i]))
        {
            if(arity > 0)
            {
                nestargs->varray->values[0] = accumulator;
                if(arity > 1)
                {
                    nestargs->varray->values[1] = list->varray->values[i];
                    if(arity > 2)
                    {
                        nestargs->varray->values[2] = nn_value_makenumber(i);
                        if(arity > 4)
                        {
                            nestargs->varray->values[3] = args->thisval;
                        }
                    }
                }
            }
            nn_nestcall_callfunction(state, callable, args->thisval, nestargs, &accumulator);
        }
    }
    nn_vm_stackpop(state);
    return accumulator;
}

NeonValue nn_memberfunc_range_lower(NeonState* state, NeonArguments* args)
{
    NeonArgCheck check;
    nn_argcheck_init(state, &check, args);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    return nn_value_makenumber(nn_value_asrange(args->thisval)->lower);
}

NeonValue nn_memberfunc_range_upper(NeonState* state, NeonArguments* args)
{
    NeonArgCheck check;
    nn_argcheck_init(state, &check, args);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    return nn_value_makenumber(nn_value_asrange(args->thisval)->upper);
}

NeonValue nn_memberfunc_range_range(NeonState* state, NeonArguments* args)
{
    NeonArgCheck check;
    nn_argcheck_init(state, &check, args);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    return nn_value_makenumber(nn_value_asrange(args->thisval)->range);
}

NeonValue nn_memberfunc_range_iter(NeonState* state, NeonArguments* args)
{
    int val;
    int index;
    NeonObjRange* range;
    NeonArgCheck check;
    nn_argcheck_init(state, &check, args);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isnumber);
    range = nn_value_asrange(args->thisval);
    index = nn_value_asnumber(args->args[0]);
    if(index >= 0 && index < range->range)
    {
        if(index == 0)
        {
            return nn_value_makenumber(range->lower);
        }
        if(range->lower > range->upper)
        {
            val = --range->lower;
        }
        else
        {
            val = ++range->lower;
        }
        return nn_value_makenumber(val);
    }
    return nn_value_makenull();
}

NeonValue nn_memberfunc_range_itern(NeonState* state, NeonArguments* args)
{
    int index;
    NeonObjRange* range;
    NeonArgCheck check;
    nn_argcheck_init(state, &check, args);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    range = nn_value_asrange(args->thisval);
    if(nn_value_isnull(args->args[0]))
    {
        if(range->range == 0)
        {
            return nn_value_makenull();
        }
        return nn_value_makenumber(0);
    }
    if(!nn_value_isnumber(args->args[0]))
    {
        NEON_RETURNERROR("ranges are numerically indexed");
    }
    index = (int)nn_value_asnumber(args->args[0]) + 1;
    if(index < range->range)
    {
        return nn_value_makenumber(index);
    }
    return nn_value_makenull();
}

NeonValue nn_memberfunc_range_loop(NeonState* state, NeonArguments* args)
{
    int i;
    int arity;
    NeonValue callable;
    NeonValue unused;
    NeonObjRange* range;
    NeonObjArray* nestargs;
    NeonArgCheck check;
    nn_argcheck_init(state, &check, args);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_iscallable);
    range = nn_value_asrange(args->thisval);
    callable = args->args[0];
    nestargs = nn_object_makearray(state);
    nn_vm_stackpush(state, nn_value_fromobject(nestargs));
    arity = nn_nestcall_prepare(state, callable, args->thisval, nestargs);
    for(i = 0; i < range->range; i++)
    {
        if(arity > 0)
        {
            nestargs->varray->values[0] = nn_value_makenumber(i);
            if(arity > 1)
            {
                nestargs->varray->values[1] = nn_value_makenumber(i);
            }
        }
        nn_nestcall_callfunction(state, callable, args->thisval, nestargs, &unused);
    }
    nn_vm_stackpop(state);
    return nn_value_makeempty();
}

NeonValue nn_memberfunc_range_expand(NeonState* state, NeonArguments* args)
{
    int i;
    NeonValue val;
    NeonObjRange* range;
    NeonObjArray* oa;
    range = nn_value_asrange(args->thisval);
    oa = nn_object_makearray(state);
    for(i = 0; i < range->range; i++)
    {
        val = nn_value_makenumber(i);
        nn_array_push(oa, val);
    }
    return nn_value_fromobject(oa);
}

NeonValue nn_memberfunc_range_constructor(NeonState* state, NeonArguments* args)
{
    int a;
    int b;
    NeonObjRange* orng;
    a = nn_value_asnumber(args->args[0]);
    b = nn_value_asnumber(args->args[1]);
    orng = nn_object_makerange(state, a, b);
    return nn_value_fromobject(orng);
}

NeonValue nn_memberfunc_string_utf8numbytes(NeonState* state, NeonArguments* args)
{
    int incode;
    int res;
    NeonArgCheck check;
    nn_argcheck_init(state, &check, args);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isnumber);
    incode = nn_value_asnumber(args->args[0]);
    //static int utf8NumBytes(int value)
    res = nn_util_utf8numbytes(incode);
    return nn_value_makenumber(res);
}

NeonValue nn_memberfunc_string_utf8decode(NeonState* state, NeonArguments* args)
{
    int res;
    NeonObjString* instr;
    NeonArgCheck check;
    nn_argcheck_init(state, &check, args);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
    instr = nn_value_asstring(args->args[0]);
    res = nn_util_utf8decode((const uint8_t*)instr->sbuf->data, instr->sbuf->length);
    return nn_value_makenumber(res);
}

NeonValue nn_memberfunc_string_utf8encode(NeonState* state, NeonArguments* args)
{
    int incode;
    size_t len;
    NeonObjString* res;
    char* buf;
    NeonArgCheck check;
    nn_argcheck_init(state, &check, args);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isnumber);
    incode = nn_value_asnumber(args->args[0]);
    //static char* utf8Encode(unsigned int code)
    buf = nn_util_utf8encode(state, incode, &len);
    res = nn_string_takelen(state, buf, len);
    return nn_value_fromobject(res);
}

NeonValue nn_util_stringutf8chars(NeonState* state, NeonArguments* args, bool onlycodepoint)
{
    int cp;
    const char* cstr;
    NeonObjArray* res;
    NeonObjString* os;
    NeonObjString* instr;
    utf8iterator_t iter;
    (void)state;
    instr = nn_value_asstring(args->thisval);
    res = nn_array_make(state);
    utf8iter_init(&iter, instr->sbuf->data, instr->sbuf->length);
    while (utf8iter_next(&iter))
    {
        cp = iter.codepoint;
        cstr = utf8iter_getchar(&iter);
        if(onlycodepoint)
        {
            nn_array_push(res, nn_value_makenumber(cp));
        }
        else
        {
            os = nn_string_copylen(state, cstr, iter.charsize);
            nn_array_push(res, nn_value_fromobject(os));
        }
    }
    return nn_value_fromobject(res);
}

NeonValue nn_memberfunc_string_utf8chars(NeonState* state, NeonArguments* args)
{
    return nn_util_stringutf8chars(state, args, false);
}

NeonValue nn_memberfunc_string_utf8codepoints(NeonState* state, NeonArguments* args)
{
    return nn_util_stringutf8chars(state, args, true);
}


NeonValue nn_memberfunc_string_fromcharcode(NeonState* state, NeonArguments* args)
{
    char ch;
    NeonObjString* os;
    NeonArgCheck check;
    nn_argcheck_init(state, &check, args);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isnumber);
    ch = nn_value_asnumber(args->args[0]);
    os = nn_string_copylen(state, &ch, 1);
    return nn_value_fromobject(os);
}

NeonValue nn_memberfunc_string_constructor(NeonState* state, NeonArguments* args)
{
    NeonObjString* os;
    NeonArgCheck check;
    (void)args;
    nn_argcheck_init(state, &check, args);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    os = nn_string_copylen(state, "", 0);
    return nn_value_fromobject(os);
}

NeonValue nn_memberfunc_string_length(NeonState* state, NeonArguments* args)
{
    NeonArgCheck check;
    NeonObjString* selfstr;
    nn_argcheck_init(state, &check, args);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    selfstr = nn_value_asstring(args->thisval);
    return nn_value_makenumber(selfstr->sbuf->length);
}

NeonValue nn_string_fromrange(NeonState* state, const char* buf, int len)
{
    NeonObjString* str;
    if(len <= 0)
    {
        return nn_value_fromobject(nn_string_copylen(state, "", 0));
    }
    str = nn_string_copylen(state, "", 0);
    dyn_strbuf_appendstrn(str->sbuf, buf, len);
    return nn_value_fromobject(str);
}

NeonObjString* nn_string_substring(NeonState* state, NeonObjString* selfstr, size_t start, size_t end, bool likejs)
{
    size_t asz;
    size_t len;
    size_t tmp;
    size_t maxlen;
    char* raw;
    (void)likejs;
    maxlen = selfstr->sbuf->length;
    len = maxlen;
    if(end > maxlen)
    {
        tmp = start;
        start = end;
        end = tmp;
        len = maxlen;
    }
    if(end < start)
    {
        tmp = end;
        end = start;
        start = tmp;
        len = end;
    }
    len = (end - start);
    if(len > maxlen)
    {
        len = maxlen;
    }
    asz = ((end + 1) * sizeof(char));
    raw = (char*)nn_gcmem_allocate(state, sizeof(char), asz);
    memset(raw, 0, asz);
    memcpy(raw, selfstr->sbuf->data + start, len);
    return nn_string_takelen(state, raw, len);
}

NeonValue nn_memberfunc_string_substring(NeonState* state, NeonArguments* args)
{
    size_t end;
    size_t start;
    size_t maxlen;
    NeonObjString* nos;
    NeonObjString* selfstr;
    NeonArgCheck check;
    nn_argcheck_init(state, &check, args);
    selfstr = nn_value_asstring(args->thisval);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isnumber);
    maxlen = selfstr->sbuf->length;
    end = maxlen;
    start = nn_value_asnumber(args->args[0]);
    if(args->count > 1)
    {
        NEON_ARGS_CHECKTYPE(&check, 1, nn_value_isnumber);
        end = nn_value_asnumber(args->args[1]);
    }
    nos = nn_string_substring(state, selfstr, start, end, true);
    return nn_value_fromobject(nos);
}

NeonValue nn_memberfunc_string_charcodeat(NeonState* state, NeonArguments* args)
{
    int ch;
    int idx;
    int selflen;
    NeonObjString* selfstr;
    NeonArgCheck check;
    nn_argcheck_init(state, &check, args);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isnumber);
    selfstr = nn_value_asstring(args->thisval);
    idx = nn_value_asnumber(args->args[0]);
    selflen = (int)selfstr->sbuf->length;
    if((idx < 0) || (idx >= selflen))
    {
        ch = -1;
    }
    else
    {
        ch = selfstr->sbuf->data[idx];
    }
    return nn_value_makenumber(ch);
}

NeonValue nn_memberfunc_string_charat(NeonState* state, NeonArguments* args)
{
    char ch;
    int idx;
    int selflen;
    NeonObjString* selfstr;
    NeonArgCheck check;
    nn_argcheck_init(state, &check, args);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isnumber);
    selfstr = nn_value_asstring(args->thisval);
    idx = nn_value_asnumber(args->args[0]);
    selflen = (int)selfstr->sbuf->length;
    if((idx < 0) || (idx >= selflen))
    {
        return nn_value_fromobject(nn_string_copylen(state, "", 0));
    }
    else
    {
        ch = selfstr->sbuf->data[idx];
    }
    return nn_value_fromobject(nn_string_copylen(state, &ch, 1));
}

NeonValue nn_memberfunc_string_upper(NeonState* state, NeonArguments* args)
{
    size_t slen;
    char* string;
    NeonObjString* str;
    NeonArgCheck check;
    nn_argcheck_init(state, &check, args);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    str = nn_value_asstring(args->thisval);
    slen = str->sbuf->length;
    string = nn_util_strtoupper(str->sbuf->data, slen);
    return nn_value_fromobject(nn_string_copylen(state, string, slen));
}

NeonValue nn_memberfunc_string_lower(NeonState* state, NeonArguments* args)
{
    size_t slen;
    char* string;
    NeonObjString* str;
    NeonArgCheck check;
    nn_argcheck_init(state, &check, args);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    str = nn_value_asstring(args->thisval);
    slen = str->sbuf->length;
    string = nn_util_strtolower(str->sbuf->data, slen);
    return nn_value_fromobject(nn_string_copylen(state, string, slen));
}

NeonValue nn_memberfunc_string_isalpha(NeonState* state, NeonArguments* args)
{
    int i;
    NeonArgCheck check;
    NeonObjString* selfstr;
    nn_argcheck_init(state, &check, args);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    selfstr = nn_value_asstring(args->thisval);
    for(i = 0; i < (int)selfstr->sbuf->length; i++)
    {
        if(!isalpha((unsigned char)selfstr->sbuf->data[i]))
        {
            return nn_value_makebool(false);
        }
    }
    return nn_value_makebool(selfstr->sbuf->length != 0);
}

NeonValue nn_memberfunc_string_isalnum(NeonState* state, NeonArguments* args)
{
    int i;
    NeonObjString* selfstr;
    NeonArgCheck check;
    nn_argcheck_init(state, &check, args);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    selfstr = nn_value_asstring(args->thisval);
    for(i = 0; i < (int)selfstr->sbuf->length; i++)
    {
        if(!isalnum((unsigned char)selfstr->sbuf->data[i]))
        {
            return nn_value_makebool(false);
        }
    }
    return nn_value_makebool(selfstr->sbuf->length != 0);
}

NeonValue nn_memberfunc_string_isfloat(NeonState* state, NeonArguments* args)
{
    double f;
    char* p;
    NeonObjString* selfstr;
    NeonArgCheck check;
    (void)f;
    nn_argcheck_init(state, &check, args);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    selfstr = nn_value_asstring(args->thisval);
    errno = 0;
    if(selfstr->sbuf->length ==0)
    {
        return nn_value_makebool(false);
    }
    f = strtod(selfstr->sbuf->data, &p);
    if(errno)
    {
        return nn_value_makebool(false);
    }
    else
    {
        if(*p == 0)
        {
            return nn_value_makebool(true);
        }
    }
    return nn_value_makebool(false);
}

NeonValue nn_memberfunc_string_isnumber(NeonState* state, NeonArguments* args)
{
    int i;
    NeonObjString* selfstr;
    NeonArgCheck check;
    nn_argcheck_init(state, &check, args);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    selfstr = nn_value_asstring(args->thisval);
    for(i = 0; i < (int)selfstr->sbuf->length; i++)
    {
        if(!isdigit((unsigned char)selfstr->sbuf->data[i]))
        {
            return nn_value_makebool(false);
        }
    }
    return nn_value_makebool(selfstr->sbuf->length != 0);
}

NeonValue nn_memberfunc_string_islower(NeonState* state, NeonArguments* args)
{
    int i;
    bool alphafound;
    NeonObjString* selfstr;
    NeonArgCheck check;
    nn_argcheck_init(state, &check, args);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    selfstr = nn_value_asstring(args->thisval);
    alphafound = false;
    for(i = 0; i < (int)selfstr->sbuf->length; i++)
    {
        if(!alphafound && !isdigit(selfstr->sbuf->data[0]))
        {
            alphafound = true;
        }
        if(isupper(selfstr->sbuf->data[0]))
        {
            return nn_value_makebool(false);
        }
    }
    return nn_value_makebool(alphafound);
}

NeonValue nn_memberfunc_string_isupper(NeonState* state, NeonArguments* args)
{
    int i;
    bool alphafound;
    NeonObjString* selfstr;
    NeonArgCheck check;
    nn_argcheck_init(state, &check, args);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    selfstr = nn_value_asstring(args->thisval);
    alphafound = false;
    for(i = 0; i < (int)selfstr->sbuf->length; i++)
    {
        if(!alphafound && !isdigit(selfstr->sbuf->data[0]))
        {
            alphafound = true;
        }
        if(islower(selfstr->sbuf->data[0]))
        {
            return nn_value_makebool(false);
        }
    }
    return nn_value_makebool(alphafound);
}

NeonValue nn_memberfunc_string_isspace(NeonState* state, NeonArguments* args)
{
    int i;
    NeonObjString* selfstr;
    NeonArgCheck check;
    nn_argcheck_init(state, &check, args);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    selfstr = nn_value_asstring(args->thisval);
    for(i = 0; i < (int)selfstr->sbuf->length; i++)
    {
        if(!isspace((unsigned char)selfstr->sbuf->data[i]))
        {
            return nn_value_makebool(false);
        }
    }
    return nn_value_makebool(selfstr->sbuf->length != 0);
}

NeonValue nn_memberfunc_string_trim(NeonState* state, NeonArguments* args)
{
    char trimmer;
    char* end;
    char* string;
    NeonObjString* selfstr;
    NeonArgCheck check;
    nn_argcheck_init(state, &check, args);
    NEON_ARGS_CHECKCOUNTRANGE(&check, 0, 1);
    trimmer = '\0';
    if(args->count == 1)
    {
        trimmer = (char)nn_value_asstring(args->args[0])->sbuf->data[0];
    }
    selfstr = nn_value_asstring(args->thisval);
    string = selfstr->sbuf->data;
    end = NULL;
    /* Trim leading space*/
    if(trimmer == '\0')
    {
        while(isspace((unsigned char)*string))
        {
            string++;
        }
    }
    else
    {
        while(trimmer == *string)
        {
            string++;
        }
    }
    /* All spaces? */
    if(*string == 0)
    {
        return nn_value_fromobject(nn_string_copylen(state, "", 0));
    }
    /* Trim trailing space */
    end = string + strlen(string) - 1;
    if(trimmer == '\0')
    {
        while(end > string && isspace((unsigned char)*end))
        {
            end--;
        }
    }
    else
    {
        while(end > string && trimmer == *end)
        {
            end--;
        }
    }
    end[1] = '\0';
    return nn_value_fromobject(nn_string_copycstr(state, string));
}

NeonValue nn_memberfunc_string_ltrim(NeonState* state, NeonArguments* args)
{
    char* end;
    char* string;
    char trimmer;
    NeonObjString* selfstr;
    NeonArgCheck check;
    nn_argcheck_init(state, &check, args);
    NEON_ARGS_CHECKCOUNTRANGE(&check, 0, 1);
    trimmer = '\0';
    if(args->count == 1)
    {
        trimmer = (char)nn_value_asstring(args->args[0])->sbuf->data[0];
    }
    selfstr = nn_value_asstring(args->thisval);
    string = selfstr->sbuf->data;
    end = NULL;
    /* Trim leading space */
    if(trimmer == '\0')
    {
        while(isspace((unsigned char)*string))
        {
            string++;
        }
    }
    else
    {
        while(trimmer == *string)
        {
            string++;
        }
    }
    /* All spaces? */
    if(*string == 0)
    {
        return nn_value_fromobject(nn_string_copylen(state, "", 0));
    }
    end = string + strlen(string) - 1;
    end[1] = '\0';
    return nn_value_fromobject(nn_string_copycstr(state, string));
}

NeonValue nn_memberfunc_string_rtrim(NeonState* state, NeonArguments* args)
{
    char* end;
    char* string;
    char trimmer;
    NeonObjString* selfstr;
    NeonArgCheck check;
    nn_argcheck_init(state, &check, args);
    NEON_ARGS_CHECKCOUNTRANGE(&check, 0, 1);
    trimmer = '\0';
    if(args->count == 1)
    {
        trimmer = (char)nn_value_asstring(args->args[0])->sbuf->data[0];
    }
    selfstr = nn_value_asstring(args->thisval);
    string = selfstr->sbuf->data;
    end = NULL;
    /* All spaces? */
    if(*string == 0)
    {
        return nn_value_fromobject(nn_string_copylen(state, "", 0));
    }
    end = string + strlen(string) - 1;
    if(trimmer == '\0')
    {
        while(end > string && isspace((unsigned char)*end))
        {
            end--;
        }
    }
    else
    {
        while(end > string && trimmer == *end)
        {
            end--;
        }
    }
    /* Write new null terminator character */
    end[1] = '\0';
    return nn_value_fromobject(nn_string_copycstr(state, string));
}


NeonValue nn_memberfunc_array_constructor(NeonState* state, NeonArguments* args)
{
    int i;
    int cnt;
    NeonValue filler;
    NeonObjArray* arr;
    NeonArgCheck check;
    nn_argcheck_init(state, &check, args);
    NEON_ARGS_CHECKCOUNTRANGE(&check, 1, 2);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isnumber);
    filler = nn_value_makeempty();
    if(args->count > 1)
    {
        filler = args->args[1];
    }
    cnt = nn_value_asnumber(args->args[0]);
    arr = nn_array_makefilled(state, cnt, filler);
    return nn_value_fromobject(arr);
}

NeonValue nn_memberfunc_array_join(NeonState* state, NeonArguments* args)
{
    int i;
    int count;
    NeonPrinter pr;
    NeonValue vjoinee;
    NeonObjArray* selfarr;
    NeonObjString* joinee;
    NeonValue* list;
    selfarr = nn_value_asarray(args->thisval);
    joinee = NULL;
    if(args->count > 0)
    {
        vjoinee = args->args[0];
        if(nn_value_isstring(vjoinee))
        {
            joinee = nn_value_asstring(vjoinee);
        }
        else
        {
            joinee = nn_value_tostring(state, vjoinee);
        }
    }
    list = selfarr->varray->values;
    count = selfarr->varray->count;
    if(count == 0)
    {
        return nn_value_fromobject(nn_string_copycstr(state, ""));
    }
    nn_printer_makestackstring(state, &pr);
    for(i = 0; i < count; i++)
    {
        nn_printer_printvalue(&pr, list[i], false, true);
        if((joinee != NULL) && ((i+1) < count))
        {
            nn_printer_writestringl(&pr, joinee->sbuf->data, joinee->sbuf->length);
        }
    }
    return nn_value_fromobject(nn_printer_takestring(&pr));
}

NeonValue nn_memberfunc_string_indexof(NeonState* state, NeonArguments* args)
{
    int startindex;
    char* result;
    char* haystack;
    NeonObjString* string;
    NeonObjString* needle;
    NeonArgCheck check;
    nn_argcheck_init(state, &check, args);
    NEON_ARGS_CHECKCOUNTRANGE(&check, 1, 2);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
    string = nn_value_asstring(args->thisval);
    needle = nn_value_asstring(args->args[0]);
    startindex = 0;
    if(args->count == 2)
    {
        NEON_ARGS_CHECKTYPE(&check, 1, nn_value_isnumber);
        startindex = nn_value_asnumber(args->args[1]);
    }
    if(string->sbuf->length > 0 && needle->sbuf->length > 0)
    {
        haystack = string->sbuf->data;
        result = strstr(haystack + startindex, needle->sbuf->data);
        if(result != NULL)
        {
            return nn_value_makenumber((int)(result - haystack));
        }
    }
    return nn_value_makenumber(-1);
}

NeonValue nn_memberfunc_string_startswith(NeonState* state, NeonArguments* args)
{
    NeonObjString* substr;
    NeonObjString* string;
    NeonArgCheck check;
    nn_argcheck_init(state, &check, args);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
    string = nn_value_asstring(args->thisval);
    substr = nn_value_asstring(args->args[0]);
    if(string->sbuf->length == 0 || substr->sbuf->length == 0 || substr->sbuf->length > string->sbuf->length)
    {
        return nn_value_makebool(false);
    }
    return nn_value_makebool(memcmp(substr->sbuf->data, string->sbuf->data, substr->sbuf->length) == 0);
}

NeonValue nn_memberfunc_string_endswith(NeonState* state, NeonArguments* args)
{
    int difference;
    NeonObjString* substr;
    NeonObjString* string;
    NeonArgCheck check;
    nn_argcheck_init(state, &check, args);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
    string = nn_value_asstring(args->thisval);
    substr = nn_value_asstring(args->args[0]);
    if(string->sbuf->length == 0 || substr->sbuf->length == 0 || substr->sbuf->length > string->sbuf->length)
    {
        return nn_value_makebool(false);
    }
    difference = string->sbuf->length - substr->sbuf->length;
    return nn_value_makebool(memcmp(substr->sbuf->data, string->sbuf->data + difference, substr->sbuf->length) == 0);
}

NeonValue nn_memberfunc_string_count(NeonState* state, NeonArguments* args)
{
    int count;
    const char* tmp;
    NeonObjString* substr;
    NeonObjString* string;
    NeonArgCheck check;
    nn_argcheck_init(state, &check, args);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
    string = nn_value_asstring(args->thisval);
    substr = nn_value_asstring(args->args[0]);
    if(substr->sbuf->length == 0 || string->sbuf->length == 0)
    {
        return nn_value_makenumber(0);
    }
    count = 0;
    tmp = string->sbuf->data;
    while((tmp = nn_util_utf8strstr(tmp, substr->sbuf->data)))
    {
        count++;
        tmp++;
    }
    return nn_value_makenumber(count);
}

NeonValue nn_memberfunc_string_tonumber(NeonState* state, NeonArguments* args)
{
    NeonObjString* selfstr;
    NeonArgCheck check;
    nn_argcheck_init(state, &check, args);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    selfstr = nn_value_asstring(args->thisval);
    return nn_value_makenumber(strtod(selfstr->sbuf->data, NULL));
}

NeonValue nn_memberfunc_string_isascii(NeonState* state, NeonArguments* args)
{
    NeonObjString* string;
    NeonArgCheck check;
    nn_argcheck_init(state, &check, args);
    NEON_ARGS_CHECKCOUNTRANGE(&check, 0, 1);
    if(args->count == 1)
    {
        NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isbool);
    }
    string = nn_value_asstring(args->thisval);
    return nn_value_fromobject(string);
}

NeonValue nn_memberfunc_string_tolist(NeonState* state, NeonArguments* args)
{
    int i;
    int end;
    int start;
    int length;
    NeonObjArray* list;
    NeonObjString* string;
    NeonArgCheck check;
    nn_argcheck_init(state, &check, args);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    string = nn_value_asstring(args->thisval);
    list = (NeonObjArray*)nn_gcmem_protect(state, (NeonObject*)nn_object_makearray(state));
    length = string->sbuf->length;
    if(length > 0)
    {
        for(i = 0; i < length; i++)
        {
            start = i;
            end = i + 1;
            nn_array_push(list, nn_value_fromobject(nn_string_copylen(state, string->sbuf->data + start, (int)(end - start))));
        }
    }
    return nn_value_fromobject(list);
}

NeonValue nn_memberfunc_string_lpad(NeonState* state, NeonArguments* args)
{
    int i;
    int width;
    int fillsize;
    int finalsize;
    int finalutf8size;
    char fillchar;
    char* str;
    char* fill;
    NeonObjString* ofillstr;
    NeonObjString* result;
    NeonObjString* string;
    NeonArgCheck check;
    nn_argcheck_init(state, &check, args);
    NEON_ARGS_CHECKCOUNTRANGE(&check, 1, 2);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isnumber);
    string = nn_value_asstring(args->thisval);
    width = nn_value_asnumber(args->args[0]);
    fillchar = ' ';
    if(args->count == 2)
    {
        ofillstr = nn_value_asstring(args->args[1]);
        fillchar = ofillstr->sbuf->data[0];
    }
    if(width <= (int)string->sbuf->length)
    {
        return args->thisval;
    }
    fillsize = width - string->sbuf->length;
    fill = (char*)nn_gcmem_allocate(state, sizeof(char), (size_t)fillsize + 1);
    finalsize = string->sbuf->length + fillsize;
    finalutf8size = string->sbuf->length + fillsize;
    for(i = 0; i < fillsize; i++)
    {
        fill[i] = fillchar;
    }
    str = (char*)nn_gcmem_allocate(state, sizeof(char), (size_t)finalsize + 1);
    memcpy(str, fill, fillsize);
    memcpy(str + fillsize, string->sbuf->data, string->sbuf->length);
    str[finalsize] = '\0';
    nn_gcmem_freearray(state, sizeof(char), fill, fillsize + 1);
    result = nn_string_takelen(state, str, finalsize);
    result->sbuf->length = finalutf8size;
    result->sbuf->length = finalsize;
    return nn_value_fromobject(result);
}

NeonValue nn_memberfunc_string_rpad(NeonState* state, NeonArguments* args)
{
    int i;
    int width;
    int fillsize;
    int finalsize;
    int finalutf8size;
    char fillchar;
    char* str;
    char* fill;
    NeonObjString* ofillstr;
    NeonObjString* string;
    NeonObjString* result;
    NeonArgCheck check;
    nn_argcheck_init(state, &check, args);
    NEON_ARGS_CHECKCOUNTRANGE(&check, 1, 2);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isnumber);
    string = nn_value_asstring(args->thisval);
    width = nn_value_asnumber(args->args[0]);
    fillchar = ' ';
    if(args->count == 2)
    {
        ofillstr = nn_value_asstring(args->args[1]);
        fillchar = ofillstr->sbuf->data[0];
    }
    if(width <= (int)string->sbuf->length)
    {
        return args->thisval;
    }
    fillsize = width - string->sbuf->length;
    fill = (char*)nn_gcmem_allocate(state, sizeof(char), (size_t)fillsize + 1);
    finalsize = string->sbuf->length + fillsize;
    finalutf8size = string->sbuf->length + fillsize;
    for(i = 0; i < fillsize; i++)
    {
        fill[i] = fillchar;
    }
    str = (char*)nn_gcmem_allocate(state, sizeof(char), (size_t)finalsize + 1);
    memcpy(str, string->sbuf->data, string->sbuf->length);
    memcpy(str + string->sbuf->length, fill, fillsize);
    str[finalsize] = '\0';
    nn_gcmem_freearray(state, sizeof(char), fill, fillsize + 1);
    result = nn_string_takelen(state, str, finalsize);
    result->sbuf->length = finalutf8size;
    result->sbuf->length = finalsize;
    return nn_value_fromobject(result);
}

NeonValue nn_memberfunc_string_split(NeonState* state, NeonArguments* args)
{
    int i;
    int end;
    int start;
    int length;
    NeonObjArray* list;
    NeonObjString* string;
    NeonObjString* delimeter;
    NeonArgCheck check;
    nn_argcheck_init(state, &check, args);
    NEON_ARGS_CHECKCOUNTRANGE(&check, 1, 2);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
    string = nn_value_asstring(args->thisval);
    delimeter = nn_value_asstring(args->args[0]);
    /* empty string matches empty string to empty list */
    if(((string->sbuf->length == 0) && (delimeter->sbuf->length == 0)) || (string->sbuf->length == 0) || (delimeter->sbuf->length == 0))
    {
        return nn_value_fromobject(nn_object_makearray(state));
    }
    list = (NeonObjArray*)nn_gcmem_protect(state, (NeonObject*)nn_object_makearray(state));
    if(delimeter->sbuf->length > 0)
    {
        start = 0;
        for(i = 0; i <= (int)string->sbuf->length; i++)
        {
            /* match found. */
            if(memcmp(string->sbuf->data + i, delimeter->sbuf->data, delimeter->sbuf->length) == 0 || i == (int)string->sbuf->length)
            {
                nn_array_push(list, nn_value_fromobject(nn_string_copylen(state, string->sbuf->data + start, i - start)));
                i += delimeter->sbuf->length - 1;
                start = i + 1;
            }
        }
    }
    else
    {
        length = (int)string->sbuf->length;
        for(i = 0; i < length; i++)
        {
            start = i;
            end = i + 1;
            nn_array_push(list, nn_value_fromobject(nn_string_copylen(state, string->sbuf->data + start, (int)(end - start))));
        }
    }
    return nn_value_fromobject(list);
}


NeonValue nn_memberfunc_string_replace(NeonState* state, NeonArguments* args)
{
    int i;
    int totallength;
    StringBuffer* result;
    NeonObjString* substr;
    NeonObjString* string;
    NeonObjString* repsubstr;
    NeonArgCheck check;
    nn_argcheck_init(state, &check, args);
    NEON_ARGS_CHECKCOUNTRANGE(&check, 2, 3);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
    NEON_ARGS_CHECKTYPE(&check, 1, nn_value_isstring);
    string = nn_value_asstring(args->thisval);
    substr = nn_value_asstring(args->args[0]);
    repsubstr = nn_value_asstring(args->args[1]);
    if((string->sbuf->length == 0 && substr->sbuf->length == 0) || string->sbuf->length == 0 || substr->sbuf->length == 0)
    {
        return nn_value_fromobject(nn_string_copylen(state, string->sbuf->data, string->sbuf->length));
    }
    result = dyn_strbuf_makeempty(0);
    totallength = 0;
    for(i = 0; i < (int)string->sbuf->length; i++)
    {
        if(memcmp(string->sbuf->data + i, substr->sbuf->data, substr->sbuf->length) == 0)
        {
            if(substr->sbuf->length > 0)
            {
                dyn_strbuf_appendstrn(result, repsubstr->sbuf->data, repsubstr->sbuf->length);
            }
            i += substr->sbuf->length - 1;
            totallength += repsubstr->sbuf->length;
        }
        else
        {
            dyn_strbuf_appendchar(result, string->sbuf->data[i]);
            totallength++;
        }
    }
    return nn_value_fromobject(nn_string_makefromstrbuf(state, result, nn_util_hashstring(result->data, result->length)));
}

NeonValue nn_memberfunc_string_iter(NeonState* state, NeonArguments* args)
{
    int index;
    int length;
    NeonObjString* string;
    NeonObjString* result;
    NeonArgCheck check;
    nn_argcheck_init(state, &check, args);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isnumber);
    string = nn_value_asstring(args->thisval);
    length = string->sbuf->length;
    index = nn_value_asnumber(args->args[0]);
    if(index > -1 && index < length)
    {
        result = nn_string_copylen(state, &string->sbuf->data[index], 1);
        return nn_value_fromobject(result);
    }
    return nn_value_makenull();
}

NeonValue nn_memberfunc_string_itern(NeonState* state, NeonArguments* args)
{
    int index;
    int length;
    NeonObjString* string;
    NeonArgCheck check;
    nn_argcheck_init(state, &check, args);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    string = nn_value_asstring(args->thisval);
    length = string->sbuf->length;
    if(nn_value_isnull(args->args[0]))
    {
        if(length == 0)
        {
            return nn_value_makebool(false);
        }
        return nn_value_makenumber(0);
    }
    if(!nn_value_isnumber(args->args[0]))
    {
        NEON_RETURNERROR("strings are numerically indexed");
    }
    index = nn_value_asnumber(args->args[0]);
    if(index < length - 1)
    {
        return nn_value_makenumber((double)index + 1);
    }
    return nn_value_makenull();
}

NeonValue nn_memberfunc_string_each(NeonState* state, NeonArguments* args)
{
    int i;
    int arity;
    NeonValue callable;
    NeonValue unused;
    NeonObjString* string;
    NeonObjArray* nestargs;
    NeonArgCheck check;
    nn_argcheck_init(state, &check, args);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_iscallable);
    string = nn_value_asstring(args->thisval);
    callable = args->args[0];
    nestargs = nn_object_makearray(state);
    nn_vm_stackpush(state, nn_value_fromobject(nestargs));
    arity = nn_nestcall_prepare(state, callable, args->thisval, nestargs);
    for(i = 0; i < (int)string->sbuf->length; i++)
    {
        if(arity > 0)
        {
            nestargs->varray->values[0] = nn_value_fromobject(nn_string_copylen(state, string->sbuf->data + i, 1));
            if(arity > 1)
            {
                nestargs->varray->values[1] = nn_value_makenumber(i);
            }
        }
        nn_nestcall_callfunction(state, callable, args->thisval, nestargs, &unused);
    }
    /* pop the argument list */
    nn_vm_stackpop(state);
    return nn_value_makeempty();
}

NeonValue nn_memberfunc_object_dump(NeonState* state, NeonArguments* args)
{
    NeonValue v;
    NeonPrinter pr;
    NeonObjString* os;
    v = args->thisval;
    nn_printer_makestackstring(state, &pr);
    nn_printer_printvalue(&pr, v, true, false);
    os = nn_printer_takestring(&pr);
    return nn_value_fromobject(os);
}

NeonValue nn_memberfunc_object_tostring(NeonState* state, NeonArguments* args)
{
    NeonValue v;
    NeonPrinter pr;
    NeonObjString* os;
    v = args->thisval;
    nn_printer_makestackstring(state, &pr);
    nn_printer_printvalue(&pr, v, false, true);
    os = nn_printer_takestring(&pr);
    return nn_value_fromobject(os);
}

NeonValue nn_memberfunc_object_typename(NeonState* state, NeonArguments* args)
{
    NeonValue v;
    NeonObjString* os;
    v = args->args[0];
    os = nn_string_copycstr(state, nn_value_typename(v));
    return nn_value_fromobject(os);
}

NeonValue nn_memberfunc_object_isstring(NeonState* state, NeonArguments* args)
{
    NeonValue v;
    (void)state;
    v = args->thisval;
    return nn_value_makebool(nn_value_isstring(v));
}

NeonValue nn_memberfunc_object_isarray(NeonState* state, NeonArguments* args)
{
    NeonValue v;
    (void)state;
    v = args->thisval;
    return nn_value_makebool(nn_value_isarray(v));
}

NeonValue nn_memberfunc_object_iscallable(NeonState* state, NeonArguments* args)
{
    NeonValue selfval;
    (void)state;
    selfval = args->thisval;
    return (nn_value_makebool(
        nn_value_isclass(selfval) ||
        nn_value_isfuncscript(selfval) ||
        nn_value_isfuncclosure(selfval) ||
        nn_value_isfuncbound(selfval) ||
        nn_value_isfuncnative(selfval)
    ));
}

NeonValue nn_memberfunc_object_isbool(NeonState* state, NeonArguments* args)
{
    NeonValue selfval;
    (void)state;
    selfval = args->thisval;
    return nn_value_makebool(nn_value_isbool(selfval));
}

NeonValue nn_memberfunc_object_isnumber(NeonState* state, NeonArguments* args)
{
    NeonValue selfval;
    (void)state;
    selfval = args->thisval;
    return nn_value_makebool(nn_value_isnumber(selfval));
}

NeonValue nn_memberfunc_object_isint(NeonState* state, NeonArguments* args)
{
    NeonValue selfval;
    (void)state;
    selfval = args->thisval;
    return nn_value_makebool(nn_value_isnumber(selfval) && (((int)nn_value_asnumber(selfval)) == nn_value_asnumber(selfval)));
}

NeonValue nn_memberfunc_object_isdict(NeonState* state, NeonArguments* args)
{
    NeonValue selfval;
    (void)state;
    selfval = args->thisval;
    return nn_value_makebool(nn_value_isdict(selfval));
}

NeonValue nn_memberfunc_object_isobject(NeonState* state, NeonArguments* args)
{
    NeonValue selfval;
    (void)state;
    selfval = args->thisval;
    return nn_value_makebool(nn_value_isobject(selfval));
}

NeonValue nn_memberfunc_object_isfunction(NeonState* state, NeonArguments* args)
{
    NeonValue selfval;
    (void)state;
    selfval = args->thisval;
    return nn_value_makebool(
        nn_value_isfuncscript(selfval) ||
        nn_value_isfuncclosure(selfval) ||
        nn_value_isfuncbound(selfval) ||
        nn_value_isfuncnative(selfval)
    );
}

NeonValue nn_memberfunc_object_isiterable(NeonState* state, NeonArguments* args)
{
    bool isiterable;
    NeonValue dummy;
    NeonObjClass* klass;
    NeonValue selfval;
    (void)state;
    selfval = args->thisval;
    isiterable = nn_value_isarray(selfval) || nn_value_isdict(selfval) || nn_value_isstring(selfval);
    if(!isiterable && nn_value_isinstance(selfval))
    {
        klass = nn_value_asinstance(selfval)->klass;
        isiterable = nn_table_get(klass->methods, nn_value_fromobject(nn_string_copycstr(state, "@iter")), &dummy)
            && nn_table_get(klass->methods, nn_value_fromobject(nn_string_copycstr(state, "@itern")), &dummy);
    }
    return nn_value_makebool(isiterable);
}

NeonValue nn_memberfunc_object_isclass(NeonState* state, NeonArguments* args)
{
    NeonValue selfval;
    (void)state;
    selfval = args->thisval;
    return nn_value_makebool(nn_value_isclass(selfval));
}

NeonValue nn_memberfunc_object_isfile(NeonState* state, NeonArguments* args)
{
    NeonValue selfval;
    (void)state;
    selfval = args->thisval;
    return nn_value_makebool(nn_value_isfile(selfval));
}

NeonValue nn_memberfunc_object_isinstance(NeonState* state, NeonArguments* args)
{
    NeonValue selfval;
    (void)state;
    selfval = args->thisval;
    return nn_value_makebool(nn_value_isinstance(selfval));
}


NeonObjString* nn_util_numbertobinstring(NeonState* state, long n)
{
    int i;
    int rem;
    int count;
    int length;
    long j;
    /* assume maximum of 1024 bits */
    char str[1024];
    char newstr[1027];
    count = 0;
    j = n;
    if(j == 0)
    {
        str[count++] = '0';
    }
    while(j != 0)
    {
        rem = abs((int)(j % 2));
        j /= 2;
        if(rem == 1)
        {
            str[count] = '1';
        }
        else
        {
            str[count] = '0';
        }
        count++;
    }
    /* assume maximum of 1024 bits + 0b (indicator) + sign (-). */
    length = 0;
    if(n < 0)
    {
        newstr[length++] = '-';
    }
    newstr[length++] = '0';
    newstr[length++] = 'b';
    for(i = count - 1; i >= 0; i--)
    {
        newstr[length++] = str[i];
    }
    newstr[length++] = 0;
    return nn_string_copylen(state, newstr, length);
    /*
    //  // To store the binary number
    //  long long number = 0;
    //  int cnt = 0;
    //  while (n != 0) {
    //    long long rem = n % 2;
    //    long long c = (long long) pow(10, cnt);
    //    number += rem * c;
    //    n /= 2;
    //
    //    // Count used to store exponent value
    //    cnt++;
    //  }
    //
    //  char str[67]; // assume maximum of 64 bits + 2 binary indicators (0b)
    //  int length = sprintf(str, "0b%lld", number);
    //
    //  return nn_string_copylen(state, str, length);
    */
}

NeonObjString* nn_util_numbertooctstring(NeonState* state, long long n, bool numeric)
{
    int length;
    /* assume maximum of 64 bits + 2 octal indicators (0c) */
    char str[66];
    length = sprintf(str, numeric ? "0c%llo" : "%llo", n);
    return nn_string_copylen(state, str, length);
}

NeonObjString* nn_util_numbertohexstring(NeonState* state, long long n, bool numeric)
{
    int length;
    /* assume maximum of 64 bits + 2 hex indicators (0x) */
    char str[66];
    length = sprintf(str, numeric ? "0x%llx" : "%llx", n);
    return nn_string_copylen(state, str, length);
}

NeonValue nn_memberfunc_number_tobinstring(NeonState* state, NeonArguments* args)
{
    return nn_value_fromobject(nn_util_numbertobinstring(state, nn_value_asnumber(args->thisval)));
}

NeonValue nn_memberfunc_number_tooctstring(NeonState* state, NeonArguments* args)
{
    return nn_value_fromobject(nn_util_numbertooctstring(state, nn_value_asnumber(args->thisval), false));
}

NeonValue nn_memberfunc_number_tohexstring(NeonState* state, NeonArguments* args)
{
    return nn_value_fromobject(nn_util_numbertohexstring(state, nn_value_asnumber(args->thisval), false));
}

void nn_state_initbuiltinmethods(NeonState* state)
{
    {
        nn_class_setstaticpropertycstr(state->classprimprocess, "env", nn_value_fromobject(state->envdict));
    }
    {
        nn_class_defstaticnativemethod(state, state->classprimobject, "typename", nn_memberfunc_object_typename);
        nn_class_defnativemethod(state, state->classprimobject, "dump", nn_memberfunc_object_dump);
        nn_class_defnativemethod(state, state->classprimobject, "toString", nn_memberfunc_object_tostring);
        nn_class_defnativemethod(state, state->classprimobject, "isArray", nn_memberfunc_object_isarray);        
        nn_class_defnativemethod(state, state->classprimobject, "isString", nn_memberfunc_object_isstring);
        nn_class_defnativemethod(state, state->classprimobject, "isCallable", nn_memberfunc_object_iscallable);
        nn_class_defnativemethod(state, state->classprimobject, "isBool", nn_memberfunc_object_isbool);
        nn_class_defnativemethod(state, state->classprimobject, "isNumber", nn_memberfunc_object_isnumber);
        nn_class_defnativemethod(state, state->classprimobject, "isInt", nn_memberfunc_object_isint);
        nn_class_defnativemethod(state, state->classprimobject, "isDict", nn_memberfunc_object_isdict);
        nn_class_defnativemethod(state, state->classprimobject, "isObject", nn_memberfunc_object_isobject);
        nn_class_defnativemethod(state, state->classprimobject, "isFunction", nn_memberfunc_object_isfunction);
        nn_class_defnativemethod(state, state->classprimobject, "isIterable", nn_memberfunc_object_isiterable);
        nn_class_defnativemethod(state, state->classprimobject, "isClass", nn_memberfunc_object_isclass);
        nn_class_defnativemethod(state, state->classprimobject, "isFile", nn_memberfunc_object_isfile);
        nn_class_defnativemethod(state, state->classprimobject, "isInstance", nn_memberfunc_object_isinstance);

    }
    
    {
        nn_class_defnativemethod(state, state->classprimnumber, "toHexString", nn_memberfunc_number_tohexstring);
        nn_class_defnativemethod(state, state->classprimnumber, "toOctString", nn_memberfunc_number_tooctstring);
        nn_class_defnativemethod(state, state->classprimnumber, "toBinString", nn_memberfunc_number_tobinstring);
    }
    {
        nn_class_defnativeconstructor(state, state->classprimstring, nn_memberfunc_string_constructor);
        nn_class_defstaticnativemethod(state, state->classprimstring, "fromCharCode", nn_memberfunc_string_fromcharcode);
        nn_class_defstaticnativemethod(state, state->classprimstring, "utf8Decode", nn_memberfunc_string_utf8decode);
        nn_class_defstaticnativemethod(state, state->classprimstring, "utf8Encode", nn_memberfunc_string_utf8encode);
        nn_class_defstaticnativemethod(state, state->classprimstring, "utf8NumBytes", nn_memberfunc_string_utf8numbytes);

        nn_class_defnativemethod(state, state->classprimstring, "utf8Chars", nn_memberfunc_string_utf8chars);
        nn_class_defnativemethod(state, state->classprimstring, "utf8Codepoints", nn_memberfunc_string_utf8codepoints);
        nn_class_defnativemethod(state, state->classprimstring, "utf8Bytes", nn_memberfunc_string_utf8codepoints);


        nn_class_defcallablefield(state, state->classprimstring, "length", nn_memberfunc_string_length);
        nn_class_defnativemethod(state, state->classprimstring, "@iter", nn_memberfunc_string_iter);
        nn_class_defnativemethod(state, state->classprimstring, "@itern", nn_memberfunc_string_itern);
        nn_class_defnativemethod(state, state->classprimstring, "size", nn_memberfunc_string_length);
        nn_class_defnativemethod(state, state->classprimstring, "substr", nn_memberfunc_string_substring);
        nn_class_defnativemethod(state, state->classprimstring, "substring", nn_memberfunc_string_substring);
        nn_class_defnativemethod(state, state->classprimstring, "charCodeAt", nn_memberfunc_string_charcodeat);
        nn_class_defnativemethod(state, state->classprimstring, "charAt", nn_memberfunc_string_charat);
        nn_class_defnativemethod(state, state->classprimstring, "upper", nn_memberfunc_string_upper);
        nn_class_defnativemethod(state, state->classprimstring, "lower", nn_memberfunc_string_lower);
        nn_class_defnativemethod(state, state->classprimstring, "trim", nn_memberfunc_string_trim);
        nn_class_defnativemethod(state, state->classprimstring, "ltrim", nn_memberfunc_string_ltrim);
        nn_class_defnativemethod(state, state->classprimstring, "rtrim", nn_memberfunc_string_rtrim);
        nn_class_defnativemethod(state, state->classprimstring, "split", nn_memberfunc_string_split);
        nn_class_defnativemethod(state, state->classprimstring, "indexOf", nn_memberfunc_string_indexof);
        nn_class_defnativemethod(state, state->classprimstring, "count", nn_memberfunc_string_count);
        nn_class_defnativemethod(state, state->classprimstring, "toNumber", nn_memberfunc_string_tonumber);
        nn_class_defnativemethod(state, state->classprimstring, "toList", nn_memberfunc_string_tolist);
        nn_class_defnativemethod(state, state->classprimstring, "lpad", nn_memberfunc_string_lpad);
        nn_class_defnativemethod(state, state->classprimstring, "rpad", nn_memberfunc_string_rpad);
        nn_class_defnativemethod(state, state->classprimstring, "replace", nn_memberfunc_string_replace);
        nn_class_defnativemethod(state, state->classprimstring, "each", nn_memberfunc_string_each);
        nn_class_defnativemethod(state, state->classprimstring, "startswith", nn_memberfunc_string_startswith);
        nn_class_defnativemethod(state, state->classprimstring, "endswith", nn_memberfunc_string_endswith);
        nn_class_defnativemethod(state, state->classprimstring, "isAscii", nn_memberfunc_string_isascii);
        nn_class_defnativemethod(state, state->classprimstring, "isAlpha", nn_memberfunc_string_isalpha);
        nn_class_defnativemethod(state, state->classprimstring, "isAlnum", nn_memberfunc_string_isalnum);
        nn_class_defnativemethod(state, state->classprimstring, "isNumber", nn_memberfunc_string_isnumber);
        nn_class_defnativemethod(state, state->classprimstring, "isFloat", nn_memberfunc_string_isfloat);
        nn_class_defnativemethod(state, state->classprimstring, "isLower", nn_memberfunc_string_islower);
        nn_class_defnativemethod(state, state->classprimstring, "isUpper", nn_memberfunc_string_isupper);
        nn_class_defnativemethod(state, state->classprimstring, "isSpace", nn_memberfunc_string_isspace);
        
    }
    {
        #if 1
        nn_class_defnativeconstructor(state, state->classprimarray, nn_memberfunc_array_constructor);
        #endif
        nn_class_defcallablefield(state, state->classprimarray, "length", nn_memberfunc_array_length);
        nn_class_defnativemethod(state, state->classprimarray, "size", nn_memberfunc_array_length);
        nn_class_defnativemethod(state, state->classprimarray, "join", nn_memberfunc_array_join);
        nn_class_defnativemethod(state, state->classprimarray, "append", nn_memberfunc_array_append);
        nn_class_defnativemethod(state, state->classprimarray, "push", nn_memberfunc_array_append);
        nn_class_defnativemethod(state, state->classprimarray, "clear", nn_memberfunc_array_clear);
        nn_class_defnativemethod(state, state->classprimarray, "clone", nn_memberfunc_array_clone);
        nn_class_defnativemethod(state, state->classprimarray, "count", nn_memberfunc_array_count);
        nn_class_defnativemethod(state, state->classprimarray, "extend", nn_memberfunc_array_extend);
        nn_class_defnativemethod(state, state->classprimarray, "indexOf", nn_memberfunc_array_indexof);
        nn_class_defnativemethod(state, state->classprimarray, "insert", nn_memberfunc_array_insert);
        nn_class_defnativemethod(state, state->classprimarray, "pop", nn_memberfunc_array_pop);
        nn_class_defnativemethod(state, state->classprimarray, "shift", nn_memberfunc_array_shift);
        nn_class_defnativemethod(state, state->classprimarray, "removeAt", nn_memberfunc_array_removeat);
        nn_class_defnativemethod(state, state->classprimarray, "remove", nn_memberfunc_array_remove);
        nn_class_defnativemethod(state, state->classprimarray, "reverse", nn_memberfunc_array_reverse);
        nn_class_defnativemethod(state, state->classprimarray, "sort", nn_memberfunc_array_sort);
        nn_class_defnativemethod(state, state->classprimarray, "contains", nn_memberfunc_array_contains);
        nn_class_defnativemethod(state, state->classprimarray, "delete", nn_memberfunc_array_delete);
        nn_class_defnativemethod(state, state->classprimarray, "first", nn_memberfunc_array_first);
        nn_class_defnativemethod(state, state->classprimarray, "last", nn_memberfunc_array_last);
        nn_class_defnativemethod(state, state->classprimarray, "isEmpty", nn_memberfunc_array_isempty);
        nn_class_defnativemethod(state, state->classprimarray, "take", nn_memberfunc_array_take);
        nn_class_defnativemethod(state, state->classprimarray, "get", nn_memberfunc_array_get);
        nn_class_defnativemethod(state, state->classprimarray, "compact", nn_memberfunc_array_compact);
        nn_class_defnativemethod(state, state->classprimarray, "unique", nn_memberfunc_array_unique);
        nn_class_defnativemethod(state, state->classprimarray, "zip", nn_memberfunc_array_zip);
        nn_class_defnativemethod(state, state->classprimarray, "zipFrom", nn_memberfunc_array_zipfrom);
        nn_class_defnativemethod(state, state->classprimarray, "toDict", nn_memberfunc_array_todict);
        nn_class_defnativemethod(state, state->classprimarray, "each", nn_memberfunc_array_each);
        nn_class_defnativemethod(state, state->classprimarray, "map", nn_memberfunc_array_map);
        nn_class_defnativemethod(state, state->classprimarray, "filter", nn_memberfunc_array_filter);
        nn_class_defnativemethod(state, state->classprimarray, "some", nn_memberfunc_array_some);
        nn_class_defnativemethod(state, state->classprimarray, "every", nn_memberfunc_array_every);
        nn_class_defnativemethod(state, state->classprimarray, "reduce", nn_memberfunc_array_reduce);
        nn_class_defnativemethod(state, state->classprimarray, "@iter", nn_memberfunc_array_iter);
        nn_class_defnativemethod(state, state->classprimarray, "@itern", nn_memberfunc_array_itern);
    }
    {
        #if 0
        nn_class_defnativeconstructor(state, state->classprimdict, nn_memberfunc_dict_constructor);
        nn_class_defstaticnativemethod(state, state->classprimdict, "keys", nn_memberfunc_dict_keys);
        #endif
        nn_class_defnativemethod(state, state->classprimdict, "keys", nn_memberfunc_dict_keys);
        nn_class_defnativemethod(state, state->classprimdict, "size", nn_memberfunc_dict_length);
        nn_class_defnativemethod(state, state->classprimdict, "add", nn_memberfunc_dict_add);
        nn_class_defnativemethod(state, state->classprimdict, "set", nn_memberfunc_dict_set);
        nn_class_defnativemethod(state, state->classprimdict, "clear", nn_memberfunc_dict_clear);
        nn_class_defnativemethod(state, state->classprimdict, "clone", nn_memberfunc_dict_clone);
        nn_class_defnativemethod(state, state->classprimdict, "compact", nn_memberfunc_dict_compact);
        nn_class_defnativemethod(state, state->classprimdict, "contains", nn_memberfunc_dict_contains);
        nn_class_defnativemethod(state, state->classprimdict, "extend", nn_memberfunc_dict_extend);
        nn_class_defnativemethod(state, state->classprimdict, "get", nn_memberfunc_dict_get);
        nn_class_defnativemethod(state, state->classprimdict, "values", nn_memberfunc_dict_values);
        nn_class_defnativemethod(state, state->classprimdict, "remove", nn_memberfunc_dict_remove);
        nn_class_defnativemethod(state, state->classprimdict, "isEmpty", nn_memberfunc_dict_isempty);
        nn_class_defnativemethod(state, state->classprimdict, "findKey", nn_memberfunc_dict_findkey);
        nn_class_defnativemethod(state, state->classprimdict, "toList", nn_memberfunc_dict_tolist);
        nn_class_defnativemethod(state, state->classprimdict, "each", nn_memberfunc_dict_each);
        nn_class_defnativemethod(state, state->classprimdict, "filter", nn_memberfunc_dict_filter);
        nn_class_defnativemethod(state, state->classprimdict, "some", nn_memberfunc_dict_some);
        nn_class_defnativemethod(state, state->classprimdict, "every", nn_memberfunc_dict_every);
        nn_class_defnativemethod(state, state->classprimdict, "reduce", nn_memberfunc_dict_reduce);
        nn_class_defnativemethod(state, state->classprimdict, "@iter", nn_memberfunc_dict_iter);
        nn_class_defnativemethod(state, state->classprimdict, "@itern", nn_memberfunc_dict_itern);
    }
    {
        nn_class_defnativeconstructor(state, state->classprimfile, nn_memberfunc_file_constructor);
        nn_class_defstaticnativemethod(state, state->classprimfile, "exists", nn_memberfunc_file_exists);
        nn_class_defnativemethod(state, state->classprimfile, "close", nn_memberfunc_file_close);
        nn_class_defnativemethod(state, state->classprimfile, "open", nn_memberfunc_file_open);
        nn_class_defnativemethod(state, state->classprimfile, "read", nn_memberfunc_file_read);
        nn_class_defnativemethod(state, state->classprimfile, "get", nn_memberfunc_file_get);
        nn_class_defnativemethod(state, state->classprimfile, "gets", nn_memberfunc_file_gets);
        nn_class_defnativemethod(state, state->classprimfile, "write", nn_memberfunc_file_write);
        nn_class_defnativemethod(state, state->classprimfile, "puts", nn_memberfunc_file_puts);
        nn_class_defnativemethod(state, state->classprimfile, "printf", nn_memberfunc_file_printf);
        nn_class_defnativemethod(state, state->classprimfile, "number", nn_memberfunc_file_number);
        nn_class_defnativemethod(state, state->classprimfile, "isTTY", nn_memberfunc_file_istty);
        nn_class_defnativemethod(state, state->classprimfile, "isOpen", nn_memberfunc_file_isopen);
        nn_class_defnativemethod(state, state->classprimfile, "isClosed", nn_memberfunc_file_isclosed);
        nn_class_defnativemethod(state, state->classprimfile, "flush", nn_memberfunc_file_flush);
        nn_class_defnativemethod(state, state->classprimfile, "stats", nn_memberfunc_file_stats);
        nn_class_defnativemethod(state, state->classprimfile, "path", nn_memberfunc_file_path);
        nn_class_defnativemethod(state, state->classprimfile, "seek", nn_memberfunc_file_seek);
        nn_class_defnativemethod(state, state->classprimfile, "tell", nn_memberfunc_file_tell);
        nn_class_defnativemethod(state, state->classprimfile, "mode", nn_memberfunc_file_mode);
        nn_class_defnativemethod(state, state->classprimfile, "name", nn_memberfunc_file_name);
    }
    {
        nn_class_defnativeconstructor(state, state->classprimrange, nn_memberfunc_range_constructor);
        nn_class_defnativemethod(state, state->classprimrange, "lower", nn_memberfunc_range_lower);
        nn_class_defnativemethod(state, state->classprimrange, "upper", nn_memberfunc_range_upper);
        nn_class_defnativemethod(state, state->classprimrange, "range", nn_memberfunc_range_range);
        nn_class_defnativemethod(state, state->classprimrange, "loop", nn_memberfunc_range_loop);
        nn_class_defnativemethod(state, state->classprimrange, "expand", nn_memberfunc_range_expand);
        nn_class_defnativemethod(state, state->classprimrange, "toArray", nn_memberfunc_range_expand);
        nn_class_defnativemethod(state, state->classprimrange, "@iter", nn_memberfunc_range_iter);
        nn_class_defnativemethod(state, state->classprimrange, "@itern", nn_memberfunc_range_itern);
    }
}

NeonValue nn_nativefn_time(NeonState* state, NeonArguments* args)
{
    struct timeval tv;
    (void)args;
    NeonArgCheck check;
    nn_argcheck_init(state, &check, args);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    osfn_gettimeofday(&tv, NULL);
    return nn_value_makenumber((double)tv.tv_sec + ((double)tv.tv_usec / 10000000));
}

NeonValue nn_nativefn_microtime(NeonState* state, NeonArguments* args)
{
    struct timeval tv;
    (void)args;
    NeonArgCheck check;
    nn_argcheck_init(state, &check, args);

    NEON_ARGS_CHECKCOUNT(&check, 0);
    osfn_gettimeofday(&tv, NULL);
    return nn_value_makenumber((1000000 * (double)tv.tv_sec) + ((double)tv.tv_usec / 10));
}

NeonValue nn_nativefn_id(NeonState* state, NeonArguments* args)
{
    NeonValue val;
    NeonArgCheck check;
    nn_argcheck_init(state, &check, args);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    val = args->args[0];
    return nn_value_makenumber(*(long*)&val);
}

NeonValue nn_nativefn_int(NeonState* state, NeonArguments* args)
{
    NeonArgCheck check;
    nn_argcheck_init(state, &check, args);
    NEON_ARGS_CHECKCOUNTRANGE(&check, 0, 1);
    if(args->count == 0)
    {
        return nn_value_makenumber(0);
    }
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isnumber);
    return nn_value_makenumber((double)((int)nn_value_asnumber(args->args[0])));
}

NeonValue nn_nativefn_chr(NeonState* state, NeonArguments* args)
{
    size_t len;
    char* string;
    NeonArgCheck check;
    nn_argcheck_init(state, &check, args);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isnumber);
    string = nn_util_utf8encode(state, (int)nn_value_asnumber(args->args[0]), &len);
    return nn_value_fromobject(nn_string_takecstr(state, string));
}

NeonValue nn_nativefn_ord(NeonState* state, NeonArguments* args)
{
    int ord;
    int length;
    NeonObjString* string;
    NeonArgCheck check;
    nn_argcheck_init(state, &check, args);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
    string = nn_value_asstring(args->args[0]);
    length = string->sbuf->length;
    if(length > 1)
    {
        NEON_RETURNERROR("ord() expects character as argument, string given");
    }
    ord = (int)string->sbuf->data[0];
    if(ord < 0)
    {
        ord += 256;
    }
    return nn_value_makenumber(ord);
}

#define MT_STATE_SIZE 624
void nn_util_mtseed(uint32_t seed, uint32_t* binst, uint32_t* index)
{
    uint32_t i;
    binst[0] = seed;
    for(i = 1; i < MT_STATE_SIZE; i++)
    {
        binst[i] = (uint32_t)(1812433253UL * (binst[i - 1] ^ (binst[i - 1] >> 30)) + i);
    }
    *index = MT_STATE_SIZE;
}

uint32_t nn_util_mtgenerate(uint32_t* binst, uint32_t* index)
{
    uint32_t i;
    uint32_t y;
    if(*index >= MT_STATE_SIZE)
    {
        for(i = 0; i < MT_STATE_SIZE - 397; i++)
        {
            y = (binst[i] & 0x80000000) | (binst[i + 1] & 0x7fffffff);
            binst[i] = binst[i + 397] ^ (y >> 1) ^ ((y & 1) * 0x9908b0df);
        }
        for(; i < MT_STATE_SIZE - 1; i++)
        {
            y = (binst[i] & 0x80000000) | (binst[i + 1] & 0x7fffffff);
            binst[i] = binst[i + (397 - MT_STATE_SIZE)] ^ (y >> 1) ^ ((y & 1) * 0x9908b0df);
        }
        y = (binst[MT_STATE_SIZE - 1] & 0x80000000) | (binst[0] & 0x7fffffff);
        binst[MT_STATE_SIZE - 1] = binst[396] ^ (y >> 1) ^ ((y & 1) * 0x9908b0df);
        *index = 0;
    }
    y = binst[*index];
    *index = *index + 1;
    y = y ^ (y >> 11);
    y = y ^ ((y << 7) & 0x9d2c5680);
    y = y ^ ((y << 15) & 0xefc60000);
    y = y ^ (y >> 18);
    return y;
}

double nn_util_mtrand(double lowerlimit, double upperlimit)
{
    double randnum;
    uint32_t randval;
    struct timeval tv;
    static uint32_t mtstate[MT_STATE_SIZE];
    static uint32_t mtindex = MT_STATE_SIZE + 1;
    if(mtindex >= MT_STATE_SIZE)
    {
        osfn_gettimeofday(&tv, NULL);
        nn_util_mtseed((uint32_t)(1000000 * tv.tv_sec + tv.tv_usec), mtstate, &mtindex);
    }
    randval = nn_util_mtgenerate(mtstate, &mtindex);
    randnum = lowerlimit + ((double)randval / UINT32_MAX) * (upperlimit - lowerlimit);
    return randnum;
}

NeonValue nn_nativefn_rand(NeonState* state, NeonArguments* args)
{
    int tmp;
    int lowerlimit;
    int upperlimit;
    NeonArgCheck check;
    nn_argcheck_init(state, &check, args);
    NEON_ARGS_CHECKCOUNTRANGE(&check, 0, 2);
    lowerlimit = 0;
    upperlimit = 1;
    if(args->count > 0)
    {
        NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isnumber);
        lowerlimit = nn_value_asnumber(args->args[0]);
    }
    if(args->count == 2)
    {
        NEON_ARGS_CHECKTYPE(&check, 1, nn_value_isnumber);
        upperlimit = nn_value_asnumber(args->args[1]);
    }
    if(lowerlimit > upperlimit)
    {
        tmp = upperlimit;
        upperlimit = lowerlimit;
        lowerlimit = tmp;
    }
    return nn_value_makenumber(nn_util_mtrand(lowerlimit, upperlimit));
}

NeonValue nn_nativefn_typeof(NeonState* state, NeonArguments* args)
{
    const char* result;
    NeonArgCheck check;
    nn_argcheck_init(state, &check, args);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    result = nn_value_typename(args->args[0]);
    return nn_value_fromobject(nn_string_copycstr(state, result));
}

NeonValue nn_nativefn_eval(NeonState* state, NeonArguments* args)
{
    NeonValue result;
    NeonObjString* os;
    NeonArgCheck check;
    nn_argcheck_init(state, &check, args);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    os = nn_value_asstring(args->args[0]);
    /*fprintf(stderr, "eval:src=%s\n", os->sbuf->data);*/
    result = nn_state_evalsource(state, os->sbuf->data);
    return result;
}

/*
NeonValue nn_nativefn_loadfile(NeonState* state, NeonArguments* args)
{
    NeonValue result;
    NeonObjString* os;
    NeonArgCheck check;
    nn_argcheck_init(state, &check, args);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    os = nn_value_asstring(args->args[0]);
    fprintf(stderr, "eval:src=%s\n", os->sbuf->data);
    result = nn_state_evalsource(state, os->sbuf->data);
    return result;
}
*/

NeonValue nn_nativefn_instanceof(NeonState* state, NeonArguments* args)
{
    NeonArgCheck check;
    nn_argcheck_init(state, &check, args);
    NEON_ARGS_CHECKCOUNT(&check, 2);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isinstance);
    NEON_ARGS_CHECKTYPE(&check, 1, nn_value_isclass);
    return nn_value_makebool(nn_util_isinstanceof(nn_value_asinstance(args->args[0])->klass, nn_value_asclass(args->args[1])));
}


void nn_strformat_init(NeonState* state, NeonFormatInfo* nfi, NeonPrinter* writer, const char* fmtstr, size_t fmtlen)
{
    nfi->pvm = state;
    nfi->fmtstr = fmtstr;
    nfi->fmtlen = fmtlen;
    nfi->writer = writer;
}

void nn_strformat_destroy(NeonFormatInfo* nfi)
{
    (void)nfi;
}

bool nn_strformat_format(NeonFormatInfo* nfi, int argc, int argbegin, NeonValue* argv)
{
    int ch;
    int ival;
    int nextch;
    bool failed;
    size_t i;
    size_t argpos;
    NeonValue cval;
    i = 0;
    argpos = argbegin;
    failed = false;
    while(i < nfi->fmtlen)
    {
        ch = nfi->fmtstr[i];
        nextch = -1;
        if((i + 1) < nfi->fmtlen)
        {
            nextch = nfi->fmtstr[i+1];
        }
        i++;
        if(ch == '%')
        {
            if(nextch == '%')
            {
                nn_printer_writechar(nfi->writer, '%');
            }
            else
            {
                i++;
                if((int)argpos > argc)
                {
                    failed = true;
                    cval = nn_value_makeempty();
                }
                else
                {
                    cval = argv[argpos];
                }
                argpos++;
                switch(nextch)
                {
                    case 'q':
                    case 'p':
                        {
                            nn_printer_printvalue(nfi->writer, cval, true, true);
                        }
                        break;
                    case 'c':
                        {
                            ival = (int)nn_value_asnumber(cval);
                            nn_printer_writefmt(nfi->writer, "%c", ival);
                        }
                        break;
                    /* TODO: implement actual field formatting */
                    case 's':
                    case 'd':
                    case 'i':
                    case 'g':
                        {
                            nn_printer_printvalue(nfi->writer, cval, false, true);
                        }
                        break;
                    default:
                        {
                            nn_exceptions_throw(nfi->pvm, "unknown/invalid format flag '%%c'", nextch);
                        }
                        break;
                }
            }
        }
        else
        {
            nn_printer_writechar(nfi->writer, ch);
        }
    }
    return failed;
}

NeonValue nn_nativefn_sprintf(NeonState* state, NeonArguments* args)
{
    NeonFormatInfo nfi;
    NeonPrinter pr;
    NeonObjString* res;
    NeonObjString* ofmt;
    NeonArgCheck check;
    nn_argcheck_init(state, &check, args);
    NEON_ARGS_CHECKMINARG(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
    ofmt = nn_value_asstring(args->args[0]);
    nn_printer_makestackstring(state, &pr);
    nn_strformat_init(state, &nfi, &pr, nn_string_getcstr(ofmt), nn_string_getlength(ofmt));
    if(!nn_strformat_format(&nfi, args->count, 1, args->args))
    {
        return nn_value_makenull();
    }
    res = nn_printer_takestring(&pr);
    return nn_value_fromobject(res);
}

NeonValue nn_nativefn_printf(NeonState* state, NeonArguments* args)
{
    NeonFormatInfo nfi;
    NeonObjString* ofmt;
    NeonArgCheck check;
    nn_argcheck_init(state, &check, args);
    NEON_ARGS_CHECKMINARG(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
    ofmt = nn_value_asstring(args->args[0]);
    nn_strformat_init(state, &nfi, state->stdoutprinter, nn_string_getcstr(ofmt), nn_string_getlength(ofmt));
    if(!nn_strformat_format(&nfi, args->count, 1, args->args))
    {
    }
    return nn_value_makenull();
}

NeonValue nn_nativefn_print(NeonState* state, NeonArguments* args)
{
    int i;
    for(i = 0; i < args->count; i++)
    {
        nn_printer_printvalue(state->stdoutprinter, args->args[i], false, true);
    }
    if(state->isrepl)
    {
        nn_printer_writestring(state->stdoutprinter, "\n");
    }
    return nn_value_makeempty();
}

NeonValue nn_nativefn_println(NeonState* state, NeonArguments* args)
{
    NeonValue v;
    v = nn_nativefn_print(state, args);
    nn_printer_writestring(state->stdoutprinter, "\n");
    return v;
}

void nn_state_initbuiltinfunctions(NeonState* state)
{
    nn_state_defnativefunction(state, "chr", nn_nativefn_chr);
    nn_state_defnativefunction(state, "id", nn_nativefn_id);
    nn_state_defnativefunction(state, "int", nn_nativefn_int);
    nn_state_defnativefunction(state, "instanceof", nn_nativefn_instanceof);
    nn_state_defnativefunction(state, "microtime", nn_nativefn_microtime);
    nn_state_defnativefunction(state, "ord", nn_nativefn_ord);
    nn_state_defnativefunction(state, "sprintf", nn_nativefn_sprintf);
    nn_state_defnativefunction(state, "printf", nn_nativefn_printf);
    nn_state_defnativefunction(state, "print", nn_nativefn_print);
    nn_state_defnativefunction(state, "println", nn_nativefn_println);
    nn_state_defnativefunction(state, "rand", nn_nativefn_rand);
    nn_state_defnativefunction(state, "time", nn_nativefn_time);
    nn_state_defnativefunction(state, "eval", nn_nativefn_eval);    
}

void nn_state_vwarn(NeonState* state, const char* fmt, va_list va)
{
    if(state->conf.enablewarnings)
    {
        fprintf(stderr, "WARNING: ");
        vfprintf(stderr, fmt, va);
        fprintf(stderr, "\n");
    }
}

void nn_state_warn(NeonState* state, const char* fmt, ...)
{
    va_list va;
    va_start(va, fmt);
    nn_state_vwarn(state, fmt, va);
    va_end(va);
}

NeonValue nn_exceptions_getstacktrace(NeonState* state)
{
    int line;
    size_t i;
    size_t instruction;
    const char* fnname;
    const char* physfile;
    NeonCallFrame* frame;
    NeonObjFuncScript* function;
    NeonObjString* os;
    NeonObjArray* oa;
    NeonPrinter pr;
    oa = nn_object_makearray(state);
    {
        for(i = 0; i < state->vmstate.framecount; i++)
        {
            nn_printer_makestackstring(state, &pr);
            frame = &state->vmstate.framevalues[i];
            function = frame->closure->scriptfunc;
            /* -1 because the IP is sitting on the next instruction to be executed */
            instruction = frame->inscode - function->blob.instrucs - 1;
            line = function->blob.instrucs[instruction].srcline;
            physfile = "(unknown)";
            if(function->module->physicalpath != NULL)
            {
                if(function->module->physicalpath->sbuf != NULL)
                {
                    physfile = function->module->physicalpath->sbuf->data;
                }
            }
            fnname = "<script>";
            if(function->name != NULL)
            {
                fnname = function->name->sbuf->data;
            }
            nn_printer_writefmt(&pr, "from %s() in %s:%d", fnname, physfile, line);
            os = nn_printer_takestring(&pr);
            nn_array_push(oa, nn_value_fromobject(os));
            if((i > 15) && (state->conf.showfullstack == false))
            {
                nn_printer_makestackstring(state, &pr);
                nn_printer_writefmt(&pr, "(only upper 15 entries shown)");
                os = nn_printer_takestring(&pr);
                nn_array_push(oa, nn_value_fromobject(os));
                break;
            }
        }
        return nn_value_fromobject(oa);
    }
    return nn_value_fromobject(nn_string_copylen(state, "", 0));
}

bool nn_exceptions_propagate(NeonState* state)
{
    int i;
    int cnt;
    int srcline;
/*
{
    NEON_COLOR_RESET,
    NEON_COLOR_RED,
    NEON_COLOR_GREEN,
    NEON_COLOR_YELLOW,
    NEON_COLOR_BLUE,
    NEON_COLOR_MAGENTA,
    NEON_COLOR_CYAN
}
*/
    const char* colred;
    const char* colreset;
    const char* colyellow;
    const char* srcfile;
    NeonValue stackitm;
    NeonObjArray* oa;
    NeonObjFuncScript* function;
    NeonExceptionFrame* handler;
    NeonObjString* emsg;
    NeonObjInstance* exception;
    NeonProperty* field;
    exception = nn_value_asinstance(nn_vm_stackpeek(state, 0));
    while(state->vmstate.framecount > 0)
    {
        state->vmstate.currentframe = &state->vmstate.framevalues[state->vmstate.framecount - 1];
        for(i = state->vmstate.currentframe->handlercount; i > 0; i--)
        {
            handler = &state->vmstate.currentframe->handlers[i - 1];
            function = state->vmstate.currentframe->closure->scriptfunc;
            if(handler->address != 0 && nn_util_isinstanceof(exception->klass, handler->klass))
            {
                state->vmstate.currentframe->inscode = &function->blob.instrucs[handler->address];
                return true;
            }
            else if(handler->finallyaddress != 0)
            {
                /* continue propagating once the 'finally' block completes */
                nn_vm_stackpush(state, nn_value_makebool(true));
                state->vmstate.currentframe->inscode = &function->blob.instrucs[handler->finallyaddress];
                return true;
            }
        }
        state->vmstate.framecount--;
    }
    colred = nn_util_color(NEON_COLOR_RED);
    colreset = nn_util_color(NEON_COLOR_RESET);
    colyellow = nn_util_color(NEON_COLOR_YELLOW);
    /* at this point, the exception is unhandled; so, print it out. */
    fprintf(stderr, "%sunhandled %s%s", colred, exception->klass->name->sbuf->data, colreset);
    srcfile = "none";
    srcline = 0;
    field = nn_table_getfieldbycstr(exception->properties, "srcline");
    if(field != NULL)
    {
        srcline = nn_value_asnumber(field->value);
    }
    field = nn_table_getfieldbycstr(exception->properties, "srcfile");
    if(field != NULL)
    {
        srcfile = nn_value_asstring(field->value)->sbuf->data;
    }
    fprintf(stderr, " [from native %s%s:%d%s]", colyellow, srcfile, srcline, colreset);
    
    field = nn_table_getfieldbycstr(exception->properties, "message");
    if(field != NULL)
    {
        emsg = nn_value_tostring(state, field->value);
        if(emsg->sbuf->length > 0)
        {
            fprintf(stderr, ": %s", emsg->sbuf->data);
        }
        else
        {
            fprintf(stderr, ":");
        }
        fprintf(stderr, "\n");
    }
    else
    {
        fprintf(stderr, "\n");
    }
    field = nn_table_getfieldbycstr(exception->properties, "stacktrace");
    if(field != NULL)
    {
        fprintf(stderr, "  stacktrace:\n");
        oa = nn_value_asarray(field->value);
        cnt = oa->varray->count;
        i = cnt-1;
        if(cnt > 0)
        {
            while(true)
            {
                stackitm = oa->varray->values[i];
                nn_printer_writefmt(state->debugwriter, "  ");
                nn_printer_printvalue(state->debugwriter, stackitm, false, true);
                nn_printer_writefmt(state->debugwriter, "\n");
                if(i == 0)
                {
                    break;
                }
                i--;
            }
        }
    }
    return false;
}

bool nn_exceptions_pushhandler(NeonState* state, NeonObjClass* type, int address, int finallyaddress)
{
    NeonCallFrame* frame;
    frame = &state->vmstate.framevalues[state->vmstate.framecount - 1];
    if(frame->handlercount == NEON_CFG_MAXEXCEPTHANDLERS)
    {
        nn_vm_raisefatalerror(state, "too many nested exception handlers in one function");
        return false;
    }
    frame->handlers[frame->handlercount].address = address;
    frame->handlers[frame->handlercount].finallyaddress = finallyaddress;
    frame->handlers[frame->handlercount].klass = type;
    frame->handlercount++;
    return true;
}


bool nn_exceptions_vthrowactual(NeonState* state, NeonObjClass* klass, const char* srcfile, int srcline, const char* format, va_list va)
{
    bool b;
    b = nn_exceptions_vthrowwithclass(state, klass, srcfile, srcline, format, va);
    return b;
}

bool nn_exceptions_throwactual(NeonState* state, NeonObjClass* klass, const char* srcfile, int srcline, const char* format, ...)
{
    bool b;
    va_list va;
    va_start(va, format);
    b = nn_exceptions_vthrowactual(state, klass, srcfile, srcline, format, va);
    va_end(va);
    return b;
}

bool nn_exceptions_throwwithclass(NeonState* state, NeonObjClass* klass, const char* srcfile, int srcline, const char* format, ...)
{
    bool b;
    va_list args;
    va_start(args, format);
    b = nn_exceptions_vthrowwithclass(state, klass, srcfile, srcline, format, args);
    va_end(args);
    return b;
}

bool nn_exceptions_vthrowwithclass(NeonState* state, NeonObjClass* exklass, const char* srcfile, int srcline, const char* format, va_list args)
{
    int length;
    int needed;
    char* message;
    va_list vcpy;
    NeonValue stacktrace;
    NeonObjInstance* instance;
    va_copy(vcpy, args);
    /* TODO: used to be vasprintf. need to check how much to actually allocate! */
    needed = vsnprintf(NULL, 0, format, vcpy);
    needed += 1;
    va_end(vcpy);
    message = (char*)nn_util_memmalloc(state, needed+1);
    length = vsnprintf(message, needed, format, args);
    instance = nn_exceptions_makeinstance(state, exklass, srcfile, srcline, nn_string_takelen(state, message, length));
    nn_vm_stackpush(state, nn_value_fromobject(instance));
    stacktrace = nn_exceptions_getstacktrace(state);
    nn_vm_stackpush(state, stacktrace);
    nn_instance_defproperty(instance, "stacktrace", stacktrace);
    nn_vm_stackpop(state);
    return nn_exceptions_propagate(state);
}

static NEON_FORCEINLINE NeonInstruction nn_util_makeinst(bool isop, uint8_t code, int srcline)
{
    NeonInstruction inst;
    inst.isop = isop;
    inst.code = code;
    inst.srcline = srcline;
    return inst;
}

NeonObjClass* nn_exceptions_makeclass(NeonState* state, NeonObjModule* module, const char* cstrname)
{
    int messageconst;
    NeonObjClass* klass;
    NeonObjString* classname;
    NeonObjFuncScript* function;
    NeonObjFuncClosure* closure;
    classname = nn_string_copycstr(state, cstrname);
    nn_vm_stackpush(state, nn_value_fromobject(classname));
    klass = nn_object_makeclass(state, classname);
    nn_vm_stackpop(state);
    nn_vm_stackpush(state, nn_value_fromobject(klass));
    function = nn_object_makefuncscript(state, module, NEON_FUNCTYPE_METHOD);
    function->arity = 1;
    function->isvariadic = false;
    nn_vm_stackpush(state, nn_value_fromobject(function));
    {
        /* g_loc 0 */
        nn_blob_push(state, &function->blob, nn_util_makeinst(true, NEON_OP_LOCALGET, 0));
        nn_blob_push(state, &function->blob, nn_util_makeinst(false, (0 >> 8) & 0xff, 0));
        nn_blob_push(state, &function->blob, nn_util_makeinst(false, 0 & 0xff, 0));
    }
    {
        /* g_loc 1 */
        nn_blob_push(state, &function->blob, nn_util_makeinst(true, NEON_OP_LOCALGET, 0));
        nn_blob_push(state, &function->blob, nn_util_makeinst(false, (1 >> 8) & 0xff, 0));
        nn_blob_push(state, &function->blob, nn_util_makeinst(false, 1 & 0xff, 0));
    }
    {
        messageconst = nn_blob_pushconst(state, &function->blob, nn_value_fromobject(nn_string_copycstr(state, "message")));
        /* s_prop 0 */
        nn_blob_push(state, &function->blob, nn_util_makeinst(true, NEON_OP_PROPERTYSET, 0));
        nn_blob_push(state, &function->blob, nn_util_makeinst(false, (messageconst >> 8) & 0xff, 0));
        nn_blob_push(state, &function->blob, nn_util_makeinst(false, messageconst & 0xff, 0));
    }
    {
        /* pop */
        nn_blob_push(state, &function->blob, nn_util_makeinst(true, NEON_OP_POPONE, 0));
        nn_blob_push(state, &function->blob, nn_util_makeinst(true, NEON_OP_POPONE, 0));
    }
    {
        /* g_loc 0 */
        /*
        //  nn_blob_push(state, &function->blob, nn_util_makeinst(true, NEON_OP_LOCALGET, 0));
        //  nn_blob_push(state, &function->blob, nn_util_makeinst(false, (0 >> 8) & 0xff, 0));
        //  nn_blob_push(state, &function->blob, nn_util_makeinst(false, 0 & 0xff, 0));
        */
    }
    {
        /* ret */
        nn_blob_push(state, &function->blob, nn_util_makeinst(true, NEON_OP_RETURN, 0));
    }
    closure = nn_object_makefuncclosure(state, function);
    nn_vm_stackpop(state);
    /* set class constructor */
    nn_vm_stackpush(state, nn_value_fromobject(closure));
    nn_table_set(klass->methods, nn_value_fromobject(classname), nn_value_fromobject(closure));
    klass->constructor = nn_value_fromobject(closure);
    /* set class properties */
    nn_class_defproperty(klass, "message", nn_value_makenull());
    nn_class_defproperty(klass, "stacktrace", nn_value_makenull());
    nn_table_set(state->globals, nn_value_fromobject(classname), nn_value_fromobject(klass));
    /* for class */
    nn_vm_stackpop(state);
    nn_vm_stackpop(state);
    /* assert error name */
    /* nn_vm_stackpop(state); */
    return klass;
}

NeonObjInstance* nn_exceptions_makeinstance(NeonState* state, NeonObjClass* exklass, const char* srcfile, int srcline, NeonObjString* message)
{
    NeonObjInstance* instance;
    NeonObjString* osfile;
    instance = nn_object_makeinstance(state, exklass);
    osfile = nn_string_copycstr(state, srcfile);
    nn_vm_stackpush(state, nn_value_fromobject(instance));
    nn_instance_defproperty(instance, "message", nn_value_fromobject(message));
    nn_instance_defproperty(instance, "srcfile", nn_value_fromobject(osfile));
    nn_instance_defproperty(instance, "srcline", nn_value_makenumber(srcline));
    nn_vm_stackpop(state);
    return instance;
}

void nn_vm_raisefatalerror(NeonState* state, const char* format, ...)
{
    int i;
    int line;
    size_t instruction;
    va_list args;
    NeonCallFrame* frame;
    NeonObjFuncScript* function;
    /* flush out anything on stdout first */
    fflush(stdout);
    frame = &state->vmstate.framevalues[state->vmstate.framecount - 1];
    function = frame->closure->scriptfunc;
    instruction = frame->inscode - function->blob.instrucs - 1;
    line = function->blob.instrucs[instruction].srcline;
    fprintf(stderr, "RuntimeError: ");
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fprintf(stderr, " -> %s:%d ", function->module->physicalpath->sbuf->data, line);
    fputs("\n", stderr);
    if(state->vmstate.framecount > 1)
    {
        fprintf(stderr, "stacktrace:\n");
        for(i = state->vmstate.framecount - 1; i >= 0; i--)
        {
            frame = &state->vmstate.framevalues[i];
            function = frame->closure->scriptfunc;
            /* -1 because the IP is sitting on the next instruction to be executed */
            instruction = frame->inscode - function->blob.instrucs - 1;
            fprintf(stderr, "    %s:%d -> ", function->module->physicalpath->sbuf->data, function->blob.instrucs[instruction].srcline);
            if(function->name == NULL)
            {
                fprintf(stderr, "<script>");
            }
            else
            {
                fprintf(stderr, "%s()", function->name->sbuf->data);
            }
            fprintf(stderr, "\n");
        }
    }
    nn_state_resetvmstate(state);
}

void nn_state_defglobalvalue(NeonState* state, const char* name, NeonValue val)
{
    NeonObjString* oname;
    oname = nn_string_copycstr(state, name);
    nn_vm_stackpush(state, nn_value_fromobject(oname));
    nn_vm_stackpush(state, val);
    nn_table_set(state->globals, state->vmstate.stackvalues[0], state->vmstate.stackvalues[1]);
    nn_vm_stackpopn(state, 2);
}

void nn_state_defnativefunctionptr(NeonState* state, const char* name, NeonNativeFN fptr, void* uptr)
{
    NeonObjFuncNative* func;
    func = nn_object_makefuncnative(state, fptr, name, uptr);
    return nn_state_defglobalvalue(state, name, nn_value_fromobject(func));
}

void nn_state_defnativefunction(NeonState* state, const char* name, NeonNativeFN fptr)
{
    return nn_state_defnativefunctionptr(state, name, fptr, NULL);
}

NeonObjClass* nn_util_makeclass(NeonState* state, const char* name, NeonObjClass* parent)
{
    NeonObjClass* cl;
    NeonObjString* os;
    os = nn_string_copycstr(state, name);
    cl = nn_object_makeclass(state, os);
    cl->superclass = parent;
    nn_table_set(state->globals, nn_value_fromobject(os), nn_value_fromobject(cl));
    return cl;
}

void nn_vm_initvmstate(NeonState* state)
{
    state->vmstate.linkedobjects = NULL;
    state->vmstate.currentframe = NULL;
    {
        state->vmstate.stackcapacity = NEON_CFG_INITSTACKCOUNT;
        state->vmstate.stackvalues = (NeonValue*)nn_util_memmalloc(state, NEON_CFG_INITSTACKCOUNT * sizeof(NeonValue));
        if(state->vmstate.stackvalues == NULL)
        {
            fprintf(stderr, "error: failed to allocate stackvalues!\n");
            abort();
        }
        memset(state->vmstate.stackvalues, 0, NEON_CFG_INITSTACKCOUNT * sizeof(NeonValue));
    }
    {
        state->vmstate.framecapacity = NEON_CFG_INITFRAMECOUNT;
        state->vmstate.framevalues = (NeonCallFrame*)nn_util_memmalloc(state, NEON_CFG_INITFRAMECOUNT * sizeof(NeonCallFrame));
        if(state->vmstate.framevalues == NULL)
        {
            fprintf(stderr, "error: failed to allocate framevalues!\n");
            abort();
        }
        memset(state->vmstate.framevalues, 0, NEON_CFG_INITFRAMECOUNT * sizeof(NeonCallFrame));
    }
}

bool nn_vm_checkmayberesize(NeonState* state)
{
    if((state->vmstate.stackidx+1) >= state->vmstate.stackcapacity)
    {
        if(!nn_vm_resizestack(state, state->vmstate.stackidx + 1))
        {
            return nn_exceptions_throw(state, "failed to resize stack due to overflow");
        }
        return true;
    }
    if(state->vmstate.framecount >= state->vmstate.framecapacity)
    {
        if(!nn_vm_resizeframes(state, state->vmstate.framecapacity + 1))
        {
            return nn_exceptions_throw(state, "failed to resize frames due to overflow");
        }
        return true;
    }
    return false;
}

/*
* grows vmstate.(stack|frame)values, respectively.
* currently it works fine with mob.js (man-or-boy test), although
* there are still some invalid reads regarding the closure;
* almost definitely because the pointer address changes.
*
* currently, the implementation really does just increase the
* memory block available:
* i.e., [NeonValue x 32] -> [NeonValue x <newsize>], without
* copying anything beyond primitive values.
*/
bool nn_vm_resizestack(NeonState* state, size_t needed)
{
    size_t oldsz;
    size_t newsz;
    size_t allocsz;
    size_t nforvals;
    NeonValue* oldbuf;
    NeonValue* newbuf;
    nforvals = (needed * 2);
    oldsz = state->vmstate.stackcapacity;
    newsz = oldsz + nforvals;
    allocsz = ((newsz + 1) * sizeof(NeonValue));
    fprintf(stderr, "*** resizing stack: needed %ld, from %ld to %ld, allocating %ld ***\n", nforvals, oldsz, newsz, allocsz);
    oldbuf = state->vmstate.stackvalues;
    newbuf = (NeonValue*)nn_util_memrealloc(state, oldbuf, allocsz);
    if(newbuf == NULL)
    {
        fprintf(stderr, "internal error: failed to resize stackvalues!\n");
        abort();
    }
    state->vmstate.stackvalues = (NeonValue*)newbuf;
    state->vmstate.stackcapacity = newsz;
    return true;
}

bool nn_vm_resizeframes(NeonState* state, size_t needed)
{
    /* return false; */
    size_t i;
    size_t oldsz;
    size_t newsz;
    size_t allocsz;
    int oldhandlercnt;
    NeonInstruction* oldip;
    NeonObjFuncClosure* oldclosure;
    NeonCallFrame* oldbuf;
    NeonCallFrame* newbuf;
    (void)i;
    fprintf(stderr, "*** resizing frames ***\n");
    oldclosure = state->vmstate.currentframe->closure;
    oldip = state->vmstate.currentframe->inscode;
    oldhandlercnt = state->vmstate.currentframe->handlercount;
    oldsz = state->vmstate.framecapacity;
    newsz = oldsz + needed;
    allocsz = ((newsz + 1) * sizeof(NeonCallFrame));
    #if 1
        oldbuf = state->vmstate.framevalues;
        newbuf = (NeonCallFrame*)nn_util_memrealloc(state, oldbuf, allocsz);
        if(newbuf == NULL)
        {
            fprintf(stderr, "internal error: failed to resize framevalues!\n");
            abort();
        }
    #endif
    state->vmstate.framevalues = (NeonCallFrame*)newbuf;
    state->vmstate.framecapacity = newsz;
    /*
    * this bit is crucial: realloc changes pointer addresses, and to keep the
    * current frame, re-read it from the new address.
    */
    state->vmstate.currentframe = &state->vmstate.framevalues[state->vmstate.framecount - 1];
    state->vmstate.currentframe->handlercount = oldhandlercnt;
    state->vmstate.currentframe->inscode = oldip;
    state->vmstate.currentframe->closure = oldclosure;
    return true;
}

void nn_state_resetvmstate(NeonState* state)
{
    state->vmstate.framecount = 0;
    state->vmstate.stackidx = 0;
    state->vmstate.openupvalues = NULL;
}

bool nn_state_addsearchpathobj(NeonState* state, NeonObjString* os)
{
    nn_valarray_push(state->importpath, nn_value_fromobject(os));
    return true;
}

bool nn_state_addsearchpath(NeonState* state, const char* path)
{
    return nn_state_addsearchpathobj(state, nn_string_copycstr(state, path));
}

NeonState* nn_state_make()
{
    return nn_state_makewithuserptr(NULL);
}

NeonState* nn_state_makewithuserptr(void* userptr)
{
    static const char* defaultsearchpaths[] =
    {
        "mods",
        "mods/@/index" NEON_CFG_FILEEXT,
        ".",
        NULL
    };
    int i;
    NeonState* state;
    state = (NeonState*)nn_util_rawmalloc(userptr, sizeof(NeonState));
    if(state == NULL)
    {
        return NULL;
    }
    memset(state, 0, sizeof(NeonState));
    state->memuserptr = userptr;
    state->exceptions.stdexception = NULL;
    state->rootphysfile = NULL;
    state->cliargv = NULL;
    state->isrepl = false;
    state->markvalue = true;
    nn_vm_initvmstate(state);
    nn_state_resetvmstate(state);
    {
        state->conf.enablestrictmode = false;
        state->conf.shoulddumpstack = false;
        state->conf.enablewarnings = false;
        state->conf.dumpbytecode = false;
        state->conf.exitafterbytecode = false;
        state->conf.showfullstack = false;
        state->conf.enableapidebug = false;
        state->conf.enableastdebug = false;
    }
    {
        state->gcstate.bytesallocated = 0;
        /* default is 1mb. Can be modified via the -g flag. */
        state->gcstate.nextgc = NEON_CFG_DEFAULTGCSTART;
        state->gcstate.graycount = 0;
        state->gcstate.graycapacity = 0;
        state->gcstate.graystack = NULL;
    }
    {
        state->stdoutprinter = nn_printer_makeio(state, stdout, false);
        state->stdoutprinter->shouldflush = true;
        state->stderrprinter = nn_printer_makeio(state, stderr, false);
        state->debugwriter = nn_printer_makeio(state, stderr, false);
        state->debugwriter->shortenvalues = true;
        state->debugwriter->maxvallength = 15;
    }
    {
        state->modules = nn_table_make(state);
        state->strings = nn_table_make(state);
        state->globals = nn_table_make(state);
    }
    {
        state->topmodule = nn_module_make(state, "", "<state>", false);
        state->constructorname = nn_string_copycstr(state, "constructor");
    }
    {
        state->importpath = nn_valarray_make(state);
        for(i=0; defaultsearchpaths[i]!=NULL; i++)
        {
            nn_state_addsearchpath(state, defaultsearchpaths[i]);
        }
    }
    {
        state->classprimobject = nn_util_makeclass(state, "Object", NULL);
        state->classprimprocess = nn_util_makeclass(state, "Process", state->classprimobject);
        state->classprimnumber = nn_util_makeclass(state, "Number", state->classprimobject);
        state->classprimstring = nn_util_makeclass(state, "String", state->classprimobject);
        state->classprimarray = nn_util_makeclass(state, "Array", state->classprimobject);
        state->classprimdict = nn_util_makeclass(state, "Dict", state->classprimobject);
        state->classprimfile = nn_util_makeclass(state, "File", state->classprimobject);
        state->classprimrange = nn_util_makeclass(state, "Range", state->classprimobject);
    }
    {
        state->envdict = nn_object_makedict(state);
    }
    {
        if(state->exceptions.stdexception == NULL)
        {
            state->exceptions.stdexception = nn_exceptions_makeclass(state, NULL, "Exception");
        }
        state->exceptions.asserterror = nn_exceptions_makeclass(state, NULL, "AssertError");
        state->exceptions.syntaxerror = nn_exceptions_makeclass(state, NULL, "SyntaxError");
        state->exceptions.ioerror = nn_exceptions_makeclass(state, NULL, "IOError");
        state->exceptions.oserror = nn_exceptions_makeclass(state, NULL, "OSError");
        state->exceptions.argumenterror = nn_exceptions_makeclass(state, NULL, "ArgumentError");
    }
    {
        nn_state_initbuiltinfunctions(state);
        nn_state_initbuiltinmethods(state);
    }
    {
        {
            state->filestdout = nn_object_makefile(state, stdout, true, "<stdout>", "wb");
            nn_state_defglobalvalue(state, "STDOUT", nn_value_fromobject(state->filestdout));
        }
        {
            state->filestderr = nn_object_makefile(state, stderr, true, "<stderr>", "wb");
            nn_state_defglobalvalue(state, "STDERR", nn_value_fromobject(state->filestderr));
        }
        {
            state->filestdin = nn_object_makefile(state, stdin, true, "<stdin>", "rb");
            nn_state_defglobalvalue(state, "STDIN", nn_value_fromobject(state->filestdin));
        }
    }
    return state;
}

#if 0
    #define destrdebug(...) \
        { \
            fprintf(stderr, "in nn_state_destroy: "); \
            fprintf(stderr, __VA_ARGS__); \
            fprintf(stderr, "\n"); \
        }
#else
    #define destrdebug(...)     
#endif
void nn_state_destroy(NeonState* state)
{
    destrdebug("destroying importpath...");
    nn_valarray_destroy(state->importpath);
    destrdebug("destroying linked objects...");
    nn_gcmem_destroylinkedobjects(state);
    /* since object in module can exist in globals, it must come before */
    destrdebug("destroying module table...");
    nn_table_destroy(state->modules);
    destrdebug("destroying globals table...");
    nn_table_destroy(state->globals);
    destrdebug("destroying strings table...");
    nn_table_destroy(state->strings);
    destrdebug("destroying stdoutprinter...");
    nn_printer_destroy(state->stdoutprinter);
    destrdebug("destroying stderrprinter...");
    nn_printer_destroy(state->stderrprinter);
    destrdebug("destroying debugwriter...");
    nn_printer_destroy(state->debugwriter);
    destrdebug("destroying framevalues...");
    nn_util_memfree(state, state->vmstate.framevalues);
    destrdebug("destroying stackvalues...");
    nn_util_memfree(state, state->vmstate.stackvalues);
    destrdebug("destroying state...");
    nn_util_memfree(state, state);
    destrdebug("done destroying!");
}

bool nn_util_methodisprivate(NeonObjString* name)
{
    return name->sbuf->length > 0 && name->sbuf->data[0] == '_';
}

bool nn_vm_callclosure(NeonState* state, NeonObjFuncClosure* closure, NeonValue thisval, int argcount)
{
    int i;
    int startva;
    NeonCallFrame* frame;
    NeonObjArray* argslist;
    NEON_APIDEBUG(state, "thisval.type=%s, argcount=%d", nn_value_typename(thisval), argcount);
    /* fill empty parameters if not variadic */
    for(; !closure->scriptfunc->isvariadic && argcount < closure->scriptfunc->arity; argcount++)
    {
        nn_vm_stackpush(state, nn_value_makenull());
    }
    /* handle variadic arguments... */
    if(closure->scriptfunc->isvariadic && argcount >= closure->scriptfunc->arity - 1)
    {
        startva = argcount - closure->scriptfunc->arity;
        argslist = nn_object_makearray(state);
        nn_vm_stackpush(state, nn_value_fromobject(argslist));
        for(i = startva; i >= 0; i--)
        {
            nn_valarray_push(argslist->varray, nn_vm_stackpeek(state, i + 1));
        }
        argcount -= startva;
        /* +1 for the gc protection push above */
        nn_vm_stackpopn(state, startva + 2);
        nn_vm_stackpush(state, nn_value_fromobject(argslist));
    }
    if(argcount != closure->scriptfunc->arity)
    {
        nn_vm_stackpopn(state, argcount);
        if(closure->scriptfunc->isvariadic)
        {
            return nn_exceptions_throw(state, "expected at least %d arguments but got %d", closure->scriptfunc->arity - 1, argcount);
        }
        else
        {
            return nn_exceptions_throw(state, "expected %d arguments but got %d", closure->scriptfunc->arity, argcount);
        }
    }
    if(nn_vm_checkmayberesize(state))
    {
        /* nn_vm_stackpopn(state, argcount); */
    }
    frame = &state->vmstate.framevalues[state->vmstate.framecount++];
    frame->closure = closure;
    frame->inscode = closure->scriptfunc->blob.instrucs;
    frame->stackslotpos = state->vmstate.stackidx + (-argcount - 1);
    return true;
}

bool nn_vm_callnative(NeonState* state, NeonObjFuncNative* native, NeonValue thisval, int argcount)
{
    size_t spos;
    NeonValue r;
    NeonValue* vargs;
    NeonArguments fnargs;
    NEON_APIDEBUG(state, "thisval.type=%s, argcount=%d", nn_value_typename(thisval), argcount);
    spos = state->vmstate.stackidx + (-argcount);
    vargs = &state->vmstate.stackvalues[spos];
    fnargs.count = argcount;
    fnargs.args = vargs;
    fnargs.thisval = thisval;
    fnargs.userptr = native->userptr;
    fnargs.name = native->name;
    r = native->natfunc(state, &fnargs);
    {
        state->vmstate.stackvalues[spos - 1] = r;
        state->vmstate.stackidx -= argcount;
    }
    nn_gcmem_clearprotect(state);
    return true;
}

bool nn_vm_callvaluewithobject(NeonState* state, NeonValue callable, NeonValue thisval, int argcount)
{
    size_t spos;
    NEON_APIDEBUG(state, "thisval.type=%s, argcount=%d", nn_value_typename(thisval), argcount);
    if(nn_value_isobject(callable))
    {
        switch(nn_value_objtype(callable))
        {
            case NEON_OBJTYPE_FUNCBOUND:
                {
                    NeonObjFuncBound* bound;
                    bound = nn_value_asfuncbound(callable);
                    spos = (state->vmstate.stackidx + (-argcount - 1));
                    state->vmstate.stackvalues[spos] = thisval;
                    return nn_vm_callclosure(state, bound->method, thisval, argcount);
                }
                break;
            case NEON_OBJTYPE_CLASS:
                {
                    NeonObjClass* klass;
                    klass = nn_value_asclass(callable);
                    spos = (state->vmstate.stackidx + (-argcount - 1));
                    state->vmstate.stackvalues[spos] = thisval;
                    if(!nn_value_isempty(klass->constructor))
                    {
                        return nn_vm_callvaluewithobject(state, klass->constructor, thisval, argcount);
                    }
                    else if(klass->superclass != NULL && !nn_value_isempty(klass->superclass->constructor))
                    {
                        return nn_vm_callvaluewithobject(state, klass->superclass->constructor, thisval, argcount);
                    }
                    else if(argcount != 0)
                    {
                        return nn_exceptions_throw(state, "%s constructor expects 0 arguments, %d given", klass->name->sbuf->data, argcount);
                    }
                    return true;
                }
                break;
            case NEON_OBJTYPE_MODULE:
                {
                    NeonObjModule* module;
                    NeonProperty* field;
                    module = nn_value_asmodule(callable);
                    field = nn_table_getfieldbyostr(module->deftable, module->name);
                    if(field != NULL)
                    {
                        return nn_vm_callvalue(state, field->value, thisval, argcount);
                    }
                    return nn_exceptions_throw(state, "module %s does not export a default function", module->name);
                }
                break;
            case NEON_OBJTYPE_FUNCCLOSURE:
                {
                    return nn_vm_callclosure(state, nn_value_asfuncclosure(callable), thisval, argcount);
                }
                break;
            case NEON_OBJTYPE_FUNCNATIVE:
                {
                    return nn_vm_callnative(state, nn_value_asfuncnative(callable), thisval, argcount);
                }
                break;
            default:
                break;
        }
    }
    return nn_exceptions_throw(state, "object of type %s is not callable", nn_value_typename(callable));
}

bool nn_vm_callvalue(NeonState* state, NeonValue callable, NeonValue thisval, int argcount)
{
    NeonValue actualthisval;
    if(nn_value_isobject(callable))
    {
        switch(nn_value_objtype(callable))
        {
            case NEON_OBJTYPE_FUNCBOUND:
                {
                    NeonObjFuncBound* bound;
                    bound = nn_value_asfuncbound(callable);
                    actualthisval = bound->receiver;
                    if(!nn_value_isempty(thisval))
                    {
                        actualthisval = thisval;
                    }
                    NEON_APIDEBUG(state, "actualthisval.type=%s, argcount=%d", nn_value_typename(actualthisval), argcount);
                    return nn_vm_callvaluewithobject(state, callable, actualthisval, argcount);
                }
                break;
            case NEON_OBJTYPE_CLASS:
                {
                    NeonObjClass* klass;
                    NeonObjInstance* instance;
                    klass = nn_value_asclass(callable);
                    instance = nn_object_makeinstance(state, klass);
                    actualthisval = nn_value_fromobject(instance);
                    if(!nn_value_isempty(thisval))
                    {
                        actualthisval = thisval;
                    }
                    NEON_APIDEBUG(state, "actualthisval.type=%s, argcount=%d", nn_value_typename(actualthisval), argcount);
                    return nn_vm_callvaluewithobject(state, callable, actualthisval, argcount);
                }
                break;
            default:
                {
                }
                break;
        }
    }
    NEON_APIDEBUG(state, "thisval.type=%s, argcount=%d", nn_value_typename(thisval), argcount);
    return nn_vm_callvaluewithobject(state, callable, thisval, argcount);
}

NeonFuncType nn_value_getmethodtype(NeonValue method)
{
    switch(nn_value_objtype(method))
    {
        case NEON_OBJTYPE_FUNCNATIVE:
            return nn_value_asfuncnative(method)->type;
        case NEON_OBJTYPE_FUNCCLOSURE:
            return nn_value_asfuncclosure(method)->scriptfunc->type;
        default:
            break;
    }
    return NEON_FUNCTYPE_FUNCTION;
}


NeonObjClass* nn_value_getclassfor(NeonState* state, NeonValue receiver)
{
    if(nn_value_isnumber(receiver))
    {
        return state->classprimnumber;
    }
    if(nn_value_isobject(receiver))
    {
        switch(nn_value_asobject(receiver)->type)
        {
            case NEON_OBJTYPE_STRING:
                return state->classprimstring;
            case NEON_OBJTYPE_RANGE:
                return state->classprimrange;
            case NEON_OBJTYPE_ARRAY:
                return state->classprimarray;
            case NEON_OBJTYPE_DICT:
                return state->classprimdict;
            case NEON_OBJTYPE_FILE:
                return state->classprimfile;
            /*
            case NEON_OBJTYPE_FUNCBOUND:
            case NEON_OBJTYPE_FUNCCLOSURE:
            case NEON_OBJTYPE_FUNCSCRIPT:
                return state->classprimcallable;
            */
            default:
                {
                    fprintf(stderr, "getclassfor: unhandled type!\n");
                }
                break;
        }
    }
    return NULL;
}

static NEON_FORCEINLINE void nn_vmbits_stackpush(NeonState* state, NeonValue value)
{
    nn_vm_checkmayberesize(state);
    state->vmstate.stackvalues[state->vmstate.stackidx] = value;
    state->vmstate.stackidx++;
}

void nn_vm_stackpush(NeonState* state, NeonValue value)
{
    nn_vmbits_stackpush(state, value);
}

static NEON_FORCEINLINE NeonValue nn_vmbits_stackpop(NeonState* state)
{
    NeonValue v;
    state->vmstate.stackidx--;
    v = state->vmstate.stackvalues[state->vmstate.stackidx];
    return v;
}

NeonValue nn_vm_stackpop(NeonState* state)
{
    return nn_vmbits_stackpop(state);
}

static NEON_FORCEINLINE NeonValue nn_vmbits_stackpopn(NeonState* state, int n)
{
    NeonValue v;
    state->vmstate.stackidx -= n;
    v = state->vmstate.stackvalues[state->vmstate.stackidx];
    return v;
}

NeonValue nn_vm_stackpopn(NeonState* state, int n)
{
    return nn_vmbits_stackpopn(state, n);
}

static NEON_FORCEINLINE NeonValue nn_vmbits_stackpeek(NeonState* state, int distance)
{
    NeonValue v;
    v = state->vmstate.stackvalues[state->vmstate.stackidx + (-1 - distance)];
    return v;
}

NeonValue nn_vm_stackpeek(NeonState* state, int distance)
{
    return nn_vmbits_stackpeek(state, distance);
}

#define nn_vmmac_exitvm(state) \
    { \
        (void)you_are_calling_exit_vm_outside_of_runvm; \
        return NEON_STATUS_FAILRUNTIME; \
    }        

#define nn_vmmac_tryraise(state, rtval, ...) \
    if(!nn_exceptions_throw(state, ##__VA_ARGS__)) \
    { \
        return rtval; \
    }

static NEON_FORCEINLINE uint8_t nn_vmbits_readbyte(NeonState* state)
{
    uint8_t r;
    r = state->vmstate.currentframe->inscode->code;
    state->vmstate.currentframe->inscode++;
    return r;
}

static NEON_FORCEINLINE NeonInstruction nn_vmbits_readinstruction(NeonState* state)
{
    NeonInstruction r;
    r = *state->vmstate.currentframe->inscode;
    state->vmstate.currentframe->inscode++;
    return r;
}

static NEON_FORCEINLINE uint16_t nn_vmbits_readshort(NeonState* state)
{
    uint8_t b;
    uint8_t a;
    a = state->vmstate.currentframe->inscode[0].code;
    b = state->vmstate.currentframe->inscode[1].code;
    state->vmstate.currentframe->inscode += 2;
    return (uint16_t)((a << 8) | b);
}

static NEON_FORCEINLINE NeonValue nn_vmbits_readconst(NeonState* state)
{
    uint16_t idx;
    idx = nn_vmbits_readshort(state);
    return state->vmstate.currentframe->closure->scriptfunc->blob.constants->values[idx];
}

static NEON_FORCEINLINE NeonObjString* nn_vmbits_readstring(NeonState* state)
{
    return nn_value_asstring(nn_vmbits_readconst(state));
}

static NEON_FORCEINLINE bool nn_vmutil_invokemethodfromclass(NeonState* state, NeonObjClass* klass, NeonObjString* name, int argcount)
{
    NeonProperty* field;
    NEON_APIDEBUG(state, "argcount=%d", argcount);
    field = nn_table_getfieldbyostr(klass->methods, name);
    if(field != NULL)
    {
        if(nn_value_getmethodtype(field->value) == NEON_FUNCTYPE_PRIVATE)
        {
            return nn_exceptions_throw(state, "cannot call private method '%s' from instance of %s", name->sbuf->data, klass->name->sbuf->data);
        }
        return nn_vm_callvaluewithobject(state, field->value, nn_value_fromobject(klass), argcount);
    }
    return nn_exceptions_throw(state, "undefined method '%s' in %s", name->sbuf->data, klass->name->sbuf->data);
}

static NEON_FORCEINLINE bool nn_vmutil_invokemethodself(NeonState* state, NeonObjString* name, int argcount)
{
    size_t spos;
    NeonValue receiver;
    NeonObjInstance* instance;
    NeonProperty* field;
    NEON_APIDEBUG(state, "argcount=%d", argcount);
    receiver = nn_vmbits_stackpeek(state, argcount);
    if(nn_value_isinstance(receiver))
    {
        instance = nn_value_asinstance(receiver);
        field = nn_table_getfieldbyostr(instance->klass->methods, name);
        if(field != NULL)
        {
            return nn_vm_callvaluewithobject(state, field->value, receiver, argcount);
        }
        field = nn_table_getfieldbyostr(instance->properties, name);
        if(field != NULL)
        {
            spos = (state->vmstate.stackidx + (-argcount - 1));
            #if 0
                state->vmstate.stackvalues[spos] = field->value;
                return nn_vm_callvaluewithobject(state, field->value, receiver, argcount);
            #else
                state->vmstate.stackvalues[spos] = receiver;
                return nn_vm_callvaluewithobject(state, field->value, receiver, argcount);
            #endif
        }
    }
    else if(nn_value_isclass(receiver))
    {
        field = nn_table_getfieldbyostr(nn_value_asclass(receiver)->methods, name);
        if(field != NULL)
        {
            if(nn_value_getmethodtype(field->value) == NEON_FUNCTYPE_STATIC)
            {
                return nn_vm_callvaluewithobject(state, field->value, receiver, argcount);
            }
            return nn_exceptions_throw(state, "cannot call non-static method %s() on non instance", name->sbuf->data);
        }
    }
    return nn_exceptions_throw(state, "cannot call method '%s' on object of type '%s'", name->sbuf->data, nn_value_typename(receiver));
}

static NEON_FORCEINLINE bool nn_vmutil_invokemethod(NeonState* state, NeonObjString* name, int argcount)
{
    size_t spos;
    NeonObjType rectype;
    NeonValue receiver;
    NeonProperty* field;
    NeonObjClass* klass;
    receiver = nn_vmbits_stackpeek(state, argcount);
    NEON_APIDEBUG(state, "receiver.type=%s, argcount=%d", nn_value_typename(receiver), argcount);
    if(nn_value_isobject(receiver))
    {
        rectype = nn_value_asobject(receiver)->type;
        switch(rectype)
        {
            case NEON_OBJTYPE_MODULE:
                {
                    NeonObjModule* module;
                    NEON_APIDEBUG(state, "receiver is a module");
                    module = nn_value_asmodule(receiver);
                    field = nn_table_getfieldbyostr(module->deftable, name);
                    if(field != NULL)
                    {
                        if(nn_util_methodisprivate(name))
                        {
                            return nn_exceptions_throw(state, "cannot call private module method '%s'", name->sbuf->data);
                        }
                        return nn_vm_callvaluewithobject(state, field->value, receiver, argcount);
                    }
                    return nn_exceptions_throw(state, "module %s does not define class or method %s()", module->name, name->sbuf->data);
                }
                break;
            case NEON_OBJTYPE_CLASS:
                {
                    NEON_APIDEBUG(state, "receiver is a class");
                    klass = nn_value_asclass(receiver);
                    field = nn_table_getfieldbyostr(klass->methods, name);
                    if(field != NULL)
                    {
                        if(nn_value_getmethodtype(field->value) == NEON_FUNCTYPE_PRIVATE)
                        {
                            return nn_exceptions_throw(state, "cannot call private method %s() on %s", name->sbuf->data, klass->name->sbuf->data);
                        }
                        return nn_vm_callvaluewithobject(state, field->value, receiver, argcount);
                    }
                    else
                    {
                        field = nn_class_getstaticproperty(klass, name);
                        if(field != NULL)
                        {
                            return nn_vm_callvaluewithobject(state, field->value, receiver, argcount);
                        }
                        field = nn_class_getstaticmethodfield(klass, name);
                        if(field != NULL)
                        {
                            return nn_vm_callvaluewithobject(state, field->value, receiver, argcount);
                        }
                    }
                    return nn_exceptions_throw(state, "unknown method %s() in class %s", name->sbuf->data, klass->name->sbuf->data);
                }
            case NEON_OBJTYPE_INSTANCE:
                {
                    NeonObjInstance* instance;
                    NEON_APIDEBUG(state, "receiver is an instance");
                    instance = nn_value_asinstance(receiver);
                    field = nn_table_getfieldbyostr(instance->properties, name);
                    if(field != NULL)
                    {
                        spos = (state->vmstate.stackidx + (-argcount - 1));
                        #if 0
                            state->vmstate.stackvalues[spos] = field->value;
                        #else
                            state->vmstate.stackvalues[spos] = receiver;
                        #endif
                        return nn_vm_callvaluewithobject(state, field->value, receiver, argcount);
                    }
                    return nn_vmutil_invokemethodfromclass(state, instance->klass, name, argcount);
                }
                break;
            case NEON_OBJTYPE_DICT:
                {
                    NEON_APIDEBUG(state, "receiver is a dictionary");
                    field = nn_class_getmethodfield(state->classprimdict, name);
                    if(field != NULL)
                    {
                        return nn_vm_callnative(state, nn_value_asfuncnative(field->value), receiver, argcount);
                    }
                    /* NEW in v0.0.84, dictionaries can declare extra methods as part of their entries. */
                    else
                    {
                        field = nn_table_getfieldbyostr(nn_value_asdict(receiver)->htab, name);
                        if(field != NULL)
                        {
                            if(nn_value_iscallable(field->value))
                            {
                                return nn_vm_callvaluewithobject(state, field->value, receiver, argcount);
                            }
                        }
                    }
                    return nn_exceptions_throw(state, "'dict' has no method %s()", name->sbuf->data);
                }
                default:
                    {
                    }
                    break;
        }
    }
    klass = nn_value_getclassfor(state, receiver);
    if(klass == NULL)
    {
        /* @TODO: have methods for non objects as well. */
        return nn_exceptions_throw(state, "non-object %s has no method named '%s'", nn_value_typename(receiver), name->sbuf->data);
    }
    field = nn_class_getmethodfield(klass, name);
    if(field != NULL)
    {
        return nn_vm_callvaluewithobject(state, field->value, receiver, argcount);
    }
    return nn_exceptions_throw(state, "'%s' has no method %s()", klass->name->sbuf->data, name->sbuf->data);
}

static NEON_FORCEINLINE bool nn_vmutil_bindmethod(NeonState* state, NeonObjClass* klass, NeonObjString* name)
{
    NeonValue val;
    NeonProperty* field;
    NeonObjFuncBound* bound;
    field = nn_table_getfieldbyostr(klass->methods, name);
    if(field != NULL)
    {
        if(nn_value_getmethodtype(field->value) == NEON_FUNCTYPE_PRIVATE)
        {
            return nn_exceptions_throw(state, "cannot get private property '%s' from instance", name->sbuf->data);
        }
        val = nn_vmbits_stackpeek(state, 0);
        bound = nn_object_makefuncbound(state, val, nn_value_asfuncclosure(field->value));
        nn_vmbits_stackpop(state);
        nn_vmbits_stackpush(state, nn_value_fromobject(bound));
        return true;
    }
    return nn_exceptions_throw(state, "undefined property '%s'", name->sbuf->data);
}

static NEON_FORCEINLINE NeonObjUpvalue* nn_vmutil_upvaluescapture(NeonState* state, NeonValue* local, int stackpos)
{
    NeonObjUpvalue* upvalue;
    NeonObjUpvalue* prevupvalue;
    NeonObjUpvalue* createdupvalue;
    prevupvalue = NULL;
    upvalue = state->vmstate.openupvalues;
    while(upvalue != NULL && (&upvalue->location) > local)
    {
        prevupvalue = upvalue;
        upvalue = upvalue->next;
    }
    if(upvalue != NULL && (&upvalue->location) == local)
    {
        return upvalue;
    }
    createdupvalue = nn_object_makeupvalue(state, local, stackpos);
    createdupvalue->next = upvalue;
    if(prevupvalue == NULL)
    {
        state->vmstate.openupvalues = createdupvalue;
    }
    else
    {
        prevupvalue->next = createdupvalue;
    }
    return createdupvalue;
}

static NEON_FORCEINLINE void nn_vmutil_upvaluesclose(NeonState* state, const NeonValue* last)
{
    NeonObjUpvalue* upvalue;
    while(state->vmstate.openupvalues != NULL && (&state->vmstate.openupvalues->location) >= last)
    {
        upvalue = state->vmstate.openupvalues;
        upvalue->closed = upvalue->location;
        upvalue->location = upvalue->closed;
        state->vmstate.openupvalues = upvalue->next;
    }
}

static NEON_FORCEINLINE void nn_vmutil_definemethod(NeonState* state, NeonObjString* name)
{
    NeonValue method;
    NeonObjClass* klass;
    method = nn_vmbits_stackpeek(state, 0);
    klass = nn_value_asclass(nn_vmbits_stackpeek(state, 1));
    nn_table_set(klass->methods, nn_value_fromobject(name), method);
    if(nn_value_getmethodtype(method) == NEON_FUNCTYPE_INITIALIZER)
    {
        klass->constructor = method;
    }
    nn_vmbits_stackpop(state);
}

static NEON_FORCEINLINE void nn_vmutil_defineproperty(NeonState* state, NeonObjString* name, bool isstatic)
{
    NeonValue property;
    NeonObjClass* klass;
    property = nn_vmbits_stackpeek(state, 0);
    klass = nn_value_asclass(nn_vmbits_stackpeek(state, 1));
    if(!isstatic)
    {
        nn_class_defproperty(klass, name->sbuf->data, property);
    }
    else
    {
        nn_class_setstaticproperty(klass, name, property);
    }
    nn_vmbits_stackpop(state);
}

bool nn_value_isfalse(NeonValue value)
{
    if(nn_value_isbool(value))
    {
        return nn_value_isbool(value) && !nn_value_asbool(value);
    }
    if(nn_value_isnull(value) || nn_value_isempty(value))
    {
        return true;
    }
    /* -1 is the number equivalent of false */
    if(nn_value_isnumber(value))
    {
        return nn_value_asnumber(value) < 0;
    }
    /* Non-empty strings are true, empty strings are false.*/
    if(nn_value_isstring(value))
    {
        return nn_value_asstring(value)->sbuf->length < 1;
    }
    /* Non-empty lists are true, empty lists are false.*/
    if(nn_value_isarray(value))
    {
        return nn_value_asarray(value)->varray->count == 0;
    }
    /* Non-empty dicts are true, empty dicts are false. */
    if(nn_value_isdict(value))
    {
        return nn_value_asdict(value)->names->count == 0;
    }
    /*
    // All classes are true
    // All closures are true
    // All bound methods are true
    // All functions are in themselves true if you do not account for what they
    // return.
    */
    return false;
}

bool nn_util_isinstanceof(NeonObjClass* klass1, NeonObjClass* expected)
{
    size_t klen;
    size_t elen;
    const char* kname;
    const char* ename;
    while(klass1 != NULL)
    {
        elen = expected->name->sbuf->length;
        klen = klass1->name->sbuf->length;
        ename = expected->name->sbuf->data;
        kname = klass1->name->sbuf->data;
        if(elen == klen && memcmp(kname, ename, klen) == 0)
        {
            return true;
        }
        klass1 = klass1->superclass;
    }
    return false;
}

bool nn_dict_setentry(NeonObjDict* dict, NeonValue key, NeonValue value)
{
    NeonValue tempvalue;
    if(!nn_table_get(dict->htab, key, &tempvalue))
    {
        /* add key if it doesn't exist. */
        nn_valarray_push(dict->names, key);
    }
    return nn_table_set(dict->htab, key, value);
}

void nn_dict_addentry(NeonObjDict* dict, NeonValue key, NeonValue value)
{
    nn_dict_setentry(dict, key, value);
}

void nn_dict_addentrycstr(NeonObjDict* dict, const char* ckey, NeonValue value)
{
    NeonObjString* os;
    NeonState* state;
    state = ((NeonObject*)dict)->pvm;
    os = nn_string_copycstr(state, ckey);
    nn_dict_addentry(dict, nn_value_fromobject(os), value);
}

NeonProperty* nn_dict_getentry(NeonObjDict* dict, NeonValue key)
{
    return nn_table_getfield(dict->htab, key);
}

static NEON_FORCEINLINE NeonObjString* nn_vmutil_multiplystring(NeonState* state, NeonObjString* str, double number)
{
    int i;
    int times;
    NeonPrinter pr;
    times = (int)number;
    /* 'str' * 0 == '', 'str' * -1 == '' */
    if(times <= 0)
    {
        return nn_string_copylen(state, "", 0);
    }
    /* 'str' * 1 == 'str' */
    else if(times == 1)
    {
        return str;
    }
    nn_printer_makestackstring(state, &pr);
    for(i = 0; i < times; i++)
    {
        nn_printer_writestringl(&pr, str->sbuf->data, str->sbuf->length);
    }
    return nn_printer_takestring(&pr);
}

static NEON_FORCEINLINE NeonObjArray* nn_vmutil_combinearrays(NeonState* state, NeonObjArray* a, NeonObjArray* b)
{
    int i;
    NeonObjArray* list;
    list = nn_object_makearray(state);
    nn_vmbits_stackpush(state, nn_value_fromobject(list));
    for(i = 0; i < a->varray->count; i++)
    {
        nn_valarray_push(list->varray, a->varray->values[i]);
    }
    for(i = 0; i < b->varray->count; i++)
    {
        nn_valarray_push(list->varray, b->varray->values[i]);
    }
    nn_vmbits_stackpop(state);
    return list;
}

static NEON_FORCEINLINE void nn_vmutil_multiplyarray(NeonState* state, NeonObjArray* from, NeonObjArray* newlist, int times)
{
    int i;
    int j;
    (void)state;
    for(i = 0; i < times; i++)
    {
        for(j = 0; j < from->varray->count; j++)
        {
            nn_valarray_push(newlist->varray, from->varray->values[j]);
        }
    }
}

static NEON_FORCEINLINE bool nn_vmutil_dogetrangedindexofarray(NeonState* state, NeonObjArray* list, bool willassign)
{
    int i;
    int idxlower;
    int idxupper;
    NeonValue valupper;
    NeonValue vallower;
    NeonObjArray* newlist;
    valupper = nn_vmbits_stackpeek(state, 0);
    vallower = nn_vmbits_stackpeek(state, 1);
    if(!(nn_value_isnull(vallower) || nn_value_isnumber(vallower)) || !(nn_value_isnumber(valupper) || nn_value_isnull(valupper)))
    {
        nn_vmbits_stackpopn(state, 2);
        return nn_exceptions_throw(state, "list range index expects upper and lower to be numbers, but got '%s', '%s'", nn_value_typename(vallower), nn_value_typename(valupper));
    }
    idxlower = 0;
    if(nn_value_isnumber(vallower))
    {
        idxlower = nn_value_asnumber(vallower);
    }
    if(nn_value_isnull(valupper))
    {
        idxupper = list->varray->count;
    }
    else
    {
        idxupper = nn_value_asnumber(valupper);
    }
    if(idxlower < 0 || (idxupper < 0 && ((list->varray->count + idxupper) < 0)) || idxlower >= list->varray->count)
    {
        /* always return an empty list... */
        if(!willassign)
        {
            /* +1 for the list itself */
            nn_vmbits_stackpopn(state, 3);
        }
        nn_vmbits_stackpush(state, nn_value_fromobject(nn_object_makearray(state)));
        return true;
    }
    if(idxupper < 0)
    {
        idxupper = list->varray->count + idxupper;
    }
    if(idxupper > list->varray->count)
    {
        idxupper = list->varray->count;
    }
    newlist = nn_object_makearray(state);
    nn_vmbits_stackpush(state, nn_value_fromobject(newlist));
    for(i = idxlower; i < idxupper; i++)
    {
        nn_valarray_push(newlist->varray, list->varray->values[i]);
    }
    /* clear gc protect */
    nn_vmbits_stackpop(state);
    if(!willassign)
    {
        /* +1 for the list itself */
        nn_vmbits_stackpopn(state, 3);
    }
    nn_vmbits_stackpush(state, nn_value_fromobject(newlist));
    return true;
}

static NEON_FORCEINLINE bool nn_vmutil_dogetrangedindexofstring(NeonState* state, NeonObjString* string, bool willassign)
{
    int end;
    int start;
    int length;
    int idxupper;
    int idxlower;
    NeonValue valupper;
    NeonValue vallower;
    valupper = nn_vmbits_stackpeek(state, 0);
    vallower = nn_vmbits_stackpeek(state, 1);
    if(!(nn_value_isnull(vallower) || nn_value_isnumber(vallower)) || !(nn_value_isnumber(valupper) || nn_value_isnull(valupper)))
    {
        nn_vmbits_stackpopn(state, 2);
        return nn_exceptions_throw(state, "string range index expects upper and lower to be numbers, but got '%s', '%s'", nn_value_typename(vallower), nn_value_typename(valupper));
    }
    length = string->sbuf->length;
    idxlower = 0;
    if(nn_value_isnumber(vallower))
    {
        idxlower = nn_value_asnumber(vallower);
    }
    if(nn_value_isnull(valupper))
    {
        idxupper = length;
    }
    else
    {
        idxupper = nn_value_asnumber(valupper);
    }
    if(idxlower < 0 || (idxupper < 0 && ((length + idxupper) < 0)) || idxlower >= length)
    {
        /* always return an empty string... */
        if(!willassign)
        {
            /* +1 for the string itself */
            nn_vmbits_stackpopn(state, 3);
        }
        nn_vmbits_stackpush(state, nn_value_fromobject(nn_string_copylen(state, "", 0)));
        return true;
    }
    if(idxupper < 0)
    {
        idxupper = length + idxupper;
    }
    if(idxupper > length)
    {
        idxupper = length;
    }
    start = idxlower;
    end = idxupper;
    if(!willassign)
    {
        /* +1 for the string itself */
        nn_vmbits_stackpopn(state, 3);
    }
    nn_vmbits_stackpush(state, nn_value_fromobject(nn_string_copylen(state, string->sbuf->data + start, end - start)));
    return true;
}

static NEON_FORCEINLINE bool nn_vmdo_getrangedindex(NeonState* state)
{
    bool isgotten;
    uint8_t willassign;
    NeonValue vfrom;
    willassign = nn_vmbits_readbyte(state);
    isgotten = true;
    vfrom = nn_vmbits_stackpeek(state, 2);
    if(nn_value_isobject(vfrom))
    {
        switch(nn_value_asobject(vfrom)->type)
        {
            case NEON_OBJTYPE_STRING:
            {
                if(!nn_vmutil_dogetrangedindexofstring(state, nn_value_asstring(vfrom), willassign))
                {
                    return false;
                }
                break;
            }
            case NEON_OBJTYPE_ARRAY:
            {
                if(!nn_vmutil_dogetrangedindexofarray(state, nn_value_asarray(vfrom), willassign))
                {
                    return false;
                }
                break;
            }
            default:
            {
                isgotten = false;
                break;
            }
        }
    }
    else
    {
        isgotten = false;
    }
    if(!isgotten)
    {
        return nn_exceptions_throw(state, "cannot range index object of type %s", nn_value_typename(vfrom));
    }
    return true;
}

static NEON_FORCEINLINE bool nn_vmutil_doindexgetdict(NeonState* state, NeonObjDict* dict, bool willassign)
{
    NeonValue vindex;
    NeonProperty* field;
    vindex = nn_vmbits_stackpeek(state, 0);
    field = nn_dict_getentry(dict, vindex);
    if(field != NULL)
    {
        if(!willassign)
        {
            /* we can safely get rid of the index from the stack */
            nn_vmbits_stackpopn(state, 2);
        }
        nn_vmbits_stackpush(state, field->value);
        return true;
    }
    nn_vmbits_stackpopn(state, 1);
    nn_vmbits_stackpush(state, nn_value_makeempty());
    return true;
}

static NEON_FORCEINLINE bool nn_vmutil_doindexgetmodule(NeonState* state, NeonObjModule* module, bool willassign)
{
    NeonValue vindex;
    NeonValue result;
    vindex = nn_vmbits_stackpeek(state, 0);
    if(nn_table_get(module->deftable, vindex, &result))
    {
        if(!willassign)
        {
            /* we can safely get rid of the index from the stack */
            nn_vmbits_stackpopn(state, 2);
        }
        nn_vmbits_stackpush(state, result);
        return true;
    }
    nn_vmbits_stackpop(state);
    return nn_exceptions_throw(state, "%s is undefined in module %s", nn_value_tostring(state, vindex)->sbuf->data, module->name);
}

static NEON_FORCEINLINE bool nn_vmutil_doindexgetstring(NeonState* state, NeonObjString* string, bool willassign)
{
    int end;
    int start;
    int index;
    int maxlength;
    int realindex;
    NeonValue vindex;
    NeonObjRange* rng;
    (void)realindex;
    vindex = nn_vmbits_stackpeek(state, 0);
    if(!nn_value_isnumber(vindex))
    {
        if(nn_value_isrange(vindex))
        {
            rng = nn_value_asrange(vindex);
            nn_vmbits_stackpop(state);
            nn_vmbits_stackpush(state, nn_value_makenumber(rng->lower));
            nn_vmbits_stackpush(state, nn_value_makenumber(rng->upper));
            return nn_vmutil_dogetrangedindexofstring(state, string, willassign);
        }
        nn_vmbits_stackpopn(state, 1);
        return nn_exceptions_throw(state, "strings are numerically indexed");
    }
    index = nn_value_asnumber(vindex);
    maxlength = string->sbuf->length;
    realindex = index;
    if(index < 0)
    {
        index = maxlength + index;
    }
    if(index < maxlength && index >= 0)
    {
        start = index;
        end = index + 1;
        if(!willassign)
        {
            /*
            // we can safely get rid of the index from the stack
            // +1 for the string itself
            */
            nn_vmbits_stackpopn(state, 2);
        }
        nn_vmbits_stackpush(state, nn_value_fromobject(nn_string_copylen(state, string->sbuf->data + start, end - start)));
        return true;
    }
    nn_vmbits_stackpopn(state, 1);
    #if 0
        return nn_exceptions_throw(state, "string index %d out of range of %d", realindex, maxlength);
    #else
        nn_vmbits_stackpush(state, nn_value_makeempty());
        return true;
    #endif
}

static NEON_FORCEINLINE bool nn_vmutil_doindexgetarray(NeonState* state, NeonObjArray* list, bool willassign)
{
    int index;
    NeonValue finalval;
    NeonValue vindex;
    NeonObjRange* rng;
    vindex = nn_vmbits_stackpeek(state, 0);
    if(NEON_UNLIKELY(!nn_value_isnumber(vindex)))
    {
        if(nn_value_isrange(vindex))
        {
            rng = nn_value_asrange(vindex);
            nn_vmbits_stackpop(state);
            nn_vmbits_stackpush(state, nn_value_makenumber(rng->lower));
            nn_vmbits_stackpush(state, nn_value_makenumber(rng->upper));
            return nn_vmutil_dogetrangedindexofarray(state, list, willassign);
        }
        nn_vmbits_stackpop(state);
        return nn_exceptions_throw(state, "list are numerically indexed");
    }
    index = nn_value_asnumber(vindex);
    if(NEON_UNLIKELY(index < 0))
    {
        index = list->varray->count + index;
    }
    if(index < list->varray->count && index >= 0)
    {
        finalval = list->varray->values[index];
    }
    else
    {
        finalval = nn_value_makenull();
    }
    if(!willassign)
    {
        /*
        // we can safely get rid of the index from the stack
        // +1 for the list itself
        */
        nn_vmbits_stackpopn(state, 2);
    }
    nn_vmbits_stackpush(state, finalval);
    return true;
}

static NEON_FORCEINLINE bool nn_vmdo_indexget(NeonState* state)
{
    bool isgotten;
    uint8_t willassign;
    NeonValue peeked;
    willassign = nn_vmbits_readbyte(state);
    isgotten = true;
    peeked = nn_vmbits_stackpeek(state, 1);
    if(NEON_LIKELY(nn_value_isobject(peeked)))
    {
        switch(nn_value_asobject(peeked)->type)
        {
            case NEON_OBJTYPE_STRING:
            {
                if(!nn_vmutil_doindexgetstring(state, nn_value_asstring(peeked), willassign))
                {
                    return false;
                }
                break;
            }
            case NEON_OBJTYPE_ARRAY:
            {
                if(!nn_vmutil_doindexgetarray(state, nn_value_asarray(peeked), willassign))
                {
                    return false;
                }
                break;
            }
            case NEON_OBJTYPE_DICT:
            {
                if(!nn_vmutil_doindexgetdict(state, nn_value_asdict(peeked), willassign))
                {
                    return false;
                }
                break;
            }
            case NEON_OBJTYPE_MODULE:
            {
                if(!nn_vmutil_doindexgetmodule(state, nn_value_asmodule(peeked), willassign))
                {
                    return false;
                }
                break;
            }
            default:
            {
                isgotten = false;
                break;
            }
        }
    }
    else
    {
        isgotten = false;
    }
    if(!isgotten)
    {
        nn_exceptions_throw(state, "cannot index object of type %s", nn_value_typename(peeked));
    }
    return true;
}


static NEON_FORCEINLINE bool nn_vmutil_dosetindexdict(NeonState* state, NeonObjDict* dict, NeonValue index, NeonValue value)
{
    nn_dict_setentry(dict, index, value);
    /* pop the value, index and dict out */
    nn_vmbits_stackpopn(state, 3);
    /*
    // leave the value on the stack for consumption
    // e.g. variable = dict[index] = 10
    */
    nn_vmbits_stackpush(state, value);
    return true;
}

static NEON_FORCEINLINE bool nn_vmutil_dosetindexmodule(NeonState* state, NeonObjModule* module, NeonValue index, NeonValue value)
{
    nn_table_set(module->deftable, index, value);
    /* pop the value, index and dict out */
    nn_vmbits_stackpopn(state, 3);
    /*
    // leave the value on the stack for consumption
    // e.g. variable = dict[index] = 10
    */
    nn_vmbits_stackpush(state, value);
    return true;
}

static NEON_FORCEINLINE bool nn_vmutil_doindexsetarray(NeonState* state, NeonObjArray* list, NeonValue index, NeonValue value)
{
    int tmp;
    int rawpos;
    int position;
    int ocnt;
    int ocap;
    int vasz;
    if(NEON_UNLIKELY(!nn_value_isnumber(index)))
    {
        nn_vmbits_stackpopn(state, 3);
        /* pop the value, index and list out */
        return nn_exceptions_throw(state, "list are numerically indexed");
    }
    ocap = list->varray->capacity;
    ocnt = list->varray->count;
    rawpos = nn_value_asnumber(index);
    position = rawpos;
    if(rawpos < 0)
    {
        rawpos = list->varray->count + rawpos;
    }
    if(position < ocap && position > -(ocap))
    {
        list->varray->values[position] = value;
        if(position >= ocnt)
        {
            list->varray->count++;
        }
    }
    else
    {
        if(position < 0)
        {
            fprintf(stderr, "inverting negative position %d\n", position);
            position = (~position) + 1;
        }
        tmp = 0;
        vasz = list->varray->count;
        if((position > vasz) || ((position == 0) && (vasz == 0)))
        {
            if(position == 0)
            {
                nn_array_push(list, nn_value_makeempty());
            }
            else
            {
                tmp = position + 1;
                while(tmp > vasz)
                {
                    nn_array_push(list, nn_value_makeempty());
                    tmp--;
                }
            }
        }
        fprintf(stderr, "setting value at position %d (array count: %d)\n", position, list->varray->count);
    }
    list->varray->values[position] = value;
    /* pop the value, index and list out */
    nn_vmbits_stackpopn(state, 3);
    /*
    // leave the value on the stack for consumption
    // e.g. variable = list[index] = 10    
    */
    nn_vmbits_stackpush(state, value);
    return true;
    /*
    // pop the value, index and list out
    //nn_vmbits_stackpopn(state, 3);
    //return nn_exceptions_throw(state, "lists index %d out of range", rawpos);
    //nn_vmbits_stackpush(state, nn_value_makeempty());
    //return true;
    */
}

static NEON_FORCEINLINE bool nn_vmutil_dosetindexstring(NeonState* state, NeonObjString* os, NeonValue index, NeonValue value)
{
    int iv;
    int rawpos;
    int position;
    int oslen;
    if(!nn_value_isnumber(index))
    {
        nn_vmbits_stackpopn(state, 3);
        /* pop the value, index and list out */
        return nn_exceptions_throw(state, "strings are numerically indexed");
    }
    iv = nn_value_asnumber(value);
    rawpos = nn_value_asnumber(index);
    oslen = os->sbuf->length;
    position = rawpos;
    if(rawpos < 0)
    {
        position = (oslen + rawpos);
    }
    if(position < oslen && position > -oslen)
    {
        os->sbuf->data[position] = iv;
        /* pop the value, index and list out */
        nn_vmbits_stackpopn(state, 3);
        /*
        // leave the value on the stack for consumption
        // e.g. variable = list[index] = 10
        */
        nn_vmbits_stackpush(state, value);
        return true;
    }
    else
    {
        dyn_strbuf_appendchar(os->sbuf, iv);
        nn_vmbits_stackpopn(state, 3);
        nn_vmbits_stackpush(state, value);
    }
    return true;
}

static NEON_FORCEINLINE bool nn_vmdo_indexset(NeonState* state)
{
    bool isset;
    NeonValue value;
    NeonValue index;
    NeonValue target;
    isset = true;
    target = nn_vmbits_stackpeek(state, 2);
    if(NEON_LIKELY(nn_value_isobject(target)))
    {
        value = nn_vmbits_stackpeek(state, 0);
        index = nn_vmbits_stackpeek(state, 1);
        if(NEON_UNLIKELY(nn_value_isempty(value)))
        {
            return nn_exceptions_throw(state, "empty cannot be assigned");
        }
        switch(nn_value_asobject(target)->type)
        {
            case NEON_OBJTYPE_ARRAY:
                {
                    if(!nn_vmutil_doindexsetarray(state, nn_value_asarray(target), index, value))
                    {
                        return false;
                    }
                }
                break;
            case NEON_OBJTYPE_STRING:
                {
                    if(!nn_vmutil_dosetindexstring(state, nn_value_asstring(target), index, value))
                    {
                        return false;
                    }
                }
                break;
            case NEON_OBJTYPE_DICT:
                {
                    return nn_vmutil_dosetindexdict(state, nn_value_asdict(target), index, value);
                }
                break;
            case NEON_OBJTYPE_MODULE:
                {
                    return nn_vmutil_dosetindexmodule(state, nn_value_asmodule(target), index, value);
                }
                break;
            default:
                {
                    isset = false;
                }
                break;
        }
    }
    else
    {
        isset = false;
    }
    if(!isset)
    {
        return nn_exceptions_throw(state, "type of %s is not a valid iterable", nn_value_typename(nn_vmbits_stackpeek(state, 3)));
    }
    return true;
}

static NEON_FORCEINLINE bool nn_vmutil_concatenate(NeonState* state)
{
    NeonValue vleft;
    NeonValue vright;
    NeonPrinter pr;
    NeonObjString* result;
    vright = nn_vmbits_stackpeek(state, 0);
    vleft = nn_vmbits_stackpeek(state, 1);
    nn_printer_makestackstring(state, &pr);
    nn_printer_printvalue(&pr, vleft, false, true);
    nn_printer_printvalue(&pr, vright, false, true);
    result = nn_printer_takestring(&pr);
    nn_vmbits_stackpopn(state, 2);
    nn_vmbits_stackpush(state, nn_value_fromobject(result));
    return true;
}

static NEON_FORCEINLINE int nn_vmutil_floordiv(double a, double b)
{
    int d;
    d = (int)a / (int)b;
    return d - ((d * b == a) & ((a < 0) ^ (b < 0)));
}

static NEON_FORCEINLINE double nn_vmutil_modulo(double a, double b)
{
    double r;
    r = fmod(a, b);
    if(r != 0 && ((r < 0) != (b < 0)))
    {
        r += b;
    }
    return r;
}

static NEON_FORCEINLINE NeonProperty* nn_vmutil_getproperty(NeonState* state, NeonValue peeked, NeonObjString* name)
{
    NeonProperty* field;
    switch(nn_value_asobject(peeked)->type)
    {
        case NEON_OBJTYPE_MODULE:
            {
                NeonObjModule* module;
                module = nn_value_asmodule(peeked);
                field = nn_table_getfieldbyostr(module->deftable, name);
                if(field != NULL)
                {
                    if(nn_util_methodisprivate(name))
                    {
                        nn_exceptions_throw(state, "cannot get private module property '%s'", name->sbuf->data);
                        return NULL;
                    }
                    return field;
                }
                nn_exceptions_throw(state, "%s module does not define '%s'", module->name, name->sbuf->data);
                return NULL;
            }
            break;
        case NEON_OBJTYPE_CLASS:
            {
                field = nn_table_getfieldbyostr(nn_value_asclass(peeked)->methods, name);
                if(field != NULL)
                {
                    if(nn_value_getmethodtype(field->value) == NEON_FUNCTYPE_STATIC)
                    {
                        if(nn_util_methodisprivate(name))
                        {
                            nn_exceptions_throw(state, "cannot call private property '%s' of class %s", name->sbuf->data,
                                nn_value_asclass(peeked)->name->sbuf->data);
                            return NULL;
                        }
                        return field;
                    }
                }
                else
                {
                    field = nn_class_getstaticproperty(nn_value_asclass(peeked), name);
                    if(field != NULL)
                    {
                        if(nn_util_methodisprivate(name))
                        {
                            nn_exceptions_throw(state, "cannot call private property '%s' of class %s", name->sbuf->data,
                                nn_value_asclass(peeked)->name->sbuf->data);
                            return NULL;
                        }
                        return field;
                    }
                }
                nn_exceptions_throw(state, "class %s does not have a static property or method named '%s'",
                    nn_value_asclass(peeked)->name->sbuf->data, name->sbuf->data);
                return NULL;
            }
            break;
        case NEON_OBJTYPE_INSTANCE:
            {
                NeonObjInstance* instance;
                instance = nn_value_asinstance(peeked);
                field = nn_table_getfieldbyostr(instance->properties, name);
                if(field != NULL)
                {
                    if(nn_util_methodisprivate(name))
                    {
                        nn_exceptions_throw(state, "cannot call private property '%s' from instance of %s", name->sbuf->data, instance->klass->name->sbuf->data);
                        return NULL;
                    }
                    return field;
                }
                if(nn_util_methodisprivate(name))
                {
                    nn_exceptions_throw(state, "cannot bind private property '%s' to instance of %s", name->sbuf->data, instance->klass->name->sbuf->data);
                    return NULL;
                }
                if(nn_vmutil_bindmethod(state, instance->klass, name))
                {
                    return field;
                }
                nn_exceptions_throw(state, "instance of class %s does not have a property or method named '%s'",
                    nn_value_asinstance(peeked)->klass->name->sbuf->data, name->sbuf->data);
                return NULL;
            }
            break;
        case NEON_OBJTYPE_STRING:
            {
                field = nn_class_getpropertyfield(state->classprimstring, name);
                if(field != NULL)
                {
                    return field;
                }
                nn_exceptions_throw(state, "class String has no named property '%s'", name->sbuf->data);
                return NULL;
            }
            break;
        case NEON_OBJTYPE_ARRAY:
            {
                field = nn_class_getpropertyfield(state->classprimarray, name);
                if(field != NULL)
                {
                    return field;
                }
                nn_exceptions_throw(state, "class Array has no named property '%s'", name->sbuf->data);
                return NULL;
            }
            break;
        case NEON_OBJTYPE_RANGE:
            {
                field = nn_class_getpropertyfield(state->classprimrange, name);
                if(field != NULL)
                {
                    return field;
                }
                nn_exceptions_throw(state, "class Range has no named property '%s'", name->sbuf->data);
                return NULL;
            }
            break;
        case NEON_OBJTYPE_DICT:
            {
                field = nn_table_getfieldbyostr(nn_value_asdict(peeked)->htab, name);
                if(field == NULL)
                {
                    field = nn_class_getpropertyfield(state->classprimdict, name);
                }
                if(field != NULL)
                {
                    return field;
                }
                nn_exceptions_throw(state, "unknown key or class Dict property '%s'", name->sbuf->data);
                return NULL;
            }
            break;
        case NEON_OBJTYPE_FILE:
            {
                field = nn_class_getpropertyfield(state->classprimfile, name);
                if(field != NULL)
                {
                    return field;
                }
                nn_exceptions_throw(state, "class File has no named property '%s'", name->sbuf->data);
                return NULL;
            }
            break;
        default:
            {
                nn_exceptions_throw(state, "object of type %s does not carry properties", nn_value_typename(peeked));
                return NULL;
            }
            break;
    }
    return NULL;
}

static NEON_FORCEINLINE bool nn_vmdo_propertyget(NeonState* state)
{
    NeonValue peeked;
    NeonProperty* field;
    NeonObjString* name;
    name = nn_vmbits_readstring(state);
    peeked = nn_vmbits_stackpeek(state, 0);
    if(nn_value_isobject(peeked))
    {
        field = nn_vmutil_getproperty(state, peeked, name);
        if(field == NULL)
        {
            return false;
        }
        else
        {
            if(field->type == NEON_PROPTYPE_FUNCTION)
            {
                nn_vm_callvaluewithobject(state, field->value, peeked, 0);
            }
            else
            {
                nn_vmbits_stackpop(state);
                nn_vmbits_stackpush(state, field->value);
            }
        }
        return true;
    }
    else
    {
        nn_exceptions_throw(state, "'%s' of type %s does not have properties", nn_value_tostring(state, peeked)->sbuf->data,
            nn_value_typename(peeked));
    }
    return false;
}

static NEON_FORCEINLINE bool nn_vmdo_propertygetself(NeonState* state)
{
    NeonValue peeked;
    NeonObjString* name;
    NeonObjClass* klass;
    NeonObjInstance* instance;
    NeonObjModule* module;
    NeonProperty* field;
    name = nn_vmbits_readstring(state);
    peeked = nn_vmbits_stackpeek(state, 0);
    if(nn_value_isinstance(peeked))
    {
        instance = nn_value_asinstance(peeked);
        field = nn_table_getfieldbyostr(instance->properties, name);
        if(field != NULL)
        {
            /* pop the instance... */
            nn_vmbits_stackpop(state);
            nn_vmbits_stackpush(state, field->value);
            return true;
        }
        if(nn_vmutil_bindmethod(state, instance->klass, name))
        {
            return true;
        }
        nn_vmmac_tryraise(state, false, "instance of class %s does not have a property or method named '%s'",
            nn_value_asinstance(peeked)->klass->name->sbuf->data, name->sbuf->data);
        return false;
    }
    else if(nn_value_isclass(peeked))
    {
        klass = nn_value_asclass(peeked);
        field = nn_table_getfieldbyostr(klass->methods, name);
        if(field != NULL)
        {
            if(nn_value_getmethodtype(field->value) == NEON_FUNCTYPE_STATIC)
            {
                /* pop the class... */
                nn_vmbits_stackpop(state);
                nn_vmbits_stackpush(state, field->value);
                return true;
            }
        }
        else
        {
            field = nn_class_getstaticproperty(klass, name);
            if(field != NULL)
            {
                /* pop the class... */
                nn_vmbits_stackpop(state);
                nn_vmbits_stackpush(state, field->value);
                return true;
            }
        }
        nn_vmmac_tryraise(state, false, "class %s does not have a static property or method named '%s'", klass->name->sbuf->data, name->sbuf->data);
        return false;
    }
    else if(nn_value_ismodule(peeked))
    {
        module = nn_value_asmodule(peeked);
        field = nn_table_getfieldbyostr(module->deftable, name);
        if(field != NULL)
        {
            /* pop the module... */
            nn_vmbits_stackpop(state);
            nn_vmbits_stackpush(state, field->value);
            return true;
        }
        nn_vmmac_tryraise(state, false, "module %s does not define '%s'", module->name, name->sbuf->data);
        return false;
    }
    nn_vmmac_tryraise(state, false, "'%s' of type %s does not have properties", nn_value_tostring(state, peeked)->sbuf->data,
        nn_value_typename(peeked));
    return false;
}

static NEON_FORCEINLINE bool nn_vmdo_propertyset(NeonState* state)
{
    NeonValue value;
    NeonValue vtarget;
    NeonValue vpeek;
    NeonObjClass* klass;
    NeonObjString* name;
    NeonObjDict* dict;
    NeonObjInstance* instance;
    vtarget = nn_vmbits_stackpeek(state, 1);
    if(!nn_value_isclass(vtarget) && !nn_value_isinstance(vtarget) && !nn_value_isdict(vtarget))
    {
        nn_exceptions_throw(state, "object of type %s cannot carry properties", nn_value_typename(vtarget));
        return false;
    }
    else if(nn_value_isempty(nn_vmbits_stackpeek(state, 0)))
    {
        nn_exceptions_throw(state, "empty cannot be assigned");
        return false;
    }
    name = nn_vmbits_readstring(state);
    vpeek = nn_vmbits_stackpeek(state, 0);
    if(nn_value_isclass(vtarget))
    {
        klass = nn_value_asclass(vtarget);
        if(nn_value_iscallable(vpeek))
        {
            //fprintf(stderr, "setting '%s' as method\n", name->sbuf->data);
            nn_class_defmethod(state, klass, name->sbuf->data, vpeek);
        }
        else
        {
            nn_class_defproperty(klass, name->sbuf->data, vpeek);
        }
        value = nn_vmbits_stackpop(state);
        /* removing the class object */
        nn_vmbits_stackpop(state);
        nn_vmbits_stackpush(state, value);
    }
    else if(nn_value_isinstance(vtarget))
    {
        instance = nn_value_asinstance(vtarget);
        nn_instance_defproperty(instance, name->sbuf->data, vpeek);
        value = nn_vmbits_stackpop(state);
        /* removing the instance object */
        nn_vmbits_stackpop(state);
        nn_vmbits_stackpush(state, value);
    }
    else
    {
        dict = nn_value_asdict(vtarget);
        nn_dict_setentry(dict, nn_value_fromobject(name), vpeek);
        value = nn_vmbits_stackpop(state);
        /* removing the dictionary object */
        nn_vmbits_stackpop(state);
        nn_vmbits_stackpush(state, value);
    }
    return true;
}

static NEON_FORCEINLINE double nn_vmutil_valtonum(NeonValue v)
{
    if(nn_value_isnull(v))
    {
        return 0;
    }
    if(nn_value_isbool(v))
    {
        if(nn_value_asbool(v))
        {
            return 1;
        }
        return 0;
    }
    return nn_value_asnumber(v);
}


static NEON_FORCEINLINE uint32_t nn_vmutil_valtouint(NeonValue v)
{
    if(nn_value_isnull(v))
    {
        return 0;
    }
    if(nn_value_isbool(v))
    {
        if(nn_value_asbool(v))
        {
            return 1;
        }
        return 0;
    }
    return nn_value_asnumber(v);
}

static NEON_FORCEINLINE long nn_vmutil_valtoint(NeonValue v)
{
    return (long)nn_vmutil_valtonum(v);
}



static NEON_FORCEINLINE bool nn_vmdo_dobinary(NeonState* state)
{
    bool isfail;
    long ibinright;
    long ibinleft;
    uint32_t ubinright;
    uint32_t ubinleft;
    double dbinright;
    double dbinleft;
    NeonOpCode instruction;
    NeonValue res;
    NeonValue binvalleft;
    NeonValue binvalright;
    instruction = (NeonOpCode)state->vmstate.currentinstr.code;
    binvalright = nn_vmbits_stackpeek(state, 0);
    binvalleft = nn_vmbits_stackpeek(state, 1);
    isfail = (
        (!nn_value_isnumber(binvalright) && !nn_value_isbool(binvalright) && !nn_value_isnull(binvalright)) ||
        (!nn_value_isnumber(binvalleft) && !nn_value_isbool(binvalleft) && !nn_value_isnull(binvalleft))
    );
    if(isfail)
    {
        nn_vmmac_tryraise(state, false, "unsupported operand %s for %s and %s", nn_dbg_op2str(instruction), nn_value_typename(binvalleft), nn_value_typename(binvalright));
        return false;
    }
    binvalright = nn_vmbits_stackpop(state);
    binvalleft = nn_vmbits_stackpop(state);
    res = nn_value_makeempty();
    switch(instruction)
    {
        case NEON_OP_PRIMADD:
            {
                dbinright = nn_vmutil_valtonum(binvalright);
                dbinleft = nn_vmutil_valtonum(binvalleft);
                res = nn_value_makenumber(dbinleft + dbinright);
            }
            break;
        case NEON_OP_PRIMSUBTRACT:
            {
                dbinright = nn_vmutil_valtonum(binvalright);
                dbinleft = nn_vmutil_valtonum(binvalleft);
                res = nn_value_makenumber(dbinleft - dbinright);
            }
            break;
        case NEON_OP_PRIMDIVIDE:
            {
                dbinright = nn_vmutil_valtonum(binvalright);
                dbinleft = nn_vmutil_valtonum(binvalleft);
                res = nn_value_makenumber(dbinleft / dbinright);
            }
            break;
        case NEON_OP_PRIMMULTIPLY:
            {
                dbinright = nn_vmutil_valtonum(binvalright);
                dbinleft = nn_vmutil_valtonum(binvalleft);
                res = nn_value_makenumber(dbinleft * dbinright);
            }
            break;
        case NEON_OP_PRIMAND:
            {
                ibinright = nn_vmutil_valtoint(binvalright);
                ibinleft = nn_vmutil_valtoint(binvalleft);
                res = nn_value_makeint(ibinleft & ibinright);
            }
            break;
        case NEON_OP_PRIMOR:
            {
                ibinright = nn_vmutil_valtoint(binvalright);
                ibinleft = nn_vmutil_valtoint(binvalleft);
                res = nn_value_makeint(ibinleft | ibinright);
            }
            break;
        case NEON_OP_PRIMBITXOR:
            {
                ibinright = nn_vmutil_valtoint(binvalright);
                ibinleft = nn_vmutil_valtoint(binvalleft);
                res = nn_value_makeint(ibinleft ^ ibinright);
            }
            break;
        case NEON_OP_PRIMSHIFTLEFT:
            {
                /*
                via quickjs:
                    uint32_t v1;
                    uint32_t v2;
                    v1 = JS_VALUE_GET_INT(op1);
                    v2 = JS_VALUE_GET_INT(op2);
                    v2 &= 0x1f;
                    sp[-2] = JS_NewInt32(ctx, v1 << v2);
                */
                ubinright = nn_vmutil_valtouint(binvalright);
                ubinleft = nn_vmutil_valtouint(binvalleft);
                ubinright &= 0x1f;
                //res = nn_value_makeint(ibinleft << ibinright);
                res = nn_value_makeint(ubinleft << ubinright);

            }
            break;
        case NEON_OP_PRIMSHIFTRIGHT:
            {
                /*
                    uint32_t v2;
                    v2 = JS_VALUE_GET_INT(op2);
                    v2 &= 0x1f;
                    sp[-2] = JS_NewUint32(ctx, (uint32_t)JS_VALUE_GET_INT(op1) >> v2);
                */
                ubinright = nn_vmutil_valtouint(binvalright);
                ubinleft = nn_vmutil_valtouint(binvalleft);
                ubinright &= 0x1f;
                res = nn_value_makeint(ubinleft >> ubinright);
            }
            break;
        case NEON_OP_PRIMGREATER:
            {
                dbinright = nn_vmutil_valtonum(binvalright);
                dbinleft = nn_vmutil_valtonum(binvalleft);
                res = nn_value_makebool(dbinleft > dbinright);
            }
            break;
        case NEON_OP_PRIMLESSTHAN:
            {
                dbinright = nn_vmutil_valtonum(binvalright);
                dbinleft = nn_vmutil_valtonum(binvalleft);
                res = nn_value_makebool(dbinleft < dbinright);
            }
            break;
        default:
            {
                fprintf(stderr, "unhandled instruction %d (%s)!\n", instruction, nn_dbg_op2str(instruction));
                return false;
            }
            break;
    }
    nn_vmbits_stackpush(state, res);
    return true;
}

static NEON_FORCEINLINE bool nn_vmdo_globaldefine(NeonState* state)
{
    NeonValue val;
    NeonObjString* name;
    NeonHashTable* tab;
    name = nn_vmbits_readstring(state);
    val = nn_vmbits_stackpeek(state, 0);
    if(nn_value_isempty(val))
    {
        nn_vmmac_tryraise(state, false, "empty cannot be assigned");
        return false;
    }
    tab = state->vmstate.currentframe->closure->scriptfunc->module->deftable;
    nn_table_set(tab, nn_value_fromobject(name), val);
    nn_vmbits_stackpop(state);
    #if (defined(DEBUG_TABLE) && DEBUG_TABLE) || 0
    nn_table_print(state, state->debugwriter, state->globals, "globals");
    #endif
    return true;
}

static NEON_FORCEINLINE bool nn_vmdo_globalget(NeonState* state)
{
    NeonObjString* name;
    NeonHashTable* tab;
    NeonProperty* field;
    name = nn_vmbits_readstring(state);
    tab = state->vmstate.currentframe->closure->scriptfunc->module->deftable;
    field = nn_table_getfieldbyostr(tab, name);
    if(field == NULL)
    {
        field = nn_table_getfieldbyostr(state->globals, name);
        if(field == NULL)
        {
            nn_vmmac_tryraise(state, false, "global name '%s' is not defined", name->sbuf->data);
            return false;
        }
    }
    nn_vmbits_stackpush(state, field->value);
    return true;
}

static NEON_FORCEINLINE bool nn_vmdo_globalset(NeonState* state)
{
    NeonObjString* name;
    NeonHashTable* table;
    if(nn_value_isempty(nn_vmbits_stackpeek(state, 0)))
    {
        nn_vmmac_tryraise(state, false, "empty cannot be assigned");
        return false;
    }
    name = nn_vmbits_readstring(state);
    table = state->vmstate.currentframe->closure->scriptfunc->module->deftable;
    if(nn_table_set(table, nn_value_fromobject(name), nn_vmbits_stackpeek(state, 0)))
    {
        if(state->conf.enablestrictmode)
        {
            nn_table_delete(table, nn_value_fromobject(name));
            nn_vmmac_tryraise(state, false, "global name '%s' was not declared", name->sbuf->data);
            return false;
        }
    }
    return true;
}

static NEON_FORCEINLINE bool nn_vmdo_localget(NeonState* state)
{
    size_t ssp;
    uint16_t slot;
    NeonValue val;
    slot = nn_vmbits_readshort(state);
    ssp = state->vmstate.currentframe->stackslotpos;
    val = state->vmstate.stackvalues[ssp + slot];
    nn_vmbits_stackpush(state, val);
    return true;
}

static NEON_FORCEINLINE bool nn_vmdo_localset(NeonState* state)
{
    size_t ssp;
    uint16_t slot;
    NeonValue peeked;
    slot = nn_vmbits_readshort(state);
    peeked = nn_vmbits_stackpeek(state, 0);
    if(nn_value_isempty(peeked))
    {
        nn_vmmac_tryraise(state, false, "empty cannot be assigned");
        return false;
    }
    ssp = state->vmstate.currentframe->stackslotpos;
    state->vmstate.stackvalues[ssp + slot] = peeked;
    return true;
}

static NEON_FORCEINLINE bool nn_vmdo_funcargget(NeonState* state)
{
    size_t ssp;
    uint16_t slot;
    NeonValue val;
    slot = nn_vmbits_readshort(state);
    ssp = state->vmstate.currentframe->stackslotpos;
    //fprintf(stderr, "FUNCARGGET: %s\n", state->vmstate.currentframe->closure->scriptfunc->name->sbuf->data);
    val = state->vmstate.stackvalues[ssp + slot];
    nn_vmbits_stackpush(state, val);
    return true;
}

static NEON_FORCEINLINE bool nn_vmdo_funcargset(NeonState* state)
{
    size_t ssp;
    uint16_t slot;
    NeonValue peeked;
    slot = nn_vmbits_readshort(state);
    peeked = nn_vmbits_stackpeek(state, 0);
    if(nn_value_isempty(peeked))
    {
        nn_vmmac_tryraise(state, false, "empty cannot be assigned");
        return false;
    }
    ssp = state->vmstate.currentframe->stackslotpos;
    state->vmstate.stackvalues[ssp + slot] = peeked;
    return true;
}

static NEON_FORCEINLINE bool nn_vmdo_makeclosure(NeonState* state)
{
    int i;
    int index;
    size_t ssp;
    uint8_t islocal;
    NeonValue* upvals;
    NeonObjFuncScript* function;
    NeonObjFuncClosure* closure;
    function = nn_value_asfuncscript(nn_vmbits_readconst(state));
    closure = nn_object_makefuncclosure(state, function);
    nn_vmbits_stackpush(state, nn_value_fromobject(closure));
    for(i = 0; i < closure->upvalcount; i++)
    {
        islocal = nn_vmbits_readbyte(state);
        index = nn_vmbits_readshort(state);
        if(islocal)
        {
            ssp = state->vmstate.currentframe->stackslotpos;
            upvals = &state->vmstate.stackvalues[ssp + index];
            closure->upvalues[i] = nn_vmutil_upvaluescapture(state, upvals, index);

        }
        else
        {
            closure->upvalues[i] = state->vmstate.currentframe->closure->upvalues[index];
        }
    }
    return true;
}

static NEON_FORCEINLINE bool nn_vmdo_makearray(NeonState* state)
{
    int i;
    int count;
    NeonObjArray* array;
    count = nn_vmbits_readshort(state);
    array = nn_object_makearray(state);
    state->vmstate.stackvalues[state->vmstate.stackidx + (-count - 1)] = nn_value_fromobject(array);
    for(i = count - 1; i >= 0; i--)
    {
        nn_array_push(array, nn_vmbits_stackpeek(state, i));
    }
    nn_vmbits_stackpopn(state, count);
    return true;
}

static NEON_FORCEINLINE bool nn_vmdo_makedict(NeonState* state)
{
    int i;
    int count;
    int realcount;
    NeonValue name;
    NeonValue value;
    NeonObjDict* dict;
    /* 1 for key, 1 for value */
    realcount = nn_vmbits_readshort(state);
    count = realcount * 2;
    dict = nn_object_makedict(state);
    state->vmstate.stackvalues[state->vmstate.stackidx + (-count - 1)] = nn_value_fromobject(dict);
    for(i = 0; i < count; i += 2)
    {
        name = state->vmstate.stackvalues[state->vmstate.stackidx + (-count + i)];
        if(!nn_value_isstring(name) && !nn_value_isnumber(name) && !nn_value_isbool(name))
        {
            nn_vmmac_tryraise(state, false, "dictionary key must be one of string, number or boolean");
            return false;
        }
        value = state->vmstate.stackvalues[state->vmstate.stackidx + (-count + i + 1)];
        nn_dict_setentry(dict, name, value);
    }
    nn_vmbits_stackpopn(state, count);
    return true;
}

#define BINARY_MOD_OP(state, type, op) \
    do \
    { \
        double dbinright; \
        double dbinleft; \
        NeonValue binvalright; \
        NeonValue binvalleft; \
        binvalright = nn_vmbits_stackpeek(state, 0); \
        binvalleft = nn_vmbits_stackpeek(state, 1);\
        if((!nn_value_isnumber(binvalright) && !nn_value_isbool(binvalright)) \
        || (!nn_value_isnumber(binvalleft) && !nn_value_isbool(binvalleft))) \
        { \
            nn_vmmac_tryraise(state, NEON_STATUS_FAILRUNTIME, "unsupported operand %s for %s and %s", #op, nn_value_typename(binvalleft), nn_value_typename(binvalright)); \
            break; \
        } \
        binvalright = nn_vmbits_stackpop(state); \
        dbinright = nn_value_isbool(binvalright) ? (nn_value_asbool(binvalright) ? 1 : 0) : nn_value_asnumber(binvalright); \
        binvalleft = nn_vmbits_stackpop(state); \
        dbinleft = nn_value_isbool(binvalleft) ? (nn_value_asbool(binvalleft) ? 1 : 0) : nn_value_asnumber(binvalleft); \
        nn_vmbits_stackpush(state, type(op(dbinleft, dbinright))); \
    } while(false)



NeonStatus nn_vm_runvm(NeonState* state, int exitframe, NeonValue* rv)
{
    int iterpos;
    int printpos;
    int ofs;
    /*
    * this variable is a NOP; it only exists to ensure that functions outside of the
    * switch tree are not calling nn_vmmac_exitvm(), as its behavior could be undefined.
    */
    bool you_are_calling_exit_vm_outside_of_runvm;
    NeonValue* dbgslot;
    NeonInstruction currinstr;
    you_are_calling_exit_vm_outside_of_runvm = false;
    state->vmstate.currentframe = &state->vmstate.framevalues[state->vmstate.framecount - 1];
    for(;;)
    {
        /*
        // try...finally... (i.e. try without a catch but finally
        // whose try body raises an exception)
        // can cause us to go into an invalid mode where frame count == 0
        // to fix this, we need to exit with an appropriate mode here.
        */
        if(state->vmstate.framecount == 0)
        {
            return NEON_STATUS_FAILRUNTIME;
        }
        if(state->conf.shoulddumpstack)
        {
            ofs = (int)(state->vmstate.currentframe->inscode - state->vmstate.currentframe->closure->scriptfunc->blob.instrucs);
            nn_dbg_printinstructionat(state->debugwriter, &state->vmstate.currentframe->closure->scriptfunc->blob, ofs);
            fprintf(stderr, "stack (before)=[\n");
            iterpos = 0;
            for(dbgslot = state->vmstate.stackvalues; dbgslot < &state->vmstate.stackvalues[state->vmstate.stackidx]; dbgslot++)
            {
                printpos = iterpos + 1;
                iterpos++;
                fprintf(stderr, "  [%s%d%s] ", nn_util_color(NEON_COLOR_YELLOW), printpos, nn_util_color(NEON_COLOR_RESET));
                nn_printer_writefmt(state->debugwriter, "%s", nn_util_color(NEON_COLOR_YELLOW));
                nn_printer_printvalue(state->debugwriter, *dbgslot, true, false);
                nn_printer_writefmt(state->debugwriter, "%s", nn_util_color(NEON_COLOR_RESET));
                fprintf(stderr, "\n");
            }
            fprintf(stderr, "]\n");
        }
        currinstr = nn_vmbits_readinstruction(state);
        state->vmstate.currentinstr = currinstr;
        //fprintf(stderr, "now executing at line %d\n", state->vmstate.currentinstr.srcline);
        switch(currinstr.code)
        {
            case NEON_OP_RETURN:
                {
                    size_t ssp;
                    NeonValue result;
                    result = nn_vmbits_stackpop(state);
                    if(rv != NULL)
                    {
                        *rv = result;
                    }
                    ssp = state->vmstate.currentframe->stackslotpos;
                    nn_vmutil_upvaluesclose(state, &state->vmstate.stackvalues[ssp]);
                    state->vmstate.framecount--;
                    if(state->vmstate.framecount == 0)
                    {
                        nn_vmbits_stackpop(state);
                        return NEON_STATUS_OK;
                    }
                    ssp = state->vmstate.currentframe->stackslotpos;
                    state->vmstate.stackidx = ssp;
                    nn_vmbits_stackpush(state, result);
                    state->vmstate.currentframe = &state->vmstate.framevalues[state->vmstate.framecount - 1];
                    if(state->vmstate.framecount == (size_t)exitframe)
                    {
                        return NEON_STATUS_OK;
                    }
                }
                break;
            case NEON_OP_PUSHCONSTANT:
                {
                    NeonValue constant;
                    constant = nn_vmbits_readconst(state);
                    nn_vmbits_stackpush(state, constant);
                }
                break;
            case NEON_OP_PRIMADD:
                {
                    NeonValue valright;
                    NeonValue valleft;
                    NeonValue result;
                    valright = nn_vmbits_stackpeek(state, 0);
                    valleft = nn_vmbits_stackpeek(state, 1);
                    if(nn_value_isstring(valright) || nn_value_isstring(valleft))
                    {
                        if(!nn_vmutil_concatenate(state))
                        {
                            nn_vmmac_tryraise(state, NEON_STATUS_FAILRUNTIME, "unsupported operand + for %s and %s", nn_value_typename(valleft), nn_value_typename(valright));
                            break;
                        }
                    }
                    else if(nn_value_isarray(valleft) && nn_value_isarray(valright))
                    {
                        result = nn_value_fromobject(nn_vmutil_combinearrays(state, nn_value_asarray(valleft), nn_value_asarray(valright)));
                        nn_vmbits_stackpopn(state, 2);
                        nn_vmbits_stackpush(state, result);
                    }
                    else
                    {
                        nn_vmdo_dobinary(state);
                    }
                }
                break;
            case NEON_OP_PRIMSUBTRACT:
                {
                    nn_vmdo_dobinary(state);
                }
                break;
            case NEON_OP_PRIMMULTIPLY:
                {
                    int intnum;
                    double dbnum;
                    NeonValue peekleft;
                    NeonValue peekright;
                    NeonValue result;
                    NeonObjString* string;
                    NeonObjArray* list;
                    NeonObjArray* newlist;
                    peekright = nn_vmbits_stackpeek(state, 0);
                    peekleft = nn_vmbits_stackpeek(state, 1);
                    if(nn_value_isstring(peekleft) && nn_value_isnumber(peekright))
                    {
                        dbnum = nn_value_asnumber(peekright);
                        string = nn_value_asstring(nn_vmbits_stackpeek(state, 1));
                        result = nn_value_fromobject(nn_vmutil_multiplystring(state, string, dbnum));
                        nn_vmbits_stackpopn(state, 2);
                        nn_vmbits_stackpush(state, result);
                        break;
                    }
                    else if(nn_value_isarray(peekleft) && nn_value_isnumber(peekright))
                    {
                        intnum = (int)nn_value_asnumber(peekright);
                        nn_vmbits_stackpop(state);
                        list = nn_value_asarray(peekleft);
                        newlist = nn_object_makearray(state);
                        nn_vmbits_stackpush(state, nn_value_fromobject(newlist));
                        nn_vmutil_multiplyarray(state, list, newlist, intnum);
                        nn_vmbits_stackpopn(state, 2);
                        nn_vmbits_stackpush(state, nn_value_fromobject(newlist));
                        break;
                    }
                    nn_vmdo_dobinary(state);
                }
                break;
            case NEON_OP_PRIMDIVIDE:
                {
                    nn_vmdo_dobinary(state);
                }
                break;
            case NEON_OP_PRIMMODULO:
                {
                    BINARY_MOD_OP(state, nn_value_makenumber, nn_vmutil_modulo);
                }
                break;
            case NEON_OP_PRIMPOW:
                {
                    BINARY_MOD_OP(state, nn_value_makenumber, pow);
                }
                break;
            case NEON_OP_PRIMFLOORDIVIDE:
                {
                    BINARY_MOD_OP(state, nn_value_makenumber, nn_vmutil_floordiv);
                }
                break;
            case NEON_OP_PRIMNEGATE:
                {
                    NeonValue peeked;
                    peeked = nn_vmbits_stackpeek(state, 0);
                    if(!nn_value_isnumber(peeked))
                    {
                        nn_vmmac_tryraise(state, NEON_STATUS_FAILRUNTIME, "operator - not defined for object of type %s", nn_value_typename(peeked));
                        break;
                    }
                    nn_vmbits_stackpush(state, nn_value_makenumber(-nn_value_asnumber(nn_vmbits_stackpop(state))));
                }
                break;
            case NEON_OP_PRIMBITNOT:
            {
                NeonValue peeked;
                peeked = nn_vmbits_stackpeek(state, 0);
                if(!nn_value_isnumber(peeked))
                {
                    nn_vmmac_tryraise(state, NEON_STATUS_FAILRUNTIME, "operator ~ not defined for object of type %s", nn_value_typename(peeked));
                    break;
                }
                nn_vmbits_stackpush(state, nn_value_makeint(~((int)nn_value_asnumber(nn_vmbits_stackpop(state)))));
                break;
            }
            case NEON_OP_PRIMAND:
                {
                    nn_vmdo_dobinary(state);
                }
                break;
            case NEON_OP_PRIMOR:
                {
                    nn_vmdo_dobinary(state);
                }
                break;
            case NEON_OP_PRIMBITXOR:
                {
                    nn_vmdo_dobinary(state);
                }
                break;
            case NEON_OP_PRIMSHIFTLEFT:
                {
                    nn_vmdo_dobinary(state);
                }
                break;
            case NEON_OP_PRIMSHIFTRIGHT:
                {
                    nn_vmdo_dobinary(state);
                }
                break;
            case NEON_OP_PUSHONE:
                {
                    nn_vmbits_stackpush(state, nn_value_makenumber(1));
                }
                break;
            /* comparisons */
            case NEON_OP_EQUAL:
                {
                    NeonValue a;
                    NeonValue b;
                    b = nn_vmbits_stackpop(state);
                    a = nn_vmbits_stackpop(state);
                    nn_vmbits_stackpush(state, nn_value_makebool(nn_value_compare(state, a, b)));
                }
                break;
            case NEON_OP_PRIMGREATER:
                {
                    nn_vmdo_dobinary(state);
                }
                break;
            case NEON_OP_PRIMLESSTHAN:
                {
                    nn_vmdo_dobinary(state);
                }
                break;
            case NEON_OP_PRIMNOT:
                {
                    nn_vmbits_stackpush(state, nn_value_makebool(nn_value_isfalse(nn_vmbits_stackpop(state))));
                }
                break;
            case NEON_OP_PUSHNULL:
                {
                    nn_vmbits_stackpush(state, nn_value_makenull());
                }
                break;
            case NEON_OP_PUSHEMPTY:
                {
                    nn_vmbits_stackpush(state, nn_value_makeempty());
                }
                break;
            case NEON_OP_PUSHTRUE:
                {
                    nn_vmbits_stackpush(state, nn_value_makebool(true));
                }
                break;
            case NEON_OP_PUSHFALSE:
                {
                    nn_vmbits_stackpush(state, nn_value_makebool(false));
                }
                break;

            case NEON_OP_JUMPNOW:
                {
                    uint16_t offset;
                    offset = nn_vmbits_readshort(state);
                    state->vmstate.currentframe->inscode += offset;
                }
                break;
            case NEON_OP_JUMPIFFALSE:
                {
                    uint16_t offset;
                    offset = nn_vmbits_readshort(state);
                    if(nn_value_isfalse(nn_vmbits_stackpeek(state, 0)))
                    {
                        state->vmstate.currentframe->inscode += offset;
                    }
                }
                break;
            case NEON_OP_LOOP:
                {
                    uint16_t offset;
                    offset = nn_vmbits_readshort(state);
                    state->vmstate.currentframe->inscode -= offset;
                }
                break;
            case NEON_OP_ECHO:
                {
                    NeonValue val;
                    val = nn_vmbits_stackpeek(state, 0);
                    nn_printer_printvalue(state->stdoutprinter, val, state->isrepl, true);
                    if(!nn_value_isempty(val))
                    {
                        nn_printer_writestring(state->stdoutprinter, "\n");
                    }
                    nn_vmbits_stackpop(state);
                }
                break;
            case NEON_OP_STRINGIFY:
                {
                    NeonValue peeked;
                    NeonObjString* value;
                    peeked = nn_vmbits_stackpeek(state, 0);
                    if(!nn_value_isstring(peeked) && !nn_value_isnull(peeked))
                    {
                        value = nn_value_tostring(state, nn_vmbits_stackpop(state));
                        if(value->sbuf->length != 0)
                        {
                            nn_vmbits_stackpush(state, nn_value_fromobject(value));
                        }
                        else
                        {
                            nn_vmbits_stackpush(state, nn_value_makenull());
                        }
                    }
                }
                break;
            case NEON_OP_DUPONE:
                {
                    nn_vmbits_stackpush(state, nn_vmbits_stackpeek(state, 0));
                }
                break;
            case NEON_OP_POPONE:
                {
                    nn_vmbits_stackpop(state);
                }
                break;
            case NEON_OP_POPN:
                {
                    nn_vmbits_stackpopn(state, nn_vmbits_readshort(state));
                }
                break;
            case NEON_OP_UPVALUECLOSE:
                {
                    nn_vmutil_upvaluesclose(state, &state->vmstate.stackvalues[state->vmstate.stackidx - 1]);
                    nn_vmbits_stackpop(state);
                }
                break;
            case NEON_OP_GLOBALDEFINE:
                {
                    if(!nn_vmdo_globaldefine(state))
                    {
                        nn_vmmac_exitvm(state);
                    }
                }
                break;
            case NEON_OP_GLOBALGET:
                {
                    if(!nn_vmdo_globalget(state))
                    {
                        nn_vmmac_exitvm(state);
                    }
                }
                break;
            case NEON_OP_GLOBALSET:
                {
                    if(!nn_vmdo_globalset(state))
                    {
                        nn_vmmac_exitvm(state);
                    }
                }
                break;
            case NEON_OP_LOCALGET:
                {
                    if(!nn_vmdo_localget(state))
                    {
                        nn_vmmac_exitvm(state);
                    }
                }
                break;
            case NEON_OP_LOCALSET:
                {
                    if(!nn_vmdo_localset(state))
                    {
                        nn_vmmac_exitvm(state);
                    }
                }
                break;
            case NEON_OP_FUNCARGGET:
                {
                    if(!nn_vmdo_funcargget(state))
                    {
                        nn_vmmac_exitvm(state);
                    }
                }
                break;
            case NEON_OP_FUNCARGSET:
                {
                    if(!nn_vmdo_funcargset(state))
                    {
                        nn_vmmac_exitvm(state);
                    }
                }
                break;

            case NEON_OP_PROPERTYGET:
                {
                    if(!nn_vmdo_propertyget(state))
                    {
                        nn_vmmac_exitvm(state);
                    }
                }
                break;
            case NEON_OP_PROPERTYSET:
                {
                    if(!nn_vmdo_propertyset(state))
                    {
                        nn_vmmac_exitvm(state);
                    }
                }
                break;
            case NEON_OP_PROPERTYGETSELF:
                {
                    if(!nn_vmdo_propertygetself(state))
                    {
                        nn_vmmac_exitvm(state);
                    }
                }
                break;
            case NEON_OP_MAKECLOSURE:
                {
                    if(!nn_vmdo_makeclosure(state))
                    {
                        nn_vmmac_exitvm(state);
                    }
                }
                break;
            case NEON_OP_UPVALUEGET:
                {
                    int index;
                    NeonObjFuncClosure* closure;
                    index = nn_vmbits_readshort(state);
                    closure = state->vmstate.currentframe->closure;
                    if(index < closure->upvalcount)
                    {
                        nn_vmbits_stackpush(state, closure->upvalues[index]->location);
                    }
                    else
                    {
                        nn_vmbits_stackpush(state, nn_value_makeempty());
                    }
                }
                break;
            case NEON_OP_UPVALUESET:
                {
                    int index;
                    index = nn_vmbits_readshort(state);
                    if(nn_value_isempty(nn_vmbits_stackpeek(state, 0)))
                    {
                        nn_vmmac_tryraise(state, NEON_STATUS_FAILRUNTIME, "empty cannot be assigned");
                        break;
                    }
                    state->vmstate.currentframe->closure->upvalues[index]->location = nn_vmbits_stackpeek(state, 0);
                }
                break;
            case NEON_OP_CALLFUNCTION:
                {
                    int argcount;
                    argcount = nn_vmbits_readbyte(state);
                    if(!nn_vm_callvalue(state, nn_vmbits_stackpeek(state, argcount), nn_value_makeempty(), argcount))
                    {
                        nn_vmmac_exitvm(state);
                    }
                    state->vmstate.currentframe = &state->vmstate.framevalues[state->vmstate.framecount - 1];
                }
                break;
            case NEON_OP_CALLMETHOD:
                {
                    int argcount;
                    NeonObjString* method;
                    method = nn_vmbits_readstring(state);
                    argcount = nn_vmbits_readbyte(state);
                    if(!nn_vmutil_invokemethod(state, method, argcount))
                    {
                        nn_vmmac_exitvm(state);
                    }
                    state->vmstate.currentframe = &state->vmstate.framevalues[state->vmstate.framecount - 1];
                }
                break;
            case NEON_OP_CLASSGETTHIS:
                {
                    NeonValue thisval;
                    //thisval = nn_vmbits_stackpeek(state, state->vmstate.stackidx-0);
                    thisval = nn_vmbits_stackpeek(state, 3);
                    nn_printer_writefmt(state->debugwriter, "CLASSGETTHIS: thisval=");
                    nn_printer_printvalue(state->debugwriter, thisval, true, false);
                    nn_printer_writefmt(state->debugwriter, "\n");
                    nn_vmbits_stackpush(state, thisval);
                }
                break;
            case NEON_OP_CLASSINVOKETHIS:
                {
                    int argcount;
                    NeonObjString* method;
                    method = nn_vmbits_readstring(state);
                    argcount = nn_vmbits_readbyte(state);
                    if(!nn_vmutil_invokemethodself(state, method, argcount))
                    {
                        nn_vmmac_exitvm(state);
                    }
                    state->vmstate.currentframe = &state->vmstate.framevalues[state->vmstate.framecount - 1];
                }
                break;
            case NEON_OP_MAKECLASS:
                {
                    bool haveval;
                    NeonValue pushme;
                    NeonObjString* name;
                    NeonObjClass* klass;
                    NeonProperty* field;
                    haveval = false;
                    name = nn_vmbits_readstring(state);
                    field = nn_table_getfieldbyostr(state->vmstate.currentframe->closure->scriptfunc->module->deftable, name);
                    if(field != NULL)
                    {
                        if(nn_value_isclass(field->value))
                        {
                            haveval = true;
                            pushme = field->value;
                        }
                    }
                    field = nn_table_getfieldbyostr(state->globals, name);
                    if(field != NULL)
                    {
                        if(nn_value_isclass(field->value))
                        {
                            haveval = true;
                            pushme = field->value;
                        }
                    }
                    if(!haveval)
                    {
                        klass = nn_object_makeclass(state, name);
                        pushme = nn_value_fromobject(klass);
                    }
                    nn_vmbits_stackpush(state, pushme);
                }
                break;
            case NEON_OP_MAKEMETHOD:
                {
                    NeonObjString* name;
                    name = nn_vmbits_readstring(state);
                    nn_vmutil_definemethod(state, name);
                }
                break;
            case NEON_OP_CLASSPROPERTYDEFINE:
                {
                    int isstatic;
                    NeonObjString* name;
                    name = nn_vmbits_readstring(state);
                    isstatic = nn_vmbits_readbyte(state);
                    nn_vmutil_defineproperty(state, name, isstatic == 1);
                }
                break;
            case NEON_OP_CLASSINHERIT:
                {
                    NeonObjClass* superclass;
                    NeonObjClass* subclass;
                    if(!nn_value_isclass(nn_vmbits_stackpeek(state, 1)))
                    {
                        nn_vmmac_tryraise(state, NEON_STATUS_FAILRUNTIME, "cannot inherit from non-class object");
                        break;
                    }
                    superclass = nn_value_asclass(nn_vmbits_stackpeek(state, 1));
                    subclass = nn_value_asclass(nn_vmbits_stackpeek(state, 0));
                    nn_class_inheritfrom(subclass, superclass);
                    /* pop the subclass */
                    nn_vmbits_stackpop(state);
                }
                break;
            case NEON_OP_CLASSGETSUPER:
                {
                    NeonObjClass* klass;
                    NeonObjString* name;
                    name = nn_vmbits_readstring(state);
                    klass = nn_value_asclass(nn_vmbits_stackpeek(state, 0));
                    if(!nn_vmutil_bindmethod(state, klass->superclass, name))
                    {
                        nn_vmmac_tryraise(state, NEON_STATUS_FAILRUNTIME, "class %s does not define a function %s", klass->name->sbuf->data, name->sbuf->data);
                    }
                }
                break;
            case NEON_OP_CLASSINVOKESUPER:
                {
                    int argcount;
                    NeonObjClass* klass;
                    NeonObjString* method;
                    method = nn_vmbits_readstring(state);
                    argcount = nn_vmbits_readbyte(state);
                    klass = nn_value_asclass(nn_vmbits_stackpop(state));
                    if(!nn_vmutil_invokemethodfromclass(state, klass, method, argcount))
                    {
                        nn_vmmac_exitvm(state);
                    }
                    state->vmstate.currentframe = &state->vmstate.framevalues[state->vmstate.framecount - 1];
                }
                break;
            case NEON_OP_CLASSINVOKESUPERSELF:
                {
                    int argcount;
                    NeonObjClass* klass;
                    argcount = nn_vmbits_readbyte(state);
                    klass = nn_value_asclass(nn_vmbits_stackpop(state));
                    if(!nn_vmutil_invokemethodfromclass(state, klass, state->constructorname, argcount))
                    {
                        nn_vmmac_exitvm(state);
                    }
                    state->vmstate.currentframe = &state->vmstate.framevalues[state->vmstate.framecount - 1];
                }
                break;
            case NEON_OP_MAKEARRAY:
                {
                    if(!nn_vmdo_makearray(state))
                    {
                        nn_vmmac_exitvm(state);
                    }
                }
                break;

            case NEON_OP_MAKERANGE:
                {
                    double lower;
                    double upper;
                    NeonValue vupper;
                    NeonValue vlower;
                    vupper = nn_vmbits_stackpeek(state, 0);
                    vlower = nn_vmbits_stackpeek(state, 1);
                    if(!nn_value_isnumber(vupper) || !nn_value_isnumber(vlower))
                    {
                        nn_vmmac_tryraise(state, NEON_STATUS_FAILRUNTIME, "invalid range boundaries");
                        break;
                    }
                    lower = nn_value_asnumber(vlower);
                    upper = nn_value_asnumber(vupper);
                    nn_vmbits_stackpopn(state, 2);
                    nn_vmbits_stackpush(state, nn_value_fromobject(nn_object_makerange(state, lower, upper)));
                }
                break;
            case NEON_OP_MAKEDICT:
                {
                    if(!nn_vmdo_makedict(state))
                    {
                        nn_vmmac_exitvm(state);
                    }
                }
                break;
            case NEON_OP_INDEXGETRANGED:
                {
                    if(!nn_vmdo_getrangedindex(state))
                    {
                        nn_vmmac_exitvm(state);
                    }
                }
                break;
            case NEON_OP_INDEXGET:
                {
                    if(!nn_vmdo_indexget(state))
                    {
                        nn_vmmac_exitvm(state);
                    }
                }
                break;
            case NEON_OP_INDEXSET:
                {
                    if(!nn_vmdo_indexset(state))
                    {
                        nn_vmmac_exitvm(state);
                    }
                }
                break;
            case NEON_OP_IMPORTIMPORT:
                {
                    NeonValue res;
                    NeonObjString* name;
                    NeonObjModule* mod;
                    name = nn_value_asstring(nn_vmbits_stackpeek(state, 0));
                    fprintf(stderr, "IMPORTIMPORT: name='%s'\n", name->sbuf->data);
                    mod = nn_import_loadmodulescript(state, state->topmodule, name);
                    fprintf(stderr, "IMPORTIMPORT: mod='%p'\n", mod);
                    if(mod == NULL)
                    {
                        res = nn_value_makenull();
                    }
                    else
                    {
                        res = nn_value_fromobject(mod);
                    }
                    nn_vmbits_stackpush(state, res);
                }
                break;
            case NEON_OP_TYPEOF:
                {
                    NeonValue res;
                    NeonValue thing;
                    const char* result;
                    thing = nn_vmbits_stackpop(state);
                    result = nn_value_typename(thing);
                    res = nn_value_fromobject(nn_string_copycstr(state, result));
                    nn_vmbits_stackpush(state, res);
                }
                break;
            case NEON_OP_ASSERT:
                {
                    NeonValue message;
                    NeonValue expression;
                    message = nn_vmbits_stackpop(state);
                    expression = nn_vmbits_stackpop(state);
                    if(nn_value_isfalse(expression))
                    {
                        if(!nn_value_isnull(message))
                        {
                            nn_exceptions_throwclass(state, state->exceptions.asserterror, nn_value_tostring(state, message)->sbuf->data);
                        }
                        else
                        {
                            nn_exceptions_throwclass(state, state->exceptions.asserterror, "assertion failed");
                        }
                    }
                }
                break;
            case NEON_OP_EXTHROW:
                {
                    bool isok;
                    NeonValue peeked;
                    NeonValue stacktrace;
                    NeonObjInstance* instance;
                    peeked = nn_vmbits_stackpeek(state, 0);
                    isok = (
                        nn_value_isinstance(peeked) ||
                        nn_util_isinstanceof(nn_value_asinstance(peeked)->klass, state->exceptions.stdexception)
                    );
                    if(!isok)
                    {
                        nn_vmmac_tryraise(state, NEON_STATUS_FAILRUNTIME, "instance of Exception expected");
                        break;
                    }
                    stacktrace = nn_exceptions_getstacktrace(state);
                    instance = nn_value_asinstance(peeked);
                    nn_instance_defproperty(instance, "stacktrace", stacktrace);
                    if(nn_exceptions_propagate(state))
                    {
                        state->vmstate.currentframe = &state->vmstate.framevalues[state->vmstate.framecount - 1];
                        break;
                    }
                    nn_vmmac_exitvm(state);
                }
            case NEON_OP_EXTRY:
                {
                    uint16_t addr;
                    uint16_t finaddr;
                    NeonValue value;
                    NeonObjString* type;
                    type = nn_vmbits_readstring(state);
                    addr = nn_vmbits_readshort(state);
                    finaddr = nn_vmbits_readshort(state);
                    if(addr != 0)
                    {
                        if(!nn_table_get(state->globals, nn_value_fromobject(type), &value) || !nn_value_isclass(value))
                        {
                            if(!nn_table_get(state->vmstate.currentframe->closure->scriptfunc->module->deftable, nn_value_fromobject(type), &value) || !nn_value_isclass(value))
                            {
                                nn_vmmac_tryraise(state, NEON_STATUS_FAILRUNTIME, "object of type '%s' is not an exception", type->sbuf->data);
                                break;
                            }
                        }
                        nn_exceptions_pushhandler(state, nn_value_asclass(value), addr, finaddr);
                    }
                    else
                    {
                        nn_exceptions_pushhandler(state, NULL, addr, finaddr);
                    }
                }
                break;
            case NEON_OP_EXPOPTRY:
                {
                    state->vmstate.currentframe->handlercount--;
                }
                break;
            case NEON_OP_EXPUBLISHTRY:
                {
                    state->vmstate.currentframe->handlercount--;
                    if(nn_exceptions_propagate(state))
                    {
                        state->vmstate.currentframe = &state->vmstate.framevalues[state->vmstate.framecount - 1];
                        break;
                    }
                    nn_vmmac_exitvm(state);
                }
                break;
            case NEON_OP_SWITCH:
                {
                    NeonValue expr;
                    NeonValue value;
                    NeonObjSwitch* sw;
                    sw = nn_value_asswitch(nn_vmbits_readconst(state));
                    expr = nn_vmbits_stackpeek(state, 0);
                    if(nn_table_get(sw->table, expr, &value))
                    {
                        state->vmstate.currentframe->inscode += (int)nn_value_asnumber(value);
                    }
                    else if(sw->defaultjump != -1)
                    {
                        state->vmstate.currentframe->inscode += sw->defaultjump;
                    }
                    else
                    {
                        state->vmstate.currentframe->inscode += sw->exitjump;
                    }
                    nn_vmbits_stackpop(state);
                }
                break;
            default:
                {
                }
                break;
        }

    }
}

int nn_nestcall_prepare(NeonState* state, NeonValue callable, NeonValue mthobj, NeonObjArray* callarr)
{
    int arity;
    NeonObjFuncClosure* closure;
    (void)state;
    arity = 0;
    if(nn_value_isfuncclosure(callable))
    {
        closure = nn_value_asfuncclosure(callable);
        arity = closure->scriptfunc->arity;
    }
    else if(nn_value_isfuncscript(callable))
    {
        arity = nn_value_asfuncscript(callable)->arity;
    }
    else if(nn_value_isfuncnative(callable))
    {
        //arity = nn_value_asfuncnative(callable);
    }
    if(arity > 0)
    {
        nn_array_push(callarr, nn_value_makenull());
        if(arity > 1)
        {
            nn_array_push(callarr, nn_value_makenull());
            if(arity > 2)
            {
                nn_array_push(callarr, mthobj);
            }
        }
    }
    return arity;
}

/* helper function to access call outside the state file. */
bool nn_nestcall_callfunction(NeonState* state, NeonValue callable, NeonValue thisval, NeonObjArray* args, NeonValue* dest)
{
    int i;
    int argc;
    size_t pidx;
    NeonStatus status;
    pidx = state->vmstate.stackidx;
    /* set the closure before the args */
    nn_vm_stackpush(state, callable);
    argc = 0;
    if(args && (argc = args->varray->count))
    {
        for(i = 0; i < args->varray->count; i++)
        {
            nn_vm_stackpush(state, args->varray->values[i]);
        }
    }
    if(!nn_vm_callvaluewithobject(state, callable, thisval, argc))
    {
        fprintf(stderr, "nestcall: nn_vm_callvalue() failed\n");
        abort();
    }
    status = nn_vm_runvm(state, state->vmstate.framecount - 1, NULL);
    if(status != NEON_STATUS_OK)
    {
        fprintf(stderr, "nestcall: call to runvm failed\n");
        abort();
    }
    *dest = state->vmstate.stackvalues[state->vmstate.stackidx - 1];
    nn_vm_stackpopn(state, argc + 1);
    state->vmstate.stackidx = pidx;
    return true;
}

NeonObjFuncClosure* nn_state_compilesource(NeonState* state, NeonObjModule* module, bool fromeval, const char* source)
{
    NeonBlob blob;
    NeonObjFuncScript* function;
    NeonObjFuncClosure* closure;
    nn_blob_init(state, &blob);
    function = nn_astparser_compilesource(state, module, source, &blob, false, fromeval);
    if(function == NULL)
    {
        nn_blob_destroy(state, &blob);
        return NULL;
    }
    if(!fromeval)
    {
        nn_vm_stackpush(state, nn_value_fromobject(function));
    }
    else
    {
        function->name = nn_string_copycstr(state, "(evaledcode)");
    }
    closure = nn_object_makefuncclosure(state, function);
    if(!fromeval)
    {
        nn_vm_stackpop(state);
        nn_vm_stackpush(state, nn_value_fromobject(closure));
    }
    nn_blob_destroy(state, &blob);
    return closure;
}

NeonStatus nn_state_execsource(NeonState* state, NeonObjModule* module, const char* source, NeonValue* dest)
{
    NeonStatus status;
    NeonObjFuncClosure* closure;
    nn_module_setfilefield(state, module);
    closure = nn_state_compilesource(state, module, false, source);
    if(closure == NULL)
    {
        return NEON_STATUS_FAILCOMPILE;
    }
    if(state->conf.exitafterbytecode)
    {
        return NEON_STATUS_OK;
    }
    nn_vm_callclosure(state, closure, nn_value_makenull(), 0);
    status = nn_vm_runvm(state, 0, dest);
    return status;
}

NeonValue nn_state_evalsource(NeonState* state, const char* source)
{
    bool ok;
    int argc;
    NeonValue callme;
    NeonValue retval;
    NeonObjFuncClosure* closure;
    NeonObjArray* args;
    (void)argc;
    closure = nn_state_compilesource(state, state->topmodule, true, source);
    callme = nn_value_fromobject(closure);
    args = nn_array_make(state);
    argc = nn_nestcall_prepare(state, callme, nn_value_makenull(), args);
    ok = nn_nestcall_callfunction(state, callme, nn_value_makenull(), args, &retval);
    if(!ok)
    {
        nn_exceptions_throw(state, "eval() failed");
    }
    return retval;
}

