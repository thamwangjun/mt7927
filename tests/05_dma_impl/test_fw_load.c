// SPDX-License-Identifier: GPL-2.0
/*
 * MT7927 Firmware Load Test Module
 *
 * Implements the complete firmware loading sequence based on:
 * - docs/ZOUYONGHAO_ANALYSIS.md (polling-based, no mailbox)
 * - docs/MT6639_ANALYSIS.md (ring 15/16 configuration)
 * - reference_zouyonghao_mt7927/mt76-outoftree/mt7927_fw_load.c
 * - reference_zouyonghao_mt7927/mt76-outoftree/mt7925/pci_mcu.c
 *
 * Key requirements (from ZOUYONGHAO_ANALYSIS.md):
 * 1. Wake CONN_INFRA before anything else
 * 2. Wait for MCU IDLE state (0x1D1E)
 * 3. NO mailbox waits - ROM doesn't support mailbox protocol
 * 4. Aggressive TX cleanup before/after each chunk
 * 5. Polling delays (5-50ms) for ROM processing
 * 6. Skip FW_START, manually set SW_INIT_DONE
 */

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/firmware.h>
#include <linux/dma-mapping.h>
#include <linux/delay.h>

#define MT7927_VENDOR_ID        0x14c3
#define MT7927_DEVICE_ID        0x7927

/* Firmware files (MT7925 compatible per docs/FIRMWARE_ANALYSIS.md) */
#define FW_PATCH "mediatek/mt7925/WIFI_MT7925_PATCH_MCU_1_1_hdr.bin"
#define FW_RAM   "mediatek/mt7925/WIFI_RAM_CODE_MT7925_1_1.bin"

/* ============================================================
 * Register Definitions (from reference mt7927_regs.h)
 * ============================================================ */

/* Remap registers (BAR0 offsets) */
#define MT_HIF_REMAP_L1             0x155024
#define MT_HIF_REMAP_L1_MASK        GENMASK(31, 16)
#define MT_HIF_REMAP_BASE_L1        0x130000

/* WFDMA registers (BAR0 offsets) */
#define MT_WFDMA0_BASE              0x2000
#define MT_WFDMA0_HOST_INT_STA      (MT_WFDMA0_BASE + 0x200)
#define MT_WFDMA0_HOST_INT_ENA      (MT_WFDMA0_BASE + 0x204)
#define MT_WFDMA0_GLO_CFG           (MT_WFDMA0_BASE + 0x208)
#define MT_WFDMA0_RST_DTX_PTR       (MT_WFDMA0_BASE + 0x20c)

#define MT_WFDMA0_TX_RING_BASE(n)   (MT_WFDMA0_BASE + 0x300 + (n) * 0x10)
#define MT_WFDMA0_TX_RING_CNT(n)    (MT_WFDMA0_BASE + 0x304 + (n) * 0x10)
#define MT_WFDMA0_TX_RING_CIDX(n)   (MT_WFDMA0_BASE + 0x308 + (n) * 0x10)
#define MT_WFDMA0_TX_RING_DIDX(n)   (MT_WFDMA0_BASE + 0x30c + (n) * 0x10)

/* GLO_CFG bits */
#define MT_WFDMA0_GLO_CFG_TX_DMA_EN     BIT(0)
#define MT_WFDMA0_GLO_CFG_RX_DMA_EN     BIT(2)
#define MT_WFDMA0_GLO_CFG_TX_WB_DDONE   BIT(6)

/* Key addresses accessed via remap (from mt7927_regs.h) */
#define MT_CONN_ON_LPCTL                0x7c060010  /* Power management */
#define MT_WFSYS_SW_RST_B               0x7c000140  /* WiFi subsystem reset */
#define MT_MCU_ROMCODE_INDEX            0x81021604  /* MCU state */
#define MT_MCU_STATUS                   0x7c060204  /* MCU status */
#define MT_CONN_ON_MISC                 0x7c0600f0  /* MCU ready */

/* CONN_INFRA registers */
#define MT_CONNINFRA_WAKEUP             0x7C0601A0  /* CONN_INFRA wakeup */
#define MT_CONNINFRA_VERSION            0x7C011000  /* Version */
#define MT_WF_SUBSYS_RST                0x70028600  /* WiFi subsystem reset */
#define MT_CRYPTO_MCU_OWN               0x70025380  /* Crypto MCU ownership */

/* Expected values */
#define MCU_IDLE                        0x1D1E
#define CONNINFRA_VERSION_OK            0x03010002

/* LPCTL bits */
#define MT_LPCTL_HOST_OWN               BIT(0)
#define MT_LPCTL_FW_OWN                 BIT(1)
#define MT_LPCTL_OWN_SYNC               BIT(2)

/* WFSYS bits */
#define MT_WFSYS_SW_RST_B_EN            BIT(0)
#define MT_WFSYS_SW_INIT_DONE           BIT(4)

/* Ring configuration */
#define FWDL_RING_IDX           16      /* Ring 16 for firmware download */
#define FWDL_RING_SIZE          128
#define FW_CHUNK_SIZE           4096    /* 4KB chunks like reference */

/* DMA descriptor format */
struct mt7927_desc {
	__le32 buf0;
	__le32 ctrl;
	__le32 buf1;
	__le32 info;
} __packed;

