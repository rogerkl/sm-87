#!/bin/sh
# SM-87's host tests: the C unit tests, then the app's scripts in fm1-base's host simulator.
#   test/run.sh            (from the repository's root; Python with Pillow and numpy: .venv, or PYTHON=...)
set -e
cd "$(dirname "$0")/.."
PY="${PYTHON:-$(pwd)/.venv/bin/python}"
CC="${CC:-cc}"
mkdir -p build/test
CFLAGS="-O2 -ffp-contract=off -std=c99 -Wall -Wextra -Wno-unused-function -Iapp/s700 -Iapp/mmt8 -Ifm1-base/include"
for t in test/*_test.c; do
    n=$(basename "$t" .c)
    extra=""
    [ -f "test/$n.deps" ] && extra=$(cat "test/$n.deps")
    $CC $CFLAGS -o "build/test/$n" "$t" app/s700/s7engine.c $extra -lm
    "build/test/$n" ../Axs7SdsW/web/test/fixtures
done
"$PY" -I test/gen_sysex_test.py
# the scripts expect the X7000's empty memory: samples built in (app/rom/rom_samples.h, tools/mkrom.py) are set
# aside meanwhile, and put back however this ends
ROM=app/rom/rom_samples.h
if [ -f "$ROM" ]; then
    mv "$ROM" build/test/rom_samples.h.yours
    trap 'mv build/test/rom_samples.h.yours "$ROM"' EXIT
fi
build_host() { (cd fm1-base && "$PY" tools/build.py ../app --host >../build/test/host_build.log 2>&1) || { cat build/test/host_build.log; exit 1; }; }
run_scripts() {
    for s in "$@"; do
        app/build/host/sm87_host "$s" app/build/host | grep -E "FAIL|fail|^host:" || true
        app/build/host/sm87_host "$s" app/build/host >/dev/null || { echo "FAILED: $s"; exit 1; }
    done
}
build_host
run_scripts app/tests/*.txt
# samples built in: a disk image and a WAV, from the fixtures
"$PY" -I test/make_test_wav.py build/test/sine440.wav
"$PY" -I tools/mkrom.py ../Axs7SdsW/web/test/fixtures/s700.qd.gz@1 build/test/sine440.wav@2 >/dev/null
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
