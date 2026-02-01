# MediaTek conninfra Source Code Reference

This document provides a complete inventory of the conninfra source code available in `reference_mtk_modules/connectivity/conninfra/`.

## Summary

| Metric | Count |
|--------|-------|
| C source files (.c) | 97 |
| Header files (.h) | 155 |
| Total source files | 252 |
| Total lines of C code | ~52,000 |
| License | GPL-2.0 |

---

## Directory Structure

```
conninfra/
├── include/                    # Public API headers
├── adaptor/                    # Kernel adaptor layer
│   ├── connsyslog/            # System logging
│   └── coredump/              # Coredump handling
├── base/                       # OS abstraction layer
├── conf/                       # Configuration
├── conn_drv/
│   ├── connv2/                # ConnV2 architecture
│   │   ├── core/              # Core logic
│   │   ├── src/               # Main implementation
│   │   ├── platform/          # Platform-specific code
│   │   │   ├── mt6877/
│   │   │   ├── mt6885/
│   │   │   ├── mt6893/
│   │   │   ├── mt6899/
│   │   │   └── mt6991/
│   │   ├── drv_init/          # Driver initialization
│   │   └── debug_utility/     # Debug tools
│   └── connv3/                # ConnV3 architecture
│       ├── core/
│       ├── src/
│       ├── platform/
│       │   ├── mt6639/        # MT7927 support
│       │   ├── mt6653/
│       │   └── mt6991/
│       └── debug_utility/
├── test/                       # Test code
├── Kbuild                      # Kernel build (27KB)
├── Makefile
└── Android.mk
```

---

## Complete Source File List

### Adaptor Layer (6 files)

| File | Purpose |
|------|---------|
| `adaptor/conn_kern_adaptor.c` | Kernel adaptor interface |
| `adaptor/connadp_internal.c` | Internal adaptor functions |
| `adaptor/conninfra_dev.c` | Device registration |
| `adaptor/connsyslog/connsyslog_to_user.c` | Log to userspace |
| `adaptor/connsyslog/fw_log_mcu.c` | MCU firmware logging |
| `adaptor/coredump/conndump_netlink.c` | Coredump netlink interface |

### Base Layer (4 files)

| File | Purpose |
|------|---------|
| `base/msg_thread.c` | Message thread handling |
| `base/osal.c` | OS abstraction layer |
| `base/osal_dbg.c` | OSAL debug utilities |
| `base/ring.c` | Ring buffer implementation |

### Configuration (1 file)

| File | Purpose |
|------|---------|
| `conf/conninfra_conf.c` | Configuration management |

### ConnV2 Core (2 files)

| File | Purpose |
|------|---------|
| `conn_drv/connv2/core/conninfra_core.c` | Core state machine and logic |
| `conn_drv/connv2/src/conninfra.c` | Main ConnV2 API implementation |
| `conn_drv/connv2/src/connv2_drv.c` | ConnV2 driver registration |

### ConnV2 Debug Utilities (6 files)

| File | Purpose |
|------|---------|
| `conn_drv/connv2/debug_utility/conninfra_dbg.c` | Debug interface |
| `conn_drv/connv2/debug_utility/connsyslog/connsyslog.c` | System logging |
| `conn_drv/connv2/debug_utility/connsyslog/ring_emi.c` | EMI ring buffer logging |
| `conn_drv/connv2/debug_utility/coredump/connsys_coredump.c` | Coredump collection |
| `conn_drv/connv2/debug_utility/coredump/coredump_mng.c` | Coredump management |
| `conn_drv/connv2/debug_utility/metlog/metlog.c` | MET logging |

### ConnV2 Driver Init (5 files)

| File | Purpose |
|------|---------|
| `conn_drv/connv2/drv_init/conn_drv_init.c` | Main driver init |
| `conn_drv/connv2/drv_init/wlan_drv_init.c` | WLAN driver init hooks |
| `conn_drv/connv2/drv_init/bluetooth_drv_init.c` | Bluetooth driver init hooks |
| `conn_drv/connv2/drv_init/gps_drv_init.c` | GPS driver init hooks |
| `conn_drv/connv2/drv_init/fm_drv_init.c` | FM driver init hooks |

