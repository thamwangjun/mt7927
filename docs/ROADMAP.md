# Development Roadmap

> **⚠️ PROJECT STATUS: STALLED (Phase 29c) ⚠️**
>
> This project has reached an impasse. The driver does not work and WiFi functionality is not available.

---

## Current Status

**Status as of 2026-02-01**: **PHASE 29c - DMA PATH FAILURE (UNSOLVED)**

After 29+ phases of investigation, firmware loading fails because DMA data never reaches device memory.

### Summary

| Milestone | Status |
|-----------|--------|
| PCI enumeration | ✅ Complete |
| BAR mapping | ✅ Complete |
| CB_INFRA initialization | ✅ Complete |
| WiFi subsystem reset | ✅ Complete |
| DMA ring configuration | ✅ Complete |
| Firmware file parsing | ✅ Complete |
| Host-side DMA transfer | ✅ Complete |
| **Device receives data** | ❌ **FAILED** |
| Firmware execution | ❌ BLOCKED |
| WiFi interface | ❌ BLOCKED |

---

## The Unsolved Problem

**DMA data never reaches device memory.**

### Evidence

1. **MCU status = 0x00000000** after all firmware regions
   - MCU never acknowledges receiving any data
   - Expected: status updates as each region is processed

2. **FW_N9_RDY = 0x00000002** (stuck)
   - BIT(1) = 1: Download mode entered
   - BIT(0) = 0: N9 CPU not ready
   - Expected: 0x00000003 (both bits set)

3. **FW_START command times out**
   - Tested with wait=true: 3-second timeout
   - Firmware not running, cannot respond

4. **IOMMU page faults** observed in some runs
   - Addresses like 0xff6b0xxx cause AMD-Vi IO_PAGE_FAULT
   - Suggests DMA mapping issues

### Investigated But Not Resolved

- IOMMU domain configuration
- Coherent vs streaming DMA differences
- DMA address mask (32-bit vs 64-bit)
- MediaTek-specific DMA enable bits
- PCIe bus master configuration
- Power/clock gating for DMA engine

---

## Investigation History

### Phase 1-16: Initial Development
- Assumed MT7927 = MT7925 variant (wrong)
- Assumed mailbox protocol works (wrong)
- Assumed WFDMA at 0x2000 (wrong)

### Phase 17: Root Cause Discovery
- **Found**: ROM doesn't support mailbox protocol
- **Solution**: Use polling-based firmware loading

### Phase 18-21: Architecture Fixes
- **Found**: MT7927 = MT6639 variant (not MT7925)
- **Found**: WFDMA base is 0xd4000 (not 0x2000)
- **Found**: CB_INFRA remap required (0x74037001)

### Phase 22-26: Ring Configuration
- Fixed GLO_CFG timing
- Fixed unused ring initialization
- Fixed TXD control word format

### Phase 27: DMA Page Faults
- Fixed TX ring page faults (all rings need valid BASE)
- Fixed RX_DMA_EN timing (only enable TX during FWDL)
- Added HOST2MCU doorbell interrupts

### Phase 28: Memory Access Failure
- WFDMA_OVERFLOW=1 but DIDX=0
- Device sees something but doesn't process it
- Investigated zouyonghao config additions

### Phase 29: Linux 6.18 Adaptation
- Fixed mac80211 API changes
- Fixed hrtimer API changes
- **Firmware "loads" but never executes**

### Phase 29b-29c: Final Investigation
- Verified ring configuration is correct (hardware readback matches)
- Tested FW_START with wait=true (times out)
- MCU status never changes from 0x00000000
- **Concluded: DMA path failure, cause unknown**

---

## What Would Be Needed to Continue

### Option 1: IOMMU Investigation
Test with `iommu=off` kernel parameter to rule out IOMMU as the cause.

### Option 2: DMA Tracing
Add extensive tracing to understand exactly where DMA fails:
- Verify dma_map_single returns valid addresses
- Check if IOMMU page tables include mapped addresses
- Monitor PCIe traffic (if tools available)

### Option 3: MediaTek Documentation
Obtain official MediaTek documentation for:
- MT6639/MT7927 DMA configuration
- Required enable bits and remap registers
- Initialization sequence

### Option 4: Reference Implementation
Find a working MT7927 driver implementation:
- Check if MediaTek has released official Linux support
- Look for other community efforts
- Analyze Windows driver behavior

---

## Lessons Learned

1. **Wrong initial assumptions** led to months of debugging
   - MT7927 is NOT MT7925
   - ROM doesn't support mailbox
   - WFDMA base is not at 0x2000

2. **Hardware debugging without documentation is difficult**
   - MediaTek provides limited public documentation
   - Reference code analysis was essential
   - Many discoveries came from comparing implementations

3. **DMA issues are hard to diagnose**
   - Host-side success doesn't mean device receives data
   - IOMMU adds complexity
   - Need visibility into device-side behavior

---

## Completed Components

Despite the failure, significant work was completed:

### Working Code
- PCI probe and initialization
- BAR mapping and register access
- CB_INFRA configuration
- WiFi subsystem reset
- DMA ring allocation and configuration
- Firmware file parsing
- Polling-based firmware loader structure
- Linux 6.18 API compatibility

### Documentation
- Complete 29-phase development log
- Architecture analysis (MT6639 relationship)
- Register definitions
- Reference code analysis
- Hardware specifications

### Test Infrastructure
- Diagnostic modules
- Test modules for each component
- Debug output throughout driver

---

## Future Phases (If Continued)

### Phase 2: Make It Work
- [ ] Resolve DMA path failure
- [ ] Achieve firmware execution
- [ ] Create WiFi interface
- [ ] Basic connectivity

### Phase 3: Make It Good
- [ ] Full network functionality
- [ ] Power management
- [ ] Performance optimization
- [ ] WiFi 7 features (320MHz, MLO)

### Phase 4: Make It Official
- [ ] Code cleanup for upstream
- [ ] Submit to linux-wireless
- [ ] Address review feedback
- [ ] Merge to mainline

---

## See Also

- [../README.md](../README.md) - Project status summary
- [../DEVELOPMENT_LOG.md](../DEVELOPMENT_LOG.md) - Complete investigation history
- [ZOUYONGHAO_ANALYSIS.md](ZOUYONGHAO_ANALYSIS.md) - DMA debugging analysis
- [MT6639_ANALYSIS.md](MT6639_ANALYSIS.md) - Architecture proof

---

*Last updated: 2026-02-01 (Phase 29c - Project Stalled)*
