#!/usr/bin/env bash
# Translates crash addresses from a Telegram "Zrzut awarii" message into source lines.
# Usage: tools/decode_crash.sh <firmware-version> 0x42049c47 0x42040005 ...
# Needs the ELF kept by firmware/build.sh in firmware/build/elf/ (same version as on the device).
set -euo pipefail
cd "$(dirname "$0")/.."
VER=$1; shift
ELF=firmware/build/elf/straznik-$VER.elf
[ -f "$ELF" ] || { echo "Brak $ELF — zbuduj tę wersję przez firmware/build.sh"; exit 1; }
"$HOME/.platformio/packages/toolchain-xtensa-esp32s3/bin/xtensa-esp32s3-elf-addr2line" -pfiaC -e "$ELF" "$@" \
  | sed 's#/home/runner/work/esp32-arduino-lib-builder/esp32-arduino-lib-builder/##'
