#!/bin/sh
# Use /bin/sh as the shell to run this script.

set -e
# Exit immediately if any command in this script fails (non‑zero status).

# Same bus ID as in setup script
KBD_BUS_ID="1-1"
# KBD_BUS_ID: identifies the USB keyboard on the USB bus.
# This must match the ID you used when you *unbound* it from usbhid
# in your setup script (you usually get it from lsusb or /sys).

echo "[cleanup] Removing keylogger module..."
sudo rmmod keylogger || echo "keylogger module not loaded"
# Try to remove (unload) the "keylogger" kernel module.
# If rmmod fails (for example because it is not loaded), print a message
# instead of stopping the script (because of the "|| ...").

echo "[cleanup] Rebinding keyboard to usbhid..."
echo "${KBD_BUS_ID}:1.0" | sudo tee /sys/bus/usb/drivers/usbhid/bind
# Re‑attach the USB keyboard to the normal usbhid driver.
# We write "<bus-id>:1.0" into the "bind" file of the usbhid driver in /sys.
# That tells the kernel: "bind usbhid to this specific USB interface again".

echo "[cleanup] Done."
# Print a final message so the user knows the cleanup is finished.
