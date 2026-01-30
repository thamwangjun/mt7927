# MT7927 Hardware Analysis from lspci

## Device Information

**Full Product Name**: MT7927 802.11be 320MHz 2x2 PCIe Wireless Network Adapter [Filogic 380]

**PCI Location**: 01:00.0 (varies by system - update commands accordingly)

**PCI IDs**:
- Vendor: 14c3 (MEDIATEK Corp.)
- Device: 7927
- Subsystem: MEDIATEK Corp. MT7927

## Critical Findings

### ⚠️ BAR0 Size Discrepancy

**lspci reports**: Region 0: 2MB (size=2M)
**Documentation claims**: 1MB

**Impact**: This is a significant discrepancy. The actual hardware has a 2MB BAR0, not 1MB as stated in multiple documentation files. Memory mapping code should account for full 2MB range.

**Memory Regions**:
- **BAR0**: 0x90600000, 64-bit, non-prefetchable, **2MB** (not 1MB!)
- **BAR2**: 0x90800000, 64-bit, non-prefetchable, 32KB (matches docs)

### PCIe Link Configuration

**Link Speed**: Gen3 x1 (8GT/s, single lane)
- Current speed: 8GT/s ✓
- Current width: x1 ✓
- Link training complete

**ASPM (Active State Power Management)**:
- **L0s**: Not mentioned in LnkCtl (driver disables this)
- **L1**: **ENABLED** ⚠️
- **L1.1 Substates**: ENABLED ⚠️
- **L1.2 Substates**: ENABLED ⚠️

**Potential DMA Blocker**: L1 power states are active! The driver disables L0s but L1 and L1 substates remain enabled. These deeper power states could interfere with DMA operations.

```
L1SubCtl1: PCI-PM_L1.2+ PCI-PM_L1.1+ ASPM_L1.2+ ASPM_L1.1+
           T_CommonMode=0us LTR1.2_Threshold=166912ns
```

### Interrupt Configuration

**IRQ**: 48 (pin A)
**MSI Capability**:
- **Count**: 1/32 (supports up to 32 MSI interrupts)
- Currently: Disabled (Enable-)
- 64-bit addressing supported
- Maskable interrupts supported

**Note**: Driver might benefit from enabling MSI with multiple vectors for better interrupt distribution (separate vectors for TX complete, RX complete, MCU events).

### PCIe Capabilities

**Max Payload**: 256 bytes
**Max Read Request**: 512 bytes
**Cache Line Size**: 64 bytes
**RCB (Read Completion Boundary)**: 64 bytes

**Extended Tags**: Enabled (allows larger transaction IDs)
**Relaxed Ordering**: Enabled (allows reordering for performance)
**No Snoop**: Enabled (bypass cache coherency for DMA)

### Power Management

**Current State**: D0 (fully powered)
**PME (Power Management Events)**: Disabled
**Supported States**: D0, D3hot, D3cold (D1/D2 not supported)

### Advanced Error Reporting

**Status**: No errors detected
**Correctable Errors**: AdvNonFatalErr+ (but masked)
**Uncorrectable Errors**: None

### Latency Tolerance Reporting

**Max Snoop Latency**: 1048576ns (1.048ms)
**Max No-Snoop Latency**: 1048576ns (1.048ms)

**LTR Active**: Enabled in DevCtl2
**Current Threshold**: 166912ns (L1.2 threshold)

## DMA Investigation Implications

### 1. L1 Power States May Block DMA

The driver currently only disables L0s via PCIE_MAC_PM register, but lspci shows:
- L1 ASPM is **ENABLED**
- L1.1 and L1.2 substates are **ENABLED**

**Hypothesis**: When the device enters L1.x states, DMA processing might be suspended. The chip may need explicit wake-up or L1 must be disabled entirely.

**Test**: Disable all ASPM states (L0s and L1) and L1 substates:
```bash
# Disable ASPM on MT7927
sudo setpci -s 01:00.0 CAP_EXP+10.w=0x0000
```

### 2. MSI vs Legacy Interrupts

Currently using legacy INTx interrupts (pin A). MSI might provide more reliable interrupt delivery for DMA completion events.

**Test**: Enable MSI in driver probe:
```c
pci_enable_msi(pdev);
```

### 3. Memory Mapping Size

BAR0 is 2MB, not 1MB. Verify driver maps full region:
```c
// Should be:
dev->bar0_size = pci_resource_len(pdev, 0);  // Will return 2MB
// Not hardcoded to 1MB
```

### 4. Read Completion Boundary

RCB is 64 bytes. DMA descriptor reads should align with this boundary for optimal performance.

### 5. No-Snoop Enabled

