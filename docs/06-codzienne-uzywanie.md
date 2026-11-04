# 6. Codzienne używanie

## Jakie wiadomości przychodzą

| Wiadomość | Znaczenie |
|---|---|
| 🔴 *Serwer* nie odpowiada (timeout) — zależnych od niego: 4 | awaria; wiadomość idzie po 3 nieudanych sprawdzeniach z rzędu (≈ 1,5 min) |
| 🟢 *Serwer* znów działa (przerwa 12 min) | koniec awarii |
| ⏰ Nadal nie działa: • *NAS* — od 12:09 (45 min) | przypomnienie co 30 min, dopóki cel leży i nie jest wyciszony |
| 💚 Strażnik żyje (działa od 3 d 4 h). | codziennie rano; **brak tej wiadomości = sprawdź strażnika** |
| 📶 Strażnik nie miał Wi-Fi przez 6 min… | strażnik stracił sieć; w tym czasie nic nie sprawdzał |
| 🛡️ Strażnik wystartował… | po włączeniu, restarcie albo aktualizacji |

Jeśli przez chwilę nie ma internetu, wiadomości czekają w kolejce i wychodzą później
z godziną zdarzenia.

Pod alarmem są przyciski: **🔕 *cel* 1 h**, **do odwołania** i **🔕 Wszystko na 1 h**.
Jedno stuknięcie wycisza bez wpisywania komendy.

## Komendy Telegrama

Wpisz `/` w czacie z botem, a Telegram pokaże listę. `<id>` to identyfikator celu (widać
go komendą `/cele` albo w panelu).

| Komenda | Co robi |
|---|---|
| `/status` | stan wszystkich celów |
| `/problemy` | tylko to, co leży albo ma porażki |
| `/cele` | lista celów z identyfikatorami |
| `/sprawdz` | sprawdź wszystko od razu |
| `/wycisz` | wycisz wszystkie alarmy na 60 min |
| `/wycisz 120` | wycisz wszystko na 120 min; `/wycisz -1` = do odwołania |
| `/wycisz <id>` | wycisz jeden cel do odwołania |
| `/wycisz <id> 120` | wycisz jeden cel na 120 min |
| `/odcisz`, `/odcisz <id>` | cofnij wyciszenie wszystkiego / jednego celu |
| `/wylacz <id>`, `/wlacz <id>` | przestań sprawdzać cel / sprawdzaj znowu |
| `/test` | czy strażnik żyje (odpisze z IP i sygnałem Wi-Fi) |
| `/ipmi`, `/restart`, `/symuluj` | tylko przy IPMI, rozdział 5 |

## Wyciszyć czy wyłączyć?

| | Wyciszony cel | Wyłączony cel |
|---|---|---|
| Czy jest sprawdzany? | tak, widać go w panelu i na wykresie | nie |
| Czy wysyła alarmy? | nie | nie |
| Kiedy używać | znana awaria, prace, urządzenie, które czasem się wyłącza | urządzenie oddane do naprawy, przeprowadzka |
| Jak | przycisk pod alarmem, `/wycisz <id>`, w panelu karta celu → Wycisz… | `/wylacz <id>`, w panelu karta celu → Wyłącz sprawdzanie |

Wyciszenia i wyłączenia przeżywają restart strażnika.

## Dioda na płytce

| Kolor | Znaczenie |
|---|---|
| 🟢 zielony | wszystko działa |
| 🟡 żółty | coś nie odpowiada, ale to jeszcze nie awaria (mniej niż 3 porażki z rzędu) |
| 🔴 czerwony | awaria |
| 🔵 niebieski | brak Wi-Fi |

Pojedyncza żółta dioda albo „1× brak” w panelu bez wiadomości to norma: pojedyncze
zgubione odpowiedzi zdarzają się w każdej sieci.

## Aktualizacja programu przez sieć

Gdy zmienisz coś w kodzie albo w `secrets.h`, nie musisz podłączać kabla.

1. Zbuduj program: w VS Code **✓ Build** (albo `pio run` w katalogu `firmware`).
2. Plik z programem: `firmware/.pio/build/esp32s3/firmware.bin`.
3. Panel → **Ustawienia → System → Aktualizacja (OTA)** (albo wprost
   `http://straznik.local/update`).
4. W sekcji **Firmware** wybierz plik `firmware.bin` → **Update**. Nie używaj sekcji
   „FileSystem”, bo skasowałaby listę celów.
5. Po około 30 sekundach strażnik się zrestartuje, a w Telegramie przyjdzie
   „🛡️ Strażnik wystartował (fw …)”.

Na Linuksie i macOS to samo robi jedna komenda: `firmware/build.sh ota straznik.local`
(zapyta o hasło panelu).

Ustawienia, lista celów i wyciszenia zostają po aktualizacji.

## Kopia zapasowa listy celów

**Ustawienia → Lista celów — edytor JSON** → zaznacz całość → skopiuj do pliku. Żeby
przywrócić, wklej i zapisz. Warto to zrobić po większych zmianach.
