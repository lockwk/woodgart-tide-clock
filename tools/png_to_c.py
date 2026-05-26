#!/usr/bin/env python3
"""
png_to_c.py — Convert RGBA PNG icons to 4-gray C header files
for the 13.3" e-Paper HAT (K) display.

Gray encoding matches Waveshare GUI_Paint 4-gray convention:
  GRAY1 = 0x03  (black,      luminosity   0– 63)
  GRAY2 = 0x02  (dark gray,  luminosity  64–127)
  GRAY3 = 0x01  (light gray, luminosity 128–191)
  GRAY4 = 0x00  (white,      luminosity 192–255)

Pixel packing: 4 pixels per byte, MSB-first:
  byte = (p0 << 6) | (p1 << 4) | (p2 << 2) | p3
  — matches Paint_DrawPoint() with Scale=4

Row width is padded to the next multiple of 4 pixels (1 byte boundary).
RGBA images are flattened onto a white background before conversion.

Usage:
  python3 tools/png_to_c.py              # convert all icons in ICON_MAP
  python3 tools/png_to_c.py rain.png     # convert a single named file
"""

import sys
import os
from pathlib import Path
from PIL import Image

# ---------------------------------------------------------------------------
# Source → C identifier → output header filename
# ---------------------------------------------------------------------------
ICON_MAP = [
    # Status bar icons
    ("rain.png",                       "icon_rain",                  "icon_rain.h"),
    ("wind lines.png",                 "icon_wind",                  "icon_wind.h"),
    ("waves.png",                      "icon_watertemp",             "icon_watertemp.h"),
    # Sun icons
    ("sunrise.png",                    "icon_sunrise",               "icon_sunrise.h"),
    ("sunset.png",                     "icon_sunset",                "icon_sunset.h"),
    # Moon phases (8)
    ("Moon Phase New.png",             "icon_moon_new",              "icon_moon_new.h"),
    ("Moon Phase Full Moon.png",       "icon_moon_full",             "icon_moon_full.h"),
    ("Moon Phase Waxing Crescent.png", "icon_moon_waxing_crescent",  "icon_moon_waxing_crescent.h"),
    ("Moon Phase Waxing Gibbous.png",  "icon_moon_waxing_gibbous",   "icon_moon_waxing_gibbous.h"),
    ("Moon Phase First Quarter.png",   "icon_moon_first_quarter",    "icon_moon_first_quarter.h"),
    ("Moon Phase Waning Gibbous.png",  "icon_moon_waning_gibbous",   "icon_moon_waning_gibbous.h"),
    ("Moon Phase Last Quarter.png",    "icon_moon_last_quarter",     "icon_moon_last_quarter.h"),
    ("Moon Phase Waning Crescent.png", "icon_moon_waning_crescent",  "icon_moon_waning_crescent.h"),
    # Tide cycle icons
    ("spring tide curve.png",          "icon_tide_spring",           "icon_tide_spring.h"),
    ("neap tide curve.png",            "icon_tide_neap",             "icon_tide_neap.h"),
    ("half tide curve.png",            "icon_tide_half",             "icon_tide_half.h"),
    # Tide direction (replaces programmatic arrow drawing)
    ("high tide.png",                  "icon_high_tide",             "icon_high_tide.h"),
    ("low tide.png",                   "icon_low_tide",              "icon_low_tide.h"),
]

# ---------------------------------------------------------------------------
# Conversion helpers
# ---------------------------------------------------------------------------

def lum_to_gray(lum: int) -> int:
    """Map 0–255 luminosity to a 2-bit Waveshare gray value (0x00–0x03)."""
    if lum < 64:
        return 0x03   # GRAY1 — black
    elif lum < 128:
        return 0x02   # GRAY2 — dark gray
    elif lum < 192:
        return 0x01   # GRAY3 — light gray
    else:
        return 0x00   # GRAY4 — white


def png_to_4gray_bytes(img_path: Path):
    """
    Load an RGBA PNG, flatten onto white, quantize to 4 gray levels,
    and pack 4 pixels per byte (MSB-first).

    Returns (width, height, data_bytes).
    Width in the returned metadata is the ORIGINAL image width.
    The data is row-padded to the next 4-pixel (1-byte) boundary.
    """
    img = Image.open(img_path).convert("RGBA")
    w, h = img.size

    # Flatten transparent pixels onto a white background
    bg = Image.new("RGBA", (w, h), (255, 255, 255, 255))
    bg.alpha_composite(img)
    gray = bg.convert("L")
    pixels = list(gray.getdata())

    # Row stride in bytes: ceiling of (w / 4)
    stride = (w + 3) // 4   # bytes per row (w_padded = stride * 4)

    out = []
    for row in range(h):
        for col_byte in range(stride):
            byte = 0
            for bit in range(4):
                col = col_byte * 4 + bit
                lum = pixels[row * w + col] if col < w else 255  # pad white
                gv  = lum_to_gray(lum)
                byte |= gv << (6 - bit * 2)
            out.append(byte)

    return w, h, bytes(out)


