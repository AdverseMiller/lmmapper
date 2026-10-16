ifneq ($(KERNELRELEASE),)
ccflags-y += -I$(src)/include
obj-m += lmmapper.o
lmmapper-y := src/lmmapper.o
else
KDIR ?= /lib/modules/$(shell uname -r)/build

.PHONY: all module clean

all: module

module:
	$(MAKE) -C $(KDIR) M=$(CURDIR) modules

clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) clean
endif
