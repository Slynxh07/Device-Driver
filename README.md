# Build and load driver
cd driver
make
sudo insmod myfifo.ko
ls -l /dev/myfifo
cat /proc/myfifo_stats

# Build user app
cd ../user
make

# Run bridge (adjust serial device if needed)
/* if Arduino is /dev/ttyACM0 */
./arduino_bridge
/* or, e.g. */
./arduino_bridge /dev/ttyUSB0

# While arduino is running
echo "hello from shell" > /dev/myfifo
cat /dev/myfifo
(use s, w, q menu in the bridge window to show stats, send test messages, and quit)

# Unload
cd ../driver
sudo rmmod myfifo


Project Structre:

  driver/
    myfifo.c
    Makefile
  user/
    arduino_bridge.c
    Makefile
  README.md
