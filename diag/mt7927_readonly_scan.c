// SPDX-License-Identifier: GPL-2.0
/*
 * MT7927 Read-Only BAR0 Scan
 * 
 * SAFE: Only reads from BAR0, no writes at all.
 * Scans for ring-like patterns and non-zero regions.
 */

#include <linux/module.h>
#include <linux/pci.h>

#define MT7927_VENDOR_ID    0x14c3
#define MT7927_DEVICE_ID    0x7927

static void __iomem *bar0;
static void __iomem *bar2;

/* Look for regions with ring-like structure */
static void find_ring_patterns(void)
{
    u32 offset;
    u32 val0, val1, val2, val3;
    int candidates = 0;
    
    pr_info("Scanning BAR0 for ring-like patterns (0, 0x200, 0, 0)...\n");
    
    /* Scan first 1MB in 16-byte increments */
    for (offset = 0; offset < 0x100000; offset += 0x10) {
        val0 = readl(bar0 + offset);      /* BASE */
        val1 = readl(bar0 + offset + 4);  /* CNT */
        val2 = readl(bar0 + offset + 8);  /* CIDX */
        val3 = readl(bar0 + offset + 12); /* DIDX */
        
        /* Pattern 1: BASE=0, CNT=0x200 (typical default) */
        if (val0 == 0 && val1 == 0x200 && val2 == 0 && val3 == 0) {
            candidates++;
            pr_info("  [0x%05x] Ring pattern: BASE=0 CNT=0x200 CIDX=0 DIDX=0\n", offset);
        }
        /* Pattern 2: BASE=0, CNT non-zero power of 2 */
        else if (val0 == 0 && val1 != 0 && (val1 & (val1-1)) == 0 && val1 >= 64 && val1 <= 4096) {
            candidates++;
            pr_info("  [0x%05x] Ring-like: BASE=0 CNT=0x%x CIDX=0x%x DIDX=0x%x\n", 
                    offset, val1, val2, val3);
        }
    }
    
    pr_info("Found %d ring-like patterns\n", candidates);
}

/* Scan for non-zero interesting regions */
static void scan_nonzero_regions(void)
{
    u32 regions[] = {
        0x0000, 0x1000, 0x2000, 0x3000, 0x4000, 0x5000,
        0x6000, 0x7000, 0x8000, 0x9000, 0xa000, 0xb000,
        0xc000, 0xd000, 0xe000, 0xf000,
        0x10000, 0x18000, 0x20000, 0x30000, 0x40000, 
        0x50000, 0x54000, 0x55000, 0x60000, 0x70000,
        0x80000, 0x88000, 0x90000, 0xa0000, 0xb0000,
        0xc0000, 0xd0000, 0xe0000, 0xf0000,
    };
    int i;
    
    pr_info("\nSampling key BAR0 regions:\n");
    
    for (i = 0; i < ARRAY_SIZE(regions); i++) {
        u32 base = regions[i];
        u32 v0 = readl(bar0 + base);
        u32 v4 = readl(bar0 + base + 4);
        u32 v8 = readl(bar0 + base + 8);
        
        /* Only report if something interesting */
        if (v0 != 0 || v4 != 0 || v8 != 0) {
            pr_info("  [0x%05x]: 0x%08x 0x%08x 0x%08x\n", base, v0, v4, v8);
        }
        
        /* Also check +0x200 and +0x300 offsets (common WFDMA offsets) */
        v0 = readl(bar0 + base + 0x200);
        v4 = readl(bar0 + base + 0x208);
        if (v0 != 0 || v4 != 0) {
            pr_info("  [0x%05x]: +0x200=0x%08x +0x208=0x%08x\n", base, v0, v4);
        }
        
        v0 = readl(bar0 + base + 0x300);
        v4 = readl(bar0 + base + 0x304);
        if (v0 != 0 || v4 != 0) {
            pr_info("  [0x%05x]: +0x300=0x%08x +0x304=0x%08x\n", base, v0, v4);
        }
    }
}

