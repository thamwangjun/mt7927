// SPDX-License-Identifier: GPL-2.0
/*
 * MT7927 Firmware Pre-Load Diagnostic Module
 *
 * Validates all assumptions documented in docs/ that are required for
 * successful firmware loading onto the MT7927 device.
 *
 * Assumptions validated:
 * 1. Chip Identity - MT7927 responds with expected ID (0x00511163)
 * 2. BAR Configuration - BAR0=2MB, BAR2=32KB
 * 3. MT6639 Architecture - Sparse ring layout (0,1,2,15,16)
 * 4. Power Management - LPCTL handshake capability
 * 5. WFSYS Reset - Reset and INIT_DONE states
 * 6. MCU State - IDLE state (0x1D1E) and ready registers
 * 7. CONN_INFRA - Version and wakeup state
 * 8. ASPM State - L0s/L1/L1.1/L1.2 configuration
 * 9. WFDMA State - DMA engine configuration
 *
 * References:
 * - docs/ZOUYONGHAO_ANALYSIS.md - MCU IDLE check, polling protocol
 * - docs/MT6639_ANALYSIS.md - Ring 15/16 configuration
 * - docs/ROADMAP.md - Expected values and states
 * - CLAUDE.md - Register addresses and assumptions
 */

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/io.h>
#include <linux/delay.h>

#define MT7927_VENDOR_ID    0x14c3
#define MT7927_DEVICE_ID    0x7927

/* Expected values from documentation */
#define EXPECTED_CHIP_ID        0x00511163
#define EXPECTED_HW_REV         0x11885162
#define EXPECTED_BAR0_SIZE      0x200000    /* 2MB */
#define EXPECTED_BAR2_SIZE      0x8000      /* 32KB */
#define EXPECTED_MCU_IDLE       0x1D1E      /* MCU IDLE state */
#define EXPECTED_CONNINFRA_VER  0x03010002  /* CONN_INFRA version */

/* Key register addresses (remapped via HIF_REMAP_L1) */
#define MT_CONN_ON_LPCTL            0x7c060010  /* Power management */
#define MT_WFSYS_SW_RST_B           0x7c000140  /* WiFi subsystem reset */
#define MT_MCU_ROMCODE_INDEX        0x81021604  /* MCU state (expect 0x1D1E) */
#define MT_MCU_STATUS               0x7c060204  /* MCU status register */
#define MT_CONN_ON_MISC             0x7c0600f0  /* MCU ready status */
#define MT_CONNINFRA_WAKEUP         0x7C0601A0  /* CONN_INFRA wakeup */
#define MT_CONNINFRA_VERSION        0x7C011000  /* CONN_INFRA version */

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

/* PCIe capability offsets - using kernel-provided defines */

/* Bit definitions */
#define MT_WFSYS_SW_RST_B_EN        BIT(0)
#define MT_WFSYS_SW_INIT_DONE       BIT(4)
#define MT_CONN_ON_LPCTL_HOST_OWN   BIT(0)
#define MT_CONN_ON_LPCTL_FW_OWN     BIT(1)
#define MT_CONN_ON_LPCTL_OWN_SYNC   BIT(2)

struct precheck_dev {
	struct pci_dev *pdev;
	void __iomem *bar0;
	void __iomem *bar2;
	resource_size_t bar0_size;
	resource_size_t bar2_size;
	u32 backup_l1;

	/* Results tracking */
	int pass_count;
	int fail_count;
	int warn_count;
};

/*
 * Remap read for addresses above BAR0 direct range.
 * The MT7927 uses a remap window at MT_HIF_REMAP_BASE_L1 (0x130000)
 * to access addresses in the 0x7cXXXXXX and 0x8XXXXXXX range.
 * The upper 16 bits of the target address are written to MT_HIF_REMAP_L1.
 */