/* Descriptor control bits */
#define MT_DMA_CTL_SD_LEN0      GENMASK(13, 0)
#define MT_DMA_CTL_LAST_SEC0    BIT(14)
#define MT_DMA_CTL_BURST        BIT(15)
#define MT_DMA_CTL_DMA_DONE     BIT(31)

/* MCU command codes */
#define MCU_CMD_TARGET_ADDRESS_LEN_REQ  0x01
#define MCU_CMD_PATCH_START_REQ         0x05
#define MCU_CMD_PATCH_FINISH_REQ        0x07
#define MCU_CMD_FW_SCATTER              0xee

/* MCU TXD header for init commands (not needed for FW_SCATTER) */
#define MCU_PKT_ID                      0xa0
#define MCU_PQ_ID(p, q)                 (((p) << 15) | ((q) << 10))
#define MT_TX_PORT_IDX_MCU              1
#define MT_TX_MCU_PORT_RX_Q0            0
#define MCU_S2D_H2N                     0  /* Host to WM (WiFi Manager) */
#define MCU_Q_NA                        0

/* TXD fields */
#define MT_TXD0_TX_BYTES        GENMASK(15, 0)
#define MT_TXD0_PKT_FMT         GENMASK(24, 23)
#define MT_TXD0_Q_IDX           GENMASK(31, 25)
#define MT_TX_TYPE_CMD          1
#define MT_TXD1_HDR_FORMAT      GENMASK(7, 5)
#define MT_HDR_FORMAT_CMD       0

/* MCU TXD structure (for init commands) */
struct mt7927_mcu_txd {
	__le32 txd[8];          /* Hardware TXD (32 bytes) */
	__le16 len;             /* Length after this header */
	__le16 pq_id;           /* Port/Queue ID */
	u8 cid;                 /* Command ID */
	u8 pkt_type;            /* Must be MCU_PKT_ID (0xa0) */
	u8 set_query;           /* FW don't care */
	u8 seq;                 /* Sequence number */
	u8 uc_d2b0_rev;
	u8 ext_cid;
	u8 s2d_index;           /* Source to Dest index */
	u8 ext_cid_ack;
	u32 rsv[5];
} __packed __aligned(4);

/* Firmware header structures */
struct mt7927_patch_hdr {
	char build_date[16];
	char platform[4];
	__be32 hw_sw_ver;
	__be32 patch_ver;
	__be16 checksum;
	u16 rsv;
	struct {
		__be32 patch_ver;
		__be32 subsys;
		__be32 feature;
		__be32 n_region;
		__be32 crc;
	} desc;
} __packed;

struct mt7927_patch_sec {
	__be32 type;
	char reserved[4];
	union {
		__be32 spec[13];
		struct {
			__be32 addr;
			__be32 len;
			u8 sec_key_idx;
			u8 align_len;
			u8 reserved[2];
			__be32 enc_type;
		} __packed info;
	};
	__be32 offs;
} __packed;

struct mt7927_fw_region {
	__le32 decomp_crc;
	__le32 decomp_len;
	__le32 decomp_blk_sz;
	u8 rsv[4];
	__le32 addr;
	__le32 len;
	u8 feature_set;
	u8 type;
	u8 rsv1[14];
} __packed;

struct mt7927_fw_trailer {
	u8 chip_id;
	u8 eco_code;
	u8 n_region;
	u8 format_ver;
	u8 format_flag;
	u8 rsv[2];
	char fw_ver[10];
	char build_date[15];
	__le32 crc;
} __packed;

/* Test device structure */
struct test_dev {
	struct pci_dev *pdev;
	void __iomem *regs;         /* BAR0 mapping */
	u32 backup_l1;              /* Backup for remap register */

	/* FWDL ring */
	struct mt7927_desc *ring;
	dma_addr_t ring_dma;
	int ring_head;

	/* Data buffer for DMA */
	void *dma_buf;
	dma_addr_t dma_buf_phys;

	/* Firmware */
	const struct firmware *fw_patch;
	const struct firmware *fw_ram;
};

/* ============================================================
 * Register Access Helpers
 * ============================================================ */

static u32 mt_rr(struct test_dev *dev, u32 offset)
{
	return readl(dev->regs + offset);
}

static void mt_wr(struct test_dev *dev, u32 offset, u32 val)
{
	writel(val, dev->regs + offset);
}

/* Remap read for addresses above BAR0 direct range */
static u32 remap_read(struct test_dev *dev, u32 addr)
{
	u32 offset = addr & 0xffff;
	u32 base = (addr >> 16) & 0xffff;
	u32 val;

	dev->backup_l1 = mt_rr(dev, MT_HIF_REMAP_L1);
	mt_wr(dev, MT_HIF_REMAP_L1,
	      (dev->backup_l1 & ~MT_HIF_REMAP_L1_MASK) | (base << 16));
	mt_rr(dev, MT_HIF_REMAP_L1); /* flush */

	val = readl(dev->regs + MT_HIF_REMAP_BASE_L1 + offset);

	mt_wr(dev, MT_HIF_REMAP_L1, dev->backup_l1);
	return val;
}

