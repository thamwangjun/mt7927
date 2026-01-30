// SPDX-License-Identifier: GPL-2.0
/*
 * MT7927 WiFi 7 Linux Driver - MCU Communication
 *
 * Handles MCU message protocol and firmware loading
 *
 * Copyright (C) 2024 MT7927 Linux Driver Project
 */

#include <linux/skbuff.h>
#include <linux/firmware.h>
#include <linux/delay.h>

#include "mt7927.h"
#include "mt7927_mcu.h"

/* Maximum firmware chunk size for scatter command */
#define MT7927_FW_CHUNK_SIZE    (64 * 1024)

/* ============================================
 * MCU Message Operations
 * ============================================ */

/**
 * mt7927_mcu_fill_message - Fill MCU message header
 * @dev: device structure
 * @skb: SKB containing the message data
 * @cmd: command ID
 * @seq: pointer to store sequence number
 */
int mt7927_mcu_fill_message(struct mt7927_dev *dev, struct sk_buff *skb,
                            int cmd, int *seq)
{
    struct mt7927_mcu_txd *txd;
    u32 val;
    u8 s2d = S2D_IDX_MCU;
    u8 pkt_type = MT_PKT_TYPE_CMD;
    int cmd_id = MCU_CMD_ID(cmd);
    int ext_id = MCU_CMD_EXT_ID(cmd);

    /* Reserve space for header */
    txd = (struct mt7927_mcu_txd *)skb_push(skb, sizeof(*txd));
    memset(txd, 0, sizeof(*txd));

    /* Get sequence number */
    *seq = dev->mcu.seq++;
    dev->mcu.seq &= 0xf;  /* 4-bit sequence number */

    /* Set TX descriptor word 0 */
    val = FIELD_PREP(MT_TXD0_TX_BYTES, skb->len) |
          FIELD_PREP(MT_TXD0_PKT_FMT, pkt_type);
    txd->txd[0] = cpu_to_le32(val);

    /* Fill MCU header fields */
    txd->len = cpu_to_le16(skb->len);
    txd->pq_id = cpu_to_le16(0x8000);  /* Priority queue ID */
    txd->cid = cmd_id;
    txd->pkt_type = pkt_type;
    txd->set_query = MCU_SET;
    txd->seq = *seq;

    if (cmd & MCU_CMD_FIELD_EXT_ID) {
        txd->ext_cid = ext_id;
        txd->ext_cid_ack = 1;
    }

    txd->s2d_index = s2d;

    return 0;
}

/**
 * mt7927_mcu_send_msg - Send MCU message
 * @dev: device structure
 * @cmd: command ID
 * @data: message data
 * @len: data length
 * @wait_resp: wait for response
 */
int mt7927_mcu_send_msg(struct mt7927_dev *dev, int cmd,
                        const void *data, int len, bool wait_resp)
{
    struct sk_buff *skb = NULL;

    return mt7927_mcu_send_and_get_msg(dev, cmd, data, len, wait_resp, &skb);
}

/**
 * mt7927_mcu_send_and_get_msg - Send MCU message and get response
 * @dev: device structure
 * @cmd: command ID
 * @data: message data
 * @len: data length
 * @wait_resp: wait for response
 * @ret_skb: pointer to store response SKB
 */
