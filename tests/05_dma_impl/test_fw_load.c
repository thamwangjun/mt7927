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
#include <linux/sched.h>

#define MT7927_VENDOR_ID        0x14c3
#define MT7927_DEVICE_ID        0x7927

/* Firmware files (MT7925 compatible per docs/FIRMWARE_ANALYSIS.md) */
#define FW_PATCH "mediatek/mt7925/WIFI_MT7925_PATCH_MCU_1_1_hdr.bin"
#define FW_RAM   "mediatek/mt7925/WIFI_RAM_CODE_MT7925_1_1.bin"

/* ============================================================
 * Register Definitions (from reference mt7927_regs.h)
 * ============================================================ */

/* WFDMA registers (BAR0 offsets)
 * CRITICAL: MT7927 WFDMA0 is at chip address 0x7C024000 (0x18024000 + CONN_INFRA_REMAPPING)
 * From fixed_map_mt7927[]: { 0x7c020000, 0x0d0000, 0x0010000 }
 * So 0x7C024000 maps to BAR0 offset 0x0d0000 + 0x4000 = 0x0d4000
 *
 * OLD WRONG VALUE: 0x2000 (from MT7921 which uses different mapping)
 */
#define MT_WFDMA0_BASE              0xd4000
#define MT_WFDMA0_HOST_INT_STA      (MT_WFDMA0_BASE + 0x200)
#define MT_WFDMA0_HOST_INT_ENA      (MT_WFDMA0_BASE + 0x204)
#define MT_WFDMA0_GLO_CFG           (MT_WFDMA0_BASE + 0x208)
#define MT_WFDMA0_RST_DTX_PTR       (MT_WFDMA0_BASE + 0x20c)
#define MT_WFDMA0_RST_DRX_PTR       (MT_WFDMA0_BASE + 0x280)

/* DMA priority and delay interrupt registers (from mt792x_regs.h)
 * These are REQUIRED for MT7927 per mt792x_dma_enable() */
#define MT_WFDMA0_INT_RX_PRI        (MT_WFDMA0_BASE + 0x298)
#define MT_WFDMA0_INT_TX_PRI        (MT_WFDMA0_BASE + 0x29c)
#define MT_WFDMA0_PRI_DLY_INT_CFG0  (MT_WFDMA0_BASE + 0x2f0)

/* DMA reset control - CRITICAL: Must reset before ring config works! */
#define MT_WFDMA0_RST               (MT_WFDMA0_BASE + 0x100)
#define MT_WFDMA0_RST_LOGIC_RST         BIT(4)
#define MT_WFDMA0_RST_DMASHDL_ALL_RST   BIT(5)

/* PHASE 27d DIAGNOSTIC REGISTERS - Error status investigation */
/* MCU_INT_STA - MCU interrupt status (memory errors, DMA errors)
 * At WFDMA0 + 0x110 = BAR0 0xd4110 */
#define MT_WFDMA0_MCU_INT_STA       (MT_WFDMA0_BASE + 0x110)
#define MT_WFDMA0_MCU_INT_MEM_RANGE_ERR  BIT(0)  /* Memory range error */
#define MT_WFDMA0_MCU_INT_DMA_ERR        BIT(1)  /* DMA error */

/* WPDMA2HOST_ERR_INT_STA - WPDMA to host error interrupt status
 * At WFDMA0 + 0x1E8 = BAR0 0xd41E8 */
#define MT_WFDMA0_WPDMA2HOST_ERR_INT_STA (MT_WFDMA0_BASE + 0x1E8)
#define MT_WFDMA0_ERR_TX_TIMEOUT_INT     BIT(0)  /* TX timeout */
#define MT_WFDMA0_ERR_RX_TIMEOUT_INT     BIT(1)  /* RX timeout */
#define MT_WFDMA0_ERR_TX_DMA_ERR_INT     BIT(2)  /* TX DMA error */
#define MT_WFDMA0_ERR_RX_DMA_ERR_INT     BIT(3)  /* RX DMA error */

/* MCU DMA0 base - chip address 0x54000000 maps to BAR0 0x2000
 * This is SEPARATE from HOST DMA0 at 0xd4000 */
#define MT_MCU_DMA0_BASE            0x2000

/* PDA (Patch Download Agent) registers at MCU_DMA0
 * These registers control firmware download on the MCU side */
#define MT_PDA_TAR_ADDR             (MT_MCU_DMA0_BASE + 0x800)  /* Target address for FW download */
#define MT_PDA_TAR_LEN              (MT_MCU_DMA0_BASE + 0x804)  /* Target length for FW download */
#define MT_PDA_DWLD_STATE           (MT_MCU_DMA0_BASE + 0x808)  /* Download state register */
#define MT_PDA_CONFG                (MT_MCU_DMA0_BASE + 0x80C)  /* Configuration register */

/* PDA_CONFG bits */
#define MT_PDA_FWDL_EN              BIT(31)  /* Firmware download enable (default on) */
#define MT_PDA_FWDL_LS_QSEL_EN      BIT(30)  /* Ring selection: 1=ring 0, 0=max ring */

/* PDA_DWLD_STATE bits */
#define MT_PDA_FWDL_FINISH          BIT(0)   /* PDA finished processing */
#define MT_PDA_FWDL_BUSY            BIT(1)   /* PDA busy */
#define MT_WFDMA_FWDL_FINISH        BIT(2)   /* WFDMA received all data */
#define MT_WFDMA_FWDL_BUSY          BIT(3)   /* WFDMA busy */
#define MT_WFDMA_FWDL_OVERFLOW      BIT(4)   /* Received more than target length */
#define MT_PDA_FWDL_OVERFLOW        BIT(6)   /* PDA overflow */

/* MCU_DMA0 GLO_CFG - RX DMA enable for MCU side */
#define MT_MCU_DMA0_GLO_CFG         (MT_MCU_DMA0_BASE + 0x208)
#define MT_MCU_DMA0_GLO_CFG_RX_DMA_EN  BIT(2)

/* HOST2MCU_SW_INT_SET - Doorbell to wake MCU from sleep
 * Phase 27e: MCU ROM sits in IDLE (0x1D1E) and doesn't actively poll DMA rings.
 * Writing BIT(0) here generates an interrupt to wake MCU and process rings.
 * CRITICAL: This is in HOST_DMA0 space (0xd4000), NOT MCU_DMA0 (0x2000)!
 * From: reference_mtk_modules/include/chips/coda/mt6639/wf_wfdma_host_dma0.h
 * Chip address 0x7c024108 → BAR0 0xd4108 */
#define MT_HOST2MCU_SW_INT_SET      (MT_WFDMA0_BASE + 0x108)

/* TX_RING_CTRL0 for Ring 16 (FWDL) - check BASE address
 * At WFDMA0 + 0x400 = BAR0 0xd4400 */
#define MT_WFDMA0_TX_RING16_CTRL0   (MT_WFDMA0_BASE + 0x400)
#define MT_WFDMA0_TX_RING16_CTRL1   (MT_WFDMA0_BASE + 0x404)
#define MT_WFDMA0_TX_RING16_CTRL2   (MT_WFDMA0_BASE + 0x408)
#define MT_WFDMA0_TX_RING16_CTRL3   (MT_WFDMA0_BASE + 0x40c)

/* GLO_CFG extension for DMASHDL */
#define MT_WFDMA0_GLO_CFG_EXT0      (MT_WFDMA0_BASE + 0x2b0)
#define MT_WFDMA0_GLO_CFG_EXT1      (MT_WFDMA0_BASE + 0x2b4)  /* MT7927-specific enable */
#define MT_WFDMA0_CSR_TX_DMASHDL_ENABLE BIT(6)

/* MT_UWFDMA0_GLO_CFG_EXT1 - Same as MT_WFDMA0_GLO_CFG_EXT1, chip address 0x7c0242b4
 * BIT(28) MUST be set for MT7927 per mt792x_dma_enable() */
#define MT_WFDMA0_GLO_CFG_EXT1_MT7927_EN  BIT(28)

/* WFDMA_DUMMY_CR - MCU address 0x54000120 (needs MCU register access)
 * Used to signal reinit needed. For simplicity, we use direct WFDMA0 offset.
 * Note: This may need L1 remap in some implementations. */
#define MT_WFDMA_DUMMY_CR           (MT_WFDMA0_BASE + 0x120)
#define MT_WFDMA_NEED_REINIT        BIT(1)

#define MT_WFDMA0_TX_RING_BASE(n)   (MT_WFDMA0_BASE + 0x300 + (n) * 0x10)
#define MT_WFDMA0_TX_RING_CNT(n)    (MT_WFDMA0_BASE + 0x304 + (n) * 0x10)
#define MT_WFDMA0_TX_RING_CIDX(n)   (MT_WFDMA0_BASE + 0x308 + (n) * 0x10)
#define MT_WFDMA0_TX_RING_DIDX(n)   (MT_WFDMA0_BASE + 0x30c + (n) * 0x10)

/* TX Ring EXT_CTRL (prefetch) registers - CRITICAL for rings 15/16!
 * Without these configured, the higher rings don't work.
 * From mt792x_regs.h: MT_WFDMA0(0x600 + n*4) for rings 0-6
 * Rings 15/16 have dedicated offsets */
#define MT_WFDMA0_TX_RING15_EXT_CTRL    (MT_WFDMA0_BASE + 0x63c)
#define MT_WFDMA0_TX_RING16_EXT_CTRL    (MT_WFDMA0_BASE + 0x640)

/* Prefetch configuration: PREFETCH(base, depth) = (base << 16) | depth
 * For MT7927 (from mt792x_dma.c):
 *   Ring 15: PREFETCH(0x0500, 0x4) = 0x05000004
 *   Ring 16: PREFETCH(0x0540, 0x4) = 0x05400004
 */
#define PREFETCH_RING15                 0x05000004
#define PREFETCH_RING16                 0x05400004

/* GLO_CFG bits - CRITICAL: All bits from MediaTek asicConnac3xWfdmaControl!
 * Reference: reference_mtk_modules/connectivity/wlan/core/gen4m/chips/common/cmm_asic_connac3x.c
 * WPDMA_GLO_CFG_STRUCT field_conn3x bit mapping from mt66xx_reg.h */
#define MT_WFDMA0_GLO_CFG_TX_DMA_EN             BIT(0)
#define MT_WFDMA0_GLO_CFG_TX_DMA_BUSY           BIT(1)
#define MT_WFDMA0_GLO_CFG_RX_DMA_EN             BIT(2)
#define MT_WFDMA0_GLO_CFG_RX_DMA_BUSY           BIT(3)
#define MT_WFDMA0_GLO_CFG_PDMA_BT_SIZE          (3 << 4)  /* Burst size = 3 */
#define MT_WFDMA0_GLO_CFG_TX_WB_DDONE           BIT(6)
#define MT_WFDMA0_GLO_CFG_CSR_AXI_BUFRDY_BYP    BIT(11)   /* AXI buffer ready bypass */
#define MT_WFDMA0_GLO_CFG_FIFO_LITTLE_ENDIAN    BIT(12)
#define MT_WFDMA0_GLO_CFG_CSR_RX_WB_DDONE       BIT(13)   /* RX write back done */
#define MT_WFDMA0_GLO_CFG_CSR_DISP_BASE_PTR_CHAIN_EN BIT(15)
#define MT_WFDMA0_GLO_CFG_CSR_LBK_RX_Q_SEL_EN   BIT(20)   /* Loopback RX queue select enable */
#define MT_WFDMA0_GLO_CFG_OMIT_RX_INFO_PFET2    BIT(21)
#define MT_WFDMA0_GLO_CFG_OMIT_TX_INFO          BIT(28)
#define MT_WFDMA0_GLO_CFG_CLK_GATE_DIS          BIT(30)   /* CRITICAL: Disable clock gating! */

/* GLO_CFG setup value BEFORE enabling DMA (from asicConnac3xWfdmaControl enable=TRUE)
 * This MUST be set before ring configuration for register writes to work! */
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

/*
 * Key register addresses - DIRECT BAR0 OFFSETS from zouyonghao's fixed_map_mt7927[]
 * These are calculated from the fixed_map entries, NOT using L1 remap.
 *
 * Fixed map format: { high_addr, bar0_offset, size }
 * Translation: bar0_offset + (addr - high_addr_base)
 *
 * Key entries from zouyonghao/mt7925/pci.c fixed_map_mt7927[]:
 *   { 0x81020000, 0x0c0000, 0x0010000 } - WF_TOP_MISC_ON (MCU_ROMCODE)
 *   { 0x7c060000, 0x0e0000, 0x0010000 } - conn_host_csr_top (LPCTL, WAKEUP)
 *   { 0x7c000000, 0x0f0000, 0x0010000 } - CONN_INFRA (AP2WF_BUS)
 *   { 0x7c010000, 0x100000, 0x0010000 } - CONN_INFRA (VERSION)
 *   { 0x70020000, 0x1f0000, 0x0010000 } - CB_INFRA (WF_RST, CRYPTO)
 */

