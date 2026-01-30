// SPDX-License-Identifier: GPL-2.0
/*
 * MT7927 WFDMA1 Scan
 * 
 * Check if MCU rings are at WFDMA1 (0x3000) instead of WFDMA0 (0x2000)
 */

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/delay.h>

#define MT7927_VENDOR_ID    0x14c3
#define MT7927_DEVICE_ID    0x7927

static void __iomem *bar0;

static void scan_wfdma_region(u32 base, const char *name)
{
    u32 offset;
    int ring_count = 0;
    
    pr_info("Scanning %s (0x%04x):\n", name, base);
    
    /* Check GLO_CFG at +0x208 */
    pr_info("  GLO_CFG (0x%04x): 0x%08x\n", base + 0x208, readl(bar0 + base + 0x208));
    pr_info("  RST     (0x%04x): 0x%08x\n", base + 0x100, readl(bar0 + base + 0x100));
    
    /* Check TX rings at +0x300 */
    pr_info("  TX rings at +0x300:\n");
    for (offset = 0x300; offset < 0x500; offset += 0x10) {
        u32 base_reg = readl(bar0 + base + offset);
        u32 cnt = readl(bar0 + base + offset + 4);
        u32 cidx = readl(bar0 + base + offset + 8);
        u32 didx = readl(bar0 + base + offset + 0xc);
        
        /* Show if CNT is non-zero (likely a real ring) */
        if (cnt != 0 || base_reg != 0) {
            int ring_idx = (offset - 0x300) / 0x10;
            pr_info("    Ring %2d (0x%04x): BASE=0x%08x CNT=%d CIDX=%d DIDX=%d\n",
                    ring_idx, base + offset, base_reg, cnt, cidx, didx);
            ring_count++;
        }
    }
    
    /* Check RX rings at +0x500 */
    pr_info("  RX rings at +0x500:\n");
    for (offset = 0x500; offset < 0x600; offset += 0x10) {
        u32 cnt = readl(bar0 + base + offset + 4);
        
        if (cnt != 0) {
            int ring_idx = (offset - 0x500) / 0x10;
            pr_info("    Ring %2d (0x%04x): CNT=%d\n",
                    ring_idx, base + offset, cnt);
            ring_count++;
        }
    }
    
    pr_info("  Found %d rings with non-zero values\n", ring_count);
}

static void test_wfdma1_write(void)
{
    u32 base = 0x3000;  /* WFDMA1 */
    u32 test_val = 0xABCD1234;
    u32 before, after;
    
    pr_info("\nTesting WFDMA1 ring writeability:\n");
    
    /* Test ring 0 at WFDMA1 (would be MCU ring 16) */
    before = readl(bar0 + base + 0x300);
    writel(test_val, bar0 + base + 0x300);
    wmb();
    after = readl(bar0 + base + 0x300);
    writel(before, bar0 + base + 0x300);  /* restore */
    wmb();
    
    pr_info("  WFDMA1 Ring 0 (0x3300): before=0x%08x wrote=0x%08x read=0x%08x -> %s\n",
            before, test_val, after,
            (after == test_val) ? "WRITABLE!" : "read-only");
    
    /* Test ring 1 at WFDMA1 (would be MCU ring 17) */
    before = readl(bar0 + base + 0x310);
    writel(test_val, bar0 + base + 0x310);
    wmb();
    after = readl(bar0 + base + 0x310);
    writel(before, bar0 + base + 0x310);  /* restore */
    wmb();
    
    pr_info("  WFDMA1 Ring 1 (0x3310): before=0x%08x wrote=0x%08x read=0x%08x -> %s\n",
            before, test_val, after,
            (after == test_val) ? "WRITABLE!" : "read-only");
}

static int __init mt7927_wfdma1_scan_init(void)
{
    struct pci_dev *pdev = NULL;
    int ret;
    
    pr_info("MT7927 WFDMA1 Scan\n");
    pr_info("==================\n");
    
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
    
    bar0 = pci_iomap(pdev, 0, 0x10000);  /* Map 64KB */
    if (!bar0) {
        pr_err("MT7927: Failed to map BAR0\n");
        pci_disable_device(pdev);
        pci_dev_put(pdev);
        return -ENOMEM;
    }
    
    /* Scan WFDMA0 at 0x2000 */
    scan_wfdma_region(0x2000, "WFDMA0");
    
    /* Scan WFDMA1 at 0x3000 */
    scan_wfdma_region(0x3000, "WFDMA1");
    
    /* Test if WFDMA1 rings are writable */
    test_wfdma1_write();
    
    /* Cleanup */
    pci_iounmap(pdev, bar0);
    pci_disable_device(pdev);
    pci_dev_put(pdev);
    
    pr_info("\nWFDMA1 scan complete\n");
    
    return -ENODEV;
}

static void __exit mt7927_wfdma1_scan_exit(void)
{
}

module_init(mt7927_wfdma1_scan_init);
module_exit(mt7927_wfdma1_scan_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("MT7927 WFDMA1 Scan");
MODULE_AUTHOR("MT7927 Development");