int mt7927_mcu_send_and_get_msg(struct mt7927_dev *dev, int cmd,
                                const void *data, int len,
                                bool wait_resp, struct sk_buff **ret_skb)
{
    struct mt7927_queue *q;
    struct sk_buff *skb;
    int ret, seq;

    /* Allocate SKB for message */
    skb = alloc_skb(len + MT_MCU_HDR_SIZE + 32, GFP_KERNEL);
    if (!skb)
        return -ENOMEM;

    skb_reserve(skb, MT_MCU_HDR_SIZE + 16);

    /* Copy data to SKB */
    if (data && len > 0)
        skb_put_data(skb, data, len);

    /* Fill message header */
    ret = mt7927_mcu_fill_message(dev, skb, cmd, &seq);
    if (ret) {
        dev_kfree_skb(skb);
        return ret;
    }

    /* Select queue based on command */
    if (cmd == MCU_CMD(MCU_CMD_FW_SCATTER))
        q = dev->q_mcu[MT_MCUQ_FWDL];
    else
        q = dev->q_mcu[MT_MCUQ_WM];

    if (!q) {
        dev_err(dev->dev, "MCU queue not initialized\n");
        dev_kfree_skb(skb);
        return -EINVAL;
    }

    /* Queue the message */
    ret = mt7927_tx_queue_skb(dev, q, skb);
    if (ret) {
        dev_err(dev->dev, "Failed to queue MCU message: %d\n", ret);
        dev_kfree_skb(skb);
        return ret;
    }

    /* Wait for response if requested */
    if (wait_resp) {
        unsigned long timeout = dev->mcu.timeout;
        struct sk_buff *resp_skb;
        struct mt7927_mcu_rxd *rxd;

        ret = wait_event_timeout(dev->mcu.wait,
                                !skb_queue_empty(&dev->mcu.res_q),
                                timeout);
        if (ret <= 0) {
            dev_err(dev->dev, "MCU command 0x%04x timeout (ret=%d)\n", cmd, ret);
            return -ETIMEDOUT;
        }

        /* Get response from queue */
        resp_skb = skb_dequeue(&dev->mcu.res_q);
        if (!resp_skb) {
            dev_err(dev->dev, "MCU response queue empty\n");
            return -EIO;
        }

        /* Validate response sequence */
        rxd = (struct mt7927_mcu_rxd *)resp_skb->data;
        if (rxd->seq != seq) {
            dev_err(dev->dev, "MCU response seq mismatch: expected %d, got %d\n",
                    seq, rxd->seq);
            dev_kfree_skb(resp_skb);
            return -EIO;
        }

        /* Return response SKB if requested */
        if (ret_skb)
            *ret_skb = resp_skb;
        else
            dev_kfree_skb(resp_skb);
    }

    return 0;
}

/* ============================================
 * Firmware Download Protocol
 * ============================================ */

/**
 * mt7927_mcu_patch_sem_ctrl - Control patch semaphore
 * @dev: device structure
 * @get: true to acquire, false to release
 */
int mt7927_mcu_patch_sem_ctrl(struct mt7927_dev *dev, bool get)
{
    struct mt7927_patch_sem_req req = {
        .op = get ? PATCH_SEM_GET : PATCH_SEM_RELEASE,
    };
    struct sk_buff *skb;
    struct mt7927_mcu_rxd *rxd;
    int ret;

    ret = mt7927_mcu_send_and_get_msg(dev, MCU_CMD(MCU_CMD_PATCH_SEM_CONTROL),
                                      &req, sizeof(req), true, &skb);
    if (ret)
        return ret;

    /* Check response */
    rxd = (struct mt7927_mcu_rxd *)skb->data;
    skb_pull(skb, sizeof(*rxd) - 4);
    ret = *skb->data;
    dev_kfree_skb(skb);

    if (get) {
        if (ret == PATCH_SEM_READY) {
            dev_dbg(dev->dev, "Patch semaphore acquired\n");
            return 0;
        } else if (ret == PATCH_SEM_NOT_READY) {
            dev_dbg(dev->dev, "Patch already loaded\n");
            return 1;  /* Patch already loaded */
        } else {
            dev_err(dev->dev, "Failed to acquire patch semaphore: %d\n", ret);
            return -EIO;
        }
    }

    return ret;
}

/**
 * mt7927_mcu_start_patch - Signal patch loading complete
 */
int mt7927_mcu_start_patch(struct mt7927_dev *dev)
{
    u8 req[4] = {};

    return mt7927_mcu_send_msg(dev, MCU_CMD(MCU_CMD_PATCH_FINISH_REQ),
                               req, sizeof(req), true);
}

/**
 * mt7927_mcu_start_firmware - Start firmware execution
 * @dev: device structure
 * @addr: firmware entry point address
 */
int mt7927_mcu_start_firmware(struct mt7927_dev *dev, u32 addr)
{
    struct mt7927_start_fw_req req = {
        .override = cpu_to_le32(addr ? 1 : 0),
        .addr = cpu_to_le32(addr),
    };

    return mt7927_mcu_send_msg(dev, MCU_CMD(MCU_CMD_START_FIRMWARE),
                               &req, sizeof(req), true);
}

/**
 * mt7927_mcu_send_firmware - Send firmware data via scatter command
 * @dev: device structure
 * @cmd: scatter command
 * @data: firmware data
 * @len: data length
 */
