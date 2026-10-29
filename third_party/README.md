# third_party

Вендоренные зависимости. Используются только как запасной вариант, когда
`find_package()` (vcpkg) не находит пакет — например, при кросс-сборке MinGW
без vcpkg или в CI на Linux.

| Библиотека | Версия | Лицензия | Назначение |
|-----------|--------|----------|------------|
| enet      | 1.3.17 | MIT      | транспорт UDP (reliable/unreliable каналы) |
| minhook   | 1.3.3  | BSD-2    | хуки функций в памяти (x64) |

ImGui, spdlog и nlohmann-json не вендорятся: берутся из vcpkg (`vcpkg.json`),
из системы (`IMGUI_DIR`, apt) или через FetchContent (см. `cmake/Dependencies.cmake`).
