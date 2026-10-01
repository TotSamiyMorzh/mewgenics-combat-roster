# Combat Roster Panel — MODLOG

Journal for the Mewgenics "Combat Roster Panel" QoL mod. Anything not written here is lost.

## Target
- Game: Mewgenics (Steam 686060), `C:\Program Files (x86)\Steam\steamapps\common\Mewgenics`
- Build: Steam buildid 25143593 = game 1.1.21239
- `Mewgenics.exe` SHA256 `4127cd6a792ae528bca6f65a8873dd61789591937d87656c2b586a5e30eb77ea`
  (matches the build mgmp and cat-bridge are pinned to)
- Engine: Tyler Glaiel's native C++ engine (`glaiel::`), SDL3 statically linked with the
  dynamic-API shim, OpenGL 3.2 core, Flash SWF UI player, GON data in `resources.gpak`.
  **Not Unity**: no Mono, no BepInEx/Harmony.
- Anti-cheat: none. Single-player.

## Route (decided 2026-10-01)
Native DLL mod loaded by **Mewjector** (version.dll proxy, MIT, community standard) from `mods/`.
- Function hooks via `MJ_InstallHook` (chains with other mods).
- Screen-space UI: Dear ImGui (win32 + opengl3 backends) drawn from the `SDL_GL_SwapWindow`
  **DYNAPI jump-table slot** (RVA `0x012DE650`), written lazily after SDL init.
  Hooking the SDL thunk/stub with a splice never fires (mgmp CLAUDE.md, "SDL3 is statically linked").
- Board highlight: the game's own immediate-mode UI `tile_piece` (the call that draws the aim
  `TargetCursor`) on the hovered unit's tile, submitted from `StatusMenu::update`.
  NOT `sub_140138A10` (range highlight) — it applies statuses and mutates the simulation.

## Sources (reference only, kept in ~/mewgenics-re, never committed)
- SanTertrust/mgmp (MIT) — battle structures, swap slot, tile_piece ABI. CLAUDE.md is gold.
- p0lymeric/mewgenics_analysis (MIT) — `CatData` layout (`types/glaiel_cat.hpp`).
- z3ndroot/mewgenics-cat-bridge (MIT) — CatData offsets, MewDirector.
- githubuser508/mewjector (MIT) — loader + API.
- Avoid: "Mewgenics-Mods-Menu", "External-Trainer 2026" repos (trainer/release-bait pattern, unverified).

## Known facts (from mgmp, this build)
| What | Where |
|---|---|
| char list | `TurnControl+0x18 ->+0x08 ->+0x20 ->+0x1F90` = `{u32 rc, pad, u32 cap@8, u32 count@12, Character** data@16}` |
| HP / shield / maxHP / dead | `Character+0x4B0` i32 / `+0x4B4` i32 / `+0x4BC` i32 / `+0x4C2` bool |
| Character -> TacticsObject | `+0x60`; back-link `TacticsObject+0x98` |
| tile | `TacticsObject+0x48` iVec2D; off-board sentinel (-5000,-5000) |
| abilities | `+0xD0` move, `+0xD8` attack, `+0xE0` bonus, `+0xE8` spells `{u32 cap,u32 count,Ability** @+0xF0}` |
| ability GON name | `*(Ability+0x28)+0x88` std::string |
| passives cache | `Character+0xE70` `{u32 cap,u32 count,Passive**}` — lazy, dirty flag `+0xE50` |
| brain | `Character+0x68`; `Brain+0x38` = Character |
| current actor | `TurnControl+0x68` |
| ImmediateModeGameUI from Component | `0x14013C570` (null off battle) |
| tile_piece | `0x14033FFD0` — rgba/scale args must be 16-byte aligned; strings consumed, keep <=15 chars |
| StatusMenu::update | `0x140817320` (battle HUD tick) |
| engine mouse cache | RVA `0x012F2E80` two doubles |
| RTTI | full MSVC RTTI ships — class names from vptr |

## Found by us [CR] (static, capstone; RVAs this build)
| What | Where | Evidence |
|---|---|---|
| team (current) | `Character+0x350` i16 | `sub_14011A3C0`/`sub_14011A410` select +0x350 vs +0x358 on a bool arg |
| team (original) | `Character+0x358` i16 | same; differs under mind control |
| team tables | base = `lea r11` target in `sub_14011A410` (data RVA 0x13C4550); `+0x0E` is-ally[14], `+0xD2` is-enemy[14]; team>0xD -> team==1 | portrait frame picker `sub_14011D770` uses +0x0E table then picks "cat"/"ally"; else is-enemy -> "boss"/"enemy" |
| unit kind | `Character+0xCF4` i32: 2 boss, 4 object | same picker: 4 -> "object", 2 -> "boss" |
| is_enemy(ch, current) | `sub_14011A3C0` pure leaf | |
| uses_per_fight | `Ability+0x64` | Ability GON parse `sub_14002B3B0` @ 0x2BBB6 |
| statuses | RTTI class name of each entry in the passive cache == keyword_tooltips.gon key (Bleed, Poison, DodgeChance_Status...) | `glaiel::apply_status` returns `Status*`; all are Passive subclasses |
| CatData layout | name wstring 0x18, stats 0x6F0/0x70C/0x728, passives 0x910/0x938, mutations 0x960/0x988, equipment 5x0x60 @0x9B0 (name +8), level 0xC30, sql_key 0xC48, coi 0xC50 | p0lymeric struct, cross-checked vs cat-bridge + mgmp offsets |

