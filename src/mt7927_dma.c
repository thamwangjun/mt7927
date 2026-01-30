// SPDX-License-Identifier: GPL-2.0
/*
 * MT7927 WiFi 7 Linux Driver - DMA Implementation
 *
 * Handles DMA queue allocation, TX/RX ring management, and data transfer
 *
 * Copyright (C) 2024 MT7927 Linux Driver Project
 */

#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/dma-mapping.h>
#include <linux/delay.h>

#include "mt7927.h"

/* ============================================
 * DMA Queue Allocation
 * ============================================ */

/**
 * mt7927_queue_alloc - Allocate a DMA queue
 * @dev: device structure
 * @q: queue structure to initialize
 * @idx: hardware queue index
 * @ndesc: number of descriptors
 * @buf_size: buffer size for RX queues (0 for TX queues)
 * @ring_base: base register for this ring
 */
int mt7927_queue_alloc(struct mt7927_dev *dev, struct mt7927_queue *q,
                       int idx, int ndesc, int buf_size, u32 ring_base)
{
    int i, size;

    spin_lock_init(&q->lock);
    q->hw_idx = idx;
    q->ndesc = ndesc;
    q->head = 0;
    q->tail = 0;
    q->stopped = false;

    /* Allocate descriptor ring */
    size = ndesc * sizeof(struct mt7927_desc);
    q->desc = dma_alloc_coherent(dev->dev, size, &q->desc_dma, GFP_KERNEL);
    if (!q->desc) {
        dev_err(dev->dev, "Failed to allocate descriptor ring for queue %d\n", idx);
        return -ENOMEM;
    }
    memset(q->desc, 0, size);

    /* Allocate SKB pointer array */
    q->skb = kcalloc(ndesc, sizeof(struct sk_buff *), GFP_KERNEL);
    if (!q->skb) {
        dev_err(dev->dev, "Failed to allocate SKB array for queue %d\n", idx);
        goto err_free_desc;
    }

    /* Allocate DMA address array */
    q->dma_addr = kcalloc(ndesc, sizeof(dma_addr_t), GFP_KERNEL);
    if (!q->dma_addr) {
        dev_err(dev->dev, "Failed to allocate DMA addr array for queue %d\n", idx);
        goto err_free_skb;
    }

    /* For RX queues, pre-allocate buffers */
    if (buf_size > 0) {
        for (i = 0; i < ndesc; i++) {
            struct sk_buff *skb;
            dma_addr_t dma_addr;

            skb = dev_alloc_skb(buf_size);
            if (!skb) {
                dev_err(dev->dev, "Failed to allocate RX buffer %d\n", i);
                goto err_free_buffers;
            }

            dma_addr = dma_map_single(dev->dev, skb->data, buf_size,
                                      DMA_FROM_DEVICE);
            if (dma_mapping_error(dev->dev, dma_addr)) {
                dev_kfree_skb(skb);
                dev_err(dev->dev, "Failed to map RX buffer %d\n", i);
                goto err_free_buffers;
            }

            q->skb[i] = skb;
            q->dma_addr[i] = dma_addr;

            /* Set up descriptor */
            q->desc[i].buf0 = cpu_to_le32(lower_32_bits(dma_addr));
            q->desc[i].buf1 = cpu_to_le32(upper_32_bits(dma_addr));
            q->desc[i].ctrl = cpu_to_le32(buf_size);
        }
    }

    /*
     * Configure hardware ring registers
     * Ring register layout (from mt76_queue_regs):
     *   offset 0x00: desc_base (32-bit DMA address, low bits)
     *   offset 0x04: ring_size (descriptor count)
     *   offset 0x08: cpu_idx
     *   offset 0x0c: dma_idx
     *
     * For 64-bit DMA, high bits are written to EXT_CTRL registers.
     */
    dev_info(dev->dev, "Queue %d: writing ring_base=0x%x, dma=0x%llx, ndesc=%d\n",
             idx, ring_base, (u64)q->desc_dma, ndesc);

    mt7927_wr(dev, ring_base + 0x00, lower_32_bits(q->desc_dma));  /* Base (low 32 bits) */
    mt7927_wr(dev, ring_base + 0x04, ndesc);                        /* Ring size (count) */
    mt7927_wr(dev, ring_base + 0x08, 0);                            /* CPU index */
    mt7927_wr(dev, ring_base + 0x0c, 0);                            /* DMA index */
    wmb();  /* Ensure writes are visible to hardware */

    /* Verify the write succeeded */
    {
        u32 readback = mt7927_rr(dev, ring_base);
        if (readback != lower_32_bits(q->desc_dma)) {
            dev_warn(dev->dev, "Queue %d: ring base write failed! wrote=0x%x, read=0x%x\n",
                     idx, lower_32_bits(q->desc_dma), readback);
        }
    }

    dev_dbg(dev->dev, "Queue %d allocated: %d descriptors at 0x%llx\n",
            idx, ndesc, (u64)q->desc_dma);

    return 0;

err_free_buffers:
    for (i = 0; i < ndesc; i++) {
        if (q->skb[i]) {
            if (q->dma_addr[i])
                dma_unmap_single(dev->dev, q->dma_addr[i], buf_size,
                                DMA_FROM_DEVICE);
            dev_kfree_skb(q->skb[i]);
        }
    }
    kfree(q->dma_addr);
err_free_skb:
    kfree(q->skb);
err_free_desc:
    dma_free_coherent(dev->dev, size, q->desc, q->desc_dma);
    return -ENOMEM;
}

