# Дорожная карта внедрения ID24 в Eternity Engine (EE) — подробный чеклист

Репозиторий: `dron12261games/eternity-fork`  
Ветка: `master`  
Спеки: `doom-cross-port-collab/id24` (папка: `version_0_99_2_md`)

См. также (English): `docs/id24_todo.md`

## Вводные (первый этап)
- **Безопасный старт:** сначала **не меняем** `demo_version`. Новое поведение гейтится флагом `id24_enabled`.
- Автодетект должен быть **предсказуемым**: решение принимается один раз, обязательно логируется, и не “скачет” в процессе старта.
- Придерживаемся стиля EE (нейминг, логирование, обработка ошибок).

## Ключевые точки кода EE (подтверждены)
- Старт / WAD init: `source/d_main.cpp` (после `wGlobalDir.initMultipleFiles(wadfiles)` и `D_InitGMIPostWads()`).
- JSON-инфраструктура: `source/id24_json.h`, `source/id24_json.cpp`.
- Пример ID24-модуля: `source/id24_demoloop.cpp`.
- id24res.wad (путь/конфиг): `source/d_findiwads.cpp`, `source/g_cmd.cpp`, `source/m_syscfg.cpp`, `source/mn_menus.cpp`, `source/d_gi.*`.

---

## TASK 01 — ID24 runtime mode + автодетект (без смены demo_version)
**Цель:** EE автоматически включает режим ID24 при обнаружении ID24-контента. Решение принимается один раз после построения WAD directory и логируется.

**Зависимости:** нет.

### Подзадачи
- [ ] Добавить центральное состояние (там, где это соответствует стилю EE: `doomstat.*` или выделенный `id24_*.cpp`):
  - [ ] `bool id24_enabled = false;`
  - [ ] `bool id24_autodetect = true;` (опционально; по умолчанию true)
  - [ ] `qstring id24_reason;` (или enum + строка деталей)
- [ ] Добавить API:
  - [ ] `void ID24_Reset();`
  - [ ] `void ID24_Enable(const char* reason, const char* details);`
  - [ ] `bool ID24_Enabled();`
- [ ] Встроить вызов детектора в старт:
  - [ ] В `source/d_main.cpp` вызвать `ID24_DetectAndEnableFromLoadedWads()` **после**
    - `wGlobalDir.initMultipleFiles(wadfiles);`
    - `D_InitGMIPostWads();`
    и **до** начала обработки DeHackEd.
- [ ] Реализовать strong-сигналы детекта:
  - [ ] JSON-lump кандидаты: попытка распарсить/провалидировать `SBARDEF`, `SKYDEFS`, `DEMOLOOP`
    - если валидный JSON-lump и ожидаемый type+version совпал — включить ID24.
  - [ ] DEHACKED маркер: Doom version == 2024 → включить ID24 (маркер ID24HACKED).
- [ ] Логирование:
  - [ ] `ID24: включен (причина=..., детали=...)`
  - [ ] `ID24: не включен (ID24 сигналы не найдены)`
- [ ] Фиксация:
  - [ ] После включения ID24 не отключать в рамках этого старта.

### Критерии приёмки
- На обычных (не ID24) WAD’ах ID24 **не включается**.
- ID24 включается при наличии валидного JSON-lump `SKYDEFS` / `SBARDEF` / `DEMOLOOP`.
- Невалидный JSON в этих lumps **не включает** ID24, но выводит предупреждение.

---

## TASK 02 — best-effort загрузка id24res.wad при включенном ID24
**Цель:** если ID24 включён, попытаться загрузить `id24res.wad` (supporting data).

**Зависимости:** TASK 01.

### Подзадачи
- [ ] Реализовать `ID24_TryLoadId24Res()`:
  - [ ] Использовать существующий `gi_path_id24res` (автоскан) и конфиг (`pwad_id24res`).
  - [ ] Если путь задан и WAD ещё не загружен — загрузить как PWAD (best effort).
  - [ ] Если не найдено — warning, но не фатально.
