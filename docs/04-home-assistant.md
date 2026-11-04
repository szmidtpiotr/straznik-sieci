# 4. Home Assistant przez MQTT (opcja)

Strażnik może pokazywać stan każdego celu w Home Assistancie. Nie musisz tego robić:
Telegram działa bez Home Assistanta, a nawet wtedy, gdy Home Assistant leży.

**MQTT** to prosty system wiadomości między urządzeniami. Pośrodku stoi **broker**
(najczęściej Mosquitto), do którego strażnik wysyła stany, a Home Assistant je czyta.

## Co dostajesz

W Home Assistancie pojawia się samo urządzenie **„Strażnik sieci”** z encjami:

| Encja | Co pokazuje |
|---|---|
| czujnik połączenia dla każdego celu (`binary_sensor`) | włączony = cel działa; w atrybutach czas odpowiedzi (`ms`), błąd (`err`), od kiedy (`since`), adres (`host`), wyciszenie (`muted`) |
| Cele działające / Cele leżące | liczniki |
| Wi-Fi sygnał, Czas działania | diagnostyka strażnika |
| przycisk **Sprawdź teraz** | natychmiastowy obchód |
| przełącznik **Wycisz powiadomienia** | wycisza wszystkie alarmy Telegrama do odwołania |

Gdy strażnik zniknie z sieci, jego encje przechodzą w stan „niedostępne”. Możesz na tym
oprzeć automatyzację „strażnik przestał działać”, dzięki czemu Home Assistant i strażnik
pilnują się nawzajem.

## Krok 1. Broker MQTT

Jeśli używasz już MQTT (np. dla Zigbee2MQTT), masz broker; przejdź do kroku 2.

Jeśli nie: w Home Assistancie **Ustawienia → Dodatki → Sklep z dodatkami →
Mosquitto broker → Zainstaluj → Uruchom**. Potem **Ustawienia → Urządzenia i usługi**:
Home Assistant sam zaproponuje integrację MQTT; zatwierdź ją.

## Krok 2. Konto dla strażnika

Załóż strażnikowi osobne konto, żeby nie znał haseł innych urządzeń.

- **Dodatek Mosquitto w HA:** najprościej założyć w Home Assistancie zwykłego
  użytkownika `straznik` (**Ustawienia → Osoby → Użytkownicy → Dodaj**; wymaga
  włączonego trybu zaawansowanego w profilu). Dodatek Mosquitto akceptuje konta
  użytkowników HA.
- **Własny Mosquitto:** `mosquitto_passwd /etc/mosquitto/passwd straznik`, potem restart
  usługi.

**Jeśli broker ma listę uprawnień (ACL),** strażnik potrzebuje prawa zapisu do
dwóch gałęzi i odczytu poleceń:

```
user straznik
topic readwrite straznik/#
topic readwrite homeassistant/+/straznik/#
topic read homeassistant/status
```

Konto bez wpisu w ACL **nie dostaje żadnego dostępu**, a strażnik pokaże wtedy błąd
połączenia albo po cichu nic nie opublikuje.

## Krok 3. Ustawienia w panelu strażnika

Panel → **Ustawienia → Home Assistant (MQTT)**:

| Pole | Wpisz |
|---|---|
| Broker | adres IP brokera, np. `192.168.1.12` (to zwykle adres Home Assistanta, jeśli używasz dodatku) |
| Port | `1883` |
| Użytkownik | `straznik` |
| Hasło | hasło z kroku 2 |

**Zapisz.** Status w nagłówku sekcji powinien zmienić się na „połączone”. W dzienniku
pojawi się „MQTT: połączono z …”.

## Krok 4. Sprawdź w Home Assistancie

**Ustawienia → Urządzenia i usługi → MQTT → Urządzenia → Strażnik sieci.** Powinieneś
zobaczyć listę czujników, po jednym na każdy cel. Nazwy encji zaczynają się od
`straznik`, np. `binary_sensor.straznik_ha`. Dokładna postać zależy od wersji Home
Assistanta, więc najpewniej szukać ich w urządzeniu.

Po dodaniu albo usunięciu celu w panelu strażnik sam zaktualizuje encje w HA.

## Przykład: powiadomienie z HA, gdy strażnik zniknie

```yaml
alias: Strażnik sieci przestał odpowiadać
triggers:
  - trigger: state
    entity_id: sensor.straznik_up        # „Cele działające”; sprawdź nazwę u siebie
    to: unavailable
    for: "00:05:00"
actions:
  - action: notify.notify
    data:
      message: "Strażnik sieci nie odpowiada od 5 minut."
```

**Dalej:** [rozdział 5, restart przez IPMI](05-restart-serwera-ipmi.md) albo
[rozdział 6, codzienne używanie](06-codzienne-uzywanie.md).
