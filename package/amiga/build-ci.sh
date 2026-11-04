#!/bin/sh
# OpenTBBlue for AmigaOS 3.2 (68040), built on a Linux CI runner with the
# OpenAmigaGCC toolchain, OpenNDK and GFOpenGPU's SDL2, all pinned:
#   - DalsinAI/gfopengcc's release (OpenAmigaGCC 16.2.0-open6), sha256 checked
#   - DalsinAI/gfopensdk (OpenNDK headers) at SDK_COMMIT
#   - DalsinAI/gfopenrtg's SDL2 link stub (it loads LIBS:GFOpenGPU/SDL2.module
#     at run time) at RTG_COMMIT
# and, with build-curl.sh, libcurl (built into the program, which downloads
# System/Next itself) and gfopentls.library (HTTPS). Then packs
# OpenTBBlue-<version>-amiga-68040.lha: the OpenTBBlue drawer with its
# GlowIcons, Network/ (gfopentls.library and its CA list), and
# Install-OpenTBBlue beside them.
#
#   package/amiga/build-ci.sh VERSION OUTDIR
set -eu
VERSION=${1:?version}
DIST=${2:?out dir}
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$HERE/../.." && pwd)
WORK=${WORK:-$ROOT/build-amiga}

GCC_TAG=v16.2.0-open6
GCC_TAR=gfopenamigagcc-16.2.0-open6-x86_64-linux.tar.xz
GCC_SHA=65f284476ec20cdf7851475d91d2c18d77fa6f2a0eb0f844b6ca23dbfa57eaa7
SDK_COMMIT=a7384f334c3eb64b30dd27cc5a6f480d72471f74
RTG_COMMIT=8850c2856c79f66280b341346856aacbece06f0c

mkdir -p "$WORK" "$DIST"
DIST=$(CDPATH= cd -- "$DIST" && pwd)   # the archive is made from another folder
cd "$WORK"

# 1. The toolchain.
if [ ! -x gcc/bin/m68k-amigaos-gcc ]; then
    curl -fsSL -o "$GCC_TAR" "https://github.com/DalsinAI/gfopengcc/releases/download/$GCC_TAG/$GCC_TAR"
    echo "$GCC_SHA  $GCC_TAR" | sha256sum -c -
    mkdir -p gcc
    tar xJf "$GCC_TAR" -C gcc --strip-components=1
fi
TC=$WORK/gcc

# 2. OpenNDK and the Team's SDK headers, into the toolchain's include
#    directory.
fetch() {   # fetch REPO COMMIT DIR
    if [ ! -d "$3/.git" ]; then
        git init -q "$3"
        git -C "$3" remote add origin "https://github.com/DalsinAI/$1"
    fi
    git -C "$3" fetch -q --depth 1 origin "$2"
    git -C "$3" checkout -q FETCH_HEAD
}
fetch gfopensdk "$SDK_COMMIT" sdk
fetch gfopenrtg "$RTG_COMMIT" rtg
cp -R sdk/OpenNDK/Include_H/. "$TC/m68k-amigaos/include/"
cp -R sdk/Include/. "$TC/m68k-amigaos/include/"
# OpenNDK names bsdsocket.library's header proto/bsdsocket.h; the usual
# proto/socket.h name points at it.
[ -f "$TC/m68k-amigaos/include/proto/socket.h" ] ||
    printf '#include <proto/bsdsocket.h>\n' > "$TC/m68k-amigaos/include/proto/socket.h"

# 3. The SDL2 link stub (libSDL2.a) and SDL's headers. The stub needs none
#    of the CyberGraphX and AHI headers the module does: empty ones stand in.
NOSDK=$WORK/no-sdk-headers
for f in cybergraphx/cybergraphics.h proto/cybergraphics.h inline/cybergraphics.h \
         clib/cybergraphics_protos.h devices/ahi.h proto/ahi.h inline/ahi.h clib/ahi_protos.h; do
    mkdir -p "$NOSDK/$(dirname "$f")"
    : > "$NOSDK/$f"
