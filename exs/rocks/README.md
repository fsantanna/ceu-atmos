# The "Rocks!" game

A spaceship shooter game for 2 simultaneous players, ported from
[atmos-lang/sdl-rocks](https://github.com/atmos-lang/sdl-rocks) (v0.5).

- Hit the other ship.
- Avoid the rocks!
- Controls:
    - Left Ship: `WASD` to move, `Shift Left` to shoot.
    - Right Ship: Arrow keys to move, `Shift Right` to shoot.
    - `Enter` starts a battle, and `P` pauses it.

# Run

It requires the development files of SDL2, SDL2_image, SDL2_ttf, and
SDL2_mixer:

```
sudo apt-get install libsdl2-dev libsdl2-image-dev libsdl2-ttf-dev libsdl2-mixer-dev
```

```
cd exs/rocks
ATMOS_LDFLAGS="-lSDL2 -lSDL2_image -lSDL2_ttf -lSDL2_mixer" ../../atmos main.atm
```

# Files

- `main.atm`: window, background, points, and the main loop.
- `battle.atm`: a battle, with the pools of ships, shots, and meteors,
  and the collisions.
- `ts.atm`: the tasks for ships, shots, and meteors.
- `../../lib/sdl.atm`: the SDL environment.

The images and sounds have their own authors: see `imgs/CREDITS` and
`snds/CREDITS`.

# Differences from the original

- `include` instead of `require`, and functions of `sdl.atm` instead of
  the methods of the Lua bindings: `sdl.copy(tex, crop, rect)` for
  `REN::copy(tex, crop, rect)`.
- The events have their own tags: `:sdl.key.dn [name='Return']` for
  `:sdl [type=SDL.event.KeyDown, name='Return']`.
- Textures are pinned to their tasks, which releases them when the task
  ends.
- The collision list is a pinned table, filled from the pools. The
  original builds it with `[] ++ ships ++ ...`, which is now an escape
  error: the tasks of a pool cannot go to an unpinned table.

# Test with no screen

```
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
SDL_ATM_FRAMES=320:16 \
SDL_ATM_KEYS="5:dn:Return,20:dn:Left Shift,21:up:Left Shift" \
SDL_ATM_SHOT="100:/tmp/battle.png,310:/tmp/score.png" \
ATMOS_LDFLAGS="-lSDL2 -lSDL2_image -lSDL2_ttf -lSDL2_mixer" ../../atmos main.atm
```

It runs 320 frames of 16ms, starts a battle, shoots once from the left
ship, and saves the screen at frames 100 and 310. The shot hits the
other ship, so the last image shows the points as 1 and 0.
