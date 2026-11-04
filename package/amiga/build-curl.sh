#!/bin/sh
# The curl command and gfopentls.library for OpenTBBlue's Amiga package:
# the first run (and --update) fetch System/Next with curl over HTTPS, and
# AmigaOS 3.2 has no curl of its own. Everything is built from source here,
# pinned:
#   - DalsinAI/gfopentls at TLS_COMMIT: gfopentls.library (HTTPS, on BearSSL)
#     and S:GFOpenTLS/ca-bundle.pem (Mozilla's CA list, from this machine's
#     ca-certificates package)
#   - DalsinAI/gfopencurl at CURL_COMMIT: libcurl 8.22.0 with GFOpenTLS and
#     HTTP/2, which it builds from curl's release tarball (SHA-256 checked)
#   - curl's own command (curl-8.22.0/src), linked against that libcurl with
#     curl-amiga-net.c (bsdsocket.library for the command)
# 68040, soft float (gfopentls's specs/m68k-soft-m68040.specs).
#
#   package/amiga/build-curl.sh TOOLCHAIN NDK OUTDIR
#     TOOLCHAIN  the unpacked OpenAmigaGCC folder (bin/m68k-amigaos-gcc),
#                with OpenNDK's headers already in its include directory
#     NDK        OpenNDK (its Include_H holds the bsdsocket headers)
#   CA_DIR     (optional) ca-bundle.pem and ca-bundle.LICENSE to use as they
#              are, or none for no CA list
# OUTDIR gets curl, gfopentls.library, ca-bundle.pem and the licences.
set -eu
TC=$(CDPATH= cd -- "${1:?toolchain}" && pwd)
NDK=$(CDPATH= cd -- "${2:?OpenNDK}" && pwd)
OUT=${3:?out dir}
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$HERE/../.." && pwd)
WORK=${WORK:-$ROOT/build-amiga}/curl

TLS_COMMIT=aa6f6a5a34a0558351ece25f29066f51a4e85499
CURL_COMMIT=ed7fe173a57e74dbe83a455f7fdb3ce7bf03d3be
# The upstream tarballs gfopencurl's SOURCES lists, from their GitHub
# releases (the same files; gfopencurl's build.sh checks each SHA-256).
CURL_URL=https://github.com/curl/curl/releases/download/curl-8_22_0/curl-8.22.0.tar.xz
NGHTTP2_URL=https://github.com/nghttp2/nghttp2/releases/download/v1.70.0/nghttp2-1.70.0.tar.xz
ZLIB_URL=https://github.com/madler/zlib/releases/download/v1.3.1/zlib-1.3.1.tar.xz

mkdir -p "$WORK/tarballs" "$OUT"
rm -f "$OUT/ca-bundle.pem" "$OUT/Licences/ca-bundle.txt"
OUT=$(CDPATH= cd -- "$OUT" && pwd)
cd "$WORK"
fetch() {   # fetch REPO COMMIT DIR
    if [ ! -d "$3/.git" ]; then
        git init -q "$3"
        git -C "$3" remote add origin "https://github.com/DalsinAI/$1"
    fi
    git -C "$3" fetch -q --depth 1 origin "$2"
    git -C "$3" checkout -q -f FETCH_HEAD
}
fetch gfopentls "$TLS_COMMIT" gfopentls
fetch gfopencurl "$CURL_COMMIT" gfopencurl
for u in "$CURL_URL" "$NGHTTP2_URL" "$ZLIB_URL"; do
    f=tarballs/$(basename "$u")
    [ -f "$f" ] || curl -fsSL -o "$f" "$u"
done

