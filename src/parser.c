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
const char* OPT_ENV = NULL;

static Tk TK0;
static Tk TK1;

static const char* UNOS[] = { "-", "#", "!", "~", NULL };

static const char* BINS[] = {
    "==", "!=",
    "??", "!?",
    "+", "-", "*", "/", "//", "%",
    ">", "<", ">=", "<=",
    "||", "&&",
    "++",
    "&", "|", "^", "<<", ">>",
    NULL
};

static Expr* parser (void);
static Expr* parser_1_prim (void);
static Expr* parser_7_out (Expr* pre);
static Patt* parser_await (void);
static Patt* parser_await_blk (int blk);
static Expr* parser_spawn (void);
static Expr* parser_at_err (void);
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
// Inside the delimiters `<` and `>` of an await pattern, a bare `>`
// closes the pattern: it is not the operator, unless the expression is
// enclosed by its own delimiters.
static int ANGLE = 0;

static List parser_list (const char* clo) {
    List l = { 0, NULL };
    if (check_str(clo)) {
        return l;
    }
    int angle = ANGLE;
    ANGLE = 0;
    list_add(&l, parser());
    while (accept_str(",")) {
        if (check_str(clo)) {
            break;
        }
        list_add(&l, parser());
    }
    ANGLE = angle;
    return l;
}

// Sequence of expressions up to (not including) `clo`.
static List parser_stmts (const char* clo);

// include "file.atm": the statements of the file take the place of
// the include, as if they were written there. The path is relative to
// the file with the include. A file is only included once.
static void include_file (List* l, Tk tk, const char* path);

static void parser_include (List* l) {
    Tk tk = TK0;
    if (!accept_tag(TK_STR)) {
        err(TK1, "expected file name");
    }

    // relative to the including file
    const char* cur = FILES[tk.file];
    const char* sla = strrchr(cur, '/');
    const char* path = TK0.str;
    if (path[0]!='/' && sla!=NULL) {
        path = xfmt("%.*s/%s", (int)(sla-cur), cur, TK0.str);
    }
    include_file(l, TK0, path);
}

// Adds the statements of a file to `l`, unless it was included before.
// `tk` is where an error is reported.
static void include_file (List* l, Tk tk, const char* path) {
    static char* done[FILES_MAX];
    static int   ndone = 0;

    char* real = realpath(path, NULL);
    size_t len = 0;
    char* src = (real == NULL) ? NULL : file_read(real, &len);
    if (src == NULL) {
        err(tk, "invalid include : cannot read file");
    }
    for (int i=0; i<ndone; i++) {
        if (!strcmp(done[i], real)) {
            return;
        }
    }
    if (ndone == FILES_MAX) {
        err(tk, "invalid include : too many files");
    }
    done[ndone++] = real;

    // reads the file in the middle of the current one
    Lex lex = lexer_save();
    Tk  tk0 = TK0;
    Tk  tk1 = TK1;
    lexer_init(path, src, len);
    lexer_token(&TK1);
    TK0 = TK1;
    List inc = parser_stmts("<eof>");
    for (int i=0; i<inc.n; i++) {
        list_add(l, inc.v[i]);
    }
    lexer_restore(lex);
    TK0 = tk0;
    TK1 = tk1;
}