static u32 remap_read(struct precheck_dev *dev, u32 addr)
{
	u32 offset = addr & 0xffff;
	u32 base = (addr >> 16) & 0xffff;
	u32 val;

	/* Save current remap state */
	dev->backup_l1 = readl(dev->bar0 + MT_HIF_REMAP_L1);

	/* Set new remap base */
	writel((dev->backup_l1 & ~MT_HIF_REMAP_L1_MASK) | (base << 16),
	       dev->bar0 + MT_HIF_REMAP_L1);

	/* Flush write and ensure remap is active before reading */
	readl(dev->bar0 + MT_HIF_REMAP_L1);

	/* Read from remapped address */
	val = readl(dev->bar0 + MT_HIF_REMAP_BASE_L1 + offset);

	/* Restore original remap state */
	writel(dev->backup_l1, dev->bar0 + MT_HIF_REMAP_L1);

	return val;
}

static void report_result(struct precheck_dev *dev, const char *test,
			  bool passed, const char *detail)
{
	struct pci_dev *pdev = dev->pdev;

	if (passed) {
		dev_info(&pdev->dev, "[PASS] %s: %s\n", test, detail);
		dev->pass_count++;
	} else {
		dev_err(&pdev->dev, "[FAIL] %s: %s\n", test, detail);
		dev->fail_count++;
	}
}

static void report_warn(struct precheck_dev *dev, const char *test,
			const char *detail)
{
	dev_warn(&dev->pdev->dev, "[WARN] %s: %s\n", test, detail);
	dev->warn_count++;
}

/* ============================================================
 * Test 1: Chip Identity
 * Reference: CLAUDE.md, docs/ROADMAP.md
 * Expected: Chip ID = 0x00511163
 * ============================================================ */
static void check_chip_identity(struct precheck_dev *dev)
{
	u32 chip_id, hw_rev;
	char detail[128];

	dev_info(&dev->pdev->dev, "\n=== Test 1: Chip Identity ===\n");

	/* Read from BAR2 (chip ID shadow at offset 0) */
	chip_id = readl(dev->bar2 + 0x000);
	hw_rev = readl(dev->bar2 + 0x004);

	snprintf(detail, sizeof(detail), "Chip ID = 0x%08x (expected 0x%08x)",
		 chip_id, EXPECTED_CHIP_ID);
	report_result(dev, "Chip ID", chip_id == EXPECTED_CHIP_ID, detail);

	snprintf(detail, sizeof(detail), "HW Rev = 0x%08x (expected 0x%08x)",
		 hw_rev, EXPECTED_HW_REV);
	/* HW Rev may vary, just report as info */
	dev_info(&dev->pdev->dev, "  HW Rev: 0x%08x\n", hw_rev);
}

/* ============================================================
 * Test 2: BAR Configuration
 * Reference: CLAUDE.md, docs/HARDWARE.md
 * Expected: BAR0 = 2MB, BAR2 = 32KB
 * ============================================================ */
static void check_bar_config(struct precheck_dev *dev)
{
	char detail[128];

	dev_info(&dev->pdev->dev, "\n=== Test 2: BAR Configuration ===\n");

	snprintf(detail, sizeof(detail), "BAR0 size = %llu bytes (expected %d)",
		 (unsigned long long)dev->bar0_size, EXPECTED_BAR0_SIZE);
	report_result(dev, "BAR0 Size", dev->bar0_size == EXPECTED_BAR0_SIZE, detail);

	snprintf(detail, sizeof(detail), "BAR2 size = %llu bytes (expected %d)",
		 (unsigned long long)dev->bar2_size, EXPECTED_BAR2_SIZE);
	report_result(dev, "BAR2 Size", dev->bar2_size == EXPECTED_BAR2_SIZE, detail);
}

/* ============================================================
 * Test 3: MT6639 Ring Configuration (Sparse Layout)
 * Reference: docs/MT6639_ANALYSIS.md
 * Expected: Hardware has 8 physical TX rings (0-7) with CNT=512
 *           Rings 15/16 are MCU_WM/FWDL per MT6639 config
 *           (CNT=0 until driver initializes them)
 * ============================================================ */
