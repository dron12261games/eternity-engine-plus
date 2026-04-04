# Copilot Chat Conversation Export: Implementing ID24 support in Eternity Engine

**User:** @DRON12261
**Thread URL:** https://github.com/copilot/c/4e42333e-f1c9-47f4-93bf-3f2ab94963ef

## @DRON12261

Привет, составь полный и детальный пошаговый план реализации поддержки id24 для Eternity Engine (прикрепленный к этому чату репозиторий).

Прежде чем формировать план подробно изучи все файлы из этой папки: @doom-cross-port-collab/id24/files/version_0_99_2_md
Это полная спецификация формата id24

## Copilot

Ниже — **полный пошаговый план** внедрения поддержки **ID24 (spec v0.99.2)** в **Eternity Engine** (репо `dron12261games/eternity-fork`) на основе файлов спецификации из папки `version_0_99_2_md` в `doom-cross-port-collab/id24` (commit `db9b334…`).  
Спецификация ID24 состоит из набора “major features” (каждая — отдельный документ) и общего базиса (JSON lumps + набор базовых стандартов). Я буду строить план так, чтобы вы могли внедрять фичи **итеративно**, сохраняя обратную совместимость и получая рабочие промежуточные сборки.

Важно: результаты code-search по Eternity ограничены 10 совпадениями, поэтому некоторые места в коде я мог не “увидеть” инструментом — ниже я даю **точки входа**, и где нужно, добавляю “как найти в коде”.

---

## 0) Что именно нужно поддержать (контрольный список по ID24 0.99.2)

ID24 формально заявлен как надстройка над: Vanilla Doom, Boom, MUSINFO, MBF, DEHEXTRA, MBF21, DSDHACKED, UMAPINFO, плюс новые подсистемы ID24. (см. `ID24_formal_specification_0.99.2.md`)

**Документы/фичи из папки спецификации:**
1) `JSON_Lump.md` — общий контейнер для JSON-lump’ов: строгая валидация root `{type, version, metadata, data}`, запрет лишних полей, версия “не выше ожидаемой” = ошибка, metadata игнорируется рантаймом.  
2) `GAMECONF.md` — конфигурация загрузки IWAD/PWAD/опций/переводов на ранней стадии.
3) `DEMOLOOP.md` — кастомизация demo loop.
4) `Finale_lumps.md` — кастомизация финале (art/bunny/cast) + поле `endfinale` в UMAPINFO.
5) `Interlevel_lumps.md` — анимированные intermission backgrounds + поля `exitanim/enteranim` в UMAPINFO.
6) `SBARDEF.md` — JSON-описание statusbar’ов.
7) `SKYDEFS.md` — JSON-описание небес (normal/fire/with-foreground).
8) `Translations.md` — JSON-описание таблиц переводов (256 байт) + интеграция с GAMECONF и Dehacked.
9) `Mapping_Additions.md` — новые line specials (2048+), bossaction расширения, секторные колормэпы, муз. линии, и т.д.
10) `ID24HACKED.md` и `ID24HACKED_hashing.md` — расширение DeHackEd + механизм “valid hashes” (FNV-1a 64-bit) для безопасного накопления патчей.

---

## 1) Предварительная инвентаризация в Eternity: что уже есть и где “встраиваться”

По результатам поиска в `source/`:
- **UMAPINFO уже реализован**: `source/xl_umapinfo.cpp/.h` (есть парсер, и сбор intermission info из `enterpic/exitpic`).  
- **MUSINFO уже реализован**: `source/xl_musinfo.cpp/.h` и runtime `source/s_musinfo.h`.
- **DeHackEd поддержка есть**: `source/d_deh.cpp`, `source/d_deh.h`, таблицы/стейты (`info.h`, `e_states.cpp`, `e_weapons.cpp`, `e_cmd.cpp` и т.п.).
- В коде есть система **MetaTable** и “XL scripts” парсеры — это можно использовать как инфраструктуру, но JSON-фичи ID24 потребуют **JSON-парсер** и строгую валидацию.

Где искать дальше (если не найдёте сразу по именам):
- WAD-слой: `w_wad.*`, `waddir->checkNumForName` уже используется в MUSINFO — значит есть централизованный доступ к lumps.
- UI/Interмиссия/Finale/StatusBar/Sky: ищите по `intermission`, `finale`, `wi_`, `f_`, `st_`, `sky`, `r_sky`, `status bar`, `screenblocks`.

Ссылка для просмотра дополнительных совпадений (GitHub UI поиск по репо):  
```text
https://github.com/dron12261games/eternity-fork/search?q=UMAPINFO+OR+DEHACKED+OR+SBARDEF+OR+SKYDEFS&type=code
```
(инструмент выдаёт ограниченный набор совпадений, поэтому лучше добивать вручную через UI).

---

## 2) Архитектурное решение: “ID24 support” как отдельный слой

### 2.1. Ввести режим/флаг “featureset”
ID24 оперирует понятием “feature level” (doom1.9 → … → id24). В Eternity уже есть куча compat/gi-флагов, но для корректной поддержки ID24 удобнее иметь явное:
- `enum feature_level { doom19, limitremoving, bugfixed, boom202, complevel9, mbf, mbf21, dsdhacked, id24 }`
- текущий уровень определяется (в порядке приоритета):
  1) Явная настройка/параметр запуска (если хотите)
  2) **GAMECONF** (если найден и валиден)
  3) эвристика: наличие ID24HACKED (doom version 2024 / новые поля), наличие ID24 JSON lumps и т.п.
  4) дефолт как сейчас в Eternity

### 2.2. Загрузка id24res.wad (ресурсный WAD)
Спека рекомендует: если мод “просит ID24”, загрузить `id24res.wad` *до* основного IWAD, чтобы работали overrides.  
План:
- Добавить механизм поиска `id24res.wad` по “IWAD resolution rules” Eternity (те же пути, что IWAD).
- Включать автозагрузку **только если**:
  - featureset >= id24, или
  - обнаружены ID24 lumps (например `GAMECONF` с executable=id24, или `ID24HACKED` признаки).
- Предусмотреть graceful-degradation: если файла нет — не падать, но корректно обрабатывать missing resources (как Doom: ошибка/замена).

---

## 3) Базовый слой №1: JSON Lump инфраструктура (обязательная для почти всех фич)

**Цель:** единый валидатор/парсер JSON lumps по `JSON_Lump.md` (версия 0.9.2 документа, но используется в остальных спеках как JSON Lump 1.0.0 — вы должны реализовать требования root-структуры строго).

### Шаги реализации
1) **Выбрать JSON библиотеку**:
   - либо встроенная (если в Eternity уже есть),
   - либо добавить зависимость (rapidjson / nlohmann/json — но следить за лицензиями и билдом).
2) Реализовать модуль `id24_jsonlump.*`:
   - `bool ParseJSONLump(lumpIndex, expectedType, maxVersion, outDataNode, outError)`
   - строго:
     - root содержит **ровно**: `type`, `version`, `metadata`, `data`
     - любые лишние поля => ошибка
     - типы значений — строго
     - `type` regex `^[a-z0-9\_-]+$` (lowercase)
     - `version` regex `^(\d+)\.(\d+)\.(\d+)$`
     - версия lump’а **не может быть больше** maxVersion (forward-compat запрещён)
     - `metadata` должен существовать и быть object, но его содержимое игнорируется рантаймом (однако наличие полей `author/timestamp/application` требуется для валидности инструментариями; спеке нужно “must contain”, поэтому лучше валидировать наличие этих ключей, но значения могут быть null).
     - `data` должен быть object и не null
   - JSON lump должен определяться **попыткой полноценного парсинга**, без “первый символ `{`”.
3) Система логирования ошибок:
   - сделать единый формат: “LUMP <name>: JSONLump invalid: …”
   - по спекам многие ошибки должны считаться “error condition”, иногда — без фолбэка (например Interlevel: нельзя падать назад на exitpic/enterpic при ошибке конкретного exitanim/enteranim).
4) Тесты:
   - минимальные unit-тесты на:
     - лишний ключ в root
     - version выше max
     - неверный тип поля
     - metadata отсутствует
     - data отсутствует / null

---

## 4) Базовый слой №2: расширения UMAPINFO (точечные, но нужны для Finale/Interlevel/Mapping)

UMAPINFO у вас уже есть (`xl_umapinfo.*`). Нужно добавить новые поля, описанные в спеках:

### 4.1. `Interlevel_lumps`: поля `exitanim`, `enteranim`
Шаги:
1) В `XL_initRules()` (или аналог) добавить правила:
   - `exitanim` (string)
   - `enteranim` (string)
2) В структуру интермиссии (у вас `intermapinfo_t` в `IN_GetMapInfo`) добавить новые поля:
   - `exitanim`, `enteranim` (строки)
3) В `XL_BuildInterUMapInfo()` дополнить извлечение:
   - сначала `exitanim/enteranim`
   - при рендере victory screen:
     - если определён `exitanim` → пытаться загрузить JSON lump типа `interlevel` версии `1.0.0`
     - если он **невалидный** → это *ошибка*, и по спецификации **нельзя** откатываться на `exitpic`.
     - если поля нет → работать как раньше (`exitpic/enterpic`)

### 4.2. `Finale_lumps`: поле `endfinale`
Шаги:
1) Добавить правило `endfinale` (string) в UMAPINFO parser.
2) В финале-логике:
   - если `endfinale` задан → игнорировать старые `endpic/endbunny/endcast`
   - загрузить JSON lump типа `finale`, версия `1.0.0`
   - реализовать 3 режима: art/bunny/cast + `donextmap`.

### 4.3. `Mapping_Additions`: `bossaction` расширение + `bossactionednum`
В `Mapping_Additions.md`:
- `bossaction` теперь может быть строкой-мнемоникой **или** числом “thing number as defined by DeHackEd”.
- добавлен `bossactionednum` — резолв по editor number (doomednum).

Шаги:
1) Найти где Eternity обрабатывает `bossaction` (в `xl_umapinfo.cpp` есть addRule("bossaction"...)).
2) Расширить парсер:
   - разрешить int-форму для bossaction
   - добавить отдельное поле bossactionednum (int)
3) На уровне логики “boss death triggers”:
   - если bossactionednum: сначала editor→thingtype, потом стандартный механизм.

---

## 5) GAMECONF (JSON lump `type=gameconf`, `version=1.0.0`)

**Это “ранняя” фича:** она влияет на выбор IWAD/PWAD/опций/переводов до инициализации игры.

### Шаги
1) Реализовать поиск `GAMECONF` в каждом загружаемом IWAD/PWAD:
   - порядок: default IWAD → затем PWAD из `-file` в порядке
   - “destructive” поля перезаписывают ранее заданные; pwads — additive; `executable` и `mode` — max.
2) Реализовать сбор окончательного:
   - `executable` (featureset string из списка: doom1.9, limitremoving, bugfixed, boom2.02, complevel9, mbf, mbf21, mbf21ex, id24)
   - `mode` (registered/retail/commercial)
   - `iwad` (только имя файла, путь запрещён)
   - `pwads` (список имён файлов без путей; добавить описанный алгоритм “вставки supporting pwads перед текущим”)
   - `playertranslations` (минимум 4 строки — иначе ошибка)
   - `wadtranslation` (применить перевод ко всем ресурсам IWAD + всем PWAD кроме текущего)
   - `options` (additive; ограничение по version)
3) Критично: GAMECONF в PWAD не должен триггерить рекурсивный парсинг GAMECONF в тех PWAD, которые он добавил (спека запрещает).
4) Интеграция с “autoload id24res.wad”:
   - если `executable == id24` → включать попытку подгрузки id24res.wad до IWAD.

---

## 6) Translations (JSON lump `type=translation`, `version=1.0.0`)

### Шаги
1) Реализовать парсер translation-lump:
   - `table` ровно 256 int (0..255)
   - `name` (DeHackEd mnemonic)
   - `sbarback`, `sbartranslate`, `interback`, `intertranslate`
2) Инициализация переводов в порядке (важно по спецификации):
   1) переводы из Dehacked Thing field `Translation`
   2) переводы из `GAMECONF.playertranslations`
   3) built-in (T_GREEN/T_INDIGO/T_BROWN/T_RED + дополнительные T_YELLOW/T_BLUE/T_NAVY/T_MAGENTA если не overridden)
3) Применение:
   - для игроков: по умолчанию `translation = playertranslations[playernum % count]`
   - для вещей: если `MF_TRANSLATION` выставлен и Thing не имеет явного `Translation` → брать из playertranslations.
4) Поддержка `sbarback/interback`:
   - statusbar face background и interlevel player background должны уметь рендериться с/без применения таблицы (bool флаги).

---

## 7) SKYDEFS (JSON lump `type=skydefs`, `version=1.0.0`)

### Шаги
1) Реализовать загрузчик одного lump `SKYDEFS` (последний в WAD dictionary).
2) Модель данных:
   - root: `skies[]` (или null), `flatmapping[]` (или null)
   - sky: `type(0/1/2)`, `name`, `mid`, `scrollx`, `scrolly`, `scalex`, `scaley`, опционально `fire{palette[], updatetime}`, `foregroundtex{...}`
3) Интеграция с рендерером:
   - `F_SKY1` всегда “default sky behavior”
   - если sky texture объявлен как skydef — при рендере использовать параметры skydef, но **анимация** должна резолвить “текущую текстуру” через систему ANIMATED и применять свойства исходного skydef (как в спеке).
4) Fire skies:
   - симуляция doom_fire_psx, обновление раз в `updatetime`, перезаписывает texture data.
   - priming: нижний ряд = last palette index, и минимум 64 итерации на старте.
5) With Foreground:
   - индекс 0 палитры = 100% прозрачность foreground, остальное — непрозрачно
   - нужен tranmap/таблица прозрачности (в спеке упомянут SKYTRAN.lmp как ресурс; вероятно будет в id24res.wad).

---

## 8) SBARDEF (JSON lump `type=statusbar`, `version=1.0.0`)

Это большой пласт UI, но внедряется независимо от симуляции.

### Шаги
1) Реализовать резолв lump `SBARDEF` (последний) и fallback на старый статусбар если его нет.
2) Ввести виртуальную систему координат 320x200, “compact mode”, правила screenblocks:
   - `barindex = Max(screenblocks - 10, 0)`; clamp при необходимости.
3) Реализовать дерево элементов (transform tree):
   - базовые поля: position x/y, alignment, children[]
   - render flags: tranmap (Boom), translation (ID24 translations)
4) Element types (использовать “первый тип в element”):
   - Canvas, Graphic, Animation, Face, Face Background, Number, Percentage
5) Conditions (все должны быть true; null => “0 условий” => true; пустой массив => ошибка):
   - оружие/слот/предмет/версия/режим/тип сессии/hud mode
6) “ID24 weapons”: id24res.wad добавляет слоты 8-9. В Eternity есть `NUMWEAPONSLOTS=16` (`e_player.h`) — это хорошо: нужно обеспечить корректную работу условий “slot 8/9” и отображения.

---

## 9) Interlevel lumps (JSON `type=interlevel`, `version=1.0.0`)

### Шаги
1) Реализовать загрузку interlevel JSON по имени lump’а из UMAPINFO `exitanim/enteranim`.
2) Виртуальная среда 320x200, backgroundimage + music обязательны.
3) Данные:
   - root { music, backgroundimage, layers(null|array) }
   - layer { anims(array>=1), conditions(null|array) }
   - anim { x,y, frames(array>=1), conditions(null|array) }
   - frame { image, type(bitfield), duration, maxduration }
4) Невыполнение условий:
   - условия вычисляются **один раз при инстанцировании interlevel**; не пересчитывать каждый кадр.
5) Нондетерминизм:
   - RNG допускается (можно использовать `M_Random`).
   - duration округлять до тиков (35 Гц).
6) Интеграция с существующим victory screen кодом:
   - переписать/расширить код, который сейчас использует `exitpic/enterpic` из `XL_BuildInterUMapInfo()`.

---

## 10) Finale lumps (JSON `type=finale`, `version=1.0.0`)

### Шаги
1) Включать через UMAPINFO `endfinale`.
2) Поддержать `type`:
   - 0 art screen (music loop, patch 320x200)
   - 1 bunny scroller (music once, stitchimage, overlay animation)
   - 2 cast roll call (music loop, list castmembers)
3) Cast roll call:
   - имя = DeHackEd mnemonic
   - кадры: duration, sound, tranmap, translation, flip-x
4) `donextmap`: после финале позволить продолжить на next map.

---

## 11) DEMOLOOP (JSON `type=demoloop`, `version=1.0.0`)

### Шаги
1) Резолв единственного lump `DEMOLOOP` (последний).
2) entries[] >= 1, иначе ошибка.
3) entry types:
   - art screen: duration обязателен; primarylump графика; secondarylump музыка (play once)
   - demo lump: primarylump demo; duration игнорируется; музыка по карте как doom1.9
4) wipes:
   - immediate / melt (желательно 160-column melt как в doom1.9)

---

## 12) Mapping Additions (линии 2048+ и прочее)

Это “геймплей + рендер” пласт.

### Шаги
1) Расширить таблицу line specials:
   - 2048..2056: offsets/rotations floor/ceiling
   - 2057..2068 + 2087..2098: music line specials (loop / play once / reset variants)
   - 2069..2074: reset exits (reset inventory on exit)
   - 2075..2081: sector colormap set/clear
   - 2082..2086: scroll front+back sidedefs
2) Рендер “any texture/flat on any surface”:
   - разрешить TEXTURE1/2 на пол/потолок, и flats на стены
   - прозрачность по палитре индекс 0
   - для floors/ceilings TEXTURE1/2: не clamp к power-of-two, 1:1 texel-to-world-unit
3) Music changing через line special:
   - upper texture (front activation) / lower (back activation) = имя music lump
4) Sector tinting:
   - верх/низ текстуры = имя colormap lump
   - если не найдено или не задано → очистить colormap
5) Reserved line special ranges:
   - соблюдать: 2048–4095 (bit 0x0800) как id-defined вне generalized boom range.

---

## 13) ID24HACKED + hashing (самый “рискованный” пласт)

Это лучше делать последним, когда уже понятна модель featureset и есть инфраструктура для расширений.

### 13.1. ID24HACKED расширения DeHackEd
Ключевые требования:
- ID24HACKED базируется на DSDHACKED и расширяет индексы до (2^32 - 1) значений, включая отрицательные.
- Добавляет новые поля (например Thing “Pickup …”, Weapon slot/priorities, Frame tranmap и т.д. — детали в `ID24HACKED.md` и hashing doc).
- Меняет порядок загрузки DEHACKED: все DEHACKED lumps по WAD’ам в порядке загрузки, потом .deh из командной строки.
  - hashing doc уточняет порядок: IWAD (last) → для каждого PWAD (last) → .deh files.

Шаги:
1) Выделить “DeHackEd state” как отдельную структуру, которую можно хэшировать и проверять.
2) Добавить поддержку “Doom version 2024” как сигнал ID24HACKED.
3) Реализовать “associative maps” вместо прямых таблиц:
   - thing/frame/sprite/sound/weapon/ammo: in-order table + map by index
   - spawn map (ID #) для спавна вместо линейного поиска
4) Реализовать корректное создание объектов при первом упоминании (спека снимает правило “alloc range from lowest to highest”).
5) Добавить новые таблицы “ID24 additions” (для вещей/оружия/патронов/звуков/спрайтов/кадров). В спеке сказано, что полный список в отдельном файле “stored with these specifications” — в вашей папке `version_0_99_2_md` его нет, значит:
   - либо он в другом месте репо `doom-cross-port-collab/id24`,
   - либо в `id24res.wad` как данные,
   - либо отдельным приложением/таблицей.  
   Это блокер: без точной таблицы вы не сможете корректно реализовать “in-order table” для ID24 сущностей.
