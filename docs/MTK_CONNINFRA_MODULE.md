# MediaTek Connectivity Infrastructure (conninfra) Module

This document describes the `conninfra` module, a required dependency for the MediaTek proprietary WLAN driver. The module is located at `reference_mtk_modules/connectivity/conninfra/`.

## Overview

**conninfra** (Connectivity Infrastructure) is a MediaTek kernel module that manages the shared connectivity subsystem on MediaTek SoCs. It serves as the common foundation layer for all wireless radios:

- WiFi (WLAN)
- Bluetooth (BT)
- GPS
- FM Radio

The module coordinates shared resources between these subsystems to ensure proper operation and power management.

---

## Purpose and Functions

| Function | Description |
|----------|-------------|
| **Power Management** | Coordinated power on/off sequences for all radios |
| **EMI Memory** | Shared memory (External Memory Interface) allocation for firmware |
| **Clock Control** | Bus clock management (26MHz/52MHz COTMS/EXTCXO) |
| **PMIC Control** | Power regulator management via MediaTek PMIC |
| **RF SPI** | Shared SPI bus for A-die (analog die) register access |
| **Chip Reset** | Coordinated whole-chip reset across all subsystems |
| **Calibration** | Pre-calibration coordination between radios |
| **Coredump** | Debug dump collection on failures |
| **Thermal** | Temperature monitoring and throttling |

---

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                      Applications                            │
├───────────────┬───────────────┬───────────────┬─────────────┤
│     WiFi      │   Bluetooth   │      GPS      │     FM      │
│    Driver     │    Driver     │    Driver     │   Driver    │
│   (gen4m)     │     (bt)      │    (gps)      │ (fmradio)   │
├───────────────┴───────────────┴───────────────┴─────────────┤
│                        conninfra                             │
│                                                              │
│  ┌─────────┐ ┌─────────┐ ┌─────────┐ ┌─────────┐ ┌────────┐ │
│  │  Power  │ │   EMI   │ │  Clock  │ │   PMIC  │ │ RF SPI │ │
│  │   Mgt   │ │   Mgt   │ │   Mgt   │ │   Mgt   │ │  Mgt   │ │
│  └─────────┘ └─────────┘ └─────────┘ └─────────┘ └────────┘ │
│  ┌─────────┐ ┌─────────┐ ┌─────────┐ ┌─────────┐            │
│  │  Reset  │ │  Calib  │ │ Coredmp │ │ Thermal │            │
│  │   Mgt   │ │   Mgt   │ │   Mgt   │ │   Mgt   │            │
│  └─────────┘ └─────────┘ └─────────┘ └─────────┘            │
├─────────────────────────────────────────────────────────────┤
│                  MediaTek SoC Hardware                       │
│                                                              │
│    ┌──────────────────┐      ┌──────────────────┐           │
│    │     D-die        │ ←──→ │      A-die       │           │
│    │   (Digital)      │      │   (Analog/RF)    │           │
│    │                  │      │                  │           │
│    │ - CPU/DSP        │      │ - RF Frontend    │           │
│    │ - Memory Ctrl    │      │ - PLL/Clock      │           │
│    │ - Bus Interface  │      │ - Power Mgmt     │           │
│    └──────────────────┘      └──────────────────┘           │
└─────────────────────────────────────────────────────────────┘
```

---

## Supported Subsystems

From `conninfra.h`:

```c
enum consys_drv_type {
    CONNDRV_TYPE_BT = 0,        // Bluetooth
    CONNDRV_TYPE_FM = 1,        // FM Radio
    CONNDRV_TYPE_GPS = 2,       // GPS
    CONNDRV_TYPE_WIFI = 3,      // WiFi (MT7927 uses this)
    CONNDRV_TYPE_MAWD = 4,      // MAWD (WiFi offload)
    CONNDRV_TYPE_CONNINFRA = 5, // Conninfra itself
    CONNDRV_TYPE_MAX
};
```

---

## Key APIs

### Header File

`connectivity/conninfra/include/conninfra.h`

### Power Control

```c
// Power on a subsystem
// drv_type: CONNDRV_TYPE_WIFI for WiFi
// Returns: 0 on success, negative on error
int conninfra_pwr_on(enum consys_drv_type drv_type);

// Power off a subsystem
int conninfra_pwr_off(enum consys_drv_type drv_type);
```

### EMI Memory Management

```c
// Get firmware EMI physical address and size
void conninfra_get_phy_addr(phys_addr_t *addr, unsigned int *size);

// Get specific EMI region
// type: CONNSYS_EMI_FW (firmware) or CONNSYS_EMI_MCIF (modem interface)
void conninfra_get_emi_phy_addr(enum connsys_emi_type type,
                                 phys_addr_t *base,
                                 unsigned int *size);