/* From 0x7c060000 block (BAR0 + 0x0e0000) */
#define MT_CONN_ON_LPCTL                0x0e0010    /* 0x7c060010: Power management */
#define MT_MCU_STATUS                   0x0e0204    /* 0x7c060204: MCU status */
#define MT_CONN_ON_MISC                 0x0e00f0    /* 0x7c0600f0: MCU ready */
#define MT_CONNINFRA_WAKEUP             0x0e01a0    /* 0x7c0601a0: CONN_INFRA wakeup */

/* From 0x7c000000 block (BAR0 + 0x0f0000) */
#define MT_WFSYS_SW_RST_B               0x0f0140    /* 0x7c000140: WiFi subsystem reset (AP2WF) */

/* From 0x7c010000 block (BAR0 + 0x100000) */
#define MT_CONNINFRA_VERSION            0x101000    /* 0x7c011000: Version */

/* From 0x81020000 block (BAR0 + 0x0c0000) */
#define MT_MCU_ROMCODE_INDEX            0x0c1604    /* 0x81021604: MCU state */

/* NOTE: Addresses in 0x70xxxxxx range require L1 remap - see CHIP_* definitions below */

/* Expected values */
#define MCU_IDLE                        0x1D1E
#define CONNINFRA_VERSION_OK            0x03010002

/* PCIE2AP Remap - CRITICAL for PCIe to MCU communication
 * From fixed_map_mt7927[]: { 0x7c020000, 0x0d0000, 0x0010000 }
 * So 0x7C021034 = 0x7c020000 + 0x1034 → BAR0 0x0d0000 + 0x1034 = 0x0d1034
 */
#define CONN_BUS_CR_VON_PCIE2AP_REMAP_WF_1_BA           0x0d1034    /* BAR0 offset for 0x7C021034 */
#define MT7927_PCIE2AP_REMAP_WF_1_BA_VALUE              0x18051803

/* WFDMA Extension Registers - all in 0x7c020000 block → BAR0 0x0d0000
 * 0x7C027xxx = BAR0 0x0d7xxx
 * 0x7C024xxx = BAR0 0x0d4xxx
 */
#define MT_WFDMA_HOST_CONFIG                            0x0d7030    /* 0x7C027030 */
#define MT_WFDMA_MSI_INT_CFG0                           0x0d70F0    /* 0x7C0270F0 */
#define MT_WFDMA_MSI_INT_CFG1                           0x0d70F4    /* 0x7C0270F4 */
#define MT_WFDMA_MSI_INT_CFG2                           0x0d70F8    /* 0x7C0270F8 */
#define MT_WFDMA_MSI_INT_CFG3                           0x0d70FC    /* 0x7C0270FC */

/* WFDMA Host DMA extension registers (0x7C024xxx → BAR0 0x0d4xxx) */
#define MT_WFDMA_GLO_CFG_EXT1                           0x0d42B4    /* 0x7C0242B4 */
#define MT_WFDMA_GLO_CFG_EXT2                           0x0d42B8    /* 0x7C0242B8 */
#define MT_WFDMA_HOST_PER_DLY_INT_CFG                   0x0d42E8    /* 0x7C0242E8 */
#define MT_WFDMA_PAUSE_RX_Q_TH10                        0x0d4260    /* 0x7C024260 */
#define MT_WFDMA_PAUSE_RX_Q_TH1110                      0x0d4274    /* 0x7C024274 */

/* WFDMA EXT_WRAP_CSR registers (0x7C027xxx → BAR0 0x0d7xxx) */
#define MT_WFDMA_HIF_PERF_MAVG_DIV                      0x0d70C0    /* 0x7C0270C0 */
#define MT_WFDMA_DLY_IDX_CFG_0                          0x0d70E8    /* 0x7C0270E8 */

/* PCIe MAC Interrupt Routing - CRITICAL for DMA completion signaling!
 * From zouyonghao pci.c:608-612: Set AFTER IRQ registration, BEFORE DMA init
 * fixed_map: { 0x74030000, 0x010000, 0x0010000 }
 * Chip address 0x74030074 → BAR0 0x010074 */
#define MT_PCIE_MAC_INT_CONFIG                          0x010074
#define MT_PCIE_MAC_INT_CONFIG_VALUE                    0x08021000

/* MT7927 MSI configuration values (from MTK driver) */
#define MT7927_MSI_NUM_SINGLE                           0
#define MT7927_MSI_INT_CFG0_VALUE                       0x00660077
#define MT7927_MSI_INT_CFG1_VALUE                       0x00001100
#define MT7927_MSI_INT_CFG2_VALUE                       0x0030004F
#define MT7927_MSI_INT_CFG3_VALUE                       0x00542200

/* WFDMA flow control values */
#define MT7927_WPDMA_GLO_CFG_EXT1_VALUE                 0x8C800404
#define MT7927_WPDMA_GLO_CFG_EXT2_VALUE                 0x44

/* WFDMA additional configuration values (from zouyonghao mt7927_regs.h) */
#define MT7927_WFDMA_HIF_PERF_MAVG_DIV_VALUE            0x36        /* Moving average divisor */
#define MT7927_PER_DLY_INT_CFG_VALUE                    0xF00008    /* Periodic delay interrupt */
#define MT7927_DLY_IDX_CFG_RING4_7_VALUE                0x40654065  /* Ring 4-7 delay config */
#define MT7927_RX_RING_THRESHOLD_DEFAULT                0x22        /* Default threshold = 2 */

/* L1 Remap registers (for 0x70xxxxxx addresses) */
#define MT_HIF_REMAP_L1                 0x155024
#define MT_HIF_REMAP_L1_MASK            GENMASK(31, 16)
#define MT_HIF_REMAP_L1_OFFSET          GENMASK(15, 0)
#define MT_HIF_REMAP_L1_BASE            GENMASK(31, 16)
#define MT_HIF_REMAP_BASE_L1            0x130000

/* ==========================================================================
 * CB_INFRA Register Definitions (from reference_mtk_modules/coda/mt6639/)
 *
 * CRITICAL: These MUST be initialized BEFORE any WFDMA register access!
 *
 * Address translation via fixed_map from zouyonghao pci.c:
 *   { 0x70020000, 0x1f0000, 0x0010000 }
 * So chip address 0x70020000 + offset → BAR0 0x1f0000 + offset
 *
 * NOTE: Addresses 0x70020000-0x7002FFFF are in fixed_map and use DIRECT access.
 *       Addresses 0x70005xxx (CBTOP GPIO) are NOT in fixed_map, need L1 remap.
 * ========================================================================== */

/* CB_INFRA_MISC0 - PCIe Remap Configuration (direct BAR0 offsets)
 * Chip 0x70026000 + offset → BAR0 0x1f6000 + offset
 * From: reference_gen4m/chips/mt6639/mt6639.c:2727-2737 (set_cbinfra_remap)
 */
#define CB_INFRA_PCIE_REMAP_WF          0x1f6554    /* Chip 0x70026554 */
#define CB_INFRA_PCIE_REMAP_WF_BT       0x1f6558    /* Chip 0x70026558 */
#define CB_INFRA_PCIE_REMAP_WF_VALUE    0x74037001  /* From mt6639.c */
#define CB_INFRA_PCIE_REMAP_WF_BT_VALUE 0x70007000  /* From mt6639.c */

/* CB_INFRA_RGU - Reset Generation Unit (direct BAR0 offsets)
 * Chip 0x70028000 + offset → BAR0 0x1f8000 + offset
 * From: reference_mtk_modules/coda/mt6639/cb_infra_rgu.h
 */
#define CB_INFRA_WF_SUBSYS_RST          0x1f8600    /* Chip 0x70028600 */
#define CB_INFRA_BT_SUBSYS_RST          0x1f8610    /* Chip 0x70028610 */

/* CB_INFRA_SLP_CTRL - Sleep Control (direct BAR0 offsets)
 * Chip 0x70025000 + offset → BAR0 0x1f5000 + offset
 * From: reference_mtk_modules/coda/mt6639/cb_infra_slp_ctrl.h
 * IMPORTANT: Use the SET register (0x034) to grant MCU ownership, not status (0x030)
 */
#define CB_INFRA_CRYPTO_MCU_OWN         0x1f5030    /* Chip 0x70025030 - Status */
#define CB_INFRA_CRYPTO_MCU_OWN_SET     0x1f5034    /* Chip 0x70025034 - SET register */

/* CBTOP GPIO Mode Configuration (0x70005000 range)
 * NOTE: These are NOT in fixed_map, so they need L1 remap!
 * From: reference_gen4m/chips/mt6639/mt6639.c:2651-2653 (mt6639_mcu_reinit)
 */
#define CBTOP_GPIO_MODE5_CHIP           0x7000535c  /* Needs L1 remap */
#define CBTOP_GPIO_MODE6_CHIP           0x7000536c  /* Needs L1 remap */
#define GPIO_MODE5_VALUE                0x80000000  /* From mt6639.c */
#define GPIO_MODE6_VALUE                0x80        /* From mt6639.c */

/* Reset sequence values from MTK mt6639
 * From: reference_gen4m/chips/mt6639/mt6639.c:2660-2669
 */
#define WF_SUBSYS_RST_ASSERT            0x10351
#define WF_SUBSYS_RST_DEASSERT          0x10340
#define BT_SUBSYS_RST_ASSERT            0x10351
#define BT_SUBSYS_RST_DEASSERT          0x10340

/* WF_SUBSYS_RST bit fields for RMW */
#define WF_SUBSYS_RST_WF_MASK           0x00000010
#define WF_SUBSYS_RST_WF_SHFT           4

/* Backward compatibility - these are now direct BAR0 offsets */
#define CHIP_WF_SUBSYS_RST              CB_INFRA_WF_SUBSYS_RST
#define CHIP_BT_SUBSYS_RST              CB_INFRA_BT_SUBSYS_RST
#define CHIP_CRYPTO_MCU_OWN             CB_INFRA_CRYPTO_MCU_OWN_SET

/* LPCTL bits - Power management handshake
 * From zouyonghao mt792x_core.c:
 *   SET_OWN (BIT 0) = Write to give ownership TO FIRMWARE
 *   CLR_OWN (BIT 1) = Write to claim ownership FOR DRIVER
 *   OWN_SYNC (BIT 2) = Status bit: 4 = FW owns, 0 = driver owns
 */
#define PCIE_LPCR_HOST_SET_OWN          BIT(0)  /* Give to firmware */
#define PCIE_LPCR_HOST_CLR_OWN          BIT(1)  /* Claim for driver */
#define PCIE_LPCR_HOST_OWN_SYNC         BIT(2)  /* Status: 4=FW, 0=driver */

/* Backward compatibility aliases */
#define MT_LPCTL_SET_OWN                PCIE_LPCR_HOST_SET_OWN
#define MT_LPCTL_CLR_OWN                PCIE_LPCR_HOST_CLR_OWN
#define MT_LPCTL_OWN_SYNC               PCIE_LPCR_HOST_OWN_SYNC

/* WFSYS bits */
#define MT_WFSYS_SW_RST_B_EN            BIT(0)
#define MT_WFSYS_SW_INIT_DONE           BIT(4)

/* Ring configuration - MT7927 uses BOTH rings per zouyonghao reference:
 * - Ring 15 (MCU_WM): Init commands (PATCH_START_REQ, TARGET_ADDRESS_LEN_REQ, etc.)
 * - Ring 16 (FWDL): Firmware data chunks (FW_SCATTER)
 */
#define MCU_WM_RING_IDX         15      /* Ring 15 for MCU commands */
#define FWDL_RING_IDX           16      /* Ring 16 for firmware download */
#define RING_SIZE               128
#define FW_CHUNK_SIZE           4096    /* 4KB chunks like reference */

/* DMA descriptor format */
struct mt7927_desc {
	__le32 buf0;
	__le32 ctrl;
	__le32 buf1;
	__le32 info;
} __packed __aligned(4);

