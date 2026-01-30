/*
 * MT7927 WiFi System Reset Test Module
 * Tests WiFi subsystem reset functionality
 */

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/delay.h>

#define MT7927_VENDOR_ID        0x14c3
#define MT7927_DEVICE_ID        0x7927

/* WiFi system registers (require remapping) */
#define MT_WFSYS_SW_RST_B       0x7c000140
#define MT_WFSYS_SW_RST_B_EN    BIT(0)

#define MT_CONN_ON_MISC         0x7c0600f0

/* Register remap */
#define MT_HIF_REMAP_L1         0x155024
#define MT_HIF_REMAP_L1_MASK    GENMASK(31, 16)
#define MT_HIF_REMAP_BASE_L1    0x130000

struct test_dev {
    struct pci_dev *pdev;
    void __iomem *regs;
    u32 backup_l1;
};

static u32 remap_read(struct test_dev *dev, u32 addr)
{
    u32 offset = addr & 0xffff;
    u32 base = (addr >> 16) & 0xffff;
    u32 val;

    dev->backup_l1 = readl(dev->regs + MT_HIF_REMAP_L1);
    writel((dev->backup_l1 & ~MT_HIF_REMAP_L1_MASK) | (base << 16),
           dev->regs + MT_HIF_REMAP_L1);
    readl(dev->regs + MT_HIF_REMAP_L1);

    val = readl(dev->regs + MT_HIF_REMAP_BASE_L1 + offset);

    writel(dev->backup_l1, dev->regs + MT_HIF_REMAP_L1);
    return val;
}

static void remap_write(struct test_dev *dev, u32 addr, u32 val)
{
    u32 offset = addr & 0xffff;
    u32 base = (addr >> 16) & 0xffff;

    dev->backup_l1 = readl(dev->regs + MT_HIF_REMAP_L1);
    writel((dev->backup_l1 & ~MT_HIF_REMAP_L1_MASK) | (base << 16),
           dev->regs + MT_HIF_REMAP_L1);
    readl(dev->regs + MT_HIF_REMAP_L1);

    writel(val, dev->regs + MT_HIF_REMAP_BASE_L1 + offset);

    writel(dev->backup_l1, dev->regs + MT_HIF_REMAP_L1);
}

static int test_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
    struct test_dev *dev;
    u32 val;
    int i, ret;

    dev_info(&pdev->dev, "=== MT7927 WiFi System Reset Test ===\n");

    dev = kzalloc(sizeof(*dev), GFP_KERNEL);
    if (!dev)
        return -ENOMEM;

    dev->pdev = pdev;
    pci_set_drvdata(pdev, dev);

    ret = pcim_enable_device(pdev);
    if (ret)
        goto err_free;

    ret = pcim_iomap_regions(pdev, BIT(0), "mt7927_test");
    if (ret)
        goto err_free;

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

    /* Test 1: Read current WiFi system state */
    dev_info(&pdev->dev, "Test 1: Reading WiFi system state\n");
    val = remap_read(dev, MT_WFSYS_SW_RST_B);
    dev_info(&pdev->dev, "  WFSYS_SW_RST_B: 0x%08x\n", val);
    dev_info(&pdev->dev, "  Reset enable: %s\n",
             (val & MT_WFSYS_SW_RST_B_EN) ? "YES" : "NO");

    val = remap_read(dev, MT_CONN_ON_MISC);
    dev_info(&pdev->dev, "  CONN_ON_MISC: 0x%08x\n", val);

    /* Test 2: Assert reset */
    dev_info(&pdev->dev, "Test 2: Asserting WiFi system reset\n");
    val = remap_read(dev, MT_WFSYS_SW_RST_B);
    val &= ~MT_WFSYS_SW_RST_B_EN;
    remap_write(dev, MT_WFSYS_SW_RST_B, val);
    wmb();
    msleep(10);

    val = remap_read(dev, MT_WFSYS_SW_RST_B);
    dev_info(&pdev->dev, "  After assert: WFSYS_SW_RST_B = 0x%08x\n", val);

    /* Test 3: Deassert reset */
    dev_info(&pdev->dev, "Test 3: Deasserting WiFi system reset\n");
    val |= MT_WFSYS_SW_RST_B_EN;
    remap_write(dev, MT_WFSYS_SW_RST_B, val);
    wmb();

    /* Wait for reset to complete */
    for (i = 0; i < 100; i++) {
        val = remap_read(dev, MT_WFSYS_SW_RST_B);
        if (val & MT_WFSYS_SW_RST_B_EN) {
            dev_info(&pdev->dev, "  Reset complete after %d ms\n", i * 10);
            break;
        }
        msleep(10);
    }

    if (i >= 100) {
        dev_warn(&pdev->dev, "  Reset timeout\n");
    }

    /* Test 4: Verify system state after reset */
    dev_info(&pdev->dev, "Test 4: Verifying state after reset\n");
    val = remap_read(dev, MT_WFSYS_SW_RST_B);
    dev_info(&pdev->dev, "  Final WFSYS_SW_RST_B: 0x%08x\n", val);

    val = remap_read(dev, MT_CONN_ON_MISC);
    dev_info(&pdev->dev, "  Final CONN_ON_MISC: 0x%08x\n", val);

    dev_info(&pdev->dev, "=== WiFi System Reset Test Complete ===\n");

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
    .name = "mt7927_test_reset",
    .id_table = test_ids,
    .probe = test_probe,
    .remove = test_remove,
};

module_pci_driver(test_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("MT7927 WiFi System Reset Test Module");