/**
 * mt7927_queue_free - Free a DMA queue
 */
void mt7927_queue_free(struct mt7927_dev *dev, struct mt7927_queue *q)
{
    int i;

    if (!q->desc)
        return;

    /* Free any remaining SKBs and DMA mappings */
    for (i = 0; i < q->ndesc; i++) {
        if (q->skb && q->skb[i]) {
            if (q->dma_addr && q->dma_addr[i])
                dma_unmap_single(dev->dev, q->dma_addr[i],
                                MT_RX_BUF_SIZE, DMA_FROM_DEVICE);
            dev_kfree_skb(q->skb[i]);
        }
    }

    kfree(q->dma_addr);
    kfree(q->skb);

    dma_free_coherent(dev->dev, q->ndesc * sizeof(struct mt7927_desc),
                      q->desc, q->desc_dma);

    memset(q, 0, sizeof(*q));
}

/* ============================================
 * TX Queue Operations
 * ============================================ */

/**
 * mt7927_tx_queue_skb - Queue an SKB for transmission
 */
int mt7927_tx_queue_skb(struct mt7927_dev *dev, struct mt7927_queue *q,
                        struct sk_buff *skb)
{
    struct mt7927_desc *desc;
    dma_addr_t dma_addr;
    unsigned long flags;
    int idx;

    spin_lock_irqsave(&q->lock, flags);

    /* Check if queue is full */
    idx = q->head;
    if (((idx + 1) % q->ndesc) == q->tail) {
        spin_unlock_irqrestore(&q->lock, flags);
        return -ENOSPC;
    }

    /* Map the SKB data */
    dma_addr = dma_map_single(dev->dev, skb->data, skb->len, DMA_TO_DEVICE);
    if (dma_mapping_error(dev->dev, dma_addr)) {
        spin_unlock_irqrestore(&q->lock, flags);
        return -ENOMEM;
    }

    /* Store SKB and DMA address */
    q->skb[idx] = skb;
    q->dma_addr[idx] = dma_addr;

    /* Set up descriptor */
    desc = &q->desc[idx];
    desc->buf0 = cpu_to_le32(lower_32_bits(dma_addr));
    desc->buf1 = cpu_to_le32(upper_32_bits(dma_addr));
    desc->ctrl = cpu_to_le32(skb->len | MT_DMA_CTL_LAST_SEC0);
    desc->info = 0;
    wmb();  /* Ensure descriptor is written before updating index */

    /* Update head index */
    q->head = (idx + 1) % q->ndesc;

