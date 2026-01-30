// SPDX-License-Identifier: GPL-2.0
/*
 * MT7927 Minimal Safe Scan
 * 
 * ULTRA SAFE: Only reads from known-good regions (0x2000-0x2FFF)
 * that we've successfully accessed before.
 */

#include <linux/module.h>
#include <linux/pci.h>

#define MT7927_VENDOR_ID    0x14c3
#define MT7927_DEVICE_ID    0x7927

static void __iomem *bar0;
static void __iomem *bar2;

static int __init mt7927_minimal_scan_init(void)
{
    struct pci_dev *pdev = NULL;
    int ret;
    u32 offset;
    int ring_count = 0;
    
    pr_info("MT7927 Minimal Safe Scan\n");
    pr_info("========================\n");
    
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
    
    /* Map only what we need - don't map full BAR */
    bar0 = pci_iomap(pdev, 0, 0x10000);  /* Only first 64KB */
    bar2 = pci_iomap(pdev, 2, 0x1000);   /* Only first 4KB */
    
    if (!bar0) {
        pr_err("MT7927: Failed to map BAR0\n");
        pci_disable_device(pdev);
        pci_dev_put(pdev);
        return -ENOMEM;
    }
    
    pr_info("Mapped BAR0 (64KB), BAR2 %s\n", bar2 ? "(4KB)" : "failed");
    
    /* Check BAR2 chip ID area - known safe */
    if (bar2) {
        pr_info("\nBAR2 key registers:\n");
        pr_info("  [0x000] = 0x%08x (chip ID?)\n", readl(bar2 + 0x000));
        pr_info("  [0x004] = 0x%08x\n", readl(bar2 + 0x004));
        pr_info("  [0x200] = 0x%08x (FW_STATUS?)\n", readl(bar2 + 0x200));
        pr_info("  [0x208] = 0x%08x (GLO_CFG?)\n", readl(bar2 + 0x208));
    }
    
    /* Check WFDMA0 area at BAR0+0x2000 - known safe from previous scans */
    pr_info("\nBAR0 WFDMA0 control registers (0x2000-0x2300):\n");
    pr_info("  [0x2100] RST     = 0x%08x\n", readl(bar0 + 0x2100));
    pr_info("  [0x2200] INT_STA = 0x%08x\n", readl(bar0 + 0x2200));
    pr_info("  [0x2204] INT_ENA = 0x%08x\n", readl(bar0 + 0x2204));
    pr_info("  [0x2208] GLO_CFG = 0x%08x\n", readl(bar0 + 0x2208));
    
    /* Check TX ring 0 area */
    pr_info("\nBAR0 TX Ring registers:\n");
    for (offset = 0x2300; offset < 0x2400; offset += 0x10) {
        u32 base = readl(bar0 + offset);
        u32 cnt = readl(bar0 + offset + 4);
        u32 cidx = readl(bar0 + offset + 8);
        u32 didx = readl(bar0 + offset + 12);
        
        if (cnt == 0x200) {
            ring_count++;
            pr_info("  TX Ring at 0x%04x: BASE=0x%08x CNT=%d CIDX=%d DIDX=%d\n",
                    offset, base, cnt, cidx, didx);
        }
    }
    
    /* Check RX ring area */
    for (offset = 0x2500; offset < 0x2600; offset += 0x10) {
        u32 base = readl(bar0 + offset);
        u32 cnt = readl(bar0 + offset + 4);
        
        if (cnt == 0x200) {
            ring_count++;
            pr_info("  RX Ring at 0x%04x: BASE=0x%08x CNT=%d\n",
                    offset, base, cnt);
        }
    }
    
    pr_info("\nFound %d rings with CNT=0x200\n", ring_count);
    
    /* 
     * Note: We only mapped 64KB of BAR0, so we can't check BAR0+0x10000
     * BAR2 is likely a separate window, already checked above
     */
    pr_info("\nBAR2 is separate from BAR0 - already examined above\n");
    
    /* Cleanup */
    if (bar2)
        pci_iounmap(pdev, bar2);
    pci_iounmap(pdev, bar0);
    pci_disable_device(pdev);
    pci_dev_put(pdev);
    
    pr_info("\nMinimal scan complete\n");
    
    return -ENODEV;
}

static void __exit mt7927_minimal_scan_exit(void)
{
}

module_init(mt7927_minimal_scan_init);
module_exit(mt7927_minimal_scan_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("MT7927 Minimal Safe Scan");
MODULE_AUTHOR("MT7927 Development");
