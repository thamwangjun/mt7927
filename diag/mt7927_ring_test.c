// SPDX-License-Identifier: GPL-2.0
/*
 * MT7927 Ring Register Diagnostic
 * 
 * Tests which ring registers are writable at BAR0 WFDMA offsets.
 */

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/delay.h>

#define MT7927_VENDOR_ID    0x14c3
#define MT7927_DEVICE_ID    0x7927

/* WFDMA0 base at BAR0 */
#define WFDMA0_BASE         0x2000

/* Register offsets relative to WFDMA0 */
#define RST_OFFSET          0x100
#define GLO_CFG_OFFSET      0x208
#define TX0_BASE_OFFSET     0x300
#define TX0_CNT_OFFSET      0x304
#define TX0_CIDX_OFFSET     0x308
#define TX0_DIDX_OFFSET     0x30c
#define TX15_BASE_OFFSET    0x3f0
#define TX16_BASE_OFFSET    0x400
#define RX0_BASE_OFFSET     0x500

static void __iomem *bar0;
static void __iomem *bar2;

static u32 test_write(void __iomem *base, u32 offset, const char *name)
{
    u32 before, after;
    u32 test_val = 0xDEADBEEF;
    
    before = readl(base + offset);
    writel(test_val, base + offset);
    wmb();
    after = readl(base + offset);
    
    /* Restore original value */
    writel(before, base + offset);
    wmb();
    
    pr_info("  [0x%05x] %-20s: before=0x%08x, wrote=0x%08x, read=0x%08x -> %s\n",
            offset, name, before, test_val, after,
            (after == test_val) ? "WRITABLE" : 
            (after == before) ? "read-only" : "partial");
    
    return after;
}

static void scan_wfdma_regs(void)
{
    u32 offset, val;
    
    pr_info("MT7927: Scanning WFDMA0 register range (0x2000-0x2FFF)\n");
    pr_info("MT7927: Looking for non-zero values...\n");
    
    for (offset = WFDMA0_BASE; offset < WFDMA0_BASE + 0x1000; offset += 4) {
        val = readl(bar0 + offset);
        if (val != 0) {
            pr_info("  [0x%05x] = 0x%08x\n", offset, val);
        }
    }
}

