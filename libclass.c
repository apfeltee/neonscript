
#include "neon.h"


NNObjClass* nn_object_makeclass(NNState* state, NNObjString* name, NNObjClass* parent)
{
    NNObjClass* klass;
    klass = (NNObjClass*)nn_object_allocobject(state, sizeof(NNObjClass), NEON_OBJTYPE_CLASS, false);
    klass->name = name;
    nn_valtable_init(state, &klass->instproperties);
    nn_valtable_init(state, &klass->staticproperties);
    nn_valtable_init(state, &klass->instmethods);
    nn_valtable_init(state, &klass->staticmethods);
    klass->constructor = nn_value_makenull();
    klass->destructor = nn_value_makenull();
    klass->superclass = parent;
    return klass;
}

void nn_class_destroy(NNObjClass* klass)
{
    NNState* state;
    state = ((NNObject*)klass)->pstate;
    nn_valtable_destroy(&klass->instmethods);
    nn_valtable_destroy(&klass->staticmethods);
    nn_valtable_destroy(&klass->instproperties);
    nn_valtable_destroy(&klass->staticproperties);
    /*
    // We are not freeing the initializer because it's a closure and will still be freed accordingly later.
    */
    memset(klass, 0, sizeof(NNObjClass));
    nn_gcmem_release(state, klass, sizeof(NNObjClass));   
}

bool nn_class_inheritfrom(NNObjClass* subclass, NNObjClass* superclass)
{
    int failcnt;
    failcnt = 0;
    if(!nn_valtable_addall(&superclass->instproperties, &subclass->instproperties, true))
    {
        failcnt++;
    }
    if(!nn_valtable_addall(&superclass->instmethods, &subclass->instmethods, true))
    {
        failcnt++;
    }
    subclass->superclass = superclass;
    if(failcnt == 0)
    {
        return true;
    }
    return false;
}

bool nn_class_defproperty(NNObjClass* klass, NNObjString* cstrname, NNValue val)
{
    return nn_valtable_set(&klass->instproperties, nn_value_fromobject(cstrname), val);
}

bool nn_class_defcallablefieldptr(NNObjClass* klass, NNObjString* name, NNNativeFN function, void* uptr)
{
    NNState* state;
    NNObjFunction* ofn;
    state = ((NNObject*)klass)->pstate;
    ofn = nn_object_makefuncnative(state, function, name->sbuf.data, uptr);
    return nn_valtable_setwithtype(&klass->instproperties, nn_value_fromobject(name), nn_value_fromobject(ofn), NEON_PROPTYPE_FUNCTION, true);
}

bool nn_class_defcallablefield(NNObjClass* klass, NNObjString* name, NNNativeFN function)
{
    return nn_class_defcallablefieldptr(klass, name, function, NULL);
}

bool nn_class_defstaticcallablefieldptr(NNObjClass* klass, NNObjString* name, NNNativeFN function, void* uptr)
{
    NNState* state;
    NNObjFunction* ofn;
    state = ((NNObject*)klass)->pstate;
    ofn = nn_object_makefuncnative(state, function, name->sbuf.data, uptr);
    return nn_valtable_setwithtype(&klass->staticproperties, nn_value_fromobject(name), nn_value_fromobject(ofn), NEON_PROPTYPE_FUNCTION, true);
}

bool nn_class_defstaticcallablefield(NNObjClass* klass, NNObjString* name, NNNativeFN function)
{
    return nn_class_defstaticcallablefieldptr(klass, name, function, NULL);
}

bool nn_class_setstaticproperty(NNObjClass* klass, NNObjString* name, NNValue val)
{
    return nn_valtable_set(&klass->staticproperties, nn_value_fromobject(name), val);
}

bool nn_class_defnativeconstructorptr(NNObjClass* klass, NNNativeFN function, void* uptr)
{
    const char* cname;
    NNState* state;
    NNObjFunction* ofn;
    state = ((NNObject*)klass)->pstate;
    cname = "constructor";
    ofn = nn_object_makefuncnative(state, function, cname, uptr);
    klass->constructor = nn_value_fromobject(ofn);
    return true;
}

bool nn_class_defnativeconstructor(NNObjClass* klass, NNNativeFN function)
{
    return nn_class_defnativeconstructorptr(klass, function, NULL);
}

bool nn_class_defmethod(NNObjClass* klass, NNObjString* name, NNValue val)
{
    return nn_valtable_set(&klass->instmethods, nn_value_fromobject(name), val);
}

bool nn_class_defnativemethodptr(NNObjClass* klass, NNObjString* name, NNNativeFN function, void* ptr)
{
    NNObjFunction* ofn;
    NNState* state;
    state = ((NNObject*)klass)->pstate;
    ofn = nn_object_makefuncnative(state, function, name->sbuf.data, ptr);
    return nn_class_defmethod(klass, name, nn_value_fromobject(ofn));
}

bool nn_class_defnativemethod(NNObjClass* klass, NNObjString* name, NNNativeFN function)
{
    return nn_class_defnativemethodptr(klass, name, function, NULL);
}