int mt7927_mcu_send_firmware(struct mt7927_dev *dev, int cmd,
                             const void *data, int len)
{
    struct sk_buff *skb;
    struct mt7927_mcu_txd *txd;
    int ret, seq;
    u32 val;

    /* Allocate SKB for firmware chunk */
    skb = alloc_skb(len + MT_MCU_HDR_SIZE + 32, GFP_KERNEL);
    if (!skb)
        return -ENOMEM;

    skb_reserve(skb, MT_MCU_HDR_SIZE + 16);

    /* Copy firmware data */
    skb_put_data(skb, data, len);

    /* Fill header */
    txd = (struct mt7927_mcu_txd *)skb_push(skb, sizeof(*txd));
    memset(txd, 0, sizeof(*txd));

    seq = dev->mcu.seq++;
    dev->mcu.seq &= 0xf;

    val = FIELD_PREP(MT_TXD0_TX_BYTES, skb->len) |
          FIELD_PREP(MT_TXD0_PKT_FMT, MT_PKT_TYPE_FW);
    txd->txd[0] = cpu_to_le32(val);

    txd->len = cpu_to_le16(skb->len);
    txd->pq_id = cpu_to_le16(0x8000);
    txd->cid = MCU_CMD_ID(cmd);
    txd->pkt_type = MT_PKT_TYPE_FW;
    txd->seq = seq;
    txd->s2d_index = S2D_IDX_MCU;

    /* Send via FWDL queue */
    ret = mt7927_tx_queue_skb(dev, dev->q_mcu[MT_MCUQ_FWDL], skb);
    if (ret)
        dev_kfree_skb(skb);

    return ret;
}

/* ============================================
 * Firmware Loading
 * ============================================ */

/**
 * mt7927_load_patch - Load ROM patch firmware
 */
int mt7927_load_patch(struct mt7927_dev *dev)
{
    const struct firmware *fw = dev->fw_patch;
    const struct mt7927_patch_hdr *hdr;
    const struct mt7927_patch_sec *sec;
    const u8 *data;
    int i, ret, n_region;
    u32 addr, len, offset;

    if (!fw || fw->size < sizeof(*hdr)) {
        dev_err(dev->dev, "Invalid patch firmware\n");
        return -EINVAL;
    }

    hdr = (const struct mt7927_patch_hdr *)fw->data;
    n_region = le32_to_cpu(hdr->sec_info.n_region);

    dev_info(dev->dev, "Loading patch firmware: %d regions\n", n_region);

    /* Parse and load each region */
    offset = sizeof(*hdr) + n_region * sizeof(*sec);
    sec = (const struct mt7927_patch_sec *)(fw->data + sizeof(*hdr));

    for (i = 0; i < n_region; i++, sec++) {
        addr = le32_to_cpu(sec->info.addr);
        len = le32_to_cpu(sec->info.len);

        if (offset + len > fw->size) {
            dev_err(dev->dev, "Patch region %d exceeds firmware size\n", i);
            return -EINVAL;
        }

        data = fw->data + offset;

        dev_dbg(dev->dev, "Patch region %d: addr=0x%08x len=%u\n", i, addr, len);

        /* Send firmware data in chunks */
        while (len > 0) {
            u32 chunk_len = min_t(u32, len, MT7927_FW_CHUNK_SIZE);
            struct mt7927_fw_scatter scatter = {
                .addr = cpu_to_le32(addr),
                .len = cpu_to_le32(chunk_len),
                .mode = cpu_to_le32(FW_MODE_DL),
            };

            /* Send scatter header */
            ret = mt7927_mcu_send_msg(dev, MCU_CMD(MCU_CMD_FW_SCATTER),
                                      &scatter, sizeof(scatter), false);
            if (ret) {
                dev_err(dev->dev, "Failed to send patch scatter: %d\n", ret);
                return ret;
            }

            /* Send firmware chunk */
            ret = mt7927_mcu_send_firmware(dev, MCU_CMD(MCU_CMD_FW_SCATTER),
                                           data, chunk_len);
            if (ret) {
                dev_err(dev->dev, "Failed to send patch data: %d\n", ret);
                return ret;
            }

            data += chunk_len;
            addr += chunk_len;
            len -= chunk_len;

            /* Small delay between chunks */
            usleep_range(100, 200);
        }

        offset += le32_to_cpu(sec->info.len);
    }

    return 0;
}

/**
 * mt7927_load_ram - Load RAM code firmware
 */