/* Descriptor control bits - from MediaTek TXD_STRUCT in hif_pdma.h
 *
 * Word 1 layout (little-endian bitfield):
 *   bits 0-13:  SDLen1   - Length for scatter pointer 1
 *   bit 14:     LastSec1 - Last section for pointer 1
 *   bit 15:     Burst    - Burst flag
 *   bits 16-29: SDLen0   - Length for scatter pointer 0 (OUR DATA!)
 *   bit 30:     LastSec0 - Last section for pointer 0 (SET THIS!)
 *   bit 31:     DMADONE  - DMA done flag
 *
 * CRITICAL: SDLen0 and LastSec0 are in bits 16-30, NOT bits 0-14!
 * Previous bug: We put length in bits 0-13 (SDLen1) which made hardware
 * think buffer 1 (SDPtr1=0x0) had the data, causing page fault at addr 0!
 */
#define MT_DMA_CTL_SD_LEN1      GENMASK(13, 0)   /* bits 0-13: buffer 1 length */
#define MT_DMA_CTL_LAST_SEC1    BIT(14)          /* bit 14: buffer 1 last section */
#define MT_DMA_CTL_BURST        BIT(15)          /* bit 15: burst mode */
/* TEMPORARILY REVERTED TO OLD (BUGGY) FORMAT FOR PAGE FAULT TEST!
 * This should cause page faults at address 0 if DMA is working. */
#define MT_DMA_CTL_SD_LEN0      GENMASK(13, 0)   /* OLD: bits 0-13 (WRONG - causes page fault!) */
#define MT_DMA_CTL_LAST_SEC0    BIT(14)          /* OLD: bit 14 (WRONG - causes page fault!) */
#define MT_DMA_CTL_DMA_DONE     BIT(31)          /* bit 31: DMA done */

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
		u32 rsv[11];	/* Phase 27f fix: was missing 44 bytes */
	} desc;
} __packed;

struct mt7927_patch_sec {
	__be32 type;
	__be32 offs;	/* Phase 27f fix: moved from end to position 2 */
	__be32 size;	/* Phase 27f fix: was missing entirely */
	union {
		__be32 spec[13];
		struct {
			__be32 addr;
			__be32 len;
			__be32 sec_key_idx;
			__be32 align_len;
			u32 rsv[9];
		} info;
	};
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

	/* Ring 15 - MCU_WM (for init commands) */
	struct mt7927_desc *mcu_ring;
	dma_addr_t mcu_ring_dma;
	int mcu_ring_head;

	/* Ring 16 - FWDL (for firmware data) */
	struct mt7927_desc *fwdl_ring;
	dma_addr_t fwdl_ring_dma;
	int fwdl_ring_head;

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

/*
 * L1 Remap for addresses in 0x70xxxxxx range
 * These addresses are NOT in fixed_map and require dynamic remapping.
 *
 * The L1 remap mechanism:
 * 1. Write base (upper 16 bits of target addr) to MT_HIF_REMAP_L1 register
 * 2. Access via MT_HIF_REMAP_BASE_L1 + offset (lower 16 bits)
 */
static u32 backup_l1 = 0;

static u32 reg_map_l1(struct test_dev *dev, u32 addr)
{
	u32 offset = FIELD_GET(MT_HIF_REMAP_L1_OFFSET, addr);
	u32 base = FIELD_GET(MT_HIF_REMAP_L1_BASE, addr);
	u32 val;

	/* Save current remap value */
	backup_l1 = mt_rr(dev, MT_HIF_REMAP_L1);

	/* Set new remap base */
	val = (backup_l1 & ~MT_HIF_REMAP_L1_MASK) |
	      FIELD_PREP(MT_HIF_REMAP_L1_MASK, base);
	mt_wr(dev, MT_HIF_REMAP_L1, val);

	/* Read back to push write */
	mt_rr(dev, MT_HIF_REMAP_L1);

	return MT_HIF_REMAP_BASE_L1 + offset;
}

static void reg_remap_restore(struct test_dev *dev)
{
	if (backup_l1) {
		mt_wr(dev, MT_HIF_REMAP_L1, backup_l1);
		backup_l1 = 0;
	}
}

/* Read/write with automatic L1 remap for 0x70xxxxxx addresses (CBTOP GPIO only) */
static u32 __maybe_unused mt_rr_remap(struct test_dev *dev, u32 addr)
{
	u32 val, mapped_addr;

	if (addr >= 0x70000000 && addr < 0x78000000) {
		mapped_addr = reg_map_l1(dev, addr);
		val = mt_rr(dev, mapped_addr);
		reg_remap_restore(dev);
		return val;
	}

	/* For fixed_map addresses, use direct access */
	return mt_rr(dev, addr);
}

static void mt_wr_remap(struct test_dev *dev, u32 addr, u32 val)
{
	u32 mapped_addr;

	if (addr >= 0x70000000 && addr < 0x78000000) {
		mapped_addr = reg_map_l1(dev, addr);
		mt_wr(dev, mapped_addr, val);
		reg_remap_restore(dev);
		return;
	}

	/* For fixed_map addresses, use direct access */
	mt_wr(dev, addr, val);
}

/* ============================================================
 * MCU Command Functions (with TXD header for init commands)
 * ============================================================ */

static u8 mcu_seq = 0;

/*
 * TX cleanup - wait for ring to drain (DIDX catches up to head)
 * Per zouyonghao: Called before AND after each chunk with force=true
 *
 * @ring_idx: Which ring to clean (MCU_WM_RING_IDX or FWDL_RING_IDX)
 * @head: Current ring head position
 * @flush: If true, wait longer and reset ring tracking
 */
static void tx_cleanup(struct test_dev *dev, int ring_idx, int head, bool flush)
{
	u32 didx;
	int timeout = flush ? 200 : 50;

	/* Wait for DMA to process all pending descriptors */
	while (timeout-- > 0) {
		didx = mt_rr(dev, MT_WFDMA0_TX_RING_DIDX(ring_idx));
		if (didx == head)
			return;
		usleep_range(50, 100);
	}

	/* If flush mode and still not caught up, reset the ring pointer */
	if (flush) {
		mt_wr(dev, MT_WFDMA0_RST_DTX_PTR, BIT(ring_idx));
		wmb();
		usleep_range(100, 200);
	}
}

/*
 * Send MCU command with proper TXD header via Ring 15 (MCU_WM)
 * Note: FW_SCATTER doesn't use TXD header, only init commands do
 * Per zouyonghao: Init commands go to Ring 15, FW data goes to Ring 16
 *
 * Uses same aggressive cleanup pattern as send_fw_chunk
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

	/* Step 1: Aggressive cleanup BEFORE sending (per zouyonghao) */
	tx_cleanup(dev, MCU_WM_RING_IDX, dev->mcu_ring_head, true);

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

	/* Setup descriptor on Ring 15 (MCU_WM) */
	idx = dev->mcu_ring_head;
	desc = &dev->mcu_ring[idx];

	/* TXD descriptor format (from MediaTek hif_pdma.h TXD_STRUCT):
	 * Word 0 (buf0): SDPtr0 - lower 32 bits of buffer 0 address
	 * Word 1 (ctrl): Control word with SDLen0 in bits 16-29, LastSec0 in bit 30
	 * Word 2 (buf1): SDPtr1 - lower 32 bits of buffer 1 (0 if not using scatter)
	 * Word 3 (info): SDPtr0Ext:SDPtr1Ext - upper 16 bits of each pointer packed
	 */
	/* OLD FORMAT FOR PAGE FAULT TEST - buf1 gets upper bits, info=0 */
	desc->buf0 = cpu_to_le32(lower_32_bits(dev->dma_buf_phys));
	desc->buf1 = cpu_to_le32(upper_32_bits(dev->dma_buf_phys));  /* OLD: upper bits here */
	ctrl = FIELD_PREP(MT_DMA_CTL_SD_LEN0, total_len) | MT_DMA_CTL_LAST_SEC0;
	desc->ctrl = cpu_to_le32(ctrl);
	desc->info = cpu_to_le32(0);  /* OLD: info was 0 */
	wmb();

	/* DIAGNOSTIC: Dump descriptor before DMA kick (first MCU cmd only) */
	{
		static int mcu_dump_count;
		if (mcu_dump_count < 2) {
			u32 c = le32_to_cpu(desc->ctrl);
			dev_info(&dev->pdev->dev, "  [DIAG] Ring 15 desc[%d] before kick:\n", idx);
			dev_info(&dev->pdev->dev, "    buf0=0x%08x (SDPtr0 lower32)\n", le32_to_cpu(desc->buf0));
			dev_info(&dev->pdev->dev, "    ctrl=0x%08x (SDLen0=%d[bits16-29], LS0=%d[bit30], DONE=%d[bit31])\n",
				 c, (c >> 16) & 0x3FFF, (c >> 30) & 1, (c >> 31) & 1);
			dev_info(&dev->pdev->dev, "    buf1=0x%08x (SDPtr1 - should be 0 for single buffer)\n", le32_to_cpu(desc->buf1));
			dev_info(&dev->pdev->dev, "    info=0x%08x (SDPtr0Ext=%d, SDPtr1Ext=%d)\n",
				 le32_to_cpu(desc->info),
				 le32_to_cpu(desc->info) & 0xFFFF,
				 (le32_to_cpu(desc->info) >> 16) & 0xFFFF);
			dev_info(&dev->pdev->dev, "    dma_buf_phys=0x%llx\n", (unsigned long long)dev->dma_buf_phys);
			mcu_dump_count++;
		}
	}

	/* Kick DMA on Ring 15 */
	dev->mcu_ring_head = (idx + 1) % RING_SIZE;
	mt_wr(dev, MT_WFDMA0_TX_RING_CIDX(MCU_WM_RING_IDX), dev->mcu_ring_head);
	wmb();

	/* Phase 27e: Trigger MCU interrupt - MCU ROM is in IDLE (0x1D1E) and
	 * doesn't actively poll DMA rings. We must ring the doorbell to wake it.
	 * BIT(0) = SW_INT_0, generates interrupt on MCU side. */
	mt_wr(dev, MT_HOST2MCU_SW_INT_SET, BIT(0));
	wmb();

	/* Poll for completion - but don't wait for mailbox response */
	timeout = 100;
	while (timeout-- > 0) {
		didx = mt_rr(dev, MT_WFDMA0_TX_RING_DIDX(MCU_WM_RING_IDX));
		if (didx == dev->mcu_ring_head)
			break;
		usleep_range(100, 200);
	}

	if (timeout <= 0)
		dev_dbg(&dev->pdev->dev, "  MCU cmd 0x%02x on Ring 15: DMA timeout (continuing)\n", cmd);

	/* Step 3: Aggressive cleanup AFTER sending (per zouyonghao) */
	tx_cleanup(dev, MCU_WM_RING_IDX, dev->mcu_ring_head, true);