/* Remap write for addresses above BAR0 direct range */
static void remap_write(struct test_dev *dev, u32 addr, u32 val)
{
	u32 offset = addr & 0xffff;
	u32 base = (addr >> 16) & 0xffff;

	dev->backup_l1 = mt_rr(dev, MT_HIF_REMAP_L1);
	mt_wr(dev, MT_HIF_REMAP_L1,
	      (dev->backup_l1 & ~MT_HIF_REMAP_L1_MASK) | (base << 16));
	mt_rr(dev, MT_HIF_REMAP_L1); /* flush */

	writel(val, dev->regs + MT_HIF_REMAP_BASE_L1 + offset);

	mt_wr(dev, MT_HIF_REMAP_L1, dev->backup_l1);
}

/* ============================================================
 * MCU Command Functions (with TXD header for init commands)
 * ============================================================ */

static u8 mcu_seq = 0;

/*
 * Send MCU command with proper TXD header
 * Note: FW_SCATTER doesn't use TXD header, only init commands do
 */
static int send_mcu_cmd(struct test_dev *dev, u8 cmd, const void *data, size_t len)
{
	struct mt7927_mcu_txd *txd;
	struct mt7927_desc *desc;
	size_t total_len;
	int idx;
	u32 ctrl, didx;
	int timeout;

	total_len = sizeof(*txd) + len;
	if (total_len > FW_CHUNK_SIZE) {
		dev_err(&dev->pdev->dev, "MCU cmd too large: %zu\n", total_len);
		return -EINVAL;
	}

	/* Increment sequence */
	mcu_seq = (mcu_seq + 1) & 0xf;
	if (!mcu_seq)
		mcu_seq = 1;

	/* Format TXD header in DMA buffer */
	memset(dev->dma_buf, 0, sizeof(*txd));
	txd = (struct mt7927_mcu_txd *)dev->dma_buf;

	/* Hardware TXD (first 8 DWORDs) */
	txd->txd[0] = cpu_to_le32(FIELD_PREP(MT_TXD0_TX_BYTES, total_len) |
				  FIELD_PREP(MT_TXD0_PKT_FMT, MT_TX_TYPE_CMD) |
				  FIELD_PREP(MT_TXD0_Q_IDX, MT_TX_MCU_PORT_RX_Q0));
	txd->txd[1] = cpu_to_le32(FIELD_PREP(MT_TXD1_HDR_FORMAT, MT_HDR_FORMAT_CMD));

	/* MCU header */
	txd->len = cpu_to_le16(total_len - sizeof(txd->txd));
	txd->pq_id = cpu_to_le16(MCU_PQ_ID(MT_TX_PORT_IDX_MCU, MT_TX_MCU_PORT_RX_Q0));
	txd->cid = cmd;
	txd->pkt_type = MCU_PKT_ID;
	txd->seq = mcu_seq;
	txd->s2d_index = MCU_S2D_H2N;
	txd->set_query = MCU_Q_NA;

	/* Copy payload after TXD */
	if (data && len > 0)
		memcpy(dev->dma_buf + sizeof(*txd), data, len);

	wmb();

	/* Setup descriptor */
	idx = dev->ring_head;
	desc = &dev->ring[idx];

	desc->buf0 = cpu_to_le32(lower_32_bits(dev->dma_buf_phys));
	desc->buf1 = cpu_to_le32(upper_32_bits(dev->dma_buf_phys));
	ctrl = FIELD_PREP(MT_DMA_CTL_SD_LEN0, total_len) | MT_DMA_CTL_LAST_SEC0;
	desc->ctrl = cpu_to_le32(ctrl);
	desc->info = 0;
	wmb();

	/* Kick DMA */
	dev->ring_head = (idx + 1) % FWDL_RING_SIZE;
	mt_wr(dev, MT_WFDMA0_TX_RING_CIDX(FWDL_RING_IDX), dev->ring_head);
	wmb();

	/* Poll for completion - but don't wait for mailbox response */
	timeout = 100;
	while (timeout-- > 0) {
		didx = mt_rr(dev, MT_WFDMA0_TX_RING_DIDX(FWDL_RING_IDX));
		if (didx == dev->ring_head)
			return 0;
		usleep_range(100, 200);
	}

	dev_dbg(&dev->pdev->dev, "  MCU cmd 0x%02x: DMA timeout (continuing)\n", cmd);
	return 0; /* Continue anyway - ROM doesn't send responses */
}

/*
 * Initialize firmware download region
 * This tells the ROM where to load the firmware
 */
static int init_download(struct test_dev *dev, u32 addr, u32 len, u32 mode)
{
	struct {
		__le32 addr;
		__le32 len;
		__le32 mode;
	} req = {
		.addr = cpu_to_le32(addr),
		.len = cpu_to_le32(len),
		.mode = cpu_to_le32(mode),
	};
	u8 cmd;

	/* Determine command based on address */
	if (addr == 0x200000 || addr == 0x900000 || addr == 0xe0002800)
		cmd = MCU_CMD_PATCH_START_REQ;
	else
		cmd = MCU_CMD_TARGET_ADDRESS_LEN_REQ;

	dev_info(&dev->pdev->dev, "  Init download: addr=0x%08x len=%u mode=0x%x cmd=0x%02x\n",
		 addr, len, mode, cmd);

	return send_mcu_cmd(dev, cmd, &req, sizeof(req));
}

