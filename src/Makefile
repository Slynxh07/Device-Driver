# This is a Makefile. It defines how to build, run, and clean a program.

# This adds flags to make. 
# += means add a flag, := would mean remove all flags and replace by the following flag.

# ---1---
# -s or "silent mode" means that the only things printed in the terminal are errors or program output.
# Other command lines are ignored.
MAKEFLAGS += -s

# ---2---
# This line says "I want to treat myfifo.c as an object file of myfifo.c.
# Then turn that object file into a kernel loadable module (.ko). 
obj-m := myfifo.o
# Now because we are building a KERNEL module we need to talk about kbuild.
# kbuild is a kernel build manager. This is how we turn source to kernel code.

# ---3 and 4---
# KDIR is the path to the kernel build directory.
# PWD means the path of the folder of are currently in.
KDIR := /lib/modules/$(shell uname -r)/build
PWD  := $(shell pwd)

# ---5---
# Compile KDIR and PWD to make the module.
all:
	make -C $(KDIR) M=$(PWD) modules

# ---6---
# The same as above but with "clean" instead of "modules".
clean:
	make -C $(KDIR) M=$(PWD) clean