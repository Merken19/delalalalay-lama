#!/bin/bash
# Builds and runs the C tests on macOS: the shared DSP tests and the CoreMIDI
# bridge (Sources/standalone_bridge.c) with the same checks the Windows bridge
# passes. Needs the Xcode Command Line Tools. Usage:
#   Tests/run_macos_tests.sh [output-directory]
# NOTE: written for the macOS port, which has not been run on a Mac by the
# maintainers yet; GitHub Actions runs it in .github/workflows/macos.yml.
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
out="${1:-$root/build/tests}"
mkdir -p "$out"

cc=(xcrun clang -std=c11 -O2 -Wall -Wextra -I"$root/DSP" -I"$root/Sources")
dsp=("$root/DSP/delay.c" "$root/DSP/synth.c" "$root/DSP/voice.c")

run() {
    echo "> $*"
    "$@"
}

run "${cc[@]}" "${dsp[@]}" "$root/Tests/dsp_smoke.c" -o "$out/dsp_smoke"
run "${cc[@]}" "${dsp[@]}" "$root/Tests/windows_smoke.c" -o "$out/dsp_portable_smoke"
run "${cc[@]}" "${dsp[@]}" "$root/Tests/dsp_hardening.c" -o "$out/dsp_hardening"
run "${cc[@]}" "${dsp[@]}" "$root/Sources/standalone_bridge.c" "$root/Tests/bridge_smoke.c" \
    -framework CoreMIDI -framework CoreFoundation -o "$out/bridge_smoke"

for test in dsp_smoke dsp_portable_smoke dsp_hardening bridge_smoke; do
    run "$out/$test"
done

echo "macOS C tests passed."