/* ============================================================
 * Phase 1: CONN_INFRA Wakeup and MCU Initialization
 * Reference: pci_mcu.c::mt7927e_mcu_pre_init()
 * ============================================================ */

static int init_conninfra(struct test_dev *dev)
{
	u32 val;
	int i;

	dev_info(&dev->pdev->dev, "=== Phase 1: CONN_INFRA Initialization ===\n");

	/* Step 1: Force CONN_INFRA wakeup */
	dev_info(&dev->pdev->dev, "  Waking CONN_INFRA (0x%08x = 0x1)...\n",
		 MT_CONNINFRA_WAKEUP);
	remap_write(dev, MT_CONNINFRA_WAKEUP, 0x1);
	msleep(5);

	/* Step 2: Poll for CONN_INFRA version */
	dev_info(&dev->pdev->dev, "  Polling CONN_INFRA version...\n");
	for (i = 0; i < 100; i++) {
		val = remap_read(dev, MT_CONNINFRA_VERSION);
		if (val == CONNINFRA_VERSION_OK || val == 0x03010001) {
			dev_info(&dev->pdev->dev, "  CONN_INFRA version: 0x%08x (OK)\n", val);
			break;
		}
		msleep(10);
	}
	if (i >= 100) {
		val = remap_read(dev, MT_CONNINFRA_VERSION);
		dev_warn(&dev->pdev->dev, "  CONN_INFRA version: 0x%08x (unexpected, continuing)\n", val);
	}

	/* Step 3: WiFi subsystem reset */
	dev_info(&dev->pdev->dev, "  Performing WiFi subsystem reset...\n");
	val = remap_read(dev, MT_WF_SUBSYS_RST);
	remap_write(dev, MT_WF_SUBSYS_RST, val | BIT(0));  /* Assert */
	msleep(5);
	remap_write(dev, MT_WF_SUBSYS_RST, val & ~BIT(0)); /* Deassert */
	msleep(10);

	/* Step 4: Set Crypto MCU ownership */
	dev_info(&dev->pdev->dev, "  Setting Crypto MCU ownership...\n");
	remap_write(dev, MT_CRYPTO_MCU_OWN, BIT(0));
	msleep(5);

	/* Step 5: Wait for MCU IDLE state */
	dev_info(&dev->pdev->dev, "  Waiting for MCU IDLE (0x%04x)...\n", MCU_IDLE);
	for (i = 0; i < 500; i++) {
		val = remap_read(dev, MT_MCU_ROMCODE_INDEX);
		if ((val & 0xFFFF) == MCU_IDLE) {
			dev_info(&dev->pdev->dev, "  MCU IDLE reached: 0x%08x\n", val);
			return 0;
		}
		if ((i % 50) == 0 && i > 0) {
			dev_info(&dev->pdev->dev, "  MCU state: 0x%08x (waiting...)\n", val);
		}
		msleep(10);
	}

	val = remap_read(dev, MT_MCU_ROMCODE_INDEX);
	dev_err(&dev->pdev->dev, "  MCU IDLE timeout! State: 0x%08x\n", val);
	return -ETIMEDOUT;
}

/* ============================================================
 * Phase 2: Power Management - Claim Host Ownership
 * ============================================================ */

static int claim_host_ownership(struct test_dev *dev)
{
	u32 val;
	int i;

	dev_info(&dev->pdev->dev, "=== Phase 2: Claim Host Ownership ===\n");

	val = remap_read(dev, MT_CONN_ON_LPCTL);
	dev_info(&dev->pdev->dev, "  LPCTL initial: 0x%08x\n", val);

	/* Write FW_OWN bit to claim host ownership */
	remap_write(dev, MT_CONN_ON_LPCTL, MT_LPCTL_FW_OWN);
	msleep(5);

	/* Poll until OWN_SYNC clears */
	for (i = 0; i < 100; i++) {
		val = remap_read(dev, MT_CONN_ON_LPCTL);
		if (!(val & MT_LPCTL_OWN_SYNC)) {
			dev_info(&dev->pdev->dev, "  Host ownership claimed: 0x%08x\n", val);
			return 0;
		}
		msleep(10);
	}

	dev_warn(&dev->pdev->dev, "  Ownership timeout (LPCTL=0x%08x), continuing...\n", val);
	return 0; /* Continue anyway */
}

/* ============================================================
 * Phase 3: DMA Ring Setup
 * ============================================================ */

