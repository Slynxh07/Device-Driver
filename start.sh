#!/bin/bash
set -e

echo "Building user program..."
cd user
make

echo "Building kernel module..."
cd ../kernel
make

echo "Unloading usbhid driver..."
sudo modprobe -r usbhid

echo "Loading KeyDriver module..."
sudo insmod KeyDriver.ko

echo "Running user program..."
cd ../user/build
./user