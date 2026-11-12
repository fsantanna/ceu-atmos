# The Programming Language Atmos

* DESIGN
    * Structured Deterministic Concurrency
    * Event Signaling Mechanisms
    * Hierarchical Tags
    * Lexical Memory Management
    * Integration with C
* EXECUTION
    * Environments
* LEXICON
    * Keywords
    * Symbols
    * Operators
        - `==` `!=` `??` `!?`
        - `>` `<` `>=` `<=`
        - `+` `-` `*` `/` `//` `%`
        - `!` `||` `&&`
        - `&` `|` `^` `~` `<<` `>>`
        - `#` `++`
    * Identifiers
        - `[A-Za-z_][A-Za-z0-9_]*`
    * Literals
        - `nil` `true` `false`
        - `:X` `10` `"hello"`
        - `2h30min` `` `c` ``
    * Comments
        - `;; *` `;;; * ;;;`
* TYPES & VALUES
    * Table
        - `[]` `:X []`
    * Function
        - `func` `\{}`
    * Tasks
        - `task` `tasks` `pub`
    * Native
* EXPRESSIONS
    * Chunks
        - `;` `include` `do` `escape` `defer` `test`
    * Declarations and Assignments
        - `val` `var` `pin` `where`
        - `func` `return`
        - `set` `+=`
    * Operations
        - `??` `!?`
        - `#` `++`
    * Indexing
        - `t.x` `t@0` `t@i` `t@#` `t@+`
    * Calls
        - `f(*)` `-->` `->` `<-` `<--`
    * Conditionals
        - `if` `ifs` `match`
    * Loop
        - `loop` `break` `until` `while`
    * Exceptions
        - `throw` `catch`
    * Task Operations
        - `spawn` `await` `emit` `toggle`
        - `par` `watching`
* STANDARD LIBRARIES
    - `math` `string` `vector` `io` `os` `X` `atmos`
* SYNTAX
    * Ambiguities

<!-- CONTENTS -->

# DESIGN

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
- Lexical Memory Management:
    - A lexical policy to manage dynamic allocation automatically.
    - A set of strict escaping rules to preserve structured reasoning.
    - A reference-counter collector for deterministic reclamation.

Atmos is inspired by [synchronous programming languages][sync] like [Céu][ceu]
and [Esterel][esterel].

Follows an extended list of functionalities in Atmos:

- Dynamic typing
- Statements as expressions
- Dynamic collections (tables)
- Deferred statements for finalization
- Exception handling (throw & catch)
- Seamless integration with C

Atmos is inspired by [Lua][lua]:
It mimics most of the semantics of Lua with respect to values, types,
declarations, and expressions.
Nevertheless, this implementation does not depend on Lua: it compiles to C,
which links with a small runtime with no virtual machine and no garbage
collector.

Atmos is in **experimental stage**.

In the rest of this section, we introduce key aspects of Atmos:
*Structured Deterministic Concurrency*, *Event Signaling Mechanisms*,
*Hierarchical Tags*, *Lexical Memory Management*, and *Integration with C*.

[sc]:           https://en.wikipedia.org/wiki/Structured_concurrency
[events]:       https://en.wikipedia.org/wiki/Event-driven_programming
[sync]:         https://fsantanna.github.io/sc.html
[ceu]:          http://www.ceu-lang.org/
[esterel]:      https://en.wikipedia.org/wiki/Esterel
[lua]:          https://www.lua.org/
[syms]:         https://en.wikipedia.org/wiki/Symbol_(programming)

## Structured Deterministic Concurrency

