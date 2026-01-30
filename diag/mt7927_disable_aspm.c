// SPDX-License-Identifier: GPL-2.0
/*
 * MT7927 Disable ASPM and Claim Host
 * 
 * Disables PCIe Active State Power Management before claiming HOST_OWN.
 * ASPM can put the chip in a low-power state where it ignores writes.
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

struct mt7927_aspm {
    struct pci_dev *pdev;
    void __iomem *bar0;
    void __iomem *bar2;
};

static void disable_aspm(struct pci_dev *pdev)
{
    u16 link_ctrl;
    int pos;

    /* Find PCIe capability */
    pos = pci_find_capability(pdev, PCI_CAP_ID_EXP);
    if (!pos) {
        dev_warn(&pdev->dev, "No PCIe capability found\n");
        return;
    }

    /* Read current Link Control register */
    pci_read_config_word(pdev, pos + PCI_EXP_LNKCTL, &link_ctrl);
    dev_info(&pdev->dev, "PCIe Link Control before: 0x%04x\n", link_ctrl);
    dev_info(&pdev->dev, "  ASPM L0s: %s, L1: %s\n",
             (link_ctrl & PCI_EXP_LNKCTL_ASPM_L0S) ? "enabled" : "disabled",
             (link_ctrl & PCI_EXP_LNKCTL_ASPM_L1) ? "enabled" : "disabled");

    /* Disable ASPM */
    if (link_ctrl & (PCI_EXP_LNKCTL_ASPM_L0S | PCI_EXP_LNKCTL_ASPM_L1)) {
        link_ctrl &= ~(PCI_EXP_LNKCTL_ASPM_L0S | PCI_EXP_LNKCTL_ASPM_L1);
        pci_write_config_word(pdev, pos + PCI_EXP_LNKCTL, link_ctrl);
        
        /* Read back to verify */
        pci_read_config_word(pdev, pos + PCI_EXP_LNKCTL, &link_ctrl);
        dev_info(&pdev->dev, "PCIe Link Control after: 0x%04x\n", link_ctrl);
    } else {
        dev_info(&pdev->dev, "ASPM already disabled\n");
    }

    /* Also try to disable ASPM on parent bridge */
    if (pdev->bus && pdev->bus->self) {
        struct pci_dev *bridge = pdev->bus->self;
        int bridge_pos = pci_find_capability(bridge, PCI_CAP_ID_EXP);
        if (bridge_pos) {
            u16 bridge_ctrl;
            pci_read_config_word(bridge, bridge_pos + PCI_EXP_LNKCTL, &bridge_ctrl);
            dev_info(&pdev->dev, "Bridge Link Control: 0x%04x\n", bridge_ctrl);
            
            if (bridge_ctrl & (PCI_EXP_LNKCTL_ASPM_L0S | PCI_EXP_LNKCTL_ASPM_L1)) {
                bridge_ctrl &= ~(PCI_EXP_LNKCTL_ASPM_L0S | PCI_EXP_LNKCTL_ASPM_L1);
                pci_write_config_word(bridge, bridge_pos + PCI_EXP_LNKCTL, bridge_ctrl);
                dev_info(&pdev->dev, "Bridge ASPM disabled\n");
            }
        }
    }
}

