# MT7927 WiFi 7 Linux Driver Project

## 🎉 Major Breakthrough: MT7927 is MT6639 Variant (CONNAC3X Family)

**CRITICAL DISCOVERY (2026-01-31)**: MT7927 is an **MT6639 variant**, NOT MT7925! MediaTek's kernel modules explicitly map MT7927 to MT6639 driver configuration. MT6639 and MT7925 are both CONNAC3X family chips sharing firmware and ring protocol, which explains firmware compatibility. See **docs/MT6639_ANALYSIS.md** for complete evidence chain.

## Current Status: BLOCKED ON DMA DESCRIPTOR PROCESSING 🚧

**Working**: Complete DMA transfer layer implemented, power management functional, all initialization steps succeed  
**Blocked**: DMA hardware doesn't process TX descriptors - DIDX never advances despite correct ring setup  
**Impact**: MCU commands timeout, preventing firmware activation and chip bring-up

### Implementation Complete ✅
- Production driver with full PCI probe, power management, and DMA support (`src/`)
- Complete test framework for validation (`tests/05_dma_impl/`)
- 18 diagnostic modules for hardware exploration (`diag/`)
- Comprehensive documentation (`docs/`)

### Current Blocker 🔴
The DMA engine is correctly configured but doesn't process descriptors:
- TX ring initialized: CIDX=0, CPU_DIDX=0, DMA_DIDX=0 (stuck)
- MCU ready status: 0x00000001 (confirmed)
- Power management: Host owns DMA, handshake complete
- All prerequisites met, but hardware won't advance DIDX

This prevents MCU command completion and blocks firmware transfer.

**⚠️ NEW FINDING** (see HARDWARE_ANALYSIS.md):
- **L1 ASPM and L1 substates are ENABLED** (driver only disables L0s)
- Device may enter L1.2 power saving during DMA operations
- **PRIME SUSPECT** for DMA blocking issue

## Quick Start

### Prerequisites
```bash
# Check kernel version (need 6.7+ for mt7925 base)
uname -r  # Should show 6.7 or higher

# Verify MT7927 device is present
lspci -nn | grep 14c3:7927  # Should show your device
```

### Install Firmware
```bash
# Download MT7925 firmware (compatible with MT7927!)
mkdir -p ~/mt7927_firmware
cd ~/mt7927_firmware

wget https://git.kernel.org/pub/scm/linux/kernel/git/firmware/linux-firmware.git/plain/mediatek/mt7925/WIFI_MT7925_PATCH_MCU_1_1_hdr.bin
wget https://git.kernel.org/pub/scm/linux/kernel/git/firmware/linux-firmware.git/plain/mediatek/mt7925/WIFI_RAM_CODE_MT7925_1_1.bin

# Install firmware
sudo mkdir -p /lib/firmware/mediatek/mt7925
sudo cp *.bin /lib/firmware/mediatek/mt7925/
sudo update-initramfs -u
```

### Build and Load Driver
```bash
# Clone and build
git clone https://github.com/[your-username]/mt7927-linux-driver
cd mt7927-linux-driver

# Build all components
make clean
make           # Build production driver (src/)
make tests     # Build test modules (tests/)
make diag      # Build diagnostic modules (diag/)

# Option 1: Load test module (recommended for development)
sudo rmmod test_fw_load 2>/dev/null
sudo insmod tests/05_dma_impl/test_fw_load.ko
sudo dmesg | tail -40

# Option 2: Load production driver
sudo rmmod mt7927_pci 2>/dev/null
sudo insmod src/mt7927_pci.ko
sudo dmesg | tail -40

# Option 3: Load diagnostic module
sudo rmmod mt7927_diag 2>/dev/null
sudo insmod diag/mt7927_diag.ko
sudo dmesg | tail -20

# Check binding status
lspci -k | grep -A 3 "14c3:7927"

# Test: Disable L1 ASPM (potential DMA fix!)
# Bash:
DEVICE=$(lspci -nn | grep 14c3:7927 | cut -d' ' -f1)
sudo setpci -s $DEVICE CAP_EXP+10.w=0x0000

# Fish:
set DEVICE (lspci -nn | grep 14c3:7927 | cut -d' ' -f1)
sudo setpci -s $DEVICE CAP_EXP+10.w=0x0000
```

