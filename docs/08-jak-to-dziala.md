# 8. Jak to działa w środku

Ten rozdział jest dla osób, które chcą zmieniać kod albo zrozumieć, dlaczego coś działa
tak, a nie inaczej. Do używania strażnika nie jest potrzebny.

## Obchód

Co 30 sekund (ustawialne) strażnik sprawdza po kolei wszystkie cele:

- **ping:** 3 próby, wystarczy jedna odpowiedź, bo Wi-Fi gubi pojedyncze pakiety;
- **tcp:** samo nawiązanie połączenia na porcie;
- **http/https:** pełne zapytanie i kod odpowiedzi; przy https także data ważności
  certyfikatu;
- **dns:** własne zapytanie UDP o podaną nazwę do wskazanego serwera.

Każde sprawdzenie ma limit czasu (3 s, a dla http 6 s). Cel staje się **awarią** po
3 porażkach z rzędu. Po obchodzie strażnik zbiera wszystkie zmiany w **jedną**
wiadomość. Cele, których rodzic albo zależność (`needs`) leży, nie wysyłają osobnych
alarmów; licznik „zależnych od niego: N” trafia do wiadomości rodzica.

## Budowa programu

PlatformIO, Arduino-ESP32 (rdzeń 2.0.17), biblioteki ArduinoJson i PubSubClient. Kilka
zadań FreeRTOS działa równolegle: obchód, Telegram, MQTT, panel WWW i restart serwera.
Wspólne dane chroni jedna blokada (`gLock`).

| Plik | Rola |
|---|---|
| `main.cpp` | start, Wi-Fi z siecią awaryjną, DNS, zegar (NTP), mDNS, watchdog, zadania |
| `config.h` | wersja, czasy, wartości domyślne, strefa czasowa |
| `secrets.h` | hasła (tylko lokalnie, wzór w `secrets.h.example`) |
| `state.h` | ustawienia (pamięć NVS), lista celów (`/targets.json` w LittleFS) z domyślną listą, wyciszenia (`/mutes.json`), dziennik |
| `net.h` | bezpieczne rozwiązywanie nazw, minimalny klient HTTP/1.1 |
| `probes.h` | sprawdzanie: ping, tcp, http(s), dns |
| `monitor.h` | obchody, stany, zależności, decyzja o alarmach, dioda, „żyję” |
| `telegram.h` | kolejka wiadomości, wysyłka z przypiętym certyfikatem, komendy, przyciski |
| `mqtt.h` | MQTT i automatyczne wykrywanie w Home Assistancie |
| `ipmi.h` | klient IPMI 2.0 / RMCP+ (port `tools/ipmi_proto.py`) |
| `rescue.h` | awaryjny restart serwera, tryb testowy, symulacja |
| `webui.h` | panel WWW i API, aktualizacja OTA |
| `ui_html.h` | cała strona panelu w jednym pliku (HTML, CSS, JS, bez zewnętrznych zasobów) |
| `ca_telegram.h` | certyfikat główny Go Daddy G2 dla `api.telegram.org` |

### API panelu

Wszystko za tym samym hasłem co panel (HTTP Basic, login `admin`):

| Metoda i adres | Co robi |
|---|---|
| `GET /api/status` | stan wszystkich celów i ustawień |
| `GET /api/targets` | lista celów (JSON, jak w edytorze) |
| `POST /api/targets` | zapis całej listy (`Content-Type: application/json`) |
| `GET /api/target?id=…` | jeden cel z historią czasów odpowiedzi |
| `GET /api/log`, `GET /api/tglog` | dziennik zdarzeń i wiadomości Telegram |
| `POST /api/action?do=sweep` | obchód teraz; inne akcje: `mute`, `unmute`, `enable`, `disable`, `test`, `reboot` |
| `POST /api/settings` | zmiana ustawień; zmienia tylko przesłane pola |
| `POST /update` | wgranie nowego programu (formularz z plikiem `firmware`) |

Przykład:

```bash
curl -u admin:HASLO http://straznik.local/api/targets > cele.json
curl -u admin:HASLO -H 'Content-Type: application/json' --data @cele.json http://straznik.local/api/targets
```

### Podgląd panelu bez płytki

`python3 tools/panel_demo.py` uruchamia panel w przeglądarce na `http://localhost:8000`
z wymyślonymi danymi. Przydaje się przy zmianach w `ui_html.h`. Zrzuty w `docs/img/`
odświeża `python3 tools/panel_screenshots.py` (wymaga `pip install playwright`
i `playwright install chromium`).

## Decyzje projektowe

| Decyzja | Dlaczego | Odrzucona alternatywa |
|---|---|---|
| Własny program zamiast ESPHome | ma działać, gdy Home Assistant leży; zależności, kolejka i komendy są w ESPHome trudne | ESPHome |
| ESP32-S3 z PSRAM | szyfrowane połączenie z Telegramem potrzebuje dużo pamięci | ESP8266: za mało pamięci, zawieszał się |
| Osobny bot Telegram | wyciszenie strażnika nie wycisza innych powiadomień | wspólny bot z innymi systemami |
| Przypięty certyfikat Telegrama (ważny do 2037) | token bota nie wycieknie przez podsłuch w sieci | połączenie bez weryfikacji |
| Domowe https sprawdzane bez weryfikacji certyfikatu | domowe serwery mają często własne certyfikaty; liczy się, że odpowiadają | pełna weryfikacja |
| Własne DNS 1.1.1.1 i 8.8.8.8 | domowy DNS (np. AdGuard) jest jednym z pilnowanych celów i nie może oślepić strażnika | DNS z routera |
| Alarm po 3 porażkach | mniej fałszywych alarmów kosztem ok. 1,5 min opóźnienia | alarm od razu |
| Jedna wiadomość na obchód | awaria serwera to jedna wiadomość, a nie piętnaście | wiadomość na cel |

## Znane pułapki (już rozwiązane w kodzie)

- **Rozwiązywanie nazw w rdzeniu Arduino 2.0.17.** `WiFi.hostByName()` i podawanie
  nazwy do `connect()`/`HTTPClient` wywraca program przy kilku zadaniach naraz (błąd
  w tablicy DNS biblioteki lwIP). Dlatego program używa `net.h::resolveName()`
  (`getaddrinfo`), łączy się po IP, a nazwę podaje tylko do SNI. Nie wracaj do
  `hostByName` ani `HTTPClient`.
- **Uzgadnianie TLS domyślnie czeka do 120 s**, dłużej niż watchdog (60 s). Stąd
  `setHandshakeTimeout(10)` dla Telegrama i 8 s dla celów https.
- **Strony za reverse proxy wymagają nazwy (SNI)**, inaczej proxy odrzuca połączenie.
- **`time_t` ma tu 32 bity.** „Do odwołania” to `2147000000` (rok 2038); większa
  liczba się przekręca.
- **Odnowienie adresu z DHCP przywraca DNS routera**, więc strażnik co minutę ustawia
  swoje serwery DNS ponownie.
- **Otwarcie portu szeregowego restartuje płytkę** (mostek USB z automatycznym
  resetem).
- **Czarna skrzynka:** każde zadanie zapisuje swój etap w pamięci, która przeżywa
  reset (`RTC_NOINIT`), więc po restarcie od watchdoga wiadomo, gdzie program stanął.
  Test: `POST /api/action?do=wdttest` celowo wiesza panel na 70 s.
