# Plan: Atmos with a C backend (new implementation, new repo)

## Context

A from-scratch reimplementation of Atmos that compiles to C.
No Lua: no VM, no interpreter, no Lua syntax, no Lua libraries.
The current implementation (atmos-lang/atmos + lua-atmos) is the
reference for semantics, and its test suite becomes the spec (see Tests).

Origin: `atmos-lang/atmos@5977b07`, `.claude/plans/261109-c-backend.md`.
This copy lives in the new repo and carries the revisions agreed after
that commit (see Revisions).

Goal: the simplest dynamic language that still supports Atmos'
structured, deterministic concurrency.

## Settled decisions

- **C backend only.**
    - Whole-program compilation to one C file, then `cc`.
    - No VM, no bytecode, no interpreter.
- **No `load`.**
    - No runtime evaluation of source.
    - Modules are resolved statically at compile time.
- **No metatables, no coroutines.**
    - Tasks are the only suspension mechanism.
- **Stackless tasks with shallow `await`.**
    - `await` is legal only lexically inside a task body: task prototypes,
      the main chunk, and transparent blocks inside them.
    - `await` inside a `func` body is a compile error.
    - Reusable waiting logic is a task awaited with `await T(...)`.
- **Reference counting plus numeric pin scopes, no GC.**
    - `pin` is extended to all dynamic values (see Memory).
    - A pinned value records the depth of its pin block and is freed,
      guaranteed, when that block ends.
    - A pinned value never escapes its pin block: every escape is
      checked at runtime and raises an error at the escape point.
    - No reference to a freed value is ever observable: no dead values,
      no liveness check on access.
    - Unpinned values are refcounted and may go anywhere.
    - Closures inherit the scope: a closure that captures pinned values
      is pinned at the deepest of their depths.
    - Leaks are accepted for cycles made only of unpinned values.
- **Inline frames for structured constructs.**
    - `par`, `watching`, `loop on`, `toggle on` keep their locals inline
      in the owner's frame.
    - A transparent block is a trail: a small record inside the frame
      of its task, with no allocation (see Tasks).
    - No more lowering to lambdas or Lua closures.
- **Non-local exits as status codes.**
    - Errors, `escape`, `break`, `return` all propagate as a returned
      status, never `setjmp`/`longjmp`.
- **Tables only, Lua-like.**
    - One mutable table type with array part and hash part.
    - No separate tuple type.
    - Hash part iterates in insertion order (determinism).
- **Native code is C.**
    - Native blocks emit C.
    - Native resources are boxed values with a finalizer.
    - `$x` inside a native block refers to the Atmos variable `x`.
- **Compiler written in C.**
    - One self-contained binary, no runtime dependency.
    - Self-hosting in Atmos may come later.
- **Numbers are integers or reals, as in Lua 5.3.**
    - One type `number` with two subtypes: 64-bit integers and
      doubles. `1` and `0xFF` are integers; `1.5` and `1e3` are reals.
    - Integer with integer gives an integer, wrapping on overflow; any
      mix gives a real.
    - `/` is always real. `//` is the integer division, and it
      truncates, to pair with the `%` of C: `(a//b)*b + a%b == a`.
    - Integer division or remainder by zero is an error.
    - `1 == 1.0`, and both are the same table key.
    - `print`/`tostring` show `5` for an integer and `5.0` for a real.
    - Where only an integer fits (bitwise, string positions), a real
      with an integral value is accepted; any other is an error.
- **Operators follow C.**
    - Precedence, from the strongest: prefix `-` `!` `#` `~`; then
      `*` `/` `//` `%`; `+` `-`; `++`; `<<` `>>`; `&`; `^`; `|`;
      `<` `<=` `>` `>=`; `==` `!=` `??` `!?`; `&&`; `||`.
        - Bitwise operators bind stronger than comparisons, unlike C.
        - Binary operators associate to the left.
    - `%` is the remainder of C, with the sign of the dividend
      (`fmod` for reals).
    - Bitwise on integers: `&` `|` `^` (xor) `~` (not) `<<` `>>`.
        - `>>` is arithmetic. A shift of 64 or more gives `0` (or
          `-1`), and a negative one shifts the other way.
    - `&&` and `||` still yield the deciding operand.
    - Compound assignment: `set x += 1`, with `+=` `-=` `*=` `/=`
      `//=` `%=` `++=` `&=` `|=` `^=` `<<=` `>>=` `&&=` `||=`.
        - A single destination, evaluated once.
    - Removed: `**` (use `math.pow`), `?>` `!>` (`X.xin`, `X.nin`),
      `=>=` `=<=` (`X.gte`, `X.lte`), `===` `=!=` (`X.eq`, `X.neq`).
    - New: the strict `X.gt` and `X.lt`.
    - `??` is the single matching operator, always as
      `value ?? pattern`. `a ?? b` holds if:
        - `a == b`; or
        - `b` names the type of `a`; or
        - `b` is a dot prefix of `a`, or of the tag of `a`; or
        - both are tables, and every field of `b` matches the same
          field of `a`, recursively with `??`.
    - The combinators of an await pattern follow the same precedence:
      `!`, then `&&`, then `||`, then the `until`/`while` suffix.
    - `->` and `<-` stay, as do `-->` and `<--`.
- **Multiple values and varargs are kept, exactly as in Lua 5.4.**
    - `catch`, `await`, `escape`, `par :all`, and `val a,b = ...` rely
      on them.
    - Adjustment: the last expression of a list expands, the others
      truncate to one value, and parentheses truncate.
    - Loose arity: a missing argument is `nil`, an extra one is dropped.
    - Only the anonymous `...`; no named lists.
    - Native functions written in C may be variadic.
    - Implemented with a runtime value stack (see Calls).
    - No tuple type (see Revisions).