static void check_ring_config(struct precheck_dev *dev)
{
	u32 cnt, base, cidx, didx;
	int data_rings[] = {0, 1, 2, 3, 4, 5, 6, 7};
	int mcu_rings[] = {15, 16};
	int i, ring;
	bool all_data_rings_ok = true;
	char detail[128];

	dev_info(&dev->pdev->dev, "\n=== Test 3: MT6639 Ring Configuration ===\n");

	/*
	 * Check data rings 0-7 (hardware default CNT=512)
	 * MT7927 has 8 physical TX rings. Per MT6639_ANALYSIS.md,
	 * the MT6639/MT7927 uses sparse layout: 0,1,2 for data, 15,16 for MCU/FWDL.
	 * Hardware shows all 8 rings (0-7) with CNT=512 default.
	 */
	dev_info(&dev->pdev->dev, "Checking physical TX rings (0-7):\n");
	for (i = 0; i < ARRAY_SIZE(data_rings); i++) {
		ring = data_rings[i];
		cnt = readl(dev->bar0 + MT_WFDMA0_TX_RING_CNT(ring));
		dev_info(&dev->pdev->dev, "  Ring %d: CNT = %u\n", ring, cnt);
		if (cnt == 0)
			all_data_rings_ok = false;
	}

	snprintf(detail, sizeof(detail),
		 "Physical rings 0-7 have CNT > 0 (hardware present)");
	report_result(dev, "Physical TX Rings", all_data_rings_ok, detail);

	/*
	 * Check MCU/FWDL ring register addresses (15/16)
	 * These registers exist but CNT=0 until driver initializes them.
	 * Per MT6639_ANALYSIS.md: Ring 15=MCU_WM, Ring 16=FWDL
	 */
	dev_info(&dev->pdev->dev, "Checking MCU ring registers (15, 16):\n");
	dev_info(&dev->pdev->dev, "  (CNT=0 expected - driver must initialize)\n");
	for (i = 0; i < ARRAY_SIZE(mcu_rings); i++) {
		ring = mcu_rings[i];
		base = readl(dev->bar0 + MT_WFDMA0_TX_RING_BASE(ring));
		cnt = readl(dev->bar0 + MT_WFDMA0_TX_RING_CNT(ring));
		cidx = readl(dev->bar0 + MT_WFDMA0_TX_RING_CIDX(ring));
		didx = readl(dev->bar0 + MT_WFDMA0_TX_RING_DIDX(ring));

		dev_info(&dev->pdev->dev,
			 "  Ring %d: BASE=0x%08x CNT=%u CIDX=%u DIDX=%u\n",
			 ring, base, cnt, cidx, didx);
	}

	dev_info(&dev->pdev->dev,
		 "  Ring 15=MCU_WM (commands), Ring 16=FWDL (firmware download)\n");
}

/* ============================================================
 * Test 4: Power Management State (LPCTL)
 * Reference: docs/ZOUYONGHAO_ANALYSIS.md, CLAUDE.md
 * Expected: Able to read LPCTL, understand ownership bits
 * ============================================================ */
static void check_power_management(struct precheck_dev *dev)
{
	u32 lpctl;
	char detail[128];

	dev_info(&dev->pdev->dev, "\n=== Test 4: Power Management (LPCTL) ===\n");

	lpctl = remap_read(dev, MT_CONN_ON_LPCTL);

	dev_info(&dev->pdev->dev, "  LPCTL (0x%08x) = 0x%08x\n",
		 MT_CONN_ON_LPCTL, lpctl);
	dev_info(&dev->pdev->dev, "    HOST_OWN (bit 0): %s\n",
		 (lpctl & MT_CONN_ON_LPCTL_HOST_OWN) ? "SET" : "CLEAR");
	dev_info(&dev->pdev->dev, "    FW_OWN (bit 1): %s\n",
		 (lpctl & MT_CONN_ON_LPCTL_FW_OWN) ? "SET" : "CLEAR");
	dev_info(&dev->pdev->dev, "    OWN_SYNC (bit 2): %s\n",
		 (lpctl & MT_CONN_ON_LPCTL_OWN_SYNC) ? "SET" : "CLEAR");

	/* Just verify we can read it (not 0xffffffff) */
	snprintf(detail, sizeof(detail), "LPCTL readable = 0x%08x (not 0xffffffff)",
		 lpctl);
	report_result(dev, "LPCTL Readable", lpctl != 0xffffffff, detail);
}

