#!/bin/bash
# deploy.sh — Push local changes to the Raspberry Pi
#
# Usage: ./scripts/deploy.sh [environment]
#   environment: prod (default) | dev | playground
#
# TODO: Update PI_USER and PI_HOST with your Pi's details

PI_USER="pi"
PI_HOST="raspberrypi.local"   # or your Pi's IP address

ENV=${1:-prod}

case $ENV in
  prod)       PI_DIR="/home/pi/tide-clock" ;;
  dev)        PI_DIR="/home/pi/tide-clock-dev" ;;
  playground) PI_DIR="/home/pi/tide-clock-playground" ;;
  *)          echo "Unknown environment: $ENV"; exit 1 ;;
esac

echo "Deploying to $PI_USER@$PI_HOST:$PI_DIR..."
# TODO: fill in deploy steps once Pi is set up
# rsync -av --exclude='.git' . "$PI_USER@$PI_HOST:$PI_DIR"
echo "Done."
