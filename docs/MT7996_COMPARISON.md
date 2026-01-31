# MT7996 vs MT7927 Comparison Analysis

This document analyzes the relationship between MT7927 and the MT7996 WiFi chipset family to determine if MT7927 could be a variant of MT7996.

**Conclusion: MT7927 is NOT an MT7996 variant. It is confirmed to be an MT6639 variant.**

## Executive Summary

While MT7996 and MT7927 share some CONNAC3X infrastructure (WFDMA base addresses, some register mappings), they have fundamentally different:
- Firmware loading protocols (mailbox vs polling)
- Ring assignments (dense vs sparse layout)
- Hardware capabilities (WFDMA1 presence, number of bands)
- Product positioning (AP/router vs client WiFi)

The MediaTek BSP explicitly maps MT7927 to MT6639 driver data, not MT7996.

---

## Device ID Comparison

| Chip | PCI Device ID | Family |
|------|---------------|--------|
| MT7996 | 0x7990, 0x7991 | MT7996 (tri-band AP) |
| MT7992 | 0x7992, 0x799a | MT7996 (dual-band AP) |
| MT7990 | 0x7993, 0x799b | MT7996 (dual-band AP, no WA) |
| **MT7927** | **0x7927** | **MT6639 (client WiFi)** |

MT7927's device ID (0x7927) is NOT registered in the MT7996 driver's device table.

---

## Hardware Architecture Comparison

| Feature | MT7996 Family | MT7927 | Match? |
|---------|---------------|--------|--------|
| WFDMA0 Base | 0xd4000 | 0xd4000 | ✅ Same |
| WFDMA1 Base | 0xd5000 | **NOT PRESENT** | ❌ Different |
| TX Ring Layout | Dense (16,17,18,19,20,21) | Sparse (0,1,2,15,16) | ❌ Different |
| FWDL Ring | Ring 16 | Ring 16 | ✅ Same |
| MCU_WM Ring | Ring 17 | Ring 15 | ❌ Different |
| MCU_WA Ring | Ring 20 (MT7996) | Not used | ❌ Different |
| Number of Bands | 2-3 (tri-band/dual-band) | 2 (2.4/5GHz) | Partial |
| Product Type | AP/Router | Client WiFi | ❌ Different |
| WiFi Agent (WA) | Required | Not used | ❌ Different |

### Critical Hardware Difference: WFDMA1

MT7996 has a second WFDMA block at 0xd5000:
```c
/* From reference_mt76/mt7996/regs.h:464 */
#define MT_WFDMA1_BASE              0xd5000
```

MT7927 does NOT have WFDMA1 (confirmed in Phase 5 hardware exploration). Any code that references WFDMA1 registers would fail on MT7927.

---

## Ring Layout Comparison

### MT7996 TX Queues (from mt7996.h:172-179)
```c
enum mt7996_txq_id {
    MT7996_TXQ_FWDL = 16,      // Firmware download
    MT7996_TXQ_MCU_WM,         // = 17, MCU WM commands
    MT7996_TXQ_BAND0,          // = 18, Data band 0
    MT7996_TXQ_BAND1,          // = 19, Data band 1
    MT7996_TXQ_MCU_WA,         // = 20, MCU WA commands
    MT7996_TXQ_BAND2,          // = 21, Data band 2
};
```

### MT7927 TX Queues (from MT6639 analysis)
```c
// Sparse layout - only these rings are present
TX Ring 0:  Data
TX Ring 1:  Data
TX Ring 2:  Data
TX Ring 15: MCU_WM (MCU commands)
TX Ring 16: FWDL (Firmware download)
// Rings 3-14 and 17+ are NOT USED
```

---

## Firmware Loading Protocol Comparison

### MT7996 Firmware Loading (mailbox-based)

From `reference_mt76/mt7996/mcu.c:3131-3215`:

