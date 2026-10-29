# Графика и шрифты установщика

| Файл | Что | Происхождение / лицензия |
|---|---|---|
| `hero.jpg` | фон окна: туманные хребты, два портера | оригинальная процедурная графика, `tools/art/gen_installer_art.py` |
| `icon.ico`, `icon.png` | иконка: золотое кольцо с нитью | тот же генератор |
| `nodes.txt` | координаты узлов анимированной сети (единицы окна 920×580) | тот же генератор; значения скопированы в `src/installer/win/ui.cpp` |
| `fonts/RussoOne-Regular.ttf` | заголовки, логотип | Russo One, SIL Open Font License 1.1 (`fonts/OFL-RussoOne.txt`) |
| `fonts/PTSans-*.ttf` | текст | PT Sans (ParaType), SIL Open Font License 1.1 (`fonts/OFL-PTSans.txt`) |

Официальные материалы Death Stranding / Kojima Productions не используются.

Перегенерировать графику (детерминированно, тот же seed даёт те же пиксели):
```bash
python3 tools/art/gen_installer_art.py      # нужны numpy и pillow
```
Если поменялись `nodes.txt`, обнови массив `kNodes` в `src/installer/win/ui.cpp`.
