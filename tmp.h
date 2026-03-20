
        #define NEON_ARGS_CHECKTYPE(chp, i, typefunc) \
            /* if(NEON_UNLIKELY(!(typefunc)(chp.m_scriptfnctx.argv[i]))) */ \
            if(NEON_UNLIKELY(!((chp.m_scriptfnctx.argv[i]))->*typefunc)) \
            { \
                return NEON_ARGS_FAIL(chp, "%s() expects argument %d as %s, %s given", chp.m_argcheckfuncname, (i) + 1, Value::typeFromFunction(typefunc), Value::typeName(chp.m_scriptfnctx.argv[i], false), false); \
            }

        NEON_ARGS_CHECKTYPE(check, 0, &Value::isNumber);

if(NEON_UNLIKELY(!((check.m_scriptfnctx.argv[0]))->*&Value::isNumber))
{
    return NEON_ARGS_FAIL(check, "%s() expects argument %d as %s, %s given", check.m_argcheckfuncname, (0) + 1, Value::typeFromFunction(&Value::isNumber), Value::typeName(check.m_scriptfnctx.argv[0], false), false);
};
