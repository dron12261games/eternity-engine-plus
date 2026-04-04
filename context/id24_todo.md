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