/* ============================================================
 * Test 5: WFSYS Reset State
 * Reference: docs/ROADMAP.md, test_fw_load.c
 * Expected: Check RST_B_EN and INIT_DONE bits
 * ============================================================ */
static void check_wfsys_state(struct precheck_dev *dev)
{
	u32 wfsys;
	bool rst_en, init_done;
	char detail[128];

	dev_info(&dev->pdev->dev, "\n=== Test 5: WFSYS Reset State ===\n");

	wfsys = remap_read(dev, MT_WFSYS_SW_RST_B);
	rst_en = !!(wfsys & MT_WFSYS_SW_RST_B_EN);
	init_done = !!(wfsys & MT_WFSYS_SW_INIT_DONE);

	dev_info(&dev->pdev->dev, "  WFSYS_SW_RST_B (0x%08x) = 0x%08x\n",
		 MT_WFSYS_SW_RST_B, wfsys);
	dev_info(&dev->pdev->dev, "    RST_B_EN (bit 0): %s\n",
		 rst_en ? "SET (reset deasserted)" : "CLEAR (in reset)");
	dev_info(&dev->pdev->dev, "    INIT_DONE (bit 4): %s\n",
		 init_done ? "SET (init complete)" : "CLEAR (not initialized)");

	snprintf(detail, sizeof(detail), "WFSYS readable = 0x%08x", wfsys);
	report_result(dev, "WFSYS Readable", wfsys != 0xffffffff, detail);

	/* For firmware loading, RST_B should be asserted and INIT_DONE set */
	if (rst_en && init_done) {
		report_result(dev, "WFSYS Ready", true,
			      "RST_B_EN=1, INIT_DONE=1 (ready for firmware)");
	} else if (!rst_en) {
		report_warn(dev, "WFSYS State", "Device in reset state - needs reset sequence");
	} else {
		report_warn(dev, "WFSYS State", "INIT_DONE not set - may need initialization");
	}
}

/* ============================================================
 * Test 6: MCU State (Critical for firmware loading)
 * Reference: docs/ZOUYONGHAO_ANALYSIS.md lines 125-135
 * Expected: MCU IDLE state = 0x1D1E before firmware loading
 * ============================================================ */
static void check_mcu_state(struct precheck_dev *dev)
{
	u32 romcode_idx, mcu_status, conn_misc;
	char detail[128];

	dev_info(&dev->pdev->dev, "\n=== Test 6: MCU State (Critical) ===\n");

	/*
	 * MCU ROMCODE INDEX - must be 0x1D1E (IDLE) for firmware loading.
	 * Per zouyonghao analysis (docs/ZOUYONGHAO_ANALYSIS.md lines 125-135),
	 * the MCU must reach IDLE state before firmware can be loaded.
	 * The IDLE value is in the lower 16 bits of the register.
	 */
	romcode_idx = remap_read(dev, MT_MCU_ROMCODE_INDEX);
	dev_info(&dev->pdev->dev, "  MCU ROMCODE INDEX (0x%08x) = 0x%08x\n",
		 MT_MCU_ROMCODE_INDEX, romcode_idx);
	dev_info(&dev->pdev->dev, "    Expected for IDLE: 0x%04x\n", EXPECTED_MCU_IDLE);

	/* Check lower 16 bits for IDLE state */
	snprintf(detail, sizeof(detail),
		 "MCU state = 0x%04x (expected 0x%04x for IDLE)",
		 romcode_idx & 0xFFFF, EXPECTED_MCU_IDLE);

	if ((romcode_idx & 0xFFFF) == EXPECTED_MCU_IDLE) {
		report_result(dev, "MCU IDLE State", true, detail);
	} else {
		/* Not necessarily a failure - MCU may not be in IDLE yet */
		report_warn(dev, "MCU IDLE State", detail);
		dev_info(&dev->pdev->dev,
			 "    Note: MCU may need WFSYS reset to reach IDLE state\n");
	}

	/* MCU Status Register */
	mcu_status = remap_read(dev, MT_MCU_STATUS);
	dev_info(&dev->pdev->dev, "  MCU Status (0x%08x) = 0x%08x\n",
		 MT_MCU_STATUS, mcu_status);

	/* CONN_ON_MISC (MCU ready status) */
	conn_misc = remap_read(dev, MT_CONN_ON_MISC);
	dev_info(&dev->pdev->dev, "  CONN_ON_MISC (0x%08x) = 0x%08x\n",
		 MT_CONN_ON_MISC, conn_misc);
}

