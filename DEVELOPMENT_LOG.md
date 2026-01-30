# MT7927 WiFi 7 Linux Driver Development Log

## Executive Summary

This document chronicles the development effort to create a Linux driver for the MediaTek MT7927 WiFi 7 chip. The key discovery was that MT7927 is architecturally similar to MT7925 (which has full Linux support), differing mainly in 320MHz channel width support.

**Current Status**: Driver binds to hardware, DMA rings configure correctly, power management handshake works, but MCU commands timeout because DMA hardware doesn't process TX descriptors (DIDX never advances despite CIDX incrementing).

---

## Phase 1: Initial Discovery and Architecture Understanding

### What We Did
- Analyzed the MT7925 driver in the Linux kernel (`drivers/net/wireless/mediatek/mt76/mt7925/`)
- Identified that MT7927 (PCI ID: 14c3:7927) shares the same firmware files as MT7925
- Created initial driver structure based on MT7925 architecture

### Key Findings
- MT7927 = MT7925 + 320MHz channel support
- MT7925 firmware is compatible with MT7927
- The chip uses the mt76 driver framework patterns

### Files Created
- `src/mt7927.h` - Main header with device structures
- `src/mt7927_regs.h` - Register definitions
- `src/mt7927_pci.c` - PCI probe/remove and power management
- `src/mt7927_dma.c` - DMA queue management
- `src/mt7927_mcu.c` - MCU communication and firmware loading

---

## Phase 2: Initial DMA Implementation

### What We Did
- Implemented DMA descriptor ring allocation
- Created TX/RX queue structures based on MT7925
- Implemented ring configuration at BAR0 + 0x2000 (WFDMA0 base)

### Errors Encountered
```
TX Q15: ring base write failed (read back 0x00000000, expected 0xXXXXXXXX)
TX Q16: ring base write failed
```

### Diagnostic Done
- Created test modules to check register writability
- Discovered that TX rings 15/16/17 don't exist on MT7927

### What We Found
- MT7927 only has **8 TX rings** (0-7), not 17+ like MT7925
- The minimal scan diagnostic showed:
  - TX Ring 0-7: Valid (CNT registers show 0x00000200)
  - TX Ring 8+: Invalid (CNT registers show 0x00000000)
- MT7925 uses rings 15/16 for MCU_WM and FWDL queues, but these don't exist on MT7927

### Fix Applied
Changed MCU queue assignments in `src/mt7927_regs.h`:
```c
// Before (copied from MT7925):
MT7927_TXQ_MCU_WM = 15,
MT7927_TXQ_FWDL = 16,

// After (adapted for MT7927's 8-ring architecture):
MT7927_TXQ_FWDL = 4,       // Firmware download - use ring 4
MT7927_TXQ_MCU_WM = 5,     // MCU commands - use ring 5
```

Updated interrupt enable masks accordingly:
```c
#define MT_INT_TX_DONE_MCU_WM   HOST_TX_DONE_INT_ENA5
#define MT_INT_TX_DONE_FWDL     HOST_TX_DONE_INT_ENA4
```

---

## Phase 3: Register Writability Investigation

### What We Did
After fixing queue assignments, ring writes still failed. Created diagnostic modules:
1. `diag/mt7927_ring_test.c` - Test individual ring register writes
2. `diag/mt7927_wide_scan.c` - Scan entire BAR0 for writable regions

### Errors Encountered
- `mt7927_wide_scan.ko` caused system reboot (kernel panic)
- `mt7927_readonly_scan.ko` also caused system reboot

### Diagnostic Done
Created safer diagnostic: `diag/mt7927_minimal_scan.c`
- Limited reads to known-good region (0x2000-0x2FFF)
- Fixed bug where code tried to read beyond mapped BAR0 region

### What We Found
```
DMA RST on fresh boot: 0x00000030
TX Ring 0-7: Valid
TX Ring 8-15: Invalid
RX Ring 0-3: Valid (16 total)
```

Key insight: **On fresh boot with RST=0x30, ring registers ARE writable.**

---

## Phase 4: DMA Reset State Investigation

### What We Did
Created `diag/mt7927_power_unlock.c` to test the power management handshake sequence from MT7925 and verify ring writability under different conditions.

### What We Found
1. On fresh boot: RST=0x30, rings writable ✓
2. After our driver's DMA reset sequence: RST=0x00, rings become read-only ✗
3. The DMA reset logic was CLEARING the RST bits, which made rings unwritable

