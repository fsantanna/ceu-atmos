# ceu-atmos

Atmos with a C backend: a from-scratch implementation of the
[Atmos](https://github.com/atmos-lang/atmos) programming language that
compiles to C.

- No VM, no interpreter, no Lua.
- Whole-program compilation to one C file, then `cc`.
- Stackless tasks with structured, deterministic concurrency.
- Reference counting, no garbage collector.

The language is described in the [manual](doc/manual.md), and there are
a few [examples](exs/).

The plan, with the settled decisions and the milestones, is in
[`.claude/plans/261109-c-backend.md`](.claude/plans/261109-c-backend.md).

**Experimental: work in progress.**

## Build

```
make
```

It builds the compiler `./atmos` and the runtime libraries in `build/`.
It requires a C compiler and `make`.

## Use

```
./atmos hello.atm [args]    # compiles, builds, and runs
./atmos -c hello.atm        # emits C only
./atmos -o hello hello.atm  # builds only
./atmos -t hello.atm        # enables test blocks
```

```
spawn {
    watching :done {
        par {
            loop on :tick {
                print "tick A"
            }
        } with {
            loop on :tick {
                print "tick B"
            }
        }
    }
    print "done"
}
emit(:tick)     ;; --> tick A, tick B
emit(:done)     ;; --> done
```

Environment variables:

- `ATMOS_CC`: C compiler (default `cc`).
- `ATMOS_CFLAGS`: C flags (default `-O1`).
- `ATMOS_LDFLAGS`: linker flags, which go after the program
  (e.g. `-lSDL2`).
- `ATMOS_ASAN`: if set, builds the program with the address sanitizer.

## Test

```
make test
```

- `tst/rt/`: unit tests of the runtime, in C.
- `tst/lex/`, `tst/ast/`: lexer and parser golden cases.
- `tst/golden/`: the cases of the reference implementation that this
  compiler reproduces, extracted by `tst/extract.py`.
    - Some are adapted to this language (base 0, operators that
      became functions), as `tst/adapt.py` tells. `ADAPTED.md` lists
      them.
    - Those left out are in `tst/dropped/`, with the reasons in
      `INDEX.md`.
- `tst/cases/`: golden programs, built with the address sanitizer
  (leaks included).

## Layout

```
src/    compiler: lexer, parser, resolver, coder
rt/     runtime: values, strings, tables, operations, tasks, stdlib
rt/atmos.h   C API for generated code, natives, and environments
tst/    tests
doc/    manual
exs/    examples
lib/    libraries in Atmos: sdl.atm
```

## Differences from the current Atmos

- Indexes start at `0`: vectors, numeric loops, and strings.
- An unknown identifier is a compile error: globals are static.
- `await` is only valid lexically inside a task body.
- `catch` only catches `throw`.
- `break`, `until`, and `while` need a lexically enclosing `loop`.
- Numbers are 64-bit integers or reals, as in Lua 5.3: `7 / 2` is
  `3.5`, `7 // 2` is `3`, and a real always shows as one (`5.0`).
- Operators follow C:
    - precedence: `a + b * c` needs no parentheses;
    - `%` has the sign of the dividend, and `//` truncates;
    - bitwise `&` `|` `^` `~` `<<` `>>` on integers, binding stronger
      than comparisons;
    - compound assignment: `set x += 1`.
- `**`, `?>`, `!>`, `=>=`, `=<=`, `===`, and `=!=` are functions:
  `math.pow`, `X.xin`, `X.nin`, `X.gte`, `X.lte`, `X.eq`, and `X.neq`.
  The strict `X.gt` and `X.lt` are new.
- Statements take their arguments with no parentheses: `return x + 1`,
  `break`, `until i == 10`, `throw :X, "msg"`, `abort t`, `emit e`.
    - Lists need no parentheses either: `return 1, 2`,
      `val a, b = 1, 2`, `set x, y = y, x`.
    - `(a, b)` is still a list anywhere, and `(f())` has a single
      value, as in Lua.
    - The predicates of await patterns too: `await :X until it.n > 3`.
      Inside `<...>`, the operators `>` `>>` `&&` `||` need
      parentheses, or it is an ambiguity error.
- A declaration, an assignment, or a `defer` is only valid as a
  statement of a block. Using one as a value, as in
  `print(val x = 1)`, is a compile error.
- `include "file.atm"` joins files at compile time: the statements of
  the file take the place of the include. There is no `require`.
- Native literals are C code, not Lua: `` `:number strlen("abc")` ``.
    - A leading tag converts the expression: `:number`, `:boolean`,
      `:string`, `:value`. With no tag, the code is a statement.
    - `` `:prelude ...` `` declares at file scope, as in `#include`.
    - `$x` refers to the variable `x`.
- The libraries are `string`, `vector`, `math`, `io`, and `os`, with
  0-based positions, instead of the libraries of Lua.
- `pin` applies to any dynamic value: tasks, pools, tables, and
  functions.
    - A pinned value is freed when its block ends, cycles included.
    - It cannot escape its block: storing it in an outer variable, in
      an unpinned table, in a `throw`, or emitting it outwards raises
      `invalid escape : pinned value`.
    - Returning it from its own block transfers it: it flows out as a
      fresh value, which may be pinned again.
