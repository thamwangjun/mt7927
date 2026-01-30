/* SPDX-License-Identifier: GPL-2.0 */
/*
 * MT7927 WiFi 7 Linux Driver - MCU Protocol Definitions
 *
 * Message structures and command IDs for MCU communication
 *
 * Copyright (C) 2024 MT7927 Linux Driver Project
 */

#ifndef __MT7927_MCU_H
#define __MT7927_MCU_H

#include <linux/types.h>

/* ============================================
 * MCU Command Structure
 * ============================================ */

/* MCU command header size */
#define MT_MCU_HDR_SIZE         sizeof(struct mt7927_mcu_txd)

/* Max message size */
#define MT_MCU_MSG_MAX_SIZE     2048

/* MCU command IDs */
#define MCU_CMD_FW_SCATTER          0x0f
#define MCU_CMD_PATCH_SEM_CONTROL   0x10
#define MCU_CMD_PATCH_FINISH_REQ    0x11
#define MCU_CMD_PATCH_START_REQ     0x14
#define MCU_CMD_START_FIRMWARE      0x15
#define MCU_CMD_RESTART_DL          0x18

/* MCU UNI command IDs (unified interface) */
#define MCU_UNI_CMD_DEV_INFO_UPDATE 0x01
#define MCU_UNI_CMD_BSS_INFO_UPDATE 0x02
#define MCU_UNI_CMD_STA_REC_UPDATE  0x03
#define MCU_UNI_CMD_SUSPEND         0x04
#define MCU_UNI_CMD_OFFLOAD         0x06
#define MCU_UNI_CMD_HIF_CTRL        0x07
#define MCU_UNI_CMD_BAND_CONFIG     0x08
#define MCU_UNI_CMD_REPT_MUAR       0x09
#define MCU_UNI_CMD_REG_ACCESS      0x0d

/* MCU event IDs */
#define MCU_EVENT_FW_READY          0x01
#define MCU_EVENT_RESTART_DL        0x02
#define MCU_EVENT_PATCH_SEM         0x04
#define MCU_EVENT_GENERIC           0x05

/* Command field bits */
#define MCU_CMD_FIELD_ID            GENMASK(7, 0)
#define MCU_CMD_FIELD_EXT_ID        GENMASK(15, 8)
#define MCU_CMD_FIELD_QUERY         BIT(16)
#define MCU_CMD_FIELD_UNI           BIT(17)
#define MCU_CMD_FIELD_WM            BIT(19)

/* Source to destination indices */
#define S2D_IDX_MCU                 0
#define D2S_IDX_MCU                 0

/* ============================================
 * TX Descriptor (MCU Command Header)
 * ============================================ */

struct mt7927_mcu_txd {
    __le32 txd[8];          /* TX descriptor words */

    __le16 len;             /* Message length */
    __le16 pq_id;           /* Packet queue ID */

    u8 cid;                 /* Command ID */
    u8 pkt_type;            /* Packet type */
    u8 set_query;           /* 0: set, 1: query */
    u8 seq;                 /* Sequence number */

    u8 uc_d2b0_rev;
    u8 ext_cid;             /* Extended command ID */
    u8 s2d_index;           /* Source to destination index */
    u8 ext_cid_ack;         /* Extended CID acknowledgment */

    u32 rsv[5];
} __packed __aligned(4);

/* TX descriptor bits */
#define MT_TXD0_Q_IDX           GENMASK(31, 25)
#define MT_TXD0_PKT_FMT         GENMASK(24, 23)
#define MT_TXD0_ETH_TYPE_OFFSET GENMASK(22, 16)
#define MT_TXD0_TX_BYTES        GENMASK(15, 0)

#define MT_TXD1_OWN_MAC         GENMASK(31, 26)
#define MT_TXD1_HDR_FORMAT      GENMASK(7, 5)
#define MT_TXD1_TID             GENMASK(4, 0)

/* Packet type values */
#define MT_PKT_TYPE_TXD         0
#define MT_PKT_TYPE_FW          1
#define MT_PKT_TYPE_CMD         2
#define MT_PKT_TYPE_EVENT       3

/* Set/Query values */
#define MCU_SET                 0
#define MCU_QUERY               1

/* ============================================
 * RX Descriptor (MCU Event Header)
 * ============================================ */

struct mt7927_mcu_rxd {
    __le32 rxd[8];          /* RX descriptor words */

    __le16 len;             /* Message length */
    __le16 pkt_type_id;     /* Packet type ID */

    u8 eid;                 /* Event ID */
    u8 seq;                 /* Sequence number */
    u8 option;              /* Options */
    u8 rsv0;

    u8 ext_eid;             /* Extended event ID */
    u8 rsv1[2];
    u8 s2d_index;           /* Source to destination index */

    u8 tlv[];               /* TLV data follows */
} __packed __aligned(4);

/* UNI event structure */
struct mt7927_mcu_uni_event {
    u8 cid;
    u8 pad[3];
    __le32 status;          /* 0: success, others: fail */
} __packed;

/* ============================================
 * Firmware Download Structures
 * ============================================ */

/* Patch semaphore control */
#define PATCH_SEM_RELEASE       0
#define PATCH_SEM_GET           1

struct mt7927_patch_hdr {
    char build_date[16];
    char platform[4];
    __le32 hw_sw_ver;
    __le32 patch_ver;
    __le16 checksum;
    __le16 rsv0;
    struct {
        __le32 patch_ver;
        __le32 subsys;
        __le32 feature;
        __le32 n_region;
        __le32 crc;
        __le32 rsv[11];
    } sec_info;
    u8 rsv1[108];
} __packed;

struct mt7927_patch_sec {
    __le32 type;
    __le32 offs;
    __le32 size;
    union {
        __le32 spec[13];
        struct {
            __le32 addr;
            __le32 len;
            __le32 sec_key_idx;
            __le32 align_len;
            __le32 rsv[9];
        } info;
    };
} __packed;

/* Firmware header */
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

struct mt7927_fw_region {
    __le32 decomp_crc;
    __le32 decomp_len;
    __le32 decomp_blk_sz;
    u8 rsv0[4];
    __le32 addr;
    __le32 len;
    u8 feature_set;
    u8 type;
    u8 rsv1[14];
    char name[32];
} __packed;

/* FW scatter command */
struct mt7927_fw_scatter {
    __le32 addr;
    __le32 len;
    __le32 mode;
    u8 rsv[4];
} __packed;

/* Firmware download modes */
#define FW_MODE_DL              0
#define FW_MODE_START           1
#define FW_MODE_READY           2

/* ============================================
 * MCU Messages
 * ============================================ */

/* Patch semaphore request */
struct mt7927_patch_sem_req {
    u8 op;                  /* PATCH_SEM_GET or PATCH_SEM_RELEASE */
    u8 rsv[3];
} __packed;

/* Patch semaphore response */
#define PATCH_SEM_NOT_READY     0
#define PATCH_SEM_READY         1
#define PATCH_SEM_ERROR         2

/* Start firmware request */
struct mt7927_start_fw_req {
    __le32 override;
    __le32 addr;
} __packed;

/* Restart download request */
struct mt7927_restart_dl_req {
    u8 rsv[4];
} __packed;

/* ============================================
 * Helper Macros
 * ============================================ */

/* Build MCU command value */
#define MCU_CMD(cmd)            ((cmd) & MCU_CMD_FIELD_ID)
#define MCU_EXT_CMD(cmd)        (((cmd) << 8) | MCU_CMD_FIELD_EXT_ID)
#define MCU_UNI_CMD(cmd)        ((cmd) | MCU_CMD_FIELD_UNI)
#define MCU_WM_CMD(cmd)         ((cmd) | MCU_CMD_FIELD_WM)
#define MCU_WM_UNI_CMD(cmd)     ((cmd) | MCU_CMD_FIELD_UNI | MCU_CMD_FIELD_WM)
#define MCU_WM_UNI_CMD_QUERY(cmd) ((cmd) | MCU_CMD_FIELD_UNI | \
                                   MCU_CMD_FIELD_WM | MCU_CMD_FIELD_QUERY)

/* Get command ID from full command value */
#define MCU_CMD_ID(cmd)         FIELD_GET(MCU_CMD_FIELD_ID, cmd)
#define MCU_CMD_EXT_ID(cmd)     FIELD_GET(MCU_CMD_FIELD_EXT_ID, cmd)

/* ============================================
 * Function Declarations
 * ============================================ */

/* MCU message operations */
int mt7927_mcu_fill_message(struct mt7927_dev *dev, struct sk_buff *skb,
                            int cmd, int *seq);

/* Firmware loading */
int mt7927_mcu_patch_sem_ctrl(struct mt7927_dev *dev, bool get);
int mt7927_mcu_start_patch(struct mt7927_dev *dev);
int mt7927_mcu_start_firmware(struct mt7927_dev *dev, u32 addr);
int mt7927_mcu_send_firmware(struct mt7927_dev *dev, int cmd,
                             const void *data, int len);

#endif /* __MT7927_MCU_H */
