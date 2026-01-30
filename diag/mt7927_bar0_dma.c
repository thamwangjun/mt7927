// SPDX-License-Identifier: GPL-2.0
/*
 * MT7927 BAR0-based DMA Test
 * 
 * The mt7925 driver uses BAR0 for ALL register access, not BAR2.
 * This test uses BAR0 to access WFDMA registers.
 */

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/io.h>
#include <linux/delay.h>

#define MT7927_VENDOR_ID    0x14c3
#define MT7927_DEVICE_ID    0x7927

/* WFDMA register offsets - direct BAR0 access */
#define MT_WFDMA0_GLO_CFG          0x208
#define MT_WFDMA0_GLO_CFG_TX_DMA_EN     BIT(0)
#define MT_WFDMA0_GLO_CFG_TX_DMA_BUSY   BIT(1)
#define MT_WFDMA0_GLO_CFG_RX_DMA_EN     BIT(2)
#define MT_WFDMA0_GLO_CFG_RX_DMA_BUSY   BIT(3)
#define MT_WFDMA0_GLO_CFG_TX_WB_DDONE   BIT(6)
#define MT_WFDMA0_GLO_CFG_FIFO_LITTLE_ENDIAN BIT(12)

#define MT_WFDMA0_RST              0x100
#define MT_WFDMA0_RST_LOGIC_RST    BIT(4)
#define MT_WFDMA0_RST_DMASHDL_ALL  BIT(5)

#define MT_WFDMA0_HOST_INT_STA     0x200
#define MT_WFDMA0_RST_DTX_PTR      0x20c

/* Power control in BAR0 */
#define MT_CONN_ON_LPCTL           0xe0010
#define PCIE_LPCR_HOST_CLR_OWN     BIT(1)
#define PCIE_LPCR_HOST_OWN_SYNC    BIT(2)

struct mt7927_bar0_test {
    struct pci_dev *pdev;
    void __iomem *bar0;
    void __iomem *bar2;
};

static void compare_bars(struct mt7927_bar0_test *dev, u32 offset, const char *name)
{
    u32 bar0_val, bar2_val;

    bar0_val = readl(dev->bar0 + offset);
    bar2_val = readl(dev->bar2 + offset);

    dev_info(&dev->pdev->dev, "  [0x%03x] %s: BAR0=0x%08x, BAR2=0x%08x %s\n",
             offset, name, bar0_val, bar2_val,
             (bar0_val != bar2_val) ? " <-- DIFFERENT!" : "");
}

static void dump_both_bars(struct mt7927_bar0_test *dev)
{
    dev_info(&dev->pdev->dev, "Comparing BAR0 vs BAR2 WFDMA registers:\n");
    compare_bars(dev, 0x000, "Chip ID    ");
    compare_bars(dev, 0x004, "HW Rev     ");
    compare_bars(dev, 0x100, "WFDMA_RST  ");
    compare_bars(dev, 0x200, "HOST_INT   ");
    compare_bars(dev, 0x204, "INT_ENA    ");
    compare_bars(dev, 0x208, "GLO_CFG    ");
    compare_bars(dev, 0x20c, "RST_DTX    ");
    compare_bars(dev, 0x300, "TX0_BASE   ");
    compare_bars(dev, 0x500, "RX0_BASE   ");
}

static int claim_driver_own(struct mt7927_bar0_test *dev)
{
    u32 lpctl;
    int i;

    lpctl = readl(dev->bar0 + MT_CONN_ON_LPCTL);
    if (!(lpctl & PCIE_LPCR_HOST_OWN_SYNC)) {
        dev_info(&dev->pdev->dev, "Driver already owns chip (LPCTL=0x%08x)\n", lpctl);
        return 0;
    }

    dev_info(&dev->pdev->dev, "Claiming driver ownership...\n");
    writel(PCIE_LPCR_HOST_CLR_OWN, dev->bar0 + MT_CONN_ON_LPCTL);

    for (i = 0; i < 100; i++) {
        lpctl = readl(dev->bar0 + MT_CONN_ON_LPCTL);
        if (!(lpctl & PCIE_LPCR_HOST_OWN_SYNC)) {
            dev_info(&dev->pdev->dev, "  Claimed in %d ms\n", i);
            return 0;
        }
        msleep(1);
    }

    return -ETIMEDOUT;
}

static void dma_reset_bar0(struct mt7927_bar0_test *dev)
{
    u32 val;

    dev_info(&dev->pdev->dev, "Performing DMA reset via BAR0...\n");

    val = readl(dev->bar0 + MT_WFDMA0_RST);
    dev_info(&dev->pdev->dev, "  RST before: 0x%08x\n", val);

    /* Clear then set reset bits */
    val &= ~(MT_WFDMA0_RST_DMASHDL_ALL | MT_WFDMA0_RST_LOGIC_RST);
    writel(val, dev->bar0 + MT_WFDMA0_RST);
    udelay(100);

    val = readl(dev->bar0 + MT_WFDMA0_RST);
    dev_info(&dev->pdev->dev, "  RST after clear: 0x%08x\n", val);

    val |= MT_WFDMA0_RST_DMASHDL_ALL | MT_WFDMA0_RST_LOGIC_RST;
    writel(val, dev->bar0 + MT_WFDMA0_RST);
    udelay(100);

    val = readl(dev->bar0 + MT_WFDMA0_RST);
    dev_info(&dev->pdev->dev, "  RST after set: 0x%08x\n", val);
}

