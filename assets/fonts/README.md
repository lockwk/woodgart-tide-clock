# _fonts/

Drop Inter TTF files here to use the correct Figma font in `generate_bitmap.py`.

The script needs two files:

| File | Weight | Used for |
|------|--------|----------|
| `Inter-Light.ttf` | Light (300) | Data values — times, heights, temperature |
| `Inter-SemiBold.ttf` | Semi Bold (600) | Labels — SUNRISE, HIGH TIDE, letter-spaced caps |

## Where to get them

**Option A — Download directly (one-time):**
```
https://github.com/rsms/inter/raw/master/docs/font-files/Inter-Light.ttf
https://github.com/rsms/inter/raw/master/docs/font-files/Inter-SemiBold.ttf
```

**Option B — Install the full Inter family:**
```bash
# macOS (Homebrew)
brew install --cask font-inter

# Raspberry Pi / Debian
sudo apt install fonts-inter        # if available
# or download and copy to /usr/local/share/fonts/
```

**Option C — Raspberry Pi auto-download:**
On first run, `generate_bitmap.py` will try to download both files automatically
and cache them here. This works as long as the Pi has internet access.

## Font search order

The script checks these locations in order and uses the first match found:

1. `_fonts/` ← this folder (highest priority)
2. `~/Library/Fonts/` (macOS user fonts)
3. `/Library/Fonts/` (macOS system fonts)
4. `/usr/share/fonts/truetype/inter/` (Linux apt install)
5. Auto-download from GitHub → saved here
6. DejaVu Sans fallback (already on the Pi)