### Errors Encountered
When driver ran `mt7927_dma_disable`:
```c
mt7927_clear(dev, MT_WFDMA0_RST, MT_WFDMA0_RST_DMASHDL_ALL_RST | MT_WFDMA0_RST_LOGIC_RST);
```
This took RST from 0x30 → 0x00, and rings became read-only.

### Fix Applied
Modified `mt7927_dma_disable()` in `src/mt7927_dma.c`:
```c
// Before: Clear RST bits (broke ring writability)
mt7927_clear(dev, MT_WFDMA0_RST, ...);

// After: Keep RST bits SET during ring configuration
// Only do reset pulse if not already in reset
if (!(rst_before & (MT_WFDMA0_RST_DMASHDL_ALL_RST | MT_WFDMA0_RST_LOGIC_RST))) {
    mt7927_clear(dev, MT_WFDMA0_RST, ...);
    mt7927_set(dev, MT_WFDMA0_RST, ...);
}
// Leave RST bits SET - ring registers need this to be writable
```

### Result After Fix
Ring writes now succeed:
```
Queue 0: writing ring_base=0x2300, dma=0xffff8000, ndesc=2048
Queue 5: writing ring_base=0x2350, dma=0xff209000, ndesc=256
Queue 4: writing ring_base=0x2340, dma=0xff208000, ndesc=128
Ring verify: TX0=0xffff8000 TX4=0xff208000 TX5=0xff209000  ✓
```

---

## Phase 5: WFDMA1 Investigation

### What We Did
Created `diag/mt7927_wfdma1_scan.c` to check if MT7927 has a second DMA engine (WFDMA1) at BAR0 + 0x3000, as used by MT7996.

### What We Found
- WFDMA1 does NOT exist on MT7927
- Reading from 0x3000+ returned 0x00000000 (unmapped)
- This confirms MT7927 has only WFDMA0 with 8 TX rings

---

## Phase 6: WiFi Subsystem Reset Fix

### What We Did
Analyzed the `mt792x_wfsys_reset()` function in the reference driver.

### Errors Encountered
Our reset was too short and didn't wait for proper completion:
```c
// Our code: only waited 1-2ms
usleep_range(1000, 2000);
// Checked wrong bit (RST_B_EN instead of INIT_DONE)
```

### What We Found
Reference driver waits 50ms and polls for INIT_DONE (bit 4):
```c
mt76_clear(dev, addr, WFSYS_SW_RST_B);  // Assert reset
msleep(50);                              // Wait 50ms
mt76_set(dev, addr, WFSYS_SW_RST_B);    // Release reset
// Poll for WFSYS_SW_INIT_DONE (bit 4)
```

### Fix Applied
Updated `mt7927_wfsys_reset()` in `src/mt7927_pci.c`:
```c
// Added INIT_DONE constant
#define MT_WFSYS_SW_INIT_DONE  BIT(4)

// Fixed reset sequence
mt7927_clear(dev, MT_WFSYS_SW_RST_B, MT_WFSYS_SW_RST_B_EN);
msleep(50);  // Wait 50ms (was 1-2ms)
mt7927_set(dev, MT_WFSYS_SW_RST_B, MT_WFSYS_SW_RST_B_EN);

// Poll for INIT_DONE instead of just checking RST bit
for (i = 0; i < 50; i++) {
    val = mt7927_rr(dev, MT_WFSYS_SW_RST_B);
    if (val & MT_WFSYS_SW_INIT_DONE)
        return 0;
    msleep(10);
}
```

---

## Phase 7: Power Management Handshake

### What We Did
Analyzed the power management sequence in MT7925: `__mt792xe_mcu_drv_pmctrl()`.

### What We Found
The reference driver does a full handshake at `MT_CONN_ON_LPCTL` (0x7c060010):
1. Write SET_OWN (bit 0) - give ownership to firmware
2. Write CLR_OWN (bit 1) - claim ownership for driver
3. Poll for OWN_SYNC (bit 2) to clear

### Implementation
Already had this in `src/mt7927_pci.c`, but added verbose logging:
```
LPCTL before fw_pmctrl: 0x00000000
Writing SET_OWN to LPCTL...
FW power control acquired after 0 iterations (LPCTL: 0x00000004)
LPCTL before drv_pmctrl: 0x00000004
Writing CLR_OWN to LPCTL...
Driver power control acquired after 9 iterations (LPCTL: 0x00000000)  ✓
```

