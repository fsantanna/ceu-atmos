// Runtime unit test: tables.

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "atmos.h"

const char* atm_files[] = { "test" };

void atm_free (void) {
}

static double len (AVal t) {
    AVal n = a_nil();
    assert(a_len(&n, t)==A_OK && n.type==A_INT);
    return (double) n.u.i;
}

static double get (AVal t, AVal k) {
    AVal v = a_tab_get(A_TAB_OF(t), k);
    return (v.type == A_NIL) ? -1 : v.u.n;
}

int main (void) {
    atm_init(0, NULL);
    AVal t = a_tab();
    ATab* T = A_TAB_OF(t);

    // vector: 0-based, appended at #t
    assert(len(t) == 0);
    for (int i=0; i<100; i++) {
        a_tab_set(T, a_num(len(t)), a_num(i*10));
    }
    assert(len(t) == 100);
    assert(get(t,a_num(0))==0 && get(t,a_num(99))==990);
    assert(get(t,a_num(100)) == -1);

    // pop: nil at the last element shrinks
    a_tab_set(T, a_num(99), a_nil());
    assert(len(t) == 99);

    // hole: #t is still a border
    a_tab_set(T, a_num(97), a_nil());
    assert(len(t) == 99);
    a_tab_set(T, a_num(98), a_nil());
    assert(len(t) == 97);

    // sparse keys move in from the hash part when reached
    a_tab_set(T, a_num(99), a_num(1));
    a_tab_set(T, a_num(98), a_num(2));
    assert(len(t) == 97);
    a_tab_set(T, a_num(97), a_num(3));
    assert(len(t) == 100);
    assert(get(t,a_num(98))==2 && get(t,a_num(99))==1);

    // hash part: any key but nil
    AVal x = a_strz("x");
    AVal y = a_strz("y");
    a_tab_set(T, x, a_num(1));
    a_tab_set(T, y, a_num(2));
    a_tab_set(T, a_num(-1), a_num(3));
    a_tab_set(T, a_num(1.5), a_num(4));
    a_tab_set(T, a_bool(1), a_num(5));
    a_tab_set(T, t, a_num(6));
    assert(get(t,x)==1 && get(t,y)==2 && get(t,a_num(-1))==3);
    assert(get(t,a_num(1.5))==4 && get(t,a_bool(1))==5 && get(t,t)==6);
    assert(get(t,a_bool(0)) == -1);
    a_tab_set(T, x, a_num(7));
    assert(get(t,x) == 7);
    assert(len(t) == 100);

    // the table holds itself as a key: break the cycle
    a_tab_set(T, t, a_nil());
    assert(get(t,t) == -1);

    // iteration: array part, then insertion order, skipping deleted
    a_tab_set(T, y, a_nil());
    a_tab_set(T, y, a_num(8));
    AVal u = a_tab();
    ATab* U = A_TAB_OF(u);
    a_tab_set(U, a_num(0), a_num(10));
    a_tab_set(U, a_num(1), a_num(11));
    a_tab_set(U, x, a_num(12));
    a_tab_set(U, y, a_num(13));
    a_tab_set(U, a_bool(1), a_num(14));
    a_tab_set(U, x, a_nil());
    a_tab_set(U, x, a_num(15));
    uint32_t pos = 0;
    AVal k, v;
    double vs[8];
    int n = 0;
    while (a_tab_next(U, &pos, &k, &v)) {
        vs[n++] = v.u.n;
    }
    assert(n == 5);
    assert(vs[0]==10 && vs[1]==11 && vs[2]==13 && vs[3]==14 && vs[4]==15);

    // many keys: rehash keeps everything
    char buf[32];
    for (int i=0; i<2000; i++) {
        snprintf(buf, sizeof(buf), "k%d", i);
        AVal s = a_strz(buf);
        a_tab_set(U, s, a_num(i));
        a_dec(s);
    }
    for (int i=0; i<2000; i+=2) {
        snprintf(buf, sizeof(buf), "k%d", i);
        AVal s = a_strz(buf);
        a_tab_set(U, s, a_nil());
        a_dec(s);
    }
    for (int i=0; i<2000; i++) {
        snprintf(buf, sizeof(buf), "k%d", i);
        AVal s = a_strz(buf);
        assert(get(u,s) == ((i%2 == 0) ? -1 : i));
        a_dec(s);
    }

    // an integer and a real with the same value are the same key
    a_tab_set(U, a_num(7), a_num(70));
    assert(get(u,a_int(7))==70 && get(u,a_num(7.0))==70);
    a_tab_set(U, a_int(7), a_nil());
    assert(get(u,a_num(7)) == -1);

    // generic operations
    AVal d = a_nil();
    assert(a_index(&d, u, a_num(1))==A_OK && d.u.n==11);
    assert(a_index(&d, a_num(1), a_num(1)) == A_ERROR);
    assert(strstr(a_error_msg(), "expected table : got number"));
    assert(a_setindex(u, a_nil(), a_num(1)) == A_ERROR);
    assert(a_setindex(u, a_num(2), a_num(1)) == A_OK);
    assert(len(u)==3 && len(x)==1);

    a_dec(x);
    a_dec(y);
    a_dec(t);
    a_dec(u);
    atm_quit();
    puts("ok");
    return 0;
}
