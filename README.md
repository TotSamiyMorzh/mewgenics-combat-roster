# Combat Roster Panel — Mewgenics

A side panel for battles in **Mewgenics** that lists every unit on your side — cats, familiars, summons and
allied bosses — in the game's own look. *(Русская версия ниже.)*

- **Portraits:** each cat's real face, composed from the game's cat parts in its class colours. Other units
  use their own game portraits.
- **Bars and icons:** HP, shield and mana bars, plus the game's status icons with stack counts.
- **Tooltip** on hover: stats, equipment, passives, mutations, abilities (mana cost, charge, uses per fight)
  and effect descriptions, each with its game icon. The wheel scrolls it.
- **Board highlight:** hovering a row puts the game's own aim reticle on that unit's tile.
- **Panel controls:** drag it by its title, resize it by the bottom edge, collapse it into the screen edge
  with the tab, and set its opacity with the ◐ button. Everything is remembered.
- **Your language:** all names and descriptions come from the game's own text, so they follow the game's
  language setting.
- **No asset copies:** nothing from the game is copied or shipped. Art, fonts and text are read from your own
  `resources.gpak` at runtime.

## Requirements
- Mewgenics **1.1.21239** (Steam build 25143593), Windows. Other builds: the mod checks the game image and
  stays off instead of guessing.
- [Mewjector](https://github.com/githubuser508/mewjector) (DLL mod loader, v3 API) —
  also on Nexus: https://www.nexusmods.com/mewgenics/mods/218

## Install
1. Install Mewjector: put `version.dll` and `chainloader.ini` next to `Mewgenics.exe`.
2. Put `combat_roster.dll` into `<Mewgenics>\mods\` (create the folder if needed).
3. Start the game. The panel appears in battle.

## Uninstall
Delete `mods\combat_roster.dll` (and `mods\combat_roster.ini` with your panel settings).

## Troubleshooting
- Log: `<Mewgenics>\mod_logs\combat_roster.log` (and Mewjector's `chainloader.log`).
- "unsupported game build": the game was updated; the mod needs an update too.

## Compatibility
Single-player only; it changes nothing in the simulation (read-only, plus a cosmetic board marker). It works
alongside other Mewjector mods (hooks are chained through Mewjector).

## Building
Visual Studio Build Tools (MSVC, C++ workload), CMake, Ninja → `build.bat`. Offline tools in `tools/`
(`swf_test`, `cat_test`, `font_test`) render art from your own `resources.gpak` to PNG.

## Credits
- Loader: [Mewjector](https://github.com/githubuser508/mewjector) (MIT).
- Libraries: [Dear ImGui](https://github.com/ocornut/imgui) (MIT), [stb](https://github.com/nothings/stb)
  (public domain / MIT).
- Reverse-engineering references (facts learned from their public notes, no code copied):
  [mgmp](https://github.com/SanTertrust/mgmp), [mewgenics_analysis](https://github.com/p0lymeric/mewgenics_analysis),
  [mewgenics-cat-bridge](https://github.com/z3ndroot/mewgenics-cat-bridge).
- **AI disclosure:** this mod was built by Claude Code (Claude Opus 5.5) with the
  [universal-modder](https://github.com/rehan-remade/universal-modder) toolkit, guided and play-tested by a
  human. No AI-generated art: every visual is the game's own, rendered at runtime.

Mewgenics © Edmund McMillen & Tyler Glaiel. This is an unofficial fan mod.

## License
MIT for the code in this repository (see `LICENSE`). Third-party code keeps its own license
(`third_party/`).

---

# Combat Roster Panel — Mewgenics (RU)

Панель сбоку экрана в бою: все твои юниты (коты, фамильяры, призванные, союзные боссы) в стиле самой игры.

- **Портреты:** настоящие морды котов, собранные из частей игры в цветах класса. У остальных юнитов —
  их собственные портреты из игры.
- **Полоски и иконки:** HP, щит и мана, иконки статусов игры с числом стаков.
- **Тултип** при наведении: характеристики, снаряжение, пассивки, мутации, способности (мана, зарядка,
  «за бой») и описания эффектов, у каждого пункта своя иконка. Колесо мыши его прокручивает.
- **Подсветка:** при наведении на строку юнит подсвечивается на поле прицелом самой игры.
- **Панель:** перетаскивается за заголовок, тянется за нижний край, прячется за край экрана язычком,
  прозрачность настраивается кнопкой ◐. Всё сохраняется.
- **Язык:** названия и описания берутся из текстов игры и всегда на её языке.
- **Ничего не копирует:** графика, шрифты и тексты читаются из твоего `resources.gpak`.

**Установка:** поставь [Mewjector](https://www.nexusmods.com/mewgenics/mods/218) (`version.dll` +
`chainloader.ini` рядом с `Mewgenics.exe`), затем положи `combat_roster.dll` в папку `mods\` игры.
**Удаление:** удали `mods\combat_roster.dll`. Лог: `mod_logs\combat_roster.log`.
Нужна версия игры 1.1.21239 (Steam build 25143593). На другой версии мод сам выключится.

Мод сделан Claude Code (Claude Opus 5.5) с помощью universal-modder, протестирован человеком в игре.
