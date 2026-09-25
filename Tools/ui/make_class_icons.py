"""Make the class icon kit: one coin-medallion icon per class.

    python Tools/ui/make_class_icons.py

Writes the vector sources to ``Docs/branding/class-icons/<class>.svg`` and, when
CairoSVG is installed (``pip install cairosvg``), the textures Unreal imports to
``Import/UI/ClassIcons/T_ClassIcon_<Class>.png`` (256 x 256, transparent).
Import those with valhalla_tools/import_ui_icons.py like the other UI icons.

The kit (Kevin, 2026-09-25): a gold-rimmed coin like the Valhalla logo, a face
colour per class and a gold symbol. Used on character select (about 52 px) and
in the party pane (about 24 px); the symbols were checked down to 18 px.
Cleric uses a dark-gold symbol because its coin is ivory.
"""

import os

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
SVG_DIR = os.path.join(ROOT, "Docs", "branding", "class-icons")
PNG_DIR = os.path.join(ROOT, "Import", "UI", "ClassIcons")
PNG_SIZE = 256

# Symbols, drawn as strokes in a 64 x 64 box. ("path", d) or ("circle", (x, y, r)).
SYMBOLS = {
    "warrior": [  # upright sword
        ("path", "M32 5 L37 11.5 V40 H27 V11.5 Z"), ("path", "M32 14 V36"), ("path", "M19 41 H45"),
        ("path", "M32 41 V52"), ("circle", (32, 56, 3)),
    ],
    "cleric": [  # templar cross (cross pattee)
        ("path", "M25.5 6 H38.5 L34.5 27.5 L58 25.5 V38.5 L34.5 36.5 L38.5 58 H25.5 L29.5 36.5 L6 38.5 V25.5 L29.5 27.5 Z"),
    ],
    "ranger": [  # bow and arrow
        ("path", "M22 7 C47 17 47 47 22 57"), ("path", "M22 7 L22 57"), ("path", "M9 32 H52"),
        ("path", "M45 25.5 L53 32 L45 38.5"), ("path", "M9 32 L5 26.5 M9 32 L5 37.5 M15 32 L11 26.5 M15 32 L11 37.5"),
    ],
    "rogue": [  # dagger, point down
        ("path", "M32 60 L26 30 H38 Z"), ("path", "M32 35 V52"), ("path", "M19 26.5 Q22 30 26 29.5 H38 Q42 30 45 26.5"),
        ("path", "M32 29 V16"), ("path", "M29 20.5 H35 M29 24.5 H35"), ("circle", (32, 11, 3.5)),
    ],
    "shaman": [  # dreamcatcher
        ("circle", (32, 22, 16)), ("path", "M32 12 L41 18 L39.5 28 L32 32 L24.5 28 L23 18 Z"),
        ("path", "M32 6 L32 12 M45.9 14 L41 18 M45.9 30 L39.5 28 M32 38 L32 32 M18.1 30 L24.5 28 M18.1 14 L23 18"),
        ("circle", (32, 22, 2.3)), ("path", "M21 34 L18 47 M32 38 V51 M43 34 L46 47"),
        ("path", "M18 47 C14 51 15 57 18 60 C21 57 22 51 18 47 Z"),
        ("path", "M32 51 C28 55 29 61 32 63 C35 61 36 55 32 51 Z"),
        ("path", "M46 47 C42 51 43 57 46 60 C49 57 50 51 46 47 Z"),
    ],
    "wizard": [  # open spellbook with a star rising
        ("path", "M8 44 C18 39 27 39 32 44 C37 39 46 39 56 44 V54 C46 49 37 49 32 54 C27 49 18 49 8 54 Z"),
        ("path", "M32 44 V54"), ("path", "M32 6 Q33.5 17 44 19 Q33.5 21 32 32 Q30.5 21 20 19 Q30.5 17 32 6 Z"),
    ],
}

# Coin face: (highlight, base, shadow), symbol colour, symbol keyline.
COLOURS = {
    "warrior": (("#b8463a", "#8e2a22", "#5a1712"), "#f0d58c", "#2e0b08"),  # crimson
    "cleric":  (("#fbf6e9", "#e6dcc2", "#b9aa84"), "#6b4c14", "#fbf6e9"),  # ivory
    "ranger":  (("#4a8055", "#2f5d3a", "#1b3a22"), "#f0d58c", "#0e2213"),  # forest green
    "rogue":   (("#4a4a57", "#2e2e38", "#18181f"), "#f0d58c", "#09090d"),  # charcoal
    "shaman":  (("#2f8f89", "#1d6a66", "#10403d"), "#f0d58c", "#08262a"),  # teal
    "wizard":  (("#7b4bb3", "#5a2f8a", "#351a55"), "#f0d58c", "#1c0c30"),  # violet
}

GOLD_RIM = ("#f0d58c", "#b08d3c", "#7d6124")


def _symbol(cls, stroke, width):
    out = []
    for kind, data in SYMBOLS[cls]:
        if kind == "path":
            out.append('<path d="%s" fill="none" stroke="%s" stroke-width="%s" stroke-linecap="round" stroke-linejoin="round"/>'
                       % (data, stroke, width))
        else:
            x, y, r = data
            out.append('<circle cx="%s" cy="%s" r="%s" fill="none" stroke="%s" stroke-width="%s"/>' % (x, y, r, stroke, width))
    return '<g transform="translate(32 32) scale(0.7) translate(-32 -32)">' + "".join(out) + "</g>"


def icon_svg(cls):
    (hi, base, lo), ink, keyline = COLOURS[cls]
    defs = ('<radialGradient id="face" cx="0.4" cy="0.35" r="0.7"><stop offset="0" stop-color="%s"/>'
            '<stop offset="0.6" stop-color="%s"/><stop offset="1" stop-color="%s"/></radialGradient>'
            '<linearGradient id="rim" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="%s"/>'
            '<stop offset="0.5" stop-color="%s"/><stop offset="1" stop-color="%s"/></linearGradient>') % ((hi, base, lo) + GOLD_RIM)
    body = ('<circle cx="32" cy="32" r="31" fill="#0c0a07"/><circle cx="32" cy="32" r="30" fill="url(#rim)"/>'
            '<circle cx="32" cy="32" r="25.5" fill="url(#face)"/>'
            '<circle cx="32" cy="32" r="25.5" fill="none" stroke="#5c4a24" stroke-width="1"/>'
            + _symbol(cls, keyline, 6.2) + _symbol(cls, ink, 4.0))
    return ('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 64 64" width="64" height="64">'
            "<defs>%s</defs>%s</svg>") % (defs, body)


def main():
    os.makedirs(SVG_DIR, exist_ok=True)
    try:
        import cairosvg
    except ImportError:
        cairosvg = None
        print("CairoSVG not installed: writing the SVGs only (pip install cairosvg for the PNGs).")
    if cairosvg:
        os.makedirs(PNG_DIR, exist_ok=True)
    for cls in SYMBOLS:
        svg = icon_svg(cls)
        with open(os.path.join(SVG_DIR, cls + ".svg"), "w", encoding="utf-8") as f:
            f.write(svg)
        if cairosvg:
            cairosvg.svg2png(bytestring=svg.encode("utf-8"), write_to=os.path.join(PNG_DIR, "T_ClassIcon_%s.png" % cls.capitalize()),
                             output_width=PNG_SIZE, output_height=PNG_SIZE)
    print("class icons written: %d" % len(SYMBOLS))


if __name__ == "__main__":
    main()
