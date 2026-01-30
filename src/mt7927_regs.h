/* SPDX-License-Identifier: GPL-2.0 */
/*
 * MT7927 Register Definitions
 * Adapted from mt7925 driver (mt76/mt7925/regs.h and mt792x_regs.h)
 * 
 * Copyright (C) 2024 MT7927 Linux Driver Project
 */

#ifndef __MT7927_REGS_H
#define __MT7927_REGS_H

#include <linux/types.h>
#include <linux/bitfield.h>

/* ============================================
 * WFDMA Base Offsets
 * ============================================ */

/*
 * IMPORTANT: Real WFDMA registers are at BAR0 + 0x2000, NOT BAR2!
 * 
 * Discovery from diagnostic testing:
 * - BAR0 + 0x2000 + offset = Real writable WFDMA registers
 * - BAR2 (= BAR0 + 0x10000) = Read-only shadow/status registers
 * 
 * The mt7925 fixed_map confirms: { 0x54000000, 0x002000, 0x0001000 }
 */
#define MT_WFDMA0_BASE                  0x2000
#define MT_WFDMA0(ofs)                  (MT_WFDMA0_BASE + (ofs))

/* ============================================
 * WFDMA Global Configuration
 * ============================================ */

#define MT_WFDMA0_GLO_CFG               MT_WFDMA0(0x208)
#define MT_WFDMA0_GLO_CFG_TX_DMA_EN     BIT(0)
#define MT_WFDMA0_GLO_CFG_TX_DMA_BUSY   BIT(1)
#define MT_WFDMA0_GLO_CFG_RX_DMA_EN     BIT(2)
#define MT_WFDMA0_GLO_CFG_RX_DMA_BUSY   BIT(3)
#define MT_WFDMA0_GLO_CFG_TX_WB_DDONE   BIT(6)
#define MT_WFDMA0_GLO_CFG_FIFO_LITTLE_ENDIAN BIT(12)
#define MT_WFDMA0_GLO_CFG_CSR_DISP_BASE_PTR_CHAIN_EN BIT(15)
#define MT_WFDMA0_GLO_CFG_OMIT_RX_INFO_PFET2 BIT(21)
#define MT_WFDMA0_GLO_CFG_OMIT_RX_INFO  BIT(27)
#define MT_WFDMA0_GLO_CFG_OMIT_TX_INFO  BIT(28)
#define MT_WFDMA0_GLO_CFG_EXT_EN        BIT(26)

#define MT_WFDMA0_RST_DTX_PTR           MT_WFDMA0(0x20c)
#define MT_WFDMA0_RST_DRX_PTR           MT_WFDMA0(0x280)

/* ============================================
 * Interrupt Registers
 * ============================================ */

#define MT_WFDMA0_HOST_INT_STA          MT_WFDMA0(0x200)
#define MT_WFDMA0_HOST_INT_ENA          MT_WFDMA0(0x204)
#define MT_WFDMA0_HOST_INT_DIS          MT_WFDMA0(0x22c)

/* TX Done Interrupts */
#define HOST_TX_DONE_INT_ENA0           BIT(0)   /* Band0 Data */
#define HOST_TX_DONE_INT_ENA1           BIT(1)
#define HOST_TX_DONE_INT_ENA2           BIT(2)
#define HOST_TX_DONE_INT_ENA3           BIT(3)
#define HOST_TX_DONE_INT_ENA4           BIT(4)   /* MT7927: FWDL */
#define HOST_TX_DONE_INT_ENA5           BIT(5)   /* MT7927: MCU WM */
#define HOST_TX_DONE_INT_ENA6           BIT(6)
#define HOST_TX_DONE_INT_ENA7           BIT(7)
#define HOST_TX_DONE_INT_ENA15          BIT(25)  /* Not used on MT7927 */
#define HOST_TX_DONE_INT_ENA16          BIT(26)  /* Not used on MT7927 */
#define HOST_TX_DONE_INT_ENA17          BIT(27)  /* Not used on MT7927 */

/* RX Done Interrupts */
#define HOST_RX_DONE_INT_ENA0           BIT(16)  /* MCU WM */
#define HOST_RX_DONE_INT_ENA1           BIT(17)  /* MCU WM2 */
#define HOST_RX_DONE_INT_ENA2           BIT(18)  /* Band0 Data */
#define HOST_RX_DONE_INT_ENA3           BIT(19)  /* Band1 Data */
#define HOST_RX_DONE_INT_ENA4           BIT(12)
#define HOST_RX_DONE_INT_ENA5           BIT(13)

