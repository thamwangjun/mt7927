# Reference Source Code Analysis

This document provides detailed analysis of the reference implementations used in developing the MT7927 Linux driver.

## Overview

The MT7927 driver development relies on three primary reference sources:

1. **reference_mtk_modules/** - MediaTek vendor kernel modules (MT6639 official implementation)
2. **reference_zouyonghao_mt7927/** - Working MT7927 community driver
3. **Linux kernel mt7925 driver** - CONNAC3X sibling implementation

This document analyzes each source's origin, licensing, and relevance to MT7927 development.

---

## 1. MediaTek Kernel Modules (reference_mtk_modules/)

### Source and Origin

**Repository**: https://github.com/Fede2782/MTK_modules

**Git Submodule**: Added to project at `reference_mtk_modules/`

**Original Source**: MediaTek vendor-specific kernel modules extracted from **Xiaomi device kernel tree**

**Device**: Xiaomi "Rodin" (device codename)

**Date**: Initial commit February 20, 2025

### Evidence of Origin

#### Git History
```
commit 26e64cd4c795b2ff2c07d530b956beabfdada2f1
Author: Yue Kang <yuekang@xiaomi.com>
Date:   Thu Feb 20 21:21:19 2025 +0800

    rodin open source
```

- Committed by Xiaomi engineer
- "Rodin" is Xiaomi device codename
- Complete vendor modules tree from MediaTek BSP (Board Support Package)

#### Copyright Headers
```c
// SPDX-License-Identifier: BSD-2-Clause (WLAN drivers)
// SPDX-License-Identifier: GPL-2.0 (chip-specific code)
/*
 * Copyright (c) 2021 MediaTek Inc.
 */
