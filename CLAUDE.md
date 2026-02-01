# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Context

This is a Linux kernel driver for the MediaTek MT7927 WiFi 7 chipset. **CRITICAL DISCOVERY (2026-01-31)**: MT7927 is an **MT6639 variant** (NOT MT7925 as initially assumed). MediaTek's kernel modules explicitly map MT7927 to MT6639 driver data. MT6639 and MT7925 share firmware (CONNAC3X family) and ring protocol, explaining our earlier firmware compatibility findings. See **docs/MT6639_ANALYSIS.md** for complete evidence.

**Official Product Name**: MT7927 802.11be 320MHz 2x2 PCIe Wireless Network Adapter [Filogic 380]

**Current Status**: **PHASE 29c - DMA PATH FAILURE CONFIRMED**
- ✅ **Ring configuration correct** - HW desc_base matches desc_dma (verified!)
- ✅ **Firmware transfer completes** - All 5 RAM regions sent "successfully"
- ❌ **MCU never acknowledges** - Status stays 0x00000000 after all regions
- ❌ **Firmware not ready** - FW_N9_RDY stuck at 0x00000002 (download mode, N9 not ready)
- ❌ **FW_START times out** - No mailbox response when waiting (tested wait=true)
- 🔬 **Current blocker**: DMA data not reaching device memory

**Phase 29c Finding** (2026-02-01):

**FW_START wait=true experiment confirms DMA path failure.**

Tested FW_START with `wait=true` instead of `wait=false`:
```
[MT7927] Sending FW_START command (waiting for response)
Message 00000002 (seq 9) timeout
[MT7927] FW_START send failed: -110 (continuing)
[MT7927] Waiting for FW_N9_RDY... (0x00000002)   <- BIT(1) set, BIT(0) clear
```

**Key findings**:
- MCU status = 0x00000000 after ALL firmware regions (never changes)
- FW_N9_RDY = 0x00000002: Download mode entered, but N9 CPU not ready
- FW_START times out: Firmware not executing, cannot respond
- wait=true vs wait=false: Same result, just longer timeout

**Root cause**: DMA transfers complete from host's perspective but data never reaches device memory.

See **DEVELOPMENT_LOG.md** Phase 29c for complete analysis.

## Critical Files to Review First

1. **docs/ZOUYONGHAO_ANALYSIS.md** - 🎯 Complete DMA fix history
   - Section "2a": GLO_CFG timing difference (Phase 26)
   - Section "2b": Phase 27 findings - DMA page fault analysis
   - Section "2c": Phase 27 fix - Unused TX rings initialization
   - Section "2d": Phase 27b fix - RX_DMA_EN timing
   - Section "2e": Phase 27c fix - TXD control word bit positions (verified!)
   - Section "2f": Phase 27d - Global DMA path investigation
   - Section "2g": Phase 27e - HOST2MCU software interrupt discovery
   - Section "2h": Phase 27f - Firmware structure mismatch discovery
   - Section "2i": Phase 27f - Structure fixes and register verification
   - Section "2j": Phase 27g - Comprehensive memory reference verification
   - Section "2k": Phase 27g - test_fw_load.c alignment with zouyonghao
   - Section "2l": Phase 28 - DMA memory access failure analysis
   - Section "2m": Phase 28b - Zouyonghao config additions test
   - Section "2n": Phase 28c - DMA path verification insight
   - Section "2o": Phase 28d - DMA path investigation results
   - Section "2p": Phase 29 - Linux 6.18 adaptation
   - Section "2q": Phase 29b - IOMMU page fault deep analysis
   - Section "2r": **Phase 29c** - FW_START response test (CURRENT)
2. **docs/ROADMAP.md** - Current status, blockers, and next steps
3. **docs/MT6639_ANALYSIS.md** - Proves MT7927 is MT6639 variant, validates rings 15/16
4. **docs/MT7996_COMPARISON.md** - Comparison analysis: why MT7927 is NOT an MT7996 variant
5. **docs/REFERENCE_SOURCES.md** - Analysis of reference code origins and authoritative sources
6. **DEVELOPMENT_LOG.md** - Complete chronological history (29+ phases, Phase 29 = current)
7. **AGENTS.md** - Session bootstrap with current blocker details, hardware context, and what's been tried
8. **README.md** - Project overview, build instructions, expected outputs

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
# Build and load the working zouyonghao driver (Phase 29 - firmware loads!)
cd reference_zouyonghao_mt7927/mt76-outoftree
make clean && make
sudo rmmod mt7925e mt7925_common mt792x_lib mt76_connac_lib mt76 2>/dev/null
sudo insmod mt76.ko && sudo insmod mt76-connac-lib.ko && sudo insmod mt792x-lib.ko
sudo insmod mt7925/mt7925-common.ko && sudo insmod mt7925/mt7925e.ko
sudo dmesg | tail -60