### ConnV2 Platform Common (6 files)

| File | Purpose |
|------|---------|
| `conn_drv/connv2/platform/consys_hw.c` | Hardware abstraction |
| `conn_drv/connv2/platform/consys_hw_plat_data.c` | Platform data |
| `conn_drv/connv2/platform/consys_reg_mng.c` | Register management |
| `conn_drv/connv2/platform/clock_mng.c` | Clock management |
| `conn_drv/connv2/platform/emi_mng.c` | EMI memory management |
| `conn_drv/connv2/platform/pmic_mng.c` | PMIC management |

### ConnV2 Platform: MT6877 (6 files)

| File | Purpose |
|------|---------|
| `conn_drv/connv2/platform/mt6877/mt6877.c` | MT6877 main |
| `conn_drv/connv2/platform/mt6877/mt6877_consys_reg.c` | Register definitions |
| `conn_drv/connv2/platform/mt6877/mt6877_coredump.c` | Coredump support |
| `conn_drv/connv2/platform/mt6877/mt6877_emi.c` | EMI configuration |
| `conn_drv/connv2/platform/mt6877/mt6877_pmic.c` | PMIC control |
| `conn_drv/connv2/platform/mt6877/mt6877_pos.c` | Power-on sequence |

### ConnV2 Platform: MT6885 (6 files)

| File | Purpose |
|------|---------|
| `conn_drv/connv2/platform/mt6885/mt6885.c` | MT6885 main |
| `conn_drv/connv2/platform/mt6885/mt6885_consys_reg.c` | Register definitions |
| `conn_drv/connv2/platform/mt6885/mt6885_coredump.c` | Coredump support |
| `conn_drv/connv2/platform/mt6885/mt6885_emi.c` | EMI configuration |
| `conn_drv/connv2/platform/mt6885/mt6885_pmic.c` | PMIC control |
| `conn_drv/connv2/platform/mt6885/mt6885_pos.c` | Power-on sequence |

### ConnV2 Platform: MT6893 (6 files)

| File | Purpose |
|------|---------|
| `conn_drv/connv2/platform/mt6893/mt6893.c` | MT6893 main |
| `conn_drv/connv2/platform/mt6893/mt6893_consys_reg.c` | Register definitions |
| `conn_drv/connv2/platform/mt6893/mt6893_coredump.c` | Coredump support |
| `conn_drv/connv2/platform/mt6893/mt6893_emi.c` | EMI configuration |
| `conn_drv/connv2/platform/mt6893/mt6893_pmic.c` | PMIC control |
| `conn_drv/connv2/platform/mt6893/mt6893_pos.c` | Power-on sequence |

### ConnV2 Platform: MT6899 (11 files)

| File | Purpose |
|------|---------|
| `conn_drv/connv2/platform/mt6899/mt6899.c` | MT6899 main |
| `conn_drv/connv2/platform/mt6899/mt6899_atf.c` | ARM TrustZone Firmware |
| `conn_drv/connv2/platform/mt6899/mt6899_consys_reg.c` | Register definitions |
| `conn_drv/connv2/platform/mt6899/mt6899_coredump.c` | Coredump support |
| `conn_drv/connv2/platform/mt6899/mt6899_debug_gen.c` | Debug generation |
| `conn_drv/connv2/platform/mt6899/mt6899_emi.c` | EMI configuration |
| `conn_drv/connv2/platform/mt6899/mt6899_ops.c` | Operations |
| `conn_drv/connv2/platform/mt6899/mt6899_pmic.c` | PMIC control |
| `conn_drv/connv2/platform/mt6899/mt6899_pos.c` | Power-on sequence |
| `conn_drv/connv2/platform/mt6899/mt6899_pos_gen.c` | POS generation |
| `conn_drv/connv2/platform/mt6899/mt6899_soc.c` | SoC integration |

### ConnV2 Platform: MT6991 (10 files)