static int try_enable_dma_bar0(struct mt7927_bar0_test *dev)
{
    u32 val;

    dev_info(&dev->pdev->dev, "Enabling DMA via BAR0...\n");

    /* Reset DMA pointers first */
    writel(~0, dev->bar0 + MT_WFDMA0_RST_DTX_PTR);

    /* Read current GLO_CFG */
    val = readl(dev->bar0 + MT_WFDMA0_GLO_CFG);
    dev_info(&dev->pdev->dev, "  GLO_CFG before: 0x%08x\n", val);

    /* Configure and enable */
    val = MT_WFDMA0_GLO_CFG_TX_WB_DDONE |
          MT_WFDMA0_GLO_CFG_FIFO_LITTLE_ENDIAN |
          MT_WFDMA0_GLO_CFG_TX_DMA_EN |
          MT_WFDMA0_GLO_CFG_RX_DMA_EN;

    writel(val, dev->bar0 + MT_WFDMA0_GLO_CFG);
    wmb();
    udelay(100);

    val = readl(dev->bar0 + MT_WFDMA0_GLO_CFG);
    dev_info(&dev->pdev->dev, "  GLO_CFG after: 0x%08x\n", val);

    if (val & (MT_WFDMA0_GLO_CFG_TX_DMA_EN | MT_WFDMA0_GLO_CFG_RX_DMA_EN)) {
        dev_info(&dev->pdev->dev, "  *** DMA ENABLED via BAR0! ***\n");
        return 0;
    }

    dev_info(&dev->pdev->dev, "  DMA still not enabled\n");
    return -EIO;
}

static int mt7927_bar0_probe(struct pci_dev *pdev,
                              const struct pci_device_id *id)
{
    struct mt7927_bar0_test *dev;
    int ret;

    dev_info(&pdev->dev, "=== MT7927 BAR0-based DMA Test ===\n");

    dev = devm_kzalloc(&pdev->dev, sizeof(*dev), GFP_KERNEL);
    if (!dev)
        return -ENOMEM;

    dev->pdev = pdev;
    pci_set_drvdata(pdev, dev);

    ret = pcim_enable_device(pdev);
    if (ret)
        return ret;

    pci_set_master(pdev);

    ret = pcim_iomap_regions(pdev, BIT(0) | BIT(2), "mt7927_bar0");
    if (ret)
        return ret;

    dev->bar0 = pcim_iomap_table(pdev)[0];
    dev->bar2 = pcim_iomap_table(pdev)[2];

    dev_info(&pdev->dev, "BAR0: %pR (size: 0x%llx)\n",
             &pdev->resource[0], pci_resource_len(pdev, 0));
    dev_info(&pdev->dev, "BAR2: %pR (size: 0x%llx)\n",
             &pdev->resource[2], pci_resource_len(pdev, 2));

    /* First, compare what BAR0 vs BAR2 show */
    dump_both_bars(dev);

    /* Claim driver ownership */
    ret = claim_driver_own(dev);
    if (ret) {
        dev_err(&pdev->dev, "Failed to claim ownership\n");
        goto done;
    }

    /* DMA reset via BAR0 */
    dma_reset_bar0(dev);

    /* Try to enable DMA via BAR0 */
    ret = try_enable_dma_bar0(dev);

    dev_info(&pdev->dev, "\nFinal state (both BARs):\n");
    compare_bars(dev, 0x208, "GLO_CFG");
    compare_bars(dev, 0x200, "FW_STATUS");

    if (ret == 0)
        dev_info(&pdev->dev, "\n*** SUCCESS: DMA enabled! ***\n");

done:
    dev_info(&pdev->dev, "\n=== Test complete ===\n");
    return 0;
}

static void mt7927_bar0_remove(struct pci_dev *pdev)
{
    dev_info(&pdev->dev, "MT7927 BAR0 test unloaded\n");
}

static const struct pci_device_id mt7927_bar0_table[] = {
    { PCI_DEVICE(MT7927_VENDOR_ID, MT7927_DEVICE_ID) },
    { }
};
MODULE_DEVICE_TABLE(pci, mt7927_bar0_table);

static struct pci_driver mt7927_bar0_driver = {
    .name = "mt7927_bar0_dma",
    .id_table = mt7927_bar0_table,
    .probe = mt7927_bar0_probe,
    .remove = mt7927_bar0_remove,
};

module_pci_driver(mt7927_bar0_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("MT7927 Project");
MODULE_DESCRIPTION("MT7927 BAR0-based DMA Test");
