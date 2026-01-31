# Development Roadmap

## Current Status

**Status as of 2026-01-31**: **PHASE 26 - IMPLEMENTATION FIXES COMPLETE**

**Progress**:
- ✅ Root cause found (Phase 17): ROM doesn't support mailbox protocol
- ✅ Correct WFDMA base address (Phase 21): 0xd4000 (not 0x2000)
- ✅ MCU reaches IDLE state (Phase 24): 0x1D1E confirmed!
- ✅ Phase 26 fixes implemented: DMA priority, GLO_CFG_EXT1, descriptor init, etc.
- 🔧 Ring configuration still failing - likely GLO_CFG timing issue

**Current Blocker**: Ring registers (BASE, EXT_CTRL) not accepting writes. Zouyonghao sets CLK_GAT_DIS AFTER ring configuration, our code sets it BEFORE.

**Next Step**: Fix GLO_CFG timing (set CLK_GAT_DIS after ring config, not before).

See **[ZOUYONGHAO_ANALYSIS.md](ZOUYONGHAO_ANALYSIS.md)** section "2a. Critical GLO_CFG Timing Difference" for details.

## Phase 1: Get It Working 🎯 CURRENT PHASE

### Completed ✅

- [x] Bind driver to device
- [x] Load firmware files into kernel memory
- [x] Implement power management handshake (host claims DMA ownership)
- [x] Implement WiFi subsystem reset (WFSYS_SW_RST_B timing)
- [x] Implement DMA ring allocation and configuration
- [x] Implement MCU communication protocol structures
- [x] Implement firmware download sequence (mailbox-based - wrong protocol)
- [x] **Identify root cause** - Mailbox protocol not supported by ROM bootloader
- [x] **Fix WFDMA base address** - Changed from 0x2000 to 0xd4000
- [x] **Implement CB_INFRA initialization** - PCIe remap, crypto MCU ownership
- [x] **Confirm MCU IDLE state** - 0x1D1E reached successfully!
- [x] **Phase 26 fixes** - DMA priority, GLO_CFG_EXT1, descriptor init, RST_DTX_PTR

### In Progress 🔧

- [ ] **Fix ring configuration** ← Current task
  - Ring registers (BASE, EXT_CTRL) not accepting writes
  - GLO_CFG timing difference identified (see ZOUYONGHAO_ANALYSIS.md)
  - Need to reorder: set CLK_GAT_DIS AFTER ring config, not before

### Blocked (Pending Implementation) ⏸️

- [ ] Fix DMA descriptor processing (blocked on firmware loader)
- [ ] Achieve MCU command completion
- [ ] Complete firmware transfer and activation
- [ ] Create network interface (wlan0)

### Implementation Approaches

#### Current Approach: Fix GLO_CFG Timing

**File**: `tests/05_dma_impl/test_fw_load.c`

Reorder initialization sequence to match zouyonghao:

```c
// Phase 1: Clear/minimize GLO_CFG
mt_wr(dev, MT_WFDMA0_GLO_CFG, 0);  // Or minimal value

// Phase 2: Configure rings (GLO_CFG cleared!)
mt_wr(dev, MT_WFDMA0_TX_RING_BASE(15), dma_addr_15);
mt_wr(dev, MT_WFDMA0_TX_RING_CNT(15), ring_count);
mt_wr(dev, MT_WFDMA0_TX_RING_CIDX(15), 0);
mt_wr(dev, MT_WFDMA0_TX_RING_DIDX(15), 0);
// ... same for ring 16 ...

// Phase 3: Configure prefetch
mt_wr(dev, MT_WFDMA0_TX_RING15_EXT_CTRL, PREFETCH_RING15);
mt_wr(dev, MT_WFDMA0_TX_RING16_EXT_CTRL, PREFETCH_RING16);

// Phase 4: NOW set GLO_CFG with CLK_GAT_DIS (AFTER rings!)
mt_wr(dev, MT_WFDMA0_GLO_CFG, MT_WFDMA0_GLO_CFG_SETUP);

// Phase 5: Enable TX/RX DMA
mt_set(dev, MT_WFDMA0_GLO_CFG, TX_DMA_EN | RX_DMA_EN);
```