	/* Step 4: Let other kernel threads run (per zouyonghao) */
	cond_resched();

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
 * Phase 0a: CB_INFRA PCIe Remap Initialization (CRITICAL - must run FIRST!)
 * Reference: reference_gen4m/chips/mt6639/mt6639.c:2727-2737 (set_cbinfra_remap)
 *
 * This sets up the PCIe to WFDMA address translation.
 * Without this, WFDMA registers may not be accessible!
 * ============================================================ */

static int init_cbinfra_remap(struct test_dev *dev)
{
	u32 val;

	dev_info(&dev->pdev->dev, "=== Phase 0a: CB_INFRA PCIe Remap Initialization ===\n");

	/* Step 1: Set PCIe remap for WiFi WFDMA access
	 * From mt6639.c: HAL_MCR_WR(ad, CB_INFRA_MISC0_CBTOP_PCIE_REMAP_WF_ADDR, 0x74037001)
	 * This maps PCIe address space to the correct WFDMA registers
	 *
	 * NOTE: CB_INFRA registers are in fixed_map, so use DIRECT BAR0 access!
	 * Chip 0x70026554 → BAR0 0x1f6554 (via fixed_map { 0x70020000, 0x1f0000, 0x0010000 })
	 */
	dev_info(&dev->pdev->dev, "  Setting CB_INFRA PCIE_REMAP_WF (BAR0 + 0x%06x)...\n",
		 CB_INFRA_PCIE_REMAP_WF);
	mt_wr(dev, CB_INFRA_PCIE_REMAP_WF, CB_INFRA_PCIE_REMAP_WF_VALUE);
	val = mt_rr(dev, CB_INFRA_PCIE_REMAP_WF);
	dev_info(&dev->pdev->dev, "    PCIE_REMAP_WF = 0x%08x (expected 0x%08x) %s\n",
		 val, CB_INFRA_PCIE_REMAP_WF_VALUE,
		 val == CB_INFRA_PCIE_REMAP_WF_VALUE ? "OK" : "MISMATCH!");

	/* Step 2: Set PCIe remap for WiFi/BT shared area
	 * From mt6639.c: HAL_MCR_WR(ad, CB_INFRA_MISC0_CBTOP_PCIE_REMAP_WF_BT_ADDR, 0x70007000)
	 */
	dev_info(&dev->pdev->dev, "  Setting CB_INFRA PCIE_REMAP_WF_BT (BAR0 + 0x%06x)...\n",
		 CB_INFRA_PCIE_REMAP_WF_BT);
	mt_wr(dev, CB_INFRA_PCIE_REMAP_WF_BT, CB_INFRA_PCIE_REMAP_WF_BT_VALUE);
	val = mt_rr(dev, CB_INFRA_PCIE_REMAP_WF_BT);
	dev_info(&dev->pdev->dev, "    PCIE_REMAP_WF_BT = 0x%08x (expected 0x%08x) %s\n",
		 val, CB_INFRA_PCIE_REMAP_WF_BT_VALUE,
		 val == CB_INFRA_PCIE_REMAP_WF_BT_VALUE ? "OK" : "MISMATCH!");

	/* Step 3: Brief delay for remap to take effect */
	usleep_range(100, 200);

	dev_info(&dev->pdev->dev, "  CB_INFRA PCIe remap initialization complete\n");
	return 0;
}

/* ============================================================
 * Phase 0b: MT7927 WiFi/BT Subsystem Reset (CRITICAL - must run after remap!)
 * Reference: pci.c::mt7927_wfsys_reset()
 *
 * This resets the entire WiFi subsystem including WFDMA.
 * Without this, WFDMA ring registers may not accept writes!
 * ============================================================ */

static int wfsys_reset(struct test_dev *dev)
{
	u32 val;

	dev_info(&dev->pdev->dev, "=== Phase 0b: WiFi/BT Subsystem Reset ===\n");

	/* Step 1: GPIO mode configuration
	 * NOTE: CBTOP_GPIO addresses (0x70005xxx) are NOT in fixed_map,
	 * so they need L1 remap access.
	 */
	dev_info(&dev->pdev->dev, "  Setting GPIO mode registers (via L1 remap)...\n");
	mt_wr_remap(dev, CBTOP_GPIO_MODE5_CHIP, GPIO_MODE5_VALUE);
	mt_wr_remap(dev, CBTOP_GPIO_MODE6_CHIP, GPIO_MODE6_VALUE);
	usleep_range(100, 200);

	/* Step 2: BT subsystem reset
	 * NOTE: CB_INFRA addresses are in fixed_map, use DIRECT access.
	 */
	dev_info(&dev->pdev->dev, "  Resetting BT subsystem (BAR0 + 0x%06x)...\n",
		 CHIP_BT_SUBSYS_RST);
	mt_wr(dev, CHIP_BT_SUBSYS_RST, BT_SUBSYS_RST_ASSERT);
	msleep(10);
	mt_wr(dev, CHIP_BT_SUBSYS_RST, BT_SUBSYS_RST_DEASSERT);
	msleep(10);

	/* Step 3: First WF subsystem reset */
	dev_info(&dev->pdev->dev, "  Resetting WF subsystem (first pass, BAR0 + 0x%06x)...\n",
		 CHIP_WF_SUBSYS_RST);
	mt_wr(dev, CHIP_WF_SUBSYS_RST, WF_SUBSYS_RST_ASSERT);
	msleep(10);
	mt_wr(dev, CHIP_WF_SUBSYS_RST, WF_SUBSYS_RST_DEASSERT);
	msleep(50);

	/* Step 4: Second WF reset (MTK RMW on bit 4 - exact mt6639 sequence) */
	dev_info(&dev->pdev->dev, "  Resetting WF subsystem (second pass - RMW)...\n");

	/* Read current value */
	val = mt_rr(dev, CHIP_WF_SUBSYS_RST);
	dev_info(&dev->pdev->dev, "    WF_SUBSYS_RST read: 0x%08x\n", val);

	/* Assert reset: clear mask, then set bit */
	val &= ~WF_SUBSYS_RST_WF_MASK;
	val |= (1 << WF_SUBSYS_RST_WF_SHFT);
	mt_wr(dev, CHIP_WF_SUBSYS_RST, val);
	dev_info(&dev->pdev->dev, "    WF_SUBSYS_RST wrote: 0x%08x (assert)\n", val);
	msleep(1);

	/* Read again */
	val = mt_rr(dev, CHIP_WF_SUBSYS_RST);
	dev_info(&dev->pdev->dev, "    WF_SUBSYS_RST after 1ms: 0x%08x\n", val);

	/* De-assert reset */
	val &= ~WF_SUBSYS_RST_WF_MASK;
	mt_wr(dev, CHIP_WF_SUBSYS_RST, val);
	dev_info(&dev->pdev->dev, "    WF_SUBSYS_RST wrote: 0x%08x (de-assert)\n", val);
	msleep(10);

	dev_info(&dev->pdev->dev, "  WF/BT subsystem reset complete\n");
	return 0;
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
	mt_wr(dev, MT_CONNINFRA_WAKEUP, 0x1);
	msleep(5);

	/* Step 2: Poll for CONN_INFRA version */
	dev_info(&dev->pdev->dev, "  Polling CONN_INFRA version...\n");
	for (i = 0; i < 100; i++) {
		val = mt_rr(dev, MT_CONNINFRA_VERSION);
		if (val == CONNINFRA_VERSION_OK || val == 0x03010001) {
			dev_info(&dev->pdev->dev, "  CONN_INFRA version: 0x%08x (OK)\n", val);
			break;
		}
		msleep(10);
	}
	if (i >= 100) {
		val = mt_rr(dev, MT_CONNINFRA_VERSION);
		dev_warn(&dev->pdev->dev, "  CONN_INFRA version: 0x%08x (unexpected, continuing)\n", val);
	}

	/* Step 3: WFSYS reset already done in Phase 0 - skip duplicate reset here
	 * Per zouyonghao analysis: mt7927e_mcu_pre_init() only does single reset */

	/* Step 4: Set Crypto MCU ownership
	 * NOTE: CB_INFRA addresses are in fixed_map, use DIRECT access.
	 */
	dev_info(&dev->pdev->dev, "  Setting Crypto MCU ownership (BAR0 + 0x%06x)...\n",
		 CHIP_CRYPTO_MCU_OWN);
	mt_wr(dev, CHIP_CRYPTO_MCU_OWN, BIT(0));
	msleep(5);

	/* Step 5: Wait for MCU IDLE state */
	dev_info(&dev->pdev->dev, "  Waiting for MCU IDLE (0x%04x)...\n", MCU_IDLE);
	for (i = 0; i < 500; i++) {
		val = mt_rr(dev, MT_MCU_ROMCODE_INDEX);
		if ((val & 0xFFFF) == MCU_IDLE) {
			dev_info(&dev->pdev->dev, "  MCU IDLE reached: 0x%08x\n", val);
			return 0;
		}
		if ((i % 50) == 0 && i > 0) {
			dev_info(&dev->pdev->dev, "  MCU state: 0x%08x (waiting...)\n", val);
		}
		msleep(10);
	}

	val = mt_rr(dev, MT_MCU_ROMCODE_INDEX);
	dev_err(&dev->pdev->dev, "  MCU IDLE timeout! State: 0x%08x\n", val);
	return -ETIMEDOUT;
}

/* ============================================================
 * Phase 2: Power Management - Claim Host Ownership
 * ============================================================ */

/* ============================================================
 * Power Management - Two-step handshake from zouyonghao
 * Reference: mt792x_core.c lines 872-895 and 827-848
 *
 * CRITICAL: These must run BEFORE WFSYS reset!
 * The full handshake is:
 *   1. fw_pmctrl: Give ownership to firmware (SET_OWN, wait for OWN_SYNC=4)
 *   2. drv_pmctrl: Claim ownership for driver (CLR_OWN, wait for OWN_SYNC=0)
 * ============================================================ */

/* Step 1: Give ownership to firmware (fw_pmctrl) */
static int fw_pmctrl(struct test_dev *dev)
{
	u32 val;
	int i;

	dev_info(&dev->pdev->dev, "  fw_pmctrl: Giving ownership to firmware...\n");

	val = mt_rr(dev, MT_CONN_ON_LPCTL);
	dev_info(&dev->pdev->dev, "    LPCTL before SET_OWN: 0x%08x\n", val);

	/* Write SET_OWN to give ownership to firmware */
	mt_wr(dev, MT_CONN_ON_LPCTL, PCIE_LPCR_HOST_SET_OWN);

	/* Poll until OWN_SYNC becomes 4 (BIT(2) set = FW owns) */
	for (i = 0; i < 100; i++) {
		val = mt_rr(dev, MT_CONN_ON_LPCTL);
		if ((val & PCIE_LPCR_HOST_OWN_SYNC) == PCIE_LPCR_HOST_OWN_SYNC) {
			dev_info(&dev->pdev->dev, "    Firmware owns device: LPCTL=0x%08x\n", val);
			return 0;
		}
		msleep(1);
	}

	dev_warn(&dev->pdev->dev, "    fw_pmctrl timeout (LPCTL=0x%08x)\n", val);
	return -ETIMEDOUT;
}

/* Step 2: Claim ownership for driver (drv_pmctrl) */
static int drv_pmctrl(struct test_dev *dev)
{
	u32 val;
	int i;

	dev_info(&dev->pdev->dev, "  drv_pmctrl: Claiming ownership for driver...\n");

	val = mt_rr(dev, MT_CONN_ON_LPCTL);
	dev_info(&dev->pdev->dev, "    LPCTL before CLR_OWN: 0x%08x\n", val);

	/* Write CLR_OWN to claim ownership for driver */
	mt_wr(dev, MT_CONN_ON_LPCTL, PCIE_LPCR_HOST_CLR_OWN);

	/* Per zouyonghao: 2-3ms delay if ASPM supported */
	usleep_range(2000, 3000);

	/* Poll until OWN_SYNC becomes 0 (BIT(2) clear = driver owns) */
	for (i = 0; i < 100; i++) {
		val = mt_rr(dev, MT_CONN_ON_LPCTL);
		if (!(val & PCIE_LPCR_HOST_OWN_SYNC)) {
			dev_info(&dev->pdev->dev, "    Driver owns device: LPCTL=0x%08x\n", val);
			return 0;
		}
		msleep(1);
	}

	dev_warn(&dev->pdev->dev, "    drv_pmctrl timeout (LPCTL=0x%08x)\n", val);
	return -ETIMEDOUT;
}

/* Combined power control sequence - MUST run BEFORE wfsys_reset! */
static int power_control_handshake(struct test_dev *dev)
{
	int ret;

	dev_info(&dev->pdev->dev, "=== Power Control Handshake ===\n");

	/* Step 1: Give to firmware */
	ret = fw_pmctrl(dev);
	if (ret) {
		dev_warn(&dev->pdev->dev, "  fw_pmctrl failed, continuing anyway...\n");
	}

	/* Step 2: Claim for driver */
	ret = drv_pmctrl(dev);
	if (ret) {
		dev_warn(&dev->pdev->dev, "  drv_pmctrl failed, continuing anyway...\n");
	}

	return 0; /* Always continue - failures logged but not fatal */
}

/* Legacy function name for compatibility */
static int claim_host_ownership(struct test_dev *dev)
{
	dev_info(&dev->pdev->dev, "=== Phase 2: Claim Host Ownership (drv_pmctrl only) ===\n");
	return drv_pmctrl(dev);
}

/* ============================================================
 * Phase 2a: PCIe MAC Interrupt Routing Configuration
 * Reference: zouyonghao pci.c:608-612
 *
 * CRITICAL: This configures how DMA completion interrupts are routed
 * through the PCIe MAC. Without this, DMA engine might work but
 * can't signal completion properly (DIDX doesn't advance).
 *
 * Set AFTER power control/IRQ setup, BEFORE DMA init.
 * ============================================================ */

static int configure_pcie_mac_int(struct test_dev *dev)
{
	u32 val;

	dev_info(&dev->pdev->dev, "=== Phase 2a: PCIe MAC Interrupt Routing ===\n");

	/* From zouyonghao pci.c:608-612:
	 * if (pdev->device == 0x7927) {
	 *     mt76_wr(dev, MT7927_PCIE_MAC_INT_CONFIG_ADDR, MT7927_PCIE_MAC_INT_CONFIG_VALUE);
	 * }
	 * This is MT7927-specific - MT7925 doesn't need it.
	 */
	dev_info(&dev->pdev->dev, "  Setting PCIe MAC interrupt config (BAR0 + 0x%06x)...\n",
		 MT_PCIE_MAC_INT_CONFIG);
	mt_wr(dev, MT_PCIE_MAC_INT_CONFIG, MT_PCIE_MAC_INT_CONFIG_VALUE);
	val = mt_rr(dev, MT_PCIE_MAC_INT_CONFIG);
	dev_info(&dev->pdev->dev, "    PCIE_MAC_INT_CONFIG = 0x%08x (expected 0x%08x) %s\n",
		 val, MT_PCIE_MAC_INT_CONFIG_VALUE,
		 val == MT_PCIE_MAC_INT_CONFIG_VALUE ? "OK" : "MISMATCH!");

	return 0;
}

/* ============================================================
 * Phase 2.5: WFDMA Extension Configuration (MSI, GLO_CFG_EXT, etc.)
 * Reference: pci_mcu.c::mt7927e_mcu_init() lines 159-206
 *
 * NOTE: PCIE2AP remap moved to Phase 3.5 (after DMA init) per
 * zouyonghao initialization order analysis.
 * ============================================================ */

static int configure_wfdma_extensions(struct test_dev *dev)
{
	dev_info(&dev->pdev->dev, "=== Phase 2.5: WFDMA Extension Configuration ===\n");

	/* Step 1: Configure WFDMA MSI (single MSI mode) */
	dev_info(&dev->pdev->dev, "  Configuring WFDMA MSI...\n");
	mt_wr(dev, MT_WFDMA_HOST_CONFIG, MT7927_MSI_NUM_SINGLE);
	mt_wr(dev, MT_WFDMA_MSI_INT_CFG0, MT7927_MSI_INT_CFG0_VALUE);
	mt_wr(dev, MT_WFDMA_MSI_INT_CFG1, MT7927_MSI_INT_CFG1_VALUE);
	mt_wr(dev, MT_WFDMA_MSI_INT_CFG2, MT7927_MSI_INT_CFG2_VALUE);
	mt_wr(dev, MT_WFDMA_MSI_INT_CFG3, MT7927_MSI_INT_CFG3_VALUE);
	dev_info(&dev->pdev->dev, "    MSI_INT_CFG0-3 configured\n");

	/* Step 2: Configure WFDMA extensions (TX flow control) */
	dev_info(&dev->pdev->dev, "  Configuring WFDMA extensions...\n");
	mt_wr(dev, MT_WFDMA_GLO_CFG_EXT1, MT7927_WPDMA_GLO_CFG_EXT1_VALUE);
	mt_wr(dev, MT_WFDMA_GLO_CFG_EXT2, MT7927_WPDMA_GLO_CFG_EXT2_VALUE);
	dev_info(&dev->pdev->dev, "    GLO_CFG_EXT1 = 0x%08x, EXT2 = 0x%08x\n",
		 mt_rr(dev, MT_WFDMA_GLO_CFG_EXT1), mt_rr(dev, MT_WFDMA_GLO_CFG_EXT2));

	/* Step 3: Configure HIF performance moving average divisor
	 * From zouyonghao pci_mcu.c: mt7927e_mcu_init() */
	dev_info(&dev->pdev->dev, "  Configuring WFDMA HIF performance...\n");
	mt_wr(dev, MT_WFDMA_HIF_PERF_MAVG_DIV, MT7927_WFDMA_HIF_PERF_MAVG_DIV_VALUE);
	dev_info(&dev->pdev->dev, "    HIF_PERF_MAVG_DIV = 0x%08x\n",
		 mt_rr(dev, MT_WFDMA_HIF_PERF_MAVG_DIV));

	/* Step 4: Configure RX ring thresholds
	 * From zouyonghao pci_mcu.c: Loop from TH10 to TH1110 */
	dev_info(&dev->pdev->dev, "  Configuring RX ring thresholds...\n");
	{
		u32 addr;
		for (addr = MT_WFDMA_PAUSE_RX_Q_TH10;
		     addr <= MT_WFDMA_PAUSE_RX_Q_TH1110;
		     addr += 0x4) {
			mt_wr(dev, addr, MT7927_RX_RING_THRESHOLD_DEFAULT);
		}
	}
	dev_info(&dev->pdev->dev, "    RX thresholds set to 0x%02x\n",
		 MT7927_RX_RING_THRESHOLD_DEFAULT);

	/* Step 5: Configure periodic delayed interrupt
	 * From zouyonghao pci_mcu.c */
	dev_info(&dev->pdev->dev, "  Configuring delay interrupts...\n");
	mt_wr(dev, MT_WFDMA_HOST_PER_DLY_INT_CFG, MT7927_PER_DLY_INT_CFG_VALUE);
	dev_info(&dev->pdev->dev, "    PER_DLY_INT_CFG = 0x%08x\n",
		 mt_rr(dev, MT_WFDMA_HOST_PER_DLY_INT_CFG));

	/* Step 6: Configure ring 4-7 delay interrupt
	 * From zouyonghao pci_mcu.c */
	mt_wr(dev, MT_WFDMA_DLY_IDX_CFG_0, MT7927_DLY_IDX_CFG_RING4_7_VALUE);
	dev_info(&dev->pdev->dev, "    DLY_IDX_CFG_0 = 0x%08x\n",
		 mt_rr(dev, MT_WFDMA_DLY_IDX_CFG_0));

	dev_info(&dev->pdev->dev, "  PCIE/WFDMA configuration complete\n");
	return 0;
}

/* ============================================================
 * Phase 3: DMA Ring Setup
 * ============================================================ */

static int setup_dma_ring(struct test_dev *dev)
{
	u32 val;

	dev_info(&dev->pdev->dev, "=== Phase 3: DMA Ring Setup ===\n");
	dev_info(&dev->pdev->dev, "  Setting up Ring 15 (MCU_WM) and Ring 16 (FWDL)\n");

	/* Allocate Ring 15 (MCU_WM) for init commands */
	dev->mcu_ring = dma_alloc_coherent(&dev->pdev->dev,
					   RING_SIZE * sizeof(struct mt7927_desc),
					   &dev->mcu_ring_dma, GFP_KERNEL);
	if (!dev->mcu_ring) {
		dev_err(&dev->pdev->dev, "  Failed to allocate MCU ring\n");
		return -ENOMEM;
	}
	/* CRITICAL: Initialize ALL descriptors with DMA_DONE bit set!
	 * mt76 does this in __mt76_dma_queue_reset() - sets ctrl to DMA_DONE
	 * for all descriptors. Hardware expects unused descriptors to have
	 * DMA_DONE set to indicate "available/completed". Using memset(0)
	 * leaves DMA_DONE=0 which may cause hardware to think all are pending. */
	{
		int i;
		for (i = 0; i < RING_SIZE; i++) {
			dev->mcu_ring[i].buf0 = 0;
			dev->mcu_ring[i].ctrl = cpu_to_le32(MT_DMA_CTL_DMA_DONE);
			dev->mcu_ring[i].buf1 = 0;
			dev->mcu_ring[i].info = 0;
		}
	}
	dev->mcu_ring_head = 0;

	dev_info(&dev->pdev->dev, "  Ring 15 (MCU_WM) allocated at DMA 0x%pad\n",
		 &dev->mcu_ring_dma);

	/* Allocate Ring 16 (FWDL) for firmware data */
	dev->fwdl_ring = dma_alloc_coherent(&dev->pdev->dev,
					    RING_SIZE * sizeof(struct mt7927_desc),
					    &dev->fwdl_ring_dma, GFP_KERNEL);
	if (!dev->fwdl_ring) {
		dev_err(&dev->pdev->dev, "  Failed to allocate FWDL ring\n");
		return -ENOMEM;
	}
	/* Same for FWDL ring - set DMA_DONE on all descriptors */
	{
		int i;
		for (i = 0; i < RING_SIZE; i++) {
			dev->fwdl_ring[i].buf0 = 0;
			dev->fwdl_ring[i].ctrl = cpu_to_le32(MT_DMA_CTL_DMA_DONE);
			dev->fwdl_ring[i].buf1 = 0;
			dev->fwdl_ring[i].info = 0;
		}
	}
	dev->fwdl_ring_head = 0;

	dev_info(&dev->pdev->dev, "  Ring 16 (FWDL) allocated at DMA 0x%pad\n",
		 &dev->fwdl_ring_dma);

	/* Allocate DMA buffer for firmware chunks */
	dev->dma_buf = dma_alloc_coherent(&dev->pdev->dev, FW_CHUNK_SIZE,
					  &dev->dma_buf_phys, GFP_KERNEL);
	if (!dev->dma_buf) {
		dev_err(&dev->pdev->dev, "  Failed to allocate DMA buffer\n");
		return -ENOMEM;
	}

	/* DIAGNOSTIC: Verify dma_buf_phys is valid (Phase 27 debug) */
	dev_info(&dev->pdev->dev, "  DMA buffer allocated: virt=%px phys=0x%llx (lower=0x%08x upper=0x%08x)\n",
		 dev->dma_buf,
		 (unsigned long long)dev->dma_buf_phys,
		 lower_32_bits(dev->dma_buf_phys),
		 upper_32_bits(dev->dma_buf_phys));
	if (dev->dma_buf_phys == 0) {
		dev_err(&dev->pdev->dev, "  ERROR: dma_buf_phys is 0! This will cause IOMMU fault.\n");
		return -EINVAL;
	}

	/* =====================================================================
	 * CRITICAL: DMA Reset Sequence - ZOUYONGHAO TIMING FIX (Phase 27)
	 *
	 * Key insight from zouyonghao mt792x_dma.c analysis:
	 * 1. mt792x_dma_disable() - CLEAR GLO_CFG (no CLK_GAT_DIS yet!)
	 * 2. Ring configuration happens (BASE, CNT, CIDX, DIDX)
	 * 3. Prefetch configuration (EXT_CTRL)
	 * 4. mt792x_dma_enable() - NOW set CLK_GAT_DIS and enable DMA
	 *
	 * CRITICAL TIMING DIFFERENCE:
	 * - OLD (broken): Set CLK_GAT_DIS BEFORE ring configuration
	 * - NEW (correct): Set CLK_GAT_DIS AFTER ring configuration
	 *
	 * Zouyonghao's mt792x_dma_disable() clears TX/RX_DMA_EN and polls for
	 * DMA not busy, but does NOT set CLK_GAT_DIS until mt792x_dma_enable().
	 * ===================================================================== */
	dev_info(&dev->pdev->dev, "  Step 1: Clear GLO_CFG (disable DMA, NO clk_gate_dis yet!)...\n");
	val = mt_rr(dev, MT_WFDMA0_GLO_CFG);
	dev_info(&dev->pdev->dev, "    GLO_CFG before: 0x%08x\n", val);

	/* CLEAR GLO_CFG - disable DMA and clear config bits
	 * This matches zouyonghao's mt792x_dma_disable() which clears DMA_EN bits
	 * and polls for not busy. CLK_GAT_DIS is set LATER in mt792x_dma_enable().
	 *
	 * Per mt792x_dma_disable():
	 *   mt76_clear(dev, MT_WFDMA0_GLO_CFG, MT_WFDMA0_GLO_CFG_OMIT_TX_INFO |
	 *              MT_WFDMA0_GLO_CFG_OMIT_RX_INFO | ... | TX_DMA_EN | RX_DMA_EN);
	 */
	mt_wr(dev, MT_WFDMA0_GLO_CFG, 0);
	wmb();
	usleep_range(1000, 2000);
	dev_info(&dev->pdev->dev, "    GLO_CFG after clear: 0x%08x (expected 0x00000000)\n",
		 mt_rr(dev, MT_WFDMA0_GLO_CFG));

	dev_info(&dev->pdev->dev, "  Step 2: Disable DMASHDL...\n");
	val = mt_rr(dev, MT_WFDMA0_GLO_CFG_EXT0);
	dev_info(&dev->pdev->dev, "    GLO_CFG_EXT0 before: 0x%08x\n", val);
	val &= ~MT_WFDMA0_CSR_TX_DMASHDL_ENABLE;
	mt_wr(dev, MT_WFDMA0_GLO_CFG_EXT0, val);
	wmb();

	/* Step 3: Check DMA Reset state - DO NOT manipulate RST register!
	 * Analysis of test failure showed that clearing RST bits may prevent ring
	 * registers from accepting writes. MediaTek's asicConnac3xWfdmaControl()
	 * does NOT explicitly manipulate the RST register - it just configures GLO_CFG.
	 *
	 * The RST register value of 0x30 (bits 4,5 set) is the power-on default.
	 * Instead of clearing it, we'll leave it alone and see if rings work. */
	val = mt_rr(dev, MT_WFDMA0_RST);
	dev_info(&dev->pdev->dev, "  Step 3: DMA Reset state check (NOT modifying!)...\n");
	dev_info(&dev->pdev->dev, "    RST = 0x%08x (leaving unchanged)\n", val);

	/* Step 3b: Initialize ALL unused TX rings to valid DMA address
	 * Phase 27 fix: The DMA engine may scan all rings when enabled.
	 * Rings with BASE=0 cause IOMMU page fault at address 0.
	 * Point unused rings (0-14) to our mcu_ring_dma (valid memory).
	 * Set CNT=1 and CIDX=DIDX=0 so DMA thinks they're empty/done. */
	dev_info(&dev->pdev->dev, "  Step 3b: Initialize unused rings (0-14) to prevent BASE=0...\n");
	{
		int ring;
		for (ring = 0; ring <= 14; ring++) {
			/* Skip rings 15 and 16 - configured separately */
			if (ring == MCU_WM_RING_IDX || ring == FWDL_RING_IDX)
				continue;

			/* Point to valid DMA memory (reuse mcu_ring which has DMA_DONE set) */
			mt_wr(dev, MT_WFDMA0_TX_RING_BASE(ring), lower_32_bits(dev->mcu_ring_dma));
			/* CTRL1: upper bits + CNT=1 (minimal ring) */
			mt_wr(dev, MT_WFDMA0_TX_RING_BASE(ring) + 4,
			      (upper_32_bits(dev->mcu_ring_dma) & 0x000F0000) | 1);
			/* CIDX=DIDX=0 means ring is empty */
			mt_wr(dev, MT_WFDMA0_TX_RING_CIDX(ring), 0);
			mt_wr(dev, MT_WFDMA0_TX_RING_DIDX(ring), 0);
		}
		wmb();
	}
	dev_info(&dev->pdev->dev, "    Unused rings 0-14 now point to valid DMA addr 0x%pad\n",
		 &dev->mcu_ring_dma);

	/* Step 4: Configure Ring 15 (MCU_WM) - configure BEFORE EXT_CTRL
	 * MediaTek's halWpdmaAllocRing() order: CTRL0, CTRL1, CTRL2, then EXT_CTRL
	 * mt76 also writes DIDX (dma_idx) to 0 in mt76_dma_queue_reset() */
	dev_info(&dev->pdev->dev, "  Step 4: Configuring Ring %d (MCU_WM)...\n", MCU_WM_RING_IDX);
	mt_wr(dev, MT_WFDMA0_TX_RING_BASE(MCU_WM_RING_IDX), lower_32_bits(dev->mcu_ring_dma));
	wmb();
	/* CTRL1 contains upper address bits AND max count - write as combined value */
	val = (upper_32_bits(dev->mcu_ring_dma) & 0x000F0000) | RING_SIZE;
	mt_wr(dev, MT_WFDMA0_TX_RING_BASE(MCU_WM_RING_IDX) + 4, val);
	wmb();
	/* Write BOTH CIDX and DIDX to 0 (mt76 does this in queue_reset) */
	mt_wr(dev, MT_WFDMA0_TX_RING_CIDX(MCU_WM_RING_IDX), 0);
	mt_wr(dev, MT_WFDMA0_TX_RING_DIDX(MCU_WM_RING_IDX), 0);
	wmb();
	msleep(1);

	val = mt_rr(dev, MT_WFDMA0_TX_RING_BASE(MCU_WM_RING_IDX));
	dev_info(&dev->pdev->dev, "    Ring %d: BASE=0x%08x CTRL1=0x%08x CIDX=%d DIDX=%d\n",
		 MCU_WM_RING_IDX, val,
		 mt_rr(dev, MT_WFDMA0_TX_RING_BASE(MCU_WM_RING_IDX) + 4),
		 mt_rr(dev, MT_WFDMA0_TX_RING_CIDX(MCU_WM_RING_IDX)),
		 mt_rr(dev, MT_WFDMA0_TX_RING_DIDX(MCU_WM_RING_IDX)));

	/* Step 5: Configure Ring 16 (FWDL) */
	dev_info(&dev->pdev->dev, "  Step 5: Configuring Ring %d (FWDL)...\n", FWDL_RING_IDX);
	mt_wr(dev, MT_WFDMA0_TX_RING_BASE(FWDL_RING_IDX), lower_32_bits(dev->fwdl_ring_dma));
	wmb();
	val = (upper_32_bits(dev->fwdl_ring_dma) & 0x000F0000) | RING_SIZE;
	mt_wr(dev, MT_WFDMA0_TX_RING_BASE(FWDL_RING_IDX) + 4, val);
	wmb();
	/* Write BOTH CIDX and DIDX to 0 (mt76 does this in queue_reset) */
	mt_wr(dev, MT_WFDMA0_TX_RING_CIDX(FWDL_RING_IDX), 0);
	mt_wr(dev, MT_WFDMA0_TX_RING_DIDX(FWDL_RING_IDX), 0);
	wmb();
	msleep(1);

	val = mt_rr(dev, MT_WFDMA0_TX_RING_BASE(FWDL_RING_IDX));
	dev_info(&dev->pdev->dev, "    Ring %d: BASE=0x%08x CTRL1=0x%08x CIDX=%d DIDX=%d\n",
		 FWDL_RING_IDX, val,
		 mt_rr(dev, MT_WFDMA0_TX_RING_BASE(FWDL_RING_IDX) + 4),
		 mt_rr(dev, MT_WFDMA0_TX_RING_CIDX(FWDL_RING_IDX)),
		 mt_rr(dev, MT_WFDMA0_TX_RING_DIDX(FWDL_RING_IDX)));

	/* Step 6: Configure prefetch/EXT_CTRL AFTER ring setup
	 * From mt792x_dma.c: mt792x_dma_prefetch() */
	dev_info(&dev->pdev->dev, "  Step 6: Configuring prefetch (EXT_CTRL)...\n");
	mt_wr(dev, MT_WFDMA0_TX_RING15_EXT_CTRL, PREFETCH_RING15);
	mt_wr(dev, MT_WFDMA0_TX_RING16_EXT_CTRL, PREFETCH_RING16);
	wmb();
	msleep(1);
	dev_info(&dev->pdev->dev, "    Ring 15 EXT_CTRL: 0x%08x (expected 0x%08x)\n",
		 mt_rr(dev, MT_WFDMA0_TX_RING15_EXT_CTRL), PREFETCH_RING15);
	dev_info(&dev->pdev->dev, "    Ring 16 EXT_CTRL: 0x%08x (expected 0x%08x)\n",
		 mt_rr(dev, MT_WFDMA0_TX_RING16_EXT_CTRL), PREFETCH_RING16);

	/* Step 7: Reset ring pointers AFTER configuration
	 * CRITICAL: mt76 uses ~0 to reset ALL rings, not just specific ones.
	 * Previous code used BIT(15)|BIT(16) which only reset rings 15/16.
	 * Per mt792x_dma_enable(): mt76_wr(dev, MT_WFDMA0_RST_DTX_PTR, ~0); */
	dev_info(&dev->pdev->dev, "  Step 7: Resetting ALL ring DMA pointers (~0)...\n");
	mt_wr(dev, MT_WFDMA0_RST_DTX_PTR, ~0);
	wmb();

	/* Step 7a: Set delay interrupt to 0 (per mt792x_dma_enable, before GLO_CFG) */
	mt_wr(dev, MT_WFDMA0_PRI_DLY_INT_CFG0, 0);
	wmb();
	dev_info(&dev->pdev->dev, "    PRI_DLY_INT_CFG0 = 0\n");

	/* Step 7b: NOW set GLO_CFG with CLK_GAT_DIS (AFTER ring configuration!)
	 *
	 * THIS IS THE CRITICAL TIMING FIX!
	 * Zouyonghao's mt792x_dma_enable() sets GLO_CFG bits AFTER rings are configured.
	 * Our old code set CLK_GAT_DIS BEFORE ring config which caused ring registers
	 * to not accept writes.
	 *
	 * Per mt792x_dma_enable():
	 *   mt76_set(dev, MT_WFDMA0_GLO_CFG,
	 *            MT_WFDMA0_GLO_CFG_TX_WB_DDONE |
	 *            MT_WFDMA0_GLO_CFG_FIFO_LITTLE_ENDIAN |
	 *            MT_WFDMA0_GLO_CFG_CLK_GAT_DIS |  <-- SET HERE, AFTER RINGS!
	 *            MT_WFDMA0_GLO_CFG_OMIT_TX_INFO | ...);
	 */
	dev_info(&dev->pdev->dev, "  Step 7b: NOW set GLO_CFG with CLK_GAT_DIS (AFTER rings!)...\n");
	val = MT_WFDMA0_GLO_CFG_SETUP;  /* Includes CLK_GAT_DIS but NOT DMA_EN */
	mt_wr(dev, MT_WFDMA0_GLO_CFG, val);
	wmb();
	msleep(1);
	dev_info(&dev->pdev->dev, "    GLO_CFG after setup: 0x%08x (expected 0x%08x)\n",
		 mt_rr(dev, MT_WFDMA0_GLO_CFG), (u32)MT_WFDMA0_GLO_CFG_SETUP);

	/* Step 7c: Enable TX DMA ONLY
	 * Phase 27b fix: Do NOT enable RX_DMA_EN during firmware loading!
	 * RX rings have not been initialized (BASE=0), so enabling RX_DMA causes
	 * the DMA engine to scan RX ring descriptors at address 0x0, triggering
	 * AMD-Vi IO_PAGE_FAULT and halting the DMA engine (DIDX never increments).
	 *
	 * For firmware loading, only TX DMA is needed (rings 15 and 16).
	 * RX_DMA_EN should be enabled later after RX rings are configured.
	 *
	 * Expected value: 0x5030B871 (setup + TX_DMA_EN only)
	 */
	dev_info(&dev->pdev->dev, "  Step 7c: Enable DMA (TX_DMA_EN only - RX not configured!)...\n");
	val = MT_WFDMA0_GLO_CFG_SETUP |
	      MT_WFDMA0_GLO_CFG_TX_DMA_EN;
	/* NOTE: RX_DMA_EN intentionally NOT set - RX rings have BASE=0! */
	mt_wr(dev, MT_WFDMA0_GLO_CFG, val);
	wmb();

	dev_info(&dev->pdev->dev, "  DMA enabled, GLO_CFG=0x%08x (expected 0x%08x)\n",
		 mt_rr(dev, MT_WFDMA0_GLO_CFG),
		 (u32)(MT_WFDMA0_GLO_CFG_SETUP | MT_WFDMA0_GLO_CFG_TX_DMA_EN));

	/* Step 8: MT7927-specific DMA configuration (from mt792x_dma_enable)
	 * These are REQUIRED for MT7925/MT7927 per zouyonghao mt792x_dma.c:153-158
	 *
	 * if (is_mt7925(&dev->mt76) || is_mt7927(&dev->mt76)) {
	 *     mt76_rmw(dev, MT_UWFDMA0_GLO_CFG_EXT1, BIT(28), BIT(28));
	 *     mt76_set(dev, MT_WFDMA0_INT_RX_PRI, 0x0F00);
	 *     mt76_set(dev, MT_WFDMA0_INT_TX_PRI, 0x7F00);
	 * }
	 * mt76_set(dev, MT_WFDMA_DUMMY_CR, MT_WFDMA_NEED_REINIT);
	 */
	dev_info(&dev->pdev->dev, "  Step 8: MT7927-specific DMA configuration...\n");

	/* Set GLO_CFG_EXT1 BIT(28) - MT7927-specific enable bit */
	val = mt_rr(dev, MT_WFDMA0_GLO_CFG_EXT1);
	dev_info(&dev->pdev->dev, "    GLO_CFG_EXT1 before: 0x%08x\n", val);
	val |= MT_WFDMA0_GLO_CFG_EXT1_MT7927_EN;  /* BIT(28) */
	mt_wr(dev, MT_WFDMA0_GLO_CFG_EXT1, val);
	wmb();
	dev_info(&dev->pdev->dev, "    GLO_CFG_EXT1 after:  0x%08x (set BIT(28))\n",
		 mt_rr(dev, MT_WFDMA0_GLO_CFG_EXT1));

	/* Set DMA RX priority - 0x0F00 */
	val = mt_rr(dev, MT_WFDMA0_INT_RX_PRI);
	dev_info(&dev->pdev->dev, "    INT_RX_PRI before: 0x%08x\n", val);
	val |= 0x0F00;
	mt_wr(dev, MT_WFDMA0_INT_RX_PRI, val);
	wmb();
	dev_info(&dev->pdev->dev, "    INT_RX_PRI after:  0x%08x (set 0x0F00)\n",
		 mt_rr(dev, MT_WFDMA0_INT_RX_PRI));

	/* Set DMA TX priority - 0x7F00 */
	val = mt_rr(dev, MT_WFDMA0_INT_TX_PRI);
	dev_info(&dev->pdev->dev, "    INT_TX_PRI before: 0x%08x\n", val);
	val |= 0x7F00;
	mt_wr(dev, MT_WFDMA0_INT_TX_PRI, val);
	wmb();
	dev_info(&dev->pdev->dev, "    INT_TX_PRI after:  0x%08x (set 0x7F00)\n",
		 mt_rr(dev, MT_WFDMA0_INT_TX_PRI));

	/* Set WFDMA_DUMMY_CR reinit flag
	 * NOTE: MT_WFDMA_DUMMY_CR is at MCU address 0x54000120 in mt76.
	 * We use the WFDMA0 base offset (0x120) which may or may not work.
	 * If this doesn't work, it may need L1 remap like other MCU regs. */
	val = mt_rr(dev, MT_WFDMA_DUMMY_CR);
	dev_info(&dev->pdev->dev, "    WFDMA_DUMMY_CR before: 0x%08x\n", val);
	val |= MT_WFDMA_NEED_REINIT;  /* BIT(1) */
	mt_wr(dev, MT_WFDMA_DUMMY_CR, val);
	wmb();
	dev_info(&dev->pdev->dev, "    WFDMA_DUMMY_CR after:  0x%08x (set NEED_REINIT)\n",
		 mt_rr(dev, MT_WFDMA_DUMMY_CR));

	/* PRI_DLY_INT_CFG0 already set to 0 in Step 7a (before GLO_CFG setup) */

	/* DIAGNOSTIC: Scan ALL ring BASE registers to find any with address 0
	 * Phase 27 debug: DMA engine might scan uninitialized rings with BASE=0 */
	dev_info(&dev->pdev->dev, "  === DIAGNOSTIC: All TX Ring BASE registers ===\n");
	{
		int ring;
		u32 base_lo, base_hi;
		int zero_base_count = 0;

		for (ring = 0; ring <= 16; ring++) {
			base_lo = mt_rr(dev, MT_WFDMA0_TX_RING_BASE(ring));
			base_hi = mt_rr(dev, MT_WFDMA0_TX_RING_BASE(ring) + 4);

			/* Only print details for rings with interesting state */
			if (ring == MCU_WM_RING_IDX || ring == FWDL_RING_IDX ||
			    base_lo != 0 || (base_hi & 0x000F0000) != 0) {
				dev_info(&dev->pdev->dev, "    Ring %2d: BASE_LO=0x%08x CTRL1=0x%08x (CNT=%d)\n",
					 ring, base_lo, base_hi, base_hi & 0xFFF);
			}

			/* Count rings with BASE=0 (potential DMA fault sources) */
			if (base_lo == 0 && (base_hi & 0x000F0000) == 0)
				zero_base_count++;
		}

		if (zero_base_count > 0) {
			dev_warn(&dev->pdev->dev, "  WARNING: %d TX rings have BASE=0 (potential IOMMU fault source!)\n",
				 zero_base_count);
			dev_info(&dev->pdev->dev, "    Rings with BASE=0 might be scanned by DMA even if not used.\n");
		}
	}

	dev_info(&dev->pdev->dev, "  DMA setup complete!\n");
	return 0;
}

/* ============================================================
 * Phase 3.5: PCIE2AP Remap Configuration (AFTER DMA init!)
 * Reference: pci_mcu.c::mt7927e_mcu_init() line 148
 *
 * CRITICAL TIMING: Zouyonghao sets this in mt7927e_mcu_init() which is
 * called AFTER DMA queues are allocated (mt792x_dma_init).
 * This is different from our previous approach of setting it early.
 *
 * This maps PCIe address space to MCU's address space for communication.
 * ============================================================ */

static int configure_pcie2ap_remap(struct test_dev *dev)
{
	u32 val;

	dev_info(&dev->pdev->dev, "=== Phase 3.5: PCIE2AP Remap (AFTER DMA init!) ===\n");

	/* Set PCIE2AP remap for MCU communication
	 * From zouyonghao pci_mcu.c:148:
	 *   mt76_wr(dev, CONN_BUS_CR_VON_CONN_INFRA_PCIE2AP_REMAP_WF_1_BA_ADDR, 0x18051803);
	 *
	 * Register: 0x7C021034 → BAR0 0x0d1034
	 * Value: 0x18051803
	 */
	dev_info(&dev->pdev->dev, "  Setting PCIE2AP remap for MCU communication...\n");
	mt_wr(dev, CONN_BUS_CR_VON_PCIE2AP_REMAP_WF_1_BA, MT7927_PCIE2AP_REMAP_WF_1_BA_VALUE);
	val = mt_rr(dev, CONN_BUS_CR_VON_PCIE2AP_REMAP_WF_1_BA);
	dev_info(&dev->pdev->dev, "    PCIE2AP_REMAP_WF_1_BA = 0x%08x (expected 0x%08x) %s\n",
		 val, MT7927_PCIE2AP_REMAP_WF_1_BA_VALUE,
		 val == MT7927_PCIE2AP_REMAP_WF_1_BA_VALUE ? "OK" : "MISMATCH!");

	return 0;
}

/* ============================================================
 * Phase 4: Send Firmware Chunk via DMA (Polling Mode)
 * Reference: mt7927_fw_load.c::mt7927_mcu_send_firmware()
 *
 * KEY INSIGHT: No mailbox waits! Just send and poll for completion.
 * ============================================================ */

/*
 * Send firmware data chunk via Ring 16 (FWDL)
 * Per zouyonghao: FW_SCATTER data goes to Ring 16, raw data without TXD header
 *
 * Key pattern from zouyonghao mt7927_fw_load.c:
 * 1. Aggressive TX cleanup BEFORE sending (force=true)
 * 2. Send the chunk
 * 3. Aggressive TX cleanup AFTER sending (force=true)
 * 4. cond_resched() to yield CPU
 * 5. msleep(5) delay for ROM processing
 */
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