    /* Kick the hardware */
    mt7927_wr(dev, MT_WFDMA0_TX_RING_CIDX(q->hw_idx), q->head);

    /* Debug: Check if DMA picks up the command */
    {
        u32 cidx = mt7927_rr(dev, MT_WFDMA0_TX_RING_CIDX(q->hw_idx));
        u32 didx = mt7927_rr(dev, MT_WFDMA0_TX_RING_DIDX(q->hw_idx));
        u32 base = mt7927_rr(dev, MT_WFDMA0_TX_RING_BASE(q->hw_idx));
        u32 cnt = mt7927_rr(dev, MT_WFDMA0_TX_RING_CNT(q->hw_idx));
        dev_info(dev->dev, "TX Q%d: CIDX=%d DIDX=%d BASE=0x%08x CNT=%d\n",
                 q->hw_idx, cidx, didx, base, cnt);
    }

    spin_unlock_irqrestore(&q->lock, flags);

    return 0;
}

/**
 * mt7927_tx_complete - Process completed TX descriptors
 */
void mt7927_tx_complete(struct mt7927_dev *dev, struct mt7927_queue *q)
{
    struct mt7927_desc *desc;
    unsigned long flags;
    int idx;

    spin_lock_irqsave(&q->lock, flags);

    while (q->tail != q->head) {
        idx = q->tail;
        desc = &q->desc[idx];

        /* Check if DMA has completed this descriptor */
        if (!(le32_to_cpu(desc->ctrl) & MT_DMA_CTL_DMA_DONE))
            break;

        /* Unmap and free the SKB */
        if (q->skb[idx]) {
            dma_unmap_single(dev->dev, q->dma_addr[idx],
                            q->skb[idx]->len, DMA_TO_DEVICE);
            dev_kfree_skb_irq(q->skb[idx]);
            q->skb[idx] = NULL;
            q->dma_addr[idx] = 0;
        }

        /* Clear descriptor */
        desc->ctrl = 0;

        /* Move tail forward */
        q->tail = (idx + 1) % q->ndesc;
    }

    /* Wake queue if it was stopped */
    if (q->stopped) {
        q->stopped = false;
        /* TODO: Signal that queue is available */
    }

    spin_unlock_irqrestore(&q->lock, flags);
}

/* ============================================
 * RX Queue Operations
 * ============================================ */

/**
 * mt7927_rx_poll - Poll RX queue for received packets
 */
