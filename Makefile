CC     ?= cc
CFLAGS ?= -O2 -g -Wall -Wextra
ASAN   := -fsanitize=address -fno-omit-frame-pointer

RT_SRC := rt/val.c rt/str.c rt/std.c
RT_HDR := rt/atmos.h

CC_SRC := src/aux.c src/lexer.c src/parser.c src/ast.c src/coder.c \
          src/main.c
CC_HDR := src/atmc.h

all: atmos build/libatmos.a build/libatmos-asan.a

atmos: $(CC_SRC) $(CC_HDR)
	$(CC) $(CFLAGS) -DATMOS_HOME='"$(CURDIR)"' -Isrc $(CC_SRC) -o $@

build/rt/%.o: rt/%.c $(RT_HDR)
	@mkdir -p build/rt
	$(CC) $(CFLAGS) -Irt -c $< -o $@

build/rt-asan/%.o: rt/%.c $(RT_HDR)
	@mkdir -p build/rt-asan
	$(CC) $(CFLAGS) $(ASAN) -Irt -c $< -o $@

build/libatmos.a: $(RT_SRC:rt/%.c=build/rt/%.o) build/rt/main.o
	ar rcs $@ $^

build/libatmos-asan.a: $(RT_SRC:rt/%.c=build/rt-asan/%.o) build/rt-asan/main.o
	ar rcs $@ $^

# runtime unit tests, built with the address sanitizer
RT_TST := $(wildcard tst/rt/*.c)

test-rt: $(RT_TST:tst/rt/%.c=build/tst/rt/%)
	@for t in $^; do echo "$$t"; $$t || exit 1; done

build/tst/rt/%: tst/rt/%.c $(RT_SRC) $(RT_HDR)
	@mkdir -p build/tst/rt
	$(CC) $(CFLAGS) $(ASAN) -Irt $< $(RT_SRC) -lm -o $@

test-lex: atmos
	tst/run.sh lex

test-ast: atmos
	tst/run.sh ast

test-cases: all
	tst/run.sh cases

test: test-rt test-lex test-ast test-cases

clean:
	rm -rf build

.PHONY: all test test-rt test-lex test-ast test-cases clean
