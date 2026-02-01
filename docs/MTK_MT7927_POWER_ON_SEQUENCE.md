# MT7927 Power-On Sequence Analysis

This document traces the complete power-on sequence for the MT7927 WiFi adapter in the MediaTek proprietary driver. MT7927 uses the MT6639/ConnV3 path.

## Overview

The power-on sequence spans multiple layers:

1. **WLAN Driver** (`gen4m/`) - Entry point and coordination
2. **ConnV3** (`conninfra/conn_drv/connv3/`) - Power management framework
3. **Platform** - PMIC and GPIO control
4. **Hardware** - Actual power rails and clocks

```
┌─────────────────────────────────────────────────────────────────┐
│                        POWER-ON FLOW                             │
├─────────────────────────────────────────────────────────────────┤
│                                                                  │
│  START: Module Load / PCIe Probe                                │
│              ↓                                                   │
│  ┌──────────────────────────────────┐                           │
│  │ PHASE 1: WLAN Driver Entry       │  gl_init.c                │
│  │   wlanFuncOn()                   │                           │
│  │     └→ connsys_power_on()        │                           │
│  └──────────────────────────────────┘                           │
│              ↓                                                   │
│  ┌──────────────────────────────────┐                           │
│  │ PHASE 2: ConnV3 Gate Checks      │  connv3.c                 │
│  │   connv3_pwr_on()                │                           │
│  │     ├→ Reset check               │                           │
│  │     ├→ FMD mode check            │                           │
│  │     └→ Pre-cal blocking          │                           │
│  └──────────────────────────────────┘                           │
│              ↓                                                   │
│  ┌──────────────────────────────────┐                           │
│  │ PHASE 3: Core Power-On           │  connv3_core.c            │
│  │   opfunc_power_on_internal()     │                           │
│  │     ├→ Status validation         │                           │
│  │     ├→ Pre-power-on callbacks    │                           │
│  │     └→ connv3_hw_pwr_on()        │                           │
│  └──────────────────────────────────┘                           │
│              ↓                                                   │
│  ┌──────────────────────────────────┐                           │
│  │ PHASE 4: Hardware Power-On       │  connv3_hw.c              │
│  │   connv3_hw_pwr_on()             │                           │
│  │     ├→ VSEL control              │                           │
│  │     ├→ PMIC enable               │                           │
│  │     ├→ Pinctrl setup             │                           │
│  │     └→ Antenna power             │                           │
│  └──────────────────────────────────┘                           │
│              ↓                                                   │
│  ┌──────────────────────────────────┐                           │
│  │ PHASE 5: PMIC Sequencing         │  connv3_pmic_mng.c        │
│  │   Platform-specific PMIC ops     │  mt6991_pmic.c            │
│  │     ├→ PMIC_EN GPIO high         │                           │
│  │     ├→ 30ms delay                │                           │
│  │     ├→ 20ms delay                │                           │
│  │     └→ Enable FAULTB IRQ         │                           │
│  └──────────────────────────────────┘                           │
│              ↓                                                   │
│  ┌──────────────────────────────────┐                           │
│  │ PHASE 6: Power-On Done           │  connv3_core.c            │
│  │   opfunc_power_on_done()         │                           │
│  │     ├→ Pinctrl finalize          │                           │
│  │     ├→ Status → DRV_STS_POWER_ON │                           │
│  │     └→ Notify SCP (optional)     │                           │
│  └──────────────────────────────────┘                           │
│              ↓                                                   │
│  ┌──────────────────────────────────┐                           │
│  │ PHASE 7: WLAN Post-Power         │  gl_init.c, pcie.c        │
│  │   wlanFuncOnImpl()               │                           │
│  │     ├→ PCIe port probe           │                           │
│  │     ├→ Device initialization     │                           │
│  │     └→ connsys_power_done()      │                           │
│  └──────────────────────────────────┘                           │
│              ↓                                                   │
│  END: Hardware Ready (DRV_STS_POWER_ON)                         │
│                                                                  │
└─────────────────────────────────────────────────────────────────┘
```

