
#include "neon.h"

/*
* TODO: when executable is run outside of current dev environment, it should correctly
*       find the 'mods' folder. trickier than it sounds:
*   src/<cfiles>
*   src/vsbuild/run.exe
*   src/eg/<scriptfiles>
*   src/mods/<scriptmodules>
*
* there's definitely some easy workaround by using Process.scriptdirectory() et al, but it's less than ideal.
*/

void nn_import_loadbuiltinmodules(NNState* state)
{
    int i;
    static NNModInitFN g_builtinmodules[] =
    {
        nn_natmodule_load_null,
        nn_natmodule_load_os,
        nn_natmodule_load_astscan,
        nn_natmodule_load_complex,
        NULL,
    };
    for(i = 0; g_builtinmodules[i] != NULL; i++)
    {
        nn_import_loadnativemodule(state, g_builtinmodules[i], NULL, "<__native__>", NULL);
    }
}


bool nn_state_addmodulesearchpathobj(NNState* state, NNObjString* os)
{
    nn_valarray_push(&state->importpath, nn_value_fromobject(os));
    return true;
}

bool nn_state_addmodulesearchpath(NNState* state, const char* path)
{
    return nn_state_addmodulesearchpathobj(state, nn_string_copycstr(state, path));
}

void nn_state_setupmodulepaths(NNState* state)
{
    int i;
    static const char* defaultsearchpaths[] =
    {
        "mods",
        "mods/@/index" NEON_CONFIG_FILEEXT,
        ".",
        NULL
    };
    nn_valarray_init(state, &state->importpath);
    nn_state_addmodulesearchpathobj(state, state->processinfo->cliexedirectory);
    for(i=0; defaultsearchpaths[i]!=NULL; i++)
    {
        nn_state_addmodulesearchpath(state, defaultsearchpaths[i]);
    }
}

void nn_module_setfilefield(NNState* state, NNObjModule* module)
{
    return;
    nn_valtable_set(&module->deftable, nn_value_fromobject(nn_string_copycstr(state, "__file__")), nn_value_fromobject(nn_string_copyobject(state, module->physicalpath)));
}

void nn_module_destroy(NNState* state, NNObjModule* module)
{
    NNModLoaderFN asfn;
    nn_valtable_destroy(&module->deftable);
    /*
    nn_memory_free(module->name);
    nn_memory_free(module->physicalpath);
    */
    if(module->fnunloaderptr != NULL && module->imported)
    {
        asfn = *(NNModLoaderFN*)module->fnunloaderptr;
        asfn(state);
    }
    if(module->handle != NULL)
    {
        nn_import_closemodule(module->handle);
    }
}

NNObjModule* nn_import_loadmodulescript(NNState* state, NNObjModule* intomodule, NNObjString* modulename)
{
    int argc;
    size_t fsz;
    char* source;
    char* physpath;
    NNBlob blob;
    NNValue retv;
    NNValue callable;
    NNProperty* field;
    NNObjString* os;
    NNObjModule* module;
    NNObjFunction* closure;
    NNObjFunction* function;
    (void)os;
    (void)argc;
    (void)intomodule;
    field = nn_valtable_getfieldbyostr(&state->openedmodules, modulename);
    if(field != NULL)
    {
        return nn_value_asmodule(field->value);
    }
    physpath = nn_import_resolvepath(state, nn_string_getdata(modulename), nn_string_getdata(intomodule->physicalpath), NULL, false);
    if(physpath == NULL)
    {
        nn_except_throwclass(state, state->exceptions.importerror, "module not found: '%s'", nn_string_getdata(modulename));
        return NULL;
    }
    fprintf(stderr, "loading module from '%s'\n", physpath);
    source = nn_util_filereadfile(state, physpath, &fsz, false, 0);
    if(source == NULL)
    {
        nn_except_throwclass(state, state->exceptions.importerror, "could not read import file %s", physpath);
        return NULL;
    }
    nn_blob_init(state, &blob);
    module = nn_module_make(state, nn_string_getdata(modulename), physpath, true, true);
    nn_memory_free(physpath);
    function = nn_astparser_compilesource(state, module, source, &blob, true, false);
    nn_memory_free(source);
    closure = nn_object_makefuncclosure(state, function, nn_value_makenull());
    callable = nn_value_fromobject(closure);
    nn_nestcall_prepare(state, callable, nn_value_makenull(), NULL, 0);     
    if(!nn_nestcall_callfunction(state, callable, nn_value_makenull(), NULL, 0, &retv, false))
    {
        nn_blob_destroy(&blob);
        nn_except_throwclass(state, state->exceptions.importerror, "failed to call compiled import closure");
        return NULL;
    }
    nn_blob_destroy(&blob);
    return module;
}

