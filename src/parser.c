// Atmos compiler: recursive descent parser, 7 precedence levels.
//
// 7_out : v where {...}
// 6_pip : v --> f     f <-- v
// 5_bin : a + b
// 4_pre : -a
// 3_met : v->f    f<-v
// 2_suf : v@(0)   v.x    x::m()   f()
//         :X() :X[]
//         f[] f"" f``
// 1_prim

#include <stdlib.h>
#include <string.h>

#include "atmc.h"

static Tk TK0;
static Tk TK1;

static const char* UNOS[] = { "-", "#", "!", NULL };

static const char* BINS[] = {
    "==", "!=", "===", "=!=", "=>=", "=<=",
    "??", "!?",
    "+", "-", "*", "**", "/", "//", "%",
    ">", "<", ">=", "<=",
    "||", "&&",
    "++",
    "?>", "!>",
    NULL
};

static Expr* parser (void);
static Expr* parser_1_prim (void);

///////////////////////////////////////////////////////////////////////////////
// AUX
///////////////////////////////////////////////////////////////////////////////

void list_add (List* l, Expr* e) {
    list_ins(l, l->n, e);
}

void list_ins (List* l, int i, Expr* e) {
    l->v = realloc(l->v, (l->n+1) * sizeof(Expr*));
    memmove(&l->v[i+1], &l->v[i], (l->n-i) * sizeof(Expr*));
    l->v[i] = e;
    l->n++;
}

static int in (const char** set, const char* s) {
    for (int i=0; set[i]!=NULL; i++) {
        if (!strcmp(set[i], s)) {
            return 1;
        }
    }
    return 0;
}

static Expr* mk (ETag tag, Tk tk) {
    Expr* e = xalloc(sizeof(Expr));
    e->tag = tag;
    e->tk  = tk;
    return e;
}

static void next (void) {
    TK0 = TK1;
    lexer_token(&TK1);
}

// Literal tokens only match by tag: their contents are data.
static int is_lit (TkTag tag) {
    return tag==TK_STR || tag==TK_NAT || tag==TK_NUM || tag==TK_CLK;
}

static int check_str (const char* str) {
    return !is_lit(TK1.tag) && !strcmp(TK1.str,str);
}

static int check_tag (TkTag tag) {
    return TK1.tag == tag;
}

static int accept_str (const char* str) {
    if (check_str(str)) {
        next();
        return 1;
    }
    return 0;
}

static int accept_tag (TkTag tag) {
    if (check_tag(tag)) {
        next();
        return 1;
    }
    return 0;
}

static void accept_str_err (const char* str) {
    if (!accept_str(str)) {
        err(TK1, "expected '%s'", str);
    }
}

static void accept_tag_err (TkTag tag) {
    if (!accept_tag(tag)) {
        err(TK1, "expected <%s>", lexer_tag(tag));
    }
}

// Field name: keyword or identifier.
static void accept_field_err (void) {
    if (!accept_tag(TK_KEY)) {
        accept_tag_err(TK_ID);
    }
}

static int check_op (const char** set) {
    return check_tag(TK_OP) && in(set, TK1.str);
}

// Comma-separated expressions up to (not including) `clo`.
// A trailing comma is accepted.
static List parser_list (const char* clo) {
    List l = { 0, NULL };
    if (check_str(clo)) {
        return l;
    }
    list_add(&l, parser());
    while (accept_str(",")) {
        if (check_str(clo)) {
            break;
        }
        list_add(&l, parser());
    }
    return l;
}

// Sequence of expressions up to (not including) `clo`.
static List parser_stmts (const char* clo) {
    List l = { 0, NULL };
    while (!check_str(clo)) {
        if (l.n>0 && TK0.sep==TK1.sep) {
            err(TK1, "sequence error : expected ';' or new line");
        }
        list_add(&l, parser());
    }
    return l;
}

// Identifiers separated by commas.
static int parser_ids (Tk** ids) {
    int n = 0;
    *ids = NULL;
    do {
        accept_tag_err(TK_ID);
        *ids = realloc(*ids, (n+1) * sizeof(Tk));
        (*ids)[n++] = TK0;
    } while (accept_str(","));
    return n;
}

///////////////////////////////////////////////////////////////////////////////
// 1_PRIM
///////////////////////////////////////////////////////////////////////////////

// Tokens that can start a juxtaposed call argument: f "s", f [t], f :X.
static int check_call_arg (void) {
    return check_str("[") || check_str("\\") ||
           check_tag(TK_STR) || check_tag(TK_TAG) ||
           check_tag(TK_NAT) || check_tag(TK_CLK);
}