---

## Phase 1: PCIe Probe Entry

### File Locations

| File | Path |
|------|------|
| PCIe Driver | `gen4m/os/linux/hif/pcie/pcie.c` |
| GL Init | `gen4m/os/linux/gl_init.c` |

### Entry Point: `mtk_pci_probe()`

**Location:** `pcie.c:1639-1746`

```c
static int mtk_pci_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
    // Line 1651: Enable PCI device
    ret = pcim_enable_device(pdev);

    // Line 1659: Map BAR0 memory region
    ret = pcim_iomap_regions(pdev, BIT(0), pci_name(pdev));

    // Line 1683: Set PCI bus master
    pci_set_master(pdev);

    // Line 1685: Setup MSI/MSI-X interrupts
    mtk_pcie_setup_msi(pdev, prGlueInfo);

    // Line 1689: Set DMA mask
    dma_set_mask(&pdev->dev, DMA_BIT_MASK(32));

    // Line 1710: Initialize EMI memory
    emi_mem_init(prChipInfo, pdev);

    // Line 1713: Call WLAN probe
    pfWlanProbe(pdev, driver_data);
}
```

### Device ID Matching

```c
// pcie.c:104-105
#define NIC7927_PCIe_DEVICE_ID 0x7927
#define NIC7925_PCIe_DEVICE_ID 0x7925

// pcie.c:170-171 - MT7927 uses MT6639 driver data
{ PCI_DEVICE(MTK_PCI_VENDOR_ID, NIC7927_PCIe_DEVICE_ID),
  .driver_data = (kernel_ulong_t)&mt66xx_driver_data_mt6639 },
```

---

## Phase 2: WLAN Function On

### Entry Point: `wlanFuncOn()`

**Location:** `gl_init.c:9357-9384`

```c
int wlanFuncOn(void)
{
    // Line 9361: Request power from conninfra
    ret = connsys_power_on();
    if (ret) {
        // Power-on failed
        return ret;
    }

    // Line 9365: Initialize WLAN function
    ret = wlanFuncOnImpl();

    // Line 9372: Notify power-on complete
    connsys_power_done();

    return ret;
}
```

### `connsys_power_on()` Implementation

**Location:** `gl_init.c:2939`

```c
static int connsys_power_on(void)
{
    return conninfra_pwr_on(CONNDRV_TYPE_WIFI);
}
```

This calls into the ConnV3 layer (for MT7927/MT6639).

---

## Phase 3: ConnV3 Power-On

### Entry Point: `connv3_pwr_on()`

**Location:** `conninfra/conn_drv/connv3/src/connv3.c:72-86`

```c
int connv3_pwr_on(enum connv3_drv_type drv_type)
{
    // Line 75: Check if reset is ongoing
    if (connv3_core_is_rst_locking()) {
        return CONNV3_ERR_RST_ONGOING;
    }

    // Line 79: Check if FMD mode is locked
    if (connv3_core_is_fmd_locking()) {
        return CONNV3_ERR_FMD_ONGOING;
    }

    // Line 83: Block if pre-calibration in progress
    connv3_core_pre_cal_blocking();

    // Line 85: Call core power-on
    return connv3_core_power_on(drv_type);
}
```

### Gate Checks

| Check | Purpose | Error Code |
|-------|---------|------------|
| Reset locking | Prevent power-on during chip reset | `CONNV3_ERR_RST_ONGOING` |
| FMD locking | Prevent power-on during FMD mode | `CONNV3_ERR_FMD_ONGOING` |
| Pre-cal blocking | Wait for calibration to complete | (blocks) |

---

## Phase 4: Core Power-On Internal

### Entry Point: `opfunc_power_on_internal()`

**Location:** `conninfra/conn_drv/connv3/core/connv3_core.c:258-410`

### Step 4.1: Status Validation (Lines 258-300)