char* nn_import_resolvepath(NNState* state, const   char* modulename, const char* currentfile, char* rootfile, bool isrelative)
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
    NNObjString* pitem;
    NNStringBuffer* pathbuf;
    (void)rootfile;
    (void)isrelative;
    (void)stroot;
    (void)stmod;
    mlen = strlen(modulename);
    splen = nn_valarray_count(&state->importpath);
    pathbuf = nn_strbuf_makebasicempty(NULL, 0);
    for(i=0; i<splen; i++)
    {
        pitem = nn_value_asstring(nn_valarray_get(&state->importpath, i));
        nn_strbuf_reset(pathbuf);
        nn_strbuf_appendstrn(pathbuf, nn_string_getdata(pitem), nn_string_getlength(pitem));
        if(nn_strbuf_containschar(pathbuf, '@'))
        {
            nn_strbuf_charreplace(pathbuf, '@', modulename, mlen);
        }
        else
        {
            nn_strbuf_appendstr(pathbuf, "/");
            nn_strbuf_appendstr(pathbuf, modulename);
            nn_strbuf_appendstr(pathbuf, NEON_CONFIG_FILEEXT);
        }
        cstrpath = nn_strbuf_data(pathbuf); 
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
            #if 1   
                path1 = osfn_realpath(cstrpath, NULL);
                path2 = osfn_realpath(currentfile, NULL);
            #else
                path1 = strdup(cstrpath);
                path2 = strdup(currentfile);
            #endif
            if(path1 != NULL && path2 != NULL)
            {
                if(memcmp(path1, path2, (int)strlen(path2)) == 0)
                {
                    nn_memory_free(path1);
                    nn_memory_free(path2);
                    path1 = NULL;
                    path2 = NULL;
                    fprintf(stderr, "resolvepath: refusing to import itself\n");
                    return NULL;
                }
                if(path2 != NULL)
                {
                    nn_memory_free(path2);
                }
                nn_strbuf_destroy(pathbuf);
                pathbuf = NULL;
                retme = nn_util_strdup(path1);
                if(path1 != NULL)
                {
                    nn_memory_free(path1);
                }
                return retme;
            }
        }
        else
        {
            fprintf(stderr, "does not exist\n");
        }
    }
    nn_strbuf_destroy(pathbuf);
    return NULL;
}


