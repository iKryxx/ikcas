#!/bin/sh
# Writes the default plugins.conf to stdout. The Makefile runs this once, when
# plugins.conf does not exist yet, and never overwrites an existing file.
cat <<'CONF'
# Plugins compiled into the ikcas binary, one per line.
#
# Each name is a directory under plugins/ whose *.c files declare the plugin
# with IKCAS_PLUGIN(<name>, ...). Lines starting with '#' and blank lines are
# ignored. Edit this file and run `make` to change what is integrated;
# plugins/arith provides the number types and the + - * / operators; without
# it ikcas cannot evaluate numbers.
#
# Plugins that are not listed here can still be loaded at run time:
#   make plugin-extra_ops && ikcas -p build/plugins/extra_ops.so
arith
CONF