```

### RF SPI Access

```c
// Read from RF SPI subsystem
// subsystem: SYS_SPI_WF (WiFi), SYS_SPI_BT, SYS_SPI_GPS, etc.
int conninfra_spi_read(enum sys_spi_subsystem subsystem,
                       unsigned int addr,
                       unsigned int *data);

// Write to RF SPI subsystem
int conninfra_spi_write(enum sys_spi_subsystem subsystem,
                        unsigned int addr,
                        unsigned int data);

// Read-modify-write
int conninfra_spi_update_bits(enum sys_spi_subsystem subsystem,
                              unsigned int addr,
                              unsigned int data,
                              unsigned int mask);
```

### SPI Subsystems

```c
enum sys_spi_subsystem {
    SYS_SPI_WF1 = 0x00,  // WiFi 1
    SYS_SPI_WF  = 0x01,  // WiFi
    SYS_SPI_BT  = 0x02,  // Bluetooth
    SYS_SPI_FM  = 0x03,  // FM Radio
    SYS_SPI_GPS = 0x04,  // GPS
    SYS_SPI_TOP = 0x05,  // Top-level
    SYS_SPI_WF2 = 0x06,  // WiFi 2
    SYS_SPI_WF3 = 0x07,  // WiFi 3
    SYS_SPI_MAX
};
```

### Chip Reset

```c
// Trigger whole-chip reset
// drv: which driver triggered the reset
// reason: string describing why reset was triggered
// Returns: 0 = triggered, 1 = ongoing, <0 = error
int conninfra_trigger_whole_chip_rst(enum consys_drv_type drv, char *reason);

// Reset callback structure
struct whole_chip_rst_cb {
    int (*pre_whole_chip_rst)(enum consys_drv_type drv, char *reason);
    int (*post_whole_chip_rst)(void);
};
```

### Clock Control

```c
// Get clock schematic type
int conninfra_get_clock_schematic(void);

// Clock schematic types
enum connsys_clock_schematic {
    CONNSYS_CLOCK_SCHEMATIC_26M_COTMS = 0,  // 26MHz COTMS
    CONNSYS_CLOCK_SCHEMATIC_52M_COTMS,       // 52MHz COTMS
    CONNSYS_CLOCK_SCHEMATIC_26M_EXTCXO,      // 26MHz External XO
    CONNSYS_CLOCK_SCHEMATIC_52M_EXTCXO,      // 52MHz External XO
    CONNSYS_CLOCK_SCHEMATIC_MAX,
};

// SPI clock switch
int conninfra_spi_clock_switch(enum connsys_spi_speed_type type);

// Bus clock control
int conninfra_bus_clock_ctrl(enum consys_drv_type drv_type,
                             unsigned int bus_clock,
                             int status);
```

### A-die Top Clock Enable

```c
// Enable A-die top clock for a subsystem
int conninfra_adie_top_ck_en_on(enum consys_adie_ctl_type type);
int conninfra_adie_top_ck_en_off(enum consys_adie_ctl_type type);

enum connsys_adie_ctl_type {
    CONNSYS_ADIE_CTL_HOST_BT,
    CONNSYS_ADIE_CTL_HOST_FM,
    CONNSYS_ADIE_CTL_HOST_GPS,
    CONNSYS_ADIE_CTL_HOST_WIFI,
    CONNSYS_ADIE_CTL_HOST_CONNINFRA,
    CONNSYS_ADIE_CTL_FW_BT,
    CONNSYS_ADIE_CTL_FW_WIFI,
    CONNSYS_ADIE_CTL_MAX
};
```

### Subsystem Registration

```c
// Register subsystem driver callbacks
int conninfra_sub_drv_ops_register(enum consys_drv_type drv_type,
                                   struct sub_drv_ops_cb *cb);
int conninfra_sub_drv_ops_unregister(enum consys_drv_type drv_type);

// Callback structure
struct sub_drv_ops_cb {
    // Chip reset callbacks
    struct whole_chip_rst_cb rst_cb;

    // Pre-calibration callbacks
    struct pre_calibration_cb pre_cal_cb;

    // Thermal query callback
    int (*thermal_qry)(void);

    // UTC time change notification
    void (*time_change_notify)(void);
};

// Pre-calibration callbacks
struct pre_calibration_cb {
    int (*pwr_on_cb)(void);
    int (*do_cal_cb)(void);
    int (*get_cal_result_cb)(unsigned int *offset, unsigned int *size);
};
```

### Status and Debug

```c
// Check if registers are readable
int conninfra_reg_readable(void);

