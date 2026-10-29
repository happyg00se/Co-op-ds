# Формат RTTI-дампа (`dscoop/dumps/rtti_<build_id>.json`)

Создаётся DSLoader при `dsloader.json → "dump_rtti": true`. Сканер: `src/loader/src/rtti_scan.*`
(тесты на синтетическом модуле — `tests/test_sigscan.cpp`).

```json
{
  "format": 1,
  "game_build": 123456789,
  "module_base": 140694538682368,
  "layout": { "...": "раскладка, которой сканировали (копия offsets.json → rtti.layout)" },
  "class_count": 4312,
  "classes": [
    {
      "name": "Entity",
      "rva": 66830336,
      "size": 512,
      "bases": [ { "name": "RTTIRefObject", "offset": 0, "rva": 66829824 } ],
      "members": [
        { "name": "General", "offset": 0, "flags": 0, "category": true },
        { "name": "Name", "offset": 16, "flags": 2, "type": "String" },
        { "name": "WorldTransform", "offset": 48, "flags": 0, "type": "WorldTransform" }
      ]
    }
  ]
}
```

* `rva` — смещение записи RTTI от базы модуля (стабильно внутри одного билда, меняется между
  билдами, поэтому в код не попадает — только имена и смещения полей).
* `members[].offset` — смещение поля внутри экземпляра класса; `category: true` — строка-раздел
  (в Decima таблица полей содержит заголовки групп с нулевым типом).
* `bases[].offset` — смещение базового класса внутри производного (множественное наследование).
* Типы полей — имена RTTI-записей (`float`, `WorldPosition`, `Ref<Entity>`, `Array<...>`).
  Для не-классов имя извлекается эвристически и может быть `?kindN` — тогда нужна
  раскладка для этого вида записи (добавим после проверки на реальном дампе).

## Как мы это используем

1. Поиск классов `Player*`, `Mover`, `WorldTransform`, `Inventory*`, `Vehicle*`, `Weather*`
   даёт смещения полей без ручного реверса каждого — это основа этапов 2–5.
2. Утилита (позже) соберёт из дампа заголовки C++ со структурами, как это делают HZD-моддеры.
3. Сравнение дампов двух билдов показывает, что изменилось после патча.

## Раскладка (`offsets.json → rtti.layout`)

Поля `*Offset` — байтовые смещения внутри записи RTTI класса; `kindClass` — значение байта вида
для классов. `verified: false` означает «значения по умолчанию из Horizon Zero Dawn PC,
не подтверждены для DS DC» — дампер откажется работать без `rtti_force_unverified: true`.
Проверка раскладки — шаг 6 в `docs/00-recon-guide.md`.
