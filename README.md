# The Programming Language Atmos

***Structured Event-Driven Concurrency***

[
    [About](#about)                 |
    [Hello World!](#hello-world)    |
    [Install & Run](#install--run)  |
    [Documentation](#documentation) |
    [Environments](#environments)   |
    [Resources](#resources)         |
    [Development](#development)
]

<img src="atmos-logo.png" width="250" align="right">

# About

Atmos is a programming language that reconciles *[Structured Concurrency][sc]*
and *[Event-Driven Programming][events]*, extending classical structured
programming with two main functionalities:

- Structured Deterministic Concurrency:
    - A `task` primitive with deterministic scheduling provides predictable
      behavior and safe abortion.
    - A `tasks` container primitive holds attached tasks and controls their
      lifecycle.
    - A `pin` declaration attaches a task or tasks to its enclosing lexical
      scope.
    - Structured primitives (e.g., `par`) compose concurrent tasks with lexical
      scope.
- Event Signaling Mechanisms:
    - An `await` primitive suspends a task and waits for events.
    - An `emit` primitive signals events and awakes awaiting tasks.

Atmos is inspired by [synchronous programming languages][sync] like [Céu][ceu]
and [Esterel][esterel].

This repository is the implementation of Atmos that compiles to C:

- No virtual machine, no interpreter, and no garbage collector.
- The whole program becomes a single C file, which a C compiler builds.
- Tasks have no stacks of their own: a task is a record with its variables
  and the point where it resumes.
- Memory is reclaimed by reference counting, and by the lexical scope of
  `pin` declarations.

The original implementation of Atmos, which compiles to [Lua][lua], is at
[atmos-lang/atmos][atmos-lua].
The two differ in some points, which are [listed below](#differences).
The functional streams and the threads of the original are not part of this
implementation.

**Experimental: work in progress.**

[sc]:           https://en.wikipedia.org/wiki/Structured_concurrency
[events]:       https://en.wikipedia.org/wiki/Event-driven_programming

[sync]:         https://fsantanna.github.io/sc.html
[ceu]:          http://www.ceu-lang.org/
[esterel]:      https://en.wikipedia.org/wiki/Esterel
[lua]:          https://www.lua.org/
[atmos-lua]:    https://github.com/atmos-lang/atmos/

# Hello World!

During 5 seconds, displays `Hello World!` every second:

```
watching 5s {
    loop on 1s {
        print "Hello World!"
    }
}
```

The program body is a task in Atmos that behaves as follows:

- The `watching` command will execute its block during 5 seconds.
- The `loop on` will execute its block every second.
- Once the `watching` terminates, the body reaches its end, and the program
  exits cleanly.

Time is part of the language: with no environment, a program follows the real
clock.

# Install & Run

Atmos requires a C compiler and `make`:

```
git clone https://github.com/ia-atmos/lang
cd lang/
make
./atmos exs/hello.atm
```

The `make` builds the compiler `./atmos` and the runtime libraries in
`build/`.

The compiler translates a program to C, builds it, and runs it:

```
./atmos hello.atm [args]    # compiles, builds, and runs
./atmos -c hello.atm        # emits C only, to the standard output
./atmos -o hello hello.atm  # builds only
./atmos -t hello.atm        # enables test blocks
./atmos -e env.atm game.atm # includes an environment first
```

Environment variables:

- `ATMOS_CC`: C compiler (default `cc`).
- `ATMOS_CFLAGS`: C flags (default `-O1`).
- `ATMOS_LDFLAGS`: linker flags, which go after the program
  (e.g. `-lSDL2`).
- `ATMOS_ASAN`: if set, builds the program with the address sanitizer.

# Documentation

- [Manual](doc/manual.md)
- [Examples](exs/)

# Environments

An environment is an external component that bridges input events from the real
world into an Atmos application.

The default environment only deals with time: it follows the real clock, and
terminates the program when there is nothing left to awake.

Other environments are written in C, in the Atmos program itself or in a file
that it includes:

- [`lib/sdl.atm`](lib/sdl.atm):
    A basic environment that relies on [SDL][sdl] to provide a window, keys,
    images, text, sounds, and timers.
- [`raylib.atm`](https://github.com/ia-atmos/env-ray):
    An environment that relies on [raylib][raylib], with keys, mouse,
    textures, sounds, and fonts.

Both have a test mode with no screen, with a fixed number of frames and
scripted input.

An environment may have a compiler of its own, such as `atmos-pico`, so that a
program needs no `include`: `make env` builds one (see the
[manual](doc/manual.md#environments)).

[sdl]:          https://www.libsdl.org/
[raylib]:       https://www.raylib.com/

# Resources

- [A toy problem][toy]: Drag, Click, or Cancel
- A simple but complete 2D game in Atmos:
    - [`exs/rocks/`](exs/rocks/)
- Academic publications (Céu):
    - http://ceu-lang.org/chico/#ceu
- Mailing list (Céu & Atmos):
    - https://groups.google.com/g/ceu-lang

[toy]: https://fsantanna.github.io/toy.html

# Development

The plan, with the settled decisions and the milestones, is in
[`.claude/plans/261109-c-backend.md`](.claude/plans/261109-c-backend.md).

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

## Differences

From the [original implementation][atmos-lua], which compiles to Lua:

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
- `X.load(s)` reads data written with the constructors of the
  language, as a JSON of Atmos: tables, vectors, tags, numbers,
  durations, strings, and comments. No expressions. `X.dump(v)` writes
  it back.
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
