# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Context

This is a Linux kernel driver for the MediaTek MT7927 WiFi 7 chipset. **CRITICAL DISCOVERY (2026-01-31)**: MT7927 is an **MT6639 variant** (NOT MT7925 as initially assumed). MediaTek's kernel modules explicitly map MT7927 to MT6639 driver data. MT6639 and MT7925 share firmware (CONNAC3X family) and ring protocol, explaining our earlier firmware compatibility findings. See **docs/MT6639_ANALYSIS.md** for complete evidence.

**Official Product Name**: MT7927 802.11be 320MHz 2x2 PCIe Wireless Network Adapter [Filogic 380]

**Current Status**: **ROOT CAUSE FOUND (2026-01-31)** - MT7927 ROM bootloader does NOT support mailbox command protocol! Our driver waits for mailbox responses that the ROM will never send. DMA hardware works correctly; we're using the wrong communication protocol.

**⚠️ SOLUTION**: Switch to polling-based firmware loading (no mailbox waits). Working reference driver from zouyonghao proves this approach. See ZOUYONGHAO_ANALYSIS.md for complete details.

## Critical Files to Review First

1. **docs/ZOUYONGHAO_ANALYSIS.md** - 🎯 **ROOT CAUSE FOUND** - No mailbox protocol in MT7927 ROM!
2. **docs/MT6639_ANALYSIS.md** - Proves MT7927 is MT6639 variant, validates rings 15/16
3. **docs/REFERENCE_SOURCES.md** - Analysis of reference code origins and authoritative sources
4. **DEVELOPMENT_LOG.md** - Complete chronological history (Phase 17 = root cause discovery)
5. **AGENTS.md** - Session bootstrap with current blocker details, hardware context, and what's been tried
6. **HARDWARE_ANALYSIS.md** - lspci analysis (L1 ASPM hypothesis now invalidated)
7. **README.md** - Project overview, build instructions, expected outputs

## Build System

### Main Build Commands

```bash
# Build everything
make clean && make

# Build specific components
make driver    # Production driver (src/)
make tests     # Test modules (tests/)
make diag      # Diagnostic modules (diag/)

# The main Makefile calls subdirectory Makefiles:
# - src/Makefile: builds mt7927.ko (mt7927_pci.o + mt7927_dma.o + mt7927_mcu.o)
# - tests/05_dma_impl/Kbuild: builds test_*.ko modules
# - diag/Makefile: builds diagnostic modules
```

### Testing Commands

```bash
# Load production driver
sudo rmmod mt7927 2>/dev/null
sudo insmod src/mt7927.ko
sudo dmesg | tail -40

# Load test module (recommended for development)
sudo rmmod test_fw_load 2>/dev/null
sudo insmod tests/05_dma_impl/test_fw_load.ko
sudo dmesg | tail -40

# Load diagnostic module
sudo rmmod mt7927_diag 2>/dev/null
sudo insmod diag/mt7927_diag.ko
sudo dmesg | tail -20

# Check chip state
make check

# Recover from error state (if chip shows 0xffffffff)
make recover

# OR manually (update PCI address to match your system - check with lspci)
# Bash:
DEVICE=$(lspci -nn | grep 14c3:7927 | cut -d' ' -f1)
echo 1 | sudo tee /sys/bus/pci/devices/0000:$DEVICE/remove
sleep 2
echo 1 | sudo tee /sys/bus/pci/rescan

# Fish:
set DEVICE (lspci -nn | grep 14c3:7927 | cut -d' ' -f1)
echo 1 | sudo tee /sys/bus/pci/devices/0000:$DEVICE/remove
sleep 2
echo 1 | sudo tee /sys/bus/pci/rescan

# ⚠️ Test: Disable L1 ASPM (potential DMA blocker!)
# Bash: DEVICE=$(lspci -nn | grep 14c3:7927 | cut -d' ' -f1); sudo setpci -s $DEVICE CAP_EXP+10.w=0x0000
# Fish: set DEVICE (lspci -nn | grep 14c3:7927 | cut -d' ' -f1); sudo setpci -s $DEVICE CAP_EXP+10.w=0x0000
```

## Code Architecture

### Source Layout

```
src/
├── mt7927_pci.c    # PCI probe, power management (LPCTL handshake), WFSYS reset
├── mt7927_dma.c    # DMA ring allocation/configuration (8 TX, 16 RX rings)
├── mt7927_mcu.c    # MCU protocol, firmware loading sequence
├── mt7927.h        # Core structures: mt7927_dev, mt7927_queue, descriptor format
├── mt7927_regs.h   # Register definitions, addresses, queue IDs
└── mt7927_mcu.h    # MCU message structures

tests/05_dma_impl/  # Component validation tests
├── test_power_ctrl.c    # Power management handshake
├── test_wfsys_reset.c   # WiFi subsystem reset
├── test_dma_queues.c    # DMA ring allocation
└── test_fw_load.c       # Complete firmware loading (integration test)

diag/               # 18 hardware exploration/diagnostic modules
```

### Initialization Sequence

1. **PCI Probe** (mt7927_pci.c)
   - Map BAR0 (2MB main register space) - **NOT BAR2** (read-only shadow)
   - Acquire power management ownership via LPCTL register (0x04 → 0x00)
   - Execute WFSYS reset (WFSYS_SW_RST_B timing, poll INIT_DONE)
   - Disable PCIe ASPM L0s power saving

2. **DMA Setup** (mt7927_dma.c)
   - Allocate coherent DMA memory for descriptor rings
   - Configure 8 TX rings (0-7) and 16 RX rings (0-15)
   - Set ring base addresses, counts, and indices
   - **MT7927-specific**: Uses rings 4 (FWDL) and 5 (MCU_WM), NOT 16/15 like MT7925

3. **Firmware Load** (mt7927_mcu.c) - **CURRENTLY BLOCKED HERE**
   - Send PATCH_SEM_CONTROL command via TX ring 15 (MCU_WM)
   - Transfer firmware chunks via TX ring 16 (FWDL)
   - Signal completion and activate firmware
   - **Blocker**: First MCU command times out because DMA_DIDX never advances

### Architecture: MT6639 Family (CONNAC3X)

**MT7927 is an MT6639 variant** (confirmed via MediaTek kernel modules). MT6639 and MT7925 are both CONNAC3X family chips sharing firmware and ring protocol. See docs/MT6639_ANALYSIS.md for complete analysis.

| Property | MT6639 (Parent) | MT7925 (Sibling) | MT7927 (This chip) | Validation Status |
|----------|-----------------|------------------|---------------------|-------------------|
| Architecture | CONNAC3X | CONNAC3X | CONNAC3X | **✓ CONFIRMED** |
| TX Ring Count | Sparse (0,1,2,15,16) | 17 (0-16) | 8 physical, sparse layout | **✓ CONFIRMED** |
| FWDL Queue | Ring 16 | Ring 16 | Ring 16 | **✓ CONFIRMED** (2026-01-31) |
| MCU Queue | Ring 15 | Ring 15 | Ring 15 | **✓ CONFIRMED** (2026-01-31) |
| Firmware | MT7925 compatible | Native | MT7925 files | **✓ PROVEN** (Windows analysis) |
| WFDMA1 | TBD | Present | **NOT present** | **✓ CONFIRMED** (Phase 5) |
| BAR0 Size | TBD | Unknown | **2MB** (lspci confirmed) | **✓ CONFIRMED** |
| Register offsets | Reference | Reference | Same as MT6639 | Assumed |

### Hardware Configuration (from lspci)

| Property | Value |
|----------|-------|
| Chip ID | **0x00511163** (confirmed 2026-01-31) |
| HW Revision | 0x11885162 |
| PCI Location | 01:00.0 (varies by system) |
| BAR0 | 2MB @ 0x90600000 (64-bit, non-prefetchable) |
| BAR2 | 32KB @ 0x90800000 (64-bit, non-prefetchable) |
| PCIe Link | Gen3 x1 (8GT/s) |
| IRQ | 48 (pin A, legacy INTx mode) |
| MSI | 1/32 vectors available (currently disabled) |
| ASPM L0s | Disabled (by driver) |
| **⚠️ ASPM L1** | **ENABLED** (potential DMA blocker!) |
| **⚠️ L1.1 Substate** | **ENABLED** |
| **⚠️ L1.2 Substate** | **ENABLED** |

### Key Register Locations (BAR0 offsets)

```c
WFDMA0_BASE        = 0x2000      // Real writable DMA registers
WFDMA0_RST         = 0x2100      // DMA reset control (bits 4,5)
WFDMA0_GLO_CFG     = 0x2208      // DMA global config (TX/RX enable)
TX_RING_BASE(n)    = 0x2300 + n*0x10
RX_RING_BASE(n)    = 0x2500 + n*0x10
LPCTL              = 0x7c060010  // Power management handshake
WFSYS_SW_RST_B     = 0x7c000140  // WiFi subsystem reset
PCIE_MAC_PM        = 0x74030194  // PCIe power management (L0S)
SWDEF_MODE         = 0x0041f23c  // Firmware mode register
```

## Current Blocker Details

**All initialization steps succeed**, but DMA hardware doesn't process TX descriptors:
- TX ring initialized: CIDX=0, CPU_DIDX=0, DMA_DIDX=0 (stuck)
- MCU ready status: 0x00000001 (confirmed)
- Power management: Host owns DMA, handshake complete
- Descriptors in coherent DMA memory, ring registers configured correctly
- CIDX write triggers processing attempt, but DMA_DIDX never advances
- No DMA completion interrupts occur

**This is the Catch-22**:
- RST=0x30 (reset bits SET): Ring registers writable, but DMA doesn't process
- RST=0x00 (reset bits CLEAR): Ring configuration gets wiped immediately

Reference MT7925 driver leaves RST=0x30 and DMA works. MT7927 behaves differently.

### ⚠️ NEW PRIME SUSPECT: L1 ASPM States

**lspci analysis reveals** (see HARDWARE_ANALYSIS.md):
- Driver disables L0s ✓
- **But L1 ASPM is still ENABLED** ⚠️
- **L1.1 and L1.2 substates are ENABLED** ⚠️
- LTR threshold: 166912ns for L1.2 entry

**Hypothesis**: Device enters L1.2 power saving state when DMA should be active, causing DIDX to never advance.

**First Test**: Disable all ASPM states:
```bash
sudo setpci -s 01:00.0 CAP_EXP+10.w=0x0000
```

See AGENTS.md for detailed history of all 8+ phases of attempts and DEVELOPMENT_LOG.md for complete investigation record.

## Recent Fixes: test_fw_load.c (2026-01-30)

The test module `tests/05_dma_impl/test_fw_load.c` had several critical bugs that caused kernel crashes. All fixes have been applied:

### 1. Wrong BAR Mapping (Caused Kernel Oops)
**Symptom**: Page fault at address `ffffccbb878f0010` in `remap_write+0x42/0x60`

**Root Cause**: Module mapped BAR2 (32KB) but tried to access MT_HIF_REMAP_L1 at offset 0x155024 (~1.3MB)

**Fix**:
```c
// Before (WRONG):
ret = pcim_iomap_regions(pdev, BIT(2), "mt7927_test");
dev->regs = pcim_iomap_table(pdev)[2];

// After (CORRECT):
ret = pcim_iomap_regions(pdev, BIT(0), "mt7927_test");
dev->regs = pcim_iomap_table(pdev)[0];  /* Use BAR0 (2MB) */
```

### 2. Wrong WFDMA Register Offsets
**Problem**: Registers were at BAR2-style offsets (0x02xx) instead of BAR0 offsets (0x22xx)

**Fix**: Added proper base offset:
```c
#define MT_WFDMA0_BASE              0x2000
#define MT_WFDMA0_GLO_CFG           (MT_WFDMA0_BASE + 0x208)
#define MT_WFDMA0_HOST_INT_STA      (MT_WFDMA0_BASE + 0x200)
#define MT_WFDMA0_TX_RING_BASE(n)   (MT_WFDMA0_BASE + 0x300 + (n) * 0x10)
```

### 3. Wrong FWDL Queue Index
**Problem**: Used ring 16 (MT7925 style), but MT7927 only has rings 0-7

