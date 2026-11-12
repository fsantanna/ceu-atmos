// Atmos compiler: resolver.
//
// Binds every access to its declaration through lexical scopes, marks
// calls to reserved names, and checks assignments.

#include <stdlib.h>
#include <string.h>

#include "atmc.h"

typedef struct Scope {
    Var** vars;
    int   n;
    struct Scope* up;
} Scope;

static Scope* SCOPE = NULL;
static int    N     = 0;

// Number of enclosing loops in the current function.
static int LOOPS = 0;

// Number of enclosing defers.
static int DEFERS = 0;

// Current function.
Proto* PROTO_MAIN = NULL;
static Proto* PROTO = NULL;
static int    NPROTOS = 0;

// Top-level variables of the main chunk.
static Var** GLOBALS  = NULL;
static int   NGLOBALS = 0;

// Global variables provided by the runtime.
static const char* BUILTINS[][2] = {
    { "atmos",    "atm_g_atmos"    },
    { "math",     "atm_g_math"     },
    { "print",    "atm_g_print"    },
    { "type",     "atm_g_type"     },
    { "tostring", "atm_g_tostring" },
    { "tonumber", "atm_g_tonumber" },
    { "assert",   "atm_g_assert"   },
    { "X",        "atm_g_X"        },
    { "string",   "atm_g_string"   },
    { "vector",   "atm_g_vector"   },
    { "os",       "atm_g_os"       },
    { "io",       "atm_g_io"       },
    { "next",     "atm_g_next"     },
    { NULL, NULL }
};

static const char* SPECIALS[] = {
    "", "do", "break", "until", "while", "return", "escape", "throw",
    "abort", NULL
};

static void res (Expr* e);

///////////////////////////////////////////////////////////////////////////////

static void scope_push (void) {
    Scope* s = xalloc(sizeof(Scope));
    s->up = SCOPE;
    SCOPE = s;
}

static void scope_pop (void) {
    SCOPE = SCOPE->up;
}

static Var* declare (Tk tk, VarKind kind) {
    // `pub` is always the public value of the running task
    if (!strcmp(tk.str, "pub")) {
        err(tk, "invalid declaration : unexpected 'pub'");
    }
    Var* v = xalloc(sizeof(Var));
    v->tk   = tk;
    v->kind = kind;
    v->id    = ++N;
    v->slot  = -1;
    v->proto = PROTO;
    SCOPE->vars = realloc(SCOPE->vars, (SCOPE->n+1) * sizeof(Var*));
    SCOPE->vars[SCOPE->n++] = v;

    // scopes: builtins, then the main block
    int top = (SCOPE->up!=NULL && SCOPE->up->up==NULL);
    if (top) {
        v->global = 1;
        GLOBALS = realloc(GLOBALS, (NGLOBALS+1) * sizeof(Var*));
        GLOBALS[NGLOBALS++] = v;
    }
    return v;
}

// Innermost declaration of a name: the latest one in each scope.
static Var* lookup (const char* id) {
    for (Scope* s=SCOPE; s!=NULL; s=s->up) {
        for (int i=s->n-1; i>=0; i--) {
            if (!strcmp(s->vars[i]->tk.str, id)) {
                return s->vars[i];
            }
        }
    }
    return NULL;
}

// A variable of an enclosing function is captured by every function
// from the current one up to its owner. Static variables are not.
static void capture (Var* v) {
    if (v->csym!=NULL || v->global) {
        return;
    }
    for (Proto* p=PROTO; p!=v->proto; p=p->up) {
        // the function that the variable names: it is the function
        // itself there, and a capture would be a cycle
        if (p->self == v) {
            break;
        }
        int has = 0;
        for (int i=0; i<p->ncaps; i++) {
            has = has || (p->caps[i] == v);
        }
        if (!has) {
            p->caps = realloc(p->caps, (p->ncaps+1) * sizeof(Var*));
            p->caps[p->ncaps++] = v;
        }
        // a captured mutable variable is shared through a box
        if (v->kind == V_VAR) {
            v->boxed = 1;
        }
    }
}

// Variable that names the next function to be resolved.
static Var* SELF = NULL;

static Proto* proto_new (int dots, int task) {
    Proto* p = xalloc(sizeof(Proto));
    p->id   = NPROTOS++;
    p->up   = PROTO;
    p->dots = dots;
    p->task = task;
    p->self = SELF;
    SELF = NULL;
    return p;
}

static void res_patt (Patt* p) {
    if (p == NULL) {
        return;
    }
    res(p->e);
    for (int i=0; i<p->args.n; i++) {
        res(p->args.v[i]);
    }
    res_patt(p->a);
    res_patt(p->b);
}

