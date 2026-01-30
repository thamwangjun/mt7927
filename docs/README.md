# MT7927 Driver Documentation Index

Welcome to the MT7927 WiFi 7 Linux Driver documentation. This index provides navigation to all documentation files and a quick start guide for new developers.

## Table of Contents

1. [Quick Start Guide](#quick-start-guide)
2. [Documentation Files](#documentation-files)
3. [Project Overview](#project-overview)
4. [Development Resources](#development-resources)

---

## Quick Start Guide

### For New Developers

If you're new to this project, start here:

1. **Read the Project Overview** → [README.md](../README.md)
   - Understand what MT7927 is and why this driver exists
   - Learn about the key discovery: MT7927 = MT7925 + 320MHz

2. **Review Development History** → [DEVELOPMENT_LOG.md](../DEVELOPMENT_LOG.md)
   - Understand the journey and key discoveries
   - Learn about challenges encountered and solutions found
   - See the current status and remaining issues

3. **Study the Test Modules** → [test_modules_documentation.md](test_modules_documentation.md)
   - Learn how individual components are tested
   - Understand the initialization sequence
   - See examples of working code

4. **Explore Diagnostic Tools** → [diagnostic_modules_documentation.md](diagnostic_modules_documentation.md)
   - Understand how to probe and debug the hardware
   - Learn about safe vs. dangerous operations
   - See register layouts and memory maps

5. **Dive into Implementation** → [dma_mcu_documentation.md](dma_mcu_documentation.md)
   - Understand DMA ring structures
   - Learn about MCU communication protocol
   - See firmware loading mechanisms

### For Experienced Developers

Jump directly to:
- **PCI & Power Management** → [mt7927_pci_documentation.md](mt7927_pci_documentation.md)
- **Register Definitions** → [headers_documentation.md](headers_documentation.md)
- **Test Results** → [TEST_RESULTS_SUMMARY.md](TEST_RESULTS_SUMMARY.md)

---

## Documentation Files

### Core Documentation

#### [AGENTS.md](../AGENTS.md)
**Purpose:** Session bootstrap guide for resuming work

**Contents:**
- Current project status and blocker
- Complete project structure
- Hardware architecture details
- What has been tried (8 phases)
- Hypotheses for next investigation
- Working commands and troubleshooting

**When to Read:**
- Starting a new development session
- Need quick context for AI assistance
- Want to understand what's been done
- Looking for next investigation steps

---

#### [DEVELOPMENT_LOG.md](../DEVELOPMENT_LOG.md)
**Purpose:** Complete chronological record of the development process

**Contents:**
- Executive summary and current status
- Phase-by-phase development history
- Key discoveries and breakthroughs
- Error analysis and debugging journey
- Register values and observations
- Next steps and hypotheses

**When to Read:**
- Understanding project history and context
- Learning from debugging approaches
- Finding solutions to similar problems
- Understanding why certain design decisions were made

---

#### [dma_transfer_implementation.plan.md](dma_transfer_implementation.plan.md)
**Purpose:** Implementation plan with task tracking

**Contents:**
- Original implementation plan
- Task status (completed/blocked)
- Architecture overview
- Phase-by-phase implementation tasks
- Current blocker documentation

**When to Read:**
- Understanding the original plan
- Checking what tasks are complete
- Seeing what was supposed to happen
- Planning next steps

---

#### [test_modules_documentation.md](test_modules_documentation.md)
**Purpose:** Comprehensive documentation for DMA implementation test modules

**Contents:**
- Detailed documentation for each test module:
  - `test_power_ctrl.c` - Power management handshake
  - `test_wfsys_reset.c` - WiFi subsystem reset
  - `test_dma_queues.c` - DMA ring configuration
  - `test_fw_load.c` - Complete firmware loading sequence
- Test sequences and expected outputs
- Pass/fail conditions
- Building and running instructions
- Troubleshooting guide

**When to Read:**
- Running and interpreting test results
- Understanding component validation
- Debugging initialization issues
- Learning the initialization sequence

**Location:** `tests/05_dma_impl/`

---

#### [diagnostic_modules_documentation.md](diagnostic_modules_documentation.md)
**Purpose:** Documentation for hardware diagnostic and exploration modules

**Contents:**
- Safe read-only diagnostic modules
- Power management and initialization tests
- DMA and ring testing tools
- Full initialization sequence tests
- Wide scanning modules (dangerous operations)
- Register layouts and memory maps
- Safety considerations and warnings

**When to Read:**
- Probing hardware behavior
- Understanding register layouts
- Debugging hardware issues
- Learning safe exploration techniques

**Location:** `diag/`

---

#### [dma_mcu_documentation.md](dma_mcu_documentation.md)
**Purpose:** Technical documentation for DMA and MCU implementation

**Contents:**
- DMA ring structures and descriptors
- MCU communication protocol
- Firmware loading mechanisms
- Queue assignments and configurations
- Interrupt handling
- Command/response formats

**When to Read:**
- Implementing DMA functionality
- Understanding MCU protocol
- Debugging firmware loading
- Working with descriptor rings

**Related Files:** `src/mt7927_dma.c`, `src/mt7927_mcu.c`

---

#### [mt7927_pci_documentation.md](mt7927_pci_documentation.md)
**Purpose:** PCI initialization, power management, and device setup

**Contents:**
- PCI probe and remove sequences
- BAR mapping and memory regions
- Power management handshake
- WiFi subsystem reset
- Device initialization order
- Register access patterns

**When to Read:**
- Understanding device initialization
- Implementing power management
- Debugging PCI binding issues
- Learning register remapping

**Related Files:** `src/mt7927_pci.c`

---

#### [headers_documentation.md](headers_documentation.md)
**Purpose:** Register definitions, constants, and data structures

**Contents:**
- Register address definitions
- Bit field constants
- Queue ID assignments
- Data structure definitions
- Interrupt masks and flags

**When to Read:**
- Looking up register addresses
- Understanding bit field meanings
- Finding queue assignments
- Understanding data structures

**Related Files:** `src/mt7927_regs.h`, `src/mt7927.h`

---

#### [TEST_RESULTS_SUMMARY.md](TEST_RESULTS_SUMMARY.md)
**Purpose:** Summary of test results and validation status

**Contents:**
- Test execution results
- Component validation status
- Known issues and limitations
- Success criteria and pass/fail conditions

**When to Read:**
- Checking what's been validated
- Understanding current capabilities
- Finding known issues
- Planning next test steps

---

## Project Overview

### What is MT7927?

The MediaTek MT7927 is a WiFi 7 (802.11be) chipset that supports 320MHz channel width. It is architecturally identical to the MT7925 (which has full Linux support) except for the additional 320MHz capability.

### Project Status

**Current State:** ON-HOLD INDEFINITELY 🚧

The driver successfully:
- ✅ Binds to MT7927 hardware
- ✅ Completes power management handshake
- ✅ Configures DMA rings correctly
- ✅ Loads firmware files

**Remaining Issue:**
- ❌ DMA hardware doesn't process TX descriptors (DIDX doesn't advance)

See [DEVELOPMENT_LOG.md](../DEVELOPMENT_LOG.md) Phase 9-10 for detailed status.

### Key Discovery

**MT7927 = MT7925 + 320MHz**

This means we can adapt the existing MT7925 driver rather than writing from scratch. The MT7925 firmware is compatible with MT7927.

### Project Structure

```
mt7927/
├── README.md                    # Main project overview
├── AGENTS.md                    # Session bootstrap guide
├── DEVELOPMENT_LOG.md           # Complete development history
├── docs/                       # Documentation (this directory)
│   ├── README.md              # This file (documentation index)
│   ├── dma_transfer_implementation.plan.md  # Implementation plan
│   ├── test_modules_documentation.md
│   ├── diagnostic_modules_documentation.md
│   ├── dma_mcu_documentation.md
│   ├── mt7927_pci_documentation.md
│   ├── headers_documentation.md
│   └── TEST_RESULTS_SUMMARY.md
├── src/                        # Production driver source
│   ├── mt7927.h
│   ├── mt7927_regs.h
│   ├── mt7927_pci.c
│   ├── mt7927_dma.c
│   └── mt7927_mcu.c
├── tests/                      # Test modules
│   └── 05_dma_impl/           # DMA implementation tests
│       ├── test_power_ctrl.c
│       ├── test_wfsys_reset.c
│       ├── test_dma_queues.c
│       └── test_fw_load.c
└── diag/                       # Diagnostic modules
    ├── mt7927_diag.c
    ├── mt7927_power_unlock.c
    └── ... (many more)
```

---

## Development Resources

### Reference Implementations

The project is based on the MT7925 driver in the Linux kernel:

**Kernel Source Location:**
```
drivers/net/wireless/mediatek/mt76/mt7925/
├── pci.c         # PCI probe and initialization
├── mcu.c         # MCU communication and firmware loading
├── init.c        # Hardware initialization
└── dma.c         # DMA setup and transfer
```

**Online References:**
- [Linux kernel source](https://github.com/torvalds/linux/tree/master/drivers/net/wireless/mediatek/mt76/mt7925)
- [mt76 framework](https://github.com/openwrt/mt76)

### Hardware Information

- **Chip:** MediaTek MT7927 WiFi 7 (802.11be)
- **PCI ID:** 14c3:7927 (vendor: MediaTek, device: MT7927)
- **Architecture:** Same as MT7925 except supports 320MHz channels
- **Firmware:** Uses MT7925 firmware files

### Building and Testing

**Prerequisites:**
- Linux kernel 6.7+ (for MT7925 base support)
- Kernel headers: `sudo apt install linux-headers-$(uname -r)`
- MT7927 hardware: `lspci -nn | grep 14c3:7927`
- Firmware files (see [README.md](../README.md))

**Build:**
```bash
make tests          # Build test modules
make                # Build production driver
```

**Test:**
```bash
# Unload conflicting drivers
sudo rmmod mt7921e mt7925e mt7927 2>/dev/null

# Load test module
sudo insmod tests/05_dma_impl/test_power_ctrl.ko

# View results
sudo dmesg | tail -30
```

### Getting Help

**Documentation:**
- Start with this index for navigation
- Check [DEVELOPMENT_LOG.md](../DEVELOPMENT_LOG.md) for historical context
- Review test modules for working examples

**Debugging:**
- Use diagnostic modules in `diag/` for hardware exploration
- Check kernel logs: `sudo dmesg | grep mt7927`
- Review test module outputs for expected behavior

**Known Issues:**
- See [DEVELOPMENT_LOG.md](../DEVELOPMENT_LOG.md) Phase 9-10 for current blocker
- Check [TEST_RESULTS_SUMMARY.md](TEST_RESULTS_SUMMARY.md) for validation status

---

## Documentation Maintenance

### Adding New Documentation

When adding new documentation:

1. **Create the file** in `docs/` directory
2. **Update this index** with:
   - File name and path
   - Purpose and contents
   - When to read it
   - Related files
3. **Cross-reference** from related documentation
4. **Update README.md** if it's a major addition

### Documentation Standards

- Use Markdown format
- Include table of contents for long documents
- Provide code examples where applicable
- Include troubleshooting sections
- Cross-reference related documentation
- Keep examples up-to-date with code

---

## Quick Reference

### Most Important Documents

1. **[AGENTS.md](../AGENTS.md)** - Session bootstrap (start here!)
2. **[DEVELOPMENT_LOG.md](../DEVELOPMENT_LOG.md)** - Complete project history
3. **[dma_transfer_implementation.plan.md](dma_transfer_implementation.plan.md)** - Implementation plan
4. **[test_modules_documentation.md](test_modules_documentation.md)** - Test module reference
5. **[diagnostic_modules_documentation.md](diagnostic_modules_documentation.md)** - Hardware exploration tools

### For Specific Tasks

- **Understanding initialization:** [test_modules_documentation.md](test_modules_documentation.md)
- **Debugging hardware:** [diagnostic_modules_documentation.md](diagnostic_modules_documentation.md)
- **Implementing DMA:** [dma_mcu_documentation.md](dma_mcu_documentation.md)
- **PCI setup:** [mt7927_pci_documentation.md](mt7927_pci_documentation.md)
- **Register lookup:** [headers_documentation.md](headers_documentation.md)

---

## Summary

This documentation index provides:

- ✅ **Quick start guide** for new developers
- ✅ **Complete documentation listing** with descriptions
- ✅ **Navigation** to all project documentation
- ✅ **Development resources** and references
- ✅ **Project overview** and status

Start with the [Quick Start Guide](#quick-start-guide) above, then dive into the specific documentation you need. The [DEVELOPMENT_LOG.md](../DEVELOPMENT_LOG.md) provides the best overall context for understanding the project's journey and current state.