| File | Purpose |
|------|---------|
| `conn_drv/connv2/platform/mt6991/mt6991.c` | MT6991 main |
| `conn_drv/connv2/platform/mt6991/mt6991_consys_reg.c` | Register definitions |
| `conn_drv/connv2/platform/mt6991/mt6991_coredump.c` | Coredump support |
| `conn_drv/connv2/platform/mt6991/mt6991_debug_gen.c` | Debug generation |
| `conn_drv/connv2/platform/mt6991/mt6991_emi.c` | EMI configuration |
| `conn_drv/connv2/platform/mt6991/mt6991_ops.c` | Operations |
| `conn_drv/connv2/platform/mt6991/mt6991_pmic.c` | PMIC control |
| `conn_drv/connv2/platform/mt6991/mt6991_pos.c` | Power-on sequence |
| `conn_drv/connv2/platform/mt6991/mt6991_pos_gen.c` | POS generation |
| `conn_drv/connv2/platform/mt6991/mt6991_soc.c` | SoC integration |

### ConnV3 Core (3 files)

| File | Purpose |
|------|---------|
| `conn_drv/connv3/core/connv3_core.c` | ConnV3 core logic |
| `conn_drv/connv3/src/connv3.c` | Main ConnV3 API |
| `conn_drv/connv3/src/connv3_drv.c` | ConnV3 driver registration |

### ConnV3 Debug Utilities (3 files)

| File | Purpose |
|------|---------|
| `conn_drv/connv3/debug_utility/connsyslog/connv3_mcu_log.c` | MCU logging |
| `conn_drv/connv3/debug_utility/coredump/connv3_coredump.c` | Coredump collection |
| `conn_drv/connv3/debug_utility/coredump/connv3_dump_mng.c` | Dump management |

### ConnV3 Platform Common (5 files)

| File | Purpose |
|------|---------|
| `conn_drv/connv3/platform/connv3_hw.c` | Hardware abstraction |
| `conn_drv/connv3/platform/connv3_hw_dbg.c` | Hardware debug |
| `conn_drv/connv3/platform/connv3_hw_plat_data.c` | Platform data |
| `conn_drv/connv3/platform/connv3_pinctrl_mng.c` | Pin control management |
| `conn_drv/connv3/platform/connv3_pmic_mng.c` | PMIC management |

### ConnV3 Platform: MT6639 (1 file) - MT7927 Support

| File | Purpose |
|------|---------|
| `conn_drv/connv3/platform/mt6639/mt6639_dbg.c` | MT6639/MT7927 debug |

### ConnV3 Platform: MT6653 (1 file)

| File | Purpose |
|------|---------|
| `conn_drv/connv3/platform/mt6653/mt6653_dbg.c` | MT6653 debug |

### ConnV3 Platform: MT6991 (3 files)

| File | Purpose |
|------|---------|
| `conn_drv/connv3/platform/mt6991/mt6991.c` | MT6991 main |
| `conn_drv/connv3/platform/mt6991/mt6991_pinctrl.c` | Pin control |
| `conn_drv/connv3/platform/mt6991/mt6991_pmic.c` | PMIC control |

### Test Code (11 files)

#### ConnV2 Tests
| File | Purpose |
|------|---------|
| `test/connv2/conninfra_test.c` | Main test entry |
| `test/connv2/conninfra_core_test.c` | Core tests |
| `test/connv2/cal_test.c` | Calibration tests |
| `test/connv2/chip_rst_test.c` | Chip reset tests |
| `test/connv2/conf_test.c` | Configuration tests |
| `test/connv2/connsyslog_test.c` | Logging tests |
| `test/connv2/connv2_pos_test.c` | Power-on sequence tests |
| `test/connv2/dump_test.c` | Dump tests |
| `test/connv2/msg_evt_test.c` | Message event tests |

#### ConnV3 Tests
| File | Purpose |
|------|---------|
| `test/connv3/connv3_test.c` | ConnV3 tests |
| `test/connv3/connv3_dump_test.c` | ConnV3 dump tests |

---

## Key Files for MT7927 Development

For MT7927 (which uses MT6639 internally), the most relevant files are:

### Primary Files