// Check for bus hang
// Returns: 0 = no hang, >0 = hang detected, CONNINFRA_ERR_RST_ONGOING = reset in progress
int conninfra_is_bus_hang(void);

// Get IC information
unsigned int conninfra_get_ic_info(enum connsys_ic_info_type type);

enum connsys_ic_info_type {
    CONNSYS_SOC_CHIPID,      // SoC chip ID
    CONNSYS_HW_VER,          // Hardware version
    CONNSYS_ADIE_CHIPID,     // A-die chip ID
    CONNSYS_GPS_ADIE_CHIPID, // GPS A-die chip ID
    CONNSYS_IC_INFO_MAX,
};
```

---

## Error Codes

```c
#define CONNINFRA_ERR_RST_ONGOING        -0x7788  // Reset in progress
#define CONNINFRA_ERR_WAKEUP_FAIL        -0x5566  // Wakeup failed

#define CONNINFRA_POWER_ON_D_DIE_FAIL    -0x1111  // D-die power on failed
#define CONNINFRA_POWER_ON_A_DIE_FAIL    -0x2222  // A-die power on failed
#define CONNINFRA_POWER_ON_CONFIG_FAIL   -0x3333  // Configuration failed
```

---

## Module Structure

### Directory Layout

```
conninfra/
├── include/
│   ├── conninfra.h              # Main API header (connv2)
│   └── connv3.h                 # ConnV3 API header
│
├── adaptor/
│   ├── conninfra_dev.c          # Device registration
│   ├── conn_kern_adaptor.c      # Kernel adapter
│   ├── connsyslog/              # System logging
│   └── coredump/                # Coredump handling
│
├── base/
│   ├── osal.c                   # OS abstraction layer
│   ├── msg_thread.c             # Message thread handling
│   └── ring.c                   # Ring buffer utilities
│
├── conf/
│   └── conninfra_conf.c         # Configuration management
│
└── conn_drv/
    ├── connv2/                  # ConnV2 (older SoCs)
    │   ├── core/
    │   │   └── conninfra_core.c # Core functionality
    │   ├── src/
    │   │   ├── conninfra.c      # Main implementation
    │   │   └── connv2_drv.c     # Driver registration
    │   ├── platform/
    │   │   ├── consys_hw.c      # Hardware abstraction
    │   │   ├── emi_mng.c        # EMI management
    │   │   ├── pmic_mng.c       # PMIC management
    │   │   ├── clock_mng.c      # Clock management
    │   │   ├── mt6877/          # MT6877 platform
    │   │   ├── mt6885/          # MT6885 platform
    │   │   ├── mt6893/          # MT6893 platform
    │   │   ├── mt6899/          # MT6899 platform
    │   │   └── mt6991/          # MT6991 platform
    │   ├── drv_init/
    │   │   ├── wlan_drv_init.c  # WLAN init hooks
    │   │   ├── bluetooth_drv_init.c
    │   │   ├── gps_drv_init.c
    │   │   └── fm_drv_init.c
    │   └── debug_utility/
    │       ├── connsyslog/      # System logging
    │       └── coredump/        # Coredump collection
    │
    └── connv3/                  # ConnV3 (newer SoCs)
        ├── core/
        │   └── connv3_core.c
        ├── src/
        │   ├── connv3.c
        │   └── connv3_drv.c
        ├── platform/
        │   ├── connv3_hw.c
        │   ├── connv3_pmic_mng.c
        │   ├── mt6639/          # MT6639 (MT7927)
        │   ├── mt6653/          # MT6653
        │   └── mt6991/          # MT6991
        └── debug_utility/
            ├── connsyslog/
            └── coredump/
```

### Platform Versions

| Version | Platforms | Description |
|---------|-----------|-------------|
| **connv2** | MT6877, MT6885, MT6893, MT6899, MT6991 | Older connectivity architecture |
| **connv3** | MT6639, MT6653, MT6991 | Newer architecture (includes MT7927 support) |

---

## Platform-Specific Files

### ConnV3 Platform for MT6639 (MT7927)

Located at `conn_drv/connv3/platform/mt6639/`:

```
mt6639/
└── mt6639_dbg.c    # MT6639-specific debug functions
```

The MT6639 platform in connv3 provides support for the MT7927 WiFi chip.

### Platform Operations

Each platform implements:

```c
struct consys_hw_ops_struct {
    // Power sequence
    int (*consys_plt_hw_power_on)(void);
    int (*consys_plt_hw_power_off)(void);

    // Clock control
    int (*consys_plt_clock_buffer_ctrl)(unsigned int enable);

    // PMIC control
    int (*consys_plt_pmic_ctrl)(unsigned int enable);

    // A-die control
    int (*consys_plt_adie_type_cfg)(void);

