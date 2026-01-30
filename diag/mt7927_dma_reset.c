// SPDX-License-Identifier: GPL-2.0
/*
 * MT7927 DMA Reset and Enable Test
 * 
 * Performs DMA logic reset before enabling DMA,
 * following the mt792x_dma_disable sequence.
 */

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/io.h>
#include <linux/delay.h>

#define MT7927_VENDOR_ID    0x14c3
#define MT7927_DEVICE_ID    0x7927

/* Register offsets - all in BAR2 */
#define MT_WFDMA0_RST              0x100
#define MT_WFDMA0_RST_LOGIC_RST    BIT(4)
#define MT_WFDMA0_RST_DMASHDL_ALL  BIT(5)

#define MT_WFDMA0_GLO_CFG          0x208
#define MT_WFDMA0_GLO_CFG_TX_DMA_EN     BIT(0)
#define MT_WFDMA0_GLO_CFG_TX_DMA_BUSY   BIT(1)
#define MT_WFDMA0_GLO_CFG_RX_DMA_EN     BIT(2)
#define MT_WFDMA0_GLO_CFG_RX_DMA_BUSY   BIT(3)
#define MT_WFDMA0_GLO_CFG_TX_WB_DDONE   BIT(6)
#define MT_WFDMA0_GLO_CFG_FIFO_LITTLE_ENDIAN BIT(12)

#define MT_WFDMA0_RST_DTX_PTR      0x20c
#define MT_WFDMA0_RST_DRX_PTR      0x280

/* BAR0 offsets */
#define MT_CONN_ON_LPCTL_OFFSET    0xe0010
#define PCIE_LPCR_HOST_CLR_OWN     BIT(1)
#define PCIE_LPCR_HOST_OWN_SYNC    BIT(2)

struct mt7927_dma_test {
    struct pci_dev *pdev;
    void __iomem *bar0;
    void __iomem *bar2;
};

static void print_dma_state(struct mt7927_dma_test *dev, const char *label)
{
    u32 glo_cfg, rst, fw_status;

    glo_cfg = readl(dev->bar2 + MT_WFDMA0_GLO_CFG);
    rst = readl(dev->bar2 + MT_WFDMA0_RST);
    fw_status = readl(dev->bar2 + 0x200);

    dev_info(&dev->pdev->dev, "%s:\n", label);
    dev_info(&dev->pdev->dev, "  GLO_CFG: 0x%08x (TX_EN=%d, RX_EN=%d)\n",
             glo_cfg,
             (glo_cfg & MT_WFDMA0_GLO_CFG_TX_DMA_EN) ? 1 : 0,
             (glo_cfg & MT_WFDMA0_GLO_CFG_RX_DMA_EN) ? 1 : 0);
    dev_info(&dev->pdev->dev, "  RST:     0x%08x (LOGIC=%d, SHDL=%d)\n",
             rst,
             (rst & MT_WFDMA0_RST_LOGIC_RST) ? 1 : 0,
             (rst & MT_WFDMA0_RST_DMASHDL_ALL) ? 1 : 0);
    dev_info(&dev->pdev->dev, "  FW_STA:  0x%08x\n", fw_status);
}

static int claim_driver_own(struct mt7927_dma_test *dev)
{
    u32 lpctl;
    int i;

    lpctl = readl(dev->bar0 + MT_CONN_ON_LPCTL_OFFSET);
    if (!(lpctl & PCIE_LPCR_HOST_OWN_SYNC)) {
        dev_info(&dev->pdev->dev, "Driver already owns chip\n");
        return 0;
    }

    dev_info(&dev->pdev->dev, "Claiming driver ownership...\n");
    writel(PCIE_LPCR_HOST_CLR_OWN, dev->bar0 + MT_CONN_ON_LPCTL_OFFSET);

    for (i = 0; i < 100; i++) {
        lpctl = readl(dev->bar0 + MT_CONN_ON_LPCTL_OFFSET);
        if (!(lpctl & PCIE_LPCR_HOST_OWN_SYNC)) {
            dev_info(&dev->pdev->dev, "  Claimed in %d ms\n", i);
            return 0;
        }
        msleep(1);
    }

    dev_err(&dev->pdev->dev, "Failed to claim ownership\n");
    return -ETIMEDOUT;
}

static void dma_reset(struct mt7927_dma_test *dev)
{
    u32 val;

    dev_info(&dev->pdev->dev, "Performing DMA logic reset...\n");

    /* Step 1: Clear reset bits */
    val = readl(dev->bar2 + MT_WFDMA0_RST);
    dev_info(&dev->pdev->dev, "  RST before: 0x%08x\n", val);
    
    val &= ~(MT_WFDMA0_RST_DMASHDL_ALL | MT_WFDMA0_RST_LOGIC_RST);
    writel(val, dev->bar2 + MT_WFDMA0_RST);
    udelay(100);

    val = readl(dev->bar2 + MT_WFDMA0_RST);
    dev_info(&dev->pdev->dev, "  RST after clear: 0x%08x\n", val);

    /* Step 2: Set reset bits */
    val |= MT_WFDMA0_RST_DMASHDL_ALL | MT_WFDMA0_RST_LOGIC_RST;
    writel(val, dev->bar2 + MT_WFDMA0_RST);
    udelay(100);

    val = readl(dev->bar2 + MT_WFDMA0_RST);
    dev_info(&dev->pdev->dev, "  RST after set: 0x%08x\n", val);
}