---

## Phase 8: Pre-Firmware Initialization Steps

### What We Did
Analyzed what MT7925 does before loading firmware in `mt7925e_mcu_init()`.

### What We Found
Two additional steps before firmware download:
1. **Disable L0S power saving** - Can interfere with DMA
   ```c
   mt76_rmw_field(dev, MT_PCIE_MAC_PM, MT_PCIE_MAC_PM_L0S_DIS, 1);
   ```
2. **Set SWDEF_MODE to NORMAL_MODE** - Required for firmware download
   ```c
   mt76_wr(dev, MT_SWDEF_MODE, MT_SWDEF_NORMAL_MODE);
   ```

### Fix Applied
Updated `mt7927_mcu_init()` in `src/mt7927_mcu.c`:
```c
// Disable L0S power saving for stable DMA operation
mt7927_set(dev, MT_PCIE_MAC_PM, MT_PCIE_MAC_PM_L0S_DIS);

// Set firmware mode to normal (required before firmware download)
mt7927_wr(dev, MT_SWDEF_MODE, MT_SWDEF_NORMAL_MODE);
```

### Result
```
PCIE_MAC_PM before: 0x00000e0f
PCIE_MAC_PM after L0S disable: 0x00000f0f  ✓
SWDEF_MODE before: 0xf6fbfffb
SWDEF_MODE after: 0x00000000  ✓
```

---

## Phase 9: Current Status - DMA Not Processing

### Current State
Everything initializes correctly:
- ✓ Driver binds to PCI device
- ✓ Power management handshake completes
- ✓ WiFi subsystem reset completes
- ✓ DMA rings configure with correct addresses
- ✓ L0S disabled, SWDEF_MODE set to normal
- ✓ Firmware files loaded into memory

### The Problem
```
TX Q5: CIDX=1 DIDX=0 BASE=0xff209000 CNT=256
MCU command 0x0010 timeout
```

- **CIDX=1**: CPU wrote 1 descriptor to the TX ring
- **DIDX=0**: DMA hardware NEVER processed it (DIDX should advance to 1)
- **BASE=correct**: Ring address is properly configured

The DMA engine is not processing descriptors despite:
- TX_DMA_EN and RX_DMA_EN set in GLO_CFG
- Ring addresses correctly written
- Interrupts enabled

---

## Phase 10: MCU Command Attempts (Detailed Timeline)

This section documents every attempt to send the first MCU command (PATCH_SEM_CONTROL = 0x0010) and why each failed.

### Attempt 1: Initial Implementation with Rings 15/16

**What We Did:**
- Copied MT7925 queue assignments: MCU_WM=15, FWDL=16
- Configured rings at addresses 0x23F0 (ring 15) and 0x2400 (ring 16)

**Result:**
```
TX Q15: ring base write failed (read back 0x00000000)
TX Q16: ring base write failed (read back 0x00000000)
MCU command 0x0010 timeout
```

**Analysis:**
- Ring 15/16 registers don't exist on MT7927
- Writes to non-existent rings silently fail
- Diagnostic confirmed MT7927 only has 8 TX rings (0-7)

**Fix:** Changed to use rings 4/5 for FWDL/MCU_WM

---

### Attempt 2: Using Rings 4/5, RST Cleared During DMA Init

**What We Did:**
- Changed MCU_WM=5, FWDL=4
- DMA init sequence cleared RST bits (0x30 → 0x00)

**Result:**
```
Queue 5: writing ring_base=0x2350, dma=0xff129000, ndesc=256
TX Q5: CIDX=1 DIDX=0 BASE=0x00000000 CNT=512
MCU command 0x0010 timeout
```

**Analysis:**
- Ring writes succeeded initially
- But BASE reads back as 0x00000000 after DMA enable
- Clearing RST bits WIPED the ring configuration!

**Fix:** Modified dma_disable to keep RST=0x30

---

### Attempt 3: Keeping RST=0x30 During Ring Config

**What We Did:**
- Kept RST=0x30 during ring configuration
- Did NOT clear RST in dma_disable

**Result:**
```
Queue 5: writing ring_base=0x2350, dma=0xffdfb000, ndesc=256
Ring verify: TX0=0xffff8000 TX4=0xffdfa000 TX5=0xffdfb000  ✓
TX Q5: CIDX=1 DIDX=0 BASE=0xffdfb000 CNT=256
MCU command 0x0010 timeout
```

