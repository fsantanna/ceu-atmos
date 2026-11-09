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

int OPT_TEST = 0;

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
static Expr* parser_2_suf (Expr* pre);
static Expr* parser_3_met (Expr* pre);
static Expr* parser_4_pre (Expr* pre);
static Expr* parser_5_bin (Expr* pre);
static Expr* parser_6_pip (Expr* pre);

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

// { e1 ; e2 }
static Expr* parser_block (void) {
    accept_str_err("{");
    Expr* e = mk(E_BLOCK, TK0);
    e->u.block.es = parser_stmts("}");
    accept_str_err("}");
    return e;
}

// A single expression as a block.
static Expr* block1 (Expr* x) {
    Expr* e = mk(E_BLOCK, x->tk);
    list_add(&e->u.block.es, x);
    return e;
}

static Tk tk_id (Tk tk, const char* str) {
    tk.tag = TK_ID;
    tk.str = str;
    tk.len = strlen(str);
    return tk;
}

///////////////////////////////////////////////////////////////////////////////
// PROTOS
///////////////////////////////////////////////////////////////////////////////

// Parameters, after '(': ids separated by commas, optional `...`.
static void parser_pars (Expr* p) {
    while (!check_str(")")) {
        if (accept_str("...")) {
            p->u.proto.dots = 1;
            break;
        }
        accept_tag_err(TK_ID);
        int n = p->u.proto.n;
        p->u.proto.pars = realloc(p->u.proto.pars, (n+1) * sizeof(Tk));
        p->u.proto.pars[n] = TK0;
        p->u.proto.n++;
        if (!accept_str(",")) {
            break;
        }
    }
    accept_str_err(")");
}

static void proto_par (Expr* p, Tk id, int first) {
    int n = p->u.proto.n;
    p->u.proto.pars = realloc(p->u.proto.pars, (n+1) * sizeof(Tk));
    if (first) {
        memmove(&p->u.proto.pars[1], &p->u.proto.pars[0], n * sizeof(Tk));
        p->u.proto.pars[0] = id;
    } else {
        p->u.proto.pars[n] = id;
    }
    p->u.proto.n++;
}

// \{...}  \x{...}  \(x,y){...}  \+  \-
static Expr* parser_lambda (void) {
    accept_str_err("\\");
    Expr* p = mk(E_PROTO, TK0);

    // operator lambda: \- is \(a,b){ a - b }
    if (accept_tag(TK_OP)) {
        Tk op = TK0;
        Tk a = tk_id(op, "a");
        Tk b = tk_id(op, "b");
        Expr* e = NULL;
        if (in(BINS, op.str)) {
            proto_par(p, a, 0);
            proto_par(p, b, 0);
            e = mk(E_BIN, op);
            e->u.bin.e1 = mk(E_ACC, a);
            e->u.bin.e2 = mk(E_ACC, b);
        } else if (in(UNOS, op.str)) {
            proto_par(p, a, 0);
            e = mk(E_UNO, op);
            e->u.uno.e = mk(E_ACC, a);
        } else {
            err(op, "lambda error : invalid operator");
        }
        p->u.proto.blk = block1(e);
        return p;
    }

    if (accept_str("(")) {
        parser_pars(p);
    } else if (accept_tag(TK_ID)) {
        proto_par(p, TK0, 0);
    } else {
        proto_par(p, tk_id(TK0,"it"), 0);
    }
    if (!check_str("{")) {
        err(TK1, "expected '{'");
    }
    p->u.proto.blk = parser_block();
    return p;
}