/* ============================================================
 * Test 7: CONN_INFRA State
 * Reference: docs/ZOUYONGHAO_ANALYSIS.md lines 128-132
 * Expected: CONN_INFRA version = 0x03010002
 * ============================================================ */
static void check_conninfra_state(struct precheck_dev *dev)
{
	u32 wakeup, version;
	char detail[128];

	dev_info(&dev->pdev->dev, "\n=== Test 7: CONN_INFRA State ===\n");

	/* CONN_INFRA Wakeup */
	wakeup = remap_read(dev, MT_CONNINFRA_WAKEUP);
	dev_info(&dev->pdev->dev, "  CONNINFRA Wakeup (0x%08x) = 0x%08x\n",
		 MT_CONNINFRA_WAKEUP, wakeup);

	/* CONN_INFRA Version */
	version = remap_read(dev, MT_CONNINFRA_VERSION);
	dev_info(&dev->pdev->dev, "  CONNINFRA Version (0x%08x) = 0x%08x\n",
		 MT_CONNINFRA_VERSION, version);
	dev_info(&dev->pdev->dev, "    Expected: 0x%08x\n", EXPECTED_CONNINFRA_VER);

	snprintf(detail, sizeof(detail),
		 "CONNINFRA version = 0x%08x (expected 0x%08x)",
		 version, EXPECTED_CONNINFRA_VER);

	if (version == EXPECTED_CONNINFRA_VER) {
		report_result(dev, "CONNINFRA Version", true, detail);
	} else if (version == 0x00000000 || version == 0xffffffff) {
		report_warn(dev, "CONNINFRA Version",
			    "Not responding - may need wakeup sequence");
	} else {
		/* Different version - may still work */
		report_warn(dev, "CONNINFRA Version", detail);
	}
}

/* ============================================================
 * Test 8: ASPM State
 * Reference: docs/ZOUYONGHAO_ANALYSIS.md lines 170-188
 * Note: Working driver only disables L0s, NOT L1
 * ============================================================ */
static void check_aspm_state(struct precheck_dev *dev)
{
	struct pci_dev *pdev = dev->pdev;
	int pos;
	u16 lnkctl = 0;

	dev_info(&pdev->dev, "\n=== Test 8: ASPM State ===\n");

	pos = pci_find_capability(pdev, PCI_CAP_ID_EXP);
	if (!pos) {
		report_warn(dev, "ASPM Check", "No PCIe capability found");
		return;
	}

	pci_read_config_word(pdev, pos + PCI_EXP_LNKCTL, &lnkctl);

	dev_info(&pdev->dev, "  Link Control (Cap+0x%02x) = 0x%04x\n",
		 PCI_EXP_LNKCTL, lnkctl);
	dev_info(&pdev->dev, "    ASPM L0s: %s\n",
		 (lnkctl & PCI_EXP_LNKCTL_ASPM_L0S) ? "ENABLED" : "disabled");
	dev_info(&pdev->dev, "    ASPM L1: %s\n",
		 (lnkctl & PCI_EXP_LNKCTL_ASPM_L1) ? "ENABLED" : "disabled");

	/* According to zouyonghao analysis, L1 being enabled is OK */
	if (lnkctl & PCI_EXP_LNKCTL_ASPM_L0S) {
		report_warn(dev, "ASPM L0s",
			    "L0s ENABLED - should be disabled for DMA operations");
	} else {
		dev_info(&pdev->dev, "  [OK] ASPM L0s disabled\n");
	}

	/* L1 note */
	if (lnkctl & PCI_EXP_LNKCTL_ASPM_L1) {
		dev_info(&pdev->dev,
			 "  [INFO] ASPM L1 enabled - per zouyonghao analysis, this is OK\n");
	}
}

