# SPDX-License-Identifier: GPL-2.0
#
# MT7927 WiFi 7 Linux Driver Makefile
#

obj-m := mt7927.o

mt7927-y := mt7927_pci.o mt7927_dma.o mt7927_mcu.o

# Kernel build directory
KDIR ?= /lib/modules/$(shell uname -r)/build

# Current directory
PWD := $(shell pwd)

# Default target
all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean

install:
	$(MAKE) -C $(KDIR) M=$(PWD) modules_install

# Load module (for testing)
load: all
	-sudo rmmod mt7927 2>/dev/null
	sudo insmod mt7927.ko

# Unload module
unload:
	-sudo rmmod mt7927 2>/dev/null

# View kernel messages
dmesg:
	sudo dmesg | tail -50

# Check device binding
status:
	@echo "=== PCI Device Status ==="
	lspci -k | grep -A 3 "14c3:7927" || echo "MT7927 not found"
	@echo ""
	@echo "=== Module Status ==="
	lsmod | grep mt7927 || echo "Module not loaded"

.PHONY: all clean install load unload dmesg status