// After `func` or `task`:
//  (...) {...}             anonymous
//  f (...) {...}           set f = func
//  t.x.f (...) {...}       set t.x.f = func
//  o::f (...) {...}        set o.f = func (self, ...)
static Expr* parser_proto (void) {
    Tk tk = TK0;
    int task = !strcmp(tk.str, "task");

    // bare `task` is the running task: a prototype needs a `(` or a
    // name in the same line
    int hdr = (check_str("(") || check_tag(TK_ID)) && TK0.sep==TK1.sep;
    if (task && !hdr) {
        return mk(E_ME, tk);
    }

    Expr* p = mk(E_PROTO, tk);
    p->u.proto.task = task;

    if (accept_str("(")) {
        parser_pars(p);
        p->u.proto.blk = parser_block();
        return p;
    }

    accept_tag_err(TK_ID);
    Expr* dst = mk(E_ACC, TK0);
    int met = 0;
    while (1) {
        if (accept_str(".")) {
            // ok
        } else if (!task && accept_str("::")) {
            met = 1;
        } else {
            break;
        }
        Expr* idx = mk(E_INDEX, TK0);
        accept_field_err();
        idx->u.index.t    = dst;
        idx->u.index.kind = IDX_EXPR;
        idx->u.index.idx  = mk(E_STR, TK0);
        dst = idx;
        if (met) {
            break;
        }
    }

    accept_str_err("(");
    parser_pars(p);
    if (met) {
        proto_par(p, tk_id(tk,"self"), 1);
    }
    p->u.proto.blk = parser_block();

    Expr* set = mk(E_SET, tk);
    list_add(&set->u.set.dsts, dst);
    set->u.set.src   = p;
    set->u.set.proto = 1;
    return set;
}

///////////////////////////////////////////////////////////////////////////////
// STATEMENTS
///////////////////////////////////////////////////////////////////////////////

// val x  |  var x, y = e  |  val func f () {...}
static Expr* parser_dcl (void) {
    Expr* dcl = mk(E_DCL, TK0);

    if (!strcmp(TK0.str,"val") &&
        (accept_str("func") || accept_str("task")))
    {
        Expr* p = mk(E_PROTO, TK0);
        p->u.proto.task = !strcmp(TK0.str, "task");
        accept_tag_err(TK_ID);
        dcl->u.dcl.n   = 1;
        dcl->u.dcl.ids = xalloc(sizeof(Tk));
        dcl->u.dcl.ids[0] = TK0;
        accept_str_err("(");
        parser_pars(p);
        p->u.proto.blk = parser_block();
        dcl->u.dcl.set = p;
        return dcl;
    }

    dcl->u.dcl.n = parser_ids(&dcl->u.dcl.ids);
    if (accept_str("=")) {
        dcl->u.dcl.set = parser();
    }
    return dcl;
}

// set x, t@1 = e
static Expr* parser_set (void) {
    Expr* set = mk(E_SET, TK0);
    do {
        Tk tk = TK1;
        Expr* e = parser();
        if (e->tag!=E_ACC && e->tag!=E_INDEX && e->tag!=E_NAT) {
            err(tk, "expected assignable expression");
        }
        list_add(&set->u.set.dsts, e);
    } while (accept_str(","));
    accept_str_err("=");
    set->u.set.src = parser();
    return set;
}

// Body of a case: block, lambda, or expression.
static Expr* parser_case_body (void) {
    if (check_str("{")) {
        return parser_block();
    } else if (check_str("\\")) {
        return parser_lambda();
    } else {
        return block1(parser());
    }
}

static void ifs_add (Expr* ifs, Expr* cnd, int test, Expr* body) {
    int n = ifs->u.ifs.n;
    ifs->u.ifs.cases = realloc(ifs->u.ifs.cases, (n+1) * sizeof(Case));
    ifs->u.ifs.cases[n].cnd  = cnd;
    ifs->u.ifs.cases[n].test = test;
    ifs->u.ifs.cases[n].body = body;
    ifs->u.ifs.n++;
}

