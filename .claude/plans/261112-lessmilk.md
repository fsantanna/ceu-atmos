# Plan: the Lessmilk games in Atmos

Status: sent to the session that works in the repository
`fsantanna/lessmilk`.

## Context

- Lessmilk is the challenge of Thomas Palef in 2014: twelve games in
  twelve weeks, in JavaScript over Phaser. The games are small (150 to
  420 lines each) and well known among people who learn to make games
  for the web.
- They are the common shape of a small game in a dynamic language
  over a framework: states of the game as objects with `create` and
  `update`, tweens chained by callbacks, timers compared to the clock
  every frame, groups of sprites, and flags.
- A student, Jéssica Oliveira, ported all twelve to the reference
  Atmos over `pico-sdl`, with a written analysis per game. The author
  considers that port good: this plan starts from it.
- This plan ports the twelve to Atmos over the compiler
  `ia-atmos/lang`, which differs from the reference Atmos, and
  compares each one with its original running in a browser.
- It is the counterpart, for a dynamic language, of the port of the
  raylib games (`fsantanna/raylib`, plan in `261112-raylib.md`): many
  small games and a table, next to the large ports (Pingus, SNKRX,
  Flare).
- Goals, in order:
    1. The twelve games running over this compiler, from the student's
       port, corrected against the originals.
    2. Behavior written as sequences: every tween callback, every
       comparison with the clock, every change of state of the game
       becomes an `await`, a block, or a task.
    3. The modules that replace what Phaser gave (motion, particles,
       physics), written once and shared by the twelve.
    4. A table per game: lines, tokens, state variables removed,
       memory, and CPU, against the JavaScript.

## Sources

| What | Where | State |
|------|-------|-------|
| The student port, the starting point | `jessicacoliveira/atmos-lessmilk@3e24458` | 2026-11-01, reference Atmos 0.8, `pico-sdl` 0.7 |
| The originals, a copy of the site | `leokaplan/lessmilk@`(2015), folder `www.lessmilk.com/games/` | JavaScript, Phaser 1.1.3 (games 1 to 9), 1.1.6 (10), 2.0.1 (11), 2.0.2 (12) |
| An earlier sketch by the author | `fsantanna-no/atmos-lessmilk` | 2026-01, `run.atm` and notes |
| Ports to Céu of games 1 and 2 | `leokaplan/lessmilk`, folder `ceu/` | 2015, 954 lines |
| Port of Pingus, the model | `fsantanna/ceu-atmos-pingus` | complete |
| Its tips | the section "What the Pingus port taught", below | |
| The raylib environment | `fsantanna/raylib`, `raylib.atm` | in course, by the session `raylib` |
| The SDL environment, as a model of one | `ia-atmos/lang`, `lib/sdl.atm`, `exs/rocks/` | |

- The site `lessmilk.com` could not be read when this plan was
  written. The license of the original games and of their images and
  sounds is not known: see Settled decisions.

## What the target is made of

### The originals: 12 games, 3388 lines of JavaScript

| # | Game | Lines | Files |
|---|------|-------|-------|
| 1 | Run! | 148 | 4 |
| 2 | Pixel War | 201 | 5 |
| 3 | Box Jump | 189 | 4 |
| 4 | Man vs Penguins | 313 | 4 |
| 5 | Fill the Holes | 381 | 6 |
| 6 | Princess Quest | 418 | 5 |
| 7 | Connected | 338 | 5 |
| 8 | Save the City | 180 | 3 |
| 9 | I Hate Rabbits | 262 | 6 |
| 10 | Dark Blue | 311 | 5 |
| 11 | The Three Hearts | 359 | 4 |
| 12 | Crazy Snake | 289 | 5 |

- Each game is a few states of Phaser (`load`, `menu`, `play`, `end`),
  one file each, switched with `game.state.start` (60 uses).
- Sequences are tweens (102 uses of `game.add.tween`), with
  `onComplete` callbacks, and comparisons with the clock
  (`game.time.now`, 30 uses).
- Phaser gives the rest: arcade physics (27 uses of `overlap` and
  `collide`), particle emitters (38 uses), groups, sprites with
  animations, text, and sound.
- Images and sounds: 600 KB in all.

### The student port: about 3400 lines of Atmos per version