static int mt7927_aspm_probe(struct pci_dev *pdev,
                              const struct pci_device_id *id)
{
    struct mt7927_aspm *dev;
    u32 lpctl_before, lpctl_after, fw_status;
    u32 wpdma_before, wpdma_after;
    int ret, i;

    dev_info(&pdev->dev, "=== MT7927 Disable ASPM and Claim Host ===\n");

    dev = devm_kzalloc(&pdev->dev, sizeof(*dev), GFP_KERNEL);
    if (!dev)
        return -ENOMEM;

    dev->pdev = pdev;
    pci_set_drvdata(pdev, dev);

    ret = pcim_enable_device(pdev);
    if (ret)
        return ret;

    /* Enable bus mastering (needed for DMA) */
    pci_set_master(pdev);
    dev_info(&pdev->dev, "Bus mastering enabled\n");

    ret = pcim_iomap_regions(pdev, BIT(0) | BIT(2), "mt7927_aspm");
    if (ret)
        return ret;

    dev->bar0 = pcim_iomap_table(pdev)[0];
    dev->bar2 = pcim_iomap_table(pdev)[2];

    /* Read initial state */
    lpctl_before = readl(dev->bar0 + MT_CONN_ON_LPCTL_OFFSET);
    fw_status = readl(dev->bar2 + 0x200);
    wpdma_before = readl(dev->bar2 + 0x208);

    dev_info(&pdev->dev, "Initial state:\n");
    dev_info(&pdev->dev, "  LPCTL:     0x%08x (HOST=%d, FW=%d)\n", 
             lpctl_before,
             (lpctl_before & MT_CONN_ON_LPCTL_HOST_OWN) ? 1 : 0,
             (lpctl_before & MT_CONN_ON_LPCTL_FW_OWN) ? 1 : 0);
    dev_info(&pdev->dev, "  FW_STATUS: 0x%08x\n", fw_status);
    dev_info(&pdev->dev, "  WPDMA_CFG: 0x%08x\n", wpdma_before);

    /* Step 1: Disable ASPM */
    dev_info(&pdev->dev, "\n--- Disabling ASPM ---\n");
    disable_aspm(pdev);

    /* Small delay after ASPM change */
    msleep(10);

    /* Step 2: Try to claim HOST_OWN */
    dev_info(&pdev->dev, "\n--- Claiming HOST_OWN ---\n");
    
    /* Read LPCTL again after ASPM change */
    lpctl_before = readl(dev->bar0 + MT_CONN_ON_LPCTL_OFFSET);
    dev_info(&pdev->dev, "LPCTL after ASPM disable: 0x%08x\n", lpctl_before);

    /* Write HOST_OWN */
    dev_info(&pdev->dev, "Writing HOST_OWN bit...\n");
    writel(MT_CONN_ON_LPCTL_HOST_OWN, dev->bar0 + MT_CONN_ON_LPCTL_OFFSET);
    
    /* Read back immediately */
    udelay(100);
    lpctl_after = readl(dev->bar0 + MT_CONN_ON_LPCTL_OFFSET);
    dev_info(&pdev->dev, "LPCTL after write: 0x%08x\n", lpctl_after);

    /* Poll for state change */
    for (i = 0; i < 200; i++) {
        lpctl_after = readl(dev->bar0 + MT_CONN_ON_LPCTL_OFFSET);
        
        if (lpctl_after & MT_CONN_ON_LPCTL_HOST_OWN) {
            dev_info(&pdev->dev, "HOST_OWN acquired after %d ms!\n", i);
            break;
        }
        
        if (i == 0 || i == 50 || i == 100 || i == 150) {
            dev_info(&pdev->dev, "  [%d ms] LPCTL=0x%08x\n", i, lpctl_after);
        }
        
        msleep(1);
    }

    /* Final state */
    lpctl_after = readl(dev->bar0 + MT_CONN_ON_LPCTL_OFFSET);
    fw_status = readl(dev->bar2 + 0x200);
    wpdma_after = readl(dev->bar2 + 0x208);

    dev_info(&pdev->dev, "\nFinal state:\n");
    dev_info(&pdev->dev, "  LPCTL:     0x%08x (HOST=%d, FW=%d)\n", 
             lpctl_after,
             (lpctl_after & MT_CONN_ON_LPCTL_HOST_OWN) ? 1 : 0,
             (lpctl_after & MT_CONN_ON_LPCTL_FW_OWN) ? 1 : 0);
    dev_info(&pdev->dev, "  FW_STATUS: 0x%08x\n", fw_status);
    dev_info(&pdev->dev, "  WPDMA_CFG: 0x%08x\n", wpdma_after);

    if (lpctl_after & MT_CONN_ON_LPCTL_HOST_OWN) {
        dev_info(&pdev->dev, "\n*** SUCCESS: Host owns the chip! ***\n");
    } else {
        dev_info(&pdev->dev, "\n*** HOST_OWN still not acquired ***\n");
        dev_info(&pdev->dev, "The chip may need WFSYS reset or other init.\n");
    }

    dev_info(&pdev->dev, "\n=== Test complete ===\n");
    return 0;
}

static void mt7927_aspm_remove(struct pci_dev *pdev)
{
    dev_info(&pdev->dev, "MT7927 ASPM test unloaded\n");
}

static const struct pci_device_id mt7927_aspm_table[] = {
    { PCI_DEVICE(MT7927_VENDOR_ID, MT7927_DEVICE_ID) },
    { }
};
MODULE_DEVICE_TABLE(pci, mt7927_aspm_table);

static struct pci_driver mt7927_aspm_driver = {
    .name = "mt7927_disable_aspm",
    .id_table = mt7927_aspm_table,
    .probe = mt7927_aspm_probe,
    .remove = mt7927_aspm_remove,
};

module_pci_driver(mt7927_aspm_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("MT7927 Project");
MODULE_DESCRIPTION("MT7927 Disable ASPM and Claim Host");
