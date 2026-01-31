# MT7927 WiFi 7 Linux Driver Development - Session Bootstrap

## Critical Discoveries

1. **MT7927 = MT6639 variant** (NOT MT7925 as initially assumed). MediaTek kernel modules prove this. MT7925 firmware is compatible (CONNAC3X shared firmware).

2. **ROOT CAUSE FOUND (Phase 17)**: MT7927 ROM bootloader does **NOT support mailbox protocol**! DMA hardware works fine - we're using the wrong communication protocol.

3. **WIRING GAP (Phase 18)**: Zouyonghao reference driver has correct polling-based FW loader functions, but they are **never called** - firmware loading is not wired into MCU init!

4. **REGISTER MAPPING CLARIFIED (Phase 19)**: MediaTek vendor code confirms WFDMA0 is at BAR0 offset **0xd4000** (NOT 0x2000). Previous documentation had wrong base address. Also discovered mandatory CB_INFRA PCIe remap initialization.

5. **CB_INFRA VALUES CONFIRMED (Phase 20)**: Deep analysis of both `reference_mtk_modules/` and `reference_gen4m/` confirmed exact register values:
   - PCIe Remap WF: **0x74037001** (from mt6639.c:2727-2737)
   - PCIe Remap WF_BT: **0x70007000**
   - Crypto MCU ownership uses **SET register** at 0x70025034 (not status at 0x70025030)
   - Firmware loading confirmed to use **polling mode** (`fgCheckStatus=FALSE` in fw_dl.c)

