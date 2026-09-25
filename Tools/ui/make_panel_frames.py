"""Make the HUD's panel-frame textures, one per border thickness.

    python Tools/ui/make_panel_frames.py

Slate draws a box brush's border at (margin x the texture's own pixel size):
one texel per Slate unit, whatever the brush's Image Size says. So the 24 px
trim of ``Import/UI/Frames/T_UI_Panel.png`` (256 x 256) always draws 24 px
wide. To draw it N px wide the texture itself has to be smaller: this writes
``T_UI_PanelFrame_NN.png`` (N = 1 .. 12) beside it, T_UI_Panel scaled to
round(256 x N / 24) px, so its trim is N texels. The HUD uses the one for the
panel border thickness in force (UValhallaGameHUDWidget::PanelBorderThickness
or the player's Options -> Layout "Border thickness") with a margin of N / size.

Import them with valhalla_tools/import_ui_icons.py (frame art: bilinear, no mips).
Needs Pillow.
"""

import os

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
FRAMES = os.path.normpath(os.path.join(HERE, "..", "..", "Import", "UI", "Frames"))
SOURCE = os.path.join(FRAMES, "T_UI_Panel.png")
TRIM_PX = 24          # T_UI_Panel's trim: outer frame, inner line, a little fill
THICKNESSES = range(1, 13)


def size_for(px, source_size=256):
    return max(3, int(round(source_size * px / float(TRIM_PX))))


def main():
    source = Image.open(SOURCE).convert("RGBA")
    # Premultiplied while resampling, so the transparent corner texels do not bleed dark fringes.
    premultiplied = source.convert("RGBa")
    for px in THICKNESSES:
        n = size_for(px, source.size[0])
        frame = premultiplied.resize((n, n), Image.LANCZOS).convert("RGBA")
        out = os.path.join(FRAMES, "T_UI_PanelFrame_{:02d}.png".format(px))
        frame.save(out, optimize=True)
        print("{}: {} x {}".format(os.path.basename(out), n, n))


if __name__ == "__main__":
    main()