- **Static globals.**
    - No dynamic global table.
    - Top-level declarations become static C variables.
    - An unknown identifier is a compile error.
    - `func f` without `val` declares a top-level variable.
- **All strings are interned in v1.**
    - Equality and table key hashing become pointer operations.
    - Revisit if concatenation-heavy code suffers.
- **Indexes are 0-based.**
    - `[10,20,30]@0` is `10`, and positional entries of a constructor
      start at `0`.
    - `t@#` is `t@(#t-1)` and `t@+` is `t@(#t)`.
    - `loop i in n` ranges over `0..n-1`, and `loop i { }` starts at `0`.
    - `loop i in (a,b)` still includes both ends.
    - String positions (`sub`, `find`, `byte`) also start at `0`.
- **No string patterns in v1.**
    - Plain `find`, `sub`, `split` only.
- **No auto-pin of task arguments in v1.**
    - Unpinned arguments are already safe by refcount.
    - Auto-pin would only add a guaranteed free when the task ends and
      break cycles through the argument. Revisit later.

## Language delta (relative to current Atmos)

### Kept

- Literals: nil, booleans, numbers, strings, tags, clocks, natives.
- `val`, `var`, `pin`, `set`, `where`.
- Blocks, `do :X`, `escape`, `defer`, `test`.
- `if`, `ifs`, `match`, all four `loop` forms, `break`, `until`, `while`.
- `throw`, `catch` (with `true`, `false`, function, and `??` matching).
- Operators `??` `!?` `#` `++`, and the arithmetic, relational, and
  logical ones.
- Indexing: `t@(e)`, `t@1`, `t.x`, `t@#`, `t@+`.
- Calls with single constructor argument, pipes, parenthesis lists.
- `func`, lambdas (`\{ it }`), prototypes, `o::f()` as sugar for `o.f(o)`.
- `task`, `tasks(n)`, `spawn`, `spawn @ts`, transparent `spawn {}`.
- `await` (full pattern language), `emit @target`, `toggle`, `abort`,
  `pub`, `par`, `par :any`, `par :all`, `watching`.
- `X.tostring`, `X.print`, `X.copy`.

### Changed

- `await` is shallow (compile error inside `func`).
- `pin` applies to any dynamic value, not only tasks and pools.
- A pinned value cannot escape its pin block (runtime error).
    - Today a pinned task can be aliased outside its block and is then
      seen as `dead`; this becomes an error at the assignment.
- Returning a pinned value from its own pin block transfers ownership.
    - Today `func f () { pin t = spawn T() ; t }` returns a dead task;
      it now returns a live task that the caller must pin.
- Parameters are immutable, as the manual states.
    - Today `set` on a parameter is silently accepted.
- Native blocks contain C, not Lua.
- Globals are static (see Settled decisions).
- Deep ordering drops the metatable rule (rule 2).
- Operators follow C (see Settled decisions).
    - Today all binary operators share one level and cannot be mixed
      without parentheses; `%` follows Lua; there are no bitwise
      operators and no compound assignment.
    - `**`, `?>`, `!>`, `=>=`, `=<=`, `===`, and `=!=` become
      functions.
- `//` truncates. Today it floors, as in Lua.
- Indexes are 0-based (see Settled decisions).
    - Today vectors, numeric loops, and strings start at `1`.
- `catch` only catches `throw`.
    - Today `catch true` also catches `escape`, `break`, and `return`,
      because all of them are Lua throws.
- `break`, `until`, and `while` require a lexically enclosing `loop` in
  the same function or task body (transparent blocks may be crossed).
    - Today they cross calls dynamically.
    - `escape` stays dynamic.
- `spawn { ... }` inside a `func` body is a compile error in v1.
    - Its body may `await`, and a function frame lives on the C stack.
- The last positional entry of a table constructor expands, as in Lua.
    - Today `[f()]` keeps only the first value.

### Dropped

- `load` and anything that evaluates source at runtime.
- Metatables, metamethods, `__call`/`__pairs` iteration, `__concat`.
- The `meta` await pattern (`__atmos`).
- Lua standard libraries (replaced by a small library, see Stdlib).
- `thread`, `async` (v1), functional streams (v1).
- String methods via metatable (`s::len()`).
- Types `thread` and `userdata` (replaced by the native box).

## Runtime

### Values

```c
typedef enum {
    A_NIL, A_BOOL, A_INT, A_NUM, A_STR,
    A_TAB, A_FUNC, A_TASK, A_XTASK, A_TASKS, A_NAT
} AType;

typedef struct {
    uint8_t type;
    union {
        int     b;
        int64_t i;
        double  n;
        AObj*   o;      // STR, TAB, FUNC, TASK, TASKS, NAT
    } u;
} AVal;
```

- 16-byte tagged union in v1. NaN-boxing is a later optimization.
- Tags are interned strings (`:x == 'x'` holds as today).
- `type(v)` returns the tag `:nil`, `:table`, `:xtask`, etc.

### Memory

```c
typedef struct {
    int32_t  rc;
    uint8_t  type;
    uint8_t  pinned;
    uint32_t depth;     // depth of the pin block, if pinned
} AObj;
```

#### Ownership convention

- Arguments are borrowed.
- Return values are owned (+1).
- Storing into a variable, frame slot, table, or closure takes a
  reference; overwriting releases the old one.
- All objects keep a refcount, pinned or not. For a pinned object it
  only matters after a transfer (see Exits).

#### Block depth