static int setup_dma_ring(struct test_dev *dev)
{
	u32 val;

	dev_info(&dev->pdev->dev, "=== Phase 3: DMA Ring Setup ===\n");

	/* Allocate FWDL ring (ring 16) */
	dev->ring = dma_alloc_coherent(&dev->pdev->dev,
				       FWDL_RING_SIZE * sizeof(struct mt7927_desc),
				       &dev->ring_dma, GFP_KERNEL);
	if (!dev->ring) {
		dev_err(&dev->pdev->dev, "  Failed to allocate FWDL ring\n");
		return -ENOMEM;
	}
	memset(dev->ring, 0, FWDL_RING_SIZE * sizeof(struct mt7927_desc));
	dev->ring_head = 0;

	dev_info(&dev->pdev->dev, "  FWDL ring allocated at DMA 0x%pad\n",
		 &dev->ring_dma);

	/* Allocate DMA buffer for firmware chunks */
	dev->dma_buf = dma_alloc_coherent(&dev->pdev->dev, FW_CHUNK_SIZE,
					  &dev->dma_buf_phys, GFP_KERNEL);
	if (!dev->dma_buf) {
		dev_err(&dev->pdev->dev, "  Failed to allocate DMA buffer\n");
		return -ENOMEM;
	}

	/* Reset DMA pointers */
	mt_wr(dev, MT_WFDMA0_RST_DTX_PTR, BIT(FWDL_RING_IDX));
	wmb();
	msleep(5);

	/* Configure ring 16 (FWDL) */
	dev_info(&dev->pdev->dev, "  Configuring ring %d (FWDL)...\n", FWDL_RING_IDX);
	mt_wr(dev, MT_WFDMA0_TX_RING_BASE(FWDL_RING_IDX), lower_32_bits(dev->ring_dma));
	mt_wr(dev, MT_WFDMA0_TX_RING_BASE(FWDL_RING_IDX) + 4, upper_32_bits(dev->ring_dma));
	mt_wr(dev, MT_WFDMA0_TX_RING_CNT(FWDL_RING_IDX), FWDL_RING_SIZE);
	mt_wr(dev, MT_WFDMA0_TX_RING_CIDX(FWDL_RING_IDX), 0);
	wmb();

	/* Verify ring configuration */
	val = mt_rr(dev, MT_WFDMA0_TX_RING_BASE(FWDL_RING_IDX));
	dev_info(&dev->pdev->dev, "  Ring %d: BASE=0x%08x CNT=%d CIDX=%d DIDX=%d\n",
		 FWDL_RING_IDX, val,
		 mt_rr(dev, MT_WFDMA0_TX_RING_CNT(FWDL_RING_IDX)),
		 mt_rr(dev, MT_WFDMA0_TX_RING_CIDX(FWDL_RING_IDX)),
		 mt_rr(dev, MT_WFDMA0_TX_RING_DIDX(FWDL_RING_IDX)));

	/* Enable DMA */
	val = MT_WFDMA0_GLO_CFG_TX_DMA_EN | MT_WFDMA0_GLO_CFG_RX_DMA_EN |
	      MT_WFDMA0_GLO_CFG_TX_WB_DDONE;
	mt_wr(dev, MT_WFDMA0_GLO_CFG, val);
	wmb();

	dev_info(&dev->pdev->dev, "  DMA enabled, GLO_CFG=0x%08x\n",
		 mt_rr(dev, MT_WFDMA0_GLO_CFG));

	return 0;
}

/* ============================================================
 * Phase 4: Send Firmware Chunk via DMA (Polling Mode)
 * Reference: mt7927_fw_load.c::mt7927_mcu_send_firmware()
 *
 * KEY INSIGHT: No mailbox waits! Just send and poll for completion.
 * ============================================================ */

static int send_fw_chunk(struct test_dev *dev, const void *data, size_t len)
{
	struct mt7927_desc *desc;
	int idx;
	u32 ctrl, didx;
	int timeout;

	if (len > FW_CHUNK_SIZE) {
		dev_err(&dev->pdev->dev, "Chunk too large: %zu > %d\n",
			len, FW_CHUNK_SIZE);
		return -EINVAL;
	}

	/* Copy data to DMA buffer */
	memcpy(dev->dma_buf, data, len);
	wmb();

	/* Setup descriptor */
	idx = dev->ring_head;
	desc = &dev->ring[idx];

	desc->buf0 = cpu_to_le32(lower_32_bits(dev->dma_buf_phys));
	desc->buf1 = cpu_to_le32(upper_32_bits(dev->dma_buf_phys));
	ctrl = FIELD_PREP(MT_DMA_CTL_SD_LEN0, len) | MT_DMA_CTL_LAST_SEC0;
	desc->ctrl = cpu_to_le32(ctrl);
	desc->info = 0;
	wmb();

	/* Advance ring head and kick DMA */
	dev->ring_head = (idx + 1) % FWDL_RING_SIZE;
	mt_wr(dev, MT_WFDMA0_TX_RING_CIDX(FWDL_RING_IDX), dev->ring_head);
	wmb();

	/* Poll for DMA completion (DIDX should advance) */
	timeout = 100;
	while (timeout-- > 0) {
		didx = mt_rr(dev, MT_WFDMA0_TX_RING_DIDX(FWDL_RING_IDX));
		if (didx == dev->ring_head) {
			/* DMA completed */
			return 0;
		}
		usleep_range(100, 200);
	}

	dev_warn(&dev->pdev->dev, "  DMA completion timeout (CIDX=%d, DIDX=%d)\n",
		 dev->ring_head, didx);

	/* Per zouyonghao: Continue anyway, ROM may have processed it */
	return 0;
}

