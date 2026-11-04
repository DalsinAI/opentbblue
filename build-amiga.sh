#!/bin/sh
# For a build service that sets STOVE= and OUT= (target .): the Amiga
# package unpacked into OUT, ready to copy onto a test Amiga: OpenTBBlue/ (the program, its
# GlowIcons, curl), OpenTBBlue.info, Network/ (gfopentls.library and, where
# this machine's ca-certificates allow, its CA list) and Install-OpenTBBlue.
# The toolchain, OpenNDK and SDL2 stub are package/amiga/build-ci.sh's own
# pins, fetched into build-amiga/, so STOVE is not written to. No archive.
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
OUT=${OUT:?OUT}
mkdir -p "$OUT"
OUT=$(CDPATH= cd -- "$OUT" && pwd)
# make_ca_bundle.py reads Debian's ca-certificates; elsewhere the Amiga's own
# S:GFOpenTLS/ca-bundle.pem is used
if ! command -v dpkg >/dev/null 2>&1; then
    export CA_DIR=${CA_DIR:-none}
fi
VERSION=$(sed -n 's/^(set #version "\([^"]*\)")/\1/p' "$HERE/package/amiga/files/Install-OpenTBBlue")
NO_LHA=1 PKG_OUT="$OUT" sh "$HERE/package/amiga/build-ci.sh" "$VERSION" "$HERE/build-amiga/dist"
(cd "$OUT" && find . -type f | sort) > "$OUT/FILES.txt"
cat "$OUT/FILES.txt"