static List parser_stmts (const char* clo) {
    List l = { 0, NULL };
    int angle = ANGLE;
    ANGLE = 0;
    int top = !strcmp(clo, "<eof>");
    while (!check_str(clo)) {
        if (l.n>0 && TK0.sep==TK1.sep) {
            err(TK1, "sequence error : expected ';' or new line");
        }
        if (check_str("include")) {
            if (!top) {
                err(TK1, "invalid include : expected top-level statement");
            }
            next();
            parser_include(&l);
            continue;
        }
        list_add(&l, parser());
    }
    ANGLE = angle;
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

// spawn {...}  |  spawn T(...)  |  spawn @ts T(...)
static Expr* parser_spawn (void) {
    accept_str_err("spawn");
    Tk tk = TK0;
    if (check_str("{")) {
        Expr* e = mk(E_SPAWN_BLK, tk);
        e->u.sblk.blk = parser_block();
        return e;
    }
    Expr* e = mk(E_SPAWN, tk);
    if (accept_str("@")) {
        e->u.spawn.pool = parser_at_err();
    }
    Expr* call = parser_6_pip(NULL);
    if (call->tag != E_CALL) {
        err(tk, "expected call syntax");
    }
    e->u.spawn.f    = call->u.call.f;
    e->u.spawn.args = call->u.call.es;
    return parser_7_out(e);
}

// val x  |  var x, y = e  |  val func f () {...}
// A token that may start an expression, in the same line as the
// previous one. It tells if a keyword such as `return` has arguments.
static int starts_expr (void) {
    static const char* NO[] = {
        "{", "}", ")", "]", ",", "=", "=>", ">", "else", "in", "on",
        "where", "with", NULL
    };
    static const char* PRE[] = { "-", "!", "#", "~", NULL };
    if (TK0.sep!=TK1.sep || check_tag(TK_EOF)) {
        return 0;
    }
    if (is_lit(TK1.tag)) {
        return 1;
    }
    if (check_tag(TK_OP)) {
        return in(PRE, TK1.str);
    }
    return !in(NO, TK1.str);
}

// e1, e2, ...: a list with no parentheses, as the single expression
// `(e1, e2, ...)`.
static Expr* parser_exprs (void) {
    Tk tk = TK1;
    Expr* e = parser();
    if (!check_str(",")) {
        return e;
    }
    Expr* l = mk(E_LIST, tk);
    list_add(&l->u.list.es, e);
    while (accept_str(",")) {
        list_add(&l->u.list.es, parser());
    }
    return l;
}

// Statements that look like calls take their arguments with or
// without parentheses, up to the end of the expression:
//      return              no values: nothing else in the line
//      return e
//      return e1, e2       same as return (e1, e2)
static const char* EXITS[] = {
    "return", "break", "escape", "throw", "abort", "until", "while",
    "skip", NULL
};

static Expr* parser_exit (Tk tk) {
    Expr* call = mk(E_CALL, tk);
    call->u.call.f = mk(E_ACC, tk);
    if (starts_expr()) {
        list_add(&call->u.call.es, parser());
        while (accept_str(",")) {
            list_add(&call->u.call.es, parser());
        }
    }
    // return (e1, e2) is return e1, e2
    List* es = &call->u.call.es;
    if (es->n==1 && es->v[0]->tag==E_LIST) {
        *es = es->v[0]->u.list.es;
    }
    return call;
}

static int is_exit (Expr* e) {
    return e->tag==E_CALL && e->u.call.f->tag==E_ACC &&
           in(EXITS, e->u.call.f->tk.str);
}

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
        dcl->u.dcl.rec = 1;
        return dcl;
    }

    dcl->u.dcl.n = parser_ids(&dcl->u.dcl.ids);
    if (accept_str("=")) {
        if (check_str("spawn")) {
            Expr* spw = parser_spawn();
            if (spw->tag == E_SPAWN_BLK) {
                // pin t = spawn {...}: the block is promoted to an
                // anonymous task with its own identity
                if (strcmp(dcl->tk.str, "pin")) {
                    err(dcl->tk,
                        "invalid assignment : unexpected transparent task");
                }
                Expr* p = mk(E_PROTO, spw->tk);
                p->u.proto.task = 1;
                p->u.proto.blk  = spw->u.sblk.blk;
                Expr* e = mk(E_SPAWN, spw->tk);
                e->u.spawn.f = p;
                spw = e;
            }
            dcl->u.dcl.set = spw;
        } else {
            dcl->u.dcl.set = parser_exprs();
        }
    }
    return dcl;
}

// Compound assignment operators.
static const char* SETS[] = {
    "+=", "-=", "*=", "/=", "//=", "%=", "++=",
    "&=", "|=", "^=", "<<=", ">>=", "&&=", "||=",
    NULL
};

// set x, t@1 = e  |  set x += e
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
    if (check_op(SETS)) {
        // a single destination, updated in place
        if (set->u.set.dsts.n != 1) {
            err(TK1, "invalid assignment : expected single destination");
        }
        next();
        set->u.set.op = xstrndup(TK0.str, TK0.len-1);
    } else {
        accept_str_err("=");
        set->u.set.src = parser_exprs();
        return set;
    }
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

