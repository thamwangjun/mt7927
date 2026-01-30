// SPDX-License-Identifier: GPL-2.0
/*
 * MT7927 Diagnostic Module - READ ONLY, safe for debugging
 * 
 * This module only reads registers and prints diagnostics.
 * It does NOT write to any registers or enable DMA/IRQs.
 */

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/io.h>

#define MT7927_VENDOR_ID    0x14c3
#define MT7927_DEVICE_ID    0x7927

struct mt7927_diag {
    struct pci_dev *pdev;
    void __iomem *bar0;  /* BAR0: 2MB memory region */
    void __iomem *bar2;  /* BAR2: 32KB registers */
};

static void dump_registers(struct mt7927_diag *diag)
{
    struct pci_dev *pdev = diag->pdev;
    u32 val;
    int i;

    dev_info(&pdev->dev, "=== MT7927 Register Dump (READ ONLY) ===\n");

    /* BAR2 - Control registers */
    dev_info(&pdev->dev, "BAR2 (Control Registers):\n");
    
    val = readl(diag->bar2 + 0x000);
    dev_info(&pdev->dev, "  [0x000] Chip ID:      0x%08x\n", val);
    
    val = readl(diag->bar2 + 0x004);
    dev_info(&pdev->dev, "  [0x004] HW Rev:       0x%08x\n", val);

    val = readl(diag->bar2 + 0x200);
    dev_info(&pdev->dev, "  [0x200] HOST_INT_STA: 0x%08x (FW_STATUS)\n", val);

    val = readl(diag->bar2 + 0x204);
    dev_info(&pdev->dev, "  [0x204] HOST_INT_ENA: 0x%08x\n", val);

    val = readl(diag->bar2 + 0x208);
    dev_info(&pdev->dev, "  [0x208] WPDMA_GLO_CFG: 0x%08x\n", val);

    val = readl(diag->bar2 + 0x20c);
    dev_info(&pdev->dev, "  [0x20c] RST_DTX_PTR: 0x%08x\n", val);

    /* TX Ring registers */
    dev_info(&pdev->dev, "TX Ring 0 (Band0 Data):\n");
    dev_info(&pdev->dev, "  [0x300] BASE: 0x%08x\n", readl(diag->bar2 + 0x300));
    dev_info(&pdev->dev, "  [0x304] CNT:  0x%08x\n", readl(diag->bar2 + 0x304));
    dev_info(&pdev->dev, "  [0x308] CIDX: 0x%08x\n", readl(diag->bar2 + 0x308));
    dev_info(&pdev->dev, "  [0x30c] DIDX: 0x%08x\n", readl(diag->bar2 + 0x30c));

    dev_info(&pdev->dev, "TX Ring 16 (FWDL):\n");
    dev_info(&pdev->dev, "  [0x400] BASE: 0x%08x\n", readl(diag->bar2 + 0x400));
    dev_info(&pdev->dev, "  [0x404] CNT:  0x%08x\n", readl(diag->bar2 + 0x404));

    /* RX Ring registers */
    dev_info(&pdev->dev, "RX Ring 0 (MCU WM):\n");
    dev_info(&pdev->dev, "  [0x500] BASE: 0x%08x\n", readl(diag->bar2 + 0x500));
    dev_info(&pdev->dev, "  [0x504] CNT:  0x%08x\n", readl(diag->bar2 + 0x504));

    /* Check some BAR0 locations */
    dev_info(&pdev->dev, "BAR0 (Memory Region) - first 16 words:\n");
    for (i = 0; i < 16; i++) {
        val = readl(diag->bar0 + i * 4);
        if (val != 0)
            dev_info(&pdev->dev, "  [0x%03x] 0x%08x\n", i * 4, val);
    }

    /* Check if any BAR0 locations have data */
    dev_info(&pdev->dev, "BAR0 scan for non-zero values:\n");
    for (i = 0; i < 0x1000; i += 0x100) {
        val = readl(diag->bar0 + i);
        if (val != 0)
            dev_info(&pdev->dev, "  [0x%05x] 0x%08x\n", i, val);
    }

    /* Decode FW_STATUS */
    val = readl(diag->bar2 + 0x200);
    dev_info(&pdev->dev, "\nFW_STATUS Analysis (0x%08x):\n", val);
    if (val == 0xffff10f1)
        dev_info(&pdev->dev, "  -> Pre-initialization state (chip locked)\n");
    else if ((val & 0xFFFF0000) == 0xFFFF0000)
        dev_info(&pdev->dev, "  -> Error/pre-init state (upper 16 bits = 0xFFFF)\n");
    else if (val == 0x00000001)
        dev_info(&pdev->dev, "  -> MCU ready\n");
    else
        dev_info(&pdev->dev, "  -> Unknown state\n");

    /* Check WPDMA state */
    val = readl(diag->bar2 + 0x208);
    dev_info(&pdev->dev, "\nWPDMA_GLO_CFG Analysis (0x%08x):\n", val);
    dev_info(&pdev->dev, "  TX_DMA_EN:  %s\n", (val & BIT(0)) ? "ON" : "OFF");
    dev_info(&pdev->dev, "  TX_DMA_BUSY: %s\n", (val & BIT(1)) ? "YES" : "NO");
    dev_info(&pdev->dev, "  RX_DMA_EN:  %s\n", (val & BIT(2)) ? "ON" : "OFF");
    dev_info(&pdev->dev, "  RX_DMA_BUSY: %s\n", (val & BIT(3)) ? "YES" : "NO");

    dev_info(&pdev->dev, "=== End of Register Dump ===\n");
}

static int mt7927_diag_probe(struct pci_dev *pdev,
                             const struct pci_device_id *id)
{
    struct mt7927_diag *diag;
    int ret;

    dev_info(&pdev->dev, "MT7927 Diagnostic Module - READ ONLY\n");

    diag = devm_kzalloc(&pdev->dev, sizeof(*diag), GFP_KERNEL);
    if (!diag)
        return -ENOMEM;

    diag->pdev = pdev;
    pci_set_drvdata(pdev, diag);

    /* Enable PCI device */
    ret = pcim_enable_device(pdev);
    if (ret) {
        dev_err(&pdev->dev, "Failed to enable PCI device\n");
        return ret;
    }

    /* Map BAR regions */
    ret = pcim_iomap_regions(pdev, BIT(0) | BIT(2), "mt7927_diag");
    if (ret) {
        dev_err(&pdev->dev, "Failed to map BARs\n");
        return ret;
    }

    diag->bar0 = pcim_iomap_table(pdev)[0];
    diag->bar2 = pcim_iomap_table(pdev)[2];

    if (!diag->bar0 || !diag->bar2) {
        dev_err(&pdev->dev, "BAR mapping failed\n");
        return -ENOMEM;
    }

    dev_info(&pdev->dev, "BAR0: %pR -> %p\n", &pdev->resource[0], diag->bar0);
    dev_info(&pdev->dev, "BAR2: %pR -> %p\n", &pdev->resource[2], diag->bar2);

    /* Dump all registers - READ ONLY */
    dump_registers(diag);

    dev_info(&pdev->dev, "Diagnostic complete. Module staying loaded for re-reads.\n");
    dev_info(&pdev->dev, "Unload with: sudo rmmod mt7927_diag\n");

    return 0;
}

static void mt7927_diag_remove(struct pci_dev *pdev)
{
    dev_info(&pdev->dev, "MT7927 Diagnostic Module unloaded\n");
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
MODULE_DESCRIPTION("MT7927 Read-Only Diagnostic Module");
