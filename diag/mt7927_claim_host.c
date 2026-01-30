// SPDX-License-Identifier: GPL-2.0
/*
 * MT7927 Claim Host Ownership
 * 
 * Attempts to claim HOST_OWN by writing to LPCTL register.
 * This is the fundamental first step to initializing the chip.
 */

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/io.h>
#include <linux/delay.h>

#define MT7927_VENDOR_ID    0x14c3
#define MT7927_DEVICE_ID    0x7927

/* LPCTL register bits */
#define MT_CONN_ON_LPCTL_OFFSET    0xe0010
#define MT_CONN_ON_LPCTL_HOST_OWN  BIT(0)
#define MT_CONN_ON_LPCTL_FW_OWN    BIT(1)

struct mt7927_claim {
    struct pci_dev *pdev;
    void __iomem *bar0;
    void __iomem *bar2;
};

static int mt7927_claim_probe(struct pci_dev *pdev,
                               const struct pci_device_id *id)
{
    struct mt7927_claim *dev;
    u32 lpctl_before, lpctl_after, fw_status_before, fw_status_after;
    u32 wpdma_before, wpdma_after;
    int ret, i;

    dev_info(&pdev->dev, "=== MT7927 Claim Host Ownership ===\n");

    dev = devm_kzalloc(&pdev->dev, sizeof(*dev), GFP_KERNEL);
    if (!dev)
        return -ENOMEM;

    dev->pdev = pdev;
    pci_set_drvdata(pdev, dev);

    ret = pcim_enable_device(pdev);
    if (ret)
        return ret;

    ret = pcim_iomap_regions(pdev, BIT(0) | BIT(2), "mt7927_claim");
    if (ret)
        return ret;

    dev->bar0 = pcim_iomap_table(pdev)[0];
    dev->bar2 = pcim_iomap_table(pdev)[2];

    /* Read current state */
    lpctl_before = readl(dev->bar0 + MT_CONN_ON_LPCTL_OFFSET);
    fw_status_before = readl(dev->bar2 + 0x200);
    wpdma_before = readl(dev->bar2 + 0x208);

    dev_info(&pdev->dev, "BEFORE claiming HOST_OWN:\n");
    dev_info(&pdev->dev, "  LPCTL:     0x%08x (HOST=%d, FW=%d)\n", 
             lpctl_before, 
             (lpctl_before & MT_CONN_ON_LPCTL_HOST_OWN) ? 1 : 0,
             (lpctl_before & MT_CONN_ON_LPCTL_FW_OWN) ? 1 : 0);
    dev_info(&pdev->dev, "  FW_STATUS: 0x%08x\n", fw_status_before);
    dev_info(&pdev->dev, "  WPDMA_CFG: 0x%08x\n", wpdma_before);

    /* Attempt to claim HOST_OWN */
    dev_info(&pdev->dev, "\nWriting HOST_OWN bit to LPCTL...\n");
    writel(MT_CONN_ON_LPCTL_HOST_OWN, dev->bar0 + MT_CONN_ON_LPCTL_OFFSET);
    
    /* Small delay */
    udelay(100);

    /* Read back immediately */
    lpctl_after = readl(dev->bar0 + MT_CONN_ON_LPCTL_OFFSET);
    dev_info(&pdev->dev, "  LPCTL after write: 0x%08x\n", lpctl_after);

    /* Poll for up to 100ms to see if state changes */
    dev_info(&pdev->dev, "\nPolling for state change (up to 100ms)...\n");
    for (i = 0; i < 100; i++) {
        lpctl_after = readl(dev->bar0 + MT_CONN_ON_LPCTL_OFFSET);
        fw_status_after = readl(dev->bar2 + 0x200);
        
        /* Check if HOST_OWN is set */
        if (lpctl_after & MT_CONN_ON_LPCTL_HOST_OWN) {
            dev_info(&pdev->dev, "  HOST_OWN acquired after %d ms!\n", i);
            break;
        }
        
        /* Check if FW_STATUS changed */
        if (fw_status_after != fw_status_before) {
            dev_info(&pdev->dev, "  FW_STATUS changed at %d ms: 0x%08x\n", 
                     i, fw_status_after);
        }
        
        msleep(1);
    }

    /* Final state */
    lpctl_after = readl(dev->bar0 + MT_CONN_ON_LPCTL_OFFSET);
    fw_status_after = readl(dev->bar2 + 0x200);
    wpdma_after = readl(dev->bar2 + 0x208);

    dev_info(&pdev->dev, "\nAFTER claiming HOST_OWN:\n");
    dev_info(&pdev->dev, "  LPCTL:     0x%08x (HOST=%d, FW=%d)\n", 
             lpctl_after,
             (lpctl_after & MT_CONN_ON_LPCTL_HOST_OWN) ? 1 : 0,
             (lpctl_after & MT_CONN_ON_LPCTL_FW_OWN) ? 1 : 0);
    dev_info(&pdev->dev, "  FW_STATUS: 0x%08x\n", fw_status_after);
    dev_info(&pdev->dev, "  WPDMA_CFG: 0x%08x\n", wpdma_after);

    /* Analyze result */
    if (lpctl_after & MT_CONN_ON_LPCTL_HOST_OWN) {
        dev_info(&pdev->dev, "\n*** SUCCESS: Host now owns the chip! ***\n");
        
        if (wpdma_after != wpdma_before) {
            dev_info(&pdev->dev, "*** WPDMA_CFG changed - DMA may be accessible now! ***\n");
        }
    } else {
        dev_info(&pdev->dev, "\n*** HOST_OWN not acquired ***\n");
        dev_info(&pdev->dev, "The chip may need additional initialization.\n");
    }

    if (fw_status_after != fw_status_before) {
        dev_info(&pdev->dev, "*** FW_STATUS changed: 0x%08x -> 0x%08x ***\n",
                 fw_status_before, fw_status_after);
    }

    dev_info(&pdev->dev, "\n=== Claim test complete ===\n");
    return 0;
}

static void mt7927_claim_remove(struct pci_dev *pdev)
{
    dev_info(&pdev->dev, "MT7927 Claim module unloaded\n");
}

static const struct pci_device_id mt7927_claim_table[] = {
    { PCI_DEVICE(MT7927_VENDOR_ID, MT7927_DEVICE_ID) },
    { }
};
MODULE_DEVICE_TABLE(pci, mt7927_claim_table);

static struct pci_driver mt7927_claim_driver = {
    .name = "mt7927_claim_host",
    .id_table = mt7927_claim_table,
    .probe = mt7927_claim_probe,
    .remove = mt7927_claim_remove,
};

module_pci_driver(mt7927_claim_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("MT7927 Project");
MODULE_DESCRIPTION("MT7927 Claim Host Ownership Test");
