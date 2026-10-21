# ikcas

A small computer algebra system written in C. It evaluates expressions with arbitrary-precision integers, exact fractions and floats, keeps unknown names as symbols, and is built to grow through plugins.

> **Platform:** Linux only. The scratch arena uses `mmap` and plugins are loaded with `dlopen`.

---

## Table of Contents

- [Requirements](#requirements)
- [Building](#building)
- [Quick Start](#quick-start)
- [Command Line](#command-line)
- [Language Reference](#language-reference)
  - [Numbers](#numbers)
  - [Operators](#operators)
  - [Functions](#functions)
  - [Symbols](#symbols)
  - [Errors](#errors)
- [Plugins](#plugins)
  - [plugins.conf](#pluginsconf)
  - [Loadable Plugins](#loadable-plugins)
  - [Writing a Plugin](#writing-a-plugin)
- [How It Works](#how-it-works)
- [Project Layout](#project-layout)
- [Running Tests](#running-tests)
- [Contributing](#contributing)
- [License](#license)

---

## Requirements

- GCC
- GNU make
- `bear` (optional, only for `make compdb`)

On Debian/Ubuntu:

```sh
sudo apt install gcc make
```

---

## Building

```sh
git clone https://github.com/iKryxx/ikcas.git
cd ikcas
make
```

The first `make` creates `plugins.conf` and builds `./ikcas`.

```sh
make debug       # -O0 -g3 builds
make asan        # tests under AddressSanitizer and UBSan
make clean       # remove build/ and the binaries
make compdb      # regenerate compile_commands.json (needs bear)
```

---

## Quick Start

Start the REPL, or pipe expressions in:

```sh
./ikcas
> 1 + 2 * 3
eval: 7
> 7/2 + 1
eval: 9/2
> (1/2 + 1/3) * 6
eval: 5
> 2*3*x
eval: 6 * x
```

Evaluate one expression and exit:

```sh
./ikcas -e '(1+2)*x + 1'
eval: 3 * x + 1
```

---

## Command Line

```
ikcas [-v[v[v]]] [-p plugin.so]... [--ast] [-e expression]
```

| Flag | Meaning |
| --- | --- |
| `-e EXPR` | Evaluate `EXPR` and exit instead of starting the REPL. |
| `--ast` | Print the parsed tree before the result. |
| `-p FILE` | Load a plugin shared object. Can be repeated. |
| `-v` | Log to stderr. Repeat for more detail. |
| `-h`, `--help` | Show the usage text. |

Verbosity levels:

| Flag | What gets logged |
| --- | --- |
| `-v` | Plugins loaded, each line evaluated. |
| `-vv` | Every registration with its owner, passes run, constant folding. |
| `-vvv` | Every token. |

```sh
./ikcas --ast -e '-(1+x)*3'
*
├── neg
│   └── +
│       ├── 1
│       └── x
└── 3
eval: -(1 + x) * 3
```

---

## Language Reference

Everything here comes from plugins. The list below is what the default build (the `arith` plugin) provides.

### Numbers

Integer literals have any size. Results use the simplest type that holds them:

| Type | Example | Notes |
| --- | --- | --- |
| int | `42` | Arbitrary size. |
| rational | `7/2` | Exact, always in lowest terms. Collapses to an int when it can, so `6/3` is `2`. |
| float | `float(1/3)` | Binary floating point, 128 bits of precision by default. |

Mixing types promotes up: int to rational to float.

```
> 1/3 + 1/6
eval: 1/2
> float(1/3)
eval: 0.33333333333333333333333333333333333333
```

### Operators

From highest to lowest precedence:

| Operator | Meaning | Associativity |
| --- | --- | --- |
| `-x` | Negation | prefix |
| `*` `/` | Multiply, divide | left |
| `+` `-` | Add, subtract | left |

Division is exact. `7/2` is the fraction `7/2`, not `3`. The `extra_ops` plugin adds `^` (see [Plugins](#plugins)).

### Functions

| Function | Meaning |
| --- | --- |
| `float(x)` | Converts to a float. |

Parentheses group, and function arguments are separated by commas.

### Symbols

A name that isn't a registered function or constant is a symbol. Constant parts around it still fold, but nothing is simplified beyond that yet:

```
> 2*3*x
eval: 6 * x
> x*2*3
eval: 6 * x
> x - x
eval: x - x
```

### Errors

Errors point at the offending text and the REPL keeps going:

```
> 1/0
error: division by zero
  1/0
  ^^^
> 2 +* 3
error: '*' cannot start an expression
  2 +* 3
     ^
```

---

## Plugins

Everything the interpreter knows about arithmetic lives in a plugin (`plugins/arith/`). A plugin registers value types, functions, operators, number literals, constants and simplification passes. Adding an operator never touches the lexer or parser.

Three plugins ship with the repository:

| Plugin | Provides | Integrated |
| --- | --- | --- |
| `arith` | int, rational and float types, integer literals, `+ - * /`, prefix `-`, `float()`. | Yes |
| `extra_ops` | Right-associative `^` (int or rational base, int exponent) and `abs()`. An example of a third-party plugin. | No |
| `constants` | Mathematical constants like `pi` | Yes |

> Note: Be careful with editing the order of integrated plugins as that could break the program. It is also not recommended to exclude
Integrated plugins from the program.

### plugins.conf

`make` creates `plugins.conf` on first use and never overwrites it. It lists the plugins that are compiled into the binary, one per line:

```
# one plugin per line; each is a directory under plugins/
arith
```

Each name has to be a directory `plugins/<name>/` with `*.c` files that declare `IKCAS_PLUGIN(<name>, ...)`. Edit the file and run `make`. A name without a directory is a build error. Without `arith` there are no number types or operators.

### Loadable Plugins

Any plugin directory also builds as a shared object:

```sh
make plugin-extra_ops
./ikcas -p build/plugins/extra_ops.so -e '2^100'
eval: 1267650600228229401496703205376
```

```sh
./ikcas -p build/plugins/extra_ops.so -e 'abs(-7/2)^2'
eval: 49/4
```

`ikcas` is linked with `-rdynamic`, so a shared plugin calls the registry and the numeric core straight from the host. A plugin built on `arith` needs `arith` integrated.

### Writing a Plugin

```c
#include "engine/plugin.h"

static cas_status_t my_init(cas_t *cas)
{
    cas_status_t st = registry_add_fn(cas, &fn_fact);
    if (st == CAS_OK)
        st = registry_add_op(cas, &op_fact);
    return st;
}

IKCAS_PLUGIN(my_plugin, "1.0", my_init, NULL);
```

`plugins/extra_ops/extra_ops.c` is a complete small example, and `docs/PLUGINS.md` covers the registry calls, the rules for `eval` and passes, and logging.

---

## How It Works

For one line of input:

```
text -> lexer -> parser -> expression tree -> simplification passes -> result
```

- The lexer and parser read operators, binding powers, number syntax, functions and constants from the registry.
- Every operator application is an application of a function, and syntax is attached to it separately. That is what lets passes work on operators and calls the same way.
- Expressions are immutable. Passes build new trees, so the parse tree is still there after simplification.
- The one pass that ships folds constant subtrees. With symbols present it folds what it can, so `2*3*x` becomes `6 * x`.
- Values are tagged, with a rank that orders the numeric tower. Plugins can add their own types.

Symbolic work (collecting terms, reordering, expansion) is meant to slot in as new passes or per-function rewrite hooks.

---

## Project Layout

Headers are in `inc/<module>/` and sources in `src/<module>/`. Includes are rooted at `inc/`.

```
misc/     allocator.h, arena.h, span.h   memory and source spans
core/     bignum, bigrational, bigfloat  numeric kernel, knows nothing about parsing
expr/     value, op, expr                values, descriptors, expression tree
engine/   diag, registry, cas, plugin    session, extension points, plugin loader
input/    token, lexer, parser           text to expression tree
eval/     simplify                       the pass pipeline
output/   printer                        expression to text or tree dump
app/      main.c                         command line and REPL
plugins/  arith, extra_ops               shipped plugins
tests/    core, frontend and CLI tests
docs/     PLUGINS.md
```

---

## Running Tests

```sh
make test         # core, frontend and CLI golden tests
make asan         # the same under AddressSanitizer and UBSan
make bench        # numeric benchmarks
```

The core tests cover the numeric library. The frontend tests cover the lexer, parser, evaluation, plugins and logging. Each CLI test is a pair `tests/cli/<name>.in` and `tests/cli/<name>.out`, plus an optional `<name>.args` with extra command-line arguments. The runner feeds the `.in` file to `ikcas` and diffs stdout and stderr against the `.out` file. The CLI tests assume `plugins.conf` still lists `arith`.

---

## Contributing

Pull requests and forks are welcome. Please mention this project if you distribute derived work.

---

## License

MIT. See [LICENSE](LICENSE).