- Every block instance has a depth: its parent block's depth plus one.
- The main chunk's block has depth 1. Top-level variables live there.
- A function body is one deeper than the caller's current block.
- A task body is one deeper than the block it is attached to: the
  spawning block, or the block of its pool.
- Depths are stored in frames, so a resumed task keeps its own depths,
  whoever emitted.

#### Unpinned values

- Live by refcount and may be stored anywhere: emitted, thrown, put in
  top-level variables, in other tables, in `pub`.
- Can never reference a pinned value (see Stores), so everything
  reachable from them is refcounted too.
- Cycles made only of unpinned values leak (accepted).

#### Pin

- `pin x = e` pins the value of `e` in the current block.
    - The value must be unique (`rc == 1`), else
      `invalid pin : shared value`.
    - Pinning an already pinned value is an error.
        - Except for a unique value that is only confined by what it
          holds (a closure, or data that left its block): the pin
          narrows it to the current block.
    - `pin x = [...]` pins the table before filling it, so that the
      constructor accepts pinned values. Nested constructors do not.
    - Pinning the running task or an ancestor is an error.
    - An unassigned `spawn` is an implicit pin in the enclosing block.
    - Tasks and pools must be pinned: `val`/`var` of an unpinned task
      or pool is an error, as today.
- Sets `pinned = 1` and `depth` to the block depth.
- When the block ends, each pinned value is freed unconditionally:
    - task: aborted (defers run), then freed;
    - pool: all tasks aborted, then freed;
    - table: entries released, then freed;
    - function: captures released, then freed;
    - native: finalizer called, then freed.
- The block ends in two phases, so that no code ever sees empty data:
    - first, tasks and pools are aborted and defers run, in reverse
      order of declaration;
    - then, tables and functions are released.

#### Invariant

- Every reference to a pinned value lives in its pin block or in a
  block nested inside it, on the same chain of blocks.
- Hence freeing at the end of the pin block never leaves a reference
  behind. There are no dead values and no liveness check on access.
- Along a single chain, nesting equals depth order, so one integer
  comparison suffices.
- Every lateral channel (between parallel branches or tasks) is an
  escape, checked as below.

#### Stores

Storing a pinned value `v` with depth `p`:

| Destination                                    | Rule     |
|------------------------------------------------|----------|
| local variable of a block at depth `d`         | `d >= p` |
| table pinned at depth `d`                      | `d >= p` |
| `pub` of a task pinned at depth `d`            | `d >= p` |
| `emit` payload, target root at depth `d`       | `d >= p` |
| `spawn` argument, task attached at depth `d`   | `d >= p` |
| captured `var` (heap box)                      | error    |
| unpinned table                                 | error    |
| `throw` payload                                | error    |

- A failed rule raises `invalid escape : pinned value` at the store.
- `emit` targets `:global`, `:parent`, and `n > 0` are always outer, so
  emitting a pinned value to them is an error.
- `emit @:task` is allowed only if `v` is pinned in the outermost block
  of the running task.
- A task that receives a pinned argument inherits its depth: it cannot
  be stored, returned, or pooled above `p`.
    - If nobody pins it (dropped, rejected by a `val`, refused by a
      transfer), it is aborted when the block at `p` ends.
    - A task with no pinned arguments that nobody pins lives with its
      parent task, as today. It belongs to no block, so it ends with
      the outermost block of the parent, after all its finalizers.
- Depths only compare along one chain of blocks. `emit` is the lateral
  channel, so its target must also be in the subtree of the task that
  owns the pin block, attached inside that block.
- A named `func f` or `task T` is a top-level variable, as in Lua. If
  it captures a pinned local, the declaration is an escape: use
  `val f = func ...` instead.
- A closure that captures pinned values inherits their scope (see
  Functions and closures).
- Unpinned task arguments are plain refcounted references. They are
  safe because unpinned values cannot reference pinned ones.
- `throw` is always an error because the pin block is unwound before
  the `catch` receives the value.

#### Exits

`return`, `escape`, `break`, a block's final value, and a task's result
carry a value `v` with depth `p` to a destination at depth `d`:

- **Alias (`p <= d`).** `v` stays pinned where it is.
- **Transfer (`p > d`).** The pin block of `v` is being exited.
    - `v` is unpinned and flows out as a fresh value.
    - Pinned values reachable from `v` with depth `> d` are unpinned
      too. The traversal only visits pinned objects, since unpinned
      ones cannot hold pinned ones.
    - The destination decides: `pin` again, or keep it refcounted.
      Tasks and pools must be pinned again.
    - Safe by the invariant: every other reference lived in the exited
      blocks and was released with them, so refcounts are exact.
    - If what leaves still holds values pinned at depth `<= d`, it is
      not freed of them: it stays confined to the deepest one.
    - A task or pool that leaves directly is unpinned. One that is only
      held by data that leaves stays, and is aborted with its block.
    - A task confined by its arguments (depth `> d`) cannot leave:
      `invalid escape : pinned value`.
    - The transfer is all or nothing, and happens before the block
      runs its defers.
- Today `func f () { pin t = spawn T() ; t }` returns a dead task.
  With transfer it behaves like `func f () { spawn T() }`.

#### Cycles

- Cycles through pinned values are freed when their pin block ends.
  Since unpinned values cannot reference pinned ones, such cycles are
  made only of pinned values.
- Cycles of unpinned values leak. The remedy is `pin`, and ASan finds
  them in tests.

### Strings and tags

- Immutable, refcounted, interned (weak intern table).
- Length stored; contents NUL-terminated for C interop.
- Tag hierarchy `??` is a dot-prefix test on the interned string.

### Tables

