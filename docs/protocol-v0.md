# Протокол DSCoop v0

Транспорт — ENet поверх UDP. Хост — единственный источник правды: назначает id игроков,
собирает состояния гостей и рассылает `WorldSnapshot` с частотой тика (30 Гц по умолчанию).
Гости отправляют только своё состояние и ввод; хост валидирует (в v0 — только диапазоны).

Исходник: `src/proto/include/dsproto/*.hpp` (header-only, без зависимостей, юнит-тесты в
`tests/test_proto.cpp`).

## Каналы ENet

| Канал | Режим | Что идёт |
|---|---|---|
| 0 `Control` | reliable, ordered | Hello/Welcome/Reject, PlayerJoined/Left, Ping/Pong, Chat, HostSettings |
| 1 `State` | unsequenced (unreliable) | PlayerState (гость→хост), WorldSnapshot (хост→гости) |
| 2 `World` | reliable | зарезервирован: сюжет, груз, постройки (этап 3+) |

## Формат

Little-endian. Каждый пакет ENet = `u8 MsgType` + тело. Строки — `u8 len` + байты (UTF-8,
ник ≤ 24 байт, чат ≤ 240).

### Рукопожатие
```
C→H Hello        u16 protocolVersion(=0) u32 clientVersion u32 gameBuild u32 features str8 nick
H→C Welcome      u8 playerId u32 hostTick u8 tickRate u64 sessionId u32 hostFlags u32 hostVersion
                 u8 n { u8 id str8 nick } × n        -- кто уже в сессии (хост = id 0)
H→C Reject       u8 reason str8 text                 -- затем disconnect
H→C PlayerJoined u8 id str8 nick
H→C PlayerLeft   u8 id u8 reason(0 left,1 timeout,2 kicked)
```
`clientVersion` = `DSCOOP_VERSION_INT` (0x00MMmmpp). Хост отклоняет другие версии
(`strictVersion`). `gameBuild` = FNV-1a от TimeDateStamp/SizeOfImage/CheckSum exe — у хоста и
гостя должны совпадать, иначе `GameBuildMismatch` (0 = «неизвестно», тест-клиент).

### Состояние игрока (32 байта)
```
u32 tick | f32 x y z | u16 yaw(0..2π→0..65535) | i16 pitch(×10000) | i16 vx vy vz (×256, м/с)
u16 animState | u16 flags | u8 health(×255) | u8 stamina(×255)
```
`flags`: Grounded 1, Sprinting 2, Crouched 4, InVehicle 8, Downed 16, InCutscene 32, InMenu 64,
Fake 0x8000 (состояние сгенерировано тест-клиентом/без оффсетов).

```
C→H PlayerState  u32 ackedSnapshotTick + PlayerState
```
`ackedSnapshotTick` — последний снапшот, который гость декодировал; хост использует его как
базу для дельты.

### Снапшот мира (дельта-сжатие)
```
H→C WorldSnapshot u32 hostTick u32 baselineTick(0 = полный) u8 count
                  count × { u8 id, u8 mask, поля по маске }
mask: 1 Pos(3×f32) 2 Rot(u16,i16) 4 Vel(3×i16) 8 Anim(u16) 16 Flags(u16) 32 Vitals(u8,u8)
```
Хост хранит 64 последних отправленных снапшота. Если `ackedSnapshotTick` гостя есть в
истории — кодируется дельта (неизменившийся игрок = 2 байта), иначе полный снапшот.
Гость, получив дельту от неизвестной базы, отвечает `ackedSnapshotTick = 0` и получает полный
(счётчик `desyncs` в статистике).

Типичный трафик при 8 игроках: полный снапшот ≈ 10 + 8×34 = 282 байта; дельта при ходьбе
≈ 10 + 8×(2+12+6) ≈ 170 байт; ×30 Гц ≈ 5 КБ/с на гостя.

### Время
```
C→H Ping u32 clientTimeMs
H→C Pong u32 clientTimeMs u32 hostTimeMs u32 hostTick
```
Гость оценивает непрерывный `hostTick(now)` (`TickClock`), рендерит остальных с задержкой
3 тика (100 мс) через `PlayerInterpolator` и экстраполирует по скорости до 8 тиков.

### Прочее
```
Chat         u8 fromId str8 text        -- хост переписывает fromId (не верим отправителю)
HostSettings u32 hostFlags u8 tickRate  -- PvP 1, DC-контент 2, погода 4, время суток 8, friendly fire 16
```

## Таймауты
Hello не пришёл за 5 с → disconnect. Нет пакетов 10 с → `PlayerLeft(timeout)`.
Гость: нет снапшотов 10 с → `host timeout`.

## Что добавится в v1 (этап 2–3)
Полная репликация анимационного графа, инвентарь/груз (reliable канал 2), события мира
(постройки, транспорт), подтверждения и откаты ввода. Версия протокола в `Hello` защитит от
смешивания билдов.