# ---------------------------------------------------------------------------
# C output generation
# ---------------------------------------------------------------------------

def make_header(c_name: str, w: int, h: int, data: bytes) -> str:
    guard = c_name.upper() + "_H"
    stride = (w + 3) // 4

    # 16 bytes per line in the array
    hex_lines = []
    for i in range(0, len(data), 16):
        chunk = data[i:i + 16]
        hex_lines.append("    " + ", ".join(f"0x{b:02X}" for b in chunk))
    array_body = ",\n".join(hex_lines)

    return (
        f"#ifndef {guard}\n"
        f"#define {guard}\n"
        f"\n"
        f'#include "icon_types.h"\n'
        f"\n"
        f"/* {c_name}: {w}x{h} px, 2-bit 4-gray, stride={stride} bytes/row, "
        f"{len(data)} bytes total */\n"
        f"/* Generated by tools/png_to_c.py — do not edit by hand            */\n"
        f"\n"
        f"static const uint8_t {c_name}_table[] = {{\n"
        f"{array_body}\n"
        f"}};\n"
        f"\n"
        f"const sICON {c_name} = {{\n"
        f"    .table  = {c_name}_table,\n"
        f"    .Width  = {w},\n"
        f"    .Height = {h},\n"
        f"}};\n"
        f"\n"
        f"#endif /* {guard} */\n"
    )


def make_icon_types_h() -> str:
    return (
        "#ifndef ICON_TYPES_H\n"
        "#define ICON_TYPES_H\n"
        "\n"
        "#include <stdint.h>\n"
        "\n"
        "/* Shared icon descriptor — used by all icon_*.h headers and layout.c */\n"
        "typedef struct {\n"
        "    const uint8_t *table;  /* packed 2-bit 4-gray bitmap, 4px/byte MSB-first */\n"
        "    uint16_t       Width;  /* original pixel width (unpacked)                 */\n"
        "    uint16_t       Height; /* pixel height                                    */\n"
        "} sICON;\n"
        "\n"
        "#endif /* ICON_TYPES_H */\n"
    )


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def convert_one(src: Path, c_name: str, h_name: str, out_dir: Path, repo_root: Path):
    if not src.exists():
        print(f"  SKIP   {src.name} (not found)")
        return False
    try:
        w, h, data = png_to_4gray_bytes(src)
        header = make_header(c_name, w, h, data)
        out_path = out_dir / h_name
        out_path.write_text(header, encoding="utf-8")
        rel = out_path.relative_to(repo_root)
        stride = (w + 3) // 4
        print(f"  wrote  {rel}  ({w}x{h}, stride={stride}, {len(data)} bytes)")
        return True
    except Exception as exc:
        print(f"  ERROR  {src.name}: {exc}")
        return False


def main():
    script_dir = Path(__file__).resolve().parent
    repo_root  = script_dir.parent
    src_dir    = repo_root / "assets" / "icons" / "960x680_light"
    out_dir    = repo_root / "src" / "c" / "icons"
    out_dir.mkdir(parents=True, exist_ok=True)

    # Always write the shared types header first
    types_path = out_dir / "icon_types.h"
    types_path.write_text(make_icon_types_h(), encoding="utf-8")
    print(f"  wrote  {types_path.relative_to(repo_root)}")

    # Single-file mode: python3 png_to_c.py rain.png
    if len(sys.argv) > 1:
        target_name = sys.argv[1]
        match = next(
            ((png, cname, hname) for png, cname, hname in ICON_MAP
             if png == target_name or cname == target_name),
            None
        )
        if match is None:
            print(f"ERROR: '{target_name}' not found in ICON_MAP")
            sys.exit(1)
        png_name, c_name, h_name = match
        ok = convert_one(src_dir / png_name, c_name, h_name, out_dir, repo_root)
        sys.exit(0 if ok else 1)

    # Batch mode: convert all icons in ICON_MAP
    ok_count = fail_count = 0
    for png_name, c_name, h_name in ICON_MAP:
        if convert_one(src_dir / png_name, c_name, h_name, out_dir, repo_root):
            ok_count += 1
        else:
            fail_count += 1

    print(f"\nDone — {ok_count} icons written, {fail_count} skipped/failed")
    if fail_count:
        sys.exit(1)


if __name__ == "__main__":
    main()