// @-qualifier, after '@': @(e) | @num | @id | @:tag.
// Returns NULL on no match.
static Expr* parser_at (void) {
    if (accept_str("(")) {
        Expr* e = parser();
        accept_str_err(")");
        return e;
    } else if (accept_tag(TK_TAG)) {
        return mk(E_TAG, TK0);
    } else if (check_tag(TK_NUM) || check_tag(TK_ID)) {
        return parser_1_prim();
    } else {
        return NULL;
    }
}

static Expr* parser_at_err (void) {
    Expr* e = parser_at();
    if (e == NULL) {
        err(TK1, "expected name, number, tag, or '('");
    }
    return e;
}

// [ e, id=e, @(k)=e ]
static Expr* parser_table (void) {
    Expr* t = mk(E_TABLE, TK0);
    while (!check_str("]")) {
        Expr* key = NULL;
        Expr* val;
        if (accept_str("@")) {
            key = parser_at_err();
            accept_str_err("=");
            val = parser();
        } else {
            val = parser();
            if (val->tag==E_ACC && accept_str("=")) {
                key = mk(E_STR, val->tk);
                val = parser();
            }
        }
        list_add(&t->u.table.ks, key);
        list_add(&t->u.table.vs, val);
        if (!accept_str(",")) {
            break;
        }
    }
    accept_str_err("]");
    return t;
}

static Expr* parser_1_prim (void) {
    // literals: nil, true, false, ..., num, str, nat, clk
    if (accept_str("nil")) {
        return mk(E_NIL, TK0);
    } else if (accept_str("true") || accept_str("false")) {
        return mk(E_BOOL, TK0);
    } else if (accept_str("...")) {
        return mk(E_DOTS, TK0);
    } else if (accept_tag(TK_NUM)) {
        return mk(E_NUM, TK0);
    } else if (accept_tag(TK_STR)) {
        return mk(E_STR, TK0);
    } else if (accept_tag(TK_NAT)) {
        return mk(E_NAT, TK0);
    } else if (accept_tag(TK_CLK)) {
        return mk(E_CLK, TK0);

    // id: x, __v
    } else if (accept_tag(TK_ID)) {
        return mk(E_ACC, TK0);

    // tag: :X  :X [...]  :X (...)
    } else if (accept_tag(TK_TAG)) {
        Tk tag = TK0;
        if ((check_str("(") || check_str("[")) && TK0.sep==TK1.sep) {
            Expr* e = mk(E_TAGGED, tag);
            e->u.tagged.e = parser_1_prim();
            return e;
        } else {
            return mk(E_TAG, tag);
        }

    // table: [...]
    } else if (accept_str("[")) {
        return parser_table();

    // parens: (e)  (e1,e2)  ()
    } else if (accept_str("(")) {
        Tk tk = TK0;
        List es = parser_list(")");
        accept_str_err(")");
        if (es.n == 1) {
            Expr* e = mk(E_PARENS, tk);
            e->u.parens.e = es.v[0];
            return e;
        } else {
            Expr* e = mk(E_LIST, tk);
            e->u.list.es = es;
            return e;
        }

    } else {
        err(TK1, "expected expression");
        return NULL;
    }
}

///////////////////////////////////////////////////////////////////////////////
// 2_SUF
///////////////////////////////////////////////////////////////////////////////

static int is_prefix (Expr* e) {
    switch (e->tag) {
        case E_TAG:
        case E_ACC:
        case E_NAT:
        case E_CALL:
        case E_MET:
        case E_INDEX:
        case E_PARENS:
        case E_TAGGED:
            return 1;
        default:
            return 0;
    }
}

static Expr* parser_2_suf (Expr* pre) {
    int no = check_str("emit") || check_str("spawn") ||
             check_str("toggle") || check_str("thread");
    Expr* e = pre ? pre : parser_1_prim();

    while (1) {
        int ok = !no && is_prefix(e) && (
            TK0.sep==TK1.sep ||
            check_str("@") || check_str(".") || check_str("::")
        );
        if (!ok) {
            return e;
        }

        Expr* ret;
        if (accept_str("@")) {
            // t@(e)  t@1  t@i  t@#  t@+
            ret = mk(E_INDEX, TK0);
            ret->u.index.t = e;
            ret->u.index.idx = parser_at();
            if (ret->u.index.idx != NULL) {
                ret->u.index.kind = IDX_EXPR;
            } else if (accept_str("#")) {
                ret->u.index.kind = IDX_LAST;
            } else if (accept_str("+")) {
                ret->u.index.kind = IDX_NEXT;
            } else {
                err(TK1, "expected name, number, or '('");
            }
        } else if (accept_str(".")) {
            // t.id
            ret = mk(E_INDEX, TK0);
            accept_field_err();
            ret->u.index.t    = e;
            ret->u.index.kind = IDX_EXPR;
            ret->u.index.idx  = mk(E_STR, TK0);
        } else if (accept_str("::")) {
            // o::m
            ret = mk(E_MET, TK0);
            accept_field_err();
            ret->u.met.o   = e;
            ret->u.met.met = TK0;
            if (!check_call_arg() && !check_str("(")) {
                err(TK1, "expected '('");
            }
        } else if (accept_str("(")) {
            // f(...)
            ret = mk(E_CALL, TK0);
            ret->u.call.f  = e;
            ret->u.call.es = parser_list(")");
            accept_str_err(")");
        } else if (check_call_arg()) {
            // f "s"  f [t]  f :X
            ret = mk(E_CALL, TK1);
            ret->u.call.f = e;
            list_add(&ret->u.call.es, parser_1_prim());
        } else {
            return e;
        }
        e = ret;
    }
}

