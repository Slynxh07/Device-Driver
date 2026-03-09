obj-m := keylogger.o
# obj-m: list of object files to build as a kernel *module* (not built-in).
# Here it says: build keylogger.o (from keylogger.c) into keylogger.ko.

KDIR := /lib/modules/$(shell uname -r)/build
# KDIR: path to the build directory of the *currently running* kernel.
# $(shell uname -r) runs "uname -r" to get the kernel version, and
# /lib/modules/<version>/build points at that kernel’s build tree.

PWD  := $(shell pwd)
# PWD: the absolute path to this directory (where this Makefile lives).
# The kernel build system uses this to find keylogger.c and this Makefile.

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules
# "all" is the default target when you run "make".
# $(MAKE) -C $(KDIR): run make inside the kernel build directory.
# M=$(PWD): tell the kernel build system that we are building an *external*
#           module located in directory PWD.
# "modules": ask the kernel build system to build the modules listed in obj-m,
#            so this compiles keylogger.c into keylogger.ko.

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
# "clean" target, run with "make clean".
# Calls the kernel build system to remove build files for this external module
# in PWD (e.g. keylogger.o, keylogger.ko, and temporary objects).
