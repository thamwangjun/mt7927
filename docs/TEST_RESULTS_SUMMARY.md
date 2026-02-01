# MT7927 Test Results Summary

> **⚠️ PROJECT STATUS: STALLED (Phase 29c) ⚠️**
>
> The driver does not work. Firmware loads but never executes.

**Date**: 2026-02-01
**Status**: ❌ STALLED - DMA path failure prevents firmware execution

---

## Executive Summary

After 29+ phases of investigation, the driver development has stalled. Hardware exploration is complete and many components work correctly, but **firmware never executes** because DMA data doesn't reach device memory.

**Final State**:
- ✅ Hardware validated and understood
- ✅ Architecture proven (MT7927 = MT6639 variant)
- ✅ Ring configuration verified correct
- ✅ Host-side DMA transfer completes
- ❌ **Device never receives firmware data**
- ❌ **WiFi functionality not available**

---

## Architecture Validated ✅

| Discovery | Status | Evidence |
|-----------|--------|----------|
| MT7927 = MT6639 variant | **✓ CONFIRMED** | MediaTek kernel module PCI table |
| MT7925 firmware compatible | **✓ CONFIRMED** | Windows driver analysis + CONNAC3X family |
| Ring 15 = MCU_WM | **✓ CONFIRMED** | MT6639 bus_info structure |
| Ring 16 = FWDL | **✓ CONFIRMED** | MT6639 bus_info structure |
| Sparse ring layout | **✓ CONFIRMED** | Hardware scan (rings 0,1,2,15,16) |
| No mailbox in ROM | **✓ CONFIRMED** | Zouyonghao driver analysis |
| WFDMA base = 0xd4000 | **✓ CONFIRMED** | Phase 21 discovery |
| CB_INFRA remap required | **✓ CONFIRMED** | Phase 19-20 discovery |

---

## Hardware Configuration

| Property | Value |
|----------|-------|
| Chip ID | 0x00511163 |
| BAR0 | 2MB (main registers) |
| BAR2 | 32KB (read-only shadow) |
| TX Rings | Sparse layout (0,1,2,15,16 used) |
| WFDMA1 | NOT present |
| Architecture | CONNAC3X (MT6639 variant) |

---

## Component Status

### Working ✅

| Component | Status | Notes |
|-----------|--------|-------|
| PCI enumeration | ✅ Works | Device detected as 14c3:7927 |
| BAR0 mapping | ✅ Works | 2MB register space accessible |
| Chip ID read | ✅ Works | Returns 0x00511163 |
| CB_INFRA init | ✅ Works | PCIe remap = 0x74037001 |
| WFSYS reset | ✅ Works | Reset sequence completes |
| MCU IDLE state | ✅ Works | Reaches 0x1D1E |
| DMA ring alloc | ✅ Works | Rings configured correctly |
| Ring base config | ✅ Works | HW readback matches desc_dma |
| Firmware parsing | ✅ Works | Patch + 5 RAM regions parsed |
| Host-side DMA | ✅ Works | Descriptors written, CIDX advanced |

### Failed ❌

| Component | Status | Issue |
|-----------|--------|-------|
| Device receives data | ❌ FAILED | MCU status stays 0x00000000 |
| FW_N9_RDY | ❌ FAILED | Stuck at 0x00000002 |
| Firmware execution | ❌ FAILED | N9 CPU never starts |
| Mailbox communication | ❌ FAILED | All commands timeout |
| WiFi interface | ❌ FAILED | Not created |

---

## The Unsolved Problem

**DMA data never reaches device memory.**

### Evidence

1. **MCU status = 0x00000000** after all 5 firmware regions
   - Expected: Status updates as each region is processed
   - Actual: Never changes

2. **FW_N9_RDY = 0x00000002**
   - BIT(0) = 0: N9 CPU NOT ready
   - BIT(1) = 1: Download mode entered
   - Expected: 0x00000003

3. **FW_START times out** when wait=true
   - Firmware not running, cannot respond

4. **IOMMU page faults** in some runs
   - Addresses like 0xff6b0xxx cause AMD-Vi errors

### Hypotheses Investigated

| Hypothesis | Result |
|------------|--------|
| Mailbox protocol issue | Fixed (use polling) - not the cause |
| Wrong WFDMA base | Fixed (0xd4000) - not the cause |
| Missing CB_INFRA init | Fixed (0x74037001) - not the cause |
| GLO_CFG timing | Fixed - not the cause |
| TXD format wrong | Fixed - not the cause |
| Missing doorbell | Fixed (HOST2MCU_SW_INT) - not the cause |
| Unused rings cause faults | Fixed - not the cause |
| RX_DMA_EN timing | Fixed - not the cause |
| IOMMU domain issue | **UNSOLVED** |
| MediaTek DMA config | **UNSOLVED** |

---

## Firmware Files

Uses MT7925 firmware (CONNAC3X family compatibility):
```
/lib/firmware/mediatek/mt7925/
├── WIFI_MT7925_PATCH_MCU_1_1_hdr.bin  # MCU patch (38912 bytes)
└── WIFI_RAM_CODE_MT7925_1_1.bin        # RAM code (5 regions, ~1.2MB total)
```

**Note**: No MT7927-specific firmware exists. MT7925 files are correct.

---

## Test Output (Phase 29c)

```
[MT7927] CONN_INFRA VERSION = 0x03010002 - OK
[MT7927] MCU IDLE (0x00001d1e)
[MT7927] Loading patch: mediatek/mt7925/WIFI_MT7925_PATCH_MCU_1_1_hdr.bin
[MT7927] Patch: addr=0x00900000 len=38912
[MT7927] Sending patch data (38912 bytes)...
[MT7927] MCU status after patch: 0x00000000    <- Never changes
[MT7927] Loading RAM: 5 regions
[MT7927] RAM region 0: addr=0x0090d000 len=77200 - SUCCESS
[MT7927] MCU status after region 0: 0x00000000 <- Never changes
[MT7927] RAM region 1-4: SUCCESS
[MT7927] MCU status after all regions: 0x00000000 <- Never changes
[MT7927] Sending FW_START command (waiting for response)
Message 00000002 (seq 9) timeout                <- Firmware not running
[MT7927] Waiting for FW_N9_RDY... (0x00000002)  <- Stuck
[MT7927] Firmware not ready after 1s (0x00000002)
```

---

## Key Documentation

- [../DEVELOPMENT_LOG.md](../DEVELOPMENT_LOG.md) - Complete 29-phase history
- [ZOUYONGHAO_ANALYSIS.md](ZOUYONGHAO_ANALYSIS.md) - DMA investigation details
- [MT6639_ANALYSIS.md](MT6639_ANALYSIS.md) - Architecture proof
- [ROADMAP.md](ROADMAP.md) - Project status

---

*Last updated: 2026-02-01 (Phase 29c - Project Stalled)*
