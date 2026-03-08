#!/bin/sh
set -e

# Same bus ID as in setup script
KBD_BUS_ID="1-1"

echo "[cleanup] Removing keylogger module..."
sudo rmmod keylogger || echo "keylogger module not loaded"

echo "[cleanup] Rebinding keyboard to usbhid..."
echo "${KBD_BUS_ID}:1.0" | sudo tee /sys/bus/usb/drivers/usbhid/bind

echo "[cleanup] Done."
