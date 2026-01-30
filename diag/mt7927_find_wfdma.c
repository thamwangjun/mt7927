// SPDX-License-Identifier: GPL-2.0
/*
 * MT7927 WFDMA Register Finder
 * 
 * The mt7925 fixed_map shows WFDMA_0 at BAR offset 0x2000.
 * This test scans different base offsets to find where WFDMA lives.
 */

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/io.h>
#include <linux/delay.h>

#define MT7927_VENDOR_ID    0x14c3
#define MT7927_DEVICE_ID    0x7927

/* WFDMA register offsets relative to WFDMA base */
#define WFDMA_GLO_CFG_OFS      0x208
#define WFDMA_HOST_INT_OFS     0x200
#define WFDMA_RST_OFS          0x100

/* Power control */
#define MT_CONN_ON_LPCTL       0xe0010
#define PCIE_LPCR_HOST_CLR_OWN BIT(1)
#define PCIE_LPCR_HOST_OWN_SYNC BIT(2)

/* DMA enable bits */
#define GLO_CFG_TX_DMA_EN      BIT(0)
#define GLO_CFG_RX_DMA_EN      BIT(2)

struct mt7927_find {
    struct pci_dev *pdev;
    void __iomem *bar0;
    void __iomem *bar2;
};

static void scan_wfdma_base(struct mt7927_find *dev, u32 base, const char *name)
{
    u32 glo_cfg, host_int, rst;

    /* Check if this offset is within BAR0 (2MB = 0x200000) */
    if (base + WFDMA_GLO_CFG_OFS >= 0x200000) {
        dev_info(&dev->pdev->dev, "%s (0x%05x): Out of range\n", name, base);
        return;
    }

    glo_cfg = readl(dev->bar0 + base + WFDMA_GLO_CFG_OFS);
    host_int = readl(dev->bar0 + base + WFDMA_HOST_INT_OFS);
    rst = readl(dev->bar0 + base + WFDMA_RST_OFS);

    dev_info(&dev->pdev->dev, "%s (base=0x%05x):\n", name, base);
    dev_info(&dev->pdev->dev, "  GLO_CFG[+0x208]: 0x%08x\n", glo_cfg);
    dev_info(&dev->pdev->dev, "  HOST_INT[+0x200]: 0x%08x%s\n", host_int,
             (host_int == 0xffff10f1) ? " <-- FW_STATUS match!" : "");
    dev_info(&dev->pdev->dev, "  RST[+0x100]: 0x%08x\n", rst);

    /* Check for signs of life */
    if (host_int != 0 && host_int != 0xffffffff) {
        dev_info(&dev->pdev->dev, "  *** POSSIBLE WFDMA FOUND! ***\n");
    }
}

static void try_enable_at_base(struct mt7927_find *dev, u32 base)
{
    u32 before, after, val;

    dev_info(&dev->pdev->dev, "\nTrying DMA enable at base 0x%05x...\n", base);

    before = readl(dev->bar0 + base + WFDMA_GLO_CFG_OFS);
    dev_info(&dev->pdev->dev, "  GLO_CFG before: 0x%08x\n", before);

    /* Try to write enable bits */
    val = before | GLO_CFG_TX_DMA_EN | GLO_CFG_RX_DMA_EN;
    writel(val, dev->bar0 + base + WFDMA_GLO_CFG_OFS);
    wmb();
    udelay(100);

    after = readl(dev->bar0 + base + WFDMA_GLO_CFG_OFS);
    dev_info(&dev->pdev->dev, "  GLO_CFG after:  0x%08x\n", after);

    if (after != before) {
        dev_info(&dev->pdev->dev, "  *** REGISTER IS WRITABLE! ***\n");
    }
}