- Array part (`0..n-1`) plus insertion-ordered hash part.
- Hash: open addressing index into a dense entry array.
- Deletion leaves a tombstone, compacted on rehash.
- `#t` is a border, exactly as in Lua, shifted to base `0`:
  `t@(#t-1)` is not `nil` and `t@(#t)` is `nil`, or `#t` is `0` when
  `t@0` is `nil`.
    - With holes, any border may be returned, as in Lua.
    - The search follows Lua: the array size if its last element is not
      `nil`, else a binary search inside the array part.
- Iteration: array part, then hash entries in insertion order.
- `tag` field is an ordinary string key.
- User types (`:Pos [x=10, y=20]`) are plain tables with a `tag` key.
    - No type declarations and no fixed layouts.
- Runtime helpers: `++` on tables, `X.xin`, `X.eq`, `X.gte`,
  `X.copy`, `X.tostring`.

### Functions and closures

```c
typedef AStatus (*AFn) (AFunc* self, int argc, AVal* argv);

typedef struct {
    AObj  hdr;
    AFn   fn;
    int   ncap;
    AVal  caps[];
} AFunc;
```

- A captured `val` is copied into `caps`.
- **Scope inheritance.** A closure that captures pinned values is
  pinned at creation, at the deepest of their depths.
    - The closure is fresh, so the pin is always valid.
    - It is then subject to the Stores rules like any pinned value: it
      cannot go to an unpinned table, a captured `var`, a `throw`, or an
      outer variable.
    - On exits it follows the transfer rule: returning it from the pin
      block unpins it together with the captured pinned values.
    - Task prototypes written as literals follow the same rule.
- A closure that captures only unpinned values stays unpinned.
- A captured `var` lives in a refcounted box shared by all closures and
  the declaring frame.
- Stdlib and native functions use the same type, with `ncap == 0`.

### Calls and multiple values

- One global value stack, as in Lua.
- Caller pushes arguments, callee pushes results.
- The callee returns a status; the result count is in the context.
- On error, the stack is unwound to the frame base, releasing values.
- Nothing on the stack survives an `await`: task temporaries live in
  the task frame (see Compiler).

### Status and non-local exits

```c
typedef enum {
    A_OK, A_ERROR, A_ESCAPE, A_BREAK, A_RETURN, A_ABORT
} AStatus;
```

- The payload (thrown values, escape tag and values, ...) is kept in
  the runtime context.
- Within one C function, `break`/`escape`/`return` compile to `goto`
  through the cleanup chain.
- Across C functions (calls, transparent blocks) they propagate as a
  status, and each level runs its cleanup.
- Cleanup = run defers in reverse, release locals, free pins.
- `catch` tests the status and payload and stops propagation on match.
- Uncaught `A_ERROR` at the top prints the trace and exits.
- Traces: each frame records Atmos file and line before calls and
  awaits, producing the bidimensional trace (call stack plus task tree).

### Tasks

- **Prototype.** A `task` value is a closure whose `fn` is a resume
  function plus a frame size.
- **Instance.** An `xtask` is a heap object:

```c
typedef struct ATask {
    AObj    hdr;
    AResume resume;     // generated
    int     pc;         // resumption point
    uint8_t status;     // running, suspended, terminated
    uint8_t toggled;    // on/off
    struct ATask* up;   // owner
    ABlock* blks;       // active nested blocks with tasks
    APatt*  await;      // current await pattern
    AVal    pub;
    AVal    ret;
    // generated frame follows: params, locals, temporaries
} ATask;
```

- **Resume signature.**

```c
typedef enum { R_START, R_EVENT, R_EXIT, R_ABORT } AKind;
typedef AStatus (*AResume) (ATask* t, AKind k, AVal evt);
```

- `R_EVENT`: an emit matched the current await pattern.
- `R_EXIT`: a transparent child delivered a non-local exit; the task
  behaves as if it was raised at its current await point.
- `R_ABORT`: the task jumps to the cleanup chain of its current `pc`.
- **Transparent blocks** (`spawn {}`, `par` branches, `watching`,
  `toggle on`) share the frame of their owner task.
    - Their locals are slots of the owner's frame: no frame allocation.
    - `return`/`break`/`throw`/`escape` cross to the owner with their
      values, matching `tst/throw.lua` (pre and pos cases).
    - They delegate `pub` and `emit` to the owner.
- **Trails**, as in Céu. A trail is a line of execution of a task: its
  body, or a transparent block nested in it.
    - The trails of a prototype are known at compile time, since a
      function cannot await and a block that starts in a loop ends with
      the iteration. So is their tree: a block that starts another is
      its parent.
    - The frame of a task holds one record per trail (72 bytes), then
      the slots. A record has the resumption point, the await, and the
      state of the trail. Nothing is allocated when a block starts.
    - Each trail has its own C function. The static description of the
      prototype has, per trail, the function, the parent, the first
      child, the next sibling, and the level of its outermost block.
    - The pattern of an await with a single leaf (a tag, a task, a
      duration) lives in the record. Only composite patterns are built
      as trees.
    - State that few trails need is allocated on demand: the pending
      exit of a block that waits, the filters of a `toggle on`.
    - Only tasks and pools are nodes of the tree. A node keeps the
      trail of its parent that spawned it.
    - Order. Siblings in the static tree are in source order, which is
      also the order they start in. The children of a trail are its
      nested trails and the nodes it spawned, merged in the order they
      started: a trail keeps how many children its task had when it
      started, and the count is adjusted when dead children leave.
    - The termination of a block is still an event, as in the
      reference: `await(true)` sees it, with the result in `ret`. Since
      the block has no identity, the event is a terminated task made up
      for the occasion. The parent that waits for its blocks (`par
      :all`, `toggle on`) is awakened by that event.