```c
static int mt7996_load_patch(struct mt7996_dev *dev)
{
    // 1. Acquire patch semaphore via MAILBOX COMMAND
    sem = mt76_connac_mcu_patch_sem_ctrl(&dev->mt76, 1);
    switch (sem) {
    case PATCH_IS_DL:
        return 0;
    case PATCH_NOT_DL_SEM_SUCCESS:
        break;
    default:
        dev_err(dev->mt76.dev, "Failed to get patch semaphore\n");
        return -EAGAIN;
    }

    // 2. For each firmware region, use MAILBOX to init download
    ret = mt76_connac_mcu_init_download(&dev->mt76, addr, len, mode);

    // 3. Send firmware data
    ret = __mt76_mcu_send_firmware(&dev->mt76, MCU_CMD(FW_SCATTER),
                                   dl, len, 4096);

    // 4. Start patch via MAILBOX
    ret = mt76_connac_mcu_start_patch(&dev->mt76);

    // 5. Release semaphore via MAILBOX
    sem = mt76_connac_mcu_patch_sem_ctrl(&dev->mt76, 0);
}
```

**Key point**: MT7996 relies on mailbox protocol responses for every stage.

### MT7927 Firmware Loading (polling-based)

MT7927 ROM bootloader does NOT support mailbox commands (proven in Phase 21):

```c
// MT7927 approach (from zouyonghao reference)
static int mt7927_load_patch(struct mt7927_dev *dev)
{
    // 1. NO semaphore command - ROM doesn't respond to mailbox!

    // 2. Configure ring 16 for FWDL

    // 3. Write firmware chunks via DMA (polling mode, no response wait)
    // Use wait=false for all MCU commands:
    mt76_mcu_send_msg(dev, cmd, data, len, false);

    // 4. Aggressively clean up TX queue after each chunk
    dev->queue_ops->tx_cleanup(dev, dev->q_mcu[MT_MCUQ_FWDL], true);

    // 5. Ring doorbell (HOST2MCU_SW_INT)
    writel(0x1, bar0 + MT_WFDMA0_HOST2MCU_SW_INT_SET);

    // 6. Skip FW_START command - manually set SW_INIT_DONE
    val = readl(bar0 + MT_WFSYS_SW_RST_B);
    writel(val | BIT(4), bar0 + MT_WFSYS_SW_RST_B);
}
```

**Key point**: MT7927 must use polling because ROM doesn't respond to mailbox.

---

## Register Map Comparison

### Shared Mappings (CONNAC3X common infrastructure)

| Chip Address | BAR0 Offset | Purpose | MT7996 | MT7927 |
|--------------|-------------|---------|--------|--------|
| 0x7c020000 | 0xd0000 | CONN_INFRA, WFDMA | ✅ | ✅ |
| 0x7c060000 | 0xe0000 | CONN_HOST_CSR_TOP | ✅ | ✅ |
| 0x7c000000 | 0xf0000 | CONN_INFRA | ✅ | ✅ |
| 0x81020000 | 0xc0000 | WF_TOP_MISC_ON | ✅ | ✅ |

### MT7927-Specific (MT6639 family)

| Chip Address | BAR0 Offset | Purpose | MT7996 | MT7927 |
|--------------|-------------|---------|--------|--------|
| 0x70020000 | 0x1f0000 | CB_INFRA | ❌ | ✅ Required |
| 0x70025034 | 0x1f5034 | CRYPTO_MCU_OWN | ❌ | ✅ Required |
| 0x70026554 | 0x1f6554 | PCIE_REMAP_WF | ❌ | ✅ Required |

---

## Firmware Files Comparison

### MT7996 Family
```
/lib/firmware/mediatek/mt7996/
├── mt7996_wa.bin          # WiFi Agent (required)
├── mt7996_wm.bin          # WiFi Manager
├── mt7996_dsp.bin         # DSP firmware
└── mt7996_rom_patch.bin   # ROM patch

/lib/firmware/mediatek/mt7996/
├── mt7992_wa.bin
├── mt7992_wm.bin
├── mt7992_dsp.bin
└── mt7992_rom_patch.bin
```