| Game | Lines | Files |
|------|-------|-------|
| `Run` | 407 | 7 |
| `box_jump` | 233 | 1 |
| `connected` | 349 | 2 |
| `crazy_snake` | 410 | 3 |
| `dark_blue` | 796 | 4 |
| `fill_the_holes` | 622 | 4 |
| `i_hate_rabbits` | 678 | 10 |
| `man_penguins` | 976 | 12 |
| `pixel_war` | 345 | 1 |
| `princess_quest` | 577 | 3 |
| `save_the_city` | 652 | 9 |
| `the_three_hearts` | 696 | 4 |

- The counts include two versions of several games: one file
  (`main.atm`) and a split one (`modular/`). So the port is about the
  size of the original.
- Shared modules, written to replace what Phaser gave: `motion.atm`
  (a task that takes properties of a table to target values in a
  given time: linear, accelerate, decelerate, bounce) and
  `particles.atm` (one, burst, flow).
- A README per game, in Portuguese: the flow of screens as a diagram,
  the tasks, and the comparison with the Phaser code by pattern
  (continuation passing, lifespan, and the others).
- Four games carry the original Phaser code next to the port.
- A folder of bugs found in the reference implementation.
- It uses the reference Atmos: `require`, indexes from 1, and `pico`
  calls with coordinates in percentages. None of it compiles here as
  it is.

## Settled decisions

The author asked for this plan, based on the student's port. The
items marked "(confirm)" are recommendations that he has not
answered: ask, one at a time, before building on them.

- **Repository**: `fsantanna/lessmilk`, commits straight to `main`,
  in small grains.
- **From the student's port.** Its task structure, its modules, and
  its documents are the base, with credit to her in the README and in
  each document. Each game is rewritten for this compiler and then
  corrected against the original.
- **Library**: raylib, decided by the author. The port uses the
  environment `raylib.atm` that the port of the raylib games is
  writing (`fsantanna/raylib`, session `raylib`), copied at a fixed
  commit and updated when convenient. The two sessions talk directly
  about the library: what this port needs from it (sprites with
  rotation, easings, sounds, text, the test mode), and what it finds.
  The first draft of this plan proposed SDL2, when that environment
  did not exist.
- **Coordinates**: pixels, as the originals, not the percentages of
  `pico`. Confirmed by the author at L0.
- **Assets**: copied into the repository, unchanged, with the credits
  in the README, decided by the author at L0 (this plan proposed to
  keep them out). Games 1 and 2 come from the copy of the site, which
  has the assets of those two only; games 3 to 12 come from the
  student's port. Two files exist nowhere: `levels/map.json` of game
  11, rebuilt from the student's `map.atm`, and `tiles.png` of game
  10.
- **One version per game**: the split one, with the shared modules in
  one place.
- **Documents in English**, as the other ports, from the student's
  documents in Portuguese.

## Patterns

What the JavaScript does, and what it becomes (the student's port
already does most of this):

| Original | Atmos |
|----------|-------|
| states with `create` and `update`, switched by `game.state.start` | a task per screen, awaited in sequence, in a `loop` |
| the score kept in a global for the next state | the value that the screen returns |
| `game.add.tween(...).to(...)` with `onComplete` | `await Motion...`, then what follows |
| tweens chained by callbacks | awaits in sequence |
| `if (game.time.now > this.next)` every frame | `await` of the time, in a `loop` |
| a flag that says an animation is running | the block between two awaits |
| a group of sprites, recycled with `getFirstDead` | a pool of tasks |
| `overlap(a, b, callback)` every frame | a collision that arrives as an event |
| an emitter with `start(explode, lifespan, ...)` | `Particles.burst` and `Particles.flow` |

## The shared modules

Written once, in a folder of the repository, used by the twelve:

- **Motion**: the student's module, with the easings that the
  originals use (Phaser's `Linear`, `Quadratic`, `Bounce`, and the
  others that appear).
- **Particles**: the student's module.
- **Physics**: what the games use of the arcade physics of Phaser:
  velocity, acceleration, gravity, bounce, the bounds of the world,
  and overlap and collision of boxes. One pass per frame over all
  bodies, which emits an event to the two tasks of each contact. No
  task scans the others (the lesson of Pingus).
- **Sprite**: an image or a sheet with named animations.
- **Screen flow, text, sound**: thin helpers.

When they are stable, they are candidates for `lib/` of `ceu-atmos`:
the port of SNKRX needs tweens too.

## State variables

