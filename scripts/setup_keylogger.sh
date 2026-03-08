#!/bin/sh
set -e

# TODO: adjust this to your actual keyboard bus ID, e.g. "1-1.2"
KBD_BUS_ID="1-1"

echo "[setup] Unbinding keyboard from usbhid..."
echo "${KBD_BUS_ID}:1.0" | sudo tee /sys/bus/usb/drivers/usbhid/unbind

echo "[setup] Inserting keylogger module..."
sudo insmod ../driver/keylogger.ko

# If /dev/keylogger not created by udev for some reason, create manually.
if [ ! -e /dev/keylogger ]; then
    echo "[setup] /dev/keylogger missing, creating manually..."
    MAJOR=$(grep keylogger /proc/devices | awk '{print $1}')
    if [ -z "$MAJOR" ]; then
        echo "Cannot find major number for keylogger"
        exit 1
    fi
    sudo mknod /dev/keylogger c "$MAJOR" 0
    sudo chmod 666 /dev/keylogger
fi

echo "[setup] Done."
