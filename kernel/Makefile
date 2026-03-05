obj-m += Driver.o

Driver-objs := src/main.o

KDIR := /lib/modules/$(shell uname -r)/build

PWD := $(shell pwd)

EXTRA_CFLAGS += -I$(PWD)/include

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean