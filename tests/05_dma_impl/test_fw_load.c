/*
 * MT7927 Firmware Load Test Module
 * Integration test for complete firmware loading sequence
 */

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/firmware.h>
#include <linux/dma-mapping.h>
#include <linux/delay.h>
#include <linux/skbuff.h>

#define MT7927_VENDOR_ID        0x14c3
#define MT7927_DEVICE_ID        0x7927

/* Firmware files */
#define FW_RAM   "mediatek/mt7925/WIFI_RAM_CODE_MT7925_1_1.bin"
#define FW_PATCH "mediatek/mt7925/WIFI_MT7925_PATCH_MCU_1_1_hdr.bin"

/* Key registers */
#define MT_WFDMA0_GLO_CFG           0x0208
#define MT_WFDMA0_HOST_INT_STA      0x0200
#define MT_WFDMA0_HOST_INT_ENA      0x0204
#define MT_WFDMA0_RST_DTX_PTR       0x020c
#define MT_WFDMA0_TX_RING_BASE(n)   (0x0300 + (n) * 0x10)

#define MT_HIF_REMAP_L1             0x155024
#define MT_HIF_REMAP_L1_MASK        GENMASK(31, 16)
#define MT_HIF_REMAP_BASE_L1        0x130000

#define MT_CONN_ON_LPCTL            0x7c060010
#define MT_CONN_ON_LPCTL_HOST_OWN   BIT(0)
#define MT_CONN_ON_LPCTL_FW_OWN     BIT(1)

#define MT_WFSYS_SW_RST_B           0x7c000140
#define MT_WFSYS_SW_RST_B_EN        BIT(0)

/* DMA descriptor */
struct test_desc {
    __le32 buf0;
    __le32 ctrl;
    __le32 buf1;
    __le32 info;
} __packed;

#define FWDL_QUEUE_IDX      16
#define FWDL_RING_SIZE      128
#define FW_CHUNK_SIZE       (64 * 1024)

struct test_dev {
    struct pci_dev *pdev;
    void __iomem *regs;
    u32 backup_l1;
    
    /* FWDL ring */
    struct test_desc *ring;
    dma_addr_t ring_dma;
    int head;
    
    /* Firmware buffer */
    void *fw_buf;
    dma_addr_t fw_dma;
    
