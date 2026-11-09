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
    if (a.type != b.type) {
        return 0;
    } else if (a_eq(a, b)) {
        return 1;
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

AStatus a_in (AVal* dst, AVal v, AVal t, int neg) {
    int ret = 0;
    if (t.type == A_TAB) {
        // any value, or any non-numeric key
        uint32_t pos = 0;
        AVal k, x;
        while (!ret && a_tab_next(A_TAB_OF(t), &pos, &k, &x)) {
            ret = a_gte(v, x) || (k.type!=A_NUM && a_gte(v, k));
        }
    } else if (t.type == A_NUM) {
        // range 0..t-1
        for (double i=0; !ret && i<t.u.n; i++) {
            ret = a_gte(v, a_num(i));
        }
    } else {
        return a_error("invalid operation '%s' : unexpected %s",
            neg ? "!>" : "?>", a_typename(t));
    }
    a_set(dst, a_bool(neg ? !ret : ret));
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
    int str_a = (a.type==A_STR || a.type==A_NUM);
    int str_b = (b.type==A_STR || b.type==A_NUM);
    if (str_a && str_b) {
        return cat_str(dst, a, b);
    }
    if (a.type!=A_TAB || b.type!=A_TAB) {
        return a_error("invalid operation '++' : unexpected %s",
            a_typename((a.type==A_TAB || str_a) ? b : a));
    }

    AVal ret = a_tab();
    ATab* t = A_TAB_OF(ret);
    uint32_t pos = 0;
    AVal k, v;
    while (a_tab_next(A_TAB_OF(a), &pos, &k, &v)) {
        a_tab_set(t, k, v);
    }
    // keys 0,1,... of `b` in sequence are appended
    double n = 0;
    pos = 0;
    while (a_tab_next(A_TAB_OF(b), &pos, &k, &v)) {
        if (k.type==A_NUM && k.u.n==n) {
            a_tab_set(t, a_num(t->an), v);
            n++;
        } else {
            a_tab_set(t, k, v);
        }
    }
    a_set(dst, ret);
    return A_OK;
}