DMA transactions use No-Snoop (bypass cache coherency). Driver correctly uses `dma_alloc_coherent()` which handles this, but verify:
- Memory barriers after descriptor writes
- Explicit cache flushes if needed (though coherent DMA shouldn't need this)

## Recommendations for Next Investigation

### Priority 1: Disable L1 ASPM States

**Bash:**
```bash
# Method 1: Via setpci (runtime)
DEVICE=$(lspci -nn | grep 14c3:7927 | cut -d' ' -f1)
sudo setpci -s $DEVICE CAP_EXP+10.w=0x0000
```

**Fish:**
```fish
# Method 1: Via setpci (runtime)
set DEVICE (lspci -nn | grep 14c3:7927 | cut -d' ' -f1)
sudo setpci -s $DEVICE CAP_EXP+10.w=0x0000
```

**Other methods:**
```bash
# Method 2: Via kernel parameter (boot time)
# Add to kernel command line: pcie_aspm=off

# Method 3: Per-device in driver probe (C code)
pci_disable_link_state(pdev, PCIE_LINK_STATE_L0S | PCIE_LINK_STATE_L1);
```

### Priority 2: Check LTR (Latency Tolerance Reporting)

L1.2 uses LTR thresholds. The device may be entering L1.2 sleep when DMA should be active.

**In driver**: Adjust LTR settings or disable L1 substates explicitly.

### Priority 3: Verify BAR0 Mapping

Ensure driver maps full 2MB BAR0, not just 1MB:
```c
// In probe:
bar0_size = pci_resource_len(pdev, 0);
pr_info("BAR0 size: %llu bytes\n", (unsigned long long)bar0_size);
// Should print: "BAR0 size: 2097152 bytes" (2MB)
```

### Priority 4: Enable MSI

Test with MSI instead of legacy interrupts:
```c
ret = pci_enable_msi(pdev);
if (ret)
    pr_warn("MSI enable failed, using INTx\n");
```

## Hardware Register Locations Update

Based on 2MB BAR0 size, register map should be:

```
BAR0: 0x90600000 - 0x907FFFFF (2MB total)
  0x000000 - 0x0FFFFF: SRAM (1MB)
  0x100000 - 0x1FFFFF: Control registers (1MB)
    0x102000: WFDMA0_BASE
    0x102100: WFDMA0_RST
    0x102208: WFDMA0_GLO_CFG
    ...

BAR2: 0x90800000 - 0x90807FFF (32KB, read-only shadow)
```

**Note**: If DMA registers are actually in the second 1MB of BAR0 (0x100000+ offset), current driver code may be accessing wrong addresses!

## Device-Specific PCI Address

**Current system**: 01:00.0

**Previous documentation assumed**: 0a:00.0

**Bash:**
```bash
# Get device address dynamically
DEVICE=$(lspci -nn | grep 14c3:7927 | cut -d' ' -f1)
echo "MT7927 found at: $DEVICE"

# Use with commands
lspci -s $DEVICE -vvv
sudo setpci -s $DEVICE CAP_EXP+10.w=0x0000
echo 1 | sudo tee /sys/bus/pci/devices/0000:$DEVICE/remove
```

**Fish:**
```fish
# Get device address dynamically
set DEVICE (lspci -nn | grep 14c3:7927 | cut -d' ' -f1)
echo "MT7927 found at: $DEVICE"

# Use with commands
lspci -s $DEVICE -vvv
sudo setpci -s $DEVICE CAP_EXP+10.w=0x0000
echo 1 | sudo tee /sys/bus/pci/devices/0000:$DEVICE/remove
```

## Summary of Action Items

1. ✅ Document actual BAR0 size (2MB, not 1MB)
2. ⚠️ **Test disabling L1 ASPM and L1 substates** (high priority!)
3. ✅ Verify register offset calculations account for 2MB BAR0 - Validated via ring scan (2026-01-30)
4. ⚠️ Test MSI interrupt mode
5. ⚠️ Check if LTR thresholds interfere with DMA timing
6. 📝 Update all documentation with correct PCI address (01:00.0)
7. 📝 Update commands in Makefile and scripts

---

## Validation Log (2026-01-30)

### TX Ring Hardware Validation

**Methodology**: Created `diag/mt7927_ring_scan_readonly.c` to safely scan TX rings 0-17 by reading CNT registers.

**Results**:
- Rings 0-7: CNT=512 (0x200) - **VALID**
- Rings 8-17: CNT=0 - **INVALID** (do not exist)

**Conclusion**: MT7927 has exactly 8 TX rings (0-7), confirming our architecture assumption.

### ASPM L1 Effect on Register Access

**Test**: Ran ring scan with ASPM L1 enabled vs disabled (via `setpci -s $DEVICE CAP_EXP+10.w=0x0000`)

**Result**: Identical scan results in both cases.

**Conclusion**: ASPM L1 does NOT affect register reads. If L1 causes the DMA blocker, it's by interfering with DMA processing, not register access. Test with driver load after L1 disable is still pending.

### Chip ID Location

**Mystery**: `mt7927_diag.ko` reads Chip ID correctly, other modules read 0x00000000.

**Resolution**:
- BAR2 is read-only shadow of BAR0 starting at offset 0x10000
- Chip ID is at BAR0+0x10000, NOT BAR0+0x0000
- `mt7927_diag.c` uses BAR2, so BAR2+0x000 = Chip ID ✓
- Other modules using BAR0+0x0000 read SRAM content (0x00000000)