- A table per game in `doc/state.md`: every flag, timestamp, and
  global of the JavaScript, with what became of it.
- The summary counts, per game, the control variables of the
  original and the ones left in the port.

## Fidelity

The originals run in a browser. The container has a Chromium with no
screen, driven by Playwright. Set up before the first game:

- A page per original game, served from the copy, with a small common
  harness (`tools/original/`): a seeded random generator in place of
  `Math.random`, a clock stepped by hand in fixed frames, scripted
  keyboard and mouse by frame, screenshots at chosen frames, and a
  trace per frame with the score and the positions that each game
  exposes.
- The port runs the same script and seed, in the test mode with no
  screen.
- `tools/compare.sh` compares the traces line by line and shows the
  first frame that differs. Screenshots are compared side by side,
  not pixel by pixel: fonts and scaling differ between a canvas and
  raylib.
- What can match exactly: the logic of each game (positions, score,
  what spawns when) if the port uses the same generator and the same
  arithmetic. JavaScript numbers are doubles, as in Atmos.
- Where the physics of Phaser cannot be matched exactly, say so in
  `doc/fidelity.md` for that game, and compare timings and scores.
- If the browser cannot run in the container, the originals are
  judged by reading the code and by the author's eye.

## Scope

- All twelve games, each from the menu to the end and back.
- The shared modules.
- Out: the pages of the site, the resizing for phones, touch input.

## Milestones

- [x] L0: repository, readme with the credits (Thomas Palef, Jéssica
      Oliveira). The copies of the originals and of the student port
      checked out next to the repository. The originals running in
      the browser with no screen, with the harness. Done at
      `fsantanna/lessmilk@e2e08e8`: seeded `Math.random`, a stepped
      clock, keys and mouse scripted in the `RL_INPUT` format of
      `fsantanna/raylib`; 11 of the 12 originals run deterministic.
- [ ] L1: the engine over `raylib.atm`: window, images, text, sound,
      the test mode. The modules Motion (over the easings of raylib,
      `reasings.h`) and Sprite.
- [ ] L2: `Run!`, the smallest: ported, compared with the original,
      documented. It sets the form of the other eleven.
- [ ] L3: the module Physics and the module Particles. `Box Jump`,
      `Save the City`, `Pixel War`.
- [ ] L4: `I Hate Rabbits`, `Crazy Snake`, `Dark Blue`,
      `Man vs Penguins`.
- [ ] L5: `Connected`, `The Three Hearts`, `Fill the Holes`,
      `Princess Quest`.
- [ ] L6: a launcher for the twelve. The table and the benchmark.
      `doc/state.md`, `doc/fidelity.md`, `doc/compiler.md`.
- [ ] L7: the case study in `doc/`: for each pattern, the JavaScript
      and the Atmos of the shortest game that shows it, from the
      student's analyses.

Each game ends with its tests in the suite and its row in the table.

## Benchmark

- Baseline: each original in the browser with no screen, in the same
  container, one after the other.
- Measure, per game: memory of the process (the browser for the
  original), CPU in 10 s at 60 frames per second, the time of a frame
  of logic with no drawing, lines and tokens against the JavaScript.
- A stress case in a few games, made for the test: hundreds of
  enemies or particles.
- The comparison of memory is unfair to the browser, which is a large
  program: report it, and say so. The time of a frame of logic is the
  number that compares the languages.

## Tests

- `tst/run.sh`: every game runs its scripts with no screen; the
  traces are compared to stored ones, and screenshots are kept.
- `ASAN=1 tst/run.sh`: the same with the address sanitizer.
- `tools/compare-all.sh`: every script against the original in the
  browser.

## Documents

- `doc/table.md`: one row per game.
- A document per game, from the student's README: the flow of
  screens, the tasks, and the comparison with the original by
  pattern.
- `doc/state.md`, `doc/fidelity.md`, `doc/bench.md`,
  `doc/compiler.md`, as in the Pingus port.
- `doc/from-reference.md`: what changed from the reference Atmos to
  this compiler in practice, game by game. It is the first port that
  crosses the two implementations.

## What the Pingus port taught

The list its session sent, in its order of importance. It holds for
any port of this kind, whatever the game.