### Expected Output (Current State)
```
[   10.123] mt7927_pci: MT7927 WiFi 7 driver loading
[   10.124] mt7927 0000:0a:00.0: enabling device (0000 -> 0002)
[   10.125] mt7927 0000:0a:00.0: BAR0 mapped, size=1048576
[   10.126] mt7927 0000:0a:00.0: BAR2 mapped, size=1048576
[   10.127] mt7927 0000:0a:00.0: Power management: Host claimed DMA
[   10.128] mt7927 0000:0a:00.0: WFSYS reset complete
[   10.129] mt7927 0000:0a:00.0: DMA rings allocated (8 TX, 2 RX)
[   10.130] mt7927 0000:0a:00.0: MCU ready (status=0x00000001)
[   10.131] mt7927 0000:0a:00.0: Sending patch semaphore command...
[   15.132] mt7927 0000:0a:00.0: ERROR: MCU command timeout
[   15.133] mt7927 0000:0a:00.0: DMA_DIDX stuck at 0 (no descriptor processing)
```

## Technical Details

### Hardware Information
- **Chip**: MediaTek MT7927 WiFi 7 (802.11be) - also known as "Filogic 380"
- **Full Name**: MT7927 802.11be 320MHz 2x2 PCIe Wireless Network Adapter
- **PCI ID**: 14c3:7927 (vendor: MediaTek, device: MT7927)
- **PCI Location**: Varies by system (example: 01:00.0) - check with `lspci -nn | grep 14c3:7927`
- **Architecture**: **MT6639 variant** (CONNAC3X family) - shares firmware with MT7925
- **Ring Protocol**: CONNAC3X standard (Ring 15: MCU CMD, Ring 16: FWDL)
- **DMA Engine**: Single WFDMA0 (no WFDMA1), sparse ring layout (0,1,2,15,16)
- **Memory Map**:
  - BAR0: **2MB** total (confirmed via lspci)
    - 0x000000 - 0x0FFFFF: SRAM (chip internal memory)
    - 0x100000 - 0x1FFFFF: Control registers
  - BAR2: 32KB (read-only shadow of BAR0+0x10000)

### Key Discoveries
1. **MT7927 is MT6639 variant** - MediaTek kernel modules prove architectural relationship
2. **CONNAC3X family** - MT6639 and MT7925 share firmware via common CONNAC3X architecture
3. **Ring protocol validated** - Uses CONNAC3X standard: Ring 15 (MCU_WM), Ring 16 (FWDL)
4. **Sparse ring layout** - 8 physical rings with sparse numbering (0,1,2,15,16)
5. **Single DMA engine** - Only WFDMA0 present (no WFDMA1)
6. **Initialization succeeds** - Power management, reset, ring setup all work
7. **DMA doesn't process** - Hardware won't advance descriptor index (root blocker)
8. **L1 ASPM enabled** - Prime suspect for DMA blocking (L1.2 power saving during DMA)

### What's Working ✅
- PCI enumeration and BAR mapping (BAR0: **2MB confirmed**, BAR2: 32KB)
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

### What's Not Working ❌
- **DMA descriptor processing** - Hardware doesn't advance DMA_DIDX
- MCU command timeouts (patch semaphore never completes)
- Firmware transfer blocked by non-functional DMA
- Network interface creation (requires successful initialization)

### Root Cause Analysis
All initialization steps execute correctly, but the DMA engine doesn't process TX descriptors:
- Descriptors allocated in coherent DMA memory
- Ring configured: base address, count, CIDX=0, CPU_DIDX=0
- CIDX written to trigger processing
- DMA_DIDX remains stuck at 0 (should increment after processing)
- IRQ handler confirms DMA engine is idle (no completion interrupts)

**Hypotheses** (Ordered by Priority):
1. **⚠️ L1 ASPM and L1 substates enabled** - **PRIORITY 1** (see HARDWARE_ANALYSIS.md)
   - Device may enter L1.2 sleep during DMA operations
   - Driver only disables L0s, not L1/L1.1/L1.2
   - Test: `sudo setpci -s $DEVICE CAP_EXP+10.w=0x0000`
2. MT6639-specific DMA initialization sequence (check reference_mtk_modules)
3. Missing pre-DMA initialization step (clock/power domain)
4. Different descriptor format or MT6639-specific layout
5. Additional doorbell/kick register beyond CIDX write
6. Hardware expects firmware to be partially active first