	/* Step 1: Aggressive cleanup BEFORE sending (per zouyonghao) */
	tx_cleanup(dev, FWDL_RING_IDX, dev->fwdl_ring_head, true);

	/* Copy data to DMA buffer */
	memcpy(dev->dma_buf, data, len);
	wmb();

	/* Setup descriptor on Ring 16 (FWDL) - same format as Ring 15 */
	idx = dev->fwdl_ring_head;
	desc = &dev->fwdl_ring[idx];

	/* OLD FORMAT FOR PAGE FAULT TEST */
	desc->buf0 = cpu_to_le32(lower_32_bits(dev->dma_buf_phys));
	desc->buf1 = cpu_to_le32(upper_32_bits(dev->dma_buf_phys));  /* OLD: upper bits here */
	ctrl = FIELD_PREP(MT_DMA_CTL_SD_LEN0, len) | MT_DMA_CTL_LAST_SEC0;
	desc->ctrl = cpu_to_le32(ctrl);
	desc->info = cpu_to_le32(0);  /* OLD: info was 0 */

	/* DIAGNOSTIC: Dump descriptor before DMA kick (first FWDL chunk only) */
	{
		static int fwdl_dump_count;
		if (fwdl_dump_count < 2) {
			u32 c = le32_to_cpu(desc->ctrl);
			dev_info(&dev->pdev->dev, "  [DIAG] Ring 16 desc[%d] before kick:\n", idx);
			dev_info(&dev->pdev->dev, "    buf0=0x%08x (SDPtr0 lower32)\n", le32_to_cpu(desc->buf0));
			dev_info(&dev->pdev->dev, "    ctrl=0x%08x (SDLen0=%d[bits16-29], LS0=%d[bit30], DONE=%d[bit31])\n",
				 c, (c >> 16) & 0x3FFF, (c >> 30) & 1, (c >> 31) & 1);
			dev_info(&dev->pdev->dev, "    buf1=0x%08x (SDPtr1 - should be 0 for single buffer)\n", le32_to_cpu(desc->buf1));
			dev_info(&dev->pdev->dev, "    info=0x%08x (SDPtr0Ext=%d, SDPtr1Ext=%d)\n",
				 le32_to_cpu(desc->info),
				 le32_to_cpu(desc->info) & 0xFFFF,
				 (le32_to_cpu(desc->info) >> 16) & 0xFFFF);
			dev_info(&dev->pdev->dev, "    dma_buf_phys=0x%llx\n", (unsigned long long)dev->dma_buf_phys);
			fwdl_dump_count++;
		}
	}
	wmb();