**Expected Result**: Ring registers should accept writes when GLO_CFG is in cleared state.

#### Already Implemented (Phase 26)

1. ✅ DMA priority registers (INT_RX_PRI=0x0F00, INT_TX_PRI=0x7F00)
2. ✅ GLO_CFG_EXT1 BIT(28) for MT7927
3. ✅ WFDMA_DUMMY_CR with NEED_REINIT flag
4. ✅ RST_DTX_PTR = ~0 (reset all rings)
5. ✅ Descriptor init with DMA_DONE bit
6. ✅ DIDX register writes
7. ✅ Polling-based firmware loading (no mailbox waits)

See [ZOUYONGHAO_ANALYSIS.md](ZOUYONGHAO_ANALYSIS.md) section "2a. Critical GLO_CFG Timing Difference" for details.

### Expected Output (Current State)

**Before mailbox fix (current state)**:
```
[   10.123] mt7927: Chip ID: 0x00511163
[   10.124] mt7927: BAR0 mapped, size=1048576
[   10.125] mt7927: Power management: Host claimed DMA
[   10.126] mt7927: WFSYS reset complete
[   10.127] mt7927: DMA rings allocated
[   10.128] mt7927: MCU ready (status=0x00000001)
[   10.129] mt7927: Sending patch semaphore command...
[   15.130] mt7927: ERROR: MCU command timeout  ← Expected blocker
```

**After mailbox fix (expected)**:
```
[   10.123] mt7927: Chip ID: 0x00511163
[   10.124] mt7927: BAR0 mapped, size=1048576
[   10.125] mt7927: Power management: Host claimed DMA
[   10.126] mt7927: WFSYS reset complete
[   10.127] mt7927: DMA rings allocated
[   10.128] mt7927: MCU ready (status=0x00000001)
[   10.129] mt7927: Loading patch (polling mode)
[   10.145] mt7927: Patch sent successfully
[   10.146] mt7927: Loading RAM firmware
[   10.230] mt7927: Firmware loaded successfully
[   10.231] mt7927: Network interface created: wlan0
```

## Phase 2: Make It Good

### Network Functionality
- [ ] Port full mt7925 functionality
  - [ ] TX/RX queue management
  - [ ] MAC80211 integration
  - [ ] Power saving modes
  - [ ] Multi-BSS support
- [ ] Add 320MHz channel support
  - [ ] Update channel definitions
  - [ ] PHY configuration for 320MHz
  - [ ] Regulatory domain handling
- [ ] Implement WiFi 7 features
  - [ ] MLO (Multi-Link Operation)
  - [ ] 4096-QAM modulation
  - [ ] Enhanced puncturing
  - [ ] Multi-RU support

### Performance Optimization
- [ ] Enable MSI/MSI-X interrupts (currently using legacy INTx)
- [ ] Optimize DMA buffer sizes
- [ ] Implement interrupt coalescing
- [ ] Add CPU affinity for interrupt handling
- [ ] Profile and optimize hot paths

### Power Management
- [ ] Implement runtime PM
- [ ] Add suspend/resume support
- [ ] Optimize power saving modes
- [ ] Implement wake-on-WLAN

### Robustness
- [ ] Add comprehensive error handling
- [ ] Implement firmware recovery
- [ ] Add hardware watchdog
- [ ] Improve diagnostics and debugging

## Phase 3: Make It Official

### Code Quality
- [ ] Clean up code for upstream standards
  - [ ] Follow Linux kernel coding style strictly
  - [ ] Remove debug code and comments
  - [ ] Add comprehensive documentation
  - [ ] Add KUnit tests where appropriate
- [ ] Split into logical patch series
- [ ] Write detailed commit messages
- [ ] Add maintainer documentation

### Upstream Submission
- [ ] Submit RFC (Request for Comments) to linux-wireless
  - [ ] Get initial feedback from maintainers
  - [ ] Address architectural concerns
  - [ ] Clarify relationship with MT6639/MT7925
