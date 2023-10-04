
src=<<__eos__
        /* Compiling Expressions rules < Calls and Functions infix-left-paren */
        // [NEON_TOK_PARENOPEN]    = {grouping, NULL,   NEON_PREC_NONE},
        [NEON_TOK_PARENOPEN] = { neon_prs_rulegrouping, neon_prs_rulecall, NEON_PREC_CALL },
        [NEON_TOK_PARENCLOSE] = { NULL, NULL, NEON_PREC_NONE },
        [NEON_TOK_BRACEOPEN] = { NULL, NULL, NEON_PREC_NONE },// [big]
        [NEON_TOK_BRACECLOSE] = { NULL, NULL, NEON_PREC_NONE },
        [NEON_TOK_BRACKETOPEN] = { neon_prs_rulearray, neon_prs_ruleindex, NEON_PREC_CALL },
        [NEON_TOK_BRACKETCLOSE] = { NULL, NULL, NEON_PREC_NONE },
        [NEON_TOK_COMMA] = { NULL, NULL, NEON_PREC_NONE },
        /* Compiling Expressions rules < Classes and Instances table-dot */
        // [NEON_TOK_DOT]           = {NULL,     NULL,   NEON_PREC_NONE},
        [NEON_TOK_DOT] = { NULL, neon_prs_ruledot, NEON_PREC_CALL },
        [NEON_TOK_MINUS] = { neon_prs_ruleunary, neon_prs_rulebinary, NEON_PREC_TERM },
        [NEON_TOK_PLUS] = { NULL, neon_prs_rulebinary, NEON_PREC_TERM },
        [NEON_TOK_SEMICOLON] = { NULL, NULL, NEON_PREC_NONE },
        [NEON_TOK_NEWLINE] = { NULL, NULL, NEON_PREC_NONE },
        [NEON_TOK_SLASH] = { NULL, neon_prs_rulebinary, NEON_PREC_FACTOR },
        [NEON_TOK_STAR] = { NULL, neon_prs_rulebinary, NEON_PREC_FACTOR },
        [NEON_TOK_MODULO] = { NULL, neon_prs_rulebinary, NEON_PREC_FACTOR },
        [NEON_TOK_BINAND] = { NULL, neon_prs_rulebinary, NEON_PREC_FACTOR },
        [NEON_TOK_BINOR] = { NULL, neon_prs_rulebinary, NEON_PREC_FACTOR },
        [NEON_TOK_BINXOR] = { NULL, neon_prs_rulebinary, NEON_PREC_FACTOR },
        [NEON_TOK_SHIFTLEFT] = {NULL, neon_prs_rulebinary, NEON_PREC_SHIFT},
        [NEON_TOK_SHIFTRIGHT] = {NULL, neon_prs_rulebinary, NEON_PREC_SHIFT},
        [NEON_TOK_INCREMENT] = {NULL, NULL, NEON_PREC_NONE},
        [NEON_TOK_DECREMENT] = {NULL, NULL, NEON_PREC_NONE},
        /* Compiling Expressions rules < Types of Values table-not */
        // [NEON_TOK_EXCLAM]          = {NULL,     NULL,   NEON_PREC_NONE},
        [NEON_TOK_EXCLAM] = { neon_prs_ruleunary, NULL, NEON_PREC_NONE },
        /* Compiling Expressions rules < Types of Values table-equal */
        // [NEON_TOK_COMPNOTEQUAL]    = {NULL,     NULL,   NEON_PREC_NONE},
        [NEON_TOK_COMPNOTEQUAL] = { NULL, neon_prs_rulebinary, NEON_PREC_EQUALITY },
        [NEON_TOK_ASSIGN] = { NULL, NULL, NEON_PREC_NONE },
        /* Compiling Expressions rules < Types of Values table-comparisons */
        // [NEON_TOK_COMPEQUAL]   = {NULL,     NULL,   NEON_PREC_NONE},
        // [NEON_TOK_COMPGREATERTHAN]       = {NULL,     NULL,   NEON_PREC_NONE},
        // [NEON_TOK_COMPGREATEREQUAL] = {NULL,     NULL,   NEON_PREC_NONE},
        // [NEON_TOK_COMPLESSTHAN]          = {NULL,     NULL,   NEON_PREC_NONE},
        // [NEON_TOK_COMPLESSEQUAL]    = {NULL,     NULL,   NEON_PREC_NONE},
        [NEON_TOK_COMPEQUAL] = { NULL, neon_prs_rulebinary, NEON_PREC_EQUALITY },
        [NEON_TOK_COMPGREATERTHAN] = { NULL, neon_prs_rulebinary, NEON_PREC_COMPARISON },
        [NEON_TOK_COMPGREATEREQUAL] = { NULL, neon_prs_rulebinary, NEON_PREC_COMPARISON },
        [NEON_TOK_COMPLESSTHAN] = { NULL, neon_prs_rulebinary, NEON_PREC_COMPARISON },
        [NEON_TOK_COMPLESSEQUAL] = { NULL, neon_prs_rulebinary, NEON_PREC_COMPARISON },
        /* Compiling Expressions rules < Global Variables table-identifier */
        // [NEON_TOK_IDENTIFIER]    = {NULL,     NULL,   NEON_PREC_NONE},
        [NEON_TOK_IDENTIFIER] = { neon_prs_rulevariable, NULL, NEON_PREC_NONE },
        /* Compiling Expressions rules < Strings table-string */
        // [NEON_TOK_STRING]        = {NULL,     NULL,   NEON_PREC_NONE},
        [NEON_TOK_STRING] = { neon_prs_rulestring, NULL, NEON_PREC_NONE },
        [NEON_TOK_NUMBER] = { neon_prs_rulenumber, NULL, NEON_PREC_NONE },
        /* Compiling Expressions rules < Jumping Back and Forth table-and */
        // [NEON_TOK_KWAND]           = {NULL,     NULL,   NEON_PREC_NONE},
        [NEON_TOK_KWBREAK] = {NULL, NULL, NEON_PREC_NONE},
        [NEON_TOK_KWCONTINUE] = { NULL, NULL, NEON_PREC_NONE },
        [NEON_TOK_KWAND] = { NULL, neon_prs_ruleand, NEON_PREC_AND },
        [NEON_TOK_KWCLASS] = { NULL, NULL, NEON_PREC_NONE },
        [NEON_TOK_KWELSE] = { NULL, NULL, NEON_PREC_NONE },
        /* Compiling Expressions rules < Types of Values table-false */
        // [NEON_TOK_KWFALSE]         = {NULL,     NULL,   NEON_PREC_NONE},
        [NEON_TOK_KWFALSE] = { neon_prs_ruleliteral, NULL, NEON_PREC_NONE },
        [NEON_TOK_KWFOR] = { NULL, NULL, NEON_PREC_NONE },
        [NEON_TOK_KWFUNCTION] = { NULL, NULL, NEON_PREC_NONE },
        [NEON_TOK_KWIF] = { NULL, NULL, NEON_PREC_NONE },
        /* Compiling Expressions rules < Types of Values table-nil
        * [NEON_TOK_KWNIL]           = {NULL,     NULL,   NEON_PREC_NONE},
        */
        [NEON_TOK_KWNIL] = { neon_prs_ruleliteral, NULL, NEON_PREC_NONE },
        /* Compiling Expressions rules < Jumping Back and Forth table-or
        * [NEON_TOK_KWOR]            = {NULL,     NULL,   NEON_PREC_NONE},
        */
        [NEON_TOK_KWOR] = { NULL, neon_prs_ruleor, NEON_PREC_OR },
        [NEON_TOK_KWDEBUGPRINT] = { NULL, NULL, NEON_PREC_NONE },
        [NEON_TOK_KWRETURN] = { NULL, NULL, NEON_PREC_NONE },
        /* Compiling Expressions rules < Superclasses table-super
        * [NEON_TOK_KWSUPER]         = {NULL,     NULL,   NEON_PREC_NONE},
        */
        [NEON_TOK_KWSUPER] = { neon_prs_rulesuper, NULL, NEON_PREC_NONE },
        /* Compiling Expressions rules < Methods and Initializers table-this
        * [NEON_TOK_KWTHIS]          = {NULL,     NULL,   NEON_PREC_NONE},
        */
        [NEON_TOK_KWTHIS] = { neon_prs_rulethis, NULL, NEON_PREC_NONE },
        /* Compiling Expressions rules < Types of Values table-true
        * [NEON_TOK_KWTRUE]          = {NULL,     NULL,   NEON_PREC_NONE},
        */
        [NEON_TOK_KWTRUE] = { neon_prs_ruleliteral, NULL, NEON_PREC_NONE },
        [NEON_TOK_KWVAR] = { NULL, NULL, NEON_PREC_NONE },
        [NEON_TOK_KWWHILE] = { NULL, NULL, NEON_PREC_NONE },
        [NEON_TOK_ERROR] = { NULL, NULL, NEON_PREC_NONE },
        [NEON_TOK_EOF] = { NULL, NULL, NEON_PREC_NONE },

__eos__

src.split("\n").map(&:strip).reject(&:empty?).each do |s|
  if s.match?(/^\s*\[/) then
    m = s.match(/^\s*\[(?<tname>\w+)\]\s*=\s*{(?<vals>.*)\},/)
    name = m["tname"]
    bod = m["vals"]
    printf("    case %s:\n", name)
    printf("        {\n")
    printf("            return neon_prs_setrule(&dest, %s);\n", bod)
    printf("        }\n");
    printf("        break;\n")
  else
    puts(s)
  end
end
