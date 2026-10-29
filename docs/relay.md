# Relay / hub (DSCP)

`relay/relay.py` — один файл на asyncio без зависимостей. Решает две задачи: коды комнат и
ретрансляция UDP для игроков за NAT. Прямое P2P-подключение по IP работает без релея.

## Идея: релей как «прозрачный NAT»

ENet не трогаем. Для каждой комнаты релей открывает **1 host-порт** (к нему ENet-коннектятся
гости) и **N guest-портов** (с них хост «видит» гостей):

```
гость  ──►  relay:hostPort        ═══►  relay:guestPort[slot]  ──►  хост
хост   ──►  relay:guestPort[slot] ═══►  relay:hostPort         ──►  гость
```

Хост видит каждого гостя как отдельный стабильный адрес `relay:guestPort[slot]`, гость видит
хоста как `relay:hostPort`. Всё, что проходит, — байты ENet как есть.

Соответствие «адрес ↔ слот» релей узнаёт из служебных пакетов **DSCP** (магия `DSCP`), которые
клиент шлёт **с того же UDP-сокета, что и ENet** (`ITransport::sendRaw`, приём через
`ENetHost::intercept`). Поэтому симметричный NAT тоже работает: хост пингует каждый guest-порт
раз в 2 с, гость пингует host-порт — релей всегда знает актуальные внешние адреса.

## Протокол DSCP v1 (`src/proto/include/dsproto/relay_control.hpp`)

```
'D' 'S' 'C' 'P'  u8 ver=1  u8 op  тело
1 CREATE  { u8 maxGuests, u32 clientVersion }                         клиент → relay:control
2 CREATED { char code[6], u64 hostToken, u16 hostPort, u8 n, u16 guestPorts[n] }
3 JOIN    { char code[6], u32 clientVersion }                         гость → relay:control
4 JOINED  { u16 hostPort, u8 slot, u64 slotToken }
5 ERROR   { u8 code, str8 text }   1 bad request, 2 room not found, 3 room full,
                                   4 no capacity, 5 version, 6 bad token
6 PING    { u64 token, u8 slot }   хост → guestPort[slot] (hostToken) | гость → hostPort (slotToken)
7 PONG    { u8 slot, u32 serverTimeMs }
8 CLOSE   { u64 hostToken }        хост → relay:control
```

Коды комнат: 6 символов из алфавита без похожих знаков (`23456789ABCDEFGHJKMNPQRSTUVWXYZ`).
Гость ENet-коннектится только после первого PONG (`RelayClient::readyToConnect`).

Релей проверяет, что `clientVersion` гостя совпадает с версией хоста; `--require-version`
дополнительно ограничивает релей одной версией.

## Запуск

```bash
python3 relay/relay.py --control-port 27000 --port-range 27100-27999
# Docker
cd relay && docker compose up -d --build
```
`--port-range` задаёт ёмкость: 8 портов на комнату (1 + 7 гостей). 27100–27999 → 112 комнат.
Открой на файрволе UDP `27000` и весь диапазон. Комната закрывается, когда хост не пингует 90 с
(`--room-idle`), слот гостя освобождается через 45 с (`--guest-idle`).

Логи — stdout; `--stats-interval 300` печатает счётчики. Тесты: `cd relay && pytest`.

## Свой сервер

Любой Linux VPS с публичным IP. Клиенту в `dscoop/dscoop.json` → `relay.host` = домен/IP,
`relay.control_port` = 27000. Хост-игроку порты пробрасывать **не нужно**.

## Чего пока нет
* NAT hole punching (прямое соединение после знакомства через хаб) — релей сейчас всегда
  ретранслирует. Добавится на этапе 6; протокол DSCP уже содержит всё необходимое
  (адреса видны релею).
* Аутентификация/лимиты на создание комнат — комнаты анонимны, защищены только случайным
  кодом и 64-битными токенами.