- [ ] Логи:
  - [ ] `ID24: id24res.wad загружен из <path>`
  - [ ] `ID24: id24res.wad не найден; часть ресурсов может отсутствовать`
- [ ] (Позже) улучшение порядка/приоритета (в идеале — до IWAD), когда появится GAMECONF/ранний сигнал.

### Критерии приёмки
- При наличии `id24res.wad` EE загружает его при включенном ID24.
- При отсутствии — продолжает работу, только с предупреждением.

---

## TASK 03 — Укрепление JSON-платформы (общий loader + единая диагностика)
**Цель:** единый паттерн парсинга/валидации/применения JSON-lumps для всех ID24-фич.

**Зависимости:** TASK 01.

### Подзадачи
- [ ] Добавить helper-обвязку (в `id24_json.*` или отдельном модуле):
  - [ ] `ID24_ParseJSONLumpByName(...)`
  - [ ] единый формат ошибок/предупреждений (имя lump, type/version, путь до поля)
- [ ] Обеспечить строгие требования JSON_Lump:
  - [ ] root содержит ровно `type`, `version`, `metadata`, `data`
  - [ ] версия выше поддерживаемой → ошибка
  - [ ] неизвестные поля root → ошибка
- [ ] (Опционально) перевести `id24_demoloop.cpp` на общий helper.

### Критерии приёмки
- Новый JSON-lump модуль можно добавлять по короткому шаблону.
- Сообщения об ошибках единообразны.

---

## TASK 04 — Translations (translation 1.0.0)
**Цель:** парсинг и применение таблиц переводов ID24.

**Зависимости:** TASK 03 (желательно), TASK 01.

### Подзадачи
- [ ] Реестр переводов по имени.
- [ ] Парсинг/валидация:
  - [ ] table строго длины 256
  - [ ] поддержка override встроенных переводов по спекам
- [ ] Интеграция с существующим пайплайном перевода спрайтов/объектов в EE.
- [ ] Тесты (минимальный WAD или примеры).

### Критерии приёмки
- Валидный перевод загружается и применяется.
- Невалидный — корректная ошибка без краша.

---

## TASK 05 — SBARDEF (statusbar 1.0.0)
**Цель:** статусбар, управляемый данными.

**Зависимости:** TASK 03, TASK 01 (и желательно TASK 02).

### Подзадачи
- [ ] JSON-структуры: `numberfonts`, `statusbars`, `elements`, `conditions`.
- [ ] Runtime:
  - [ ] virtual 320x200
  - [ ] логика обновления на 35Hz
  - [ ] дерево элементов + alignment + clipping
- [ ] Выбор по screenblocks: `barindex = max(screenblocks - 10, 0)`.
- [ ] Fallback на старый статусбар при отсутствии/ошибке SBARDEF.

### Критерии приёмки
- SBARDEF из `id24res.wad` рендерится без регрессий.
- Выбор bar index работает как требуется.

---

## TASK 06 — SKYDEFS (skydefs 1.0.0)
**Цель:** кастомные небеса (normal/foreground/fire).

**Зависимости:** TASK 03, TASK 01.

### Подзадачи
- [ ] Парсинг `skies` + `flatmapping`.
- [ ] Интеграция:
  - [ ] сохранить дефолтное поведение `F_SKY1`
  - [ ] применять дефы по texture name
- [ ] With Foreground: прозрачность по palette index 0 в foreground.
- [ ] Fire sky: симуляция + priming + update timing.

### Критерии приёмки
- normal/foreground/fire работают на примерах.
- При отсутствии SKYDEFS — старое поведение.

---

## TASK 07 — DEMOLOOP (demoloop 1.0.0)
**Цель:** демо-цикл из данных.

**Зависимости:** TASK 03, TASK 01.