# Check if wlan interface was created
ip link | grep wlan
iw dev

# Load original production driver (development/testing)
sudo rmmod mt7927 2>/dev/null
sudo insmod src/mt7927.ko
sudo dmesg | tail -40

# Load test module (for DMA debugging)
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

# Note: ASPM L1 is NOT the DMA blocker (disproven). Disabling is optional for debugging:
# Bash: DEVICE=$(lspci -nn | grep 14c3:7927 | cut -d' ' -f1); sudo setpci -s $DEVICE CAP_EXP+10.w=0x0000
# Fish: set DEVICE (lspci -nn | grep 14c3:7927 | cut -d' ' -f1); sudo setpci -s $DEVICE CAP_EXP+10.w=0x0000
```

## Code Architecture

### Source Layout

```
src/
├── mt7927_pci.c    # PCI probe, power management (LPCTL handshake), WFSYS reset
├── mt7927_dma.c    # DMA ring allocation/configuration (sparse TX: 0-3,8-11,14-16)
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
   - Configure sparse TX ring layout (0,1,2,15,16) and RX rings
   - Set ring base addresses, counts, and indices
   - **MT7927-specific**: Uses rings 15 (MCU_WM) and 16 (FWDL) - same as MT6639/CONNAC3X standard

3. **Firmware Load** (mt7927_mcu.c) - **ROOT CAUSE FOUND**
   - ~~Send PATCH_SEM_CONTROL command via TX ring 15 (MCU_WM)~~ - ROM doesn't support mailbox
   - Transfer firmware chunks via TX ring 16 (FWDL) using **polling mode**
   - Skip FW_START, manually set SW_INIT_DONE
   - **Solution**: Use polling-based loading (see ZOUYONGHAO_ANALYSIS.md)

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
| ASPM L1 | Enabled (normal - NOT the blocker) |
| L1.1 Substate | Enabled (normal) |
| L1.2 Substate | Enabled (normal) |

### Key Register Locations (BAR0 offsets)

**WFDMA0 Registers** (via bus2chip mapping: chip 0x7C024000 → BAR0 0xd4000):
```c
WFDMA0_BASE        = 0xd4000     // WFDMA Host DMA0 base (NOT 0x2000!)
WFDMA0_RST         = 0xd4100     // DMA reset control (bits 4,5)
WFDMA0_GLO_CFG     = 0xd4208     // DMA global config (TX/RX enable)
TX_RING_BASE(n)    = 0xd4300 + n*0x10
TX_RING15_CTRL0    = 0xd43F0     // Ring 15 (MCU_WM)
TX_RING16_CTRL0    = 0xd4400     // Ring 16 (FWDL)
```

**Chip addresses via fixed_map translation** (0x7cXXXXXX → BAR0):
```c
LPCTL              = 0xe0010     // 0x7c060010 → BAR0 (power management)
WFSYS_SW_RST_B     = 0xf0140     // 0x7c000140 → BAR0 (WiFi reset)
MCU_ROMCODE_INDEX  = 0xc1604     // 0x81021604 → BAR0 (MCU state, expect 0x1D1E)
```

**CB_INFRA Registers** (via 0x70020000 mapping → BAR0 0x1f0000):
```c
CB_INFRA_PCIE_REMAP_WF     = 0x1f6554  // MUST set to 0x74037001 before WFDMA access!
CB_INFRA_PCIE_REMAP_WF_BT  = 0x1f6558  // Set to 0x70007000
CB_INFRA_WF_SUBSYS_RST     = 0x1f8600  // WF reset via CB_INFRA_RGU (bit 4)
CB_INFRA_CRYPTO_MCU_OWN    = 0x1f5034  // Set BIT(0) for MCU ownership
```

## Root Cause (FOUND 2026-01-31, REFINED Phase 22-23)

### Issue 1: Mailbox Protocol Not Supported
**MT7927 ROM bootloader does NOT support mailbox command protocol!**