6. **⚠️ CRITICAL BUG FOUND (Phase 21)**: Driver was writing to WRONG DMA engine!
   - BAR0+0x2000 = **MCU DMA** (for chip's internal MCU, NOT for host)
   - BAR0+0xd4000 = **HOST DMA** (what firmware loading actually needs!)
   - All previous ring configuration went to wrong address space
   - Ring 15 should be at **0xd43f0** (not 0x23f0)
   - Ring 16 should be at **0xd4400** (not 0x2400)
   - From MT6639 bus2chip: `{0x7c020000, 0xd0000, 0x10000}` → chip 0x7c024000 = BAR0+0xd4000

7. **✅ MCU IDLE CONFIRMED (Phase 24)**: First successful MCU IDLE (0x1D1E) - hardware initialization progresses correctly!

8. **⚠️ RING CONFIG FAILURE (Phase 25)**: Ring configuration registers not accepting writes despite correct WFDMA base. Missing DMA initialization steps identified.

9. **🔧 PHASE 26 FIXES**: Implemented 6 critical differences from zouyonghao reference:
   - **DMA priority registers**: Added `INT_RX_PRI=0x0F00`, `INT_TX_PRI=0x7F00`
   - **GLO_CFG_EXT1 BIT(28)**: Added MT7927-specific enable bit
   - **WFDMA_DUMMY_CR**: Added `MT_WFDMA_NEED_REINIT` flag
   - **Reset scope**: Changed `RST_DTX_PTR` to `~0` (all rings)
   - **Descriptor init**: Set DMA_DONE bit on all descriptors (was memset to 0)
   - **DIDX write**: Now write both CIDX and DIDX to 0

10. **✅ GLO_CFG TIMING FIX (Phase 27)**: Fixed ring configuration by reordering:
    - Clear GLO_CFG BEFORE ring configuration
    - Set CLK_GAT_DIS AFTER ring configuration
    - Ring 15/16 BASE and EXT_CTRL now accept writes!

11. **✅ PAGE FAULT ROOT CAUSE (Phase 27)**: DMA page faults at address 0x0 were caused by:
    - **15 unused TX rings (0-14) had BASE=0**
    - DMA engine scans ALL rings when enabled → tries to fetch from 0x0 → IOMMU fault
    - **Fix implemented**: Initialize all unused rings to valid DMA address

12. **✅ RX_DMA_EN TIMING (Phase 27b)**: After TX ring fix, 3 page faults remained:
    - **RX rings have BASE=0** (not initialized) but `RX_DMA_EN` was enabled
    - DMA engine scans RX rings → access 0x0 → IOMMU fault → DMA halts → DIDX stuck at 0
    - **Fix implemented**: Only enable `TX_DMA_EN` during firmware loading
    - RX_DMA_EN should be enabled later after RX rings are properly configured

13. **✅ TXD CONTROL WORD FIX VERIFIED (Phase 27c)**: TRUE root cause of page faults FIXED:
    - **TXD descriptor control word bit layout was WRONG!**
    - SDLen0 was in bits 0-13 (should be **bits 16-29**)
    - LastSec0 was in bit 14 (should be **bit 30**)
    - With ctrl=0x404C, hardware read SDLen1=76 bytes at SDPtr1=0x0 → DMA tried to fetch from address 0!
    - **Fix verified**: Control word now shows `ctrl=0x404c0000` (correct format)
    - **No more AMD-Vi IO_PAGE_FAULT errors!**

14. **⚠️ GLOBAL DMA PATH ISSUE (Phase 27d)**: Critical finding from diagnostic run:
    - **BOTH Ring 15 AND Ring 16 have DIDX stuck at 0** - NOT a Ring 16-specific issue!
    - Ring 15 (MCU_WM): CIDX=7, DIDX=0 - commands not processed
    - Ring 16 (FWDL): CIDX=50, DIDX=0 - data not processed
    - `TX_TIMEOUT=1` error at end - DMA tried to send but MCU didn't acknowledge
    - `tx_done_int_sts_15=1` but DIDX=0 - interrupt may be stale or spurious
    - **Root cause**: MCU/target side not accepting DMA data
    - Hypothesis: MCU_DMA0 RX not enabled, or PDA not configured

## Current Status

**Status**: 🔧 PHASE 27d - RING 16 DIDX INVESTIGATION
**Last Updated**: January 2026 (Phase 27c fix verified)

### What's Working ✅
- Driver successfully binds to MT7927 hardware (PCI ID: 14c3:7927)
- Power management handshake completes (LPCTL: 0x04 → 0x00)
- WiFi subsystem reset completes (INIT_DONE achieved)
- CB_INFRA PCIe remap configured (0x74037001)
- **MCU reaches IDLE state (0x1D1E)** - Confirmed! ✅
- CONN_INFRA version correct (0x03010002)
- GLO_CFG setup with CLK_GATE_DIS works
- WFDMA extensions configured correctly
- **Ring configuration registers NOW work** (Phase 27 GLO_CFG timing fix)
  - Ring 15 BASE=0xffffb000, EXT_CTRL=0x05000004 ✅
  - Ring 16 BASE=0xffffc000, EXT_CTRL=0x05400004 ✅
- **All TX rings 0-16 have valid BASE addresses** (Phase 27 unused ring fix)
- **TXD control word format CORRECT** (Phase 27c fix verified)
  - `ctrl=0x404c0000` (SDLen0 in bits 16-29, LastSec0 in bit 30)
  - **No more AMD-Vi IO_PAGE_FAULT errors!**
- Ring 15 (MCU_WM) shows DMA activity - tx_done_int_sts_15 set
- Correct WFDMA base address (0xd4000)
- L0S and L1 ASPM disabled
- Firmware files load into kernel memory (1.4MB RAM code + patch)

### What Was Fixed (Phase 27c) ✅ VERIFIED
- **TXD control word fix WORKING**: No more page faults!
- SDLen0 now correctly in **bits 16-29** (was bits 0-13)
- LastSec0 now correctly in **bit 30** (was bit 14)
- Control word shows `ctrl=0x404c0000` (correct format)
- buf1 (SDPtr1) correctly set to 0 for single-buffer mode
- **No more AMD-Vi IO_PAGE_FAULT errors!**

### Current Issue (Phase 27d) ⚠️

**CRITICAL: BOTH Rings Have DIDX Stuck at 0**

Diagnostic run revealed this is a **global DMA path issue**, not Ring 16-specific:

| Ring | Purpose | CIDX | DIDX | Error |
|------|---------|------|------|-------|
| Ring 15 | MCU_WM | 7 | **0** | Commands not processed |
| Ring 16 | FWDL | 50 | **0** | Data not processed |

**Key findings from diagnostic**:
- `TX_TIMEOUT=1` at end - DMA tried to send, target didn't acknowledge
- `tx_done_int_sts_15=1` but DIDX=0 - interrupt may be stale
- `PDA_CONFG=0xc0000002` - FWDL_EN=1, LS_QSEL_EN=1 (correct)

**Root cause hypothesis**: MCU receiving side not accepting DMA data:
- MCU_DMA0 RX may not be enabled
- PDA_TAR_ADDR/PDA_TAR_LEN may not be configured
- ROM bootloader may need explicit download mode activation

### Next Step 🔧 - ENHANCED DIAGNOSTICS ADDED

**Additional PDA registers now checked in test_fw_load.c:**

✅ Implemented diagnostics:
1. `0xd41E8` - WPDMA2HOST_ERR_INT_STA (TX/RX timeout errors)
2. `0xd4110` - MCU_INT_STA (memory range errors, DMA errors)
3. `0x280C` - PDA_CONFG (PDA_FWDL_EN, LS_QSEL_EN)
4. `0x2800` - **PDA_TAR_ADDR** (target address - should be non-zero if MCU processed commands)
5. `0x2804` - **PDA_TAR_LEN** (target length)
6. `0x2808` - **PDA_DWLD_STATE** (BUSY/FINISH/OVERFLOW flags)
7. `0x2208` - **MCU_DMA0_GLO_CFG** (RX_DMA_EN status)
8. Ring 15 vs Ring 16 comparison (CNT, CIDX, DIDX, EXT_CTRL)

**To run the enhanced diagnostic test:**
```bash
make clean && make tests
sudo rmmod test_fw_load 2>/dev/null
sudo insmod tests/05_dma_impl/test_fw_load.ko
sudo dmesg | tail -100
```

**Investigation paths**:
1. If PDA_TAR_ADDR/LEN = 0 → MCU commands not being processed, need direct PDA init
2. If MCU_DMA0_GLO_CFG has RX_DMA_EN = 0 → MCU RX DMA needs explicit enable
3. If PDA_DWLD_STATE shows no activity → ROM not in download mode

See **docs/ZOUYONGHAO_ANALYSIS.md** section "2f" for complete analysis.

---

## Project Structure

```
mt7927/
├── README.md                          # Project overview
├── AGENTS.md                          # This file - session bootstrap
├── DEVELOPMENT_LOG.md                 # Detailed chronological development history
├── Makefile                           # Main build system
│
├── src/                               # Production driver source
│   ├── mt7927.h                       # Main header: device structures, register access
│   ├── mt7927_regs.h                  # Register definitions, queue IDs, address mapping
│   ├── mt7927_mcu.h                   # MCU protocol definitions
│   ├── mt7927_pci.c                   # PCI probe/remove, power management, reset
│   ├── mt7927_dma.c                   # DMA queue management, ring configuration
│   └── mt7927_mcu.c                   # MCU communication, firmware loading
│
├── tests/                             # Test modules
│   ├── Kbuild                         # Kernel build configuration
│   └── 05_dma_impl/                   # DMA implementation tests
│       ├── test_power_ctrl.c          # Power management handshake test
│       ├── test_wfsys_reset.c         # WiFi subsystem reset test
│       ├── test_dma_queues.c          # DMA ring configuration test
│       └── test_fw_load.c             # Complete firmware loading sequence
│
├── diag/                              # Diagnostic modules
│   ├── mt7927_diag.c                  # Safe read-only diagnostic
│   ├── mt7927_minimal_scan.c          # Minimal BAR0 scan
│   ├── mt7927_power_unlock.c          # Power handshake testing
│   ├── mt7927_ring_test.c             # Ring writability testing
│   └── ... (more diagnostic modules)
│
├── docs/                              # Documentation
│   ├── README.md                      # Documentation index
│   ├── dma_transfer_implementation.plan.md  # Implementation plan with task status
│   ├── headers_documentation.md       # Header file documentation
│   ├── mt7927_pci_documentation.md    # PCI driver documentation
│   ├── dma_mcu_documentation.md       # DMA and MCU documentation
│   ├── diagnostic_modules_documentation.md
│   ├── test_modules_documentation.md
│   └── TEST_RESULTS_SUMMARY.md
│
└── reference/                         # Reference driver source (not committed)
    └── linux/drivers/net/wireless/mediatek/mt76/
```

---

## Key Technical Context

### Hardware Architecture

| Property | Value |
|----------|-------|
| Chip | MediaTek MT7927 WiFi 7 (802.11be) |
| PCI ID | 14c3:7927 |
| BAR0 | 2MB memory region (main registers) |
| BAR2 | 32KB window (read-only shadow at BAR0+0x10000) |
| TX Rings | Sparse layout: 0-3, 8-11, 14-16 (same as MT6639) |
| RX Rings | 4-11 (data/event rings) |
| WFDMA1 | Does NOT exist on MT7927 |

### Critical Register Locations

| Register | Chip Address | BAR0 Offset | Value/Purpose |
|----------|--------------|-------------|---------------|
| **WFDMA Registers** | | | |
| WFDMA0_BASE | 0x7c024000 | **0xd4000** | WFDMA Host DMA0 registers |
| WFDMA0_RST | 0x7c024100 | 0xd4100 | DMA reset control (bits 4,5) |
| WFDMA0_GLO_CFG | 0x7c024208 | 0xd4208 | DMA global config (TX/RX enable) |
| TX_RING_BASE(n) | - | 0xd4300 + n*0x10 | TX ring configuration |
| TX_RING15_CTRL0 | - | 0xd43F0 | Ring 15 (MCU_WM) base address |
| TX_RING16_CTRL0 | - | 0xd4400 | Ring 16 (FWDL) base address |
| **Power/Reset Registers** | | | |
| LPCTL | 0x7c060010 | 0xe0010 | Power management handshake |
| WFSYS_SW_RST_B | 0x7c000140 | 0xf0140 | WiFi subsystem reset (AP2WF) |
| MCU_ROMCODE_INDEX | 0x81021604 | 0xc1604 | MCU state (expect **0x1D1E** for IDLE) |
| **CB_INFRA Registers (0x70xxxxxx - require L1 remap)** | | | |
| CB_INFRA_PCIE_REMAP_WF | 0x70026554 | via L1 | **Set 0x74037001** - CRITICAL, run FIRST! |
| CB_INFRA_PCIE_REMAP_WF_BT | 0x70026558 | via L1 | Set 0x70007000 |
| CB_INFRA_WF_SUBSYS_RST | 0x70028600 | via L1 | Bit 4 for WF reset (assert=0x10351, deassert=0x10340) |
| CB_INFRA_BT_SUBSYS_RST | 0x70028610 | via L1 | Bit 4 for BT reset |
| CB_INFRA_CRYPTO_MCU_OWN_SET | 0x70025034 | via L1 | Set BIT(0) to grant MCU ownership |
| CBTOP_GPIO_MODE5 | 0x7000535c | via L1 | Set 0x80000000 during init |
| CBTOP_GPIO_MODE6 | 0x7000536c | via L1 | Set 0x80 during init |

**Address Translation Notes**:
- Addresses 0x7cXXXXXX use fixed bus2chip mapping (see `reference_mtk_modules/.../mt6639.c`)
- Addresses 0x70XXXXXX require L1 remap via MT_HIF_REMAP_L1 register (BAR0+0x155024)
- Addresses 0x81XXXXXX use fixed mapping (0x81020000 → BAR0+0xc0000)

### Queue Assignments (MT6639/MT7927 - CONNAC3X Standard)

```c
// From MediaTek vendor code mt6639_wfmda_host_tx_group[]:
// MT7927 uses SPARSE ring layout like MT6639, NOT dense like MT7925
Ring 0-3:   AP DATA0-3      // Application data
Ring 8-11:  MD DATA0-3      // Modem data (mobile segment)
Ring 14:    MD CMD          // Modem commands
Ring 15:    AP CMD (MCU_WM) // **MCU commands** ← Use this
Ring 16:    FWDL            // **Firmware download** ← Use this
```

**IMPORTANT**: Previous documentation incorrectly stated rings 4/5 for MCU/FWDL. This was WRONG. Always use rings 15/16.

---

## Confirmed Values (From Reference Code Analysis)

| Register | Value | Source |
|----------|-------|--------|
| CB_INFRA_PCIE_REMAP_WF | 0x74037001 | reference_gen4m/chips/mt6639/mt6639.c:2727 |
| CB_INFRA_PCIE_REMAP_WF_BT | 0x70007000 | reference_gen4m/chips/mt6639/mt6639.c:2728 |
| CBTOP_GPIO_MODE5 | 0x80000000 | reference_gen4m/chips/mt6639/mt6639.c:2651 |
| CBTOP_GPIO_MODE6 | 0x80 | reference_gen4m/chips/mt6639/mt6639.c:2653 |
| WF_SUBSYS_RST assert | 0x10351 | reference_gen4m/chips/mt6639/mt6639.c:2660 |
| WF_SUBSYS_RST deassert | 0x10340 | reference_gen4m/chips/mt6639/mt6639.c:2665 |
| Ring 16 prefetch count | 0x4 | reference_gen4m/chips/mt6639/mt6639.c:1395 |
| Crypto MCU ownership | 0x70025034 (SET reg) | cb_infra_slp_ctrl.h |
| WFDMA HOST DMA0 base | BAR0+0xd4000 | bus2chip mapping in mt6639.c |
| Ring 15 (MCU_WM) | BAR0+0xd43f0 | wf_wfdma_host_dma0.h |
| Ring 16 (FWDL) | BAR0+0xd4400 | wf_wfdma_host_dma0.h |

**Phase 26 Additions (from zouyonghao comparison):**

| Register | Value | Source |
|----------|-------|--------|
| INT_RX_PRI | 0x0F00 | mt792x_dma.c:mt792x_dma_enable() |
| INT_TX_PRI | 0x7F00 | mt792x_dma.c:mt792x_dma_enable() |
| GLO_CFG_EXT1 | BIT(28) set | mt792x_dma.c (MT7927-specific) |
| WFDMA_DUMMY_CR | MT_WFDMA_NEED_REINIT | mt792x_dma.c |
| RST_DTX_PTR | ~0 (all rings) | mt792x_dma.c |
| Descriptor init | DMA_DONE bit set | mt76_dma.c:mt76_dma_alloc_queue() |

**Firmware Loading**: Uses polling mode (`fgCheckStatus=FALSE` in fw_dl.c) - NO mailbox response expected.

**GLO_CFG Timing**: Zouyonghao sets CLK_GAT_DIS **AFTER** ring configuration in `mt792x_dma_enable()`.

## Next Steps

1. ✅ **Phase 27c VERIFIED** - TXD control word fix works:
   - Control word: `ctrl=0x404c0000` (correct format)
   - No more `AMD-Vi: Event logged [IO_PAGE_FAULT]` errors
   - Ring 15 (MCU_WM) shows DMA activity

2. **Phase 27d - Run diagnostic test**:
   ```bash
   make clean && make tests
   sudo rmmod test_fw_load 2>/dev/null
   sudo insmod tests/05_dma_impl/test_fw_load.ko
   sudo dmesg | tail -80
   ```

3. **Analyze Phase 27d diagnostic output**:
   - Check `WPDMA2HOST_ERR_INT_STA (0xd41E8)` for TX/RX errors
   - Check `MCU_INT_STA (0xd4110)` for memory/DMA errors
   - Check `PDA_CONFG (0x280C)` for FWDL enable status
   - Compare Ring 15 vs Ring 16 configuration

**Already Implemented in test_fw_load.c:**
- ✅ CB_INFRA PCIe remap (0x74037001)
- ✅ Correct WFDMA base (0xd4000)
- ✅ DMA priority registers (INT_RX/TX_PRI)
- ✅ GLO_CFG_EXT1 BIT(28)
- ✅ WFDMA_DUMMY_CR NEED_REINIT
- ✅ RST_DTX_PTR = ~0
- ✅ Descriptor DMA_DONE init
- ✅ MCU IDLE polling
- ✅ TXD control word format (SDLen0 bits 16-29, LastSec0 bit 30) - Phase 27c
- ✅ Phase 27d diagnostic registers (WPDMA2HOST_ERR_INT_STA, MCU_INT_STA, PDA_CONFG)

---

## Working Commands

```bash
# Check MT7927 detection
lspci -nn | grep 14c3:7927

# Build drivers/tests
cd ~/development/github/mt7927
make clean && make tests

# Load production driver
sudo rmmod mt7927 2>/dev/null
sudo insmod src/mt7927.ko
sudo dmesg | tail -50

# Check binding status
lspci -k | grep -A 3 "14c3:7927"

# If chip enters error state (registers read 0xffffffff)
DEVICE=$(lspci -nn | grep 14c3:7927 | cut -d' ' -f1)
echo 1 | sudo tee /sys/bus/pci/devices/0000:$DEVICE/remove
sleep 2
echo 1 | sudo tee /sys/bus/pci/rescan

# View specific diagnostic
sudo insmod diag/mt7927_minimal_scan.ko
sudo dmesg | tail -30
sudo rmmod mt7927_minimal_scan
```

---

## How to Assist

### When User Asks About Status
1. Driver binds, power/reset work, **MCU reaches IDLE (0x1D1E)** ✅
2. **ROOT CAUSES ADDRESSED**:
   - ✅ Mailbox protocol → Polling-based approach implemented
   - ✅ WFDMA base → Correct address (0xd4000) used
   - ✅ CB_INFRA → PCIe remap initialized (0x74037001)
   - ✅ Phase 26 fixes → DMA priority, GLO_CFG_EXT1, descriptor init, etc.
   - ✅ Phase 27 → GLO_CFG timing fixed, ring registers accept writes
   - ✅ Phase 27 → All TX rings 0-16 initialized to valid DMA addresses
   - ✅ Phase 27b → RX_DMA_EN disabled during firmware loading
   - ✅ Phase 27c → TXD control word bit positions corrected
3. **CURRENT BLOCKER**: Need to test Phase 27c fix
4. **FIX APPLIED**: TXD control word - SDLen0 now in bits 16-29, LastSec0 in bit 30
5. **NEXT**: Test the fix to verify no page faults and DIDX increments

### When User Wants to Debug
1. Read `DEVELOPMENT_LOG.md` for full history and all attempts
2. Check `docs/` for detailed code documentation
3. Current driver is in `src/` directory
4. Test modules are in `tests/05_dma_impl/`
5. Safe diagnostics are in `diag/`

### When User Wants to Test New Theory
1. Create focused test module for specific hypothesis
2. Always check register states before/after changes
3. Use `dmesg | tail -50` to see kernel output
4. Keep tests small and incremental
5. Document findings in DEVELOPMENT_LOG.md

---

## Key Documentation to Read

| Document | Purpose |
|----------|---------|
| `DEVELOPMENT_LOG.md` | Complete chronological history, all attempts, all findings |
| `docs/dma_transfer_implementation.plan.md` | Implementation plan with task statuses |
| `docs/dma_mcu_documentation.md` | DMA and MCU subsystem technical details |
| `docs/mt7927_pci_documentation.md` | PCI initialization and power management |
| `docs/headers_documentation.md` | Register definitions and data structures |

---

## Reference Driver Locations

### Priority 1: MediaTek Vendor Code (Most Authoritative)

**reference_mtk_modules/** - Official MediaTek BSP from Xiaomi device kernel
```
connectivity/wlan/core/gen4m/
├── chips/mt6639/
│   ├── mt6639.c                    # Main chip driver (3600+ lines)
│   ├── hal_wfsys_reset_mt6639.c    # WiFi subsystem reset logic
│   └── hal_dmashdl_mt6639.c        # DMASHDL configuration
├── chips/common/
│   └── fw_dl.c                     # Firmware download (2995 lines) - POLLING MODE!
├── include/chips/coda/mt6639/
│   ├── cb_infra_misc0.h            # PCIe remap registers
│   ├── cb_infra_rgu.h              # WF/BT subsystem reset
│   ├── cb_infra_slp_ctrl.h         # Crypto MCU ownership
│   └── wf_wfdma_host_dma0.h        # WFDMA registers
└── os/linux/hif/pcie/pcie.c        # PCI device table (MT7927→MT6639 mapping!)
```

**reference_gen4m/** - Gen4M WLAN driver with detailed init sequences
```
chips/mt6639/
├── mt6639.c                        # Init sequences, CB_INFRA values
└── (same structure as above)
```

### Priority 2: Linux Kernel MT7925 (CONNAC3X Patterns)

```
drivers/net/wireless/mediatek/mt76/mt7925/
├── pci.c         # PCI probe and initialization sequence
├── mcu.c         # MCU communication (RUNTIME only, NOT firmware loading)
├── init.c        # Hardware initialization
└── dma.c         # DMA setup and transfer
```

### Priority 3: Zouyonghao MT7927 (Polling Protocol Reference)

**reference_zouyonghao_mt7927/** - Has correct polling-based FW loader but incomplete wiring

---

## Project Mission

**Goal**: Implement correct initialization sequence for MT7927 firmware loading.

**What's Working (Phase 27c)**:
1. ✅ WFDMA registers at BAR0 + **0xd4000** (correct)
2. ✅ CB_INFRA PCIe remap (0x74037001) set correctly
3. ✅ WF/BT subsystem reset via CB_INFRA_RGU
4. ✅ Crypto MCU ownership granted
5. ✅ **MCU reaches IDLE (0x1D1E)** - confirmed working!
6. ✅ GLO_CFG setup with CLK_GATE_DIS
7. ✅ WFDMA extensions configured
8. ✅ Ring configuration registers accept writes (Phase 27 GLO_CFG timing fix)
9. ✅ All TX rings 0-16 have valid BASE addresses (Phase 27 unused ring fix)
10. ✅ RX_DMA_EN disabled during firmware loading (Phase 27b fix)
11. ✅ TXD control word bit positions corrected (Phase 27c fix)

**Current Status (Phase 27c)**: TXD control word fix implemented, pending test
- Root cause: SDLen0 was in wrong bit position (0-13 instead of 16-29)
- Hardware read SDLen1=76 at SDPtr1=0x0 → DMA accessed address 0 → page fault
- Fix: Corrected bit positions to match MediaTek TXD_STRUCT

**Implementation sequence**:
1. ✅ Set CB_INFRA PCIe remap registers
2. ✅ WF/BT subsystem reset via CB_INFRA_RGU
3. ✅ Set crypto MCU ownership
4. ✅ Poll for MCU IDLE (0x1D1E)
5. ✅ Configure WFDMA rings (GLO_CFG timing fixed)
6. 🔧 Load firmware with polling (pending Phase 27c test)
7. ⏸️ Set SW_INIT_DONE manually (blocked on step 6)

The hardware works. All initialization steps complete. TXD control word fix applied. Need to test to verify DMA processing works.

---

## Quick Checklist for New Session

- [ ] Read this AGENTS.md for context
- [ ] Check `DEVELOPMENT_LOG.md` for detailed history
- [ ] Look at `docs/dma_transfer_implementation.plan.md` for task status
- [ ] Review last `dmesg` output if available
- [ ] Check if any new theories emerged from previous session
- [ ] Reference MT7925 source in kernel or `reference/` directory