/* ============================================================
 * Phase 5: Load Firmware (Polling Mode, No Mailbox)
 * Reference: mt7927_fw_load.c
 *
 * Sequence:
 * 1. Parse patch header to get address/length
 * 2. Send PATCH_START_REQ with addr/len (MCU command with TXD)
 * 3. Send patch data via FW_SCATTER (raw data, no TXD)
 * 4. Send PATCH_FINISH_REQ
 * 5. Parse RAM header for regions
 * 6. For each region: TARGET_ADDRESS_LEN_REQ then FW_SCATTER
 * 7. Set SW_INIT_DONE (skip FW_START - ROM doesn't support mailbox)
 * ============================================================ */

static int load_patch(struct test_dev *dev)
{
	const struct mt7927_patch_hdr *hdr;
	const struct mt7927_patch_sec *sec;
	const u8 *data;
	size_t remaining, chunk_len;
	size_t total_sent = 0;
	u32 addr, len, offset;
	int ret;

	if (!dev->fw_patch || dev->fw_patch->size < sizeof(*hdr)) {
		dev_err(&dev->pdev->dev, "Invalid patch file\n");
		return -EINVAL;
	}

	hdr = (const struct mt7927_patch_hdr *)dev->fw_patch->data;
	dev_info(&dev->pdev->dev, "  Patch info: build=%s platform=%.4s ver=0x%08x\n",
		 hdr->build_date, hdr->platform, be32_to_cpu(hdr->hw_sw_ver));

	/* Get first patch section */
	sec = (const struct mt7927_patch_sec *)(dev->fw_patch->data + sizeof(*hdr));
	addr = be32_to_cpu(sec->info.addr);
	len = be32_to_cpu(sec->info.len);
	offset = be32_to_cpu(sec->offs);

	dev_info(&dev->pdev->dev, "  Patch section: addr=0x%08x len=%u offs=%u\n",
		 addr, len, offset);

	/* Validate offset */
	if (offset + len > dev->fw_patch->size) {
		dev_err(&dev->pdev->dev, "Patch section exceeds file size\n");
		return -EINVAL;
	}

	/* Step 1: Send PATCH_START_REQ (init download) */
	ret = init_download(dev, addr, len, 0);
	if (ret)
		dev_warn(&dev->pdev->dev, "  PATCH_START warning: %d\n", ret);
	msleep(10);

	/* Step 2: Send patch data via FW_SCATTER (raw, no TXD) */
	data = dev->fw_patch->data + offset;
	remaining = len;

	while (remaining > 0) {
		chunk_len = min_t(size_t, FW_CHUNK_SIZE, remaining);

		ret = send_fw_chunk(dev, data, chunk_len);
		if (ret)
			dev_warn(&dev->pdev->dev, "  Chunk warning: %d\n", ret);

		data += chunk_len;
		remaining -= chunk_len;
		total_sent += chunk_len;

		msleep(5);

		if ((total_sent % (64 * 1024)) == 0)
			dev_info(&dev->pdev->dev, "  Sent %zu / %u bytes...\n", total_sent, len);
	}

	dev_info(&dev->pdev->dev, "  Patch data sent (%zu bytes)\n", total_sent);

	/* Step 3: Send PATCH_FINISH_REQ */
	dev_info(&dev->pdev->dev, "  Sending PATCH_FINISH_REQ...\n");
	ret = send_mcu_cmd(dev, MCU_CMD_PATCH_FINISH_REQ, NULL, 0);
	if (ret)
		dev_warn(&dev->pdev->dev, "  PATCH_FINISH warning: %d\n", ret);

	msleep(50);  /* Give ROM time to apply patch */

	return 0;
}

static int load_ram(struct test_dev *dev)
{
	const struct mt7927_fw_trailer *trailer;
	const struct mt7927_fw_region *region;
	const u8 *data;
	size_t remaining, chunk_len;
	size_t total_sent, offset = 0;
	u32 addr, len;
	int ret, i;

	if (!dev->fw_ram || dev->fw_ram->size < sizeof(*trailer)) {
		dev_err(&dev->pdev->dev, "Invalid RAM file\n");
		return -EINVAL;
	}

	/* Trailer is at end of file */
	trailer = (const struct mt7927_fw_trailer *)
		  (dev->fw_ram->data + dev->fw_ram->size - sizeof(*trailer));

	dev_info(&dev->pdev->dev, "  RAM info: chip_id=0x%02x eco=0x%02x regions=%d ver=%s\n",
		 trailer->chip_id, trailer->eco_code, trailer->n_region, trailer->fw_ver);

	/* Process each region */
	for (i = 0; i < trailer->n_region; i++) {
		/* Region descriptors are before trailer */
		region = (const struct mt7927_fw_region *)
			 ((const u8 *)trailer - (trailer->n_region - i) * sizeof(*region));

		addr = le32_to_cpu(region->addr);
		len = le32_to_cpu(region->len);

		dev_info(&dev->pdev->dev, "  Region %d: addr=0x%08x len=%u type=%d\n",
			 i, addr, len, region->type);

		/* Step 1: Send TARGET_ADDRESS_LEN_REQ */
		ret = init_download(dev, addr, len, 0);
		if (ret)
			dev_warn(&dev->pdev->dev, "  Init region warning: %d\n", ret);
		msleep(5);

		/* Step 2: Send region data */
		data = dev->fw_ram->data + offset;
		remaining = len;
		total_sent = 0;

		while (remaining > 0) {
			chunk_len = min_t(size_t, FW_CHUNK_SIZE, remaining);

			ret = send_fw_chunk(dev, data, chunk_len);
			if (ret)
				dev_warn(&dev->pdev->dev, "  Chunk warning: %d\n", ret);

			data += chunk_len;
			remaining -= chunk_len;
			total_sent += chunk_len;

			msleep(5);

			if ((total_sent % (128 * 1024)) == 0)
				dev_info(&dev->pdev->dev, "    Sent %zu / %u bytes...\n",
					 total_sent, len);
		}

		offset += len;
		dev_info(&dev->pdev->dev, "  Region %d sent (%zu bytes)\n", i, total_sent);
	}

	return 0;
}

