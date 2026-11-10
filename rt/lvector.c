// Library `vector`: tables used as sequences, with the keys
// `0..#t-1`.

#include "lib.h"

AVal atm_g_vector;

// Argument 0 of function `f` as a table, with its length.
static AStatus arg_vec (int argc, AVal* argv, const char* f, int64_t* n) {
    AVal t = arg(argc, argv, 0);
    if (t.type != A_TAB) {
        return a_error("invalid argument to '%s' : expected table : got %s",
            f, a_typename(t));
    }
    AVal len = a_nil();
    if (a_len(&len, t) != A_OK) {
        return A_ERROR;
    }
    *n = len.u.i;
    return A_OK;
}

// vector.insert(t, v): appends `v`.
// vector.insert(t, i, v): inserts `v` at position `i`, shifting up
// the elements from `i` on.
static AStatus vec_insert (AFunc* self, int argc, AVal* argv) {
    (void) self;
    int64_t n;
    if (arg_vec(argc, argv, "insert", &n) != A_OK) {
        return A_ERROR;
    }
    AVal t = argv[0];
    if (argc < 3) {
        return a_setindex(t, a_int(n), arg(argc,argv,1));
    }
    int64_t i;
    if (arg_int(argc, argv, 1, "insert", 0, 0, &i) != A_OK) {
        return A_ERROR;
    }
    if (i<0 || i>n) {
        return a_error("invalid argument to 'insert' : position out of bounds");
    }
    // the table must be able to hold the value
    if (a_chk_tab(A_TAB_OF(t), argv[2]) != A_OK) {
        return A_ERROR;
    }
    for (int64_t j=n; j>i; j--) {
        a_tab_set(A_TAB_OF(t), a_int(j), a_tab_get(A_TAB_OF(t),a_int(j-1)));
    }
    a_tab_set(A_TAB_OF(t), a_int(i), argv[2]);
    return A_OK;
}

// vector.remove(t): removes and gives the last element.
// vector.remove(t, i): removes and gives the element at position `i`,
// shifting down the elements after it.
static AStatus vec_remove (AFunc* self, int argc, AVal* argv) {
    (void) self;
    int64_t n, i;
    if (arg_vec(argc,argv,"remove",&n)!=A_OK ||
        arg_int(argc,argv,1,"remove",1,n-1,&i)!=A_OK)
    {
        return A_ERROR;
    }
    ATab* t = A_TAB_OF(argv[0]);
    if (n == 0) {
        a_push(a_nil());
        return A_OK;
    }
    if (i<0 || i>=n) {
        return a_error("invalid argument to 'remove' : position out of bounds");
    }
    a_pushc(a_tab_get(t, a_int(i)));
    for (int64_t j=i; j<n-1; j++) {
        a_tab_set(t, a_int(j), a_tab_get(t,a_int(j+1)));
    }
    a_tab_set(t, a_int(n-1), a_nil());
    return A_OK;
}

// `a` comes before `b`, according to `cmp`, or to `<` if nil.
static AStatus before (AVal cmp, AVal a, AVal b, int* yes) {
    if (cmp.type == A_NIL) {
        AVal r = a_nil();
        if (a_lt(&r, a, b) != A_OK) {
            return A_ERROR;
        }
        *yes = a_truthy(r);
        return A_OK;
    }
    int base = a_top;
    a_pushc(a);
    a_pushc(b);
    AStatus st = a_call(cmp, base);
    if (st != A_OK) {
        return st;
    }
    *yes = (a_top > base) && a_truthy(a_stk[base]);
    a_settop(base);
    return A_OK;
}

// Stable merge sort of `v[0..n)`, with `tmp` as scratch.
static AStatus msort (AVal cmp, AVal* v, AVal* tmp, int64_t n) {
    if (n < 2) {
        return A_OK;
    }
    int64_t h = n / 2;
    AStatus st = msort(cmp, v, tmp, h);
    if (st == A_OK) {
        st = msort(cmp, v+h, tmp, n-h);
    }
    if (st != A_OK) {
        return st;
    }
    int64_t i=0, j=h, k=0;
    while (i<h && j<n) {
        // the right one goes first only if strictly before
        int yes;
        st = before(cmp, v[j], v[i], &yes);
        if (st != A_OK) {
            // keeps every element exactly once
            break;
        }
        tmp[k++] = yes ? v[j++] : v[i++];
    }
    while (i < h) {
        tmp[k++] = v[i++];
    }
    while (j < n) {
        tmp[k++] = v[j++];
    }
    memcpy(v, tmp, n * sizeof(AVal));
    return st;
}

// vector.sort(t, cmp): sorts in place. `cmp(a,b)` tells whether `a`
// comes before `b`, and defaults to `a < b`. The sort is stable.
static AStatus vec_sort (AFunc* self, int argc, AVal* argv) {
    (void) self;
    int64_t n;
    if (arg_vec(argc, argv, "sort", &n) != A_OK) {
        return A_ERROR;
    }
    AVal cmp = arg(argc, argv, 1);
    if (cmp.type!=A_NIL && cmp.type!=A_FUNC) {
        return a_error("invalid argument to 'sort' : expected function : got %s",
            a_typename(cmp));
    }
    // the comparator may change the table: sorts a copy
    ATab* t = A_TAB_OF(argv[0]);
    AVal* v   = malloc((n+1) * sizeof(AVal));
    AVal* tmp = malloc((n+1) * sizeof(AVal));
    for (int64_t i=0; i<n; i++) {
        v[i] = a_tab_get(t, a_int(i));
        a_inc(v[i]);
    }
    AStatus st = msort(cmp, v, tmp, n);
    for (int64_t i=0; i<n; i++) {
        if (st == A_OK) {
            a_tab_set(t, a_int(i), v[i]);
        }
        a_dec(v[i]);
    }
    free(v);
    free(tmp);
    return st;
}

// vector.join(t, sep): the strings and numbers of `t` in one string,
// separated by `sep`.
static AStatus vec_join (AFunc* self, int argc, AVal* argv) {
    (void) self;
    int64_t n;
    AStr* sep = NULL;
    if (arg_vec(argc, argv, "join", &n) != A_OK) {
        return A_ERROR;
    }
    if (arg(argc,argv,1).type!=A_NIL &&
        arg_str(argc,argv,1,"join",&sep)!=A_OK)
    {
        return A_ERROR;
    }
    Buf b = { NULL, 0, 0 };
    for (int64_t i=0; i<n; i++) {
        AVal x = a_tab_get(A_TAB_OF(argv[0]), a_int(i));
        if (x.type!=A_STR && !a_isnum(x)) {
            free(b.s);
            return a_error("invalid argument to 'join' : "
                "expected string or number : got %s", a_typename(x));
        }
        if (i>0 && sep!=NULL) {
            buf_add(&b, sep->s, sep->len);
        }
        AVal s = a_tostring(x);
        buf_str(&b, s);
        a_dec(s);
    }
    a_push(buf_val(&b));
    return A_OK;
}

void atm_lib_vector_init (void) {
    atm_g_vector = a_tab();
    field(atm_g_vector, "insert", vec_insert);
    field(atm_g_vector, "remove", vec_remove);
    field(atm_g_vector, "sort",   vec_sort);
    field(atm_g_vector, "join",   vec_join);
}

void atm_lib_vector_quit (void) {
    a_clr(&atm_g_vector);
}