	/* Advance ring head and kick DMA on Ring 16 */
	dev->fwdl_ring_head = (idx + 1) % RING_SIZE;
	mt_wr(dev, MT_WFDMA0_TX_RING_CIDX(FWDL_RING_IDX), dev->fwdl_ring_head);
	wmb();

	/* Phase 27e: Trigger MCU interrupt - MCU ROM is in IDLE (0x1D1E) and
	 * doesn't actively poll DMA rings. We must ring the doorbell to wake it.
	 * BIT(0) = SW_INT_0, generates interrupt on MCU side. */
	mt_wr(dev, MT_HOST2MCU_SW_INT_SET, BIT(0));
	wmb();

	/* Poll for DMA completion (DIDX should advance) */
	timeout = 100;
	while (timeout-- > 0) {
		didx = mt_rr(dev, MT_WFDMA0_TX_RING_DIDX(FWDL_RING_IDX));
		if (didx == dev->fwdl_ring_head)
			break;
		usleep_range(100, 200);
	}

	if (timeout <= 0) {
		/* Phase 27d: Add diagnostic register reads on timeout */
		static int diag_count;
		u32 err_sta, mcu_int, pda_cfg, ring16_base;

		dev_warn(&dev->pdev->dev, "  Ring 16 DMA timeout (CIDX=%d, DIDX=%d)\n",
			 dev->fwdl_ring_head, didx);

		/* Only dump diagnostics for first 3 timeouts to avoid log spam */
		if (diag_count < 3) {
			err_sta = mt_rr(dev, MT_WFDMA0_WPDMA2HOST_ERR_INT_STA);
			mcu_int = mt_rr(dev, MT_WFDMA0_MCU_INT_STA);
			pda_cfg = mt_rr(dev, MT_PDA_CONFG);
			ring16_base = mt_rr(dev, MT_WFDMA0_TX_RING16_CTRL0);

			dev_info(&dev->pdev->dev, "  [DIAG] Phase 27d Error Investigation:\n");
			dev_info(&dev->pdev->dev, "    WPDMA2HOST_ERR_INT_STA(0xd41E8)=0x%08x (TX_TO=%d RX_TO=%d)\n",
				 err_sta,
				 !!(err_sta & MT_WFDMA0_ERR_TX_TIMEOUT_INT),
				 !!(err_sta & MT_WFDMA0_ERR_RX_TIMEOUT_INT));
			dev_info(&dev->pdev->dev, "    MCU_INT_STA(0xd4110)=0x%08x (MEM_ERR=%d DMA_ERR=%d)\n",
				 mcu_int,
				 !!(mcu_int & MT_WFDMA0_MCU_INT_MEM_RANGE_ERR),
				 !!(mcu_int & MT_WFDMA0_MCU_INT_DMA_ERR));
			dev_info(&dev->pdev->dev, "    PDA_CONFG(0x280C)=0x%08x (FWDL_EN=%d)\n",
				 pda_cfg, !!(pda_cfg & MT_PDA_FWDL_EN));
			dev_info(&dev->pdev->dev, "    Ring16 BASE(0xd4400)=0x%08x\n", ring16_base);
			diag_count++;
		}
		/* Per zouyonghao: Continue anyway, ROM may have processed it */
	}