int mt7927_rx_poll(struct mt7927_dev *dev, struct mt7927_queue *q, int budget)
{
    struct mt7927_desc *desc;
    struct sk_buff *skb, *new_skb;
    dma_addr_t dma_addr;
    unsigned long flags;
    int idx, len, count = 0;

    spin_lock_irqsave(&q->lock, flags);

    while (count < budget) {
        idx = q->tail;
        desc = &q->desc[idx];

        /* Check if DMA has completed this descriptor */
        if (!(le32_to_cpu(desc->ctrl) & MT_DMA_CTL_DMA_DONE))
            break;

        /* Get received length */
        len = FIELD_GET(MT_DMA_CTL_SD_LEN0, le32_to_cpu(desc->ctrl));

        /* Get the received SKB */
        skb = q->skb[idx];
        if (!skb)
            goto next;

        /* Unmap the buffer */
        dma_unmap_single(dev->dev, q->dma_addr[idx], MT_RX_BUF_SIZE,
                        DMA_FROM_DEVICE);

        /* Allocate new buffer */
        new_skb = dev_alloc_skb(MT_RX_BUF_SIZE);
        if (!new_skb) {
            /* Reuse old buffer */
            dma_addr = dma_map_single(dev->dev, skb->data, MT_RX_BUF_SIZE,
                                      DMA_FROM_DEVICE);
            if (!dma_mapping_error(dev->dev, dma_addr)) {
                q->dma_addr[idx] = dma_addr;
                desc->buf0 = cpu_to_le32(lower_32_bits(dma_addr));
                desc->buf1 = cpu_to_le32(upper_32_bits(dma_addr));
            }
            goto next;
        }

        /* Map new buffer */
        dma_addr = dma_map_single(dev->dev, new_skb->data, MT_RX_BUF_SIZE,
                                  DMA_FROM_DEVICE);
        if (dma_mapping_error(dev->dev, dma_addr)) {
            dev_kfree_skb(new_skb);
            /* Reuse old buffer */
            dma_addr = dma_map_single(dev->dev, skb->data, MT_RX_BUF_SIZE,
                                      DMA_FROM_DEVICE);
            if (!dma_mapping_error(dev->dev, dma_addr)) {
                q->dma_addr[idx] = dma_addr;
                desc->buf0 = cpu_to_le32(lower_32_bits(dma_addr));
                desc->buf1 = cpu_to_le32(upper_32_bits(dma_addr));
            }
            goto next;
        }

        /* Set received data length */
        skb_put(skb, len);

        /* Store new buffer in queue */
        q->skb[idx] = new_skb;
        q->dma_addr[idx] = dma_addr;
        desc->buf0 = cpu_to_le32(lower_32_bits(dma_addr));
        desc->buf1 = cpu_to_le32(upper_32_bits(dma_addr));

        /* Process the received SKB */
        if (q->hw_idx == MT7927_RXQ_MCU_WM) {
            /* MCU response - add to response queue */
            skb_queue_tail(&dev->mcu.res_q, skb);
            wake_up(&dev->mcu.wait);
        } else {
            /* Data packet - would go to mac80211 */
            dev_kfree_skb(skb);  /* For now, just free */
        }

        count++;

next:
        /* Clear DMA done flag and reset length */
        desc->ctrl = cpu_to_le32(MT_RX_BUF_SIZE);
        wmb();

        /* Update tail and notify hardware */
        q->tail = (idx + 1) % q->ndesc;
        mt7927_wr(dev, MT_WFDMA0_RX_RING_CIDX(q->hw_idx), q->tail);
    }

    spin_unlock_irqrestore(&q->lock, flags);

    return count;
}

/* ============================================
 * DMA Prefetch Configuration
 * ============================================ */

#define PREFETCH(base, depth)   ((base) << 16 | (depth))

/**
 * mt7927_dma_prefetch - Configure DMA prefetch settings
 * This must be called before enabling DMA.
 */
static void mt7927_dma_prefetch(struct mt7927_dev *dev)
{
    /* RX ring prefetch (based on mt7925 settings) */
    mt7927_wr(dev, MT_WFDMA0_RX_RING_EXT_CTRL(0), PREFETCH(0x0000, 0x4));
    mt7927_wr(dev, MT_WFDMA0_RX_RING_EXT_CTRL(1), PREFETCH(0x0040, 0x4));
    mt7927_wr(dev, MT_WFDMA0_RX_RING_EXT_CTRL(2), PREFETCH(0x0080, 0x4));
    mt7927_wr(dev, MT_WFDMA0_RX_RING_EXT_CTRL(3), PREFETCH(0x00c0, 0x4));

    /* TX ring prefetch */
    mt7927_wr(dev, MT_WFDMA0_TX_RING_EXT_CTRL(0), PREFETCH(0x0100, 0x10));
    mt7927_wr(dev, MT_WFDMA0_TX_RING_EXT_CTRL(1), PREFETCH(0x0200, 0x10));
    mt7927_wr(dev, MT_WFDMA0_TX_RING_EXT_CTRL(2), PREFETCH(0x0300, 0x10));
    mt7927_wr(dev, MT_WFDMA0_TX_RING_EXT_CTRL(3), PREFETCH(0x0400, 0x10));

    /* MT7927 uses rings 15/16 to match MT7925 (shared firmware)
     * Fallback: If rings 15/16 don't work, change to rings 4/5:
     *   mt7927_wr(dev, MT_WFDMA0_TX_RING_EXT_CTRL(4), PREFETCH(0x0500, 0x4));
     *   mt7927_wr(dev, MT_WFDMA0_TX_RING_EXT_CTRL(5), PREFETCH(0x0540, 0x4));
     */
    mt7927_wr(dev, MT_WFDMA0_TX_RING_EXT_CTRL(15), PREFETCH(0x0500, 0x4)); /* MCU WM ring 15 */
    mt7927_wr(dev, MT_WFDMA0_TX_RING_EXT_CTRL(16), PREFETCH(0x0540, 0x4)); /* FWDL ring 16 */

    dev_info(dev->dev, "DMA prefetch configured\n");
}

