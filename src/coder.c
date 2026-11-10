// Atmos compiler: C code generation.
//
// A construct is compiled against a destination: nowhere (effects
// only), a slot (one value), or the value stack (all its values, with
// the count in a C variable).
//
// A simple expression is compiled to an operand: either a borrowed C
// expression (constant, immutable variable), or an owned temporary
// slot `L[n]`. A temporary is released or moved by whoever consumes
// it.
//
// Invariant: when an expression starts to evaluate, its function has
// no pending values on the stack. Arguments are evaluated into slots
// and only then pushed, so an exit can always unwind to `base`.
//
// Failures and non-local exits set `st` and jump to the cleanup label
// of the enclosing block, which releases its slots and propagates the
// status. The values of an exit travel in the payload.

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "atmc.h"

///////////////////////////////////////////////////////////////////////////////
// OUTPUT
///////////////////////////////////////////////////////////////////////////////

typedef struct Buf {
    char*  s;
    size_t n;
    size_t cap;
} Buf;

static void buf_add (Buf* b, const char* s) {
    size_t n = strlen(s);
    if (b->n + n + 1 > b->cap) {
        b->cap = (b->n + n + 1) * 2;
        b->s = realloc(b->s, b->cap);
    }
    memcpy(b->s + b->n, s, n + 1);
    b->n += n;
}

// One C function being generated.
typedef struct Unit {
    Buf body;
    // function being compiled, and its number of parameters
    Proto* proto;
    int npar;
    // task: resumable, with the slots in its frame
    int task;
    // transparent block: shares the slots of the unit of its task
    struct Unit* root;
    // number of resumption points
    int npcs;
    // number of slots in `L`
    int nslots;
    // number of count variables `n<i>`
    int ncnts;
    // number of cleanup labels
    int nlabels;
    // cleanup label of the enclosing block
    int cleanup;
    // extra indentation of the generated lines
    int ind;
    // finalizers of the enclosing block
    struct Defer* defs;
    int ndefs;
} Unit;

// Finalizer of a block: a defer (flag slot and body), or a pin (the
// storage of the pinned task or pool).
typedef struct Defer {
    int   flag;
    Expr* e;
    const char* pin;
} Defer;

