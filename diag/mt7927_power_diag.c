// SPDX-License-Identifier: GPL-2.0
/*
 * MT7927 Power State Diagnostic
 * 
 * Carefully reads the LPCTL register to understand power ownership.
 * Uses a single BAR0 read after verifying BAR0 is accessible.
 */

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/io.h>
#include <linux/delay.h>

#define MT7927_VENDOR_ID    0x14c3
#define MT7927_DEVICE_ID    0x7927

/* LPCTL register - power management */
#define MT_CONN_ON_LPCTL_OFFSET    0xe0010

struct mt7927_pwr_diag {
    struct pci_dev *pdev;
    void __iomem *bar0;
    void __iomem *bar2;
};

static int mt7927_pwr_diag_probe(struct pci_dev *pdev,
                                  const struct pci_device_id *id)
{
    struct mt7927_pwr_diag *diag;
    u32 val, chip_id;
    int ret;

    dev_info(&pdev->dev, "MT7927 Power State Diagnostic\n");

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

    /* Map both BARs */
    ret = pcim_iomap_regions(pdev, BIT(0) | BIT(2), "mt7927_pwr_diag");
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

    dev_info(&pdev->dev, "BAR0 size: %lld bytes\n", 
             (long long)pci_resource_len(pdev, 0));
    dev_info(&pdev->dev, "BAR2 size: %lld bytes\n", 
             (long long)pci_resource_len(pdev, 2));

    /* First verify BAR2 works (we know this is safe) */
    chip_id = readl(diag->bar2 + 0x000);
    dev_info(&pdev->dev, "BAR2[0x000] Chip ID: 0x%08x\n", chip_id);

    if (chip_id != 0x00511163) {
        dev_warn(&pdev->dev, "Unexpected chip ID, BAR2 may not be working\n");
    }

    /* Show FW_STATUS from BAR2 */
    val = readl(diag->bar2 + 0x200);
    dev_info(&pdev->dev, "BAR2[0x200] FW_STATUS: 0x%08x\n", val);

    /* Now carefully test BAR0 access */
    dev_info(&pdev->dev, "\n--- Testing BAR0 access (carefully) ---\n");

    /* Test a known-safe low offset first */
    dev_info(&pdev->dev, "Reading BAR0[0x000]...\n");
    val = readl(diag->bar0 + 0x000);
    dev_info(&pdev->dev, "BAR0[0x000] = 0x%08x\n", val);

    /* Small delay to let any bus activity settle */
    udelay(100);

    /* Try reading LPCTL at 0xe0010 */
    dev_info(&pdev->dev, "Reading BAR0[0xe0010] (LPCTL)...\n");
    val = readl(diag->bar0 + MT_CONN_ON_LPCTL_OFFSET);
    dev_info(&pdev->dev, "BAR0[0xe0010] LPCTL = 0x%08x\n", val);

    /* Decode LPCTL bits */
    dev_info(&pdev->dev, "LPCTL decode:\n");
    dev_info(&pdev->dev, "  Bit0 HOST_OWN: %d\n", (val >> 0) & 1);
    dev_info(&pdev->dev, "  Bit1 FW_OWN:   %d\n", (val >> 1) & 1);
    dev_info(&pdev->dev, "  Bit2:          %d\n", (val >> 2) & 1);
    dev_info(&pdev->dev, "  Bit3:          %d\n", (val >> 3) & 1);
    dev_info(&pdev->dev, "  Upper bits:    0x%08x\n", val >> 4);

    if ((val & 0x3) == 0x0)
        dev_info(&pdev->dev, "  -> Neither HOST nor FW owns the chip!\n");
    else if ((val & 0x3) == 0x1)
        dev_info(&pdev->dev, "  -> HOST owns the chip\n");
    else if ((val & 0x3) == 0x2)
        dev_info(&pdev->dev, "  -> FW owns the chip\n");
    else
        dev_info(&pdev->dev, "  -> Both bits set (invalid?)\n");

    /* Try reading a few more BAR0 addresses in the conn_infra area */
    udelay(100);
    dev_info(&pdev->dev, "\nConn_infra area sample:\n");
    
    val = readl(diag->bar0 + 0xe0000);
    dev_info(&pdev->dev, "  BAR0[0xe0000] = 0x%08x\n", val);
    
    val = readl(diag->bar0 + 0xe00f0);
    dev_info(&pdev->dev, "  BAR0[0xe00f0] CONN_ON_MISC = 0x%08x\n", val);

    dev_info(&pdev->dev, "\n=== Power diagnostic complete ===\n");
    dev_info(&pdev->dev, "Unload with: sudo rmmod mt7927_power_diag\n");

    return 0;
}

static void mt7927_pwr_diag_remove(struct pci_dev *pdev)
{
    dev_info(&pdev->dev, "MT7927 Power Diagnostic unloaded\n");
}

static const struct pci_device_id mt7927_pwr_diag_table[] = {
    { PCI_DEVICE(MT7927_VENDOR_ID, MT7927_DEVICE_ID) },
    { }
};
MODULE_DEVICE_TABLE(pci, mt7927_pwr_diag_table);

static struct pci_driver mt7927_pwr_diag_driver = {
    .name = "mt7927_power_diag",
    .id_table = mt7927_pwr_diag_table,
    .probe = mt7927_pwr_diag_probe,
    .remove = mt7927_pwr_diag_remove,
};

module_pci_driver(mt7927_pwr_diag_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("MT7927 Project");
MODULE_DESCRIPTION("MT7927 Power State Diagnostic");
