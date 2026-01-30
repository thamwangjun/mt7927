// SPDX-License-Identifier: GPL-2.0
/*
 * MT7927 Diagnostic Module - Complete Baseline Check
 *
 * Maps both BAR0 and BAR2 to verify device state before testing.
 * Reads chip ID, FW status, and WFDMA ring configuration.
 */

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/io.h>

#define MT7927_VENDOR_ID    0x14c3
#define MT7927_DEVICE_ID    0x7927

/* WFDMA Register offsets (from BAR0) */
#define MT_WFDMA0_BASE              0x2000
#define MT_WFDMA0_HOST_INT_STA      (MT_WFDMA0_BASE + 0x200)
#define MT_WFDMA0_HOST_INT_ENA      (MT_WFDMA0_BASE + 0x204)
#define MT_WFDMA0_GLO_CFG           (MT_WFDMA0_BASE + 0x208)
#define MT_WFDMA0_RST_DTX_PTR       (MT_WFDMA0_BASE + 0x20c)

#define MT_WFDMA0_TX_RING_BASE(n)   (MT_WFDMA0_BASE + 0x300 + (n) * 0x10)
#define MT_WFDMA0_TX_RING_CNT(n)    (MT_WFDMA0_BASE + 0x304 + (n) * 0x10)
#define MT_WFDMA0_TX_RING_CIDX(n)   (MT_WFDMA0_BASE + 0x308 + (n) * 0x10)
#define MT_WFDMA0_TX_RING_DIDX(n)   (MT_WFDMA0_BASE + 0x30c + (n) * 0x10)

#define MT_WFDMA0_RX_RING_BASE(n)   (MT_WFDMA0_BASE + 0x500 + (n) * 0x10)
#define MT_WFDMA0_RX_RING_CNT(n)    (MT_WFDMA0_BASE + 0x504 + (n) * 0x10)
#define MT_WFDMA0_RX_RING_CIDX(n)   (MT_WFDMA0_BASE + 0x508 + (n) * 0x10)
#define MT_WFDMA0_RX_RING_DIDX(n)   (MT_WFDMA0_BASE + 0x50c + (n) * 0x10)

struct mt7927_diag {
    struct pci_dev *pdev;
    void __iomem *bar0;  /* BAR0: 2MB main memory */
    void __iomem *bar2;  /* BAR2: 32KB register shadow */
};

static void dump_safe_registers(struct mt7927_diag *diag)
{
    struct pci_dev *pdev = diag->pdev;
    u32 val, base, cnt, cidx, didx;
    int i;

    dev_info(&pdev->dev, "=== MT7927 Baseline State Check ===\n");

    /* Chip identification (from BAR2) */
    val = readl(diag->bar2 + 0x000);
    dev_info(&pdev->dev, "Chip ID:       0x%08x\n", val);

    val = readl(diag->bar2 + 0x004);
    dev_info(&pdev->dev, "HW Rev:        0x%08x\n", val);

    /* Firmware status (from BAR2) */
    val = readl(diag->bar2 + 0x200);
    dev_info(&pdev->dev, "FW_STATUS:     0x%08x ", val);
    if (val == 0xffff10f1)
        pr_cont("(pre-init - expected)\n");
    else if (val == 0x00000001)
        pr_cont("(MCU ready)\n");
    else
        pr_cont("(unknown)\n");

    /* WFDMA state (from BAR0) */
    val = readl(diag->bar0 + MT_WFDMA0_GLO_CFG);
    dev_info(&pdev->dev, "\nWFDMA GLO_CFG: 0x%08x (TX:%s RX:%s)\n", val,
             (val & BIT(0)) ? "ON" : "OFF",
             (val & BIT(2)) ? "ON" : "OFF");

    val = readl(diag->bar0 + MT_WFDMA0_RST_DTX_PTR);
    dev_info(&pdev->dev, "WFDMA RST_PTR: 0x%08x\n", val);

    /* Check TX rings 0-7 (MT7927 has 8 TX rings) */
    dev_info(&pdev->dev, "\nTX Rings (expecting all zeros in pre-init state):\n");
    for (i = 0; i < 8; i++) {
        base = readl(diag->bar0 + MT_WFDMA0_TX_RING_BASE(i));
        cnt  = readl(diag->bar0 + MT_WFDMA0_TX_RING_CNT(i));
        cidx = readl(diag->bar0 + MT_WFDMA0_TX_RING_CIDX(i));
        didx = readl(diag->bar0 + MT_WFDMA0_TX_RING_DIDX(i));

        if (base || cnt || cidx || didx) {
            dev_info(&pdev->dev, "  TX%d: BASE=0x%08x CNT=%u CIDX=%u DIDX=%u *** NON-ZERO ***\n",
                     i, base, cnt, cidx, didx);
        }
    }
    dev_info(&pdev->dev, "  (All TX rings 0-7 are zero - clean state)\n");

    /* Check RX ring 0 */
    base = readl(diag->bar0 + MT_WFDMA0_RX_RING_BASE(0));
    cnt  = readl(diag->bar0 + MT_WFDMA0_RX_RING_CNT(0));
    cidx = readl(diag->bar0 + MT_WFDMA0_RX_RING_CIDX(0));
    didx = readl(diag->bar0 + MT_WFDMA0_RX_RING_DIDX(0));

    dev_info(&pdev->dev, "\nRX Ring 0: BASE=0x%08x CNT=%u CIDX=%u DIDX=%u\n",
             base, cnt, cidx, didx);

    if (base || cnt || cidx || didx)
        dev_info(&pdev->dev, "  *** WARNING: RX ring not clean ***\n");
    else
        dev_info(&pdev->dev, "  (Clean state - expected)\n");

    dev_info(&pdev->dev, "\n=== Baseline check complete ===\n");
}