done
STOVE=$TC SDK_HEADERS=$NOSDK VASM=true CPU="-m68040" sh rtg/library/modules/sdl2/build.sh "$WORK/sdl" stub
mkdir -p "$WORK/sdl/lib"
cp "$WORK/sdl/libSDL2.a" "$WORK/sdl/lib/"

# 4. libcurl and gfopentls.library (build-curl.sh).
sh "$HERE/build-curl.sh" "$TC" "$WORK/sdk/OpenNDK" "$WORK/net"

# 5. OpenTBBlue, with libcurl in it.
make -C "$ROOT" amiga AMIGA_CC="$TC/bin/m68k-amigaos-gcc" AMIGA_SDL="$WORK/sdl" AMIGA_CURL="$WORK/net"
"$TC/bin/m68k-amigaos-size" "$ROOT/opentbblue.68k"
fpu=$("$TC/bin/m68k-amigaos-objdump" -d "$ROOT/opentbblue.68k" | grep -cE '\sf(add|sub|mul|div|sqrt|cmp|tst|mov[ex]?)[a-z.]*\s' || true)
echo "FPU instructions: $fpu (libnix's startup has two fmovel to fpcr)"


# 6. The archive: the drawer, its icons, the Installer script.
PKG=$WORK/pkg
rm -rf "$PKG"
mkdir -p "$PKG/OpenTBBlue"
cp "$ROOT/opentbblue.68k" "$PKG/OpenTBBlue/OpenTBBlue"
cp "$ROOT/README.md" "$ROOT/RELEASE-NOTES.md" "$ROOT/LICENSE" "$PKG/OpenTBBlue/"
cp "$ROOT/docs/Amiga.md" "$PKG/OpenTBBlue/"
for i in OpenTBBlue README.md Amiga.md RELEASE-NOTES.md LICENSE; do
    cp "$HERE/files/$i.info" "$PKG/OpenTBBlue/"
done
cp "$HERE/files/OpenTBBlue-drawer.info" "$PKG/OpenTBBlue.info"
# the icons the program gives the drawers it makes (make_drawer_icons.py)
mkdir -p "$PKG/OpenTBBlue/Icons"
cp "$HERE"/files/Icons/*.info "$PKG/OpenTBBlue/Icons/"
# the licences of what is built in, and Network/ for the Installer
mkdir -p "$PKG/OpenTBBlue/Licences" "$PKG/Network/Libs" "$PKG/Network/S/GFOpenTLS"
cp "$WORK"/net/Licences/* "$PKG/OpenTBBlue/Licences/"
cp "$WORK/net/gfopentls.library" "$PKG/Network/Libs/"
[ ! -f "$WORK/net/ca-bundle.pem" ] || cp "$WORK/net/ca-bundle.pem" "$PKG/Network/S/GFOpenTLS/"
sed "s/^(set #version \"[^\"]*\")/(set #version \"$VERSION\")/; s/^; \$VER: Install-OpenTBBlue [^ ]*/; \$VER: Install-OpenTBBlue $VERSION/; s/installs OpenTBBlue [0-9][^ ]* for/installs OpenTBBlue $VERSION for/" \
    "$HERE/files/Install-OpenTBBlue" > "$PKG/Install-OpenTBBlue"
cp "$HERE/files/Install-OpenTBBlue.info" "$PKG/"
# PKG_OUT: the unpacked package there too (for a lab); NO_LHA: no archive
if [ -n "${PKG_OUT:-}" ]; then
    mkdir -p "$PKG_OUT" && cp -R "$PKG"/. "$PKG_OUT"/
fi
[ -z "${NO_LHA:-}" ] || exit 0
LHA=$(command -v lha || command -v jlha)
NAME=OpenTBBlue-$VERSION-amiga-68040.lha
rm -f "$DIST/$NAME"
(cd "$PKG" && "$LHA" a "$DIST/$NAME" OpenTBBlue OpenTBBlue.info Network Install-OpenTBBlue Install-OpenTBBlue.info)
"$LHA" l "$DIST/$NAME"
ls -l "$DIST/$NAME"
