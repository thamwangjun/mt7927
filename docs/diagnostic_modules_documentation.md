# MT7927 Diagnostic Modules Documentation

This document provides comprehensive documentation for all MT7927 diagnostic modules located in `/diag/`. These modules are used to probe, test, and understand the MT7927 WiFi 7 chip's register layout, power management, DMA functionality, and initialization sequences.

> **⚠️ HISTORICAL NOTE (Phase 21)**: Many of these diagnostic modules were created when we incorrectly believed WFDMA HOST DMA0 was at BAR0+0x2000. The **correct address is BAR0+0xd4000** (chip address 0x7C024000). Modules that scan/write to 0x2000 were accessing the MCU DMA region, not the HOST DMA region needed for firmware loading. This explains why DMA tests at 0x2000 appeared to work but firmware loading never succeeded.

## Table of Contents

1. [Pre-Flight Checks](#pre-flight-checks)
2. [Safe/Read-Only Diagnostic Modules](#safe-read-only-diagnostic-modules)
3. [Power Management & Initialization](#power-management--initialization)
4. [DMA & Ring Testing](#dma--ring-testing)
5. [Full Initialization Sequences](#full-initialization-sequences)
6. [Wide Scanning (Dangerous)](#wide-scanning-dangerous)

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

**Registers/Memory Accessed:**
- **BAR0:** WFDMA registers at 0x2000, remap registers at 0x155024
- **BAR2:** Chip ID at 0x000, HW Rev at 0x004
- **Remapped:** LPCTL (0x7c060010), WFSYS_SW_RST_B (0x7c000140), MCU_ROMCODE_INDEX (0x81021604), MCU_STATUS (0x7c060204), CONN_ON_MISC (0x7c0600f0), CONNINFRA_VERSION (0x7C011000)

**Expected Output:**
```
╔══════════════════════════════════════════════════════════╗
║           MT7927 Firmware Pre-Load Check Summary         ║
╠══════════════════════════════════════════════════════════╣
║  PASSED:   X / Y                                          ║
║  FAILED:   X                                              ║
║  WARNINGS: X                                              ║
╠══════════════════════════════════════════════════════════╣
║  Key Requirements for Firmware Loading:                  ║
║  1. Chip ID = 0x00511163                                 ║
║  2. MCU in IDLE state (0x1D1E at 0x81021604)             ║
║  3. Use polling-based protocol (NO mailbox waits)        ║
║  4. Ring 15=MCU_WM, Ring 16=FWDL (MT6639 config)         ║
║  5. Disable ASPM L0s before DMA operations               ║
╚══════════════════════════════════════════════════════════╝
```

**Safety Considerations:**
- ✅ **SAFE** - Only reads registers, no writes
- ✅ No system crash risk
- ✅ Should be run FIRST before any firmware loading attempts

**Usage:**
```bash
# Build
make diag

# Load pre-check module
sudo insmod diag/mt7927_fw_precheck.ko
sudo dmesg | tail -80

# Review results, then unload
sudo rmmod mt7927_fw_precheck
```

**Key Findings:**
- MCU must be in IDLE state (0x1D1E) before firmware can be loaded
- If MCU is not IDLE, WFSYS reset sequence is required
- ASPM L0s should be disabled before DMA operations
- Per zouyonghao analysis, ASPM L1 being enabled is acceptable

---

## Safe/Read-Only Diagnostic Modules

### mt7927_diag.c - Minimal Safe Diagnostic

**Purpose:** Ultra-minimal diagnostic that only reads from BAR2 registers known to be safe. Does NOT scan BAR0 or access potentially dangerous regions.

**Key Functions:**
- `dump_safe_registers()`: Reads and displays safe BAR2 registers including chip ID, HW revision, FW_STATUS, WPDMA config, and ring registers.

**Registers/Memory Accessed:**
- **BAR2 only:**
  - `0x000`: Chip ID
  - `0x004`: HW Revision
  - `0x200`: HOST_INT_STA (FW_STATUS)
  - `0x204`: HOST_INT_ENA
  - `0x208`: WPDMA_GLO_CFG
  - `0x20c`: RST_DTX_PTR
  - `0x300`: TX0_BASE
  - `0x304`: TX0_CNT
  - `0x400`: TX16_BASE
  - `0x500`: RX0_BASE

**Expected Output:**
- Chip ID: `0x00511163` (MT7927)
- FW_STATUS: `0xffff10f1` indicates pre-init state (chip locked)
- FW_STATUS: `0x00000001` indicates MCU ready
- WPDMA_GLO_CFG shows TX_DMA and RX_DMA enable status

**Safety Considerations:**
- ✅ **SAFE** - Only reads BAR2, no writes
- ✅ No system crash risk
- ✅ Can be run without affecting hardware state

**Key Findings:**
- BAR2 is always accessible and shows current chip state
- FW_STATUS value indicates chip initialization state
- WPDMA registers are readable but may not be writable until chip is unlocked

---

### mt7927_readonly_scan.c - Read-Only BAR0 Scan

**Purpose:** SAFE read-only scan of BAR0 to find ring-like patterns and non-zero regions. Performs NO writes.

**Key Functions:**
- `find_ring_patterns()`: Scans BAR0 in 16-byte increments looking for ring register patterns (BASE=0, CNT=0x200, etc.)
- `scan_nonzero_regions()`: Samples key BAR0 regions at various offsets
- `scan_bar2()`: Dumps BAR2 non-zero values for reference
- `compare_bars()`: Compares BAR0 vs BAR2 at equivalent offsets

**Registers/Memory Accessed:**
- **BAR0:** Scans from `0x0000` to `0x100000` looking for patterns
- **BAR2:** Reads first `0x1000` bytes
- Key regions sampled: `0x0000`, `0x1000`, `0x2000`, `0x3000`, `0x4000`, `0x5000`, `0x6000`, `0x7000`, `0x8000`, `0x9000`, `0xa000`, `0xb000`, `0xc000`, `0xd000`, `0xe000`, `0xf000`, `0x10000`, `0x18000`, `0x20000`, `0x30000`, `0x40000`, `0x50000`, `0x54000`, `0x55000`, `0x60000`, `0x70000`, `0x80000`, `0x88000`, `0x90000`, `0xa0000`, `0xb0000`, `0xc0000`, `0xd0000`, `0xe0000`, `0xf0000`

**Expected Output:**
- Lists ring-like patterns found (BASE=0, CNT=power-of-2)
- Non-zero register values at sampled offsets
- Comparison between BAR0 and BAR2 showing which registers match

**Safety Considerations:**
- ✅ **SAFE** - Read-only, no writes
- ✅ No system crash risk
- ✅ Safe to run on any system state

**Key Findings:**
- Identifies potential WFDMA register locations
- Shows which BAR0 offsets contain non-zero values
- Reveals relationship between BAR0 and BAR2 register windows

---

### mt7927_scan_readonly.c - Read-Only Register Scanner

**Purpose:** SAFE read-only scanner that maps register layout by scanning BAR0 and BAR2 at various offsets. Performs bounds checking.

**Key Functions:**
- `scan_wfdma_region()`: Scans a specific base offset for WFDMA register patterns
- `dump_bar2_reference()`: Dumps known-working BAR2 registers as reference
- `scan_bar0_regions()`: Scans multiple potential WFDMA base offsets
- `check_lpctl()`: Reads power control state registers

**Registers/Memory Accessed:**
- **BAR0:** Scans at offsets: `0x00000`, `0x02000`, `0x03000`, `0x04000`, `0x05000`, `0x06000`, `0x07000`, `0x08000`, `0x10000`, `0x20000`, `0x40000`
- **BAR2:** Reference dump of known registers
- **Power Control:** `BAR0+0xe0010` (LPCTL), `BAR0+0xf0140` (WFSYS_RST), `BAR0+0xe00f0` (CONN_MISC)

**Expected Output:**
- WFDMA register values at each scanned base offset
- Comparison with BAR2 reference values
- Power control state (driver/FW ownership)
- Identification of which BAR0 offset contains real WFDMA registers

**Safety Considerations:**
- ✅ **SAFE** - Read-only with bounds checking
- ✅ No system crash risk
- ✅ Safe to run without affecting hardware

**Key Findings:**
- Identifies WFDMA base address in BAR0 (typically `0x2000`)
- Shows power ownership state
- Maps register layout without modifying hardware

---

### mt7927_power_diag.c - Power State Diagnostic

**Purpose:** Carefully reads LPCTL register to understand power ownership state. Uses a single BAR0 read after verifying BAR0 is accessible.

**Key Functions:**
- `mt7927_pwr_diag_probe()`: Main probe function that reads power state registers

**Registers/Memory Accessed:**
- **BAR2:** `0x000` (Chip ID), `0x200` (FW_STATUS)
- **BAR0:** `0x000` (test read), `0xe0010` (LPCTL), `0xe0000`, `0xe00f0` (CONN_ON_MISC)

**Expected Output:**
- Chip ID from BAR2
- FW_STATUS value
- LPCTL register value with bit decode:
  - Bit 0: HOST_OWN
  - Bit 1: FW_OWN
  - Bit 2: Status bit
- Interpretation of ownership state

**Safety Considerations:**
- ✅ **SAFE** - Read-only
- ✅ No system crash risk
- ✅ Minimal BAR0 access (only known-safe offsets)

**Key Findings:**
- Shows current power ownership (driver vs firmware)
- FW_STATUS indicates chip initialization state
- LPCTL bit pattern reveals ownership handshake state

---

## Power Management & Initialization

### mt7927_power_unlock.c - Power Handshake Testing

**Purpose:** Tests if the power handshake sequence unlocks ring registers. Follows exact mt7925 probe sequence.

**Key Functions:**
- `do_fw_pmctrl()`: Gives ownership to firmware (SET_OWN)
- `do_drv_pmctrl()`: Claims ownership for driver (CLR_OWN)
- `test_ring_writeability()`: Tests if ring registers become writable after power handshake
- `translate_addr()`: Address translation for CONN_INFRA region

**Registers/Memory Accessed:**
- **BAR0:**
  - `0xe0010` (LPCTL via translation) - Power control
  - `0x2100` (WFDMA0_RST)
  - `0x2208` (WFDMA0_GLO_CFG)
  - `0x2300` (WFDMA0_TX0_BASE)
  - `0x2304` (WFDMA0_TX0_CNT)
- **Translated addresses:**
  - `0x70010200` → `0xe0200` (MT_HW_CHIPID)
  - `0x70010204` → `0xe0204` (MT_HW_REV)

**Expected Output:**
- Initial state: Ring registers are read-only
- After FW pmctrl: LPCTL shows FW ownership
- After driver pmctrl: LPCTL shows driver ownership
- Ring writeability test: Shows if rings become writable after handshake

**Safety Considerations:**
- ⚠️ **MODERATE RISK** - Writes to power control registers
- ⚠️ May affect chip power state
- ⚠️ Could potentially lock chip if sequence fails
- ✅ Restores ring register values after testing

**Key Findings:**
- Power handshake sequence: SET_OWN → wait → CLR_OWN → wait
- Ring registers may remain read-only even after handshake
- Address translation needed for CONN_INFRA region access

---

### mt7927_claim_host.c - Host Claim Testing

**Purpose:** Attempts to claim HOST_OWN by writing to LPCTL register. This is the fundamental first step to initializing the chip.

**Key Functions:**
- `mt7927_claim_probe()`: Writes HOST_OWN bit and polls for state change

**Registers/Memory Accessed:**
- **BAR0:** `0xe0010` (LPCTL)
- **BAR2:** `0x200` (FW_STATUS), `0x208` (WPDMA_CFG)

**Expected Output:**
- Before: LPCTL shows current ownership state
- After write: LPCTL value immediately after HOST_OWN write
- Polling: Shows if HOST_OWN bit gets set over time
- Final state: Whether host successfully claimed ownership

**Safety Considerations:**
- ⚠️ **MODERATE RISK** - Writes to power control
- ⚠️ May fail silently if chip is in wrong state
- ✅ Read-only if chip doesn't respond

**Key Findings:**
- Simple HOST_OWN write may not be sufficient
- Chip may need additional initialization before claiming works
- FW_STATUS may change when ownership is claimed

---

### mt7927_correct_pmctrl.c - Power Control Correction

**Purpose:** Uses the CORRECT power control bits based on mt792x driver:
- `PCIE_LPCR_HOST_SET_OWN` (BIT 0) = Give ownership to FIRMWARE
- `PCIE_LPCR_HOST_CLR_OWN` (BIT 1) = Claim ownership for DRIVER
- `PCIE_LPCR_HOST_OWN_SYNC` (BIT 2) = Status bit

**Key Functions:**
- `claim_driver_own()`: Writes CLR_OWN and polls for OWN_SYNC to clear
- `print_state()`: Shows LPCTL, FW_STATUS, and WPDMA state

**Registers/Memory Accessed:**
- **BAR0:** `0xe0010` (LPCTL)
- **BAR2:** `0x200` (FW_STATUS), `0x208` (WPDMA_CFG)

**Expected Output:**
- Initial state: Shows current ownership
- After CLR_OWN: OWN_SYNC should clear (driver owns)
- WPDMA writeability test: Tests if WPDMA becomes writable after ownership claim

**Safety Considerations:**
- ⚠️ **MODERATE RISK** - Writes to power control
- ⚠️ May affect chip state
- ✅ Uses correct bit definitions from reference driver

**Key Findings:**
- CLR_OWN (BIT 1) is the correct way to claim driver ownership
- OWN_SYNC (BIT 2) is the status bit (1=FW owns, 0=driver owns)
- WPDMA may become writable after successful ownership claim

---

### mt7927_disable_aspm.c - ASPM Disable Test

**Purpose:** Disables PCIe Active State Power Management before claiming HOST_OWN. ASPM can put the chip in a low-power state where it ignores writes.

**Key Functions:**
- `disable_aspm()`: Disables ASPM on device and parent bridge
- `mt7927_aspm_probe()`: Disables ASPM then attempts to claim HOST_OWN

**Registers/Memory Accessed:**
- **PCIe Config Space:** Link Control register (ASPM bits)
- **BAR0:** `0xe0010` (LPCTL)
- **BAR2:** `0x200` (FW_STATUS), `0x208` (WPDMA_CFG)

**Expected Output:**
- PCIe Link Control before/after ASPM disable
- LPCTL state before and after HOST_OWN claim attempt
- Whether HOST_OWN is successfully acquired

**Safety Considerations:**
- ⚠️ **MODERATE RISK** - Modifies PCIe power management
- ⚠️ May affect system power consumption
- ⚠️ Writes to power control registers
- ✅ ASPM changes are typically reversible

**Key Findings:**
- ASPM must be disabled before chip initialization
- Parent bridge ASPM may also need disabling
- ASPM can prevent register writes from taking effect

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

**Expected Output:**
- Initial state: Shows chip state before reset
- After reset assert: WFSYS_RST EN bit cleared
- After reset deassert: WFSYS_RST EN bit set
- After HOST_OWN claim: Whether ownership was acquired

**Safety Considerations:**
- ⚠️ **HIGH RISK** - Performs hardware reset
- ⚠️ May disrupt chip operation
- ⚠️ Could cause system instability if chip is in use
- ⚠️ Should only be run when chip is not in use

**Key Findings:**
- WFSYS reset sequence: Assert (clear EN) → wait → Deassert (set EN) → wait
- Reset may be needed to bring chip out of power-down
- HOST_OWN claim may succeed after reset

---

## DMA & Ring Testing

### mt7927_ring_test.c - Ring Writability Testing

**Purpose:** Tests which ring registers are writable at BAR0 WFDMA offsets.

**Key Functions:**
- `test_write()`: Tests if a register is writable by writing a test value and reading back
- `scan_wfdma_regs()`: Scans WFDMA0 range for non-zero values
- Tests with DMA enabled/disabled and RST cleared

**Registers/Memory Accessed:**
- **BAR0 WFDMA0 base (0x2000):**
  - `0x2100` (RST)
  - `0x2208` (GLO_CFG)
  - `0x2300` (TX0_BASE)
  - `0x2304` (TX0_CNT)
  - `0x2308` (TX0_CIDX)
  - `0x230c` (TX0_DIDX)
  - `0x23f0` (TX15_BASE)
  - `0x2400` (TX16_BASE)
  - `0x2500` (RX0_BASE)
- **BAR2:** Comparison reads

**Expected Output:**
- For each register: before value, test value written, value read back
- Classification: WRITABLE, read-only, or partial
- State after DMA disable/enable and RST clear

**Safety Considerations:**
- ⚠️ **MODERATE RISK** - Writes to DMA registers
- ⚠️ May affect DMA operation if chip is active
- ✅ Restores original values after testing
- ⚠️ Modifies DMA enable state

**Key Findings:**
- Identifies which ring registers are writable
- Shows effect of DMA enable/disable on writability
- Reveals which registers are read-only vs writable

---

### mt7927_wfdma1_scan.c - Second DMA Engine Check

**Purpose:** Check if MCU rings are at WFDMA1 (0x3000) instead of WFDMA0 (0x2000).

**Key Functions:**
- `scan_wfdma_region()`: Scans a WFDMA region for ring registers
- `test_wfdma1_write()`: Tests if WFDMA1 rings are writable

**Registers/Memory Accessed:**
- **BAR0:**
  - `0x2000` (WFDMA0): GLO_CFG, RST, TX rings, RX rings
  - `0x3000` (WFDMA1): GLO_CFG, RST, TX rings, RX rings

**Expected Output:**
- Ring counts and values at WFDMA0
- Ring counts and values at WFDMA1
- Writability test results for WFDMA1 rings

**Safety Considerations:**
- ⚠️ **MODERATE RISK** - Writes to test writability
- ⚠️ May affect DMA if chip is active
- ✅ Restores values after testing

**Key Findings:**
- Identifies if WFDMA1 exists and contains rings
- Shows which DMA engine contains MCU rings
- Reveals ring configuration at each DMA engine

---

### mt7927_find_wfdma.c - WFDMA Location Finder

**Purpose:** Scans different base offsets to find where WFDMA lives. The mt7925 fixed_map shows WFDMA_0 at BAR offset 0x2000.

**Key Functions:**
- `scan_wfdma_base()`: Checks if a base offset contains WFDMA registers
- `try_enable_at_base()`: Attempts to enable DMA at a specific base offset

**Registers/Memory Accessed:**
- **BAR0:** Scans at offsets: `0x00000`, `0x02000`, `0x03000`, `0x04000`, `0x08000`, `0x10000`, `0x20000`, `0x80000`
- **BAR2:** `0x200` (FW_STATUS), `0x208` (GLO_CFG)
- **BAR0:** `0xe0010` (LPCTL) for ownership claim

**Expected Output:**
- GLO_CFG, HOST_INT, RST values at each scanned offset
- Identification of which offset matches FW_STATUS pattern
- DMA enable attempt results

**Safety Considerations:**
- ⚠️ **MODERATE RISK** - Writes to DMA registers
- ⚠️ May affect chip state
- ⚠️ Claims driver ownership

**Key Findings:**
- WFDMA typically found at `0x2000` in BAR0
- BAR2 may be a shadow/status window
- HOST_INT at WFDMA base should match FW_STATUS

---

### mt7927_dma_reset.c - DMA Reset Testing

**Purpose:** Performs DMA logic reset before enabling DMA, following the `mt792x_dma_disable` sequence.

**Key Functions:**
- `claim_driver_own()`: Claims driver ownership
- `dma_reset()`: Performs DMA logic reset (clear then set reset bits)
- `reset_dma_pointers()`: Resets DMA TX/RX pointers
- `try_enable_dma()`: Configures and enables DMA

**Registers/Memory Accessed:**
- **BAR2:**
  - `0x100` (MT_WFDMA0_RST) - Reset control
  - `0x208` (MT_WFDMA0_GLO_CFG) - Global config
  - `0x20c` (MT_WFDMA0_RST_DTX_PTR) - TX pointer reset
  - `0x280` (MT_WFDMA0_RST_DRX_PTR) - RX pointer reset
- **BAR0:** `0xe0010` (LPCTL) for ownership

**Expected Output:**
- Initial DMA state
- After reset: RST register state
- After enable: GLO_CFG with TX/RX DMA enabled

**Safety Considerations:**
- ⚠️ **HIGH RISK** - Performs DMA reset and enables DMA
- ⚠️ May disrupt ongoing DMA operations
- ⚠️ Could cause system instability
- ⚠️ Should only run when chip is not in use

**Key Findings:**
- DMA reset sequence: Clear reset bits → Set reset bits → Reset pointers → Enable
- GLO_CFG needs specific bits set (TX_WB_DDONE, FIFO_LITTLE_ENDIAN)
- DMA enable may require firmware to be loaded first

---

### mt7927_bar0_dma.c - BAR0 DMA Testing

**Purpose:** Uses BAR0 for ALL register access (not BAR2), matching mt7925 driver behavior.

**Key Functions:**
- `compare_bars()`: Compares BAR0 vs BAR2 register values
- `claim_driver_own()`: Claims driver ownership
- `dma_reset_bar0()`: Performs DMA reset via BAR0
- `try_enable_dma_bar0()`: Enables DMA via BAR0

**Registers/Memory Accessed:**
- **BAR0:**
  - `0x000` (Chip ID)
  - `0x100` (WFDMA_RST)
  - `0x200` (HOST_INT)
  - `0x208` (GLO_CFG)
  - `0x20c` (RST_DTX)
  - `0x300` (TX0_BASE)
  - `0x500` (RX0_BASE)
  - `0xe0010` (LPCTL)
- **BAR2:** Comparison reads

**Expected Output:**
- Comparison showing BAR0 vs BAR2 differences
- DMA reset and enable via BAR0
- Final state comparison

**Safety Considerations:**
- ⚠️ **HIGH RISK** - Performs DMA reset and enable
- ⚠️ May disrupt chip operation
- ⚠️ Uses BAR0 directly (may have different behavior than BAR2)

**Key Findings:**
- BAR0 and BAR2 may show different values for same registers
- BAR0 is the "real" register window, BAR2 may be shadow/status
- DMA operations via BAR0 may behave differently than BAR2

---

### mt7927_real_dma.c - Real DMA Testing

**Purpose:** Discovery: The REAL WFDMA registers are at BAR0 + 0x2000, not BAR2. This module writes to the correct address.

**Key Functions:**
- `wfdma_read()` / `wfdma_write()`: Helper functions for real WFDMA access
- `dump_wfdma_state()`: Shows WFDMA state
- `claim_driver_own()`: Claims driver ownership
- `enable_dma()`: Enables DMA at real WFDMA location

**Registers/Memory Accessed:**
- **BAR0 + 0x2000 (WFDMA_REAL_BASE):**
  - `0x100` (RST)
  - `0x200` (HOST_INT)
  - `0x208` (GLO_CFG)
  - `0x300` (TX0_BASE)
  - `0x304` (TX0_CNT)
- **BAR0:** `0xe0010` (LPCTL)

**Expected Output:**
- Initial WFDMA state
- After ownership claim
- After DMA enable: GLO_CFG with TX/RX enabled

**Safety Considerations:**
- ⚠️ **HIGH RISK** - Enables DMA
- ⚠️ May affect chip operation
- ⚠️ Uses real WFDMA location (BAR0+0x2000)

**Key Findings:**
- **CRITICAL:** Real WFDMA is at `BAR0 + 0x2000`, not BAR2
- BAR2[0x208] = 0x00000000 (shadow/status only)
- BAR0[0x2208] = 0x1010b870 (real config)
- DMA enable must target BAR0+0x2208, not BAR2+0x208

---

## Full Initialization Sequences

### mt7927_full_init.c - Full Initialization Test

**Purpose:** Based on mt7925 driver probe sequence:
1. Claim driver ownership (CLR_OWN)
2. WFSYS reset
3. Try to enable DMA

**Key Functions:**
- `claim_driver_own()`: Claims driver ownership
- `wfsys_reset()`: Performs WFSYS reset
- `try_enable_dma()`: Attempts to enable DMA
- `print_state()`: Shows chip state at each step

**Registers/Memory Accessed:**
- **BAR0:**
  - `0xe0010` (LPCTL)
  - `0xf0140` (WFSYS_SW_RST_B)
- **BAR2:** `0x200` (FW_STATUS), `0x208` (WPDMA_CFG)

**Expected Output:**
- Initial state
- After ownership claim
- After WFSYS reset
- After DMA enable attempt
- Final state

**Safety Considerations:**
- ⚠️ **HIGH RISK** - Full initialization sequence
- ⚠️ Performs hardware reset
- ⚠️ May disrupt chip operation
- ⚠️ Should only run when chip is not in use

**Key Findings:**
- Complete initialization sequence: Ownership → Reset → DMA Enable
- DMA may not enable until firmware is loaded
- Sequence matches mt7925 driver behavior

---

## Wide Scanning (Dangerous)

### mt7927_wide_scan.c - Full BAR0 Scan

**Purpose:** Scans the entire BAR0 to find writable ring-like registers. Looking for patterns that match DMA ring registers. **DANGEROUS** - writes to many registers.

**Key Functions:**
- `test_ring_region()`: Tests if a 16-byte region looks like a ring register set
- `find_ring_candidates()`: Looks for regions with ring-like structure (BASE=0, CNT=0x200)
- `scan_writable_regions()`: Scans key regions for writable ring bases
- `check_chip_id()`: Checks potential chip ID locations
- `detailed_wfdma_scan()`: Detailed scan of WFDMA area with write tests

**Registers/Memory Accessed:**
- **BAR0:** Scans entire BAR0 (up to 1MB) writing test values
- Tests offsets: `0x0000`, `0x1000`, `0x2000`, `0x3000`, `0x4000`, `0x5000`, `0x6000`, `0x7000`, `0x8000`, `0x9000`, `0xa000`, `0xb000`, `0xc000`, `0xd000`, `0xe000`, `0xf000`, `0x10000`, `0x20000`, `0x30000`, `0x40000`, `0x50000`, `0x54000`, `0x55000`, `0x80000`, `0x90000`, `0xa0000`, `0xb0000`, `0xc0000`, `0xd0000`, `0xe0000`, `0xf0000`

**Expected Output:**
- Chip ID locations found
- Writable registers in WFDMA area
- Ring-like candidates found
- Writable regions at key offsets

**Safety Considerations:**
- 🔴 **VERY HIGH RISK** - Writes to many unknown registers
- 🔴 **CAN CRASH SYSTEM** - May write to critical control registers
- 🔴 **CAN DAMAGE HARDWARE** - May put chip in bad state
- 🔴 **DO NOT RUN ON PRODUCTION SYSTEMS**
- ⚠️ Restores values but may miss some registers
- ⚠️ May trigger unexpected chip behavior

**Key Findings:**
- Identifies all writable registers in BAR0
- Finds ring register patterns across entire address space
- Reveals register layout but at high risk

---

## Summary of Safety Levels

### ✅ SAFE (Read-Only)
- `mt7927_diag.c`
- `mt7927_readonly_scan.c`
- `mt7927_scan_readonly.c`
- `mt7927_power_diag.c`

### ⚠️ MODERATE RISK (Limited Writes)
- `mt7927_power_unlock.c`
- `mt7927_claim_host.c`
- `mt7927_correct_pmctrl.c`
- `mt7927_disable_aspm.c`
- `mt7927_ring_test.c`
- `mt7927_wfdma1_scan.c`
- `mt7927_find_wfdma.c`

### ⚠️ HIGH RISK (Hardware Reset/DMA Operations)
- `mt7927_wfsys_reset.c`
- `mt7927_dma_reset.c`
- `mt7927_bar0_dma.c`
- `mt7927_real_dma.c`
- `mt7927_full_init.c`

### 🔴 VERY HIGH RISK (Wide Scanning)
- `mt7927_wide_scan.c`

---

## Key Discoveries

1. **WFDMA Location:** Real WFDMA registers are at `BAR0 + 0x2000`, not BAR2. BAR2 appears to be a shadow/status window.

2. **Power Control:** Correct sequence is:
   - `PCIE_LPCR_HOST_CLR_OWN` (BIT 1) to claim driver ownership
   - `PCIE_LPCR_HOST_OWN_SYNC` (BIT 2) is status (1=FW owns, 0=driver owns)

3. **Initialization Sequence:** 
   - Disable ASPM
   - Claim driver ownership
   - WFSYS reset
   - DMA reset
   - Enable DMA

4. **Register Access:** BAR0 is the primary register window. BAR2 provides status/shadow registers but may not be writable.

5. **FW_STATUS:** `0xffff10f1` indicates pre-init state (chip locked), `0x00000001` indicates MCU ready.

---

## Usage Recommendations

1. **Start with safe modules** (`mt7927_diag.c`, `mt7927_readonly_scan.c`) to understand chip state
2. **Use power management modules** (`mt7927_power_diag.c`, `mt7927_correct_pmctrl.c`) to understand ownership
3. **Test DMA access** (`mt7927_ring_test.c`, `mt7927_real_dma.c`) after ownership is claimed
4. **Full initialization** (`mt7927_full_init.c`) only when chip is not in use
5. **Avoid wide scanning** (`mt7927_wide_scan.c`) unless absolutely necessary and on test hardware

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
