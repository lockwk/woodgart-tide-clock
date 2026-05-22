#!/usr/bin/env python3
"""
font_to_c.py — Convert Inter TTF files into C font sources for the tide clock.

Supports both separate weight files (Inter-SemiBold.ttf / Inter-Light.ttf) and
the Inter variable font (Inter-VariableFont_opsz,wght.ttf).  The script auto-
detects which is present and sets variation axes accordingly.

Usage:
    pip3 install Pillow
    python3 tools/font_to_c.py

Output files (src/c/fonts/):
    inter_sb_96.c / .h   — Inter SemiBold 96px  (large tide height number)
    inter_sb_56.c / .h   — Inter SemiBold 56px  (secondary numbers)
    inter_sb_28.c / .h   — Inter SemiBold 28px  (time strings, graph labels)
    inter_sb_20.c / .h   — Inter SemiBold 20px  (tide labels on graph)
    inter_lt_16.c / .h   — Inter Light 16px     (sub-labels: HIGH TIDE, etc.)
"""

import os
import sys
import math

try:
    from PIL import Image, ImageDraw, ImageFont
except ImportError:
    print("ERROR: Pillow not installed. Run:  pip3 install Pillow --break-system-packages")
    sys.exit(1)

# ---------------------------------------------------------------------------
# Paths
# ---------------------------------------------------------------------------
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT  = os.path.dirname(SCRIPT_DIR)
FONT_DIR   = os.path.join(REPO_ROOT, "assets", "fonts")
OUT_DIR    = os.path.join(REPO_ROOT, "src", "c", "fonts")

# Auto-detect variable font vs separate weight files
_VARIABLE = os.path.join(FONT_DIR, "Inter-VariableFont_opsz,wght.ttf")
_SEMI_TTF = os.path.join(FONT_DIR, "Inter-SemiBold.ttf")
_LITE_TTF = os.path.join(FONT_DIR, "Inter-Light.ttf")

VARIABLE_FONT = os.path.isfile(_VARIABLE)

# ---------------------------------------------------------------------------
# Character set — everything the display will ever show.
# Ordered so the index is stable across regenerations.
# ---------------------------------------------------------------------------
CHARSET = (
    " '\"0123456789:"
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "abcdefghijklmnopqrstuvwxyz"
    "."
)

# ---------------------------------------------------------------------------
# Font configurations: (output_name, ttf_path, point_size, wght, opsz)
#   wght — Inter weight axis: 300=Light, 600=SemiBold
#   opsz — Inter optical size axis: clamp to font's [14, 32] range
# ---------------------------------------------------------------------------
if VARIABLE_FONT:
    TTF = _VARIABLE
    FONTS = [
        ("inter_sb_96", TTF, 96, 600, 32),
        ("inter_sb_56", TTF, 56, 600, 32),
        ("inter_sb_28", TTF, 28, 600, 28),
        ("inter_sb_20", TTF, 20, 600, 20),
        ("inter_lt_16", TTF, 16, 300, 16),
    ]
else:
    FONTS = [
        ("inter_sb_96", _SEMI_TTF, 96, None, None),
        ("inter_sb_56", _SEMI_TTF, 56, None, None),
        ("inter_sb_28", _SEMI_TTF, 28, None, None),
        ("inter_sb_20", _SEMI_TTF, 20, None, None),
        ("inter_lt_16", _LITE_TTF, 16, None, None),
    ]


# ---------------------------------------------------------------------------
# Glyph rendering
# ---------------------------------------------------------------------------

def render_font(ttf_path, size, wght=None, opsz=None):
    """
    Render every character in CHARSET at the given pixel size.

    Returns:
        glyphs   — list of dicts, one per char in CHARSET order
        bitmaps  — flat bytearray of packed 4bpp data
        metrics  — dict with line_height and ascent
    """
    font = ImageFont.truetype(ttf_path, size)

    # Apply variable font axes if provided
    if wght is not None or opsz is not None:
        try:
            axes = font.get_variation_axes()
            values = []
            for ax in axes:
                name = ax['name'].decode() if isinstance(ax['name'], bytes) else ax['name']
                lo, hi = ax['minimum'], ax['maximum']
                if 'Optical' in name and opsz is not None:
                    values.append(max(lo, min(hi, opsz)))
                elif 'Weight' in name and wght is not None:
                    values.append(max(lo, min(hi, wght)))
                else:
                    values.append(ax['default'])
            font.set_variation_by_axes(values)
        except (OSError, AttributeError):
            pass  # not a variable font or Pillow too old

    ascent, descent = font.getmetrics()
    line_height = ascent + descent

    # Canvas height: full line plus generous padding for safety
    canvas_h = line_height + size
    canvas_w = size * 4          # wide enough for any glyph

    glyphs  = []
    bitmaps = bytearray()

    for char in CHARSET:
        adv_w = round(font.getlength(char))

        # Render onto a white canvas (white = 255, glyph = 0)
        canvas = Image.new("L", (canvas_w, canvas_h), 255)
        draw   = ImageDraw.Draw(canvas)
        # anchor='lt' places the top-left of the text block at (pad, 0)
        pad = size
        draw.text((pad, 0), char, font=font, fill=0)

        # Find tight bounding box of non-white pixels
        px = canvas.load()
        min_x, min_y = canvas_w, canvas_h
        max_x, max_y = -1, -1
        for y in range(canvas_h):
            for x in range(canvas_w):
                if px[x, y] < 255:
                    if x < min_x: min_x = x
                    if x > max_x: max_x = x
                    if y < min_y: min_y = y
                    if y > max_y: max_y = y

        # Space / empty glyphs — no bitmap data
        if min_x > max_x:
            glyphs.append({
                "char":        char,
                "width":       0,
                "height":      0,
                "ofs_x":       0,
                "ofs_y":       0,
                "adv_w":       adv_w,
                "data_offset": len(bitmaps),
            })
            continue

        box_w = max_x - min_x + 1
        box_h = max_y - min_y + 1

        # ofs_x: signed horizontal distance from pen position to bbox left edge
        ofs_x = min_x - pad

        # ofs_y: signed distance from baseline to bbox BOTTOM edge
        #   baseline is at canvas y = ascent
        #   positive  → bottom is above baseline  (normal for caps: ofs_y ≈ 0)
        #   negative  → bottom is below baseline  (descenders: g, p, y …)
        ofs_y = ascent - max_y

        # Pack pixels as 4bpp: high nibble = left pixel, low nibble = right
        data_start = len(bitmaps)
        for row in range(min_y, max_y + 1):
            col = min_x
            while col <= max_x:
                left_pix  = px[col, row]
                right_pix = px[col + 1, row] if (col + 1 <= max_x) else 255
                alpha_l = (255 - left_pix)  * 15 // 255
                alpha_r = (255 - right_pix) * 15 // 255
                bitmaps.append((alpha_l << 4) | alpha_r)
                col += 2

        glyphs.append({
            "char":        char,
            "width":       box_w,
            "height":      box_h,
            "ofs_x":       ofs_x,
            "ofs_y":       ofs_y,
            "adv_w":       adv_w,
            "data_offset": data_start,
        })

    metrics = {"line_height": line_height, "ascent": ascent}
    return glyphs, bitmaps, metrics


