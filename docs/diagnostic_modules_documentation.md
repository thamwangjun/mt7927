# MT7927 Diagnostic Modules Documentation

This document provides documentation for MT7927 diagnostic modules located in `/diag/`. These modules are used to probe, test, and understand the MT7927 WiFi 7 chip's register layout, power management, and initialization sequences.

> **Phase 21 Cleanup Note**: 10 DMA-focused modules were removed because they used the wrong WFDMA address (BAR0+0x2000 instead of correct BAR0+0xd4000). The remaining 11 modules focus on power management, WFSYS reset, and general diagnostics which remain useful.

## Table of Contents

1. [Pre-Flight Checks](#pre-flight-checks)
2. [Safe/Read-Only Diagnostic Modules](#saferead-only-diagnostic-modules)
3. [Power Management & Initialization](#power-management--initialization)
4. [Full Initialization Sequences](#full-initialization-sequences)

---

## Pre-Flight Checks

### mt7927_fw_precheck.c - Firmware Pre-Load Diagnostic

**Purpose:** Comprehensive diagnostic that validates all assumptions documented in `docs/` that are required for successful firmware loading onto the MT7927 device. This should be run BEFORE attempting firmware loading to ensure the device is in the expected state.

**Key Functions:**
- `check_chip_identity()`: Validates Chip ID = 0x00511163
- `check_bar_config()`: Validates BAR0 = 2MB, BAR2 = 32KB
- `check_ring_config()`: Validates MT6639 sparse ring layout (0,1,2,15,16)
- `check_power_management()`: Reads LPCTL state and ownership bits
- `check_wfsys_state()`: Checks WFSYS reset and INIT_DONE status
- `check_mcu_state()`: **CRITICAL** - Checks MCU IDLE state (0x1D1E)
- `check_conninfra_state()`: Validates CONN_INFRA version
- `check_aspm_state()`: Reports ASPM L0s/L1 configuration
- `check_wfdma_state()`: Reports WFDMA DMA engine state

**Assumptions Validated (from docs/):**

| Assumption | Reference | Expected Value |
|------------|-----------|----------------|
| Chip ID | CLAUDE.md, ROADMAP.md | 0x00511163 |
| BAR0 Size | HARDWARE.md | 2MB (0x200000) |
| BAR2 Size | HARDWARE.md | 32KB (0x8000) |
| MCU IDLE State | ZOUYONGHAO_ANALYSIS.md | 0x1D1E at 0x81021604 |
| CONN_INFRA Version | ZOUYONGHAO_ANALYSIS.md | 0x03010002 |
| Ring 15 | MT6639_ANALYSIS.md | MCU_WM commands |
| Ring 16 | MT6639_ANALYSIS.md | FWDL firmware download |

**Safety Considerations:**
- ✅ **SAFE** - Only reads registers, no writes
- ✅ No system crash risk
- ✅ Should be run FIRST before any firmware loading attempts

**Usage:**
```bash
make diag
sudo insmod diag/mt7927_fw_precheck.ko
sudo dmesg | tail -80
sudo rmmod mt7927_fw_precheck
```

**Key Findings:**
- MCU must be in IDLE state (0x1D1E) before firmware can be loaded
- If MCU is not IDLE, WFSYS reset sequence is required
- ASPM L0s should be disabled before DMA operations

---

## Safe/Read-Only Diagnostic Modules

### mt7927_diag.c - Minimal Safe Diagnostic

**Purpose:** Ultra-minimal diagnostic that only reads from BAR2 registers known to be safe. Does NOT scan BAR0 or access potentially dangerous regions.

**Key Functions:**
- `dump_safe_registers()`: Reads and displays safe BAR2 registers including chip ID, HW revision, FW_STATUS, WPDMA config.

**Registers/Memory Accessed:**
- **BAR2 only:**
  - `0x000`: Chip ID
  - `0x004`: HW Revision
  - `0x200`: HOST_INT_STA (FW_STATUS)
  - `0x204`: HOST_INT_ENA
  - `0x208`: WPDMA_GLO_CFG

**Expected Output:**
- Chip ID: `0x00511163` (MT7927)
- FW_STATUS: `0xffff10f1` indicates pre-init state (chip locked)
- FW_STATUS: `0x00000001` indicates MCU ready

**Safety Considerations:**
- ✅ **SAFE** - Only reads BAR2, no writes
- ✅ No system crash risk

---

### mt7927_readonly_scan.c - Read-Only BAR0 Scan

**Purpose:** SAFE read-only scan of BAR0 to find non-zero regions. Performs NO writes.

**Key Functions:**
- `find_ring_patterns()`: Scans BAR0 looking for register patterns
- `scan_nonzero_regions()`: Samples key BAR0 regions at various offsets
- `compare_bars()`: Compares BAR0 vs BAR2 at equivalent offsets

**Safety Considerations:**
- ✅ **SAFE** - Read-only, no writes
- ✅ No system crash risk

---

### mt7927_scan_readonly.c - Read-Only Register Scanner

**Purpose:** SAFE read-only scanner that maps register layout by scanning BAR0 and BAR2 at various offsets. Performs bounds checking.

**Key Functions:**
- `dump_bar2_reference()`: Dumps known-working BAR2 registers as reference
- `scan_bar0_regions()`: Scans multiple BAR0 offsets
- `check_lpctl()`: Reads power control state registers

**Registers/Memory Accessed:**
- **Power Control:** `BAR0+0xe0010` (LPCTL), `BAR0+0xf0140` (WFSYS_RST), `BAR0+0xe00f0` (CONN_MISC)

**Safety Considerations:**
- ✅ **SAFE** - Read-only with bounds checking
- ✅ No system crash risk

---

### mt7927_power_diag.c - Power State Diagnostic

**Purpose:** Carefully reads LPCTL register to understand power ownership state.

**Key Functions:**
- `mt7927_pwr_diag_probe()`: Main probe function that reads power state registers

**Registers/Memory Accessed:**
- **BAR2:** `0x000` (Chip ID), `0x200` (FW_STATUS)
- **BAR0:** `0xe0010` (LPCTL), `0xe00f0` (CONN_ON_MISC)

**Expected Output:**
- LPCTL register value with bit decode:
  - Bit 0: HOST_OWN
  - Bit 1: FW_OWN
  - Bit 2: Status bit
- Interpretation of ownership state

**Safety Considerations:**
- ✅ **SAFE** - Read-only
- ✅ No system crash risk

---

## Power Management & Initialization

### mt7927_power_unlock.c - Power Handshake Testing

**Purpose:** Tests if the power handshake sequence works. Follows mt7925 probe sequence.

**Key Functions:**
- `do_fw_pmctrl()`: Gives ownership to firmware (SET_OWN)
- `do_drv_pmctrl()`: Claims ownership for driver (CLR_OWN)
- `translate_addr()`: Address translation for CONN_INFRA region

**Registers/Memory Accessed:**
- **BAR0:** `0xe0010` (LPCTL via translation) - Power control

**Safety Considerations:**
- ⚠️ **MODERATE RISK** - Writes to power control registers
- ⚠️ May affect chip power state

**Key Findings:**
- Power handshake sequence: SET_OWN → wait → CLR_OWN → wait
- Address translation needed for CONN_INFRA region access

---

### mt7927_claim_host.c - Host Claim Testing

**Purpose:** Attempts to claim HOST_OWN by writing to LPCTL register.

**Key Functions:**
- `mt7927_claim_probe()`: Writes HOST_OWN bit and polls for state change

**Registers/Memory Accessed:**
- **BAR0:** `0xe0010` (LPCTL)
- **BAR2:** `0x200` (FW_STATUS), `0x208` (WPDMA_CFG)

**Safety Considerations:**
- ⚠️ **MODERATE RISK** - Writes to power control

---

### mt7927_correct_pmctrl.c - Power Control Correction

**Purpose:** Uses the CORRECT power control bits based on mt792x driver:
- `PCIE_LPCR_HOST_SET_OWN` (BIT 0) = Give ownership to FIRMWARE
- `PCIE_LPCR_HOST_CLR_OWN` (BIT 1) = Claim ownership for DRIVER
- `PCIE_LPCR_HOST_OWN_SYNC` (BIT 2) = Status bit

**Key Functions:**
- `claim_driver_own()`: Writes CLR_OWN and polls for OWN_SYNC to clear
- `print_state()`: Shows LPCTL, FW_STATUS state

**Registers/Memory Accessed:**
- **BAR0:** `0xe0010` (LPCTL)
- **BAR2:** `0x200` (FW_STATUS), `0x208` (WPDMA_CFG)

**Safety Considerations:**
- ⚠️ **MODERATE RISK** - Writes to power control
- ✅ Uses correct bit definitions from reference driver

**Key Findings:**
- CLR_OWN (BIT 1) is the correct way to claim driver ownership
- OWN_SYNC (BIT 2) is the status bit (1=FW owns, 0=driver owns)

---

### mt7927_disable_aspm.c - ASPM Disable Test

**Purpose:** Disables PCIe Active State Power Management before claiming HOST_OWN.

**Key Functions:**
- `disable_aspm()`: Disables ASPM on device and parent bridge
- `mt7927_aspm_probe()`: Disables ASPM then attempts to claim HOST_OWN

**Registers/Memory Accessed:**
- **PCIe Config Space:** Link Control register (ASPM bits)
- **BAR0:** `0xe0010` (LPCTL)

**Safety Considerations:**
- ⚠️ **MODERATE RISK** - Modifies PCIe power management
- ✅ ASPM changes are typically reversible

**Key Findings:**
- ASPM L0s should be disabled before chip initialization
- Parent bridge ASPM may also need disabling

---

### mt7927_wfsys_reset.c - WiFi Reset Testing

**Purpose:** Tries WFSYS reset sequence to bring chip out of power-down state. Based on `mt792x_wfsys_reset` from mt7925 driver.

**Key Functions:**
- `print_state()`: Shows LPCTL, FW_STATUS, WPDMA, WFSYS_RST, CONN_MISC state
- `mt7927_wfsys_probe()`: Performs WFSYS reset sequence then attempts to claim HOST_OWN

**Registers/Memory Accessed:**
- **BAR0:**
  - `0xf0140` (MT_WFSYS_SW_RST_B) - WFSYS reset control
  - `0xe0010` (MT_CONN_ON_LPCTL) - Power control
  - `0xe00f0` (MT_CONN_ON_MISC) - Connection misc
- **BAR2:** `0x200` (FW_STATUS), `0x208` (WPDMA_CFG)

**Safety Considerations:**
- ⚠️ **HIGH RISK** - Performs hardware reset
- ⚠️ Should only be run when chip is not in use

**Key Findings:**
- WFSYS reset sequence: Assert (clear EN) → wait → Deassert (set EN) → wait
- Reset may be needed to bring chip out of power-down
- HOST_OWN claim may succeed after reset

---

## Full Initialization Sequences

### mt7927_full_init.c - Full Initialization Test

**Purpose:** Based on mt7925 driver probe sequence:
1. Claim driver ownership (CLR_OWN)
2. WFSYS reset
3. Report state

**Key Functions:**
- `claim_driver_own()`: Claims driver ownership
- `wfsys_reset()`: Performs WFSYS reset
- `print_state()`: Shows chip state at each step

**Registers/Memory Accessed:**
- **BAR0:**
  - `0xe0010` (LPCTL)
  - `0xf0140` (WFSYS_SW_RST_B)
- **BAR2:** `0x200` (FW_STATUS), `0x208` (WPDMA_CFG)

**Safety Considerations:**
- ⚠️ **HIGH RISK** - Full initialization sequence
- ⚠️ Performs hardware reset
- ⚠️ Should only run when chip is not in use

**Key Findings:**
- Complete initialization sequence: Ownership → Reset
- Sequence matches mt7925 driver behavior

---

## Summary of Safety Levels

### ✅ SAFE (Read-Only)
- `mt7927_diag.c`
- `mt7927_readonly_scan.c`
- `mt7927_scan_readonly.c`
- `mt7927_power_diag.c`
- `mt7927_fw_precheck.c`

### ⚠️ MODERATE RISK (Limited Writes)
- `mt7927_power_unlock.c`
- `mt7927_claim_host.c`
- `mt7927_correct_pmctrl.c`
- `mt7927_disable_aspm.c`

### ⚠️ HIGH RISK (Hardware Reset)
- `mt7927_wfsys_reset.c`
- `mt7927_full_init.c`

---

## Key Information

1. **WFDMA HOST DMA Location:** `BAR0 + 0xd4000` (NOT 0x2000 which is MCU DMA)

2. **Power Control:** Correct sequence is:
   - `PCIE_LPCR_HOST_CLR_OWN` (BIT 1) to claim driver ownership
   - `PCIE_LPCR_HOST_OWN_SYNC` (BIT 2) is status (1=FW owns, 0=driver owns)

3. **Initialization Sequence:**
   - Disable ASPM L0s
   - Claim driver ownership
   - WFSYS reset

4. **Register Access:** BAR0 is the primary register window. BAR2 provides status/shadow registers.

5. **FW_STATUS:** `0xffff10f1` indicates pre-init state (chip locked), `0x00000001` indicates MCU ready.

---

## Removed Modules (Phase 21)

The following 10 modules were removed because they used the wrong WFDMA address (BAR0+0x2000 instead of BAR0+0xd4000):

- `mt7927_real_dma.c` - DMA testing at wrong address
- `mt7927_ring_test.c` - Ring writability tests at wrong address
- `mt7927_ring_scan_readonly.c` - Ring scanning at wrong address
- `mt7927_ring_scan_readwrite.c` - Ring scanning at wrong address
- `mt7927_bar0_dma.c` - DMA testing at wrong address
- `mt7927_dma_reset.c` - DMA reset at wrong address
- `mt7927_find_wfdma.c` - Scanned wrong offsets
- `mt7927_wfdma1_scan.c` - Checked wrong WFDMA addresses
- `mt7927_wide_scan.c` - Dangerous wide scanner, obsolete
- `mt7927_minimal_scan.c` - Scanned wrong region

---

## Building and Loading

All modules are kernel modules that can be built and loaded:

```bash
cd diag/
make
sudo insmod mt7927_<module_name>.ko
sudo dmesg | tail -50  # View output
sudo rmmod mt7927_<module_name>
```

Ensure the MT7927 device is not in use by other drivers before loading diagnostic modules.