```c
static int opfunc_power_on_internal(struct msg_op_data *op)
{
    // Line 268-271: Validate driver type
    if (drv_type >= CONNV3_DRV_TYPE_MAX) {
        return -EINVAL;
    }

    // Line 274-279: Validate driver status
    drv_inst = &g_connv3_ctx.drv_inst[drv_type];
    if (drv_inst->drv_status != DRV_STS_POWER_OFF) {
        return -EALREADY;
    }

    // Line 281-285: Acquire core lock
    ret = osal_lock_sleepable_lock(&g_connv3_ctx.core_lock);

    // Line 295-300: Check platform status
    ret = connv3_hw_check_status();
    if (ret != CONNV3_PLT_STATE_READY) {
        return CONNV3_ERR_CLOCK_NOT_READY;
    }
}
```

### Step 4.2: Power Recycle (Lines 308-319)

```c
    // If system was in power-off state, ensure clean state
    if (core_status == DRV_STS_POWER_OFF) {
        // Line 312: Force power off first (cleanup)
        connv3_hw_pwr_off(0, CONNV3_DRV_TYPE_MAX, NULL);
    }
```

### Step 4.3: Pre-Power-On Callbacks (Lines 321-345)

```c
    // Line 323: Initialize semaphore for synchronization
    sema_init(&pre_pwr_sema, 1);

    // Lines 325-329: Send PRE_PWR_ON to all subsystems
    for (i = 0; i < CONNV3_DRV_TYPE_MAX; i++) {
        msg_thread_send_1(&drv_inst->msg_ctx,
                          CONNV3_SUBDRV_OPID_PRE_PWR_ON, i);
    }

    // Lines 332-345: Wait for all subsystems to respond
    ret = down_timeout(&pre_pwr_sema, CONNV3_RESET_TIMEOUT);
```

### Step 4.4: Hardware Power-On (Lines 348-351)

```c
    // Line 349: Acquire wake lock
    connv3_core_wake_lock_get();

    // Line 350: Perform hardware power-on
    ret = connv3_hw_pwr_on(curr_status, drv_type);

    // Line 351: Release wake lock
    connv3_core_wake_lock_put();
```

---

## Phase 5: Hardware Power-On

### Entry Point: `connv3_hw_pwr_on()`

**Location:** `conninfra/conn_drv/connv3/platform/connv3_hw.c:201-224`

```c
int connv3_hw_pwr_on(unsigned int curr_status, enum connv3_drv_type drv_type)
{
    int on_radio = opfunc_get_on_radio(drv_type);

    // First-time power-on (curr_status == 0)
    if (curr_status == 0) {
        // Line 206: VSEL control (voltage selector)
        connv3_pmic_mng_vsel_ctrl(1);

        // Line 210: Common power control (PMIC enable)
        connv3_pmic_mng_common_power_ctrl(1);

        // Line 214: Pinctrl pre-setup (GPIO configuration)
        connv3_pinctrl_mng_setup_pre();
    }

    // Line 219: Antenna/radio-specific power
    connv3_pmic_mng_antenna_power_ctrl(on_radio, 1);

    return 0;
}
```

### Hardware Operations

| Function | Purpose |
|----------|---------|
| `connv3_pmic_mng_vsel_ctrl(1)` | Set voltage selector |
| `connv3_pmic_mng_common_power_ctrl(1)` | Enable PMIC power rails |
| `connv3_pinctrl_mng_setup_pre()` | Configure GPIO pins |
| `connv3_pmic_mng_antenna_power_ctrl(radio, 1)` | Enable WiFi radio power |

---

## Phase 6: PMIC Sequencing

### PMIC Manager Layer

**Location:** `conninfra/conn_drv/connv3/platform/connv3_pmic_mng.c`