/* Combined interrupt masks */
#define MT_INT_RX_DONE_DATA             HOST_RX_DONE_INT_ENA2
#define MT_INT_RX_DONE_WM               HOST_RX_DONE_INT_ENA0
#define MT_INT_RX_DONE_WM2              HOST_RX_DONE_INT_ENA1
#define MT_INT_RX_DONE_ALL              (MT_INT_RX_DONE_DATA | \
                                         MT_INT_RX_DONE_WM | \
                                         MT_INT_RX_DONE_WM2)

/* MT7927 uses rings 4/5 for MCU, not 15/16/17 like MT7925 */
#define MT_INT_TX_DONE_MCU_WM           HOST_TX_DONE_INT_ENA5
#define MT_INT_TX_DONE_FWDL             HOST_TX_DONE_INT_ENA4
#define MT_INT_TX_DONE_BAND0            HOST_TX_DONE_INT_ENA0
#define MT_INT_TX_DONE_MCU              (MT_INT_TX_DONE_MCU_WM | \
                                         MT_INT_TX_DONE_FWDL)
#define MT_INT_TX_DONE_ALL              (MT_INT_TX_DONE_MCU | \
                                         MT_INT_TX_DONE_BAND0 | \
                                         GENMASK(18, 4))

/* MCU command interrupt */
#define MT_INT_MCU_CMD                  BIT(29)

/* ============================================
 * TX Ring Registers
 * ============================================ */

#define MT_TX_RING_BASE                 MT_WFDMA0(0x300)

/* TX Ring 0 - Band0 Data */
#define MT_WFDMA0_TX_RING0_BASE         MT_WFDMA0(0x300)
#define MT_WFDMA0_TX_RING0_CNT          MT_WFDMA0(0x304)
#define MT_WFDMA0_TX_RING0_CIDX         MT_WFDMA0(0x308)
#define MT_WFDMA0_TX_RING0_DIDX         MT_WFDMA0(0x30c)

/* TX Ring extension control for 36-bit DMA and prefetch */
#define MT_WFDMA0_TX_RING0_EXT_CTRL     MT_WFDMA0(0x600)
#define MT_WFDMA0_TX_RING15_EXT_CTRL    MT_WFDMA0(0x600 + 15 * 0x04)
#define MT_WFDMA0_TX_RING16_EXT_CTRL    MT_WFDMA0(0x600 + 16 * 0x04)

/* Generic TX ring access macros */
#define MT_WFDMA0_TX_RING_BASE(n)       MT_WFDMA0(0x300 + (n) * 0x10)
#define MT_WFDMA0_TX_RING_CNT(n)        MT_WFDMA0(0x304 + (n) * 0x10)
#define MT_WFDMA0_TX_RING_CIDX(n)       MT_WFDMA0(0x308 + (n) * 0x10)
#define MT_WFDMA0_TX_RING_DIDX(n)       MT_WFDMA0(0x30c + (n) * 0x10)
#define MT_WFDMA0_TX_RING_EXT_CTRL(n)   MT_WFDMA0(0x600 + (n) * 0x04)

/* ============================================
 * RX Ring Registers
 * ============================================ */

#define MT_RX_EVENT_RING_BASE           MT_WFDMA0(0x500)
#define MT_RX_DATA_RING_BASE            MT_WFDMA0(0x500)

/* Generic RX ring access macros */
#define MT_WFDMA0_RX_RING_BASE(n)       MT_WFDMA0(0x500 + (n) * 0x10)
#define MT_WFDMA0_RX_RING_CNT(n)        MT_WFDMA0(0x504 + (n) * 0x10)
#define MT_WFDMA0_RX_RING_CIDX(n)       MT_WFDMA0(0x508 + (n) * 0x10)
#define MT_WFDMA0_RX_RING_DIDX(n)       MT_WFDMA0(0x50c + (n) * 0x10)
#define MT_WFDMA0_RX_RING_EXT_CTRL(n)   MT_WFDMA0(0x680 + (n) * 0x04)

/* DMA reset and configuration */
#define MT_WFDMA0_RST                   MT_WFDMA0(0x100)
#define MT_WFDMA0_RST_LOGIC_RST         BIT(4)
#define MT_WFDMA0_RST_DMASHDL_ALL_RST   BIT(5)

#define MT_WFDMA0_PRI_DLY_INT_CFG0      MT_WFDMA0(0x2f0)

