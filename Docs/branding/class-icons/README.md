# Class icon kit

One icon per class, used on character select (about 52 px) and in the in-game party pane (about 24 px). Chosen by Kevin on 2026-09-25 (design canvas "Valhalla login & character select", board "Icons 1c · Class icon set").

A gold-rimmed coin like the Valhalla logo, with a face colour per class and a gold symbol. The symbols were checked down to 18 px.

| Class | Coin face | Symbol |
|---|---|---|
| Warrior | Crimson `#8e2a22` | Upright sword |
| Cleric | Ivory `#e6dcc2` (dark-gold symbol `#6b4c14`) | Templar cross (cross pattée) |
| Ranger | Forest green `#2f5d3a` | Bow and arrow |
| Rogue | Charcoal `#2e2e38` | Dagger, point down |
| Shaman | Teal `#1d6a66` | Dreamcatcher |
| Wizard | Violet `#5a2f8a` | Open spellbook with a star rising |

Rim: gold gradient `#f0d58c` → `#b08d3c` → `#7d6124`. Symbol: `#f0d58c` (Cleric `#6b4c14`) over a dark keyline in the coin's shadow colour.

## Files

- `Docs/branding/class-icons/<class>.svg`: vector sources (64 × 64 viewBox).
- `Import/UI/ClassIcons/T_ClassIcon_<Class>.png`: 256 × 256 textures for Unreal. Import them with `valhalla_tools/import_ui_icons.py` like the other UI icons.
- `Tools/ui/make_class_icons.py`: regenerates both. The symbols and colours live in that script; edit there, not in the SVGs. The PNGs need CairoSVG (`pip install cairosvg`).

## Adding a class

Add a symbol and a coin colour to `SYMBOLS` and `COLOURS` in the script, run it, and check the new icon at 24 px next to the others. The face colour should differ from the other six in lightness as well as hue, so the party pane stays readable for colour-blind players.

## The screens it belongs to: "Gilded Hall"

The login and character select screens use the Gilded Hall look (design canvas, Option A):

- Dark bronze-black background `#0c0a07` → `#2a2112`, panels `#15110a` with a 2 px gold border `#b08d3c` and a thin outer line `#5c4a24`, matching `Import/UI/Frames/T_UI_Panel.png`.
- Headings and buttons in Cinzel; body text in EB Garamond (both SIL Open Font License, free on Google Fonts).
- Primary button: gold `#b08d3c` with dark text `#1a1305`. Text on dark: `#eadfc4`; secondary `#c9b88f`.