The driver was:
1. Sending PATCH_SEM_CONTROL command
2. **Waiting for mailbox response** ← Blocked here forever
3. ROM bootloader never responds (mailbox not implemented)
4. Driver times out, never proceeds to firmware loading

### Issue 2: Wrong WFDMA Register Base Address
**WFDMA registers were accessed at wrong BAR0 offset!**

- Previous documentation said WFDMA0 at **0x2000** (wrong - this is MT7921)
- Correct address: **0xd4000** (from MT6639 bus2chip mapping)
- All ring configuration writes went to wrong addresses

### Issue 3: Missing CB_INFRA Initialization
**PCIe remap registers must be set before WFDMA access works!**

From MediaTek vendor code `mt6639_mcu_init()`:
```c
// MANDATORY - set BEFORE any WFDMA register access
writel(0x74037001, bar0 + 0x1f6554);  // PCIE_REMAP_WF
writel(0x70007000, bar0 + 0x1f6558);  // PCIE_REMAP_WF_BT
```

### Issue 4: Missing GLO_CFG Clock Gating Disable (Phase 22)
**Ring register writes fail without `clk_gate_dis` (BIT 30) set in GLO_CFG!**

Hardware testing showed ring BASE and EXT_CTRL reads back as 0x00000000, while CNT worked. Analysis of MediaTek's `asicConnac3xWfdmaControl()` revealed missing GLO_CFG bits:

```c
// CRITICAL: GLO_CFG must be set BEFORE ring configuration!
// Expected value: 0x5030B870 (without TX/RX_DMA_EN)
// Final value:    0x5030B875 (with TX/RX_DMA_EN)
#define MT_WFDMA0_GLO_CFG_SETUP \
    (PDMA_BT_SIZE(3) |         /* bits 4-5: burst size */ \
     TX_WB_DDONE |             /* BIT(6) */ \
     CSR_AXI_BUFRDY_BYP |      /* BIT(11) */ \
     FIFO_LITTLE_ENDIAN |      /* BIT(12) */ \
     CSR_RX_WB_DDONE |         /* BIT(13) */ \
     CSR_DISP_BASE_PTR_CHAIN_EN | /* BIT(15) */ \
     CSR_LBK_RX_Q_SEL_EN |     /* BIT(20) */ \
     OMIT_RX_INFO_PFET2 |      /* BIT(21) */ \
     OMIT_TX_INFO |            /* BIT(28) */ \
     CLK_GATE_DIS)             /* BIT(30) - CRITICAL! */
```

### Issue 5: Missing Register Definitions & Fixed Map Entries (Phase 23)
**Our `src/mt7927_regs.h` is missing critical definitions from the zouyonghao mt76-based driver!**

**Missing CB_INFRA register definitions:**
```c
#define CB_INFRA_RGU_WF_SUBSYS_RST_ADDR         0x70028600  // WF reset register
#define CB_INFRA_MISC0_CBTOP_PCIE_REMAP_WF_ADDR 0x70026554  // PCIe remap WF
#define CB_INFRA_SLP_CTRL_CRYPTO_MCU_OWN_ADDR   0x70025380  // Crypto MCU ownership
#define WF_TOP_CFG_ON_ROMCODE_INDEX_ADDR        0x81021604  // ROM state (expect 0x1D1E)
#define CBTOP_GPIO_MODE5_MOD_ADDR               0x7000535c  // GPIO mode 5
#define CBTOP_GPIO_MODE6_MOD_ADDR               0x7000536c  // GPIO mode 6
```

**Missing configuration values:**
```c
#define MT7927_WF_SUBSYS_RST_ASSERT             0x10351
#define MT7927_WF_SUBSYS_RST_DEASSERT           0x10340
#define MT7927_CBTOP_PCIE_REMAP_WF_VALUE        0x74037001
#define MT7927_CBTOP_PCIE_REMAP_WF_BT_VALUE     0x70007000
#define MT7927_GPIO_MODE5_VALUE                 0x80000000
#define MT7927_GPIO_MODE6_VALUE                 0x80
```

**Missing fixed_map entries:**
| Chip Address | BAR0 Offset | Purpose |
|--------------|-------------|---------|
| 0x7c010000 | 0x100000 | CONN_INFRA (CONN_CFG) |
| 0x7c030000 | 0x1a0000 | CONN_INFRA_ON_CCIF |
| 0x70000000 | 0x1e0000 | CBTOP low range |