**Fix**:
```c
// Before: #define FWDL_QUEUE_IDX 16
// After:
#define FWDL_QUEUE_IDX      4   /* MT7927 uses ring 4 for FWDL */
```

### 4. Incomplete Reset Check
**Problem**: Only checked RST_B_EN (bit 0), but should also verify INIT_DONE (bit 4)

**Fix**:
```c
#define MT_WFSYS_SW_INIT_DONE       BIT(4)

// Check both bits:
if ((val & (MT_WFSYS_SW_RST_B_EN | MT_WFSYS_SW_INIT_DONE)) ==
    (MT_WFSYS_SW_RST_B_EN | MT_WFSYS_SW_INIT_DONE)) {
```

### 5. Added Safety Checks
**Added**: Chip ID validation after BAR mapping to catch hung chip early:
```c
u32 chip_id = readl(dev->regs + 0x0000);
if (chip_id == 0xffffffff) {
    dev_err(&pdev->dev, "Chip not responding (0xffffffff)\n");
    return -EIO;
}
```

### Testing After Fixes
1. Disable ASPM L1 first: `sudo setpci -s $DEVICE CAP_EXP+10.w=0x0000`
2. Load module: `sudo insmod tests/05_dma_impl/test_fw_load.ko`
3. Check output: `sudo dmesg | tail -40`

## Production Driver Fixes (2026-01-30)

Critical bugs found and fixed in production driver after comprehensive code review:

### 1. DMA Prefetch Uses Wrong Ring Indices (CRITICAL)
**File**: `src/mt7927_dma.c:395-412`

**Problem**: Code configured prefetch for rings 15/16 (MT7925 style) but MT7927 uses rings 4/5:
```c
// Before (WRONG):
mt7927_wr(dev, MT_WFDMA0_TX_RING15_EXT_CTRL, PREFETCH(...));  /* MCU WM */
mt7927_wr(dev, MT_WFDMA0_TX_RING16_EXT_CTRL, PREFETCH(...));  /* FWDL */

// After (CORRECT):
mt7927_wr(dev, MT_WFDMA0_TX_RING_EXT_CTRL(4), PREFETCH(...));  /* FWDL ring 4 */
mt7927_wr(dev, MT_WFDMA0_TX_RING_EXT_CTRL(5), PREFETCH(...));  /* MCU WM ring 5 */
```

### 2. TX Interrupt Handler Swapped Queue Indices (CRITICAL)
**File**: `src/mt7927_pci.c:217-228`

**Problem**: Ring 4 completion went to tx_q[1], Ring 5 to tx_q[2] - backwards!
```c
// Before (WRONG):
if (intr & HOST_TX_DONE_INT_ENA4)
    mt7927_tx_complete(dev, &dev->tx_q[1]);  /* Should be tx_q[2] */
if (intr & HOST_TX_DONE_INT_ENA5)
    mt7927_tx_complete(dev, &dev->tx_q[2]);  /* Should be tx_q[1] */

// After (CORRECT):
if (intr & HOST_TX_DONE_INT_ENA4)
    mt7927_tx_complete(dev, &dev->tx_q[2]);  /* Ring 4 FWDL */
if (intr & HOST_TX_DONE_INT_ENA5)
    mt7927_tx_complete(dev, &dev->tx_q[1]);  /* Ring 5 MCU WM */
```

### 3. Ring Config Wipe Not Handled (HIGH)
**File**: `src/mt7927_dma.c:544-546`

**Problem**: Code detected ring config was wiped but continued execution with invalid DMA state.
**Fix**: Return -EIO when ring config is lost.

### 4. MCU Response Sequence Mismatch Only Warned (HIGH)
**File**: `src/mt7927_mcu.c:164-169`

**Problem**: Wrong response detected but processing continued anyway.
**Fix**: Return -EIO and free SKB on sequence mismatch.

### 5. wait_event_timeout Defensive Check (HIGH)
**File**: `src/mt7927_mcu.c:149-155`

**Problem**: Only checked `if (!ret)` but negative values indicate interruption.
**Fix**: Changed to `if (ret <= 0)` for defensive handling.