6) Интеграция с Eternity-EDF:
   - Eternity уже имеет динамику EDF (things/states/weapons). Нужно решить: ID24HACKED будет:
     - напрямую менять базовые массивы `states/mobjinfo/weapons` (как классический DeHackEd), или
     - транслировать в EDF-структуры.
   - Практически проще сначала “классический путь” и минимально затрагивать EDF.

### 13.2. Hashing (Valid hashes)
Шаги:
1) Реализовать FNV-1a 64-bit:
   - init 14695981039346656037
   - prime 1099511628211
   - ints в little-endian
   - undefined/empty strings => строка `"null"`
2) Поддержать поле DeHackEd root: `Valid hashes` — CSV upper-hex uint64.
3) Логика:
   - первый patch всегда применяется (baseline tables)
   - если уже был patch: следующий применяется только если currentHash ∈ Valid hashes.
4) Условия полей для hashing зависят от feature level (в doc есть таблицы условий).
5) Важно: в doc есть NOTE, что в официальном релизе таблицы Thing/Frame были неполные (на момент публикации), из-за чего сообщество не могло совпасть хэшами — это историческая оговорка, но ваша реализация должна соответствовать спекам.

---

## 14) План внедрения по этапам (рекомендуемая последовательность PR’ов/веток)

Чтобы не “утонуть”, лучше дробить на логические инкременты:

### Этап A — инфраструктура
A1) feature_level + обнаружение ID24 (через GAMECONF/лумпы)  
A2) JSONLump валидатор + тесты  
A3) автопоиск/загрузка id24res.wad (за флагом)

### Этап B — GAMECONF + Translations
B1) GAMECONF парсинг и влияние на загрузку IWAD/PWAD/опций  
B2) Translations JSON + применение к игрокам/вещам/рендеру backgrounds

### Этап C — визуальные JSON-фичи
C1) SKYDEFS (normal) → затем foreground → затем fire  
C2) SBARDEF (минимум: Graphic/Number/Face) → затем conditions/animations/translations/tranmap  
C3) Interlevel lumps (фон+музыка+анимы)  
C4) Finale lumps (art) → bunny → cast roll

### Этап D — DEMOLOOP
D1) DEMOLOOP art screens  
D2) DEMOLOOP demo lumps + wipes

### Этап E — Mapping additions
E1) line specials 2048..2056 (floor/ceiling transforms)  
E2) music line specials  
E3) sector colormap  
E4) reset exits  
E5) scroll both sidedefs + “any surface texture rules”

### Этап F — ID24HACKED
F1) “detector” ID24HACKED + doom version 2024  
F2) associative maps / расширенные индексы  
F3) hashing (Valid hashes)  
F4) новые поля weapon/ammo/thing/frame (slot priorities, pickup fields, tranmap frame и т.д.)  
F5) таблицы “ID24 additions” (как только найдёте источник)

---

## 15) Вопросы, без которых план “на 100% реализуемый” не замкнётся

1) **Какую ветку/коммит Eternity вы считаете базовой?** (сейчас я видел ссылки на `master` и один commit `911b198f…` из поиска).  
2) **Есть ли в Eternity уже JSON библиотека?** (иначе нужно выбирать/добавлять).  
3) **Где взять “полные таблицы новых объектов ID24”**, о которых говорит `ID24HACKED.md` (“separate file stored with these specifications”) — в указанной папке `version_0_99_2_md` их нет. Хотите, я поищу их в репозитории `doom-cross-port-collab/id24` (вне этой папки) и составлю точный план интеграции таблиц?  
4) Вам нужна поддержка **полного ID24** или достаточно “ID24 map/visual features без ID24HACKED”? (это резко влияет на объём).

Если ответите на эти 4 пункта — я смогу превратить план выше в **ещё более конкретный**, с привязкой к предполагаемым файлам Eternity (куда именно добавлять модули, какие структуры расширять, и какие подсистемы трогать в каком порядке).

## @DRON12261

1) Ветка master
2) Да, в id24_json
3) Попробуй поискать там везде. Так же там есть еще папка @doom-cross-port-collab/id24/files/wad_resources и @doom-cross-port-collab/id24/files/source_code_reference . Думаю стоит обратить внимание на их содержимое, т.к. там содержатся примеры файлов id24, которые потом EE должен успешно обрабатывать, и кажется еще примеры какого-то кода, связанного с id24.
4) Полный ID24

Я могу как-то убрать ограничение на 10 совпадений в code-searh для тебя, чтобы ты мог проводить более детальный анализ репозитория с кодом? Как вообще я могу повлиять на то, чтобы у тебя был полный доступ ко всему коду репозитория для анализа?

## Copilot

Да — имеет смысл смотреть `wad_resources/` и `source_code_reference/`: это, по сути, **эталонные входные данные** (WAD + JSON + ресурсы) и **референсная реализация таблиц/поведения** для ID24.

Ниже: (1) что я уже нашёл в этих папках, (2) как это использовать для реализации “полного ID24”, (3) ответ про лимит 10 совпадений и как дать мне “полный доступ”.

---

## 1) Что лежит в `doom-cross-port-collab/id24/wad_resources` (и зачем это EE)

Содержимое папки (на ветке `main` репо `doom-cross-port-collab/id24`, но у тебя указан commit `db9b334…`; набор файлов совпадает по сути):

- `SBARDEF.json` — пример/эталон **SBARDEF** (statusbar JSON) для классического набора.
- `SBARDEF24.json` — пример/эталон **SBARDEF** c ID24-расширениями (слоты 8–9, дополнительные элементы и т.п.).
- `SKYTRAN.lmp` (65536 байт) — таблица прозрачности, используемая в спеках (в т.ч. для SKYDEFS foreground).
- `STAMMO24.png`, `STARMS24.png` — графика статусбара под ID24.
- `WATERMAP.png` + `watermap.xcf` — судя по названию, ресурс для watermap/colormap тестов (или вспомогательный арт для примера).
- `rum-and-raisin-id24test.wad` — **ключевой тестовый WAD**. EE должен уметь его грузить и корректно интерпретировать его lumps (JSON + mapping additions + возможно id24hacked).

Это означает: в плане реализации нужно завести отдельный “acceptance suite”: *запуск EE с этим WAD и сравнение поведения/скриншотов/логов*.

---

## 2) Что лежит в `source_code_reference` (где искать таблицы “ID24 additions”)

Содержимое папки:

- `id24data.cpp` (очень большой файл ~160 KB)
- `id24data.h`
- `id24thinker.h`

Это почти наверняка и есть “тот самый” источник истины, которого не хватало в `version_0_99_2_md`: таблицы новых вещей/кадров/оружия/звуков/спрайтов/пикапов и т.д. + вспомогательные структуры.

Следующий шаг (я предлагаю сделать это прямо сейчас, если ты ок): я открою `id24data.h/.cpp` и:
1) вытащу **конкретные массивы/таблицы** (Thing/Frame/Sprite/SFX/Music/Weapons/Ammo/Strings…),
2) составлю точную карту “какие элементы добавляет ID24 и какие индексы/имена/значения должны быть”,
3) на базе этого обновлю твой план “полный ID24” уже не абстрактно, а с прямой привязкой: *какие структуры в Eternity расширяем и какими значениями*.

---

## 3) В Eternity уже есть `id24_json` и частично `id24_demoloop`

Хорошая новость: в `dron12261games/eternity-fork` уже есть:

- `source/id24_json.cpp/.h` — парсер JSON lumps на `nlohmann::json`, с:
  - парсингом version (`major/minor/revision`),
  - валидацией `type` (lowercase + цифры + `_`/`-`),
  - проверкой root-структуры (ровно 4 поля: type/version/metadata/data).
- `source/id24_demoloop.cpp/.h` — уже есть реализация парсинга DEMOLOOP через `id24::ParseJSONLump`.

Это значит, что для остальных JSON-фич (SBARDEF/SKYDEFS/Interlevel/Finale/Translations/GAMECONF) можно строиться **на существующем API**, а не писать заново.

---

## 4) Ответ: можно ли убрать лимит “10 совпадений” и как дать мне полный доступ к коду?

### 4.1. Про лимит 10 совпадений
Этот лимит — **ограничение инструмента поиска по коду**, которым я пользуюсь в чате. Ты “с твоей стороны” его обычно **не можешь отключить**, потому что это не GitHub-лимит, а лимит конкретного интеграционного инструмента.

Но это *не означает*, что у меня нет доступа к остальному коду — просто “поиск” отдаёт мало результатов за раз.

### 4.2. Что реально помогает (практические варианты)
1) **Дать мне прямые ссылки на нужные файлы/папки** (GitHub URL).  
   Тогда я могу читать содержимое через `githubread` без reliance на code-search.

2) **Сказать, какие подсистемы важнее всего**, и я буду открывать каталоги и файлы “деревом”:
   - `source/` → `r_*.cpp`, `st_*.cpp`, `wi_*.cpp`, `f_*.cpp`, `p_*.cpp`, `w_*.cpp` и т.д.
   Это медленнее, чем глобальный поиск, но даёт 100% покрытие.

3) Если ты хочешь максимально ускорить анализ:
   - скинь **список ключевых файлов EE**, которые ты считаешь точками входа (например WAD loader, статусбар, рендер неба, finale/intermission).
   - либо дай “карта проекта” (если есть документ/README по архитектуре).

4) Самый простой “форс-мультипликатор”:
   - ты запускаешь у себя `ripgrep`/IDE search по репо Eternity и даёшь мне **короткие списки путей**, где встречаются ключевые токены: `SBARDEF`, `SKYDEFS`, `ID24HACKED`, `GAMECONF`, `translation`, `interlevel`, `finale`, `Valid hashes`.  
   Дальше я прицельно открою эти файлы и составлю план/патчи.

---

## 5) Что делаем дальше (предлагаю конкретный следующий шаг)

Чтобы перейти от общего плана к “плану с конкретными таблицами и точками интеграции”, нужно прямо сейчас разобрать `source_code_reference/id24data.cpp` и `id24data.h`.

Если ок — скажи “да”, и я:
1) открою эти файлы,
2) извлеку оттуда **полный перечень ID24 additions** (то, что нужно для ID24HACKED и совместимости),
3) затем обновлю пошаговый план внедрения ID24 в EE, добавив:
   - какие именно новые вещи/оружия/слоты/кадры должны появиться,
   - как их привязать к существующим `mobjinfo/states/weapons/sounds` в Eternity,
   - и как построить hashing (Valid hashes) так, чтобы совпадало с референсом.

С твоей стороны пока достаточно подтвердить: **используем именно commit `db9b3348912ee360f935999ecb96b3c142e8774a` как “источник истины”**, или можно ориентироваться на `main` репо `doom-cross-port-collab/id24` (если там есть изменения после этого коммита).

## @DRON12261

Да

Думаю лучше ориентироваться на main

## Copilot

Принято: ориентируемся на `main` репозитория `doom-cross-port-collab/id24`.

### Важное уточнение по “main”
Инструмент `githubread` фактически открыл файлы по commit `db9b334…` (это тот, что ты уже присылал). Такое бывает из‑за того, что запросы через API в этой среде иногда резолвят “main” в конкретный pinned commit/снимок. Для плана это не критично, но чтобы строго следовать “main”, я дальше буду:
- опираться на **содержимое, которое вижу**, и
- когда понадобится (особенно по таблицам additions), я попрошу тебя подтвердить: “это актуально на main”, либо ты дашь ссылку на конкретный SHA на main.

---

## 1) Что дают `id24data.h / id24data.cpp` для “полного ID24” (ключевые находки)

### 1.1. ID24 additions реально определены через “таблицы объектов”
В `source_code_reference/id24data.h` описаны структуры, которые явно соответствуют расширениям ID24 и ID24HACKED:

- **Уровни фич** `gamefeatures_t`:
  `features_vanilla ... features_mbf21_extended, features_id24`  
  Это подтверждает, что “featureset” в реализации лучше вести как enum и сравнивать.

- **Новые типы ammo/weapon** как *отрицательные индексы*:
  - `id24ammotype_t`: `am_fuel = -1879048192`
  - `id24weapontype_t`: `wp_incinerator = -1879048192`, `wp_calamityblade = -1879048191`

- **state_t** (frames) включает:
  - vanilla-поля (sprite/frame/tics/action/nextstate/misc1/misc2)
  - MBF21 flags + 8 аргументов
  - ID24 extension: `tranmaplump` (строка) — это прямо из ID24HACKED: per-frame tranmap.

- **mobjinfo_t** (things) включает:
  - vanilla
  - MBF21 (fastspeed, meleerange, infighting/projectile/splash groups, mbf21flags, ripsound)
  - ID24 extensions:
    - `id24flags`
    - respawn tuning: `minrespawntics`, `respawndice`, `dropthing`
    - **pickup-метаданные**: `pickupammotype`, `pickupammocategory`, `pickupweapontype`, `pickupitemtype`, `pickupbonuscount`
    - `pickupsound`
    - `pickupstringmnemonic` (DeHackEd string key)
    - `translationlump` (имя JSON translation)

- **ammoinfo_t** (ammo table) включает ID24 расширенные количества: initial/max upgraded/box/backpack/weapon/dropped*/deathmatchweaponammo + `skillmul[sk_max]`.

- **weaponinfo_t** включает ID24 расширения:
  - `slot`, `slotpriority`, `switchpriority`
  - `initialowned`, `initialraised`
  - `carouselicon`
  - conditional switching rules:
    `allowswitchifownedweapon`, `noswitchifownedweapon`, `allowswitchifowneditem`, `noswitchifowneditem`

Это очень круто: теперь “полный ID24” в EE можно реализовать **не угадывая** поля — все ключевые расширения уже “на поверхности”.

### 1.2. `id24data.cpp` действительно содержит большие массивы additions
Я вижу начало:
- `id24sprites[]` — список новых sprite mnemonics, с индексами типа `-1879048192` и т.д.
- `id24states[]` — огромный список новых состояний/фреймов, с:
  - отрицательными statenum
  - ссылками на action-функции (`A_Scream`, `A_Fall`, `A_MonsterProjectile`, `A_SpawnObject` и т.д.)
  - и местами ссылками на новые звуки/спрайты через отрицательные индексы.

То есть таблицы additions “есть”, но их надо **полностью вычитать** и перенести в EE (или сделать генерацию/импорт).

---

## 2) Что даёт `id24thinker.h` (и что это означает для EE)

`source_code_reference/id24thinker.h` — это референс про **type-safe thinkers / actionf_t**, и там прямым текстом сказано, что ID24 требует type/invocation safety и что “раньше выключали thinker через null pointer, а теперь через флаг”.

Для Eternity Engine это почти наверняка НЕ требование формата WAD, а требование референс-порта (или предлагаемый подход), но есть два практических вывода:

1) **ID24 additions активно используют экшены/thinkers**, поэтому “поломать” сигнатуры action-функций нельзя. В EE уже есть action pointers в `state_t` (`info.h`), и DeHackEd/BEX меняют codeptr’ы — значит нужно просто убедиться, что:
   - ID24HACKED не сможет поставить “некорректный” codeptr для сигнатуры,
   - а при расширениях (например A_MonsterProjectile с аргументами) аргументы корректно парсятся и применяются.

2) Если ты хочешь 100% строгость “как в референсе”, можно добавить дополнительные runtime-проверки при вызове action pointers (но это, вероятно, опционально; спеке формата это обычно не требуется).

---

## 3) Как обновляется план реализации “полного ID24” именно для EE (конкретизация)

Ниже — **обновлённый план**, уже привязанный к найденным additions и полям.

### Этап 1 — “Feature level” + загрузка id24res
1) Ввести `gamefeatures_t`/аналог в EE и режим `features_id24`.
2) Детекция ID24 через `GAMECONF.executable == "id24"` и/или presence JSON lumps.
3) При `features_id24` — попытка autoload `id24res.wad` (как в твоём раннем плане).

### Этап 2 — ID24HACKED data model в EE (ядро)
Сейчас главный пробел: в EE DeHackEd вероятно работает “по старому” (фиксированные диапазоны, таблицы). ID24 требует associative maps и unlimited ranges.

С учётом `id24data.h`:
1) Расширить внутренние структуры, чтобы хранить ID24-поля:
   - `state_t` → добавить `tranmaplump` (строка/имя lump’а) или эквивалент (в EE это может быть `const char*` + cached pointer на tranmap).
   - `mobjinfo_t` → добавить ID24-pickup поля + respawn fields + translation reference.
   - `weaponinfo_t` → добавить slot/switch priorities + initial owned/raised + carouselicon + allow/no-switch rules.
   - `ammoinfo` → добавить все ID24 количества.
2) Реализовать поддержку **отрицательных индексов** как “ID24-added objects”:
   - по референсу видно, что additions используют значения `-1879048192 ...` (это похоже на 0x90000000 со знаком, т.е. зарезервированный диапазон).
   - Нужно определить правило: *любое значение < 0* — потенциально “расширенный индекс”, который не должен идти в стандартный массив напрямую, а должен резолвиться через map.
3) Ввести таблицы:
   - `SpriteById`, `StateById`, `ThingById`, `WeaponById`, `AmmoById`, `SoundById`, `StringByMnemonic`…
   - с возможностью “ленивого создания” записей (как требует ID24HACKED).

### Этап 3 — Импорт ID24 additions (из `id24data.cpp`)
Тут два пути:

**A) “Жёстко” перенести таблицы в EE**
- скопировать (или сгенерировать) массивы id24sprites/id24states/… в `source/id24_data_tables.*` внутри EE
- на старте движка, при `features_id24`, зарегистрировать additions в свои maps.

**B) “Сборка из WAD”**
- положиться на `id24res.wad` как источник графики/звуков, но *таблицы индексов/поведения* всё равно должны быть в коде движка (потому что это формат/семантика, не только ресурсы).

Для “полного ID24” без гадания правильнее A: таблицы как code truth + ресурсы из id24res.wad.

### Этап 4 — ID24HACKED parsing + hashing
С имеющимися структурами из `id24data.h` будет проще:
1) Реализовать новые поля DeHackEd:
   - для frames: `tranmaplump`
   - для things: pickup* поля + translationlump
   - для ammo: expanded ammo quantities
   - для weapons: slot priorities / switch priorities / carouselicon / initialowned, initialraised / allow/no switch.
2) Реализовать “Valid hashes” (FNV-1a 64-bit) и gating применения патчей.

### Этап 5 — JSON lumps (GAMECONF/Translations/SKYDEFS/SBARDEF/Interlevel/Finale)
Тут у тебя уже есть `id24_json` и `id24_demoloop`; расширяем по аналогии:
- на каждый тип — `ParseJSONLump(..., "SBARDEF"/"SKYDEFS"/..., maxver={1,0,0}, parseFunc, ...)`
- и затем интеграция в рендер/интерфейс.

### Этап 6 — Acceptance тесты из `wad_resources`
1) Добавить “известные тестовые WAD” в документацию дев-сборки:
   - `rum-and-raisin-id24test.wad`
2) Добавить режим логирования “id24 validate”:
   - отчёт какие JSON lumps найдены и валидны
   - какие additions зарегистрированы (кол-во и диапазоны индексов)
   - какие ID24HACKED patches применены/отклонены (по hashing)

---

## 4) Следующее действие (чтобы сделать план ещё точнее)
Чтобы “закрыть” additions полностью, нужно вытащить из `id24data.cpp` остальные массивы (не только sprites/states). По логике там должны быть также:
- `id24mobjinfo[]`
- `id24weapons[]`
- `id24ammo[]`
- `id24sounds[]`
- возможно `id24strings[]` / mappings

Я могу продолжить анализ, но мне нужно “дробить” чтение файла, потому что он большой. Скажи, пожалуйста, что приоритетнее вытащить в первую очередь:

1) **Таблицы things/weapons/ammo** (для ID24HACKED core)
или
2) **Таблицы sounds/strings/sprites** (для ресурсов/переводов/статусбара)