### Complete Solution

1. **Set CB_INFRA PCIe remap** (0x74037001)
2. **WF subsystem reset** via CB_INFRA_RGU (BAR0+0x1f8600), not just WFSYS_SW_RST_B
3. **Set crypto MCU ownership** (BAR0+0x1f5034 = BIT(0))
4. **Wait for MCU IDLE** (0x1D1E at BAR0+0xc1604)
5. **Set GLO_CFG = 0x5030B870** (includes clk_gate_dis, BEFORE ring config!)
6. **Configure WFDMA rings** at correct base **0xd4000**
7. **Set GLO_CFG = 0x5030B875** (add TX/RX_DMA_EN)
8. **Polling-based firmware loading** (no mailbox waits)

See **docs/ZOUYONGHAO_ANALYSIS.md** and **reference_mtk_modules/.../mt6639.c** for implementation details.

### Phase 20 Findings (January 2026)

**Deep analysis of reference_mtk_modules/ and reference_gen4m/** revealed:

| Finding | Old Assumption | Correct Value | Source |
|---------|----------------|---------------|--------|
| Crypto MCU ownership | 0x70025380 | **0x70025034** (SET register) | cb_infra_slp_ctrl.h |
| PCIe remap WF | Unknown | **0x74037001** | mt6639.c:2727 |
| PCIe remap WF_BT | Unknown | **0x70007000** | mt6639.c:2728 |
| GPIO_MODE5 | Placeholder | **0x80000000** | mt6639.c:2651 |
| GPIO_MODE6 | Placeholder | **0x80** | mt6639.c:2653 |
| Ring 16 prefetch | Unknown | **0x4** | mt6639.c:1395 |
| FW loading mode | Mailbox wait | **Polling** (fgCheckStatus=FALSE) | fw_dl.c |

### Phase 23 Findings (January 2026)

**Analysis of zouyonghao mt76-based driver** (alternative branch with complete mt76 fork):

| Finding | Our Current State | Correct Value/Approach | Source |
|---------|-------------------|------------------------|--------|
| WFDMA base | 0x2000 (MCU DMA!) | **0xd4000** (HOST DMA) | mt7925/pci.c fixed_map |
| CB_INFRA registers | Missing | Full CB_INFRA_RGU, MISC0, SLP_CTRL | mt7927_regs.h |
| Pre-init sequence | None | CONN_INFRA wakeup → version poll → WF reset → MCU IDLE | pci_mcu.c:mt7927e_mcu_pre_init() |
| Firmware loading | Mailbox waits | **wait=false**, aggressive TX cleanup | mt7927_fw_load.c |
| FW_START command | Sent to MCU | **Skipped** - set SW_INIT_DONE manually | mt7927_fw_load.c:270-278 |
| Fixed map entries | Missing 3 | Add 0x7c010000, 0x7c030000, 0x70000000 | pci.c:fixed_map_mt7927 |

**Key zouyonghao implementation patterns:**
```c
/* 1. Send commands without waiting for mailbox response */
return mt76_mcu_send_msg(dev, cmd, data, len, false);  // wait=false!

/* 2. Aggressive cleanup before and after each chunk */
dev->queue_ops->tx_cleanup(dev, dev->q_mcu[MT_MCUQ_FWDL], true);

