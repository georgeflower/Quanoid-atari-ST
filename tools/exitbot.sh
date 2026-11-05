#!/bin/sh
# Exit-door bot: plays a level in Hatari, sits out the 60 s exit countdown,
# catches the "?" capsule and leaves through the door on the right.
#   sh tools/exitbot.sh                          # level 1, no life lost
#   sh tools/exitbot.sh -DTEST_START=4           # 0-based start level
#   sh tools/exitbot.sh -DEXITBOT_DIE=10         # lose a life 10 s into the countdown
#   sh tools/exitbot.sh -DEXITBOT_MISSCAP        # let the first "?" fall past
# Prints the bot's log, copies the door screenshot to build/exitbot.PI1
# (tools/pi1png.py converts it) and exits 0 on PASS.
set -e
cd "$(dirname "$0")/.."
D=$(mktemp -d)
mkdir -p build
rm -f build/main_exitbot.o build/main_exitbot.elf build/QEXITBOT.PRG build/exitbot.PI1
make build/QEXITBOT.PRG EXITBOT_FLAGS="$*" >/dev/null
cp build/QEXITBOT.PRG "$D/QUANOID.PRG"
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy timeout "${EXITBOT_TIMEOUT:-900}" \
    hatari --tos roms/etos512se.img --machine st --monitor rgb --memsize 1024 \
    --natfeats true --sound off --statusbar false --fast-forward true --confirm-quit false \
    --harddrive "$D" --auto 'C:\QUANOID.PRG' >"$D/log" 2>&1 &
H=$!
while kill -0 $H 2>/dev/null && ! grep -q 'EXITBOT \(PASS\|FAIL\)' "$D/log"; do sleep 2; done
sleep 1
kill $H 2>/dev/null || true
wait $H 2>/dev/null || true
grep -E 'EXITBOT|EXITTIMER|^LEVEL|SHOT .*exitbot' "$D/log" || true
S=$(grep 'SHOT .*exitbot door open' "$D/log" | awk '{print $2}')
[ -n "$S" ] && cp "$D/$S" build/exitbot.PI1
R=0; grep -q 'EXITBOT PASS' "$D/log" || R=1
rm -rf "$D"
exit $R
