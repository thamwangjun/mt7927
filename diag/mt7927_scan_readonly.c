// SPDX-License-Identifier: GPL-2.0
/*
 * MT7927 Read-Only Register Scanner
 * 
 * SAFE: This module only READS registers - NO WRITES.
 * Scans BAR0 and BAR2 at various offsets to map the register layout.
 */

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/io.h>

#define MT7927_VENDOR_ID    0x14c3
#define MT7927_DEVICE_ID    0x7927

struct mt7927_scan {
    struct pci_dev *pdev;
    void __iomem *bar0;
    void __iomem *bar2;
    resource_size_t bar0_size;
    resource_size_t bar2_size;
};

/* Check if offset is safe to read */
static bool safe_offset(struct mt7927_scan *dev, void __iomem *bar, 
                         resource_size_t bar_size, u32 offset)
{
    return (offset + 4) <= bar_size;
}

/* Read with bounds checking */
static u32 safe_read(struct mt7927_scan *dev, void __iomem *bar,
                     resource_size_t bar_size, u32 offset)
{
    if (!safe_offset(dev, bar, bar_size, offset))
        return 0xDEADBEEF;  /* Marker for out-of-bounds */
    return readl(bar + offset);
}

static void scan_wfdma_region(struct mt7927_scan *dev, u32 base, const char *name)
{
    u32 val;
    bool found_nonzero = false;

    dev_info(&dev->pdev->dev, "\n%s (BAR0 + 0x%05x):\n", name, base);

    /* Key WFDMA register offsets */
    struct {
        u32 offset;
        const char *name;
    } regs[] = {
        { 0x000, "Base+0x000" },
        { 0x004, "Base+0x004" },
        { 0x100, "RST       " },
        { 0x200, "HOST_INT  " },
        { 0x204, "INT_ENA   " },
        { 0x208, "GLO_CFG   " },
        { 0x20c, "RST_DTX   " },
        { 0x300, "TX0_BASE  " },
        { 0x304, "TX0_CNT   " },
        { 0x400, "TX16_BASE " },
        { 0x500, "RX0_BASE  " },
    };

    for (int i = 0; i < ARRAY_SIZE(regs); i++) {
        u32 addr = base + regs[i].offset;
        val = safe_read(dev, dev->bar0, dev->bar0_size, addr);
        
        if (val != 0 && val != 0xDEADBEEF && val != 0xFFFFFFFF)
            found_nonzero = true;

        dev_info(&dev->pdev->dev, "  [0x%05x] %s: 0x%08x%s\n",
                 addr, regs[i].name, val,
                 (val == 0xDEADBEEF) ? " (out of bounds)" : 
                 (val == 0xffff10f1) ? " <-- FW_STATUS!" : "");
    }

    if (found_nonzero)
        dev_info(&dev->pdev->dev, "  *** NON-ZERO VALUES FOUND - possible WFDMA! ***\n");
}

static void dump_bar2_reference(struct mt7927_scan *dev)
{
    dev_info(&dev->pdev->dev, "\n=== BAR2 Reference (known working) ===\n");
    
    struct {
        u32 offset;
        const char *name;
    } regs[] = {
        { 0x000, "Chip ID   " },
        { 0x004, "HW Rev    " },
        { 0x100, "RST       " },
        { 0x200, "FW_STATUS " },
        { 0x204, "INT_ENA   " },
        { 0x208, "GLO_CFG   " },
        { 0x20c, "RST_DTX   " },
        { 0x300, "TX0_BASE  " },
        { 0x304, "TX0_CNT   " },
        { 0x308, "TX0_CPU   " },
        { 0x30c, "TX0_DMA   " },
        { 0x400, "TX16_BASE " },
        { 0x500, "RX0_BASE  " },
    };

    for (int i = 0; i < ARRAY_SIZE(regs); i++) {
        u32 val = safe_read(dev, dev->bar2, dev->bar2_size, regs[i].offset);
        dev_info(&dev->pdev->dev, "  BAR2[0x%03x] %s: 0x%08x\n",
                 regs[i].offset, regs[i].name, val);
    }
}

