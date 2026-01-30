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

1. **Read the Root Cause Analysis** → [ZOUYONGHAO_ANALYSIS.md](ZOUYONGHAO_ANALYSIS.md) ⚠️ **CRITICAL!**
   - Understand the root cause: MT7927 ROM doesn't support mailbox protocol
   - Learn about the solution: polling-based firmware loading
   - See the working reference implementation

2. **Understand the Architecture** → [MT6639_ANALYSIS.md](MT6639_ANALYSIS.md)
   - Learn that MT7927 is MT6639 variant (NOT MT7925!)
   - Understand CONNAC3X family architecture
   - See ring assignment validation (rings 15/16)

3. **Review Development History** → [../DEVELOPMENT_LOG.md](../DEVELOPMENT_LOG.md)
   - Understand the journey and key discoveries (17 phases)
   - Phase 16: MT6639 discovery
   - Phase 17: Root cause found (mailbox protocol)

4. **Check the Roadmap** → [ROADMAP.md](ROADMAP.md)
   - See current status and implementation needs
   - Understand what's working and what's not
   - Plan next steps

5. **Study the Test Modules** → [test_modules_documentation.md](test_modules_documentation.md)
   - Learn how individual components are tested
   - Understand the initialization sequence
   - See examples of working code

### For Experienced Developers

Jump directly to:
- **Root Cause & Solution** → [ZOUYONGHAO_ANALYSIS.md](ZOUYONGHAO_ANALYSIS.md) ⚠️ **READ THIS FIRST!**
- **Architecture Proof** → [MT6639_ANALYSIS.md](MT6639_ANALYSIS.md)
- **Hardware Details** → [HARDWARE.md](HARDWARE.md)
- **Implementation Guide** → [CONTRIBUTING.md](CONTRIBUTING.md)
- **PCI & Power Management** → [mt7927_pci_documentation.md](mt7927_pci_documentation.md)
- **Register Definitions** → [headers_documentation.md](headers_documentation.md)
- **Test Results** → [TEST_RESULTS_SUMMARY.md](TEST_RESULTS_SUMMARY.md)

---

## Documentation Files

### Core Documentation

#### [ZOUYONGHAO_ANALYSIS.md](ZOUYONGHAO_ANALYSIS.md) ⚠️ **CRITICAL - READ FIRST!**
**Purpose:** Root cause analysis of DMA blocker and solution

**Contents:**
- **ROOT CAUSE**: MT7927 ROM bootloader doesn't support mailbox protocol
- Why our driver blocks (waits for responses ROM never sends)
- Working solution: polling-based firmware loading
- Complete implementation requirements
- Quick test approach (30 min)
- Full solution approach (2-3 hours)
- Evidence from zouyonghao working driver

**When to Read:**
- **IMMEDIATELY** - This explains everything!
- Before implementing any fixes
- When understanding why DMA appears stuck
- When planning next development steps

---

#### [MT6639_ANALYSIS.md](MT6639_ANALYSIS.md)
**Purpose:** Proves MT7927 is MT6639 variant, validates architecture

**Contents:**
- Evidence from MediaTek kernel modules
- MT7927 → MT6639 driver data mapping
- CONNAC3X family architecture
- Ring assignment validation (15/16)
- Firmware compatibility proof
- Confidence assessment

**When to Read:**
- Understanding MT7927 architecture
- Validating ring assignments
- Understanding firmware compatibility
- Comparing with MT7925 (sibling chip)

---

#### [AGENTS.md](../AGENTS.md)
**Purpose:** Session bootstrap guide for resuming work

**Contents:**
- Current project status (root cause found!)
- Complete project structure
- Hardware architecture details
- Development history (17 phases)
- Implementation path forward
- Working commands and troubleshooting

**When to Read:**
- Starting a new development session
- Need quick context for AI assistance
- Want to understand what's been done
- Looking for implementation next steps

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

#### [ROADMAP.md](ROADMAP.md)
**Purpose:** Development roadmap and implementation status

**Contents:**
- Current status (root cause found!)
- Phase 1: Get It Working (current focus)
  - Completed components ✅
  - In progress 🔧 (polling-based firmware loader)
  - Blocked items ⏸️
- Phase 2: Make It Good (network functionality, performance)
- Phase 3: Make It Official (upstream submission)
- Implementation approaches (quick test vs full solution)
- Testing and validation status
- Documentation status

**When to Read:**
- Understanding current project status
- Planning next development steps
- Checking what's completed vs pending
- Understanding upstream submission timeline

---

#### [HARDWARE.md](HARDWARE.md)
**Purpose:** Complete hardware specifications and configuration

**Contents:**
- Hardware information (chip, PCI ID, architecture)
- Memory map (BAR0: 2MB, BAR2: 32KB)
- lspci hardware configuration details
- Key register locations
- Architecture comparison (MT6639/MT7925/MT7927)
- Firmware files and download links

**When to Read:**
- Looking up hardware specifications
- Understanding memory layout
- Finding register addresses
- Comparing with other chips

---

