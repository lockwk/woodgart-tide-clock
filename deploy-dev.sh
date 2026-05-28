#!/usr/bin/env bash
# deploy-dev.sh — pull latest dev, rebuild, and restart the dev service.
#
# Typical dev testing workflow:
#   sudo systemctl stop tide-clock-13in3     # pause production
#   ./deploy-dev.sh                          # build + run dev
#   journalctl -u tide-clock-13in3-dev -f   # watch logs
#   sudo systemctl stop tide-clock-13in3-dev # done testing
#   sudo systemctl start tide-clock-13in3    # resume production
#
# Usage:
#   ./deploy-dev.sh

set -e  # exit immediately on any error

REPO_DIR="/home/pi/tide-clock-dev"
SRC_DIR="$REPO_DIR/src/c"
SERVICE_NAME="tide-clock-13in3-dev"
SERVICE_SRC="$REPO_DIR/services/$SERVICE_NAME.service"
SERVICE_DEST="/etc/systemd/system/$SERVICE_NAME.service"

echo "==> Pulling latest code (dev)..."
cd "$REPO_DIR"
git pull

echo "==> Building C binary..."
cd "$SRC_DIR"
make clean && make RPI

echo "==> Installing dev service..."
sudo cp "$SERVICE_SRC" "$SERVICE_DEST"
sudo systemctl daemon-reload

# Note: dev service is NOT enabled (won't start on boot)
sudo systemctl restart "$SERVICE_NAME"

echo "==> Done. Current service status:"
sudo systemctl status "$SERVICE_NAME" --no-pager
