# ikcas

A Linux-first symbolic CAS TUI app in C (CMake), with exact rational arithmetic and symbolic manipulation.

## Features

- Immutable AST in arena allocators
- Exact evaluation pass with rationals (`rat_t`)
- Symbolic atoms and user-defined functions
- Function/callable dispatch through environment bindings
- TUI built with notcurses (split REPL/info layout)
- Evaluation mode toggle:
  - `:mode exact`
  - `:mode approx`

## Build

Requirements:

- Linux
- CMake
- C compiler (GCC/Clang)
- notcurses development package
- pkg-config

Build:

```bash
cmake -S . -B cmake-build-debug
cmake --build cmake-build-debug -j
```

Run:

```bash
./cmake-build-debug/ikcas
```

## Usage

- Enter expressions directly:
  - `2/3 + 1/6`
  - `f(x)=x^2`
  - `f(2)`
- Commands:
  - `:help`
  - `:mode exact`
  - `:mode approx`
  - `:quit` or `:q`
  - etc.

## Notes

- Exact mode preserves symbolic forms unless exact folding is valid.
- Approx mode applies approximation at the end of evaluation.
- Predefined symbolic constants are available as environment bindings (`pi`, `e`).