/* ============================================
 * DMA Enable/Disable
 * ============================================ */

/**
 * mt7927_dma_disable - Disable DMA engine
 */
int mt7927_dma_disable(struct mt7927_dev *dev, bool force)
{
    u32 val;
    int i;

    /* Clear DMA enable bits and other config */
    mt7927_clear(dev, MT_WFDMA0_GLO_CFG,
                 MT_WFDMA0_GLO_CFG_TX_DMA_EN |
                 MT_WFDMA0_GLO_CFG_RX_DMA_EN |
                 MT_WFDMA0_GLO_CFG_CSR_DISP_BASE_PTR_CHAIN_EN);

    /* Wait for DMA to become idle */
    if (!force) {
        for (i = 0; i < 1000; i++) {
            val = mt7927_rr(dev, MT_WFDMA0_GLO_CFG);
            if (!(val & (MT_WFDMA0_GLO_CFG_TX_DMA_BUSY |
                        MT_WFDMA0_GLO_CFG_RX_DMA_BUSY))) {
                return 0;
            }
            usleep_range(100, 200);
        }

        dev_err(dev->dev, "Timeout waiting for DMA idle\n");
        return -ETIMEDOUT;
    }

    /* Perform logic reset (required for proper DMA initialization) */
    if (force) {
        u32 rst_before = mt7927_rr(dev, MT_WFDMA0_RST);
        dev_info(dev->dev, "DMA RST before: 0x%08x\n", rst_before);

        /*
         * IMPORTANT DISCOVERY: Ring registers are only writable when
         * RST bits are SET (chip in reset state). On fresh boot RST=0x30
         * and registers work. When we clear RST, they become read-only.
         *
         * So we do NOT clear RST here - leave it in reset state for
         * ring configuration. DMA enable will take care of the rest.
         */
        
        /* Only do reset pulse if we're not already in reset */
        if (!(rst_before & (MT_WFDMA0_RST_DMASHDL_ALL_RST | MT_WFDMA0_RST_LOGIC_RST))) {
            /* Clear first, then set to trigger reset */
            mt7927_clear(dev, MT_WFDMA0_RST,
                         MT_WFDMA0_RST_DMASHDL_ALL_RST |
                         MT_WFDMA0_RST_LOGIC_RST);
            mt7927_set(dev, MT_WFDMA0_RST,
                       MT_WFDMA0_RST_DMASHDL_ALL_RST |
                       MT_WFDMA0_RST_LOGIC_RST);
        }
        
        /* Leave RST bits SET - ring registers need this to be writable */
        dev_info(dev->dev, "DMA RST after: 0x%08x (keeping in reset for ring config)\n",
                 mt7927_rr(dev, MT_WFDMA0_RST));
    }

    return 0;
}

/**
 * mt7927_dma_enable - Enable DMA engine
 */