bool nn_class_defstaticnativemethodptr(NNObjClass* klass, NNObjString* name, NNNativeFN function, void* uptr)
{
    NNState* state;
    NNObjFunction* ofn;
    state = ((NNObject*)klass)->pstate;
    ofn = nn_object_makefuncnative(state, function, name->sbuf.data, uptr);
    return nn_valtable_set(&klass->staticmethods, nn_value_fromobject(name), nn_value_fromobject(ofn));
}

bool nn_class_defstaticnativemethod(NNObjClass* klass, NNObjString* name, NNNativeFN function)
{
    return nn_class_defstaticnativemethodptr(klass, name, function, NULL);
}

NNProperty* nn_class_getmethodfield(NNObjClass* klass, NNObjString* name)
{
    NNProperty* field;
    field = nn_valtable_getfield(&klass->instmethods, nn_value_fromobject(name));
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

NNProperty* nn_class_getpropertyfield(NNObjClass* klass, NNObjString* name)
{
    NNProperty* field;
    field = nn_valtable_getfield(&klass->instproperties, nn_value_fromobject(name));
    return field;
}

NNProperty* nn_class_getstaticproperty(NNObjClass* klass, NNObjString* name)
{
    NNProperty* np;
    np = nn_valtable_getfieldbyostr(&klass->staticproperties, name);
    if(np != NULL)
    {
        return np;
    }
    if(klass->superclass != NULL)
    {
        return nn_class_getstaticproperty(klass->superclass, name);
    }
    return NULL;
}

NNProperty* nn_class_getstaticmethodfield(NNObjClass* klass, NNObjString* name)
{
    NNProperty* field;
    field = nn_valtable_getfield(&klass->staticmethods, nn_value_fromobject(name));
    return field;

}

NNObjInstance* nn_object_makeinstancesize(NNState* state, NNObjClass* klass, size_t sz)
{
    NNObjInstance* oinst;
    NNObjInstance* instance;
    oinst = NULL;
    instance = (NNObjInstance*)nn_object_allocobject(state, sz, NEON_OBJTYPE_INSTANCE, false);
    /* gc fix */
    nn_vm_stackpush(state, nn_value_fromobject(instance));
    instance->active = true;
    instance->klass = klass;
    instance->superinstance = NULL;
    nn_valtable_init(state, &instance->properties);
    if(nn_valtable_count(&klass->instproperties) > 0)
    {
        nn_valtable_copy(&klass->instproperties, &instance->properties);
    }
    if(klass->superclass != NULL)
    {
        oinst = nn_object_makeinstance(state, klass->superclass);
        instance->superinstance = oinst;
    }
    /* gc fix */
    nn_vm_stackpop(state);
    return instance;
}

NNObjInstance* nn_object_makeinstance(NNState* state, NNObjClass* klass)
{
    return nn_object_makeinstancesize(state, klass, sizeof(NNObjInstance));
}

void nn_instance_mark(NNObjInstance* instance)
{
    NNState* state;
    state = ((NNObject*)instance)->pstate;
    if(instance->active == false)
    {
        nn_state_warn(state, "trying to mark inactive instance <%p>!", instance);
        return;
    }
    nn_valtable_mark(state, &instance->properties);
    nn_gcmem_markobject(state, (NNObject*)instance->klass);
}

void nn_instance_destroy(NNObjInstance* instance)
{
    NNState* state;
    state = ((NNObject*)instance)->pstate;
    if(!nn_value_isnull(instance->klass->destructor))
    {
        if(!nn_vm_callvaluewithobject(state, instance->klass->destructor, nn_value_fromobject(instance), 0, false))
        {
            
        }
    }
    nn_valtable_destroy(&instance->properties);
    instance->active = false;
    nn_gcmem_release(state, instance, sizeof(NNObjInstance));
}

bool nn_instance_defproperty(NNObjInstance* instance, NNObjString* name, NNValue val)
{
    return nn_valtable_set(&instance->properties, nn_value_fromobject(name), val);
}

NNProperty* nn_instance_getvar(NNObjInstance* inst, NNObjString* name)
{
    NNProperty* field;
    field = nn_valtable_getfield(&inst->properties, nn_value_fromobject(name));
    if(field == NULL)
    {
        if(inst->superinstance != NULL)
        {
            return nn_instance_getvar(inst->superinstance, name);
        }
    }
    return field;
}


NNProperty* nn_instance_getvarcstr(NNObjInstance* inst, const char* name)
{
    NNObjString* os;
    os = nn_string_intern(((NNObject*)inst)->pstate, name);
    return nn_instance_getvar(inst, os);
}

NNProperty* nn_instance_getmethod(NNObjInstance* inst, NNObjString* name)
{
    NNProperty* field;
    field = nn_class_getmethodfield(inst->klass, name);
    if(field == NULL)
    {
        if(inst->superinstance != NULL)
        {
            return nn_instance_getmethod(inst->superinstance, name);
        }
    }
    return field;
}

NNProperty* nn_instance_getmethodcstr(NNObjInstance* inst, const char* name)
{
    NNObjString* os;
    os = nn_string_intern(((NNObject*)inst)->pstate, name);
    return nn_instance_getmethod(inst, os);    
}

