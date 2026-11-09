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
    // cleanup label of the enclosing block
    int cleanup;
    // extra indentation of the generated lines
    int ind;
} Unit;

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

// Global variables provided by the runtime.
static const char* GLOBALS[][2] = {
    { "print", "atm_g_print" },
    { NULL, NULL }
};

static Opnd gen_expr (Unit* u, Expr* e);

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

static Opnd gen_acc (Unit* u, Expr* e) {
    (void) u;
    for (int i=0; GLOBALS[i][0]!=NULL; i++) {
        if (!strcmp(GLOBALS[i][0], e->tk.str)) {
            return opnd_c(GLOBALS[i][1]);
        }
    }
    err(e->tk, "undeclared variable");
    return opnd_c(NULL);
}

static Opnd gen_uno (Unit* u, Expr* e) {
    Opnd x = gen_expr(u, e->u.uno.e);
    Opnd d = opnd_tmp(u);
    if (!strcmp(e->tk.str, "-")) {
        out(u, "if ((st = a_neg(&%s, %s)) != A_OK) goto CL_%d;",
            d.c, x.c, u->cleanup);
    } else if (!strcmp(e->tk.str, "!")) {
        out(u, "a_not(&%s, %s);", d.c, x.c);
    } else {
        err(e->tk, "operator not implemented");
    }
    opnd_rel(u, x);
    return d;
}

static Opnd gen_bin (Unit* u, Expr* e) {
    static const char* OPS[][2] = {
        { "+",  "a_add"  }, { "-",  "a_sub" }, { "*",  "a_mul" },
        { "/",  "a_div"  }, { "//", "a_idiv" }, { "%", "a_mod" },
        { "**", "a_pow"  },
        { NULL, NULL }
    };
    const char* f = NULL;
    for (int i=0; OPS[i][0]!=NULL; i++) {
        if (!strcmp(OPS[i][0], e->tk.str)) {
            f = OPS[i][1];
        }
    }
    if (f == NULL) {
        err(e->tk, "operator not implemented");
    }
    Opnd x = gen_expr(u, e->u.bin.e1);
    Opnd y = gen_expr(u, e->u.bin.e2);
    Opnd d = opnd_tmp(u);
    out(u, "if ((st = %s(&%s, %s, %s)) != A_OK) goto CL_%d;",
        f, d.c, x.c, y.c, u->cleanup);
    opnd_rel(u, x);
    opnd_rel(u, y);
    return d;
}

static Opnd gen_call (Unit* u, Expr* e) {
    List args = { 0, NULL };
    flatten(&args, &e->u.call.es);

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
    out(u, "if ((st = a_call(%s, b)) != A_OK) goto CL_%d;",
        f.c, u->cleanup);
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
        case E_PARENS:
            return gen_expr(u, e->u.parens.e);
        case E_UNO:
            return gen_uno(u, e);
        case E_BIN:
            return gen_bin(u, e);
        case E_CALL:
            return gen_call(u, e);
        default:
            err(e->tk, "expression not implemented");
            return opnd_c(NULL);
    }
}

///////////////////////////////////////////////////////////////////////////////
// MAIN
///////////////////////////////////////////////////////////////////////////////

char* coder_main (Expr* prog) {
    Unit u;
    memset(&u, 0, sizeof(u));

    // each statement is evaluated and its value discarded
    for (int i=0; i<prog->u.block.es.n; i++) {
        Opnd o = gen_expr(&u, prog->u.block.es.v[i]);
        opnd_rel(&u, o);
    }

    int n = (u.nslots > 0) ? u.nslots : 1;
    Buf c = { NULL, 0, 0 };
    buf_add(&c, "#include \"atmos.h\"\n\n");

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
    buf_add(&c, xfmt("    a_clrs(K, %d);\n", KN));
    buf_add(&c, "    return st;\n");
    buf_add(&c, "}\n");
    return c.s;
}