static int load_firmware(struct test_dev *dev)
{
	u32 mcu_status;
	int ret;

	dev_info(&dev->pdev->dev, "=== Phase 5: Firmware Loading (Polling Mode) ===\n");
	dev_info(&dev->pdev->dev, "  NOTE: NO mailbox waits - MT7927 ROM doesn't support mailbox\n");

	/* Check MCU status before loading */
	mcu_status = remap_read(dev, MT_MCU_STATUS);
	dev_info(&dev->pdev->dev, "  MCU status before: 0x%08x\n", mcu_status);

	/* Load patch */
	if (dev->fw_patch && dev->fw_patch->size > 0) {
		dev_info(&dev->pdev->dev, "\n--- Loading PATCH (%zu bytes) ---\n",
			 dev->fw_patch->size);
		ret = load_patch(dev);
		if (ret)
			dev_warn(&dev->pdev->dev, "  Patch load returned: %d\n", ret);

		mcu_status = remap_read(dev, MT_MCU_STATUS);
		dev_info(&dev->pdev->dev, "  MCU status after patch: 0x%08x\n", mcu_status);
	}

	/* Load RAM */
	if (dev->fw_ram && dev->fw_ram->size > 0) {
		dev_info(&dev->pdev->dev, "\n--- Loading RAM (%zu bytes) ---\n",
			 dev->fw_ram->size);
		ret = load_ram(dev);
		if (ret)
			dev_warn(&dev->pdev->dev, "  RAM load returned: %d\n", ret);
	}

	/* Skip FW_START - ROM doesn't support mailbox
	 * Instead, set SW_INIT_DONE bit to signal host is ready
	 */
	dev_info(&dev->pdev->dev, "\n--- Finalizing ---\n");
	dev_info(&dev->pdev->dev, "  Skipping FW_START (mailbox not supported)\n");
	dev_info(&dev->pdev->dev, "  Setting SW_INIT_DONE bit...\n");

	{
		u32 ap2wf = remap_read(dev, MT_WFSYS_SW_RST_B);
		remap_write(dev, MT_WFSYS_SW_RST_B, ap2wf | MT_WFSYS_SW_INIT_DONE);
		dev_info(&dev->pdev->dev, "  AP2WF: 0x%08x -> 0x%08x\n",
			 ap2wf, remap_read(dev, MT_WFSYS_SW_RST_B));
	}

	/* Check final MCU status */
	msleep(100);
	mcu_status = remap_read(dev, MT_MCU_STATUS);
	dev_info(&dev->pdev->dev, "  MCU status after load: 0x%08x\n", mcu_status);

	{
		u32 mcu_ready = remap_read(dev, MT_CONN_ON_MISC);
		dev_info(&dev->pdev->dev, "  MCU ready (CONN_ON_MISC): 0x%08x\n", mcu_ready);
	}

	return 0;
}

/* ============================================================
 * Module Probe/Remove
 * ============================================================ */

