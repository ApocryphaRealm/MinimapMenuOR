# Minimap Menu (Oblivion Remastered)

A minimap in Oblivion Remastered's own HUD, configured on its own page in the Apocrypha Menu Framework.

- **Positioning:** Dragon's Eye Minimap's. Pick a corner, give each corner its own nudge, and set the size, which is
  capped at a quarter of the screen. The map can be square or a circle.
- **Location banner:** the game's own banner follows the minimap's corner.
- **Map:** the game's own parchment and map icons. Markers come from the HUD compass: locations, doors, quest targets and
  (optionally) enemies.
- **Keys:** K taps to hide and holds to pan; L taps between two zoom levels. The right stick click does the same on a
  controller. Keys are rebindable on the page.

**Requires:** OBSE64 and the Apocrypha Menu Framework (Oblivion Remastered) 1.0.4 or later.

Status: test builds. See `CHANGELOG.md`.

## Building

- **Needs:** Visual Studio 2022 or later (MSVC, C++23) and [xmake](https://xmake.io).
- **CommonLibOB64:** the submodule in `lib/commonlibob64`. Run `git submodule update --init --recursive`.
- **Build:**

```
xmake f -c -p windows -a x64 -m releasedbg --toolchain=msvc
xmake
python tools/gen.py
```

`tools/gen.py` writes the shipped INI and the English translation file from the source.

## Licence

GPL-3.0-or-later.