/* 3. Skip FW_START, manually set SW_INIT_DONE */
__mt76_wr(dev, 0x7C000140, ap2wf | BIT(4));
```

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

### 2. Wrong WFDMA Register Offsets (FURTHER CORRECTED)
**Problem**: Initial fix used 0x2000 base, but correct base is **0xd4000**!

**Current correct values** (from MT6639 vendor code):
```c
#define MT_WFDMA0_BASE              0xd4000  // NOT 0x2000!
#define MT_WFDMA0_GLO_CFG           (MT_WFDMA0_BASE + 0x208)   // = 0xd4208
#define MT_WFDMA0_HOST_INT_STA      (MT_WFDMA0_BASE + 0x200)   // = 0xd4200
#define MT_WFDMA0_TX_RING_BASE(n)   (MT_WFDMA0_BASE + 0x300 + (n) * 0x10)
// Ring 15 = 0xd43F0, Ring 16 = 0xd4400
```

**Note**: test_fw_load.c was updated to use 0xd4000 in a previous session.

### 3. Ring Assignment Clarified
**Note**: MT7927 uses **sparse ring layout** like MT6639 (rings 0,1,2,15,16), NOT dense layout like MT7925 (0-16).

**Current correct assignment** (validated via MT6639 analysis):
```c
#define FWDL_RING_IDX       16   /* Ring 16 for FWDL (CONNAC3X standard) */
#define MCU_WM_RING_IDX     15   /* Ring 15 for MCU commands */
```

**Note**: Physical ring CNT=0 for rings 8-17 in uninitialized state is normal. Driver initializes CNT when configuring rings.

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

### 1. DMA Prefetch Ring Indices (Clarified)
**File**: `src/mt7927_dma.c`

**Current correct configuration** (MT6639/CONNAC3X standard):
```c
// Ring 15 = MCU_WM, Ring 16 = FWDL (validated via MT6639 analysis)
mt7927_wr(dev, MT_WFDMA0_TX_RING_EXT_CTRL(15), PREFETCH(...));  /* MCU WM */
mt7927_wr(dev, MT_WFDMA0_TX_RING_EXT_CTRL(16), PREFETCH(...));  /* FWDL */
```

### 2. TX Interrupt Handler Queue Indices (Clarified)
**File**: `src/mt7927_pci.c`

**Current correct configuration**:
```c
// Ring 15 = MCU_WM, Ring 16 = FWDL
if (intr & HOST_TX_DONE_INT_ENA15)
    mt7927_tx_complete(dev, &dev->tx_q[MT_MCUQ_WM]);   /* Ring 15 MCU */
if (intr & HOST_TX_DONE_INT_ENA16)
    mt7927_tx_complete(dev, &dev->tx_q[MT_MCUQ_FWDL]); /* Ring 16 FWDL */
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

// BAR0 memory map (2MB)
// - 0x00000-0x0FFFF: SRAM
// - 0x10000+: Chip registers (via BAR2 shadow)
// - 0xc0000+: WF_TOP_MISC_ON (MCU ROMCODE at 0xc1604)
// - 0xd0000+: CONN_INFRA WFDMA area
// - 0xd4000+: WFDMA Host DMA0 registers (NOT 0x2000!)
// - 0xe0000+: CONN_HOST_CSR_TOP (LPCTL at 0xe0010)
// - 0xf0000+: CONN_INFRA (WFSYS_SW_RST_B at 0xf0140)
// - 0x1f0000+: CB_INFRA/CBTOP (PCIe remap, WF reset, crypto)

// BAR2 (32KB) - Read-only shadow of BAR0+0x10000
// - Chip ID: BAR2+0x0 (or BAR0+0x10000)
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
- `connectivity/wlan/core/gen4m/chips/common/fw_dl.c` - **Firmware download** (2995 lines, uses polling mode!)
- `connectivity/wlan/core/gen4m/include/chips/coda/mt6639/cb_infra_*.h` - CB_INFRA register definitions
- `connectivity/wlan/core/gen4m/include/chips/coda/mt6639/wf_wfdma_host_dma0.h` - DMA registers
- `connectivity/wlan/core/gen4m/os/linux/hif/pcie/pcie.c` - PCI device table (MT7927→MT6639!)

**When to use**: Architecture, register definitions, initialization sequences, DMA configuration

#### Priority 1b: Gen4M WLAN Driver (reference_gen4m/) - **INITIALIZATION DETAILS**

**Source**: MediaTek gen4m WLAN driver (same structure as reference_mtk_modules but separate repository)

**Critical values discovered (Phase 20)**:
```c
// From reference_gen4m/chips/mt6639/mt6639.c:2727-2737
CB_INFRA_PCIE_REMAP_WF     = 0x74037001  // MUST set before WFDMA access
CB_INFRA_PCIE_REMAP_WF_BT  = 0x70007000

// From reference_gen4m/chips/mt6639/mt6639.c:2651-2653
CBTOP_GPIO_MODE5           = 0x80000000
CBTOP_GPIO_MODE6           = 0x80

// From reference_gen4m/chips/mt6639/mt6639.c:2660-2669
WF_SUBSYS_RST_ASSERT       = 0x10351
WF_SUBSYS_RST_DEASSERT     = 0x10340

// From reference_gen4m/chips/mt6639/mt6639.c:1395
RING_16_PREFETCH_CNT       = 0x4
```