static int __init mt7927_ring_test_init(void)
{
    struct pci_dev *pdev = NULL;
    int ret;
    u32 rst_val, glo_val;
    
    pr_info("MT7927 Ring Register Diagnostic\n");
    pr_info("================================\n");
    
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
    
    bar0 = pci_iomap(pdev, 0, 0);
    bar2 = pci_iomap(pdev, 2, 0);
    
    if (!bar0) {
        pr_err("MT7927: Failed to map BAR0\n");
        pci_disable_device(pdev);
        pci_dev_put(pdev);
        return -ENOMEM;
    }
    
    pr_info("BAR0 mapped: %p\n", bar0);
    pr_info("BAR2 mapped: %p\n", bar2);
    
    /* Check current state */
    rst_val = readl(bar0 + WFDMA0_BASE + RST_OFFSET);
    glo_val = readl(bar0 + WFDMA0_BASE + GLO_CFG_OFFSET);
    pr_info("\nCurrent state:\n");
    pr_info("  RST (0x2100): 0x%08x\n", rst_val);
    pr_info("  GLO_CFG (0x2208): 0x%08x\n", glo_val);
    
    /* Scan for non-zero values */
    scan_wfdma_regs();
    
    /* Test control registers */
    pr_info("\nTesting control register writeability:\n");
    test_write(bar0, WFDMA0_BASE + RST_OFFSET, "RST");
    test_write(bar0, WFDMA0_BASE + GLO_CFG_OFFSET, "GLO_CFG");
    
    /* Test TX ring 0 registers */
    pr_info("\nTesting TX Ring 0 registers:\n");
    test_write(bar0, WFDMA0_BASE + TX0_BASE_OFFSET, "TX0_BASE");
    test_write(bar0, WFDMA0_BASE + TX0_CNT_OFFSET, "TX0_CNT");
    test_write(bar0, WFDMA0_BASE + TX0_CIDX_OFFSET, "TX0_CIDX");
    test_write(bar0, WFDMA0_BASE + TX0_DIDX_OFFSET, "TX0_DIDX");
    
    /* Test TX ring 15/16 (MCU queues) */
    pr_info("\nTesting TX Ring 15/16 (MCU) registers:\n");
    test_write(bar0, WFDMA0_BASE + TX15_BASE_OFFSET, "TX15_BASE");
    test_write(bar0, WFDMA0_BASE + TX16_BASE_OFFSET, "TX16_BASE");
    
    /* Test RX ring 0 */
    pr_info("\nTesting RX Ring 0 registers:\n");
    test_write(bar0, WFDMA0_BASE + RX0_BASE_OFFSET, "RX0_BASE");
    
    /* Now try with RST cleared and DMA disabled */
    pr_info("\n--- Testing with DMA disabled ---\n");
    
    /* Disable DMA */
    writel(glo_val & ~0x05, bar0 + WFDMA0_BASE + GLO_CFG_OFFSET);
    wmb();
    msleep(10);
    
    /* Clear RST bits */
    writel(0, bar0 + WFDMA0_BASE + RST_OFFSET);
    wmb();
    msleep(10);
    
    pr_info("After disabling DMA and clearing RST:\n");
    pr_info("  RST: 0x%08x\n", readl(bar0 + WFDMA0_BASE + RST_OFFSET));
    pr_info("  GLO_CFG: 0x%08x\n", readl(bar0 + WFDMA0_BASE + GLO_CFG_OFFSET));
    
    pr_info("\nRe-testing TX Ring 0:\n");
    test_write(bar0, WFDMA0_BASE + TX0_BASE_OFFSET, "TX0_BASE");
    test_write(bar0, WFDMA0_BASE + TX0_CNT_OFFSET, "TX0_CNT");
    
    /* Try with DMA enabled */
    pr_info("\n--- Testing with DMA enabled ---\n");
    writel(glo_val | 0x05, bar0 + WFDMA0_BASE + GLO_CFG_OFFSET);
    wmb();
    msleep(10);
    
    pr_info("After enabling DMA:\n");
    pr_info("  GLO_CFG: 0x%08x\n", readl(bar0 + WFDMA0_BASE + GLO_CFG_OFFSET));
    
    pr_info("\nRe-testing TX Ring 0:\n");
    test_write(bar0, WFDMA0_BASE + TX0_BASE_OFFSET, "TX0_BASE");
    test_write(bar0, WFDMA0_BASE + TX0_CNT_OFFSET, "TX0_CNT");
    
    /* Compare with BAR2 access */
    pr_info("\n--- Comparing BAR0 vs BAR2 for TX ring registers ---\n");
    if (bar2) {
        /* BAR2 is at BAR0+0x10000, so TX0_BASE relative to BAR2 would be at 
         * (0x2300 - 0x10000) which is negative - not accessible via BAR2 
         * But let's check what's at equivalent offsets */
        pr_info("BAR2[0x000] = 0x%08x (chip ID area)\n", readl(bar2));
        pr_info("BAR2[0x200] = 0x%08x (interrupt status area)\n", readl(bar2 + 0x200));
        pr_info("BAR2[0x208] = 0x%08x (GLO_CFG area?)\n", readl(bar2 + 0x208));
    }
    
    /* Cleanup */
    if (bar2)
        pci_iounmap(pdev, bar2);
    pci_iounmap(pdev, bar0);
    pci_disable_device(pdev);
    pci_dev_put(pdev);
    
    pr_info("\nMT7927 Ring Register Diagnostic complete\n");
    
    return -ENODEV; /* Don't stay loaded */
}

static void __exit mt7927_ring_test_exit(void)
{
}

module_init(mt7927_ring_test_init);
module_exit(mt7927_ring_test_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("MT7927 Ring Register Diagnostic");
MODULE_AUTHOR("MT7927 Development");
