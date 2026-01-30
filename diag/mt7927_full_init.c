// SPDX-License-Identifier: GPL-2.0
/*
 * MT7927 Full Initialization Sequence
 * 
 * Based on mt7925 driver probe sequence:
 * 1. Claim driver ownership (CLR_OWN)
 * 2. WFSYS reset
 * 3. Try to enable DMA
 */

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/io.h>
#include <linux/delay.h>

#define MT7927_VENDOR_ID    0x14c3
#define MT7927_DEVICE_ID    0x7927

/* Register offsets in BAR0 (via fixed_map) */
#define MT_CONN_ON_LPCTL_OFFSET    0xe0010
#define MT_WFSYS_SW_RST_B_OFFSET   0xf0140
#define MT_CONN_ON_MISC_OFFSET     0xe00f0

/* LPCTL bits */
#define PCIE_LPCR_HOST_SET_OWN     BIT(0)
#define PCIE_LPCR_HOST_CLR_OWN     BIT(1)
#define PCIE_LPCR_HOST_OWN_SYNC    BIT(2)

/* WFSYS reset */
#define MT_WFSYS_SW_RST_B_EN       BIT(0)

/* WPDMA */
#define MT_WFDMA0_GLO_CFG_TX_DMA_EN    BIT(0)
#define MT_WFDMA0_GLO_CFG_RX_DMA_EN    BIT(2)

struct mt7927_init {
    struct pci_dev *pdev;
    void __iomem *bar0;
    void __iomem *bar2;
};

static void print_state(struct mt7927_init *dev, const char *label)
{
    u32 lpctl, fw_status, wpdma, wfsys;

    lpctl = readl(dev->bar0 + MT_CONN_ON_LPCTL_OFFSET);
    fw_status = readl(dev->bar2 + 0x200);
    wpdma = readl(dev->bar2 + 0x208);
    wfsys = readl(dev->bar0 + MT_WFSYS_SW_RST_B_OFFSET);

    dev_info(&dev->pdev->dev, "%s:\n", label);
    dev_info(&dev->pdev->dev, "  LPCTL:     0x%08x (%s)\n", lpctl,
             (lpctl & PCIE_LPCR_HOST_OWN_SYNC) ? "FW owns" : "Driver owns");
    dev_info(&dev->pdev->dev, "  FW_STATUS: 0x%08x\n", fw_status);
    dev_info(&dev->pdev->dev, "  WPDMA_CFG: 0x%08x (TX=%d, RX=%d)\n", wpdma,
             (wpdma & MT_WFDMA0_GLO_CFG_TX_DMA_EN) ? 1 : 0,
             (wpdma & MT_WFDMA0_GLO_CFG_RX_DMA_EN) ? 1 : 0);
    dev_info(&dev->pdev->dev, "  WFSYS_RST: 0x%08x (EN=%d)\n", wfsys,
             (wfsys & MT_WFSYS_SW_RST_B_EN) ? 1 : 0);
}

static int claim_driver_own(struct mt7927_init *dev)
{
    u32 lpctl;
    int i;

    dev_info(&dev->pdev->dev, "Claiming driver ownership...\n");
    
    writel(PCIE_LPCR_HOST_CLR_OWN, dev->bar0 + MT_CONN_ON_LPCTL_OFFSET);

    for (i = 0; i < 100; i++) {
        lpctl = readl(dev->bar0 + MT_CONN_ON_LPCTL_OFFSET);
        if (!(lpctl & PCIE_LPCR_HOST_OWN_SYNC)) {
            dev_info(&dev->pdev->dev, "  Driver ownership claimed in %d ms\n", i);
            return 0;
        }
        msleep(1);
    }

    dev_err(&dev->pdev->dev, "Failed to claim driver ownership\n");
    return -ETIMEDOUT;
}

static int wfsys_reset(struct mt7927_init *dev)
{
    u32 val;

    dev_info(&dev->pdev->dev, "Performing WFSYS reset...\n");

    /* Assert reset */
    val = readl(dev->bar0 + MT_WFSYS_SW_RST_B_OFFSET);
    writel(val & ~MT_WFSYS_SW_RST_B_EN, dev->bar0 + MT_WFSYS_SW_RST_B_OFFSET);
    msleep(5);

    /* Deassert reset */
    val = readl(dev->bar0 + MT_WFSYS_SW_RST_B_OFFSET);
    writel(val | MT_WFSYS_SW_RST_B_EN, dev->bar0 + MT_WFSYS_SW_RST_B_OFFSET);
    msleep(50);

    val = readl(dev->bar0 + MT_WFSYS_SW_RST_B_OFFSET);
    dev_info(&dev->pdev->dev, "  WFSYS_RST after reset: 0x%08x\n", val);

    return 0;
}