- [ ] Address review feedback
  - [ ] Implement requested changes
  - [ ] Provide additional documentation
  - [ ] Add tests as needed
- [ ] Submit formal patch series
  - [ ] Follow linux-wireless submission guidelines
  - [ ] Provide cover letter with context
  - [ ] Tag relevant maintainers
- [ ] Iterate based on maintainer feedback
- [ ] Get Acked-by/Reviewed-by from maintainers
- [ ] Wait for merge into wireless-next tree
- [ ] Propagate to mainline kernel (2-3 kernel cycles)

**Timeline estimate**: 3-6 months from working driver to mainline merge.

## Implementation Status

### What's Working ✅

- PCI enumeration and BAR mapping (BAR0: 2MB confirmed, BAR2: 32KB)
- Driver successfully binds to device and handles IRQs
- Firmware files load into kernel memory (MT7925 firmware compatible via CONNAC3X)
- Power management handshake (host claims DMA ownership via PMCTRL)
- WiFi/BT subsystem reset sequence via CB_INFRA_RGU
- DMA descriptor rings allocated (sparse layout: 0,1,2,15,16)
- **Queue assignments validated** - Ring 15 (MCU_WM), Ring 16 (FWDL) per MT6639 config
- PCIe ASPM L0s and L1 disabled
- **CB_INFRA initialization** - PCIe remap (0x74037001) configured
- **CONN_INFRA version** - 0x03010002 confirmed
- **MCU reaches IDLE (0x1D1E)** - First time confirmed! ✅
- **GLO_CFG setup** - Clock gating disabled, WFDMA extensions configured
- **Ring protocol confirmed** - MT6639 analysis validates CONNAC3X ring assignments
- **Architectural foundation confirmed** - MT7927 is MT6639 variant with CONNAC3X protocol

### What's Not Working ❌

- **Ring register writes** - BASE, EXT_CTRL not accepting writes (likely GLO_CFG timing)
- **DMA descriptor processing** - DIDX stuck at 0 (blocked on ring config)
- Firmware transfer (blocked on ring config)
- Network interface creation (requires successful initialization)

### Root Cause Analysis

**Phase 17-21 (RESOLVED)**: Mailbox protocol and WFDMA base address
- ✅ ROM doesn't support mailbox → Polling-based approach implemented
- ✅ Wrong WFDMA base (0x2000 vs 0xd4000) → Fixed to use 0xd4000

**Phase 24-26 (CURRENT)**: Ring configuration registers
- ❌ Ring registers (BASE, EXT_CTRL) not accepting writes
- Likely cause: **GLO_CFG timing difference**
  - Zouyonghao: Sets CLK_GAT_DIS **AFTER** ring configuration
  - Our code: Sets CLK_GAT_DIS **BEFORE** ring configuration
  - Hardware may require ring config while GLO_CFG is in "disabled" state

**Current Blocker**: GLO_CFG timing (Phase 26 finding)

## Testing and Validation

### Test Infrastructure (Completed)

- [x] Test modules framework (`tests/05_dma_impl/`)
  - [x] test_power_ctrl.ko - Power management
  - [x] test_wfsys_reset.ko - WiFi subsystem reset
  - [x] test_dma_queues.ko - DMA ring allocation
  - [x] test_fw_load.ko - Complete firmware loading
- [x] Diagnostic modules (`diag/`)
  - [x] 18 hardware exploration modules
  - [x] Register scanners
  - [x] Ring validators
  - [x] Power state analyzers

### Validation Completed

- [x] Chip ID verification (0x00511163)
- [x] BAR mapping correctness (BAR0: 2MB, BAR2: 32KB)
- [x] Power management handshake
- [x] WFSYS reset timing and completion
- [x] DMA ring allocation (8 TX rings: 0-7)
- [x] Ring 15/16 physical existence confirmed
- [x] MT6639 architectural relationship proven
- [x] CONNAC3X ring protocol validated
- [x] Firmware file compatibility (MT7925 firmware)

### Validation Pending (After Firmware Loader)