**Analysis:**
- Ring configuration now SURVIVES after enable ✓
- BASE reads back correctly ✓
- But DIDX still 0 - DMA not processing
- Hypothesis: Maybe RST=0x30 prevents DMA from running?

---

### Attempt 4: Clear RST After Ring Config, Before DMA Enable

**What We Did:**
- Configure rings with RST=0x30
- Clear RST to 0x00 just before enabling DMA in GLO_CFG

**Result:**
```
Queue 5: writing ring_base=0x2350, dma=0xffd15000, ndesc=256
Taking DMA out of reset (RST: 0x00000030 -> 0x00)
Ring verify: TX0=0x00000000 TX4=0x00000000 TX5=0x00000000  ✗
MCU command 0x0010 timeout
```

**Analysis:**
- Clearing RST IMMEDIATELY wipes all ring configuration
- This confirms: rings are ONLY writable when RST bits are set
- But reference driver (MT7925) leaves RST=0x30 and DMA works
- MT7927 behaves differently - DMA doesn't process with RST=0x30

**Conclusion:** Catch-22 situation:
- RST=0x30: Rings writable, but DMA doesn't process
- RST=0x00: DMA could process, but rings get wiped

---

### Attempt 5: Fixed WFSYS Reset Timing (50ms + INIT_DONE)

**What We Did:**
- Fixed wfsys_reset to wait 50ms (was 1-2ms)
- Poll for INIT_DONE (bit 4) instead of just RST bit

**Result:**
```
WFSYS_SW_RST_B before: 0x00000011
WiFi subsystem reset complete (0x00000011 after 0ms)
TX Q5: CIDX=1 DIDX=0 BASE=0xffdfb000 CNT=256
MCU command 0x0010 timeout
```

**Analysis:**
- WFSYS reset now completes properly
- But same DMA issue persists

---

### Attempt 6: Added Power Control in MCU Init

**What We Did:**
- Added drv_pmctrl call at start of mcu_init
- Ensured driver owns chip before sending commands

**Result:**
```
LPCTL before drv_pmctrl: 0x00000000
Driver already owns chip
TX Q5: CIDX=1 DIDX=0 BASE=0xff209000 CNT=256
MCU command 0x0010 timeout
```

**Analysis:**
- Power control already acquired (from probe sequence)
- Not the cause of DMA issue

---

### Attempt 7: Disabled L0S Power Saving

**What We Did:**
- Added L0S disable before firmware load (from MT7925 reference)
- Set bit 8 in MT_PCIE_MAC_PM register

**Result:**
```
PCIE_MAC_PM before: 0x00000e0f
PCIE_MAC_PM after L0S disable: 0x00000f0f  ✓
TX Q5: CIDX=1 DIDX=0 BASE=0xff209000 CNT=256
MCU command 0x0010 timeout
```

**Analysis:**
- L0S now disabled
- Still same DMA issue

---

### Attempt 8: Set SWDEF_MODE to Normal

**What We Did:**
- Added SWDEF_MODE = 0 (NORMAL_MODE) before firmware load
- This tells firmware what mode to operate in

**Result:**
```
SWDEF_MODE before: 0xf6fbfffb
SWDEF_MODE after: 0x00000000  ✓
TX Q5: CIDX=1 DIDX=0 BASE=0xff209000 CNT=256
MCU command 0x0010 timeout
```

**Analysis:**
- SWDEF_MODE set correctly
- Still same fundamental DMA issue

---

### Current State After All Attempts

**What Works:**
| Component | Status | Evidence |
|-----------|--------|----------|
| PCI binding | ✓ | Device detected, BARs mapped |
| Power handshake | ✓ | LPCTL: 0x04 → 0x00 |
| WFSYS reset | ✓ | INIT_DONE achieved |
| Ring allocation | ✓ | DMA memory allocated |
| Ring configuration | ✓ | BASE/CNT written, verified |
| L0S disable | ✓ | PCIE_MAC_PM bit 8 set |
| SWDEF_MODE | ✓ | Set to 0 (normal) |
| GLO_CFG enable | ✓ | TX/RX DMA EN bits set |
| Descriptor write | ✓ | CIDX advances to 1 |
| Firmware in memory | ✓ | 1.4MB loaded |