static int mt7927_find_probe(struct pci_dev *pdev,
                              const struct pci_device_id *id)
{
    struct mt7927_find *dev;
    u32 lpctl;
    int ret, i;

    dev_info(&pdev->dev, "=== MT7927 WFDMA Register Finder ===\n");

    dev = devm_kzalloc(&pdev->dev, sizeof(*dev), GFP_KERNEL);
    if (!dev)
        return -ENOMEM;

    dev->pdev = pdev;
    pci_set_drvdata(pdev, dev);

    ret = pcim_enable_device(pdev);
    if (ret)
        return ret;

    pci_set_master(pdev);

    ret = pcim_iomap_regions(pdev, BIT(0) | BIT(2), "mt7927_find");
    if (ret)
        return ret;

    dev->bar0 = pcim_iomap_table(pdev)[0];
    dev->bar2 = pcim_iomap_table(pdev)[2];

    /* Ensure driver owns chip */
    lpctl = readl(dev->bar0 + MT_CONN_ON_LPCTL);
    if (lpctl & PCIE_LPCR_HOST_OWN_SYNC) {
        writel(PCIE_LPCR_HOST_CLR_OWN, dev->bar0 + MT_CONN_ON_LPCTL);
        for (i = 0; i < 100; i++) {
            lpctl = readl(dev->bar0 + MT_CONN_ON_LPCTL);
            if (!(lpctl & PCIE_LPCR_HOST_OWN_SYNC))
                break;
            msleep(1);
        }
    }
    dev_info(&pdev->dev, "LPCTL: 0x%08x\n", lpctl);

    /* Reference: BAR2 values */
    dev_info(&pdev->dev, "\nBAR2 reference values:\n");
    dev_info(&pdev->dev, "  BAR2[0x200]: 0x%08x (FW_STATUS)\n",
             readl(dev->bar2 + 0x200));
    dev_info(&pdev->dev, "  BAR2[0x208]: 0x%08x (GLO_CFG)\n",
             readl(dev->bar2 + 0x208));

    /* Scan possible WFDMA base offsets in BAR0 */
    dev_info(&pdev->dev, "\nScanning BAR0 for WFDMA registers...\n");

    /* From mt7925 fixed_map: { 0x54000000, 0x02000, 0x0001000 } */
    scan_wfdma_base(dev, 0x00000, "Direct (0x0)");
    scan_wfdma_base(dev, 0x02000, "WFDMA_0 (0x2000)");
    scan_wfdma_base(dev, 0x03000, "WFDMA_1 (0x3000)");
    scan_wfdma_base(dev, 0x04000, "Reserved (0x4000)");
    scan_wfdma_base(dev, 0x08000, "WF_UMAC_TOP (0x8000)");

    /* Also check higher offsets that might contain WFDMA */
    scan_wfdma_base(dev, 0x10000, "0x10000");
    scan_wfdma_base(dev, 0x20000, "0x20000");
    scan_wfdma_base(dev, 0x80000, "WF_MCU_SYSRAM (0x80000)");

    /* Try enabling DMA at most promising base */
    try_enable_at_base(dev, 0x02000);

    /* Also try BAR2 directly since it shows live values */
    dev_info(&pdev->dev, "\nTrying DMA enable via BAR2...\n");
    {
        u32 before, after;
        before = readl(dev->bar2 + WFDMA_GLO_CFG_OFS);
        writel(before | GLO_CFG_TX_DMA_EN | GLO_CFG_RX_DMA_EN,
               dev->bar2 + WFDMA_GLO_CFG_OFS);
        wmb();
        udelay(100);
        after = readl(dev->bar2 + WFDMA_GLO_CFG_OFS);
        dev_info(&pdev->dev, "  BAR2 GLO_CFG: 0x%08x -> 0x%08x\n", before, after);
        if (after & (GLO_CFG_TX_DMA_EN | GLO_CFG_RX_DMA_EN))
            dev_info(&pdev->dev, "  *** DMA ENABLED via BAR2! ***\n");
    }

    dev_info(&pdev->dev, "\n=== Scan complete ===\n");
    return 0;
}

static void mt7927_find_remove(struct pci_dev *pdev)
{
    dev_info(&pdev->dev, "MT7927 finder unloaded\n");
}

static const struct pci_device_id mt7927_find_table[] = {
    { PCI_DEVICE(MT7927_VENDOR_ID, MT7927_DEVICE_ID) },
    { }
};
MODULE_DEVICE_TABLE(pci, mt7927_find_table);

static struct pci_driver mt7927_find_driver = {
    .name = "mt7927_find_wfdma",
    .id_table = mt7927_find_table,
    .probe = mt7927_find_probe,
    .remove = mt7927_find_remove,
};

module_pci_driver(mt7927_find_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("MT7927 Project");
MODULE_DESCRIPTION("MT7927 WFDMA Register Finder");
