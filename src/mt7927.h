/* SPDX-License-Identifier: GPL-2.0 */
/*
 * MT7927 WiFi 7 Linux Driver
 * Main header file with device structures and function declarations
 *
 * Copyright (C) 2024 MT7927 Linux Driver Project
 */

#ifndef __MT7927_H
#define __MT7927_H

#include <linux/types.h>
#include <linux/pci.h>
#include <linux/firmware.h>
#include <linux/skbuff.h>
#include <linux/interrupt.h>
#include <linux/dma-mapping.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>

#include "mt7927_regs.h"

/* ============================================
 * Device Identification
 * ============================================ */

#define MT7927_VENDOR_ID                0x14c3
#define MT7927_DEVICE_ID                0x7927

#define MT7927_FIRMWARE_WM              "mediatek/mt7925/WIFI_RAM_CODE_MT7925_1_1.bin"
#define MT7927_ROM_PATCH                "mediatek/mt7925/WIFI_MT7925_PATCH_MCU_1_1_hdr.bin"

/* ============================================
 * DMA Descriptor
 * ============================================ */

struct mt7927_desc {
    __le32 buf0;        /* Buffer pointer (low 32 bits) */
    __le32 ctrl;        /* Control: length, last segment, DMA done */
    __le32 buf1;        /* Buffer pointer (high 32 bits, for 64-bit DMA) */
    __le32 info;        /* Additional info */
} __packed __aligned(4);

/* ============================================
 * DMA Queue Structure
 * ============================================ */

struct mt7927_queue {
    /* Descriptor ring */
    struct mt7927_desc *desc;
    dma_addr_t desc_dma;
    int ndesc;

    /* Buffer management */
    struct sk_buff **skb;
    dma_addr_t *dma_addr;

    /* Ring indices */
    int head;               /* CPU write index */
    int tail;               /* DMA read index (from hardware) */

    /* Queue identification */
    int hw_idx;             /* Hardware queue index */
    bool stopped;

    /* Spinlock for queue access */
    spinlock_t lock;
};

/* ============================================
 * IRQ Map Structure
 * ============================================ */

struct mt7927_irq_map {
    u32 host_irq_enable;
    struct {
        u32 all_complete_mask;
        u32 mcu_complete_mask;
    } tx;
    struct {
        u32 data_complete_mask;
        u32 wm_complete_mask;
        u32 wm2_complete_mask;
    } rx;
};

/* ============================================
 * MCU State
 * ============================================ */

enum mt7927_mcu_state {
    MT7927_MCU_STATE_INIT = 0,
    MT7927_MCU_STATE_FW_LOADED,
    MT7927_MCU_STATE_RUNNING,
    MT7927_MCU_STATE_ERROR,
};

/* ============================================
 * Device Structure
 * ============================================ */

struct mt7927_dev {
    struct pci_dev *pdev;
    struct device *dev;

    /* Memory mapped I/O */
    void __iomem *regs;         /* BAR2: 32KB read-only shadow (not used for writes) */
    void __iomem *mem;          /* BAR0: 2MB main register space (use this for all access) */

    /* Register remapping backup */
    u32 backup_l1;
    u32 backup_l2;

    /* DMA queues */
    struct mt7927_queue tx_q[4];        /* TX queues */
    struct mt7927_queue rx_q[4];        /* RX queues */
    struct mt7927_queue *q_mcu[__MT_MCUQ_MAX];  /* MCU queue pointers */

    /* Firmware */
    const struct firmware *fw_ram;
    const struct firmware *fw_patch;

    /* MCU communication */
    struct {
        struct sk_buff_head res_q;      /* Response queue */
        wait_queue_head_t wait;         /* Wait queue for responses */
        u32 timeout;                    /* MCU timeout in jiffies */
        u8 seq;                         /* Sequence number */
        enum mt7927_mcu_state state;
    } mcu;

    /* IRQ handling */
    struct tasklet_struct irq_tasklet;
    const struct mt7927_irq_map *irq_map;
    int irq;

    /* Hardware info */
    u32 chip_id;
    u32 hw_rev;
    u32 fw_ver;

    /* State flags */
    unsigned long state;
    bool hw_init_done;
    bool fw_assert;

    /* Work structures */
    struct work_struct reset_work;
    struct work_struct init_work;

    /* Spinlock for device access */
    spinlock_t lock;
    struct mutex mutex;
};

/* State flags */
#define MT7927_STATE_INITIALIZED        BIT(0)
#define MT7927_STATE_MCU_RUNNING        BIT(1)
#define MT7927_STATE_RESET              BIT(2)

/* ============================================
 * Register Access Functions
 * ============================================ */

