// SPDX-License-Identifier: GPL-2.0
/*
 * MT7927 Correct Power Control Sequence
 * 
 * Uses the CORRECT bits based on mt792x driver:
 * - PCIE_LPCR_HOST_SET_OWN (BIT 0) = Give ownership to FIRMWARE
 * - PCIE_LPCR_HOST_CLR_OWN (BIT 1) = Claim ownership for DRIVER
 * - PCIE_LPCR_HOST_OWN_SYNC (BIT 2) = Status bit
 */

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/io.h>
#include <linux/delay.h>

#define MT7927_VENDOR_ID    0x14c3
#define MT7927_DEVICE_ID    0x7927

/* Register offsets */
#define MT_CONN_ON_LPCTL_OFFSET    0xe0010

/* LPCTL bits - from mt792x_regs.h */
#define PCIE_LPCR_HOST_SET_OWN     BIT(0)  /* Write to give ownership to FW */
#define PCIE_LPCR_HOST_CLR_OWN     BIT(1)  /* Write to claim ownership for driver */
#define PCIE_LPCR_HOST_OWN_SYNC    BIT(2)  /* Status: 1=FW owns, 0=driver owns */

struct mt7927_pmctrl {
    struct pci_dev *pdev;
    void __iomem *bar0;
    void __iomem *bar2;
};

static void print_state(struct mt7927_pmctrl *dev, const char *label)
{
    u32 lpctl, fw_status, wpdma;

    lpctl = readl(dev->bar0 + MT_CONN_ON_LPCTL_OFFSET);
    fw_status = readl(dev->bar2 + 0x200);
    wpdma = readl(dev->bar2 + 0x208);

    dev_info(&dev->pdev->dev, "%s:\n", label);
    dev_info(&dev->pdev->dev, "  LPCTL:     0x%08x (SYNC=%d -> %s)\n",
             lpctl,
             (lpctl & PCIE_LPCR_HOST_OWN_SYNC) ? 1 : 0,
             (lpctl & PCIE_LPCR_HOST_OWN_SYNC) ? "FW owns" : "Driver owns");
    dev_info(&dev->pdev->dev, "  FW_STATUS: 0x%08x\n", fw_status);
    dev_info(&dev->pdev->dev, "  WPDMA_CFG: 0x%08x\n", wpdma);
}

static int mt7927_pmctrl_probe(struct pci_dev *pdev,
                                const struct pci_device_id *id)
{
    struct mt7927_pmctrl *dev;
    u32 lpctl;
    int ret, i;

    dev_info(&pdev->dev, "=== MT7927 Correct Power Control ===\n");
    dev_info(&pdev->dev, "BIT(0) SET_OWN = give to FW\n");
    dev_info(&pdev->dev, "BIT(1) CLR_OWN = claim for driver\n");
    dev_info(&pdev->dev, "BIT(2) OWN_SYNC = status (1=FW, 0=driver)\n");

    dev = devm_kzalloc(&pdev->dev, sizeof(*dev), GFP_KERNEL);
    if (!dev)
        return -ENOMEM;

    dev->pdev = pdev;
    pci_set_drvdata(pdev, dev);

    ret = pcim_enable_device(pdev);
    if (ret)
        return ret;

    pci_set_master(pdev);

    ret = pcim_iomap_regions(pdev, BIT(0) | BIT(2), "mt7927_pmctrl");
    if (ret)
        return ret;

    dev->bar0 = pcim_iomap_table(pdev)[0];
    dev->bar2 = pcim_iomap_table(pdev)[2];

    print_state(dev, "\nInitial state");

    /* Step 1: Write CLR_OWN (BIT 1) to claim driver ownership */
    dev_info(&pdev->dev, "\n--- Step 1: Write CLR_OWN (BIT 1) to claim for driver ---\n");
    
    writel(PCIE_LPCR_HOST_CLR_OWN, dev->bar0 + MT_CONN_ON_LPCTL_OFFSET);
    udelay(100);
    
    lpctl = readl(dev->bar0 + MT_CONN_ON_LPCTL_OFFSET);
    dev_info(&pdev->dev, "LPCTL immediately after write: 0x%08x\n", lpctl);

    /* Poll for OWN_SYNC to clear (driver ownership) */
    dev_info(&pdev->dev, "\nPolling for OWN_SYNC to clear (up to 200ms)...\n");
    for (i = 0; i < 200; i++) {
        lpctl = readl(dev->bar0 + MT_CONN_ON_LPCTL_OFFSET);
        
        if (!(lpctl & PCIE_LPCR_HOST_OWN_SYNC)) {
            dev_info(&pdev->dev, "*** OWN_SYNC cleared after %d ms! Driver owns! ***\n", i);
            break;
        }
        
        if (i == 0 || i == 50 || i == 100 || i == 150) {
            dev_info(&pdev->dev, "  [%d ms] LPCTL=0x%08x (SYNC still set)\n", i, lpctl);
        }
        
        msleep(1);
    }

    print_state(dev, "\nAfter claiming driver ownership");

    /* Check if we got ownership */
    lpctl = readl(dev->bar0 + MT_CONN_ON_LPCTL_OFFSET);
    if (!(lpctl & PCIE_LPCR_HOST_OWN_SYNC)) {
        dev_info(&pdev->dev, "\n*** SUCCESS: Driver now owns the chip! ***\n");
        
        /* Try writing to WPDMA to see if it's now writable */
        dev_info(&pdev->dev, "\nTesting if WPDMA is now writable...\n");
        u32 wpdma_before = readl(dev->bar2 + 0x208);
        writel(0x00000001, dev->bar2 + 0x208);  /* Try to set TX_DMA_EN */
        udelay(100);
        u32 wpdma_after = readl(dev->bar2 + 0x208);
        dev_info(&pdev->dev, "WPDMA: 0x%08x -> 0x%08x\n", wpdma_before, wpdma_after);
        
        if (wpdma_after != wpdma_before) {
            dev_info(&pdev->dev, "*** WPDMA is WRITABLE! Chip unlocked! ***\n");
        } else {
            dev_info(&pdev->dev, "WPDMA still not writable\n");
        }
        
        /* Restore WPDMA to 0 */
        writel(0, dev->bar2 + 0x208);
        
    } else {
        dev_info(&pdev->dev, "\n*** OWN_SYNC still set - driver ownership NOT acquired ***\n");
        dev_info(&pdev->dev, "The chip may need additional initialization.\n");
    }

    dev_info(&pdev->dev, "\n=== Test complete ===\n");
    return 0;
}

static void mt7927_pmctrl_remove(struct pci_dev *pdev)
{
    dev_info(&pdev->dev, "MT7927 pmctrl test unloaded\n");
}

static const struct pci_device_id mt7927_pmctrl_table[] = {
    { PCI_DEVICE(MT7927_VENDOR_ID, MT7927_DEVICE_ID) },
    { }
};
MODULE_DEVICE_TABLE(pci, mt7927_pmctrl_table);

static struct pci_driver mt7927_pmctrl_driver = {
    .name = "mt7927_correct_pmctrl",
    .id_table = mt7927_pmctrl_table,
    .probe = mt7927_pmctrl_probe,
    .remove = mt7927_pmctrl_remove,
};

module_pci_driver(mt7927_pmctrl_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("MT7927 Project");
MODULE_DESCRIPTION("MT7927 Correct Power Control Test");