/* Check BAR2 in detail */
static void scan_bar2(void)
{
    u32 offset;
    int count = 0;
    
    if (!bar2) {
        pr_info("\nBAR2 not mapped\n");
        return;
    }
    
    pr_info("\nBAR2 non-zero values (first 0x1000):\n");
    
    for (offset = 0; offset < 0x1000; offset += 4) {
        u32 val = readl(bar2 + offset);
        if (val != 0 && val != 0xffffffff) {
            count++;
            if (count <= 50)
                pr_info("  BAR2[0x%04x] = 0x%08x\n", offset, val);
        }
    }
    
    pr_info("BAR2: %d non-zero registers found\n", count);
}

/* Compare specific offsets between BAR0 and BAR2 */
static void compare_bars(void)
{
    u32 offsets[] = {
        0x000, 0x004, 0x100, 0x200, 0x204, 0x208, 0x20c,
        0x300, 0x304, 0x308, 0x30c,
        0x400, 0x500, 0x600, 0x700,
    };
    int i;
    
    pr_info("\nComparing BAR0 vs BAR2 (BAR2 might be BAR0+0x10000 window):\n");
    pr_info("  %-8s %-12s %-12s %-12s\n", "Offset", "BAR0", "BAR0+0x10000", "BAR2");
    
    for (i = 0; i < ARRAY_SIZE(offsets); i++) {
        u32 off = offsets[i];
        u32 bar0_val = readl(bar0 + off);
        u32 bar0_10k = readl(bar0 + 0x10000 + off);
        u32 bar2_val = bar2 ? readl(bar2 + off) : 0xDEAD;
        
        if (bar0_val != 0 || bar0_10k != 0 || bar2_val != 0) {
            pr_info("  0x%04x:  0x%08x   0x%08x   0x%08x%s\n", 
                    off, bar0_val, bar0_10k, bar2_val,
                    (bar0_10k == bar2_val) ? " (match)" : "");
        }
    }
}

static int __init mt7927_readonly_scan_init(void)
{
    struct pci_dev *pdev = NULL;
    int ret;
    resource_size_t bar0_len, bar2_len;
    
    pr_info("MT7927 Read-Only BAR Scan (SAFE - no writes)\n");
    pr_info("=============================================\n");
    
    pdev = pci_get_device(MT7927_VENDOR_ID, MT7927_DEVICE_ID, NULL);
    if (!pdev) {
        pr_err("MT7927: Device not found\n");
        return -ENODEV;
    }
    
    ret = pci_enable_device(pdev);
    if (ret) {
        pr_err("MT7927: Failed to enable device\n");
        pci_dev_put(pdev);
        return ret;
    }
    
    bar0_len = pci_resource_len(pdev, 0);
    bar2_len = pci_resource_len(pdev, 2);
    pr_info("BAR0 length: 0x%llx (%llu KB)\n", (u64)bar0_len, (u64)bar0_len/1024);
    pr_info("BAR2 length: 0x%llx (%llu KB)\n", (u64)bar2_len, (u64)bar2_len/1024);
    
    bar0 = pci_iomap(pdev, 0, 0);
    bar2 = pci_iomap(pdev, 2, 0);
    
    if (!bar0) {
        pr_err("MT7927: Failed to map BAR0\n");
        pci_disable_device(pdev);
        pci_dev_put(pdev);
        return -ENOMEM;
    }
    
    pr_info("BAR0 mapped OK, BAR2 %s\n", bar2 ? "mapped OK" : "FAILED");
    
    /* Read-only scans */
    scan_nonzero_regions();
    compare_bars();
    scan_bar2();
    find_ring_patterns();
    
    /* Cleanup */
    if (bar2)
        pci_iounmap(pdev, bar2);
    pci_iounmap(pdev, bar0);
    pci_disable_device(pdev);
    pci_dev_put(pdev);
    
    pr_info("\nRead-only scan complete - no hardware state changed\n");
    
    return -ENODEV; /* Don't stay loaded */
}

static void __exit mt7927_readonly_scan_exit(void)
{
}

module_init(mt7927_readonly_scan_init);
module_exit(mt7927_readonly_scan_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("MT7927 Read-Only BAR Scan");
MODULE_AUTHOR("MT7927 Development");
