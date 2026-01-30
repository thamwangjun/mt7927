// SPDX-License-Identifier: GPL-2.0
/*
 * MT7927 WiFi System Reset Test
 * 
 * Tries WFSYS reset sequence to bring chip out of power-down state.
 * Based on mt792x_wfsys_reset from mt7925 driver.
 */

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/io.h>
#include <linux/delay.h>

#define MT7927_VENDOR_ID    0x14c3
#define MT7927_DEVICE_ID    0x7927

/* Register offsets in BAR0 (after fixed_map translation) */
#define MT_WFSYS_SW_RST_B_OFFSET   0xf0140  /* WFSYS reset: 0x7c000140 -> 0xf0140 */
#define MT_CONN_ON_LPCTL_OFFSET    0xe0010  /* LPCTL: 0x7c060010 -> 0xe0010 */
#define MT_CONN_ON_MISC_OFFSET     0xe00f0  /* CONN_ON_MISC: 0x7c0600f0 -> 0xe00f0 */

/* Bits */
#define MT_WFSYS_SW_RST_B_EN       BIT(0)
#define MT_CONN_ON_LPCTL_HOST_OWN  BIT(0)
#define MT_CONN_ON_LPCTL_FW_OWN    BIT(1)

struct mt7927_wfsys {
    struct pci_dev *pdev;
    void __iomem *bar0;
    void __iomem *bar2;
};

static void print_state(struct mt7927_wfsys *dev, const char *label)
{
    u32 lpctl, fw_status, wpdma, wfsys, conn_misc;

    lpctl = readl(dev->bar0 + MT_CONN_ON_LPCTL_OFFSET);
    fw_status = readl(dev->bar2 + 0x200);
    wpdma = readl(dev->bar2 + 0x208);
    wfsys = readl(dev->bar0 + MT_WFSYS_SW_RST_B_OFFSET);
    conn_misc = readl(dev->bar0 + MT_CONN_ON_MISC_OFFSET);

    dev_info(&dev->pdev->dev, "%s:\n", label);
    dev_info(&dev->pdev->dev, "  LPCTL:     0x%08x (HOST=%d, FW=%d, Bit2=%d)\n",
             lpctl,
             (lpctl & BIT(0)) ? 1 : 0,
             (lpctl & BIT(1)) ? 1 : 0,
             (lpctl & BIT(2)) ? 1 : 0);
    dev_info(&dev->pdev->dev, "  FW_STATUS: 0x%08x\n", fw_status);
    dev_info(&dev->pdev->dev, "  WPDMA_CFG: 0x%08x\n", wpdma);
    dev_info(&dev->pdev->dev, "  WFSYS_RST: 0x%08x (EN=%d)\n", 
             wfsys, (wfsys & MT_WFSYS_SW_RST_B_EN) ? 1 : 0);
    dev_info(&dev->pdev->dev, "  CONN_MISC: 0x%08x\n", conn_misc);
}

