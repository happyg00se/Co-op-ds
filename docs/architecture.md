# Архитектура

```
┌──────────────── игра ds.exe (Decima, DX12) ─────────────────┐
│  version.dll  = DSLoader                                     │
│   ├─ прокси 16 экспортов → System32\version.dll              │
│   ├─ поток: лог → бэкап сейвов → ждём окно → MinHook         │
│   ├─ sigscan: data/offsets.json → адреса (+кэш по build_id)  │
│   ├─ RTTI-дамп (.data → dumps/rtti_<build>.json)             │
│   └─ плагины dscoop/plugins/*.dll (C ABI, plugin_api.h)      │
│          │                                                   │
│  DSCoop.dll (плагин)                                         │
│   ├─ GameBridge: чтение памяти по pointer-цепочкам           │
│   ├─ dx12_hook: Present/ResizeBuffers/ExecuteCommandLists    │
│   │     └─ ImGui overlay (меню F9, радар, маркеры, чат)      │
│   └─ CoopManager → dsnet::Session (ENet) ─┐                  │
└───────────────────────────────────────────┼──────────────────┘
                                            │ UDP
                relay.py (DSCP + ретрансляция) или напрямую
```

## Модули

| Каталог | Что | Платформа |
|---|---|---|
| `src/proto` | протокол v0: сериализация, снапшоты, интерполяция, DSCP | любая, header-only |
| `src/common` | лог (spdlog), пути `dscoop/`, JSON-конфиги | любая |
| `src/net` | `ITransport` (ENet / loopback), `RelayClient`, `Session` | любая |
| `src/loader` | DSLoader (`version.dll`) | Windows |
| `src/plugin` | DSCoop.dll | Windows |
| `tools/testclient` | консольный клиент с фейковыми координатами | любая |
| `relay` | релей/хаб на Python + Dockerfile | Linux/Docker |
| `tests` | юнит-тесты (протокол, сигнатуры, RTTI-скан, сессия на loopback) | любая |

## Жизненный цикл загрузчика

1. `DllMain(PROCESS_ATTACH)`: грузим настоящий `version.dll`, стартуем поток. В DllMain
   больше ничего не делаем (loader lock).
2. Поток: `dscoop/dsloader.json` → лог `dscoop/logs/dsloader.log` → crash handler
   (минидамп в `reports/`) → информация о модуле (секции, `build_id`) → бэкап сейвов →
   ждём главное окно игры (+5 с) → `MH_Initialize`.
3. `offsets.json`: каждая сигнатура ищется в исполняемых секциях (уникальность обязательна),
   RIP-relative операнды разыменовываются, результат кэшируется в
   `cache/sigcache_<build_id>.json`. Если нет **обязательных** сигнатур — плагины не грузим.
4. Плагины: `DSPlugin_Query` → проверка `api_version` → `DSPlugin_Load(api)`. API даёт лог,
   `resolve(name)`, хуки MinHook, безопасное чтение памяти, сбор отчёта.

## Плагин DSCoop

* **Рендер-поток как «тик»**: `Present` → `CoopManager::update()` → `GameBridge::update()`
  (чтение памяти) → `Session::update()` (ENet service, отправка состояния / снапшотов) →
  ImGui. До появления хука игрового тика (этап 2) это самый безопасный момент читать память.
* **Без оффсетов** плагин всё равно работает: шлёт фейковую окружность, рисует радар и меню.
  Так проверяется связка прокси → хук DX12 → сеть ещё до разведки.
* **Маркеры** требуют камеру (шаг 5 разведки). Радар — нет.

## Сеть

* Хост: `Session::host()` слушает UDP (по умолчанию 27015), при релее — создаёт комнату и
  пингует guest-порты. Тик 30 Гц, снапшоты с дельтой по подтверждённой базе.
* Гость: `joinDirect`/`joinRelay` → Hello → Welcome → состояние 30 Гц → интерполяция.
* Всё тестируется без игры: `tests/test_session.cpp` гоняет хоста и двух гостей на
  loopback-транспорте с задержкой и потерями; `scripts/e2e-local.sh` — через настоящий релей.

## Файлы в папке игры

```
<game>/version.dll
<game>/dscoop/
  dsloader.json  dscoop.json
  plugins/DSCoop.dll
  data/offsets.json
  logs/ cache/ dumps/ backups/ reports/
```

## Ограничения, о которых честно

* Exe на диске не трогаем, онлайн Kojima Productions не трогаем (играть в оффлайн-режиме;
  отключение их трафика внутри сессии — этап 5).
* Все адреса — только через сигнатуры; пока разведка не сделана, `offsets.json` содержит
  пустые паттерны и мод работает в «фейковом» режиме.
* Чтение памяти из рендер-потока может дать «рваные» значения (torn read) — для позиции в
  этапе 1 допустимо, этап 2 переведёт нас на хук игрового тика.