Я бы начал с (1), потому что без них ID24HACKED и weapon slots 8–9 не замкнутся.

## @DRON12261

Давай продолжим анализ этих файлов так, как ты посчитаешь более эффективным.

## Copilot

Ок, продолжаю самым эффективным способом: сначала фиксируем **все доступные “входные артефакты” (spec/examples/reference)**, затем извлекаем из них **требования к EE** и **минимальный набор подсистем**, который нужно реализовать, чтобы WAD’ы из `wad_resources` реально работали.

Ниже — результаты следующего шага анализа (по тем папкам/файлам, до которых я уже добрался инструментами), и что это означает для плана.

---

## A) Быстрый аудит “что вообще есть” в репозитории id24 (на указанном SHA)

### `source_code_reference/`
Успешно вижу 3 файла:
- `source_code_reference/id24data.h`
- `source_code_reference/id24data.cpp`
- `source_code_reference/id24thinker.h`

`id24data.cpp` большой (~160 KB) и содержит **таблицы additions** (как минимум `id24sprites[]` и огромный `id24states[]`). Это почти наверняка “ядро” того, что EE должен встроить/смоделировать для *полного* ID24.

### `wad_resources/`
Список файлов, которые EE в итоге должен корректно обработать в составе тестов/ресурсов:
- `wad_resources/SBARDEF.json`
- `wad_resources/SBARDEF24.json`
- `wad_resources/SKYTRAN.lmp` (65536 bytes — классический TRANMAP размер)
- `wad_resources/STAMMO24.png`
- `wad_resources/STARMS24.png`
- `wad_resources/WATERMAP.png`
- `wad_resources/rum-and-raisin-id24test.wad` (тестовый WAD)
- `wad_resources/watermap.xcf` (исходник, в рантайме не нужен)

Я уже открыл `SBARDEF24.json` и он **очень показательный** (см. ниже).

> Примечание: я попытался “листинг” `version_0_99_2_md`, но tool вернул “resource not found / no access”. Это похоже на сбой запроса (инструмент полез почему-то в другой repo path). Мы это обойдём: я сделаю точечные `Get file ...` запросы по конкретным путям/именам файлов, но мне нужно, чтобы ты подсказал названия файлов в `version_0_99_2_md` (или дал скрин/листинг), либо я попробую другим способом вытащить содержимое директории.

---

## B) Ключевые выводы из `id24data.cpp`: что это значит для EE

### B1) ID24 additions используют “зарезервированный отрицательный диапазон”
В `id24sprites[]` индексы начинаются с `-1879048192` и идут вверх. Это соответствует 0x90000000 как signed int32. То есть:
- **любые отрицательные индексы** в полях типа `sprite`, `statenum`, `sound`, `mobjtype`, `ammotype`, `weapontype` *могут быть* ссылками на ID24-added таблицы,
- нельзя просто делать `array[index]`.

**Следствие для плана:** в EE нужна прослойка резолвинга “id → запись” через map/хэш-таблицы для ID24 additions.

### B2) `id24states[]` вшивает MBF21-аргументы и использует “сложные” экшены
По строкам, которые видны в начале файла:
- присутствуют action’ы типа `A_MonsterProjectile`, `A_SpawnObject`, `A_RadiusDamage`, `A_PlaySound`, `A_Scream`, `A_Fall`, `A_Pain` и т.п.
- у `A_MonsterProjectile` и `A_SpawnObject` явно заполнены аргументы (`arg1..arg8`), включая фиксированную арифметику (например `-524288`, `11468800` и т.д.)

**Следствие:** даже если EE уже поддерживает MBF21, нужно проверить:
1) что **аргументы действительно учитываются** в экшенах (в некоторых портах часть MBF21 action args реализована не полностью),
2) что **ID24-added states** “встраиваются” в state machine так же, как ванильные.

---

## C) Ключевые выводы из `wad_resources/SBARDEF24.json`: это “must-have” для полного ID24

Я открыл `SBARDEF24.json`. Важнейшие наблюдения:

### C1) SBARDEF использует *отрицательные индексы* ammo прямо в параметрах UI
Внутри statusbar definition встречается:
- `"type": 3, "param": -1879048192`  
- `"type": 5, "param": -1879048192`

То есть statusbar просит отрисовать:
- текущее/макс (или аналог) **для ammo с ID24 индексом** (в данном случае это `am_fuel` из `id24data.h`).

**Следствие для EE:** поддержка ID24 — это не только DeHackEd и gameplay, но и **UI слой**:
- функции получения ammo по индексу должны принимать **int32 ammoid**, а не enum 0..NUMAMMO-1,
- statusbar должен уметь запрашивать “ammo count” по этому ID, и если ID отрицательный — резолвить через id24 ammo table.

### C2) SBARDEF24 также использует условие/проверку на “ammo icon patch” для ID24 ammo
Есть кусок, где отображается `"patch": "FTNKA0"` при condition `"param": -1879048192`.  
Это означает, что условия/логика statusbar тоже могут зависеть от ID24 ammo id.

**Следствие:** condition evaluation в SBARDEF (или твоём id24_json аналоге) тоже должен быть “id-aware”.

---

## D) Что я сделаю дальше (следующий эффективный шаг анализа)

1) **Дожать `id24data.cpp` до конца по структуре:** найти какие ещё таблицы там есть кроме sprites/states:
   - mobjinfo additions?
   - weaponinfo additions?
   - ammoinfo additions?
   - sfxinfo additions?
   - string mnemonics?
   
   Для этого я сделаю точечные поисковые запросы по тексту файла через `lexical-code-search` уже в твоём репозитории EE (когда перейдём к интеграции), но внутри `id24data.cpp` в этой среде проще: мне нужно вычитать нужные фрагменты — я буду запрашивать файл повторно, но с уточнениями (например “покажи участок где определяется id24ammo / id24weapons”). В текущем виде `githubread` возвращает начало и дальше режет вывод, так что придётся “выцеливать” нужные места.

2) Параллельно я начну привязку требований к Eternity Engine:
   - где в EE лежат `state_t`, `mobjinfo_t`, `weaponinfo_t`, `ammoinfo_t`,
   - как устроен DeHackEd/BEX парсер,
   - как устроена система HUD/statusbar (и как туда встраивается `SBARDEF`-подобная JSON-логика).

Но для этого мне нужно прочитать код именно твоего repo:
`dron12261games/eternity-fork` (ветка `master`).

---

## Вопрос, без которого я не смогу продолжить “сквозной” план до уровня файлов/модулей EE
Можешь дать ссылку на репозиторий EE в формате GitHub URL (ветка master), например:
`https://github.com/dron12261games/eternity-fork/tree/master`
или подтвердить, что это приватный repo и нужно другое подключение?

Как только у меня будет корректная ссылка/доступ, я:
- найду точки расширения в EE,
- и составлю уже **реально пошаговый план по конкретным файлам EE** (какие структуры менять, где вводить maps для отрицательных индексов, куда подключать таблицы id24data.cpp, какие тесты гонять на `rum-and-raisin-id24test.wad`).

## @DRON12261

Попробуй по этой ссылке: https://github.com/dron12261games/eternity-fork/tree/master

## Copilot

Да, ссылка рабочая — у меня есть доступ к `dron12261games/eternity-fork` (ветка `master`), и я уже нашёл в коде важные точки, которые напрямую связаны с ID24.

## 1) Важный статус: GitHub “code search” у меня действительно ограничен
Мои результаты `lexical-code-search` **ограничены первыми 10 совпадениями**, поэтому они *могут быть неполными*. Чтобы ты мог сам посмотреть все совпадения в GitHub UI, вот ссылки на поиск:

- По `SBARDEF` в репозитории:  
  `https://github.com/dron12261games/eternity-fork/search?q=SBARDEF&type=code`
- По `id24_json`:  
  `https://github.com/dron12261games/eternity-fork/search?q=id24_json&type=code`
- По `ParseJSONLump`:  
  `https://github.com/dron12261games/eternity-fork/search?q=ParseJSONLump&type=code`
- По `DeHackEd`:  
  `https://github.com/dron12261games/eternity-fork/search?q=DeHackEd&type=code`

(Эти ссылки — самый практичный способ обойти лимит: ты видишь все результаты, я — только первые 10.)

## 2) Уже найденные “якорные” файлы в Eternity Engine, которые понадобятся для поддержки ID24

### 2.1. В EE уже есть базовый парсер “ID24 JSON lumps”
Найдены:
- `source/id24_json.h` citeturn2search0  
- `source/id24_json.cpp` citeturn2search0turn2search2  

Там реализовано:
- парсинг `version` вида `major.minor.revision`,
- проверка полей корневого объекта (`type`, `version`, `metadata`, `data`),
- механизм warning/error callback. citeturn2search2  

Это очень хорошо: значит “проводка” под ID24 JSON формат уже начата (или даже частично завершена), и план будет строиться поверх неё.

### 2.2. В EE уже есть пример конкретного ID24 JSON-формата: `ID24 DEMOLOOP`
Найден файл:
- `source/id24_demoloop.cpp` citeturn2search2  

Он использует `id24_json` и читает JSON-лумп через `ParseJSONLump`. Это будет эталонный паттерн, как интегрировать **другие** JSON-lumps из спецификации ID24.

### 2.3. DeHackEd/BEX в EE есть и довольно развитый
Найдены:
- `source/d_deh.cpp` (таблица блоков `Thing/Frame/Pointer/...` и BEX блоки) citeturn2search4  
- `source/d_deh.h` citeturn2search4  
- `source/d_dehtbl.h/.cpp` (хеш-таблицы для строк/кодпойнтеров/спрайтов и т.п.) citeturn2search4  

Это означает: часть “ID24 additions” можно/нужно будет подключать либо как:
- additive dehacked (у EE есть концепция “additive”), либо
- через EDF/MetaTable механизмы (в EE они развиты), либо
- прямым “нативным” расширением таблиц.

### 2.4. Очень важное: в EE уже есть инфраструктура “sprite by DEHNum” (hash table)
Например `source/e_sprite.cpp` содержит:
- `E_SpriteNumForDEHNum(int num)` для additive dehacked sprite-number lookup. citeturn2search4  

Это почти “родной” мостик к тому, что мы видим в reference-коде id24, где ID24 additions живут в особом диапазоне индексов.

---

## 3) Почему мои прошлые `githubread`-поиски “по словам” не сработали
`functions.githubread` не поддерживает endpoint GitHub code search (`/search/code`), поэтому запросы типа “Search for SBARDEF in code” вернули `This endpoint is not supported...`.

Решение:
- для **поиска** по репо используем `lexical-code-search` / `semantic-code-search`;
- для **чтения конкретного файла** — `githubread getfile` (когда известен путь).

---

## 4) Что мне нужно от тебя прямо сейчас, чтобы я смог перейти к “полному детальному плану”
Ты просил: “Прежде чем формировать план подробно изучи все файлы из `version_0_99_2_md` (полная спецификация формата id24) + примеры/код”.

С примерами (`wad_resources`) и reference-кодом (`source_code_reference`) я уже начал, но **со спецификацией `version_0_99_2_md` пока проблема**: у меня не получилось получить листинг директории этим инструментом (он не умеет “рекурсивно перечислить” папку произвольно).

Поэтому самый быстрый путь:

1) Ты даёшь **список файлов** внутри `version_0_99_2_md` (названия), или просто копируешь сюда вывод `ls`/скрин/текст со страницы GitHub.
2) Я дальше точечно вытягиваю каждый файл через `githubread` (Get file …) и строю план “по спецификации”, а не по догадкам.

Если хочешь без ручного списка: скажи, какие там расширения/структура (например: `*.md`, `*.json`, есть ли `index.md`/`README.md`) — я попробую угадать “входной файл” и от него плясать, но лучше список.

---

## 5) Ответ на твой вопрос “как дать тебе полный доступ / убрать лимит”
- **Убрать лимит выдачи `lexical-code-search` (10 результатов) ты напрямую не можешь**: это ограничение инструмента в этой среде.
- Что реально помогает:
  1) Давать **более узкие запросы** (с `path:source`, `language:C++`, поиск уникальных строк).
  2) Давать **точные пути файлов** — тогда я читаю файл целиком через `githubread getfile`.
  3) Если репозиторий приватный — обеспечить доступ токеном/интеграцией (но здесь он публичный/доступный, раз я читаю `source/id24_json.cpp`).
  4) Ты можешь сам открыть GitHub UI search (по ссылкам выше) и подсказать “вот ещё важные файлы из результатов” — это самый быстрый способ обойти лимит.

---

Если пришлёшь список файлов из `version_0_99_2_md`, я в следующем сообщении:
- вытащу каждый файл,
- составлю **полный пошаговый план поддержки “полного ID24”** в EE, уже с привязкой к реальным модулям `source/` (DeHackEd/EDF/MetaTable, таблицы states/things/weapons/ammo, UI/statusbar, загрузка lumps, тест-пак на `rum-and-raisin-id24test.wad`).

## @DRON12261

Список файлов (все в *.md формате):
DEMOLOOP.md
Finale_lumps.md
GAMECONF.md
ID24HACKED.md
ID24HACKED_hashing.md
ID24_formal_specification_0.99.2.md
Interlevel_lumps.md
JSON_Lump.md
Mapping_Additions.md
SBARDEF.md
SKYDEFS.md
Translations.md

## Copilot

Ниже — **полный детальный пошаговый план** реализации **полной поддержки ID24** в твоём форке **Eternity Engine** (`dron12261games/eternity-fork`, ветка `master`) на основе изученных спецификаций из `version_0_99_2_md` и уже найденных точек интеграции в коде EE.

---

## 0) Что именно считается “полной поддержкой ID24” (границы работ)
Согласно формальной спецификации ID24 0.99.2, **ID24 = супермножество**: Vanilla Doom, Boom, MUSINFO, MBF, DEHEXTRA, MBF21, DSDHACKED, UMAPINFO, плюс “major features” в отдельных документах (DEMOLOOP, Finale lumps, GAMECONF, Mapping additions, ID24HACKED + hashing, Interlevel lumps, SBARDEF, SKYDEFS, Translations). Также упомянуты **id24res.wad** как supporting data и дополнительные **музыкальные форматы** (OGG и tracker). citeturn3search0

План ниже строится так, чтобы EE:
1) корректно определял/включал режим **featureset id24**;
2) грузил (по возможности автоматически) `id24res.wad`;
3) понимал и применял все новые JSON-lumps;
4) поддерживал ID24HACKED (расширения DeHackEd) + hashing;
5) поддерживал mapping additions;
6) имел тестовый контур на примерах (из `wad_resources`) и (по возможности) сверку с `source_code_reference`.

---

## 1) Базовая инфраструктура: JSON-lumps как “платформа” для новых фич

### 1.1. Убедиться, что парсер JSON-lump соответствует спецификации
Спецификация JSON-lump требует:
- парсить JSON “по-настоящему” (без эвристик вида “первый символ `{`”),  
- корневой объект обязан иметь **ровно** `type`, `version`, `metadata`, `data`, и никаких лишних полей;  
- при версии выше поддерживаемой — ошибка (forward compatibility не допускается). citeturn9search0

В EE уже есть реализация `ParseJSONLump` (в `source/id24_json.*`), которая:
- парсит версию `major.minor.revision`,
- проверяет структуру root (`type/version/metadata/data`),
- возвращает `UNSUPPORTED_VERSION`, если версия выше `maxversion`,
- имеет систему warnings/errors. citeturn2search2turn2search0

**Шаги:**
1) Привести все новые ID24 JSON-lumps к единому паттерну подключения:  
   - найти lump по имени (обычно “единственный” lump — см. каждую спецификацию),
   - прочитать raw data,
   - вызвать `ParseJSONLump(raw, size, lumptype, maxVersion, parseFunc, context, warningFunc)`.
2) Сделать общую “обвязку” предупреждений: единый формат сообщений, чтобы моддеру было ясно *какой lump* и *какое поле* некорректно.
3) Стандартизировать “поведение при ошибке”:
   - если lump объявлен authoritative (например, некоторые поля в UMAPINFO дополнениях), и JSON-lump некорректен — это должно считаться **ошибкой**, без fallback на старые поля (там, где это прямо запрещено спецификацией; пример — Interlevel lumps для `exitanim/enteranim`). citeturn7search0

---

## 2) Supporting data: `id24res.wad` и порядок загрузки ресурсов

Спецификация ID24 рекомендует при включенном ID24:
- попытаться найти/загрузить `id24res.wad` **до** основного IWAD, чтобы overrides работали правильно. citeturn3search0

В EE уже есть:
- поиск `id24res.wad` при сканировании директорий IWAD’ов (`D_CheckPathForWADs` запоминает `gi_path_id24res`, если видит файл `id24res.wad`). citeturn10search0  
- конфиг/меню-переменная `pwad_id24res`, позволяющая пользователю указать путь вручную. citeturn10search0

**Шаги:**
1) Ввести чёткое правило “когда грузим id24res.wad”:
   - минимум: когда GAMECONF/UMAPINFO/DEHACKED/любая ID24 JSON-фича требует featureset `id24`.
2) Определить точный порядок:
   - `id24res.wad` должен подгружаться **до** IWAD или по крайней мере в “ранней фазе”, чтобы ресурсы (SBARDEF, SKYDEFS, TRANSLATION lumps, новые спрайты/текстуры и т.д.) были доступны при инициализации подсистем (рендер/статусбар/небо).
3) Поведение при отсутствии `id24res.wad`:
   - не падать, но честно логировать, что часть ресурсов может отсутствовать (а дальше уже стандартное поведение EE: missing sprite/error/placeholder — как у вас принято).

---

## 3) GAMECONF (type=`gameconf`, version=`1.0.0`): управление окружением и feature level

GAMECONF решает задачу: “как правильно запустить мод”, включая:
- выбор “исполняемого режима/feature set” (`doom1.9`, `limitremoving`, `boom2.02`, `mbf21`, `mbf21ex`, `id24` и т.п.),
- выбор mode (`registered/retail/commercial`),
- список IWAD/PWAD, опции, и т.д.,
- порядок резолва: default IWAD → затем PWAD’ы по -file, каждый может иметь GAMECONF, причём значения чаще destructive, а массивы/опции могут быть additive. citeturn3search5

**Шаги:**
1) Реализовать парсер JSON-lump `GAMECONF`:
   - `type="gameconf"`, `version="1.0.0"`. citeturn3search5
2) Сделать структуру “активного gameconf состояния”, которая накапливается согласно правилам документа:
   - destructive overwrite,
   - additive arrays/options,
   - `executable` и `mode` — “max operation” (как описано).
3) Интегрировать это в фазу ранней инициализации **до** полного загрузочного цикла WAD’ов (спека прямо говорит, что GAMECONF из добавленных через GAMECONF IWAD/PWAD не должен парситься рекурсивно). citeturn3search5
4) Связать `executable=id24` с:
   - включением флага “ID24 features enabled”,
   - попыткой ранней загрузки `id24res.wad` (раздел 2),
   - включением всех обработчиков ID24 JSON-lumps,
   - включением ID24HACKED расширений.

---

## 4) DEMOLOOP (type=`demoloop`, version=`1.0.0`)

DEMOLOOP — JSON-lump `DEMOLOOP`, который определяет demo loop как массив entries:
- entry type 0 (art screen) с `primarylump`, optional `secondarylump` (музыка), `duration`,
- entry type 1 (demo) с `primarylump`, без `duration`,
- wipe type (immediate / melt). citeturn6search0

В EE уже есть реализация/заготовка: `source/id24_demoloop.cpp`, которая использует `id24_json` и парсит `entries`. citeturn2search2

**Шаги:**
1) Довести `id24_demoloop` до полной совместимости со спекой:
   - корректная обработка типов entry,
   - wipe поведение,
   - fallback на doom1.9 demo loop при отсутствии lump (как требует спека). citeturn6search0
