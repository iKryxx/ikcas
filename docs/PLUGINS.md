# Writing ikcas plugins

Everything the interpreter knows about arithmetic lives in a plugin
(`plugins/arith/`). A plugin is a descriptor whose `init` registers
**types, functions, operators, literals, constants and passes** with the
session. The same sources can be compiled into the `ikcas` binary or built as
a shared object and loaded at run time.

```c
#include "engine/plugin.h"

static cas_status_t my_init(cas_t *cas)
{
    cas_status_t st = registry_add_fn(cas, &fn_fact);
    if (st == CAS_OK)
        st = registry_add_op(cas, &op_fact);
    return st;
}

IKCAS_PLUGIN(my_plugin, "1.0", my_init, NULL /* shutdown */);
```

`plugins/extra_ops/extra_ops.c` is a complete small example (`^` and `abs`).
`tests/test_plugin.c` shows a postfix operator, a constant and a rewrite pass.

## What can be registered

| Call | Descriptor | Purpose |
| --- | --- | --- |
| `registry_add_type` | `value_type_t` | A kind of value (int, rational, float, ...). `rank` orders the numeric tower; mixed operands are promoted to the higher rank. |
| `registry_add_fn` | `fn_def_t` | A callable: name, arity range, `eval` that folds constant arguments, optional `rewrite`, algebraic `attrs`. |
| `registry_add_op` | `op_def_t` | Syntax for a function: symbol, prefix/infix/postfix, precedence, associativity. |
| `registry_add_literal` | `literal_def_t` | Number syntax: `match` returns the length of a literal at the cursor, `make` builds the value. Longest match wins. |
| `registry_add_constant` | `constant_def_t` | A named value such as `pi`. |
| `registry_add_pass` | `pass_def_t` | A step of the simplification pipeline. Passes run in registration order; `fold_constants` is first, then comes `collect_assoc` and so on. |

Operators never touch the parser: it reads binding powers from the registry.
A higher `precedence` binds tighter. Current levels: `+ -` 10, `* /` 20,
prefix `-` 30, `^` 40. Pick a number between existing ones to slot in.

Identifiers that are not registered functions or constants become **symbols**;
`2*3*x` folds to `6 * x`. Functions and operators are the same thing: every
operator application is an `EXPR_APPLY` of a `fn_def_t`, which is what rewrite
passes pattern-match on.

## Rules for `eval`, kernels and passes

* `eval(cas, args, n, out)` receives borrowed argument values and writes an
  **uninitialized** `out`. Do not destroy the arguments. Return a
  `cas_status_t`; report problems with `cas_error(cas, CAS_ERR_DOMAIN, "...")`
  (the span of the offending expression is filled in for you).
* Value kernels (`value_type_t.binop`) choose their own output type, so
  `int / int` can give a rational and `rational + rational` can collapse to an
  int.
* Values that own heap memory must be destroyed by their type's `destroy`.
  `expr_new_const` takes ownership and arranges that when the scratch arena is
  reset.
* Passes must not modify the tree they receive. Use `expr_map` (bottom-up
  rewrite) and the `expr_new_*` constructors; unchanged subtrees are shared.
* Descriptors are stored by pointer: keep them `static const` (or otherwise
  alive until `cas_free`).

## Integrated plugins: `plugins.conf`

`make` creates `plugins.conf` on first use (it never overwrites it):

```
# one plugin per line; each is a directory under plugins/
arith
```

Every listed name must be a directory `plugins/<name>/` containing `*.c` files
that declare `IKCAS_PLUGIN(<name>, ...)`. `make` compiles them into the
binary and generates `build/gen/builtin_plugins.c`, whose `cas_load_builtin()`
registers them at startup. Editing `plugins.conf` and running `make` is all it
takes; a name without a directory is a build error.

The default build integrates `arith`. Without it there are no number types or
operators.

## Loadable plugins

Any plugin directory also builds as a shared object:

```sh
make plugin-extra_ops                       # -> build/plugins/extra_ops.so
./ikcas -p build/plugins/extra_ops.so       # repeatable
```

A shared object exports one data symbol, `ikcas_plugin` (the `IKCAS_PLUGIN`
macro emits it when `IKCAS_PLUGIN_SHARED` is defined; the Makefile sets that
for `.so` builds). The loader checks `api_version` against
`IKCAS_PLUGIN_API_VERSION` and rejects mismatches. `ikcas` is linked with
`-rdynamic`, so a plugin calls `registry_*`, `bignum_*` and the `arith_*`
helpers (`plugins/arith/arith.h`) straight from the host. A plugin built on
`arith` therefore needs it integrated.

Plugins are shut down in reverse load order and their shared objects are
closed after the registry is freed.

## Logging

`ikcas -v` logs to stderr; repeat for more detail:

| Flag | Level | What is logged |
| --- | --- | --- |
| `-v` | `LOG_INFO` | plugins loaded, each line evaluated |
| `-vv` | `LOG_DEBUG` | every registration with its owner, passes run, constant folds |
| `-vvv` | `LOG_TRACE` | every token |

Plugins log with `CAS_LOG(cas, LOG_DEBUG, "fmt", ...)` (`engine/diag.h`); the
arguments are only evaluated when the level is enabled. Log lines are separate
from error diagnostics: they never end up in `cas->diags`. An embedder sets
`cas_options_t.verbosity` and may redirect output with `cas_set_log_sink`.

## Where symbolic work goes

The expression tree and the pass pipeline are the extension points for
simplification:

* write a `pass_def_t` that rewrites with `expr_map` (collect like terms,
  canonical ordering, expansion);
* give a `fn_def_t` a `rewrite` hook and `attrs` (`FN_COMMUTATIVE`,
  `FN_ASSOCIATIVE`) for a generic pass to use;
* add a value type (complex numbers, matrices) with a higher `rank` and
  `promote_from`; `+ - * /` then work on it through `value_arith`.
