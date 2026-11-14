# TODO

Everything that is pending in the session that maintains the compiler
and coordinates the other sessions: asked and not decided, reported and
not acknowledged, or known and not done. Keep it current: remove an item
when it is settled, add one whenever something is left open.

The numbers are the ones used in the conversation with the author, and
do not change. Numbers that are missing were settled (see the end).

Last update: 2026-11-13.

## A. Decisions waiting for an answer

Each has the option recommended by this session.

### The language

| # | Decision | Recommended |
|---|----------|-------------|
| 1 | An `await` over a pool with a condition ("a pingu enters my zone") | Do it. Design it from the cases of SNKRX and Flare, which now exist |
| 2 | Phases inside a tick, instead of relying on the order of creation | Not in the language: a convention with two events, as the ports did |
| 3 | Arithmetic of 32-bit floats | Not to do: exact arithmetic stays in C |
| 4 | An `emit` that names its receiver, instead of `emit @2` | Not to change: pass the receiver as an argument, and document it |
| 5 | An optional `task main` as the entry point | Not to do |
| 6 | The event of termination of transparent blocks | Remove it. It changes the semantics, and two golden tests must be adapted |
| 31 | `await x` for a variable or a field (today only `await(x)`) | Accept |
| 32 | `await :all ts` on an empty pool never awakes, as in the reference | Satisfied at once |
| 33 | Time emitted to a pool or a task (slow motion in SNKRX) | Not now: a global factor and `:tick` solve it there |
| 34 | A constructor nested in a `pin` constructor is not pinned with it | Pin it with the outer one |
| 36 | A `spawn` that dies with its block, silently (29 places in SNKRX; also Pingus and Lessmilk). Settled: a spawn in an `if` is not accepted, and the way out is a pin outside (`pin t = if c { spawn T() }`) or a pool. Open: a diagnostic for a spawn with no pool and no assignment whose block ends with no await after it | A compile error, with a message that names the two ways out. The alternative is a warning, which the compiler does not have today |
| 39 | `grab` and `context`, issues 22 and 21 of `atmos-lang/atmos`. `grab` would serve the modals of all ports; `context` the draw targets of SNKRX and raylib | To talk about. The comments of the issues were not read (no access) |
| 40 | Friction with `pin`, in six ports: a table of a module cannot hold a pinned value (Flare); nor a variable captured by a function (Lessmilk); nor a table that is an argument, `f([x=a])` (pico); nor the `pub` of the task itself (Flare); nor the table of an await pattern or of an emit, `await :close [h=dlg]` with `dlg` pinned, which every program of IUP has (IUP); and a connection accepted in a loop cannot be pinned there and live in a pool outside (libuv). The author opened a session for it, "Pin rules discussion", which has these cases | For the module: a scoped assignment, `with m.x = v {...}`. For the temporary tables (an argument, a pattern, an event): let a constructor that receives a pinned value be pinned in the block of that value, instead of an error. Not weak references, and not the task in its own `pub` |
| 41 | The error `invalid escape : pinned value` does not say what escaped (asked by two ports) | Add the type of the value. It changes the expected output of 13 tests |
| 45 | A snapshot of the tasks by the runtime, to save and restore a program. Possible in this backend (a task is a record, not a C stack); native values need hooks from each binding | To talk about. Doom is the use case |
| 46 | The vocabulary of the events of an environment. Settled on 2026-11-13 (in the session `pico-sdl`): the tags have no prefix. Open: the names and the fields. Today: keys are `:key.dn [key='A']` in pico, `[name='A']` in sdl, `[key=262, name='RIGHT']` in raylib; the mouse is `:mouse.button.dn` and `:mouse.motion` in pico, `:mouse.dn` in the others, `:mouse.mv` in SNKRX; a frame is the time and `:draw` in pico and sdl, `:frame` and `:draw` in raylib and Flare, `:tick` and `:draw` in SNKRX | The names of pico, which are those of the original `env-pico`: `:quit`; `:key.dn` and `:key.up` with `key` as the name of the key (and `code` where the library has a number); `:mouse.motion`, `:mouse.button.dn`, `:mouse.button.up` with `x`, `y`, `button`; and two events per frame, `:frame` for the logic and `:draw` for the drawing. Events of a handle carry it as `h` (`:action [h=btn]`) |
| 48 | A `skip` (continue) for loops: issue 7 of `atmos-lang/atmos`, and Lessmilk missed it | Not studied yet |
| 60 | An `emit` to one task (`emit @t (e)`) whose target raises an exception: the exception goes to the task that emitted, and the task that holds the target never sees it. With a broadcast of the same event it goes to the holder. The reference does the same. Found when an environment emitted to one task from C; for that case there is now `atm_emit_to`, which delivers the exception to the holder | To the holder in both, as in a broadcast: where an exception goes should not depend on how the event was emitted. It departs from the reference |
| 58 | `test` blocks to test a game (asked by the author in the session `pico-sdl`). Today a run with no screen is driven from outside, by environment variables that each binding reads in its `main`, with other names in each | A convention of the bindings, not of the language: one call that puts the environment in the test mode (fixed frames, no real time, no sound), and the rest is plain Atmos in the `test` block (`await(10, :frame)`, an `emit` of an input event, an assertion, a screenshot by a function; the run ends with the block). Polled input needs a function of the binding to set it. To try first in one environment |
| 59 | How a program selects its environment. The author asked (2026-11-14, in the session of pico) for a compiler of its own per environment (`atmos-pico`, `atmos-iup`), made from this compiler, with `include` still working, and with the examples and documents using the dedicated one. Done here: `make env` builds one, and the option `-e` does the same by hand. Open: it still needs the directory of this compiler, a C compiler, and the development files of the library; a download that needs none of them is another, larger thing | To confirm the mechanism. Each environment then builds its `atmos-<env>` with it and uses it in its examples |