**Key files**:
- `chips/mt6639/mt6639.c` - Init sequences with CB_INFRA values (lines 2603-2838)
- `chips/common/fw_dl.c` - Firmware loading (**fgCheckStatus=FALSE** = polling mode!)
- `include/chips/coda/mt6639/cb_infra_slp_ctrl.h` - **Crypto MCU ownership at 0x70025034**

**When to use**: CB_INFRA register values, GPIO mode setup, polling-mode firmware loading patterns

#### Priority 2: Zouyonghao MT7927 Driver (reference_zouyonghao_mt7927/) - **REFERENCE IMPLEMENTATION**

**Source**: Community driver with correct firmware loading functions
- Repository: https://github.com/zouyonghao/mt7927 (git submodule)
- Status: Has correct polling-based FW loader, but **wiring is incomplete** (see below)

**Why this is valuable**:
1. **Correct polling protocol** - `mt7927_fw_load.c` shows NO mailbox waits
2. **Root cause insight** - Comments explain why mailbox protocol fails
3. **DMA validation** - Hardware works when correct protocol is used

**✅ CRITICAL GAP FIXED (Phase 29, 2026-01-31)**:
The zouyonghao code had correct firmware loading *functions* (`mt7927_load_patch`, `mt7927_load_ram`) but they were **never called**!
- `mt7927e_mcu_init()` said "firmware already loaded" but skipped `mt792x_load_firmware()`
- **FIXED**: Added actual call to `mt792x_load_firmware()` in `pci_mcu.c`
- **FIXED**: Skip LPCTL power control for MT7927 (not usable before reset sequence)
- **FIXED**: Use MT7925 firmware paths (confirmed CONNAC3X compatible)
- **FIXED**: Linux 6.18 API compatibility (hrtimer, mac80211 MLO callbacks)

**Key files**:
- `mt7927_fw_load.c` - Polling-based firmware loader (working!)
- `pci_mcu.c:mt7927e_mcu_init()` - **NOW calls firmware loader correctly**
- `mt792x_core.c:mt792x_load_firmware()` - Has MT7927 detection, calls polling loader

**When to use**: This is now the primary driver! Build with `make` in `reference_zouyonghao_mt7927/mt76-outoftree/`

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
1. **Working Driver** → reference_zouyonghao_mt7927/mt76-outoftree/ (firmware loads! Phase 29)
2. **Architecture & Registers** → reference_mtk_modules (MT6639 official code)
3. **CB_INFRA Values & Init Sequences** → reference_gen4m (exact register values for init)
4. **Network Operations** → Linux mt7925 (CONNAC3X family patterns, after init)

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

## Implementation Path

**Root causes identified (Phase 21)**:
1. **Wrong WFDMA base**: BAR0+0x2000 is MCU DMA; BAR0+0xd4000 is HOST DMA
2. **Mailbox not supported**: MT7927 ROM doesn't respond to mailbox commands

**Full Solution**:
1. Set CB_INFRA PCIe remap (0x74037001 at BAR0+0x1f6554)
2. Set crypto MCU ownership (BIT(0) at BAR0+0x1f5034)
3. Configure WFDMA at correct base **0xd4000** (not 0x2000)
4. Use polling-based firmware loading (no mailbox waits)
5. Skip PATCH_SEM_CONTROL command entirely
6. Add polling delays (5-50ms for ROM processing)
7. Skip FW_START, manually set SW_INIT_DONE (0x7C000140 bit 4)

See **docs/ZOUYONGHAO_ANALYSIS.md** for complete implementation details.

## Reference Documentation

All technical documentation is in `docs/` directory:
- **docs/ZOUYONGHAO_ANALYSIS.md** - 🎯 **ROOT CAUSE** - Polling-based firmware loading solution
- **docs/MT6639_ANALYSIS.md** - Architecture proof (MT7927 = MT6639 variant)
- **docs/REFERENCE_SOURCES.md** - Analysis of all reference code sources
- **docs/README.md** - Documentation navigation
- **docs/mt7927_pci_documentation.md** - PCI layer details
- **docs/dma_mcu_documentation.md** - DMA/MCU implementation (note: mailbox protocol doesn't work for ROM)
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
- **Kernel**: Linux 6.18+ (tested), 6.7+ (minimum for mt7925 infrastructure)
- **Memory**: 2MB+ for firmware and DMA buffers
- **PCI Slot**: Confirmed working at 0000:0a:00.0 (update commands if different)
