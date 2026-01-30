# Makefile for MT7927 Diagnostic Modules

KDIR ?= /lib/modules/$(shell uname -r)/build

# Remaining diagnostic modules (10 DMA-focused modules removed in Phase 21)
# Removed: mt7927_real_dma, mt7927_ring_test, mt7927_ring_scan_readonly,
#          mt7927_ring_scan_readwrite, mt7927_bar0_dma, mt7927_dma_reset,
#          mt7927_find_wfdma, mt7927_wfdma1_scan, mt7927_wide_scan, mt7927_minimal_scan
# These modules used wrong WFDMA address (0x2000 instead of 0xd4000)

obj-m := mt7927_diag.o \
         mt7927_power_diag.o \
         mt7927_claim_host.o \
         mt7927_disable_aspm.o \
         mt7927_wfsys_reset.o \
         mt7927_correct_pmctrl.o \
         mt7927_full_init.o \
         mt7927_scan_readonly.o \
         mt7927_readonly_scan.o \
         mt7927_power_unlock.o \
         mt7927_fw_precheck.o

all:
	$(MAKE) -C $(KDIR) M=$(CURDIR) modules

clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) clean