```

- Official MediaTek copyright
- Mixed licensing: BSD-2-Clause for WLAN, GPL-2.0 for chip drivers
- Professional code quality with standard kernel conventions

#### README Files
```
WMT driver - kernel modules move out of kernel tree
Wlan character device driver - kernel modules move out of kernel tree
```

These modules were intentionally separated from the main kernel tree, which is standard practice for MediaTek vendor kernel distributions.

### Repository Structure

```
reference_mtk_modules/
├── connectivity/           # Combo chip drivers
│   ├── wlan/              # WiFi driver (gen4m = CONNAC3X generation)
│   │   ├── core/
│   │   │   ├── gen4m/     # CONNAC3X family (gen4-mt79xx)
│   │   │   │   └── chips/
│   │   │   │       ├── mt6639/    # MT6639 chip driver ← PRIMARY REFERENCE
│   │   │   │       ├── mt7925/    # MT7925 chip driver
│   │   │   │       ├── mt7927/    # Would be here if implemented
│   │   │   │       └── ...
│   ├── bt/                # Bluetooth driver
│   ├── gps/               # GPS driver
│   └── fmradio/           # FM radio driver
├── gpu/                   # ARM Mali GPU drivers (r44p0, r44p1, r46p0, r49p3, r49p4)
├── mtkcam/                # MediaTek camera subsystem
├── fpsgo_cus/             # Frame-per-second governor (gaming optimization)
├── sched_cus/             # Custom scheduler extensions
├── met_drv_v2/            # MediaTek Embedded Tracer v2 (profiling/debugging)
├── met_drv_v3/            # MediaTek Embedded Tracer v3
├── hbt_driver/            # Heart-beat tracker (system health monitoring)
├── afs_common_utils/      # Android framework scheduling utilities
└── task_turbo_cus/        # Task turbo (performance boost)
```

### Android Build System Integration

**Evidence of Android origin**:

1. **Android.mk files throughout**:
   ```
   connectivity/bt/linux_v2/Android.mk
   connectivity/common/Android.mk
   connectivity/wlan/adaptor/Android.mk
   gpu/gpu_mali/.../Android.mk
   ```

2. **Android-specific includes**:
   ```makefile
   ccflags-y += -I$(srctree)/drivers/staging/android
   ccflags-y += -I$(srctree)/drivers/staging/android/ion
   ```

3. **MTK_PLATFORM configuration**:
   ```makefile
   MTK_PLATFORM := $(subst ",,$(CONFIG_MTK_PLATFORM))
   ccflags-y += -I$(srctree)/drivers/misc/mediatek/emi/$(MTK_PLATFORM)
   ```

### Critical Discovery: MT7927 → MT6639 Mapping

**File**: `connectivity/wlan/core/gen4m/os/linux/hif/pcie/pcie.c`

**Line 104**: Device ID definition
```c
#define NIC7927_PCIe_DEVICE_ID 0x7927
```

**Line 170-172**: PCI device table entry
```c
{   PCI_DEVICE(MTK_PCI_VENDOR_ID, NIC7927_PCIe_DEVICE_ID),
    .driver_data = (kernel_ulong_t)&mt66xx_driver_data_mt6639},
#endif /* MT6639 */
```

**Significance**: This is the **smoking gun** that proves MT7927 uses MT6639 driver data, NOT MT7925. This validates the architectural analysis in `MT6639_ANALYSIS.md`.

### Key Files for MT7927 Development

#### Primary Reference (MT6639 Implementation)

| File | Purpose | Relevance |
|------|---------|-----------|
| `chips/mt6639/mt6639.c` | Complete chip implementation | **CRITICAL** - Initialization sequences, ring setup |
| `chips/mt6639/mt6639.h` | Chip-specific constants | **CRITICAL** - Configuration values, feature flags |
| `chips/mt6639/coda/mt6639/wf_wfdma_host_dma0.h` | DMA register definitions | **CRITICAL** - Register offsets, bit definitions |
| `chips/mt6639/coda/mt6639/wf_pse_top.h` | Packet switching engine | Important - PSE configuration |
| `chips/mt6639/coda/mt6639/pcie_mac_ireg.h` | PCIe MAC registers | Important - PCIe link control |
| `chips/mt6639/hal_dmashdl_mt6639.c` | DMA scheduler | Important - DMA arbitration |

#### Secondary Reference (MT7925 for CONNAC3X Patterns)

| File | Purpose | Relevance |
|------|---------|-----------|
| `chips/mt7925/mt7925.c` | MT7925 implementation | Reference - CONNAC3X family patterns |
| `chips/mt7925/mt7925.h` | MT7925 constants | Reference - Compare with MT6639 |

### Comparison with Linux Kernel mt7925

| Aspect | MediaTek Vendor Code | Linux Kernel mt76/mt7925 |
|--------|----------------------|--------------------------|
| License | BSD-2-Clause + GPL-2.0 | GPL-2.0 only |
| Code Style | MediaTek internal style | Linux kernel style |
| Features | Full vendor feature set | Upstream-acceptable subset |
| Documentation | Minimal comments | Better documented |
| MT6639 Support | **Native, complete** | Not present |
| MT7927 Reference | **Explicit mapping** | No MT7927 support |
| Register Definitions | Comprehensive (coda/) | Minimal, abstracted |
| Validation | Production device tested | Community tested |

### Why This Source is Most Authoritative for MT7927

1. **Direct MT7927 → MT6639 mapping** - Proves architectural relationship
2. **Official MediaTek code** - Not reverse-engineered or community guesswork
3. **Production-tested** - Shipped in millions of Xiaomi devices
4. **Complete implementation** - All features, not just upstreamed subset
5. **Register-level documentation** - coda/ headers have exact bit definitions
6. **MT6639 native support** - Linux kernel doesn't have MT6639 driver

### Licensing Considerations

**WLAN Driver Core** (gen4m):
- License: BSD-2-Clause
- Permissive - Can read, reference, adapt for GPL driver
- MediaTek copyright retained

**Chip-Specific Code** (mt6639.c, etc.):
- License: GPL-2.0
- Compatible with Linux kernel submission
- Can reference and adapt directly

**Conclusion**: Safe to use as reference for GPL-2.0 MT7927 driver development.

### Not from Android AOSP Mainline

This code is **NOT** from Android Open Source Project (AOSP) mainline kernel because:

1. **AOSP excludes vendor drivers** - Google's AOSP doesn't include MediaTek WiFi/BT drivers
2. **Vendor kernel tree structure** - Layout matches MediaTek BSP distribution
3. **Proprietary subsystems** - MET tracer, custom schedulers are MediaTek-specific
4. **OEM customizations** - Xiaomi-specific components present
5. **README confirms extraction** - "kernel modules move out of kernel tree"

**Actual source chain**:
```
MediaTek BSP → Xiaomi device kernel tree → rodin open source → GitHub extraction
```

---

## 2. Zouyonghao MT7927 Driver (reference_zouyonghao_mt7927/)

### Source and Origin

**Repository**: https://github.com/zouyonghao/mt7927

**Git Submodule**: Added to project at `reference_zouyonghao_mt7927/`

**Author**: Community developer (zouyonghao)

**Status**: **Working driver** - Successfully loads firmware and creates network interface

### Significance

This is the **proof that MT7927 works** and the source of the root cause discovery:

- **Firmware loading works** - Uses polling-based protocol, NOT mailbox
- **Network interface created** - Achieves full initialization
- **DMA processing confirmed** - Hardware works when protocol is correct
- **Root cause validated** - Proves mailbox protocol assumption was wrong

See `ZOUYONGHAO_ANALYSIS.md` for complete analysis of this implementation.

### Key Discovery: Polling-Based Firmware Loading

**Critical difference from mt7925**:
- MT7925: Waits for mailbox responses during firmware loading
- MT7927 (zouyonghao): **Never waits**, uses polling and delays
- Result: MT7927 firmware loads successfully

This validates that **MT7927 ROM bootloader doesn't support mailbox protocol**.

### Licensing

License: Not explicitly specified in repository

**Note**: Used purely as reference for understanding correct initialization sequence. Our implementation is independent, GPL-2.0 licensed.

---

## 3. Linux Kernel MT7925 Driver

### Source

**Path**: `drivers/net/wireless/mediatek/mt76/mt7925/`

**Repository**: https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git

**Maintainer**: MediaTek, upstream Linux wireless subsystem

### Significance for MT7927

**CONNAC3X family member** - MT7925 and MT6639/MT7927 share:
- Firmware format (both use MT7925 firmware files)
- Ring protocol (rings 15/16 for MCU_WM/FWDL)
- DMA descriptor format
- MCU message structures

**Differences from MT7927**:
- MT7925: Dense ring layout (0-16), has WFDMA1
- MT7927: Sparse layout (0,1,2,15,16), WFDMA0 only
- MT7925: Mailbox protocol works in ROM bootloader
- MT7927: ROM bootloader doesn't support mailbox

**Use as reference**:
- ✓ General CONNAC3X patterns
- ✓ Firmware file handling
- ✓ DMA descriptor structures
- ✗ Direct ring assignments (different from MT6639)
- ✗ Firmware loading protocol (mailbox doesn't work on MT7927)

### Licensing

License: GPL-2.0

Fully compatible with MT7927 driver development.

---

## Reference Priority for MT7927 Development

### Priority 1: MediaTek Vendor Code (reference_mtk_modules/)

**When to use**: Architecture, register definitions, initialization sequences

**Key files**:
- `connectivity/wlan/core/gen4m/chips/mt6639/` - Complete MT6639 implementation
- `connectivity/wlan/core/gen4m/chips/mt6639/coda/mt6639/` - Register definitions

**Why**: Official MediaTek code, explicit MT7927→MT6639 mapping, production-tested

### Priority 2: Zouyonghao MT7927 Driver (reference_zouyonghao_mt7927/)

**When to use**: Firmware loading protocol, polling-based initialization

**Key files**:
- `mt7927_fw_load.c` - Working firmware loader implementation

**Why**: Proven to work on MT7927 hardware, demonstrates correct protocol

### Priority 3: Linux Kernel MT7925 (drivers/net/wireless/mediatek/mt76/mt7925/)

**When to use**: CONNAC3X family patterns, mac80211 integration, power management

**Key files**:
- `mt7925/pci.c` - PCI infrastructure
- `mt7925/mcu.c` - MCU protocol (but NOT firmware loading sequence)
- `mt7925/mac.c` - MAC layer operations

**Why**: Upstream quality, well-documented, but doesn't match MT7927 architecture exactly

---

## Validation of MT6639 Relationship

The MediaTek vendor code **definitively proves** the claims in `MT6639_ANALYSIS.md`:

### Evidence Summary

| Claim | Evidence Location | Validation |
|-------|-------------------|------------|
| MT7927 is MT6639 variant | `pcie.c` line 170-172 | **PROVEN** - Direct PCI ID mapping |
| Uses mt6639 driver data | Same location | **PROVEN** - `.driver_data = &mt66xx_driver_data_mt6639` |
| Ring 15 = MCU_WM | `mt6639/wf_wfdma_host_dma0.h` | **CONFIRMED** - Register definitions |
| Ring 16 = FWDL | Same file | **CONFIRMED** - CONNAC3X standard |
| Sparse ring layout | `mt6639.c` ring allocation | **CONFIRMED** - Only rings 0,1,2,15,16 |
| Single WFDMA0 | `mt6639/` implementation | **CONFIRMED** - No WFDMA1 references |
| Shares MT7925 firmware | Firmware name tables | **CONFIRMED** - Both use MT7925 files |

All architectural claims are validated by official MediaTek source code.

---

## Implications for Driver Development

### What We Know with Certainty

1. **MT7927 = MT6639 variant** (proven by vendor code)
2. **Ring assignments: 0,1,2,15,16** (MT6639 configuration)
3. **Firmware loading requires polling** (proven by zouyonghao)
4. **Mailbox protocol doesn't work in ROM** (proven by failure + zouyonghao success)
5. **Register offsets match MT6639** (vendor code reference)

### Implementation Confidence Levels

| Component | Confidence | Reference Source |
|-----------|-----------|------------------|
| PCI initialization | **High** | MT6639 vendor code |
| Power management | **High** | MT6639 vendor code + zouyonghao |
| WFSYS reset | **High** | MT6639 vendor code + zouyonghao |
| DMA ring allocation | **High** | MT6639 vendor code (rings 0,1,2,15,16) |
| Firmware loading (polling) | **High** | Zouyonghao working implementation |
| MCU protocol (runtime) | **Medium** | MT6639 + MT7925 (after firmware loads) |
| Network operations | **Medium** | MT7925 CONNAC3X patterns |
| WiFi 7 features | **Low** | No reference implementation yet |

### Code Attribution and References

When implementing MT7927 driver:

1. **Document reference sources** - Comment which reference was used
2. **Note differences** - Where MT7927 deviates from references
3. **Cite evidence** - Link to specific files/lines in references
4. **Credit discoveries** - Acknowledge zouyonghao's working implementation

Example:
```c
/* MT7927 uses MT6639 ring configuration (sparse layout: 0,1,2,15,16)
 * Reference: reference_mtk_modules/connectivity/wlan/core/gen4m/chips/mt6639/mt6639.c
 * Confirmed by PCI device table mapping: MT7927 -> mt66xx_driver_data_mt6639
 */
```

---

## See Also

- **[MT6639_ANALYSIS.md](MT6639_ANALYSIS.md)** - Architectural analysis proving MT7927 is MT6639 variant
- **[ZOUYONGHAO_ANALYSIS.md](ZOUYONGHAO_ANALYSIS.md)** - Analysis of working implementation, root cause discovery
- **[FIRMWARE_ANALYSIS.md](FIRMWARE_ANALYSIS.md)** - Firmware compatibility analysis
- **[DEVELOPMENT_LOG.md](../DEVELOPMENT_LOG.md)** - Complete development history
- **[ROADMAP.md](ROADMAP.md)** - Current status and implementation plan

---

**Last Updated**: 2026-01-31

**Status**: Reference sources identified and validated. Ready for polling-based firmware loader implementation.