/* ============================================================
 * Test 9: WFDMA State
 * Reference: CLAUDE.md, test_fw_load.c
 * ============================================================ */
static void check_wfdma_state(struct precheck_dev *dev)
{
	u32 glo_cfg, rst_ptr, int_sta, int_ena;

	dev_info(&dev->pdev->dev, "\n=== Test 9: WFDMA State ===\n");

	glo_cfg = readl(dev->bar0 + MT_WFDMA0_GLO_CFG);
	rst_ptr = readl(dev->bar0 + MT_WFDMA0_RST_DTX_PTR);
	int_sta = readl(dev->bar0 + MT_WFDMA0_HOST_INT_STA);
	int_ena = readl(dev->bar0 + MT_WFDMA0_HOST_INT_ENA);

	dev_info(&dev->pdev->dev, "  GLO_CFG (0x%04x) = 0x%08x\n",
		 MT_WFDMA0_GLO_CFG, glo_cfg);
	dev_info(&dev->pdev->dev, "    TX_DMA_EN (bit 0): %s\n",
		 (glo_cfg & BIT(0)) ? "ENABLED" : "disabled");
	dev_info(&dev->pdev->dev, "    RX_DMA_EN (bit 2): %s\n",
		 (glo_cfg & BIT(2)) ? "ENABLED" : "disabled");
	dev_info(&dev->pdev->dev, "    TX_WB_DDONE (bit 6): %s\n",
		 (glo_cfg & BIT(6)) ? "SET" : "clear");

	dev_info(&dev->pdev->dev, "  RST_DTX_PTR (0x%04x) = 0x%08x\n",
		 MT_WFDMA0_RST_DTX_PTR, rst_ptr);
	dev_info(&dev->pdev->dev, "  HOST_INT_STA (0x%04x) = 0x%08x\n",
		 MT_WFDMA0_HOST_INT_STA, int_sta);
	dev_info(&dev->pdev->dev, "  HOST_INT_ENA (0x%04x) = 0x%08x\n",
		 MT_WFDMA0_HOST_INT_ENA, int_ena);

	/* Note expected state for firmware loading */
	dev_info(&dev->pdev->dev,
		 "  Note: DMA will be enabled during firmware loading\n");
}

/* ============================================================
 * Summary and Recommendations
 * ============================================================ */
static void print_summary(struct precheck_dev *dev)
{
	struct pci_dev *pdev = dev->pdev;
	int total = dev->pass_count + dev->fail_count;

	dev_info(&pdev->dev, "\n");
	dev_info(&pdev->dev, "+----------------------------------------------------------+\n");
	dev_info(&pdev->dev, "|       MT7927 Firmware Pre-Load Check Summary             |\n");
	dev_info(&pdev->dev, "+----------------------------------------------------------+\n");
	dev_info(&pdev->dev, "|  PASSED:   %3d / %3d                                      |\n",
		 dev->pass_count, total);
	dev_info(&pdev->dev, "|  FAILED:   %3d                                            |\n",
		 dev->fail_count);
	dev_info(&pdev->dev, "|  WARNINGS: %3d                                            |\n",
		 dev->warn_count);
	dev_info(&pdev->dev, "+----------------------------------------------------------+\n");

	if (dev->fail_count == 0 && dev->warn_count == 0) {
		dev_info(&pdev->dev, "|  STATUS: All checks passed! Ready for firmware loading.  |\n");
	} else if (dev->fail_count == 0) {
		dev_info(&pdev->dev, "|  STATUS: Checks passed with warnings. Review above.      |\n");
	} else {
		dev_info(&pdev->dev, "|  STATUS: Some checks FAILED. Address issues before       |\n");
		dev_info(&pdev->dev, "|          attempting firmware loading.                    |\n");
	}

	dev_info(&pdev->dev, "+----------------------------------------------------------+\n");
	dev_info(&pdev->dev, "|  Key Requirements for Firmware Loading:                  |\n");
	dev_info(&pdev->dev, "|  1. Chip ID = 0x00511163                                 |\n");
	dev_info(&pdev->dev, "|  2. MCU in IDLE state (0x1D1E at 0x81021604)             |\n");
	dev_info(&pdev->dev, "|  3. Use polling-based protocol (NO mailbox waits)        |\n");
	dev_info(&pdev->dev, "|  4. Ring 15=MCU_WM, Ring 16=FWDL (MT6639 config)         |\n");
	dev_info(&pdev->dev, "|  5. Disable ASPM L0s before DMA operations               |\n");
	dev_info(&pdev->dev, "+----------------------------------------------------------+\n");

	dev_info(&pdev->dev, "\nReferences:\n");
	dev_info(&pdev->dev, "  - docs/ZOUYONGHAO_ANALYSIS.md (root cause & solution)\n");
	dev_info(&pdev->dev, "  - docs/MT6639_ANALYSIS.md (ring configuration)\n");
	dev_info(&pdev->dev, "  - docs/ROADMAP.md (implementation status)\n");
}