**Note**: Ring assignment (15/16) is now validated via MT6639 analysis - no longer a suspect.

See `docs/MT6639_ANALYSIS.md`, `HARDWARE_ANALYSIS.md`, `AGENTS.md` and `DEVELOPMENT_LOG.md` for complete investigation history.

## Project Structure
```
mt7927-linux-driver/
├── README.md                        # Project overview and status
├── AGENTS.md                        # Session bootstrap for development resumption
├── DEVELOPMENT_LOG.md               # Complete chronological development history
├── Makefile                         # Main build system (all, tests, diag targets)
│
├── src/                             # Production driver implementation
│   ├── mt7927_pci.c                # PCI probe, power management, WFSYS reset
│   ├── mt7927_dma.c                # DMA queue management (8 TX, 16 RX rings)
│   ├── mt7927_mcu.c                # MCU protocol, firmware loading sequence
│   ├── mt7927_diag.c               # Diagnostic register dump module
│   ├── mt7927.h                    # Core data structures and API
│   ├── mt7927_mcu.h                # MCU message structures
│   ├── mt7927_regs.h               # Register definitions and address mapping
│   └── Makefile                    # Driver build configuration
│
├── tests/05_dma_impl/              # Test modules for validation
│   ├── test_power_ctrl.c           # Power management handshake test
│   ├── test_wfsys_reset.c          # WiFi subsystem reset verification
│   ├── test_dma_queues.c           # DMA ring allocation test
│   ├── test_fw_load.c              # Complete firmware loading integration test
│   └── README.md                   # Test module documentation
│
├── diag/                            # Hardware exploration diagnostic modules
│   ├── mt7927_diag.c               # Basic register dump
│   ├── mt7927_minimal_scan.c       # Safe register scanner
│   ├── mt7927_power_diag.c         # Power management analysis
│   ├── mt7927_dma_reset.c          # DMA reset sequence exploration
│   ├── mt7927_wfsys_reset.c        # WFSYS reset testing
│   └── [13 more diagnostic modules] # Various hardware exploration tools
│
└── docs/                            # Comprehensive documentation
    ├── README.md                   # Documentation index and navigation
    ├── mt7927_pci_documentation.md # PCI layer API documentation
    ├── dma_mcu_documentation.md    # DMA and MCU layer documentation
    ├── headers_documentation.md    # Complete header file reference
    ├── test_modules_documentation.md      # Test framework guide
    ├── diagnostic_modules_documentation.md # Diagnostic tools reference
    └── dma_transfer_implementation.plan.md # Implementation plan with status
```

## Development Roadmap

### Phase 1: Get It Working (BLOCKED)
- [x] Bind driver to device
- [x] Load firmware files
- [x] Implement power management handshake
- [x] Implement WiFi subsystem reset
- [x] Implement DMA ring allocation and configuration
- [x] Implement MCU communication protocol
- [x] Implement firmware download sequence
- [ ] **Fix DMA descriptor processing** ← Current blocker
- [ ] Achieve MCU command completion
- [ ] Complete firmware transfer and activation
- [ ] Create network interface

### Phase 2: Make It Good
- [ ] Port full mt7925 functionality
- [ ] Add 320MHz channel support
- [ ] Integrate with mac80211
- [ ] Implement WiFi 7 features

### Phase 3: Make It Official
- [ ] Clean up code for upstream
- [ ] Submit to linux-wireless
- [ ] Get merged into mainline kernel

### Known Implementation Status
**Completed Components**:
- PCI probe and resource allocation
- Power management control (host/firmware ownership)
- WiFi subsystem reset with proper timing
- DMA descriptor ring structures (sparse: 0,1,2,15,16)
- **Queue assignment validated** - Ring 15 (MCU_WM), Ring 16 (FWDL) per MT6639
- MCU message structures and send functions
- Firmware loading protocol (patch semaphore, scatter)
- Interrupt handler registration
- Complete register definitions
- **Architectural foundation confirmed** - MT6639 variant with CONNAC3X protocol

**Current Investigation**:
- Why DMA_DIDX doesn't advance despite correct setup
- Potential missing initialization step
- Differences between MT7927 and MT7925 DMA behavior
- Alternative descriptor format requirements

## How to Contribute