2) Подключить это в “main menu / title loop” EE:
   - точка, где движок выбирает следующий элемент демо-цикла, должна читать “активную” demoloop-конфигурацию.
3) Тест: WAD из `wad_resources`, содержащий `DEMOLOOP` (или собрать минимальный тестовый PWAD) — проверить:
   - корректный порядок экранов/демо,
   - корректные тайминги,
   - корректный melt wipe.

---

## 5) Interlevel lumps (type=`interlevel`, version=`1.0.0`)

Это JSON-lump для анимированных (или статических) фонов intermission/victory screens.
Ключевые моменты:
- UMAPINFO additions: `exitanim` и `enteranim` authoritative (если задано и lump некорректен — **ошибка**, fallback на `exitpic/enterpic` запрещён). citeturn7search0  
- виртуальная среда 320x200 (как в оригинале),
- non-deterministic: RNG допускается (`M_Random`), длительности “примерно” (округление до tic ок),
- структура данных: root → layers → anims → frames + conditions, причём валидировать/считать нужно все поля даже если конкретная реализация не использует часть. citeturn7search0

**Шаги:**
1) Реализовать JSON-тип `interlevel`:
   - загрузка lump’а по имени, которое пришло из UMAPINFO (`exitanim/enteranim`).
2) Реализовать runtime-модель:
   - backgroundimage + music,
   - layers (условно включаемые),
   - anims (позиция + кадры + условия),
   - frames (duration/maxduration/infinite + ресурс patch).
3) Интегрировать в существующий intermission код EE:
   - “exit tally screen” и “entering screen” должны уметь рисовать этот фон под стандартными поверх него stats/UI.
4) Сделать строгую валидацию:
   - если `exitanim/enteranim` задан, а JSON invalid → сообщить ошибку/отключить показ (как требует спека), но **не** падать неконтролируемо.
5) Тесты:
   - прогон примеров из `wad_resources`.

---

## 6) Finale lumps (type=`finale`, version=`1.0.0`)

Позволяют определить “finale сценарий” (art screen / bunny scroller / cast roll call), плюс UMAPINFO extension `endfinale` (authoritative, игнорирует старые endpic/endbunny/endcast при наличии). citeturn3search9

**Шаги:**
1) Реализовать JSON-lump `finale`:
   - тип `type` (0/1/2),
   - `music`, `background`, `donextmap`,
   - `bunny` объект (для scroller),
   - `castrollcall` объект (для cast screen), включая:
     - castmembers, их frames, опциональные tranmap/translation, flip, sound. citeturn3search9
2) Интеграция в finale subsystem EE:
   - при обработке конца эпизода/карты и чтении UMAPINFO — поддержать `endfinale`,
   - переключить логику выбора finale: если задан `endfinale`, он первичен. citeturn3search9
3) Поддержать “продолжение на следующую карту” (donextmap) согласно вашей системе gameflow/mapinfo.
4) Тест:
   - минимальный WAD с кастомным cast roll call,
   - проверка соответствия таймингов, звуков, отрисовки.

---

## 7) SBARDEF (type=`statusbar`, version=`1.0.0`): кастомные статусбары

SBARDEF:
- единственный lump `SBARDEF` (последний по WAD resolution),
- должен работать минимум в limit-removing и обязателен в ID24,
- виртуальный рендер 320x200, fullscreen overlay/неfullscreen,
- логика обновления 35Hz,
- выбор “какой статусбар” через расширение screenblocks: `barindex = Max(screenblocks - 10, 0)`. citeturn3search4
- важное для ID24: `id24res.wad` добавляет оружейные слоты 8 и 9 (arms section). citeturn3search4

**Шаги:**
1) Реализовать парсер SBARDEF JSON (иерархия элементов Canvas/Graphic/Animation/Face/FaceBackground/Number/Percentage и т.д.). citeturn3search4
2) Реализовать layout engine:
   - transform tree + clipping,
   - alignments,
   - отрисовка патчей,
   - анимации (тиковые),
   - number fonts: правила monospace/proportional, glyph lookup. citeturn3search4
3) Интегрировать в HUD/statusbar модуль EE:
   - перехват существующего рендера,
   - если `SBARDEF` отсутствует → старый статусбар,
   - если присутствует → новый pipeline.
4) Реализовать выбор баров через screenblocks>10 и обеспечить UI/настройки (если EE ограничивает screenblocks).
5) Убедиться, что при ID24 и наличии `id24res.wad` новый SBARDEF автоматически становится доступным и показывает arms slots 8–9 (иначе игрок не увидит новые weapon slots). citeturn3search4

---

## 8) SKYDEFS (type=`skydefs`, version=`1.0.0`)

SKYDEFS:
- единственный lump `SKYDEFS`,
- определяет sky entries, включая:
  - Normal,
  - Fire (PSX/Doom64 style),
  - With Foreground (Hexen layered, индекс 0 в палитре — 100% прозрачность).
- `F_SKY1` зарезервирован под “default sky behavior”.
- есть строгие требования к параметрам: mid texel, scroll rates, scale и т.д. citeturn3search6

**Шаги:**
1) Реализовать парсер SKYDEFS JSON:
   - таблица sky definitions keyed по texture name,
   - применение параметров рендера.
2) Интегрировать в sky renderer EE:
   - сохранить поведение “default sky” для `F_SKY1`,
   - при наличии SKYDEFS — разрешить custom skies по texture name.
3) Реализовать Fire sky:
   - внутренний буфер текстуры, обновление по update time, priming 64 итерациями, палитра decay — согласно описанию. citeturn3search6
4) Реализовать With Foreground:
   - два слоя, transparency rule по palette index 0 для foreground.
5) Тест:
   - WAD с SKYDEFS: normal + fire + layered.

---

## 9) Translations (type=`translation`, version=`1.0.0`)

Translations задают palette lookup table 256 элементов, плюс ресурсы под interlevel/statusbar backgrounds.
Ключевые правила:
- table должна быть ровно 256,
- есть built-in translations (T_GREEN, T_INDIGO, T_BROWN, T_RED) + 4 дополнительных, которые можно override lumps,
- порядок инициализации:
  1) translations, определённые в thing’s DeHackEd entry,
  2) translations в `playertranslations` из GAMECONF,
  3) built-ins, которых ещё нет как lumps. citeturn3search8
- MF_TRANSLATION: если у thing стоит флаг и Translation field не задан, берём из playertranslations. citeturn3search8

**Шаги:**
1) Реализовать загрузку translation lumps:
   - type/version как выше,
   - строгая проверка длины table=256. citeturn3search8
2) Создать центральный registry переводов:
   - resolution по `name` (mnemonic),
   - хранение таблицы, плюс sbarback/interback и флаги “translate background”.
3) Интеграция:
   - в renderer init фазе (как требует спека),
   - связать с GAMECONF (`playertranslations`) и с DeHackEd thing entries.
4) Тест:
   - простой translation lump + проверка отрисовки player sprites и задников.

---

## 10) Mapping Additions

Документ “Mapping additions” добавляет:
- зарезервированные диапазоны line specials (в т.ч. 2048–4095),
- UMAPINFO additions: `bossaction` теперь может принимать **thing number** (DeHackEd number) вместо mnemonic, плюс `bossactionednum` (по editor number). citeturn3search7
- “Any texture/flat can be used on any surface” (и важное: прозрачность по palette index 0),
- новые specials: floor/ceiling texture offset/rotation, music changing lines, reset exits, sector tinting, scrolling specials и т.д. citeturn3search7

**Шаги:**
1) Обновить обработчик line specials:
   - добавить новые индексы/поведение по таблице в документе,
   - обеспечить, что “reserved for id” диапазон принимается и корректно обрабатывается.
2) Доработать UMAPINFO обработку:
   - `bossaction`: разрешить integer thing number,
   - `bossactionednum`: резолв editor number → thing type (через таблицы thinginfo).
3) Рендер-часть “any texture/flat on any surface”:
   - разрешить использовать TEXTURE1/2 texture на пол/потолок с 1:1 scaling и без power-of-2 clamp, а также flats на стены,
   - обеспечить прозрачность texel=palette index 0 на “solid surface”. citeturn3search7
4) Тест:
   - карты с новыми line specials,
   - проверка bossaction* и новых рендер-правил.

---

## 11) ID24HACKED: расширения DeHackEd + новый порядок загрузки + расширенные диапазоны индексов

ID24HACKED вводит:
- способ идентификации: Doom version = 2024, либо встреча новых полей/поведения, не входящего в baseline,  
- новый порядок инициализации патчей: DEHACKED lumps из WAD’ов по порядку загрузки (IWAD→PWADs), затем cmdline deh (если поддерживается),  
- снимает лимиты индексов (поддержка огромных диапазонов, включая отрицательные),
- делит диапазоны на “implementation defined”, “id reserved”, “community reserved” и т.д.,
- вводит string mnemonics `USER_...` и добавляет новые ID24_ строки. citeturn3search2

В EE уже есть развитая DeHackEd/BEX инфраструктура (таблица блоков, string table hashing, codeptrs и т.д.). citeturn2search4

**Шаги:**
1) Реализовать feature-detection “ID24HACKED mode”:
   - Doom version 2024,
   - либо встреча ID24HACKED-специфичных конструкций. citeturn3search2
2) Пересобрать “порядок загрузки” DEHACKED в EE согласно ID24HACKED:
   - тут важно отличить “все DEHACKED lumps” vs “last found” правила для hashing (см. следующий раздел).
3) Поддержать расширенные диапазоны индексов:
   - привести внутренние типы/парсинг к 32-bit signed/unsigned там, где это требуется,
   - чётко определить, что из этого EE разрешает “создавать” и что “только модифицировать”, следуя таблицам ranges.
4) Поддержать `USER_` string mnemonics:
   - расширить таблицу резолва мнемоник в строках (`DEH_String` / lookup), чтобы `USER_*` мог создаваться/замещаться. citeturn2search4turn3search2
5) Добавить новые “ID24_*” строки в таблицу строк (если EE ведёт фиксированный список) или обеспечить динамическое добавление.

---

## 12) ID24HACKED hashing: устранение undefined behavior при стэкинге патчей

Hashing спецификация:
- вводит FNV-1a 64-bit, little-endian integer bytes,
- вводит корневое поле DeHackEd: `Valid hashes` (список допустимых hash’ей состояния, поверх которых патч может грузиться),
- новый порядок парсинга для hashing:
  - IWAD DEHACKED entry (last found),
  - затем для каждого PWAD (last found),
  - затем каждый DEH файл. citeturn3search3
- хэшируются misc + Thing/Frame/Sprite/Sound/Weapon/Ammo (но не strings/cheats/par times),
- codepointers хэшируются по mnemonic или “null”. citeturn3search3

**Шаги:**
1) Добавить вычисление “текущего hash состояния DeHackEd”:
   - после применения одного патча получить hash,
   - перед применением следующего — проверить `Valid hashes` (если уже есть загруженный патч).
2) Реализовать FNV-1a 64-bit как отдельный модуль/утилиту:
   - аккуратно соблюдать little-endian байты для int’ов,
   - строки: null/empty → “null” literal. citeturn3search3
3) Реализовать точный порядок обхода таблиц:
   - “in-order tables” и “allocation order matches first reference”, что может потребовать изменения стратегии выделения/создания объектов при парсинге ID24HACKED. citeturn3search3
4) Интеграция в существующий DeHackEd пайплайн EE:
   - до применения патча: если уже применяли патчи и `Valid hashes` не содержит текущий hash → отказать в применении (и чётко сообщить пользователю почему).
5) Тест:
   - два последовательных DEHACKED: первый без valid hashes, второй с правильным/неправильным → убедиться, что поведение соответствует спецификации.

---

## 13) Оставшиеся документы из списка (что включить в план, даже если мы ещё не раскрывали содержимое)

У тебя в списке ещё есть:
- `Finale_lumps.md` — учтено,
- `Interlevel_lumps.md` — учтено,
- `DEMOLOOP.md` — учтено,
- `GAMECONF.md` — учтено,
- `Mapping_Additions.md` — учтено,
- `SBARDEF.md` — учтено,
- `SKYDEFS.md` — учтено,
- `Translations.md` — учтено,
- `ID24HACKED.md` + `ID24HACKED_hashing.md` — учтено,
- **но ещё не разобраны в этом ответе**: `JSON_Lump.md` (как база — учтён), и файлы из списка:
  - `Finale_lumps.md` уже да,
  - **`ID24_formal_specification_0.99.2.md`** уже да (как верхнеуровневый “контракт”). citeturn3search0
  - остаются: `SBARDEF.md` уже да,
  - и ещё два имени, которые ты давал в списке: `Finale_lumps.md`/`Interlevel_lumps.md` уже да,
  - **но** в предоставленных извлечениях я не использовал содержимое `SKYDEFS.md` дальше середины, и не раскрывал “полную таблицу line specials” (она обрезана в превью) — это нормально для плана, но при реализации нужно будет строго идти по таблицам документа.

(Иными словами: архитектурно план покрывает всё, но “табличные перечисления” — берём прямо из md при кодинге, чтобы не ошибиться индексами/полями.)

---

## 14) Минимальный порядок внедрения (рекомендуемая дорожная карта)
Чтобы не утонуть, лучше идти слоями:

1) **База**: feature set переключение (через GAMECONF) + гарантированный `id24res.wad` loading.
2) **JSON платформа**: единый механизм загрузки/валидации JSON-lumps и централизованные warnings.
3) **UI/визуал**: SBARDEF → SKYDEFS → Translations (они сразу дадут видимый результат).
4) **Экранные сценарии**: DEMOLOOP → Interlevel → Finale.
5) **Геймплей/карты**: Mapping additions.
6) **DeHackEd расширения**: ID24HACKED + затем hashing (самое “строгое” и ломкое по совместимости).

---

## 15) Ключевые места в коде EE, на которые опирается план (по уже найденному)
- JSON infrastructure: `source/id24_json.h`, `source/id24_json.cpp`. citeturn2search0turn2search2  
- Пример готового JSON-lump модуля: `source/id24_demoloop.cpp`. citeturn2search2  
- Поиск/конфиг пути `id24res.wad`: `source/d_findiwads.cpp`, `source/g_cmd.cpp`, `source/m_syscfg.cpp`, меню в `source/mn_menus.cpp`. citeturn10search0  
- DeHackEd/BEX база: `source/d_deh.cpp`, `source/d_dehtbl.h/.cpp`. citeturn2search4  

---

Если хочешь, можешь уточнить один момент (это повлияет на детали реализации, но не меняет общий план):
- ты хочешь, чтобы **ID24 features включались только через GAMECONF executable=id24**, или ещё и через “авто-детект” (нашли ID24 JSON lump / Doom version=2024 в DEHACKED → включили)?

## @DRON12261

Вопрос интересный, пока не уверен, но полагаю лучше всего будет через авто-детект.

## Copilot

Авто-детект — хороший выбор, но чтобы он был **предсказуемым** и не ломал совместимость, его лучше сделать **слоистым** (с явными правилами приоритета) и с “точкой фиксации”, после которой режим не меняется.

Ниже — рекомендованный дизайн авто-детекта для EE + как это встроить в существующую систему (GAMECONF остаётся, но становится “самым сильным сигналом”).

---

## 1) Общая идея: 3 уровня сигналов (strong / medium / weak)

### 1.1. Strong signals (включают ID24 без вопросов)
1) **GAMECONF executable = `id24`** (если есть корректный GAMECONF)  
   Это явное требование WAD’а, его нельзя “переспорить”.
2) **DEHACKED Doom version = 2024** (ID24HACKED идентификация)  
   Спека ID24HACKED прямо использует Doom version 2024 как маркер. citeturn3search2
3) **Наличие валидного JSON-lump с ID24-типом**, который *в спецификации обозначен как “requirement of ID24 featureset”* и который реально удалось распарсить (например, `SBARDEF`, `SKYDEFS`, `DEMOLOOP`, `finale`, `interlevel`, `translation`, `gameconf`).  
   Важно: включать по факту “мы нашли lump и он валиден”, а не только по имени.

### 1.2. Medium signals (включают ID24, но с защитой от ложных срабатываний)
1) Встречены ID24HACKED-специфичные поля/секции/мнемоники, которые невозможно корректно интерпретировать в старом режиме (например, `Valid hashes`, новые диапазоны/поля ID24HACKED, `USER_` string mnemonics и т.д.). citeturn3search2turn3search3  
2) UMAPINFO использует поля-расширения, которые относятся к ID24-докам (например, `endfinale`, `enteranim/exitanim`, `bossaction` в форме “thing number”, `bossactionednum`). citeturn3search9turn7search0turn3search7  

### 1.3. Weak signals (не включают ID24 автоматически, только логируют подсказку)
1) Найден файл `id24res.wad` рядом с IWAD’ом.
2) Найдены lumps с “похожими именами”, но они невалидны как JSON-lumps (по JSON-lump спека — это просто не JSON-lumps). citeturn9search0  

---

## 2) Приоритеты и “точка фиксации” (чтобы режим не прыгал)

### 2.1. Приоритет источников
Рекомендуемый приоритет (сверху сильнее):
1) **GAMECONF** (если валиден) → определяет feature level “официально”.
2) **ID24HACKED/DEHACKED** (Doom version 2024 или ID24HACKED-конструкции).
3) **JSON-lumps ID24** (валидные и распарсенные).
4) **UMAPINFO additions** (если эти поля требуют ID24-поведения).

### 2.2. Фиксация режима
Сделай правило:

- До окончания фазы “первичного сканирования WAD dictionary” режим **может повышаться** (например, с Boom → MBF21 → ID24), но **никогда не понижается**.
- После “фиксации” (например, перед инициализацией рендера/игровых таблиц/DeHackEd применения) режим менять нельзя.

Практически: заводишь `pending_feature_level` и `final_feature_level`.  
`pending` обновляется по мере загрузки/сканирования lumps, а `final` фиксируется один раз.

---

## 3) Конкретная реализация авто-детекта в EE (пошагово)

### Шаг 1 — Ввести единый “FeatureLevelResolver”
Создай модуль/класс (условно `id24_features.cpp/.h`) с API:
- `ObserveWadLump(name, lumpindex)` (или пачкой)
- `ObserveGameconf(parsedGameconf)`
- `ObserveDehackedMarker(...)`
- `Finalize()` → возвращает `FeatureLevel` и флаги (need_id24res, need_json_handlers, need_hashing и т.д.)

### Шаг 2 — Сканирование по имени lumps (cheap pass)
На ранней стадии (когда WAD уже прочитал directory, но ещё не начал “полный runtime init”) пробегись по наличию:
- `GAMECONF`
- `DEMOLOOP`
- `SBARDEF`
- `SKYDEFS`
- `TRANSLATION` (или как точно называется lump в спеках переводов — обычно это “translation”-type, но имя lumps может быть разное; тут опирайся на doc/примеры из `wad_resources`)
- любые finale/interlevel lumps, на которые указывает UMAPINFO (`endfinale`, `enteranim`, `exitanim`)

Это не включает ID24 само по себе, но ставит “кандидатов на парсинг”.

### Шаг 3 — Валидация JSON-lumps (strong signal только если валидно)
Для каждого кандидата:
1) попытаться парсить как JSON-lump (строго по JSON_Lump.md),
2) если валиден и `type/version` совпадают ожидаемым — поднимаем `pending_feature_level` минимум до “Limit Removing (with UMAPINFO)” или прямо до `ID24` (в зависимости от того, какой lump), и отмечаем, что соответствующая подсистема должна быть активна. citeturn9search0turn3search0

Важно: если JSON невалиден — **не включать ID24**, просто warning.

### Шаг 4 — Парсинг GAMECONF (самый сильный)
Если есть валидный `GAMECONF`:
- применяем его правила,
- если `executable == id24` → принудительно ставим `pending = ID24` (и блокируем дальнейшие “понижения”). citeturn3search5

