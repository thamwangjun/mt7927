// SPDX-License-Identifier: GPL-2.0
/*
 * MT7927 DMA Path Verification Test
 *
 * Purpose: Verify if DMA requests are reaching the host IOMMU.
 *
 * In Phase 27, we had AMD-Vi IO_PAGE_FAULT errors when DMA tried to access
 * address 0x0. After fixing descriptors (Phase 27c), page faults stopped,
 * but DMA still doesn't work (MEM_ERR=1, DIDX=0).
 *
 * This test deliberately sets a ring's BASE to 0 and kicks DMA.
 * If we get an IOMMU page fault → DMA path works, problem is elsewhere
 * If no page fault → DMA path itself is broken
 */

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/dma-mapping.h>
#include <linux/delay.h>

#define MT7927_VENDOR_ID        0x14c3
#define MT7927_DEVICE_ID        0x7927

/* WFDMA registers (BAR0 offsets) - correct base 0xd4000 */
#define MT_WFDMA0_BASE              0xd4000
#define MT_WFDMA0_GLO_CFG           (MT_WFDMA0_BASE + 0x208)
#define MT_WFDMA0_RST               (MT_WFDMA0_BASE + 0x100)
#define MT_WFDMA0_RST_LOGIC_RST         BIT(4)
#define MT_WFDMA0_RST_DMASHDL_ALL_RST   BIT(5)

#define MT_WFDMA0_TX_RING_BASE(n)   (MT_WFDMA0_BASE + 0x300 + (n) * 0x10)
#define MT_WFDMA0_TX_RING_CNT(n)    (MT_WFDMA0_BASE + 0x304 + (n) * 0x10)
#define MT_WFDMA0_TX_RING_CIDX(n)   (MT_WFDMA0_BASE + 0x308 + (n) * 0x10)
#define MT_WFDMA0_TX_RING_DIDX(n)   (MT_WFDMA0_BASE + 0x30c + (n) * 0x10)

#define MT_WFDMA0_TX_RING_EXT_CTRL(n) (MT_WFDMA0_BASE + 0x600 + (n) * 4)

/* MCU_INT_STA for error checking */
#define MT_WFDMA0_MCU_INT_STA       (MT_WFDMA0_BASE + 0x110)

/* HOST2MCU doorbell */
#define MT_HOST2MCU_SW_INT_SET      (MT_WFDMA0_BASE + 0x108)

/* CB_INFRA registers (direct BAR0 offsets) */
#define CB_INFRA_PCIE_REMAP_WF          0x1f6554
#define CB_INFRA_PCIE_REMAP_WF_BT       0x1f6558
#define CB_INFRA_PCIE_REMAP_WF_VALUE    0x74037001
#define CB_INFRA_PCIE_REMAP_WF_BT_VALUE 0x70007000

/* MCU state register */
#define MT_MCU_ROMCODE_INDEX            0x0c1604
#define MCU_IDLE                        0x1D1E

/* GLO_CFG bits */
#define MT_WFDMA0_GLO_CFG_TX_DMA_EN             BIT(0)
#define MT_WFDMA0_GLO_CFG_PDMA_BT_SIZE          (3 << 4)
#define MT_WFDMA0_GLO_CFG_TX_WB_DDONE           BIT(6)
#define MT_WFDMA0_GLO_CFG_CSR_AXI_BUFRDY_BYP    BIT(11)
#define MT_WFDMA0_GLO_CFG_FIFO_LITTLE_ENDIAN    BIT(12)
#define MT_WFDMA0_GLO_CFG_CSR_RX_WB_DDONE       BIT(13)
#define MT_WFDMA0_GLO_CFG_CSR_DISP_BASE_PTR_CHAIN_EN BIT(15)
#define MT_WFDMA0_GLO_CFG_CSR_LBK_RX_Q_SEL_EN   BIT(20)
#define MT_WFDMA0_GLO_CFG_OMIT_RX_INFO_PFET2    BIT(21)
#define MT_WFDMA0_GLO_CFG_OMIT_TX_INFO          BIT(28)
#define MT_WFDMA0_GLO_CFG_CLK_GATE_DIS          BIT(30)