/*
 * IMPORTANT MT7927 Register Layout Discovery:
 *
 * BAR0 (2MB): Main register space - use for all register access
 *   - 0x00000-0x01FFF: Reserved/unused
 *   - 0x02000-0x02FFF: WFDMA0 registers (REAL writable DMA control)
 *   - 0x03000+: Other subsystems per fixed_map
 *   - 0x10000+: Shadow of BAR2 (read-only status mirrors)
 *   - 0xE0000+: CONN_INFRA (power control, LPCTL)
 *   - 0xF0000+: CONN_INFRA (WFSYS reset)
 *
 * BAR2 (32KB): Window into BAR0+0x10000 - READ-ONLY shadow registers
 *   - Do NOT use for control writes - they won't take effect!
 *
 * Always use BAR0 (mem) for register access.
 */

/**
 * mt7927_rr_raw - Raw register read (no remapping)
 * Always uses BAR0 (mem) which contains the full 2MB register space
 */
static inline u32 mt7927_rr_raw(struct mt7927_dev *dev, u32 offset)
{
    return readl(dev->mem + offset);
}

/**
 * mt7927_wr_raw - Raw register write (no remapping)
 * Always uses BAR0 (mem) which contains the full 2MB register space
 */
static inline void mt7927_wr_raw(struct mt7927_dev *dev, u32 offset, u32 val)
{
    writel(val, dev->mem + offset);
}

/**
 * mt7927_reg_map_l1 - Remap register through L1 window
 */
static inline u32 mt7927_reg_map_l1(struct mt7927_dev *dev, u32 addr)
{
    u32 offset = FIELD_GET(MT_HIF_REMAP_L1_OFFSET, addr);
    u32 base = FIELD_GET(MT_HIF_REMAP_L1_BASE, addr);

    dev->backup_l1 = mt7927_rr_raw(dev, MT_HIF_REMAP_L1);

    mt7927_wr_raw(dev, MT_HIF_REMAP_L1,
                  (mt7927_rr_raw(dev, MT_HIF_REMAP_L1) & ~MT_HIF_REMAP_L1_MASK) |
                  FIELD_PREP(MT_HIF_REMAP_L1_MASK, base));

    /* Read to push write */
    mt7927_rr_raw(dev, MT_HIF_REMAP_L1);

    return MT_HIF_REMAP_BASE_L1 + offset;
}

/**
 * mt7927_reg_map_l2 - Remap register through L2 window
 */
static inline u32 mt7927_reg_map_l2(struct mt7927_dev *dev, u32 addr)
{
    u32 base = FIELD_GET(MT_HIF_REMAP_L1_BASE, MT_HIF_REMAP_BASE_L2);

    dev->backup_l2 = mt7927_rr_raw(dev, MT_HIF_REMAP_L1);

    mt7927_wr_raw(dev, MT_HIF_REMAP_L1,
                  (mt7927_rr_raw(dev, MT_HIF_REMAP_L1) & ~MT_HIF_REMAP_L1_MASK) |
                  FIELD_PREP(MT_HIF_REMAP_L1_MASK, base));

    mt7927_wr_raw(dev, MT_HIF_REMAP_L2, addr);

    /* Read to push write */
    mt7927_rr_raw(dev, MT_HIF_REMAP_L1);

    return MT_HIF_REMAP_BASE_L1;
}

/**
 * mt7927_reg_remap_restore - Restore register remapping to original state
 */
static inline void mt7927_reg_remap_restore(struct mt7927_dev *dev)
{
    if (dev->backup_l1) {
        mt7927_wr_raw(dev, MT_HIF_REMAP_L1, dev->backup_l1);
        dev->backup_l1 = 0;
    }

    if (dev->backup_l2) {
        mt7927_wr_raw(dev, MT_HIF_REMAP_L2, dev->backup_l2);
        dev->backup_l2 = 0;
    }
}

/**
 * mt7927_reg_addr - Translate logical address to BAR offset
 */
static inline u32 mt7927_reg_addr(struct mt7927_dev *dev, u32 addr)
{
    int i;

    /* Direct access for low addresses */
    if (addr < 0x200000)
        return addr;

    mt7927_reg_remap_restore(dev);

    /* Check fixed mapping table */
    for (i = 0; mt7927_fixed_map[i].size != 0; i++) {
        u32 ofs;

        if (addr < mt7927_fixed_map[i].phys)
            continue;

        ofs = addr - mt7927_fixed_map[i].phys;
        if (ofs >= mt7927_fixed_map[i].size)
            continue;

        return mt7927_fixed_map[i].maps + ofs;
    }

    /* Use L1 or L2 remapping for addresses not in fixed map */
    if ((addr >= 0x18000000 && addr < 0x18c00000) ||
        (addr >= 0x70000000 && addr < 0x78000000) ||
        (addr >= 0x7c000000 && addr < 0x7c400000))
        return mt7927_reg_map_l1(dev, addr);

    return mt7927_reg_map_l2(dev, addr);
}

/**
 * mt7927_rr - Read register with address translation
 * Always uses BAR0 (mem) after translation
 */