static int test_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct test_dev *dev;
	u32 chip_id;
	int ret;

	dev_info(&pdev->dev, "\n");
	dev_info(&pdev->dev, "+----------------------------------------------------------+\n");
	dev_info(&pdev->dev, "|  MT7927 Firmware Load Test (Polling Mode, No Mailbox)   |\n");
	dev_info(&pdev->dev, "+----------------------------------------------------------+\n");

	dev = devm_kzalloc(&pdev->dev, sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;

	dev->pdev = pdev;
	pci_set_drvdata(pdev, dev);

	/* Enable device */
	ret = pcim_enable_device(pdev);
	if (ret) {
		dev_err(&pdev->dev, "Failed to enable PCI device\n");
		return ret;
	}

	pci_set_master(pdev);

	/* Disable ASPM L0s and L1 before DMA operations */
	pci_disable_link_state(pdev, PCIE_LINK_STATE_L0S | PCIE_LINK_STATE_L1);
	dev_info(&pdev->dev, "ASPM L0s/L1 disabled\n");

	/* Map BAR0 (2MB) */
	ret = pcim_iomap_regions(pdev, BIT(0), "mt7927_fw_test");
	if (ret) {
		dev_err(&pdev->dev, "Failed to map BAR0\n");
		return ret;
	}

	dev->regs = pcim_iomap_table(pdev)[0];
	if (!dev->regs) {
		dev_err(&pdev->dev, "BAR0 mapping failed\n");
		return -ENOMEM;
	}

	/* Verify chip is responding */
	chip_id = mt_rr(dev, 0x0000);
	if (chip_id == 0xffffffff) {
		dev_err(&pdev->dev, "Chip not responding (0xffffffff)\n");
		return -EIO;
	}
	dev_info(&pdev->dev, "BAR0 mapped, initial read: 0x%08x\n", chip_id);

	/* Setup DMA mask */
	ret = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));
	if (ret) {
		dev_err(&pdev->dev, "Failed to set DMA mask\n");
		return ret;
	}

	/* Load firmware files */
	dev_info(&pdev->dev, "Loading firmware files...\n");

	ret = request_firmware(&dev->fw_patch, FW_PATCH, &pdev->dev);
	if (ret) {
		dev_err(&pdev->dev, "Failed to load patch: %s (%d)\n", FW_PATCH, ret);
		return ret;
	}
	dev_info(&pdev->dev, "  Patch: %s (%zu bytes)\n", FW_PATCH, dev->fw_patch->size);

	ret = request_firmware(&dev->fw_ram, FW_RAM, &pdev->dev);
	if (ret) {
		dev_err(&pdev->dev, "Failed to load RAM: %s (%d)\n", FW_RAM, ret);
		goto err_release_patch;
	}
	dev_info(&pdev->dev, "  RAM: %s (%zu bytes)\n", FW_RAM, dev->fw_ram->size);

	/* Phase 1: Initialize CONN_INFRA and wait for MCU IDLE */
	ret = init_conninfra(dev);
	if (ret) {
		dev_err(&pdev->dev, "CONN_INFRA init failed: %d\n", ret);
		/* Continue anyway - firmware loading might still work */
	}

	/* Phase 2: Claim host ownership */
	ret = claim_host_ownership(dev);
	if (ret) {
		dev_warn(&pdev->dev, "Host ownership claim issue: %d\n", ret);
	}

	/* Phase 3: Setup DMA ring */
	ret = setup_dma_ring(dev);
	if (ret) {
		dev_err(&pdev->dev, "DMA ring setup failed: %d\n", ret);
		goto err_release_ram;
	}

	/* Phase 4-5: Load firmware */
	ret = load_firmware(dev);
	if (ret) {
		dev_err(&pdev->dev, "Firmware loading failed: %d\n", ret);
	}

	/* Final status */
	dev_info(&pdev->dev, "\n");
	dev_info(&pdev->dev, "+----------------------------------------------------------+\n");
	dev_info(&pdev->dev, "|                    Test Complete                         |\n");
	dev_info(&pdev->dev, "+----------------------------------------------------------+\n");
	dev_info(&pdev->dev, "  WFDMA GLO_CFG: 0x%08x\n", mt_rr(dev, MT_WFDMA0_GLO_CFG));
	dev_info(&pdev->dev, "  WFDMA INT_STA: 0x%08x\n", mt_rr(dev, MT_WFDMA0_HOST_INT_STA));
	dev_info(&pdev->dev, "  Ring %d CIDX/DIDX: %d/%d\n", FWDL_RING_IDX,
		 mt_rr(dev, MT_WFDMA0_TX_RING_CIDX(FWDL_RING_IDX)),
		 mt_rr(dev, MT_WFDMA0_TX_RING_DIDX(FWDL_RING_IDX)));
	dev_info(&pdev->dev, "\n");
	dev_info(&pdev->dev, "Unload with: sudo rmmod test_fw_load\n");

	return 0;

err_release_ram:
	release_firmware(dev->fw_ram);
err_release_patch:
	release_firmware(dev->fw_patch);
	return ret;
}

static void test_remove(struct pci_dev *pdev)
{
	struct test_dev *dev = pci_get_drvdata(pdev);

	dev_info(&pdev->dev, "Removing test module...\n");

	/* Disable DMA */
	if (dev->regs)
		mt_wr(dev, MT_WFDMA0_GLO_CFG, 0);

	/* Free DMA resources */
	if (dev->dma_buf)
		dma_free_coherent(&pdev->dev, FW_CHUNK_SIZE,
				  dev->dma_buf, dev->dma_buf_phys);

	if (dev->ring)
		dma_free_coherent(&pdev->dev,
				  FWDL_RING_SIZE * sizeof(struct mt7927_desc),
				  dev->ring, dev->ring_dma);

	/* Release firmware */
	release_firmware(dev->fw_ram);
	release_firmware(dev->fw_patch);

	dev_info(&pdev->dev, "Test module removed\n");
}

static const struct pci_device_id test_ids[] = {
	{ PCI_DEVICE(MT7927_VENDOR_ID, MT7927_DEVICE_ID) },
	{ }
};
MODULE_DEVICE_TABLE(pci, test_ids);

static struct pci_driver test_driver = {
	.name = "mt7927_fw_test",
	.id_table = test_ids,
	.probe = test_probe,
	.remove = test_remove,
};

module_pci_driver(test_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("MT7927 Project");
MODULE_DESCRIPTION("MT7927 Firmware Load Test - Polling Mode (No Mailbox)");
MODULE_FIRMWARE(FW_PATCH);
MODULE_FIRMWARE(FW_RAM);