In structured concurrency, the life cycle of processes or tasks respect the
structure of the source code as hierarchical blocks.
In this sense, tasks in Atmos are treated in the same way as local variables of
structured programming:
When a [block](#blocks) of code terminates or goes out of scope, all of its
[local variables](#local-variables) become inaccessible to enclosing blocks.
In addition, all of its [pinned tasks](#pin) are aborted and
properly finalized by [deferred statements](#defer).

Tasks in Atmos are similar to [coroutines][lua-coroutines], and adhere to a
predictable "run-to-completion" semantics:
Unlike OS threads, tasks execute uninterruptedly up to explicit [await](#await)
operations.

The next example illustrates structured concurrency, abortion of tasks, and
deterministic scheduling.
The example uses a `par` to spawn two concurrent tasks and rejoin when `:any`
of them terminates:
    one task is just a 10-seconds timeout, and the other
    increments `n` every second, showing its value on termination:

<!-- exs/01-counter.atm -->

```
par :any {
    await 10s
} with {
    var n = 0
    defer {
        print("I counted ", n)    ;; invariably outputs 9
    }
    loop on 1s {
        set n = n + 1
    }
}
```

A [par](#parallel) is a structured mechanism that combines tasks in nested
blocks and rejoins as a whole when `:any` of them terminates (in this case),
automatically aborting the others.

The [loop on](#loop) in the second task iterates exactly 9 times before the
first task awakes and terminates the composition.
For this reason, the second task is aborted before it has the opportunity to
awake for the 10th time, but its `defer` statement still executes and outputs
`"I counted 9"`.

Since tasks only yield control explicitly, scheduling is entirely
deterministic.
In addition, tasks awake in the order they appear in the source code, which
makes the scheduling order predictable.
This rule allows us to infer that the example invariably outputs `9`, no matter
how many times we re-execute it.
Likewise, if the order of the two tasks inside the `par` were inverted, the
example would always output `10`.

[lua-coroutines]: https://www.lua.org/manual/5.4/manual.html#2.6

## Event Signaling Mechanisms

Tasks can communicate through events as follows:

- The [await](#await) statement suspends a task until it matches an event
  condition.
- The [emit](#emit) statement broadcasts an event to all awaiting
  tasks.

<img src="bcast.png" align="right"/>

Tasks form a dynamic tree representing the structure of the program, as
illustrated in the figure.
This tree is traversed on every `emit` in a predictable way, since it
respects the lexical structure of the program:
A task has exactly one active block at a time, which is first traversed `(1)`.
The active block has a list of active tasks, which are traversed in sequence
`(2,3)`, and exactly one nested block, which is traversed after the nested
tasks `(4)`.
After the nested blocks and tasks are traversed, the outer task itself is
traversed at its single yielded execution point `(5)`.
Finally, the task next to the outer task is traversed in the same way `(6)`.
An `emit` statement traversal runs to completion before proceeding to the next
statement, just like a function call.

The tasks of a block are traversed in the order they were attached to it, and
the tasks of a pool, at the position of the pool, in the order they were
spawned.
A task only reacts to an `emit` that starts after its `await`: a task that is
spawned, or that awaits again, in reaction to an event does not awake again in
the same traversal.

The next example illustrates event broadcasts and tasks traversal.
The example uses a `watching` statement to observe an event condition while
executing a nested task.
When the condition is satisfied, the nested task is aborted:

<!-- exs/02-ticks.atm -->

```
spawn {
    watching :done {
        par {
            loop on :tick {
                print "tick A"  ;; always awakes first
            }
        } with {
            loop on :tick {
                print "tick B"  ;; always awakes last
            }
        }
    }
    print "done"
}
emit(:tick)     ;; --> "tick A", "tick B"
emit(:tick)     ;; --> "tick A", "tick B"
emit(:done)     ;; --> "done"
print "end"     ;; --> "end"
```

The main body has an outermost `spawn` task, which awaits `:done`, and has two
nested tasks awaiting `:tick` events.
Then, the main body broadcasts three events in sequence.
The first two `:tick` events awake the nested tasks respecting the structure of
the program, printing `tick A` and `tick B` in this order.
The last event aborts the `watching` composition and prints `done`, before
terminating the main body.

### Bidimensional Stack Traces

`TODO`

## Hierarchical Tags

Tags represent unique human-readable values, and are similar to Lua strings or
[*symbols* or *atoms*][syms] in other programming languages.
Any identifier prefixed with a colon (`:`) is a valid tag, which is guaranteed
to be unique in comparison to others (i.e., `:x == :x` and `:x != :y`).

Tags are syntactic values that only exist at compile time.
During runtime, they are converted to strings and become indistinguishable from
them (i.e., `:x == 'x'`).

Tags are typically used as keys in table (e.g., `:x`, `:y`), or as enumerations
representing states (e.g., `:pending`, `:done`).

<!-- exs/03-tags.atm -->

The next example uses tags as table keys:

```
val pos = []           ;; a new table
set pos@:x = 10
set pos.y = 20          ;; equivalent to pos@:y=20
print(pos.x, pos@:y)    ;; -> 10, 20
```

Tags can also be used to "tag" tables, introducing the notion of lightweight
user types in Atmos.
The constructor `:Pos [x=10,y=20]` is equivalent to `[tag=:Pos,x=10,y=20]`.

Tags can describe type hierarchies by splitting identifiers with (`.`).
For instance, a tag such as `:T.A.x` is a subtype of `:T`, `:T.A`, and
`:T.A.x` at the same time, as verified by the
[equivalence operator](#equivalence) `??`:

```
print(:T.A.x ?? :T)         ;; --> true  (:T.A.x is a subtype of :T)
print(:T.A.x ?? :T.A)       ;; --> true
print(:T.A.x ?? :T.A.x)     ;; --> true
print(:T     ?? :T.A.x)     ;; --> false (:T is not a subtype of :T.A.x)
print(:T.A   ?? :T.B)       ;; --> false
```

The operator `??` also works with tagged tables.
Therefore, tags, tables, and `??` can be combined as follows:

```
val t = :T.A [ a=10 ]      ;; [ tag=:T.A, a=10 ]
print(t ?? :T)              ;; --> true
```

## Lexical Memory Management

Atmos has no garbage collector.
Dynamic values, such as tables and functions, are reclaimed by counting
references, as soon as the last reference to them goes away.

Reference counting alone has two limitations:
it does not reclaim cycles, and it does not tell when a value is reclaimed.
A [`pin` declaration](#pin) addresses both by attaching a value to its
enclosing block:

- The value is reclaimed when the block terminates, even if it is part of a
  cycle.
- The value cannot escape the block, which is verified when the program
  executes.

The next example builds a cycle that is reclaimed when the block terminates:

```
do {
    pin a = [10]
    pin b = [20]
    set a.other = b         ;; a <-> b
    set b.other = a
    print(a.other@0)        ;; --> 20
}                           ;; a and b are reclaimed here
```

The rules for tasks are the same:
a pinned task is aborted when its block terminates, and cannot be referred
from outside.
Therefore, a task, the data it uses, and the block that holds them all live
and die together.

## Integration with C

Atmos compiles to C, and a program can embed C code in
[native literals](#native-literals), which are enclosed by backquotes:

```
```:prelude
#include <math.h>
```
val x = 9
val y = `:number sqrt(a_dbl($x))`  ;; a C expression converted to a number
print(y)                            ;; --> 3.0
```

A tag in the beginning of the literal tells how to convert the C expression to
an Atmos value, and `$x` refers to the Atmos variable `x`.
A native literal with no tag is a sequence of C statements.

The same C interface is available to write [environments](#environments),
which connect a program to the outside world.

### Lua vs Atmos Subtleties

Atmos borrows much from Lua, and some differences are worth mentioning:

- Indexes:
    - Lua: vectors, numeric loops, and string positions start at `1`.
    - Atmos: they all start at `0`.
- Operators:
    - Lua: its own precedences, `%` and `//` round towards minus infinity.
    - Atmos: the precedences and the arithmetic of C.

- Statements `return` and `break`:
    - Lua: `return 10`, `break`, always the last statement of a block
    - Atmos: `return 10`, `break`, anywhere, since they are expressions
        - The same holds for `abort`, `emit`, `escape`, `throw`, `until`, and
          `while`, which take their [arguments](#statement-arguments) with or
          without parenthesis.
- Method call:
    - Lua: `o:f()` (single colon)
    - Atmos: `o::f()` (double colon)
        - The reason is to avoid ambiguity with the syntax of tags:
            - `f () :x ()` is `f():x()` or `f() ; :x()`?
- List of expressions:
    - Lua: `e1,e2,...` (no parenthesis)
    - Atmos: `e1,e2,...` or `(e1,e2,...)`
        - A list with no parenthesis is valid, like in Lua, in the right of
          declarations and assignments, and after statements such as `return`.
        - A list enclosed by parenthesis is an expression, which is valid
          anywhere, such as in `f <-- (1,2)`.
        - Like in Lua, a single expression enclosed by parenthesis has a
          single value: `(f())` is the first value of `f()`.
- Table constructor:
    - Lua: `{ ... }` (braces)
    - Atmos: `[ ... ]` (brackets)
        - The reason is to avoid ambiguity with blocks:
            - `if f { ... }` is `if f{...} ...` or `if (f) { ... }`?
- Operator symbols:
    - Lua: `~=` `and` `or` `not` `..` `^`
    - Atmos: `!=` `&&` `||` `!` `++` `math.pow`
        - The reason is to avoid identifiers as operators and to use familiar
          and consistent alternatives.
- Globals:
    - Lua: an unknown name is a global variable with value `nil`.
    - Atmos: an unknown name is a compile error.

# EXECUTION

To execute Atmos, simply pass the program filename and arguments to the
compiler, which builds and runs the program, e.g.:

```
$ atmos hello.atm 1000
```

The arguments are passed to the [program main chunk](#chunks).

The compiler accepts the following options:

```
$ atmos

Usage: atmos [-c] [-o <out>] [-t] <input> [<args>] ...

Options:
   -c          Emit C only.
   -o <out>    Build only.
   -t          Enable test blocks.
```

The compiler translates the program to a single C file, and then calls the C
compiler to build it and link it with the Atmos runtime.
The following environment variables affect the build:

- `ATMOS_CC`: the C compiler (default `cc`).
- `ATMOS_CFLAGS`: the flags for the C compiler (default `-O1`).
- `ATMOS_LDFLAGS`: the flags for the linker, which go after the program
  (e.g., `-lSDL2`).
- `ATMOS_ASAN`: if set, builds the program with the address sanitizer, which
  also reports memory leaks when the program terminates.
- `ATMOS_HOME`: the directory with the runtime (`rt/` and `build/`).
  By default, it is the directory of the compiler itself, so a checkout may be
  moved after it is built.

## Environments

An environment connects a program with the outside world:
it waits for external input, and delivers it to the program as events.

The default environment only deals with time.
After the main chunk executes, it sleeps until the nearest
[timer](#await) expires, and then lets the time pass in the program.
It terminates the program when the main chunk terminates, or when there are no
more timers to awake it.

Other environments are written in C, over the interface of the runtime:

```
atm_init(argc, argv);
AStatus st = atm_main();            // executes the main chunk
while (st==A_OK && atm_alive()) {
    <waits for input>
    st = atm_emit(evt);             // emits an event to the program
}
if (st == A_OK) {
    st = atm_stop();                // aborts the main chunk
}
atm_quit();
```

An environment may be part of the program itself, in a
[native literal](#native-literals) with the tag `:prelude`.
The example `exs/stdin.atm` emits each line of the standard input as an event.

# LEXICON

## Keywords

The following keywords are reserved in Atmos:

```
    await               ;; await event
    break               ;; loop break
    catch               ;; catch exception
    defer               ;; defer block
    do                  ;; do block
    else                ;; else block
    emit                ;; emit event
    escape              ;; escape block
    false               ;; false value
    func                ;; function                         (10)
    if                  ;; if block
    ifs                 ;; ifs block
    in                  ;; in iterator
    include             ;; include file
    it                  ;; implicit parameter
    loop                ;; loop block
    match               ;; match block
    nil                 ;; nil value
    on                  ;; on event clause
    par                 ;; par block
    pin                 ;; pin declaration                  (20)
    pub                 ;; public variable
    return              ;; escape prototype
    set                 ;; assign expression
    spawn               ;; spawn task
    task                ;; task prototype
    tasks               ;; task pool
    test                ;; test block
    throw               ;; throw error
    toggle              ;; toggle task
    true                ;; true value                       (30)
    until               ;; until loop condition
    val                 ;; constant declaration
    var                 ;; variable declaration
    watching            ;; watching block
    where               ;; where block
    while               ;; while loop condition
    with                ;; with block                       (37)
```

<!--
    skip                ;; loop skip
-->

## Symbols

The following symbols are designated in Atmos:

```
    {   }           ;; block delimiters
    (   )           ;; expression delimiters
    [   ]           ;; table constructor delimiters
    @               ;; table indexing
    \               ;; lambda declaration
    =               ;; assignment separator
    =>              ;; if/ifs/match clauses
    <-- <- -> -->   ;; pipe calls
    ;               ;; sequence separator
    '   "   `       ;; string/native delimiters
    ,               ;; argument/constructor separator
    :               ;; tag prefix
    ::              ;; method call
    .               ;; field discriminator
    ...             ;; variadic parameters/arguments
```

## Operators

The following operators are supported in Atmos:

```
    ==   !=  ??   !?                ;; equivalence
    >    <    >=   <=               ;; relational
    +    -    *    /    //   %      ;; arithmetic
    !    ||   &&                    ;; logical
    &    |    ^    ~    <<   >>     ;; bitwise
    #                               ;; length
    ++                              ;; concatenation
```

Operators are used in [operation](#operations) expressions.

The binary operators, except the equivalence and relational ones, also
combine with `=` to form the compound assignments of the
[`set` expression](#set), such as `+=` and `++=`.

## Identifiers

Atmos uses identifiers to refer to [variables](#declarations-and-assignments),
[functions](#function), and [fields](#table):

A variable identifier starts with a letter or underscore (`_`) and is followed
by letters, digits, or underscores:

```
ID : [A-Za-z_][A-Za-z0-9_]*     ;; letter/underscore/digit
```

Examples:

```
x
my_value
y10
```

## Literals

Atmos provides literals for all [value types](#types--values):
    `nil`, `boolean`, `number`, and `string`.

It also provides literals for [tags](#hierarchical-tags), clock durations, and
[native](#native-literals) code, which only exist at compile time.

```
NIL  : nil
BOOL : true | false
TAG  : :[A-Za-z0-9_\.]+     ;; colon + leter/digit/under/dot
NUM  : [0-9][0-9A-Za-z\.]*  ;; digit/letter/dot
CLK  : ([0-9\.]+(day|h|min|s|ms|us))+ ;; clock duration expression
STR  : '.*' | ".*"          ;; string expression
NAT  : `.*`                 ;; native code
```

The literals for `nil`, `boolean` and `number` follow the same
[lexical conventions of Lua][lua-lexical].
A number with no fractional part and no exponent is an integer (e.g., `10`,
`0xFF`), and any other is a real (e.g., `1.5`, `1e3`).

The literal `nil` is the single value of the `nil` type.

The literals `true` and `false` are the only values of the `boolean` type.

A `tag` literal starts with a colon (`:`) and is followed by letters,
digits, and dots (`.`).

A `number` literal starts with a digit and is followed by digits, letters, and
dots (`.`).

A `clock` duration literal is a straight sequence of numbers and time units
(e.g. `1min30s`), which is converted into a number in microseconds.

A `string` literal is a sequence of characters enclosed by an odd number
of matching double (`"`) or single (`'`) quotes.
Atmos supports multi-line strings when using multiple quote delimiters.

A string with a single delimiter is restricted to one line and supports the
escape sequences of Lua, such as `\n`, `\t`, `\\`, `\"`, `\'`, `\xHH`, and
`\ddd`.
A string with three or more delimiters may span multiple lines and is taken
verbatim, with no escape sequences.

A `native` literal is a sequence of characters enclosed by an odd number
of matching back quotes (`` ` ``).
Atmos supports multi-line native literals when using multiple quote delimiters.
Native literals are used in expressions and are interpreted as
[C code](#native-literals).

Examples:

<!-- exs/lex-01-literals.atm -->

```
nil                 ;; nil literal
false               ;; boolean literal
:X.Y                ;; tag literal
"""Hello!"""        ;; string literal
1.25                ;; number literal
`:number strlen("c")`       ;; native literal
```

[lua-lexical]: https://www.lua.org/manual/5.4/manual.html#3.1

### Native Literals

A native literal holds C code, which goes as it is to the C file that the
compiler generates.
A tag in the beginning of the literal gives its kind:

```
`stmts`             ;; C statements, evaluates to nil
`:prelude decls`    ;; C declarations in the beginning of the file
`:number e`         ;; C expression converted to a number
`:boolean e`        ;; C expression converted to a boolean
`:string e`         ;; C string, which is copied (NULL becomes nil)
`:value e`          ;; C expression that is already an Atmos value
```

The tags are the names of the [types](#types--values).
A `:number` becomes an integer or a real according to the type of the
expression in C: `` `:number 7/2` `` is `3`, and `` `:number 7/2.0` `` is `3.5`.
The tags `:number.integer` and `:number.float` force one of them.

In the code, `$x` refers to the Atmos variable `x`, which is a value of type
`AVal` that can be read and assigned.
Only a variable can follow the `$`: what comes after its name is C, such that
`$t.x` is the member `x` of the `AVal`, and not the field `x` of the table `t`,
which must be read into a variable first.
The interface of the runtime (`rt/atmos.h`) has the operations to inspect and
create values, such as `a_dbl(v)` to read a number, `A_STR_OF(v)->s` to read
a string, and `a_int(i)`, `a_num(d)`, and `a_strz(s)` to create values.

Examples:

```
```:prelude
#include <string.h>
```
val s = "hello"
val n = `:number strlen(A_STR_OF($s)->s)`       ;; 5
var x = 1
`$x = a_int($x.u.i + 1);`               ;; x becomes 2
print(n, x)                             ;; --> 5, 2
```

## Comments

Atmos provides single-line and multi-line comments.

Single-line comments start with double semi-colons (`;;`) and run until the end
of the line.

Multi-line comments are enclosed by three of more matching semi-colons.

Examples:

<!-- exs/lex-02-comments.atm -->

```
;; a comment        ;; single-line comment

;;;                 ;; multi-line comment
;; a
;; comment
;;;
```

# TYPES & VALUES

Atmos supports and mimics the semantics of the standard [Lua types][lua-types]:
    `nil`, `boolean`, `number`, `string`, `function`, and `table`.

Atmos also provides three new types related to tasks:
    `task`, `xtask`, and `tasks`.

Finally, the type `native` represents resources of [C](#native), such as
files.

Atmos differentiates between *value* and *reference* types:

- Value types are built from the [basic literals](#literals):
    `nil`, `boolean`, `number`, and `string`.
- Reference types are built from constructors:
    `function`, `table`, `task`, `xtask`, `tasks`, and `native`.

A `number` is either an integer with 64 bits or a real with double precision,
as in Lua.
An operation with two integers results in an integer, except for the division
`/`, which always results in a real.
Any other operation with numbers results in a real.
An integer and a real with the same value are equal (`1 == 1.0`), and are
the same key in a table.

[lua-types]: https://www.lua.org/manual/5.4/manual.html#2.1

## Table

The `table` reference type represents [Lua tables][lua-types] with indexes of
any type.

A table constructor `[ * ]` receives a list `*` of key-value assignments:

```
Table : `[´ Key_Val* `]´
Key_Val : At `=´ Expr           ;; computed key
        | ID `=´ Expr           ;; string key
        | Expr                  ;; positional index
```

The `At` (`@`) keys follow the same [indexing rules](#indexing).

Like [table constructors in Lua][lua-table], it accepts assignments in three
formats:

- `@(e1)=e2` maps key/index `e1` to value `e2`
- `id=e` maps string `id` to `e` (same as `@("id")=e`).
  The name may be a keyword, as in `[loop=true]`.
- `e` maps numeric index `i` to `e` (same as `@(i)=e`), where `i` starts at `0`
  and increments after each assignment

A table is also a vector if it contains numeric indexes starting from `0` with
no [holes][lua-sequences].

If the last positional entry is a call or the variadic `...`, then all of its
values are entries of the table.

[lua-sequences]: https://www.lua.org/manual/5.4/manual.html#3.4.7

Examples:

<!-- exs/val-03-table.atm -->

```
val k = "idx"
val t = [      ;; all 3 formats:
    @k = 10,   ;; same as [ @(k)=10, @("v")="x", @(0)=20, @(1)=30 ]
    v = "x",
    20, 30
]
print(type(t))              ;; --> 'table'
print(t.idx, t@("v"), t@1)  ;; --> 10, x, 30
```

<!-- exs/val-02-vector.atm -->

```
val vs = [1, 2, 3]      ;; a vector of numbers
print(vs@1)             ;; --> 2
print(vs ?? :table)     ;; --> true
set vs@(#vs) = 4        ;; [1, 2, 3, 4]
```

[lua-table]: https://www.lua.org/manual/5.4/manual.html#3.4.9

### User Types

A table constructor may also be prefixed with a [tag](#literals) to represent
an user type:

```
User : TAG Table
```

The tag is assigned to key `"tag"`, i.e., `:X [ * ]` is equivalent to
`[ tag=:X, * ]`

Examples:

<!-- exs/val-04-users.atm -->

```
val p = :Pos [         ;; a tagged table:
    x = 10,             ;; same as [ @("tag")="Pos", @("x")=10, @("y")=20 ]
    y = 20,
]
print(p ?? :table)      ;; --> true
print(p ?? :Pos)        ;; --> true
```

See [Ambiguities](#ambiguities):
    `:X` ⏎ `[]` reads as two statements `:X ; []` (not constructor `:X []`).

## Function

The `function` reference type represents [Lua functions][lua-function].

The basic constructor creates an anonymous function with a list of parameters
and an execution block:

```
Func : `func´ `(´ ID* [`...´] `)´ Block
```

The parameters is a list of [identifiers](#identifiers) with an optional
variadic symbol `...` at the end.
The parameters are immutable as if they were `val`
[declarations](#local-variables).
The [block](#blocks) is a sequence of expressions.

Examples:

<!-- exs/val-05-function.atm -->

```
val f = func (x, y) {     ;; function to add arguments
    x + y
}
print(type(f))      ;; --> 'function'
print(f(1,2))       ;; --> 3
```

Atmos also provides conventional [prototype declarations](#prototypes) for
functions and [tasks](#tasks).

### Lambda

Atmos also supports an alternative lambda notation to create functions:

```
Lambda : `\` [ID | `(` ID* `)´] Block
```

There are three variations of lambdas:

- `\(<ids>) { <body> }`:
    equivalent to `func (<ids>) { <body> }`
- `\<id> { <body> }`:
    equivalent to `\(<id>) { <body> }`
- `\{ <body> }`:
    equivalent to `\(it) { <body> }`

Note that the lambda notation is also used in [conditionals](#conditionals) to
communicate values across blocks.

Examples:

<!-- exs/val-05-function.atm -->

```
val g = \{ it + 1 }     ;; function to increment argument
print(g ?? :function)   ;; --> true
print(g(f(1,2)))        ;; --> 4
```

See [Ambiguities](#ambiguities):
    `\-` reads as `\(a,b){ a - b }` (not `\(a){ -a }`).

[lua-function]: https://www.lua.org/manual/5.4/manual.html#3.4.11

## Tasks

Tasks have three associated reference types:
    a `task` prototype,
    an `xtask` instance, and
    a `tasks` pool.

```
Task  : `task´ `(´ ID* [`...´] `)´ Block
Tasks : `tasks´ `(´ [Expr] `)´

Spawn : `spawn` [At] Expr `(´ Expr* `)`
      | `spawn` Block

XTask : `task´
```

A `task` prototype specifies an execution body and follows the same rules of
[function constructors](#function).

The `tasks` constructor creates pool of tasks, which groups related tasks as
a collection.
The optional numeric argument specifies that it holds at most `n` tasks.
If the limit is omitted, the pool is unbounded.
If the pool becomes full, further spawns fail and return `nil`.

A `spawn` receives a task prototype or a [block](#blocks), and starts it as a
task instance.
It supports the following formats:

- `spawn T(...)` receives a task prototype and a list of arguments to pass to
  the body about to start.
  The operation returns a reference to the spawned task instance.
- `spawn @ts T(...)` receives an additional pool to hold the task.
- `spawn { ... }` starts a block as a **transparent task** with no associated
  reference.
- `pin t = spawn { ... }` promotes the block to a normal task, attaching it to
  the [local](#local-variables) `t`.

A `task` expression as an identifier evaluates to the currently running
(non-transparent) task instance.

Examples:

<!-- exs/val-06-tasks.atm -->

```
val T = task (n) {          ;; task to await n seconds
    print(task ?? :xtask)   ;; --> true
    await(n * 1s)
    print "timeout"
}
print(T ?? :task)           ;; --> true
pin t1 = spawn T(1)         ;; fires an instance
print(t1 ?? :xtask)         ;; --> true

pin ts = tasks(2)           ;; `ts` holds at most 2 task instances
print(ts ?? :tasks)         ;; --> true

val t2 = spawn @ts T(2)     ;; `t2` lives in `ts`
print(t2 ?? :xtask)         ;; --> true

spawn @ts T(3)              ;; #ts==2
spawn @ts T(4)              ;; spawn fails

spawn {                     ;; fires a transparent task
    await(15s)
    print "timeout"
}
```

A task or pool is always lexically attached to an enclosing block, which on
termination, automatically aborts its owned tasks and pools, in the reverse
order in which they were attached.
A [pin](#pin) assignment, which is mandatory for pools, explicitly
attaches the reference to the declaration block.
By default, an unassigned `spawn` attaches the new task to the enclosing block.

A task that is not attached to any block, such as one that a function spawns
and its caller ignores, lives as long as the task that spawned it.
It is aborted when the outermost block of that task terminates, after all of
its [defer](#defer) blocks.

A transparent task has no own identity and is owned by its enclosing
non-transparent task.
In addition, it delegates [pub](#pub) and [emit](#emit) operations to its
owner.
Note that other compound constructs of Atmos, including
[par and watching](#parallel), also rely on transparent tasks internally.


Examples:

<!-- exs/val-07-abort.atm -->

```
task T (i) {
    defer {
        print("aborted " ++ i)
    }
    await(false)
}
do {
    pin ts = tasks()
    spawn T(1)
    spawn @ts T(2)
    spawn {
        defer {
            print("aborted 3")
        }
        await(false)
    }
}                       ;; --> aborted 3,1,2
```

See [Ambiguities](#ambiguities):
    `task` ⏎ `(x)` reads as two statements `task ; (x)` (not prototype `task(x)`).

### Pub

A task has a public field `pub`, which can be accessed both internally
and externally:

```
Pub : `pub`
    | Expr `.´ `pub´
```

Internally, it can be accessed through the special variable `pub`.
Externally, it can be accessed through `t.pub`, where `t` is a reference to the
task.

Examples:

<!-- exs/val-08-pub.atm -->

```
task T (n) {
    set pub = n
    spawn {
        set pub = n*2   ;; refers to owner's `pub`
    }
    print(pub)          ;; --> 20
}
pin t = spawn T(10)
print(t.pub)            ;; --> 20
```

A task also has the field `ret`, which can only be read, and holds the value
that the task terminated with.

## Native

The `native` reference type represents a resource of C, such as an open file.
A native value holds a pointer and a finalizer, which releases the resource
exactly once: when the value is explicitly closed, when it is no longer
referenced, or when its [pin](#pin) block terminates.

Native values are created by libraries written in C.
In the [standard libraries](#standard-libraries), files are native values.

Examples:

```
do {
    pin f = io.open("/tmp/x.txt", "w")
    print(type(f))          ;; --> 'native'
    io.write(f, "hello\n")
}                           ;; the file is closed here
```

# EXPRESSIONS

Atmos is an expression-based language in which statements are expressions
that evaluate to a final value.
Therefore, we use the terms statement and expression interchangeably.

The exceptions are the statements that have no value:
[declarations](#local-variables) (`val`, `var`, `pin`, and named `func` and
`task`), [assignments](#set) (`set`), and [`defer`](#defer).
They are only valid as an item of a [block](#blocks) or as the body of a
[conditional](#conditionals) case.
It is a compile error to use them where a value is expected, such as in an
operand or in an argument: `print(val x = 1)`.

The statements that leave a scope, such as `return`, `break`, and `throw`,
are valid in any position, even though they never produce a value:
`val v = f() || throw :X`.

All [identifiers](#identifiers), [literals](#literals) and
[values](#types--values) are also valid expressions.

## Chunks

Like in Lua, a sequence of expressions in Atmos is called a
[chunk][lua-chunks], which is their unit of compilation.
A program in Atmos is a chunk, and a block is also a chunk but enclosed by
braces (`{` and `}`):

```
Chunk : { Expr [`;´] }
Prog  : Chunk
Block : `{´ Chunk `}´
```

Each expression in a sequence may be separated by an optional semicolon (`;`).
A sequence of expressions evaluate to its last expression.

A program collects all command-line arguments into the variadic symbol
`...`.

<!-- exs/exp-01-program.atm -->

```
print(1) ; print(2)     ;; --> 1 \n 2
print(...)              ;; --> a, b, c
print(3)                ;; --> 3
```

The main chunk of a program executes as a [task](#tasks).
Hence, it may [await](#await) and it is the root of all tasks in the program.

[lua-chunks]: https://www.lua.org/manual/5.4/manual.html#3.3.2

### Include

A program may be split in multiple files, which are joined with `include`:

```
Include : `include´ STR
```

The statements of the given file take the place of the `include`, as if they
were written there.
Therefore, the declarations of the file are visible in the rest of the
program.

An `include` is only valid in the main chunk of a file, outside any block.
The file name is relative to the file with the `include`.
A file is only included once: other occurrences have no effect.

All files are compiled together, as a single program.
There is no separate compilation, and no loading of files during execution.

Examples:

```
;; geometry.atm
func area (w, h) {
    w * h
}
```

```
;; main.atm
include "geometry.atm"
print(area(2, 3))       ;; --> 6
```

### Blocks

A block delimits a lexical scope for
[local variable declarations](#local-variables).

When a block aborts or terminates, all [defer statements](#defer) execute, and
all [pin declarations](#pin) abort.

Blocks appear in compound statements, such as [if](#if), [loop](#loop), and
many others.

A block can also be created through an explicitly `do`:

```
Do : `do´ [TAG] Block
   | `do´ `(´ Expr `)´
```

The optional [tag](#literals) identifies the block such that it can match
[escape](#escape) statements.

The `do` keyword may also be used as a call to execute a simple expression as a
statement.

Examples:

<!-- exs/exp-02-blocks.atm -->

```
val v = do {            ;; block prints `:ok` and evals to `1`
    print(:ok)
    1                   ;; `v` receives 1
}

do {
    val a = 1           ;; `a` is only visible in the block
    <...>
}
a                       ;; ERROR: `a` is not declared

do {
    pin t = spawn T()   ;; attaches task T to enclosing block
    <...>
}                       ;; aborts t

do(10)                  ;; innocuous `10`
```

#### Escape

An `escape` immediately aborts the deepest enclosing block matching the given
tag:

```
Escape : `escape´ Expr+
```

The first argument to escape is the [tag](#literals) or
[tagged table](#user-types) to check.
The whole block being escaped evaluates to the other arguments.
If there is only a single argument, then the block evaluates to it.

The block tags are checked with the [equivalence operator](#equivalence) `??`,
which also allows to compare them with tagged tables.

The program raises an error if no enclosing blocks match the escape expression.

Examples:

<!-- exs/exp-03-escape.atm -->

```
val v = do :X {
    escape :X [x=10]
    print('never executes')
}
print(v.x)  ;; --> 10
```

```
val a,b =
    do :X {
        do :Y {
            escape :X, 'a', 'b'
        }
    }
print(a, b) ;; --> a, b
```

```
do :X {
    do :Y {
        escape :Z   ;; error: no matching :Z block
    }
}
```

An `escape` matches blocks dynamically:
it may cross function calls and reach a block in a caller.

### Defer

A `defer` block executes when its enclosing block terminates or aborts:

```
Defer : `defer´ Block
```

Deferred blocks execute in reverse order in which they appear in the source
code.

Examples:

<!-- exs/exp-04-defer.atm -->

```
do {
    print(1)
    defer {
        print(2)    ;; last to execute
    }
    defer {
        print(3)
    }
    print(4)
}                   ;; --> 1, 4, 3, 2
```

### Test

A `test` block behaves like a normal block, but is only included in the program
when [executing](#execution) it with the flag `-t`:

```
Test : `test´ Block
```

Examples:

<!-- exs/exp-05-test.atm -->

```
func add (x,y) {
    x + y
}
test {
    assert(add(10,20) == 30)
    assert(add(-10,10) == 0)
    print("All tests passed...")
}
```

## Declarations and Assignments

Atmos mimics the semantics of Lua [local][lua-locals] variables.

Unlike Lua, all variables are resolved when the program compiles:
a name that is not declared is a compile error, and not a global variable.
The variables declared in the main chunk, outside any block, are visible in
the whole program.

[lua-locals]: https://www.lua.org/manual/5.4/manual.html#3.3.7

### Local Variables

Locals in Atmos must be declared before use, and are only visible inside the
[block](#blocks) in which they are declared:

```
Local : (`val´ | `var` | `pin`) ID* [`=´ Expr+]
```

A declaration first specifies one of `val`, `var` or `pin` variable modifier.
A `val` is immutable, while a `var` is mutable.
A [`pin`](#pin) is immutable and attaches its value to the enclosing block,
which reclaims the value when it terminates or aborts.

A declaration may specify a list of identifiers, which supports multiple
declarations with the same modifier.

The optional initialization is a list of expressions, which assigns the
initial values to the variable(s).
Like in Lua, if the last expression of the list evaluates to multiple values,
then all of them are considered.

Note that the `val` immutable modifier rejects re-assignments to its name, but
does not prevent assignments to fields of
[reference types](#types--values).

Examples:

<!-- exs/exp-05-locals.atm -->

```
do {
    val a, b, c = 1, 2, 3
    print(a, b, c)      ;; 1, 2, 3
}

do {
    var x = 10
    set x = 20
    print(x)            ;; --> 20
}
print(x)                ;; ERROR: `x` is not declared

do {
    pin t = spawn T()
}                       ;; `t` is aborted

do {
    val y = 10
    set y = 20          ;; ERROR: `y` is immutable
}
```

#### Pin

A `pin` declaration attaches its value to the enclosing block.
It applies to all [reference types](#types--values): tasks, pools, tables,
functions, and native values.
Values of other types, such as numbers and strings, are not affected.

When the block terminates or aborts, its pinned values are reclaimed:

- First, tasks and pools are aborted, in the reverse order in which they were
  declared, and interleaved with the [defer](#defer) blocks.
- Then, tables and functions release what they hold, even if they are part of a
  cycle, and native values release their resources.

A task of the block may be the one that makes the block terminate, e.g., by
emitting the event that the enclosing task awaits.
Such a task is in the middle of an operation, and can only be aborted when the
operation returns.
In this case, the block waits: its remaining `defer` blocks and pins only
proceed after that task unwinds.
Therefore, finalization always goes from the inside out: a `defer` of a task
executes before the `defer` blocks that its enclosing block declares before
it.

The value to pin must be new, with no other references to it.
The exception is a task or a pool, since tasks may refer to themselves.
Tasks and pools must always be pinned:
it is an error to assign a running task to a `val` or `var` if it is not
pinned somewhere else.
An unassigned `spawn` is implicitly pinned in the enclosing block.

A pinned value cannot escape its block, which would leave a dangling
reference after the block reclaims it.
The following operations raise the error `invalid escape : pinned value`:

- to assign the value to a variable of an outer block, or to a variable that
  is captured by a function;
- to store the value in a table that is not pinned in the same block or in a
  nested one, or in the `pub` of a task attached outside the block;
- to pass the value in a `throw`, or to `emit` it to tasks that live outside
  the block;
- to spawn a task with the value in a pool that lives outside the block.

A function that captures a pinned value is also restricted to the block of
that value, and so is a task that receives it as an argument.

Finally, a block may give away what it pins.
If a pinned value is the final value of its block, or goes out through a
`return`, `break`, or `escape`, then it leaves the block as a new unpinned
value, along with the pinned values it holds.
The destination may pin it again.
A task or pool that leaves its block this way must be pinned again.

Examples:

```
do {
    pin t = [1, 2, 3]       ;; `t` is attached to the block
    pin u = [t]             ;; OK: `u` is in the same block
    val x = t               ;; OK: an alias in the same block
}                           ;; `t` and `u` are reclaimed

var out
do {
    pin t = [1, 2, 3]
    set out = t             ;; ERROR: invalid escape : pinned value
}

func new () {
    pin t = [1, 2, 3]
    t                       ;; `t` leaves the block
}
val v = new()               ;; `v` is a regular table
```

#### Where

An expression can be suffixed with a `where` clause to define contextual
locals:

```
Expr : Expr `where´ `{´ Decl* `}´
Decl : ID* `=´ Expr
```

A `where` initializes a list of immutable locals, which are only visible within
the prefix expression and the clause itself.

Examples:

```
val x = (2 * z) where {
    y = 10
    z = y+1
}
print(x)    ;; --> 22
```

### Prototypes

Atmos supports declarations for [functions](#function) and [tasks](#tasks):

```
Proto : [`val´] (`func´|`task`) ID `(´ ID* [`...´] `)´ Block
      | (`func´|`task`) ID {`.´ ID} [`::´ ID] `(´ ID* [`...´] `)´ Block
```

The optional `val` modifier makes the declaration local to its block.
Without it, the declaration is a variable of the main chunk, visible in the
whole program, even if it appears inside a block.

The first format supports functions and tasks:

- `func f (<pars>) { <body> }`:
    equivalent to `set f = func (<pars>) { <body> }`
- `val func f (<pars>) { <body> }`:
    equivalent to `val f = func (<pars>) { <body> }`

In the `val` form, `f` is visible in its own body, such that the function may
call itself.
The same holds for a task, which may spawn or await itself.
Inside the body, `f` is the function itself, not a captured copy.

Outside functions and tasks, the first form also declares `f` as a variable of
the program, if it does not exist, such that functions may refer to each other
regardless of their order.
Inside a function or task, `f` must already exist, since a new variable of the
program would be shared by all calls of the enclosing function.
Otherwise, the declaration is an error, and the `val` form is the one to use:

```
func f (x) {
    val func g () { x }     ;; ok: local to each call of `f`
    func h () { x }         ;; error, unless `h` exists
    g()
}
```

```
val func fact (n) {
    if n <= 1 => 1 => n * fact(n-1)     ;; calls itself
}
```

The second format supports two other variations based on
[Lua functions][lua-function]:

- `task t.x.y.f (<pars>) { <body> }`:
    equivalent to `set t.x.y.f = task (<pars>) { <body> }`
- `func o::f (<pars>) { <body> }` (only for `func`):
    equivalent to `set o.f = func (self, <pars>) { <body> }`

Examples:

<!-- exs/exp-06-function.atm -->

```
func add (x, y) {
    x + y
}
print(add(1,2))     ;; --> 3

val task T (x) {
    await :X
    x
}
```

```
val o = [ v=1 ]
func o::inc () {
    set self.v = self.v + 1
}
o::inc()
print(o.v)          ;; --> 2
```

#### Return

A `return` immediately terminates the enclosing function, aborting all active
blocks:

```
Return : `return´ Expr*
```

The list of return expressions becomes the final result of the corresponding
call.
The list follows the rules for [statement arguments](#statement-arguments).

Examples:

<!-- exs/exp-07-return.atm -->

```
func f () {
    print(1)    ;; --> 1
    return 2
    print(3)    ;; never executes
}
print(f())      ;; --> 2
```

```
func g () {
    return 1, 2
}
func all () {
    return g()      ;; all values of g
}
func one () {
    return (g())    ;; only the first value of g
}
print(all())        ;; --> 1, 2
print(one())        ;; --> 1
```

### Set

The `set` statement assigns, to the list of locations in the left of `=`, the
expression in the right of `=`:

```
Set : `set´ Expr* `=´ Expr+
    | `set´ Expr OP`=´ Expr
```

The only valid locations are
    [mutable `var` variables](#local-variables) and
    [indexes](#indexing).

The second format is a compound assignment, which updates a single location
with a binary [operation](#operations): `set x OP= e` is equivalent to
`set x = x OP (e)`, but evaluates the location only once.
The valid operators are `+` `-` `*` `/` `//` `%` `++` `&` `|` `^` `<<` `>>`
`&&` `||`.

Examples:

<!-- exs/exp-08-set.atm -->

```
var x
set x = 20              ;; OK

val y = [10]
set y@0 = 20            ;; OK
set y = 0               ;; ERROR: cannot reassign `y`

set x += 1              ;; x is 21
set y@0 *= 2            ;; y@0 is 40

var a, b = 1, 2
set a, b = b, a         ;; swaps a and b
```

## Operations

Atmos provides the [operators](#operators) as follows:

- equivalence: `==` `!=` `??` `!?`
- relational: `>` `<` `>=` `<=`
- arithmetic: `+` `-` `*` `/` `//` `%`
- logical: `!` `||` `&&`
- bitwise: `&` `|` `^` `~` `<<` `>>`
- length: `#`
- concatenation: `++`

Unary operators (`-`, `!`, `~` and `#`) use prefix notation, while binary
operators (all others, including binary `-`) use infix notation:

```
Expr : OP Expr          ;; unary operation
     | Expr OP Expr     ;; binary operation
```

<!--
For binary operations, the first operand and operator must be at the same line.
x                   ;; ERR: `x`,`+` not at same line
 + y
-->

Atmos mimics the semantics of standard [Lua operators][lua-operators] for
    (`==` `!=`),
    (`>` `<` `>=` `<=`),
    (`+` `-` `*`),
    (`!` `||` `&&`), and
    (`#`).
Note that some operators have a [different syntax](#lua-vs-atmos-subtleties) in
Lua.

The other arithmetic operators and the bitwise operators follow C:

- `/` is the real division, and always results in a real (`7 / 2` is `3.5`).
- `//` is the integer division, and truncates towards zero (`-7 // 2` is
  `-3`).
- `%` is the remainder of `//`, and has the sign of the dividend (`-7 % 3` is
  `-1`).
- `&` `|` `^` `~` `<<` `>>` operate on integers.
  The operator `^` is the exclusive or, and `>>` preserves the sign.

A division of integers by zero raises an error.
The exponentiation is the function [`math.pow`](#math).

Next, we decribe the operations that Atmos modifies or introduces:
    (`??` `!?`), (`#`), and (`++`).
The operations for membership and structural comparison are functions of the
[library `X`](#x).

Examples:

<!-- exs/exp-08-operations.atm -->

```
-(1 + 10)           ;; --> -11
!(true && false)    ;; --> true
#([1,2,3])          ;; --> 3
7 // 2              ;; --> 3
6 & 3               ;; --> 2
```

[lua-operators]: https://www.lua.org/manual/5.4/manual.html#3.4

### Equivalence

The operators `??` and `!?` ("is" and "is not") check the equivalence between
their operands.
If any of the following conditions are met, then `a ?? b` is true:

- `a == b` (e.g., `10 ?? 10`)
- `type(a) == b` (e.g., `[] ?? :table`)
- `b` is "dot prefix" of `a` (e.g., `'x.y.z' ?? 'x.y'`)
- `b` is "dot prefix" of `a.tag` (e.g., `:X.Y [] ?? :X`)
- `a` and `b` are tables, and every field `k=v` of `b` has a field in `a` such
  that `a@(k) ?? v` (e.g., `[x=1,y=2] ?? [x=1]`)

In the last rule, the table `b` works as a pattern: `a` may have other fields.
The field `tag` follows the rule of tags, so that a pattern with a tag also
matches its subtypes.

The operator `!?` is the negation of `??`.

The operator `??` is the single matching mechanism in Atmos:
[`match`](#match), [`await`](#await), [`catch`](#exceptions), and
[`escape`](#escape) all rely on it.

Examples:

<!-- exs/exp-09-equivalence.atm -->

```
\{} ?? :function    ;; --> true
nil ?? nil          ;; --> true
:X [] ?? :X.Y       ;; --> false
[] ?? []            ;; --> true
:P [x=1,y=2] ?? :P [x=1]    ;; --> true
[x=1] ?? [x=1,y=2]  ;; --> false
```

### Length

The operator `#` ("length") evaluates the number of elements in the given
collection.

Atmos preserves the semantics of the [Lua length operator][lua-length], and
adds support for the [tasks type](#tasks).

[lua-length]: https://www.lua.org/manual/5.4/manual.html#3.4.7

Examples:

<!-- exs/exp-11-length.atm -->

```
#([1,2,3])      ;; --> 3

pin ts = tasks()
spawn @ts T(...)
spawn @ts T(...)
print(#ts)      ;; --> 2
```

### Concatenation

The operator `++` ("concat") concatenates its operands into a new value.

For strings and numbers, `a ++ b` behaves the same as
[`a .. b`][lua-concat] in Lua.

Otherwise, `a ++ b` creates a table with key-values resulting from
[iterations](#loop) over `a` and `b`, favoring `b` in case of duplicate keys.
If the iterations return numeric indexes starting from `0` with no holes, they
are treated as [vector](#table) values and are put in sequence in the resulting
table.

[lua-concat]: https://www.lua.org/manual/5.4/manual.html#3.4.6

Examples:

<!-- exs/exp-11-concatenation.atm -->

```
'abc' ++ 'def'            ;; abcdef
[1,2] ++ [3,4]            ;; [1,2,3,4]
[x=10] ++ [x=1,y=2]       ;; [x=1, y=2]
```

The result is a new table, which is not pinned.
Therefore, it is an error to concatenate tables that hold
[pinned values](#pin), such as the tasks of a pool.

## Indexing

Atmos uses the at (`@`) or dot (`.`) notations to index [tables](#table):

```
Expr : Expr At                  ;; t@(e)  t@0  t@i  t@:x
     | Expr `@´ (`#´ | `+´)     ;; t@#  t@+
     | Expr `.´ ID              ;; t.x

At   : `@´ `(´ Expr `)´         ;; t@(e)
     | `@´ (NUM | ID | TAG)     ;; t@0  t@i  t@:x
```

The at notation uses parenthesis (instead of brackets) to provide the index
expression.
It is possible to omit the parenthesis to index literal numbers,
identifiers, and tags.
The dot notation is a syntactic sugar to index string keys: `t.x` expands to
`t@("x")`.
Finally, the tip markers `#` and `+` refer to the last (`#t-1`) and append
(`#t`) indexes, respectively, since the first index of a vector is `0`.

The same `At` rules apply to [table constructors](#table),
[spawn pools](#tasks), and [emit targets](#emit).

Atmos mimics the semantics of [Lua indexing][lua-indexing] for tables.
Indexing a task evaluates to `nil`, except for the fields
[`pub` and `ret`](#pub).

Examples:

<!-- exs/exp-12-indexing.atm -->

```
val t = [ x=1 ]
print(t@('x'))      ;; --> 1
print(t@:x)         ;; --> 1
print(t.x)          ;; --> 1
print(t.y)          ;; --> nil
```

```
val v = [ 1 ]
set v@0 = 10        ;; [ 10 ]
set v@+ = 20        ;; [ 10, 20 ]
print(v@#)          ;; --> 20
print(v@0)          ;; --> 10
print(v@+)          ;; --> nil
```

<!-- exs/exp-13-ppp.atm -->

[lua-indexing]: https://www.lua.org/manual/5.4/manual.html#3.2

## Calls

Atmos supports many formats to call functions:

```
Expr : Expr `(´ Expr* `)´
     | Expr Expr ;; single constructor argument
                 ;; (STR | TAG | `[´ | `\` | CLK | NAT)
```

A call expects an expression of type [func](#function) and an optional list of
expressions as arguments enclosed by parenthesis.

Like in [Lua calls][lua-call], if there is a single
[constructor](#types--values) argument, then the parenthesis are optional.
This is valid for strings, tags, tables, lambdas, clocks, and native literals.

The call formats are also valid for the statements `await`, `do`, and
`tasks`.

#### Statement Arguments

The statements `abort`, `break`, `emit`, `escape`, `return`, `throw`, `until`,
and `while` take their arguments with no parenthesis:

- The arguments are a list of expressions separated by commas, which extends
  as far as possible: `return x + 1, y` returns two values.
- The arguments must start in the same line of the statement.
  Otherwise, the statement has no arguments, which is only meaningful for
  `return`, `break`, and `throw`.
- Since a list enclosed by parenthesis is an expression, the parenthesis are
  optional: `return (1, 2)` is the same as `return 1, 2`.
- A single expression enclosed by parenthesis has a single value:
  `return (f())` returns the first value of `f()`, while `return f()` returns
  all of them.

The statements `abort` and `emit` expect a single argument.

Examples:

<!-- exs/exp-14-calls.atm -->

```
print(1,2,3)    ;; --> 1 2 3
print "Hello"   ;; --> Hello
print :ok       ;; --> ok
type []         ;; :table
```

See [Ambiguities](#ambiguities):
    `f` ⏎ `(x)` reads as two statements `f ; (x)` (not `f(x)`); and
    `f :X []` reads as `f(:X [])` (not `f(:X) []`).

[lua-call]: https://www.lua.org/manual/5.4/manual.html#3.4.10

### Pipes

Atmos supports yet another format for function calls:

```
Expr : Expr (`<--´ | `<-´ | `->´ | `-->´ ) Expr
```

The pipe operators `<-` and `<--` pass the argument in the right to the
function in the left.
The pipe operators `->` and `-->` pass the argument in the left  to the
function in the right.

Single pipe operators `<-` and `->` have higher
[precedence](#precedence-and-associativity) than double pipe operators
`<--` and `-->`.

If the receiving function is already a call, then the pipe operator inserts
the extra argument into the call either as first (`->` and `-->`) or last (`<-`
and `<--`).


Examples:

<!-- exs/exp-15-pipes.atm -->

```
f <-- 10 -> g   ;; equivalent to `f(g(10))`
t -> f(10)      ;; equivalent to `f(t,10)`
```

See [Ambiguities](#ambiguities):
    `x<-y` reads as the pipe `y(x)` (not `x < (-y)`).

### Parenthesis

Expressions can be enclosed by parenthesis:

```
Expr : `(´ Expr+ `)´
```

In Atmos, parenthesis have three uses:

- group an operation to increase its precedence
- group comma-separated expressions to create a multi-valued expression
- revert a multi-valued expression into a single value

Examples:

<!-- exs/exp-16-parenthesis.atm -->

```
f ('1' ++ '2')      ;; instead of (f '1') ++ '2'
(1,2,3)             ;; multi-valued (1,2,3)
((1,2,3))           ;; single value 1
```

### Precedence and Associativity

Operations in Atmos can be combined in expressions with the following
precedence priority (from higher to lower):

1. primary:
    - literal:      `nil` `true` `...` `:X` `'x'` `5s` (etc)
    - identifier:   `x`
    - constructor:  `[]` `\{}`
    - command:      `do` `set` `if` `await` (etc)
    - declaration:  `func` `val` (etc)
    - parenthesis:  `()`
2. suffix:
    - call:         `f()` `o::m()` `f ""` `f []` `f \{}` `f @clk`
    - index:        `t@i` `t@(e)`
    - field:        `t.x`
    - tag:          `:X()` `:X []`
3. inner pipe:
    - single pipe:  `v->f` `f<-v`
4. prefix:
    - unary:        `-x` `!v` `#t` `~n`
5. infix:
    - binary        `x*y` `r++s` `a || b`
6. outer pipe:
    - double pipe:  `v-->f` `f<--v`
7. outer where:
    - where:        `v where {...}`

Prefix operations are right associative, all others are left associative.

The binary operators follow the precedences of C, from higher to lower:

1. `*` `/` `//` `%`
2. `+` `-`
3. `++`
4. `<<` `>>`
5. `&`
6. `^`
7. `|`
8. `<` `<=` `>` `>=`
9. `==` `!=` `??` `!?`
10. `&&`
11. `||`

The only difference to C is that the bitwise operators `&` `^` `|` have
higher precedence than comparisons, such that `x & 1 == 0` reads as
`(x & 1) == 0`.

Examples:

<!-- exs/exp-17-precedence.atm -->

```
#f(10).x        ;; # ((f(10)) .x)
x + 10 - 1      ;; (x + 10) - 1
x + y * 2       ;; x + (y * 2)
a ++ b == c     ;; (a ++ b) == c
- x + y         ;; (-x) + y
x || y && z     ;; x || (y && z)
f :X []         ;; f(:X [])
```

## Conditionals

In a conditional context, [nil](#types--values) and [false](#types--values)
are interpreted as "falsy", and all other values from all other types as
"truthy".

### If

An `if` tests a condition expression and executes one of the two possible
branches:

```
If  : `if´ Expr (Block | `=>´ Lambda) [`else´ Block]
    | `if´ Expr `=>´ Expr [`=>´ Expr]
```

If the condition is truthy, the `if` executes the first branch.
Otherwise, it executes the `else` branch, which defaults to `nil` if absent.

The branches can be either [blocks](#blocks) or simple expressions prefixed by
the arrow symbol `=>`.
The `else` branch is optional.
The truthy block can use the [lambda notation](#lambda) to capture the value
of the condition satisfied.

Examples:

<!-- exs/exp-18-if.atm -->

```
val max = if x>y => x => y      ;; max value between `x` and `y`

if f() => \{                    ;; f() is assigned to it
    print("f() evaluates to " ++ it)
} else {
    print("f() evaluates to 'false'")
}
```

### Ifs

An `ifs` tests multiple conditions in sequence, until one is satisfied,
executing its associated branch:

```
Ifs : `ifs´ `{´ {Case} [Else] `}´
Case :  Expr  `=>´ (Expr | Block | Lambda)
Else : `else´ `=>´ (Expr | Block)
```

If no condition is met, the `ifs` executes the optional `else` branch, which
defaults to `nil`.

Like in an [if](#if), branches can be blocks, simple `=>` expressions or
lambdas.

Examples:

<!-- exs/exp-19-ifs.atm -->

```
val max = ifs {     ;; exclusive max value between `x` and `y`
    x > y => x
    x < y => y
    else  => throw :error, "values are equal"
}
```

### Match

A `match` tests a head expression against a series of values, until one is
satisfied, executing its associated branch:

```
Match : `match´ Expr `{´ {Case} [Else] `}´
Case :  (Lambda | Expr)  `=>´ (Expr | Block | Lambda)
Else : `else´ `=>´ (Expr | Block | Lambda)
```

If no case succeeds, the `match` executes the optional `else` branch, which
defaults to `nil`.

The tests use `h ?? i`, where `h` is the head expression and `i` corresponds to
each case expression.
In addition, a case can be a [lambda constructor](#lambda), which receives the
head expression and returns the test result.

Like in an [if](#if), branches can be blocks, simple `=>` expressions or
lambdas.

Examples:

<!-- exs/exp-20-match.atm -->

```
match f() {
    :table   => print("f() is a table")
    "hello"  => print("f() == hello")
    \{it>10} => print("f() > 10")
    else     => \{ print("f() is", it) }
}
```

## Loop

Atmos supports loops and iterators as follows:

```
Loop : `loop´ Block                 ;; infinite
     | `loop´ ID Block              ;; numeric infinite
     | `loop´ ID* `in´ Expr Block   ;; data iterator
     | `loop´ ID? `on´ Patt Block   ;; event loop
```

A `loop` executes a block of code continuously until a termination condition is
met.
Atmos supports four loop variations:

1. An *infinite loop* with an empty header, which only terminates from
   [break](#breaks) conditions.
2. A *numeric infinite loop* with a variable that ranges from `0` upwards.
3. An *iterator loop* (`in`) with multiple variables that range according to
   an iterator expression.
4. An *event loop* (`on`), which awaits an [event pattern](#await) at each
   iteration, with an optional variable to capture the event.

The following iterator expression types with predefined behaviors are
supported:

- `:table`:
    ranges over vector indexes and values, and than over key-values of a table
- `:tasks`:
    ranges over the indexes and tasks of a task pool, in the order they were
    spawned; the body may abort tasks of the pool, which are then not
    visited, and may spawn tasks into it, which are visited
- `:function`:
    ranges over calls to the given function, until it returns `nil`
- `(:number,:number)`:
    ranges from the first number up to the second number inclusive
- `:number`:
    equivalent to `(0,x-1)` where `x` is the given number, which is the
    number of iterations
- `:nil`:
    ranges from `0` upwards, with no limit

A loop evaluates to `nil` as a whole, unless a [break](#breaks) condition
occurs.

Examples:

<!-- exs/exp-21-loop.atm -->

```
loop {
    await 1s
    print("1 more second elapsed...")
}
```

```
loop i {
    print(i)        ;; --> 0, 1, ...
}
```

```
val x = loop i in (1,3) {
    print(i)        ;; --> 1, 2, 3
}
print(x)            ;; --> nil
```

```
loop i in 3 {
    print(i)        ;; --> 0, 1, 2
}
```

```
loop i,v in [10,20,30] {
    print(i,v)      ;; --> (0,10), (1,20), (2,30)
}
```

```
val f = func (s,e) {
    var nxt = s
    \{
        val cur = nxt
        set nxt = nxt + 1
        if cur>=e => nil => cur
    }
}
loop v in f(5,8) {
    print(v)        ;; --> 5, 6, 7
}
```

<!-- exs/exp-29-every.atm -->

```
loop on 1s {
    print("1 more second has elapsed")
}
```

```
loop e on :X {      ;; <-- (`emit :X [v=10]`)
    print(e.v)      ;; --> 10
}
```

### Breaks

An `break` immediately escapes the deepest enclosing loop:

```
Break : `break´ Expr*
      | `until´ Expr+
      | `while´ Expr+
```

<!--
Skip  : `skip´ `(´ `)´
The block may also contain a `skip()` statement to jump back to the next loop
step.
var i = 0
loop {                  ;; infinite loop
    set i = i + 1
    if (i % 2) == 0 {
        skip()          ;; jump back
    }
    print(i)          ;; --> 1,3,5,...
}
-->

The loop as a whole evaluates to the values passed to `break`.

A break is lexical:
it must appear inside the loop it escapes, in the same function or task body.
It may cross nested blocks, such as the branches of a [par](#parallel), but
not function calls.

In addition to `break`, Atmos provides `until` and `while` clauses to escape
loops on specific conditions:

1. A `break ...` escapes the loop with the values `...`.
2. An `until <cnd>, ...` is equivalent to `if <cnd> { break (... || <cnd>) }`.
3. A `while <cnd>, ...` is equivalent to `if not <cnd> { break ... }`.

The values follow the rules for [statement arguments](#statement-arguments).

Examples:

<!-- exs/exp-22-breaks.atm -->

```
var i = 0
loop {
    set i = i + 1
    print(i)        ;; --> 1,2,...,10
    if i == 10 {
        break
    }
}
```

```
val x = loop {
    <...>
    until f()       ;; escapes when f()
    <...>
}
print(x)            ;; value of f()
```

## Exceptions

An exception `throw` aborts all active enclosing blocks in the stack up to a
matching `catch` clause:

```
Throw : `throw´ Expr*
Catch : `catch´ Expr Block
```

A `catch` executes its body as a normal block but tries to match an occurring
exception with the given expression.
If no exceptions occur, it evaluates to `true` plus the values of its body.
If it catches an exception, it evaluates to `false` plus the values of the
matching `throw`.
Otherwise, it propagates the exception upwards in the execution stack and never
returns.

A `catch` accepts the following exception matching expressions:

- `true`: catches any throw
- `false`: ignores any throw
- `:function`: passes the throw values to the function, which matches if
    returns `true`, replacing the throw values with additional arguments
- otherwise: catches if the first throw value matches with `??`

A `catch` only deals with exceptions:
a [`return`](#return), [`break`](#breaks), or [`escape`](#escape) crosses it
with no interference.

A [pinned value](#pin) cannot be thrown, since its block terminates before
the `catch` receives it.

Errors raised by the language, such as an invalid operation, are also
exceptions, whose value is a string with the error message.

If a `throw` is not caught, the whole program aborts with a stack trace with
the source lines of the calls up to the `throw`.

Examples:

<!-- exs/exp-23-exceptions.atm -->

```
val ok, v = catch :X {
    42
}
print(ok, v)        ;; --> true, 42
```

```
val ok, x = catch :X {
    throw :X [v=10, msg="error"]
    print("unreachable")
}
print(ok, x.msg)    ;; --> false, error
```

```
func f () {
    throw :X.Y
}
val x =
    catch :X {
        catch :X.Z {
            defer {
                print "ok"
            }
            f()
        }
    }               ;; --> ok (from defer)
print(x)            ;; --> false
```

```
val ok, v = catch \{(true,42)} {    ;; catches any error, transforms into 42
    throw :X
}
print(ok, v)                        ;; --> false, 42
```

```
throw :X

;;; error stack trace
==> ERROR:
 |  atmos:9 (call)
 v  test.atm:1 (throw) <- atmos:9 (task)
==> X
;;;
```

## Task Operations

The [task](#tasks) and [pool of tasks](#tasks) primitives support a number of
operations.

### Abort

An `abort` receives a [task](#tasks) or a [tasks](#tasks) value, and immediately
aborts it:

```
Abort : `abort´ Expr
```

All nested tasks are also aborted.
All nested [deferred](#defer) statements execute.

Examples:

<!-- exs/exp-24-abort.atm -->

```
task T () {
    defer {
        print("aborted")
    }
    await(false)
}
do {
    pin t = spawn T()
    abort t             ;; --> aborted
}
```

### Await

An `await` suspends a [task](#tasks) until a matching [emit](#emit) occurs:

```
Await : `await´ Patt
Patt  : Bare | `<´ Full `>´                 ;; bare or full patterns
Bare  : [`:any´ | `:all´] Expr [ Pred ]
      | Pred
Full  : Full Pred                           ;; (5) lower precedence
      | Full `||´ Full                      ;; (4)
      | Full `&&´ Full                      ;; (3)
      | `!´ Full                            ;; (2)
      | (`:any´ | `:all´) Expr              ;; (1)
      | Pred                                ;; (1)
      | `<´ Full `>´                        ;; (1)
      | Expr                                ;; (1) higher precedence
Pred  : (`until´ | `while´) Expr
```

The bare form accepts one of the following atomic patterns, with an optional
predicate:

- a value or event to match (`:X`, `:X [v]`, `(x)`, `true`)
- a clock duration (`1h`)
- a task to spawn (`T()`)
- a pool (`:any ts`, `:all ts`)
- a base-less predicate alone (`until c`)

A predicate is an expression over the event, which is bound to `it`, or a
function that receives the event.
With `until`, the await only accepts an event if the predicate is truthy.
With `while`, only if it is falsy.
The predicate extends as far as possible, like the
[arguments of a statement](#statement-arguments).

The full combinators (`!`, `&&`, `||`, and the `until`/`while` suffix) require
the `<...>` form, with angle brackets (instead of parenthesis) as delimiters.
Like in [expressions](#precedence-and-associativity), the combinators follow
the precedences of C: `!` binds tighter than `&&`, which binds tighter than
`||`.

Inside the `<...>` form, some operators in a predicate are ambiguous, and
the predicate must be enclosed by parenthesis:

- `>` and `>>` could also close the pattern:
  `await <:X until (it.n > 3)>`.
- `&&` and `||` could also be combinators of the pattern:
  `await <:X until (a || b)>`, or `await <<:X until a> || :Y>`.

The compiler rejects the ambiguous forms with the error
`pattern error : use parentheses to disambiguate`.
The `until`/`while` suffix applies to the whole combination to its left.

An `await` is only valid in the body of a task, which includes the main chunk
and [transparent tasks](#tasks).
It is a compile error to `await` in the body of a [function](#function),
even if the function is called from a task.

When awaking, an `await` evaluates to its matching event value.

A task awakes when an `emit(e)` matches the given await pattern as follows:

| Group     | Pattern       | matches       | returns   |
|-----------|---------------|---------------|-----------|
| Boolean   | `true`        | any event     | `e`       |
|           | `false`       | never         | —         |
| Value     | `:T [...]`    | `e ?? pat`    | `e`       |
|           | `x`           | `e ?? x`      | `e`       |
| Time      | `AsBms`       | timeout       | `late`    |
|           | `:clock`      | clock tick    | `dt,late` |
| Tasks     | `t`           | `t` end       | `v,t`     |
|           | `T(...)`      | task end      | `v,t`     |
|           | `:any ts`     | any pool end  | `v,t,ts`  |
|           | `:all ts`     | all pool end  | `v,t,ts`  |
| Condition | `[p] until c` | [p] until `c` | `e / res` |
|           | `[p] while c` | [p] while `c` | `e`       |
| Logical   | `!p`          | not `p`       | `e`       |
|           | `p1 && p2`    | all subs      | `e`       |
|           | `p1 \|\| p2`  | any sub       | `e`       |

Note that some patterns may modify the final result:

- Time: `late`: time elapsed past expiry; `dt`: clock tick length
- Tasks: task result, terminating task, and task pool
- Condition: function result (defaults to `e` if `true`)

A bare call (e.g., `await T()`, or as an operand `await<T() || :X>`)
[spawns](#tasks) the given task and awaits its termination, evaluating to the
task final value.
To await the result of a call instead of spawning it, use the value form:
`await(f())` at top level, or `(f())` as a leaf inside `<>`.

A pattern is evaluated eagerly, once, when the await starts, including the
arguments of a spawned call.
However, the `until`/`while` predicate is lazy, re-evaluating its condition on
each event.

Examples:

<!-- exs/exp-26-await.atm -->

```
await(false)                    ;; never awakes
await :Key [:escape]            ;; awakes on a :Key :escape event
await 1h10min30s                ;; awakes after the given time
await until it && (it@0 > 10)   ;; awakes if event index 0 > 10
await <:X && :Y>                ;; awakes after both :X and :Y in any order
await <:X || :Y && :Z>          ;; awakes on :X, or after both :Y and :Z
await <!:X>                     ;; awakes on any non-:X event
await <:X until it.n==3>        ;; awaits :X until its field n equals 3
await :X until it.n > 3         ;; no delimiters: `>` is the operator
```

```
spawn {
    val e = await(true)
    print(e)                ;; --> OK
}
emit :OK
```

```
task T (v) {
    v * 2
}
val v = await T(10)
print(v)                ;; --> 20
```

```
pin ts = tasks()
spawn @ts T()
spawn @ts T()
val e = await :any ts   ;; awaits any task to terminate
await :all ts           ;; awaits all tasks (pool drains)
```

See [Ambiguities](#ambiguities):
    a bare `await :X || :Y` is invalid (combinators need `await<:X || :Y>`);
    `await :X` ⏎ `until c` reads as `await :X ; until c` (not
    `await<:X until c>`);
    and `await <:X until a > b>` and `await <:X until a || :Y>` are errors
    (use parenthesis to disambiguate).

### Emit

An `emit` broadcasts an event that can awake [awaiting](#await) tasks:

```
Emit : `emit´ [At] Expr
```

It takes the event to broadcast as its single argument, which is matched
against [await patterns](#await) in suspended tasks.

The optional target between brackets determines the scope of the broadcast:

- `:task` (default): current task
- `:parent`: parent task, or the pool of the task, if it lives in one
- `:global`: all tasks
- `t: task`: the given task
- `n: number`: `n`th level up in the task hierarchy (`0` = current task)

An event that is a [pinned value](#pin) can only reach tasks that live inside
the block of the value.
Otherwise, a task could keep the event after the block reclaims it.
Therefore, it is an error to emit a pinned value to `:global`, to `:parent`,
or to a task attached outside its block.

Examples:

<!-- exs/exp-27-emit.atm -->

```
task T () {
    val x = spawn X()
    val e = <...>
    emit @:global (e)   ;; global broadcast
    emit @:task (e)     ;; restricted to `T`
    emit @x (e)         ;; restricted to `x`
}
```

### Toggle

A `toggle` configures a task or block to either consider or disregard further
[emit](#emit) operations:

```
Toggle : `toggle´ Expr `(´ Expr `)´ [`with´ Patt {`,´ Patt}]
       | `toggle´ `on´ TAG [`with´ Patt {`,´ Patt}] Block
```

By default, tasks and blocks are toggled on, thus reacting to all events.

In the first format for tasks, a toggle expects the task and a
[boolean](#types--values), which is handled as follows:

- `true`: the task considers further broadcasts
- `false`: the task disregards further emits and never awakes

In the second format for blocks, a toggle spawns and awaits a block as a
[transparent task](#tasks).
It also specifies a [tag](#literals) to toggle the block when matching an
[emit](#emit).
The emit must be in the format `emit(<tag> [<boolean>])`, which sets the
toggle state.

An optional `with` filter clause specifies [await patterns](#await), which
keep the task/block responsive when matching any.

Examples:

<!-- exs/exp-28-toggle.atm -->

```
task T () {
    await :X
    print :ok
}
pin t = spawn T()
toggle t(false)
emit :X         ;; event ignored
toggle t(true)
emit :X         ;; --> ok
```

```
spawn {
    toggle on :T {
        loop {
            val e = await(:E)
            print(e@0)          ;; --> 1 3
        }
    }
}
emit :E [1]
emit :T [false]
emit :E [2]
emit :T [true]
emit :E [3]
```

```
spawn {
    toggle on :Ok with :Draw {
        par {
            loop on :Tick {
                print :tick     ;; ignored during :Ok=false
            }
        } with {
            loop on :Draw {
                print :draw     ;; responsive during :Ok=false
            }
        }
    }
}
emit(:Ok [false])
emit(:Tick)         ;; (nop)
emit(:Draw)         ;; --> draw
```

### Parallel

The parallel statement spawns multiple [transparent tasks](#tasks) until they
rejoin according to its optional suffix tag:

```
Par : `par´ [`:all´ | `:any´] Block { `with´ Block }
```

A `par` never rejoins, even if all tasks terminate.

A `par :all` rejoins only after all tasks terminate.
It evaluates to the first return value of each terminating task.

A `par :any` rejoins as soon as any task terminates, aborting the others.
It evaluates to the return values of the terminating task.

Examples:

<!-- exs/exp-31-parallels.atm -->

```
par {
    loop on 1s {
        print("1 second has elapsed")
    }
} with {
    loop on 1min {
        print("1 minute has elapsed")
    }
} with {
    loop on 1h {
        print("1 hour has elapsed")
    }
}
print("never reached")
```

```
val v = par :any {
    await 1s
} with {
    await :X
    print(":X occurred before 1 second")
    :ok
}
print(v)        ;; --> ok
```

```
val x,y = par :all {
    await :X
} with {
    await :Y
}
print(x, y)     ;; --> X, Y
```

#### Watching

A `watching` spawns and awaits a block as a [transparent task](#tasks) until an
[await pattern](#await) is satisfied, which aborts the block:

```
Watching : `watching´ Patt Block
```

A `watching P { B }` is equivalent to a `par :any` as follows:

```
par :any {
    await P
} with {
    B
}
```

Examples:

<!-- exs/exp-30-watching.atm -->

```
watching 1s {
    loop on :X {
        print("one more :X occurred before 1 second")
    }
}
```

# STANDARD LIBRARIES

Atmos provides a small set of libraries, which are modeled after the
[Lua libraries][lua-libraries] with the same names.
The main difference is that all positions, in strings and in vectors, start at
`0`.

A library is a table with functions, which is always available:
there is no `require`.

[lua-libraries]: https://www.lua.org/manual/5.4/manual.html#6

## Basic Functions

- `print(...)`: prints its arguments, separated by tabs
- `type(v)`: the type of `v` as a string: `nil`, `boolean`, `number`,
  `string`, `table`, `function`, `task`, `xtask`, `tasks`, or `native`
- `tostring(v)`: converts `v` to a string
- `tonumber(v)`: converts `v` to a number, or evaluates to `nil` if `v` is
  neither a number nor a string with a number
- `assert(v [, msg])`: raises an error with `msg` if `v` is falsy, and
  evaluates to its arguments otherwise
- `next(t [, k])`: the key and value that follow the key `k` in the table
  `t`, or the first ones if `k` is absent; evaluates to `nil` after the last

## Math

- `math.floor(x)`, `math.ceil(x)`: rounds `x` down or up to an integer
- `math.abs(x)`: the absolute value of `x`
- `math.min(...)`, `math.max(...)`: the smallest or largest argument
- `math.sqrt(x)`, `math.exp(x)`, `math.log(x)`: as in C
- `math.sin(x)`, `math.cos(x)`, `math.tan(x)`, `math.asin(x)`, `math.acos(x)`:
  as in C, in radians
- `math.atan(y, x)`, `math.atan2(y, x)`: the angle of the point `(x, y)`, in
  radians; in `math.atan`, `x` is optional and defaults to `1`
- `math.pow(x, y)`: `x` to the power of `y`, as a real
- `math.random()`: a random real in `[0,1)`
- `math.random(n)`: a random integer from `0` to `n-1`
- `math.random(a, b)`: a random integer from `a` to `b`
- `math.randomseed(n)`: sets the seed of the random numbers
- `math.huge`: the infinity
- `math.pi`: the value of π

## String

A position in a string starts at `0`.
A negative position counts from the end: `-1` is the last byte.

- `string.len(s)`: the number of bytes in `s` (same as `#s`)
- `string.sub(s [, i [, j]])`: the substring from positions `i` to `j`, both
  included, which default to the first and last
- `string.find(s, x [, i])`: the first and last positions of the first
  occurrence of the plain string `x` in `s`, from position `i` on; or `nil`
- `string.split(s [, sep])`: a vector with the pieces of `s` between the
  occurrences of `sep`, or between blanks if `sep` is absent
- `string.byte(s [, i [, j]])`: the codes of the bytes from `i` to `j`, which
  default to the first one only
- `string.char(...)`: the string with the given byte codes
- `string.upper(s)`, `string.lower(s)`: converts the letters in `s`
- `string.rep(s, n [, sep])`: `n` copies of `s`, separated by `sep`
- `string.format(fmt, ...)`: formats its arguments as `printf` in C, with the
  conversions `%d` `%i` `%x` `%X` `%o` `%c` (integers), `%f` `%e` `%g`
  (numbers), `%s` (any value, as `tostring`), and `%%`

Examples:

```
string.sub("hello", 1, 3)           ;; ell
string.find("hello", "l")           ;; 2, 2
string.split("a,b,,c", ",")         ;; ["a", "b", "", "c"]
string.format("%5.2f|%s", 3.14159, :ok)     ;; " 3.14|ok"
```

## Vector

A vector is a [table](#table) with the keys `0` to `#t-1`.

- `vector.insert(t, v)`: appends `v` to `t`
- `vector.insert(t, i, v)`: inserts `v` at position `i`, shifting up the
  elements from `i` on
- `vector.remove(t [, i])`: removes and evaluates to the element at position
  `i`, which defaults to the last, shifting down the elements after it
- `vector.sort(t [, f])`: sorts `t` in place; `f(a,b)` tells if `a` comes
  before `b`, and defaults to `a < b`; the sort is stable
- `vector.join(t [, sep])`: a string with the strings and numbers in `t`,
  separated by `sep`

Examples:

```
val t = [3, 1, 2]
vector.sort(t)                      ;; [1, 2, 3]
vector.insert(t, 0, 0)              ;; [0, 1, 2, 3]
vector.remove(t)                    ;; 3
vector.join(t, "-")                 ;; 0-1-2
```

## Input and Output

A file is a [native](#native) value, which closes itself when it is no longer
referenced, or when its [pin](#pin) block terminates.

- `io.open(path [, mode])`: opens a file, with the modes of C (`"r"` if
  absent); evaluates to the file, or to `nil` plus an error message
- `io.close(f)`: closes the file now
- `io.write(...)`: writes its arguments to the standard output
- `io.write(f, ...)`: writes its arguments to the file `f`
- `io.read([f])`: the next line of the standard input, or of the file `f`,
  with no line break; or `nil` at the end
- `io.read([f,] :all)`: all that remains to read, as a single string
- `io.lines(f)`: an iterator over the lines of a file, given as a file or as a
  path, for [loops](#loop)

Examples:

```
loop l in io.lines("data.txt") {
    print(l)
}
```

## Operating System

- `os.time()`: the current time, in seconds since the epoch
- `os.clock()`: the CPU time used by the program, in seconds
- `os.getenv(name)`: the value of an environment variable, or `nil`
- `os.remove(path)`: deletes a file; evaluates to `true`, or to `nil` plus an
  error message
- `os.exit([code])`: terminates the program at once, with no
  [defers](#defer)

## X

The library `X` deals with values structurally, inspecting the contents of
tables recursively.

- `X.tostring(v)`: deep string representation of `v`
- `X.print(...)`: `print` using `X.tostring` on each argument
- `X.copy(v)`: deep copy of `v`
- `X.load(s)`: reads a value written as data
- `X.dump(v)`: writes a value as data
- `X.xin(v, t)`, `X.nin(v, t)`: membership ("in" and "not in")
- `X.gte(a, b)`, `X.lte(a, b)`: deep ordering ("sup" and "sub")
- `X.gt(a, b)`, `X.lt(a, b)`: strict deep ordering
- `X.eq(a, b)`, `X.neq(a, b)`: deep equality

### Load and Dump

The function `X.load(s)` reads the value written in the string `s`, and
`X.dump(v)` writes the value `v` as a string that `X.load` reads.
They are meant for data, such as configuration files and saved states, in the
way of JSON.

The value is written with the constructors of the language only:

```
Value : `nil´ | `true´ | `false´
      | [`-´] NUM | CLK | STR | TAG
      | [TAG] `[´ { Entry `,´ } `]´
Entry : ID `=´ Value
      | `@´ Key `=´ Value
      | Value
Key   : `(´ Value `)´ | NUM | STR | TAG
```

A clock duration, such as `1h30min`, is a number of microseconds, as in the
language.

There are no variables, operations, functions, or calls: a text with any of
them is an error.
[Comments](#comments) are accepted.

The result is a regular value, with new tables that can be modified.
An invalid text raises an error with the line of the problem.

A dump is the reverse: `X.load(X.dump(v))` is [structurally equal](#deep-equality)
to `v`.
Only data can be written: `nil`, booleans, numbers, strings, and tables of
them, with no cycles.
Any other value, such as a function or a task, raises an error.
A string is written as a string, even if it was created as a tag, except for
the field `tag` of a table.

To read and write files, combine them with the functions of
[`io`](#input-and-output).

Examples:

```
;; config.atm
:Config [
    title = "Rocks",        ;; a field
    size  = [640, 480],     ;; a vector
    keys  = [ l='A', r='D' ],
    state = :game.paused,
]
```

```
val f = io.open("config.atm")
val c = X.load(io.read(f, :all))
print(c ?? :Config, c.title, c.size@0)      ;; --> true, Rocks, 640
print(X.load("[1, [2, 3]]")@1@0)            ;; --> 2
```

```
val s = X.dump(:Pos [x=10, y=20, "a"])
print(s)                                    ;; --> :Pos ["a", x=10, y=20]
io.write(io.open("pos.atm", "w"), s)        ;; saves it
```

### Membership

The function `X.xin(a, b)` compares `a` against each key-value `k`-`v`
resulting from an [iteration](#loop) over `b`.
If `a` is equal to `v` or is equal to a non-numeric `k`, then the result is
true.

The function `X.nin` is the negation of `X.xin`.

Examples:

```
X.xin(10, [10,20,30])       ;; true
X.xin( 1, [10,20,30])       ;; false
X.xin(:x, [x=10,y=20])      ;; true
X.nin( 1, [10,20,30])       ;; true
```

### Deep Ordering

The function `X.gte(a, b)` ("sup") checks if `a` is structurally more general
than `b`.
It is especially useful to check containment between two tables, i.e., if
one table holds all fields and values of the other.

To check the result of `X.gte(a, b)`, the following tests are made in order:

1. if `type(a) != type(b)`, then the result is `false` (e.g.,
   `X.gte(10, nil)`)
2. if `a == b`, then the result is `true` (e.g., `X.gte(10, 10)`)
3. if `a` and `b` are strings, then the result is `true` if `a` is a "dot
   prefix" of `b` (e.g., `X.gte('x', 'x.y')`)
4. if `a` and `b` are tables, then the result is `true` if every pair `ka=va`
   in `a` satisfies `X.gte(va, b@(ka))`
5. otherwise the result is `false` (e.g., `X.gte(10, 20)`)

The function `X.lte` is the reverse of `X.gte`, i.e., `X.lte(a, b)` is
equivalent to `X.gte(b, a)`.

The functions `X.gt` and `X.lt` are the strict versions:
`X.gt(a, b)` is true if `X.gte(a, b)` is true and `X.gte(b, a)` is false.

Note that the order is partial:
two values may be unrelated, such that `X.gte`, `X.lte`, `X.gt`, and `X.lt`
are all false (e.g., for `10` and `20`).
Therefore, `X.lt(a, b)` is not the negation of `X.gte(a, b)`.

Examples:

```
X.gte(10, 10)           ;; --> true
X.gte(10, 20)           ;; --> false (scalars compare by equality)
X.gte(:x, :x.y)         ;; --> true  (:x supertypes :x.y)
X.gte([], [1,2,3])      ;; --> true  (looser table supertypes richer)
X.lte([1,2,3], [])      ;; --> true  (reverse of the above)
X.gt([], [1,2,3])       ;; --> true  (and they are not equal)
X.gt([1], [1])          ;; --> false (they are equal)
```

### Deep Equality

The functions `X.eq(a, b)` and `X.neq(a, b)` check if their operands have or
not structural equality.
Therefore, tables are not only compared by reference, but also by their stored
values.

The result of `X.eq(a, b)` is true if both `X.gte(a, b)` and `X.gte(b, a)`
are true.
The function `X.neq` is the negation of `X.eq`.

Examples:

```
X.eq([1,2,3], [1,2,3])          ;; --> true
X.neq(\{}, [])                  ;; --> true (different types)
X.neq([v=[]], [v=[]])           ;; --> false
X.eq([@([])=1], [@([])=1])      ;; --> false (keys are not `==`)
X.eq(\{}, \{})                  ;; --> false (func refs are not `==`)
```

## Atmos

- `atmos.status(t)`: the state of the task `t`: `running`, `suspended`, or
  `dead`
- `atmos.next()`: the time until the nearest [timer](#await) expires, in
  microseconds, or `nil` if there are no timers

# SYNTAX

We use a BNF-like notation to describe the syntax of expressions in Atmos.
As an extension, we use `X*` to mean `{ X ',' }`, but with the leading `,`
being optional; and a `X+` variation with at least one `X`.

```
Prog  : { Expr [`;´] }
Block : `{´ Prog `}´
Expr  : `do´[TAG]  Block                            ;; explicit block
      | `do´ `(´ Expr `)´                           ;; expression as statement
      | `escape´ Expr+                              ;; escape from block
      | `defer´ Block                               ;; defer statements
      | `test´ Block                                ;; test block
      | `include´ STR                               ;; include file

      | (`val´ | `var` | `pin`) ID* [`=´ Expr+]     ;; local declarations
      | Expr `where´ `{´ (ID* `=´ Expr)* `}´        ;; where clause
      | (`func´|`task´) ID {`.´ ID} [`::´ ID]       ;; declaration (`::´ func only)
               `(´ ID* [`...´] `)´
               Block
      | `return´ Expr*                              ;; return from function

      | `set´ Expr* `=´ Expr+                       ;; assignment
      | `set´ Expr OP`=´ Expr                       ;; compound assignment

      | `nil´ | `false´ | `true´                    ;; literals
      | TAG | NUM | STR | CLK | NAT
      | ID | `pub´                                  ;;  identifiers

      | [TAG] `[´ Key_Val* `]´                      ;; table
            Key_Val : At `=´ Expr
                    | ID `=´ Expr
                    | Expr

      | (`func´|`task´) `(´ ID* [`...´] `)´ Block   ;; anon function/task
      | `\` [ID | `(` ID* `)´] Block                ;; lambda notation

      | `task´                                      ;; running task
      | `tasks´ `(´ [Expr] `)´                      ;; tasks pool
      | `abort´ Expr                                ;; abortion

      | OP Expr                                     ;; pre ops
      | Expr OP Expr                                ;; bin ops
      | `(´ Expr+ `)´                               ;; parenthesis

      | Expr `.´ ID                                 ;; table field
      | Expr At                                     ;; index t@(e) t@0 t@i
      | Expr `@´ (`#´ | `+´)                        ;; tip index t@# t@+

      | Expr `(´ Expr* `)´                          ;; call
      | Expr Expr                                   ;; single-constructor call
      | Expr (`<--` | `<-` | `->` | `-->` ) Expr    ;; pipe calls

      | `if´ Expr (Block | `=>´ Lambda)             ;; if block
            [`else´ Block]
      | `if´ Expr `=>´ Expr [`=>´ Expr]             ;; if expr

      | `ifs´ `{´ {Case} [Else] `}´                 ;; ifs
            Case :  Expr  `=>´ (Expr | Block | Lambda)
            Else : `else´ `=>´ (Expr | Block)

      | `match´ Expr `{´ {Case} [Else] `}´          ;; match
            Case :  (Lambda | Expr)  `=>´ (Expr | Block | Lambda)
            Else : `else´ `=>´ (Expr | Block | Lambda)

      | `loop´ Block                                ;; infinite loop
      | `loop´ ID Block                             ;; numeric infinite loop
      | `loop´ ID* `in´ Expr Block                  ;; iterator loop
      | `loop´ ID? `on´ Patt Block                  ;; event loop

      | `break´ Expr*                               ;; loop break
      | `until´ Expr+
      | `while´ Expr+

      | `throw´ Expr*                               ;; throw exception
      | `catch´ Expr Block                          ;; catch exception

      | `spawn` [At] Expr `(´ Expr* `)`             ;; spawn task
      | `spawn´ Block                               ;; spawn block

      | `await´ Patt                                ;; await pattern
      | `emit´ [At] Expr                            ;; emit event

      | `toggle´ Expr `(´ Expr `)´ [`with´ Patt {`,´ Patt}] ;; toggle task
      | `toggle´ `on´ TAG [`with´ Patt {`,´ Patt}] Block    ;; toggle block

      | `par´ [ `:all´ | `:any´ ]                   ;; parallel
            Block { `with´ Block }
      | `watching´ Patt Block                       ;; watching pattern block


At    : `@´ (`(´ Expr `)´ | NUM | ID)   ;; @-qualifier (index/key/pool/target)

Patt  : Bare | `<´ Full `>´             ;; bare or full pattern
Bare  : [`:any´|`:all´] Expr [Pred]     ;; bare: one primary + predicate
      | Pred                            ;; base-less predicate
Full  : Full Pred                       ;; <> combinators, by precedence
      | Full `||´ Full
      | Full `&&´ Full
      | `!´ Full
      | (`:any´|`:all´) Expr
      | Pred
      | `<´ Full `>´
      | Expr
Pred  : (`until´|`while´) Expr          ;; until/while predicate

ID    : [A-Za-z_][A-Za-z0-9_]*      ;; variable identifier
TAG   : :[A-Za-z0-9_\.]+            ;; tag
NUM   : [0-9][0-9A-Za-z\.]*         ;; number
STR   : '.*' | ".*"                 ;; string
CLK   : ([0-9\.]+(us|ms|s|min|h|day))+  ;; duration
NAT   : `.*`                        ;; native code
```

## Ambiguities

Every program has a single parse, but a few constructs resolve in a way that
may surprise a naive reading:

| #                 | case                  | what it is              | what it is **not**       |
|-------------------|-----------------------|-------------------------|--------------------------|
| [await](#await)   | `await 20min + 1s`    | `(await 20min) + 1s`    | `await(20min + 1s)`      |
| [await](#await)   | `await P` ⏎ `until c` | `await P ; until c`     | `await<P until c>`       |
| [await](#await)   | `<P until a > b>`     | error: use parenthesis  | `<P until (a > b)>`      |
| [await](#await)   | `<P until a \|\| Q>`  | error: use parenthesis  | `<<P until a> \|\| Q>`   |
| [table](#table)   | `:X` ⏎ `[]`           | `:X ; []`               | `:X []`                  |
| [calls](#calls)   | `f` ⏎ `(x)`           | `f ; (x)`               | `f(x)`                   |
| [calls](#calls)   | `f :X []`             | `f(:X [])`              | `f(:X) []`               |
| [pipes](#pipes)   | `x<-y`                | `y(x)`                  | `x < (-y)`               |
| [tasks](#tasks)   | `task` ⏎ `(x)`        | `task ; (x)`            | `task(x)`                |
| [lambda](#lambda) | `\-`                  | `\(a,b){ a - b }`       | `\(a){ -a }`             |
| [return](#statement-arguments) | `return` ⏎ `f()` | `return ; f()`         | `return f()`             |
| [return](#statement-arguments) | `return (a+1)*2` | `return ((a+1)*2)`     | `(return (a+1)) * 2`     |
| [return](#statement-arguments) | `[return a, b]`  | `[return (a, b)]`      | `[(return a), b]`        |
| [locals](#local-variables) | `val x = 1, y`      | `val x = (1, y)`        | `(val x = 1), y`         |