int mt7927_load_ram(struct mt7927_dev *dev)
{
    const struct firmware *fw = dev->fw_ram;
    const struct mt7927_fw_trailer *trailer;
    const struct mt7927_fw_region *region;
    const u8 *data;
    int i, ret, n_region;
    u32 offset;

    if (!fw || fw->size < sizeof(*trailer)) {
        dev_err(dev->dev, "Invalid RAM firmware\n");
        return -EINVAL;
    }

    /* Trailer is at the end of firmware */
    trailer = (const struct mt7927_fw_trailer *)(fw->data + fw->size - sizeof(*trailer));
    n_region = trailer->n_region;

    dev_info(dev->dev, "Loading RAM firmware: %d regions, version: %.10s\n",
             n_region, trailer->fw_ver);

    /* Region headers are before the trailer */
    offset = fw->size - sizeof(*trailer) - n_region * sizeof(*region);
    region = (const struct mt7927_fw_region *)(fw->data + offset);

    /* Process each region */
    offset = 0;
    for (i = 0; i < n_region; i++, region++) {
        u32 addr = le32_to_cpu(region->addr);
        u32 len = le32_to_cpu(region->len);

        if (offset + len > fw->size - sizeof(*trailer) - n_region * sizeof(*region)) {
            dev_err(dev->dev, "RAM region %d exceeds firmware size\n", i);
            return -EINVAL;
        }

        data = fw->data + offset;

        dev_dbg(dev->dev, "RAM region %d: addr=0x%08x len=%u name=%.32s\n",
                i, addr, len, region->name);

        /* Send firmware data in chunks */
        while (len > 0) {
            u32 chunk_len = min_t(u32, len, MT7927_FW_CHUNK_SIZE);
            struct mt7927_fw_scatter scatter = {
                .addr = cpu_to_le32(addr),
                .len = cpu_to_le32(chunk_len),
                .mode = cpu_to_le32(FW_MODE_DL),
            };

            /* Send scatter header */
            ret = mt7927_mcu_send_msg(dev, MCU_CMD(MCU_CMD_FW_SCATTER),
                                      &scatter, sizeof(scatter), false);
            if (ret) {
                dev_err(dev->dev, "Failed to send RAM scatter: %d\n", ret);
                return ret;
            }

            /* Send firmware chunk */
            ret = mt7927_mcu_send_firmware(dev, MCU_CMD(MCU_CMD_FW_SCATTER),
                                           data, chunk_len);
            if (ret) {
                dev_err(dev->dev, "Failed to send RAM data: %d\n", ret);
                return ret;
            }

            data += chunk_len;
            addr += chunk_len;
            len -= chunk_len;

            usleep_range(100, 200);
        }

        offset += le32_to_cpu(region->len);
    }

    return 0;
}

/**
 * mt7927_load_firmware - Complete firmware loading sequence
 */
int mt7927_load_firmware(struct mt7927_dev *dev)
{
    int ret;

    dev_info(dev->dev, "Loading firmware...\n");

    /* Request firmware files */
    ret = request_firmware(&dev->fw_patch, MT7927_ROM_PATCH, dev->dev);
    if (ret) {
        dev_err(dev->dev, "Failed to load ROM patch: %s\n", MT7927_ROM_PATCH);
        return ret;
    }
    dev_info(dev->dev, "Loaded ROM patch: %zu bytes\n", dev->fw_patch->size);

    ret = request_firmware(&dev->fw_ram, MT7927_FIRMWARE_WM, dev->dev);
    if (ret) {
        dev_err(dev->dev, "Failed to load RAM firmware: %s\n", MT7927_FIRMWARE_WM);
        goto err_release_patch;
    }
    dev_info(dev->dev, "Loaded RAM firmware: %zu bytes\n", dev->fw_ram->size);

    /* Step 1: Acquire patch semaphore */
    ret = mt7927_mcu_patch_sem_ctrl(dev, true);
    if (ret < 0) {
        dev_err(dev->dev, "Failed to get patch semaphore\n");
        goto err_release_ram;
    }

    if (ret == 1) {
        /* Patch already loaded, skip to RAM */
        dev_info(dev->dev, "Patch already loaded, loading RAM only\n");
        goto load_ram;
    }

    /* Step 2: Load ROM patch */
    ret = mt7927_load_patch(dev);
    if (ret) {
        dev_err(dev->dev, "Failed to load ROM patch\n");
        goto err_sem_release;
    }

    /* Step 3: Signal patch complete */
    ret = mt7927_mcu_start_patch(dev);
    if (ret) {
        dev_err(dev->dev, "Failed to signal patch complete\n");
        goto err_sem_release;
    }

    /* Step 4: Release patch semaphore */
    ret = mt7927_mcu_patch_sem_ctrl(dev, false);
    if (ret) {
        dev_err(dev->dev, "Failed to release patch semaphore\n");
        goto err_release_ram;
    }

load_ram:
    /* Step 5: Load RAM code */
    ret = mt7927_load_ram(dev);
    if (ret) {
        dev_err(dev->dev, "Failed to load RAM firmware\n");
        goto err_release_ram;
    }

    /* Step 6: Start firmware execution */
    ret = mt7927_mcu_start_firmware(dev, 0);
    if (ret) {
        dev_err(dev->dev, "Failed to start firmware\n");
        goto err_release_ram;
    }

    /* Wait for firmware to become ready */
    msleep(100);

    dev->mcu.state = MT7927_MCU_STATE_FW_LOADED;
    dev_info(dev->dev, "Firmware loaded successfully\n");

    return 0;

err_sem_release:
    mt7927_mcu_patch_sem_ctrl(dev, false);
err_release_ram:
    release_firmware(dev->fw_ram);
    dev->fw_ram = NULL;
err_release_patch:
    release_firmware(dev->fw_patch);
    dev->fw_patch = NULL;
    return ret;
}

