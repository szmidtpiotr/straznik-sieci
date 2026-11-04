#!/usr/bin/env bash
# Builds the firmware with a local PlatformIO and puts the images in firmware/build/:
#   straznik-usb-full.bin — first flash over USB at offset 0x0 (bootloader + partitions + app)
#   straznik-ota.bin      — later updates through http://<ip>/update
#   elf/straznik-<ver>.elf — kept for tools/decode_crash.sh
#
# Usage: ./build.sh            build only
#        ./build.sh ota <ip>   build and upload over the network (asks for the panel password)
set -euo pipefail
cd "$(dirname "$0")"

PIO=$(command -v pio || echo "$HOME/.platformio/penv/bin/pio")
[ -x "$PIO" ] || { echo "Nie znaleziono PlatformIO (pio). Instrukcja: docs/02-instalacja.md"; exit 1; }
[ -f src/secrets.h ] || { echo "Brak src/secrets.h — skopiuj src/secrets.h.example i uzupełnij"; exit 1; }

"$PIO" run -s
OUT=.pio/build/esp32s3
BA=$(find "$HOME/.platformio/packages/framework-arduinoespressif32/tools/partitions" -name boot_app0.bin | head -1)
ESPTOOL="$HOME/.platformio/packages/tool-esptoolpy/esptool.py"
PY="$HOME/.platformio/penv/bin/python"
mkdir -p build/elf
"$PY" "$ESPTOOL" --chip esp32s3 merge_bin -o build/straznik-usb-full.bin \
  --flash_mode keep --flash_freq keep --flash_size keep \
  0x0 "$OUT/bootloader.bin" 0x8000 "$OUT/partitions.bin" 0xe000 "$BA" 0x10000 "$OUT/firmware.bin" >/dev/null
cp "$OUT/firmware.bin" build/straznik-ota.bin
VER=$(grep -oP 'FW_VERSION\s+"\K[^"]+' src/config.h)
cp "$OUT/firmware.elf" "build/elf/straznik-$VER.elf"
ls -l build/*.bin

if [ "${1:-}" = ota ]; then
  IP=${2:?podaj adres strażnika, np. ./build.sh ota straznik.local}
  read -rsp "Hasło panelu: " PASS; echo
  curl -fsS -u "admin:$PASS" -F "firmware=@build/straznik-ota.bin" "http://$IP/update" | head -c 200; echo
fi
