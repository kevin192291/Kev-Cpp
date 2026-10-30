#!/usr/bin/env python3
"""Cuts the toolbar/panel icons out of icon-sprite-sheet.png and draws the few the sheet lacks.

Writes data/icons/hicolor/<size>x<size>/actions/kevc-*.png and data/kevcpp-ui-icons.gresource.xml.
The output is committed, so building Kev-C++ doesn't need Python or Pillow; run this again only
after changing the sheet or this script:

    python3 data/art/make_icons.py [--preview preview.png]
"""
import argparse
import math
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

HERE = Path(__file__).resolve().parent
DATA = HERE.parent
SHEET = HERE / "icon-sprite-sheet.png"
# Language logos for the New Project dialog, from Wikimedia Commons (see docs/ICONS.md).
LOGOS = {"cpp-logo": "cpp.png", "c-logo": "c.png", "ts-logo": "typescript.png"}
SIZES = (16, 24, 32, 48)
GRID = 8
# Icons are cut from squares at least this big (sheet pixels), so small ones such as Stop keep
# their smaller look next to Debug instead of being blown up to fill the button.
MIN_SIDE = 116
BOLD_FONT = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"

# Sheet order, left to right and top to bottom (see the table in docs/ICONS.md).
SHEET_NAMES = [
    "integer-variable", "float-variable", "string-variable", "bool-variable",
    "char-variable", "array-variable", "pointer-variable", "const-variable",
    "global-variable", "local-variable", "parameter", "member-variable",
    "static-variable", "enum", "struct", "class",
    "public-member", "protected-member", "private-member", "function",
    "method", "constructor", "namespace", "header-file",
    "cpp-file", "c-file", "resource-file", "project",
    "workspace", "folder", "folder-open", "document",
    "new-file", "open", "save", "save-all", "print", "cut", "copy", "paste",
    "undo", "redo", "find", "find-in-files", "replace", "settings", "help", "about",
    "compile", "build-project", "rebuild", "clean", "run", "stop", "pause", "debug",
    "breakpoint", "step-into", "step-over", "step-out", "watch", "call-stack", "memory", "error-list",
]

OUTLINE = (24, 24, 32, 255)


def sheet_icon(sheet, name):
    """The named icon, cropped to its pixels, without the semi-transparent halo the sheet has."""
    n = SHEET_NAMES.index(name)
    w = sheet.width / GRID
    r, c = divmod(n, GRID)
    tile = sheet.crop((round(c * w), round(r * w), round((c + 1) * w), round((r + 1) * w)))
    alpha = tile.getchannel("A").point(lambda a: 255 if a >= 128 else 0)
    tile.putalpha(alpha)
    return tile.crop(alpha.getbbox())