/* Additional GLO_CFG bits */
#define MT_WFDMA0_GLO_CFG_CLK_GAT_DIS   BIT(5)
#define MT_WFDMA0_GLO_CFG_RX_WB_DDONE   BIT(7)
#define MT_WFDMA0_GLO_CFG_FIFO_DIS_CHECK BIT(18)
#define MT_WFDMA0_GLO_CFG_DMA_SIZE      GENMASK(17, 16)

/* ============================================
 * MCU Registers
 * ============================================ */

#define MT_MCU2HOST_SW_INT_ENA          MT_WFDMA0(0x1f4)
#define MT_MCU_CMD_WAKE_RX_PCIE         BIT(0)

/* ============================================
 * Power Management Registers
 * ============================================ */

/*
 * LPCTL (Low Power Control) register for power management handshake.
 * 
 * From mt792x driver analysis:
 * - SET_OWN (BIT 0): Write to give ownership TO firmware
 * - CLR_OWN (BIT 1): Write to claim ownership FOR driver
 * - OWN_SYNC (BIT 2): Read to check current owner (1 = FW owns, 0 = driver owns)
 */
#define MT_CONN_ON_LPCTL                0x7c060010
#define PCIE_LPCR_HOST_SET_OWN          BIT(0)  /* Write: give to firmware */
#define PCIE_LPCR_HOST_CLR_OWN          BIT(1)  /* Write: claim for driver */
#define PCIE_LPCR_HOST_OWN_SYNC         BIT(2)  /* Read: 1=FW owns, 0=driver owns */

/* Legacy names for compatibility */
#define MT_CONN_ON_LPCTL_HOST_OWN       PCIE_LPCR_HOST_SET_OWN
#define MT_CONN_ON_LPCTL_FW_OWN         PCIE_LPCR_HOST_CLR_OWN

/*
 * PCIe MAC registers - use logical address for proper translation
 * Fixed_map: { 0x74030000, 0x010000, 0x0001000 } - PCIe MAC at BAR0+0x10000
 */
#define MT_PCIE_MAC_BASE                0x74030000
#define MT_PCIE_MAC(ofs)                (MT_PCIE_MAC_BASE + (ofs))

#define MT_PCIE_MAC_INT_ENABLE          MT_PCIE_MAC(0x188)
#define MT_PCIE_MAC_PM                  MT_PCIE_MAC(0x194)
#define MT_PCIE_MAC_PM_L0S_DIS          BIT(8)

/* ============================================
 * Hardware Control Registers
 * ============================================ */

#define MT_HW_CHIPID                    0x0000
#define MT_HW_REV                       0x0004

#define MT_HW_EMI_CTL                   0x0110
#define MT_HW_EMI_CTL_SLPPROT_EN        BIT(0)

/* WiFi System Reset */
#define MT_WFSYS_SW_RST_B               0x7c000140
#define MT_WFSYS_SW_RST_B_EN            BIT(0)
#define MT_WFSYS_SW_INIT_DONE           BIT(4)

/* ============================================
 * Register Remapping
 * ============================================ */

#define MT_HIF_REMAP_L1                 0x155024
#define MT_HIF_REMAP_L1_MASK            GENMASK(31, 16)
#define MT_HIF_REMAP_L1_OFFSET          GENMASK(15, 0)
#define MT_HIF_REMAP_L1_BASE            GENMASK(31, 16)
#define MT_HIF_REMAP_BASE_L1            0x130000

#define MT_HIF_REMAP_L2                 0x0120
#define MT_HIF_REMAP_BASE_L2            0x18500000

/* ============================================
 * Firmware Status Registers
 * ============================================ */

#define MT_CONN_ON_MISC                 0x7c0600f0
#define MT_TOP_MISC2_FW_N9_RDY          GENMASK(1, 0)
#define MT_TOP_MISC2_FW_N9_RDY_VAL      0x1

/* Scratch registers for communication */
#define MT_SWDEF_BASE                   0x00401400
#define MT_SWDEF_MODE                   (MT_SWDEF_BASE + 0x3c)
#define MT_SWDEF_MODE_MT7927_MASK       GENMASK(15, 0)
#define MT_SWDEF_NORMAL_MODE            0

/* ============================================
 * DMA Descriptor Definitions
 * ============================================ */

/* TX/RX descriptor size */
#define MT_TXD_SIZE                     32
#define MT_RXD_SIZE                     32

