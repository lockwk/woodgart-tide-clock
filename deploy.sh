#!/usr/bin/env bash
# deploy.sh — run this on the Pi to pull latest code, rebuild, and restart the service.
#
# Usage:
#   chmod +x deploy.sh   (first time only)
#   ./deploy.sh

set -e  # exit immediately on any error

REPO_DIR="/home/pi/tide-clock-dev"
SRC_DIR="$REPO_DIR/src/c"
SERVICE_NAME="tide-clock-13in3"
SERVICE_SRC="$REPO_DIR/services/$SERVICE_NAME.service"
SERVICE_DEST="/etc/systemd/system/$SERVICE_NAME.service"

echo "==> Pulling latest code..."
cd "$REPO_DIR"
git pull

echo "==> Building C binary..."
cd "$SRC_DIR"
make clean && make RPI

echo "==> Installing systemd service..."
sudo cp "$SERVICE_SRC" "$SERVICE_DEST"
sudo systemctl daemon-reload
sudo systemctl enable "$SERVICE_NAME"
sudo systemctl restart "$SERVICE_NAME"

echo "==> Done. Current service status:"
sudo systemctl status "$SERVICE_NAME" --no-pager