## Unknown (to reverse)
- [x] team / ally-vs-enemy
- [~] Character -> CatData: runtime discovery (scan Character for a validated CatData*), logs the offset
- [~] statuses: names done via RTTI; stack count field unknown (dump_passives logs raw fields on first battle)
- [ ] mana / max mana (strings `current_mana`, `is_at_max_mana` are condition ids in Ability GON, not fields)
- [ ] ability cooldown remaining (`initial_cooldown` parsed in `sub_1401CE4A0`)
- [ ] portrait: game draws it from SWF; v1 uses a tinted initial tile

## Log
- 2026-10-01: recon. `um` CLI fixed on Windows via `~/bin/um.cmd` shim (bin/um is bash).
  VS 18 BuildTools + C++ workload/SDK 26100/CMake installed. Ghidra 12.1.4 + Temurin 21 installed,
  headless analysis project at ~/mewgenics-re/ghidra.
- 2026-10-01: Mewjector built from source (githubuser508/mewjector @ccdd681, has EP fallback) and installed:
  `version.dll` + `chainloader.ini` + `mods/combat_roster.dll` in the game folder.
  **Uninstall:** delete those three plus `mod_logs/`. Saves backed up:
  `~/.universal-modder/backups/mewgenics-saves/20261001-235328.zip`.
- 2026-10-01: smoke test to intro: Mewjector loads us, 3 hooks OK, integrity OK, swap slot hooked,
  ImGui up on the SDL window, no crash. The DYNAPI table was ALREADY live at DllMain (slot held the
  real swap @ rva 0xBE94C0, default stub is @ 0xB9C5D0 and ends `48 FF 25 -> slot`), so install
  detects the stub by its self-reference instead of waiting for the value to change.
- 2026-10-02: first in-battle test by the user: panel works. CatData = `Character+0x88` (discovery log).
  Feedback: move panel lower / make it draggable; native look (frames, icons, portraits); Zaratana shown
  as "Character"; Russian names/descriptions.
- 2026-10-02: live RE via ReadProcessMemory (`~/mewgenics-re/live.py`, read-only) on the running battle:
  | What | Where |
  |---|---|
  | name loc key / desc key | `Character+0x248` / `+0x268` std::string (e.g. ENEMY_ZARATANAFRIENDLY_NAME) |
  | final localised display name | `Character+0x290` std::wstring (written by refresh_name `sub_14011C560`) |
  | class | `Character+0x2B0` std::string: "Medic"/"Butcher"... for cats, "Boss"/"Enemy"/"Object" otherwise |
  | mana / max mana | `Character+0xD18` / `+0xD1C` i32 (10/30, 7/21, 5/100 Zaratana, 5/15 worm -- all match HUD/GON) |
  | sizeof(Character) | 0xEE0 (list stride) |
  | Passive stack count | `Passive+0x5C` (Trample 3, BoostHeals 2, Metal 1 = GON values) |
  | ability cost block | `Ability+0x40` move_points, `+0x44` act_points, `+0x48` health, `+0x4C` mana, `+0x50` charge, `+0x54` prime, `+0x58` coins, `+0x5C` durability, `+0x64` uses_per_fight |
  | StringsDatabase | global (data RVA 0x13C5530), `lea r15` in refresh_name; lookup `sub_1409616D0(db, wstring* out, const string* key, bool)`; language string at db+0x40 |
  | status icon table | `unordered_map<string, StatusIconInfo>` (data RVA 0x13C48B0), `lea rcx` in `get_status_icon` `sub_1404933E0`; node: key @+0x10, info @+0x30 = {frame_pos, frame_neg, ...}; frames are 1-based into ui.swf `StatusIcon` (1015 frames); -1 = no icon |
  No mid-battle cooldowns exist; abilities have mana cost, `charge` and `uses_per_fight`.
- 2026-10-02: assets: the UI is Flash. `resources.gpak` has `swfs/ui.swf` (FWS, 46 MB, 432 exports incl.
  StatusIcon, HealthIcon, ManaIcon, FontIcon_<stat>, TurnOrderPortrait*, CharacterTooltip) and
  `swfs/portraits.swf` (730 `<Movieclip>Portrait` clips). Wrote a SWF vector rasteriser (src/swf.cpp; proven
  first in Python, ~1-18 ms per clip in C++). Character portrait = GON `graphics.portrait` or
  `graphics.movieclip + "Portrait"`, keyed by `graphics.name` (== Character+0x248). Cats: their in-game
  portrait is a runtime composite of catparts + palette -> v0.2 uses `<Class>CatPortrait` instead.
- 2026-10-02: localisation: `data/text/combined.csv` has ru; we use the game's own StringsDatabase instead
  (language follows the game). id -> key maps from GON: items `name/desc`, abilities `meta.name/desc`
  (+variant_of), passives `name/desc`, classes `meta.name`, statuses keyword_tooltips.gon (+alias).