1. **Day one: the official game with a trace.** Build it and patch it
   to print a trace in the exact format of the port (time, object,
   state, x, y), flushed, plus a dump per tick of one chosen object,
   plus a replay of its recordings faster than real time, with no
   screen. Reading the C++ found 3 differences; the traces found about
   15. Fidelity went from 17 to 142 identical demos only after the
   first differing tick could be found. Check early that the official
   game builds in the container (installing packages needs the
   author's permission).
2. **Day one too: a test mode with no screen in the port.**
   Environment variables for the frames to run, scripted input by
   tick, a screenshot at a frame, mute, no frame cap, the frame count
   at exit. Every test, replay, and benchmark used it. Stored traces
   per level caught every regression.
3. **Numbers as the original.** The same float width from the start
   (C++ `float` is 32 bits, Atmos numbers are doubles): there, the
   mover went to C. Also: the real positions of objects (the converter
   rounded them), the order of objects at setup, the two phases of an
   update (move, then react), when input applies inside a tick, and
   forced against requested changes of state. Copy the clocks of the
   official code exactly.
4. **Scope decided up front.** The plan said one thing and the author
   then wanted all the official data and objects. Put the later items
   in the main milestones. A generic converter from s-expressions,
   with a list of kinds, was easy.
5. **Engine.** A single C header (about 900 lines there: image cache,
   fonts, sounds, collision maps, mover) plus thin wrappers in Atmos,
   and one C file to build (24 s at `-O2`). Hot pixel loops and exact
   float math go to C. Sprite descriptions are cached, never loaded
   per frame.
6. **Idioms that worked.** One task per entity, whose life is the
   task; a pool per draw layer (draw order is pool order); the state
   machine of an entity as a `loop` over `par :any { action } with {
   orders } with { forced }`, whose result is the next state;
   `watching` for phases that can be aborted; `toggle` for pause;
   `await Ticks(n)` for clocks; `do :tag { escape :tag }` for
   continue.
7. **Pitfalls.** `emit @:parent` from a task in a pool reaches the
   pool, not its owner: pass the receiver. A spawn inside an `if`
   block dies with the block. Iterating a tagged table gives its
   `tag`. A sprite of one frame that never ends leaks into its pool
   (it made the rain quadratic there). In `main` since then:
   `[loop=false]`, `"a\"b"`, and the error for a nested `func`.
8. **What still reads poorly there.** Objects that scan a whole pool
   every tick; the order inside a tick carried by spawn order and two
   events; records of an entity written from outside; animation clocks
   derived by arithmetic. For a game with collisions everywhere,
   decide early how they are dispatched: one broad phase in C per
   tick, which emits events to the tasks involved, rather than each
   task scanning.
9. **Environment.** The official game needed about 15 development
   packages and a few minutes to build. Its suite takes about a
   minute; comparing 159 demos, 15 to 20 minutes with 4 jobs. Heavy
   runs go to the background, and the benchmark runs alone (the noise
   is about 10% anyway). The official checkout stays outside the
   repository, and its patch inside.
10. **With the author.** Small commits, each pushed, straight to
    `main`. Design and fidelity decisions were settled alone and
    written down. What needed the author: installing packages, scope
    beyond the plan, and what to do next. Only measured numbers go
    into the documents.

## What the port may need from the compiler

Report in `doc/compiler.md`. To watch here:

- Everything that the student's code uses from the reference Atmos
  and that this compiler lacks or does differently.
- Tweens, particles, and a simple physics as libraries.
- An await over a pool with a condition, for collisions.

## Risks

- **License.** The originals and their assets have no known license.
  The author chose to copy the assets, with credits; the license is
  still to be asked of Thomas Palef.
- **The physics of Phaser.** Small differences in collision change a
  game of jumps. The module must follow what the Phaser of each game
  does, read from its source, for the few operations that the games
  use. The games use four versions: the arcade physics changed from
  1.1 to 2.0, so games 11 and 12 may not share every rule with the
  others.
- **Two languages called Atmos.** The student's code looks close to
  what compiles here, and is not: indexes, `require`, and the `pico`
  calls change everywhere. Rewrite game by game, with the original
  open, instead of converting text.

## Workflow

- Commits straight to `main`, small, each with its test.
- Decisions that belong to the author are asked one at a time, with a
  recommended option.
- Installing packages needs the author's permission: ask at L0.
- Findings in the compiler are reported, not worked around in
  silence. The compiler is maintained by the session `ceu-atmos`.
- Only measured numbers go into the documents.
