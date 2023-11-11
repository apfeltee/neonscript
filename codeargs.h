
int neon_astparser_realgetcodeargscount(const int32_t* code, const NeonValArray* constants, int ip)
{
    int op;
    (void)constants;
    op = code[ip];
    if((int)op == (int)-1)
    {
        return 0;
    }
    switch(op)
    {
        case NEON_OP_HALTVM:
        case NEON_OP_RESTOREFRAME:
        case NEON_OP_PUSHTRUE:
        case NEON_OP_PUSHFALSE:
        case NEON_OP_PUSHNIL:
        case NEON_OP_EQUAL:
        case NEON_OP_PRIMGREATER:
        case NEON_OP_PRIMLESS:
        case NEON_OP_PRIMADD:
        case NEON_OP_PRIMSUBTRACT:
        case NEON_OP_PRIMMULTIPLY:
        case NEON_OP_PRIMDIVIDE:
        case NEON_OP_PRIMBINAND:
        case NEON_OP_PRIMBINOR:
        case NEON_OP_PRIMBINXOR:
        case NEON_OP_PRIMBINNOT:
        case NEON_OP_PRIMMODULO:
        case NEON_OP_PRIMSHIFTLEFT:
        case NEON_OP_PRIMSHIFTRIGHT:
        case NEON_OP_PRIMNOT:
        case NEON_OP_PRIMNEGATE:
        case NEON_OP_DEBUGPRINT:
        case NEON_OP_GLOBALSTMT:
        case NEON_OP_DUP:
        case NEON_OP_PUSHONE:
        case NEON_OP_UPVALCLOSE:
        case NEON_OP_RETURN:
        case NEON_OP_INHERIT:
        case NEON_OP_INDEXSET:
        case NEON_OP_INDEXGET:

        case NEON_OP_POPONE:

        case NEON_OP_PSEUDOBREAK:
        case NEON_OP_TYPEOF:

            return 0;

        case NEON_OP_UPVALGET:
        case NEON_OP_UPVALSET:

        case NEON_OP_LOCALGET:
        case NEON_OP_LOCALSET:

        case NEON_OP_PUSHCONST:
        case NEON_OP_PROPERTYGET:
        case NEON_OP_PROPERTYSET:

        case NEON_OP_CALLCALLABLE:
        case NEON_OP_INSTGETSUPER:
        case NEON_OP_GLOBALGET:
        case NEON_OP_GLOBALDEFINE:
        case NEON_OP_GLOBALSET:
        case NEON_OP_CLASS:
        case NEON_OP_METHOD:
        case NEON_OP_POPN:

            return 1;

        case NEON_OP_JUMPNOW:
        case NEON_OP_JUMPIFFALSE:
        case NEON_OP_LOOP:

        case NEON_OP_MAKEARRAY:
        case NEON_OP_MAKEMAP:

        case NEON_OP_INSTTHISPROPERTYGET:
        case NEON_OP_INSTTHISINVOKE:
        case NEON_OP_INSTSUPERINVOKE:

            return 2;

        case NEON_OP_CLOSURE:
            {
                /*
                int index = code[ip + 1];
                NeonObjScriptFunction* function = neon_value_asscriptfunction(constants->values[index]);
                // Function: 1 byte; Upvalues: 2 bytes each
                return 1 + function->upvaluecount * 2;
                */
                //return 2;
            }

    }

    fprintf(stderr, "internal error: failed to compute operand argument size of %d (%s) (prev=%d (%s))\n", op, neon_dbg_op2str(op), code[ip-1], neon_dbg_op2str(code[ip-1]));
    return 0;
}
