/*
 * MT7927 DMA Queue Test Module
 * Tests DMA ring allocation and configuration
 */

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/dma-mapping.h>
#include <linux/delay.h>

#define MT7927_VENDOR_ID        0x14c3
#define MT7927_DEVICE_ID        0x7927

/* WFDMA registers - BAR0 offsets (WFDMA at BAR0+0x2000) */
#define MT_WFDMA0_BASE          0x2000
#define MT_WFDMA0_GLO_CFG       (MT_WFDMA0_BASE + 0x208)
#define MT_WFDMA0_TX_RING0_BASE (MT_WFDMA0_BASE + 0x300)
#define MT_WFDMA0_TX_RING0_CNT  (MT_WFDMA0_BASE + 0x304)
#define MT_WFDMA0_TX_RING0_CIDX (MT_WFDMA0_BASE + 0x308)
#define MT_WFDMA0_TX_RING0_DIDX (MT_WFDMA0_BASE + 0x30c)
#define MT_WFDMA0_RST_DTX_PTR   (MT_WFDMA0_BASE + 0x20c)

/* DMA descriptor */
struct test_desc {
    __le32 buf0;
    __le32 ctrl;
    __le32 buf1;
    __le32 info;
} __packed;

#define TEST_RING_SIZE  128

struct test_dev {
    struct pci_dev *pdev;
    void __iomem *regs;
    struct test_desc *ring;
    dma_addr_t ring_dma;
};

static int test_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
    struct test_dev *dev;
    u32 val;
    int ret;

    dev_info(&pdev->dev, "=== MT7927 DMA Queue Test ===\n");

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

    dev->regs = pcim_iomap_table(pdev)[0];  /* Use BAR0 (2MB) for register access */
    if (!dev->regs) {
        dev_err(&pdev->dev, "Failed to map BAR0\n");
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

    ret = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));
    if (ret) {
        dev_err(&pdev->dev, "Failed to set DMA mask\n");
        goto err_free;
    }

    /* Test 1: Read current DMA state */
    dev_info(&pdev->dev, "Test 1: Current DMA configuration\n");
    val = readl(dev->regs + MT_WFDMA0_GLO_CFG);
    dev_info(&pdev->dev, "  GLO_CFG: 0x%08x\n", val);
    dev_info(&pdev->dev, "  TX_DMA_EN: %d, RX_DMA_EN: %d\n",
             !!(val & BIT(0)), !!(val & BIT(2)));
    dev_info(&pdev->dev, "  TX_DMA_BUSY: %d, RX_DMA_BUSY: %d\n",
             !!(val & BIT(1)), !!(val & BIT(3)));

    /* Test 2: Allocate descriptor ring */
    dev_info(&pdev->dev, "Test 2: Allocating DMA descriptor ring\n");
    dev->ring = dma_alloc_coherent(&pdev->dev,
                                   TEST_RING_SIZE * sizeof(struct test_desc),
                                   &dev->ring_dma, GFP_KERNEL);
    if (!dev->ring) {
        dev_err(&pdev->dev, "  Failed to allocate ring\n");
        ret = -ENOMEM;
        goto err_free;
    }
    memset(dev->ring, 0, TEST_RING_SIZE * sizeof(struct test_desc));
    dev_info(&pdev->dev, "  Ring allocated at DMA addr 0x%llx\n",
             (u64)dev->ring_dma);

    /* Test 3: Reset TX DMA pointers */
    dev_info(&pdev->dev, "Test 3: Resetting DMA pointers\n");
    writel(0xffffffff, dev->regs + MT_WFDMA0_RST_DTX_PTR);
    wmb();
    msleep(10);
    dev_info(&pdev->dev, "  DMA pointers reset\n");

    /* Test 4: Configure TX ring 0 */
    dev_info(&pdev->dev, "Test 4: Configuring TX ring 0\n");
    writel(lower_32_bits(dev->ring_dma), dev->regs + MT_WFDMA0_TX_RING0_BASE);
    writel(upper_32_bits(dev->ring_dma), dev->regs + MT_WFDMA0_TX_RING0_BASE + 4);
    writel(TEST_RING_SIZE, dev->regs + MT_WFDMA0_TX_RING0_CNT);
    writel(0, dev->regs + MT_WFDMA0_TX_RING0_CIDX);
    wmb();

    /* Verify configuration */
    val = readl(dev->regs + MT_WFDMA0_TX_RING0_BASE);
    dev_info(&pdev->dev, "  Ring base read back: 0x%08x (expected: 0x%08x)\n",
             val, lower_32_bits(dev->ring_dma));

    val = readl(dev->regs + MT_WFDMA0_TX_RING0_CNT);
    dev_info(&pdev->dev, "  Ring count read back: %d (expected: %d)\n",
             val, TEST_RING_SIZE);

    val = readl(dev->regs + MT_WFDMA0_TX_RING0_CIDX);
    dev_info(&pdev->dev, "  CPU index: %d\n", val);

    val = readl(dev->regs + MT_WFDMA0_TX_RING0_DIDX);
    dev_info(&pdev->dev, "  DMA index: %d\n", val);

    /* Test 5: Try to enable DMA */
    dev_info(&pdev->dev, "Test 5: Enabling DMA\n");
    val = readl(dev->regs + MT_WFDMA0_GLO_CFG);
    val |= BIT(0) | BIT(2) | BIT(6);  /* TX_EN, RX_EN, WB_DDONE */
    writel(val, dev->regs + MT_WFDMA0_GLO_CFG);
    wmb();
    msleep(10);

    val = readl(dev->regs + MT_WFDMA0_GLO_CFG);
    dev_info(&pdev->dev, "  GLO_CFG after enable: 0x%08x\n", val);
    if (val & BIT(0))
        dev_info(&pdev->dev, "  TX DMA enabled successfully!\n");
    else
        dev_warn(&pdev->dev, "  TX DMA enable FAILED\n");

    if (val & BIT(2))
        dev_info(&pdev->dev, "  RX DMA enabled successfully!\n");
    else
        dev_warn(&pdev->dev, "  RX DMA enable FAILED\n");

    dev_info(&pdev->dev, "=== DMA Queue Test Complete ===\n");

    /* Note: Don't free the ring here to keep testing */
    return 0;

err_free:
    kfree(dev);
    return ret;
}

static void test_remove(struct pci_dev *pdev)
{
    struct test_dev *dev = pci_get_drvdata(pdev);

    dev_info(&pdev->dev, "Removing test module\n");

    /* Disable DMA */
    writel(0, dev->regs + MT_WFDMA0_GLO_CFG);

    /* Free ring */
    if (dev->ring)
        dma_free_coherent(&pdev->dev,
                         TEST_RING_SIZE * sizeof(struct test_desc),
                         dev->ring, dev->ring_dma);

    kfree(dev);
}

static const struct pci_device_id test_ids[] = {
    { PCI_DEVICE(MT7927_VENDOR_ID, MT7927_DEVICE_ID) },
    { }
};
MODULE_DEVICE_TABLE(pci, test_ids);

static struct pci_driver test_driver = {
    .name = "mt7927_test_dma",
    .id_table = test_ids,
    .probe = test_probe,
    .remove = test_remove,
};

module_pci_driver(test_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("MT7927 DMA Queue Test Module");