### 6. BAR Comments Clarified (MEDIUM)
**File**: `src/mt7927.h:107-108`

**Fix**: Updated comments to clarify BAR0 is 2MB main register space, BAR2 is 32KB read-only shadow.

### 7. Test Modules Fixed (CRITICAL)
**Files**: `tests/05_dma_impl/test_dma_queues.c`, `test_power_ctrl.c`, `test_wfsys_reset.c`

All test modules had same BAR2 issue as test_fw_load.c - fixed to use BAR0 with proper offsets.

## TX Ring Validation (2026-01-30) - CONFIRMED

### Hardware Validation Results

Created two diagnostic modules to validate the 8 TX ring hypothesis:

1. **`diag/mt7927_ring_scan_readonly.c`** - Safe read-only scanner
2. **`diag/mt7927_ring_scan_readwrite.c`** - Write test with safety features (dry_run default)

### Scan Results

```
Ring | BASE       | CNT    | CIDX | DIDX | EXT_CTRL   | Status
-----|------------|--------|------|------|------------|--------
   0 | 0x00000000 |    512 |    0 |    0 | 0x04000040 | VALID
   1 | 0x00000000 |    512 |    0 |    0 | 0x04000080 | VALID
   2 | 0x00000000 |    512 |    0 |    0 | 0x040000c0 | VALID
   3 | 0x00000000 |    512 |    0 |    0 | 0x04000100 | VALID
   4 | 0x00000000 |    512 |    0 |    0 | 0x04000140 | VALID
   5 | 0x00000000 |    512 |    0 |    0 | 0x04000180 | VALID
   6 | 0x00000000 |    512 |    0 |    0 | 0x040001c0 | VALID
   7 | 0x00000000 |    512 |    0 |    0 | 0x04000200 | VALID
   8 | 0x00000000 |      0 |    0 |    0 | 0x04000240 | INVALID
   9 | 0x00000000 |      0 |    0 |    0 | 0x04000280 | INVALID
  10 | 0x00000000 |      0 |    0 |    0 | 0x040002c0 | INVALID
  11 | 0x00000000 |      0 |    0 |    0 | 0x04000300 | INVALID
  12 | 0x00000000 |      0 |    0 |    0 | 0x04000340 | INVALID
  13 | 0x00000000 |      0 |    0 |    0 | 0x04000380 | INVALID
  14 | 0x00000000 |      0 |    0 |    0 | 0x040003c0 | INVALID
  15 | 0x00000000 |      0 |    0 |    0 | 0x04000400 | INVALID
  16 | 0x00000000 |      0 |    0 |    0 | 0x04000440 | INVALID
  17 | 0x00000000 |      0 |    0 |    0 | 0x04000480 | INVALID
```

### Key Findings

| Finding | Status | Evidence |
|---------|--------|----------|
| MT7927 has 8 TX rings (0-7) | **✓ CONFIRMED** | CNT=512 for rings 0-7, CNT=0 for rings 8-17 |
| Rings 4/5 physically exist | **✓ CONFIRMED** | Both show valid CNT=512 |
| Rings 15/16 don't exist | **✓ CONFIRMED** | Both show CNT=0 |
| ASPM L1 doesn't affect register reads | **✓ CONFIRMED** | Identical results with L1 disabled |

### Chip ID Location Clarified

**Mystery solved**: Why `mt7927_diag.ko` reads Chip ID correctly but others read 0x00000000:
- BAR2 is a read-only shadow of BAR0 at offset 0x10000
- Chip ID is at BAR0+0x10000, NOT BAR0+0x0000
- `mt7927_diag.c` maps BAR2, so BAR2+0x000 = correct Chip ID
- Other modules using BAR0+0x0000 read SRAM (0x00000000)

### Outstanding Uncertainties

1. **Ring 4/5 choice not firmware-validated**: These rings exist, but firmware may expect MCU commands on different rings. MT7925 uses 15/16, MT7921 uses different assignments again. Our choice is based on availability, not reverse-engineered firmware requirements.

2. **ASPM L1 effect on DMA**: Register reads work, but DMA processing may still be blocked by L1 power states. Test with L1 disabled before concluding.

## Code Style and Conventions

