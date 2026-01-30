/*
 * MT7927 Power Control Test Module
 * Tests power management handshake between driver and firmware
 */

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/delay.h>

#define MT7927_VENDOR_ID        0x14c3
#define MT7927_DEVICE_ID        0x7927

/* Power control registers */
#define MT_CONN_ON_LPCTL        0x7c060010
#define MT_CONN_ON_LPCTL_HOST_OWN  BIT(0)
#define MT_CONN_ON_LPCTL_FW_OWN    BIT(1)

/* Register remap for high addresses */
#define MT_HIF_REMAP_L1         0x155024
#define MT_HIF_REMAP_L1_MASK    GENMASK(31, 16)
#define MT_HIF_REMAP_BASE_L1    0x130000

struct test_dev {
    struct pci_dev *pdev;
    void __iomem *regs;
    u32 backup_l1;
};

static u32 remap_and_read(struct test_dev *dev, u32 addr)
{
    u32 offset = addr & 0xffff;
    u32 base = (addr >> 16) & 0xffff;
    u32 mapped;

    /* Save current remap */
    dev->backup_l1 = readl(dev->regs + MT_HIF_REMAP_L1);

    /* Set new remap */
    writel((dev->backup_l1 & ~MT_HIF_REMAP_L1_MASK) | (base << 16),
           dev->regs + MT_HIF_REMAP_L1);
    readl(dev->regs + MT_HIF_REMAP_L1);  /* Push write */

    /* Read through remapped window */
    mapped = MT_HIF_REMAP_BASE_L1 + offset;
    return readl(dev->regs + mapped);
}

static void remap_and_write(struct test_dev *dev, u32 addr, u32 val)
{
    u32 offset = addr & 0xffff;
    u32 base = (addr >> 16) & 0xffff;
    u32 mapped;

    /* Save and set remap */
    dev->backup_l1 = readl(dev->regs + MT_HIF_REMAP_L1);
    writel((dev->backup_l1 & ~MT_HIF_REMAP_L1_MASK) | (base << 16),
           dev->regs + MT_HIF_REMAP_L1);
    readl(dev->regs + MT_HIF_REMAP_L1);

    /* Write through remapped window */
    mapped = MT_HIF_REMAP_BASE_L1 + offset;
    writel(val, dev->regs + mapped);
}

static void restore_remap(struct test_dev *dev)
{
    if (dev->backup_l1) {
        writel(dev->backup_l1, dev->regs + MT_HIF_REMAP_L1);
        dev->backup_l1 = 0;
    }
}

static int test_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
    struct test_dev *dev;
    u32 val;
    int i, ret;

    dev_info(&pdev->dev, "=== MT7927 Power Control Test ===\n");

    dev = kzalloc(sizeof(*dev), GFP_KERNEL);
    if (!dev)
        return -ENOMEM;

    dev->pdev = pdev;
    pci_set_drvdata(pdev, dev);

    ret = pcim_enable_device(pdev);
    if (ret) {
        dev_err(&pdev->dev, "Failed to enable PCI device\n");
        goto err_free;
    }

    ret = pcim_iomap_regions(pdev, BIT(0), "mt7927_test");
    if (ret) {
        dev_err(&pdev->dev, "Failed to map BAR0\n");
        goto err_free;
    }

    dev->regs = pcim_iomap_table(pdev)[0];  /* Use BAR0 (2MB) for remap access */
    if (!dev->regs) {
        dev_err(&pdev->dev, "Failed to get BAR0 mapping\n");
        ret = -ENOMEM;
        goto err_free;
    }
    pci_set_master(pdev);

    /* Safety check: verify chip is responding */
    {
        u32 chip_id = readl(dev->regs + 0x0000);
        dev_info(&pdev->dev, "Chip ID: 0x%08x\n", chip_id);
        if (chip_id == 0xffffffff) {
            dev_err(&pdev->dev, "Chip not responding\n");
            ret = -EIO;
            goto err_free;
        }
    }

    /* Test 1: Read current power state */
    dev_info(&pdev->dev, "Test 1: Reading power control state\n");
    val = remap_and_read(dev, MT_CONN_ON_LPCTL);
    restore_remap(dev);
    dev_info(&pdev->dev, "  LPCTL value: 0x%08x\n", val);
    dev_info(&pdev->dev, "  HOST_OWN: %d, FW_OWN: %d\n",
             !!(val & MT_CONN_ON_LPCTL_HOST_OWN),
             !!(val & MT_CONN_ON_LPCTL_FW_OWN));

    /* Test 2: Try to acquire driver ownership */
    dev_info(&pdev->dev, "Test 2: Attempting driver power control\n");
    remap_and_write(dev, MT_CONN_ON_LPCTL, MT_CONN_ON_LPCTL_HOST_OWN);
    restore_remap(dev);
    
    for (i = 0; i < 100; i++) {
        val = remap_and_read(dev, MT_CONN_ON_LPCTL);
        restore_remap(dev);
        if (!(val & MT_CONN_ON_LPCTL_FW_OWN)) {
            dev_info(&pdev->dev, "  Driver ownership acquired after %d ms\n", i * 10);
            break;
        }
        msleep(10);
    }

    if (i >= 100) {
        dev_warn(&pdev->dev, "  Timeout waiting for driver ownership\n");
    }

    /* Test 3: Try firmware ownership */
    dev_info(&pdev->dev, "Test 3: Attempting firmware power control\n");
    remap_and_write(dev, MT_CONN_ON_LPCTL, MT_CONN_ON_LPCTL_FW_OWN);
    restore_remap(dev);

    for (i = 0; i < 100; i++) {
        val = remap_and_read(dev, MT_CONN_ON_LPCTL);
        restore_remap(dev);
        if (val & MT_CONN_ON_LPCTL_FW_OWN) {
            dev_info(&pdev->dev, "  Firmware ownership set after %d ms\n", i * 10);
            break;
        }
        msleep(10);
    }

    dev_info(&pdev->dev, "  Final LPCTL: 0x%08x\n", val);
    dev_info(&pdev->dev, "=== Power Control Test Complete ===\n");

    return 0;

err_free:
    kfree(dev);
    return ret;
}

static void test_remove(struct pci_dev *pdev)
{
    struct test_dev *dev = pci_get_drvdata(pdev);
    dev_info(&pdev->dev, "Removing test module\n");
    kfree(dev);
}

static const struct pci_device_id test_ids[] = {
    { PCI_DEVICE(MT7927_VENDOR_ID, MT7927_DEVICE_ID) },
    { }
};
MODULE_DEVICE_TABLE(pci, test_ids);

static struct pci_driver test_driver = {
    .name = "mt7927_test_power",
    .id_table = test_ids,
    .probe = test_probe,
    .remove = test_remove,
};

module_pci_driver(test_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("MT7927 Power Control Test Module");