def square(img, min_side=MIN_SIDE, pad=4):
    side = max(img.width, img.height, min_side) + 2 * pad
    out = Image.new("RGBA", (side, side), (0, 0, 0, 0))
    out.alpha_composite(img, ((side - img.width) // 2, (side - img.height) // 2))
    return out


def scaled(img, size):
    # Premultiplied, so transparent pixels don't bleed their (black) colour into the edges.
    return img.convert("RGBa").resize((size, size), Image.LANCZOS).convert("RGBA")


def fit(img, box):
    """`img` scaled to fit a box of `box` pixels, keeping its aspect ratio."""
    k = box / max(img.width, img.height)
    return img.convert("RGBa").resize((max(1, round(img.width * k)), max(1, round(img.height * k))),
                                      Image.LANCZOS).convert("RGBA")


def with_badge(base, badge, fraction, corner="br"):
    """`base` with `badge` scaled to `fraction` of its side, in a corner."""
    out = square(base)
    b = fit(badge, round(out.width * fraction))
    x = out.width - b.width if corner[1] == "r" else 0
    y = out.height - b.height if corner[0] == "b" else 0
    out.alpha_composite(b, (x, y))
    return out


def pixels_where(img, test, margin):
    """Crop of `img` around the pixels matching `test`, widened by `margin` for their outline."""
    px = img.load()
    xs, ys = [], []
    for y in range(img.height):
        for x in range(img.width):
            if px[x, y][3] and test(*px[x, y][:3]):
                xs.append(x)
                ys.append(y)
    box = (max(0, min(xs) - margin), max(0, min(ys) - margin),
           min(img.width, max(xs) + margin + 1), min(img.height, max(ys) + margin + 1))
    return img.crop(box)


def circle_badge(img, test, margin=7):
    """A round badge (e.g. the red X on the error-list page) lifted off its background."""
    crop = pixels_where(img, test, margin)
    mask = Image.new("L", crop.size, 0)
    ImageDraw.Draw(mask).ellipse((0, 0, crop.width - 1, crop.height - 1), fill=255)
    out = Image.new("RGBA", crop.size, (0, 0, 0, 0))
    out.paste(crop, (0, 0), mask)
    return out


def is_red(r, g, b):
    return r > 150 and g < 90 and b < 90


def canvas():
    img = Image.new("RGBA", (128, 128), (0, 0, 0, 0))
    return img, ImageDraw.Draw(img)


def draw_warning():
    img, d = canvas()
    tri = [(64, 8), (122, 116), (6, 116)]
    d.polygon(tri, fill=(250, 204, 20, 255))
    d.polygon([(64, 22), (106, 104), (64, 104)], fill=(228, 172, 0, 255))  # shaded right half
    d.line(tri + [tri[0]], fill=OUTLINE, width=8, joint="curve")
    d.rounded_rectangle((56, 40, 72, 84), radius=5, fill=OUTLINE)
    d.ellipse((55, 90, 73, 108), fill=OUTLINE)
    return img


def draw_globe():
    img, d = canvas()
    d.ellipse((8, 8, 120, 120), fill=(40, 96, 220, 255))
    d.pieslice((8, 8, 120, 120), 20, 200, fill=(28, 72, 190, 255))  # darker lower half
    green, edge = (52, 170, 72, 255), (20, 100, 40, 255)
    for shape in ([(30, 30), (58, 22), (66, 38), (54, 56), (60, 74), (48, 96), (38, 80), (26, 58)],
                  [(74, 24), (98, 30), (110, 52), (96, 60), (84, 52), (76, 40)],
                  [(80, 70), (102, 72), (98, 96), (84, 104), (76, 88)]):
        d.polygon(shape, fill=green, outline=edge, width=3)
    d.arc((20, 20, 108, 108), 200, 250, fill=(190, 220, 255, 255), width=6)  # highlight
    d.ellipse((8, 8, 120, 120), outline=OUTLINE, width=8)
    return img


def draw_terminal():
    img, d = canvas()
    d.rounded_rectangle((6, 14, 122, 114), radius=6, fill=(196, 196, 204, 255), outline=OUTLINE, width=7)
    d.rectangle((12, 20, 116, 36), fill=(32, 80, 200, 255))  # title bar, as on the sheet's windows
    d.rectangle((16, 42, 112, 106), fill=(16, 16, 20, 255))
    d.line([(26, 56), (46, 72), (26, 88)], fill=(80, 220, 90, 255), width=8, joint="curve")
    d.line([(54, 92), (84, 92)], fill=(80, 220, 90, 255), width=8)
    return img


def draw_exec_arrow():
    img, d = canvas()
    arrow = [(10, 44), (64, 44), (64, 16), (118, 64), (64, 112), (64, 84), (10, 84)]
    d.polygon(arrow, fill=(48, 182, 72, 255))
    d.polygon([(14, 48), (68, 48), (68, 26), (106, 60), (14, 60)], fill=(110, 220, 120, 255))  # highlight
    d.line(arrow + [arrow[0]], fill=OUTLINE, width=8, joint="curve")
    return img


def relabel(page, text, fill, stroke):
    """A source-file page (the C++ one) with its label replaced, for JS and TS files."""
    page = page.copy()
    px = page.load()
    is_letter = lambda r, g, b: b > 120 and b > r + 50
    xs =[x for y in range(page.height) for x in range(page.width) if px[x, y][3] and is_letter(*px[x, y][:3])]
    ys = [y for y in range(page.height) for x in range(page.width) if px[x, y][3] and is_letter(*px[x, y][:3])]
    box = (min(xs) - 6, min(ys) - 6, max(xs) + 6, max(ys) + 6)
    paper = px[box[0] - 4, (box[1] + box[3]) // 2]
    ImageDraw.Draw(page).rectangle(box, fill=paper)
    font = ImageFont.truetype(BOLD_FONT, 40)
    d = ImageDraw.Draw(page)
    w = d.textlength(text, font=font)
    cx, cy = (box[0] + box[2]) / 2, (box[1] + box[3]) / 2
    d.text((cx - w / 2, cy), text, font=font, fill=fill, stroke_width=3, stroke_fill=stroke, anchor="lm")
    return page


def library(pages, gear, gears):
    """`pages` with gears on it, like the Windows DLL icon: `gears` is a list of
    (fraction of the side, centre x, centre y), the centre as a fraction of the side too."""
    out = square(pages)
    for fraction, cx, cy in gears:
        g = fit(gear, round(out.width * fraction))
        out.alpha_composite(g, (round(out.width * cx - g.width / 2), round(out.height * cy - g.height / 2)))
    return out


def logo(path):
    """A logo with the same margin as the sheet's icons, which keep a few pixels clear of the edge."""
    img = Image.open(path).convert("RGBA")
    return square(img, min_side=0, pad=round(max(img.size) * 0.03))


def build(sheet):
    icons = {name: square(sheet_icon(sheet, name)) for name in SHEET_NAMES}
    get = lambda name: sheet_icon(sheet, name)

    play = get("run")
    icons["compile-run"] = with_badge(get("compile"), play, 0.55)
    pause = get("pause")
    bar = pause.crop((0, 0, round(pause.width * 0.42), pause.height))
    cont = Image.new("RGBA", (MIN_SIDE, MIN_SIDE), (0, 0, 0, 0))
    b = fit(bar, round(MIN_SIDE * 0.72))
    p = fit(play, round(MIN_SIDE * 0.72))
    x0 = (MIN_SIDE - (b.width + 6 + p.width)) // 2
    cont.alpha_composite(b, (x0, (MIN_SIDE - b.height) // 2))
    cont.alpha_composite(p, (x0 + b.width + 6, (MIN_SIDE - p.height) // 2))
    icons["continue"] = square(cont)

    sparkle_page = get("new-file")
    sparkle = pixels_where(sparkle_page, lambda r, g, b: r > 180 and g > 140 and b < 110, 7)
    keep = sparkle.copy()
    kp = keep.load()
    for y in range(keep.height):
        for x in range(keep.width):
            r, g, b, a = kp[x, y]
            if min(r, g, b) > 150 and max(r, g, b) - min(r, g, b) < 50:  # the page, not the sparkle
                kp[x, y] = (0, 0, 0, 0)
    icons["new-project"] = with_badge(get("project"), keep, 0.5, corner="tr")

    icons["error"] = square(circle_badge(get("error-list"), is_red), min_side=0)
    icons["breakpoint-dot"] = square(circle_badge(get("breakpoint"), is_red), min_side=0)
    icons["warning"] = square(draw_warning(), min_side=0)
    icons["globe"] = square(draw_globe(), min_side=0)
    icons["terminal"] = square(draw_terminal(), min_side=0)
    icons["exec-arrow"] = square(draw_exec_arrow(), min_side=0)

    cpp = get("cpp-file")
    icons["ts-file"] = square(relabel(cpp, "TS", (32, 80, 210, 255), (16, 32, 96, 255)))
    icons["js-file"] = square(relabel(cpp, "JS", (236, 196, 20, 255), OUTLINE))

    gear = get("settings")
    icons["shared-library"] = library(get("document"), gear, [(0.42, 0.40, 0.40), (0.56, 0.62, 0.68)])
    icons["static-library"] = library(get("copy"), gear, [(0.56, 0.68, 0.70)])
    for name, file in LOGOS.items():
        icons[name] = logo(HERE / "logos" / file)
    return icons


def write(icons):
    files = []
    for size in SIZES:
        folder = DATA / "icons" / "hicolor" / f"{size}x{size}" / "actions"
        folder.mkdir(parents=True, exist_ok=True)
        for old in folder.glob("kevc-*.png"):
            old.unlink()
        for name, img in sorted(icons.items()):
            path = folder / f"kevc-{name}.png"
            scaled(img, size).save(path, optimize=True)
            files.append(path.relative_to(DATA).as_posix())
    xml = ['<?xml version="1.0" encoding="UTF-8"?>',
           "<!-- Generated by data/art/make_icons.py; do not edit. -->",
           "<gresources>", '  <gresource prefix="/org/kevcpp/KevCpp">']
    xml += [f"    <file>{f}</file>" for f in files]
    xml += ["  </gresource>", "</gresources>", ""]
    (DATA / "kevcpp-ui-icons.gresource.xml").write_text("\n".join(xml))
    return len(icons)


def preview(icons, path):
    names = sorted(icons)
    cols = 8
    cell_w, cell_h = 220, 64
    rows = math.ceil(len(names) / cols)
    out = Image.new("RGBA", (cols * cell_w, rows * cell_h), (246, 245, 244, 255))
    d = ImageDraw.Draw(out)
    font = ImageFont.truetype(BOLD_FONT, 10)
    for i, name in enumerate(names):
        x, y = (i % cols) * cell_w, (i // cols) * cell_h
        d.rectangle((x + 100, y, x + cell_w - 1, y + 52), fill=(40, 40, 46, 255))  # dark-theme check
        ox = x + 4
        for size in (16, 24, 48):
            out.alpha_composite(scaled(icons[name], size), (ox, y + 2))
            ox += size + 4
        out.alpha_composite(scaled(icons[name], 24), (x + 110, y + 14))
        out.alpha_composite(scaled(icons[name], 16), (x + 140, y + 18))
        d.text((x + 4, y + 52), name, font=font, fill=(0, 0, 0, 255))
    out.save(path)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--preview", help="also write a contact sheet of every icon to this PNG")
    args = parser.parse_args()
    icons = build(Image.open(SHEET).convert("RGBA"))
    print(f"wrote {write(icons)} icons in sizes {', '.join(map(str, SIZES))}")
    if args.preview:
        preview(icons, args.preview)


if __name__ == "__main__":
    main()