# gfopencurl's scripts want a stove: a folder whose prefix/ is the toolchain.
# Its gcc is curl/gcc-with-libm.sh: soft float needs libnix's libm at the
# end of each link, which gfopencurl's links (hard float there) leave out.
rm -rf tc && mkdir -p tc/bin stove
for f in "$TC"/*; do [ "$(basename "$f")" = bin ] || ln -s "$f" tc/; done
for f in "$TC"/bin/*; do ln -s "$f" tc/bin/; done
rm tc/bin/m68k-amigaos-gcc
cp "$HERE/curl/gcc-with-libm.sh" tc/bin/m68k-amigaos-gcc
chmod +x tc/bin/m68k-amigaos-gcc
export REAL_GCC="$TC/bin/m68k-amigaos-gcc"
ln -sfn "$WORK/tc" stove/prefix
SPECS=$WORK/gfopentls/specs/m68k-soft-m68040.specs
CPU="-m68040 -specs=$SPECS -mcrt=nix20"

# 1. gfopentls.library and the CA bundle.
STOVE=$WORK/stove OUT=$WORK/tls sh gfopentls/build-amiga-tls.sh > tls.log 2>&1 || { tail -40 tls.log; exit 1; }
# CA_DIR: a folder already holding ca-bundle.pem and ca-bundle.LICENSE
# (for a machine whose ca-certificates make_ca_bundle.py will not take).
rm -rf "$WORK/ca"
# CA_DIR=none: no CA list (where the Amiga has S:GFOpenTLS/ca-bundle.pem).
if [ "${CA_DIR:-}" = none ]; then
    :
elif [ -n "${CA_DIR:-}" ]; then
    mkdir -p "$WORK/ca" && cp "$CA_DIR/ca-bundle.pem" "$CA_DIR/ca-bundle.LICENSE" "$WORK/ca/"
else
    python3 -I gfopentls/tools/make_ca_bundle.py "$WORK/ca" > /dev/null
fi

# 2. libcurl, with nghttp2 and zlib (gfopencurl's own CI build).
TOOLCHAIN_DIR=$WORK/tc OPENNDK=$NDK OPENTLS_SRC=$WORK/gfopentls TARBALLS=$WORK/tarballs \
    OS32_CPU_FLAGS="$CPU" JOBS=${JOBS:-4} sh gfopencurl/tests/amiga/ci-build.sh

# 3. curl's command, in the same CMake tree. Its terminal-width code asks
#    for an ioctl() the Amiga has not got; there the width is COLUMNS or 79.
B=$WORK/gfopencurl/work/curl/build
SRC=$WORK/gfopencurl/work/curl/curl-8.22.0
patch -s -d "$SRC" -p1 < "$HERE/curl/0001-terminal-width-on-amigaos.patch"
"$TC/bin/m68k-amigaos-gcc" -O2 $CPU -idirafter "$NDK/Include_H" -Wall -Wextra \
    -c "$HERE/curl/curl-amiga-net.c" -o "$WORK/curl-amiga-net.o"
(cd "$B" && cmake -DBUILD_CURL_EXE=ON -DCMAKE_C_STANDARD_LIBRARIES="$WORK/curl-amiga-net.o" . > /dev/null 2>&1 &&
    make -j"${JOBS:-4}" curl > "$WORK/curl-tool.log" 2>&1) || { tail -40 "$WORK/curl-tool.log"; exit 1; }
"$TC/bin/m68k-amigaos-strip" -o "$OUT/curl" "$B/src/curl"
"$TC/bin/m68k-amigaos-objdump" -f "$OUT/curl" | grep -q amiga

cp "$WORK/tls/lib/gfopentls.library" "$OUT/"
[ ! -f "$WORK/ca/ca-bundle.pem" ] || cp "$WORK/ca/ca-bundle.pem" "$OUT/"
mkdir -p "$OUT/Licences"
cp "$WORK/gfopencurl/upstream/COPYING" "$OUT/Licences/curl.txt"
cp "$WORK/gfopencurl/upstream/COPYING.nghttp2" "$OUT/Licences/nghttp2.txt"
cp "$WORK/gfopentls/LICENSE" "$OUT/Licences/gfopentls.txt"
cp "$WORK/gfopentls/THIRD_PARTY_NOTICES.md" "$OUT/Licences/gfopentls-third-party.txt"
[ ! -f "$WORK/ca/ca-bundle.LICENSE" ] || cp "$WORK/ca/ca-bundle.LICENSE" "$OUT/Licences/ca-bundle.txt"
ls -l "$OUT" "$OUT/Licences"