    /* Firmware files */
    const struct firmware *fw_ram;
    const struct firmware *fw_patch;
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

static int setup_power_control(struct test_dev *dev)
{
    u32 val;
    int i;

    dev_info(&dev->pdev->dev, "Setting up power control...\n");

    /* Give control to firmware first */
    remap_write(dev, MT_CONN_ON_LPCTL, MT_CONN_ON_LPCTL_FW_OWN);
    msleep(10);

    /* Take control back for driver */
    remap_write(dev, MT_CONN_ON_LPCTL, MT_CONN_ON_LPCTL_HOST_OWN);

    for (i = 0; i < 200; i++) {
        val = remap_read(dev, MT_CONN_ON_LPCTL);
        if (!(val & MT_CONN_ON_LPCTL_FW_OWN)) {
            dev_info(&dev->pdev->dev, "  Driver control acquired\n");
            return 0;
        }
        msleep(10);
    }

    dev_warn(&dev->pdev->dev, "  Power control timeout\n");
    return -ETIMEDOUT;
}

static int reset_wfsys(struct test_dev *dev)
{
    u32 val;
    int i;

    dev_info(&dev->pdev->dev, "Resetting WiFi subsystem...\n");

    /* Assert reset */
    val = remap_read(dev, MT_WFSYS_SW_RST_B);
    val &= ~MT_WFSYS_SW_RST_B_EN;
    remap_write(dev, MT_WFSYS_SW_RST_B, val);
    msleep(10);

    /* Deassert reset */
    val |= MT_WFSYS_SW_RST_B_EN;
    remap_write(dev, MT_WFSYS_SW_RST_B, val);

    for (i = 0; i < 100; i++) {
        val = remap_read(dev, MT_WFSYS_SW_RST_B);
        if (val & MT_WFSYS_SW_RST_B_EN) {
            dev_info(&dev->pdev->dev, "  Reset complete\n");
            return 0;
        }
        msleep(10);
    }

    dev_warn(&dev->pdev->dev, "  Reset timeout\n");
    return -ETIMEDOUT;
}

static int setup_dma(struct test_dev *dev)
{
    dev_info(&dev->pdev->dev, "Setting up DMA...\n");

    /* Allocate FWDL ring */
    dev->ring = dma_alloc_coherent(&dev->pdev->dev,
                                   FWDL_RING_SIZE * sizeof(struct test_desc),
                                   &dev->ring_dma, GFP_KERNEL);
    if (!dev->ring) {
        dev_err(&dev->pdev->dev, "  Failed to allocate ring\n");
        return -ENOMEM;
    }
    memset(dev->ring, 0, FWDL_RING_SIZE * sizeof(struct test_desc));
    dev->head = 0;

    /* Allocate firmware buffer */
    dev->fw_buf = dma_alloc_coherent(&dev->pdev->dev, FW_CHUNK_SIZE,
                                     &dev->fw_dma, GFP_KERNEL);
    if (!dev->fw_buf) {
        dev_err(&dev->pdev->dev, "  Failed to allocate FW buffer\n");
        return -ENOMEM;
    }

    /* Reset DMA pointers */
    writel(0xffffffff, dev->regs + MT_WFDMA0_RST_DTX_PTR);
    wmb();
    msleep(10);

    /* Configure FWDL ring */
    writel(lower_32_bits(dev->ring_dma),
           dev->regs + MT_WFDMA0_TX_RING_BASE(FWDL_QUEUE_IDX));
    writel(upper_32_bits(dev->ring_dma),
           dev->regs + MT_WFDMA0_TX_RING_BASE(FWDL_QUEUE_IDX) + 4);
    writel(FWDL_RING_SIZE,
           dev->regs + MT_WFDMA0_TX_RING_BASE(FWDL_QUEUE_IDX) + 8);
    writel(0, dev->regs + MT_WFDMA0_TX_RING_BASE(FWDL_QUEUE_IDX) + 12);
    wmb();

    /* Enable DMA */
    writel(BIT(0) | BIT(2) | BIT(6), dev->regs + MT_WFDMA0_GLO_CFG);
    wmb();

    dev_info(&dev->pdev->dev, "  DMA configured, GLO_CFG=0x%08x\n",
             readl(dev->regs + MT_WFDMA0_GLO_CFG));

    return 0;
}

static int send_fw_chunk(struct test_dev *dev, const void *data, size_t len)
{
    struct test_desc *desc;
    int idx;

    /* Copy data to DMA buffer */
    memcpy(dev->fw_buf, data, len);

    /* Setup descriptor */
    idx = dev->head;
    desc = &dev->ring[idx];
    desc->buf0 = cpu_to_le32(lower_32_bits(dev->fw_dma));
    desc->buf1 = cpu_to_le32(upper_32_bits(dev->fw_dma));
    desc->ctrl = cpu_to_le32(len | BIT(30));  /* len + LAST_SEC0 */
    desc->info = 0;
    wmb();

    /* Update CPU index to kick DMA */
    dev->head = (idx + 1) % FWDL_RING_SIZE;
    writel(dev->head, dev->regs + MT_WFDMA0_TX_RING_BASE(FWDL_QUEUE_IDX) + 12);
    wmb();

    /* Wait for completion */
    msleep(10);

    return 0;
}

static int test_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
    struct test_dev *dev;
    int ret;
    size_t offset, chunk_len;

    dev_info(&pdev->dev, "=== MT7927 Firmware Load Test ===\n");

    dev = kzalloc(sizeof(*dev), GFP_KERNEL);
    if (!dev)
        return -ENOMEM;

    dev->pdev = pdev;
    pci_set_drvdata(pdev, dev);

    ret = pcim_enable_device(pdev);
    if (ret)
        goto err_free;

    ret = pcim_iomap_regions(pdev, BIT(2), "mt7927_test");
    if (ret)
        goto err_free;