static int mt7927_wfsys_probe(struct pci_dev *pdev,
                               const struct pci_device_id *id)
{
    struct mt7927_wfsys *dev;
    u32 val;
    int ret, i;

    dev_info(&pdev->dev, "=== MT7927 WFSYS Reset Test ===\n");

    dev = devm_kzalloc(&pdev->dev, sizeof(*dev), GFP_KERNEL);
    if (!dev)
        return -ENOMEM;

    dev->pdev = pdev;
    pci_set_drvdata(pdev, dev);

    ret = pcim_enable_device(pdev);
    if (ret)
        return ret;

    pci_set_master(pdev);

    ret = pcim_iomap_regions(pdev, BIT(0) | BIT(2), "mt7927_wfsys");
    if (ret)
        return ret;

    dev->bar0 = pcim_iomap_table(pdev)[0];
    dev->bar2 = pcim_iomap_table(pdev)[2];

    print_state(dev, "Initial state");

    /* Step 1: Assert WFSYS reset (clear the enable bit) */
    dev_info(&pdev->dev, "\n--- Step 1: Assert WFSYS reset ---\n");
    val = readl(dev->bar0 + MT_WFSYS_SW_RST_B_OFFSET);
    dev_info(&pdev->dev, "WFSYS_RST before: 0x%08x\n", val);
    
    writel(val & ~MT_WFSYS_SW_RST_B_EN, dev->bar0 + MT_WFSYS_SW_RST_B_OFFSET);
    udelay(100);
    
    val = readl(dev->bar0 + MT_WFSYS_SW_RST_B_OFFSET);
    dev_info(&pdev->dev, "WFSYS_RST after clear: 0x%08x\n", val);

    /* Wait for reset to take effect */
    msleep(5);

    /* Step 2: Deassert WFSYS reset (set the enable bit) */
    dev_info(&pdev->dev, "\n--- Step 2: Deassert WFSYS reset ---\n");
    val = readl(dev->bar0 + MT_WFSYS_SW_RST_B_OFFSET);
    writel(val | MT_WFSYS_SW_RST_B_EN, dev->bar0 + MT_WFSYS_SW_RST_B_OFFSET);
    udelay(100);
    
    val = readl(dev->bar0 + MT_WFSYS_SW_RST_B_OFFSET);
    dev_info(&pdev->dev, "WFSYS_RST after set: 0x%08x\n", val);

    /* Wait for chip to come out of reset */
    msleep(50);

    print_state(dev, "After WFSYS reset");

    /* Step 3: Try to claim HOST_OWN */
    dev_info(&pdev->dev, "\n--- Step 3: Claiming HOST_OWN ---\n");
    writel(MT_CONN_ON_LPCTL_HOST_OWN, dev->bar0 + MT_CONN_ON_LPCTL_OFFSET);
    
    /* Poll for HOST_OWN */
    for (i = 0; i < 100; i++) {
        val = readl(dev->bar0 + MT_CONN_ON_LPCTL_OFFSET);
        if (val & MT_CONN_ON_LPCTL_HOST_OWN) {
            dev_info(&pdev->dev, "HOST_OWN acquired after %d ms!\n", i);
            break;
        }
        if (i == 0 || i == 25 || i == 50 || i == 75) {
            dev_info(&pdev->dev, "  [%d ms] LPCTL=0x%08x\n", i, val);
        }
        msleep(1);
    }

    print_state(dev, "Final state");

    val = readl(dev->bar0 + MT_CONN_ON_LPCTL_OFFSET);
    if (val & MT_CONN_ON_LPCTL_HOST_OWN) {
        dev_info(&pdev->dev, "\n*** SUCCESS: Host owns the chip! ***\n");
    } else {
        dev_info(&pdev->dev, "\n*** HOST_OWN still not acquired ***\n");
        
        /* Check if WFSYS reset had any effect */
        val = readl(dev->bar0 + MT_WFSYS_SW_RST_B_OFFSET);
        if (val & MT_WFSYS_SW_RST_B_EN) {
            dev_info(&pdev->dev, "WFSYS reset completed (EN=1)\n");
        } else {
            dev_info(&pdev->dev, "WFSYS reset may not have worked\n");
        }
    }

    dev_info(&pdev->dev, "\n=== Test complete ===\n");
    return 0;
}

static void mt7927_wfsys_remove(struct pci_dev *pdev)
{
    dev_info(&pdev->dev, "MT7927 WFSYS test unloaded\n");
}

static const struct pci_device_id mt7927_wfsys_table[] = {
    { PCI_DEVICE(MT7927_VENDOR_ID, MT7927_DEVICE_ID) },
    { }
};
MODULE_DEVICE_TABLE(pci, mt7927_wfsys_table);

static struct pci_driver mt7927_wfsys_driver = {
    .name = "mt7927_wfsys_reset",
    .id_table = mt7927_wfsys_table,
    .probe = mt7927_wfsys_probe,
    .remove = mt7927_wfsys_remove,
};

module_pci_driver(mt7927_wfsys_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("MT7927 Project");
MODULE_DESCRIPTION("MT7927 WFSYS Reset Test");