	/* Step 3: Aggressive cleanup AFTER sending (per zouyonghao) */
	tx_cleanup(dev, FWDL_RING_IDX, dev->fwdl_ring_head, true);

	/* Step 4: Let other kernel threads run to free memory (per zouyonghao) */
	cond_resched();

	/* Step 5: Brief delay for ROM to process buffer (per zouyonghao) */
	msleep(5);

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

		/* CRITICAL: Inter-region cleanup delay from zouyonghao (10ms × 10 = 100ms)
		 * MT7927 ROM needs time to process each firmware region before the next.
		 * This is a key timing difference for MT7927 vs other devices. */
		if (i < trailer->n_region - 1) {  /* Not needed after last region */
			int j;
			dev_info(&dev->pdev->dev, "  Inter-region cleanup (100ms)...\n");
			for (j = 0; j < 10; j++) {
				tx_cleanup(dev, FWDL_RING_IDX, dev->fwdl_ring_head, false);
				msleep(10);
			}
		}
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
	mcu_status = mt_rr(dev, MT_MCU_STATUS);
	dev_info(&dev->pdev->dev, "  MCU status before: 0x%08x\n", mcu_status);

	/* Load patch */
	if (dev->fw_patch && dev->fw_patch->size > 0) {
		dev_info(&dev->pdev->dev, "\n--- Loading PATCH (%zu bytes) ---\n",
			 dev->fw_patch->size);
		ret = load_patch(dev);
		if (ret)
			dev_warn(&dev->pdev->dev, "  Patch load returned: %d\n", ret);

		mcu_status = mt_rr(dev, MT_MCU_STATUS);
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
		u32 ap2wf = mt_rr(dev, MT_WFSYS_SW_RST_B);
		mt_wr(dev, MT_WFSYS_SW_RST_B, ap2wf | MT_WFSYS_SW_INIT_DONE);
		dev_info(&dev->pdev->dev, "  AP2WF: 0x%08x -> 0x%08x\n",
			 ap2wf, mt_rr(dev, MT_WFSYS_SW_RST_B));
	}