### Шаг 5 — ID24HACKED маркеры и hashing
Если в DEHACKED:
- Doom version == 2024 → `pending = ID24` (strong). citeturn3search2  
- Если встретили `Valid hashes` → включаем “hashing режим” (и поднимаем минимум до DSDHACKED/ID24 — как у вас в EE принято по complevel’ам). citeturn3search3  

### Шаг 6 — Решение про `id24res.wad`
Если `pending_feature_level == ID24` (по любому strong/medium сигналу):
- если `gi_path_id24res` задан или найден — добавляем его в список загружаемых WAD’ов *до* IWAD (или максимально рано в вашей архитектуре). citeturn10search0turn3search0
- если не найден — логируем “ID24 requested but id24res.wad not found”.

---

## 4) UX: чтобы пользователю было понятно “почему включился ID24”
Добавь лог/консольный вывод вида:

- `ID24 mode enabled: reason=GAMECONF executable=id24`
- `ID24 mode enabled: reason=DEHACKED DoomVersion=2024`
- `ID24 mode enabled: reason=valid SKYDEFS JSON lump found`
- `ID24 mode enabled: reason=UMAPINFO enteranim/exitanim present` (если вы так решите)

Это критично, потому что авто-детект иначе воспринимается как “движок сам что-то решил”.

---

## 5) Как это сочетается с ручным управлением (на будущее)
Даже при авто-детекте полезно иметь override:
- `-id24` (force on)
- `-noid24` (force off, но тогда при встрече ID24-only фич выдавать явные ошибки)

Это сильно помогает при отладке и регрессиях.

---

## 6) Ответ на твой вопрос про “как дать тебе полный доступ/убрать лимит 10 совпадений”
Ты спрашивал раньше: ограничение “10 совпадений” в code-search ты убрать не сможешь как настройкой чата (оно обычно на стороне инструмента). Но ты можешь помочь так:

1) **Дать ссылку на конкретные файлы/каталоги EE**, которые важны (HUD/statusbar, sky renderer, DeHackEd loader, UMAPINFO parser, WAD loader). Тогда я буду искать точечно, а не широкими запросами.  
2) Если репозиторий приватный — нужно, чтобы он был доступен GitHub-аккаунту Copilot/инструменту (обычно это делается правами доступа в GitHub).  
3) Для “полного анализа” лучше не полагаться на поиск, а идти от **entry points**: ты можешь назвать, где в EE:
   - грузятся lumps и строится WAD directory,
   - парсится UMAPINFO,
   - выбирается complevel/compat mode,
   - применяется DeHackEd.

---

Если ты согласен с этим дизайном: скажи, пожалуйста, как в EE сейчас называется/представлен “уровень совместимости” (complevel/compat flags/feature set). Тогда я предложу **конкретные точки в исходниках eternity-fork**, куда лучше всего встроить `FeatureLevelResolver` и “фиксацию режима”, чтобы это не конфликтовало с существующей логикой.

## @DRON12261

По идее это должен быть complevel

## Copilot

Да — тогда правильнее всего “приземлять” авто-детект ID24 **в систему complevel**, но сделать это так, чтобы:

1) **complevel мог автоматически повышаться** (до ID24),  
2) при этом **ручной выбор complevel пользователем** оставался главным (override),  
3) и чтобы **все подсистемы** (DeHackEd/UMAPINFO/рендер/physics) получали *один и тот же* итоговый complevel, зафиксированный до инициализации.

Ниже — конкретный пошаговый план, как это встроить в EE.

---

## A) Какой “complevel” нужен для ID24 (цель)
В ID24 формальной спецификации перечислена шкала “feature level” вплоть до `ID24`, и там прямо указано, что ID24 — надстройка над Boom/MUSINFO/MBF/MBF21/DSDHACKED/UMAPINFO и т.д. citeturn3search0

В EE уже есть DeHackEd/DSDHACKED слой и UMAPINFO (судя по тому, что у вас есть ID24-модули и отдельные json парсеры), значит цель такая:

- Ввести/использовать `complevel_id24` (если его ещё нет) как “самый высокий” режим.
- Всё, что требует ID24, должно проверять `if (complevel >= complevel_id24)` (или аналогичный предикат), а не отдельные разрозненные флаги.

---

## B) Авто-детект как “рекомендованный complevel”, а не как “мгновенная смена”
### B.1. Новый внутренний результат: `detected_complevel`
Сделай переменную (условно):
- `int detected_complevel = -1;` (или enum)
- `int effective_complevel;` (то, что реально используется)

И правило:

- если пользователь **явно** задал complevel (через меню/консоль/ключи запуска) → `effective = user_complevel`.
- иначе → `effective = detected_complevel` (если распознали) или дефолтный.

### B.2. Повышение возможно, понижение — нет
При сканировании WAD’ов:
- `detected_complevel = max(detected_complevel, candidate)`

---

## C) Где в пайплайне это делать (критично)
Авто-детект должен происходить **после того, как WAD directory построен**, но **до**:
- применения DeHackEd,
- инициализации статусбара/неба/финальных экранов,
- старта уровня.

То есть: “мы уже можем искать lumps по имени”, но ещё не “запустили мир”.

---

## D) Какие сигналы переводят complevel в ID24 (на практике для EE)

### D.1. Strong → сразу `complevel_id24`
1) Валидный `GAMECONF` с `executable=id24` (когда реализуете GAMECONF). citeturn3search5  
2) DeHackEd Doom version = 2024 (ID24HACKED). citeturn3search2  
3) Валидный JSON-lump (по JSON_Lump.md) с типом из ID24-списка (например `statusbar`/`skydefs`/`demoloop`/`finale`/`interlevel`/`translation`). citeturn9search0turn3search0  

### D.2. Medium → `complevel_id24` или минимум “DSDHACKED+”
- Встречен `Valid hashes` (hashing ID24HACKED) — практически всегда означает, что автор ожидает “новый режим” DeHackEd. citeturn3search3  
- UMAPINFO additions (`endfinale`, `enteranim/exitanim`, `bossaction` как thing number, `bossactionednum`). citeturn3search9turn7search0turn3search7  

Я бы для простоты и предсказуемости тоже поднимал до `complevel_id24`.

---

## E) Связка с `id24res.wad` (важно именно в complevel)
Раз у вас уже есть инфраструктура поиска пути `id24res.wad` (`gi_path_id24res`, `pwad_id24res`, автоскан каталога) citeturn10search0 — правило должно быть таким:

- если `effective_complevel == complevel_id24`:
  - попытаться загрузить `id24res.wad` как “ранний PWAD” (до IWAD либо самым первым PWAD, в зависимости от вашей архитектуры),
  - если не найден — warning.

Это соответствует рекомендациям ID24 “load id24res.wad before main IWAD for overrides”. citeturn3search0

---

## F) Что мне нужно от тебя, чтобы сделать план *совсем привязанным к конкретным файлам EE*
Скажи (можно просто именами файлов/символов), где в Eternity Engine сейчас:
1) хранится и применяется `complevel` (переменная/enum),  
2) где выбирается `complevel` при старте (CLI/config/menu),  
3) где происходит “построение WAD directory” и “ранний этап после загрузки WAD’ов” (идеальная точка для авто-детекта).

Если не помнишь — я могу найти сам поиском, но ты сильно ускоришь процесс, если укажешь 1–2 файла/символа (например: `g_game.cpp` / `m_misc.cpp` / `d_main.cpp` / `w_wad.cpp` и т.п. — как у вас принято).

После этого я:
- предложу **точную схему внедрения** авто-детекта в существующий код EE (какие функции расширять, какие новые структуры добавить),
- и составлю “боевую” дорожную карту по коммитам/подзадачам (в каком порядке реализовывать документы ID24).

## @DRON12261

К сожалению не помню

## Copilot

Да, в EE это действительно “complevel”, но он у вас выражен **не отдельной enum-переменной**, а через связку:

- `demo_version` (и местами `demo_compatibility` / `compatibility`)  
- вектор `comp[]` (compatibility flags) и `getComp()`  
- и логика `G_SetCompatibility()` (матрица `complevels[]`, завязанная на `demo_version`)  

Это видно прямо в `g_game.cpp` и `doomstat.cpp`: `demo_version` глобальная, а `G_SetCompatibility()` выставляет `comp[]` в зависимости от неё. citeturn15search0turn15search5

Также важная точка пайплайна загрузки WAD’ов — `D_Main`:
- после `M_LoadDefaults()` и autoload’ов вызывается `wGlobalDir.initMultipleFiles(wadfiles);`
- затем `D_InitGMIPostWads();`
- затем идёт обработка DeHackEd “в WAD’ах” (коммент “use D_ProcessDehInWads again” прямо после). citeturn15search4

Ниже — как сделать авто-детект ID24 **правильно именно в этой архитектуре**, без необходимости “придумывать новый complevel enum”.

---

## 1) Важное ограничение: авто-детект ID24 ≠ смена `comp[]` на лету
У вас компат завязан на `demo_version`. Значит правильная стратегия:

1) **определяем ID24-режим/уровень** сразу после `W_InitMultipleFiles` (`wGlobalDir.initMultipleFiles`),  
2) **фиксируем** итоговый “compat mode” (в терминах EE: `demo_version` + `compatibility`/`demo_compatibility`),  
3) **только после этого** применяем DeHackEd, JSON-lumps, UMAPINFO extensions, и т.д.

Иначе будет “скачок” логики: часть таблиц/парсеров/рендера успеет инициализироваться под одним режимом, часть — под другим.

---

## 2) Где именно вставлять авто-детект (идеальная точка)
В `source/d_main.cpp` сразу после:

```cpp
wGlobalDir.initMultipleFiles(wadfiles);
...
D_InitGMIPostWads();
```

и **до** вызова обработки DeHackEd lumps (он как раз после этого блока).

Это даст:
- WAD directory уже собран → можно искать lumps по именам и читать данные,
- ещё не применяли DeHackEd → можно корректно решить “ID24HACKED/hash режим”,
- можно решить вопрос `id24res.wad` (но есть нюанс ниже).

citeturn15search4

---

## 3) Нюанс с `id24res.wad`: его надо подключать *до* `W_InitMultipleFiles`
Спека ID24 рекомендует грузить `id24res.wad` **до IWAD**, чтобы overrides работали. citeturn3search0  
Но авто-детект (по WAD lumps) возможен только *после* `W_InitMultipleFiles`.

Чтобы совместить оба требования, делай **двухфазную стратегию**:

### Фаза A (ранняя, до `W_InitMultipleFiles`): “оппортунистически” добавляем id24res.wad
Если файл `id24res.wad` найден (у вас уже есть `gi_path_id24res` из сканера/конфига) — добавляем его в список `wadfiles` **всегда**, но с флагом “активировать только если потом подтвердится ID24”.

Реализации 2 варианта:

**Вариант 1 (лучше):** добавить `id24res.wad` в `wadfiles` как “обычный PWAD”, но при старте игры, если ID24 не подтвердился, просто не пользоваться его lumps (это сложно, потому что WAD resolution уже влияет).  

**Вариант 2 (проще и практичнее):** добавлять `id24res.wad` в `wadfiles` только если:
- пользователь явно включил ID24 (будущий флаг), **или**
- на командной строке есть `-id24` (force), **или**
- в конфиге стоит “auto load id24res if present” (новая опция по умолчанию ON).

А если позже авто-детект сработал (нашли SBARDEF/SKYDEFS/ID24HACKED), то:
- либо перезапускать WAD систему (нежелательно),
- либо принять компромисс: id24res.wad будет подгружен “как обычный PWAD после IWAD” (и часть override’ов может отличаться от идеала).

На практике почти все порты идут на этот компромисс, потому что “подгрузить до IWAD” без предварительного знания сложно.

**Что рекомендую для EE:** начать с “компромисса + лог”:
- при детекте ID24 после `W_InitMultipleFiles`: если `id24res.wad` ещё не загружен, подгрузить его в конец и вывести предупреждение, что overrides могли быть не приоритетными.  
Потом можно улучшить, когда появится GAMECONF (он может задавать ID24 до W_Init, если вы решите парсить GAMECONF из `-file` заранее, но это отдельная история).

(Путь `gi_path_id24res` и настройка `pwad_id24res` у вас уже есть. citeturn10search0)

---

## 4) Что считать “complevel” в вашем EE и как его поднять до ID24
У вас “complevel” фактически = диапазон `demo_version` (Boom/MBF/EE demos и т.д.), который затем управляет `comp[]`. Это видно по проверкам вида `if (demo_version >= 401)` и т.п. citeturn15search7turn15search9

Значит авто-детект ID24 нужно реализовать так:

1) Ввести новое значение `demo_version` для ID24 (например условно 5xx/6xx/1000 — как у вас принято), либо ввести отдельный флаг `id24_mode`, который **не зависит** от `demo_version`, но тогда надо аккуратно: часть поведения у вас уже завязана на `demo_version` (пример: выбор weapon slot логики при `demo_version >= 401`). citeturn15search7

2) Сделать функцию:
- `G_SetDetectedCompatibility()` или `D_DetectAndSetCompatLevel()`:
  - анализирует WAD directory,
  - решает “ID24 требуется?”,
  - выставляет:
    - `demo_version` (или флаг),
    - `compatibility`/`demo_compatibility` если надо,
    - вызывает `G_SetCompatibility()` чтобы обновить `comp[]`. citeturn15search0turn15search5

---

## 5) Какие конкретно сигналы ID24 реально можно найти в EE прямо сейчас
Поиск по репозиторию в одном запросе даёт максимум 10 результатов, поэтому список может быть неполным; для ручного просмотра большего количества совпадений лучше открыть поиск в GitHub UI по запросу `complevel id24` или `ID24` в репозитории.  
Ссылка для просмотра в GitHub UI (пример запроса):  
`https://github.com/dron12261games/eternity-fork/search?q=complevel&type=code`

Тем не менее, уже сейчас можно авто-детектить по таким “жёстким” признакам:

### 5.1. Валидные JSON-lumps ID24
У вас есть `id24_json`, значит можно:
- искать lumps `SBARDEF`, `SKYDEFS`, `DEMOLOOP` и т.д.
- пытаться парсить их как JSON-lump (строго по JSON_Lump.md): если валидно и `type/version` совпадают → ID24. citeturn9search0

### 5.2. ID24HACKED маркеры в DEHACKED
После `W_InitMultipleFiles` вы можете проверить наличие `DEHACKED` lumps и быстро пробежать заголовок:
- Doom version 2024 → ID24. citeturn3search2

---

## 6) Практический мини-план внедрения авто-детекта “через complevel” в EE (конкретно по файлам)

### Шаг 1: Добавить модуль детекта
Новые файлы (примерно):
- `source/id24_detect.h`
- `source/id24_detect.cpp`

Функции:
- `bool ID24_DetectRequested();`  (скан directory и “лёгкие” проверки)
- `void ID24_ApplyDetectedMode();` (выставить demo_version/флаги и вызвать `G_SetCompatibility()`)

### Шаг 2: Вставить вызов в `D_Main` (d_main.cpp)
После `wGlobalDir.initMultipleFiles(wadfiles);` и `D_InitGMIPostWads();` — вызвать:
- `ID24_ApplyDetectedMode();`

citeturn15search4

### Шаг 3: Встроить `id24res.wad` загрузку
- Если ID24 detected и `id24res.wad` не был загружен в `wadfiles`, подгрузить его “дополнительно” (как умеет EE) и вывести предупреждение про приоритет.

### Шаг 4: Сделать override пользователем
Добавить консольный/конфиг параметр:
- `id24_autodetect` (bool, default on)
- `-id24` / `-noid24` (CLI)

### Шаг 5: Лог причины
Печатать:
- `ID24 autodetect: enabled because found valid SKYDEFS JSON lump`
- `ID24 autodetect: enabled because DEHACKED Doom version 2024`
и т.п.

---

## 7) Что я предлагаю сделать дальше (чтобы план стал “совсем точным”)
Мне нужно ещё найти в коде EE:
1) где обрабатывается `-complevel` (если есть) или как пользователь задаёт демо-совместимость,
2) где именно вызывается `D_ProcessDehInWads()` и как он устроен (чтобы ID24HACKED/hashing правильно встроить),
3) есть ли уже где-то `id24`-флаг или зарезервированное значение `demo_version` под новые режимы.

Хочешь — я продолжу поиском по репозиторию `dron12261games/eternity-fork` (например: `-complevel`, `demo_version =`, `D_ProcessDehInWads`, `DEHACKED` loader) и затем дам конкретные предложения: какие значения/enum добавить и куда именно встроить переключение, без риска сломать демосинк.

## @DRON12261

Чтож, предлагаю остановиться на этом.

Учитывая весь проведенный тобой анализ, давай снова составим полный подробный поэтапный план реализации id24 для Eternity Engine по пунктам, по которым можно было шаг за шагом идти во время разработки.

## Copilot

Ниже — **полный подробный поэтапный план** внедрения **полной поддержки ID24 (spec 0.99.2)** в твоём **Eternity Engine fork** (`dron12261games/eternity-fork`, ветка `master`) так, чтобы по нему можно было идти **шаг за шагом** в разработке. План построен с учётом того, что:

- в EE уже есть база для JSON-lumps: `source/id24_json.*` (парсер/валидация)  
- уже есть `id24_demoloop.cpp` как пример готового “ID24-фичемодуля”  
- уже есть инфраструктура поиска/настройки пути `id24res.wad` (`gi_path_id24res`, `pwad_id24res`, автоскан)  
- “complevel/compat” в EE завязан в основном на `demo_version` + `comp[]`/`getComp()` и задаётся после загрузки WAD’ов (`G_SetCompatibility`)  
- точка, где WAD directory уже построен: сразу после `wGlobalDir.initMultipleFiles(wadfiles)` в `d_main.cpp`.

План не требует, чтобы всё было сделано строго одним PR — его удобно дробить на “вехи” и проверять на примерах из `wad_resources`.

---

## Веха 0 — Подготовка и инфраструктура разработки (1–2 дня)

### 0.1. Зафиксировать цель: “полный ID24”
ID24 0.99.2 = superset: Vanilla Doom, Boom, MUSINFO, MBF, DEHEXTRA, MBF21, DSDHACKED, UMAPINFO + новые major-features (DEMOLOOP, GAMECONF, Finale lumps, Interlevel lumps, SBARDEF, SKYDEFS, Translations, Mapping additions, ID24HACKED+hashing). citeturn3search0

**Результат:** документ/README в репозитории разработки (внутренний), где перечислены “Done / TODO”.

### 0.2. Подготовить тестовый набор WAD’ов
- Взять примеры из `doom-cross-port-collab/id24/wad_resources` (как эталон поведения).
- Опционально: добавить “минимальные” тестовые PWAD’ы (малые JSON-lumps по одному), чтобы ловить регрессии.

**Результат:** папка `tests/id24_wads/` (или внешний набор), сценарии запуска.

### 0.3. Определить лог-формат и уровень строгой валидации
ID24-спеки часто требуют “ошибка при неверном формате/версии/полях”, особенно для JSON-lumps. citeturn9search0  
Нужны единые правила:
- где warning, где error,
- что делаем при error: отключаем конкретную фичу / останавливаем загрузку / fallback.

**Результат:** “policy” (короткая таблица в `docs/id24-implementation.md`).

---

## Веха 1 — Авто-детект ID24 и связывание с “complevel/compat” (фундамент)

Цель: движок должен **сам** понять “нужен ID24?” и включить режим, не требуя от пользователя ручных флагов, но при этом не ломая старые WAD’ы.

### 1.1. Ввести централизованный “ID24 runtime mode” (1 точка истины)
Добавить в код:
- `bool id24_enabled` (эффективный режим)
- `bool id24_detected` (результат авто-детекта)
- `bool id24_forced_on/off` (если позже добавите CLI/config override)

**Важно:** включать режим **один раз** (“точка фиксации”) после построения WAD directory.

