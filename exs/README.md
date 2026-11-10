# Examples

```bash
make
./atmos exs/hello.atm
printf 'one\ntwo\nquit\nthree\n' | ./atmos exs/stdin.atm
```

- `hello.atm`: timers over the default driver, which follows the real
  clock.
- `stdin.atm`: an environment written in the program itself. A native
  block defines the driver, which emits each line of the standard
  input as an event.

The examples of the reference implementation that are not here depend
on what this version leaves out: the functional streams (`rx.atm`,
`rx-behavior.atm`) and the `pico-sdl` environment (`clicks.atm`,
`click-drag-cancel.atm`).