int mt7927_dma_enable(struct mt7927_dev *dev)
{
    u32 val, before;

    /* Configure prefetch settings first */
    mt7927_dma_prefetch(dev);

    /* Reset DMA pointers */
    mt7927_wr(dev, MT_WFDMA0_RST_DTX_PTR, ~0);
    mt7927_wr(dev, MT_WFDMA0_RST_DRX_PTR, ~0);

    /* Configure delay interrupt */
    mt7927_wr(dev, MT_WFDMA0_PRI_DLY_INT_CFG0, 0);

    /*
     * NOTE: Do NOT clear MT_WFDMA0_RST here!
     * The reference driver (mt792x_dma_enable) leaves RST=0x30.
     * Ring configuration is done with RST set, and DMA works with RST set.
     */
    dev_info(dev->dev, "DMA RST before enable: 0x%08x (keeping as-is)\n",
             mt7927_rr(dev, MT_WFDMA0_RST));

    /* Read current state */
    before = mt7927_rr(dev, MT_WFDMA0_GLO_CFG);
    dev_info(dev->dev, "WPDMA_GLO_CFG before: 0x%08x\n", before);

    /* Configure DMA global settings (based on mt7925) */
    val = MT_WFDMA0_GLO_CFG_TX_DMA_EN |
          MT_WFDMA0_GLO_CFG_RX_DMA_EN |
          MT_WFDMA0_GLO_CFG_TX_WB_DDONE |
          MT_WFDMA0_GLO_CFG_RX_WB_DDONE |
          MT_WFDMA0_GLO_CFG_FIFO_LITTLE_ENDIAN |
          MT_WFDMA0_GLO_CFG_CLK_GAT_DIS |
          MT_WFDMA0_GLO_CFG_FIFO_DIS_CHECK |
          MT_WFDMA0_GLO_CFG_CSR_DISP_BASE_PTR_CHAIN_EN |
          FIELD_PREP(MT_WFDMA0_GLO_CFG_DMA_SIZE, 3);

    dev_info(dev->dev, "Writing WPDMA_GLO_CFG: 0x%08x\n", before | val);
    mt7927_set(dev, MT_WFDMA0_GLO_CFG, val);
    wmb();

    /* Verify DMA is enabled */
    val = mt7927_rr(dev, MT_WFDMA0_GLO_CFG);
    dev_info(dev->dev, "WPDMA_GLO_CFG after: 0x%08x\n", val);

    if (!(val & (MT_WFDMA0_GLO_CFG_TX_DMA_EN | MT_WFDMA0_GLO_CFG_RX_DMA_EN))) {
        dev_err(dev->dev, "Failed to enable DMA (register write-protected?)\n");
        dev_info(dev->dev, "FW_STATUS: 0x%08x (0xffff10f1 = pre-init state)\n",
                 mt7927_rr(dev, MT_WFDMA0_HOST_INT_STA));
        return -EIO;
    }

    dev_info(dev->dev, "DMA enabled successfully\n");

    /* Verify ring configuration survived enable */
    {
        u32 tx0_base = mt7927_rr(dev, MT_TX_RING_BASE + 0x00);
        u32 tx4_base = mt7927_rr(dev, MT_TX_RING_BASE + 0x40);
        u32 tx5_base = mt7927_rr(dev, MT_TX_RING_BASE + 0x50);
        dev_info(dev->dev, "Ring verify: TX0=0x%08x TX4=0x%08x TX5=0x%08x\n",
                 tx0_base, tx4_base, tx5_base);
        if (tx5_base == 0) {
            dev_err(dev->dev, "Ring config was wiped during enable!\n");
            return -EIO;
        }
    }

    /* Enable interrupts for TX/RX completion */
    mt7927_wr(dev, MT_WFDMA0_HOST_INT_ENA,
              MT_INT_RX_DONE_ALL | MT_INT_TX_DONE_ALL | MT_INT_MCU_CMD);
    
    dev_info(dev->dev, "Interrupts enabled: 0x%08x\n",
             mt7927_rr(dev, MT_WFDMA0_HOST_INT_ENA));

    return 0;
}

/* ============================================
 * DMA Initialization
 * ============================================ */

/**
 * mt7927_dma_init - Initialize all DMA queues
 */