### Kernel Module Coding
- Follow Linux kernel coding style (kernel.org/doc/html/latest/process/coding-style.html)
- Use GPL v2 license headers
- Include SPDX identifiers in all source files
- Use `__le32` for hardware descriptor fields (little-endian)
- All register accesses via ioread32/iowrite32 (handles endianness and memory barriers)

### Register Access Patterns
```c
// Reading registers
u32 val = ioread32(dev->bar0 + offset);

// Writing registers
iowrite32(value, dev->bar0 + offset);

// BAR0 vs BAR2
// - BAR0: Main 2MB memory region (SRAM at 0x0, registers at 0x10000+, WFDMA at 0x2000)
// - BAR2: 32KB read-only shadow of BAR0+0x10000 (do NOT use for writes)
// - Chip ID: BAR0+0x10000 (or BAR2+0x0)
```

### Error Handling
- Always check for chip error state (0xffffffff reads indicate hung chip)
- Use pr_err/pr_info for kernel logging
- Include register values in error messages for debugging
- Timeouts should include current hardware state in error output

## Common Development Tasks

### Adding Diagnostic Code
Create test modules in `tests/05_dma_impl/` or `diag/` rather than modifying production driver. Test modules allow rapid iteration without risking production code stability.

### Test Module Requirements

**All test modules that perform DMA operations MUST:**
1. Disable ASPM L0s and L1 early in probe, before any DMA operations:
   ```c
   pci_disable_link_state(pdev, PCIE_LINK_STATE_L0S | PCIE_LINK_STATE_L1);
   ```
2. Use BAR0 (2MB) for register access, NOT BAR2 (read-only shadow)
3. Check for chip error state (0xffffffff) before proceeding

**Before asking user to load any module, ALWAYS:**
1. Run `make clean && make` (or appropriate target like `make tests`, `make diag`)
2. Verify the .ko file was rebuilt with the latest changes
3. Remind user to reboot if device state may be altered from previous tests
4. Run `mt7927_diag.ko` first to verify device state is consistent before critical tests

Example pre-load sequence:
```bash
# Clean and rebuild
make clean && make tests && make diag

# Verify device state first (after reboot if needed)
sudo insmod diag/mt7927_diag.ko
sudo dmesg | tail -20
sudo rmmod mt7927_diag

# Expected: Chip ID 0x00511163, FW_STATUS 0xffff10f1 (pre-init)
# If values differ, consider rebooting

# Then load the actual test module
sudo insmod tests/05_dma_impl/test_fw_load.ko
sudo dmesg | tail -60
```

### Reference Implementations

**For complete analysis of all reference sources, see [docs/REFERENCE_SOURCES.md](docs/REFERENCE_SOURCES.md)**

MT7927 driver development uses three reference sources in priority order:

#### Priority 1: MediaTek Vendor Kernel Modules (reference_mtk_modules/) - **MOST AUTHORITATIVE**

**Source**: Official MediaTek BSP from Xiaomi "rodin" device kernel tree
- Repository: https://github.com/Fede2782/MTK_modules (git submodule)
- Origin: Xiaomi device kernel (Author: Yue Kang <yuekang@xiaomi.com>, Feb 2025)
- License: BSD-2-Clause (WLAN core) + GPL-2.0 (chip drivers)

**Why this is most authoritative**:
1. **Official MediaTek code** - Not reverse-engineered or community code
2. **Explicit MT7927→MT6639 mapping** - PCI device table proves architectural relationship:
   ```c
   // File: connectivity/wlan/core/gen4m/os/linux/hif/pcie/pcie.c:170-172
   {   PCI_DEVICE(MTK_PCI_VENDOR_ID, NIC7927_PCIe_DEVICE_ID),  // 0x7927
       .driver_data = (kernel_ulong_t)&mt66xx_driver_data_mt6639},
   ```
3. **Production-tested** - Shipped in millions of Xiaomi devices
4. **Complete implementation** - All features, not just upstreamed subset
5. **Register-level documentation** - coda/ headers have exact bit definitions

