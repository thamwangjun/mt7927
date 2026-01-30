// SPDX-License-Identifier: GPL-2.0
/*
 * MT7927 Wide BAR0 Scan
 * 
 * Scans the entire BAR0 to find writable ring-like registers.
 * Looking for patterns that match DMA ring registers.
 */

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/delay.h>

#define MT7927_VENDOR_ID    0x14c3
#define MT7927_DEVICE_ID    0x7927

static void __iomem *bar0;
static struct pci_dev *pdev;

/* Test if a 16-byte region looks like a ring register set */
static bool test_ring_region(u32 base_offset)
{
    u32 before[4], after[4];
    u32 test_val = 0x12340000;
    int i;
    bool any_writable = false;
    
    /* Read original values */
    for (i = 0; i < 4; i++)
        before[i] = readl(bar0 + base_offset + i * 4);
    
    /* Try writing test pattern to BASE register (offset 0) */
    writel(test_val, bar0 + base_offset);
    wmb();
    after[0] = readl(bar0 + base_offset);
    
    /* Restore */
    writel(before[0], bar0 + base_offset);
    wmb();
    
    if (after[0] == test_val) {
        any_writable = true;
        pr_info("  [0x%05x] WRITABLE ring base! before=0x%08x after=0x%08x\n",
                base_offset, before[0], after[0]);
        pr_info("           CNT=0x%08x CIDX=0x%08x DIDX=0x%08x\n",
                before[1], before[2], before[3]);
    }
    
    return any_writable;
}

/* Look for regions with ring-like structure (BASE=0, CNT=0x200) */
static void find_ring_candidates(void)
{
    u32 offset;
    u32 val0, val1;
    int candidates = 0;
    
    pr_info("Scanning for ring-like structures (BASE=0, CNT=0x200 pattern)...\n");
    
    for (offset = 0; offset < 0x100000; offset += 0x10) {
        val0 = readl(bar0 + offset);
        val1 = readl(bar0 + offset + 4);
        
        /* Look for pattern: offset+0 = 0, offset+4 = 0x200 */
        if (val0 == 0 && val1 == 0x200) {
            candidates++;
            if (candidates <= 50) { /* Limit output */
                pr_info("  [0x%05x] Candidate ring: BASE=0x%08x CNT=0x%08x\n",
                        offset, val0, val1);
                /* Test if writable */
                test_ring_region(offset);
            }
        }
    }
    
    pr_info("Found %d ring-like candidates\n", candidates);
}

/* Scan for any writable regions in key areas */
static void scan_writable_regions(void)
{
    u32 regions[] = {
        0x0000, 0x1000, 0x2000, 0x3000, 0x4000, 0x5000,
        0x6000, 0x7000, 0x8000, 0x9000, 0xa000, 0xb000,
        0xc000, 0xd000, 0xe000, 0xf000,
        0x10000, 0x20000, 0x30000, 0x40000, 0x50000,
        0x54000, 0x55000, /* WFDMA logical addresses mapped physically? */
        0x80000, 0x90000, 0xa0000, 0xb0000,
        0xc0000, 0xd0000, 0xe0000, 0xf0000,
    };
    int i;
    u32 before, after;
    u32 test_val = 0xCAFEBABE;
    
    pr_info("\nScanning key regions for writable ring bases...\n");
    
    for (i = 0; i < ARRAY_SIZE(regions); i++) {
        u32 base = regions[i];
        
        /* Check offset 0x300 (TX ring 0 offset) relative to potential WFDMA bases */
        before = readl(bar0 + base + 0x300);
        writel(test_val, bar0 + base + 0x300);
        wmb();
        after = readl(bar0 + base + 0x300);
        writel(before, bar0 + base + 0x300); /* restore */
        wmb();
        
        if (after == test_val) {
            pr_info("  [0x%05x+0x300] = WRITABLE! (was 0x%08x)\n", base, before);
        } else if (after != before && after != 0) {
            pr_info("  [0x%05x+0x300] = partial (before=0x%08x, after=0x%08x)\n",
                    base, before, after);
        }
    }
}

/* Check chip identification */
static void check_chip_id(void)
{
    u32 id_locations[] = {
        0x0000, 0x0004, 0x0008,
        0x1000, 0x2000, 0x3000,
        0x10000, 0x18000,
        0x80000, 0x88000,
    };
    int i;
    
    pr_info("\nChecking potential chip ID locations:\n");
    for (i = 0; i < ARRAY_SIZE(id_locations); i++) {
        u32 val = readl(bar0 + id_locations[i]);
        if (val != 0 && val != 0xffffffff) {
            pr_info("  [0x%05x] = 0x%08x\n", id_locations[i], val);
        }
    }
}

/* Detailed scan of WFDMA area with write tests */
static void detailed_wfdma_scan(void)
{
    u32 offset;
    u32 before, after, test_val = 0xABCD1234;
    int writable_count = 0;
    
    pr_info("\nDetailed WFDMA scan with write tests (0x2000-0x3000):\n");
    
    for (offset = 0x2000; offset < 0x3000; offset += 4) {
        before = readl(bar0 + offset);
        writel(test_val, bar0 + offset);
        wmb();
        after = readl(bar0 + offset);
        writel(before, bar0 + offset); /* restore */
        wmb();
        
        if (after == test_val) {
            writable_count++;
            pr_info("  [0x%05x] WRITABLE: before=0x%08x\n", offset, before);
        } else if (after != before) {
            pr_info("  [0x%05x] PARTIAL:  before=0x%08x after=0x%08x\n",
                    offset, before, after);
        }
    }
    
    pr_info("Found %d fully writable registers in WFDMA area\n", writable_count);
}

static int __init mt7927_wide_scan_init(void)
{
    int ret;
    resource_size_t bar0_len;
    
    pr_info("MT7927 Wide BAR0 Scan\n");
    pr_info("=====================\n");
    
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
    
    pci_set_master(pdev);
    
    bar0_len = pci_resource_len(pdev, 0);
    pr_info("BAR0 length: 0x%llx (%llu MB)\n", 
            (u64)bar0_len, (u64)bar0_len / (1024*1024));
    
    bar0 = pci_iomap(pdev, 0, 0);
    if (!bar0) {
        pr_err("MT7927: Failed to map BAR0\n");
        pci_disable_device(pdev);
        pci_dev_put(pdev);
        return -ENOMEM;
    }
    
    /* Check chip IDs */
    check_chip_id();
    
    /* Detailed WFDMA scan */
    detailed_wfdma_scan();
    
    /* Look for ring candidates across BAR0 */
    find_ring_candidates();
    
    /* Scan key regions */
    scan_writable_regions();
    
    /* Cleanup */
    pci_iounmap(pdev, bar0);
    pci_disable_device(pdev);
    pci_dev_put(pdev);
    
    pr_info("\nMT7927 Wide Scan complete\n");
    
    return -ENODEV; /* Don't stay loaded */
}

static void __exit mt7927_wide_scan_exit(void)
{
}

module_init(mt7927_wide_scan_init);
module_exit(mt7927_wide_scan_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("MT7927 Wide BAR0 Scan");
MODULE_AUTHOR("MT7927 Development");
