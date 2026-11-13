#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Roger Klaveness
# SM-87's host tests: the C unit tests, then the app's scripts in fm1-base's host simulator.
#   test/run.sh            (from the repository's root; Python with Pillow and numpy: .venv, or PYTHON=...)
# Axs7's test fixtures (its web/test/fixtures: a real X7000's dumps and disk images) come from an Axs7 checkout beside
# this one, ../Axs7SdsW, or AXS7=...
set -e
cd "$(dirname "$0")/.."
PY="${PYTHON:-$(pwd)/.venv/bin/python}"
CC="${CC:-cc}"
FIXTURES="${AXS7:-../Axs7SdsW}/web/test/fixtures"
if [ ! -d "$FIXTURES" ]; then
    echo "no $FIXTURES: Axs7's test fixtures (clone github.com/rogerkl/Axs7 there, or set AXS7)"
    exit 1
fi
mkdir -p build/test
CFLAGS="-O2 -ffp-contract=off -std=c99 -Wall -Wextra -Wno-unused-function -Iapp/s700 -Iapp/mmt8 -Iapp/hr16 -Ifm1-base/include"
for t in test/*_test.c; do
    n=$(basename "$t" .c)
    extra=""
    [ -f "test/$n.deps" ] && extra=$(cat "test/$n.deps")
    $CC $CFLAGS -o "build/test/$n" "$t" app/s700/s7engine.c $extra -lm
    "build/test/$n" "$FIXTURES"
done
"$PY" -I test/gen_sysex_test.py
# samples put into a package: tools/mkrom.py against fm1pkg_make.py's packages, then web/sm87bank.js against mkrom.py's
"$PY" -I test/bank_test.py build/test/bank
if command -v node >/dev/null; then
    node test/bank_js_test.mjs build/test/bank
fi
# the scripts expect the X7000's empty memory: the host build's samples built in (app/rom/emu_bank.h, tools/mkrom.py
# --emu) are set aside meanwhile, and put back however this ends. (The drum kit, resources/kit, is the build's: always
# in)
ROM=app/rom/emu_bank.h
if [ -f "$ROM" ]; then
    mv "$ROM" build/test/emu_bank.h.yours
    trap 'mv build/test/emu_bank.h.yours "$ROM"' EXIT
fi
build_host() { (cd fm1-base && "$PY" tools/build.py ../app --host >../build/test/host_build.log 2>&1) || { cat build/test/host_build.log; exit 1; }; }
run_scripts() {
    for s in "$@"; do
        app/build/host/sm87_host "$s" app/build/host | grep -E "FAIL|fail|^host:" || true
        app/build/host/sm87_host "$s" app/build/host >/dev/null || { echo "FAILED: $s"; exit 1; }
    done
}
build_host
# (fx.txt only with the effect built in: app/config.h SM87_FX)
if grep -q '^#define SM87_FX 0' app/config.h; then
    run_scripts $(ls app/tests/*.txt | grep -v '/fx\.txt$')
else
    run_scripts app/tests/*.txt
fi
# samples built in: a disk image and a WAV, from the fixtures, and a kit of the WAV (app/tests/rom/bankkit.txt)
"$PY" -I test/make_test_wav.py build/test/sine440.wav
printf 'sound BEEP sine440.wav vel=20\nsound BOP sine440.wav\npad KICK BEEP note=36\npad HIGH BEEP tune=7 level=50 pan=-2 note=40 group=1 poly=2\n' >build/test/kit.txt
"$PY" -I tools/mkrom.py --emu "$FIXTURES/s700.qd.gz@1" build/test/sine440.wav@2 \
    --kit build/test/kit.txt >/dev/null
build_host
run_scripts app/tests/rom/*.txt
rm -f "$ROM"
build_host
# the browser emulator as built (fm1-base tools/build.py ../app --web, with Emscripten), if there is one
if [ -f app/build/emu/app.wasm ]; then
    node test/emu_test.mjs app/build/emu | grep -E "FAIL|passed|failed"
    node test/emu_test.mjs app/build/emu >/dev/null || { echo "FAILED: test/emu_test.mjs"; exit 1; }
fi
echo "all tests passed"
