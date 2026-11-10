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
    // pub (of the running task)
    E_PUB,
    // spawn T(...)  spawn @ts T(...)  spawn {...}
    E_SPAWN, E_SPAWN_BLK,
    // tasks(n)
    E_TASKS,
    // emit @to (e)
    E_EMIT,
    // await P
    E_AWAIT,
    // toggle t(b) with P  toggle on :T with P {...}
    E_TOGGLE, E_TOGGLE_ON,
    // par {...} with {...}  watching P {...}
    E_PAR, E_WATCHING,
} ETag;

typedef struct Expr  Expr;
typedef struct Var   Var;
typedef struct Proto Proto;

typedef struct List {
    int    n;
    Expr** v;
} List;

// How an index is given: t@(e), t@# (last), t@+ (next).
typedef enum {
    IDX_EXPR, IDX_LAST, IDX_NEXT
} IdxKind;

// Await patterns.
typedef enum {
    // value: event, tag, table, clock, task, boolean
    PT_VAL,
    // T(...): spawns the task and awaits its termination
    PT_SPAWN,
    // :any ts  :all ts
    PT_ANY, PT_ALL,
    // [p] until (c)  [p] while (c)
    PT_UNTIL, PT_WHILE,
    // !p  p && p  p || p
    PT_NOT, PT_AND, PT_OR,
} PKind;

typedef struct Patt Patt;

struct Patt {
    PKind kind;
    Tk    tk;
    // value, task prototype, pool, or predicate
    Expr* e;
    // arguments of a spawn
    List  args;
    // operands; `a` is the optional base of until/while
    Patt* a;
    Patt* b;
};

// Rejoin of a par.
typedef enum {
    PAR_NONE, PAR_ANY, PAR_ALL
} ParMode;

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
    // resolver: function of a prototype
    Proto* proto;
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
        // on: pattern of an event loop, or NULL
        struct { int n; Tk* ids; Expr* itr; Expr* blk; Patt* on; } loop;
        // pool: destination pool, or NULL
        struct { Expr* pool; Expr* f; List args; }      spawn;
        struct { Expr* blk; }                           sblk;
        struct { Expr* n; }                             tasks;
        // to: target, or NULL
        struct { Expr* to; Expr* e; }                   emit;
        struct { Patt* p; }                             await;
        struct { Expr* t; List args; int nf; Patt** fs; } toggle;
        // tk is the tag
        struct { int nf; Patt** fs; Expr* blk; }        toggle_on;
        struct { ParMode mode; List blks; }             par;
        struct { Patt* p; Expr* blk; }                  watching;
        struct { Expr* cnd; Expr* blk; }                xcatch;
        // inl: lambda of an if/ifs/match, expanded in place
        struct { int task; int dots; int n; Tk* pars; Expr* blk; int inl; }
                                                        proto;
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
    // function that declares it
    Proto*  proto;
    // mutable and captured by a closure: lives in a shared box
    int     boxed;
    // coder: slot in `L`
    int     slot;
};

// A function being compiled: the main chunk, or a prototype that is
// not expanded in place.
struct Proto {
    int    id;
    // enclosing function (NULL for the main chunk)
    Proto* up;
    // variables of enclosing functions used here, in capture order
    Var**  caps;
    int    ncaps;
    // accepts `...`
    int    dots;
    // task prototype (or the main chunk): may await
    int    task;
};

// Binds every access to its declaration and checks assignments.
// Returns the top-level variables of the main chunk.
Var** resolver_main (Expr* prog, int* n);

// Function of the main chunk.
extern Proto* PROTO_MAIN;

///////////////////////////////////////////////////////////////////////////////
// CODER
///////////////////////////////////////////////////////////////////////////////

// Generates the C source of the whole program.
char* coder_main (Expr* prog, Var** globals, int nglobals);

#endif
