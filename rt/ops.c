// Structural operations: ++ ?? =>= ?>.

#include <stdlib.h>
#include <string.h>

#include "atmos.h"

// `a` is `b`, or a prefix of `b` followed by a dot.
static int prefix (AStr* a, AStr* b) {
    if (a == b) {
        return 1;
    }
    return (b->len > a->len) && (b->s[a->len] == '.') &&
           !memcmp(a->s, b->s, a->len);
}

int a_gte (AVal a, AVal b) {
    if (a_eq(a, b)) {
        return 1;
    } else if (a.type != b.type) {
        return 0;
    } else if (a.type == A_STR) {
        return prefix(A_STR_OF(a), A_STR_OF(b));
    } else if (a.type == A_TAB) {
        // every pair in `a` is more general than the same key in `b`
        uint32_t pos = 0;
        AVal k, v;
        while (a_tab_next(A_TAB_OF(a), &pos, &k, &v)) {
            if (!a_gte(v, a_tab_get(A_TAB_OF(b), k))) {
                return 0;
            }
        }
        return 1;
    } else {
        return 0;
    }
}

int a_is (AVal v, AVal x) {
    if (a_eq(v, x)) {
        return 1;
    }
    if (x.type==A_TAB && v.type==A_TAB) {
        // every field of the pattern matches the same field of the value
        uint32_t pos = 0;
        AVal k, xv;
        while (a_tab_next(A_TAB_OF(x), &pos, &k, &xv)) {
            if (!a_is(a_tab_get(A_TAB_OF(v), k), xv)) {
                return 0;
            }
        }
        return 1;
    }
    if (x.type != A_STR) {
        return 0;
    }
    AStr* sx = A_STR_OF(x);
    if (!strcmp(a_typename(v), sx->s)) {
        return 1;
    }
    if (v.type == A_STR) {
        return prefix(sx, A_STR_OF(v));
    }
    if (v.type == A_TAB) {
        AVal k = a_strz("tag");
        AVal tag = a_tab_get(A_TAB_OF(v), k);
        a_dec(k);
        return (tag.type == A_STR) && prefix(sx, A_STR_OF(tag));
    }
    return 0;
}

AStatus a_in (AVal* dst, AVal v, AVal t) {
    int ret = 0;
    if (t.type == A_TASKS) {
        // the tasks of a pool
        AVal x = a_tasks_tab(t);
        AStatus st = a_in(dst, v, x);
        a_dec(x);
        return st;
    }
    if (t.type == A_TAB) {
        // any value, or any non-numeric key
        uint32_t pos = 0;
        AVal k, x;
        while (!ret && a_tab_next(A_TAB_OF(t), &pos, &k, &x)) {
            ret = a_gte(v, x) || (!a_isnum(k) && a_gte(v, k));
        }
    } else if (a_isnum(t)) {
        // an integer in the range 0..t-1
        int64_t i;
        ret = a_isnum(v) && a_toint(v,&i) && i>=0 && a_num_cmp(v,t)==-1;
    } else {
        return a_error("invalid argument to 'xin' : unexpected %s",
            a_typename(t));
    }
    a_set(dst, a_bool(ret));
    return A_OK;
}

static AStatus cat_str (AVal* dst, AVal a, AVal b) {
    AVal x = a_tostring(a);
    AVal y = a_tostring(b);
    size_t nx = A_STR_OF(x)->len;
    size_t ny = A_STR_OF(y)->len;
    char* buf = malloc(nx + ny + 1);
    memcpy(buf, A_STR_OF(x)->s, nx);
    memcpy(buf+nx, A_STR_OF(y)->s, ny);
    a_dec(x);
    a_dec(y);
    AVal ret = a_str(buf, nx+ny);
    free(buf);
    a_set(dst, ret);
    return A_OK;
}