**Key files**:
- `connectivity/wlan/core/gen4m/chips/mt6639/mt6639.c` - Complete chip implementation
- `connectivity/wlan/core/gen4m/chips/mt6639/mt6639.h` - Chip-specific constants
- `connectivity/wlan/core/gen4m/chips/mt6639/coda/mt6639/wf_wfdma_host_dma0.h` - DMA registers
- `connectivity/wlan/core/gen4m/chips/mt6639/coda/mt6639/wf_pse_top.h` - PSE registers
- `connectivity/wlan/core/gen4m/chips/mt6639/coda/mt6639/pcie_mac_ireg.h` - PCIe registers

**When to use**: Architecture, register definitions, initialization sequences, DMA configuration

#### Priority 2: Zouyonghao MT7927 Driver (reference_zouyonghao_mt7927/) - **WORKING IMPLEMENTATION**

**Source**: Community working driver
- Repository: https://github.com/zouyonghao/mt7927 (git submodule)
- Status: Successfully loads firmware and creates network interface

**Why this is critical**:
1. **Proven to work** - Firmware loading succeeds on MT7927 hardware
2. **Root cause discovery** - Proves polling-based protocol works (no mailbox)
3. **DMA validation** - Hardware works when correct protocol is used

**Key files**:
- `mt7927_fw_load.c` - Polling-based firmware loader (NO mailbox waits)

**When to use**: Firmware loading protocol, polling delays, TX cleanup patterns

#### Priority 3: Linux Kernel MT7925 (drivers/net/wireless/mediatek/mt76/mt7925/) - **CONNAC3X PATTERNS**

**Source**: Upstream Linux kernel driver
- License: GPL-2.0
- Status: Well-documented, high quality, but different architecture

