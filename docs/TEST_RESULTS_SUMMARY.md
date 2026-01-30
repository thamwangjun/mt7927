# MT7927 Test Results Summary

**Date**: 2026-01-31 (Updated from 2025-08-18 initial exploration)
**Status**: ✅ HARDWARE VALIDATED | 🔧 FIRMWARE LOADING IN PROGRESS

## Executive Summary

Hardware exploration is complete. **Root cause of firmware loading blocker identified**: MT7927 ROM bootloader does NOT support mailbox command protocol. Solution is polling-based firmware loading (validated by zouyonghao working driver).

**Key Findings**:
- MT7927 is **MT6639 variant** (proven via MediaTek kernel modules)
- Uses **MT7925 firmware files** (CONNAC3X family compatibility)
- ROM bootloader requires **polling mode**, not mailbox waiting
- DMA hardware works correctly when proper protocol is used

## Architecture Validated ✅

| Discovery | Status | Evidence |
|-----------|--------|----------|
| MT7927 = MT6639 variant | **✓ CONFIRMED** | MediaTek kernel module PCI table |
| MT7925 firmware compatible | **✓ CONFIRMED** | Windows driver analysis + CONNAC3X family |
| Ring 15 = MCU_WM | **✓ CONFIRMED** | MT6639 bus_info structure |
| Ring 16 = FWDL | **✓ CONFIRMED** | MT6639 bus_info structure |
| 8 physical TX rings | **✓ CONFIRMED** | Hardware scan (CNT=512 for rings 0-7) |
| No mailbox in ROM | **✓ CONFIRMED** | Zouyonghao working driver analysis |

## Hardware Configuration

| Property | Value |
|----------|-------|
| Chip ID | 0x00511163 |
| BAR0 | 2MB (main registers) |
| BAR2 | 32KB (read-only shadow) |
| TX Rings | 8 physical, sparse layout (0,1,2,15,16 used) |
| WFDMA1 | NOT present |
| Architecture | CONNAC3X (same as MT7925) |

## Firmware Files (Resolved)

Uses MT7925 firmware (CONNAC3X family compatibility):
```
/lib/firmware/mediatek/mt7925/
├── WIFI_MT7925_PATCH_MCU_1_1_hdr.bin  # MCU patch
└── WIFI_RAM_CODE_MT7925_1_1.bin        # RAM code
```

**Note**: No MT7927-specific firmware exists. MT7925 files are the correct ones.

## Root Cause Analysis

### Why Firmware Loading Was Blocked

1. **Old assumption**: Driver should wait for mailbox responses
2. **Reality**: MT7927 ROM bootloader does NOT send mailbox responses
3. **Result**: Driver waited forever for responses that never came

### Solution (Validated)

Use polling-based firmware loading:
- Skip semaphore command (ROM doesn't support it)
- Send firmware without waiting for mailbox responses
- Use time-based delays instead of response waiting
- Set SW_INIT_DONE manually instead of FW_START command

See **[ZOUYONGHAO_ANALYSIS.md](ZOUYONGHAO_ANALYSIS.md)** for complete details.

## Current Status

### Working ✅
- PCI enumeration and BAR mapping
- Power management handshake (LPCTL)
- WiFi subsystem reset (WFSYS_SW_RST_B)
- DMA ring allocation
- Firmware file loading into memory
- Ring 15/16 configuration

### In Progress 🔧
- Polling-based firmware loader implementation
- Test module validation

### Pending (after firmware loads)
- MCU command processing
- Network interface creation
- WiFi connectivity

## Test Infrastructure

Current test modules in `tests/05_dma_impl/`:
- `test_power_ctrl.ko` - Power management
- `test_wfsys_reset.ko` - WiFi reset
- `test_dma_queues.ko` - DMA ring allocation
- `test_fw_load.ko` - Complete firmware loading (polling mode)

Diagnostic modules in `diag/`:
- 20 hardware exploration modules
- `mt7927_fw_precheck.ko` - Pre-flight validation

## References

- **[ZOUYONGHAO_ANALYSIS.md](ZOUYONGHAO_ANALYSIS.md)** - Root cause and solution
- **[MT6639_ANALYSIS.md](MT6639_ANALYSIS.md)** - Architecture proof
- **[HARDWARE.md](HARDWARE.md)** - Hardware specifications

---
*Last Updated: 2026-01-31*