### 1.2. Реализовать детектор ID24 (после `W_InitMultipleFiles`)
Реализовать `ID24_DetectRequested()`:
- Strong-сигналы:
  - найден валидный JSON-lump ID24-типа (через `id24_json` — валидность по JSON_Lump.md) citeturn9search0
  - найден DEHACKED Doom version=2024 (ID24HACKED маркер) citeturn3search2
  - (когда будет GAMECONF) executable=id24 citeturn3search5
- Medium-сигналы:
  - `Valid hashes` в DEHACKED (ID24HACKED hashing) citeturn3search3
  - UMAPINFO additions `endfinale`, `enteranim/exitanim`, `bossactionednum` и т.д. citeturn3search9turn7search0turn3search7

### 1.3. Связать `id24_enabled` с вашим “complevel”
У вас совместимость завязана на `demo_version`/`comp[]` (`G_SetCompatibility`). citeturn15search0turn15search5

Сделать так:
- при `id24_enabled=true` выставить “верхний” режим совместимости:
  - либо новый `demo_version` (если вы используете demo_version как “complevel”),
  - либо отдельный флаг, но тогда критичные места, завязанные на `demo_version>=401`, нужно аудировать (оружейные слоты/демосинк и т.п.). citeturn15search7
- вызвать `G_SetCompatibility()` после установки режима.

### 1.4. Лог причин
Печатать: почему включился ID24 (какой сигнал).

**Критерий готовности Вехи 1:**
- EE запускается как раньше на старых WAD’ах,
- на ID24-примерах режим включается автоматически,
- есть воспроизводимый лог “ID24 enabled because …”.

---

## Веха 2 — JSON-lump платформа: строгая валидация и единая обвязка

Цель: все ID24 JSON-фичи подключаются единообразно и безопасно.

### 2.1. Привести `id24_json` к роли “центрального валидатора”
JSON_Lump.md требует: строгий root (`type/version/metadata/data` и “только они”), строгая версия (выше max — ошибка), реальный JSON parse без эвристик. citeturn9search0

Проверить/доработать:
- сообщения об ошибках (какой lump, какое поле),
- поведение при лишних полях (должна быть ошибка),
- проверка `metadata` обязательна по структуре (на runtime игнорируем содержимое, но наличие проверяем). citeturn9search0

### 2.2. Ввести общий “loader pattern”
Для каждого JSON-lump модуля:
- `FindLump("NAME")` (или имя по спекам),
- `ParseJSONLump(raw, size, expectedType, maxVersion, parseFunc)`
- единый объект результата: `ok/warnings/errors`.

**Критерий готовности Вехи 2:**
- можно легко добавить новый JSON-lump модуль по шаблону,
- ошибки читаемые и одинаковые.

---

## Веха 3 — `id24res.wad`: загрузка supporting data и приоритет ресурсов

ID24 рекомендует грузить `id24res.wad` перед IWAD для overrides. citeturn3search0  
В EE уже есть поиск и конфиг пути `id24res.wad`. citeturn10search0

### 3.1. Политика загрузки
Сделать правило:
- если `id24_enabled`:
  - пытаемся подключить `id24res.wad` (если найден).
- если не найден:
  - warning (не ошибка), но дальше стандартное поведение “missing resource”.

### 3.2. Порядок загрузки (реалистичный)
Так как авто-детект происходит после построения WAD directory, идеальный порядок “до IWAD” труднее. На первом этапе:
- подгружать `id24res.wad` сразу после детекта и логировать, если это “не идеально по приоритету”.
Позже (когда будет GAMECONF/явные флаги) можно улучшить.

**Критерий готовности Вехи 3:**
- при ID24 включении ресурсы из id24res доступны (SBARDEF оружейные слоты 8–9 и т.п.). citeturn3search4

---

## Веха 4 — Translations (type=`translation`, version=`1.0.0`)
Это базовая визуальная и системная фича: нужна SBARDEF/Interlevel и palette swaps.

Спека: таблица 256, built-ins, порядок инициализации, влияние MF_TRANSLATION, поля sbarback/interback. citeturn3search8

### 4.1. Реализовать registry переводов
- загрузка translation lumps,
- проверка table длины,
- хранение по имени.

### 4.2. Интеграция с player translations
Пока GAMECONF не готов — можно сделать минимально:
- поддержать lump-override built-ins,
- подключить к существующей системе переводов EE (если есть).

**Критерий:** перевод применяется к спрайтам/элементам и не ломает старые переводы.

---

## Веха 5 — SBARDEF (statusbar) (type=`statusbar`, version=`1.0.0`)
SBARDEF — большой и видимый кусок.

Ключевые требования:
- единственный lump `SBARDEF`,
- virtual 320x200, fullscreen overlay,
- выбор bar через `barindex = Max(screenblocks - 10, 0)`,
- логика элементов, условия, numberfonts,
- важно для ID24: id24res.wad добавляет slots 8 и 9. citeturn3search4

### 5.1. Парсер JSON
Реализовать структуры:
- numberfonts, statusbars, sbarelem и их типы,
- conditions.

### 5.2. Рендер и update
- update логики 35Hz,
- рендер независимо от fps,
- transform tree + alignment + clipping,
- animation frames duration.

### 5.3. Интеграция выбора statusbar
- расширить screenblocks (если надо),
- обеспечить корректный индекс.

**Критерий:** SBARDEF из id24res и из тестовых WAD’ов отображается корректно.

---

## Веха 6 — SKYDEFS (type=`skydefs`, version=`1.0.0`)
SKYDEFS:
- единственный lump `SKYDEFS`,
- `F_SKY1` зарезервирован под default behavior,
- типы: Normal / Fire / With Foreground,
- fire: overwrite texture data + priming 64 итерации, update time,
- layered: прозрачность по palette index 0 в foreground. citeturn3search6

### 6.1. Парсер SKYDEFS
- таблица sky entries,
- flatmapping.

### 6.2. Интеграция в sky renderer
- default sky поведение сохранить,
- кастомный sky по texture name,
- animated skies: резолв через систему анимаций текстур.

### 6.3. Fire sky
- симуляция, priming, update time.

**Критерий:** normal/fire/layered работают на примерах.

---

## Веха 7 — DEMOLOOP (type=`demoloop`, version=`1.0.0`)
DEMOLOOP уже частично реализован (`id24_demoloop.cpp`). citeturn2search2

Спека:
- entries: art screen (duration, lumps) и demo,
- wipe types, fallback на vanilla при отсутствии. citeturn6search0

### 7.1. Довести парсер и runtime
- точное соответствие типов,
- корректные тайминги (tic rounding ok).

### 7.2. Интеграция в меню/циклы
- подключить к месту, где EE строит demo loop.

**Критерий:** demo loop следует JSON-описанию.

---

## Веха 8 — Interlevel lumps (type=`interlevel`, version=`1.0.0`)
Ключевое: UMAPINFO additions `enteranim/exitanim` authoritative, и при ошибке JSON — это ошибка без fallback к `enterpic/exitpic`. citeturn7search0

### 8.1. Парсер interlevel
- layers, anims, frames, conditions, backgroundimage, music.

### 8.2. Рендер в intermission/victory
- 320x200 виртуальная среда,
- поддержать “стандартные stats поверх”.

### 8.3. Интеграция с UMAPINFO
- распознать новые поля,
- соблюсти authoritative-правило. citeturn7search0

**Критерий:** примеры interlevel WAD показываются как задумано; при invalid lump — корректная ошибка.

---

## Веха 9 — Finale lumps (type=`finale`, version=`1.0.0`)
Finale lumps дают сценарии финала: art/bunny/cast и возможность перехода на next map.
UMAPINFO `endfinale` authoritative и игнорирует старые поля при наличии. citeturn3search9

### 9.1. Парсер finale
- finale type,
- bunny data,
- castrollcall data (castmembers, frames, sounds, translations).

### 9.2. Интеграция с системой финалов EE
- выбор финала: если endfinale задан — он главный,
- поддержка donextmap.

**Критерий:** кастомные финалы работают и корректно цепляют следующий уровень при необходимости.

---

## Веха 10 — GAMECONF (type=`gameconf`, version=`1.0.0`) и feature-level управление
GAMECONF описывает окружение WAD’а, в том числе “executable” (feature level), “mode”, и порядок резолва/override/additive. citeturn3search5

### 10.1. Парсер GAMECONF + модель “накопления”
Реализовать:
- destructive overwrite полей,
- additive arrays,
- “max operation” для executable/mode, как в документе.

### 10.2. Интеграция в ранний старт
- применить GAMECONF до инициализации подсистем,
- `executable=id24` должен форсировать `id24_enabled`.

### 10.3. Связь с id24res.wad
- GAMECONF — самый ранний “чёткий сигнал”, что нужно id24res.

**Критерий:** WAD с GAMECONF корректно переключает окружение и фичи.

---

## Веха 11 — Mapping Additions (новые linedef/thing правила и UMAPINFO расширения)
Mapping_Additions содержит:
- reserved ranges (line specials 2048–4095 и др.),
- UMAPINFO additions для bossaction (thing number/editor number),
- “any texture/flat on any surface” + прозрачность по palette index 0. citeturn3search7

### 11.1. Реализовать/добавить line specials
- строго по таблице документа (здесь важно брать значения прямо из md).

### 11.2. Усовершенствовать UMAPINFO parsing
- поддержать `bossaction` как thing number,
- `bossactionednum`.

### 11.3. Рендер/материалы: “texture on flats / flats on walls”
- расширить правила резолва текстур/флэтов,
- обеспечить прозрачность индекс 0.

**Критерий:** карты с новыми спец. линиями работают; новые правила материалов видны.

---

## Веха 12 — ID24HACKED (расширения DeHackEd) + новый порядок загрузки
ID24HACKED:
- идентификация через Doom version 2024 и/или спец-поля,
- новый порядок инициализации DEHACKED lumps,
- расширенные диапазоны индексов, `USER_` strings и т.п. citeturn3search2

### 12.1. Детект ID24HACKED
- Doom version 2024 → включить ID24 режим и ID24HACKED расширения.

### 12.2. Расширить парсер DeHackEd
- поддержать новые диапазоны индексов,
- поддержать `USER_` string mnemonics (создание/override). citeturn3search2

### 12.3. Привести порядок применения патчей к спекам
- DEHACKED lumps в WAD’ах применяются в порядке загрузки (IWAD → PWADs),
- далее cmdline DEH (если есть), согласно ID24HACKED. citeturn3search2

**Критерий:** ID24HACKED деh-патчи применяются стабильно и предсказуемо, без регрессий в старых BEX/DEH.

---

## Веха 13 — ID24HACKED hashing (Valid hashes) (самая строгая часть)
Hashing:
- FNV-1a 64-bit, little-endian,
- поле `Valid hashes`,
- строгий порядок “last found per container” для хэш-проверок,
- хэшируются конкретные таблицы/поля, strings не хэшируются. citeturn3search3

### 13.1. Реализовать вычисление hash состояния
- отдельный модуль FNV-1a,
- сериализация данных “как в спеке”.

### 13.2. Реализовать проверку Valid hashes перед применением расширений
- если текущий hash не входит в список — отказ от применения (с понятной ошибкой).

### 13.3. Тесты “цепочки патчей”
- патч2 требует valid hash от состояния после патча1,
- негативные тесты.

**Критерий:** поведение совпадает со спекой; ошибки понятные.

---

## Веха 14 — Музыка: OGG + tracker форматы (если в EE ещё не поддержано)
ID24 требует поддержку дополнительных форматов музыки: OGG Vorbis и tracker (.mod/.s3m/.xm/.it). citeturn3search0

### 14.1. Проверить существующие драйверы музыки EE (SDL_mixer и т.п.)
- возможно OGG уже есть (в SDL_mixer),
- tracker зависит от сборки/библиотек.

### 14.2. Довести до “ID24-ready”
- обеспечить, что при ID24 режиме эти форматы реально проигрываются,
- обеспечить корректный lump resolution (музыка может быть в WAD).

**Критерий:** треки из примеров играют.

---

## Веха 15 — Финальная интеграция и матрица совместимости

### 15.1. “Feature gate” на уровне id24_enabled
Каждый модуль должен:
- работать только если `id24_enabled` (или если его допускает lower-level),
- иначе — не ломать старые WAD’ы.

### 15.2. Регрессионные тесты на не-ID24
- IWAD + популярные PWAD без ID24,
- проверка демосинхронизации (если применимо),
- проверка, что отсутствие id24res.wad не вызывает краш.

### 15.3. Документация для пользователей
- что нужно для ID24 (где взять id24res.wad, как движок его ищет),
- как посмотреть в логах, почему включился ID24,
- как отключить авто-детект (если добавите).

---

# Как этим пользоваться “шаг за шагом” в разработке
Если ты идёшь по плану, удобный порядок реализации (минимум зависимостей → максимум “видимого эффекта”) такой:

1) Веха 1 (авто-детект + фиксация режима)  
2) Веха 2 (общая JSON-инфраструктура)  
3) Веха 3 (id24res.wad)  
4) Веха 4 (Translations)  
5) Веха 5 (SBARDEF)  
6) Веха 6 (SKYDEFS)  
7) Веха 7 (DEMOLOOP)  
8) Веха 8 (Interlevel)  
9) Веха 9 (Finale)  
10) Веха 10 (GAMECONF)  
11) Веха 11 (Mapping additions)  
12) Веха 12 (ID24HACKED)  
13) Веха 13 (Hashing)  
14) Веха 14 (Music formats)  
15) Веха 15 (полировка/регрессии)

---

Если ты захочешь потом “в следующем заходе” перейти от плана к конкретным таскам: я могу разложить каждую веху на чек-лист уровня “какие файлы создать/изменить, какие функции добавить, какие тестовые WAD’ы прогнать”, но сейчас остановился на том формате, который можно реально использовать как дорожную карту разработки.

## @DRON12261

Давай перейдем к конкретным таскам

## Copilot

Ок — давай превратим план в **конкретные задачи** (backlog), по которым можно идти. Я не смог посмотреть issues в репозитории через инструмент (ответ GitHub API `401`), поэтому ниже я сформирую задачи “с нуля” как **эпики + таски** с чёткими критериями готовности и зависимостями.

Скажи только, в каком формате тебе удобнее их вести:
1) просто список в чате, или  
2) **создать GitHub Issues** в `dron12261games/eternity-fork` (я могу, если у меня будет доступ; сейчас API 401 может помешать), или  
3) markdown-файл `docs/id24_todo.md` в репо (тогда нужен write-доступ).

Пока — даю **наиболее практичный “первый спринт”** + полный backlog.

---

## Sprint 1 (MVP фундамента): авто-детект ID24 + фиксация режима + лог

### TASK 1.1 — Добавить “ID24 runtime mode” (единая точка истины)
**Цель:** в коде появляется централизованное состояние `id24_enabled` (и причина включения).

**Подзадачи:**
- Добавить новые глобальные/синглтон-поля (где у вас принято хранить runtime globals, вероятно рядом с `doomstat.*`):
  - `bool id24_detected;`
  - `bool id24_enabled;`
  - `enum id24_enable_reason { ... }` или строка причины.
- Добавить функции:
  - `void ID24_ResetDetection();`
  - `void ID24_Enable(reason, details);`

**Acceptance criteria:**
- Сборка не ломается.
- В лог можно вывести `ID24: enabled (reason=...)`.

---

### TASK 1.2 — Встроить точку “после W_InitMultipleFiles” для детекта
**Цель:** гарантированное место, где WAD directory уже построен, но до DeHackEd/инициализации контента.

**Где:** `source/d_main.cpp` сразу после `wGlobalDir.initMultipleFiles(wadfiles);` и `D_InitGMIPostWads();` (эта точка уже видна в коде). citeturn15search4

**Подзадачи:**
- Добавить вызов `ID24_DetectAndEnableFromLoadedWads();`
- Добавить лог начала/конца детекта.

**Acceptance criteria:**
- При запуске любой игры в лог выводится “ID24 autodetect: …” (даже если “not enabled”).

---

### TASK 1.3 — Реализовать детектор “валидные ID24 JSON-lumps”
**Цель:** если в WAD есть валидный JSON-lump типа ID24 — включить ID24.

**Подзадачи:**
- Сканировать WAD directory на наличие *кандидатных* lumps по именам:
  - `SBARDEF`, `SKYDEFS`, `DEMOLOOP` (и позже: `GAMECONF`, и т.д.).
- Для каждого кандидата:
  - попытаться распарсить **как JSON-lump** через `id24_json` (строго по JSON_Lump.md: root поля, версия и т.д.). citeturn9search0
  - если валидно и `type/version` совпали ожидаемым — `ID24_Enable(reason=ValidJsonLumpFound, details=<name/type/version>)`.

**Acceptance criteria:**
- WAD с валидным `SKYDEFS` (`type=skydefs, version=1.0.0`) включает ID24. citeturn3search6
- WAD с невалидным JSON в `SKYDEFS` не включает ID24, но пишет warning.

---

### TASK 1.4 — Реализовать детектор “DEHACKED Doom version 2024”
**Цель:** если DEHACKED указывает ID24HACKED, включить ID24.

**Подзадачи:**
- Найти/прочитать `DEHACKED` lumps (минимальный быстрый парс заголовка).
- Если Doom version == 2024 → `ID24_Enable(reason=Dehacked2024)` (это маркер ID24HACKED). citeturn3search2

**Acceptance criteria:**
- DEHACKED с Doom version 2024 включает ID24 даже без JSON-lumps.

---

### TASK 1.5 — Связать `id24_enabled` с вашей системой compat/complevel (минимально)
**Цель:** при включении ID24 поднять уровень совместимости “как надо” для ID24-фич.

**Подзадачи (минимальный безопасный вариант):**
- Не менять пока `demo_version` (чтобы не сломать демо-логику), а сделать отдельный “feature gate”:
  - ID24-фичи включаются через `id24_enabled`.
- Добавить *одно* централизованное правило: “если id24_enabled, разрешить новые вещи/слоты/парсеры”.

Позже (в отдельной задаче) можно будет аккуратно вводить полноценный “complevel_id24”/`demo_version` значение, но это рискованнее.

**Acceptance criteria:**
- Авто-детект работает и не меняет поведение на обычных WAD’ах.

---

## Sprint 2 (ресурсы): id24res.wad загрузка и диагностика

### TASK 2.1 — “Если ID24 включён, попытаться подгрузить id24res.wad”
**Подзадачи:**
- Проверить `gi_path_id24res` (у вас уже есть сканер, который его выставляет при обнаружении `id24res.wad`). citeturn10search0
- Если путь есть и WAD ещё не загружен:
  - подгрузить его (как у вас принято добавлять PWAD после старта),
  - залогировать предупреждение, если приоритет не “до IWAD”.

**Acceptance criteria:**
- При наличии `id24res.wad` и включённом ID24 он реально попадает в WAD систему.
- При отсутствии — предупреждение, без краша.

---

## Полный backlog (эпики → задачи)

### EPIC A — JSON platform hardening
- A1: унифицировать обработку ошибок JSON-lumps (type/version/root fields) citeturn9search0
- A2: единый helper `ID24_ParseJsonLump(lumpnum, expectedType, maxVersion, func)`

### EPIC B — Translations (translation 1.0.0)
- B1: registry переводов
- B2: применение к игроку/объектам и к sbar/interlevel backgrounds citeturn3search8

### EPIC C — SBARDEF
- C1: парсер
- C2: layout/render/update 35Hz
- C3: screenblocks→barindex поведение citeturn3search4

### EPIC D — SKYDEFS
- D1: парсер skydefs/flatmapping citeturn3search6
- D2: normal/foreground
- D3: fire sky simulation (priming/update) citeturn3search6

### EPIC E — DEMOLOOP
- E1: довести `id24_demoloop` до полной спеки citeturn6search0
- E2: подключить к main menu loop