```c
// Line 101-105
int connv3_pmic_mng_vsel_ctrl(unsigned int enable)
{
    if (g_connv3_platform_pmic_ops->pmic_vsel_ctrl)
        return g_connv3_platform_pmic_ops->pmic_vsel_ctrl(enable);
    return 0;
}

// Line 91-95
int connv3_pmic_mng_common_power_ctrl(unsigned int enable)
{
    if (g_connv3_platform_pmic_ops->pmic_common_power_ctrl)
        return g_connv3_platform_pmic_ops->pmic_common_power_ctrl(enable);
    return 0;
}

// Line 137-142
int connv3_pmic_mng_antenna_power_ctrl(unsigned int radio, unsigned int enable)
{
    if (g_connv3_platform_pmic_ops->pmic_antenna_power_ctrl)
        return g_connv3_platform_pmic_ops->pmic_antenna_power_ctrl(radio, enable);
    return 0;
}
```

### Platform-Specific Implementation (MT6991 Example)

**Location:** `conninfra/conn_drv/connv3/platform/mt6991/mt6991_pmic.c:127-197`

```c
static int connv3_plt_pmic_common_power_ctrl_mt6991(unsigned int enable)
{
    if (enable == 1) {
        // Lines 135-144: Set PMIC_EN pin high
        state = pinctrl_lookup_state(pctrl, "connsys-pin-pmic-en-set");
        mdelay(30);  // Line 138: Wait 30ms for PMIC stable
        pinctrl_select_state(pctrl, state);

        mdelay(20);  // Line 145: Additional 20ms settle time

        // Lines 147-155: Enable FAULTB interrupt
        state = pinctrl_lookup_state(pctrl, "connsys-pin-pmic-faultb-enable");
        pinctrl_select_state(pctrl, state);

        // Line 157: Clear spurious exception flag
        connv3_hw_pwr_on_sprs_excp_clr();
    }
    else {
        // Power-off sequence
        mdelay(210);  // Line 162: Wait for FW shutdown

        // Lines 167-175: Disable FAULTB
        state = pinctrl_lookup_state(pctrl, "connsys-pin-pmic-faultb-disable");
        pinctrl_select_state(pctrl, state);

        mdelay(20);  // Line 184

        // Lines 177-189: Set PMIC_EN pin low
        state = pinctrl_lookup_state(pctrl, "connsys-pin-pmic-en-clr");
        pinctrl_select_state(pctrl, state);
    }
    return 0;
}
```

### Timing Diagram

```
Power-On Timeline:
──────────────────────────────────────────────────────────────────

T=0ms     PMIC_EN GPIO → HIGH
          │
          ├─────────── 30ms delay (PMIC stabilization) ───────────
          │
T=30ms    Pinctrl state applied
          │
          ├─────────── 20ms delay (settle time) ──────────────────
          │
T=50ms    FAULTB interrupt enabled
          Clear spurious exception flag
          │
          └─── HARDWARE READY ───


Power-Off Timeline:
──────────────────────────────────────────────────────────────────

T=0ms     Shutdown initiated
          │
          ├─────────── 210ms delay (FW shutdown) ─────────────────
          │
T=210ms   FAULTB interrupt disabled
          │
          ├─────────── 20ms delay ────────────────────────────────
          │
T=230ms   PMIC_EN GPIO → LOW
          │
          └─── POWER OFF ───
```

---

## Phase 7: Power-On Done

### Entry Point: `opfunc_power_on_done()`

**Location:** `conninfra/conn_drv/connv3/core/connv3_core.c:420-462`

```c
static int opfunc_power_on_done(struct msg_op_data *op)
{
    // Line 432: Validate current state
    if (drv_inst->drv_status != DRV_STS_PRE_POWER_ON) {
        return -EINVAL;
    }

    // Line 438: Acquire lock
    osal_lock_sleepable_lock(&g_connv3_ctx.core_lock);

    // Line 447: Finalize GPIO configuration
    connv3_hw_pwr_on_done(drv_type);
        // → connv3_pinctrl_mng_setup_done() [connv3_hw.c:231]

    // Line 451: Update driver status to POWER_ON
    drv_inst->drv_status = DRV_STS_POWER_ON;

    // Lines 453-459: Notify CONAP-SCP (Android platforms)
    #if defined(CONNINFRA_PLAT_ALPS) && CONNINFRA_PLAT_ALPS
    connectivity_export_conap_scp_state_change(conn_wifi_on);
    #endif
}
```

