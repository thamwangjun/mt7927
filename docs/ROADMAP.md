# Development Roadmap

## Current Status

**Status as of 2026-01-31**: **ROOT CAUSE FOUND**

**Discovery**: MT7927 ROM bootloader does NOT support mailbox command protocol! Our driver waits for mailbox responses that the ROM will never send. DMA hardware works correctly - we're using the wrong communication protocol.

**Solution**: Implement polling-based firmware loading (validated by zouyonghao working driver).

See **[ZOUYONGHAO_ANALYSIS.md](ZOUYONGHAO_ANALYSIS.md)** for complete root cause analysis.

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

### In Progress 🔧

- [ ] **Implement polling-based firmware loader** ← Current task
  - Based on zouyonghao reference implementation
  - Skip mailbox waits, use time-based delays
  - Aggressive TX cleanup before and after operations
  - Status register polling instead of interrupt waiting

### Blocked (Pending Implementation) ⏸️

- [ ] Fix DMA descriptor processing (blocked on firmware loader)
- [ ] Achieve MCU command completion
- [ ] Complete firmware transfer and activation
- [ ] Create network interface (wlan0)

### Implementation Approaches

#### Quick Test (30 minutes)
Validate the mailbox hypothesis with minimal code changes:

**File**: `src/mt7927_mcu.c`
```c
// OLD (wrong - waits for mailbox response):
ret = mt76_mcu_send_and_get_msg(&dev->mt76, MCU_CMD_PATCH_SEM_CONTROL,
                                &req, sizeof(req), true, &skb);

// NEW (correct - don't wait):
ret = mt76_mcu_send_msg(&dev->mt76, MCU_CMD_PATCH_SEM_CONTROL,
                       &req, sizeof(req), false);  // Don't wait!
// Skip response parsing - ROM doesn't send it
```

**Expected Result**: DMA_DIDX should advance because we're not blocking on mailbox response.

#### Full Solution (2-3 hours)
Complete polling-based firmware loader:

**Create**: `src/mt7927_fw_load.c` (based on `reference_zouyonghao_mt7927/mt7927_fw_load.c`)

**Key Components**:
1. **Skip semaphore command** - ROM doesn't support it
2. **Never wait for mailbox responses** - Set `wait_resp = false` in all mcu_send_msg calls
3. **Aggressive TX cleanup** - Force cleanup before AND after each chunk
4. **Polling delays** - 5-10ms between operations for ROM processing
5. **Skip FW_START** - Manually set SW_INIT_DONE bit instead
6. **MCU IDLE check** - Poll 0x81021604 for 0x1D1E before firmware loading
7. **Status register polling** - Check 0x7c060204 and 0x7c0600f0 for completion

See [ZOUYONGHAO_ANALYSIS.md](ZOUYONGHAO_ANALYSIS.md) lines 274-293 for complete requirements.

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
- WiFi subsystem reset sequence (WFSYS_SW_RST_B timing)
- DMA descriptor rings allocated and configured (sparse layout: 0,1,2,15,16)
- **Queue assignments validated** - Ring 15 (MCU_WM), Ring 16 (FWDL) per MT6639 config
- MCU responds as ready (FW_OWN_REQ_SET returns 0x00000001)
- PCIe ASPM L0s disabled (MT_PCIE_MAC_PM_L0S_DIS set)
- SWDEF_MODE set to normal operation (0x1)
- All registers writable, no protection issues
- **Ring protocol confirmed** - MT6639 analysis validates CONNAC3X ring assignments
- **Architectural foundation confirmed** - MT7927 is MT6639 variant with CONNAC3X protocol

### What's Not Working ❌

- **DMA descriptor processing** - Hardware doesn't advance DMA_DIDX (blocked on firmware loader)
- MCU command timeouts (patch semaphore never completes - root cause: mailbox protocol)
- Firmware transfer blocked by non-functional DMA (blocked on firmware loader)
- Network interface creation (requires successful initialization)

### Root Cause Analysis

**CONFIRMED (Phase 17)**: MT7927 ROM bootloader does NOT support mailbox command protocol!

All initialization steps execute correctly, but the communication protocol is wrong:
1. **PATCH_SEM_CONTROL sent** → ROM ignores it (doesn't understand mailbox protocol)
2. **Driver waits for response** → ROM never responds
3. **DMA_DIDX never advances** → Because we're waiting in wrong place!
4. **Timeout after 5 seconds** → MCU command never completes

**The real blocker**: Mailbox protocol assumption, NOT hardware issue!

### Invalidated Hypotheses

1. ~~**L1 ASPM blocking DMA**~~ - Working driver has L1 enabled (same as ours)
2. ~~**Ring assignment wrong**~~ - Rings 15/16 validated via MT6639
3. ~~**DMA configuration incorrect**~~ - Working driver uses same config
4. ~~**Missing initialization step**~~ - All init steps are correct

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