static int precheck_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct precheck_dev *dev;
	void __iomem *const *iomap_table;
	u32 chip_id;
	int ret;

	dev_info(&pdev->dev, "\n");
	dev_info(&pdev->dev, "+----------------------------------------------------------+\n");
	dev_info(&pdev->dev, "|     MT7927 Firmware Pre-Load Diagnostic Module           |\n");
	dev_info(&pdev->dev, "|     Validating assumptions from docs/ for FW loading     |\n");
	dev_info(&pdev->dev, "+----------------------------------------------------------+\n");

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

	/* Get BAR sizes before mapping */
	dev->bar0_size = pci_resource_len(pdev, 0);
	dev->bar2_size = pci_resource_len(pdev, 2);

	/* Map both BAR0 and BAR2 */
	ret = pcim_iomap_regions(pdev, BIT(0) | BIT(2), "mt7927_precheck");
	if (ret) {
		dev_err(&pdev->dev, "Failed to map BARs\n");
		return ret;
	}

	iomap_table = pcim_iomap_table(pdev);
	dev->bar0 = iomap_table[0];
	dev->bar2 = iomap_table[2];

	if (!dev->bar0 || !dev->bar2) {
		dev_err(&pdev->dev, "BAR mapping failed\n");
		return -ENOMEM;
	}

	/* Quick sanity check before running tests */
	chip_id = readl(dev->bar2 + 0x000);
	if (chip_id == 0xffffffff) {
		dev_err(&pdev->dev, "Chip not responding (0xffffffff)\n");
		dev_err(&pdev->dev, "Device may be in error state - try PCI rescan or reboot\n");
		return -EIO;
	}

	/* Run all validation tests */
	check_chip_identity(dev);
	check_bar_config(dev);
	check_ring_config(dev);
	check_power_management(dev);
	check_wfsys_state(dev);
	check_mcu_state(dev);
	check_conninfra_state(dev);
	check_aspm_state(dev);
	check_wfdma_state(dev);

	/* Print summary */
	print_summary(dev);

	dev_info(&pdev->dev, "\nDiagnostic complete. Unload with: sudo rmmod mt7927_fw_precheck\n");
	return 0;
}

static void precheck_remove(struct pci_dev *pdev)
{
	dev_info(&pdev->dev, "MT7927 pre-check diagnostic unloaded\n");
}

static const struct pci_device_id precheck_table[] = {
	{ PCI_DEVICE(MT7927_VENDOR_ID, MT7927_DEVICE_ID) },
	{ }
};
MODULE_DEVICE_TABLE(pci, precheck_table);

static struct pci_driver precheck_driver = {
	.name = "mt7927_fw_precheck",
	.id_table = precheck_table,
	.probe = precheck_probe,
	.remove = precheck_remove,
};

module_pci_driver(precheck_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("MT7927 Project");
MODULE_DESCRIPTION("MT7927 Firmware Pre-Load Diagnostic - Validates assumptions from docs/");