static void reset_dma_pointers(struct mt7927_dma_test *dev)
{
    dev_info(&dev->pdev->dev, "Resetting DMA pointers...\n");
    writel(~0, dev->bar2 + MT_WFDMA0_RST_DTX_PTR);
    writel(~0, dev->bar2 + MT_WFDMA0_RST_DRX_PTR);
}

static int try_enable_dma(struct mt7927_dma_test *dev)
{
    u32 val, cfg;

    dev_info(&dev->pdev->dev, "Enabling DMA...\n");

    /* Configure GLO_CFG with required settings */
    cfg = MT_WFDMA0_GLO_CFG_TX_WB_DDONE |
          MT_WFDMA0_GLO_CFG_FIFO_LITTLE_ENDIAN;

    val = readl(dev->bar2 + MT_WFDMA0_GLO_CFG);
    val |= cfg;
    writel(val, dev->bar2 + MT_WFDMA0_GLO_CFG);
    udelay(100);

    val = readl(dev->bar2 + MT_WFDMA0_GLO_CFG);
    dev_info(&dev->pdev->dev, "  GLO_CFG after config: 0x%08x\n", val);

    /* Now enable TX/RX DMA */
    val |= MT_WFDMA0_GLO_CFG_TX_DMA_EN | MT_WFDMA0_GLO_CFG_RX_DMA_EN;
    writel(val, dev->bar2 + MT_WFDMA0_GLO_CFG);
    udelay(100);

    val = readl(dev->bar2 + MT_WFDMA0_GLO_CFG);
    dev_info(&dev->pdev->dev, "  GLO_CFG after enable: 0x%08x\n", val);

    if (val & (MT_WFDMA0_GLO_CFG_TX_DMA_EN | MT_WFDMA0_GLO_CFG_RX_DMA_EN)) {
        dev_info(&dev->pdev->dev, "  *** DMA ENABLED! ***\n");
        return 0;
    }

    dev_info(&dev->pdev->dev, "  DMA still not enabled\n");
    return -EIO;
}

static int mt7927_dma_test_probe(struct pci_dev *pdev,
                                  const struct pci_device_id *id)
{
    struct mt7927_dma_test *dev;
    int ret;

    dev_info(&pdev->dev, "=== MT7927 DMA Reset and Enable Test ===\n");

    dev = devm_kzalloc(&pdev->dev, sizeof(*dev), GFP_KERNEL);
    if (!dev)
        return -ENOMEM;

    dev->pdev = pdev;
    pci_set_drvdata(pdev, dev);

    ret = pcim_enable_device(pdev);
    if (ret)
        return ret;

    pci_set_master(pdev);

    ret = pcim_iomap_regions(pdev, BIT(0) | BIT(2), "mt7927_dma_test");
    if (ret)
        return ret;

    dev->bar0 = pcim_iomap_table(pdev)[0];
    dev->bar2 = pcim_iomap_table(pdev)[2];

    print_dma_state(dev, "Initial state");

    /* Step 1: Ensure driver owns the chip */
    ret = claim_driver_own(dev);
    if (ret)
        goto done;

    /* Step 2: DMA logic reset */
    dma_reset(dev);

    /* Step 3: Reset DMA pointers */
    reset_dma_pointers(dev);

    print_dma_state(dev, "After DMA reset");

    /* Step 4: Try to enable DMA */
    ret = try_enable_dma(dev);

    print_dma_state(dev, "Final state");

    if (ret == 0) {
        dev_info(&pdev->dev, "\n*** SUCCESS: DMA is now enabled! ***\n");
    }

done:
    dev_info(&pdev->dev, "\n=== Test complete ===\n");
    return 0;
}

static void mt7927_dma_test_remove(struct pci_dev *pdev)
{
    dev_info(&pdev->dev, "MT7927 DMA test unloaded\n");
}

static const struct pci_device_id mt7927_dma_test_table[] = {
    { PCI_DEVICE(MT7927_VENDOR_ID, MT7927_DEVICE_ID) },
    { }
};
MODULE_DEVICE_TABLE(pci, mt7927_dma_test_table);

static struct pci_driver mt7927_dma_test_driver = {
    .name = "mt7927_dma_reset",
    .id_table = mt7927_dma_test_table,
    .probe = mt7927_dma_test_probe,
    .remove = mt7927_dma_test_remove,
};

module_pci_driver(mt7927_dma_test_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("MT7927 Project");
MODULE_DESCRIPTION("MT7927 DMA Reset and Enable Test");