---

## Phase 8: WLAN Post-Power Operations

### `wlanFuncOnImpl()`

**Location:** `gl_init.c`

After `connsys_power_on()` returns successfully:

```c
wlanFuncOnImpl()
{
    // glBusFuncOn() [gl_init.c:2927-3006]
    └─ mtk_pcie_remove_port(0)     // Clean up old port
    └─ mtk_pcie_probe_port(0)      // Probe new port
    └─ pci_register_driver()       // Register PCI driver
        └─ mtk_pci_probe()         // Called for device init
            └─ Firmware download
            └─ MAC initialization
            └─ Interface registration
}

// After wlanFuncOnImpl() returns:
connsys_power_done()
{
    conninfra_pwr_on_done(CONNDRV_TYPE_WIFI);
}
```

---

## State Machine

### Driver Status Transitions

```
┌─────────────────────────────────────────────────────────┐
│                                                          │
│    DRV_STS_POWER_OFF                                    │
│           │                                              │
│           │  connv3_pwr_on()                            │
│           ▼                                              │
│    ┌──────────────────┐                                 │
│    │ Lock acquired    │                                 │
│    │ Status validated │                                 │
│    │ Platform checked │                                 │
│    └────────┬─────────┘                                 │
│             │                                            │
│             │  Pre-power-on callbacks                   │
│             ▼                                            │
│    ┌──────────────────┐                                 │
│    │ PMIC enable      │                                 │
│    │ GPIO setup       │                                 │
│    │ Clock enable     │                                 │
│    └────────┬─────────┘                                 │
│             │                                            │
│             ▼                                            │
│    DRV_STS_PRE_POWER_ON                                 │
│             │                                            │
│             │  connv3_pwr_on_done()                     │
│             ▼                                            │
│    ┌──────────────────┐                                 │
│    │ Pinctrl finalize │                                 │
│    │ Status update    │                                 │
│    │ SCP notify       │                                 │
│    └────────┬─────────┘                                 │
│             │                                            │
│             ▼                                            │
│    DRV_STS_POWER_ON  ◄──── READY FOR WLAN OPERATIONS   │
│                                                          │
└─────────────────────────────────────────────────────────┘
```

### Status Definitions

| Status | Value | Description |
|--------|-------|-------------|
| `DRV_STS_POWER_OFF` | 0 | Driver not powered |
| `DRV_STS_PRE_POWER_ON` | 1 | Power sequence in progress |
| `DRV_STS_POWER_ON` | 2 | Fully powered and ready |

---

## Key Data Structures

### ConnV3 Context

**Location:** `connv3_core.c:106`

```c
struct connv3_ctx g_connv3_ctx = {
    .drv_inst[CONNV3_DRV_TYPE_MAX],  // Driver instances
    .core_status,                     // Overall power state
    .msg_ctx,                         // Message thread
    .core_lock,                       // Power state lock
};
```

### Platform PMIC Operations

**Location:** `connv3_pmic_mng.c`

```c
struct connv3_platform_pmic_ops {
    int (*pmic_vsel_ctrl)(unsigned int enable);
    int (*pmic_common_power_ctrl)(unsigned int enable);
    int (*pmic_antenna_power_ctrl)(unsigned int radio, unsigned int enable);
};
```

---

## Timing Summary

| Phase | Duration | Cumulative |
|-------|----------|------------|
| PCIe enumeration | ~10ms | T=10ms |
| ConnV3 gate checks | <1ms | T=10ms |
| Pre-power-on callbacks | ~5ms | T=15ms |
| PMIC_EN set high | - | T=15ms |
| PMIC stabilization | 30ms | T=45ms |
| Settle time | 20ms | T=65ms |
| FAULTB enable | <1ms | T=65ms |
| Pinctrl finalize | <1ms | T=65ms |
| **Total Power-On** | | **~65ms** |