**Why use with caution**:
1. CONNAC3X family patterns (firmware format, descriptor structures)
2. MT7925 has different ring layout (0-16 dense vs MT7927 sparse)
3. MT7925 mailbox protocol works in ROM (MT7927 ROM doesn't support it)

**Key files**:
- `mt7925/pci.c` - PCI infrastructure (use for patterns, not specifics)
- `mt7925/mcu.c` - MCU protocol (RUNTIME only, NOT firmware loading)
- `mt7925/mac.c` - MAC layer operations

**When to use**: General CONNAC3X patterns, mac80211 integration, power management (after firmware loads)

**When NOT to use**: Firmware loading protocol, ring assignments, initialization sequence

---

**Reference Priority Summary**:
1. **Architecture & Registers** → reference_mtk_modules (MT6639 official code)
2. **Firmware Loading** → reference_zouyonghao_mt7927 (polling-based, proven working)
3. **Network Operations** → Linux mt7925 (CONNAC3X family patterns, after init)

### Debugging DMA Issues
1. Check ring indices via test modules (test_dma_queues.ko shows CIDX/DIDX/CPU_DIDX)
2. Verify descriptor memory with coherent DMA allocation debug
3. Monitor RST register state (should be 0x30 during config)
4. Check MCU ready status (FW_OWN_REQ_SET should return 0x1)
5. Look for interrupt activity (IRQ handler logs)

## ⚠️ Device State Awareness During Testing

**IMPORTANT**: The MT7927 device state can become corrupted or altered during testing, especially after:
- Failed module loads or crashes
- Write tests to hardware registers
- Incomplete initialization sequences
- Power management state changes
- ASPM state modifications via setpci

### Symptoms of Altered Device State
- Chip ID reads as 0xffffffff (hung/unresponsive)
- Chip ID reads as 0x00000000 (wrong register or corrupted)
- Register values differ from expected fresh-boot values
- DMA rings show unexpected configuration
- Module loads fail with -EIO or timeout errors

### Recovery Options

**Option 1: PCI Rescan** (quick, often sufficient)
```bash
# Bash:
DEVICE=$(lspci -nn | grep 14c3:7927 | cut -d' ' -f1)
echo 1 | sudo tee /sys/bus/pci/devices/0000:$DEVICE/remove
sleep 2
echo 1 | sudo tee /sys/bus/pci/rescan

# Fish:
set DEVICE (lspci -nn | grep 14c3:7927 | cut -d' ' -f1)
echo 1 | sudo tee /sys/bus/pci/devices/0000:$DEVICE/remove
sleep 2
echo 1 | sudo tee /sys/bus/pci/rescan
```

**Option 2: Full System Reboot** (recommended when in doubt)

**When to reboot**:
- After kernel oops or panic related to MT7927
- After multiple failed test iterations
- Before running critical validation tests
- When PCI rescan doesn't restore expected register values
- When comparing results across different test scenarios (ensures consistent baseline)

**Best Practice**: When conducting systematic testing or validation, start each test session with a fresh reboot to ensure the device is in a known-good state.

## What NOT to Do

- **Don't write to random registers** - Can hang the chip (recovery requires PCI rescan)
- **Don't assume MT7925 code works directly** - Ring assignments differ, WFDMA1 doesn't exist
- **Don't modify production driver without tests** - Use test modules for experimentation
- **Don't clear WFDMA0_RST to 0x00** - Wipes ring configuration immediately
- **Don't access registers when chip is in error state** - Check for 0xffffffff first
- **Don't assume device state is clean after failed tests** - Reboot if uncertain

## Investigation Priorities

### 🎯 **ROOT CAUSE IDENTIFIED** (Phase 17 - 2026-01-31)

**MT7927 ROM bootloader does NOT support mailbox command protocol!**

Our driver:
- Sends PATCH_SEM_CONTROL command
- **Waits for mailbox response** ← **BLOCKS HERE FOREVER**
- ROM bootloader never sends responses
- Timeout after 5 seconds
- Never proceeds to firmware loading

**Solution**: Polling-based firmware loading (reference_zouyonghao_mt7927)

### Implementation Path

**Quick Test** (validate hypothesis):
```c
// In src/mt7927_mcu.c - change one line:
ret = mt76_mcu_send_msg(&dev->mt76, MCU_CMD_PATCH_SEM_CONTROL,
                       &req, sizeof(req), false);  // Don't wait!
```

**Full Solution**:
1. Create `src/mt7927_fw_load.c` (polling-based, no mailbox)
2. Skip semaphore command entirely
3. Add aggressive TX cleanup (before+after each chunk)
4. Add polling delays (5-50ms for ROM processing)
5. Skip FW_START, manually set SW_INIT_DONE (0x7C000140 bit 4)
6. Poll status registers (0x7c060204, 0x7c0600f0)

See **docs/ZOUYONGHAO_ANALYSIS.md** for complete implementation details.

### Invalidated Hypotheses

1. ~~**L1 ASPM blocking DMA**~~ - Working driver has L1 enabled, same as ours
2. ~~**Ring assignment wrong**~~ - Rings 15/16 validated via MT6639 analysis
3. ~~**DMA configuration incorrect**~~ - Working driver uses same config
4. ~~**Missing initialization step**~~ - All init steps are correct

**The problem was always the mailbox protocol assumption!**

## Reference Documentation

All technical documentation is in `docs/` directory:
- **docs/README.md** - Documentation navigation
- **docs/mt7927_pci_documentation.md** - PCI layer details
- **docs/dma_mcu_documentation.md** - DMA/MCU implementation
- **docs/headers_documentation.md** - Complete register reference
- **docs/test_modules_documentation.md** - Test framework guide
- **docs/diagnostic_modules_documentation.md** - Diagnostic tools
- **docs/TEST_RESULTS_SUMMARY.md** - Validation status

## Firmware Files

Uses MT7925 firmware (confirmed compatible):
```
/lib/firmware/mediatek/mt7925/
├── WIFI_MT7925_PATCH_MCU_1_1_hdr.bin  # MCU patch
└── WIFI_RAM_CODE_MT7925_1_1.bin        # RAM code (1.4MB)
```

Download from: https://git.kernel.org/pub/scm/linux/kernel/git/firmware/linux-firmware.git/tree/mediatek/mt7925

## Hardware Requirements

- **Chip**: MediaTek MT7927 WiFi 7 (PCI ID: 14c3:7927)
- **Kernel**: Linux 6.7+ (for mt7925 infrastructure)
- **Memory**: 2MB+ for firmware and DMA buffers
- **PCI Slot**: Confirmed working at 0000:0a:00.0 (update commands if different)
