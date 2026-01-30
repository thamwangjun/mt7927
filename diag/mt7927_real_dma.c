// SPDX-License-Identifier: GPL-2.0
/*
 * MT7927 Real WFDMA Enable Test
 * 
 * Discovery: The REAL WFDMA registers are at BAR0 + 0x2000, not BAR2!
 * - BAR0[0x2208] = GLO_CFG = 0x1010b870 (real config)
 * - BAR2[0x208] = 0x00000000 (shadow/status only)
 * 
 * This module writes to the correct address.
 */

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/io.h>
#include <linux/delay.h>

#define MT7927_VENDOR_ID    0x14c3
#define MT7927_DEVICE_ID    0x7927

/* Real WFDMA base in BAR0 */
#define WFDMA_REAL_BASE     0x2000

/* WFDMA register offsets (relative to WFDMA base) */
#define WFDMA_RST           0x100
#define WFDMA_HOST_INT      0x200
#define WFDMA_INT_ENA       0x204
#define WFDMA_GLO_CFG       0x208
#define WFDMA_RST_DTX       0x20c
#define WFDMA_TX0_BASE      0x300
#define WFDMA_TX0_CNT       0x304

/* GLO_CFG bits */
#define GLO_CFG_TX_DMA_EN       BIT(0)
#define GLO_CFG_TX_DMA_BUSY     BIT(1)
#define GLO_CFG_RX_DMA_EN       BIT(2)
#define GLO_CFG_RX_DMA_BUSY     BIT(3)

/* Power control */
#define MT_CONN_ON_LPCTL        0xe0010
#define PCIE_LPCR_HOST_CLR_OWN  BIT(1)
#define PCIE_LPCR_HOST_OWN_SYNC BIT(2)

struct mt7927_real {
    struct pci_dev *pdev;
    void __iomem *bar0;
};

/* Helper to read from real WFDMA */
static inline u32 wfdma_read(struct mt7927_real *dev, u32 offset)
{
    return readl(dev->bar0 + WFDMA_REAL_BASE + offset);
}

/* Helper to write to real WFDMA */
static inline void wfdma_write(struct mt7927_real *dev, u32 offset, u32 val)
{
    writel(val, dev->bar0 + WFDMA_REAL_BASE + offset);
}

static void dump_wfdma_state(struct mt7927_real *dev, const char *label)
{
    u32 glo_cfg, rst, host_int, tx0_cnt;

    glo_cfg = wfdma_read(dev, WFDMA_GLO_CFG);
    rst = wfdma_read(dev, WFDMA_RST);
    host_int = wfdma_read(dev, WFDMA_HOST_INT);
    tx0_cnt = wfdma_read(dev, WFDMA_TX0_CNT);

    dev_info(&dev->pdev->dev, "%s:\n", label);
    dev_info(&dev->pdev->dev, "  GLO_CFG:  0x%08x (TX_EN=%d, RX_EN=%d, TX_BUSY=%d, RX_BUSY=%d)\n",
             glo_cfg,
             (glo_cfg & GLO_CFG_TX_DMA_EN) ? 1 : 0,
             (glo_cfg & GLO_CFG_RX_DMA_EN) ? 1 : 0,
             (glo_cfg & GLO_CFG_TX_DMA_BUSY) ? 1 : 0,
             (glo_cfg & GLO_CFG_RX_DMA_BUSY) ? 1 : 0);
    dev_info(&dev->pdev->dev, "  RST:      0x%08x\n", rst);
    dev_info(&dev->pdev->dev, "  HOST_INT: 0x%08x\n", host_int);
    dev_info(&dev->pdev->dev, "  TX0_CNT:  0x%08x\n", tx0_cnt);
}

