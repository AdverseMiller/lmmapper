ifneq ($(KERNELRELEASE),)
ccflags-y += -I$(src)/include
obj-m += helper.o
helper-y := src/helper.o
else
KDIR ?= /lib/modules/$(shell uname -r)/build

.PHONY: all module clean

all: module

module:
	$(MAKE) -C $(KDIR) M=$(CURDIR) modules

clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) clean
endif