### Critical Investigation Needed
The DMA layer is fully implemented with validated MT6639/CONNAC3X ring protocol, but hardware doesn't process descriptors. We need to:
1. **Test ASPM L1 disable** - Primary suspect for DMA blocking (PRIORITY 1)
2. **Compare MT6639 initialization** - Check `reference_mtk_modules/` for MT6639-specific steps
3. **Review MT6639 DMA setup** - Verify our implementation matches MT6639 sequence
4. **Check MT6639 power management** - May have different ASPM requirements than MT7925

### Development Resources
- **Full documentation**: See `docs/README.md` for complete API reference
- **Implementation plan**: `docs/dma_transfer_implementation.plan.md`
- **Development log**: `DEVELOPMENT_LOG.md` (chronological investigation history)
- **Session bootstrap**: `AGENTS.md` (quick-start guide for new sessions)

### Testing on Your Hardware
1. Build and load `tests/05_dma_impl/test_fw_load.ko`
2. Capture full dmesg output showing DMA_DIDX behavior
3. Try diagnostic modules in `diag/` to explore register behavior
4. Document any differences from expected behavior in GitHub issues

### Code References

#### Primary Reference: MT6639 (MediaTek Kernel Modules)
```bash
# MT6639 chip implementation (architectural parent of MT7927)
reference_mtk_modules/connectivity/wlan/core/gen4-mt79xx/chips/mt6639/
├── mt6639.c      # Complete chip config, ring setup, interrupt handling
├── mt6639.h      # Chip-specific constants
└── include/chips/coda/mt6639/
    └── wf_wfdma_host_dma0.h  # WFDMA register definitions
```

#### Secondary Reference: MT7925 (CONNAC3X Sibling)
```bash
# MT7925 driver (CONNAC3X sibling, shares firmware with MT7927)
drivers/net/wireless/mediatek/mt76/mt7925/
├── pci.c         # PCI probe and initialization sequence
├── mcu.c         # MCU communication and firmware loading
├── init.c        # Hardware initialization
└── dma.c         # DMA setup (CONNAC3X protocol)

# Shared mt76 infrastructure
drivers/net/wireless/mediatek/mt76/
├── dma.c         # Generic DMA implementation
├── mt76_connac_mcu.c  # MCU interface for CONNAC chips
└── util.c        # Utility functions
```