### Подзадачи
- [ ] Довести `id24_demoloop.cpp` до полной спеки (типы entry, wipe, duration).
- [ ] Подключить к main/title loop.
- [ ] Тесты.

### Критерии приёмки
- Порядок/тайминги/wipe соответствуют JSON.

---

## TASK 08 — Interlevel (interlevel 1.0.0) + UMAPINFO enteranim/exitanim
**Цель:** анимированные фоны intermission/victory.

**Зависимости:** TASK 03, TASK 01.

### Подзадачи
- [ ] Парсер interlevel JSON (layers/anims/frames/conditions).
- [ ] Рендер (virtual 320x200).
- [ ] UMAPINFO:
  - [ ] `enteranim/exitanim` authoritative: если поле задано и lump invalid → ошибка, без fallback.

### Критерии приёмки
- Примеры работают.
- Authoritative правило соблюдено.

---

## TASK 09 — Finale lumps (finale 1.0.0) + UMAPINFO endfinale
**Цель:** настраиваемые финалы + переход на следующую карту.

**Зависимости:** TASK 03, TASK 01.

### Подзадачи
- [ ] Парсер art/bunny/cast.
- [ ] Интеграция endfinale (authoritative).
- [ ] Поддержка `donextmap`.
- [ ] Тесты.

### Критерии приёмки
- Финалы работают на примерах.

---

## TASK 10 — GAMECONF (gameconf 1.0.0)
**Цель:** “официальный” способ задавать окружение и уровень фич.

**Зависимости:** TASK 03, TASK 01.

### Подзадачи
- [ ] Парсер GAMECONF + merge semantics (destructive/additive/max).
- [ ] Применение на старте (до init подсистем).
- [ ] `executable=id24` форсирует ID24.
- [ ] Связь с политикой загрузки id24res.wad.

### Критерии приёмки
- GAMECONF надёжно включает ID24.

---

## TASK 11 — Mapping additions
**Цель:** новые specials + расширения UMAPINFO + правила поверхностей.

**Зависимости:** TASK 01.

### Подзадачи
- [ ] Linedef specials по таблицам спеки.
- [ ] UMAPINFO bossaction extensions:
  - [ ] `bossaction` принимает thing number
  - [ ] `bossactionednum` editor-number mapping
- [ ] “texture-on-flat / flat-on-wall” + прозрачность index 0.

### Критерии приёмки
- Карты с mapping additions работают.

---

## TASK 12 — ID24HACKED (расширения DeHackEd)
**Цель:** расширенная семантика DeHackEd по ID24HACKED.

**Зависимости:** TASK 01, текущая DeHackEd система.

### Подзадачи
- [ ] Детект Doom version 2024 и включение ID24HACKED.
- [ ] Расширенные диапазоны индексов + USER_ mnemonics.
- [ ] Порядок применения DEHACKED lumps по спекам.

### Критерии приёмки
- ID24HACKED патчи применяются стабильно.

---

## TASK 13 — ID24HACKED hashing (Valid hashes)
**Цель:** контроль корректного стэкинга патчей.

**Зависимости:** TASK 12.

### Подзадачи
- [ ] FNV-1a 64-bit (little-endian).
- [ ] Подсчёт hash текущего состояния.
- [ ] Парсинг/валидация `Valid hashes` и enforcement.
- [ ] Тесты: good chain / bad chain.

### Критерии приёмки
- Неправильный hash → патч отклоняется с понятной ошибкой.

---

## TASK 14 — Музыкальные форматы (OGG + tracker)
**Цель:** выполнить требование ID24 по музыке.

**Зависимости:** нет (но лучше проверять на ID24 тестах).

### Подзадачи
- [ ] Аудит текущего backend музыки в EE.
- [ ] OGG playback.
- [ ] Tracker (.mod/.s3m/.xm/.it) playback (deps/build).
- [ ] Тесты.

### Критерии приёмки
- Форматы воспроизводятся в ID24 контенте.