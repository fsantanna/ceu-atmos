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
| 40 | Friction with `pin`, in four ports: a table of a module cannot hold a pinned value (Flare); nor a variable captured by a function (Lessmilk); nor a table that is an argument, `f([x=a])` (pico); nor the `pub` of the task itself (Flare) | A scoped assignment, `with m.x = v {...}`, undone at the end of the block. Not weak references, and not the task in its own `pub` |
| 41 | The error `invalid escape : pinned value` does not say what escaped (asked by two ports) | Add the type of the value. It changes the expected output of 13 tests |
| 45 | A snapshot of the tasks by the runtime, to save and restore a program. Possible in this backend (a task is a record, not a C stack); native values need hooks from each binding | To talk about. Doom is the use case |
| 46 | The tags of the events across bindings: `pico.atm` has no prefix (`:key.dn`, `:draw`), `lib/sdl.atm` and `raylib.atm` have one (`:sdl.key.dn`, `:rl.frame`). The fields differ too | Not studied yet |
| 48 | A `skip` (continue) for loops: issue 7 of `atmos-lang/atmos`, and Lessmilk missed it | Not studied yet |

### Projects and repositories

| # | Decision | Recommended |
|---|----------|-------------|
| 37 | Doom: the plan is sent (`261113-doom.md`), and the session waits for the author on four items: the base (Chocolate Doom or `linuxdoom-1.10`), the engine as a copy in the repository, the game data, and how far to go after the flow | Chocolate Doom; a copy with a first untouched commit; the shareware `DOOM1.WAD` fetched by a script |
| 42 | The environment for IUP: the session `iup` waits for a plan. IUP has no native driver for the Mac, as far as this session knows (not verified) | Write the plan |
| 43 | The environment for libuv: the session `uv` waits for a plan. To decide in it: who owns the main loop when a program has a GUI and the network | Write the plan, after `ceu-lang/ceu-libuv` |
| 47 | The names of the repositories of `ia-atmos`, after the convention of the original organizations: `env-pico`, `env-iup`, `env-uv`, `env-raylib`, `raylib-lessmilk`, `sdl-pingus`, `sdl-snkrx` (today `snxkx`, with the letters swapped), `sdl-flare`. And whether to split `raylib` into the binding and its 23 games | Rename all at once; split `raylib`, so that Lessmilk drops its patched copy of `raylib.atm` |
| 27 | The repository `tux` is empty, and its session is archived | Delete or archive it (the author's to do) |
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
| 38 | The paper, "Structured Reactive Concurrency in Games". The author wrote an outline in `ia-atmos/src-games` | After the ports close: they are its evaluation |
| 51 | The three games over SDL (Pingus, SNKRX, Flare) wrote three layers of their own, and none uses `lib/sdl.atm` | An idea: one shared environment for SDL |
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
| `doom` | The first of its four questions: the base of the engine (item 37) |
| `iup`, `uv` | Their plans (items 42 and 43) |
| `snxkx` | To confirm the new name of the compiler, and the name of its local folder |
| `lessmilk` | Whether to switch to `ia-atmos/lang` now or later |
| `raylib` | A permission to change the remote of its checkout of the compiler |
| `flare` | Three items of its plan to confirm, and a permission to install packages (not checked again since 2026-11-11) |

The three that wait because of the new name of the compiler are better
answered after the renames of item 47, so that each updates names once.

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
(`await(n, p)`, done).
