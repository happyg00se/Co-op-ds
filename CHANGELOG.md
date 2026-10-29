# Changelog

Формат — [Keep a Changelog](https://keepachangelog.com/ru/1.1.0/), версии — SemVer.

## [0.1.0] — 2026-10-29 — этапы 0–1: каркас, «призраки»

### Добавлено
- **DSLoader** (`version.dll`): прокси 16 экспортов, поток инициализации, лог
  (`dscoop/logs/dsloader.log`), минидампы при крэше, сканер сигнатур с RIP-разыменованием и
  кэшем по `build_id`, загрузчик плагинов (C ABI `plugin_api.h`), бэкап сейвов, сканер и
  дампер Decima RTTI с data-driven раскладкой.
- **DSCoop.dll**: `GameBridge` (pointer-цепочки из `offsets.json`, типы vec3d/vec3f/mat3/quat,
  камера из трансформа + FOV), хуки DX12 (Present/ResizeBuffers/ExecuteCommandLists), ImGui
  оверлей — меню «Совместная игра» (создать через релей / напрямую, войти по коду / по IP),
  таблица игроков, радар, маркеры над игроками, чат, настройки, отладка памяти, «Собрать отчёт».
- **Протокол v0** (`dsproto`): Hello/Welcome/Reject, PlayerState 32 байта, WorldSnapshot с
  дельтой по подтверждённой базе, Ping/Pong + `TickClock`, интерполяция/экстраполяция,
  DSCP (протокол релея). 9 юнит-тестов.
- **Сеть** (`dsnet`): `ITransport` (ENet + loopback для тестов), `RelayClient` на одном сокете
  с ENet через `intercept`, `Session` (хост/гость, 30 Гц, до 8 игроков). 4 теста на loopback
  с задержкой и потерями.
- **Релей** `relay/relay.py` (asyncio, прозрачная UDP-трансляция, коды комнат, токены,
  таймауты), Dockerfile, docker-compose, 4 pytest-теста.
- **Тест-клиент** `dscoop_testclient` (host/join, фейковое движение, статистика, чат) —
  проверен сквозной сценарий «хост + 2 гостя через релей».
- Сборка: CMake 3.21+, пресеты MSVC+vcpkg / MinGW-cross / Linux, `vcpkg.json`, вендоренные
  ENet 1.3.17 и MinHook 1.3.3 как запасной вариант, CI (Linux тесты + e2e, MinGW, MSVC).
- Документация: гайд по разведке (этап 0), протокол, архитектура, релей, формат RTTI-дампа,
  чек-лист теста, известные баги.

### Не сделано / ограничения
- `offsets.json` содержит только заготовки сигнатур — до разведки плагин работает в
  «фейковом» режиме (окружность вместо позиции Сэма).
- Windows-бинарники проверены только компиляцией (MinGW) — см. `docs/known-bugs.md`.
