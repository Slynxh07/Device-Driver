To build and run:

```bash
git clone https://github.com/Slynxh07/Device-Driver.git
cd Device-Driver
make
sudo insmod Driver.ko
sudo dmesg | tail -n 1
sudo rmmod Driver
sudo dmesg | tail -n 1
```