

        class SyntaxNode
        {
            public:
                State* m_state;
                size_t line = 0;
        };

        class Expression: public SyntaxNode
        {
            public:
                enum class Type
                {
                    Unspecified,
                    Literal,
                    Binary,
                    Unary,
                    Variable,
                    Assign,
                    Call,
                    Set,
                    Get,
                    Lambda,
                    Array,
                    Object,
                    Subscript,
                    This,
                    Super,
                    Range,
                    Interpolation,
                    Reference,
                    Expression,
                    Block,
                    IfClause,
                    WhileLoop,
                    ForLoop,
                    VarDecl,
                    ContinueClause,
                    BreakClause,
                    FunctionDecl,
                    ReturnClause,
                    MethodDecl,
                    ClassDecl,
                    FieldDecl
                };

                using List = Util::GenericArray<Expression*>;

            public:


                static List makeList(State* state)
                {
                    (void)state;
                    return List();
                }

                template<typename ExprT>
                static void releaseParameters(State* state, Util::GenericArray<ExprT>* parameters)
                {
                    for(size_t i = 0; i < parameters->m_count; i++)
                    {
                        Expression::releaseExpression(state, parameters->m_values[i].default_value);
                    }
                }

                static void releaseExpressionList(State* state, Expression::List* expressions)
                {
                    if(expressions == nullptr)
                    {
                        return;
                    }
                    for(size_t i = 0; i < expressions->m_count; i++)
                    {
                        Expression::releaseExpression(state, expressions->m_values[i]);
                    }
                }

                static void releaseStatementList(State* state, Expression::List* statements)
                {
                    size_t i;
                    for(i = 0; i < statements->m_count; i++)
                    {
                        Expression::releaseStatement(state, statements->m_values[i]);
                    }
                }

                static void internalReleaseStatementList(State* state, Expression::List* statements)
                {
                    if(statements == nullptr)
                    {
                        return;
                    }
                    for(size_t i = 0; i < statements->m_count; i++)
                    {
                        Expression::releaseStatement(state, statements->m_values[i]);
                    }
                }

                static void releaseAllocatedExpressionList(State* state, Expression::List* expressions)
                {
                    size_t i;
                    if(expressions == nullptr)
                    {
                        return;
                    }
                    for(i = 0; i < expressions->m_count; i++)
                    {
                        Expression::releaseExpression(state, expressions->m_values[i]);
                    }
                    Memory::destroy(expressions);
                }

                static void releaseAllocatedStatementList(State* state, Expression::List* statements)
                {
                    size_t i;
                    if(statements == nullptr)
                    {
                        return;
                    }
                    for(i = 0; i < statements->m_count; i++)
                    {
                        Expression::releaseStatement(state, statements->m_values[i]);
                    }
                    Memory::destroy(statements);
                }

                static void releaseExpression(State* state, Expression* expression);
                static void releaseStatement(State* state, Expression* statement);

            public:
                Type type = Type::Unspecified;

            public:
                Expression(State* state, uint64_t l, Type t)
                {
                    this->m_state = state;
                    this->type = t;
                    this->line = l;

                }
        };

        struct Variable
        {
            const char* name;
            size_t length;
            int depth;
            bool constant;
            bool used;
            Value constant_value;
            Expression** declaration;
        };

        struct SyntaxRule
        {
            public:
                enum Precedence
                {
                    PREC_NONE,

                    /* =, &=, |=, *=, +=, -=, /=, **=, %=, >>=, <<=, ^=, //= */
                    PREC_ASSIGNMENT,
                    /* ~= ?: */
                    PREC_CONDITIONAL,
                    /* 'or' || */
                    PREC_OR,
                    /* 'and' && */
                    PREC_AND,
                    /* ==, != */
                    PREC_EQUALITY,
                    /* <, >, <=, >= */
                    PREC_COMPARISON,
                    /* | */
                    PREC_BITOR,
                    /* ^ */
                    PREC_BITXOR,
                    /* & */
                    PREC_BITAND,
                    /* <<, >> */
                    PREC_SHIFT,
                    /* .. */
                    PREC_RANGE,
                    /* +, - */
                    PREC_TERM,
                    /* *, /, %, **, // */
                    PREC_FACTOR,
                    /* !, -, ~, (++, -- this two will now be treated as statements) */
                    PREC_UNARY,
                    /* ., () */
                    PREC_CALL,
                    PREC_PRIMARY
                };

                using PrefixFN = bool (*)(Parser*, bool);
                using InfixFN = bool (*)(Parser*, Token, bool);

            public:
                static SyntaxRule* make(SyntaxRule* dest, PrefixFN prefix, InfixFN infix, Precedence precedence)
                {
                    dest->fnprefix = prefix;
                    dest->fninfix = infix;
                    dest->precedence = precedence;
                    return dest;
                }
            
            public:
                PrefixFN fnprefix;
                InfixFN fninfix;
                Precedence precedence;
        };

        /*
        class ParseRule
        {
            public:
                using PrefixFuncType = Expression* (*)(Parser*, bool);
                using InfixFuncType = Expression*(*)(Parser*, Expression*, bool);

            public:
                PrefixFuncType prefix;
                InfixFuncType infix;
                Precedence precedence;
        };
        */

        class ExprLiteral: public Expression
        {
            public:
                Value value;

            public:
                ExprLiteral(State* state, size_t ln, Value val): Expression(state, ln, Expression::Type::Literal)
                {
                    this->value = val;
                }

        };

        class ExprBinary: public Expression
        {
            public:
                Expression* left;
                Expression* right;
                Token::Type op;
                bool ignore_left;

            public:
                ExprBinary(State* state, size_t ln, Expression* lf, Expression* rh, Token::Type vop):Expression(state, ln, Expression::Type::Binary)
                {
                    this->left = lf;
                    this->right = rh;
                    this->op = vop;
                    this->ignore_left = false;
                }

        };

        class ExprUnary: public Expression
        {
            public:
                ExprUnary(State* state, size_t ln, Expression* rh, Token::Type vop): Expression(state, ln, Expression::Type::Unary)
                {
                    this->right = rh;
                    this->op = vop;
                }

            public:
                Expression* right;
                Token::Type op;
        };

        class ExprVar: public Expression
        {
            public:
                ExprVar(State* state, size_t ln, const char* n, size_t len):Expression(state, ln, Expression::Type::Variable)
                {
                    this->name = n;
                    this->length = len;
                }

            public:
                const char* name;
                size_t length;
        };

        class ExprAssign: public Expression
        {
            public:
                ExprAssign(State* state, size_t ln, Expression* vto, Expression* val):Expression(state, ln, Expression::Type::Assign)
                {
                    this->to = vto;
                    this->value = val;
                }

            public:
                Expression* to;
                Expression* value;
        };

        class ExprCall: public Expression
        {
            public:
                ExprCall(State* state, size_t ln, Expression* vc):Expression(state, ln, Expression::Type::Call)
                {
                    this->callee = vc;
                    this->objexpr = nullptr;
                }

            public:
                Expression* callee;
                Expression::List args;
                Expression* objexpr;
        };

        class ExprIndexGet: public Expression
        {
            public:
               ExprIndexGet(State* state, size_t ln, Expression* wh, const char* nm, size_t len, bool questionable, bool ignres):Expression(state, ln, Expression::Type::Get)
                {
                    this->where = wh;
                    this->name = nm;
                    this->length = len;
                    this->ignore_emit = false;
                    this->jump = questionable ? 0 : -1;
                    this->ignore_result = ignres;
                }

            public:
                Expression* where;
                const char* name;
                size_t length;
                int jump;
                bool ignore_emit;
                bool ignore_result;
        };

        class ExprIndexSet: public Expression
        {
            public:
                ExprIndexSet(State* state, size_t ln, Expression* wh, const char* n, size_t len, Expression* val):Expression(state, ln, Expression::Type::Set)
                {
                    this->where = wh;
                    this->name = n;
                    this->length = len;
                    this->value = val;
                }

            public:
                Expression* where;
                const char* name;
                size_t length;
                Expression* value;
        };

        struct ExprFuncParam
        {
            const char* name;
            size_t length;
            Expression* default_value;
        };

        class ExprLambda: public Expression
        {
            public:
                ExprLambda(State* state, size_t ln):Expression(state, ln, Expression::Type::Lambda)
                {
                    this->body = nullptr;
                }

            public:
                Util::GenericArray<ExprFuncParam> parameters;
                Expression* body;
        };

        class ExprArray: public Expression
        {
            public:
                ExprArray(State* state, size_t ln):Expression(state, ln, Expression::Type::Array)
                {
                }

            public:
                Expression::List values;
        };

        class ExprObject: public Expression
        {
            public:
                ExprObject(State* state, size_t ln):Expression(state, ln, Expression::Type::Object)
                {
                }

            public:
                Util::GenericArray<Value> keys;
                Expression::List values;
        };

        class ExprSubscript: public Expression
        {
            public:
                ExprSubscript(State* state, size_t ln, Expression* arr, Expression* ind):Expression(state, ln, Expression::Type::Subscript)
                {
                    this->array = arr;
                    this->index = ind;
                }

            public:
                Expression* array;
                Expression* index;
        };

        class ExprThis: public Expression
        {
            public:
                ExprThis(State* state, size_t ln):Expression(state, ln, Expression::Type::This)
                {
                }
        };

        class ExprSuper: public Expression
        {
            public:
                ExprSuper(State* state, size_t ln, String* mth, bool ignres):Expression(state, ln, Expression::Type::Super)
                {
                    this->method = mth;
                    this->ignore_emit = false;
                    this->ignore_result = ignres;
                }

            public:
                String* method;
                bool ignore_emit;
                bool ignore_result;
        };

        class ExprRange: public Expression
        {
            public:
                ExprRange(State* state, size_t ln, Expression* efrom, Expression* eto):Expression(state, ln, Expression::Type::Range)
                {
                    this->from = efrom;
                    this->to = eto;
                }

            public:
                Expression* from;
                Expression* to;
        };

        class ExprIfClause: public Expression
        {
            public:
                ExprIfClause(State* state, size_t ln, Expression* cond, Expression* ifb, Expression* elseb):Expression(state, ln, Expression::Type::IfClause)
                {
                    this->condition = cond;
                    this->if_branch = ifb;
                    this->else_branch = elseb;
                }

            public:
                Expression* condition;
                Expression* if_branch;
                Expression* else_branch;
        };

        class ExprInterpolation: public Expression
        {
            public:
                ExprInterpolation(State* state, size_t ln):Expression(state, ln, Expression::Type::Interpolation)
                {
                }

            public:
                Expression::List expressions;
        };

        class ExprReference: public Expression
        {
            public:
                ExprReference(State* state, size_t ln, Expression* eto):Expression(state, ln, Expression::Type::Interpolation)
                {

                    this->to = eto;
                }

            public:
                Expression* to;
        };

        class ExprStatement: public Expression
        {
            public:
                ExprStatement(State* state, size_t ln, Expression* exp):Expression(state, ln, Expression::Type::Expression)
                {
                    this->expression = exp;
                    this->pop = true;
                }

            public:
                Expression* expression;
                bool pop;
        };

        class StmtBlock: public Expression
        {
            public:
                StmtBlock(State* state, size_t ln):Expression(state, ln, Expression::Type::Block)
                {
                }

            public:
                Expression::List statements;
        };

        class StmtVar: public Expression
        {
            public:
                StmtVar(State* state, size_t ln, const char* n, size_t len, Expression* exprinit, bool vconst):Expression(state, ln, Expression::Type::VarDecl)
                {
                    this->name = n;
                    this->length = len;
                    this->valexpr = exprinit;
                    this->constant = vconst;
                }

            public:
                const char* name;
                size_t length;
                bool constant;
                Expression* valexpr;
        };

        class StmtIfClause: public Expression
        {
            public:
                StmtIfClause(State* state,
                    size_t ln,
                    Expression* cond,
                    Expression* ifb,
                    Expression* elseb,
                    Expression::List* elseifconds,
                    Expression::List* elseifbs
                ):Expression(state, ln, Expression::Type::IfClause)
                {
                    this->condition = cond;
                    this->if_branch = ifb;
                    this->else_branch = elseb;
                    this->elseif_conditions = elseifconds;
                    this->elseif_branches = elseifbs;
                }

            public:
                Expression* condition;
                Expression* if_branch;
                Expression* else_branch;
                Expression::List* elseif_conditions;
                Expression::List* elseif_branches;
        };

        class StmtWhileLoop: public Expression
        {
            public:
                StmtWhileLoop(State* state, size_t ln, Expression* cond, Expression* bod):Expression(state, ln, Expression::Type::WhileLoop)
                {
                    this->condition = cond;
                    this->body = bod;
                }

            public:
                Expression* condition;
                Expression* body;
        };

        class StmtForLoop: public Expression
        {
            public:
                StmtForLoop(State* state,
                  size_t ln,
                  Expression* einit,
                  Expression* vvar,
                  Expression* cond,
                  Expression* incr,
                  Expression* bod,
                  bool cstyle):Expression(state, ln, Expression::Type::ForLoop)
                {
                    this->exprinit = einit;
                    this->var = vvar;
                    this->condition = cond;
                    this->increment = incr;
                    this->body = bod;
                    this->c_style = cstyle;
                }

            public:
                Expression* exprinit;
                Expression* var;
                Expression* condition;
                Expression* increment;
                Expression* body;
                bool c_style;
        };

        class StmtContinue: public Expression
        {
            public:
                StmtContinue(State* state, size_t ln):Expression(state, ln, Expression::Type::ContinueClause)
                {
                }

        };

        class StmtBreak: public Expression
        {
            public:
                StmtBreak(State* state, size_t ln):Expression(state, ln, Expression::Type::BreakClause)
                {
                }
        };

        class StmtFunction: public Expression
        {
            public:
                StmtFunction(State* state, size_t ln, const char* n, size_t len):Expression(state, ln, Expression::Type::FunctionDecl)
                {
                    this->name = n;
                    this->length = len;
                    this->body = nullptr;
                }

            public:
                const char* name;
                size_t length;
                Util::GenericArray<ExprFuncParam> parameters;
                Expression* body;
                bool exported;
        };

        class StmtReturn: public Expression
        {
            public:
                StmtReturn(State* state, size_t ln, Expression* exp):Expression(state, ln, Expression::Type::ReturnClause)
                {
                    this->expression = exp;
                }

            public:
                Expression* expression;
        };

        class StmtMethod: public Expression
        {
            public:
                StmtMethod(State* state, size_t ln, String* n, bool isstatic):Expression(state, ln, Expression::Type::MethodDecl)
                {
                    this->name = n;
                    this->body = nullptr;
                    this->is_static = isstatic;
                }

            public:
                String* name;
                Util::GenericArray<ExprFuncParam> parameters;
                Expression* body;
                bool is_static;
        };

        class StmtClass: public Expression
        {
            public:
                StmtClass(State* state, size_t ln, String* n, String* par):Expression(state, ln, Expression::Type::ClassDecl)
                {
                    this->name = n;
                    this->parent = par;
                }

            public:
                String* name;
                String* parent;
                Expression::List fields;
        };

        class StmtField: public Expression
        {
            public:
                StmtField(State* state, size_t ln, String* nm, Expression* vget, Expression* vset, bool isstatic):Expression(state, ln, Expression::Type::FieldDecl)
                {
                    this->name = nm;
                    this->getter = vget;
                    this->setter = vset;
                    this->is_static = isstatic;
                }

            public:
                String* name;
                Expression* getter;
                Expression* setter;
                bool is_static;
        };

    struct Parser
    {
        public:
            enum CompContext
            {
                COMPCONTEXT_NONE,
                COMPCONTEXT_CLASS,
                COMPCONTEXT_ARRAY,
                COMPCONTEXT_NESTEDFUNCTION,
            };

            struct CompiledUpvalue
            {
                bool islocal;
                uint16_t index;
            };

            struct CompiledLocal
            {
                bool iscaptured;
                int depth;
                Token varname;
            };

            struct FuncCompiler
            {
                public:
                    enum
                    {
                        /* how many locals per function can be compiled */
                        MaxLocals = (64 / 1),
    
                        /* how many upvalues per function can be compiled */
                        MaxUpvalues = (64 / 1),
                    };

                public:
                    Parser* m_prs;
                    int m_localcount;
                    int m_scopedepth;
                    int m_compiledexcepthandlercount;
                    bool m_fromimport;
                    FuncCompiler* m_enclosing;
                    /* current function */
                    FuncScript* m_targetfunc;
                    FuncCommon::Type m_type;
                    //CompiledLocal m_compiledlocals[MaxLocals];
                    CompiledUpvalue m_compiledupvals[MaxUpvalues];
                    Util::GenericArray<CompiledLocal> m_compiledlocals;

                public:
                    FuncCompiler(Parser* prs, FuncCommon::Type t, bool isanon)
                    {
                        size_t i;
                        bool candeclthis;
                        CompiledLocal* local;
                        String* fname;
                        (void)i;
                        m_prs = prs;
                        m_enclosing = m_prs->m_pvm->m_activeparser->m_currfunccompiler;
                        m_targetfunc = nullptr;
                        m_type = t;
                        m_localcount = 0;
                        m_scopedepth = 0;
                        m_compiledexcepthandlercount = 0;
                        m_fromimport = false;
                        m_targetfunc = FuncScript::make(m_prs->m_pvm, m_prs->m_currmodule, t);
                        m_prs->m_currfunccompiler = this;
                        m_compiledlocals.push(CompiledLocal{});
                        if(t != FuncCommon::FUNCTYPE_SCRIPT)
                        {
                            m_prs->m_pvm->m_vmstate->stackPush(Value::fromObject(m_targetfunc));
                            if(isanon)
                            {
                                Printer ptmp(m_prs->m_pvm);
                                ptmp.putformat("anonymous@[%s:%d]", m_prs->m_currentphysfile, m_prs->m_prevtoken.line);
                                fname = ptmp.takeString();
                            }
                            else
                            {
                                fname = String::copy(m_prs->m_pvm, m_prs->m_prevtoken.start, m_prs->m_prevtoken.length);
                            }
                            m_prs->m_currfunccompiler->m_targetfunc->m_scriptfnname = fname;
                            m_prs->m_pvm->m_vmstate->stackPop();
                        }
                        /* claiming slot zero for use in class methods */
                        local = &m_prs->m_currfunccompiler->m_compiledlocals[0];
                        m_prs->m_currfunccompiler->m_localcount++;
                        local->depth = 0;
                        local->iscaptured = false;
                        candeclthis = (
                            (t != FuncCommon::FUNCTYPE_FUNCTION) &&
                            (m_prs->m_compcontext == COMPCONTEXT_CLASS)
                        );
                        if(candeclthis || (/*(t == FuncCommon::FUNCTYPE_ANONYMOUS) &&*/ (m_prs->m_compcontext != COMPCONTEXT_CLASS)))
                        {
                            local->varname.start = g_strthis;
                            local->varname.length = 4;
                        }
                        else
                        {
                            local->varname.start = "";
                            local->varname.length = 0;
                        }
                    }

                    ~FuncCompiler()
                    {
                    }

                    void compileBody(bool closescope, bool isanon)
                    {
                        int i;
                        FuncScript* function;
                        (void)isanon;
                        /* compile the body */
                        m_prs->ignoreSpace();
                        m_prs->consume(Token::TOK_BRACEOPEN, "expected '{' before function body");
                        m_prs->parseBlock();
                        /* create the function object */
                        if(closescope)
                        {
                            m_prs->scopeEnd();
                        }
                        function = m_prs->endCompiler();
                        m_prs->m_pvm->m_vmstate->stackPush(Value::fromObject(function));
                        m_prs->emitInstruction(Instruction::OP_MAKECLOSURE);
                        m_prs->emit1short(m_prs->pushConst(Value::fromObject(function)));
                        for(i = 0; i < function->m_upvalcount; i++)
                        {
                            m_prs->emit1byte(m_compiledupvals[i].islocal ? 1 : 0);
                            m_prs->emit1short(m_compiledupvals[i].index);
                        }
                        m_prs->m_pvm->m_vmstate->stackPop();
                    }

                    int resolveLocal(Token* name)
                    {
                        int i;
                        CompiledLocal* local;
                        for(i = m_localcount - 1; i >= 0; i--)
                        {
                            local = &m_compiledlocals[i];
                            if(Parser::identsEqual(&local->varname, name))
                            {
                                if(local->depth == -1)
                                {
                                    m_prs->raiseError("cannot read local variable in it's own initializer");
                                }
                                return i;
                            }
                        }
                        return -1;
                    }

                    int addUpvalue(uint16_t index, bool islocal)
                    {
                        int i;
                        int upcnt;
                        CompiledUpvalue* upvalue;
                        upcnt = m_targetfunc->m_upvalcount;
                        for(i = 0; i < upcnt; i++)
                        {
                            upvalue = &m_compiledupvals[i];
                            if(upvalue->index == index && upvalue->islocal == islocal)
                            {
                                return i;
                            }
                        }
                        if(upcnt == MaxUpvalues)
                        {
                            m_prs->raiseError("too many closure variables in function");
                            return 0;
                        }
                        m_compiledupvals[upcnt].islocal = islocal;
                        m_compiledupvals[upcnt].index = index;
                        return m_targetfunc->m_upvalcount++;
                    }

                    int resolveUpvalue(Token* name)
                    {
                        int local;
                        int upvalue;
                        if(m_enclosing == nullptr)
                        {
                            return -1;
                        }
                        local = m_enclosing->resolveLocal(name);
                        if(local != -1)
                        {
                            m_enclosing->m_compiledlocals[local].iscaptured = true;
                            return addUpvalue((uint16_t)local, true);
                        }
                        upvalue = m_enclosing->resolveUpvalue(name);
                        if(upvalue != -1)
                        {
                            return addUpvalue((uint16_t)upvalue, false);
                        }
                        return -1;
                    }
            };

            struct ClassCompiler
            {
                bool hassuperclass;
                ClassCompiler* m_enclosing;
                Token classname;
            };

        public:
            static bool identsEqual(Token* a, Token* b)
            {
                return a->length == b->length && memcmp(a->start, b->start, a->length) == 0;
            }

            /*
            * $keeplast: whether to emit code that retains or discards the value of the last statement/expression.
            * SHOULD NOT BE USED FOR ORDINARY SCRIPTS as it will almost definitely result in the stack containing invalid values.
            */
            static FuncScript* compileSource(State* state, Module* module, const char* source, Blob* blob, bool fromimport, bool keeplast)
            {
                Lexer* lexer;
                Parser* parser;
                FuncScript* function;
                (void)blob;
                NEON_ASTDEBUG(state, "module=%p source=[...] blob=[...] fromimport=%d keeplast=%d", module, fromimport, keeplast);
                lexer = Memory::create<Lexer>(state, source);
                parser = Memory::create<Parser>(state, lexer, module, keeplast);
                FuncCompiler compiler(parser, FuncCommon::FUNCTYPE_SCRIPT, true);
                compiler.m_fromimport = fromimport;
                parser->runParser();
                function = parser->endCompiler();
                if(parser->m_haderror)
                {
                    function = nullptr;
                }
                Memory::destroy(lexer);
                Memory::destroy(parser);
                state->m_activeparser = nullptr;
                return function;
            }

            static Token makeSynthToken(const char* name)
            {
                Token token;
                token.isglobal = false;
                token.line = 0;
                token.type = (Token::Type)0;
                token.start = name;
                token.length = (int)strlen(name);
                return token;
            }

            static int getCodeArgsCount(const Instruction* bytecode, const Value* constants, int ip)
            {
                int constant;
                Instruction::OpCode code;
                FuncScript* fn;
                code = (Instruction::OpCode)bytecode[ip].code;
                switch(code)
                {
                    case Instruction::OP_EQUAL:
                    case Instruction::OP_PRIMGREATER:
                    case Instruction::OP_PRIMLESSTHAN:
                    case Instruction::OP_PUSHNULL:
                    case Instruction::OP_PUSHTRUE:
                    case Instruction::OP_PUSHFALSE:
                    case Instruction::OP_PRIMADD:
                    case Instruction::OP_PRIMSUBTRACT:
                    case Instruction::OP_PRIMMULTIPLY:
                    case Instruction::OP_PRIMDIVIDE:
                    case Instruction::OP_PRIMFLOORDIVIDE:
                    case Instruction::OP_PRIMMODULO:
                    case Instruction::OP_PRIMPOW:
                    case Instruction::OP_PRIMNEGATE:
                    case Instruction::OP_PRIMNOT:
                    case Instruction::OP_ECHO:
                    case Instruction::OP_TYPEOF:
                    case Instruction::OP_POPONE:
                    case Instruction::OP_UPVALUECLOSE:
                    case Instruction::OP_DUPONE:
                    case Instruction::OP_RETURN:
                    case Instruction::OP_CLASSINHERIT:
                    case Instruction::OP_CLASSGETSUPER:
                    case Instruction::OP_PRIMAND:
                    case Instruction::OP_PRIMOR:
                    case Instruction::OP_PRIMBITXOR:
                    case Instruction::OP_PRIMSHIFTLEFT:
                    case Instruction::OP_PRIMSHIFTRIGHT:
                    case Instruction::OP_PRIMBITNOT:
                    case Instruction::OP_PUSHONE:
                    case Instruction::OP_INDEXSET:
                    case Instruction::OP_ASSERT:
                    case Instruction::OP_EXTHROW:
                    case Instruction::OP_EXPOPTRY:
                    case Instruction::OP_MAKERANGE:
                    case Instruction::OP_STRINGIFY:
                    case Instruction::OP_PUSHEMPTY:
                    case Instruction::OP_EXPUBLISHTRY:
                        return 0;
                    case Instruction::OP_CALLFUNCTION:
                    case Instruction::OP_CLASSINVOKESUPERSELF:
                    case Instruction::OP_INDEXGET:
                    case Instruction::OP_INDEXGETRANGED:
                        return 1;
                    case Instruction::OP_GLOBALDEFINE:
                    case Instruction::OP_GLOBALGET:
                    case Instruction::OP_GLOBALSET:
                    case Instruction::OP_LOCALGET:
                    case Instruction::OP_LOCALSET:
                    case Instruction::OP_FUNCARGSET:
                    case Instruction::OP_FUNCARGGET:
                    case Instruction::OP_UPVALUEGET:
                    case Instruction::OP_UPVALUESET:
                    case Instruction::OP_JUMPIFFALSE:
                    case Instruction::OP_JUMPNOW:
                    case Instruction::OP_BREAK_PL:
                    case Instruction::OP_LOOP:
                    case Instruction::OP_PUSHCONSTANT:
                    case Instruction::OP_POPN:
                    case Instruction::OP_MAKECLASS:
                    case Instruction::OP_PROPERTYGET:
                    case Instruction::OP_PROPERTYGETSELF:
                    case Instruction::OP_PROPERTYSET:
                    case Instruction::OP_MAKEARRAY:
                    case Instruction::OP_MAKEDICT:
                    case Instruction::OP_IMPORTIMPORT:
                    case Instruction::OP_SWITCH:
                    case Instruction::OP_MAKEMETHOD:
                    //case Instruction::OP_FUNCOPTARG:
                        return 2;
                    case Instruction::OP_CALLMETHOD:
                    case Instruction::OP_CLASSINVOKETHIS:
                    case Instruction::OP_CLASSINVOKESUPER:
                    case Instruction::OP_CLASSPROPERTYDEFINE:
                        return 3;
                    case Instruction::OP_EXTRY:
                        return 6;
                    case Instruction::OP_MAKECLOSURE:
                        {
                            constant = (bytecode[ip + 1].code << 8) | bytecode[ip + 2].code;
                            fn = constants[constant].asFuncScript();
                            /* There is two byte for the constant, then three for each up value. */
                            return 2 + (fn->m_upvalcount * 3);
                        }
                        break;
                    default:
                        break;
                }
                return 0;
            }

        public:
            State* m_pvm;
            bool m_haderror;
            bool m_panicmode;
            bool m_isreturning;
            bool m_istrying;
            bool m_replcanecho;
            bool m_keeplastvalue;
            bool m_lastwasstatement;
            bool m_infunction;
            /* used for tracking loops for the continue statement... */
            int m_innermostloopstart;
            int m_innermostloopscopedepth;
            int m_blockcount;
            /* the context in which the parser resides; none (outer level), inside a class, dict, array, etc */
            CompContext m_compcontext;
            const char* m_currentphysfile;
            Lexer* m_lexer;
            Token m_currtoken;
            Token m_prevtoken;
            ClassCompiler* m_currclasscompiler;
            FuncCompiler* m_currfunccompiler;
            Module* m_currmodule;

        public:
            Parser(State* state, Lexer* lexer, Module* module, bool keeplast): m_pvm(state)
            {
                NEON_ASTDEBUG(state, "");
                state->m_activeparser = this;
                m_currfunccompiler = nullptr;
                m_lexer = lexer;
                m_haderror = false;
                m_panicmode = false;
                m_blockcount = 0;
                m_replcanecho = false;
                m_isreturning = false;
                m_istrying = false;
                m_compcontext = COMPCONTEXT_NONE;
                m_innermostloopstart = -1;
                m_innermostloopscopedepth = 0;
                m_currclasscompiler = nullptr;
                m_currmodule = module;
                m_keeplastvalue = keeplast;
                m_lastwasstatement = false;
                m_infunction = false;
                m_currentphysfile = m_currmodule->m_physlocation->data();
            }

            ~Parser()
            {
            }

            template<typename... ArgsT>
            bool raiseErrorAt(Token* t, const char* message, ArgsT&&... args)
            {
                static auto fn_fprintf = fprintf;
                fflush(stdout);
                /*
                // do not cascade error
                // suppress error if already in panic mode
                */
                if(m_panicmode)
                {
                    return false;
                }
                m_panicmode = true;
                fprintf(stderr, "SyntaxError");
                if(t->type == Token::TOK_EOF)
                {
                    fprintf(stderr, " at end");
                }
                else if(t->type == Token::TOK_ERROR)
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
                fn_fprintf(stderr, message, args...);
                fputs("\n", stderr);
                fprintf(stderr, "  %s:%d\n", m_currmodule->m_physlocation->data(), t->line);
                m_haderror = true;
                return false;
            }

            template<typename... ArgsT>
            bool raiseError(const char* message, ArgsT&&... args)
            {
                raiseErrorAt(&m_prevtoken, message, args...);
                return false;
            }

            template<typename... ArgsT>
            bool raiseErrorAtCurrent(const char* message, ArgsT&&... args)
            {
                raiseErrorAt(&m_currtoken, message, args...);
                return false;
            }

            void synchronize()
            {
                m_panicmode = false;
                while(m_currtoken.type != Token::TOK_EOF)
                {
                    if(m_currtoken.type == Token::TOK_NEWLINE || m_currtoken.type == Token::TOK_SEMICOLON)
                    {
                        return;
                    }
                    switch(m_currtoken.type)
                    {
                        case Token::TOK_KWCLASS:
                        case Token::TOK_KWFUNCTION:
                        case Token::TOK_KWVAR:
                        case Token::TOK_KWFOREACH:
                        case Token::TOK_KWIF:
                        case Token::TOK_KWSWITCH:
                        case Token::TOK_KWCASE:
                        case Token::TOK_KWFOR:
                        case Token::TOK_KWDO:
                        case Token::TOK_KWWHILE:
                        case Token::TOK_KWECHO:
                        case Token::TOK_KWASSERT:
                        case Token::TOK_KWTRY:
                        case Token::TOK_KWCATCH:
                        case Token::TOK_KWTHROW:
                        case Token::TOK_KWRETURN:
                        case Token::TOK_KWSTATIC:
                        case Token::TOK_KWTHIS:
                        case Token::TOK_KWSUPER:
                        case Token::TOK_KWFINALLY:
                        case Token::TOK_KWIN:
                        case Token::TOK_KWIMPORT:
                        case Token::TOK_KWAS:
                            return;
                        default:
                            {
                                /* do nothing */
                            }
                    }
                    advance();
                }
            }

            Blob* currentBlob()
            {
                return m_currfunccompiler->m_targetfunc->m_compiledblob;
            }

            void advance()
            {
                m_prevtoken = m_currtoken;
                while(true)
                {
                    m_currtoken = m_lexer->scanToken();
                    if(m_currtoken.type != Token::TOK_ERROR)
                    {
                        break;
                    }
                    raiseErrorAtCurrent(m_currtoken.start);
                }
            }

            bool consume(Token::Type t, const char* message)
            {
                if(m_currtoken.type == t)
                {
                    advance();
                    return true;
                }
                return raiseErrorAtCurrent(message);
            }

            void consumeOr(const char* message, const Token::Type* ts, int count)
            {
                int i;
                for(i = 0; i < count; i++)
                {
                    if(m_currtoken.type == ts[i])
                    {
                        advance();
                        return;
                    }
                }
                raiseErrorAtCurrent(message);
            }

            void consumeStmtEnd()
            {
                /* allow block last statement to omit statement end */
                if(m_blockcount > 0 && check(Token::TOK_BRACECLOSE))
                {
                    return;
                }
                if(match(Token::TOK_SEMICOLON))
                {
                    while(match(Token::TOK_SEMICOLON) || match(Token::TOK_NEWLINE))
                    {
                    }
                    return;
                }
                if(match(Token::TOK_EOF) || m_prevtoken.type == Token::TOK_EOF)
                {
                    return;
                }
                /* consume(Token::TOK_NEWLINE, "end of statement expected"); */
                while(match(Token::TOK_SEMICOLON) || match(Token::TOK_NEWLINE))
                {
                }
            }

            void emit(uint8_t byte, int line, bool isop)
            {
                Instruction ins;
                ins.code = byte;
                ins.srcline = line;
                ins.isop = isop;
                currentBlob()->push(ins);
            }

            void patchAt(size_t idx, uint8_t byte)
            {
                currentBlob()->m_instrucs[idx].code = byte;
            }

            void emitInstruction(uint8_t byte)
            {
                emit(byte, m_prevtoken.line, true);
            }

            void emit1byte(uint8_t byte)
            {
                emit(byte, m_prevtoken.line, false);
            }

            void emit1short(uint16_t byte)
            {
                emit((byte >> 8) & 0xff, m_prevtoken.line, false);
                emit(byte & 0xff, m_prevtoken.line, false);
            }

            void emit2byte(uint8_t byte, uint8_t byte2)
            {
                emit(byte, m_prevtoken.line, false);
                emit(byte2, m_prevtoken.line, false);
            }

            void emitByteAndShort(uint8_t byte, uint16_t byte2)
            {
                emit(byte, m_prevtoken.line, false);
                emit((byte2 >> 8) & 0xff, m_prevtoken.line, false);
                emit(byte2 & 0xff, m_prevtoken.line, false);
            }

            void emitLoop(int loopstart)
            {
                int offset;
                emitInstruction(Instruction::OP_LOOP);
                offset = currentBlob()->m_count - loopstart + 2;
                if(offset > UINT16_MAX)
                {
                    raiseError("loop body too large");
                }
                emit1byte((offset >> 8) & 0xff);
                emit1byte(offset & 0xff);
            }

            void emitReturn()
            {
                if(m_istrying)
                {
                    emitInstruction(Instruction::OP_EXPOPTRY);
                }
                if(m_currfunccompiler->m_type == FuncCommon::FUNCTYPE_INITIALIZER)
                {
                    emitInstruction(Instruction::OP_LOCALGET);
                    emit1short(0);
                }
                else
                {
                    if(!m_keeplastvalue || m_lastwasstatement)
                    {
                        if(m_currfunccompiler->m_fromimport)
                        {
                            emitInstruction(Instruction::OP_PUSHNULL);
                        }
                        else
                        {
                            emitInstruction(Instruction::OP_PUSHEMPTY);
                        }
                    }
                }
                emitInstruction(Instruction::OP_RETURN);
            }

            int pushConst(Value value)
            {
                int constant;
                constant = currentBlob()->pushConst(value);
                if(constant >= UINT16_MAX)
                {
                    raiseError("too many constants in current scope");
                    return 0;
                }
                return constant;
            }

            void emitConst(Value value)
            {
                int constant;
                constant = pushConst(value);
                emitInstruction(Instruction::OP_PUSHCONSTANT);
                emit1short((uint16_t)constant);
            }

            int emitJump(uint8_t instruction)
            {
                emitInstruction(instruction);
                /* placeholders */
                emit1byte(0xff);
                emit1byte(0xff);
                return currentBlob()->m_count - 2;
            }

            int emitSwitch()
            {
                emitInstruction(Instruction::OP_SWITCH);
                /* placeholders */
                emit1byte(0xff);
                emit1byte(0xff);
                return currentBlob()->m_count - 2;
            }

            int emitTry()
            {
                emitInstruction(Instruction::OP_EXTRY);
                /* type placeholders */
                emit1byte(0xff);
                emit1byte(0xff);
                /* handler placeholders */
                emit1byte(0xff);
                emit1byte(0xff);
                /* finally placeholders */
                emit1byte(0xff);
                emit1byte(0xff);
                return currentBlob()->m_count - 6;
            }

            void emitPatchSwitch(int offset, int constant)
            {
                patchAt(offset, (constant >> 8) & 0xff);
                patchAt(offset + 1, constant & 0xff);
            }

            void emitPatchTry(int offset, int type, int address, int finally)
            {
                /* patch type */
                patchAt(offset, (type >> 8) & 0xff);
                patchAt(offset + 1, type & 0xff);
                /* patch address */
                patchAt(offset + 2, (address >> 8) & 0xff);
                patchAt(offset + 3, address & 0xff);
                /* patch finally */
                patchAt(offset + 4, (finally >> 8) & 0xff);
                patchAt(offset + 5, finally & 0xff);
            }

            void emitPatchJump(int offset)
            {
                /* -2 to adjust the bytecode for the offset itself */
                int jump;
                jump = currentBlob()->m_count - offset - 2;
                if(jump > UINT16_MAX)
                {
                    raiseError("body of conditional block too large");
                }
                patchAt(offset, (jump >> 8) & 0xff);
                patchAt(offset + 1, jump & 0xff);

            }

            void emitAssign(uint8_t realop, uint8_t getop, uint8_t setop, int arg)
            {
                NEON_ASTDEBUG(m_pvm, "");
                m_replcanecho = false;
                if(getop == Instruction::OP_PROPERTYGET || getop == Instruction::OP_PROPERTYGETSELF)
                {
                    emitInstruction(Instruction::OP_DUPONE);
                }
                if(arg != -1)
                {
                    emitByteAndShort(getop, arg);
                }
                else
                {
                    emitInstruction(getop);
                    emit1byte(1);
                }
                parseExpression();
                emitInstruction(realop);
                if(arg != -1)
                {
                    emitByteAndShort(setop, (uint16_t)arg);
                }
                else
                {
                    emitInstruction(setop);
                }
            }

            void emitNamedVar(Token name, bool canassign)
            {
                bool fromclass;
                uint8_t getop;
                uint8_t setop;
                int arg;
                (void)fromclass;
                NEON_ASTDEBUG(m_pvm, " name=%.*s", name.length, name.start);
                fromclass = m_currclasscompiler != nullptr;
                arg = m_currfunccompiler->resolveLocal(&name);
                if(arg != -1)
                {
                    if(m_infunction)
                    {
                        getop = Instruction::OP_FUNCARGGET;
                        setop = Instruction::OP_FUNCARGSET;
                    }
                    else
                    {
                        getop = Instruction::OP_LOCALGET;
                        setop = Instruction::OP_LOCALSET;
                    }
                }
                else
                {
                    arg = m_currfunccompiler->resolveUpvalue(&name);
                    if((arg != -1) && (!name.isglobal))
                    {
                        getop = Instruction::OP_UPVALUEGET;
                        setop = Instruction::OP_UPVALUESET;
                    }
                    else
                    {
                        arg = makeIdentConst(&name);
                        getop = Instruction::OP_GLOBALGET;
                        setop = Instruction::OP_GLOBALSET;
                    }
                }
                parseAssign(getop, setop, arg, canassign);
            }

            void emitDefineVariable(int global)
            {
                /* we are in a local scope... */
                if(m_currfunccompiler->m_scopedepth > 0)
                {
                    markLocalInitialized();
                    return;
                }
                emitInstruction(Instruction::OP_GLOBALDEFINE);
                emit1short(global);
            }

            void emitSetStmtVar(Token name)
            {
                int local;
                NEON_ASTDEBUG(m_pvm, "name=%.*s", name.length, name.start);
                if(m_currfunccompiler->m_targetfunc->m_scriptfnname != nullptr)
                {
                    local = addLocal(name) - 1;
                    markLocalInitialized();
                    emitInstruction(Instruction::OP_LOCALSET);
                    emit1short((uint16_t)local);
                }
                else
                {
                    emitInstruction(Instruction::OP_GLOBALDEFINE);
                    emit1short((uint16_t)makeIdentConst(&name));
                }
            }

            void scopeBegin()
            {
                NEON_ASTDEBUG(m_pvm, "current depth=%d", m_currfunccompiler->m_scopedepth);
                m_currfunccompiler->m_scopedepth++;
            }

            bool scopeEndCanContinue()
            {
                int lopos;
                int locount;
                int lodepth;
                int scodepth;
                NEON_ASTDEBUG(m_pvm, "");
                locount = m_currfunccompiler->m_localcount;
                lopos = m_currfunccompiler->m_localcount - 1;
                lodepth = m_currfunccompiler->m_compiledlocals[lopos].depth;
                scodepth = m_currfunccompiler->m_scopedepth;
                return locount > 0 && lodepth > scodepth;
            }

            void scopeEnd()
            {
                NEON_ASTDEBUG(m_pvm, "current scope depth=%d", m_currfunccompiler->m_scopedepth);
                m_currfunccompiler->m_scopedepth--;
                /*
                // remove all variables declared in scope while exiting...
                */
                if(m_keeplastvalue)
                {
                    //return;
                }
                while(scopeEndCanContinue())
                {
                    if(m_currfunccompiler->m_compiledlocals[m_currfunccompiler->m_localcount - 1].iscaptured)
                    {
                        emitInstruction(Instruction::OP_UPVALUECLOSE);
                    }
                    else
                    {
                        emitInstruction(Instruction::OP_POPONE);
                    }
                    m_currfunccompiler->m_localcount--;
                }
            }

            bool checkNumber()
            {
                Token::Type t;
                t = m_prevtoken.type;
                return t == Token::TOK_LITNUMREG || t == Token::TOK_LITNUMOCT || t == Token::TOK_LITNUMBIN || t == Token::TOK_LITNUMHEX;
            }

            bool check(Token::Type t)
            {
                return m_currtoken.type == t;
            }

            bool match(Token::Type t)
            {
                if(!check(t))
                {
                    return false;
                }
                advance();
                return true;
            }

            void ignoreSpace()
            {
                while(true)
                {
                    if(check(Token::TOK_NEWLINE))
                    {
                        advance();
                    }
                    else
                    {
                        break;
                    }
                }
            }

            int makeIdentConst(Token* name)
            {
                int rawlen;
                const char* rawstr;
                String* str;
                rawstr = name->start;
                rawlen = name->length;
                if(name->isglobal)
                {
                    rawstr++;
                    rawlen--;
                }
                str = String::copy(m_pvm, rawstr, rawlen);
                return pushConst(Value::fromObject(str));
            }

            void declareVariable()
            {
                int i;
                Token* name;
                CompiledLocal* local;
                /* global variables are implicitly declared... */
                if(m_currfunccompiler->m_scopedepth == 0)
                {
                    return;
                }
                name = &m_prevtoken;
                for(i = m_currfunccompiler->m_localcount - 1; i >= 0; i--)
                {
                    local = &m_currfunccompiler->m_compiledlocals[i];
                    if(local->depth != -1 && local->depth < m_currfunccompiler->m_scopedepth)
                    {
                        break;
                    }
                    if(identsEqual(name, &local->varname))
                    {
                        raiseError("%.*s already declared in current scope", name->length, name->start);
                    }
                }
                addLocal(*name);
            }

            int addLocal(Token name)
            {
                CompiledLocal local;
                #if 0
                if(m_currfunccompiler->m_localcount == FuncCompiler::MaxLocals)
                {
                    /* we've reached maximum local variables per scope */
                    raiseError("too many local variables in scope");
                    return -1;
                }
                #endif
                local.varname = name;
                local.depth = -1;
                local.iscaptured = false;
                //m_currfunccompiler->m_compiledlocals.push(local);
                m_currfunccompiler->m_compiledlocals.insertDefault(local, m_currfunccompiler->m_localcount, CompiledLocal{});

                m_currfunccompiler->m_localcount++;
                return m_currfunccompiler->m_localcount;
            }

            bool consumeIdent(bool raise, const char* msg)
            {
                Token tok;
                Token::Type tident;
                tident = Token::TOK_IDENTNORMAL;
                advance();
                if(m_prevtoken.iskeyident)
                {
                    return true;
                }
                else
                {
                    if(!raise)
                    {
                        if(m_currtoken.type != tident)
                        {
                            return false;
                        }
                    }
                    if(!consume(tident, msg))
                    {
                        // what to do here?
                        return false;
                    }
                }
                return true;
            }

            int parseIdentVar(const char* message)
            {
                if(!consumeIdent(true, message))
                {
                    // what to do here?
                }
                declareVariable();
                /* we are in a local scope... */
                if(m_currfunccompiler->m_scopedepth > 0)
                {
                    return 0;
                }
                return makeIdentConst(&m_prevtoken);
            }

            int discardLocalVars(int depth)
            {
                int local;
                NEON_ASTDEBUG(m_pvm, "");
                if(m_keeplastvalue)
                {
                    //return 0;
                }
                if(m_currfunccompiler->m_scopedepth == -1)
                {
                    raiseError("cannot exit top-level scope");
                }
                local = m_currfunccompiler->m_localcount - 1;
                while(local >= 0 && m_currfunccompiler->m_compiledlocals[local].depth >= depth)
                {
                    if(m_currfunccompiler->m_compiledlocals[local].iscaptured)
                    {
                        emitInstruction(Instruction::OP_UPVALUECLOSE);
                    }
                    else
                    {
                        emitInstruction(Instruction::OP_POPONE);
                    }
                    local--;
                }
                return m_currfunccompiler->m_localcount - local - 1;
            }

            void endLoop()
            {
                size_t i;
                Instruction* bcode;
                Value* cvals;
                NEON_ASTDEBUG(m_pvm, "");
                /*
                // find all Instruction::OP_BREAK_PL placeholder and replace with the appropriate jump...
                */
                i = m_innermostloopstart;
                while(i < m_currfunccompiler->m_targetfunc->m_compiledblob->m_count)
                {
                    if(m_currfunccompiler->m_targetfunc->m_compiledblob->m_instrucs[i].code == Instruction::OP_BREAK_PL)
                    {
                        m_currfunccompiler->m_targetfunc->m_compiledblob->m_instrucs[i].code = Instruction::OP_JUMPNOW;
                        emitPatchJump(i + 1);
                        i += 3;
                    }
                    else
                    {
                        bcode = m_currfunccompiler->m_targetfunc->m_compiledblob->m_instrucs;
                        cvals = m_currfunccompiler->m_targetfunc->m_compiledblob->m_constants->m_values;
                        i += 1 + getCodeArgsCount(bcode, cvals, i);
                    }
                }
            }

            bool doParsePrecedence(SyntaxRule::Precedence precedence/*, AstExpression* dest*/)
            {
                bool canassign;
                Token previous;
                SyntaxRule::InfixFN infixrule;
                SyntaxRule::PrefixFN prefixrule;
                prefixrule = getRule(m_prevtoken.type)->fnprefix;
                if(prefixrule == nullptr)
                {
                    raiseError("expected expression");
                    return false;
                }
                canassign = precedence <= SyntaxRule::PREC_ASSIGNMENT;
                prefixrule(this, canassign);
                while(precedence <= getRule(m_currtoken.type)->precedence)
                {
                    previous = m_prevtoken;
                    ignoreSpace();
                    advance();
                    infixrule = getRule(m_prevtoken.type)->fninfix;
                    infixrule(this, previous, canassign);
                }
                if(canassign && match(Token::TOK_ASSIGN))
                {
                    raiseError("invalid assignment target");
                    return false;
                }
                return true;
            }

            bool parsePrecedence(SyntaxRule::Precedence precedence)
            {
                if(m_lexer->isAtEnd() && m_pvm->m_isreplmode)
                {
                    return false;
                }
                ignoreSpace();
                if(m_lexer->isAtEnd() && m_pvm->m_isreplmode)
                {
                    return false;
                }
                advance();
                return doParsePrecedence(precedence);
            }

            bool parsePrecNoAdvance(SyntaxRule::Precedence precedence)
            {
                if(m_lexer->isAtEnd() && m_pvm->m_isreplmode)
                {
                    return false;
                }
                ignoreSpace();
                if(m_lexer->isAtEnd() && m_pvm->m_isreplmode)
                {
                    return false;
                }
                return doParsePrecedence(precedence);
            }

            void markLocalInitialized()
            {
                int xpos;
                if(m_currfunccompiler->m_scopedepth == 0)
                {
                    return;
                }
                xpos = (m_currfunccompiler->m_localcount - 1);
                m_currfunccompiler->m_compiledlocals[xpos].depth = m_currfunccompiler->m_scopedepth;
            }

            void parseAssign(uint8_t getop, uint8_t setop, int arg, bool canassign)
            {
                NEON_ASTDEBUG(m_pvm, "");
                if(canassign && match(Token::TOK_ASSIGN))
                {
                    m_replcanecho = false;
                    parseExpression();
                    if(arg != -1)
                    {
                        emitByteAndShort(setop, (uint16_t)arg);
                    }
                    else
                    {
                        emitInstruction(setop);
                    }
                }
                else if(canassign && match(Token::TOK_PLUSASSIGN))
                {
                    emitAssign(Instruction::OP_PRIMADD, getop, setop, arg);
                }
                else if(canassign && match(Token::TOK_MINUSASSIGN))
                {
                    emitAssign(Instruction::OP_PRIMSUBTRACT, getop, setop, arg);
                }
                else if(canassign && match(Token::TOK_MULTASSIGN))
                {
                    emitAssign(Instruction::OP_PRIMMULTIPLY, getop, setop, arg);
                }
                else if(canassign && match(Token::TOK_DIVASSIGN))
                {
                    emitAssign(Instruction::OP_PRIMDIVIDE, getop, setop, arg);
                }
                else if(canassign && match(Token::TOK_POWASSIGN))
                {
                    emitAssign(Instruction::OP_PRIMPOW, getop, setop, arg);
                }
                else if(canassign && match(Token::TOK_MODASSIGN))
                {
                    emitAssign(Instruction::OP_PRIMMODULO, getop, setop, arg);
                }
                else if(canassign && match(Token::TOK_AMPASSIGN))
                {
                    emitAssign(Instruction::OP_PRIMAND, getop, setop, arg);
                }
                else if(canassign && match(Token::TOK_BARASSIGN))
                {
                    emitAssign(Instruction::OP_PRIMOR, getop, setop, arg);
                }
                else if(canassign && match(Token::TOK_TILDEASSIGN))
                {
                    emitAssign(Instruction::OP_PRIMBITNOT, getop, setop, arg);
                }
                else if(canassign && match(Token::TOK_XORASSIGN))
                {
                    emitAssign(Instruction::OP_PRIMBITXOR, getop, setop, arg);
                }
                else if(canassign && match(Token::TOK_LEFTSHIFTASSIGN))
                {
                    emitAssign(Instruction::OP_PRIMSHIFTLEFT, getop, setop, arg);
                }
                else if(canassign && match(Token::TOK_RIGHTSHIFTASSIGN))
                {
                    emitAssign(Instruction::OP_PRIMSHIFTRIGHT, getop, setop, arg);
                }
                else if(canassign && match(Token::TOK_INCREMENT))
                {
                    m_replcanecho = false;
                    if(getop == Instruction::OP_PROPERTYGET || getop == Instruction::OP_PROPERTYGETSELF)
                    {
                        emitInstruction(Instruction::OP_DUPONE);
                    }
                    if(arg != -1)
                    {
                        emitByteAndShort(getop, arg);
                    }
                    else
                    {
                        emitInstruction(getop);
                        emit1byte(1);
                    }
                    emitInstruction(Instruction::OP_PUSHONE);
                    emitInstruction(Instruction::OP_PRIMADD);
                    emitInstruction(setop);
                    emit1short((uint16_t)arg);
                }
                else if(canassign && match(Token::TOK_DECREMENT))
                {
                    m_replcanecho = false;
                    if(getop == Instruction::OP_PROPERTYGET || getop == Instruction::OP_PROPERTYGETSELF)
                    {
                        emitInstruction(Instruction::OP_DUPONE);
                    }
                    if(arg != -1)
                    {
                        emitByteAndShort(getop, arg);
                    }
                    else
                    {
                        emitInstruction(getop);
                        emit1byte(1);
                    }
                    emitInstruction(Instruction::OP_PUSHONE);
                    emitInstruction(Instruction::OP_PRIMSUBTRACT);
                    emitInstruction(setop);
                    emit1short((uint16_t)arg);
                }
                else
                {
                    if(arg != -1)
                    {
                        if(getop == Instruction::OP_INDEXGET || getop == Instruction::OP_INDEXGETRANGED)
                        {
                            emitInstruction(getop);
                            emit1byte((uint8_t)0);
                        }
                        else
                        {
                            emitByteAndShort(getop, (uint16_t)arg);
                        }
                    }
                    else
                    {
                        emitInstruction(getop);
                        emit1byte((uint8_t)0);
                    }
                }
            }

            bool parseExpression()
            {
                return parsePrecedence(SyntaxRule::PREC_ASSIGNMENT);
            }

            void printCompiledFuncBlob(const char* fname)
            {
                DebugPrinter dp(m_pvm, m_pvm->m_debugprinter);
                dp.printFunctionDisassembly(currentBlob(), fname);
            }

            FuncScript* endCompiler()
            {
                const char* fname;
                FuncScript* function;
                emitReturn();
                function = m_currfunccompiler->m_targetfunc;
                fname = nullptr;
                if(function->m_scriptfnname == nullptr)
                {
                    fname = m_currmodule->m_physlocation->data();
                }
                else
                {
                    fname = function->m_scriptfnname->data();
                }
                if(!m_haderror && m_pvm->m_conf.dumpbytecode)
                {
                    printCompiledFuncBlob(fname);
                }
                NEON_ASTDEBUG(m_pvm, "for function '%s'", fname);
                m_currfunccompiler = m_currfunccompiler->m_enclosing;
                return function;
            }

            SyntaxRule* getRule(Token::Type type);

            /*
            // Reads [digits] hex digits in a string literal and returns their number value.
            */
            int readStringHexEscape(const char* str, int index, int count)
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
                    digit = Lexer::readHexDigit(cval);
                    if(digit == -1)
                    {
                        raiseError("invalid hex escape sequence at #%d of \"%s\": '%c' (%d)", pos, str, cval, cval);
                    }
                    value = (value * 16) | digit;
                }
                if(count == 4 && (digit = Lexer::readHexDigit(str[index + i + 2])) != -1)
                {
                    value = (value * 16) | digit;
                }
                return value;
            }

            int readStringUnicodeEscape(char* string, const char* realstring, int numberbytes, int realindex, int index)
            {
                int value;
                int count;
                size_t len;
                char* chr;
                NEON_ASTDEBUG(m_pvm, "");
                value = readStringHexEscape(realstring, realindex, numberbytes);
                count = Util::utf8NumBytes(value);
                if(count == -1)
                {
                    raiseError("cannot encode a negative unicode value");
                }
                /* check for greater that \uffff */
                if(value > 65535)
                {
                    count++;
                }
                if(count != 0)
                {
                    chr = Util::utf8Encode(m_pvm, value, &len);
                    if(chr != nullptr)
                    {
                        memcpy(string + index, chr, (size_t)count + 1);
                        Memory::osFree(chr);
                    }
                    else
                    {
                        raiseError("cannot decode unicode escape at index %d", realindex);
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

            char* parseString(int* length)
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
                rawlen = (((size_t)m_prevtoken.length - 2) + 1);
                NEON_ASTDEBUG(m_pvm, "raw length=%d", rawlen);
                deststr = (char*)State::VM::gcAllocMemory(m_pvm, sizeof(char), rawlen);
                quote = m_prevtoken.start[0];
                realstr = (char*)m_prevtoken.start + 1;
                reallength = m_prevtoken.length - 2;
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
                                    //k += readStringUnicodeEscape(deststr, realstr, 2, i, k) - 1;
                                    //k += readStringHexEscape(deststr, i, 2) - 0;
                                    c = readStringHexEscape(realstr, i, 2) - 0;
                                    i += 2;
                                    //continue;
                                }
                                break;
                            case 'u':
                                {
                                    count = readStringUnicodeEscape(deststr, realstr, 4, i, k);
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
                                    count = readStringUnicodeEscape(deststr, realstr, 8, i, k);
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

            Value parseNumber()
            {
                double dbval;
                long longval;
                long long llval;
                NEON_ASTDEBUG(m_pvm, "");
                if(m_prevtoken.type == Token::TOK_LITNUMBIN)
                {
                    llval = strtoll(m_prevtoken.start + 2, nullptr, 2);
                    return Value::makeNumber(llval);
                }
                else if(m_prevtoken.type == Token::TOK_LITNUMOCT)
                {
                    longval = strtol(m_prevtoken.start + 2, nullptr, 8);
                    return Value::makeNumber(longval);
                }
                else if(m_prevtoken.type == Token::TOK_LITNUMHEX)
                {
                    longval = strtol(m_prevtoken.start, nullptr, 16);
                    return Value::makeNumber(longval);
                }
                dbval = strtod(m_prevtoken.start, nullptr);
                return Value::makeNumber(dbval);
            }

            bool parseBlock()
            {
                m_blockcount++;
                ignoreSpace();
                while(!check(Token::TOK_BRACECLOSE) && !check(Token::TOK_EOF))
                {
                    parseDeclaration();
                }
                m_blockcount--;
                if(!consume(Token::TOK_BRACECLOSE, "expected '}' after block"))
                {
                    return false;
                }
                if(match(Token::TOK_SEMICOLON))
                {
                }
                return true;
            }

            void declareFuncArgumentVar()
            {
                int i;
                Token* name;
                CompiledLocal* local;
                /* global variables are implicitly declared... */
                if(m_currfunccompiler->m_scopedepth == 0)
                {
                    return;
                }
                name = &m_prevtoken;
                for(i = m_currfunccompiler->m_localcount - 1; i >= 0; i--)
                {
                    local = &m_currfunccompiler->m_compiledlocals[i];
                    if(local->depth != -1 && local->depth < m_currfunccompiler->m_scopedepth)
                    {
                        break;
                    }
                    if(identsEqual(name, &local->varname))
                    {
                        raiseError("%.*s already declared in current scope", name->length, name->start);
                    }
                }
                addLocal(*name);
            }

            int parseFuncDeclParamVar(const char* message)
            {
                if(!consume(Token::TOK_IDENTNORMAL, message))
                {
                    /* what to do here? */
                }
                declareFuncArgumentVar();
                /* we are in a local scope... */
                if(m_currfunccompiler->m_scopedepth > 0)
                {
                    return 0;
                }
                return makeIdentConst(&m_prevtoken);
            }

            uint8_t parseFuncCallArgs()
            {
                uint8_t argcount;
                argcount = 0;
                if(!check(Token::TOK_PARENCLOSE))
                {
                    do
                    {
                        ignoreSpace();
                        parseExpression();
                        argcount++;
                    } while(match(Token::TOK_COMMA));
                }
                ignoreSpace();
                if(!consume(Token::TOK_PARENCLOSE, "expected ')' after argument list"))
                {
                    /* TODO: handle this, somehow. */
                }
                return argcount;
            }

            void parseFuncDeclParamList()
            {
                int paramconstant;
                /* compile argument list... */
                do
                {
                    ignoreSpace();
                    m_currfunccompiler->m_targetfunc->m_arity++;
                    if(match(Token::TOK_TRIPLEDOT))
                    {
                        m_currfunccompiler->m_targetfunc->m_isvariadic = true;
                        addLocal(makeSynthToken("__args__"));
                        emitDefineVariable(0);
                        break;
                    }
                    paramconstant = parseFuncDeclParamVar("expected parameter name");
                    emitDefineVariable(paramconstant);
                    ignoreSpace();
                } while(match(Token::TOK_COMMA));
            }

            void parseFuncDeclaration()
            {
                int global;
                global = parseIdentVar("function name expected");
                markLocalInitialized();
                parseFuncDeclFull(FuncCommon::FUNCTYPE_FUNCTION, false);
                emitDefineVariable(global);
            }

            void parseFuncDeclFull(FuncCommon::Type type, bool isanon)
            {
                m_infunction = true;
                FuncCompiler compiler(this, type, isanon);
                scopeBegin();
                /* compile parameter list */
                consume(Token::TOK_PARENOPEN, "expected '(' after function name");
                if(!check(Token::TOK_PARENCLOSE))
                {
                    parseFuncDeclParamList();
                }
                consume(Token::TOK_PARENCLOSE, "expected ')' after function parameters");
                compiler.compileBody(false, isanon);
                m_infunction = false;
            }

            void parseClassFieldDefinition(bool isstatic)
            {
                int fieldconstant;
                consumeIdent(true, "class property name expected");
                fieldconstant = makeIdentConst(&m_prevtoken);
                if(match(Token::TOK_ASSIGN))
                {
                    parseExpression();
                }
                else
                {
                    emitInstruction(Instruction::OP_PUSHNULL);
                }
                emitInstruction(Instruction::OP_CLASSPROPERTYDEFINE);
                emit1short(fieldconstant);
                emit1byte(isstatic ? 1 : 0);
                consumeStmtEnd();
                ignoreSpace();
            }

            void parseClassDeclaration()
            {
                bool isstatic;
                int nameconst;
                CompContext oldctx;
                Token classname;
                ClassCompiler classcompiler;
                consume(Token::TOK_IDENTNORMAL, "class name expected");
                nameconst = makeIdentConst(&m_prevtoken);
                classname = m_prevtoken;
                declareVariable();
                emitInstruction(Instruction::OP_MAKECLASS);
                emit1short(nameconst);
                emitDefineVariable(nameconst);
                classcompiler.classname = m_prevtoken;
                classcompiler.hassuperclass = false;
                classcompiler.m_enclosing = m_currclasscompiler;
                m_currclasscompiler = &classcompiler;
                oldctx = m_compcontext;
                m_compcontext = COMPCONTEXT_CLASS;
                if(match(Token::TOK_LESSTHAN))
                {
                    consume(Token::TOK_IDENTNORMAL, "name of superclass expected");
                    //doRuleVarnormal(this, false);
                    emitNamedVar(m_prevtoken, false);
                    if(identsEqual(&classname, &m_prevtoken))
                    {
                        raiseError("class %.*s cannot inherit from itself", classname.length, classname.start);
                    }
                    scopeBegin();
                    addLocal(makeSynthToken(g_strsuper));
                    emitDefineVariable(0);
                    emitNamedVar(classname, false);
                    emitInstruction(Instruction::OP_CLASSINHERIT);
                    classcompiler.hassuperclass = true;
                }
                emitNamedVar(classname, false);
                ignoreSpace();
                consume(Token::TOK_BRACEOPEN, "expected '{' before class body");
                ignoreSpace();
                while(!check(Token::TOK_BRACECLOSE) && !check(Token::TOK_EOF))
                {
                    isstatic = false;
                    if(match(Token::TOK_KWSTATIC))
                    {
                        isstatic = true;
                    }

                    if(match(Token::TOK_KWVAR))
                    {
                        parseClassFieldDefinition(isstatic);
                    }
                    else
                    {
                        parseMethod(classname, isstatic);
                        ignoreSpace();
                    }
                }
                consume(Token::TOK_BRACECLOSE, "expected '}' after class body");
                if(match(Token::TOK_SEMICOLON))
                {
                }
                emitInstruction(Instruction::OP_POPONE);
                if(classcompiler.hassuperclass)
                {
                    scopeEnd();
                }
                m_currclasscompiler = m_currclasscompiler->m_enclosing;
                m_compcontext = oldctx;
            }

            void parseMethod(Token classname, bool isstatic)
            {
                size_t sn;
                int constant;
                const char* sc;
                FuncCommon::Type type;
                static Token::Type tkns[] = { Token::TOK_IDENTNORMAL, Token::TOK_DECORATOR };
                (void)classname;
                (void)isstatic;
                sc = "constructor";
                sn = strlen(sc);
                consumeOr("method name expected", tkns, 2);
                constant = makeIdentConst(&m_prevtoken);
                type = FuncCommon::FUNCTYPE_METHOD;
                if((m_prevtoken.length == (int)sn) && (memcmp(m_prevtoken.start, sc, sn) == 0))
                {
                    type = FuncCommon::FUNCTYPE_INITIALIZER;
                }
                else if((m_prevtoken.length > 0) && (m_prevtoken.start[0] == '_'))
                {
                    type = FuncCommon::FUNCTYPE_PRIVATE;
                }
                if(isstatic)
                {
                    type = FuncCommon::FUNCTYPE_STATIC;
                }
                parseFuncDeclFull(type, false);
                emitInstruction(Instruction::OP_MAKEMETHOD);
                emit1short(constant);
            }

            void parseVarDeclaration(bool isinitializer)
            {
                int global;
                int totalparsed;
                totalparsed = 0;
                do
                {
                    if(totalparsed > 0)
                    {
                        ignoreSpace();
                    }
                    global = parseIdentVar("variable name expected");
                    if(match(Token::TOK_ASSIGN))
                    {
                        parseExpression();
                    }
                    else
                    {
                        emitInstruction(Instruction::OP_PUSHNULL);
                    }
                    emitDefineVariable(global);
                    totalparsed++;
                } while(match(Token::TOK_COMMA));

                if(!isinitializer)
                {
                    consumeStmtEnd();
                }
                else
                {
                    consume(Token::TOK_SEMICOLON, "expected ';' after initializer");
                    ignoreSpace();
                }
            }

            void parseExprStatement(bool isinitializer, bool semi)
            {
                if(m_pvm->m_isreplmode && m_currfunccompiler->m_scopedepth == 0)
                {
                    m_replcanecho = true;
                }
                if(!semi)
                {
                    parseExpression();
                }
                else
                {
                    parsePrecNoAdvance(SyntaxRule::PREC_ASSIGNMENT);
                }
                if(!isinitializer)
                {
                    if(m_replcanecho && m_pvm->m_isreplmode)
                    {
                        emitInstruction(Instruction::OP_ECHO);
                        m_replcanecho = false;
                    }
                    else
                    {
                        //if(!m_keeplastvalue)
                        {
                            emitInstruction(Instruction::OP_POPONE);
                        }
                    }
                    consumeStmtEnd();
                }
                else
                {
                    consume(Token::TOK_SEMICOLON, "expected ';' after initializer");
                    ignoreSpace();
                    emitInstruction(Instruction::OP_POPONE);
                }
            }

            void parseDeclaration()
            {
                ignoreSpace();
                if(match(Token::TOK_KWCLASS))
                {
                    parseClassDeclaration();
                }
                else if(match(Token::TOK_KWFUNCTION))
                {
                    parseFuncDeclaration();
                }
                else if(match(Token::TOK_KWVAR))
                {
                    parseVarDeclaration(false);
                }
                else if(match(Token::TOK_BRACEOPEN))
                {
                    if(!check(Token::TOK_NEWLINE) && m_currfunccompiler->m_scopedepth == 0)
                    {
                        parseExprStatement(false, true);
                    }
                    else
                    {
                        scopeBegin();
                        parseBlock();
                        scopeEnd();
                    }
                }
                else
                {
                    parseStatement();
                }
                ignoreSpace();
                if(m_panicmode)
                {
                    synchronize();
                }
                ignoreSpace();
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
            void parseForStatement()
            {
                int exitjump;
                int bodyjump;
                int incrstart;
                int surroundingloopstart;
                int surroundingscopedepth;
                scopeBegin();
                consume(Token::TOK_PARENOPEN, "expected '(' after 'for'");
                /* parse initializer... */
                if(match(Token::TOK_SEMICOLON))
                {
                    /* no initializer */
                }
                else if(match(Token::TOK_KWVAR))
                {
                    parseVarDeclaration(true);
                }
                else
                {
                    parseExprStatement(true, false);
                }
                /* keep a copy of the surrounding loop's start and depth */
                surroundingloopstart = m_innermostloopstart;
                surroundingscopedepth = m_innermostloopscopedepth;
                /* update the parser's loop start and depth to the current */
                m_innermostloopstart = currentBlob()->m_count;
                m_innermostloopscopedepth = m_currfunccompiler->m_scopedepth;
                exitjump = -1;
                if(!match(Token::TOK_SEMICOLON))
                {
                    /* the condition is optional */
                    parseExpression();
                    consume(Token::TOK_SEMICOLON, "expected ';' after condition");
                    ignoreSpace();
                    /* jump out of the loop if the condition is false... */
                    exitjump = emitJump(Instruction::OP_JUMPIFFALSE);
                    emitInstruction(Instruction::OP_POPONE);
                    /* pop the condition */
                }
                /* the iterator... */
                if(!check(Token::TOK_BRACEOPEN))
                {
                    bodyjump = emitJump(Instruction::OP_JUMPNOW);
                    incrstart = currentBlob()->m_count;
                    parseExpression();
                    ignoreSpace();
                    emitInstruction(Instruction::OP_POPONE);
                    emitLoop(m_innermostloopstart);
                    m_innermostloopstart = incrstart;
                    emitPatchJump(bodyjump);
                }
                consume(Token::TOK_PARENCLOSE, "expected ')' after 'for'");
                parseStatement();
                emitLoop(m_innermostloopstart);
                if(exitjump != -1)
                {
                    emitPatchJump(exitjump);
                    emitInstruction(Instruction::OP_POPONE);
                }
                endLoop();
                /* reset the loop start and scope depth to the surrounding value */
                m_innermostloopstart = surroundingloopstart;
                m_innermostloopscopedepth = surroundingscopedepth;
                scopeEnd();
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
            void parseForeachStatement()
            {
                int citer;
                int citern;
                int falsejump;
                int keyslot;
                int valueslot;
                int iteratorslot;
                int surroundingloopstart;
                int surroundingscopedepth;
                Token iteratortoken;
                Token keytoken;
                Token valuetoken;
                scopeBegin();
                /* define @iter and @itern constant */
                citer = pushConst(Value::fromObject(String::copy(m_pvm, "@iter")));
                citern = pushConst(Value::fromObject(String::copy(m_pvm, "@itern")));
                consume(Token::TOK_PARENOPEN, "expected '(' after 'foreach'");
                consume(Token::TOK_IDENTNORMAL, "expected variable name after 'foreach'");
                if(!check(Token::TOK_COMMA))
                {
                    keytoken = makeSynthToken(" _ ");
                    valuetoken = m_prevtoken;
                }
                else
                {
                    keytoken = m_prevtoken;
                    consume(Token::TOK_COMMA, "");
                    consume(Token::TOK_IDENTNORMAL, "expected variable name after ','");
                    valuetoken = m_prevtoken;
                }
                consume(Token::TOK_KWIN, "expected 'in' after for loop variable(s)");
                ignoreSpace();
                /*
                // The space in the variable name ensures it won't collide with a user-defined
                // variable.
                */
                iteratortoken = makeSynthToken(" iterator ");
                /* Evaluate the sequence expression and store it in a hidden local variable. */
                parseExpression();
                consume(Token::TOK_PARENCLOSE, "expected ')' after 'foreach'");
                if(m_currfunccompiler->m_localcount + 3 > FuncCompiler::MaxLocals)
                {
                    raiseError("cannot declare more than %d variables in one scope", FuncCompiler::MaxLocals);
                    return;
                }
                /* add the iterator to the local scope */
                iteratorslot = addLocal(iteratortoken) - 1;
                emitDefineVariable(0);
                /* Create the key local variable. */
                emitInstruction(Instruction::OP_PUSHNULL);
                keyslot = addLocal(keytoken) - 1;
                emitDefineVariable(keyslot);
                /* create the local value slot */
                emitInstruction(Instruction::OP_PUSHNULL);
                valueslot = addLocal(valuetoken) - 1;
                emitDefineVariable(0);
                surroundingloopstart = m_innermostloopstart;
                surroundingscopedepth = m_innermostloopscopedepth;
                /*
                // we'll be jumping back to right before the
                // expression after the loop body
                */
                m_innermostloopstart = currentBlob()->m_count;
                m_innermostloopscopedepth = m_currfunccompiler->m_scopedepth;
                /* key = iterable.iter_n__(key) */
                emitInstruction(Instruction::OP_LOCALGET);
                emit1short(iteratorslot);
                emitInstruction(Instruction::OP_LOCALGET);
                emit1short(keyslot);
                emitInstruction(Instruction::OP_CALLMETHOD);
                emit1short(citern);
                emit1byte(1);
                emitInstruction(Instruction::OP_LOCALSET);
                emit1short(keyslot);
                falsejump = emitJump(Instruction::OP_JUMPIFFALSE);
                emitInstruction(Instruction::OP_POPONE);
                /* value = iterable.iter__(key) */
                emitInstruction(Instruction::OP_LOCALGET);
                emit1short(iteratorslot);
                emitInstruction(Instruction::OP_LOCALGET);
                emit1short(keyslot);
                emitInstruction(Instruction::OP_CALLMETHOD);
                emit1short(citer);
                emit1byte(1);
                /*
                // Bind the loop value in its own scope. This ensures we get a fresh
                // variable each iteration so that closures for it don't all see the same one.
                */
                scopeBegin();
                /* update the value */
                emitInstruction(Instruction::OP_LOCALSET);
                emit1short(valueslot);
                emitInstruction(Instruction::OP_POPONE);
                parseStatement();
                scopeEnd();
                emitLoop(m_innermostloopstart);
                emitPatchJump(falsejump);
                emitInstruction(Instruction::OP_POPONE);
                endLoop();
                m_innermostloopstart = surroundingloopstart;
                m_innermostloopscopedepth = surroundingscopedepth;
                scopeEnd();
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
            void parseSwitchStatement()
            {
                int i;
                int length;
                int tgtaddr;
                int swstate;
                int casecount;
                int switchcode;
                int startoffset;
                char* str;
                Value jump;
                Token::Type casetype;
                VarSwitch* sw;
                String* string;
                Util::GenericArray<int> caseends;
                /* the expression */
                parseExpression();
                consume(Token::TOK_BRACEOPEN, "expected '{' after 'switch' expression");
                ignoreSpace();
                /* 0: before all cases, 1: before default, 2: after default */
                swstate = 0;
                casecount = 0;
                sw = VarSwitch::make(m_pvm);
                m_pvm->m_vmstate->stackPush(Value::fromObject(sw));
                switchcode = emitSwitch();
                /*
                emitInstruction(Instruction::OP_SWITCH);
                emit1short(pushConst(Value::fromObject(sw)));
                */
                startoffset = currentBlob()->m_count;
                while(!match(Token::TOK_BRACECLOSE) && !check(Token::TOK_EOF))
                {
                    if(match(Token::TOK_KWCASE) || match(Token::TOK_KWDEFAULT))
                    {
                        casetype = m_prevtoken.type;
                        if(swstate == 2)
                        {
                            raiseError("cannot have another case after a default case");
                        }
                        if(swstate == 1)
                        {
                            /* at the end of the previous case, jump over the others... */
                            tgtaddr = emitJump(Instruction::OP_JUMPNOW);
                            //caseends[casecount++] = tgtaddr;
                            caseends.push(tgtaddr);
                            casecount++;
                        }
                        if(casetype == Token::TOK_KWCASE)
                        {
                            swstate = 1;
                            do
                            {
                                ignoreSpace();
                                advance();
                                jump = Value::makeNumber((double)currentBlob()->m_count - (double)startoffset);
                                if(m_prevtoken.type == Token::TOK_KWTRUE)
                                {
                                    sw->m_jumppositions->set(Value::makeBool(true), jump);
                                }
                                else if(m_prevtoken.type == Token::TOK_KWFALSE)
                                {
                                    sw->m_jumppositions->set(Value::makeBool(false), jump);
                                }
                                else if(m_prevtoken.type == Token::TOK_LITERAL)
                                {
                                    str = parseString(&length);
                                    string = String::take(m_pvm, str, length);
                                    /* gc fix */
                                    m_pvm->m_vmstate->stackPush(Value::fromObject(string));
                                    sw->m_jumppositions->set(Value::fromObject(string), jump);
                                    /* gc fix */
                                    m_pvm->m_vmstate->stackPop();
                                }
                                else if(checkNumber())
                                {
                                    sw->m_jumppositions->set(parseNumber(), jump);
                                }
                                else
                                {
                                    /* pop the switch */
                                    m_pvm->m_vmstate->stackPop();
                                    raiseError("only constants can be used in 'when' expressions");
                                    return;
                                }
                            } while(match(Token::TOK_COMMA));
                        }
                        else
                        {
                            swstate = 2;
                            sw->m_defaultjump = currentBlob()->m_count - startoffset;
                        }
                    }
                    else
                    {
                        /* otherwise, it's a statement inside the current case */
                        if(swstate == 0)
                        {
                            raiseError("cannot have statements before any case");
                        }
                        parseStatement();
                    }
                }
                /* if we ended without a default case, patch its condition jump */
                if(swstate == 1)
                {
                    tgtaddr = emitJump(Instruction::OP_JUMPNOW);
                    //caseends[casecount++] = tgtaddr;
                    caseends.push(tgtaddr);
                    casecount++;
                }
                /* patch all the case jumps to the end */
                for(i = 0; i < casecount; i++)
                {
                    emitPatchJump(caseends[i]);
                }
                sw->m_exitjump = currentBlob()->m_count - startoffset;
                emitPatchSwitch(switchcode, pushConst(Value::fromObject(sw)));
                /* pop the switch */
                m_pvm->m_vmstate->stackPop();
            }

            void parseIfStatement()
            {
                int elsejump;
                int thenjump;
                parseExpression();
                thenjump = emitJump(Instruction::OP_JUMPIFFALSE);
                emitInstruction(Instruction::OP_POPONE);
                parseStatement();
                elsejump = emitJump(Instruction::OP_JUMPNOW);
                emitPatchJump(thenjump);
                emitInstruction(Instruction::OP_POPONE);
                if(match(Token::TOK_KWELSE))
                {
                    parseStatement();
                }
                emitPatchJump(elsejump);
            }

            void parseEchoStatement()
            {
                parseExpression();
                emitInstruction(Instruction::OP_ECHO);
                consumeStmtEnd();
            }

            void parseThrowStatement()
            {
                parseExpression();
                emitInstruction(Instruction::OP_EXTHROW);
                discardLocalVars(m_currfunccompiler->m_scopedepth - 1);
                consumeStmtEnd();
            }

            void parseAssertStatement()
            {
                consume(Token::TOK_PARENOPEN, "expected '(' after 'assert'");
                parseExpression();
                if(match(Token::TOK_COMMA))
                {
                    ignoreSpace();
                    parseExpression();
                }
                else
                {
                    emitInstruction(Instruction::OP_PUSHNULL);
                }
                emitInstruction(Instruction::OP_ASSERT);
                consume(Token::TOK_PARENCLOSE, "expected ')' after 'assert'");
                consumeStmtEnd();
            }

            void parseTryStatement()
            {
                int address;
                int type;
                int finally;
                int trybegins;
                int exitjump;
                int continueexecutionaddress;
                bool catchexists;
                bool finalexists;
                if(m_currfunccompiler->m_compiledexcepthandlercount == NEON_CFG_MAXEXCEPTHANDLERS)
                {
                    raiseError("maximum exception handler in scope exceeded");
                }
                m_currfunccompiler->m_compiledexcepthandlercount++;
                m_istrying = true;
                ignoreSpace();
                trybegins = emitTry();
                /* compile the try body */
                parseStatement();
                emitInstruction(Instruction::OP_EXPOPTRY);
                exitjump = emitJump(Instruction::OP_JUMPNOW);
                m_istrying = false;
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
                if(match(Token::TOK_KWCATCH))
                {
                    catchexists = true;
                    scopeBegin();
                    consume(Token::TOK_PARENOPEN, "expected '(' after 'catch'");
                    consume(Token::TOK_IDENTNORMAL, "missing exception class name");
                    type = makeIdentConst(&m_prevtoken);
                    address = currentBlob()->m_count;
                    if(match(Token::TOK_IDENTNORMAL))
                    {
                        emitSetStmtVar(m_prevtoken);
                    }
                    else
                    {
                        emitInstruction(Instruction::OP_POPONE);
                    }
                      consume(Token::TOK_PARENCLOSE, "expected ')' after 'catch'");
                    emitInstruction(Instruction::OP_EXPOPTRY);
                    ignoreSpace();
                    parseStatement();
                    scopeEnd();
                }
                else
                {
                    type = pushConst(Value::fromObject(String::copy(m_pvm, "Exception")));
                }
                emitPatchJump(exitjump);
                if(match(Token::TOK_KWFINALLY))
                {
                    finalexists = true;
                    /*
                    // if we arrived here from either the try or handler block,
                    // we don't want to continue propagating the exception
                    */
                    emitInstruction(Instruction::OP_PUSHFALSE);
                    finally = currentBlob()->m_count;
                    ignoreSpace();
                    parseStatement();
                    continueexecutionaddress = emitJump(Instruction::OP_JUMPIFFALSE);
                    /* pop the bool off the stack */
                    emitInstruction(Instruction::OP_POPONE);
                    emitInstruction(Instruction::OP_EXPUBLISHTRY);
                    emitPatchJump(continueexecutionaddress);
                    emitInstruction(Instruction::OP_POPONE);
                }
                if(!finalexists && !catchexists)
                {
                    raiseError("try block must contain at least one of catch or finally");
                }
                emitPatchTry(trybegins, type, address, finally);
            }

            void parseReturnStatement()
            {
                m_isreturning = true;
                /*
                if(m_currfunccompiler->type == FuncCommon::FUNCTYPE_SCRIPT)
                {
                    raiseError("cannot return from top-level code");
                }
                */
                if(match(Token::TOK_SEMICOLON) || match(Token::TOK_NEWLINE))
                {
                    emitReturn();
                }
                else
                {
                    if(m_currfunccompiler->m_type == FuncCommon::FUNCTYPE_INITIALIZER)
                    {
                        raiseError("cannot return value from constructor");
                    }
                    if(m_istrying)
                    {
                        emitInstruction(Instruction::OP_EXPOPTRY);
                    }
                    parseExpression();
                    emitInstruction(Instruction::OP_RETURN);
                    consumeStmtEnd();
                }
                m_isreturning = false;
            }

            void parseLoopWhileStatement()
            {
                int exitjump;
                int surroundingloopstart;
                int surroundingscopedepth;
                surroundingloopstart = m_innermostloopstart;
                surroundingscopedepth = m_innermostloopscopedepth;
                /*
                // we'll be jumping back to right before the
                // expression after the loop body
                */
                m_innermostloopstart = currentBlob()->m_count;
                m_innermostloopscopedepth = m_currfunccompiler->m_scopedepth;
                parseExpression();
                exitjump = emitJump(Instruction::OP_JUMPIFFALSE);
                emitInstruction(Instruction::OP_POPONE);
                parseStatement();
                emitLoop(m_innermostloopstart);
                emitPatchJump(exitjump);
                emitInstruction(Instruction::OP_POPONE);
                endLoop();
                m_innermostloopstart = surroundingloopstart;
                m_innermostloopscopedepth = surroundingscopedepth;
            }

            void parseLoopDoWhileStatement()
            {
                int exitjump;
                int surroundingloopstart;
                int surroundingscopedepth;
                surroundingloopstart = m_innermostloopstart;
                surroundingscopedepth = m_innermostloopscopedepth;
                /*
                // we'll be jumping back to right before the
                // statements after the loop body
                */
                m_innermostloopstart = currentBlob()->m_count;
                m_innermostloopscopedepth = m_currfunccompiler->m_scopedepth;
                parseStatement();
                consume(Token::TOK_KWWHILE, "expecting 'while' statement");
                parseExpression();
                exitjump = emitJump(Instruction::OP_JUMPIFFALSE);
                emitInstruction(Instruction::OP_POPONE);
                emitLoop(m_innermostloopstart);
                emitPatchJump(exitjump);
                emitInstruction(Instruction::OP_POPONE);
                endLoop();
                m_innermostloopstart = surroundingloopstart;
                m_innermostloopscopedepth = surroundingscopedepth;
            }

            void parseContinueStatement()
            {
                if(m_innermostloopstart == -1)
                {
                    raiseError("'continue' can only be used in a loop");
                }
                /*
                // discard local variables created in the loop
                //  discard_local(, m_innermostloopscopedepth);
                */
                discardLocalVars(m_innermostloopscopedepth + 1);
                /* go back to the top of the loop */
                emitLoop(m_innermostloopstart);
                consumeStmtEnd();
            }

            void parseBreakStatement()
            {
                if(m_innermostloopstart == -1)
                {
                    raiseError("'break' can only be used in a loop");
                }
                /* discard local variables created in the loop */
                /*
                int i;
                for(i = m_currfunccompiler->m_localcount - 1; i >= 0 && m_currfunccompiler->m_compiledlocals[i].depth >= m_currfunccompiler->m_scopedepth; i--)
                {
                    if(m_currfunccompiler->m_compiledlocals[i].iscaptured)
                    {
                        emitInstruction(Instruction::OP_UPVALUECLOSE);
                    }
                    else
                    {
                        emitInstruction(Instruction::OP_POPONE);
                    }
                }
                */
                discardLocalVars(m_innermostloopscopedepth + 1);
                emitJump(Instruction::OP_BREAK_PL);
                consumeStmtEnd();
            }

            void parseStatement()
            {
                m_replcanecho = false;
                ignoreSpace();
                if(match(Token::TOK_KWECHO))
                {
                    parseEchoStatement();
                }
                else if(match(Token::TOK_KWIF))
                {
                    parseIfStatement();
                }
                else if(match(Token::TOK_KWDO))
                {
                    parseLoopDoWhileStatement();
                }
                else if(match(Token::TOK_KWWHILE))
                {
                    parseLoopWhileStatement();
                }
                else if(match(Token::TOK_KWFOR))
                {
                    parseForStatement();
                }
                else if(match(Token::TOK_KWFOREACH))
                {
                    parseForeachStatement();
                }
                else if(match(Token::TOK_KWSWITCH))
                {
                    parseSwitchStatement();
                }
                else if(match(Token::TOK_KWCONTINUE))
                {
                    parseContinueStatement();
                }
                else if(match(Token::TOK_KWBREAK))
                {
                    parseBreakStatement();
                }
                else if(match(Token::TOK_KWRETURN))
                {
                    parseReturnStatement();
                }
                else if(match(Token::TOK_KWASSERT))
                {
                    parseAssertStatement();
                }
                else if(match(Token::TOK_KWTHROW))
                {
                    parseThrowStatement();
                }
                else if(match(Token::TOK_BRACEOPEN))
                {
                    scopeBegin();
                    parseBlock();
                    scopeEnd();
                }
                else if(match(Token::TOK_KWTRY))
                {
                    parseTryStatement();
                }
                else
                {
                    parseExprStatement(false, false);
                }
                ignoreSpace();
            }

            void runParser()
            {
                advance();
                ignoreSpace();
                while(!match(Token::TOK_EOF))
                {
                    parseDeclaration();
                }
            }
    };


