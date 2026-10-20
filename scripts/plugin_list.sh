#!/bin/sh
# Prints the plugin names from a plugins.conf file, one per line.
sed -e 's/#.*//' "$1" | awk 'NF { print $1 }'