- **Main chunk** is a task (it may `await`).
- **Pools.** `tasks(n)` is a list of heap tasks with an optional limit.
    - `spawn @ts` fails with `nil` when full.
    - A task leaves its pool when it terminates; `#ts` counts live
      tasks.
- **Lifetime.** A task may terminate or be aborted before its pin block
  ends. This is a valid state, not a dead reference.
    - The frame (locals, nested blocks) is freed at termination.
    - The header (`status`, `pub`, `ret`) lives until the pin block
      ends, so `await t` and `t.pub` keep working.
    - Headers of pool tasks are refcounted by their aliases. All aliases
      live inside the pool's block, so a header is freed at the latest
      when that block ends.

### Scheduler

- **Spawn.** Allocate frame (or use inline slot), link into the
  enclosing block, resume with `R_START` until the first await.
- **Await.** Build the pattern once, eagerly (including spawned task
  arguments); `until`/`while` predicates stay closures, called on each
  candidate event.
    - Patterns: boolean, value or tagged table (`??`), clock,
      `:clock`, task, `T(...)`, `:any ts`, `:all ts`, `until`, `while`,
      `!`, `&&`, `||`.
    - `&&` keeps per-operand state inside the pattern.
- **Emit.** Traverse the task tree in the documented order:
    1. the active block of the task,
    2. its nested tasks in sequence,
    3. its single nested block,
    4. the task itself at its await point,
    5. the next sibling task.
- **Emit targets.** `:task` (default), `:parent`, `:global`, a task, or
  a level number.
- **Await birth.** A task only reacts to emits that started after its
  current `await` was established (as in lua-atmos).
- **Reentrancy.** A reaction may emit, or abort tasks not yet
  traversed.
    - The traversal holds a reference to the next task header and skips
      terminated tasks.
- **Deferred abort.** A reaction may abort a block whose code is still
  on the C stack (e.g. an outer `watching` aborts the emitter).
    - The abort only marks the block.
    - Cleanup and deallocation run when control returns to that code,
      which sees `A_ABORT` and unwinds.
    - Pinned values held in its temporaries stay valid until then.
- **Late exit.** The marked task may belong to a block that is being
  left right now, by a task above it in the C stack (e.g. the task
  emits the event that its owner awaits to terminate).
    - The block must not go on: its remaining defers would run before
      those of the task, which could then find resources of the owner
      already released. (lua-atmos has this inversion.)
    - So the exit waits. The task that executes the block saves the
      pending exit (status, payload, values on the stack) and suspends
      at that finalizer. It is resumed when the marked task ends, and
      goes on with the remaining finalizers.
    - Finalization is thus always from the inside out, as when the
      task is suspended.
    - Meanwhile, the other tasks of the block are doomed: a suspended
      one no longer reacts to events, and a running one is marked too.
    - A task that waits is not resumed by anything else. An abort only
      turns its pending exit into an abort, so whoever aborts it from
      outside gets control back before it has ended.
- **Exit to a running parent.** A block exits across to its parent
  (`break`, `return`, `escape`, `throw`, the join of a `par :any`). If
  the parent is suspended, the exit is raised at its await. If it is
  running, it is below in the C stack, in the call that led to the
  exit (it emitted, or it is still starting its blocks).
    - The exit waits in the parent, which takes it when control
      returns to it, at that call.
    - What the current block of the parent started stops at once, as in
      a late exit: suspended ones no longer react, and running ones
      unwind when control returns to them. This is what aborts a
      `watching` whose body emits its own event.
    - Whatever else is running in between is not affected, and the emit
      in course goes on for the others.
    - Known limit: two running tasks of the same block unwind in the
      order of the C stack, not in the reverse order of declaration.
    - Only blocks of tasks wait. In a function the case cannot occur:
      a task that runs below a function cannot be pinned in it.
    - The tasks that nobody pinned are a finalizer of the outermost
      block of their parent: the first one, so the last to execute.
      The same wait applies, so the parent only terminates after them.
- **Pinned payloads.** `emit` of a pinned value requires the target to
  live inside the value's pin block (see Memory, Stores).
- **Termination.** A finishing task stores `ret` and signals its owner,
  so `await t`, `await T()`, `:any ts`, `:all ts` match with `v,t[,ts]`.
    - As in lua-atmos, the signal is an emit of the task itself to the
      subtree of its owner, so a neighbor `await(true)` also awakes.
- **Errors in reactions** propagate through `emit` to the emitter
  (`tst/tasks.lua` "catch 1").
- **Toggle.** `toggled = 0` skips the subtree in traversal, except
  events matching the `with` patterns.
    - `toggle on :T` reacts to `emit(:T [bool])`.
- **Abort.** Resume with `R_ABORT`, depth-first, defers in reverse.
    - As in lua-atmos, the children of an aborted task are aborted
      first, in spawn order.
- **Clocks.** Replicate the current synchronous timers semantics
  (`.claude/plans/done/261019-timers.md`):
    - `await(dur)` returns `late`;
    - `await :clock` returns `dt, late`;
    - one piece per timer expiry.

### Natives

- Native literals are C code. A leading tag gives the kind:

| Literal        | Meaning                                         |
|----------------|-------------------------------------------------|
| `` `stmts` ``      | C statements, evaluates to `nil`            |
| `` `:prelude decls` `` | C declarations at file scope (`#include`)   |
| `` `:number e` ``  | C number: integer or real, by its C type    |
| `` `:number.integer e` `` | C number, always as an integer       |
| `` `:number.float e` ``   | C number, always as a real           |
| `` `:boolean e` `` | C expression converted to a boolean         |
| `` `:string e` ``  | C string copied, `NULL` gives `nil`         |
| `` `:value e` ``   | C expression of type `AVal`, owned          |