    dev->regs = pcim_iomap_table(pdev)[2];
    pci_set_master(pdev);

    ret = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));
    if (ret)
        goto err_free;

    /* Step 1: Load firmware files */
    dev_info(&pdev->dev, "Step 1: Loading firmware files\n");
    ret = request_firmware(&dev->fw_patch, FW_PATCH, &pdev->dev);
    if (ret) {
        dev_err(&pdev->dev, "  Failed to load patch: %s\n", FW_PATCH);
        goto err_free;
    }
    dev_info(&pdev->dev, "  Patch loaded: %zu bytes\n", dev->fw_patch->size);

    ret = request_firmware(&dev->fw_ram, FW_RAM, &pdev->dev);
    if (ret) {
        dev_err(&pdev->dev, "  Failed to load RAM: %s\n", FW_RAM);
        goto err_release_patch;
    }
    dev_info(&pdev->dev, "  RAM loaded: %zu bytes\n", dev->fw_ram->size);

    /* Step 2: Setup power control */
    dev_info(&pdev->dev, "Step 2: Power control\n");
    ret = setup_power_control(dev);
    if (ret)
        dev_warn(&pdev->dev, "  Power control failed (continuing)\n");

    /* Step 3: Reset WiFi subsystem */
    dev_info(&pdev->dev, "Step 3: WiFi system reset\n");
    ret = reset_wfsys(dev);
    if (ret)
        dev_warn(&pdev->dev, "  Reset failed (continuing)\n");

    /* Step 4: Setup DMA */
    dev_info(&pdev->dev, "Step 4: DMA setup\n");
    ret = setup_dma(dev);
    if (ret)
        goto err_release_ram;

    /* Step 5: Send firmware via DMA */
    dev_info(&pdev->dev, "Step 5: Sending firmware via DMA\n");
    
    offset = 0;
    while (offset < dev->fw_ram->size) {
        chunk_len = min_t(size_t, FW_CHUNK_SIZE, dev->fw_ram->size - offset);
        ret = send_fw_chunk(dev, dev->fw_ram->data + offset, chunk_len);
        if (ret)
            break;
        offset += chunk_len;
        dev_info(&pdev->dev, "  Sent %zu / %zu bytes\n", offset, dev->fw_ram->size);
    }

    /* Step 6: Check final state */
    dev_info(&pdev->dev, "Step 6: Final state\n");
    dev_info(&pdev->dev, "  GLO_CFG: 0x%08x\n", readl(dev->regs + MT_WFDMA0_GLO_CFG));
    dev_info(&pdev->dev, "  INT_STA: 0x%08x\n", readl(dev->regs + MT_WFDMA0_HOST_INT_STA));

    dev_info(&pdev->dev, "=== Firmware Load Test Complete ===\n");
    dev_info(&pdev->dev, "Note: Full driver needed for complete initialization\n");

    return 0;

err_release_ram:
    release_firmware(dev->fw_ram);
err_release_patch:
    release_firmware(dev->fw_patch);
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

    if (dev->fw_buf)
        dma_free_coherent(&pdev->dev, FW_CHUNK_SIZE, dev->fw_buf, dev->fw_dma);
    if (dev->ring)
        dma_free_coherent(&pdev->dev, FWDL_RING_SIZE * sizeof(struct test_desc),
                         dev->ring, dev->ring_dma);

    release_firmware(dev->fw_ram);
    release_firmware(dev->fw_patch);
    kfree(dev);
}

static const struct pci_device_id test_ids[] = {
    { PCI_DEVICE(MT7927_VENDOR_ID, MT7927_DEVICE_ID) },
    { }
};
MODULE_DEVICE_TABLE(pci, test_ids);

static struct pci_driver test_driver = {
    .name = "mt7927_test_fw",
    .id_table = test_ids,
    .probe = test_probe,
    .remove = test_remove,
};

module_pci_driver(test_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("MT7927 Firmware Load Test Module");
MODULE_FIRMWARE(FW_RAM);
MODULE_FIRMWARE(FW_PATCH);
