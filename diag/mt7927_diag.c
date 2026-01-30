// SPDX-License-Identifier: GPL-2.0
/*
 * MT7927 Diagnostic Module - ULTRA MINIMAL VERSION
 * 
 * Only reads BAR2 registers that are known to be safe.
 * Does NOT scan BAR0 or access potentially dangerous regions.
 */

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/io.h>

#define MT7927_VENDOR_ID    0x14c3
#define MT7927_DEVICE_ID    0x7927

struct mt7927_diag {
    struct pci_dev *pdev;
    void __iomem *bar2;  /* BAR2: 32KB registers - SAFE */
};

static void dump_safe_registers(struct mt7927_diag *diag)
{
    struct pci_dev *pdev = diag->pdev;
    u32 val;

    dev_info(&pdev->dev, "=== MT7927 Safe Register Dump (BAR2 only) ===\n");

    /* These registers were confirmed safe in first run */
    val = readl(diag->bar2 + 0x000);
    dev_info(&pdev->dev, "  [0x000] Chip ID:       0x%08x\n", val);
    
    val = readl(diag->bar2 + 0x004);
    dev_info(&pdev->dev, "  [0x004] HW Rev:        0x%08x\n", val);

    val = readl(diag->bar2 + 0x200);
    dev_info(&pdev->dev, "  [0x200] HOST_INT_STA:  0x%08x (FW_STATUS)\n", val);

    val = readl(diag->bar2 + 0x204);
    dev_info(&pdev->dev, "  [0x204] HOST_INT_ENA:  0x%08x\n", val);

    val = readl(diag->bar2 + 0x208);
    dev_info(&pdev->dev, "  [0x208] WPDMA_GLO_CFG: 0x%08x\n", val);

    val = readl(diag->bar2 + 0x20c);
    dev_info(&pdev->dev, "  [0x20c] RST_DTX_PTR:   0x%08x\n", val);

    /* TX Ring 0 */
    dev_info(&pdev->dev, "  [0x300] TX0_BASE:      0x%08x\n", readl(diag->bar2 + 0x300));
    dev_info(&pdev->dev, "  [0x304] TX0_CNT:       0x%08x\n", readl(diag->bar2 + 0x304));

    /* TX Ring 16 (FWDL) */
    dev_info(&pdev->dev, "  [0x400] TX16_BASE:     0x%08x\n", readl(diag->bar2 + 0x400));

    /* RX Ring 0 */
    dev_info(&pdev->dev, "  [0x500] RX0_BASE:      0x%08x\n", readl(diag->bar2 + 0x500));

    /* Decode FW_STATUS */
    val = readl(diag->bar2 + 0x200);
    dev_info(&pdev->dev, "\nFW_STATUS = 0x%08x:\n", val);
    if (val == 0xffff10f1)
        dev_info(&pdev->dev, "  -> Pre-init state (chip locked, needs unlock sequence)\n");
    else if ((val & 0xFFFF0000) == 0xFFFF0000)
        dev_info(&pdev->dev, "  -> Error/pre-init (upper=0xFFFF)\n");
    else if (val == 0x00000001)
        dev_info(&pdev->dev, "  -> MCU ready!\n");
    else
        dev_info(&pdev->dev, "  -> Unknown state\n");

    /* WPDMA state */
    val = readl(diag->bar2 + 0x208);
    dev_info(&pdev->dev, "\nWPDMA_GLO_CFG = 0x%08x:\n", val);
    dev_info(&pdev->dev, "  TX_DMA: %s, RX_DMA: %s\n",
             (val & BIT(0)) ? "ON" : "OFF",
             (val & BIT(2)) ? "ON" : "OFF");

    dev_info(&pdev->dev, "=== End ===\n");
}

static int mt7927_diag_probe(struct pci_dev *pdev,
                             const struct pci_device_id *id)
{
    struct mt7927_diag *diag;
    int ret;

    dev_info(&pdev->dev, "MT7927 Diagnostic - MINIMAL SAFE VERSION\n");

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

    /* Only map BAR2 - the safe control registers */
    ret = pcim_iomap_regions(pdev, BIT(2), "mt7927_diag");
    if (ret) {
        dev_err(&pdev->dev, "Failed to map BAR2\n");
        return ret;
    }

    diag->bar2 = pcim_iomap_table(pdev)[2];
    if (!diag->bar2) {
        dev_err(&pdev->dev, "BAR2 mapping failed\n");
        return -ENOMEM;
    }

    dev_info(&pdev->dev, "BAR2: %pR\n", &pdev->resource[2]);

    /* Only read safe registers */
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
