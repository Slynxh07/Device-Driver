#!/bin/bash
set -e

echo "Cleaning user program..."
cd user
make clean

echo "Cleaing kernel module..."
cd ../kernel
make clean

cd ..

echo "Unloading KeyDriver module..."
sudo rmmod KeyDriver

echo "Loading usbhid driver..."
sudo modprobe usbhid