- The tags are the full names of the types, as in Lua. The subtypes
  of a number use the dot of hierarchical tags, with the names of
  `math.type` in Lua.

- `$x` refers to an Atmos variable, as an `AVal` that may be read and
  assigned. It is captured like any other access.
- The C API in `rt/atmos.h` reads and builds values: `$x.u.i`,
  `a_dbl($x)`, `A_STR_OF($x)->s`, `a_int(i)`, `a_num(d)`, `a_strz(s)`,
  `a_tab()`, ...
- The tags follow the native literals of dceu. To confirm with the
  author.
- Native box:

```c
typedef struct {
    AObj   hdr;
    void*  ptr;
    void (*fin) (void*);
    const char* kind;   // tells C types apart, compared by address
} ANat;
```

- The C wrapper attaches `fin` at creation (e.g. `fopen` with
  `fclose`). The user never declares finalizers.
- `fin` runs on last release or when its pin block ends, exactly
  once.

### Driver and environments

```c
void    atm_init  (int argc, char** argv);
AStatus atm_main  (void);           // run main until all suspended
AStatus atm_emit  (AVal evt);       // external event, global target
AStatus atm_step  (uint32_t ms);    // advance clock
int     atm_alive (void);           // tasks still pending?
void    atm_quit  (void);
```

- The default `main()` runs the main chunk, then sleeps until the next
  timer deadline and steps, until no task is pending.
    - If the main chunk is suspended and no timer is pending, nothing
      can awake it: the program aborts the main chunk and exits.
      (lua-atmos loops forever.)
- Environments (SDL, stdin, sockets) are separate C modules that call
  `atm_emit`/`atm_step` from their own loops.

### Stdlib (v1)

- **base:** `print`, `type`, `tostring`, `tonumber`, `assert`, `next`.
- **math:** `floor`, `ceil`, `abs`, `min`, `max`, `sqrt`, `pow`, `sin`,
  `cos`, `random`, `randomseed`, `huge`, `pi`.
- **string:** `len`, `sub`, `find` (plain), `split`, `byte`, `char`,
  `upper`, `lower`, `rep`, `format` (`%d %s %f %g %x %%`).
    - Positions are 0-based, and `sub(s, i, j)` includes both ends.
    - A negative position counts from the end: `-1` is the last byte.
- **vector:** `insert`, `remove`, `sort` (with comparator), `join`.
- **io:** `write`, `read` (line), `open`/`close`/`lines` as native box.
- **os:** `time`, `clock`, `exit`, `getenv`, `remove`.
- **X:** `tostring`, `print`, `copy`, `eq`, `neq`, `gte`, `lte`, `gt`,
  `lt`, `xin`, `nin`, `load`, `dump`.
    - `load` reads data only: the constructors of the language, with
      comments. It is not the `load` of Lua: there is still no
      evaluation of source at runtime.
    - `dump` writes what `load` reads. Files go through `io`, with
      `io.read(f, :all)` for a whole file: there are no file versions.
- **atmos:** `status(t)`.

## Compiler

### Pipeline

```
.atm -> lexer -> parser -> resolver -> checker -> lowering -> coder -> .c
```

- CLI: `atmos f.atm [args]` compiles, builds, and runs.
- Flags: `-c` emit C only, `-o out` build only, `-t` enable `test`.
- `#line` directives map C diagnostics and debuggers to `.atm` lines.

### Passes

1. **Lexer.** Same lexicon as the manual; native blocks hold C text.
2. **Parser.** Recursive descent, 7 precedence levels, as the manual.
3. **Resolver.**
    - Scopes, `val`/`var`/`pin`, static globals.
    - Captures: copy `val`, box captured `var`.
    - Tags interned at compile time.
4. **Checker.**
    - `await` only inside task bodies.
    - `set` only on `var`, indexes, natives.
    - `pin` rules that are static.
5. **Lowering.**
    - `watching P {B}` to `par :any { await P } with {B}`.
    - `loop on P` to `loop { await P ... }`.
    - `until`/`while` to `if`/`break`.
    - Task bodies to A-normal form: every temporary that may be live
      across an `await` gets a frame slot (v1: all task temporaries).
6. **Coder.**
    - One C function per `func`; captures in `AFunc.caps`.
    - One resume function per task prototype and per transparent block.
    - One frame struct per task prototype, with inline frames for its
      transparent blocks.
    - Each block emits a cleanup label; each await site knows its
      cleanup chain, used for `R_ABORT` and `R_EXIT`.

### Codegen sketch

```
task T (n) {
    var i = 0
    loop on :X {
        set i = i + n
    }
}
```

```c
typedef struct {
    ATask t;
    AVal  n;
    AVal  i;
} T_Frame;

static AStatus T_resume (ATask* t, AKind k, AVal evt) {
    T_Frame* f = (T_Frame*) t;
    if (k == R_ABORT) {
        goto CLEANUP;
    }
    switch (t->pc) {
        case 0: break;
        case 1: goto AWAIT_1;
    }
    a_set(&f->i, a_num(0));
    while (1) {
        a_await(t, a_patt_val(TAG_X));
        t->pc = 1;
        return A_OK;            // suspended
    AWAIT_1:
        a_set(&f->i, a_add(f->i, f->n));
    }
CLEANUP:
    a_rel(&f->i);
    a_rel(&f->n);
    return (k == R_ABORT) ? A_ABORT : A_OK;
}
```

## Tests