- [ ] Firmware patch transfer success
- [ ] Firmware RAM transfer success
- [ ] MCU firmware activation
- [ ] Network interface creation
- [ ] Basic connectivity (association, authentication)
- [ ] Data path (TX/RX packets)
- [ ] Performance benchmarks
- [ ] Stability testing (long-term operation)

## Documentation Status

### Completed Documentation ✅

- [x] README.md - Project overview and quick start
- [x] DEVELOPMENT_LOG.md - Complete chronological history (17 phases)
- [x] AGENTS.md - Session bootstrap for developers
- [x] CLAUDE.md - AI agent instructions
- [x] docs/README.md - Documentation navigation
- [x] docs/mt7927_pci_documentation.md - PCI layer API
- [x] docs/dma_mcu_documentation.md - DMA and MCU layer
- [x] docs/headers_documentation.md - Header file reference
- [x] docs/test_modules_documentation.md - Test framework
- [x] docs/diagnostic_modules_documentation.md - Diagnostic tools
- [x] docs/TEST_RESULTS_SUMMARY.md - Validation results
- [x] docs/MT6639_ANALYSIS.md - Architecture proof
- [x] docs/ZOUYONGHAO_ANALYSIS.md - Root cause analysis
- [x] docs/FIRMWARE_ANALYSIS.md - Firmware compatibility
- [x] docs/WINDOWS_FIRMWARE_ANALYSIS.md - Windows driver analysis
- [x] docs/HARDWARE.md - Hardware specifications
- [x] docs/TROUBLESHOOTING.md - Common issues and solutions
- [x] docs/FAQ.md - Frequently asked questions
- [x] docs/ROADMAP.md - This file

### Documentation Pending

- [ ] API reference (when stable)
- [ ] User guide (when functional)
- [ ] Performance tuning guide (when optimized)
- [ ] Kernel submission guide (when ready for upstream)

## Dependencies and Requirements

### Build Requirements
- Linux kernel 6.7+ (for mt7925 infrastructure)
- Kernel headers matching running kernel
- GCC or Clang compiler
- Make build system

### Runtime Requirements
- MediaTek MT7927 WiFi 7 hardware (PCI ID: 14c3:7927)
- MT7925 firmware files in `/lib/firmware/mediatek/mt7925/`
- PCIe slot (Gen3 x1 or better)
- 2MB+ system memory for firmware and DMA buffers

### Development Requirements
- Understanding of Linux kernel module development
- Familiarity with WiFi driver architecture
- Knowledge of PCI/DMA programming
- Access to MT7927 hardware for testing

## Known Issues and Limitations

### Current Limitations
1. **No network interface** - Blocked on firmware loader implementation
2. **Legacy INTx interrupts** - MSI/MSI-X not yet implemented
3. **No power management** - Suspend/resume not implemented
4. **Limited error recovery** - Firmware recovery not implemented
5. **Debug code present** - Extensive logging needs cleanup for upstream

### Hardware Limitations
1. **BAR0 size** - 2MB total (adequate but smaller than some competitors)
2. **Single WFDMA** - Only WFDMA0 present (vs WFDMA0+1 on MT7925)
3. **Sparse rings** - Only 8 TX rings vs 17 on MT7925
4. **ROM bootloader** - Limited protocol support (no mailbox commands)

### Architectural Constraints
1. **MT6639 variant** - Must follow MT6639 configuration, not MT7925
2. **CONNAC3X family** - Limited to CONNAC3X capabilities
3. **Firmware shared** - Uses MT7925 firmware (cannot optimize for MT7927-specific features)

## See Also

- **[ZOUYONGHAO_ANALYSIS.md](ZOUYONGHAO_ANALYSIS.md)** - Root cause and solution (CRITICAL!)
- **[MT6639_ANALYSIS.md](MT6639_ANALYSIS.md)** - Architectural foundation
- **[../DEVELOPMENT_LOG.md](../DEVELOPMENT_LOG.md)** - Complete history
- **[HARDWARE.md](HARDWARE.md)** - Hardware specifications
- **[TROUBLESHOOTING.md](TROUBLESHOOTING.md)** - Common issues
- **[FAQ.md](FAQ.md)** - Frequently asked questions
