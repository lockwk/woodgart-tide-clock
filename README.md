# Woodgart Tide Clock

A Raspberry Pi-powered tide clock using Waveshare e-paper displays.
Renders tide data, moon phase, weather, and sea conditions on a 6-color ACeP e-paper panel.

## Supported Displays
- **7.3inch e-Paper HAT (E)** — 800×480, 6-color
- **13.3inch e-Paper HAT+ (E)** — 1600×1200, 6-color

## Configuration
Edit `config.py` to set your display and theme before running.

## Project Structure
```
woodgart-tide-clock/
├── config.py              # Display and theme settings
├── src/                   # Python source code
│   ├── tide_display.py    # Main renderer
│   ├── displays/          # Per-display driver configs
│   └── ...
├── templates/             # HTML display files
│   ├── 7in3/              # 7.3" display templates
│   └── 13in3/             # 13.3" display templates
├── assets/                # Fonts and icons
├── services/              # systemd service file
├── scripts/               # Deployment scripts
├── docs/                  # Reference docs
└── playground/            # Safe experimentation area
```

## Data Sources
See `docs/data_sources.md` for API details.