**What Fails:**
| Component | Status | Evidence |
|-----------|--------|----------|
| DMA processing | ✗ | DIDX stays at 0 |
| MCU command | ✗ | 0x0010 timeout after 3s |

**The Core Issue:**
The CPU successfully writes a TX descriptor (CIDX goes from 0 to 1), but the DMA hardware NEVER picks it up (DIDX remains 0). This happens regardless of:
- Ring number used (4, 5, 15, 16)
- RST state (0x30 or 0x00 - though 0x00 wipes rings)
- L0S power saving state
- SWDEF_MODE value

---

### Hypotheses for Next Investigation

1. **RST State Issue**: Maybe RST=0x30 (kept in reset for writability) prevents DMA from running?
   - Reference driver leaves RST=0x30 and DMA works
   - But maybe MT7927 is different?

2. **Different Register Offsets**: MT7927 might have different DMA register layout

3. **Missing Initialization Step**: There may be another enable bit or sequence we're missing

4. **Interrupt Routing**: The descriptor might need an interrupt or doorbell write

5. **Firmware Pre-presence**: Maybe firmware needs to be partially running before DMA works?

---

## Diagnostic Modules Created

| Module | Purpose | Key Finding |
|--------|---------|-------------|
| `mt7927_ring_test.c` | Test ring register writability | RST state affects writability |
| `mt7927_wide_scan.c` | Scan full BAR0 | Caused crash - too aggressive |
| `mt7927_readonly_scan.c` | Read-only full scan | Also crashed |
| `mt7927_minimal_scan.c` | Safe scan of 0x2000-0x2FFF | MT7927 has 8 TX, 16 RX rings |
| `mt7927_power_unlock.c` | Test power handshake | Fresh boot RST=0x30, writable |
| `mt7927_wfdma1_scan.c` | Check for WFDMA1 | WFDMA1 doesn't exist |

---

## Key Register Values Observed

| Register | Address | Fresh Boot | After Init | Notes |
|----------|---------|------------|------------|-------|
| WFDMA0_RST | 0x2100 | 0x00000030 | 0x00000030 | Bits 4,5 = reset state |
| WPDMA_GLO_CFG | 0x2208 | 0x10103870 | 0x1017b8f5 | DMA enabled |
| LPCTL | 0x7c060010 | 0x00000000 | 0x00000000 | Driver owns |
| WFSYS_SW_RST_B | 0x7c000140 | 0x00000011 | 0x00000011 | Reset complete |
| PCIE_MAC_PM | 0x74030194 | 0x00000e0f | 0x00000f0f | L0S disabled |
| SWDEF_MODE | 0x0041f23c | 0xf6fbfffb | 0x00000000 | Normal mode |

---

## Files Modified (Current State)

### `src/mt7927_regs.h`
- Fixed TX queue IDs (FWDL=4, MCU_WM=5)
- Added interrupt enable bits for rings 4-7
- Added MT_WFSYS_SW_INIT_DONE
- Added MT_SWDEF_NORMAL_MODE

### `src/mt7927_pci.c`
- Fixed wfsys_reset to wait 50ms and poll INIT_DONE
- Added verbose logging to power management functions

### `src/mt7927_dma.c`
- Fixed DMA reset to keep RST=0x30 during ring config
- Added ring verification after DMA enable
- Removed RST clear from dma_enable

### `src/mt7927_mcu.c`
- Added L0S disable before firmware load
- Added SWDEF_MODE = NORMAL_MODE before firmware load
- Added drv_pmctrl call in mcu_init

---

## Next Steps to Investigate

1. **Try clearing RST after ring config but before sending commands**
   - Maybe DMA needs RST=0x00 to actually process descriptors
   - Need to find way to prevent ring config from being wiped

2. **Study MT7996 driver** - Newer chip, might have similar quirks

3. **Check for descriptor format differences** - MT7927 might expect different TXD format

4. **Look for doorbell/kick registers** - Some DMA engines need explicit trigger

5. **Analyze the 0xffff10f1 status value** - This appears at BAR2[0x200] and might indicate chip state

---

## Summary

The MT7927 driver has progressed significantly:
- All hardware initialization steps work correctly
- Ring configuration succeeds
- Power management handshake succeeds
- Firmware loads into memory

The remaining blocker is that **DMA hardware doesn't process TX descriptors** - the CPU writes descriptors (CIDX advances) but hardware never picks them up (DIDX stays 0). This prevents MCU commands from being sent, which blocks firmware activation.
