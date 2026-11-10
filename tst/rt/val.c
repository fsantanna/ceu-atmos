// Runtime unit test: values, stack, calls, arithmetic.

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "atmos.h"

const char* atm_file = "test";

void atm_free (void) {
}

static AStatus two (AFunc* self, int argc, AVal* argv) {
    (void) self;
    assert(argc == 1);
    a_push(a_num(argv[0].u.n * 2));
    a_push(a_bool(1));
    return A_OK;
}

int main (void) {
    char buf[64];
    atm_init(0, NULL);

    // values
    assert(sizeof(AVal) == 16);
    assert(!a_truthy(a_nil()));
    assert(!a_truthy(a_bool(0)));
    assert(a_truthy(a_num(0)));

    // numbers: integers and reals
    a_num_fmt(buf, sizeof(buf), a_int(3));
    assert(!strcmp(buf, "3"));
    a_num_fmt(buf, sizeof(buf), a_num(3));
    assert(!strcmp(buf, "3.0"));
    a_num_fmt(buf, sizeof(buf), a_num(4.0/3 + 1));
    assert(!strcmp(buf, "2.3333333333333"));
    a_num_fmt(buf, sizeof(buf), a_num(-0.5));
    assert(!strcmp(buf, "-0.5"));
    a_num_fmt(buf, sizeof(buf), a_num(1e100));
    assert(!strcmp(buf, "1e+100"));
    assert(a_eq(a_int(1), a_num(1)) && !a_eq(a_int(1), a_num(1.5)));
    assert(a_num_cmp(a_int(1), a_num(1.5)) == -1);
    assert(a_num_cmp(a_num(2.5), a_int(2)) == 1);
    assert(a_num_cmp(a_int(INT64_MAX), a_num(9223372036854775808.0)) == -1);
    assert(a_num_cmp(a_int(1), a_num(0.0/0.0)) == 2);
    assert(a_numi(3.0).type==A_INT && a_numi(3.5).type==A_NUM);

    // arithmetic: integers wrap, a mix is real, / is always real
    AVal x = a_nil();
    assert(a_add(&x, a_int(1), a_int(2))==A_OK && x.type==A_INT && x.u.i==3);
    assert(a_add(&x, a_int(1), a_num(2))==A_OK && x.type==A_NUM && x.u.n==3);
    assert(a_add(&x, a_int(INT64_MAX), a_int(1))==A_OK && x.u.i==INT64_MIN);
    assert(a_div(&x, a_int(7), a_int(2))==A_OK && x.type==A_NUM && x.u.n==3.5);
    assert(a_idiv(&x, a_int(7), a_int(2))==A_OK && x.type==A_INT && x.u.i==3);
    assert(a_idiv(&x, a_int(-7), a_int(2))==A_OK && x.u.i==-3);
    assert(a_mod(&x, a_int(-7), a_int(3))==A_OK && x.u.i==-1);
    assert(a_mod(&x, a_num(-7), a_int(3))==A_OK && x.u.n==-1);
    assert(a_idiv(&x, a_num(7.5), a_int(2))==A_OK && x.u.n==3);
    assert(a_idiv(&x, a_int(1), a_int(0)) == A_ERROR);
    assert(strstr(a_error_msg(), "division by zero"));
    assert(a_mod(&x, a_int(1), a_int(0)) == A_ERROR);
    assert(a_idiv(&x, a_int(INT64_MIN), a_int(-1))==A_OK && x.u.i==INT64_MIN);
    assert(a_add(&x, a_int(1), a_nil()) == A_ERROR);
    assert(strstr(a_error_msg(), "expected number : got nil"));

    // calls: results replace the arguments
    AVal f = a_func(two, 0);
    int base = a_top;
    a_push(a_num(21));
    assert(a_call(f, base) == A_OK);
    assert(a_top == base+2);
    assert(a_stk[base].u.n==42 && a_stk[base+1].u.b==1);
    a_ret1(&x, base);
    assert(x.u.n==42 && a_top==base);

    // calls: errors unwind the stack
    a_push(a_num(1));
    assert(a_call(a_num(10), base) == A_ERROR);
    assert(a_top == base);
    assert(strstr(a_error_msg(), "expected function : got number"));

    // refcount
    assert(f.u.o->rc == 1);
    AVal g = a_nil();
    a_cpy(&g, f);
    assert(f.u.o->rc == 2);
    a_clr(&g);
    assert(f.u.o->rc == 1);
    a_dec(f);

    atm_quit();
    puts("ok");
    return 0;
}