bool nn_import_loadnativemodule(NNState* state, NNModInitFN init_fn, char* importname, const char* source, void* dlw)
{
    size_t j;
    size_t k;
    size_t slen;
    NNValue v;
    NNValue fieldname;
    NNValue funcname;
    NNValue funcrealvalue;
    NNDefFunc func;
    NNDefField field;
    NNDefModule* defmod;
    NNObjModule* targetmod;
    NNDefClass klassreg;
    NNObjString* classname;
    NNObjFunction* native;
    NNObjClass* klass;
    defmod = init_fn(state);
    if(defmod != NULL)
    {
        targetmod = (NNObjModule*)nn_gcmem_protect(state, (NNObject*)nn_module_make(state, (char*)defmod->name, source, false, true));
        targetmod->fnpreloaderptr = (void*)defmod->fnpreloaderfunc;
        targetmod->fnunloaderptr = (void*)defmod->fnunloaderfunc;
        if(defmod->definedfields != NULL)
        {
            for(j = 0; defmod->definedfields[j].name != NULL; j++)
            {
                field = defmod->definedfields[j];
                fieldname = nn_value_fromobject(nn_gcmem_protect(state, (NNObject*)nn_string_copycstr(state, field.name)));
                v = field.fieldvalfn(state, nn_value_makenull(), NULL, 0);
                nn_vm_stackpush(state, v);
                nn_valtable_set(&targetmod->deftable, fieldname, v);
                nn_vm_stackpop(state);
            }
        }
        if(defmod->definedfunctions != NULL)
        {
            for(j = 0; defmod->definedfunctions[j].name != NULL; j++)
            {
                func = defmod->definedfunctions[j];
                funcname = nn_value_fromobject(nn_gcmem_protect(state, (NNObject*)nn_string_copycstr(state, func.name)));
                funcrealvalue = nn_value_fromobject(nn_gcmem_protect(state, (NNObject*)nn_object_makefuncnative(state, func.function, func.name, NULL)));
                nn_vm_stackpush(state, funcrealvalue);
                nn_valtable_set(&targetmod->deftable, funcname, funcrealvalue);
                nn_vm_stackpop(state);
            }
        }
        if(defmod->definedclasses != NULL)
        {
            for(j = 0; ((defmod->definedclasses[j].name != NULL) && (defmod->definedclasses[j].defpubfunctions != NULL)); j++)
            {
                klassreg = defmod->definedclasses[j];
                classname = (NNObjString*)nn_gcmem_protect(state, (NNObject*)nn_string_copycstr(state, klassreg.name));
                klass = (NNObjClass*)nn_gcmem_protect(state, (NNObject*)nn_object_makeclass(state, classname, state->classprimobject));
                if(klassreg.defpubfunctions != NULL)
                {
                    for(k = 0; klassreg.defpubfunctions[k].name != NULL; k++)
                    {
                        func = klassreg.defpubfunctions[k];
                        slen = strlen(func.name);
                        funcname = nn_value_fromobject(nn_gcmem_protect(state, (NNObject*)nn_string_copycstr(state, func.name)));
                        native = (NNObjFunction*)nn_gcmem_protect(state, (NNObject*)nn_object_makefuncnative(state, func.function, func.name, NULL));
                        if(func.isstatic)
                        {
                            native->contexttype = NEON_FNCONTEXTTYPE_STATIC;
                        }
                        else if(slen > 0 && func.name[0] == '_')
                        {
                            native->contexttype = NEON_FNCONTEXTTYPE_PRIVATE;
                        }
                        if(strncmp(func.name, "constructor", slen) == 0)
                        {
                            klass->constructor = nn_value_fromobject(native);
                        }
                        else
                        {
                            nn_valtable_set(&klass->instmethods, funcname, nn_value_fromobject(native));
                        }
                    }
                }
                if(klassreg.defpubfields != NULL)
                {
                    k = 0;
                    while(true)
                    {
                        if(klassreg.defpubfields[k].name == NULL)
                        {
                            break;
                        }
                        field = klassreg.defpubfields[k];
                        if(field.name != NULL)
                        {
                            nn_class_defcallablefield(klass, nn_string_copycstr(state, field.name), field.fieldvalfn);
                        }
                        k++;
                    }
                }
                nn_valtable_set(&targetmod->deftable, nn_value_fromobject(classname), nn_value_fromobject(klass));
            }
        }
        if(dlw != NULL)
        {
            targetmod->handle = dlw;
        }
        nn_import_addnativemodule(state, targetmod, nn_string_getdata(targetmod->name));
        nn_gcmem_clearprotect(state);
        return true;
    }
    else
    {
        nn_state_warn(state, "Error loading module: %s\n", importname);
    }
    return false;
}

void nn_import_addnativemodule(NNState* state, NNObjModule* module, const char* as)
{
    NNValue name;
    if(as != NULL)
    {
        module->name = nn_string_copycstr(state, as);
    }
    name = nn_value_fromobject(nn_string_copyobject(state, module->name));
    nn_vm_stackpush(state, name);
    nn_vm_stackpush(state, nn_value_fromobject(module));
    nn_valtable_set(&state->openedmodules, name, nn_value_fromobject(module));
    nn_vm_stackpopn(state, 2);
}


void nn_import_closemodule(void* hnd)
{
    (void)hnd;
}