	/* Check final MCU status */
	msleep(100);
	mcu_status = mt_rr(dev, MT_MCU_STATUS);
	dev_info(&dev->pdev->dev, "  MCU status after load: 0x%08x\n", mcu_status);

	{
		u32 mcu_ready = mt_rr(dev, MT_CONN_ON_MISC);
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

	/* Phase 0a: CB_INFRA PCIe remap - CRITICAL, must run FIRST!
	 * This enables proper PCIe to WFDMA address translation.
	 * Reference: reference_gen4m/chips/mt6639/mt6639.c:2727-2737 */
	ret = init_cbinfra_remap(dev);
	if (ret) {
		dev_err(&pdev->dev, "CB_INFRA remap failed: %d\n", ret);
		/* Continue anyway - may work without it */
	}

	/* Phase 0b: Power control handshake - CRITICAL, must run BEFORE reset!
	 * Reference: zouyonghao pci.c:565-571
	 * This two-step handshake (fw_pmctrl then drv_pmctrl) enables the
	 * WiFi subsystem to be properly reset and WFDMA registers to be writable. */
	ret = power_control_handshake(dev);
	if (ret) {
		dev_warn(&pdev->dev, "Power control handshake issue: %d\n", ret);
	}

	/* Phase 0c: MT7927 WiFi/BT subsystem reset - MUST run AFTER power control!
	 * This resets the WiFi subsystem and enables WFDMA ring register writes. */
	ret = wfsys_reset(dev);
	if (ret) {
		dev_err(&pdev->dev, "WFSYS reset failed: %d\n", ret);
		/* Continue anyway */
	}

	/* Phase 1: Initialize CONN_INFRA and wait for MCU IDLE */
	ret = init_conninfra(dev);
	if (ret) {
		dev_err(&pdev->dev, "CONN_INFRA init failed: %d\n", ret);
		/* Continue anyway - firmware loading might still work */
	}

	/* Phase 2: Verify driver ownership (should already have it from Phase 0b) */
	ret = claim_host_ownership(dev);
	if (ret) {
		dev_warn(&pdev->dev, "Host ownership claim issue: %d\n", ret);
	}

	/* Phase 2a: PCIe MAC Interrupt Routing - CRITICAL for DMA completion!
	 * Reference: zouyonghao pci.c:608-612
	 * Set AFTER power control/ownership, BEFORE DMA init.
	 * Without this, DMA completion signals may not reach the host. */
	ret = configure_pcie_mac_int(dev);
	if (ret) {
		dev_warn(&pdev->dev, "PCIe MAC int config issue: %d\n", ret);
	}

	/* Phase 2.5: WFDMA extension configuration (MSI, GLO_CFG_EXT, etc.)
	 * NOTE: PCIE2AP remap moved to Phase 3.5 (after DMA init) per zouyonghao order */
	ret = configure_wfdma_extensions(dev);
	if (ret) {
		dev_warn(&pdev->dev, "WFDMA extension config issue: %d\n", ret);
	}

	/* Phase 3: Setup DMA ring */
	ret = setup_dma_ring(dev);
	if (ret) {
		dev_err(&pdev->dev, "DMA ring setup failed: %d\n", ret);
		goto err_release_ram;
	}

	/* Phase 3.5: PCIE2AP remap - CRITICAL, must be AFTER DMA init!
	 * Reference: zouyonghao pci_mcu.c::mt7927e_mcu_init() line 148
	 * This is a timing fix - zouyonghao sets this AFTER DMA queues are allocated. */
	ret = configure_pcie2ap_remap(dev);
	if (ret) {
		dev_warn(&pdev->dev, "PCIE2AP remap issue: %d\n", ret);
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
	{
		u32 glo_cfg, int_sta, err_sta, mcu_int, pda_cfg;
		u32 ring15_base, ring16_base, ring16_cnt, ring16_ext;

		glo_cfg = mt_rr(dev, MT_WFDMA0_GLO_CFG);
		int_sta = mt_rr(dev, MT_WFDMA0_HOST_INT_STA);
		err_sta = mt_rr(dev, MT_WFDMA0_WPDMA2HOST_ERR_INT_STA);
		mcu_int = mt_rr(dev, MT_WFDMA0_MCU_INT_STA);
		pda_cfg = mt_rr(dev, MT_PDA_CONFG);
		ring15_base = mt_rr(dev, MT_WFDMA0_TX_RING_BASE(MCU_WM_RING_IDX));
		ring16_base = mt_rr(dev, MT_WFDMA0_TX_RING16_CTRL0);
		ring16_cnt = mt_rr(dev, MT_WFDMA0_TX_RING_CNT(FWDL_RING_IDX));
		ring16_ext = mt_rr(dev, MT_WFDMA0_TX_RING16_EXT_CTRL);

		dev_info(&pdev->dev, "  WFDMA GLO_CFG: 0x%08x\n", glo_cfg);
		dev_info(&pdev->dev, "  WFDMA INT_STA: 0x%08x (tx15=%d tx16=%d)\n",
			 int_sta,
			 !!(int_sta & BIT(25)),  /* tx_done_int_sts_15 */
			 !!(int_sta & BIT(26))); /* tx_done_int_sts_16 */
		dev_info(&pdev->dev, "  Ring %d (MCU_WM) CIDX/DIDX: %d/%d, BASE=0x%08x\n",
			 MCU_WM_RING_IDX,
			 mt_rr(dev, MT_WFDMA0_TX_RING_CIDX(MCU_WM_RING_IDX)),
			 mt_rr(dev, MT_WFDMA0_TX_RING_DIDX(MCU_WM_RING_IDX)),
			 ring15_base);
		dev_info(&pdev->dev, "  Ring %d (FWDL) CIDX/DIDX: %d/%d, BASE=0x%08x\n",
			 FWDL_RING_IDX,
			 mt_rr(dev, MT_WFDMA0_TX_RING_CIDX(FWDL_RING_IDX)),
			 mt_rr(dev, MT_WFDMA0_TX_RING_DIDX(FWDL_RING_IDX)),
			 ring16_base);

		/* Phase 27d: Diagnostic registers */
		dev_info(&pdev->dev, "\n");
		dev_info(&pdev->dev, "  [Phase 27d Diagnostics]\n");
		dev_info(&pdev->dev, "    WPDMA2HOST_ERR_INT_STA(0xd41E8): 0x%08x\n", err_sta);
		dev_info(&pdev->dev, "      TX_TIMEOUT=%d RX_TIMEOUT=%d TX_DMA_ERR=%d RX_DMA_ERR=%d\n",
			 !!(err_sta & MT_WFDMA0_ERR_TX_TIMEOUT_INT),
			 !!(err_sta & MT_WFDMA0_ERR_RX_TIMEOUT_INT),
			 !!(err_sta & MT_WFDMA0_ERR_TX_DMA_ERR_INT),
			 !!(err_sta & MT_WFDMA0_ERR_RX_DMA_ERR_INT));
		dev_info(&pdev->dev, "    MCU_INT_STA(0xd4110): 0x%08x (MEM_ERR=%d DMA_ERR=%d)\n",
			 mcu_int,
			 !!(mcu_int & MT_WFDMA0_MCU_INT_MEM_RANGE_ERR),
			 !!(mcu_int & MT_WFDMA0_MCU_INT_DMA_ERR));
		dev_info(&pdev->dev, "    PDA_CONFG(0x280C): 0x%08x (FWDL_EN=%d LS_QSEL=%d)\n",
			 pda_cfg, !!(pda_cfg & MT_PDA_FWDL_EN),
			 !!(pda_cfg & MT_PDA_FWDL_LS_QSEL_EN));

		/* Enhanced PDA diagnostics - why isn't the MCU receiving data? */
		{
			u32 pda_tar_addr, pda_tar_len, pda_dwld_state, mcu_glo_cfg;
			pda_tar_addr = mt_rr(dev, MT_PDA_TAR_ADDR);
			pda_tar_len = mt_rr(dev, MT_PDA_TAR_LEN);
			pda_dwld_state = mt_rr(dev, MT_PDA_DWLD_STATE);
			mcu_glo_cfg = mt_rr(dev, MT_MCU_DMA0_GLO_CFG);
			dev_info(&pdev->dev, "    PDA_TAR_ADDR(0x2800): 0x%08x\n", pda_tar_addr);
			dev_info(&pdev->dev, "    PDA_TAR_LEN(0x2804): 0x%08x\n", pda_tar_len);
			dev_info(&pdev->dev, "    PDA_DWLD_STATE(0x2808): 0x%08x\n", pda_dwld_state);
			dev_info(&pdev->dev, "      PDA_FINISH=%d PDA_BUSY=%d WFDMA_FINISH=%d WFDMA_BUSY=%d\n",
				 !!(pda_dwld_state & MT_PDA_FWDL_FINISH),
				 !!(pda_dwld_state & MT_PDA_FWDL_BUSY),
				 !!(pda_dwld_state & MT_WFDMA_FWDL_FINISH),
				 !!(pda_dwld_state & MT_WFDMA_FWDL_BUSY));
			dev_info(&pdev->dev, "      WFDMA_OVERFLOW=%d PDA_OVERFLOW=%d\n",
				 !!(pda_dwld_state & MT_WFDMA_FWDL_OVERFLOW),
				 !!(pda_dwld_state & MT_PDA_FWDL_OVERFLOW));
			dev_info(&pdev->dev, "    MCU_DMA0_GLO_CFG(0x2208): 0x%08x (RX_DMA_EN=%d)\n",
				 mcu_glo_cfg, !!(mcu_glo_cfg & MT_MCU_DMA0_GLO_CFG_RX_DMA_EN));
		}

		/* Ring comparison - BOTH rings have DIDX=0, this is a global DMA path issue */
		{
			u32 ring15_cnt, ring15_ext, ring15_cidx, ring15_didx;
			ring15_cnt = mt_rr(dev, MT_WFDMA0_TX_RING_CNT(MCU_WM_RING_IDX));
			ring15_ext = mt_rr(dev, MT_WFDMA0_TX_RING15_EXT_CTRL);
			ring15_cidx = mt_rr(dev, MT_WFDMA0_TX_RING_CIDX(MCU_WM_RING_IDX));
			ring15_didx = mt_rr(dev, MT_WFDMA0_TX_RING_DIDX(MCU_WM_RING_IDX));
			dev_info(&pdev->dev, "    Ring 15 (MCU_WM): CNT=%d, CIDX=%d, DIDX=%d, EXT_CTRL=0x%08x\n",
				 ring15_cnt, ring15_cidx, ring15_didx, ring15_ext);
			dev_info(&pdev->dev, "    Ring 16 (FWDL):   CNT=%d, CIDX=%d, DIDX=%d, EXT_CTRL=0x%08x\n",
				 ring16_cnt,
				 mt_rr(dev, MT_WFDMA0_TX_RING_CIDX(FWDL_RING_IDX)),
				 mt_rr(dev, MT_WFDMA0_TX_RING_DIDX(FWDL_RING_IDX)),
				 ring16_ext);
		}
	}
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

	if (dev->mcu_ring)
		dma_free_coherent(&pdev->dev,
				  RING_SIZE * sizeof(struct mt7927_desc),
				  dev->mcu_ring, dev->mcu_ring_dma);

	if (dev->fwdl_ring)
		dma_free_coherent(&pdev->dev,
				  RING_SIZE * sizeof(struct mt7927_desc),
				  dev->fwdl_ring, dev->fwdl_ring_dma);

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
