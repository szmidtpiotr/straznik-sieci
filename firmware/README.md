# Strażnik sieci — firmware

PlatformIO / Arduino-ESP32 (rdzeń 2.0.17) dla ESP32-S3-DevKitC-1 N16R8.

## Budowa i wgrywanie

```bash
cp src/secrets.h.example src/secrets.h   # i uzupełnij hasła
pio run -t upload                        # pierwszy raz: kablem USB (gniazdo UART)
./build.sh                               # obrazy .bin + .elf w build/
./build.sh ota straznik.local            # aktualizacja przez sieć
```

Pełna instrukcja krok po kroku: [`../docs/02-instalacja.md`](../docs/02-instalacja.md).
Opis plików i API: [`../docs/08-jak-to-dziala.md`](../docs/08-jak-to-dziala.md).

`build/straznik-usb-full.bin` to pełny obraz do wgrania od adresu `0x0` (np. przez
`esptool` albo przeglądarkowy flasher), `build/straznik-ota.bin` to plik do
aktualizacji przez `/update`. Oba zawierają hasła z `secrets.h`, więc nie udostępniaj ich.
