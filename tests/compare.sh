#!/bin/sh
# compare.sh OLD-FRAMES NEW-FRAMES CARD
#
# Runs five scenarios through two builds of tests/frames (NextZXOS and
# NextBASIC, the tilemap demo, Warhawk, show512, Spectron 2084) on fresh
# copies of a System/Next card, and checks that every 50th frame's picture
# and the sound so far hash the same. Prints each build's time.
set -u
OLD=$1 NEW=$2 CARD=$3
T=${TMPDIR:-/tmp}/otb-compare.$$
mkdir -p "$T"
fail=0
run() {   # run BIN TAG SCENARIO
    rm -rf "$T/sd"; cp -a "$CARD" "$T/sd"
    case $3 in
    basic)    $1 "$T/sd" 2000 '1000: ' '1150:n' '1350:10 for i=1 to 20: print i,i*i: next i\n' '1700:run\n' ;;
    tm)       $1 "$T/sd" 2500 '1000: ' '1150:n' '1350:.nexload demos/tilemap/tm.nex\n' ;;
    warhawk)  $1 "$T/sd" 3000 '1000: ' '1150:n' '1350:.nexload games/Next/Warhawk/Warhawk.nex\n' '2200: ' ;;
    show512)  $1 "$T/sd" 2500 '1000: ' '1150:n' '1350:.nexload demos/show512/show512.nex\n' ;;
    spectron) $1 "$T/sd" 2800 '1000: ' '1150:n' '1350:.nexload "demos/Spectron 2084/Spectron2084.nex"\n' '2200: ' ;;
    esac > "$T/$2-$3.txt"
}
for tag in old new; do
    bin=$OLD; [ $tag = new ] && bin=$NEW
    s=$(date +%s)
    for sc in basic tm warhawk show512 spectron; do run "$bin" $tag $sc; done
    echo "$tag: $(( $(date +%s) - s )) s"
done
for sc in basic tm warhawk show512 spectron; do
    if cmp -s "$T/old-$sc.txt" "$T/new-$sc.txt"; then echo "$sc: same pictures and sound"
    else echo "$sc: DIFFERENT"; fail=1; fi
done
rm -rf "$T"
exit $fail