// if x {...} else {...}
// if x => \{...} else {...}
// if x => y => z
static Expr* parser_if (void) {
    Expr* ifs = mk(E_IFS, TK0);
    Expr* cnd = parser();
    if (check_str("{")) {
        ifs_add(ifs, cnd, 0, parser_block());
        if (accept_str("else")) {
            ifs_add(ifs, NULL, 0, parser_block());
        }
    } else {
        accept_str_err("=>");
        if (check_str("\\")) {
            ifs_add(ifs, cnd, 0, parser_lambda());
            if (accept_str("else")) {
                ifs_add(ifs, NULL, 0, parser_block());
            }
        } else {
            ifs_add(ifs, cnd, 0, block1(parser()));
            if (accept_str("=>")) {
                ifs_add(ifs, NULL, 0, block1(parser()));
            }
        }
    }
    return ifs;
}

// ifs { x => a ; y => b ; else => c }
// match e { x => a ; \{...} => b ; else => c }
static Expr* parser_ifs (int match) {
    Expr* ifs = mk(E_IFS, TK0);
    if (match) {
        ifs->u.ifs.head = parser();
    }
    accept_str_err("{");
    while (!check_str("}")) {
        Expr* cnd  = NULL;
        int   test = 0;
        int   els  = accept_str("else");
        if (els) {
            // last case
        } else if (match && check_str("\\")) {
            cnd  = parser_lambda();
            test = 1;
        } else {
            cnd = parser();
        }
        accept_str_err("=>");
        ifs_add(ifs, cnd, test, parser_case_body());
        if (els) {
            break;
        }
    }
    accept_str_err("}");
    return ifs;
}

// loop {...}  |  loop i {...}  |  loop k,v in e {...}
static Expr* parser_loop (void) {
    Expr* loop = mk(E_LOOP, TK0);
    if (check_tag(TK_ID)) {
        loop->u.loop.n = parser_ids(&loop->u.loop.ids);
    }
    if (check_str("on")) {
        err(TK1, "loop on : not implemented");
    }
    if (accept_str("in")) {
        loop->u.loop.itr = parser();
    }
    loop->u.loop.blk = parser_block();
    return loop;
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

    // func, task, lambda
    } else if (accept_str("func") || accept_str("task")) {
        return parser_proto();
    } else if (check_str("\\")) {
        return parser_lambda();

    // val x = 10
    } else if (accept_str("val") || accept_str("var") || accept_str("pin")) {
        return parser_dcl();

    // set x = 10
    } else if (accept_str("set")) {
        return parser_set();

    // do :X {...}  do {...}  do(...)
    } else if (accept_str("do")) {
        Tk tk = TK0;
        if (check_tag(TK_TAG) || check_str("{")) {
            Expr* e = mk(E_DO, tk);
            if (accept_tag(TK_TAG)) {
                e->tk = TK0;
                e->u.xdo.esc = 1;
            }
            e->u.xdo.blk = parser_block();
            return e;
        } else {
            // do(e): evaluates and discards
            Expr* cmd = mk(E_ACC, tk);
            Expr* call = parser_2_suf(cmd);
            call = parser_6_pip(parser_5_bin(parser_3_met(call)));
            if (call->tag != E_CALL) {
                err(tk, "expected call syntax");
            }
            return call;
        }

    // test {...}
    } else if (accept_str("test")) {
        Expr* e = mk(E_DO, TK0);
        e->u.xdo.blk = parser_block();
        if (!OPT_TEST) {
            e->u.xdo.blk->u.block.es.n = 0;
        }
        return e;

    // catch e {...}
    } else if (accept_str("catch")) {
        Expr* e = mk(E_CATCH, TK0);
        e->u.xcatch.cnd = parser();
        e->u.xcatch.blk = parser_block();
        return e;

    // defer {...}
    } else if (accept_str("defer")) {
        Expr* e = mk(E_DEFER, TK0);
        e->u.defer.blk = parser_block();
        return e;

    // if, ifs, match
    } else if (accept_str("if")) {
        return parser_if();
    } else if (accept_str("ifs")) {
        return parser_ifs(0);
    } else if (accept_str("match")) {
        return parser_ifs(1);

    // loop
    } else if (accept_str("loop")) {
        return parser_loop();

    // until(...)  while(...)
    } else if (accept_str("until") || accept_str("while")) {
        return mk(E_ACC, TK0);

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