// An await is only valid lexically inside a task body.
static void chk_await (Tk tk, const char* what) {
    if (!PROTO->task) {
        err(tk, "invalid %s : expected enclosing task", what);
    }
    if (DEFERS > 0) {
        err(tk, "invalid %s : unexpected enclosing defer", what);
    }
}

// `func f () {...}` declares `f` at the top level if it is unknown
// where it appears. All such names are declared ahead, so functions
// may refer to each other regardless of their order.
// This only holds outside functions and tasks. Inside one, the
// declaration takes `val`, or else it assigns a variable that must
// exist: an unknown name is an error.
static int HPROTO = 0;
static void hoist (Expr* e);

static void hoist_list (List* l) {
    for (int i=0; i<l->n; i++) {
        hoist(l->v[i]);
    }
}

static void hoist (Expr* e) {
    if (e == NULL) {
        return;
    }
    switch (e->tag) {
        case E_INDEX:
            hoist(e->u.index.t);
            hoist(e->u.index.idx);
            break;
        case E_TABLE:
            hoist_list(&e->u.table.ks);
            hoist_list(&e->u.table.vs);
            break;
        case E_TAGGED:
            hoist(e->u.tagged.e);
            break;
        case E_UNO:
            hoist(e->u.uno.e);
            break;
        case E_BIN:
            hoist(e->u.bin.e1);
            hoist(e->u.bin.e2);
            break;
        case E_CALL:
            hoist(e->u.call.f);
            hoist_list(&e->u.call.es);
            break;
        case E_MET:
            hoist(e->u.met.o);
            break;
        case E_PARENS:
            hoist(e->u.parens.e);
            break;
        case E_LIST:
            hoist_list(&e->u.list.es);
            break;
        case E_WHERE:
            hoist_list(&e->u.where.dcls);
            hoist(e->u.where.e);
            break;
        case E_DCL:
            hoist(e->u.dcl.set);
            break;
        case E_SET: {
            Expr* dst = e->u.set.dsts.v[0];
            if (e->u.set.proto && dst->tag==E_ACC &&
                lookup(dst->tk.str)==NULL)
            {
                // inside a function, nothing is declared ahead
                if (HPROTO == 0) {
                    declare(dst->tk, V_VAR);
                }
            }
            hoist_list(&e->u.set.dsts);
            hoist(e->u.set.src);
            break;
        }
        case E_BLOCK:
            hoist_list(&e->u.block.es);
            break;
        case E_DO:
            hoist(e->u.xdo.blk);
            break;
        case E_DEFER:
            hoist(e->u.defer.blk);
            break;
        case E_IFS:
            hoist(e->u.ifs.head);
            for (int i=0; i<e->u.ifs.n; i++) {
                hoist(e->u.ifs.cases[i].cnd);
                hoist(e->u.ifs.cases[i].body);
            }
            break;
        case E_LOOP:
            hoist(e->u.loop.itr);
            hoist(e->u.loop.blk);
            break;
        case E_CATCH:
            hoist(e->u.xcatch.cnd);
            hoist(e->u.xcatch.blk);
            break;
        case E_PROTO:
            HPROTO++;
            hoist(e->u.proto.blk);
            HPROTO--;
            break;
        case E_SPAWN:
            hoist(e->u.spawn.f);
            hoist_list(&e->u.spawn.args);
            break;
        case E_SPAWN_BLK:
            hoist(e->u.sblk.blk);
            break;
        case E_TOGGLE_ON:
            hoist(e->u.toggle_on.blk);
            break;
        case E_PAR:
            hoist_list(&e->u.par.blks);
            break;
        default:
            break;
    }
}

static Special special (Expr* e) {
    if (e->tag != E_ACC) {
        return SP_NONE;
    }
    for (int i=1; SPECIALS[i]!=NULL; i++) {
        if (!strcmp(SPECIALS[i], e->tk.str)) {
            return (Special) i;
        }
    }
    return SP_NONE;
}

static void res_list (List* l) {
    for (int i=0; i<l->n; i++) {
        res(l->v[i]);
    }
}

// The expression about to be resolved is an item of a block. Only
// there a statement with no value is valid: a declaration, an
// assignment, or a defer.
static int STMT = 0;

static void res_stmts (List* l) {
    for (int i=0; i<l->n; i++) {
        STMT = 1;
        res(l->v[i]);
    }
    STMT = 0;
}

static void res_block (Expr* e) {
    scope_push();
    res_stmts(&e->u.block.es);
    scope_pop();
}

///////////////////////////////////////////////////////////////////////////////