- **Golden suite from the current repo.**
    - Write a one-time extractor in the new repo that reads a checkout
      of atmos-lang/atmos and dumps every `atm_test(src)` case and its
      expected output from `tst/*.lua` into `tst/cases/NNNN-name.atm`
      plus `.out`.
    - Tag each case with the features it uses, so cases using dropped
      features (Lua natives, metatables, streams, threads, deep await)
      are moved to `tst/dropped/` or rewritten.
- **Runner.** Compile each case, run, diff stdout and error output.
- **Sanitizers from day one.** Build tests with `-fsanitize=address`.
    - Leak checking on by default.
    - Cases with intended unpinned cycles opt out explicitly.
- **Unit tests** for the runtime in C: tables, strings, refcounts,
  pin and free, transfer, scheduler order.
- **Escape checks.** One case per row of the Stores table and per exit
  kind, each in an allowed and a rejected variant, including parallel
  branches and captured variables.
- **Behavior changes.** Golden cases that observe `dead` aliases or dead
  returns (pin and return cases in `tst/tasks.lua`) are rewritten to
  expect the new escape errors or transfers.
    - Cases that index vectors, iterate numerically, or use string
      positions are rewritten for base `0`.

## Repo layout

```
src/        compiler (lexer, parser, resolver, checker, lower, coder)
rt/         runtime (values, mem, str, tab, func, task, sched, std)
rt/atmos.h  public C API for natives and environments
env/        environments (default loop, SDL, ...)
tst/cases/  golden tests (.atm + .out)
tst/rt/     runtime unit tests
doc/        manual (rewritten from the current one)
exs/        examples
```

## Milestones

- [x] M0 skeleton: build system, `AVal`, `print`, compile `print(1+2)`.
- [x] M1 expressions: numbers, strings, tags, tables, all operators,
      indexing, `if`/`ifs`/`match`, non-event loops, `where`, pipes,
      `val`/`var`/`set`, static globals.
- [x] M2 functions: closures, boxes, multiple values, varargs, `return`,
      lambdas, method sugar.
- [x] M3 control and errors: blocks, `defer`, `escape`, `break`,
      `until`, `while`, `throw`, `catch`, traces with `#line`.
- [x] M4 tasks core: prototypes, `spawn`, `await` on values and tags,
      default and `:global` emit, main as task, `pin` for tasks,
      `abort`, `pub`, `status`.
- [x] M5 structured: transparent blocks, `par`, `par :any`, `par :all`,
      `watching`, `loop on`, exit crossing, `await T()`, `await t`.
- [x] M6 pools and targets: `tasks(n)`, `spawn @ts`, `:any ts`,
      `:all ts`, emit targets, `toggle` (both forms, `with`).
- [x] M7 time: clock awaits, `:clock`, timers, default driver loop.
- [x] M8 memory: `pin` for all values, block depths, store checks,
      transfer on exits, deferred abort of blocks on the C stack.
- [x] M9 natives and stdlib: C natives, `$x`, native box, stdlib v1, X.
- [x] M10 parity: golden suite green, `exs/` ported, manual rewritten.
    - Golden suite: 444 of the 519 cases of the reference, 79 of them
      adapted (`tst/adapt.py`). The 75 left out are in `tst/dropped/`,
      each with a reason.
    - `exs/`: `hello.atm`, and `stdin.atm` with a driver in a native
      block. The streams and pico-sdl examples depend on what v1 leaves
      out.
    - Manual: `doc/manual.md`, the reference manual with the changes of
      this version.

## Later (after M10)

- Error trace with the tree of tasks (lines of `emit` and `spawn`),
  as in the reference. Skipped for now.
- The `pico-sdl` examples (`clicks.atm`, `click-drag-cancel.atm`).

## Revisions

Agreed after `atmos-lang/atmos@5977b07`.

### Accepted

- Transparent blocks as trails inside the frame of their task (Tasks),
  after the implementation of Céu. Measured with 1000 live tasks and
  `-O2`, minimum of 9 runs, before and after:

  | Task | Bytes | Spawn | Wake | Skip | Abort |
  |---|---|---|---|---|---|
  | awaits forever | 426 / 298 | 265 / 180 ns | | 9 / 10 ns | 92 / 66 ns |
  | one loop | 456 / 330 | 127 / 109 | 73 / 50 | 15 / 15 | 90 / 87 |
  | game object (table, pub, par of 2) | 1873 / 1066 | 1218 / 757 | 124 / 88 | 42 / 33 | 680 / 341 |
  | par of 2 loops | 1338 / 537 | 388 / 204 | 163 / 110 | 47 / 38 | 375 / 262 |
  | par of 4 loops | 2202 / 777 | 640 / 301 | 332 / 217 | 105 / 65 | 802 / 377 |
  | 3 spawn blocks | 1803 / 697 | 472 / 230 | 323 / 208 | 82 / 59 | 447 / 310 |
  | 3 nested watching | 3051 / 808 | 1171 / 331 | 246 / 123 | 202 / 87 | 1452 / 473 |

    - A branch costs about 120 bytes (the record and its slots), down
      from 430.
    - Structures: `ATask` 160 bytes (was 248), `ATrail` 72, `ANode` 64.
- Found by the port of Pingus (`fsantanna/ceu-atmos-pingus`,
  `doc/compiler.md`):
    - `func f` inside a function or task, with `f` unknown, is a
      compile error: `val func f` is the local form. Before, as in the
      reference, it declared a variable of the program, shared by all
      calls (two sprites shared one function).
    - A keyword is accepted as the name of a field in a constructor
      (`[loop=false]`), as in `t.loop`.
    - A backslash escapes the delimiter of a string (`"a\"b"`).
