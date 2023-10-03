
void lox_valarray_init(LoxState* state, ValArray* array)
{
    array->pvm = state;
    array->values = NULL;
    array->capacity = 0;
    array->size = 0;
}

size_t lox_valarray_count(ValArray* array)
{
    return array->size;
}

size_t lox_valarray_size(ValArray* array)
{
    return array->size;
}

Value lox_valarray_at(ValArray* array, size_t i)
{
    return array->values[i];
}

bool lox_valarray_grow(ValArray* arr, size_t count)
{
    size_t nsz;
    void* p1;
    void* newbuf;
    nsz = count * sizeof(Value);
    p1 = arr->values;
    newbuf = realloc(p1, nsz);
    if(newbuf == NULL)
    {
        return false;
    }
    arr->values = newbuf;
    arr->capacity = count;
    return true;
}

bool lox_valarray_push(ValArray* arr, Value value)
{
    size_t cap;
    cap = arr->capacity;
    if(cap <= arr->size)
    {
        if(!lox_valarray_grow(arr, lox_valarray_computenextgrow(cap)))
        {
            return false;
        }
    }
    arr->values[arr->size] = (value);
    arr->size++;
    return true;
}

bool lox_valarray_insert(ValArray* arr, size_t pos, Value val)
{
    return lox_genericarray_insert(arr->genarr, pos, val);
}

bool lox_valarray_erase(ValArray* arr, size_t idx)
{
    return lox_genericarray_erase(arr->genarr);
}

Value lox_valarray_pop(ValArray* arr)
{
    return lox_genericarray_pop(arr->genarr);
}

void lox_valarray_free(ValArray* arr)
{
    lox_genericarray_free(arr->genarr);
}