#### [TROUBLESHOOTING.md](TROUBLESHOOTING.md)
**Purpose:** Common issues and solutions

**Contents:**
- Driver won't load
- Chip in error state (PCI rescan)
- Firmware not found
- Build failures
- Device state corrupted after testing
- ASPM issues
- Kernel oops or panic
- DMA issues (mailbox protocol - root cause)
- Getting more help

**When to Read:**
- Encountering errors or issues
- Module won't load or crashes
- Need to recover chip state
- Debugging hardware problems

---

#### [FAQ.md](FAQ.md)
**Purpose:** Frequently asked questions

**Contents:**
- General questions (why not mt7925e, MT7927 vs MT7925, etc.)
- Technical questions (firmware, rings, blocker, ASPM)
- Development questions (how to help, reference code, implementation)
- Build and testing questions
- Architecture questions (CONNAC3X, MT6639 mapping)
- Debugging questions
- License and legal

**When to Read:**
- Have a common question
- New to the project
- Understanding architecture
- Want to contribute

---

#### [CONTRIBUTING.md](CONTRIBUTING.md)
**Purpose:** Contribution guidelines

**Contents:**
- Current priority: implement firmware loader
- Ways to contribute (implementation, testing, code review, documentation, bugs)
- Development workflow
- Code style guidelines
- Testing requirements
- Bug report requirements
- Pull request guidelines
- Reference material
- Communication channels

**When to Read:**
- Want to contribute code
- Planning to submit changes
- Need coding style guidelines
- Reporting bugs
- Before creating pull requests

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

The MediaTek MT7927 is a WiFi 7 (802.11be) chipset that supports 320MHz channel width. It is an **MT6639 variant** (NOT MT7925 as initially assumed), proven via MediaTek kernel modules.

### Project Status

**Current State:** ROOT CAUSE FOUND! 🎯 (2026-01-31)

**Discovery:** MT7927 ROM bootloader does NOT support mailbox command protocol! Our driver waits for mailbox responses that the ROM will never send. DMA hardware works correctly - we're using the wrong communication protocol.

The driver successfully:
- ✅ Binds to MT7927 hardware
- ✅ Completes power management handshake
- ✅ Configures DMA rings correctly
- ✅ Loads firmware files
- ✅ **Identified root cause** - Mailbox protocol not supported

**Next Step:**
- 🔧 Implement polling-based firmware loader (validated by zouyonghao working driver)

See [ZOUYONGHAO_ANALYSIS.md](ZOUYONGHAO_ANALYSIS.md) for complete root cause analysis and [DEVELOPMENT_LOG.md](../DEVELOPMENT_LOG.md) Phase 17 for detailed status.

### Key Discoveries

1. **MT7927 is MT6639 variant** - MediaTek kernel modules prove this architectural relationship
2. **CONNAC3X family** - MT6639 and MT7925 share firmware via common architecture
3. **Ring protocol validated** - Uses CONNAC3X standard (Ring 15: MCU, Ring 16: FWDL)
4. **Root cause found** - MT7927 ROM bootloader doesn't implement mailbox protocol
5. **Solution identified** - Polling-based firmware loading (zouyonghao reference proves it works)

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

- **Chip:** MediaTek MT7927 WiFi 7 (802.11be) - "Filogic 380"
- **PCI ID:** 14c3:7927 (vendor: MediaTek, device: MT7927)
- **Architecture:** **MT6639 variant** (CONNAC3X family) - NOT MT7925!
- **Firmware:** Uses MT7925 firmware files (CONNAC3X family shared firmware)
- **Ring Protocol:** CONNAC3X standard (Ring 15: MCU, Ring 16: FWDL)

See [HARDWARE.md](HARDWARE.md) for complete specifications and [MT6639_ANALYSIS.md](MT6639_ANALYSIS.md) for architecture proof.

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

1. **[ZOUYONGHAO_ANALYSIS.md](ZOUYONGHAO_ANALYSIS.md)** - **ROOT CAUSE & SOLUTION** ⚠️ **READ FIRST!**
2. **[MT6639_ANALYSIS.md](MT6639_ANALYSIS.md)** - Architecture proof (MT7927 = MT6639 variant)
3. **[DEVELOPMENT_LOG.md](../DEVELOPMENT_LOG.md)** - Complete project history (Phase 17!)
4. **[ROADMAP.md](ROADMAP.md)** - Current status and implementation plan
5. **[CONTRIBUTING.md](CONTRIBUTING.md)** - How to contribute (implementation needed!)

### For Specific Tasks

- **Understanding the blocker:** [ZOUYONGHAO_ANALYSIS.md](ZOUYONGHAO_ANALYSIS.md) ⚠️
- **Implementing the fix:** [CONTRIBUTING.md](CONTRIBUTING.md) + [ZOUYONGHAO_ANALYSIS.md](ZOUYONGHAO_ANALYSIS.md)
- **Hardware specifications:** [HARDWARE.md](HARDWARE.md)
- **Troubleshooting issues:** [TROUBLESHOOTING.md](TROUBLESHOOTING.md)
- **Common questions:** [FAQ.md](FAQ.md)
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