// loop {...}  |  loop i {...}  |  loop k,v in e {...}  |  loop e on P {...}
static Expr* parser_loop (void) {
    Expr* loop = mk(E_LOOP, TK0);
    if (check_tag(TK_ID)) {
        loop->u.loop.n = parser_ids(&loop->u.loop.ids);
    }
    if (accept_str("on")) {
        // loop e on P {...} is loop { val e = await P ; ... }
        Tk tk = TK0;
        Expr* awt = mk(E_AWAIT, tk);
        awt->u.await.p = parser_await_blk(1);
        Expr* fst = awt;
        if (loop->u.loop.n > 0) {
            fst = mk(E_DCL, tk);
            fst->tk.str = "val";
            fst->u.dcl.n   = loop->u.loop.n;
            fst->u.dcl.ids = loop->u.loop.ids;
            fst->u.dcl.set = awt;
            loop->u.loop.n = 0;
        }
        loop->u.loop.blk = parser_block();
        list_ins(&loop->u.loop.blk->u.block.es, 0, fst);
        return loop;
    } else if (accept_str("in")) {
        loop->u.loop.itr = parser();
    }
    loop->u.loop.blk = parser_block();
    return loop;
}

///////////////////////////////////////////////////////////////////////////////
// AWAIT PATTERNS
///////////////////////////////////////////////////////////////////////////////

// await / watching / loop on / toggle with take one of three tiers:
//   bare    : await P      one primary (a call spawns) + optional
//                          until (c) / while (c)
//   value   : await(E)     a plain value expression (a call is a
//                          value, not a spawn)
//   count   : await(E, P)  the pattern P, E times; watching and
//                          loop on also take it with no parentheses
//   pattern : await<PAT>   the combinators, with their own levels:
//       1_prim : :any/:all, until (c), (E) value, <P> group, primary
//       2_pre  : !p
//       3_bin  : p&&p, then p||p, then p until (c) | p while (c)

static Patt* parser_await_3_bin (void);

static Patt* mkp (PKind kind, Tk tk) {
    Patt* p = xalloc(sizeof(Patt));
    p->kind = kind;
    p->tk   = tk;
    return p;
}

// Pattern delimiters `<` and `>`: nested ones may come glued as the
// shift tokens `<<` and `>>`, which are split on demand.
static int accept_angle (const char* one) {
    if (accept_str(one)) {
        return 1;
    }
    if (check_tag(TK_OP) && TK1.len==2 &&
        TK1.str[0]==one[0] && TK1.str[1]==one[0])
    {
        // the first half is consumed, the second stays current
        TK0 = TK1;
        TK0.str = one;
        TK0.len = 1;
        TK1.str = one;
        TK1.len = 1;
        return 2;
    }
    return 0;
}

// Closes a pattern. If an operand follows in the same line, the `>`
// was meant as the operator of a predicate: `<:X until a > b>`.
static void accept_angle_close (void) {
    int ok = accept_angle(">");
    if (!ok) {
        err(TK1, "expected '>'");
    }
    Tk tk = TK0;
    int opnd = is_lit(TK1.tag) || check_tag(TK_ID) || check_tag(TK_TAG) ||
               check_str("(") || check_str("[");
    // the same for half of a `>>` that closes the outermost pattern
    int half = (ok==2 && ANGLE==0);
    if (half || (TK0.sep==TK1.sep && opnd)) {
        err(tk, "pattern error : use parentheses to disambiguate");
    }
}

// until/while predicate: a function, or an expression over the event
// `it`.
static Expr* parser_pred (void) {
    Tk tk = TK1;
    Expr* e = parser();
    // until (c): the parentheses only delimit the predicate
    if (e->tag == E_PARENS) {
        e = e->u.parens.e;
    }
    if (e->tag == E_PROTO) {
        return e;
    }
    Expr* p = mk(E_PROTO, tk);
    proto_par(p, tk_id(tk,"it"), 0);
    p->u.proto.blk = block1(e);
    return p;
}

// A pattern that is counted starts again each time: it cannot spawn.
static void chk_cnt (Patt* p) {
    if (p == NULL) {
        return;
    }
    if (p->kind == PT_SPAWN) {
        err(p->tk, "invalid await : unexpected task");
    }
    chk_cnt(p->a);
    chk_cnt(p->b);
}