# ---------------------------------------------------------------------------
# C code emission
# ---------------------------------------------------------------------------

def _c_char_literal(ch):
    """Return a safe C char literal string for ch."""
    if ch == "'":  return r"'\''"
    if ch == "\\": return r"'\\'"
    if ch == '"':  return r"'\"'"
    return f"'{ch}'"


def emit_c_file(name, glyphs, bitmaps, metrics):
    lines = []
    lines.append("/* Auto-generated by tools/font_to_c.py — do not edit. */")
    lines.append('#include "inter_font.h"')
    lines.append(f'#include "{name}.h"')
    lines.append("")

    # ---- bitmap array ----
    lines.append(f"static const uint8_t {name}_bitmaps[] = {{")
    for i in range(0, len(bitmaps), 16):
        chunk = bitmaps[i : i + 16]
        row   = ", ".join(f"0x{b:02x}" for b in chunk)
        lines.append(f"    {row},")
    lines.append("};")
    lines.append("")

    # ---- glyph descriptor array ----
    lines.append(f"static const InterGlyph {name}_glyphs[] = {{")
    for g in glyphs:
        cl = _c_char_literal(g["char"])
        lines.append(
            f"    {{ .ch={cl:6s}, .width={g['width']:3d}, .height={g['height']:3d}, "
            f".ofs_x={g['ofs_x']:4d}, .ofs_y={g['ofs_y']:4d}, "
            f".adv_w={g['adv_w']:3d}, .data_offset={g['data_offset']:6d} }},"
            f"  /* {g['char']} */"
        )
    lines.append("};")
    lines.append("")

    # ---- charset string ----
    # Build a safe C string literal
    cs = CHARSET.replace("\\", "\\\\").replace('"', '\\"')
    lines.append(f'static const char {name}_charset[] = "{cs}";')
    lines.append("")

    # ---- InterFont struct ----
    lines.append(f"const InterFont {name} = {{")
    lines.append(f"    .bitmaps     = {name}_bitmaps,")
    lines.append(f"    .glyphs      = {name}_glyphs,")
    lines.append(f"    .charset     = {name}_charset,")
    lines.append(f"    .num_glyphs  = {len(glyphs)},")
    lines.append(f"    .line_height = {metrics['line_height']},")
    lines.append(f"    .ascent      = {metrics['ascent']},")
    lines.append("};")
    lines.append("")

    return "\n".join(lines)


def emit_h_file(name):
    guard = name.upper() + "_H"
    return (
        f"/* Auto-generated by tools/font_to_c.py — do not edit. */\n"
        f"#ifndef {guard}\n"
        f"#define {guard}\n"
        f'#include "inter_font.h"\n'
        f"extern const InterFont {name};\n"
        f"#endif /* {guard} */\n"
    )


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    # Verify all referenced TTF files exist
    seen = set()
    for row in FONTS:
        p = row[1]
        if p not in seen:
            seen.add(p)
            if not os.path.isfile(p):
                print(f"ERROR: font not found: {p}")
                sys.exit(1)
    if VARIABLE_FONT:
        print(f"Using variable font: {os.path.basename(_VARIABLE)}")
    else:
        print("Using separate weight files: Inter-SemiBold.ttf / Inter-Light.ttf")

    os.makedirs(OUT_DIR, exist_ok=True)

    for (name, ttf_path, size, wght, opsz) in FONTS:
        print(f"  {name}  ({size}px)  …", end=" ", flush=True)
        glyphs, bitmaps, metrics = render_font(ttf_path, size, wght, opsz)

        c_path = os.path.join(OUT_DIR, f"{name}.c")
        h_path = os.path.join(OUT_DIR, f"{name}.h")

        with open(c_path, "w") as f:
            f.write(emit_c_file(name, glyphs, bitmaps, metrics))
        with open(h_path, "w") as f:
            f.write(emit_h_file(name))

        total_bytes = len(bitmaps)
        print(f"done  ({total_bytes:,} bytes of bitmap data, {len(glyphs)} glyphs)")

    print(f"\nAll fonts written to {OUT_DIR}/")
    print("Next: cd src/c && gcc font_test.c inter_font.c fonts/inter_sb_96.c fonts/inter_lt_16.c -o font_test && ./font_test")


if __name__ == "__main__":
    main()
