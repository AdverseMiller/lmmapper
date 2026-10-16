CC ?= cc
CFLAGS ?= -O2 -g -std=c11 -Wall -Wextra -Wpedantic -Werror
CPPFLAGS += -Iinclude -Imodule/include -MMD -MP
KDIR ?= /lib/modules/$(shell uname -r)/build
KERNEL_MAKE_FLAGS ?= LLVM=1 W=1
.DEFAULT_GOAL := all

APP_OBJECTS := src/elf.o src/reloc.o src/map.o src/symbols.o src/rw.o src/rw_helper.o src/live_image.o src/live_profile.o src/live_bridge.o src/live_map.o src/live_command.o src/main.o

-include $(APP_OBJECTS:.o=.d)

.PHONY: all clean kernel-module kernel-module-clean live-layout live-layout-clean

all: lmmapper

lmmapper: $(APP_OBJECTS)
	$(CC) $(CFLAGS) -o $@ $(APP_OBJECTS)

kernel-module:
	$(MAKE) -C module $(KERNEL_MAKE_FLAGS)

kernel-module-clean:
	$(MAKE) -C module clean

live-layout:
	$(MAKE) -B -C $(KDIR) M=$(CURDIR)/tools/kernel_layout $(KERNEL_MAKE_FLAGS) layout_probe.o

live-layout-clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR)/tools/kernel_layout clean

clean:
	$(RM) $(APP_OBJECTS) $(APP_OBJECTS:.o=.d) lmmapper