// The rest of a count, after its expression `e` and the comma.
static Patt* parser_await_cnt (Tk tk, Expr* e, Patt* sub) {
    Patt* p = mkp(PT_CNT, tk);
    p->e = e;
    p->a = sub;
    chk_cnt(sub);
    return p;
}

// (E): a value  |  (E, P): a count
static Patt* parser_await_parens (void) {
    Tk tk = TK0;
    Expr* e = parser();
    Patt* p;
    if (accept_str(",")) {
        p = parser_await_cnt(tk, e, parser_await_3_bin());
    } else {
        p = mkp(PT_VAL, tk);
        p->e = e;
    }
    accept_str_err(")");
    return p;
}

static Patt* parser_await_1_prim (void) {
    // :any ts  :all ts
    if (check_tag(TK_TAG) &&
        (!strcmp(TK1.str,":any") || !strcmp(TK1.str,":all")))
    {
        next();
        Patt* p = mkp(!strcmp(TK0.str,":any") ? PT_ANY : PT_ALL, TK0);
        p->e = parser_2_suf(NULL);
        return p;

    // until (c)  while (c)
    } else if (accept_str("until") || accept_str("while")) {
        Patt* p = mkp(!strcmp(TK0.str,"until") ? PT_UNTIL : PT_WHILE, TK0);
        p->e = parser_pred();
        return p;

    // <p>
    } else if (accept_angle("<")) {
        ANGLE++;
        Patt* p = parser_await_3_bin();
        ANGLE--;
        accept_angle_close();
        return p;

    // (E): a value  |  (E, P): a count
    } else if (accept_str("(")) {
        return parser_await_parens();

    // primary: a call spawns
    } else {
        Tk tk = TK1;
        Expr* e = parser_2_suf(NULL);
        if (e->tag == E_CALL) {
            Patt* p = mkp(PT_SPAWN, tk);
            p->e    = e->u.call.f;
            p->args = e->u.call.es;
            return p;
        }
        Patt* p = mkp(PT_VAL, tk);
        p->e = e;
        return p;
    }
}

static Patt* parser_await_2_pre (void) {
    if (check_tag(TK_OP) && !strcmp(TK1.str,"!")) {
        next();
        Patt* p = mkp(PT_NOT, TK0);
        p->a = parser_await_2_pre();
        return p;
    }
    return parser_await_1_prim();
}

// Combinators follow the precedence of the operators: `!`, then
// `&&`, then `||`. The until/while suffix is the weakest: it takes the
// whole pattern to its left.
static Patt* parser_await_bin (int and) {
    const char* op = and ? "&&" : "||";
    Patt* p1 = and ? parser_await_2_pre() : parser_await_bin(1);
    while (check_tag(TK_OP) && !strcmp(TK1.str,op)) {
        next();
        Patt* p = mkp(and ? PT_AND : PT_OR, TK0);
        p->a = p1;
        p->b = and ? parser_await_2_pre() : parser_await_bin(1);
        p1 = p;
    }
    return p1;
}

static Patt* parser_await_3_bin (void) {
    Patt* p1 = parser_await_bin(0);
    while (TK0.sep==TK1.sep && (check_str("until") || check_str("while"))) {
        next();
        Patt* p = mkp(!strcmp(TK0.str,"until") ? PT_UNTIL : PT_WHILE, TK0);
        p->a = p1;
        p->e = parser_pred();
        p1 = p;
    }
    return p1;
}

static Patt* parser_await (void) {
    return parser_await_blk(0);
}

// The pattern before a block (watching, loop on) may be a count with
// no parentheses: `watching E, P {...}`.
static Patt* parser_await_blk (int blk) {
    // value: await(E)  |  count: await(E, P)
    if (accept_str("(")) {
        Patt* p = parser_await_parens();
        if (blk && p->kind==PT_VAL && accept_str(",")) {
            return parser_await_cnt(p->tk, p->e, parser_await_blk(0));
        }
        return p;
    }

    // pattern: await<PAT>
    if (accept_angle("<")) {
        ANGLE++;
        Patt* p = parser_await_3_bin();
        ANGLE--;
        accept_angle_close();
        return p;
    }

    // bare: await P, with no combinators and no loose values
    Patt* p = parser_await_3_bin();
    if (blk && p->kind==PT_VAL && accept_str(",")) {
        return parser_await_cnt(p->tk, p->e, parser_await_blk(0));
    }
    Patt* b = p;
    if ((b->kind==PT_UNTIL || b->kind==PT_WHILE) && b->a!=NULL) {
        b = b->a;
    }
    int ok;
    if (b->kind == PT_VAL) {
        ETag t = b->e->tag;
        ok = (t==E_TAG || t==E_CLK || t==E_STR || t==E_NAT || t==E_BOOL ||
              t==E_TABLE || t==E_TAGGED || t==E_PROTO);
    } else {
        ok = (b->kind!=PT_AND && b->kind!=PT_OR && b->kind!=PT_NOT);
    }
    if (!ok) {
        // at the expression, not at what follows it
        err(b->tk, "invalid await : unexpected expression");
    }
    return p;
}