static void scan_bar0_regions(struct mt7927_scan *dev)
{
    dev_info(&dev->pdev->dev, "\n=== Scanning BAR0 for WFDMA registers ===\n");

    /* Based on mt7925 fixed_map entries */
    scan_wfdma_region(dev, 0x00000, "Direct (no offset)");
    scan_wfdma_region(dev, 0x02000, "WFDMA_0 (mt7925 map)");
    scan_wfdma_region(dev, 0x03000, "WFDMA_1 (mt7925 map)");
    scan_wfdma_region(dev, 0x04000, "Reserved");
    scan_wfdma_region(dev, 0x05000, "WFDMA_1 alt");
    scan_wfdma_region(dev, 0x06000, "WFDMA_1 alt2");
    scan_wfdma_region(dev, 0x07000, "Reserved2");
    scan_wfdma_region(dev, 0x08000, "WF_UMAC_TOP");

    /* Also try some power-of-2 boundaries */
    scan_wfdma_region(dev, 0x10000, "0x10000");
    scan_wfdma_region(dev, 0x20000, "0x20000");
    scan_wfdma_region(dev, 0x40000, "0x40000");
}

static void check_lpctl(struct mt7927_scan *dev)
{
    u32 lpctl;

    dev_info(&dev->pdev->dev, "\n=== Power Control State ===\n");
    
    /* LPCTL is at fixed offset in BAR0 */
    lpctl = safe_read(dev, dev->bar0, dev->bar0_size, 0xe0010);
    dev_info(&dev->pdev->dev, "  LPCTL (BAR0+0xe0010): 0x%08x\n", lpctl);
    dev_info(&dev->pdev->dev, "  Driver owns: %s\n", 
             (lpctl & BIT(2)) ? "NO (FW owns)" : "YES");

    /* WFSYS reset status */
    u32 wfsys = safe_read(dev, dev->bar0, dev->bar0_size, 0xf0140);
    dev_info(&dev->pdev->dev, "  WFSYS_RST (BAR0+0xf0140): 0x%08x\n", wfsys);

    /* CONN_ON_MISC for FW ready */
    u32 misc = safe_read(dev, dev->bar0, dev->bar0_size, 0xe00f0);
    dev_info(&dev->pdev->dev, "  CONN_MISC (BAR0+0xe00f0): 0x%08x\n", misc);
}

static int mt7927_scan_probe(struct pci_dev *pdev,
                              const struct pci_device_id *id)
{
    struct mt7927_scan *dev;
    int ret;

    dev_info(&pdev->dev, "=== MT7927 Read-Only Register Scanner ===\n");
    dev_info(&pdev->dev, "This module ONLY READS - no writes performed.\n");

    dev = devm_kzalloc(&pdev->dev, sizeof(*dev), GFP_KERNEL);
    if (!dev)
        return -ENOMEM;

    dev->pdev = pdev;
    pci_set_drvdata(pdev, dev);

    ret = pcim_enable_device(pdev);
    if (ret)
        return ret;

    /* Note: NOT calling pci_set_master - we're read-only */

    ret = pcim_iomap_regions(pdev, BIT(0) | BIT(2), "mt7927_scan");
    if (ret)
        return ret;

    dev->bar0 = pcim_iomap_table(pdev)[0];
    dev->bar2 = pcim_iomap_table(pdev)[2];
    dev->bar0_size = pci_resource_len(pdev, 0);
    dev->bar2_size = pci_resource_len(pdev, 2);

    dev_info(&pdev->dev, "BAR0: %pR (size: 0x%llx)\n",
             &pdev->resource[0], (u64)dev->bar0_size);
    dev_info(&pdev->dev, "BAR2: %pR (size: 0x%llx)\n",
             &pdev->resource[2], (u64)dev->bar2_size);

    /* Dump BAR2 as reference (we know this works) */
    dump_bar2_reference(dev);

    /* Check power control state */
    check_lpctl(dev);

    /* Scan BAR0 for WFDMA registers */
    scan_bar0_regions(dev);

    dev_info(&pdev->dev, "\n=== Scan complete (read-only, no changes made) ===\n");
    return 0;
}

static void mt7927_scan_remove(struct pci_dev *pdev)
{
    dev_info(&pdev->dev, "MT7927 scanner unloaded\n");
}

static const struct pci_device_id mt7927_scan_table[] = {
    { PCI_DEVICE(MT7927_VENDOR_ID, MT7927_DEVICE_ID) },
    { }
};
MODULE_DEVICE_TABLE(pci, mt7927_scan_table);

static struct pci_driver mt7927_scan_driver = {
    .name = "mt7927_scan_readonly",
    .id_table = mt7927_scan_table,
    .probe = mt7927_scan_probe,
    .remove = mt7927_scan_remove,
};

module_pci_driver(mt7927_scan_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("MT7927 Project");
MODULE_DESCRIPTION("MT7927 Read-Only Register Scanner");
