# DSCoop — сюжетный кооператив для Death Stranding Director's Cut (ПК)

Мод-плагин поверх собственного фреймворка **DSLoader** (аналог RED4ext для Decima):
прокси `version.dll`, сканер сигнатур, хуки в памяти, RTTI-дамп, загрузка плагинов.
Цель — до 8 игроков в общем мире хоста, со своими Сэмами, грузом, боями, транспортом,
постройками и сюжетом. Ориентир по качеству — NightCoop для Cyberpunk 2077.

**Статус: v0.1.0 — этапы 0–1.** Каркас собран, сеть и релей работают и протестированы без
игры (тест-клиенты). Игровая часть ждёт разведки по `docs/00-recon-guide.md`: пока в
`data/offsets.json` нет сигнатур, плагин работает в «фейковом» режиме (меню, радар, чат есть,
позиция Сэма — заглушка).

## Жёсткие правила проекта
* Только лицензионная игра (Steam/Epic). Никакого обхода DRM, exe на диске не меняется.
* Официальный онлайн Kojima Productions не трогаем; в сессии DSCoop играем оффлайн.
* Перед первым запуском сейвы автоматически копируются в `dscoop/backups/`.
* Хост — единственный источник правды; адреса в коде — только сигнатуры + `offsets.json`.

## Состав репозитория

```
src/proto     протокол v0 (header-only)        src/loader   DSLoader → version.dll
src/common    лог, пути, конфиги               src/plugin   DSCoop.dll (сеть + ImGui + мост к игре)
src/net       ENet-транспорт, релей-клиент,     tools/       dscoop_testclient, валидатор offsets.json
              сессия хост/гость                 relay/       relay.py + Dockerfile + тесты
data/offsets.json  сигнатуры/указатели/RTTI     docs/        разведка, протокол, архитектура, чек-листы
tests/        юнит-тесты                        scripts/     сборка, e2e, установка
```

## Сборка

### Windows (игрок / разработчик) — MSVC 2022 + vcpkg
```powershell
git clone <repo> && cd Co-op-ds
$env:VCPKG_ROOT = "C:\vcpkg"            # vcpkg с bootstrap'ом; при первой сборке добавь baseline:
vcpkg x-update-baseline --add-initial-baseline
cmake --preset windows-msvc
cmake --build --preset windows-msvc
ctest --preset windows-msvc
.\scripts\install.ps1 -GameDir "D:\Steam\steamapps\common\DEATH STRANDING DIRECTORS CUT"
```
Результат: `build\windows-msvc\bin\version.dll`, `bin\dscoop\plugins\DSCoop.dll`,
`bin\dscoop_testclient.exe`.

### Linux — тесты, тест-клиент, кросс-сборка Windows
```bash
sudo apt install cmake ninja-build g++ libenet-dev libspdlog-dev libfmt-dev nlohmann-json3-dev mingw-w64
pip install pytest pytest-asyncio
scripts/build-linux.sh        # ctest + pytest
scripts/e2e-local.sh          # релей + хост + 2 гостя на localhost
IMGUI_DIR=/path/to/imgui scripts/build-mingw.sh   # version.dll / DSCoop.dll / testclient.exe через mingw-w64
```

## Быстрый старт без игры
```bash
python3 relay/relay.py &                                   # релей на 27000
dscoop_testclient host --relay 127.0.0.1:27000 --nick Host  # печатает ROOM CODE
dscoop_testclient join --relay 127.0.0.1:27000 --code ABC234 --nick Guest --chat "privet"
dscoop_testclient join --direct 192.168.1.10:27015          # без релея, по IP
```

## В игре
1. Установи (`install.ps1`), запусти игру в оффлайн-режиме.
2. `F9` — меню «Совместная игра»: **Создать сессию** (через релей → код приглашения, или
   напрямую по IP) / **Войти по коду**. Настройки → ник, адрес релея.
3. Радар в правом верхнем углу показывает напарников; маркеры над головой появятся после
   шага 5 разведки (камера).
4. Проблемы → вкладка «Отладка» → **Собрать отчёт** → пришли папку `dscoop/reports/report_*`.

Конфиги: `dscoop/dsloader.json` (загрузчик), `dscoop/dscoop.json` (ник, релей, порты, правила
хоста). Логи: `dscoop/logs/`.

## Релей
`relay/relay.py` — Python asyncio без зависимостей, Docker-образ в `relay/`. Коды комнат,
прозрачная UDP-трансляция (ENet не модифицируется), работа за симметричным NAT.
Подробности — `docs/relay.md`.

## Этапы
| # | Этап | Статус |
|---|---|---|
| 0 | Разведка (гайд, формат RTTI-дампа, сигнатуры) | гайд готов → ждём данных |
| 1 | DSLoader + «призраки» (маркеры 2+ игроков через релей) | код готов, проверка в игре |
| 2 | Марионетки: полноценные модели Сэмов с анимациями (2–8) | — |
| 3 | Груз, постройки, транспорт | — |
| 4 | Враги и бои, воидауты, PvP | — |
| 5 | Сюжет: заказы, катсцены, звонки, узлы | — |
| 6 | Полировка: родное меню, журналы, автообновление, установщик | — |

Документы: `docs/00-recon-guide.md`, `docs/protocol-v0.md`, `docs/architecture.md`,
`docs/relay.md`, `docs/rtti-dump-format.md`, `docs/test-checklist.md`, `docs/known-bugs.md`,
`CHANGELOG.md`.

## Лицензии зависимостей
ENet (MIT), MinHook (BSD-2), Dear ImGui (MIT), spdlog (MIT), nlohmann/json (MIT). Лицензия
самого DSCoop пока не выбрана — добавь `LICENSE` перед публичным релизом.
