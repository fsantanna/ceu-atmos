// Atmos compiler: shared declarations.

#ifndef ATMC_H
#define ATMC_H

#include <stddef.h>

///////////////////////////////////////////////////////////////////////////////
// AUX
///////////////////////////////////////////////////////////////////////////////

// Allocations live until the compiler exits.
void* xalloc (size_t n);
char* xstrndup (const char* s, size_t n);
char* xfmt (const char* fmt, ...);

///////////////////////////////////////////////////////////////////////////////
// LEXER
///////////////////////////////////////////////////////////////////////////////

typedef enum {
    TK_SYM, TK_OP, TK_TAG, TK_KEY, TK_ID,
    TK_NUM, TK_CLK, TK_NAT, TK_STR, TK_EOF
} TkTag;

typedef struct Tk {
    TkTag       tag;
    const char* str;
    int         len;
    // line, and separator counter (newlines and semicolons)
    int         lin;
    int         sep;
    // strings: delimiter and multi-line flag
    char        chr;
    int         multi;
    // clocks: duration in microseconds
    double      clk;
} Tk;

extern const char* FILENAME;

// Reports "FILE : line N : near 'STR' : MSG" and exits.
void err (Tk tk, const char* fmt, ...);

void lexer_init (const char* file, const char* src, size_t len);

// Next token. Returns 0 after the end of file was delivered.
int lexer_token (Tk* tk);

const char* lexer_tag (TkTag tag);

///////////////////////////////////////////////////////////////////////////////
// AST
///////////////////////////////////////////////////////////////////////////////

typedef enum {
    // literals: nil, true/false, 10, "s", :X, `c`, 5s, ...
    E_NIL, E_BOOL, E_NUM, E_STR, E_TAG, E_NAT, E_CLK, E_DOTS,
    // x
    E_ACC,
    // t@(e)  t@1  t.x  t@#  t@+
    E_INDEX,
    // [...]  :X [...]
    E_TABLE, E_TAGGED,
    // -e  a+b
    E_UNO, E_BIN,
    // f(...)  o::m
    E_CALL, E_MET,
    // (e)  (e1,e2)
    E_PARENS, E_LIST,
    // e where {...}
    E_WHERE,
    // val x, y = e
    E_DCL,
    // set x, y = e
    E_SET,
    // { e1 ; e2 }
    E_BLOCK,
    // do :X {...}  test {...}
    E_DO,
    // defer {...}
    E_DEFER,
    // if  ifs  match
    E_IFS,
    // loop {...}  loop i {...}  loop k,v in e {...}
    E_LOOP,
    // catch e {...}
    E_CATCH,
    // func (...) {...}  task (...) {...}  \{...}
    E_PROTO,
    // task (the running task)
    E_ME,
} ETag;

typedef struct Expr Expr;
typedef struct Var  Var;

typedef struct List {
    int    n;
    Expr** v;
} List;

// How an index is given: t@(e), t@# (last), t@+ (next).
typedef enum {
    IDX_EXPR, IDX_LAST, IDX_NEXT
} IdxKind;

// Calls to reserved names are statements in disguise.
typedef enum {
    SP_NONE, SP_DO, SP_BREAK, SP_UNTIL, SP_WHILE,
    SP_RETURN, SP_ESCAPE, SP_THROW, SP_ABORT
} Special;

// One case of an if/ifs/match.
typedef struct Case {
    // condition, or NULL for `else`
    Expr* cnd;
    // match only: the condition is a lambda applied to the head
    int   test;
    // block, expression, or lambda that receives the tested value
    Expr* body;
} Case;

struct Expr {
    ETag tag;
    // main token: literal, identifier, operator, or position
    Tk   tk;
    // pipe or where operator that built this node (to reject mixes)
    const char* out;
    // resolver: variable of an access
    Var*  var;
    // resolver: variables of a declaration, loop, or prototype
    Var** vars;
    union {
        struct { Expr* t; IdxKind kind; Expr* idx; }    index;
        // positional entries have a NULL key
        struct { List ks; List vs; }                    table;
        struct { Expr* e; }                             tagged;
        struct { Expr* e; }                             uno;
        struct { Expr* e1; Expr* e2; }                  bin;
        // sp: set by the resolver
        struct { Expr* f; List es; Special sp; }        call;
        struct { Expr* o; Tk met; }                     met;
        struct { Expr* e; }                             parens;
        struct { List es; }                             list;
        struct { List dcls; Expr* e; }                  where;
        // tk is val, var, or pin
        struct { int n; Tk* ids; Expr* set; }           dcl;
        // proto: `func f () {}` declares `f` if it is unknown
        struct { List dsts; Expr* src; int proto; }     set;
        struct { List es; }                             block;
        // esc: has an escape tag (in tk)
        struct { int esc; Expr* blk; }                  xdo;
        struct { Expr* blk; }                           defer;
        // head: match expression, or NULL
        struct { Expr* head; int n; Case* cases; }      ifs;
        struct { int n; Tk* ids; Expr* itr; Expr* blk; } loop;
        struct { Expr* cnd; Expr* blk; }                xcatch;
        struct { int task; int dots; int n; Tk* pars; Expr* blk; } proto;
    } u;
};

void list_add (List* l, Expr* e);
void list_ins (List* l, int i, Expr* e);

///////////////////////////////////////////////////////////////////////////////
// PARSER
///////////////////////////////////////////////////////////////////////////////

// Enables `test` blocks (-t).
extern int OPT_TEST;

// Parses the whole program into a block.
Expr* parser_main (void);

// Prints the AST as S-expressions.
void ast_dump (Expr* e);

///////////////////////////////////////////////////////////////////////////////
// RESOLVER
///////////////////////////////////////////////////////////////////////////////

typedef enum {
    V_VAL, V_VAR, V_PIN, V_PAR
} VarKind;

struct Var {
    Tk      tk;
    VarKind kind;
    // unique number
    int     id;
    // runtime global: C symbol
    const char* csym;
    // top-level variable of the main chunk: static C variable
    int     global;
    // coder: slot in `L`
    int     slot;
};

// Binds every access to its declaration and checks assignments.
// Returns the top-level variables of the main chunk.
Var** resolver_main (Expr* prog, int* n);

///////////////////////////////////////////////////////////////////////////////
// CODER
///////////////////////////////////////////////////////////////////////////////

// Generates the C source of the whole program.
char* coder_main (Expr* prog, Var** globals, int nglobals);

#endif