AStatus a_cat (AVal* dst, AVal a, AVal b) {
    // a pool concatenates as the vector of its tasks
    if (a.type==A_TASKS || b.type==A_TASKS) {
        AVal x = (a.type == A_TASKS) ? a_tasks_tab(a) : a;
        AVal y = (b.type == A_TASKS) ? a_tasks_tab(b) : b;
        AStatus st = a_cat(dst, x, y);
        if (a.type == A_TASKS) {
            a_dec(x);
        }
        if (b.type == A_TASKS) {
            a_dec(y);
        }
        return st;
    }
    int str_a = (a.type==A_STR || a_isnum(a));
    int str_b = (b.type==A_STR || a_isnum(b));
    if (str_a && str_b) {
        return cat_str(dst, a, b);
    }
    if (a.type!=A_TAB || b.type!=A_TAB) {
        return a_error("invalid operation '++' : unexpected %s",
            a_typename((a.type==A_TAB || str_a) ? b : a));
    }

    // the result is a fresh table: it cannot hold pinned values
    AVal ret = a_tab();
    ATab* t = A_TAB_OF(ret);
    uint32_t pos = 0;
    AVal k, v;
    while (a_tab_next(A_TAB_OF(a), &pos, &k, &v)) {
        if (a_chk_tab(t,k)!=A_OK || a_chk_tab(t,v)!=A_OK) {
            a_dec(ret);
            return A_ERROR;
        }
        a_tab_set(t, k, v);
    }
    // keys 0,1,... of `b` in sequence are appended
    int64_t n = 0;
    pos = 0;
    while (a_tab_next(A_TAB_OF(b), &pos, &k, &v)) {
        if (a_chk_tab(t,k)!=A_OK || a_chk_tab(t,v)!=A_OK) {
            a_dec(ret);
            return A_ERROR;
        }
        if (k.type==A_INT && k.u.i==n) {
            a_tab_set(t, a_int(t->an), v);
            n++;
        } else {
            a_tab_set(t, k, v);
        }
    }
    a_set(dst, ret);
    return A_OK;
}

AStatus a_next (AVal it, AVal lim, AVal* pos, AVal* k, AVal* v) {
    int64_t p = (pos->type == A_INT) ? pos->u.i : 0;
    switch (it.type) {
        case A_TAB: {
            uint32_t i = (uint32_t) p;
            AVal x, y;
            if (a_tab_next(A_TAB_OF(it), &i, &x, &y)) {
                a_cpy(k, x);
                a_cpy(v, y);
            } else {
                a_clr(k);
                a_clr(v);
            }
            *pos = a_int(i);
            return A_OK;
        }
        case A_TASKS: {
            // index among the live tasks, and the task
            int i = (int) p;
            ATask* x = a_tasks_next(A_TASKS_OF(it), &i);
            int64_t n = (k->type == A_INT) ? k->u.i + 1 : 0;
            if (x != NULL) {
                a_set(k, a_int(n));
                a_cpy(v, a_obj(&x->node.hdr));
            } else {
                a_clr(k);
                a_clr(v);
            }
            *pos = a_int(i);
            return A_OK;
        }
        case A_NIL:
            a_set(k, a_int(p));
            *pos = a_int(p + 1);
            return A_OK;
        case A_INT:
        case A_NUM: {
            AVal x;
            int end;
            if (a_isnum(lim)) {
                // a..b
                x = (it.type == A_INT) ? a_int(it.u.i + p) : a_num(it.u.n + p);
                int c = a_num_cmp(x, lim);
                end = (c!=-1 && c!=0);
            } else if (lim.type == A_NIL) {
                // 0..n-1
                x   = a_int(p);
                end = (a_num_cmp(x, it) != -1);
            } else {
                return a_error("invalid loop : expected number : got %s",
                    a_typename(lim));
            }
            a_set(k, end ? a_nil() : x);
            *pos = a_int(p + 1);
            return A_OK;
        }
        case A_FUNC: {
            int base = a_top;
            AStatus st = a_call(it, base);
            if (st != A_OK) {
                return st;
            }
            a_set(k, (a_top > base) ? a_stk[base] : a_nil());
            a_set(v, (a_top > base+1) ? a_stk[base+1] : a_nil());
            for (int i=base+2; i<a_top; i++) {
                a_dec(a_stk[i]);
            }
            a_top = base;
            return A_OK;
        }
        default:
            return a_error("invalid loop : unexpected %s", a_typename(it));
    }
}