- An exit of a child waits in a parent that is running (Scheduler).
  Before, it travelled down the C stack as the status of whatever was
  in between: a suspended block further up took it as its own, and so
  did any task that was running in between.
- `spawn {}` inside `func` is a compile error in v1 (Language delta).
- `catch` only catches `throw` (Language delta).
- Lexical `break`/`until`/`while` (Language delta).
- Table constructors expand their last positional entry (Language
  delta).
- Golden-suite extractor lives in the new repo (Tests).
- Default driver exits when nothing can awake the main chunk (Driver).
- lua-atmos behaviors kept as they are: termination emitted to the
  owner's subtree, children aborted first in spawn order, await birth
  (Scheduler).
- Indexes are 0-based, in vectors, numeric loops, and strings (Settled
  decisions).
- `#t` exactly as in Lua, shifted to base `0` (Tables).
- User types stay plain tagged tables (Tables).
- Operators follow C: precedence, `%`, bitwise, compound assignment
  (Settled decisions).
- Integers and reals as in Lua 5.3, replacing the single `double`
  (Settled decisions).
- `**`, `?>`, `!>`, `=>=`, `=<=`, `===`, `=!=` removed in favor of
  functions (Settled decisions).

- `??` matches tables field by field, and `await`, `match`, `catch`,
  and `escape` all use it (Settled decisions).
- Await combinators follow the precedence of C (Settled decisions).
- At the end of a block, pinned tasks and pools are aborted in the
  reverse order of their declarations, interleaved with the defers.
- A block that is left while one of its tasks is running waits for
  that task to unwind (Scheduler, Late exit). This replaces the
  private copy of the frame, which kept the variables alive but let
  the outer defers run first.
- Statements take their arguments with no parentheses: `return`,
  `break`, `escape`, `throw`, `until`, `while`, `abort`, `emit`.
    - The argument list extends over the commas, as far as possible,
      and must start in the same line. Otherwise there are none.
    - Declarations and assignments also take a bare list in the
      right: `val a, b = 1, 2`.
    - `(a, b)` remains a list expression, valid anywhere. A single
      expression in parentheses has a single value, as in Lua:
      `return (f())` truncates, `return f()` does not.
    - The predicates of await patterns follow: `await :X until c`.
      Inside `<...>`, the operators `>` `>>` `&&` `||` are ambiguous
      in a predicate with no parentheses (pattern delimiter or
      combinator?), and are a compile error:
      `pattern error : use parentheses to disambiguate`.
    - `until` and `while` keep the same names in loops and in
      patterns. The line tells them apart: in the line of the `await`
      it is a predicate, in the next line it is a statement.
    - Unchanged: `do(e)`, `tasks(n)`, `await`, and `toggle`.
- Statements with no value (`val`, `var`, `pin`, `set`, `defer`, named
  `func` and `task`) are only valid as an item of a block or as the
  body of a case. Elsewhere they are a compile error.
    - The reference accepts them anywhere and then generates invalid
      Lua, so the error comes from Lua.
    - Exits (`return`, `break`, `escape`, `throw`, `until`, `while`)
      remain valid in any position: `val v = f() || throw :X`.
- Modules, first step: `include "file.atm"` splices the statements of
  a file in the main chunk, once per file, with the path relative to
  the including file.
    - Tokens carry their file, and so do the lines in `a_lin`, so that
      errors and traces name the right file.
    - A module as a value (`val m = import "m"`) is left for later.
- `ATMOS_LDFLAGS` gives the linker flags, after the program.
- `lib/sdl.atm` is a basic SDL environment: a `:prelude` with the
  driver and the wrappers, and a table `sdl` with the functions.
  Textures and fonts are native boxes. It has a test mode with no
  screen (fixed frames, scripted keys, screenshots).
- `exs/rocks/` is the "Rocks!" game, ported from
  `atmos-lang/sdl-rocks` and verified in the test mode.
- Memory refinements found while implementing M8 (Pin, Stores, Exits):
  pin at birth for constructors, two-phase block end, lateral check on
  `emit`, confinement after a transfer, abort of confined tasks that
  nobody pinned.

### Rejected

- **Tuples as values**, with or without allocation.
    - A view over stack or frame slots works in functions, but a tuple
      of unknown arity held across an `await` needs the heap.
    - First-class tuples conflict with Lua's implicit truncation.
- **Named lists**, as in `val ...vs = f()` or a `...vs` parameter.
- **Dropping varargs and multiple returns.**
    - Considered and reverted: Lua's rules stay.
- **Dropping variadic native functions.**
    - The argument count is passed anyway, and `print(a, b)` is
      everywhere.

- **Declared user types** with fixed layouts.
- **Table patterns in declarations**, as in `val [x,y] = f()`.

## Workflow

- One commit and one push per change, in small grains.

## Risks

- **Emit reentrancy.** Aborts during traversal are the main source of
  use-after-free bugs inside the runtime. Covered by deferred abort,
  refcounted task headers, and ASan.
- **Escape-check completeness.** A missed channel breaks the invariant
  silently. Every channel in the Stores table gets dedicated tests.
- **Escape-check cost.** Each store of a pinned value compares two
  integers. Cheap, but on hot paths; static elision later.
- **Refcount bugs in generated code.** Keep the ownership convention
  strict and centralize it in a few coder helpers.
- **Semantic drift.** The golden suite is the spec; any intended change
  must update the case and the manual together.
- **Big single C file.** Compile time grows with programs; acceptable
  for v1, separate compilation later.
- **Stdlib scope creep.** Hold v1 to the list above; add on demand.