---

## Error Handling

### Error Codes

| Code | Constant | Meaning |
|------|----------|---------|
| -0x7788 | `CONNV3_ERR_RST_ONGOING` | Reset in progress |
| -0x5566 | `CONNV3_ERR_WAKEUP_FAIL` | Wakeup failed |
| -0x1111 | `CONNV3_POWER_ON_D_DIE_FAIL` | D-die power failed |
| -0x2222 | `CONNV3_POWER_ON_A_DIE_FAIL` | A-die power failed |
| - | `CONNV3_ERR_CLOCK_NOT_READY` | Platform not ready |

### Error Recovery

| Phase | Failure | Recovery |
|-------|---------|----------|
| Phase 3 | Platform not ready | Return error, retry later |
| Phase 4 | PMIC failure | Unwind via `connv3_hw_pwr_off()` |
| Phase 5 | GPIO failure | Unwind, report error |
| Phase 8 | PCIe probe failure | Trigger chip reset |

---

## Implications for Standalone PCIe MT7927

### What's Different for Non-MediaTek Systems

On a standard x86 or non-MediaTek ARM system with MT7927 as a PCIe card:

1. **No PMIC control needed** - PCIe slot provides power
2. **No GPIO sequencing** - Hardware handles power rails
3. **No conninfra** - Not available/applicable
4. **PCIe provides power** - Device powered when slot is powered

### Minimal Power-On for Standalone

```c
// Standalone PCIe MT7927 power-on (simplified)
static int mt7927_standalone_probe(struct pci_dev *pdev)
{
    // 1. Enable PCI device (this powers the card)
    pci_enable_device(pdev);

    // 2. Set bus master
    pci_set_master(pdev);

    // 3. Map BAR0
    mmio_base = pci_iomap(pdev, 0, 0);

    // 4. Device is now powered and accessible
    // No PMIC, GPIO, or conninfra needed

    // 5. Continue with firmware download, MAC init, etc.
    return 0;
}
```

### Components to Skip/Stub

| Component | Action |
|-----------|--------|
| `connsys_power_on()` | Return 0 (success) |
| `conninfra_pwr_on()` | Stub - return 0 |
| `connv3_hw_pwr_on()` | Skip entirely |
| PMIC control | Not needed |
| GPIO/Pinctrl | Not needed |
| `connsys_power_done()` | Stub - return 0 |

---

## File Reference

| Phase | File | Key Function | Line |
|-------|------|--------------|------|
| 1 | `pcie.c` | `mtk_pci_probe()` | 1639 |
| 2 | `gl_init.c` | `wlanFuncOn()` | 9357 |
| 2 | `gl_init.c` | `connsys_power_on()` | 2939 |
| 3 | `connv3.c` | `connv3_pwr_on()` | 72 |
| 4 | `connv3_core.c` | `opfunc_power_on_internal()` | 258 |
| 5 | `connv3_hw.c` | `connv3_hw_pwr_on()` | 201 |
| 6 | `connv3_pmic_mng.c` | `connv3_pmic_mng_common_power_ctrl()` | 91 |
| 6 | `mt6991_pmic.c` | `connv3_plt_pmic_common_power_ctrl_mt6991()` | 127 |
| 7 | `connv3_core.c` | `opfunc_power_on_done()` | 420 |
| 8 | `gl_init.c` | `wlanFuncOnImpl()` | 9365 |

---

## Conclusion

The MT7927 power-on sequence in the MediaTek proprietary driver:

1. **Starts** at `mtk_pci_probe()` in `pcie.c:1639`
2. **Flows through** ConnV3 power management (`connv3_pwr_on()`)
3. **Performs** PMIC enable, GPIO setup, and clock enable (~65ms)
4. **Ends** at `DRV_STS_POWER_ON` status, ready for WLAN operations

For standalone PCIe operation, most of this complexity can be bypassed since the PCIe bus handles power delivery automatically.