#define MT_WFDMA0_GLO_CFG_SETUP \
	(MT_WFDMA0_GLO_CFG_PDMA_BT_SIZE | \
	 MT_WFDMA0_GLO_CFG_TX_WB_DDONE | \
	 MT_WFDMA0_GLO_CFG_CSR_AXI_BUFRDY_BYP | \
	 MT_WFDMA0_GLO_CFG_FIFO_LITTLE_ENDIAN | \
	 MT_WFDMA0_GLO_CFG_CSR_RX_WB_DDONE | \
	 MT_WFDMA0_GLO_CFG_CSR_DISP_BASE_PTR_CHAIN_EN | \
	 MT_WFDMA0_GLO_CFG_CSR_LBK_RX_Q_SEL_EN | \
	 MT_WFDMA0_GLO_CFG_OMIT_RX_INFO_PFET2 | \
	 MT_WFDMA0_GLO_CFG_OMIT_TX_INFO | \
	 MT_WFDMA0_GLO_CFG_CLK_GATE_DIS)

/* Use ring 16 (FWDL) - this is the ring that caused page faults in Phase 27 */
#define TEST_RING_IDX   16
#define RING_SIZE       4

/* DMA descriptor */
struct mt7927_desc {
	__le32 buf0;
	__le32 ctrl;
	__le32 buf1;
	__le32 info;
} __packed __aligned(4);

/* Descriptor control bits */
/* OLD (buggy) descriptor format - should cause page faults at addr 0!
 * This proves the Phase 27 page faults were from wrong descriptor format,
 * not from working DMA. */
#define MT_DMA_CTL_SD_LEN0      GENMASK(13, 0)   /* OLD: bits 0-13 (WRONG!) */
#define MT_DMA_CTL_LAST_SEC0    BIT(14)          /* OLD: bit 14 (WRONG!) */
#define MT_DMA_CTL_DMA_DONE     BIT(31)

struct test_dev {
	struct pci_dev *pdev;
	void __iomem *regs;

	/* Valid ring for comparison */
	struct mt7927_desc *valid_ring;
	dma_addr_t valid_ring_dma;

	/* Data buffer */
	void *dma_buf;
	dma_addr_t dma_buf_phys;
};

static u32 mt_rr(struct test_dev *dev, u32 offset)
{
	return readl(dev->regs + offset);
}

static void mt_wr(struct test_dev *dev, u32 offset, u32 val)
{
	writel(val, dev->regs + offset);
}