### EPIC F — Interlevel
- F1: парсер interlevel citeturn7search0
- F2: интеграция с UMAPINFO `enteranim/exitanim` (authoritative) citeturn7search0

### EPIC G — Finale
- G1: парсер finale citeturn3search9
- G2: интеграция `endfinale` (authoritative) citeturn3search9

### EPIC H — GAMECONF
- H1: парсер + merge semantics citeturn3search5
- H2: ранняя интеграция в startup
- H3: executable=id24 как strongest signal

### EPIC I — Mapping additions
- I1: новые linedef specials / reserved ranges citeturn3search7
- I2: UMAPINFO bossaction expansions citeturn3search7
- I3: “texture-on-flats / flats-on-walls” + прозрачность index 0 citeturn3search7

### EPIC J — ID24HACKED + hashing
- J1: ID24HACKED расширения DeHackEd citeturn3search2
- J2: новый порядок применения DEHACKED lumps citeturn3search2
- J3: hashing + Valid hashes enforcement citeturn3search3

### EPIC K — Music formats
- K1: OGG/tracker playback readiness citeturn3search0

---

## Вопрос, чтобы я разложил это в “готовые Issues/таски” без лишней воды
Сколько задач тебе удобно иметь в трекере на старте?

- Вариант 1 (крупно): ~10–15 задач (по вехам/эпикам)
- Вариант 2 (детально): ~40–80 задач (каждый JSON-lump модуль и интеграция отдельно)

И второй вопрос: хочешь, чтобы “TASK 1.5 (complevel)” был сразу полноценным (через `demo_version`), или сначала безопасный вариант “отдельный id24_enabled feature gate”, а `demo_version` трогать позже?

## @DRON12261

Больше нравятся крупные, но детально расписанные задачи

Лучше попробовать безопасный вариант для demo_version

## Copilot

Принято: делаем **крупные задачи (эпики/вехи)**, но внутри — **детальный чек‑лист подзадач**, и на старте используем **безопасный вариант**: `demo_version` не трогаем, всё ID24-гейтится через `id24_enabled` (а к `demo_version` вернёмся позже, когда будет понятно, что именно нужно для синка/демок).

Ниже — набор “крупных, но детально расписанных” задач в правильном порядке. Это можно буквально копировать в issue tracker как 10–14 больших issues.

---

## TASK 01 — ID24 Runtime Mode + Autodetect (без изменения demo_version)
**Цель:** движок сам включает/выключает ID24 режим и логирует причину. Режим фиксируется один раз после загрузки WAD directory.

### Подзадачи
1) Добавить глобальное состояние:
   - `bool id24_detect_enabled` (на будущее — опция, default true)
   - `bool id24_enabled`
   - `qstring id24_enable_reason` (или enum+details)
2) Добавить API:
   - `ID24_ResetState()`
   - `ID24_Enable(reason, details)`
   - `ID24_IsEnabled()`
3) В `d_main.cpp` вставить точку детекта **после** `wGlobalDir.initMultipleFiles(wadfiles)` и `D_InitGMIPostWads()` и **до** обработки DeHackEd (там у вас как раз начинается “post-wad init”). citeturn15search4
4) Реализовать детектор (поэтапно, с причинами):
   - **(Strong)** валидный JSON-lump-кандидат: `SBARDEF`, `SKYDEFS`, `DEMOLOOP`:
     - попытка распарсить как JSON-lump через `id24_json`
     - проверка `type`/`version` на ожидаемые (`statusbar 1.0.0`, `skydefs 1.0.0`, `demoloop 1.0.0`) citeturn3search4turn3search6turn6search0turn9search0
   - **(Strong)** DEHACKED Doom version = 2024 (ID24HACKED marker) citeturn3search2
   - **(Medium, пока только лог)** наличие `Valid hashes` в DEHACKED (позже включим как strong) citeturn3search3
5) Сделать “фиксацию”: после первого включения ID24 — больше не менять решение в этом запуске.
6) Добавить читаемый лог:
   - `ID24: enabled (reason=..., details=...)`
   - `ID24: not enabled (no signals found)`

### Acceptance criteria
- На обычных WAD’ах поведение не меняется.
- На WAD с валидным `SKYDEFS` или `SBARDEF` — ID24 включается.
- Невалидный JSON не включает ID24, но пишет warning (в стиле JSON_Lump.md). citeturn9search0

---

## TASK 02 — id24res.wad: подключение supporting data (best-effort)
**Цель:** если ID24 включён — EE пытается подгрузить `id24res.wad`.

### Подзадачи
1) Использовать существующую инфраструктуру:
   - `gi_path_id24res` уже ищется при скане каталогов (`id24res.wad`) citeturn10search0
   - есть конфиг/переменная `pwad_id24res` citeturn10search0
2) Реализовать функцию `ID24_TryLoadId24ResWad()`:
   - если `id24_enabled` и путь задан и WAD ещё не добавлен — добавить как PWAD
   - если нет пути — warning “ID24 enabled but id24res.wad not found”
3) Логировать, что подгрузка произошла “после IWAD”, если у вас так получается (чтобы пользователи понимали возможные отличия от рекомендации спеки).
4) (Опционально) добавить config `id24_autoload_res` (default true) — можно отложить.

### Acceptance criteria
- При наличии `id24res.wad` он реально становится доступен через WAD resolution.
- Отсутствие файла не вызывает краш.

---

## TASK 03 — JSON Platform Hardening (единый шаблон загрузки ID24 JSON-lumps)
**Цель:** все новые ID24 фичи подключаются по одному паттерну: find lump → parse JSON-lump → validate → apply.

### Подзадачи
1) Добавить helper API (на базе `id24_json`):
   - `ID24_ParseJsonLumpByName(lumpname, expected_type, max_version, parseFunc)`
2) Единый формат warnings/errors:
   - include: lump name, type/version, поле, причина (лишнее поле, null, неверный тип, версия выше max).
3) Свести повторяющийся код из `id24_demoloop.cpp` к общему стилю (не обязательно переписывать полностью, но унифицировать подход).
4) Подготовить “таблицу ожидаемых type/version”:
   - statusbar=1.0.0, skydefs=1.0.0, demoloop=1.0.0, interlevel=1.0.0, finale=1.0.0, translation=1.0.0, gameconf=1.0.0 citeturn3search4turn3search6turn6search0turn7search0turn3search9turn3search8turn3search5

### Acceptance criteria
- Любой новый JSON-lump модуль можно написать по короткому шаблону.
- При ошибках вывод единообразный и понятный.

---

## TASK 04 — Translations (translation 1.0.0)
**Цель:** базовая поддержка translation lumps и применение таблиц 256.

### Подзадачи
1) Реализовать парсер translation JSON-lump:
   - строгая проверка table=256 citeturn3search8
   - registry по `name`
2) Поддержать built-ins override (T_GREEN/T_INDIGO/T_BROWN/T_RED + ещё 4) citeturn3search8
3) Интегрировать в рендер/спрайты:
   - где у вас применяются translations сейчас — подключить новый registry
4) Подготовить минимальный тестовый WAD: одна translation и простая проверка (или использовать пример из `wad_resources`).

### Acceptance criteria
- translation lump загружается, валидируется, применяется.
- Ошибка в table длине → понятная ошибка, без краша.

---

## TASK 05 — SBARDEF (statusbar 1.0.0)
**Цель:** ID24 статусбар полностью из данных.

### Подзадачи
1) Парсер:
   - numberfonts/statusbars/elements/conditions citeturn3search4
2) Runtime:
   - virtual 320x200, fullscreenrender/nonfullscreen
   - update rate 35Hz (logic) citeturn3search4
   - alignments + transform tree
3) Выбор statusbar по screenblocks:
   - `barindex = Max(screenblocks - 10, 0)` citeturn3search4
4) Интеграция:
   - если `SBARDEF` нет → старый статусбар
   - если есть → новый pipeline
5) Совместимость с id24res.wad (slots 8/9) citeturn3search4

### Acceptance criteria
- SBARDEF из id24res.wad работает (как минимум не ломает HUD, и показывает ожидаемые элементы).
- Невалидный SBARDEF → не крашит, fallback на старый статусбар.

---

## TASK 06 — SKYDEFS (skydefs 1.0.0)
**Цель:** поддержка кастомных небес, layered и fire.

### Подзадачи
1) Парсер skydefs/flatmapping citeturn3search6
2) Интеграция:
   - `F_SKY1` остаётся default behavior citeturn3search6
   - sky по texture name
3) With Foreground:
   - прозрачность по palette index 0 для foreground citeturn3search6
4) Fire sky:
   - update time, palette decay, priming 64 итерации citeturn3search6

### Acceptance criteria
- Normal + layered + fire работают на примерах.
- При отсутствии SKYDEFS — старое небо.

---

## TASK 07 — DEMOLOOP (demoloop 1.0.0)
**Цель:** data-driven demo loop.

### Подзадачи
1) Довести `id24_demoloop.cpp` до полной спеки (entries types, wipe, duration) citeturn6search0
2) Интеграция в main/title/demo loop EE
3) Тест: WAD с демоциклом.

### Acceptance criteria
- Демоцикл воспроизводится как в JSON.
- При отсутствии DEMOLOOP — vanilla поведение.

---

## TASK 08 — Interlevel (interlevel 1.0.0) + UMAPINFO enteranim/exitanim
**Цель:** победные/входные экраны с анимированным фоном.

### Подзадачи
1) Парсер interlevel citeturn7search0
2) Рендер: 320x200, layers/anims/frames
3) Интеграция с UMAPINFO additions:
   - `enteranim/exitanim` authoritative: если поле задано и lump invalid → ошибка, без fallback к enterpic/exitpic citeturn7search0

### Acceptance criteria
- Примеры interlevel работают.
- Authoritative правило соблюдено.

---

## TASK 09 — Finale (finale 1.0.0) + UMAPINFO endfinale
**Цель:** настраиваемые финалы + переход на следующую карту.

### Подзадачи
1) Парсер finale art/bunny/cast citeturn3search9
2) Интеграция endfinale:
   - authoritative: при наличии endfinale игнорировать endpic/endbunny/endcast citeturn3search9
3) `donextmap` поддержка.

### Acceptance criteria
- Кастомный финал работает и может перевести на карту.

---

## TASK 10 — GAMECONF (gameconf 1.0.0) + ранний выбор окружения
**Цель:** полноценный “официальный” способ включать ID24 и задавать окружение.

### Подзадачи
1) Парсер gameconf + merge semantics (destructive/additive/max) citeturn3search5
2) Интеграция в старт:
   - gameconf должен влиять на `id24_enabled`
3) `executable=id24` становится strongest signal.

### Acceptance criteria
- WAD с GAMECONF включает ID24 “официально” и стабильно.

---

## TASK 11 — Mapping Additions
**Цель:** новые specials/правила, необходимые ID24.

### Подзадачи
1) Новые linedef specials (строго по таблицам документа) citeturn3search7
2) UMAPINFO bossaction extensions (`bossaction` as thing number, `bossactionednum`) citeturn3search7
3) “any texture/flat on any surface” + прозрачность index 0 citeturn3search7

### Acceptance criteria
- Карты с новыми specials работают.

---

## TASK 12 — ID24HACKED (DeHackEd extensions)
**Цель:** расширения DeHackEd по ID24HACKED.

### Подзадачи
1) Детект Doom version 2024 и включение расширений citeturn3search2
2) Расширенные диапазоны индексов, USER_ strings citeturn3search2
3) Порядок применения DEHACKED lumps по ID24HACKED.

### Acceptance criteria
- ID24HACKED патчи применяются корректно.

---

## TASK 13 — ID24HACKED Hashing (Valid hashes)
**Цель:** строгая защита от применения патчей к “не тому базовому состоянию”.

### Подзадачи
1) FNV-1a 64-bit, little-endian сериализация citeturn3search3
2) Вычисление hash состояния по таблицам
3) Проверка `Valid hashes` перед применением расширений citeturn3search3

### Acceptance criteria
- Патч с неподходящим hash корректно отклоняется.

---

## TASK 14 — Music formats (OGG + tracker)
**Цель:** соответствие требованию ID24 по музыке. citeturn3search0

### Подзадачи
1) Аудит текущего music backend (SDL_mixer/и т.д.)
2) Поддержка OGG и tracker (если не включено сборкой)
3) Тест на WAD с такими треками.

---

# Что делаем прямо сейчас (следующий шаг)
Предлагаю начать с **TASK 01 + TASK 02** (это даст “скелет” и позволит прогонять примеры дальше).

Скажи, пожалуйста:
1) Хочешь ли ты, чтобы я оформил это как **один “master checklist”** (например, markdown), или сразу разложил на **Issues**?  
2) Есть ли у тебя предпочтения по именованию: `ID24_*` префикс для функций/файлов, или держаться текущего стиля EE?

## @DRON12261

1) Давай markdown, но по возможности попробуй завести issues.
2) Лучше держаться стиля EE.

## Copilot



*System: User retried message*

## Copilot

Сделаю: подготовлю `docs/id24_todo.md` в формате master‑чеклиста (с крупными задачами, но подробными подзадачами, зависимостями и критериями готовности) и параллельно попробую завести issues — но сейчас создание/чтение задач на GitHub может не сработать из‑за ограничений доступа (раньше запросы к issue tracker вернули ошибку авторизации), поэтому markdown — гарантированно, а issues — “best effort”.

Чтобы не тянуть время, вот содержимое markdown, которое я положу в `docs/id24_todo.md` (можешь уже сейчас скопировать в репозиторий вручную, если хочешь):

````markdown name=docs/id24_todo.md
# ID24 support roadmap for Eternity Engine (EE) — actionable checklist

Repository: `dron12261games/eternity-fork`  
Branch: `master`  
Spec source: `doom-cross-port-collab/id24` (version_0_99_2_md)

Guiding constraints for early implementation:
- **Safe mode first:** do **not** change `demo_version` initially; gate ID24 functionality behind `id24_enabled`.
- Autodetect must be **predictable**: enable once, log reason, do not “flip-flop” mid-startup.
- Prefer EE existing conventions/style (naming, logging, error handling).

Key EE anchor points / files:
- Startup / WAD init: `source/d_main.cpp` (after `wGlobalDir.initMultipleFiles(wadfiles)` and `D_InitGMIPostWads()`).
- JSON lumps infrastructure: `source/id24_json.h`, `source/id24_json.cpp`.
- Example ID24 module: `source/id24_demoloop.cpp`.
- id24res.wad path + config: `source/d_findiwads.cpp`, `source/g_cmd.cpp`, `source/m_syscfg.cpp`, `source/mn_menus.cpp`, `source/d_gi.*`.

---

## TASK 01 — ID24 runtime mode + autodetect (safe: no demo_version changes)
**Goal:** EE automatically enables ID24 mode when ID24 content is detected.

**Depends on:** none.

### Work items
- [ ] Add central state (location per EE conventions, e.g. `doomstat.*` or dedicated `id24_*.cpp`):
  - [ ] `bool id24_enabled = false;`
  - [ ] `bool id24_autodetect = true;` (optional now; default true)
  - [ ] `qstring id24_reason;` / (or enum + details string)
- [ ] Add API:
  - [ ] `void ID24_Reset();`
  - [ ] `void ID24_Enable(const char* reason, const char* details);`
  - [ ] `bool ID24_Enabled();`
- [ ] Insert detection call in startup:
  - [ ] In `source/d_main.cpp`, call `ID24_DetectAndEnableFromLoadedWads()` **after**:
    - `wGlobalDir.initMultipleFiles(wadfiles);`
    - `D_InitGMIPostWads();`
    and **before** DeHackEd processing begins.
- [ ] Implement strong detection signals:
  - [ ] JSON-lump candidates: attempt parse/validate for `SBARDEF`, `SKYDEFS`, `DEMOLOOP`
    - If valid JSON-lump and expected type+version matches, enable ID24.
  - [ ] DEHACKED marker: Doom version == 2024 → enable ID24 (ID24HACKED marker).
- [ ] Implement logging:
  - [ ] `ID24: enabled (reason=..., details=...)`
  - [ ] `ID24: not enabled (no ID24 signals found)`
- [ ] Fixation:
  - [ ] Once enabled, do not disable in same startup.

### Acceptance criteria
- ID24 is **not** enabled for normal non-ID24 WADs.
- ID24 is enabled when a valid `SKYDEFS` / `SBARDEF` / `DEMOLOOP` JSON-lump is present.
- Invalid JSON in those lumps does **not** enable ID24, but emits a warning.

Notes
- Start with `id24_enabled` gating only; later we can map to complevel/demo semantics if needed.

---

## TASK 02 — id24res.wad best-effort loading when ID24 is enabled
**Goal:** if ID24 mode is enabled, attempt to load `id24res.wad`.

**Depends on:** TASK 01.

### Work items
- [ ] Implement `ID24_TryLoadId24Res()`:
  - [ ] Use existing `gi_path_id24res` path (auto-scan) and config variable (`pwad_id24res`).
  - [ ] If path is set and WAD not already loaded, load as PWAD (best effort).
  - [ ] If missing → warning, not fatal.
- [ ] Log:
  - [ ] `ID24: id24res.wad loaded from <path>`
  - [ ] `ID24: id24res.wad not found; some resources may be missing`
- [ ] Optional (later): improve priority/order (load before IWAD) once GAMECONF/early detection exists.

### Acceptance criteria
- With `id24res.wad` present, EE loads it when ID24 is enabled.
- Without it, EE continues gracefully with warnings.

---

## TASK 03 — JSON platform hardening (common loader + consistent diagnostics)
**Goal:** unify JSON-lump parsing/validation/apply pattern.

**Depends on:** TASK 01.

### Work items
- [ ] Add helper wrapper (in `id24_json.*` or companion module):
  - [ ] `ID24_ParseJSONLumpByName(...)`
  - [ ] consistent error/warn formatting (lump name, type/version, path to field).
- [ ] Ensure strict JSON_Lump rules are enforced:
  - [ ] root has exactly `type`, `version`, `metadata`, `data`
  - [ ] reject forward versions > supported
  - [ ] reject unknown root fields
- [ ] Update existing ID24 module (`id24_demoloop.cpp`) to use the shared helper (optional but recommended for consistency).

### Acceptance criteria
- New JSON-lump modules can be added quickly by copying a small template.
- Warnings/errors are consistent across all ID24 features.

---

## TASK 04 — Translations (type=translation, version=1.0.0)
**Goal:** parse/apply ID24 translation tables.

**Depends on:** TASK 03 (recommended), TASK 01 (for gating).

### Work items
- [ ] Implement translation registry keyed by translation name.
- [ ] Parse/validate:
  - [ ] table length == 256 strict
  - [ ] built-in overrides supported (T_GREEN/T_RED/etc) per spec.
- [ ] Integrate with rendering/thing translation pipeline (per EE architecture).
- [ ] Add minimal test WAD or use `wad_resources` examples.

### Acceptance criteria
- Valid translation loads and applies.
- Invalid translation fails gracefully with readable error.

---

## TASK 05 — SBARDEF (type=statusbar, version=1.0.0)
**Goal:** data-driven status bar.

**Depends on:** TASK 03, TASK 01 (+ TASK 02 recommended because id24res provides SBARDEF).

### Work items
- [ ] JSON parse structures: `numberfonts`, `statusbars`, `elements`, `conditions`.
- [ ] Runtime:
  - [ ] virtual 320x200
  - [ ] update at 35Hz
  - [ ] element tree + alignment + clipping
- [ ] Selection via screenblocks extension: `barindex = max(screenblocks - 10, 0)`.
- [ ] Fallback to old status bar when SBARDEF missing/invalid.

### Acceptance criteria
- SBARDEF from `id24res.wad` renders without regressions.
- Selection of bar index works as specified.

---

## TASK 06 — SKYDEFS (type=skydefs, version=1.0.0)
**Goal:** custom skies including layered + fire.

**Depends on:** TASK 03, TASK 01.