/* ============================================
 * MCU Initialization
 * ============================================ */

/**
 * mt7927_mcu_init - Initialize MCU and load firmware
 */
int mt7927_mcu_init(struct mt7927_dev *dev)
{
    int ret;
    u32 val;

    dev_info(dev->dev, "Initializing MCU...\n");

    /*
     * Pre-firmware initialization (from mt7925e_mcu_init reference):
     * 1. Ensure driver owns power control
     * 2. Disable L0S power saving (can interfere with DMA)
     * 3. Set firmware mode to normal
     */
    
    /* Ensure driver power control */
    ret = mt7927_mcu_drv_pmctrl(dev);
    if (ret) {
        dev_warn(dev->dev, "drv_pmctrl in mcu_init failed: %d\n", ret);
        /* Continue anyway */
    }

    /* Disable L0S power saving for stable DMA operation */
    val = mt7927_rr(dev, MT_PCIE_MAC_PM);
    dev_info(dev->dev, "PCIE_MAC_PM before: 0x%08x\n", val);
    mt7927_set(dev, MT_PCIE_MAC_PM, MT_PCIE_MAC_PM_L0S_DIS);
    dev_info(dev->dev, "PCIE_MAC_PM after L0S disable: 0x%08x\n",
             mt7927_rr(dev, MT_PCIE_MAC_PM));

    /* Set firmware mode to normal (required before firmware download) */
    val = mt7927_rr(dev, MT_SWDEF_MODE);
    dev_info(dev->dev, "SWDEF_MODE before: 0x%08x\n", val);
    mt7927_wr(dev, MT_SWDEF_MODE, MT_SWDEF_NORMAL_MODE);
    dev_info(dev->dev, "SWDEF_MODE after: 0x%08x\n",
             mt7927_rr(dev, MT_SWDEF_MODE));

    /* Enable interrupts for MCU communication */
    mt7927_irq_enable(dev, dev->irq_map->tx.mcu_complete_mask |
                           dev->irq_map->rx.wm_complete_mask |
                           MT_INT_MCU_CMD);

    /* Load firmware */
    ret = mt7927_load_firmware(dev);
    if (ret)
        return ret;

    dev->mcu.state = MT7927_MCU_STATE_RUNNING;
    set_bit(MT7927_STATE_MCU_RUNNING, &dev->state);

    dev_info(dev->dev, "MCU initialization complete\n");
    return 0;
}

/**
 * mt7927_mcu_exit - Shutdown MCU
 */
void mt7927_mcu_exit(struct mt7927_dev *dev)
{
    dev_info(dev->dev, "Shutting down MCU...\n");

    /* Purge response queue */
    skb_queue_purge(&dev->mcu.res_q);

    /* Clear MCU running state */
    clear_bit(MT7927_STATE_MCU_RUNNING, &dev->state);
    dev->mcu.state = MT7927_MCU_STATE_INIT;
}
