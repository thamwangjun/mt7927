# MT7927 Driver Documentation Index

> **⚠️ PROJECT STATUS: STALLED (Phase 29c) ⚠️**
>
> After 29+ phases of investigation, this driver development effort has reached an impasse.
> The driver **does not work** - firmware loads but never executes. See [Root Cause](#root-cause-unsolved) below.

---

## Quick Navigation

### Critical Status Documents

| Document | Purpose |
|----------|---------|
| [../README.md](../README.md) | **Project status and failure summary** |
| [ZOUYONGHAO_ANALYSIS.md](ZOUYONGHAO_ANALYSIS.md) | DMA debugging analysis (29 phases) |
| [../DEVELOPMENT_LOG.md](../DEVELOPMENT_LOG.md) | Complete 29-phase investigation history |

### Technical Documentation

| Document | Purpose |
|----------|---------|
| [MT6639_ANALYSIS.md](MT6639_ANALYSIS.md) | Architecture proof (MT7927 = MT6639 variant) |
| [HARDWARE.md](HARDWARE.md) | Hardware specifications |
| [REFERENCE_SOURCES.md](REFERENCE_SOURCES.md) | Reference code analysis |
| [headers_documentation.md](headers_documentation.md) | Register definitions |

---

## Root Cause (Unsolved)

**DMA data never reaches device memory.**

After extensive investigation:
- Ring configuration is correct (verified via hardware readback)
- Firmware files parse correctly
- DMA descriptors are written correctly
- Host-side transfer completes successfully

But:
- MCU status stays at 0x00000000 (never acknowledges data)
- FW_N9_RDY stuck at 0x00000002 (download mode, N9 not ready)
- IOMMU page faults observed in some runs
- Mailbox commands timeout (firmware not running)

**Hypotheses investigated but not resolved:**
- IOMMU domain configuration
- MediaTek-specific DMA requirements
- Missing power/clock gating configuration
- PCIe bus master configuration

---

## What Works

| Component | Status |
|-----------|--------|
| PCI enumeration | ✅ Device detected (14c3:7927) |
| BAR0 mapping | ✅ 2MB register space accessible |
| CB_INFRA initialization | ✅ PCIe remap configured |
| WiFi subsystem reset | ✅ WFSYS reset completes |
| DMA ring allocation | ✅ Rings configured correctly |
| Firmware file loading | ✅ Files parsed from disk |
| Host-side DMA transfer | ✅ Descriptors written, indices advanced |

## What Does NOT Work

| Component | Status |
|-----------|--------|
| Firmware execution | ❌ MCU never receives data |
| Mailbox communication | ❌ All commands timeout |
| WiFi interface | ❌ Not created |
| Network connectivity | ❌ Not functional |

---

## Key Technical Discoveries

Despite the failure, valuable technical knowledge was gained:

1. **MT7927 is an MT6639 variant** (not MT7925)
   - Proven via MediaTek kernel modules
   - PCI ID 0x7927 maps to MT6639 driver data

2. **ROM bootloader doesn't support mailbox protocol**
   - Must use polling-based firmware loading
   - This was discovered in Phase 17

3. **WFDMA base is at 0xd4000** (not 0x2000)
   - MT7921 legacy address doesn't work
   - Discovered in Phase 21

4. **CB_INFRA remap required**
   - Must set 0x74037001 at BAR0+0x1f6554
   - Required before any WFDMA access

5. **Sparse ring layout**
   - Only rings 0,1,2,15,16 are used
   - Ring 15: MCU commands, Ring 16: Firmware download

---

## Documentation Files

### Analysis Documents
- [MT6639_ANALYSIS.md](MT6639_ANALYSIS.md) - Architecture proof
- [MT7996_COMPARISON.md](MT7996_COMPARISON.md) - Why MT7927 is NOT MT7996 variant
- [ZOUYONGHAO_ANALYSIS.md](ZOUYONGHAO_ANALYSIS.md) - DMA investigation (sections 2a-2r)
- [REFERENCE_SOURCES.md](REFERENCE_SOURCES.md) - Reference code origins
- [FIRMWARE_ANALYSIS.md](FIRMWARE_ANALYSIS.md) - Firmware compatibility
- [WINDOWS_FIRMWARE_ANALYSIS.md](WINDOWS_FIRMWARE_ANALYSIS.md) - Windows driver analysis

### Technical Reference
- [HARDWARE.md](HARDWARE.md) - Hardware specifications
- [headers_documentation.md](headers_documentation.md) - Register definitions
- [mt7927_pci_documentation.md](mt7927_pci_documentation.md) - PCI layer
- [dma_mcu_documentation.md](dma_mcu_documentation.md) - DMA/MCU layer
- [test_modules_documentation.md](test_modules_documentation.md) - Test modules
- [diagnostic_modules_documentation.md](diagnostic_modules_documentation.md) - Diagnostic tools

### Process Documents
- [ROADMAP.md](ROADMAP.md) - Development roadmap (stalled)
- [CONTRIBUTING.md](CONTRIBUTING.md) - Contribution guidelines
- [TROUBLESHOOTING.md](TROUBLESHOOTING.md) - Common issues
- [FAQ.md](FAQ.md) - Frequently asked questions
- [TEST_RESULTS_SUMMARY.md](TEST_RESULTS_SUMMARY.md) - Test results

---

## Repository Structure

```
mt7927/
├── README.md                    # Project status (STALLED)
├── DEVELOPMENT_LOG.md           # 29-phase investigation history
├── CLAUDE.md                    # AI agent context
│
├── reference_zouyonghao_mt7927/ # Modified mt76 driver
│   └── mt76-outoftree/          # Builds but doesn't work
│
├── src/                         # Original driver (abandoned)
├── tests/                       # Test modules
├── diag/                        # Diagnostic modules
│
├── docs/                        # This directory
│   ├── README.md               # This file
│   └── ...                     # See listing above
│
└── reference_*/                 # Reference implementations
```

---

## For Future Contributors

If you wish to continue this effort, the key unsolved problem is:

**Why doesn't DMA data reach device memory?**

The host correctly writes DMA descriptors and advances indices, but the device MCU never acknowledges receiving any data. Possible areas to investigate:

1. **IOMMU configuration** - Are DMA buffer addresses properly mapped?
2. **MediaTek-specific DMA init** - Is there a missing enable bit or remap register?
3. **PCIe configuration** - Bus master, TLP settings, etc.
4. **Power/clock domains** - Is the DMA engine properly powered?

All investigation notes are in [../DEVELOPMENT_LOG.md](../DEVELOPMENT_LOG.md) and [ZOUYONGHAO_ANALYSIS.md](ZOUYONGHAO_ANALYSIS.md).

---

*Last updated: 2026-02-01 (Phase 29c - Project Stalled)*
