// Runtime unit test: interned strings.

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "atmos.h"

int main (void) {
    atm_init(0, NULL);

    // equal contents share one object
    AVal a = a_strz("hello");
    AVal b = a_str("hello world", 5);
    assert(a.type == A_STR);
    assert(a.u.o == b.u.o);
    assert(a.u.o->rc == 2);
    assert(A_STR_OF(a)->len == 5);
    assert(!strcmp(A_STR_OF(a)->s, "hello"));

    AVal c = a_strz("world");
    assert(c.u.o != a.u.o);

    // embedded NUL and the empty string
    AVal z1 = a_str("a\0b", 3);
    AVal z2 = a_str("a\0c", 3);
    assert(z1.u.o != z2.u.o);
    AVal e = a_strz("");
    assert(A_STR_OF(e)->len == 0);

    // a released string leaves the table and can be created again
    a_dec(a);
    a_dec(b);
    AVal d = a_strz("hello");
    assert(d.u.o->rc == 1);

    // many strings: the table grows and shrinks
    enum { N = 5000 };
    static AVal vs[N];
    char buf[32];
    for (int i=0; i<N; i++) {
        snprintf(buf, sizeof(buf), "s%d", i);
        vs[i] = a_strz(buf);
    }
    for (int i=0; i<N; i++) {
        snprintf(buf, sizeof(buf), "s%d", i);
        AVal x = a_strz(buf);
        assert(x.u.o == vs[i].u.o);
        a_dec(x);
    }
    for (int i=0; i<N; i+=2) {
        a_dec(vs[i]);
    }
    for (int i=1; i<N; i+=2) {
        snprintf(buf, sizeof(buf), "s%d", i);
        AVal x = a_strz(buf);
        assert(x.u.o == vs[i].u.o);
        a_dec(x);
        a_dec(vs[i]);
    }

    a_dec(c);
    a_dec(d);
    a_dec(e);
    a_dec(z1);
    a_dec(z2);
    atm_quit();
    puts("ok");
    return 0;
}
