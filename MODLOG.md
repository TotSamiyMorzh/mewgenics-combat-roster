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