// Filters of a toggle: patterns separated by commas.
static int parser_filters (Patt*** fs) {
    int n = 0;
    *fs = NULL;
    do {
        *fs = realloc(*fs, (n+1) * sizeof(Patt*));
        (*fs)[n++] = parser_await();
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
        int angle = ANGLE;
        ANGLE = 0;
        Expr* e = parser();
        ANGLE = angle;
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

// The next two tokens are a keyword and `=`: no expression starts
// like this.
static int field_key (void) {
    // a keyword, or the name of an exit statement (`break`, `skip`)
    if (!check_tag(TK_KEY) && !(check_tag(TK_ID) && in(EXITS,TK1.str))) {
        return 0;
    }
    Lex lex = lexer_save();
    Tk tk;
    lexer_token(&tk);
    lexer_restore(lex);
    return !is_lit(tk.tag) && !strcmp(tk.str,"=");
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
        } else if (field_key()) {
            // a keyword as the name of a field, as in `t.loop`
            next();
            key = mk(E_STR, TK0);
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

    // return e  break e  escape e  throw e  abort e  skip
    } else if (check_tag(TK_ID) && in(EXITS,TK1.str)) {
        next();
        return parser_exit(TK0);

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
        int angle = ANGLE;
        ANGLE = 0;
        Expr* t = parser_table();
        ANGLE = angle;
        return t;

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

    // spawn {...}  spawn T(...)
    } else if (check_str("spawn")) {
        return parser_spawn();

    // tasks(n)
    } else if (accept_str("tasks")) {
        Expr* e = mk(E_TASKS, TK0);
        accept_str_err("(");
        if (!check_str(")")) {
            e->u.tasks.n = parser();
        }
        accept_str_err(")");
        return e;

    // emit @to (e)
    } else if (accept_str("emit")) {
        Tk tk = TK0;
        Expr* e = mk(E_EMIT, tk);
        if (accept_str("@")) {
            e->u.emit.to = parser_at_err();
        }
        // emit e  |  emit (e)
        if (starts_expr()) {
            Expr* evt = parser();
            if (evt->tag == E_LIST) {
                err(tk, "expected single argument");
            }
            // an event is a single value anyway
            if (evt->tag == E_PARENS) {
                evt = evt->u.parens.e;
            }
            e->u.emit.e = evt;
            return e;
        }
        // emit <- e
        Expr* call = parser_2_suf(mk(E_ACC, tk));
        call = parser_6_pip(parser_5_bin(parser_3_met(call)));
        if (call->tag != E_CALL) {
            err(tk, "expected call syntax");
        }
        if (call->u.call.es.n != 1) {
            err(tk, "expected single argument");
        }
        e->u.emit.e = call->u.call.es.v[0];
        return parser_7_out(e);

    // await P
    } else if (accept_str("await")) {
        Expr* e = mk(E_AWAIT, TK0);
        e->u.await.p = parser_await();
        return e;

    // toggle t(b) with P  |  toggle on :T with P {...}
    } else if (accept_str("toggle")) {
        Tk tk = TK0;
        if (accept_str("on")) {
            accept_tag_err(TK_TAG);
            Expr* e = mk(E_TOGGLE_ON, TK0);
            if (accept_str("with")) {
                e->u.toggle_on.nf = parser_filters(&e->u.toggle_on.fs);
            }
            e->u.toggle_on.blk = parser_block();
            return e;
        }
        Expr* e = mk(E_TOGGLE, tk);
        Expr* call = parser_6_pip(NULL);
        if (call->tag != E_CALL) {
            err(tk, "expected call syntax");
        }
        e->u.toggle.t    = call->u.call.f;
        e->u.toggle.args = call->u.call.es;
        if (accept_str("with")) {
            e->u.toggle.nf = parser_filters(&e->u.toggle.fs);
        }
        return parser_7_out(e);

    // par {...} with {...}
    } else if (accept_str("par")) {
        Expr* e = mk(E_PAR, TK0);
        if (accept_tag(TK_TAG)) {
            if (!strcmp(TK0.str, ":all")) {
                e->u.par.mode = PAR_ALL;
            } else if (!strcmp(TK0.str, ":any")) {
                e->u.par.mode = PAR_ANY;
            } else {
                err(TK0, "invalid par : invalid tag");
            }
        }
        do {
            list_add(&e->u.par.blks, parser_block());
        } while (accept_str("with"));
        return e;

    // watching P {...} is par :any { await P } with {...}
    } else if (accept_str("watching")) {
        Tk tk = TK0;
        Expr* awt = mk(E_AWAIT, tk);
        awt->u.await.p = parser_await_blk(1);
        Expr* e = mk(E_PAR, tk);
        e->u.par.mode = PAR_ANY;
        list_add(&e->u.par.blks, block1(awt));
        list_add(&e->u.par.blks, parser_block());
        return e;

    // until c  while c
    } else if (accept_str("until") || accept_str("while")) {
        return parser_exit(TK0);

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
        case E_ME:
            return 1;
        default:
            return 0;
    }
}

static Expr* parser_2_suf (Expr* pre) {
    int no = check_str("emit") || check_str("spawn") ||
             check_str("toggle") || check_str("thread");
    Expr* e = pre ? pre : parser_1_prim();
    if (pre==NULL && is_exit(e)) {
        // it has taken the whole expression
        return e;
    }

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

// Binary operators follow the precedence of C, from the weakest
// level, and associate to the left. Unlike C, the bitwise operators
// bind stronger than the comparisons.
static const char* LEVELS[][8] = {
    { "||", NULL },
    { "&&", NULL },
    { "==", "!=", "??", "!?", NULL },
    { "<", "<=", ">", ">=", NULL },
    { "|", NULL },
    { "^", NULL },
    { "&", NULL },
    { "<<", ">>", NULL },
    { "++", NULL },
    { "+", "-", NULL },
    { "*", "/", "//", "%", NULL },
};

#define NLEVELS ((int)(sizeof(LEVELS) / sizeof(LEVELS[0])))

static int check_level (int lvl) {
    if (!check_tag(TK_OP)) {
        return 0;
    }
    if (ANGLE > 0) {
        // in a predicate with no delimiters of its own
        if (!strcmp(TK1.str,">") || !strcmp(TK1.str,">>")) {
            return 0;
        }
        // operator of the predicate, or combinator of the pattern?
        if (!strcmp(TK1.str,"&&") || !strcmp(TK1.str,"||")) {
            err(TK1, "pattern error : use parentheses to disambiguate");
        }
    }
    for (int i=0; i<8 && LEVELS[lvl][i]!=NULL; i++) {
        if (!strcmp(LEVELS[lvl][i], TK1.str)) {
            return 1;
        }
    }
    return 0;
}

static Expr* parser_5_lvl (int lvl, Expr* pre) {
    if (lvl == NLEVELS) {
        return parser_4_pre(pre);
    }
    Expr* e1 = parser_5_lvl(lvl+1, pre);
    while (check_level(lvl)) {
        next();
        Expr* e = mk(E_BIN, TK0);
        e->u.bin.e1 = e1;
        e->u.bin.e2 = parser_5_lvl(lvl+1, NULL);
        e1 = e;
    }
    return e1;
}

static Expr* parser_5_bin (Expr* pre) {
    return parser_5_lvl(0, pre);
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
    List l = { 0, NULL };
    if (OPT_ENV != NULL) {
        // the environment comes first, as an include written at the
        // top of the program (which may still have it: a file is only
        // included once)
        include_file(&l, TK1, OPT_ENV);
    }
    List prog = parser_stmts("<eof>");
    for (int i=0; i<prog.n; i++) {
        list_add(&l, prog.v[i]);
    }
    e->u.block.es = l;
    accept_str_err("<eof>");
    return e;
}