    // EMI setup
    int (*consys_plt_emi_mpu_set_region_protection)(void);

    // ... many more platform-specific operations
};
```

---

## How WLAN Uses conninfra

### Initialization Sequence

```
1. WLAN driver loads
   │
2. Register with conninfra
   │  conninfra_sub_drv_ops_register(CONNDRV_TYPE_WIFI, &ops)
   │
3. Power on request
   │  conninfra_pwr_on(CONNDRV_TYPE_WIFI)
   │  ├── PMIC enables power rails
   │  ├── Clock enables
   │  ├── D-die power on
   │  └── A-die power on
   │
4. Get EMI addresses
   │  conninfra_get_emi_phy_addr(CONNSYS_EMI_FW, &base, &size)
   │
5. RF calibration via SPI
   │  conninfra_spi_read/write(SYS_SPI_WF, ...)
   │
6. Normal operation
   │
7. Power off request
   conninfra_pwr_off(CONNDRV_TYPE_WIFI)
```

### Reset Handling

```
1. Error detected in WLAN driver
   │
2. Trigger reset
   │  conninfra_trigger_whole_chip_rst(CONNDRV_TYPE_WIFI, "reason")
   │
3. conninfra calls pre_whole_chip_rst for all subsystems
   │  ├── WiFi pre-reset callback
   │  ├── BT pre-reset callback
   │  └── GPS pre-reset callback
   │
4. Hardware reset performed
   │
5. conninfra calls post_whole_chip_rst for all subsystems
   │  ├── WiFi post-reset callback
   │  ├── BT post-reset callback
   │  └── GPS post-reset callback
   │
6. Subsystems re-initialize
```

---

## Implications for Standalone MT7927

### The Problem

The conninfra module is **deeply integrated with MediaTek SoC internals**:

1. **PMIC Dependencies**: Requires MediaTek PMIC drivers for power control
2. **Platform Device Tree**: Expects specific device tree bindings
3. **EMI Memory**: Uses SoC-specific EMI memory regions
4. **Clock Framework**: Tied to MediaTek clock controller
5. **Pinctrl**: Uses MediaTek pinctrl subsystem

### For Non-MediaTek Systems

A standalone PCIe MT7927 card on a non-MediaTek system (e.g., x86, other ARM SoCs) **cannot use conninfra directly**.

### Options

#### Option 1: Stub the APIs

Create minimal stub implementations:

```c
// Stub conninfra for standalone PCIe
int conninfra_pwr_on(enum consys_drv_type drv_type) {
    // PCIe device is always "powered on" when enumerated
    return 0;
}

int conninfra_pwr_off(enum consys_drv_type drv_type) {
    return 0;
}

void conninfra_get_emi_phy_addr(...) {
    // Not applicable for PCIe - firmware loaded via DMA
    *base = 0;
    *size = 0;
}

int conninfra_spi_read(...) {
    // Direct register access via PCIe BAR instead
    return -ENOTSUP;
}
```

#### Option 2: Use Upstream mt76 Driver

The mainline Linux kernel `mt76` driver (`reference_mt76_upstream/mt7925/`):

- **No conninfra dependency**
- Self-contained PCIe driver
- Standard Linux wireless stack integration
- Actively maintained upstream

#### Option 3: Significant Refactoring

Remove conninfra dependencies from gen4m driver:

- Replace EMI with standard DMA allocation
- Replace RF SPI with direct register access
- Implement power management via standard PCIe PM
- Significant effort required

---

## Key Source Files

| File | Purpose |
|------|---------|
| `include/conninfra.h` | Main API header |
| `conn_drv/connv2/src/conninfra.c` | ConnV2 implementation |
| `conn_drv/connv3/src/connv3.c` | ConnV3 implementation |
| `conn_drv/connv2/core/conninfra_core.c` | Core logic and state machine |
| `conn_drv/connv2/platform/consys_hw.c` | Hardware abstraction |
| `conn_drv/connv2/platform/emi_mng.c` | EMI memory management |
| `conn_drv/connv2/platform/pmic_mng.c` | PMIC control |
| `conn_drv/connv2/drv_init/wlan_drv_init.c` | WLAN init hooks |

---

## Conclusion

The `conninfra` module is a MediaTek-specific connectivity infrastructure layer that:

1. **Required** for the proprietary gen4m WLAN driver on MediaTek SoCs
2. **Not portable** to non-MediaTek platforms without significant work
3. **Manages shared resources** between WiFi, BT, GPS, and FM radios
4. **Provides power sequencing** that the WLAN driver depends on

For standalone MT7927 PCIe support on non-MediaTek systems, the **upstream mt76 driver** is the recommended approach as it doesn't require conninfra.