///////////////////////////////////////////////////////////////////////////////
// 3_MET .. 7_OUT
///////////////////////////////////////////////////////////////////////////////

// Pipes `e` into `f`: as an extra first/last argument if `f` is
// already a call, or as its single argument otherwise.
static Expr* pipe (Tk tk, Expr* f, Expr* e, int first) {
    Expr* call = f;
    while (call->tag == E_PARENS) {
        call = call->u.parens.e;
    }
    if (call->tag == E_CALL) {
        list_ins(&call->u.call.es, first ? 0 : call->u.call.es.n, e);
        return f;
    } else {
        Expr* ret = mk(E_CALL, tk);
        ret->u.call.f = f;
        list_add(&ret->u.call.es, e);
        return ret;
    }
}

static Expr* parser_3_met (Expr* pre) {
    Expr* e = pre ? pre : parser_2_suf(NULL);
    while (accept_str("->")) {
        Tk tk = TK0;
        e = pipe(tk, parser_2_suf(NULL), e, 1);
    }
    if (accept_str("<-")) {
        Tk tk = TK0;
        return pipe(tk, e, parser_3_met(NULL), 0);
    }
    return e;
}

static Expr* parser_4_pre (Expr* pre) {
    if (pre!=NULL || !check_op(UNOS)) {
        return parser_3_met(pre);
    }
    next();
    Expr* e = mk(E_UNO, TK0);
    e->u.uno.e = parser_4_pre(NULL);
    return e;
}

static Expr* parser_5_bin (Expr* pre) {
    Expr* e1 = pre ? pre : parser_4_pre(NULL);
    int chain = 0;
    while (check_op(BINS)) {
        next();
        Tk op = TK0;
        if (chain && strcmp(e1->tk.str,op.str)) {
            err(op, "operation error : use parentheses to disambiguate");
        }
        Expr* e = mk(E_BIN, op);
        e->u.bin.e1 = e1;
        e->u.bin.e2 = parser_4_pre(NULL);
        e1 = e;
        chain = 1;
    }
    return e1;
}

static void check_mix (Expr* pre, const char* op) {
    if (pre->out!=NULL && strcmp(pre->out,op)) {
        err(TK1, "operation error : use parentheses to disambiguate");
    }
}

static Expr* parser_6_pip (Expr* pre) {
    Expr* e = pre ? pre : parser_5_bin(NULL);
    while (1) {
        if (check_str("-->")) {
            check_mix(e, "-->");
            next();
            Tk tk = TK0;
            e = pipe(tk, parser_5_bin(NULL), e, 1);
            e->out = "-->";
        } else if (check_str("<--")) {
            // right to left
            check_mix(e, "<--");
            next();
            Tk tk = TK0;
            Expr* arg = parser_6_pip(NULL);
            if (arg->out!=NULL && !strcmp(arg->out,"-->")) {
                err(tk, "operation error : use parentheses to disambiguate");
            }
            return pipe(tk, e, arg, 0);
        } else {
            return e;
        }
    }
}

static Expr* parser_7_out (Expr* pre) {
    Expr* e = pre ? pre : parser_6_pip(NULL);
    while (accept_str("where")) {
        Expr* ret = mk(E_WHERE, TK0);
        accept_str_err("{");
        while (!check_str("}")) {
            Expr* dcl = mk(E_DCL, TK0);
            dcl->tk.str = "val";
            dcl->u.dcl.n = parser_ids(&dcl->u.dcl.ids);
            accept_str_err("=");
            dcl->u.dcl.set = parser();
            list_add(&ret->u.where.dcls, dcl);
        }
        accept_str_err("}");
        ret->u.where.e = e;
        e = ret;
    }
    return e;
}

static Expr* parser (void) {
    return parser_7_out(NULL);
}

Expr* parser_main (void) {
    lexer_token(&TK1);
    TK0 = TK1;
    Expr* e = mk(E_BLOCK, TK1);
    e->u.block.es = parser_stmts("<eof>");
    accept_str_err("<eof>");
    return e;
}