/* DMA descriptor control bits */
#define MT_DMA_CTL_SD_LEN0              GENMASK(15, 0)
#define MT_DMA_CTL_SD_LEN1              GENMASK(29, 16)
#define MT_DMA_CTL_LAST_SEC0            BIT(30)
#define MT_DMA_CTL_LAST_SEC1            BIT(31)
#define MT_DMA_CTL_DMA_DONE             BIT(31)
#define MT_DMA_CTL_TO_HOST              BIT(8)
#define MT_DMA_CTL_TO_HOST_V2           BIT(31)
#define MT_DMA_PPE_CPU_REASON           GENMASK(15, 11)
#define MT_DMA_PPE_ENTRY                GENMASK(30, 16)

/* DMA info field */
#define MT_DMA_INFO_DMA_FRAG            BIT(9)

/* ============================================
 * Queue IDs
 * ============================================ */

/* TX Queue IDs
 * 
 * IMPORTANT: MT7927 only has 8 TX rings (0-7), not 17 like MT7925!
 * We must use available rings for MCU communication.
 */
enum mt7927_txq_id {
    MT7927_TXQ_BAND0 = 0,      /* Data TX */
    MT7927_TXQ_BAND1 = 1,      /* Data TX (if needed) */
    MT7927_TXQ_FWDL = 4,       /* Firmware download - use ring 4 */
    MT7927_TXQ_MCU_WM = 5,     /* MCU commands - use ring 5 */
};

/* RX Queue IDs */
enum mt7927_rxq_id {
    MT7927_RXQ_MCU_WM = 0,
    MT7927_RXQ_MCU_WM2 = 1,
    MT7927_RXQ_BAND0 = 2,
    MT7927_RXQ_BAND1 = 3,
};

/* MCU Queue IDs */
enum mt7927_mcu_queue_id {
    MT_MCUQ_WM = 0,
    MT_MCUQ_WA,
    MT_MCUQ_FWDL,
    __MT_MCUQ_MAX,
};

/* ============================================
 * Ring Sizes
 * ============================================ */

#define MT7927_TX_RING_SIZE             2048
#define MT7927_TX_MCU_RING_SIZE         256
#define MT7927_TX_FWDL_RING_SIZE        128

#define MT7927_RX_RING_SIZE             1536
#define MT7927_RX_MCU_RING_SIZE         512

#define MT_RX_BUF_SIZE                  2048
#define MT_TX_TOKEN_SIZE                8192

/* ============================================
 * Address Mapping Table Entry
 * ============================================ */

struct mt7927_reg_map {
    u32 phys;       /* Physical/logical address */
    u32 maps;       /* Mapped BAR offset */
    u32 size;       /* Region size */
};

