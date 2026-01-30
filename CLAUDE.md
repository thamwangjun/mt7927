# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Context

This is a Linux kernel driver for the MediaTek MT7927 WiFi 7 chipset. **Key discovery**: MT7927 is architecturally identical to MT7925 (fully supported in Linux 6.7+) except for 320MHz channel width capability. We adapt the existing mt7925 driver rather than reverse engineering from scratch.

**Official Product Name**: MT7927 802.11be 320MHz 2x2 PCIe Wireless Network Adapter [Filogic 380]

**Current Status**: BLOCKED on DMA descriptor processing. All initialization steps succeed (PCI probe, power management, WFSYS reset, ring allocation), but DMA hardware doesn't advance descriptor index (DIDX stuck at 0). This prevents MCU command completion and firmware transfer.

**⚠️ NEW FINDING**: Hardware analysis reveals **L1 ASPM and L1 substates are ENABLED** (driver only disables L0s). This is a prime suspect for DMA blocking - device may enter L1.2 sleep during DMA operations. See HARDWARE_ANALYSIS.md for details.

## Critical Files to Review First

1. **HARDWARE_ANALYSIS.md** - ⚠️ **READ THIS FIRST** - lspci analysis revealing L1 ASPM issue and BAR0 size discrepancy
2. **AGENTS.md** - Session bootstrap with current blocker details, hardware context, and what's been tried
3. **DEVELOPMENT_LOG.md** - Complete chronological development history (critical for understanding all previous attempts)
4. **docs/dma_transfer_implementation.plan.md** - Implementation plan with task status
5. **README.md** - Project overview, build instructions, expected outputs

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
   - Map BAR0 (1MB SRAM) and BAR2 (DMA registers)
   - Acquire power management ownership via LPCTL register (0x04 → 0x00)
   - Execute WFSYS reset (WFSYS_SW_RST_B timing, poll INIT_DONE)
   - Disable PCIe ASPM L0s power saving

2. **DMA Setup** (mt7927_dma.c)
   - Allocate coherent DMA memory for descriptor rings
   - Configure 8 TX rings (0-7) and 16 RX rings (0-15)
   - Set ring base addresses, counts, and indices
   - **MT7927-specific**: Uses rings 4 (FWDL) and 5 (MCU_WM), NOT 16/15 like MT7925

3. **Firmware Load** (mt7927_mcu.c) - **CURRENTLY BLOCKED HERE**
   - Send PATCH_SEM_CONTROL command via TX ring 5
   - Transfer firmware chunks via TX ring 4
   - Signal completion and activate firmware
   - **Blocker**: First MCU command times out because DMA_DIDX never advances

### Critical Hardware Differences from MT7925

| Property | MT7925 | MT7927 |
|----------|--------|--------|
| TX Rings | 17 (0-16) | 8 (0-7) |
| FWDL Queue | Ring 16 | Ring 4 |
| MCU Queue | Ring 15 | Ring 5 |
| WFDMA1 | Present | **NOT present** |
| BAR0 Size | 1MB (unverified) | **2MB** (lspci confirmed) |
| Register offsets | See mt7925 driver | Same as MT7925 |

### Hardware Configuration (from lspci)

| Property | Value |
|----------|-------|
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
// - BAR0: Main 1MB memory region (SRAM + all registers)
// - BAR2: DMA control registers (use BAR0 instead, BAR2 is read-only shadow)
```

### Error Handling
- Always check for chip error state (0xffffffff reads indicate hung chip)
- Use pr_err/pr_info for kernel logging
- Include register values in error messages for debugging
- Timeouts should include current hardware state in error output

## Common Development Tasks

### Adding Diagnostic Code
Create test modules in `tests/05_dma_impl/` or `diag/` rather than modifying production driver. Test modules allow rapid iteration without risking production code stability.

### Comparing with MT7925 Reference
The `reference/linux/drivers/net/wireless/mediatek/mt76/mt7925/` directory contains reference implementation. Key files:
- `pci.c` - PCI initialization sequence
- `mcu.c` - MCU protocol and firmware loading
- `dma.c` - DMA setup
- `../dma.c` - Shared mt76 DMA infrastructure

### Debugging DMA Issues
1. Check ring indices via test modules (test_dma_queues.ko shows CIDX/DIDX/CPU_DIDX)
2. Verify descriptor memory with coherent DMA allocation debug
3. Monitor RST register state (should be 0x30 during config)
4. Check MCU ready status (FW_OWN_REQ_SET should return 0x1)
5. Look for interrupt activity (IRQ handler logs)

## What NOT to Do

- **Don't write to random registers** - Can hang the chip (recovery requires PCI rescan)
- **Don't assume MT7925 code works directly** - Ring assignments differ, WFDMA1 doesn't exist
- **Don't modify production driver without tests** - Use test modules for experimentation
- **Don't clear WFDMA0_RST to 0x00** - Wipes ring configuration immediately
- **Don't access registers when chip is in error state** - Check for 0xffffffff first

## Investigation Priorities

### 🔥 Priority 1: L1 ASPM Power States (NEW - Test First!)

**Test**: Disable L1 and L1 substates that lspci shows are enabled:

**Bash:**
```bash
# Runtime disable via setpci
DEVICE=$(lspci -nn | grep 14c3:7927 | cut -d' ' -f1)
sudo setpci -s $DEVICE CAP_EXP+10.w=0x0000
```

**Fish:**
```fish
# Runtime disable via setpci
set DEVICE (lspci -nn | grep 14c3:7927 | cut -d' ' -f1)
sudo setpci -s $DEVICE CAP_EXP+10.w=0x0000
```

**In driver code:**
```c
pci_disable_link_state(pdev, PCIE_LINK_STATE_L0S | PCIE_LINK_STATE_L1);
```

**Why**: Device may enter L1.2 sleep when DMA should be active. Driver only disables L0s currently.

### Original Hypotheses (from DEVELOPMENT_LOG.md)

2. **RST State Transition** - Find MT7927-specific way to enable DMA processing without losing ring config
3. **BAR0 Size Impact** - Verify register offsets with 2MB BAR0 (not 1MB as docs claimed)
4. **Missing Doorbell/Kick** - Additional trigger register beyond CIDX write
5. **Descriptor Format** - MT7927 may expect different TXD structure
6. **Bootstrap Mode** - Firmware may need partial activation before DMA works
7. **MSI Interrupts** - Test MSI mode instead of legacy INTx (32 vectors available)
8. **MT7996 Comparison** - Newer chip may share MT7927's quirks

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
