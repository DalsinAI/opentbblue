#!/bin/sh
# m68k-amigaos-gcc for gfopencurl's soft-float build: a link gets libnix's
# libm (the double maths) at its end, which gfopencurl's own links (hard
# float there) leave out. REAL_GCC is the real compiler.
for a; do
    case "$a" in
        -c|-E|-S|-M|-MM|--version|-v|-dumpversion|-dumpmachine|-print-*) exec "$REAL_GCC" "$@" ;;
    esac
done
exec "$REAL_GCC" "$@" -lm