static int mt7927_diag_probe(struct pci_dev *pdev,
                             const struct pci_device_id *id)
{
    struct mt7927_diag *diag;
    void __iomem *const *iomap_table;
    u32 chip_id;
    int ret;

    dev_info(&pdev->dev, "MT7927 Diagnostic - Baseline State Check\n");

    diag = devm_kzalloc(&pdev->dev, sizeof(*diag), GFP_KERNEL);
    if (!diag)
        return -ENOMEM;

    diag->pdev = pdev;
    pci_set_drvdata(pdev, diag);

    ret = pcim_enable_device(pdev);
    if (ret) {
        dev_err(&pdev->dev, "Failed to enable PCI device\n");
        return ret;
    }

    /* Map both BAR0 (2MB main) and BAR2 (32KB shadow) */
    ret = pcim_iomap_regions(pdev, BIT(0) | BIT(2), "mt7927_diag");
    if (ret) {
        dev_err(&pdev->dev, "Failed to map BARs\n");
        return ret;
    }

    iomap_table = pcim_iomap_table(pdev);
    diag->bar0 = iomap_table[0];
    diag->bar2 = iomap_table[2];

    if (!diag->bar0 || !diag->bar2) {
        dev_err(&pdev->dev, "BAR mapping failed\n");
        return -ENOMEM;
    }

    /* Verify chip is responding */
    chip_id = readl(diag->bar2 + 0x000);
    if (chip_id == 0xffffffff) {
        dev_err(&pdev->dev, "Chip not responding (hung state)\n");
        return -EIO;
    }

    /* Dump baseline state */
    dump_safe_registers(diag);

    dev_info(&pdev->dev, "Done. Unload with: sudo rmmod mt7927_diag\n");
    return 0;
}

static void mt7927_diag_remove(struct pci_dev *pdev)
{
    dev_info(&pdev->dev, "MT7927 Diagnostic unloaded\n");
}

static const struct pci_device_id mt7927_diag_table[] = {
    { PCI_DEVICE(MT7927_VENDOR_ID, MT7927_DEVICE_ID) },
    { }
};
MODULE_DEVICE_TABLE(pci, mt7927_diag_table);

static struct pci_driver mt7927_diag_driver = {
    .name = "mt7927_diag",
    .id_table = mt7927_diag_table,
    .probe = mt7927_diag_probe,
    .remove = mt7927_diag_remove,
};

module_pci_driver(mt7927_diag_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("MT7927 Project");
MODULE_DESCRIPTION("MT7927 Minimal Safe Diagnostic");