| Priority | File | Purpose |
|----------|------|---------|
| 1 | `include/conninfra.h` | Main API header |
| 2 | `include/connv3.h` | ConnV3 API header |
| 3 | `conn_drv/connv3/src/connv3.c` | ConnV3 implementation |
| 4 | `conn_drv/connv3/core/connv3_core.c` | ConnV3 core logic |
| 5 | `conn_drv/connv3/platform/connv3_hw.c` | Hardware abstraction |
| 6 | `conn_drv/connv3/platform/mt6639/mt6639_dbg.c` | MT6639 debug |

### Supporting Files

| File | Purpose |
|------|---------|
| `base/osal.c` | OS abstraction (needed by all) |
| `base/msg_thread.c` | Message handling |
| `adaptor/conninfra_dev.c` | Device registration |
| `conn_drv/connv2/drv_init/wlan_drv_init.c` | WLAN init hooks |

---

## Build Files

| File | Size | Purpose |
|------|------|---------|
| `Kbuild` | 27,830 bytes | Kernel build configuration |
| `Makefile` | 793 bytes | Standard Makefile |
| `Android.mk` | 352 bytes | Android build |
| `BUILD.bazel` | 634 bytes | Bazel build |

---

## Platform Support Matrix

### ConnV2 Platforms

| Platform | SoC | Files | Notes |
|----------|-----|-------|-------|
| MT6877 | Dimensity 900 | 6 | Mid-range |
| MT6885 | Dimensity 1000 | 6 | Flagship 2020 |
| MT6893 | Dimensity 1200 | 6 | Flagship 2021 |
| MT6899 | - | 11 | Extended support |
| MT6991 | - | 10 | Latest ConnV2 |

### ConnV3 Platforms

| Platform | WiFi Chip | Files | Notes |
|----------|-----------|-------|-------|
| MT6639 | MT7927 | 1 | **Target for this project** |
| MT6653 | - | 1 | Alternative WiFi chip |
| MT6991 | - | 3 | Latest platform |

---

## Code Statistics by Component

| Component | Files | Approx. Lines |
|-----------|-------|---------------|
| Adaptor | 6 | ~3,000 |
| Base | 4 | ~2,500 |
| Configuration | 1 | ~500 |
| ConnV2 Core | 3 | ~5,000 |
| ConnV2 Debug | 6 | ~4,000 |
| ConnV2 Driver Init | 5 | ~500 |
| ConnV2 Platform Common | 6 | ~6,000 |
| ConnV2 Platform-specific | 39 | ~25,000 |
| ConnV3 Core | 3 | ~3,000 |
| ConnV3 Debug | 3 | ~2,000 |
| ConnV3 Platform | 10 | ~5,000 |
| Tests | 11 | ~1,500 |
| **Total** | **97** | **~52,000** |

---

## Usage for Reverse Engineering

### Understanding Power Sequences

Study these files in order:
1. `conn_drv/connv3/platform/connv3_hw.c` - Hardware init flow
2. `conn_drv/connv2/platform/consys_hw.c` - Detailed power sequences
3. Platform-specific `*_pos.c` files - Power-on sequences

### Understanding EMI Memory

1. `conn_drv/connv2/platform/emi_mng.c` - EMI management
2. Platform-specific `*_emi.c` files - EMI configuration

### Understanding PMIC Control

1. `conn_drv/connv2/platform/pmic_mng.c` - PMIC management
2. `conn_drv/connv3/platform/connv3_pmic_mng.c` - ConnV3 PMIC
3. Platform-specific `*_pmic.c` files - PMIC sequences

### Creating Stubs for Standalone PCIe

Use these as reference to create minimal stubs:
1. `include/conninfra.h` - API definitions to stub
2. `conn_drv/connv2/src/conninfra.c` - See what each API does
3. `conn_drv/connv3/src/connv3.c` - ConnV3 implementation

---

## License

The conninfra source code is licensed under **GPL-2.0** as indicated in:
- SPDX headers in source files
- `NOTICE` file (14,858 bytes)

This means:
- Source code can be studied and modified
- Modifications must also be GPL-2.0
- Binary distribution requires source availability