### Projects and repositories

| # | Decision | Recommended |
|---|----------|-------------|
| 37 | Doom: how far to go after the flow of the game (the status bar, the messages, the menus, the automap). The other three questions of the plan were answered on 2026-11-13 with the recommended options (Chocolate Doom, a copy with a first untouched commit, the shareware data fetched by a script), and the session may start | To decide when the wipe, the intermission, the finale, and the flow are done |
| 42 | The environment for IUP: the plan is sent (`261113-env-iup.md`). It waits for the author on three items: a thin and generic binding, the seven tasks of 7GUIs as what to build, and how IUP gets into the container. Its first milestone is only a check that IUP builds and opens a dialog with no screen | The three as recommended in the plan |
| 43 | The environment for libuv: the plan is sent (`261113-env-uv.md`). It waits for the author on three items: the order of the features, the samples of `ceu-libuv` as tests, and who owns the main loop (the environment, with a step function for a GUI to drive it later) | The three as recommended in the plan |
| 47 | The binding of raylib and its 23 games share one repository (`env-ray`), and Lessmilk keeps a copy of the binding, pinned at a commit | Split the games out (`ray-games`), so that the binding is used and not copied |
| 55 | The three games over SDL (Pingus, SNKRX, Flare) wrote three platform layers of their own, and none uses `lib/sdl.atm`. The author asked to make them use the environment they are supposed to use. Of the 87 functions of SDL that the three use, 24 are common to all, and `lib/sdl.atm` covers 32. Each engine also has C of its game, which stays (the pathfinding and the maps of Flare, the physics of SNKRX, the collision maps of Pingus) | In course: the author created `ia-atmos/env-sdl`, and its session has the plan `261113-env-sdl.md`. Five items of the plan wait for him there: one file `sdl.atm`, the prefix of the events, one test mode, `lib/sdl.atm` leaving the compiler (and where `exs/rocks/` goes), and how a game gets the environment |
| 56 | Whether the file `raylib.atm`, its table `rl`, and the prefix `:rl.` of its events follow the new name of the repository (`ray`) | Not studied. Related to item 46 |
| 27 | SuperTux, taken up again as `sdl-tux`: the plan is sent (`261113-sdl-tux.md`), to a new session. It waits for the author on four items: the tag `v0.7.0`, the slice (World 1, in two steps), the scripts (those of the levels as data that a small task interprets, the long scenes as Atmos code), and the SDL renderer only | The four as recommended in the plan |
| 57 | How two environments live in one program (a GUI and the network: two loops, one `main`). Both `env-uv` and `env-iup` meet it | Not designed. `env-uv` offers a step of its loop as a first answer |
| 49 | The picture of the organization and the social preview of the repository `lang` | Upload `doc/logo/atmos-icon.png` and `doc/logo/atmos-1280x640.png` (the author's to do, in the settings of GitHub) |
| 54 | The assets of Lessmilk have no known license; the author chose to copy them | Ask Thomas Palef |

## B. Work known and not done

Waiting for the author's "ok":

| # | Work | Note |
|---|------|------|
| 6 | Remove the event of termination of transparent blocks | See A |
| 11 | Mouse events in `lib/sdl.atm` | Recommended to do now: `raylib.atm` and `pico.atm` have them |
| 18 | Delete the branch `trails`, already in `main` | |
| 22 | Mark the plan of Pingus as done, with the link to the port | |
| 23 | Ask the session `pingus` to remove the workarounds that the compiler no longer needs | |
| 24 | Ask the session `pingus` to point the link of the plan in its README to `ia-atmos/lang`, `main`; then delete the branch `ccr-566b5373-ag7186` (item 19) | |

Not started, with no decision needed:

| # | Work | Note |
|---|------|------|
| 12 | Tweens and springs as a library | To harvest from what SNKRX (`eng/anim.atm`) and Lessmilk (`lib/motion.atm`) wrote |
| 15 | Faster compilation (24 s in Pingus, a single C file) | To measure in Flare first |
| 35 | Performance. Three ports show the same profile: the dispatch of events to many tasks, the access to tables, and, the clearest, the construction of small tables (about 0.5 us each, in the Storm of pico) | Not now: SNKRX runs at almost twice the speed of its original |
| 38 | The paper, "Structured Reactive Concurrency in Games". The author wrote an outline in `ia-atmos/paper-src-games` | After the ports close: they are its evaluation |
| 52 | Environments that the families of Atmos and Céu have and this backend does not: the browser (through WebAssembly; SDL and raylib support it) and embedded boards | Ideas, not asked for |

## C. Reported and not acknowledged

Done by this session with a choice that the author did not confirm. Each
may be changed.

- **The order of awaking, in the manual.** The tasks of a block in the
  order they were attached; nested tasks and pools before the body of
  their task; a task only reacts to an emit that starts after its
  await. It states what the runtime and the reference do.
- **A loop over a pool** visits a task that its body spawns into the
  pool, and does not visit one that its body aborts.
- **`await(n, p)`**: a count lower than 1 is satisfied at once, with
  `nil`; the value is the one of the last time; a counted pattern cannot
  spawn a task; the parentheses are optional only in `watching` and
  `loop on`.
- **A `:clock` await awakes at each piece of the time**, and so many
  times per frame when timers are pending (as in the reference). Now in
  the manual, after the port of Storm took one `:clock` for one frame.
- **A native literal fails by assigning to `st`**, and the names with
  the prefixes `a_`, `A_`, and `atm_` are reserved.
- **A task that an await spawns gives its final value away**
  (`pin s = await T()` works; found by `env-uv`). The task that comes
  as the second result no longer holds the value: `val v, t = await
  T()` then `t.ret` is `nil`. A task awaited by its reference keeps
  it.
- **A compiler dedicated to an environment** is this compiler built
  with defaults (`make env`), not a wrapper nor a library: the file of
  the environment is included before the program, and its flags go to
  the C compiler before those of the user.
- **`math.atan(y, x)`** takes the point, as in Lua 5.4.
- **`pub`** is rejected as the name of a variable.
- **The README** follows the one of `atmos-lang/atmos`. It has no badge
  of tests and no list of versions: this repository has no CI and no
  releases.
- **The logos** in `doc/logo/` are rendered from `atmos.svg` of
  `atmos-lang/etc`. To upload them as the picture of the organization
  and as the social preview of the repository is the author's to do
  (item 49).

## D. Other sessions

What waits for the author in them, as far as this session knows.

| Session | Waits for |
|---------|-----------|
| `doom` | Nothing: it may start its first milestone |
| `iup`, `uv`, `sdl-tux`, `env-sdl` | The items of their plans marked to confirm (items 42, 43, 27, and 55) |
| `raylib` | A permission to change the remote of its checkout of the compiler (harmless meanwhile: the old address redirects) |
| `flare` | Three items of its plan to confirm, and a permission to install packages (not checked again since 2026-11-11) |

Each session keeps its own `.claude/plans/TODO.md`, in its repository,
since 2026-11-13: what waits for the author in each is there.

The repositories of the organization were renamed on 2026-11-13, and
every session was told its new name: `env-pico`, `env-iup`, `env-uv`,
`env-ray`, `ray-lessmilk`, `sdl-pingus`, `sdl-snxkx`, `sdl-flare`,
`paper-src-games`, `sdl-tux`; `doom` and `lang` kept theirs. The titles
of the sessions still have the old names.

## Standing rules from the author, for all sessions

- Commit and push straight to `main`, in fine-grained commits.
- Each session keeps a `.claude/plans/TODO.md` (2026-11-13).
- The events of an environment have no prefix (2026-11-13).
- **Fidelity, then expressiveness** (2026-11-14, said in the session
  of Lessmilk): once a port is fully faithful to its original, it may
  relax the constraints of exactness in favor of expressiveness, as
  long as the intent of the original author is not affected. It holds
  for every port, after it reaches full fidelity. The plans say that a
  difference is never an accepted variation: that stands until full
  fidelity, and not after.

## E. Housekeeping of this session

- If the container restarts, the checkout comes back with the old
  address (`fsantanna/ceu-atmos`): point the remote to `ia-atmos/lang`
  before any push.
- The cloud environment of this session is still named `ceu-atmos`, and
  does not reach `lessmilk.com` (the request to add the host could not
  be approved from the phone).

## Settled, for the record

7, 9, 10, 13, 14, 16, 20, 21 (kept as they are); 8 (became 31); 17 and
28 (the session `pico-sdl` wrote the binding and its examples); 25 and
26 (moved to D); 29 and 30 (the games were chosen; Doom was added); 44
(`await(n, p)`, done); the names of the repositories (first part of 47);
51 (became 55).
