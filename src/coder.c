// Atmos compiler: C code generation.
//
// Every expression is compiled to statements that leave its value in
// an operand: either a borrowed C expression (constant, variable), or
// an owned temporary slot `L[n]`. A temporary is released or moved by
// whoever consumes it.
//
// Failures set `st` and jump to the cleanup label of the enclosing
// block, which releases the slots and propagates the status.

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
    // number of slots in `L`
    int nslots;
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
// EXPRESSIONS
///////////////////////////////////////////////////////////////////////////////

static Opnd gen_expr (Unit* u, Expr* e);

// C expression of the storage of a variable.
static const char* var_c (Unit* u, Var* v) {
    if (v->csym != NULL) {
        return v->csym;
    } else if (v->global) {
        return xfmt("G%d_%s", v->id, v->tk.str);
    } else {
        if (v->slot < 0) {
            v->slot = u->nslots++;
        }
        return xfmt("L[%d]", v->slot);
    }
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

// Sequence of expressions in a new block. Its value is the value of
// the last expression, moved out before the block is cleaned up.
static Opnd gen_block (Unit* u, List* es) {
    Opnd d = opnd_tmp(u);

    int up = u->cleanup;
    int lo = u->nslots;
    u->cleanup = ++u->nlabels;

    out(u, "{");
    u->ind++;
    for (int i=0; i<es->n; i++) {
        Opnd o = gen_expr(u, es->v[i]);
        if (i == es->n-1) {
            opnd_mov(u, o, d.c);
        } else {
            opnd_rel(u, o);
        }
    }
    u->ind--;
    out0(u, "CL_%d:", u->cleanup);
    if (u->nslots > lo) {
        out(u, "    a_clrs(&L[%d], %d);", lo, u->nslots-lo);
    }
    out(u, "    if (st != A_OK) goto CL_%d;", up);
    out(u, "}");

    u->cleanup = up;
    return d;
}

// val x = e  |  var x = e
static Opnd gen_dcl (Unit* u, Expr* e) {
    if (e->u.dcl.n > 1) {
        err(e->tk, "multiple declaration : not implemented");
    }
    if (e->tk.str[0] == 'p') {
        err(e->tk, "pin : not implemented");
    }
    if (e->u.dcl.set != NULL) {
        Opnd o = gen_expr(u, e->u.dcl.set);
        opnd_mov(u, o, var_c(u,e->vars[0]));
    } else {
        var_c(u, e->vars[0]);
    }
    return opnd_c("a_nil()");
}

// set x = e  |  set t@k = e
static Opnd gen_set (Unit* u, Expr* e) {
    if (e->u.set.dsts.n > 1) {
        err(e->tk, "multiple assignment : not implemented");
    }
    Expr* dst = e->u.set.dsts.v[0];

    if (dst->tag == E_ACC) {
        Opnd o = gen_expr(u, e->u.set.src);
        opnd_mov(u, o, var_c(u,dst->var));

    } else if (dst->tag == E_INDEX) {
        // the table and the key are evaluated before the value
        Opnd t = gen_expr(u, dst->u.index.t);
        switch (dst->u.index.kind) {
            case IDX_EXPR: {
                Opnd k = gen_expr(u, dst->u.index.idx);
                Opnd v = gen_expr(u, e->u.set.src);
                out_try(u, "a_setindex(%s, %s, %s)", t.c, k.c, v.c);
                opnd_rel(u, k);
                opnd_rel(u, v);
                break;
            }
            case IDX_LAST: {
                Opnd v = gen_expr(u, e->u.set.src);
                out_try(u, "a_setindex_last(%s, %s)", t.c, v.c);
                opnd_rel(u, v);
                break;
            }
            case IDX_NEXT: {
                Opnd v = gen_expr(u, e->u.set.src);
                out_try(u, "a_setindex_next(%s, %s)", t.c, v.c);
                opnd_rel(u, v);
                break;
            }
        }
        opnd_rel(u, t);

    } else {
        err(dst->tk, "assignment : not implemented");
    }
    return opnd_c("a_nil()");
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

// [ e, id=e, @(k)=e ]: positional entries are numbered from 0.
static Opnd gen_table (Unit* u, Expr* e) {
    Opnd d = opnd_tmp(u);
    out(u, "a_set(&%s, a_tab());", d.c);
    int n = 0;
    for (int i=0; i<e->u.table.vs.n; i++) {
        Expr* key = e->u.table.ks.v[i];
        if (key == NULL) {
            Opnd v = gen_expr(u, e->u.table.vs.v[i]);
            out(u, "a_tab_set(A_TAB_OF(%s), a_num(%d), %s);", d.c, n++, v.c);
            opnd_rel(u, v);
        } else {
            Opnd k = gen_expr(u, key);
            Opnd v = gen_expr(u, e->u.table.vs.v[i]);
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
    const char* op = e->tk.str;

    if (!strcmp(op,"&&") || !strcmp(op,"||")) {
        return gen_logic(u, e);
    }

    Opnd x = gen_expr(u, e->u.bin.e1);
    Opnd y = gen_expr(u, e->u.bin.e2);
    Opnd d = opnd_tmp(u);

    if (!strcmp(op,"==") || !strcmp(op,"!=")) {
        out(u, "a_set(&%s, a_bool(%sa_eq(%s, %s)));",
            d.c, (op[0] == '!') ? "!" : "", x.c, y.c);
    } else {
        const char* f = NULL;
        int swap = 0;
        for (int i=0; OPS[i][0]!=NULL; i++) {
            if (!strcmp(OPS[i][0], op)) {
                f = OPS[i][1];
                swap = (OPS[i][2][0] != '\0');
            }
        }
        if (f == NULL) {
            err(e->tk, "operator not implemented");
        }
        out_try(u, "%s(&%s, %s, %s)",
            f, d.c, swap ? y.c : x.c, swap ? x.c : y.c);
    }
    opnd_rel(u, x);
    opnd_rel(u, y);
    return d;
}

static Opnd gen_call (Unit* u, Expr* e) {
    List args = { 0, NULL };
    flatten(&args, &e->u.call.es);

    // do(...): evaluates and discards
    if (e->u.call.sp == SP_DO) {
        for (int i=0; i<args.n; i++) {
            opnd_rel(u, gen_expr(u,args.v[i]));
        }
        return opnd_c("a_nil()");
    } else if (e->u.call.sp != SP_NONE) {
        err(e->tk, "statement not implemented");
    }

    Opnd  f  = gen_expr(u, e->u.call.f);
    Opnd* xs = xalloc(args.n * sizeof(Opnd));
    for (int i=0; i<args.n; i++) {
        xs[i] = gen_expr(u, args.v[i]);
    }

    Opnd d = opnd_tmp(u);
    out(u, "{");
    u->ind++;
    out(u, "int b = a_top;");
    for (int i=0; i<args.n; i++) {
        opnd_push(u, xs[i]);
    }
    out_try(u, "a_call(%s, b)", f.c);
    out(u, "a_ret1(&%s, b);", d.c);
    u->ind--;
    out(u, "}");
    opnd_rel(u, f);
    return d;
}

static Opnd gen_expr (Unit* u, Expr* e) {
    switch (e->tag) {
        case E_NIL:
            return opnd_c("a_nil()");
        case E_BOOL:
            return opnd_c(!strcmp(e->tk.str,"true") ?
                "a_bool(1)" : "a_bool(0)");
        case E_NUM:
            return opnd_c(xfmt("a_num(%.17g)", strtod(e->tk.str,NULL)));
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
        case E_CALL:
            return gen_call(u, e);
        case E_DCL:
            return gen_dcl(u, e);
        case E_SET:
            return gen_set(u, e);
        case E_BLOCK:
            return gen_block(u, &e->u.block.es);
        case E_DO:
            if (e->u.xdo.esc) {
                err(e->tk, "escape : not implemented");
            }
            return gen_block(u, &e->u.xdo.blk->u.block.es);
        default:
            err(e->tk, "expression not implemented");
            return opnd_c(NULL);
    }
}

///////////////////////////////////////////////////////////////////////////////
// MAIN
///////////////////////////////////////////////////////////////////////////////

char* coder_main (Expr* prog, Var** globals, int nglobals) {
    Unit u;
    memset(&u, 0, sizeof(u));

    // the value of the main block is discarded
    opnd_rel(&u, gen_block(&u, &prog->u.block.es));

    int n = (u.nslots > 0) ? u.nslots : 1;
    Buf c = { NULL, 0, 0 };
    buf_add(&c, "#include \"atmos.h\"\n\n");

    // top-level variables
    for (int i=0; i<nglobals; i++) {
        buf_add(&c, xfmt("static AVal %s;\n", var_c(&u,globals[i])));
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

    buf_add(&c, "AStatus atm_main (void) {\n");
    buf_add(&c, xfmt("    AVal L[%d];\n", n));
    buf_add(&c, xfmt("    a_nils(L, %d);\n", n));
    buf_add(&c, "    AStatus st = A_OK;\n");
    buf_add(&c, "    int base = a_top;\n");
    buf_add(&c, "    consts_init();\n");
    buf_add(&c, u.body.s ? u.body.s : "");
    buf_add(&c, "    goto CL_0;\n");
    buf_add(&c, "CL_0:\n");
    buf_add(&c, "    if (st != A_OK) {\n");
    buf_add(&c, "        a_settop(base);\n");
    buf_add(&c, "    }\n");
    buf_add(&c, xfmt("    a_clrs(L, %d);\n", n));
    for (int i=nglobals-1; i>=0; i--) {
        buf_add(&c, xfmt("    a_clr(&%s);\n", var_c(&u,globals[i])));
    }
    buf_add(&c, xfmt("    a_clrs(K, %d);\n", KN));
    buf_add(&c, "    return st;\n");
    buf_add(&c, "}\n");
    return c.s;
}