/* Fixed register mapping table from mt7925 */
static const struct mt7927_reg_map mt7927_fixed_map[] = {
    { 0x830c0000, 0x000000, 0x0001000 }, /* WF_MCU_BUS_CR_REMAP */
    { 0x54000000, 0x002000, 0x0001000 }, /* WFDMA PCIE0 MCU DMA0 */
    { 0x55000000, 0x003000, 0x0001000 }, /* WFDMA PCIE0 MCU DMA1 */
    { 0x56000000, 0x004000, 0x0001000 }, /* WFDMA reserved */
    { 0x57000000, 0x005000, 0x0001000 }, /* WFDMA MCU wrap CR */
    { 0x58000000, 0x006000, 0x0001000 }, /* WFDMA PCIE1 MCU DMA0 */
    { 0x59000000, 0x007000, 0x0001000 }, /* WFDMA PCIE1 MCU DMA1 */
    { 0x820c0000, 0x008000, 0x0004000 }, /* WF_UMAC_TOP (PLE) */
    { 0x820c8000, 0x00c000, 0x0002000 }, /* WF_UMAC_TOP (PSE) */
    { 0x820cc000, 0x00e000, 0x0002000 }, /* WF_UMAC_TOP (PP) */
    { 0x74030000, 0x010000, 0x0001000 }, /* PCIe MAC */
    { 0x820e0000, 0x020000, 0x0000400 }, /* WF_LMAC_TOP BN0 (WF_CFG) */
    { 0x820e1000, 0x020400, 0x0000200 }, /* WF_LMAC_TOP BN0 (WF_TRB) */
    { 0x820e2000, 0x020800, 0x0000400 }, /* WF_LMAC_TOP BN0 (WF_AGG) */
    { 0x820e3000, 0x020c00, 0x0000400 }, /* WF_LMAC_TOP BN0 (WF_ARB) */
    { 0x820e4000, 0x021000, 0x0000400 }, /* WF_LMAC_TOP BN0 (WF_TMAC) */
    { 0x820e5000, 0x021400, 0x0000800 }, /* WF_LMAC_TOP BN0 (WF_RMAC) */
    { 0x820ce000, 0x021c00, 0x0000200 }, /* WF_LMAC_TOP (WF_SEC) */
    { 0x820e7000, 0x021e00, 0x0000200 }, /* WF_LMAC_TOP BN0 (WF_DMA) */
    { 0x820cf000, 0x022000, 0x0001000 }, /* WF_LMAC_TOP (WF_PF) */
    { 0x820e9000, 0x023400, 0x0000200 }, /* WF_LMAC_TOP BN0 (WF_WTBLOFF) */
    { 0x820ea000, 0x024000, 0x0000200 }, /* WF_LMAC_TOP BN0 (WF_ETBF) */
    { 0x820eb000, 0x024200, 0x0000400 }, /* WF_LMAC_TOP BN0 (WF_LPON) */
    { 0x820ec000, 0x024600, 0x0000200 }, /* WF_LMAC_TOP BN0 (WF_INT) */
    { 0x820ed000, 0x024800, 0x0000800 }, /* WF_LMAC_TOP BN0 (WF_MIB) */
    { 0x820ca000, 0x026000, 0x0002000 }, /* WF_LMAC_TOP BN0 (WF_MUCOP) */
    { 0x820d0000, 0x030000, 0x0010000 }, /* WF_LMAC_TOP (WF_WTBLON) */
    { 0x40000000, 0x070000, 0x0010000 }, /* WF_UMAC_SYSRAM */
    { 0x00400000, 0x080000, 0x0010000 }, /* WF_MCU_SYSRAM */
    { 0x00410000, 0x090000, 0x0010000 }, /* WF_MCU_SYSRAM (configure) */
    { 0x820f0000, 0x0a0000, 0x0000400 }, /* WF_LMAC_TOP BN1 (WF_CFG) */
    { 0x820f1000, 0x0a0600, 0x0000200 }, /* WF_LMAC_TOP BN1 (WF_TRB) */
    { 0x820f2000, 0x0a0800, 0x0000400 }, /* WF_LMAC_TOP BN1 (WF_AGG) */
    { 0x820f3000, 0x0a0c00, 0x0000400 }, /* WF_LMAC_TOP BN1 (WF_ARB) */
    { 0x820f4000, 0x0a1000, 0x0000400 }, /* WF_LMAC_TOP BN1 (WF_TMAC) */
    { 0x820f5000, 0x0a1400, 0x0000800 }, /* WF_LMAC_TOP BN1 (WF_RMAC) */
    { 0x820f7000, 0x0a1e00, 0x0000200 }, /* WF_LMAC_TOP BN1 (WF_DMA) */
    { 0x820f9000, 0x0a3400, 0x0000200 }, /* WF_LMAC_TOP BN1 (WF_WTBLOFF) */
    { 0x820fa000, 0x0a4000, 0x0000200 }, /* WF_LMAC_TOP BN1 (WF_ETBF) */
    { 0x820fb000, 0x0a4200, 0x0000400 }, /* WF_LMAC_TOP BN1 (WF_LPON) */
    { 0x820fc000, 0x0a4600, 0x0000200 }, /* WF_LMAC_TOP BN1 (WF_INT) */
    { 0x820fd000, 0x0a4800, 0x0000800 }, /* WF_LMAC_TOP BN1 (WF_MIB) */
    { 0x820c4000, 0x0a8000, 0x0004000 }, /* WF_LMAC_TOP BN1 (WF_MUCOP) */
    { 0x820b0000, 0x0ae000, 0x0001000 }, /* [APB2] WFSYS_ON */
    { 0x80020000, 0x0b0000, 0x0010000 }, /* WF_TOP_MISC_OFF */
    { 0x81020000, 0x0c0000, 0x0010000 }, /* WF_TOP_MISC_ON */
    { 0x7c020000, 0x0d0000, 0x0010000 }, /* CONN_INFRA, wfdma */
    { 0x7c060000, 0x0e0000, 0x0010000 }, /* CONN_INFRA, conn_host_csr */
    { 0x7c000000, 0x0f0000, 0x0010000 }, /* CONN_INFRA */
    { 0x70020000, 0x1f0000, 0x0010000 }, /* Reserved for CBTOP */
    { 0x7c500000, 0x060000, 0x2000000 }, /* remap */
    { 0x0, 0x0, 0x0 } /* End marker */
};

#endif /* __MT7927_REGS_H */