static int try_enable_dma(struct mt7927_init *dev)
{
    u32 before, after;

    dev_info(&dev->pdev->dev, "Attempting to enable DMA...\n");

    before = readl(dev->bar2 + 0x208);
    dev_info(&dev->pdev->dev, "  WPDMA before: 0x%08x\n", before);

    /* Try to enable TX and RX DMA */
    writel(MT_WFDMA0_GLO_CFG_TX_DMA_EN | MT_WFDMA0_GLO_CFG_RX_DMA_EN,
           dev->bar2 + 0x208);
    udelay(100);

    after = readl(dev->bar2 + 0x208);
    dev_info(&dev->pdev->dev, "  WPDMA after:  0x%08x\n", after);

    if (after & (MT_WFDMA0_GLO_CFG_TX_DMA_EN | MT_WFDMA0_GLO_CFG_RX_DMA_EN)) {
        dev_info(&dev->pdev->dev, "  *** DMA ENABLED! ***\n");
        return 0;
    }

    dev_info(&dev->pdev->dev, "  DMA still not enabled\n");
    return -EIO;
}

static int mt7927_init_probe(struct pci_dev *pdev,
                              const struct pci_device_id *id)
{
    struct mt7927_init *dev;
    int ret;

    dev_info(&pdev->dev, "=== MT7927 Full Initialization Sequence ===\n");

    dev = devm_kzalloc(&pdev->dev, sizeof(*dev), GFP_KERNEL);
    if (!dev)
        return -ENOMEM;

    dev->pdev = pdev;
    pci_set_drvdata(pdev, dev);

    ret = pcim_enable_device(pdev);
    if (ret)
        return ret;

    pci_set_master(pdev);

    ret = pcim_iomap_regions(pdev, BIT(0) | BIT(2), "mt7927_init");
    if (ret)
        return ret;

    dev->bar0 = pcim_iomap_table(pdev)[0];
    dev->bar2 = pcim_iomap_table(pdev)[2];

    print_state(dev, "Initial state");

    /* Step 1: Claim driver ownership */
    dev_info(&pdev->dev, "\n--- Step 1: Claim driver ownership ---\n");
    ret = claim_driver_own(dev);
    if (ret)
        goto done;

    print_state(dev, "After claiming ownership");

    /* Step 2: WFSYS reset */
    dev_info(&pdev->dev, "\n--- Step 2: WFSYS reset ---\n");
    ret = wfsys_reset(dev);

    print_state(dev, "After WFSYS reset");

    /* Step 3: Try to enable DMA */
    dev_info(&pdev->dev, "\n--- Step 3: Enable DMA ---\n");
    ret = try_enable_dma(dev);

    print_state(dev, "Final state");

    if (ret == 0) {
        dev_info(&pdev->dev, "\n*** SUCCESS: Full initialization complete! ***\n");
        dev_info(&pdev->dev, "The chip is ready for firmware loading!\n");
    } else {
        dev_info(&pdev->dev, "\n*** DMA not enabled - may need firmware first ***\n");
    }

done:
    dev_info(&pdev->dev, "\n=== Test complete ===\n");
    return 0;
}

static void mt7927_init_remove(struct pci_dev *pdev)
{
    dev_info(&pdev->dev, "MT7927 init test unloaded\n");
}

static const struct pci_device_id mt7927_init_table[] = {
    { PCI_DEVICE(MT7927_VENDOR_ID, MT7927_DEVICE_ID) },
    { }
};
MODULE_DEVICE_TABLE(pci, mt7927_init_table);

static struct pci_driver mt7927_init_driver = {
    .name = "mt7927_full_init",
    .id_table = mt7927_init_table,
    .probe = mt7927_init_probe,
    .remove = mt7927_init_remove,
};

module_pci_driver(mt7927_init_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("MT7927 Project");
MODULE_DESCRIPTION("MT7927 Full Initialization Test");