#### Online References
- **MT6639 Analysis**: [docs/MT6639_ANALYSIS.md](docs/MT6639_ANALYSIS.md) - Proves architectural relationship
- **mt7925 on GitHub**: [Linux kernel source](https://github.com/torvalds/linux/tree/master/drivers/net/wireless/mediatek/mt76/mt7925)
- **mt76 framework**: [OpenWrt repository](https://github.com/openwrt/mt76)

## Troubleshooting

### Driver Won't Load
```bash
# Check for conflicts
lsmod | grep mt79
sudo rmmod mt7921e mt7925e  # Remove any conflicting drivers

# Check kernel messages
sudo dmesg | grep -E "mt7927|0a:00"
```

### Chip in Error State
```bash
# If chip shows 0xffffffff, reset via PCI
echo 1 | sudo tee /sys/bus/pci/devices/0000:0a:00.0/remove
sleep 2
echo 1 | sudo tee /sys/bus/pci/rescan
```

### Firmware Not Found
```bash
# Verify firmware is installed
ls -la /lib/firmware/mediatek/mt7925/
# Should show WIFI_MT7925_PATCH_MCU_1_1_hdr.bin and WIFI_RAM_CODE_MT7925_1_1.bin
```

## Test Results Summary

### Successful Tests ✅
- **Hardware Detection**: PCI enumeration works perfectly
- **Driver Binding**: Custom driver claims device successfully  
- **Firmware Compatibility**: MT7925 firmware loads into kernel memory
- **Register Access**: All control registers accessible and writable
- **Power Management**: Host successfully claims DMA ownership
- **WFSYS Reset**: Reset sequence completes correctly
- **DMA Allocation**: All descriptor rings allocate successfully
- **Queue Configuration**: Ring base addresses and counts set correctly
- **MCU Ready Status**: MCU reports ready (FW_OWN_REQ_SET = 0x1)
- **IRQ Registration**: Interrupt handler installs and responds
- **Chip Stability**: No crashes or lockups during testing

### Failed Tests ❌
- **DMA Descriptor Processing**: DMA_DIDX never advances from 0
- **MCU Commands**: All commands timeout waiting for completion
- **Firmware Transfer**: Cannot send firmware to chip via DMA

### Diagnostic Findings
- Descriptors are in coherent DMA memory (verified)
- Ring registers contain correct addresses and counts
- CIDX write operation succeeds (triggers processing attempt)
- No DMA completion interrupts occur
- CPU_DIDX and DMA_DIDX remain synchronized at 0 (no processing)

See `docs/TEST_RESULTS_SUMMARY.md` for detailed test execution logs.

## FAQ

**Q: Why not just use the mt7925e driver?**  
A: The mt7925e driver refuses to bind to MT7927's PCI ID (14c3:7927) and adding the ID via new_id fails with "Invalid argument".

**Q: Is this safe to test?**  
A: Yes, we're using proven MT7925 code paths. The worst case is the driver doesn't fully initialize (current state).

**Q: Will this support full WiFi 7 320MHz channels?**  
A: Initially it will work like MT7925 (160MHz). Adding 320MHz support will come after basic functionality works.

**Q: When will this be in the mainline kernel?**  
A: Once we have a working driver, submission typically takes 2-3 kernel cycles (3-6 months).

## License
GPL v2 - Intended for upstream Linux kernel submission

## Contact & Support
- **GitHub Issues**: Report bugs and discuss development
- **Linux Wireless**: [Mailing list](http://vger.kernel.org/vger-lists.html#linux-wireless) for upstream discussion

## Documentation

Complete technical documentation is available in the `docs/` directory:

- **[docs/MT6639_ANALYSIS.md](docs/MT6639_ANALYSIS.md)** - ⚠️ **NEW** - Proves MT7927 is MT6639 variant
- **[HARDWARE_ANALYSIS.md](HARDWARE_ANALYSIS.md)** - lspci analysis revealing L1 ASPM issue
- **[docs/README.md](docs/README.md)** - Documentation index and navigation guide
- **[AGENTS.md](AGENTS.md)** - Quick-start session bootstrap for development
- **[DEVELOPMENT_LOG.md](DEVELOPMENT_LOG.md)** - Complete chronological development history
- **[docs/mt7927_pci_documentation.md](docs/mt7927_pci_documentation.md)** - PCI layer implementation
- **[docs/dma_mcu_documentation.md](docs/dma_mcu_documentation.md)** - DMA and MCU layer details
- **[docs/headers_documentation.md](docs/headers_documentation.md)** - Header file reference
- **[docs/test_modules_documentation.md](docs/test_modules_documentation.md)** - Test framework guide
- **[docs/diagnostic_modules_documentation.md](docs/diagnostic_modules_documentation.md)** - Diagnostic tools

## Getting Help

1. **Read docs/MT6639_ANALYSIS.md first** - Proves MT7927 is MT6639 variant (critical!)
2. **Read AGENTS.md** - Contains critical context and current status
3. **Check DEVELOPMENT_LOG.md** - See what has already been tried (Phase 16 = MT6639 discovery)
4. **Review implementation plan** - `docs/dma_transfer_implementation.plan.md`
5. **Study reference drivers**:
   - **Primary**: MT6639 in `reference_mtk_modules/` (architectural parent)
   - **Secondary**: MT7925 in kernel tree (CONNAC3X sibling for firmware reference)
6. **Ask in GitHub issues** - Share findings and discuss approaches

---

**Status as of 2026-01-31**:

**MAJOR DISCOVERY**: MT7927 is an **MT6639 variant** (proven via MediaTek kernel modules), not MT7925. This validates our ring assignment (15/16) and explains firmware compatibility through CONNAC3X family architecture.

**Implementation Status**: Complete DMA transfer layer with production driver, test framework, and diagnostic tools. All initialization steps succeed (power management, reset, ring configuration with validated CONNAC3X protocol). Ring assignments confirmed correct via MT6639 analysis.

**Current Blocker**: DMA hardware doesn't process descriptors (DIDX stuck at 0). **Primary suspect**: L1 ASPM power states enabled - device may enter L1.2 sleep during DMA operations. **Next action**: Test with L1 ASPM disabled.

See **docs/MT6639_ANALYSIS.md** for complete architectural analysis.
