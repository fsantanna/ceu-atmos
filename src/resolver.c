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

// Top-level variables of the main chunk.
static Var** GLOBALS  = NULL;
static int   NGLOBALS = 0;

// Global variables provided by the runtime.
static const char* BUILTINS[][2] = {
    { "print", "atm_g_print" },
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
    Var* v = xalloc(sizeof(Var));
    v->tk   = tk;
    v->kind = kind;
    v->id   = ++N;
    v->slot = -1;
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

static void res_block (Expr* e) {
    scope_push();
    res_list(&e->u.block.es);
    scope_pop();
}

///////////////////////////////////////////////////////////////////////////////

static void res (Expr* e) {
    if (e == NULL) {
        return;
    }
    switch (e->tag) {
        case E_NIL:
        case E_BOOL:
        case E_NUM:
        case E_STR:
        case E_TAG:
        case E_NAT:
        case E_CLK:
        case E_DOTS:
        case E_ME:
            break;

        case E_ACC:
            if (special(e) != SP_NONE) {
                err(e->tk, "expected call syntax");
            }
            e->var = lookup(e->tk.str);
            if (e->var == NULL) {
                err(e->tk, "undeclared variable");
            }
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
            if (e->u.call.sp == SP_NONE) {
                res(e->u.call.f);
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
            res_list(&e->u.where.dcls);
            res(e->u.where.e);
            scope_pop();
            break;

        case E_DCL: {
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
                res(dst);
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

        case E_DEFER:
            res_block(e->u.defer.blk);
            break;

        case E_IFS:
            res(e->u.ifs.head);
            for (int i=0; i<e->u.ifs.n; i++) {
                res(e->u.ifs.cases[i].cnd);
                res(e->u.ifs.cases[i].body);
            }
            break;

        case E_LOOP:
            res(e->u.loop.itr);
            scope_push();
            e->vars = xalloc(e->u.loop.n * sizeof(Var*));
            for (int i=0; i<e->u.loop.n; i++) {
                e->vars[i] = declare(e->u.loop.ids[i], V_VAL);
            }
            res_block(e->u.loop.blk);
            scope_pop();
            break;

        case E_CATCH:
            res(e->u.xcatch.cnd);
            res_block(e->u.xcatch.blk);
            break;

        case E_PROTO:
            scope_push();
            e->vars = xalloc(e->u.proto.n * sizeof(Var*));
            for (int i=0; i<e->u.proto.n; i++) {
                e->vars[i] = declare(e->u.proto.pars[i], V_PAR);
            }
            res_block(e->u.proto.blk);
            scope_pop();
            break;
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
    res_block(prog);
    scope_pop();

    *n = NGLOBALS;
    return GLOBALS;
}
