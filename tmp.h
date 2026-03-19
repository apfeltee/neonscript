                    #define nn_gcmem_freearray(typsz, pointer, oldcount) SharedState::gcRelease(pointer, (typsz) * (oldcount))

                nn_gcmem_freearray(sizeof(Upvalue*), closure->m_fnvals.fnclosure.m_upvalues, closure->upvalcount);