static void res (Expr* e) {
    int stmt = STMT;
    STMT = 0;
    if (e == NULL) {
        return;
    }
    if (!stmt) {
        const char* what = NULL;
        switch (e->tag) {
            case E_DCL:
                what = "declaration";
                break;
            case E_SET:
                what = e->u.set.proto ? "declaration" : "assignment";
                break;
            case E_DEFER:
                what = "defer";
                break;
            default:
                break;
        }
        if (what != NULL) {
            err(e->tk, "invalid %s : expected statement", what);
        }
    }
    switch (e->tag) {
        case E_NIL:
        case E_BOOL:
        case E_NUM:
        case E_STR:
        case E_TAG:
        case E_CLK:
            break;

        case E_NAT: {
            // `$id` refers to a variable
            const char* s;
            NatKind kind = nat_kind(e->tk.str, &s);
            if (kind == NAT_ERR) {
                err(e->tk, "invalid native : unknown type");
            }
            int n = 0;
            for (const char* p=s; *p!='\0'; p++) {
                n += (p[0]=='$' && nat_id(p+1)>0);
            }
            if (kind==NAT_PRE && n>0) {
                err(e->tk, "invalid native : unexpected variable");
            }
            e->vars = xalloc((n+1) * sizeof(Var*));
            n = 0;
            for (const char* p=s; *p!='\0'; p++) {
                int len = nat_id(p+1);
                if (p[0]!='$' || len==0) {
                    continue;
                }
                char* id = xalloc(len + 1);
                memcpy(id, p+1, len);
                id[len] = '\0';
                Var* v = lookup(id);
                if (v == NULL) {
                    err(e->tk, "undeclared variable");
                }
                capture(v);
                e->vars[n++] = v;
                p += len;
            }
            break;
        }

        case E_DOTS:
            if (!PROTO->dots) {
                err(e->tk, "invalid '...' : expected variadic function");
            }
            break;

        case E_ACC:
            if (!strcmp(e->tk.str, "pub")) {
                e->tag = E_PUB;
                break;
            }
            if (special(e) != SP_NONE) {
                err(e->tk, "expected call syntax");
            }
            e->var = lookup(e->tk.str);
            if (e->var == NULL) {
                err(e->tk, "undeclared variable");
            }
            capture(e->var);
            break;

        case E_INDEX:
            res(e->u.index.t);
            res(e->u.index.idx);
            break;

        case E_TABLE:
            for (int i=0; i<e->u.table.vs.n; i++) {
                res(e->u.table.ks.v[i]);
                res(e->u.table.vs.v[i]);
            }
            break;

        case E_TAGGED:
            res(e->u.tagged.e);
            break;

        case E_UNO:
            res(e->u.uno.e);
            break;

        case E_BIN:
            res(e->u.bin.e1);
            res(e->u.bin.e2);
            break;

        case E_CALL:
            e->u.call.sp = special(e->u.call.f);
            switch (e->u.call.sp) {
                case SP_NONE:
                    res(e->u.call.f);
                    break;
                case SP_BREAK:
                case SP_UNTIL:
                case SP_WHILE:
                    if (LOOPS == 0) {
                        err(e->u.call.f->tk,
                            "invalid %s : expected enclosing loop",
                            e->u.call.f->tk.str);
                    }
                    break;
                default:
                    break;
            }
            res_list(&e->u.call.es);
            break;

        case E_MET:
            res(e->u.met.o);
            break;

        case E_PARENS:
            res(e->u.parens.e);
            break;

        case E_LIST:
            res_list(&e->u.list.es);
            break;

        case E_WHERE:
            // locals are visible in the clause and in the expression
            scope_push();
            res_stmts(&e->u.where.dcls);
            res(e->u.where.e);
            scope_pop();
            break;

        case E_DCL: {
            if (e->u.dcl.rec) {
                // `val func f`: the body sees `f`, to call itself
                e->vars = xalloc(sizeof(Var*));
                e->vars[0] = declare(e->u.dcl.ids[0], V_VAL);
                SELF = e->vars[0];
                res(e->u.dcl.set);
                break;
            }
            // the initialization does not see the new variables
            res(e->u.dcl.set);
            VarKind kind = !strcmp(e->tk.str,"var") ? V_VAR :
                           !strcmp(e->tk.str,"pin") ? V_PIN : V_VAL;
            e->vars = xalloc(e->u.dcl.n * sizeof(Var*));
            for (int i=0; i<e->u.dcl.n; i++) {
                e->vars[i] = declare(e->u.dcl.ids[i], kind);
            }
            break;
        }

        case E_SET:
            res(e->u.set.src);
            for (int i=0; i<e->u.set.dsts.n; i++) {
                Expr* dst = e->u.set.dsts.v[i];
                if (e->u.set.proto && dst->tag==E_ACC &&
                    lookup(dst->tk.str)==NULL)
                {
                    // `func f` inside a function, with `f` unknown: a
                    // global would be shared by all of its calls
                    err(dst->tk, "invalid declaration : expected 'val'");
                }
                res(dst);
                if (dst->tag == E_PUB) {
                    continue;
                }
                if (dst->tag==E_ACC && dst->var->kind!=V_VAR) {
                    err(dst->tk, "invalid set : expected mutable variable");
                }
            }
            break;

        case E_BLOCK:
            res_block(e);
            break;

        case E_DO:
            res_block(e->u.xdo.blk);
            break;

        case E_DEFER: {
            // a loop does not reach into a defer
            int loops = LOOPS;
            LOOPS = 0;
            DEFERS++;
            res_block(e->u.defer.blk);
            DEFERS--;
            LOOPS = loops;
            break;
        }

        case E_IFS:
            res(e->u.ifs.head);
            for (int i=0; i<e->u.ifs.n; i++) {
                Case* c = &e->u.ifs.cases[i];
                // lambdas in cases are expanded in place
                if (c->test) {
                    c->cnd->u.proto.inl = 1;
                }
                // (a variadic one is called, to bind its `...`)
                if (c->body->tag==E_PROTO && !c->body->u.proto.dots) {
                    c->body->u.proto.inl = 1;
                }
                res(c->cnd);
                res(c->body);
            }
            break;

        case E_LOOP:
            res(e->u.loop.itr);
            scope_push();
            e->vars = xalloc(e->u.loop.n * sizeof(Var*));
            for (int i=0; i<e->u.loop.n; i++) {
                e->vars[i] = declare(e->u.loop.ids[i], V_VAL);
            }
            LOOPS++;
            res_block(e->u.loop.blk);
            LOOPS--;
            scope_pop();
            break;

        case E_CATCH:
            res(e->u.xcatch.cnd);
            res_block(e->u.xcatch.blk);
            break;

        case E_ME:
        case E_PUB:
            break;

        case E_SPAWN:
            res(e->u.spawn.pool);
            res(e->u.spawn.f);
            res_list(&e->u.spawn.args);
            break;

        case E_TASKS:
            res(e->u.tasks.n);
            break;

        case E_EMIT:
            res(e->u.emit.to);
            res(e->u.emit.e);
            break;

        case E_AWAIT:
            chk_await(e->tk, "await");
            res_patt(e->u.await.p);
            break;

        case E_SPAWN_BLK:
            // a transparent block shares the frame of its task
            chk_await(e->tk, "spawn");
            res_block(e->u.sblk.blk);
            break;

        case E_PAR:
            chk_await(e->tk, "par");
            for (int i=0; i<e->u.par.blks.n; i++) {
                res_block(e->u.par.blks.v[i]);
            }
            break;

        case E_TOGGLE:
            res(e->u.toggle.t);
            res_list(&e->u.toggle.args);
            for (int i=0; i<e->u.toggle.nf; i++) {
                res_patt(e->u.toggle.fs[i]);
            }
            break;

        case E_TOGGLE_ON:
            chk_await(e->tk, "toggle");
            for (int i=0; i<e->u.toggle_on.nf; i++) {
                res_patt(e->u.toggle_on.fs[i]);
            }
            res_block(e->u.toggle_on.blk);
            break;

        case E_PROTO: {
            // a loop does not reach into a function
            int loops  = LOOPS;
            int defers = DEFERS;
            if (!e->u.proto.inl) {
                LOOPS  = 0;
                DEFERS = 0;
                e->proto = proto_new(e->u.proto.dots, e->u.proto.task);
                PROTO = e->proto;
            }
            scope_push();
            e->vars = xalloc(e->u.proto.n * sizeof(Var*));
            for (int i=0; i<e->u.proto.n; i++) {
                e->vars[i] = declare(e->u.proto.pars[i], V_PAR);
            }
            res_block(e->u.proto.blk);
            scope_pop();
            if (!e->u.proto.inl) {
                PROTO = PROTO->up;
            }
            LOOPS  = loops;
            DEFERS = defers;
            break;
        }
    }
}

Var** resolver_main (Expr* prog, int* n) {
    // runtime globals
    scope_push();
    for (int i=0; BUILTINS[i][0]!=NULL; i++) {
        Tk tk;
        memset(&tk, 0, sizeof(tk));
        tk.tag = TK_ID;
        tk.str = BUILTINS[i][0];
        declare(tk, V_VAL)->csym = BUILTINS[i][1];
    }

    // main block: its variables are static
    PROTO_MAIN = proto_new(1, 1);
    PROTO = PROTO_MAIN;
    scope_push();
    hoist(prog);
    res_stmts(&prog->u.block.es);
    scope_pop();
    scope_pop();

    *n = NGLOBALS;
    return GLOBALS;
}