static int test_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct test_dev *dev;
	u32 val, didx_before, didx_after;
	int ret, i;

	dev_info(&pdev->dev, "\n");
	dev_info(&pdev->dev, "==============================================\n");
	dev_info(&pdev->dev, "  MT7927 DMA PATH VERIFICATION TEST\n");
	dev_info(&pdev->dev, "==============================================\n");
	dev_info(&pdev->dev, "\n");
	dev_info(&pdev->dev, "Purpose: Verify if DMA requests reach host IOMMU\n");
	dev_info(&pdev->dev, "\n");
	dev_info(&pdev->dev, "If you see 'AMD-Vi: Event logged [IO_PAGE_FAULT]'\n");
	dev_info(&pdev->dev, "in dmesg after this test, DMA path is WORKING.\n");
	dev_info(&pdev->dev, "\n");
	dev_info(&pdev->dev, "If NO page fault appears, DMA path is BROKEN.\n");
	dev_info(&pdev->dev, "\n");

	dev = devm_kzalloc(&pdev->dev, sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;

	dev->pdev = pdev;
	pci_set_drvdata(pdev, dev);

	ret = pcim_enable_device(pdev);
	if (ret) {
		dev_err(&pdev->dev, "Failed to enable PCI device\n");
		return ret;
	}

	ret = pcim_iomap_regions(pdev, BIT(0), "mt7927_dma_test");
	if (ret) {
		dev_err(&pdev->dev, "Failed to map BAR0\n");
		return ret;
	}

	dev->regs = pcim_iomap_table(pdev)[0];
	if (!dev->regs) {
		dev_err(&pdev->dev, "BAR0 mapping is NULL\n");
		return -ENOMEM;
	}

	pci_set_master(pdev);

	/* Safety check */
	val = mt_rr(dev, 0x10000);  /* Chip ID at BAR0+0x10000 */
	dev_info(&pdev->dev, "Chip ID: 0x%08x\n", val);
	if (val == 0xffffffff) {
		dev_err(&pdev->dev, "Chip not responding - need PCI rescan\n");
		return -EIO;
	}

	/* Check MCU state - is it initialized? */
	val = mt_rr(dev, MT_MCU_ROMCODE_INDEX);
	dev_info(&pdev->dev, "MCU state: 0x%08x (IDLE=0x%04x)\n", val, MCU_IDLE);
	if ((val & 0xFFFF) != MCU_IDLE) {
		dev_warn(&pdev->dev, "MCU NOT in IDLE state! DMA may not work.\n");
		dev_warn(&pdev->dev, "Run test_fw_load.ko first for full init.\n");
	}

	ret = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));
	if (ret) {
		dev_err(&pdev->dev, "Failed to set DMA mask\n");
		return ret;
	}

	/* ============================================
	 * Step 1: Basic initialization (minimal)
	 * ============================================ */
	dev_info(&pdev->dev, "\n--- Step 1: Basic initialization ---\n");

	/* CB_INFRA PCIe remap (required for WFDMA access) */
	mt_wr(dev, CB_INFRA_PCIE_REMAP_WF, CB_INFRA_PCIE_REMAP_WF_VALUE);
	mt_wr(dev, CB_INFRA_PCIE_REMAP_WF_BT, CB_INFRA_PCIE_REMAP_WF_BT_VALUE);
	dev_info(&pdev->dev, "CB_INFRA remap set: WF=0x%08x WF_BT=0x%08x\n",
		 mt_rr(dev, CB_INFRA_PCIE_REMAP_WF),
		 mt_rr(dev, CB_INFRA_PCIE_REMAP_WF_BT));

	/* DMA reset */
	mt_wr(dev, MT_WFDMA0_RST, MT_WFDMA0_RST_LOGIC_RST | MT_WFDMA0_RST_DMASHDL_ALL_RST);
	usleep_range(1000, 2000);
	dev_info(&pdev->dev, "DMA reset: RST=0x%08x\n", mt_rr(dev, MT_WFDMA0_RST));

	/* GLO_CFG setup (without TX_DMA_EN initially) */
	mt_wr(dev, MT_WFDMA0_GLO_CFG, MT_WFDMA0_GLO_CFG_SETUP);
	dev_info(&pdev->dev, "GLO_CFG setup: 0x%08x\n", mt_rr(dev, MT_WFDMA0_GLO_CFG));

	/* ============================================
	 * Step 2: Allocate valid ring and buffer
	 * ============================================ */
	dev_info(&pdev->dev, "\n--- Step 2: Allocate DMA resources ---\n");

	dev->valid_ring = dma_alloc_coherent(&pdev->dev,
					     RING_SIZE * sizeof(struct mt7927_desc),
					     &dev->valid_ring_dma, GFP_KERNEL);
	if (!dev->valid_ring) {
		dev_err(&pdev->dev, "Failed to allocate ring\n");
		return -ENOMEM;
	}

	dev->dma_buf = dma_alloc_coherent(&pdev->dev, 4096,
					  &dev->dma_buf_phys, GFP_KERNEL);
	if (!dev->dma_buf) {
		dev_err(&pdev->dev, "Failed to allocate buffer\n");
		dma_free_coherent(&pdev->dev, RING_SIZE * sizeof(struct mt7927_desc),
				  dev->valid_ring, dev->valid_ring_dma);
		return -ENOMEM;
	}

	dev_info(&pdev->dev, "Ring allocated at DMA addr: 0x%llx\n",
		 (unsigned long long)dev->valid_ring_dma);
	dev_info(&pdev->dev, "Buffer allocated at DMA addr: 0x%llx\n",
		 (unsigned long long)dev->dma_buf_phys);

	/* ============================================
	 * Step 3: Configure ring with VALID address first
	 * ============================================ */
	dev_info(&pdev->dev, "\n--- Step 3: Configure ring with VALID address ---\n");

	/* Initialize descriptors */
	memset(dev->valid_ring, 0, RING_SIZE * sizeof(struct mt7927_desc));
	for (i = 0; i < RING_SIZE; i++) {
		dev->valid_ring[i].ctrl = cpu_to_le32(MT_DMA_CTL_DMA_DONE);
	}
	wmb();

	/* Configure ring 0 with valid address */
	mt_wr(dev, MT_WFDMA0_TX_RING_BASE(TEST_RING_IDX), dev->valid_ring_dma);
	mt_wr(dev, MT_WFDMA0_TX_RING_CNT(TEST_RING_IDX), RING_SIZE);
	mt_wr(dev, MT_WFDMA0_TX_RING_CIDX(TEST_RING_IDX), 0);
	mt_wr(dev, MT_WFDMA0_TX_RING_EXT_CTRL(TEST_RING_IDX), 0x01000004);

	dev_info(&pdev->dev, "Ring %d: BASE=0x%08x CNT=%d CIDX=%d\n",
		 TEST_RING_IDX,
		 mt_rr(dev, MT_WFDMA0_TX_RING_BASE(TEST_RING_IDX)),
		 mt_rr(dev, MT_WFDMA0_TX_RING_CNT(TEST_RING_IDX)),
		 mt_rr(dev, MT_WFDMA0_TX_RING_CIDX(TEST_RING_IDX)));

	/* Enable TX DMA */
	mt_wr(dev, MT_WFDMA0_GLO_CFG, MT_WFDMA0_GLO_CFG_SETUP | MT_WFDMA0_GLO_CFG_TX_DMA_EN);
	dev_info(&pdev->dev, "GLO_CFG with TX_DMA_EN: 0x%08x\n", mt_rr(dev, MT_WFDMA0_GLO_CFG));

	/* ============================================
	 * Step 4: TEST A - Try DMA with VALID address
	 * ============================================ */
	dev_info(&pdev->dev, "\n--- Step 4: TEST A - DMA with VALID address ---\n");

	/* Setup descriptor with OLD (buggy) format - should cause page fault!
	 * OLD format: buf1 = upper 32 bits, length in ctrl bits 0-13
	 * Hardware will interpret buf1 (which is 0 for 32-bit addrs) as data pointer */
	dev->valid_ring[0].buf0 = cpu_to_le32(lower_32_bits(dev->dma_buf_phys));
	dev->valid_ring[0].ctrl = cpu_to_le32(FIELD_PREP(MT_DMA_CTL_SD_LEN0, 64) |
					      MT_DMA_CTL_LAST_SEC0);
	dev->valid_ring[0].buf1 = cpu_to_le32(upper_32_bits(dev->dma_buf_phys)); /* OLD: upper bits here */
	dev->valid_ring[0].info = cpu_to_le32(0);  /* OLD: info was 0 */
	wmb();

	dev_info(&pdev->dev, "Descriptor: buf0=0x%08x ctrl=0x%08x\n",
		 le32_to_cpu(dev->valid_ring[0].buf0),
		 le32_to_cpu(dev->valid_ring[0].ctrl));

	didx_before = mt_rr(dev, MT_WFDMA0_TX_RING_DIDX(TEST_RING_IDX));
	dev_info(&pdev->dev, "Before kick: DIDX=%d\n", didx_before);

	/* Kick DMA */
	mt_wr(dev, MT_WFDMA0_TX_RING_CIDX(TEST_RING_IDX), 1);
	wmb();

	/* Ring doorbell */
	mt_wr(dev, MT_HOST2MCU_SW_INT_SET, BIT(0));
	wmb();

	/* Wait a bit */
	msleep(100);

	didx_after = mt_rr(dev, MT_WFDMA0_TX_RING_DIDX(TEST_RING_IDX));
	val = mt_rr(dev, MT_WFDMA0_MCU_INT_STA);

	dev_info(&pdev->dev, "After kick: DIDX=%d MCU_INT_STA=0x%08x\n", didx_after, val);
	dev_info(&pdev->dev, "  MEM_ERR=%d DMA_ERR=%d\n", !!(val & BIT(0)), !!(val & BIT(1)));

	if (didx_after == 1) {
		dev_info(&pdev->dev, ">>> TEST A PASSED: DMA processed descriptor!\n");
	} else {
		dev_info(&pdev->dev, ">>> TEST A: DIDX stuck (expected with current blocker)\n");
	}

	/* ============================================
	 * Step 5: TEST B - INTENTIONALLY SET BASE=0
	 * ============================================ */
	dev_info(&pdev->dev, "\n--- Step 5: TEST B - INTENTIONALLY SET BASE=0 ---\n");
	dev_info(&pdev->dev, ">>> This SHOULD cause IOMMU page fault if DMA path works!\n");

	/* Disable DMA first */
	mt_wr(dev, MT_WFDMA0_GLO_CFG, MT_WFDMA0_GLO_CFG_SETUP);
	usleep_range(1000, 2000);

	/* Reset ring */
	mt_wr(dev, MT_WFDMA0_TX_RING_CIDX(TEST_RING_IDX), 0);

	/* DELIBERATELY set BASE to 0 (INVALID!) */
	mt_wr(dev, MT_WFDMA0_TX_RING_BASE(TEST_RING_IDX), 0);
	mt_wr(dev, MT_WFDMA0_TX_RING_CNT(TEST_RING_IDX), RING_SIZE);

	dev_info(&pdev->dev, "Ring %d set to INVALID: BASE=0x%08x\n",
		 TEST_RING_IDX,
		 mt_rr(dev, MT_WFDMA0_TX_RING_BASE(TEST_RING_IDX)));

	/* Re-enable DMA */
	mt_wr(dev, MT_WFDMA0_GLO_CFG, MT_WFDMA0_GLO_CFG_SETUP | MT_WFDMA0_GLO_CFG_TX_DMA_EN);

	didx_before = mt_rr(dev, MT_WFDMA0_TX_RING_DIDX(TEST_RING_IDX));
	dev_info(&pdev->dev, "Before kick: DIDX=%d\n", didx_before);

	/* Kick DMA - this should try to read from address 0! */
	mt_wr(dev, MT_WFDMA0_TX_RING_CIDX(TEST_RING_IDX), 1);
	wmb();

	/* Ring doorbell */
	mt_wr(dev, MT_HOST2MCU_SW_INT_SET, BIT(0));
	wmb();

	dev_info(&pdev->dev, "DMA kicked with BASE=0. Waiting 500ms...\n");

	/* Wait longer for IOMMU to catch and log the fault */
	msleep(500);

	didx_after = mt_rr(dev, MT_WFDMA0_TX_RING_DIDX(TEST_RING_IDX));
	val = mt_rr(dev, MT_WFDMA0_MCU_INT_STA);

	dev_info(&pdev->dev, "After kick: DIDX=%d MCU_INT_STA=0x%08x\n", didx_after, val);

	/* ============================================
	 * Summary
	 * ============================================ */
	dev_info(&pdev->dev, "\n");
	dev_info(&pdev->dev, "==============================================\n");
	dev_info(&pdev->dev, "  TEST COMPLETE - CHECK DMESG FOR PAGE FAULTS\n");
	dev_info(&pdev->dev, "==============================================\n");
	dev_info(&pdev->dev, "\n");
	dev_info(&pdev->dev, "Run: dmesg | grep -i 'page.fault\\|amd-vi\\|dmar'\n");
	dev_info(&pdev->dev, "\n");
	dev_info(&pdev->dev, "If you see 'IO_PAGE_FAULT' at address 0x0:\n");
	dev_info(&pdev->dev, "  -> DMA path WORKS, problem is elsewhere\n");
	dev_info(&pdev->dev, "\n");
	dev_info(&pdev->dev, "If NO page fault appeared:\n");
	dev_info(&pdev->dev, "  -> DMA path is BROKEN, DMA not reaching host\n");
	dev_info(&pdev->dev, "\n");

	/* Cleanup - disable DMA */
	mt_wr(dev, MT_WFDMA0_GLO_CFG, MT_WFDMA0_GLO_CFG_SETUP);

	/* Free resources */
	dma_free_coherent(&pdev->dev, 4096, dev->dma_buf, dev->dma_buf_phys);
	dma_free_coherent(&pdev->dev, RING_SIZE * sizeof(struct mt7927_desc),
			  dev->valid_ring, dev->valid_ring_dma);

	return -ENODEV; /* Don't bind - this is just a test */
}

static void test_remove(struct pci_dev *pdev)
{
	/* Resources freed in probe */
}

static const struct pci_device_id test_ids[] = {
	{ PCI_DEVICE(MT7927_VENDOR_ID, MT7927_DEVICE_ID) },
	{ }
};
MODULE_DEVICE_TABLE(pci, test_ids);

static struct pci_driver test_driver = {
	.name = "mt7927_dma_path_test",
	.id_table = test_ids,
	.probe = test_probe,
	.remove = test_remove,
};

module_pci_driver(test_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("MT7927 Project");
MODULE_DESCRIPTION("MT7927 DMA Path Verification - Intentional page fault test");