### Work items
- [ ] Parse `skies` + `flatmapping`.
- [ ] Integrate:
  - [ ] keep default `F_SKY1` behavior
  - [ ] apply custom defs per texture name
- [ ] With Foreground: palette index 0 transparency in foreground.
- [ ] Fire sky: simulation + priming + update timing.

### Acceptance criteria
- Normal/foreground/fire skies work on examples.
- Absent SKYDEFS → legacy behavior.

---

## TASK 07 — DEMOLOOP (type=demoloop, version=1.0.0)
**Goal:** configurable demo loop.

**Depends on:** TASK 03, TASK 01.

### Work items
- [ ] Finish spec compliance in `id24_demoloop.cpp` (entry types, wipes, durations).
- [ ] Integrate into main/title loop.
- [ ] Tests with example WAD.

### Acceptance criteria
- Loop order/timing/wipes match JSON.

---

## TASK 08 — Interlevel (type=interlevel, version=1.0.0) + UMAPINFO enteranim/exitanim
**Goal:** animated intermission backgrounds.

**Depends on:** TASK 03, TASK 01.

### Work items
- [ ] Parse interlevel JSON (layers/anims/frames/conditions).
- [ ] Render in intermission/victory contexts (virtual 320x200).
- [ ] UMAPINFO integration:
  - [ ] `enteranim`/`exitanim` authoritative: if specified and invalid → error, no fallback.
- [ ] Tests.

### Acceptance criteria
- Examples render as specified.
- Authoritative error behavior matches spec intent.

---

## TASK 09 — Finale lumps (type=finale, version=1.0.0) + UMAPINFO endfinale
**Goal:** customizable finale sequences + optional next-map continuation.

**Depends on:** TASK 03, TASK 01.

### Work items
- [ ] Parse finale types (art/bunny/cast).
- [ ] Integrate endfinale authoritative behavior.
- [ ] Support `donextmap` continuation.
- [ ] Tests.

### Acceptance criteria
- Finale scenarios work on sample WADs.

---

## TASK 10 — GAMECONF (type=gameconf, version=1.0.0)
**Goal:** official way to define runtime environment and feature level.

**Depends on:** TASK 03, TASK 01.

### Work items
- [ ] Parse gameconf + merge semantics (destructive/additive/max rules).
- [ ] Apply early in startup (before feature init that depends on it).
- [ ] `executable=id24` should force ID24 (strongest signal).
- [ ] Hook to id24res loading policy.

### Acceptance criteria
- Mods using GAMECONF reliably switch to ID24.

---

## TASK 11 — Mapping additions
**Goal:** implement new linedef specials + UMAPINFO bossaction extensions + surface rules.

**Depends on:** TASK 01 (gating), plus existing map parsing modules.

### Work items
- [ ] Add linedef specials per spec table.
- [ ] UMAPINFO bossaction extensions:
  - [ ] `bossaction` accepts thing number
  - [ ] `bossactionednum` editor-number mapping
- [ ] Render/surface rule: allow texture-on-flat / flat-on-wall + palette index 0 transparency.

### Acceptance criteria
- Sample maps using mapping additions behave correctly.

---

## TASK 12 — ID24HACKED (DeHackEd extensions)
**Goal:** implement extended DeHackEd semantics.

**Depends on:** TASK 01 (enable), existing DeHackEd system.

### Work items
- [ ] Detect Doom version 2024 and enable ID24HACKED behavior.
- [ ] Support expanded index ranges and USER_ mnemonics.
- [ ] Implement/verify load order for DEHACKED lumps per spec.

### Acceptance criteria
- ID24HACKED patches apply consistently.

---

## TASK 13 — ID24HACKED hashing (Valid hashes)
**Goal:** enforce correct patch stacking.

**Depends on:** TASK 12.

### Work items
- [ ] Implement FNV-1a 64-bit hashing per spec.
- [ ] Compute hash of current state.
- [ ] Parse/validate `Valid hashes` and enforce before applying patch extensions.
- [ ] Tests: good hash chain / bad chain.

### Acceptance criteria
- Incorrect stacking is rejected with clear error.

---

## TASK 14 — Music formats readiness (OGG + tracker)
**Goal:** satisfy ID24 requirement for music formats.

**Depends on:** none (but best validated in ID24 test WADs).

### Work items
- [ ] Audit current EE music backend.
- [ ] Ensure OGG playback works.
- [ ] Ensure tracker formats (.mod/.s3m/.xm/.it) work (build deps).
- [ ] Add test cases.

### Acceptance criteria
- Audio formats play in ID24 content.

---

## Appendix — suggested implementation order
1) TASK 01 → 02 → 03  
2) TASK 04 → 05 → 06  
3) TASK 07 → 08 → 09  
4) TASK 10 → 11  
5) TASK 12 → 13  
6) TASK 14

````

### Про issues
Я попробую завести issues в `dron12261games/eternity-fork` по этим 14 крупным задачам (один issue = один TASK) и добавить ссылки/чеклист внутрь, но это может не пройти из‑за текущих ограничений доступа к issue tracker.

Если хочешь увеличить шанс, что issues заведутся:
- проверь, что репозиторий публичный (или что у интеграции/бота есть доступ),
- или просто скажи “создавай issues даже если часть не получится” — я сделаю best effort и напишу, что именно не удалось.

Какой вариант предпочитаешь: **14 issues (по TASK 01–14)** или **7 issues (по блокам: фундамент / UI / skies / interlevel+finale / gameconf / mapping / deh+hash)**?

*System: User retried message*

## @DRON12261

Давай 14 штук. И хотелось бы сделать дубликат этого *.md на русском языке. Issues лучше завести на английском. 

## Copilot

Не получится завести 14 issues автоматически: инструмент создания issues в этом окружении не принимает текст задач (он возвращает только “намерение/метаданные”), а чтение/поиск issues в репозитории ранее упиралось в `401` (нет доступа к GitHub Issues API для твоего репозитория). Поэтому сделаю так:

1) Дам тебе **два готовых markdown-файла** (EN + RU), которые можно положить в репозиторий.  
2) В английском файле добавлю секцию **“Issue templates (copy/paste)”** — 14 готовых блоков (Title + Body) для ручного создания issues на GitHub (занимает 5–10 минут).  
3) Если позже доступ появится (или ты включишь GitHub App/permissions), я смогу уже “протолкнуть” issues автоматически.

Ниже — оба файла.

---

````markdown name=docs/id24_todo.md
# ID24 support roadmap for Eternity Engine (EE) — actionable checklist

Repo: `dron12261games/eternity-fork`  
Branch: `master`  
Specs: `doom-cross-port-collab/id24` (folder: `version_0_99_2_md`)

See also (Russian): `docs/id24_todo_ru.md`

## Guiding constraints (early phase)
- **Safe mode first:** do **not** change `demo_version` initially. Gate new behavior behind `id24_enabled`.
- Autodetect must be **predictable**: enable once, log reason, do not “flip-flop” mid-startup.
- Keep EE coding/style conventions (naming, logging, error handling).

## Key EE anchor points / files (confirmed)
- Startup / WAD init: `source/d_main.cpp` (after `wGlobalDir.initMultipleFiles(wadfiles)` and `D_InitGMIPostWads()`).
- JSON lumps infra: `source/id24_json.h`, `source/id24_json.cpp`.
- Example ID24 module: `source/id24_demoloop.cpp`.
- id24res.wad path + config: `source/d_findiwads.cpp`, `source/g_cmd.cpp`, `source/m_syscfg.cpp`, `source/mn_menus.cpp`, `source/d_gi.*`.

---

## TASK 01 — ID24 runtime mode + autodetect (safe: no demo_version changes)
**Goal:** EE enables ID24 mode automatically when ID24 content is detected. Decision is made once after WAD dir is built and logged.

**Depends on:** none.

### Work items
- [ ] Add central state (where it fits EE conventions, e.g. `doomstat.*` or a dedicated `id24_*.cpp`):
  - [ ] `bool id24_enabled = false;`
  - [ ] `bool id24_autodetect = true;` (optional now; default true)
  - [ ] `qstring id24_reason;` (or enum + details string)
- [ ] Add API:
  - [ ] `void ID24_Reset();`
  - [ ] `void ID24_Enable(const char* reason, const char* details);`
  - [ ] `bool ID24_Enabled();`
- [ ] Insert detection call in startup:
  - [ ] In `source/d_main.cpp`, call `ID24_DetectAndEnableFromLoadedWads()` **after**
    - `wGlobalDir.initMultipleFiles(wadfiles);`
    - `D_InitGMIPostWads();`
    and **before** DeHackEd processing begins.
- [ ] Implement strong detection signals:
  - [ ] JSON-lump candidates: attempt parse/validate for `SBARDEF`, `SKYDEFS`, `DEMOLOOP`
    - If valid JSON-lump and expected type+version matches, enable ID24.
  - [ ] DEHACKED marker: Doom version == 2024 → enable ID24 (ID24HACKED marker).
- [ ] Implement logging:
  - [ ] `ID24: enabled (reason=..., details=...)`
  - [ ] `ID24: not enabled (no ID24 signals found)`
- [ ] Fixation:
  - [ ] Once enabled, do not disable in same startup.

### Acceptance criteria
- ID24 is **not** enabled for normal non-ID24 WADs.
- ID24 is enabled when a valid `SKYDEFS` / `SBARDEF` / `DEMOLOOP` JSON-lump is present.
- Invalid JSON in those lumps does **not** enable ID24, but emits a warning.

---

## TASK 02 — id24res.wad best-effort loading when ID24 is enabled
**Goal:** if ID24 mode is enabled, attempt to load `id24res.wad` (supporting data).

**Depends on:** TASK 01.

### Work items
- [ ] Implement `ID24_TryLoadId24Res()`:
  - [ ] Use existing `gi_path_id24res` (auto-scan) and config variable (`pwad_id24res`).
  - [ ] If path is set and WAD not already loaded, load as PWAD (best effort).
  - [ ] If missing → warning, not fatal.
- [ ] Log:
  - [ ] `ID24: id24res.wad loaded from <path>`
  - [ ] `ID24: id24res.wad not found; some resources may be missing`
- [ ] Optional (later): improve priority/order (load before IWAD) once GAMECONF/early detection exists.

### Acceptance criteria
- With `id24res.wad` present, EE loads it when ID24 is enabled.
- Without it, EE continues gracefully with warnings.

---

## TASK 03 — JSON platform hardening (common loader + consistent diagnostics)
**Goal:** unify JSON-lump parsing/validation/apply pattern for all ID24 JSON features.

**Depends on:** TASK 01.

### Work items
- [ ] Add helper wrapper (in `id24_json.*` or a companion module):
  - [ ] `ID24_ParseJSONLumpByName(...)`
  - [ ] consistent error/warn formatting (lump name, type/version, path to field)
- [ ] Ensure strict JSON_Lump rules are enforced:
  - [ ] root has exactly `type`, `version`, `metadata`, `data` (no extras)
  - [ ] reject forward versions > supported
  - [ ] reject unknown root fields
- [ ] Update existing module (`id24_demoloop.cpp`) to use the shared helper (optional but recommended).

### Acceptance criteria
- New JSON-lump modules can be added by copying a small template.
- Warnings/errors are consistent across all ID24 features.

---

## TASK 04 — Translations (type=translation, version=1.0.0)
**Goal:** parse/apply ID24 translation tables.

**Depends on:** TASK 03 (recommended), TASK 01 (for gating).

### Work items
- [ ] Implement translation registry keyed by translation name.
- [ ] Parse/validate:
  - [ ] table length == 256 strict
  - [ ] built-in overrides supported per spec
- [ ] Integrate with rendering/thing translation pipeline (per EE architecture).
- [ ] Add minimal test WAD or use `wad_resources` examples.

### Acceptance criteria
- Valid translation loads and applies.
- Invalid translation fails gracefully with readable error.

---

## TASK 05 — SBARDEF (type=statusbar, version=1.0.0)
**Goal:** data-driven status bar.

**Depends on:** TASK 03, TASK 01 (+ TASK 02 recommended because id24res provides SBARDEF).

### Work items
- [ ] JSON parse structures: `numberfonts`, `statusbars`, `elements`, `conditions`.
- [ ] Runtime:
  - [ ] virtual 320x200
  - [ ] update at 35Hz
  - [ ] element tree + alignment + clipping
- [ ] Selection via screenblocks extension: `barindex = max(screenblocks - 10, 0)`.
- [ ] Fallback to old status bar when SBARDEF missing/invalid.

### Acceptance criteria
- SBARDEF from `id24res.wad` renders without regressions.
- Selection of bar index works as specified.

---

## TASK 06 — SKYDEFS (type=skydefs, version=1.0.0)
**Goal:** custom skies including layered + fire.

**Depends on:** TASK 03, TASK 01.

### Work items
- [ ] Parse `skies` + `flatmapping`.
- [ ] Integrate:
  - [ ] keep default `F_SKY1` behavior
  - [ ] apply custom defs per texture name
- [ ] With Foreground: palette index 0 transparency in foreground.
- [ ] Fire sky: simulation + priming + update timing.

### Acceptance criteria
- Normal/foreground/fire skies work on examples.
- Absent SKYDEFS → legacy behavior.

---

## TASK 07 — DEMOLOOP (type=demoloop, version=1.0.0)
**Goal:** configurable demo loop.

**Depends on:** TASK 03, TASK 01.

### Work items
- [ ] Finish spec compliance in `id24_demoloop.cpp` (entry types, wipes, durations).
- [ ] Integrate into main/title loop.
- [ ] Tests with example WAD.

### Acceptance criteria
- Loop order/timing/wipes match JSON.

---

## TASK 08 — Interlevel (type=interlevel, version=1.0.0) + UMAPINFO enteranim/exitanim
**Goal:** animated intermission backgrounds.

**Depends on:** TASK 03, TASK 01.

### Work items
- [ ] Parse interlevel JSON (layers/anims/frames/conditions).
- [ ] Render in intermission/victory contexts (virtual 320x200).
- [ ] UMAPINFO integration:
  - [ ] `enteranim`/`exitanim` authoritative: if specified and invalid → error, no fallback.

### Acceptance criteria
- Examples render as specified.
- Authoritative error behavior matches spec intent.

---

## TASK 09 — Finale lumps (type=finale, version=1.0.0) + UMAPINFO endfinale
**Goal:** customizable finale sequences + optional next-map continuation.

**Depends on:** TASK 03, TASK 01.

### Work items
- [ ] Parse finale types (art/bunny/cast).
- [ ] Integrate endfinale authoritative behavior.
- [ ] Support `donextmap` continuation.
- [ ] Tests.

### Acceptance criteria
- Finale scenarios work on sample WADs.

---

## TASK 10 — GAMECONF (type=gameconf, version=1.0.0)
**Goal:** official way to define runtime environment and feature level.

**Depends on:** TASK 03, TASK 01.

### Work items
- [ ] Parse GAMECONF + merge semantics (destructive/additive/max rules).
- [ ] Apply early in startup (before feature init that depends on it).
- [ ] `executable=id24` should force ID24 (strongest signal).
- [ ] Hook to id24res loading policy.

### Acceptance criteria
- Mods using GAMECONF reliably switch to ID24.

---

## TASK 11 — Mapping additions
**Goal:** implement new linedef specials + UMAPINFO bossaction extensions + surface rules.

**Depends on:** TASK 01.

### Work items
- [ ] Add linedef specials per spec table.
- [ ] UMAPINFO bossaction extensions:
  - [ ] `bossaction` accepts thing number
  - [ ] `bossactionednum` editor-number mapping
- [ ] Render/surface rule: allow texture-on-flat / flat-on-wall + palette index 0 transparency.

### Acceptance criteria
- Sample maps using mapping additions behave correctly.

---

## TASK 12 — ID24HACKED (DeHackEd extensions)
**Goal:** implement extended DeHackEd semantics.

**Depends on:** TASK 01, existing DeHackEd system.

### Work items
- [ ] Detect Doom version 2024 and enable ID24HACKED behavior.
- [ ] Support expanded index ranges and USER_ mnemonics.
- [ ] Implement/verify load order for DEHACKED lumps per spec.

### Acceptance criteria
- ID24HACKED patches apply consistently.

---

## TASK 13 — ID24HACKED hashing (Valid hashes)
**Goal:** enforce correct patch stacking.

**Depends on:** TASK 12.

### Work items
- [ ] Implement FNV-1a 64-bit hashing per spec.
- [ ] Compute hash of current state.
- [ ] Parse/validate `Valid hashes` and enforce before applying patch extensions.
- [ ] Tests: good hash chain / bad chain.

### Acceptance criteria
- Incorrect stacking is rejected with clear error.

---

## TASK 14 — Music formats readiness (OGG + tracker)
**Goal:** satisfy ID24 requirement for music formats.

**Depends on:** none (but best validated with ID24 test WADs).

### Work items
- [ ] Audit current EE music backend.
- [ ] Ensure OGG playback works.
- [ ] Ensure tracker formats (.mod/.s3m/.xm/.it) work (build deps).
- [ ] Add test cases.

### Acceptance criteria
- Audio formats play in ID24 content.

---

# Issue templates (copy/paste)
Create 14 GitHub issues using the titles and bodies below.

## Issue 01
**Title:** ID24: runtime mode + autodetect (safe, no demo_version changes)  
**Body:** Copy TASK 01 above.

## Issue 02
**Title:** ID24: best-effort loading of id24res.wad when ID24 is enabled  
**Body:** Copy TASK 02 above.

## Issue 03
**Title:** ID24: JSON platform hardening (common loader + consistent diagnostics)  
**Body:** Copy TASK 03 above.

## Issue 04
**Title:** ID24: Translations (translation 1.0.0)  
**Body:** Copy TASK 04 above.

## Issue 05
**Title:** ID24: SBARDEF (statusbar 1.0.0)  
**Body:** Copy TASK 05 above.

## Issue 06
**Title:** ID24: SKYDEFS (skydefs 1.0.0)  
**Body:** Copy TASK 06 above.

## Issue 07
**Title:** ID24: DEMOLOOP (demoloop 1.0.0)  
**Body:** Copy TASK 07 above.

## Issue 08
**Title:** ID24: Interlevel lumps (interlevel 1.0.0) + UMAPINFO enteranim/exitanim  
**Body:** Copy TASK 08 above.

## Issue 09
**Title:** ID24: Finale lumps (finale 1.0.0) + UMAPINFO endfinale  
**Body:** Copy TASK 09 above.

## Issue 10
**Title:** ID24: GAMECONF (gameconf 1.0.0)  
**Body:** Copy TASK 10 above.

## Issue 11
**Title:** ID24: Mapping additions (linedef specials + UMAPINFO bossaction extensions + surface rules)  
**Body:** Copy TASK 11 above.

## Issue 12
**Title:** ID24: ID24HACKED (DeHackEd extensions)  
**Body:** Copy TASK 12 above.

## Issue 13
**Title:** ID24: ID24HACKED hashing (Valid hashes)  
**Body:** Copy TASK 13 above.

## Issue 14
**Title:** ID24: Music formats readiness (OGG + tracker)  
**Body:** Copy TASK 14 above.
````

---

````markdown name=docs/id24_todo_ru.md
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
````

---

## Как быстро завести 14 issues вручную
1) Открой `Issues` → `New issue`.
2) Скопируй Title/Body из секции **Issue templates** в английском файле `docs/id24_todo.md`.
3) Создай 14 штук.

Если хочешь, я могу прямо здесь в следующем сообщении вывести **14 готовых блоков** в формате:

- Title: `...`
- Body (markdown): ```...```

чтобы было проще копировать по одному.

---

## Следующий конкретный шаг разработки
Предлагаю начать с **TASK 01**. Чтобы не гадать, скажи:
- ты хочешь хранить `id24_enabled` в `doomstat.*` рядом с другими глобалами, или предпочитаешь отдельный модуль типа `id24_main.*` (но в EE стиле)?