int mt7927_dma_init(struct mt7927_dev *dev)
{
    int ret;

    dev_info(dev->dev, "Initializing DMA subsystem...\n");

    /* Disable DMA first */
    ret = mt7927_dma_disable(dev, true);
    if (ret)
        return ret;

    /* Reset WPDMA */
    ret = mt7927_wpdma_reset(dev, true);
    if (ret)
        return ret;

    /* ---- TX Queues ---- */

    /* TX Queue 0: Band0 Data (not needed for firmware load, but allocate anyway) */
    ret = mt7927_queue_alloc(dev, &dev->tx_q[0], MT7927_TXQ_BAND0,
                             MT7927_TX_RING_SIZE, 0,
                             MT_WFDMA0_TX_RING_BASE(MT7927_TXQ_BAND0));
    if (ret) {
        dev_err(dev->dev, "Failed to allocate TX data queue\n");
        goto err_cleanup;
    }

    /* TX Queue 1: MCU WM (for MCU commands) */
    ret = mt7927_queue_alloc(dev, &dev->tx_q[1], MT7927_TXQ_MCU_WM,
                             MT7927_TX_MCU_RING_SIZE, 0,
                             MT_WFDMA0_TX_RING_BASE(MT7927_TXQ_MCU_WM));
    if (ret) {
        dev_err(dev->dev, "Failed to allocate TX MCU queue\n");
        goto err_cleanup;
    }
    dev->q_mcu[MT_MCUQ_WM] = &dev->tx_q[1];

    /* TX Queue 2: FWDL (for firmware download) */
    ret = mt7927_queue_alloc(dev, &dev->tx_q[2], MT7927_TXQ_FWDL,
                             MT7927_TX_FWDL_RING_SIZE, 0,
                             MT_WFDMA0_TX_RING_BASE(MT7927_TXQ_FWDL));
    if (ret) {
        dev_err(dev->dev, "Failed to allocate TX FWDL queue\n");
        goto err_cleanup;
    }
    dev->q_mcu[MT_MCUQ_FWDL] = &dev->tx_q[2];

    /* Set TX ring extension control for 36-bit DMA */
    mt7927_wr(dev, MT_WFDMA0_TX_RING_EXT_CTRL(MT7927_TXQ_BAND0), 0x4);
    mt7927_wr(dev, MT_WFDMA0_TX_RING_EXT_CTRL(MT7927_TXQ_MCU_WM), 0x4);
    mt7927_wr(dev, MT_WFDMA0_TX_RING_EXT_CTRL(MT7927_TXQ_FWDL), 0x4);

    /* ---- RX Queues ---- */

    /* RX Queue 0: MCU WM (for MCU responses) */
    ret = mt7927_queue_alloc(dev, &dev->rx_q[0], MT7927_RXQ_MCU_WM,
                             MT7927_RX_MCU_RING_SIZE, MT_RX_BUF_SIZE,
                             MT_WFDMA0_RX_RING_BASE(MT7927_RXQ_MCU_WM));
    if (ret) {
        dev_err(dev->dev, "Failed to allocate RX MCU queue\n");
        goto err_cleanup;
    }

    /* RX Queue 2: Band0 Data (not needed for firmware load) */
    ret = mt7927_queue_alloc(dev, &dev->rx_q[2], MT7927_RXQ_BAND0,
                             MT7927_RX_RING_SIZE, MT_RX_BUF_SIZE,
                             MT_WFDMA0_RX_RING_BASE(MT7927_RXQ_BAND0));
    if (ret) {
        dev_err(dev->dev, "Failed to allocate RX data queue\n");
        goto err_cleanup;
    }

    /* Enable DMA */
    ret = mt7927_dma_enable(dev);
    if (ret)
        goto err_cleanup;

    dev_info(dev->dev, "DMA initialization complete\n");
    return 0;

err_cleanup:
    mt7927_dma_cleanup(dev);
    return ret;
}

/**
 * mt7927_dma_cleanup - Cleanup all DMA queues
 */
void mt7927_dma_cleanup(struct mt7927_dev *dev)
{
    int i;

    dev_info(dev->dev, "Cleaning up DMA...\n");

    /* Disable DMA */
    mt7927_dma_disable(dev, true);

    /* Free TX queues */
    for (i = 0; i < ARRAY_SIZE(dev->tx_q); i++)
        mt7927_queue_free(dev, &dev->tx_q[i]);

    /* Free RX queues */
    for (i = 0; i < ARRAY_SIZE(dev->rx_q); i++)
        mt7927_queue_free(dev, &dev->rx_q[i]);

    /* Clear MCU queue pointers */
    for (i = 0; i < __MT_MCUQ_MAX; i++)
        dev->q_mcu[i] = NULL;
}
