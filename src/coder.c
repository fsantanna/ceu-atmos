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
} Unit;

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
    o.tmp = u->nslots++;
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

static void gen      (Unit* u, Expr* e, Dst d);
static Opnd gen_expr (Unit* u, Expr* e);

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
            v->slot = u->nslots++;
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
        { "**", "a_pow",  "" }, { "++", "a_cat", ""  },
        { "<",  "a_lt",   "" }, { "<=", "a_le",  ""  },
        { ">",  "a_lt",  "x" }, { ">=", "a_le",  "x" },
        { NULL, NULL, NULL }
    };
    // operator, predicate, negated, swapped operands
    static const char* PREDS[][4] = {
        { "==",  "a_eq",  "",  ""  }, { "!=",  "a_eq",  "!", ""  },
        { "??",  "a_is",  "",  ""  }, { "!?",  "a_is",  "!", ""  },
        { "===", "a_deq", "",  ""  }, { "=!=", "a_deq", "!", ""  },
        { "=>=", "a_gte", "",  ""  }, { "=<=", "a_gte", "",  "x" },
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
    if (!strcmp(op,"?>") || !strcmp(op,"!>")) {
        out_try(u, "a_in(&%s, %s, %s, %d)", d.c, x.c, y.c, op[0]=='!');
        done = 1;
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
typedef struct Hnd {
    int brk;
    Dst brkd;
    int ret;
    Dst retd;
} Hnd;

static void gen_block_x (Unit* u, List* es, Dst d, Hnd h) {
    int up = u->cleanup;
    int lo = u->nslots;
    u->cleanup = ++u->nlabels;

    out(u, "{");
    u->ind++;
    for (int i=0; i<es->n; i++) {
        out(u, "a_lin = %d;", es->v[i]->tk.lin);
        gen(u, es->v[i], (i == es->n-1) ? d : dst_none());
    }
    if (es->n == 0) {
        store_void(u, d);
    }
    u->ind--;
    out0(u, "CL_%d:", u->cleanup);
    u->ind++;
    if (u->nslots > lo) {
        out(u, "a_clrs(&L[%d], %d);", lo, u->nslots-lo);
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
    out(u, "if (st != A_OK) {");
    out(u, "    a_settop(base);");
    out(u, "    goto CL_%d;", up);
    out(u, "}");
    u->ind--;
    out(u, "}");

    u->cleanup = up;
}

static void gen_block (Unit* u, List* es, Dst d) {
    Hnd h = { 0, dst_none(), 0, dst_none() };
    gen_block_x(u, es, d, h);
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

// val x = e  |  var x, y = e
static void gen_dcl (Unit* u, Expr* e) {
    if (e->tk.str[0] == 'p') {
        err(e->tk, "pin : not implemented");
    }
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
    } else if (dst->tag != E_ACC) {
        err(dst->tk, "assignment : not implemented");
    }
    return l;
}

// Assigns an operand to a destination, consuming both.
static void gen_assign (Unit* u, Loc l, Opnd v) {
    if (l.e->tag == E_ACC) {
        opnd_mov(u, v, var_c(u,l.e->var));
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
    Hnd h = { 0, dst_none(), end, d };
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
    Hnd h = { end, d, 0, dst_none() };
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
static void unit_emit (Unit* u, Buf* c) {
    int n = (u->nslots > 0) ? u->nslots : 1;
    buf_add(c, xfmt("    AVal L[%d];\n", n));
    buf_add(c, xfmt("    a_nils(L, %d);\n", n));
    buf_add(c, "    AStatus st = A_OK;\n");
    buf_add(c, "    int base = a_top;\n");
    for (int i=0; i<u->ncnts; i++) {
        buf_add(c, xfmt("    int n%d = 0;\n", i));
    }
    buf_add(c, u->body.s ? u->body.s : "");
    buf_add(c, "    goto CL_0;\n");
    buf_add(c, "CL_0:\n");
    buf_add(c, xfmt("    a_clrs(L, %d);\n", n));
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
    if (e->u.proto.task) {
        err(e->tk, "task : not implemented");
    }
    const char* name = gen_func(e);
    Proto* p = e->proto;
    Opnd d = opnd_tmp(u);
    out(u, "a_set(&%s, a_func(%s, %d));", d.c, name, p->ncaps);
    for (int i=0; i<p->ncaps; i++) {
        out(u, "a_cpy(&A_FUNC_OF(%s)->caps[%d], %s);",
            d.c, i, var_raw(u,p->caps[i]));
    }
    return d;
}

// `...`: the extra arguments of the function.
static void gen_dots (Unit* u, Dst d) {
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
            if (e->u.xdo.esc) {
                err(e->tk, "escape : not implemented");
            }
            gen_block(u, &e->u.xdo.blk->u.block.es, d);
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
        case E_NAT:
        case E_MET:
        case E_DEFER:
        case E_CATCH:
        case E_ME:
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
    Unit u;
    memset(&u, 0, sizeof(u));
    u.proto = PROTO_MAIN;

    // the value of the main block is discarded
    gen_block(&u, &prog->u.block.es, dst_none());

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

    // functions
    buf_add(&c, DECLS.s ? DECLS.s : "");
    buf_add(&c, "\n");
    buf_add(&c, FUNCS.s ? FUNCS.s : "");

    // main chunk
    buf_add(&c, "AStatus atm_main (int argc, AVal* argv) {\n");
    buf_add(&c, "    consts_init();\n");
    unit_emit(&u, &c);
    buf_add(&c, "    if (st == A_RETURN) {\n");
    buf_add(&c, "        st = A_OK;\n");
    buf_add(&c, "        a_pay_clear();\n");
    buf_add(&c, "    }\n");
    for (int i=nglobals-1; i>=0; i--) {
        buf_add(&c, xfmt("    a_clr(&%s);\n", var_raw(&u,globals[i])));
    }
    buf_add(&c, xfmt("    a_clrs(K, %d);\n", KN));
    buf_add(&c, "    return st;\n");
    buf_add(&c, "}\n");
    return c.s;
}