// Appends one line at column zero (labels).
static void out0 (Unit* u, const char* fmt, ...) {
    char line[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    buf_add(&u->body, line);
    buf_add(&u->body, "\n");
}

// Appends one line to the body of the unit.
static void out (Unit* u, const char* fmt, ...) {
    char line[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    for (int i=0; i<=u->ind; i++) {
        buf_add(&u->body, "    ");
    }
    buf_add(&u->body, line);
    buf_add(&u->body, "\n");
}

// Emits a runtime call that may fail.
static void out_try (Unit* u, const char* fmt, ...) {
    char call[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(call, sizeof(call), fmt, ap);
    va_end(ap);
    out(u, "if ((st = %s) != A_OK) goto CL_%d;", call, u->cleanup);
}

// Slot counter of a unit.
static int* nslots (Unit* u) {
    return (u->root != NULL) ? &u->root->nslots : &u->nslots;
}

static int slot_new (Unit* u) {
    return (*nslots(u))++;
}

///////////////////////////////////////////////////////////////////////////////
// CONSTANTS
///////////////////////////////////////////////////////////////////////////////

// String constants of the program, created once at startup as `K[i]`.

typedef struct Const {
    const char* s;
    int         len;
} Const;

static Const* KS = NULL;
static int    KN = 0;

static int konst (const char* s, int len) {
    for (int i=0; i<KN; i++) {
        if (KS[i].len==len && !memcmp(KS[i].s,s,len)) {
            return i;
        }
    }
    KS = realloc(KS, (KN+1) * sizeof(Const));
    KS[KN].s   = s;
    KS[KN].len = len;
    return KN++;
}

// Decodes the escapes of a single-line string literal, as in Lua.
static char* decode (Tk tk, int* len) {
    char* ret = xalloc(tk.len*4 + 1);
    int n = 0;
    for (int i=0; i<tk.len; i++) {
        int c = (unsigned char) tk.str[i];
        if (c != '\\') {
            ret[n++] = c;
            continue;
        }
        c = (unsigned char) tk.str[++i];
        switch (c) {
            case 'a':  ret[n++] = '\a'; break;
            case 'b':  ret[n++] = '\b'; break;
            case 'f':  ret[n++] = '\f'; break;
            case 'n':  ret[n++] = '\n'; break;
            case 'r':  ret[n++] = '\r'; break;
            case 't':  ret[n++] = '\t'; break;
            case 'v':  ret[n++] = '\v'; break;
            case '\\': ret[n++] = '\\'; break;
            case '"':  ret[n++] = '"';  break;
            case '\'': ret[n++] = '\''; break;
            case 'x': {
                // \xHH
                int v = 0;
                for (int j=0; j<2; j++) {
                    int d = (unsigned char) tk.str[++i];
                    if (d>='0' && d<='9') {
                        v = v*16 + (d - '0');
                    } else if (d>='a' && d<='f') {
                        v = v*16 + (d - 'a' + 10);
                    } else if (d>='A' && d<='F') {
                        v = v*16 + (d - 'A' + 10);
                    } else {
                        err(tk, "invalid escape sequence");
                    }
                }
                ret[n++] = v;
                break;
            }
            case 'z':
                // \z skips the following spaces
                while (tk.str[i+1]==' ' || tk.str[i+1]=='\t') {
                    i++;
                }
                break;
            default:
                if (c>='0' && c<='9') {
                    // \ddd
                    int v = c - '0';
                    for (int j=0; j<2; j++) {
                        int d = (unsigned char) tk.str[i+1];
                        if (d<'0' || d>'9') {
                            break;
                        }
                        v = v*10 + (d - '0');
                        i++;
                    }
                    if (v > 255) {
                        err(tk, "invalid escape sequence");
                    }
                    ret[n++] = v;
                } else {
                    err(tk, "invalid escape sequence");
                }
                break;
        }
    }
    ret[n] = '\0';
    *len = n;
    return ret;
}

// C literal for arbitrary bytes.
static char* quote (const char* s, int len) {
    char* ret = xalloc(len*4 + 3);
    int n = 0;
    ret[n++] = '"';
    for (int i=0; i<len; i++) {
        int c = (unsigned char) s[i];
        if (c>=32 && c<127 && c!='"' && c!='\\' && c!='?') {
            ret[n++] = c;
        } else {
            n += sprintf(&ret[n], "\\%03o", c);
        }
    }
    ret[n++] = '"';
    ret[n] = '\0';
    return ret;
}

///////////////////////////////////////////////////////////////////////////////
// OPERANDS
///////////////////////////////////////////////////////////////////////////////

typedef struct Opnd {
    // owned temporary slot, or -1
    int tmp;
    // C expression of the value
    const char* c;
} Opnd;

static Opnd opnd_tmp (Unit* u) {
    Opnd o;
    o.tmp = slot_new(u);
    o.c   = xfmt("L[%d]", o.tmp);
    return o;
}

static Opnd opnd_c (const char* c) {
    Opnd o;
    o.tmp = -1;
    o.c   = c;
    return o;
}

// Releases a consumed operand.
static void opnd_rel (Unit* u, Opnd o) {
    if (o.tmp >= 0) {
        out(u, "a_clr(&L[%d]);", o.tmp);
    }
}

// Stores an operand into a slot, consuming it.
static void opnd_mov (Unit* u, Opnd o, const char* dst) {
    if (o.tmp >= 0) {
        out(u, "a_mov(&%s, &L[%d]);", dst, o.tmp);
    } else {
        out(u, "a_cpy(&%s, %s);", dst, o.c);
    }
}

// Pushes an operand onto the value stack.
static void opnd_push (Unit* u, Opnd o) {
    if (o.tmp >= 0) {
        out(u, "a_pushm(&L[%d]);", o.tmp);
    } else {
        out(u, "a_pushc(%s);", o.c);
    }
}

///////////////////////////////////////////////////////////////////////////////
// DESTINATIONS
///////////////////////////////////////////////////////////////////////////////

typedef enum {
    D_NONE, D_SLOT, D_MULTI
} DKind;

typedef struct Dst {
    DKind kind;
    // D_SLOT: C expression of the slot
    const char* slot;
    // D_MULTI: C variable that receives the number of values
    const char* n;
} Dst;

static Dst dst_none (void) {
    Dst d = { D_NONE, NULL, NULL };
    return d;
}

static Dst dst_slot (const char* slot) {
    Dst d = { D_SLOT, slot, NULL };
    return d;
}

static Dst dst_multi (Unit* u) {
    Dst d = { D_MULTI, NULL, xfmt("n%d", u->ncnts++) };
    return d;
}

// Delivers an operand, consuming it.
static void store (Unit* u, Opnd o, Dst d) {
    switch (d.kind) {
        case D_NONE:
            opnd_rel(u, o);
            break;
        case D_SLOT:
            opnd_mov(u, o, d.slot);
            break;
        case D_MULTI:
            opnd_push(u, o);
            out(u, "%s = 1;", d.n);
            break;
    }
}

// Delivers no value: nil for a slot, nothing for the stack.
static void store_void (Unit* u, Dst d) {
    switch (d.kind) {
        case D_NONE:
            break;
        case D_SLOT:
            out(u, "a_clr(&%s);", d.slot);
            break;
        case D_MULTI:
            out(u, "%s = 0;", d.n);
            break;
    }
}

// Delivers the payload of an exit.
static void store_pay (Unit* u, Dst d) {
    switch (d.kind) {
        case D_NONE:
            out(u, "a_pay_clear();");
            break;
        case D_SLOT:
            out(u, "a_pay_take(&%s, 0);", d.slot);
            out(u, "a_pay_clear();");
            break;
        case D_MULTI:
            out(u, "%s = a_pay_push();", d.n);
            break;
    }
}

///////////////////////////////////////////////////////////////////////////////
// EXPRESSIONS
///////////////////////////////////////////////////////////////////////////////

static void gen       (Unit* u, Expr* e, Dst d);
static Opnd gen_expr  (Unit* u, Expr* e);
static void gen_block (Unit* u, List* es, Dst d);

// C expression of the raw storage of a variable: a static variable,
// a slot of its function, or a capture of the current function.
// For a boxed variable, this is the box.
static const char* var_raw (Unit* u, Var* v) {
    if (v->csym != NULL) {
        return v->csym;
    } else if (v->global) {
        return xfmt("G%d_%s", v->id, v->tk.str);
    } else if (v->proto == u->proto) {
        if (v->slot < 0) {
            v->slot = slot_new(u);
        }
        return xfmt("L[%d]", v->slot);
    } else {
        for (int i=0; i<u->proto->ncaps; i++) {
            if (u->proto->caps[i] == v) {
                return xfmt("self->caps[%d]", i);
            }
        }
        err(v->tk, "internal error : capture not found");
        return NULL;
    }
}

// C expression of the value of a variable.
static const char* var_c (Unit* u, Var* v) {
    const char* raw = var_raw(u, v);
    return v->boxed ? xfmt("A_BOX_OF(%s)->v", raw) : raw;
}

// Appends the expressions in `src` to `dst`, splicing nested lists:
// f((a,b),c) is f(a,b,c).
static void flatten (List* dst, List* src) {
    for (int i=0; i<src->n; i++) {
        Expr* e = src->v[i];
        if (e->tag == E_LIST) {
            flatten(dst, &e->u.list.es);
        } else {
            list_add(dst, e);
        }
    }
}

// Expressions that may yield a number of values other than one.
static int is_multi (Expr* e) {
    switch (e->tag) {
        case E_CALL:
        case E_DOTS:
        case E_LIST:
        case E_BLOCK:
        case E_DO:
        case E_IFS:
        case E_LOOP:
        case E_CATCH:
        case E_WHERE:
        case E_AWAIT:
        case E_PAR:
            return 1;
        default:
            return 0;
    }
}

// Pushes the values of a list of expressions: one of each, except the
// last, which gives all of its values. Sets the count in `n`.
// An optional operand `self` goes first (method calls).
static void gen_args (Unit* u, List* es, const char* n, Opnd* self) {
    List args = { 0, NULL };
    flatten(&args, es);
    int k = args.n;

    int multi = (k > 0) && is_multi(args.v[k-1]);
    int fix = multi ? k-1 : k;
    Opnd* xs = xalloc((k+1) * sizeof(Opnd));
    int nx = 0;
    if (self != NULL) {
        xs[nx++] = *self;
    }
    for (int i=0; i<fix; i++) {
        xs[nx++] = gen_expr(u, args.v[i]);
    }

    if (!multi) {
        for (int i=0; i<nx; i++) {
            opnd_push(u, xs[i]);
        }
        out(u, "%s = %d;", n, nx);
        return;
    }

    // the last expression may suspend or fail: the others wait in
    // slots and are inserted below its values
    Dst d = { D_MULTI, NULL, n };
    gen(u, args.v[k-1], d);
    if (nx > 0) {
        out(u, "a_open(%s, %d);", n, nx);
        for (int i=0; i<nx; i++) {
            if (xs[i].tmp >= 0) {
                out(u, "a_putm(a_top-%s-%d, &L[%d]);", n, nx-i, xs[i].tmp);
            } else {
                out(u, "a_putc(a_top-%s-%d, %s);", n, nx-i, xs[i].c);
            }
        }
        out(u, "%s += %d;", n, nx);
    }
}

// An immutable variable is borrowed in place. A mutable one is
// copied, since it may change before the value is consumed.
static Opnd gen_acc (Unit* u, Expr* e) {
    Var* v = e->var;
    if (v->kind != V_VAR) {
        return opnd_c(var_c(u, v));
    } else {
        Opnd d = opnd_tmp(u);
        out(u, "a_cpy(&%s, %s);", d.c, var_c(u,v));
        return d;
    }
}

static Opnd gen_uno (Unit* u, Expr* e) {
    Opnd x = gen_expr(u, e->u.uno.e);
    Opnd d = opnd_tmp(u);
    if (!strcmp(e->tk.str, "-")) {
        out_try(u, "a_neg(&%s, %s)", d.c, x.c);
    } else if (!strcmp(e->tk.str, "#")) {
        out_try(u, "a_len(&%s, %s)", d.c, x.c);
    } else {
        out(u, "a_not(&%s, %s);", d.c, x.c);
    }
    opnd_rel(u, x);
    return d;
}

// a && b, a || b: the second operand is only evaluated if needed, and
// the result is the value of the deciding operand.
static Opnd gen_logic (Unit* u, Expr* e) {
    int and = !strcmp(e->tk.str, "&&");
    Opnd d = opnd_tmp(u);
    opnd_mov(u, gen_expr(u,e->u.bin.e1), d.c);
    out(u, "if (%sa_truthy(%s)) {", and ? "" : "!", d.c);
    u->ind++;
    opnd_mov(u, gen_expr(u,e->u.bin.e2), d.c);
    u->ind--;
    out(u, "}");
    return d;
}

static Opnd gen_bin (Unit* u, Expr* e) {
    // operator, runtime function, swapped operands
    static const char* OPS[][3] = {
        { "+",  "a_add",  "" }, { "-",  "a_sub", ""  },
        { "*",  "a_mul",  "" }, { "/",  "a_div", ""  },
        { "//", "a_idiv", "" }, { "%",  "a_mod", ""  },
        { "++", "a_cat",  "" },
        { "<",  "a_lt",   "" }, { "<=", "a_le",  ""  },
        { ">",  "a_lt",  "x" }, { ">=", "a_le",  "x" },
        { NULL, NULL, NULL }
    };
    // operator, predicate, negated, swapped operands
    static const char* PREDS[][4] = {
        { "==",  "a_eq",  "",  ""  }, { "!=",  "a_eq",  "!", ""  },
        { "??",  "a_is",  "",  ""  }, { "!?",  "a_is",  "!", ""  },
        { NULL, NULL, NULL, NULL }
    };
    const char* op = e->tk.str;

    if (!strcmp(op,"&&") || !strcmp(op,"||")) {
        return gen_logic(u, e);
    }

    Opnd x = gen_expr(u, e->u.bin.e1);
    Opnd y = gen_expr(u, e->u.bin.e2);
    Opnd d = opnd_tmp(u);

    int done = 0;
    for (int i=0; PREDS[i][0]!=NULL; i++) {
        if (!strcmp(PREDS[i][0], op)) {
            int swap = (PREDS[i][3][0] != '\0');
            out(u, "a_set(&%s, a_bool(%s%s(%s, %s)));",
                d.c, PREDS[i][2], PREDS[i][1],
                swap ? y.c : x.c, swap ? x.c : y.c);
            done = 1;
        }
    }
    for (int i=0; OPS[i][0]!=NULL; i++) {
        if (!strcmp(OPS[i][0], op)) {
            int swap = (OPS[i][2][0] != '\0');
            out_try(u, "%s(&%s, %s, %s)",
                OPS[i][1], d.c, swap ? y.c : x.c, swap ? x.c : y.c);
            done = 1;
        }
    }
    if (!done) {
        err(e->tk, "operator not implemented");
    }

    opnd_rel(u, x);
    opnd_rel(u, y);
    return d;
}

// [ e, id=e, @(k)=e ]: positional entries are numbered from 0, and
// the last entry, if positional, gives all of its values.
static Opnd gen_table (Unit* u, Expr* e) {
    Opnd d = opnd_tmp(u);
    out(u, "a_set(&%s, a_tab());", d.c);
    int n = 0;
    int last = e->u.table.vs.n - 1;
    for (int i=0; i<=last; i++) {
        Expr* key = e->u.table.ks.v[i];
        Expr* val = e->u.table.vs.v[i];
        if (key==NULL && i==last && is_multi(val)) {
            Dst m = dst_multi(u);
            gen(u, val, m);
            out(u, "a_tab_fill(%s, %d, %s);", d.c, n, m.n);
        } else if (key == NULL) {
            Opnd v = gen_expr(u, val);
            out(u, "a_tab_set(A_TAB_OF(%s), a_num(%d), %s);", d.c, n++, v.c);
            opnd_rel(u, v);
        } else {
            Opnd k = gen_expr(u, key);
            Opnd v = gen_expr(u, val);
            out_try(u, "a_setindex(%s, %s, %s)", d.c, k.c, v.c);
            opnd_rel(u, k);
            opnd_rel(u, v);
        }
    }
    return d;
}

// :X [...]: tags the table in place.
static Opnd gen_tagged (Unit* u, Expr* e) {
    Opnd t = gen_expr(u, e->u.tagged.e);
    out_try(u, "a_tag(%s, K[%d])", t.c, konst(e->tk.str+1,e->tk.len-1));
    return t;
}

// t@(e)  t.x  t@#  t@+
static Opnd gen_index (Unit* u, Expr* e) {
    Opnd t = gen_expr(u, e->u.index.t);
    Opnd d = opnd_tmp(u);
    switch (e->u.index.kind) {
        case IDX_EXPR: {
            Opnd k = gen_expr(u, e->u.index.idx);
            out_try(u, "a_index(&%s, %s, %s)", d.c, t.c, k.c);
            opnd_rel(u, k);
            break;
        }
        case IDX_LAST:
            out_try(u, "a_index_last(&%s, %s)", d.c, t.c);
            break;
        case IDX_NEXT:
            // the next position is always empty
            out_try(u, "a_len(&%s, %s)", d.c, t.c);
            out(u, "a_clr(&%s);", d.c);
            break;
    }
    opnd_rel(u, t);
    return d;
}

///////////////////////////////////////////////////////////////////////////////
// BLOCKS
///////////////////////////////////////////////////////////////////////////////

// Sequence of expressions in a new block, whose value is the value of
// the last expression.
//
// A block may stop an exit: its payload is delivered to a destination
// and control goes to an end label.
//  - break: the body of a loop
//  - return: the body of a lambda expanded in place
//  - error: the body of a catch, which only diverts to its label
//  - escape: a `do :X` block, if the tag matches constant `esck`
//  - join: a `par :any`, identified by slot `joink`
typedef struct Hnd {
    int brk;
    Dst brkd;
    int ret;
    Dst retd;
    int err;
    int esc;
    int esck;
    Dst escd;
    int join;
    int joink;
    Dst joind;
} Hnd;

static Hnd hnd_none (void) {
    Hnd h;
    memset(&h, 0, sizeof(h));
    return h;
}

// An open block: its cleanup label, first slot, and what it suspends
// of the enclosing block.
typedef struct Blk {
    int up;
    int lo;
    int cl;
    Defer* defs;
    int ndefs;
} Blk;

static Blk blk_open (Unit* u) {
    Blk b;
    b.up = u->cleanup;
    b.lo = *nslots(u);
    b.cl = ++u->nlabels;
    b.defs  = u->defs;
    b.ndefs = u->ndefs;
    u->cleanup = b.cl;
    u->defs  = NULL;
    u->ndefs = 0;
    out(u, "{");
    u->ind++;
    return b;
}

// Closes a block: runs its finalizers, releases its slots, and then
// handles or propagates a pending exit.
static void blk_close (Unit* u, Blk b, Hnd h) {
    u->ind--;
    out0(u, "CL_%d:", b.cl);
    u->ind++;

    // pins are aborted and defers that were reached run, in reverse
    // order, whatever the exit. The pending exit is set aside
    // meanwhile, and replaced if a defer exits itself.
    Defer* mine = u->defs;
    int   nmine = u->ndefs;
    for (int i=nmine-1; i>=0; i--) {
        if (mine[i].pin != NULL) {
            out(u, "st = a_pin_end(&%s, st);", mine[i].pin);
            continue;
        }
        int id = ++u->nlabels;
        out(u, "if (a_truthy(L[%d])) {", mine[i].flag);
        u->ind++;
        out(u, "AStatus st_%d = st;", id);
        out(u, "APay pay_%d;", id);
        out(u, "a_pay_save(&pay_%d);", id);
        out(u, "st = A_OK;");
        out(u, "{");
        u->ind++;
        // values of the block may be pending on the stack
        out(u, "int base = a_top;");
        u->cleanup = id;
        gen_block(u, &mine[i].e->u.defer.blk->u.block.es, dst_none());
        u->ind--;
        out0(u, "CL_%d: ;", id);
        out(u, "}");
        out(u, "if (st == A_OK) {");
        out(u, "    st = st_%d;", id);
        out(u, "    a_pay_restore(&pay_%d);", id);
        out(u, "} else {");
        out(u, "    a_pay_drop(&pay_%d);", id);
        out(u, "}");
        u->ind--;
        out(u, "}");
    }
    u->cleanup = b.cl;
    u->defs  = b.defs;
    u->ndefs = b.ndefs;

    if (*nslots(u) > b.lo) {
        out(u, "a_clrs(&L[%d], %d);", b.lo, *nslots(u)-b.lo);
    }
    if (h.brk > 0) {
        out(u, "if (st == A_BREAK) {");
        u->ind++;
        out(u, "st = A_OK;");
        out(u, "a_settop(base);");
        store_pay(u, h.brkd);
        out(u, "goto LE_%d;", h.brk);
        u->ind--;
        out(u, "}");
    }
    if (h.esc > 0) {
        out(u, "if (st==A_ESCAPE && a_escape_is(K[%d])) {", h.esck);
        u->ind++;
        out(u, "st = A_OK;");
        out(u, "a_settop(base);");
        out(u, "a_escape_vals();");
        store_pay(u, h.escd);
        out(u, "goto LE_%d;", h.esc);
        u->ind--;
        out(u, "}");
    }
    if (h.ret > 0) {
        out(u, "if (st == A_RETURN) {");
        u->ind++;
        out(u, "st = A_OK;");
        out(u, "a_settop(base);");
        store_pay(u, h.retd);
        out(u, "goto LE_%d;", h.ret);
        u->ind--;
        out(u, "}");
    }
    if (h.join > 0) {
        out(u, "if (st==A_JOIN && a_join_key==&L[%d]) {", h.joink);
        u->ind++;
        out(u, "st = A_OK;");
        out(u, "a_settop(base);");
        store_pay(u, h.joind);
        out(u, "goto LE_%d;", h.join);
        u->ind--;
        out(u, "}");
    }
    out(u, "if (st != A_OK) {");
    out(u, "    a_settop(base);");
    if (h.err > 0) {
        out(u, "    if (st == A_ERROR) goto LC_%d;", h.err);
    }
    out(u, "    goto CL_%d;", b.up);
    out(u, "}");
    u->ind--;
    out(u, "}");

    u->cleanup = b.up;
}

static void gen_block_x (Unit* u, List* es, Dst d, Hnd h) {
    Blk b = blk_open(u);
    for (int i=0; i<es->n; i++) {
        // C diagnostics and debuggers point to the source line
        out0(u, "#line %d %s", es->v[i]->tk.lin,
            quote(FILENAME, strlen(FILENAME)));
        out(u, "a_lin = %d;", es->v[i]->tk.lin);
        gen(u, es->v[i], (i == es->n-1) ? d : dst_none());
    }
    if (es->n == 0) {
        store_void(u, d);
    }
    blk_close(u, b, h);
}

static void gen_block (Unit* u, List* es, Dst d) {
    gen_block_x(u, es, d, hnd_none());
}

// do :X {...}: stops a matching escape, whose values become the
// values of the block.
static void gen_do (Unit* u, Expr* e, Dst d) {
    List* es = &e->u.xdo.blk->u.block.es;
    if (!e->u.xdo.esc) {
        gen_block(u, es, d);
        return;
    }
    Hnd h = hnd_none();
    h.esc  = ++u->nlabels;
    h.esck = konst(e->tk.str+1, e->tk.len-1);
    h.escd = d;
    gen_block_x(u, es, d, h);
    out0(u, "LE_%d: ;", h.esc);
}

// defer {...}: registers the body to run when the block exits.
static void gen_defer (Unit* u, Expr* e) {
    int flag = slot_new(u);
    u->defs = realloc(u->defs, (u->ndefs+1) * sizeof(Defer));
    u->defs[u->ndefs].flag = flag;
    u->defs[u->ndefs].e    = e;
    u->defs[u->ndefs].pin  = NULL;
    u->ndefs++;
    out(u, "a_set(&L[%d], a_bool(1));", flag);
}

// The enclosing block aborts the task or pool in `slot` on exit.
static void gen_pin_end (Unit* u, const char* slot) {
    u->defs = realloc(u->defs, (u->ndefs+1) * sizeof(Defer));
    u->defs[u->ndefs].flag = -1;
    u->defs[u->ndefs].e    = NULL;
    u->defs[u->ndefs].pin  = slot;
    u->ndefs++;
}

// The value in `slot` becomes owned by the enclosing block, which
// aborts it on exit, if it is a task or a pool.
static void gen_pin (Unit* u, const char* slot) {
    out_try(u, "a_pin(&%s)", slot);
    gen_pin_end(u, slot);
}

// e where { x = e1 ; y = e2 }
static void gen_where (Unit* u, Expr* e, Dst d) {
    List es = { 0, NULL };
    for (int i=0; i<e->u.where.dcls.n; i++) {
        list_add(&es, e->u.where.dcls.v[i]);
    }
    list_add(&es, e->u.where.e);
    gen_block(u, &es, d);
}

///////////////////////////////////////////////////////////////////////////////
// DECLARATIONS AND ASSIGNMENTS
///////////////////////////////////////////////////////////////////////////////

// A captured mutable variable gets a fresh box every time its
// declaration executes.
static void gen_boxes (Unit* u, Expr* e) {
    for (int i=0; i<e->u.dcl.n; i++) {
        if (e->vars[i]->boxed) {
            out(u, "a_set(&%s, a_box());", var_raw(u,e->vars[i]));
        }
    }
}

// After a declaration: a pin takes ownership of a task or pool; a val
// or var only accepts one that is pinned elsewhere.
static void gen_dcl_chk (Unit* u, Expr* e) {
    for (int i=0; i<e->u.dcl.n; i++) {
        const char* v = var_c(u, e->vars[i]);
        if (e->vars[i]->kind == V_PIN) {
            gen_pin(u, v);
        } else {
            out_try(u, "a_chk(%s)", v);
        }
    }
}

// val x = e  |  var x, y = e  |  pin x = e
static void gen_dcl (Unit* u, Expr* e) {
    int k = e->u.dcl.n;
    const char** vs = xalloc(k * sizeof(char*));
    if (e->u.dcl.set == NULL) {
        for (int i=0; i<k; i++) {
            var_c(u, e->vars[i]);
        }
    } else if (k == 1) {
        Opnd o = gen_expr(u, e->u.dcl.set);
        gen_boxes(u, e);
        opnd_mov(u, o, var_c(u,e->vars[0]));
        gen_dcl_chk(u, e);
        return;
    } else {
        Dst m = dst_multi(u);
        gen(u, e->u.dcl.set, m);
        out(u, "a_adj(%s, %d);", m.n, k);
        gen_boxes(u, e);
        for (int i=0; i<k; i++) {
            vs[i] = var_c(u, e->vars[i]);
        }
        for (int i=k-1; i>=0; i--) {
            out(u, "a_pop(&%s);", vs[i]);
        }
        gen_dcl_chk(u, e);
        return;
    }
    gen_boxes(u, e);
}

// A destination of a set, with its table and key already evaluated.
typedef struct Loc {
    Expr* e;
    Opnd  t;
    Opnd  k;
} Loc;

static Loc gen_loc (Unit* u, Expr* dst) {
    Loc l;
    l.e = dst;
    l.t = opnd_c(NULL);
    l.k = opnd_c(NULL);
    if (dst->tag == E_INDEX) {
        l.t = gen_expr(u, dst->u.index.t);
        if (dst->u.index.kind == IDX_EXPR) {
            l.k = gen_expr(u, dst->u.index.idx);
        }
    } else if (dst->tag!=E_ACC && dst->tag!=E_PUB) {
        err(dst->tk, "assignment : not implemented");
    }
    return l;
}

// Assigns an operand to a destination, consuming both.
static void gen_assign (Unit* u, Loc l, Opnd v) {
    out_try(u, "a_chk(%s)", v.c);
    if (l.e->tag == E_ACC) {
        opnd_mov(u, v, var_c(u,l.e->var));
        return;
    }
    if (l.e->tag == E_PUB) {
        out(u, "{");
        out(u, "    AVal* pub;");
        out(u, "    if ((st = a_pub(&pub)) != A_OK) goto CL_%d;", u->cleanup);
        out(u, "    a_cpy(pub, %s);", v.c);
        out(u, "}");
        opnd_rel(u, v);
        return;
    }
    switch (l.e->u.index.kind) {
        case IDX_EXPR:
            out_try(u, "a_setindex(%s, %s, %s)", l.t.c, l.k.c, v.c);
            opnd_rel(u, l.k);
            break;
        case IDX_LAST:
            out_try(u, "a_setindex_last(%s, %s)", l.t.c, v.c);
            break;
        case IDX_NEXT:
            out_try(u, "a_setindex_next(%s, %s)", l.t.c, v.c);
            break;
    }
    opnd_rel(u, v);
    opnd_rel(u, l.t);
}

// set x = e  |  set t@k, y = e
// Tables and keys are evaluated before the values.
static void gen_set (Unit* u, Expr* e) {
    int k = e->u.set.dsts.n;
    Loc* ls = xalloc(k * sizeof(Loc));
    for (int i=0; i<k; i++) {
        ls[i] = gen_loc(u, e->u.set.dsts.v[i]);
    }
    if (k == 1) {
        gen_assign(u, ls[0], gen_expr(u,e->u.set.src));
    } else {
        Dst m = dst_multi(u);
        gen(u, e->u.set.src, m);
        out(u, "a_adj(%s, %d);", m.n, k);
        // all values are taken before the first assignment
        Opnd* vs = xalloc(k * sizeof(Opnd));
        for (int i=k-1; i>=0; i--) {
            vs[i] = opnd_tmp(u);
            out(u, "a_pop(&%s);", vs[i].c);
        }
        for (int i=0; i<k; i++) {
            gen_assign(u, ls[i], vs[i]);
        }
    }
}

///////////////////////////////////////////////////////////////////////////////
// CONDITIONALS
///////////////////////////////////////////////////////////////////////////////

// Lambda expanded in place: binds its first parameter to `arg` and
// evaluates its body as a block.
static void gen_inline (Unit* u, Expr* p, Opnd arg, Dst d) {
    if (p->u.proto.n > 0) {
        opnd_mov(u, arg, var_c(u,p->vars[0]));
    } else {
        opnd_rel(u, arg);
    }
    // a return inside the lambda gives its value
    int end = ++u->nlabels;
    Hnd h = hnd_none();
    h.ret  = end;
    h.retd = d;
    gen_block_x(u, &p->u.proto.blk->u.block.es, d, h);
    out0(u, "LE_%d: ;", end);
}

// if, ifs, match: a chain of tests. The body of the first case that
// holds gives the value, and receives the tested value if it is a
// lambda.
static void gen_ifs (Unit* u, Expr* e, Dst d) {
    Opnd h = opnd_c(NULL);
    if (e->u.ifs.head != NULL) {
        h = gen_expr(u, e->u.ifs.head);
    }

    for (int i=0; i<e->u.ifs.n; i++) {
        Case* cs = &e->u.ifs.cases[i];
        Opnd c;
        if (h.c == NULL) {
            // ifs: the condition itself
            c = (cs->cnd == NULL) ? opnd_c("a_bool(1)") :
                                    gen_expr(u, cs->cnd);
        } else if (cs->test) {
            // match: lambda applied to the head
            Opnd arg = opnd_tmp(u);
            out(u, "a_cpy(&%s, %s);", arg.c, h.c);
            c = opnd_tmp(u);
            gen_inline(u, cs->cnd, arg, dst_slot(c.c));
        } else {
            // match: the head, if it is the case value (or else)
            c = opnd_tmp(u);
            const char* v = xfmt("(a_truthy(%s) ? %s : a_bool(1))", h.c, h.c);
            if (cs->cnd == NULL) {
                out(u, "a_cpy(&%s, %s);", c.c, v);
            } else {
                Opnd x = gen_expr(u, cs->cnd);
                out(u, "if (a_is(%s, %s)) {", h.c, x.c);
                out(u, "    a_cpy(&%s, %s);", c.c, v);
                out(u, "}");
                opnd_rel(u, x);
            }
        }

        out(u, "if (a_truthy(%s)) {", c.c);
        u->ind++;
        if (cs->body->tag == E_PROTO) {
            gen_inline(u, cs->body, c, d);
        } else {
            opnd_rel(u, c);
            gen_block(u, &cs->body->u.block.es, d);
        }
        u->ind--;
        out(u, "} else {");
        u->ind++;
        opnd_rel(u, c);
    }
    // no case holds
    store_void(u, d);
    for (int i=0; i<e->u.ifs.n; i++) {
        u->ind--;
        out(u, "}");
    }

    if (h.c != NULL) {
        opnd_rel(u, h);
    }
}

///////////////////////////////////////////////////////////////////////////////
// LOOPS
///////////////////////////////////////////////////////////////////////////////

// loop {...}  |  loop i {...}  |  loop k,v in e {...}
// Its value is nothing, or the values of a break.
static void gen_loop (Unit* u, Expr* e, Dst d) {
    int iter = (e->u.loop.n > 0) || (e->u.loop.itr != NULL);

    // iterator, limit of a range, cursor
    Opnd it  = opnd_c(NULL);
    Opnd lim = opnd_c(NULL);
    Opnd pos = opnd_c(NULL);
    const char* k = NULL;
    const char* v = NULL;
    if (iter) {
        it  = opnd_tmp(u);
        lim = opnd_tmp(u);
        pos = opnd_tmp(u);
        if (e->u.loop.itr != NULL) {
            // loop i in (a,b) gives two values
            Dst m = dst_multi(u);
            gen(u, e->u.loop.itr, m);
            out(u, "a_adj(%s, 2);", m.n);
            out(u, "a_pop(&%s);", lim.c);
            out(u, "a_pop(&%s);", it.c);
        }
        k = (e->u.loop.n > 0) ? var_c(u,e->vars[0]) : opnd_tmp(u).c;
        v = (e->u.loop.n > 1) ? var_c(u,e->vars[1]) : opnd_tmp(u).c;
    }

    int end = ++u->nlabels;
    out(u, "while (1) {");
    u->ind++;
    if (iter) {
        out_try(u, "a_next(%s, %s, &%s, &%s, &%s)", it.c, lim.c, pos.c, k, v);
        out(u, "if (%s.type == A_NIL) break;", k);
    }
    Hnd h = hnd_none();
    h.brk  = end;
    h.brkd = d;
    gen_block_x(u, &e->u.loop.blk->u.block.es, dst_none(), h);
    u->ind--;
    out(u, "}");
    // the loop ends without a break
    store_void(u, d);
    out0(u, "LE_%d: ;", end);

    if (iter) {
        opnd_rel(u, it);
        opnd_rel(u, lim);
        opnd_rel(u, pos);
    }
}

// break(...)  until(c,...)  while(c,...)
static void gen_break (Unit* u, Expr* e) {
    Special sp = e->u.call.sp;
    List args = { 0, NULL };
    flatten(&args, &e->u.call.es);

    // condition, then the values of the break
    Opnd c = opnd_c(NULL);
    List vals = args;
    if (sp != SP_BREAK) {
        if (args.n == 0) {
            err(e->tk, "expected condition");
        }
        c = gen_expr(u, args.v[0]);
        vals.n--;
        vals.v++;
    }
    Dst m = dst_multi(u);
    gen_args(u, &vals, m.n, NULL);

    if (sp == SP_UNTIL) {
        // until(c, v, ...) breaks with `v, ...` if `v` holds, else `c`
        out(u, "if (a_truthy(%s)) {", c.c);
        out(u, "    if (%s>0 && a_truthy(a_stk[a_top-%s])) {", m.n, m.n);
        out(u, "        a_pay_pull(%s);", m.n);
        out(u, "    } else {");
        out(u, "        a_settop(a_top - %s);", m.n);
        out(u, "        a_pay_clear();");
        out(u, "        a_pay_addc(%s);", c.c);
        out(u, "    }");
    } else if (sp == SP_WHILE) {
        out(u, "if (!a_truthy(%s)) {", c.c);
        out(u, "    a_pay_pull(%s);", m.n);
    } else {
        out(u, "{");
        out(u, "    a_pay_pull(%s);", m.n);
    }
    out(u, "    st = A_BREAK;");
    out(u, "    goto CL_%d;", u->cleanup);
    out(u, "}");
    if (sp != SP_BREAK) {
        out(u, "a_settop(a_top - %s);", m.n);
        opnd_rel(u, c);
    }
}

///////////////////////////////////////////////////////////////////////////////
// EXCEPTIONS
///////////////////////////////////////////////////////////////////////////////

// catch e {...}: `true` plus the values of the body, or `false` plus
// the values of a matching throw.
static void gen_catch (Unit* u, Expr* e, Dst d) {
    Opnd c = gen_expr(u, e->u.xcatch.cnd);
    int lc  = ++u->nlabels;
    int end = ++u->nlabels;

    Dst m = (d.kind == D_MULTI) ? dst_multi(u) : dst_none();
    Hnd h = hnd_none();
    h.err = lc;
    gen_block_x(u, &e->u.xcatch.blk->u.block.es, m, h);

    // no error
    switch (d.kind) {
        case D_NONE:
            break;
        case D_SLOT:
            out(u, "a_set(&%s, a_bool(1));", d.slot);
            break;
        case D_MULTI:
            out(u, "a_open(%s, 1);", m.n);
            out(u, "a_putc(a_top-%s-1, a_bool(1));", m.n);
            out(u, "%s = %s + 1;", d.n, m.n);
            break;
    }
    out(u, "goto LE_%d;", end);

    // error: caught here, or propagated
    out0(u, "LC_%d:", lc);
    out(u, "{");
    out(u, "    int ok;");
    out(u, "    if ((st = a_catch(%s, &ok)) != A_OK) goto CL_%d;",
        c.c, u->cleanup);
    out(u, "    if (!ok) {");
    out(u, "        st = A_ERROR;");
    out(u, "        goto CL_%d;", u->cleanup);
    out(u, "    }");
    out(u, "}");
    switch (d.kind) {
        case D_NONE:
            out(u, "a_pay_clear();");
            break;
        case D_SLOT:
            out(u, "a_set(&%s, a_bool(0));", d.slot);
            out(u, "a_pay_clear();");
            break;
        case D_MULTI:
            out(u, "a_push(a_bool(0));");
            out(u, "%s = 1 + a_pay_push();", d.n);
            break;
    }
    out0(u, "LE_%d: ;", end);
    opnd_rel(u, c);
}

///////////////////////////////////////////////////////////////////////////////
// CALLS
///////////////////////////////////////////////////////////////////////////////

static void gen_call (Unit* u, Expr* e, Dst d) {
    switch (e->u.call.sp) {
        case SP_NONE:
            break;
        case SP_DO: {
            // do(...): evaluates and discards
            Dst m = dst_multi(u);
            gen_args(u, &e->u.call.es, m.n, NULL);
            out(u, "a_settop(a_top - %s);", m.n);
            store_void(u, d);
            return;
        }
        case SP_BREAK:
        case SP_UNTIL:
        case SP_WHILE:
            gen_break(u, e);
            store_void(u, d);
            return;
        case SP_RETURN: {
            // the values travel in the payload up to the function
            Dst m = dst_multi(u);
            gen_args(u, &e->u.call.es, m.n, NULL);
            out(u, "a_pay_pull(%s);", m.n);
            out(u, "st = A_RETURN;");
            out(u, "goto CL_%d;", u->cleanup);
            store_void(u, d);
            return;
        }
        case SP_ABORT: {
            List args = { 0, NULL };
            flatten(&args, &e->u.call.es);
            if (args.n != 1) {
                err(e->tk, "invalid abort : expected single argument");
            }
            Opnd x = gen_expr(u, args.v[0]);
            out(u, "a_lin = %d;", e->tk.lin);
            out_try(u, "a_abort(%s)", x.c);
            opnd_rel(u, x);
            store_void(u, d);
            return;
        }
        case SP_ESCAPE: {
            Dst m = dst_multi(u);
            gen_args(u, &e->u.call.es, m.n, NULL);
            out(u, "a_pay_pull(%s);", m.n);
            out(u, "st = a_escape();");
            out(u, "goto CL_%d;", u->cleanup);
            store_void(u, d);
            return;
        }
        case SP_THROW: {
            Dst m = dst_multi(u);
            gen_args(u, &e->u.call.es, m.n, NULL);
            out(u, "a_pay_pull(%s);", m.n);
            out(u, "st = a_throw();");
            out(u, "goto CL_%d;", u->cleanup);
            store_void(u, d);
            return;
        }
        default:
            err(e->tk, "statement not implemented");
    }

    Opnd f;
    Dst  m = dst_multi(u);
    if (e->u.call.f->tag == E_MET) {
        // o::m(...) is o.m(o, ...), with `o` evaluated once
        Expr* met = e->u.call.f;
        Opnd o = gen_expr(u, met->u.met.o);
        f = opnd_tmp(u);
        out_try(u, "a_index(&%s, %s, K[%d])", f.c, o.c,
            konst(met->u.met.met.str, met->u.met.met.len));
        gen_args(u, &e->u.call.es, m.n, &o);
    } else {
        f = gen_expr(u, e->u.call.f);
        gen_args(u, &e->u.call.es, m.n, NULL);
    }

    out(u, "{");
    u->ind++;
    out(u, "int b = a_top - %s;", m.n);
    out(u, "a_lin = %d;", e->tk.lin);
    out_try(u, "a_call(%s, b)", f.c);
    switch (d.kind) {
        case D_NONE:
            out(u, "a_settop(b);");
            break;
        case D_SLOT:
            out(u, "a_ret1(&%s, b);", d.slot);
            break;
        case D_MULTI:
            out(u, "%s = a_top - b;", d.n);
            break;
    }
    u->ind--;
    out(u, "}");
    opnd_rel(u, f);
}

// (e1, e2, ...): all values of the list, or only the first one.
static void gen_list (Unit* u, Expr* e, Dst d) {
    Dst m = (d.kind == D_MULTI) ? d : dst_multi(u);
    gen_args(u, &e->u.list.es, m.n, NULL);
    switch (d.kind) {
        case D_NONE:
            out(u, "a_settop(a_top - %s);", m.n);
            break;
        case D_SLOT:
            out(u, "a_ret1(&%s, a_top - %s);", d.slot, m.n);
            break;
        case D_MULTI:
            break;
    }
}

///////////////////////////////////////////////////////////////////////////////
// FUNCTIONS
///////////////////////////////////////////////////////////////////////////////

// Declarations and definitions of the generated C functions.
static Buf DECLS = { NULL, 0, 0 };
static Buf FUNCS = { NULL, 0, 0 };

// Appends the common prologue of a function, its body, and its
// cleanup label.
//
// A function keeps its slots on the C stack. A task keeps them in its
// frame, and starts by jumping to its resumption point.
static void unit_emit (Unit* u, Buf* c) {
    int n = (u->nslots > 0) ? u->nslots : 1;
    if (u->task) {
        buf_add(c, "    AVal* L = t->L;\n");
        buf_add(c, "    AFunc* self = t->proto;\n");
    } else {
        buf_add(c, xfmt("    AVal L[%d];\n", n));
        buf_add(c, xfmt("    a_nils(L, %d);\n", n));
    }
    buf_add(c, "    AStatus st = A_OK;\n");
    buf_add(c, "    int base = a_top;\n");
    for (int i=0; i<u->ncnts; i++) {
        buf_add(c, xfmt("    int n%d = 0;\n", i));
    }
    if (u->task) {
        buf_add(c, "    switch (t->pc) {\n");
        for (int i=1; i<=u->npcs; i++) {
            buf_add(c, xfmt("        case %d: goto AW_%d;\n", i, i));
        }
        buf_add(c, "    }\n");
    }
    buf_add(c, u->body.s ? u->body.s : "");
    buf_add(c, "    goto CL_0;\n");
    buf_add(c, "CL_0:\n");
    if (!u->task) {
        buf_add(c, xfmt("    a_clrs(L, %d);\n", n));
    }
}

// Compiles a task prototype (or the main chunk) to a resume function
// and its static description. Returns the name of the description.
//
// Parameters take the first slots of the frame, then comes a table
// with the extra arguments, if variadic. The value of the body, or of
// a return, becomes the result of the task.
static const char* gen_task (Proto* p, Var** pars, int npar, List* body,
                             int lin)
{
    Unit u;
    memset(&u, 0, sizeof(u));
    u.proto = p;
    u.npar  = npar;
    u.task  = 1;
    for (int i=0; i<npar; i++) {
        pars[i]->slot = u.nslots++;
    }
    if (p->dots) {
        u.nslots++;
    }
    gen_block(&u, body, dst_slot("t->ret"));

    const char* name = xfmt("T%d", p->id);
    const char* sig = xfmt(
        "static AStatus %s (ATask* t, AKind k)", name);
    buf_add(&DECLS, xfmt("%s;\n", sig));

    Buf* c = &FUNCS;
    buf_add(c, xfmt("// line %d\n%s {\n", lin, sig));
    unit_emit(&u, c);
    buf_add(c, "    if (st == A_RETURN) {\n");
    buf_add(c, "        st = A_OK;\n");
    buf_add(c, "        a_pay_take(&t->ret, 0);\n");
    buf_add(c, "        a_pay_clear();\n");
    buf_add(c, "    }\n");
    buf_add(c, "    return st;\n");
    buf_add(c, "}\n\n");
    buf_add(c, xfmt("static const AProto P%d = { %s, %d, %d, %d };\n\n",
        p->id, name, (u.nslots > 0) ? u.nslots : 1, npar, p->dots));
    return xfmt("P%d", p->id);
}

// Compiles a prototype to a C function. Returns its name.
//
// The values of the body, or of a return, are left on the stack.
static const char* gen_func (Expr* e) {
    Unit u;
    memset(&u, 0, sizeof(u));
    u.proto = e->proto;
    u.npar  = e->u.proto.n;

    // parameters are immutable copies of the arguments
    for (int i=0; i<e->u.proto.n; i++) {
        out(&u, "if (argc > %d) a_cpy(&%s, argv[%d]);",
            i, var_c(&u,e->vars[i]), i);
    }
    gen_block(&u, &e->u.proto.blk->u.block.es, dst_multi(&u));

    const char* name = xfmt("F%d", e->proto->id);
    const char* sig = xfmt(
        "static AStatus %s (AFunc* self, int argc, AVal* argv)", name);
    buf_add(&DECLS, xfmt("%s;\n", sig));

    Buf* c = &FUNCS;
    buf_add(c, xfmt("// line %d\n%s {\n", e->tk.lin, sig));
    unit_emit(&u, c);
    buf_add(c, "    if (st == A_RETURN) {\n");
    buf_add(c, "        st = A_OK;\n");
    buf_add(c, "        a_pay_push();\n");
    buf_add(c, "    }\n");
    buf_add(c, "    return st;\n");
    buf_add(c, "}\n\n");
    return name;
}

// func (...) {...}: a closure with copies of the captured variables
// (or their boxes).
static Opnd gen_proto (Unit* u, Expr* e) {
    Proto* p = e->proto;
    Opnd d = opnd_tmp(u);
    if (e->u.proto.task) {
        const char* name = gen_task(p, e->vars, e->u.proto.n,
            &e->u.proto.blk->u.block.es, e->tk.lin);
        out(u, "a_set(&%s, a_task(&%s, %d));", d.c, name, p->ncaps);
    } else {
        const char* name = gen_func(e);
        out(u, "a_set(&%s, a_func(%s, %d));", d.c, name, p->ncaps);
    }
    for (int i=0; i<p->ncaps; i++) {
        out(u, "a_cpy(&A_FUNC_OF(%s)->caps[%d], %s);",
            d.c, i, var_raw(u,p->caps[i]));
    }
    return d;
}

// `...`: the extra arguments of the function. A task keeps them in a
// table in its frame.
static void gen_dots (Unit* u, Dst d) {
    if (u->task) {
        switch (d.kind) {
            case D_NONE:
                break;
            case D_SLOT:
                out(u, "a_cpy(&%s, a_tab_get(A_TAB_OF(L[%d]), a_num(0)));",
                    d.slot, u->npar);
                break;
            case D_MULTI:
                out(u, "%s = a_tab_unpack(L[%d]);", d.n, u->npar);
                break;
        }
        return;
    }
    switch (d.kind) {
        case D_NONE:
            break;
        case D_SLOT:
            out(u, "a_cpy(&%s, (argc > %d) ? argv[%d] : a_nil());",
                d.slot, u->npar, u->npar);
            break;
        case D_MULTI:
            out(u, "%s = a_dots(argc, argv, %d);", d.n, u->npar);
            break;
    }
}

///////////////////////////////////////////////////////////////////////////////
// TASKS
///////////////////////////////////////////////////////////////////////////////

// spawn T(...)  |  spawn @ts T(...)
// As a statement, the task is pinned to the enclosing block.
// Otherwise it flows out, and the receiver must pin it.
static void gen_spawn (Unit* u, Expr* e, Dst d) {
    Opnd f = gen_expr(u, e->u.spawn.f);
    Opnd pool = opnd_c("a_nil()");
    if (e->u.spawn.pool != NULL) {
        pool = gen_expr(u, e->u.spawn.pool);
    }
    Dst m = dst_multi(u);
    gen_args(u, &e->u.spawn.args, m.n, NULL);

    Opnd t = opnd_tmp(u);
    out(u, "a_lin = %d;", e->tk.lin);
    out_try(u, "a_spawn(&%s, %s, %s, a_top-%s)", t.c, f.c, pool.c, m.n);
    opnd_rel(u, f);
    opnd_rel(u, pool);

    if (d.kind==D_NONE && e->u.spawn.pool==NULL) {
        gen_pin(u, t.c);
    } else {
        store(u, t, d);
    }
}

// Number of transparent units generated.
static int NTRA = 0;

// Compiles a transparent block to a resume function that shares the
// slots of its task. Returns the name of its static description.
//
// As a branch of a `par :any`, its values are raised as a join for
// the par identified by slot `key`. Any other exit crosses to the
// owner unchanged.
static const char* gen_tra (Unit* up, List* body, ParMode mode, int key,
                            int lin)
{
    Unit u;
    memset(&u, 0, sizeof(u));
    u.proto = up->proto;
    u.npar  = up->npar;
    u.task  = 1;
    u.root  = (up->root != NULL) ? up->root : up;

    if (mode == PAR_ANY) {
        Dst m = dst_multi(&u);
        gen_block(&u, body, m);
        out(&u, "a_pay_pull(%s);", m.n);
        out(&u, "a_join_key = &L[%d];", key);
        out(&u, "st = A_JOIN;");
    } else {
        gen_block(&u, body, dst_slot("t->ret"));
    }

    int id = ++NTRA;
    const char* name = xfmt("T%d_%d", u.proto->id, id);
    const char* sig = xfmt("static AStatus %s (ATask* t, AKind k)", name);
    buf_add(&DECLS, xfmt("%s;\n", sig));

    Buf* c = &FUNCS;
    buf_add(c, xfmt("// line %d\n%s {\n", lin, sig));
    unit_emit(&u, c);
    buf_add(c, "    return st;\n");
    buf_add(c, "}\n\n");
    buf_add(c, xfmt("static const AProto P%d_%d = { %s, 0, 0, 0 };\n\n",
        u.proto->id, id, name));
    return xfmt("P%d_%d", u.proto->id, id);
}

// Spawns a transparent block into `slot`, which a block must abort on
// exit.
static void gen_spawn_tra (Unit* u, Expr* blk, ParMode mode, int key,
                           const char* slot)
{
    const char* p = gen_tra(u, &blk->u.block.es, mode, key, blk->tk.lin);
    out(u, "a_lin = %d;", blk->tk.lin);
    out_try(u, "a_spawn_tra(&%s, t, &%s)", slot, p);
    out_try(u, "a_pin(&%s)", slot);
}

static void gen_suspend (Unit* u, Dst d);

// par {...} with {...}: never rejoins
// par :any: rejoins when a branch terminates, with its values
// par :all: rejoins when all terminate, with the first value of each
static void gen_par (Unit* u, Expr* e, Dst d) {
    int k = e->u.par.blks.n;
    ParMode mode = e->u.par.mode;
    Hnd h = hnd_none();

    // the results outlive the block of the branches
    Opnd* rs = xalloc(k * sizeof(Opnd));
    if (mode == PAR_ALL) {
        for (int i=0; i<k; i++) {
            rs[i] = opnd_tmp(u);
        }
    }
    if (mode == PAR_ANY) {
        h.join  = ++u->nlabels;
        h.joink = slot_new(u);
        h.joind = d;
    }

    Blk b = blk_open(u);
    // on exit, the branches are aborted in spawn order
    const char** ts = xalloc(k * sizeof(char*));
    for (int i=0; i<k; i++) {
        ts[i] = opnd_tmp(u).c;
    }
    for (int i=k-1; i>=0; i--) {
        gen_pin_end(u, ts[i]);
    }
    for (int i=0; i<k; i++) {
        gen_spawn_tra(u, e->u.par.blks.v[i], mode, h.joink, ts[i]);
    }
    if (mode == PAR_ALL) {
        for (int i=0; i<k; i++) {
            out(u, "st = a_await(t, a_pt_val(%s));", ts[i]);
            gen_suspend(u, dst_slot(rs[i].c));
        }
    } else {
        out(u, "st = a_await(t, a_pt_val(a_bool(0)));");
        gen_suspend(u, dst_none());
    }
    blk_close(u, b, h);

    if (mode == PAR_ALL) {
        switch (d.kind) {
            case D_NONE:
                break;
            case D_SLOT:
                out(u, "a_cpy(&%s, %s);", d.slot, rs[0].c);
                break;
            case D_MULTI:
                for (int i=0; i<k; i++) {
                    out(u, "a_pushc(%s);", rs[i].c);
                }
                out(u, "%s = %d;", d.n, k);
                break;
        }
        for (int i=0; i<k; i++) {
            opnd_rel(u, rs[i]);
        }
    } else {
        store_void(u, d);
    }
    if (mode == PAR_ANY) {
        out0(u, "LE_%d: ;", h.join);
    }
}

// Suspends the task until it is resumed, then continues with the
// status of the resumption. The results arrive in the payload.
static void gen_suspend (Unit* u, Dst d) {
    int pc = ++u->npcs;
    out(u, "if (st == A_AWAIT) {");
    out(u, "    t->pc = %d;", pc);
    out(u, "    return A_AWAIT;");
    out0(u, "AW_%d:", pc);
    out(u, "    base = a_top;");
    out(u, "    st = a_resumed(t, k);");
    out(u, "}");
    out(u, "if (st != A_OK) goto CL_%d;", u->cleanup);
    store_pay(u, d);
}

// A pattern is eager: its values, task prototypes, arguments, and
// predicates are evaluated once, in order, before the await starts.
static void patt_eval (Unit* u, Patt* p, Opnd* os, int* n) {
    switch (p->kind) {
        case PT_VAL:
            os[(*n)++] = gen_expr(u, p->e);
            break;
        case PT_SPAWN: {
            os[(*n)++] = gen_expr(u, p->e);
            // the arguments wait in a table
            Opnd t = opnd_tmp(u);
            out(u, "a_set(&%s, a_tab());", t.c);
            Dst m = dst_multi(u);
            gen_args(u, &p->args, m.n, NULL);
            out(u, "a_tab_fill(%s, 0, %s);", t.c, m.n);
            os[(*n)++] = t;
            break;
        }
        case PT_ANY:
        case PT_ALL:
            os[(*n)++] = gen_expr(u, p->e);
            break;
        case PT_UNTIL:
        case PT_WHILE:
            if (p->a != NULL) {
                patt_eval(u, p->a, os, n);
            }
            os[(*n)++] = gen_expr(u, p->e);
            break;
        case PT_NOT:
            patt_eval(u, p->a, os, n);
            break;
        case PT_AND:
        case PT_OR:
            patt_eval(u, p->a, os, n);
            patt_eval(u, p->b, os, n);
            break;
    }
}

// Builds the runtime pattern from the evaluated operands, as one C
// variable per node. Returns the variable of the root.
static const char* patt_build (Unit* u, Patt* p, Opnd* os, int* n, int* k) {
    const char* a = NULL;
    const char* b = NULL;
    const char* c = NULL;
    switch (p->kind) {
        case PT_VAL:
            c = xfmt("a_pt_val(%s)", os[(*n)++].c);
            break;
        case PT_SPAWN:
            c = xfmt("a_pt_spawn(%s, %s)", os[*n].c, os[*n+1].c);
            *n += 2;
            break;
        case PT_ANY:
        case PT_ALL:
            c = xfmt("a_pt_pool(%d, %s)", p->kind==PT_ALL, os[(*n)++].c);
            break;
        case PT_UNTIL:
        case PT_WHILE:
            a = (p->a != NULL) ? patt_build(u, p->a, os, n, k) : "NULL";
            c = xfmt("a_pt_pred(%d, %s, %s)", p->kind==PT_UNTIL, a,
                os[(*n)++].c);
            break;
        case PT_NOT:
            a = patt_build(u, p->a, os, n, k);
            c = xfmt("a_pt_not(%s)", a);
            break;
        case PT_AND:
        case PT_OR:
            a = patt_build(u, p->a, os, n, k);
            b = patt_build(u, p->b, os, n, k);
            c = xfmt("a_pt_bin(%d, %s, %s)", p->kind==PT_AND, a, b);
            break;
    }
    const char* v = xfmt("p%d", (*k)++);
    out(u, "    APatt* %s = %s;", v, c);
    return v;
}

static int patt_size (Patt* p) {
    if (p == NULL) {
        return 0;
    }
    return 2 + patt_size(p->a) + patt_size(p->b);
}

// Establishes an await on a pattern and suspends.
static void gen_await_patt (Unit* u, Patt* p, int lin, Dst d) {
    Opnd* os = xalloc(patt_size(p) * sizeof(Opnd));
    int n = 0;
    patt_eval(u, p, os, &n);

    out(u, "a_lin = %d;", lin);
    out(u, "{");
    int i = 0;
    int k = 0;
    const char* v = patt_build(u, p, os, &i, &k);
    out(u, "    st = a_await(t, %s);", v);
    out(u, "}");
    for (int j=0; j<n; j++) {
        opnd_rel(u, os[j]);
    }
    gen_suspend(u, d);
}

// Evaluates and builds the filters of a toggle into the C array
// `fs`, inside an open C block. Returns the operands to release.
static Opnd* gen_filters (Unit* u, Patt** fs, int nf, int* n) {
    int size = 0;
    for (int i=0; i<nf; i++) {
        size += patt_size(fs[i]);
    }
    Opnd* os = xalloc((size + 1) * sizeof(Opnd));
    *n = 0;
    for (int i=0; i<nf; i++) {
        patt_eval(u, fs[i], os, n);
    }
    out(u, "{");
    out(u, "    APatt* fs[%d];", (nf > 0) ? nf : 1);
    int j = 0;
    int k = 0;
    for (int i=0; i<nf; i++) {
        const char* v = patt_build(u, fs[i], os, &j, &k);
        out(u, "    fs[%d] = %s;", i, v);
    }
    return os;
}

// toggle t(b) with P, ...
static void gen_toggle (Unit* u, Expr* e) {
    List args = { 0, NULL };
    flatten(&args, &e->u.toggle.args);
    if (args.n != 1) {
        err(e->tk, "invalid toggle : expected single argument");
    }
    Opnd t  = gen_expr(u, e->u.toggle.t);
    Opnd on = gen_expr(u, args.v[0]);
    int n;
    Opnd* os = gen_filters(u, e->u.toggle.fs, e->u.toggle.nf, &n);
    out(u, "    a_lin = %d;", e->tk.lin);
    out(u, "    st = a_toggle(%s, %s, fs, %d);", t.c, on.c, e->u.toggle.nf);
    out(u, "}");
    out(u, "if (st != A_OK) goto CL_%d;", u->cleanup);
    for (int i=0; i<n; i++) {
        opnd_rel(u, os[i]);
    }
    opnd_rel(u, t);
    opnd_rel(u, on);
}

// toggle on :T with P, ... {...}: the block runs as a transparent
// task that events `:T [false]` and `:T [true]` toggle.
static void gen_toggle_on (Unit* u, Expr* e, Dst d) {
    Opnd r = opnd_tmp(u);
    Blk b = blk_open(u);
    const char* t = opnd_tmp(u).c;
    gen_pin_end(u, t);
    gen_spawn_tra(u, e->u.toggle_on.blk, PAR_NONE, 0, t);

    int n;
    Opnd* os = gen_filters(u, e->u.toggle_on.fs, e->u.toggle_on.nf, &n);
    out(u, "    a_toggle_auto(%s, K[%d], fs, %d);",
        t, konst(e->tk.str+1,e->tk.len-1), e->u.toggle_on.nf);
    out(u, "}");
    for (int i=0; i<n; i++) {
        opnd_rel(u, os[i]);
    }

    out(u, "st = a_await(t, a_pt_val(%s));", t);
    gen_suspend(u, dst_slot(r.c));
    blk_close(u, b, hnd_none());
    store(u, r, d);
}

// await P
static void gen_await (Unit* u, Expr* e, Dst d) {
    gen_await_patt(u, e->u.await.p, e->tk.lin, d);
}

// emit @to (e)
static void gen_emit (Unit* u, Expr* e) {
    Opnd to = opnd_c("a_nil()");
    if (e->u.emit.to != NULL) {
        to = gen_expr(u, e->u.emit.to);
    }
    Opnd v = gen_expr(u, e->u.emit.e);
    out(u, "a_lin = %d;", e->tk.lin);
    out_try(u, "a_emit(%s, %s)", to.c, v.c);
    opnd_rel(u, to);
    opnd_rel(u, v);
}

///////////////////////////////////////////////////////////////////////////////

// Compiles a simple expression to an operand.
static Opnd gen_expr (Unit* u, Expr* e) {
    switch (e->tag) {
        case E_NIL:
            return opnd_c("a_nil()");
        case E_BOOL:
            return opnd_c(!strcmp(e->tk.str,"true") ?
                "a_bool(1)" : "a_bool(0)");
        case E_NUM:
            return opnd_c(xfmt("a_num(%.17g)", strtod(e->tk.str,NULL)));
        case E_CLK:
            // a clock is a number of microseconds
            return opnd_c(xfmt("a_num(%.17g)", e->tk.clk));
        case E_STR: {
            // only quoted single-line literals have escapes
            int len = e->tk.len;
            const char* str = e->tk.str;
            if (e->tk.tag==TK_STR && !e->tk.multi) {
                str = decode(e->tk, &len);
            }
            return opnd_c(xfmt("K[%d]", konst(str,len)));
        }
        case E_TAG:
            // a tag is the string after the colon
            return opnd_c(xfmt("K[%d]", konst(e->tk.str+1,e->tk.len-1)));
        case E_ACC:
            return gen_acc(u, e);
        case E_INDEX:
            return gen_index(u, e);
        case E_TABLE:
            return gen_table(u, e);
        case E_TAGGED:
            return gen_tagged(u, e);
        case E_PARENS:
            return gen_expr(u, e->u.parens.e);
        case E_UNO:
            return gen_uno(u, e);
        case E_BIN:
            return gen_bin(u, e);
        case E_PROTO:
            return gen_proto(u, e);
        case E_ME: {
            Opnd t = opnd_tmp(u);
            out(u, "a_set(&%s, a_me());", t.c);
            return t;
        }
        case E_PUB: {
            Opnd t = opnd_tmp(u);
            out(u, "{");
            out(u, "    AVal* pub;");
            out(u, "    if ((st = a_pub(&pub)) != A_OK) goto CL_%d;",
                u->cleanup);
            out(u, "    a_cpy(&%s, *pub);", t.c);
            out(u, "}");
            return t;
        }
        case E_TASKS: {
            Opnd n = opnd_c("a_nil()");
            if (e->u.tasks.n != NULL) {
                n = gen_expr(u, e->u.tasks.n);
            }
            Opnd t = opnd_tmp(u);
            out_try(u, "a_tasks(&%s, %s)", t.c, n.c);
            opnd_rel(u, n);
            return t;
        }
        default: {
            // compound: evaluated into a temporary
            Opnd t = opnd_tmp(u);
            gen(u, e, dst_slot(t.c));
            return t;
        }
    }
}

// Compiles any expression against a destination.
static void gen (Unit* u, Expr* e, Dst d) {
    switch (e->tag) {
        case E_CALL:
            gen_call(u, e, d);
            break;
        case E_LIST:
            gen_list(u, e, d);
            break;
        case E_DCL:
            gen_dcl(u, e);
            store_void(u, d);
            break;
        case E_SET:
            gen_set(u, e);
            store_void(u, d);
            break;
        case E_BLOCK:
            gen_block(u, &e->u.block.es, d);
            break;
        case E_DO:
            gen_do(u, e, d);
            break;
        case E_IFS:
            gen_ifs(u, e, d);
            break;
        case E_LOOP:
            gen_loop(u, e, d);
            break;
        case E_WHERE:
            gen_where(u, e, d);
            break;
        case E_DOTS:
            gen_dots(u, d);
            break;
        case E_CATCH:
            gen_catch(u, e, d);
            break;
        case E_DEFER:
            gen_defer(u, e);
            store_void(u, d);
            break;
        case E_SPAWN:
            gen_spawn(u, e, d);
            break;
        case E_AWAIT:
            gen_await(u, e, d);
            break;
        case E_EMIT:
            gen_emit(u, e);
            store_void(u, d);
            break;
        case E_SPAWN_BLK: {
            // pinned to the enclosing block
            const char* slot = opnd_tmp(u).c;
            gen_pin_end(u, slot);
            gen_spawn_tra(u, e->u.sblk.blk, PAR_NONE, 0, slot);
            store_void(u, d);
            break;
        }
        case E_PAR:
            gen_par(u, e, d);
            break;
        case E_TOGGLE:
            gen_toggle(u, e);
            store_void(u, d);
            break;
        case E_TOGGLE_ON:
            gen_toggle_on(u, e, d);
            break;
        case E_NAT:
        case E_MET:
            err(e->tk, "expression not implemented");
            break;
        default:
            store(u, gen_expr(u,e), d);
            break;
    }
}

///////////////////////////////////////////////////////////////////////////////
// MAIN
///////////////////////////////////////////////////////////////////////////////

char* coder_main (Expr* prog, Var** globals, int nglobals) {
    // the main chunk is a task
    const char* main = gen_task(PROTO_MAIN, NULL, 0, &prog->u.block.es, 1);

    Unit u;
    memset(&u, 0, sizeof(u));
    u.proto = PROTO_MAIN;

    Buf c = { NULL, 0, 0 };
    buf_add(&c, "#include \"atmos.h\"\n\n");
    buf_add(&c, xfmt("const char* atm_file = %s;\n\n",
        quote(FILENAME, strlen(FILENAME))));

    // top-level variables
    for (int i=0; i<nglobals; i++) {
        buf_add(&c, xfmt("static AVal %s;\n", var_raw(&u,globals[i])));
    }
    buf_add(&c, "\n");

    // constants
    buf_add(&c, xfmt("static AVal K[%d];\n\n", (KN > 0) ? KN : 1));
    buf_add(&c, "static void consts_init (void) {\n");
    for (int i=0; i<KN; i++) {
        buf_add(&c, xfmt("    K[%d] = a_str(%s, %d);\n",
            i, quote(KS[i].s,KS[i].len), KS[i].len));
    }
    buf_add(&c, "}\n\n");

    // functions and tasks
    buf_add(&c, DECLS.s ? DECLS.s : "");
    buf_add(&c, "\n");
    buf_add(&c, FUNCS.s ? FUNCS.s : "");

    buf_add(&c, "AStatus atm_main (void) {\n");
    buf_add(&c, "    consts_init();\n");
    buf_add(&c, xfmt("    return a_main(&%s);\n", main));
    buf_add(&c, "}\n\n");

    buf_add(&c, "void atm_free (void) {\n");
    for (int i=nglobals-1; i>=0; i--) {
        buf_add(&c, xfmt("    a_clr(&%s);\n", var_raw(&u,globals[i])));
    }
    buf_add(&c, xfmt("    a_clrs(K, %d);\n", KN));
    buf_add(&c, "}\n");
    return c.s;
}
