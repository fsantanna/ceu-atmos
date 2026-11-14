CC     ?= cc
CFLAGS ?= -O2 -g -Wall -Wextra
ASAN   := -fsanitize=address -fno-omit-frame-pointer

RT_SRC := rt/val.c rt/str.c rt/tab.c rt/ops.c rt/task.c rt/std.c \
          rt/lstring.c rt/lvector.c rt/los.c rt/lio.c \
          rt/lload.c
RT_HDR := rt/atmos.h rt/lib.h

CC_SRC := src/aux.c src/lexer.c src/parser.c src/ast.c src/resolver.c \
          src/coder.c src/main.c
CC_HDR := src/atmc.h

all: atmos build/libatmos.a build/libatmos-asan.a

atmos: $(CC_SRC) $(CC_HDR)
	$(CC) $(CFLAGS) -DATMOS_HOME='"$(CURDIR)"' -Isrc $(CC_SRC) -o $@

# A compiler dedicated to an environment, such as `atmos-pico`: this
# compiler, with the file of the environment included before every
# program, and with the flags that the environment needs. Paths must be
# absolute, and the flags must have no quotes.
#
#   make -C <lang> env OUT=<dir>/atmos-pico ENV=<dir>/pico.atm \
#        ENV_CFLAGS="-I<dir>/c/src" ENV_LDFLAGS="<dir>/c/libpico.a -lSDL2"
env: $(CC_SRC) $(CC_HDR) build/libatmos.a build/libatmos-asan.a
	@test -n "$(OUT)" -a -n "$(ENV)" || \
	    { echo "usage: make env OUT=<executable> ENV=<file.atm> [ENV_CFLAGS=...] [ENV_LDFLAGS=...]"; exit 1; }
	$(CC) $(CFLAGS) -DATMOS_HOME='"$(CURDIR)"' \
	    -DATMOS_NAME='"$(notdir $(OUT))"' \
	    -DATMOS_ENV='"$(abspath $(ENV))"' \
	    -DATMOS_ENV_CFLAGS='"$(ENV_CFLAGS)"' \
	    -DATMOS_ENV_LDFLAGS='"$(ENV_LDFLAGS)"' \
	    -Isrc $(CC_SRC) -o $(OUT)

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

test-golden: all
	tst/run.sh golden

test-env: all
	tst/env/run.sh

test: test-rt test-lex test-ast test-cases test-golden test-env

clean:
	rm -rf build

.PHONY: all env test test-rt test-lex test-ast test-cases test-golden test-env clean