static int claim_driver_own(struct mt7927_real *dev)
{
    u32 lpctl;
    int i;

    lpctl = readl(dev->bar0 + MT_CONN_ON_LPCTL);
    if (!(lpctl & PCIE_LPCR_HOST_OWN_SYNC)) {
        dev_info(&dev->pdev->dev, "Driver already owns chip\n");
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

    dev_err(&dev->pdev->dev, "Failed to claim ownership\n");
    return -ETIMEDOUT;
}

static int enable_dma(struct mt7927_real *dev)
{
    u32 before, after;

    dev_info(&dev->pdev->dev, "\nEnabling DMA at real WFDMA (BAR0+0x%x)...\n",
             WFDMA_REAL_BASE + WFDMA_GLO_CFG);

    before = wfdma_read(dev, WFDMA_GLO_CFG);
    dev_info(&dev->pdev->dev, "  GLO_CFG before: 0x%08x\n", before);

    /* Set TX_DMA_EN and RX_DMA_EN bits while preserving other config */
    after = before | GLO_CFG_TX_DMA_EN | GLO_CFG_RX_DMA_EN;
    dev_info(&dev->pdev->dev, "  Writing:        0x%08x\n", after);

    wfdma_write(dev, WFDMA_GLO_CFG, after);
    wmb();
    udelay(100);

    after = wfdma_read(dev, WFDMA_GLO_CFG);
    dev_info(&dev->pdev->dev, "  GLO_CFG after:  0x%08x\n", after);

    if ((after & (GLO_CFG_TX_DMA_EN | GLO_CFG_RX_DMA_EN)) == 
        (GLO_CFG_TX_DMA_EN | GLO_CFG_RX_DMA_EN)) {
        dev_info(&dev->pdev->dev, "\n  *** SUCCESS: DMA ENABLED! ***\n");
        return 0;
    }

    /* Check if only some bits stuck */
    if (after != before) {
        dev_info(&dev->pdev->dev, "  Partial success: register changed\n");
        dev_info(&dev->pdev->dev, "  TX_EN: %d, RX_EN: %d\n",
                 (after & GLO_CFG_TX_DMA_EN) ? 1 : 0,
                 (after & GLO_CFG_RX_DMA_EN) ? 1 : 0);
        return 0;
    }

    dev_info(&dev->pdev->dev, "  DMA enable failed - register unchanged\n");
    return -EIO;
}

static int mt7927_real_probe(struct pci_dev *pdev,
                              const struct pci_device_id *id)
{
    struct mt7927_real *dev;
    int ret;

    dev_info(&pdev->dev, "=== MT7927 Real WFDMA Enable Test ===\n");
    dev_info(&pdev->dev, "Target: BAR0 + 0x%x (real WFDMA GLO_CFG)\n",
             WFDMA_REAL_BASE + WFDMA_GLO_CFG);

    dev = devm_kzalloc(&pdev->dev, sizeof(*dev), GFP_KERNEL);
    if (!dev)
        return -ENOMEM;

    dev->pdev = pdev;
    pci_set_drvdata(pdev, dev);

    ret = pcim_enable_device(pdev);
    if (ret)
        return ret;

    pci_set_master(pdev);

    ret = pcim_iomap_regions(pdev, BIT(0), "mt7927_real");
    if (ret)
        return ret;

    dev->bar0 = pcim_iomap_table(pdev)[0];

    /* Dump initial state */
    dump_wfdma_state(dev, "Initial WFDMA state");

    /* Ensure driver owns the chip */
    ret = claim_driver_own(dev);
    if (ret)
        goto done;

    /* Try to enable DMA */
    ret = enable_dma(dev);

    /* Dump final state */
    dump_wfdma_state(dev, "Final WFDMA state");

done:
    dev_info(&pdev->dev, "\n=== Test complete ===\n");
    return 0;
}

static void mt7927_real_remove(struct pci_dev *pdev)
{
    dev_info(&pdev->dev, "MT7927 real WFDMA test unloaded\n");
}

static const struct pci_device_id mt7927_real_table[] = {
    { PCI_DEVICE(MT7927_VENDOR_ID, MT7927_DEVICE_ID) },
    { }
};
MODULE_DEVICE_TABLE(pci, mt7927_real_table);

static struct pci_driver mt7927_real_driver = {
    .name = "mt7927_real_dma",
    .id_table = mt7927_real_table,
    .probe = mt7927_real_probe,
    .remove = mt7927_real_remove,
};

module_pci_driver(mt7927_real_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("MT7927 Project");
MODULE_DESCRIPTION("MT7927 Real WFDMA Enable Test");
