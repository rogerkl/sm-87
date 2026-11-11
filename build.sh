#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Roger Klaveness
# SM-87's build: fm1-base's tools/build.py on app/, its options passed on (as fm1-base's template has it).
#   ./build.sh                       the firmware: app/build/sm87.fwsc (JieLi's toolchain and AC79 SDK: README)
#   ./build.sh --release 0.1         a release: app/build/sm87-0.1.fwsc
#   ./build.sh --host | --web        the host simulator (app/build/host) | the browser emulator (app/build/emu)
# Python with Pillow and numpy: .venv here if there is one, else PYTHON or python3.
set -e
cd "$(dirname "$0")"
PY="${PYTHON:-}"
[ -n "$PY" ] || { [ -x .venv/bin/python ] && PY="$(pwd)/.venv/bin/python"; } || true
[ -n "$PY" ] || PY=python3
cd fm1-base
exec "$PY" tools/build.py ../app "$@"
