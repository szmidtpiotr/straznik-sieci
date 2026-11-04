# 7. Gdy coś nie działa

Pierwsze miejsce do sprawdzenia: **panel → Dziennik**. Strażnik zapisuje tam prawie
wszystko, co robi. Drugie: **podgląd komunikatów** po kablu USB (rozdział 2, krok 5).

## Wgrywanie i start

| Objaw | Przyczyna i rozwiązanie |
|---|---|
| Budowanie kończy się błędem `wpisz PANEL_PASS` albo `SETUP_AP_PASS musi miec co najmniej 8 znakow` | brak haseł w `firmware/src/secrets.h` (rozdział 2, krok 2) |
| `secrets.h: No such file or directory` | nie skopiowałeś `secrets.h.example` do `secrets.h` albo plik leży w złym katalogu (ma być w `firmware/src/`) |
| Komputer nie widzi płytki | kabel tylko do ładowania (wymień), brak sterownika CH343/CP210x (Windows), brak grupy `dialout` (Linux) |
| Wgrywanie stoi na `Connecting....` | przytrzymaj BOOT, wciśnij i puść RST, puść BOOT, wgraj ponownie |
| Płytka restartuje się w kółko, w komunikatach `PSRAM` albo `flash` | inna wersja płytki niż N16R8; zobacz „Inna wersja płytki” niżej |
| Podgląd komunikatów pusty | podłącz do gniazda **UART**, nie **USB**; prędkość 115200 |

## Wi-Fi i panel

| Objaw | Przyczyna i rozwiązanie |
|---|---|
| Nie łączy się z Wi-Fi | sieć 5 GHz (ESP32 widzi tylko 2,4 GHz), literówka w haśle, za słaby sygnał. Po minucie pojawi się sieć „Straznik-Setup”, przez którą poprawisz dane (rozdział 2, krok 6) |
| `straznik.local` się nie otwiera | wpisz adres IP; Android i niektóre sieci nie obsługują nazw `.local`. IP pokazuje router, podgląd komunikatów i `/test` w Telegramie |
| Panel pyta o hasło w kółko | login to zawsze `admin`, hasło to `PANEL_PASS` z chwili budowania programu |
| Dioda nie świeci, choć reszta działa | niektóre wersje płytki mają diodę RGB na innym pinie (GPIO38 zamiast GPIO48). Na pracę strażnika to nie wpływa |
| Częste „Strażnik nie miał Wi-Fi” | słaby sygnał (panel → Ustawienia → System, „Wi-Fi” poniżej −75 dBm); przenieś strażnika albo punkt dostępowy |

## Telegram

| Objaw | Przyczyna i rozwiązanie |
|---|---|
| Nic nie przychodzi | panel → Ustawienia → Telegram: musi być „podłączony · czat …”. Jeśli „brak czatu”, napisz do bota i kliknij „To mój czat” (rozdział 3) |
| W panelu „Telegram: brak czatu” na pomarańczowo | strażnik ma token, ale nie wie, do kogo pisać; jak wyżej |
| Wiadomości czekają w kolejce | brak internetu albo błędny token. Dziennik → wiadomości Telegram pokazuje błąd przy każdej próbie |
| Bot nie reaguje na komendy | piszesz z innego konta albo czatu niż „Twój czat” |
| Bot odpowiada „Nie znam celu” | podany identyfikator nie istnieje; lista w `/cele` |

## Fałszywe alarmy

| Objaw | Przyczyna i rozwiązanie |
|---|---|
| Alarm o urządzeniu, którego nie masz | zostały przykładowe cele z pierwszego uruchomienia; usuń je (rozdział 3) |
| Alarmy co noc o tym samym urządzeniu | urządzenie się wyłącza albo zasypia; wycisz je do odwołania albo usuń z listy |
| Alarmy o kilku usługach naraz | ustaw im rodzica (maszynę, na której działają), wtedy przyjdzie jeden alarm |
| Strona https „nie działa”, choć w przeglądarce działa | certyfikat wygasa za mniej niż 14 dni (to celowy alarm) albo wpisałeś adres IP zamiast nazwy. Serwery z wieloma stronami (np. reverse proxy) wymagają **nazwy** w polu Adres |
| Alarm o DNS w sieci domowej | strażnik celowo używa DNS 1.1.1.1 i 8.8.8.8 dla siebie, a domowy DNS tylko sprawdza. Alarm oznacza, że domowy DNS naprawdę nie odpowiada |

## Lista celów

| Objaw | Przyczyna i rozwiązanie |
|---|---|
| „rodzic … musi stać wyżej na liście” | w edytorze JSON rodzic musi być przed dzieckiem. Formularz układa to sam |
| „tcp bez portu” | cel `tcp` wymaga portu |
| „powtórzone id” | dwa cele mają ten sam identyfikator |
| `JSON: EmptyInput` przy wysyłaniu listy przez `curl` | dodaj nagłówek `-H 'Content-Type: application/json'` |

## Strażnik sam się zrestartował

Po restarcie przez watchdog (zawieszenie programu) strażnik napisze w dzienniku
i w Telegramie, które zadanie i na jakim etapie stanęło („czarna skrzynka”). Po
poważnym błędzie programu przyśle też **zrzut awarii** z adresami w stylu `0x42049c47`.
Te adresy można przetłumaczyć na linie kodu:

```bash
firmware/build.sh                                  # zachowuje plik .elf tej wersji
tools/decode_crash.sh 0.9.4 0x42049c47 0x42040005  # wersja + adresy z wiadomości
```

Działa tylko z plikiem `.elf` z **tej samej** wersji i kompilacji, która była na
płytce.

## Inna wersja płytki niż N16R8

W `firmware/platformio.ini` zmień ustawienia pamięci zgodnie z płytką, np. dla N8R2
(8 MB flash, 2 MB PSRAM typu QSPI):

```ini
board_build.flash_size = 8MB
board_upload.flash_size = 8MB
board_build.partitions = default_8MB.csv
board_build.arduino.memory_type = qio_qspi
```

Wersja bez PSRAM (N8, N16) też powinna działać, jeśli usuniesz linię
`-DBOARD_HAS_PSRAM` i `memory_type`. Ani N8R2, ani wersja bez PSRAM nie zostały
przetestowane.