### MT7927
```
/lib/firmware/mediatek/mt7925/
├── WIFI_MT7925_PATCH_MCU_1_1_hdr.bin  # MCU patch
└── WIFI_RAM_CODE_MT7925_1_1.bin       # RAM code (1.4MB)
```

MT7927 uses MT7925 firmware files (confirmed compatible via Windows driver analysis).

---

## CONNAC3X Family Tree

```
CONNAC3X Family
├── MT6639 (Mobile/Embedded, sparse ring layout)
│   ├── Ring layout: 0,1,2,15,16
│   ├── No WFDMA1
│   ├── Polling-based firmware loading
│   └── MT7927 (PCIe client WiFi variant)  ← OUR CHIP
│
├── MT7925 (PCIe Client WiFi)
│   ├── Ring layout: 0-16 (dense)
│   ├── Mailbox protocol works
│   └── Similar firmware format
│
└── MT7996 (AP/Router)
    ├── Ring layout: 16-21 (dense, high indices)
    ├── Has WFDMA1
    ├── Requires WA firmware
    ├── Mailbox protocol
    ├── MT7996 (tri-band, 3 radios)
    ├── MT7992 (dual-band, 2 radios)
    └── MT7990 (dual-band, no WA)
```

---

## Why MT7996 Driver Would Fail on MT7927

1. **Mailbox timeout**: `mt76_connac_mcu_patch_sem_ctrl()` would timeout because MT7927 ROM doesn't respond.

2. **WFDMA1 access failure**: Code referencing 0xd5000+ would access non-existent hardware.

3. **Wrong ring indices**: Using ring 17 for MCU commands instead of ring 15.

4. **Missing CB_INFRA init**: MT7996 doesn't initialize CB_INFRA_PCIE_REMAP_WF which is required for MT7927.

5. **WA firmware dependency**: MT7996 expects WiFi Agent which MT7927 doesn't use.

---

## Authoritative Evidence

### MediaTek BSP Device Table

From `reference_mtk_modules/.../pcie.c:170-172`:
```c
{   PCI_DEVICE(MTK_PCI_VENDOR_ID, NIC7927_PCIe_DEVICE_ID),  // 0x7927
    .driver_data = (kernel_ulong_t)&mt66xx_driver_data_mt6639},
```

This explicitly maps MT7927 to MT6639 driver data, NOT MT7996.

### Ring 15/16 Validation

From `reference_mtk_modules/.../mt6639.c:1389-1399`:
```c
// Confirms MT6639 (and therefore MT7927) uses rings 15 and 16
asicConnac3xWfdmaTxRingExtCtrl(prGlueInfo, prBusInfo, 15, CONNAC3X_TX_RING_IDX15_SIZE);
asicConnac3xWfdmaTxRingExtCtrl(prGlueInfo, prBusInfo, 16, CONNAC3X_TX_RING_IDX16_SIZE);
```

---

## Conclusion

MT7927 is definitively an **MT6639 variant**, not an MT7996 variant. The shared CONNAC3X infrastructure explains some register similarities, but the fundamental architecture differences make MT7996 code incompatible with MT7927.

Our current approach (based on MT6639 reference code and zouyonghao polling-based firmware loading) is correct.

---

## References

- `reference_mt76/mt7996/` - Linux kernel MT7996 driver
- `reference_mtk_modules/.../mt6639.c` - MediaTek BSP MT6639 implementation
- `reference_zouyonghao_mt7927/` - Community MT7927 driver with polling-based loading
- `docs/MT6639_ANALYSIS.md` - Detailed MT6639 architecture analysis
- `docs/ZOUYONGHAO_ANALYSIS.md` - Firmware loading protocol analysis