static inline u32 mt7927_rr(struct mt7927_dev *dev, u32 offset)
{
    u32 addr = mt7927_reg_addr(dev, offset);
    return readl(dev->mem + addr);
}

/**
 * mt7927_wr - Write register with address translation
 * Always uses BAR0 (mem) after translation
 */
static inline void mt7927_wr(struct mt7927_dev *dev, u32 offset, u32 val)
{
    u32 addr = mt7927_reg_addr(dev, offset);
    writel(val, dev->mem + addr);
}

/**
 * mt7927_rmw - Read-modify-write register
 */
static inline u32 mt7927_rmw(struct mt7927_dev *dev, u32 offset, u32 mask, u32 val)
{
    u32 cur = mt7927_rr(dev, offset);
    mt7927_wr(dev, offset, (cur & ~mask) | val);
    return cur;
}

/**
 * mt7927_set - Set bits in register
 */
static inline void mt7927_set(struct mt7927_dev *dev, u32 offset, u32 val)
{
    mt7927_rmw(dev, offset, 0, val);
}

/**
 * mt7927_clear - Clear bits in register
 */
static inline void mt7927_clear(struct mt7927_dev *dev, u32 offset, u32 val)
{
    mt7927_rmw(dev, offset, val, 0);
}

/**
 * mt7927_rmw_field - Modify a field in a register
 */
#define mt7927_rmw_field(dev, offset, field, val)                   \
    mt7927_rmw(dev, offset, field,                                  \
               FIELD_PREP(field, val))

/**
 * mt7927_poll - Poll register until condition or timeout
 * @dev: device structure
 * @offset: register offset
 * @mask: bits to check
 * @val: expected value (after masking)
 * @timeout_us: timeout in microseconds
 * 
 * Returns: true if condition met, false on timeout
 */
static inline bool mt7927_poll(struct mt7927_dev *dev, u32 offset,
                               u32 mask, u32 val, int timeout_us)
{
    u32 cur;
    int i;

    for (i = 0; i < timeout_us; i += 10) {
        cur = mt7927_rr(dev, offset);
        if ((cur & mask) == val)
            return true;
        udelay(10);
    }

    return false;
}

/* ============================================
 * Function Declarations
 * ============================================ */

/* Power management (mt7927_pci.c) */
int mt7927_mcu_fw_pmctrl(struct mt7927_dev *dev);
int mt7927_mcu_drv_pmctrl(struct mt7927_dev *dev);

/* WiFi system reset (mt7927_pci.c) */
int mt7927_wfsys_reset(struct mt7927_dev *dev);
int mt7927_wpdma_reset(struct mt7927_dev *dev, bool force);

/* DMA (mt7927_dma.c) */
int mt7927_dma_init(struct mt7927_dev *dev);
void mt7927_dma_cleanup(struct mt7927_dev *dev);
int mt7927_dma_enable(struct mt7927_dev *dev);
int mt7927_dma_disable(struct mt7927_dev *dev, bool force);

int mt7927_queue_alloc(struct mt7927_dev *dev, struct mt7927_queue *q,
                       int idx, int ndesc, int buf_size, u32 ring_base);
void mt7927_queue_free(struct mt7927_dev *dev, struct mt7927_queue *q);

int mt7927_tx_queue_skb(struct mt7927_dev *dev, struct mt7927_queue *q,
                        struct sk_buff *skb);
void mt7927_tx_complete(struct mt7927_dev *dev, struct mt7927_queue *q);
int mt7927_rx_poll(struct mt7927_dev *dev, struct mt7927_queue *q, int budget);

/* MCU (mt7927_mcu.c) */
int mt7927_mcu_init(struct mt7927_dev *dev);
void mt7927_mcu_exit(struct mt7927_dev *dev);
int mt7927_mcu_send_msg(struct mt7927_dev *dev, int cmd,
                        const void *data, int len, bool wait_resp);
int mt7927_mcu_send_and_get_msg(struct mt7927_dev *dev, int cmd,
                                const void *data, int len,
                                bool wait_resp, struct sk_buff **ret_skb);

/* Firmware loading (mt7927_mcu.c) */
int mt7927_load_firmware(struct mt7927_dev *dev);
int mt7927_load_patch(struct mt7927_dev *dev);
int mt7927_load_ram(struct mt7927_dev *dev);

/* IRQ handling (mt7927_pci.c) */
irqreturn_t mt7927_irq_handler(int irq, void *dev_instance);
void mt7927_irq_tasklet(unsigned long data);
void mt7927_irq_enable(struct mt7927_dev *dev, u32 mask);
void mt7927_irq_disable(struct mt7927_dev *dev, u32 mask);

/* Device registration (mt7927_pci.c) */
int mt7927_register_device(struct mt7927_dev *dev);
void mt7927_unregister_device(struct mt7927_dev *dev);

#endif /* __MT7927_H */
