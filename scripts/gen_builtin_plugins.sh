#!/bin/sh
# usage: gen_builtin_plugins.sh plugins.conf > builtin_plugins.c
# Generates cas_load_builtin(), which registers every plugin listed in
# plugins.conf.
set -e # stop at the first failure, so a broken conf never yields a half-written file
dir=$(dirname "$0")
names=$("$dir/plugin_list.sh" "$1")

echo "/* Generated from $1 by scripts/gen_builtin_plugins.sh. Do not edit. */"
echo '#include "engine/plugin.h"'
echo
for n in $names; do
    echo "extern const plugin_desc_t ikcas_plugin_$n;"
done
echo
echo 'static const plugin_desc_t *const builtin_plugins[] = {'
for n in $names; do
    echo "    &ikcas_plugin_$n,"
done
echo '    NULL,'
echo '};'
echo
cat <<'C'
cas_status_t cas_load_builtin(cas_t *cas)
{
    for (const plugin_desc_t *const *p = builtin_plugins; *p; p++) {
        cas_status_t st = cas_register_plugin(cas, *p);
        if (st != CAS_OK)
            return st;
    }
    return CAS_OK;
}
C
