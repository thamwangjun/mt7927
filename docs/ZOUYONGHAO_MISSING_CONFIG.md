# Missing Configuration Steps from Zouyonghao Driver

This document analyzes the zouyonghao MT7927 driver and identifies configuration steps that may be missing from our test_fw_load.c. These steps could explain why DMA transfers fail with MEM_ERR=1 and DIDX never advances.

## Executive Summary

Our test_fw_load.c has most of the core initialization but is **missing several MT7927-specific configuration steps** that zouyonghao performs:

| Priority | Missing Step | Zouyonghao Location | Likely Impact |
|----------|-------------|---------------------|---------------|
| **HIGH** | PCIe MAC interrupt routing (0x010074) | pci.c:610 | May affect DMA completion signaling |
| **HIGH** | PCIE2AP remap timing | pci_mcu.c:148 | Set AFTER DMA init, not before |
| **MEDIUM** | MSI interrupt configuration | pci_mcu.c:165-175 | Even in legacy mode, rings need mapping |
| **MEDIUM** | GLO_CFG_EXT1 full value | pci_mcu.c:182-183 | We only set BIT(28), zouyonghao sets 0x8C800404 |
| **LOW** | RX ring thresholds | pci_mcu.c:194-198 | Pause thresholds for flow control |
| **LOW** | Delay interrupt config | pci_mcu.c:201-206 | May affect interrupt coalescing |

---

## Detailed Analysis

### 1. PCIe MAC Interrupt Routing (HIGH PRIORITY)

**Location**: `pci.c:608-612`

**Zouyonghao does**:
```c
if (pdev->device == 0x7927) {
    mt76_wr(dev, MT7927_PCIE_MAC_INT_CONFIG_ADDR, MT7927_PCIE_MAC_INT_CONFIG_VALUE);
}
```

**Values**:
- `MT7927_PCIE_MAC_INT_CONFIG_ADDR` = `0x010074` (BAR0 offset, from fixed_map 0x74030000 → 0x010000)
- `MT7927_PCIE_MAC_INT_CONFIG_VALUE` = `0x08021000`

**Timing**: Set AFTER IRQ is registered, BEFORE DMA init

**Our test module**: Does NOT set this register

**Potential impact**: This configures how DMA completion interrupts are routed through the PCIe MAC. Without this, the DMA engine might work but can't signal completion properly.

---

### 2. PCIE2AP Remap Timing (HIGH PRIORITY)

**Location**: `pci_mcu.c:147-150`

**Zouyonghao does**:
```c
mt76_wr(dev, CONN_BUS_CR_VON_CONN_INFRA_PCIE2AP_REMAP_WF_1_BA_ADDR, 0x18051803);
```

**Our test module**: We set this, but timing may differ

**Key insight**: Zouyonghao sets this in `mt7927e_mcu_init()` which is called AFTER:
1. PCI probe
2. CB_INFRA remap
3. WF reset
4. IRQ registration
5. DMA init (queues allocated)

**Our timing**: We might be setting it too early (during power-on sequence)

**Potential impact**: If set before DMA hardware is ready, the remap might not take effect properly.

---

### 3. MSI Interrupt Configuration (MEDIUM PRIORITY)

**Location**: `pci_mcu.c:159-176`

**Zouyonghao does**:
```c
/* Configure MSI number - single MSI mode */
msi_val = (MT7927_MSI_NUM_SINGLE << WF_WFDMA_EXT_WRAP_CSR_WFDMA_HOST_CONFIG_PCIE0_MSI_NUM_SHFT);
mt76_wr(dev, WF_WFDMA_EXT_WRAP_CSR_WFDMA_HOST_CONFIG_ADDR, msi_val);

/* Configure MSI ring mappings */
mt76_wr(dev, WF_WFDMA_EXT_WRAP_CSR_MSI_INT_CFG0_ADDR, 0x00660077);
mt76_wr(dev, WF_WFDMA_EXT_WRAP_CSR_MSI_INT_CFG1_ADDR, 0x00001100);
mt76_wr(dev, WF_WFDMA_EXT_WRAP_CSR_MSI_INT_CFG2_ADDR, 0x0030004F);
mt76_wr(dev, WF_WFDMA_EXT_WRAP_CSR_MSI_INT_CFG3_ADDR, 0x00542200);
```

**Register addresses** (from mt7927_regs.h):
- `WFDMA_HOST_CONFIG` = `0x7C027030` → BAR0 `0x0d7030`
- `MSI_INT_CFG0` = `0x7C0270F0` → BAR0 `0x0d70F0`
- `MSI_INT_CFG1` = `0x7C0270F4` → BAR0 `0x0d70F4`
- `MSI_INT_CFG2` = `0x7C0270F8` → BAR0 `0x0d70F8`
- `MSI_INT_CFG3` = `0x7C0270FC` → BAR0 `0x0d70FC`

**Our test module**: Does NOT configure MSI registers

**Potential impact**: Even in legacy INTx mode, these registers may configure how DMA rings map to interrupt sources. Without this, the hardware might not know which interrupt to raise when a ring completes.

---

### 4. WPDMA_GLO_CFG_EXT1 Full Configuration (MEDIUM PRIORITY)

**Location**: `pci_mcu.c:181-183`

**Zouyonghao does**:
```c
cfg_val = MT7927_WPDMA_GLO_CFG_EXT1_VALUE | MT7927_WPDMA_GLO_CFG_EXT1_TX_FCTRL;
mt76_wr(dev, WF_WFDMA_HOST_DMA0_WPDMA_GLO_CFG_EXT1_ADDR, cfg_val);
```

**Values**:
- `MT7927_WPDMA_GLO_CFG_EXT1_VALUE` = `0x8C800404`
- `MT7927_WPDMA_GLO_CFG_EXT1_TX_FCTRL` = `BIT(31)` = `0x80000000`
- **Full value**: `0x8C800404 | 0x80000000` = `0x8C800404` (BIT(31) already included)

**Our test module**: We only set `BIT(28)` via `|=` operation

**The 0x8C800404 value breakdown** (bits from coda headers):
- BIT(31): TX flow control mode enable
- BIT(27): Unknown, set in config
- BIT(23): Unknown, set in config
- BIT(10): Packet count threshold related
- BIT(2): Related to prefetch

**Potential impact**: TX flow control may not work correctly. The DMA engine might stall waiting for flow control signals that never come.

---

### 5. WPDMA_GLO_CFG_EXT2 Configuration (MEDIUM PRIORITY)

**Location**: `pci_mcu.c:185-187`

**Zouyonghao does**:
```c
mt76_wr(dev, WF_WFDMA_HOST_DMA0_WPDMA_GLO_CFG_EXT2_ADDR, MT7927_WPDMA_GLO_CFG_EXT2_VALUE);
```

**Values**:
- Address: `0x7C0242B8` → BAR0 `0x0d42B8`
- Value: `0x44`

**Our test module**: Does NOT set this register

**Potential impact**: Performance monitoring or additional DMA configuration bits.

---

### 6. RX Ring Pause Thresholds (LOW PRIORITY)

**Location**: `pci_mcu.c:193-198`

**Zouyonghao does**:
```c
for (addr = WF_WFDMA_HOST_DMA0_WPDMA_PAUSE_RX_Q_TH10_ADDR;
     addr <= WF_WFDMA_HOST_DMA0_WPDMA_PAUSE_RX_Q_TH1110_ADDR;
     addr += 0x4) {
    mt76_wr(dev, addr, MT7927_RX_RING_THRESHOLD_DEFAULT);  // 0x22
}
```

**Address range**: `0x0d4260` to `0x0d4274` (6 registers, one for each RX ring pair)

**Our test module**: Does NOT set these registers

**Potential impact**: RX flow control pause thresholds. Probably not relevant for TX-only firmware loading.

---

### 7. HIF Performance Monitor (LOW PRIORITY)

**Location**: `pci_mcu.c:189-191`

**Zouyonghao does**:
```c
mt76_wr(dev, WF_WFDMA_EXT_WRAP_CSR_WFDMA_HIF_PERF_MAVG_DIV_ADDR, MT7927_WFDMA_HIF_PERF_MAVG_DIV_VALUE);
```

**Values**:
- Address: `0x7C0270C0` → BAR0 `0x0d70C0`
- Value: `0x36`

**Our test module**: Does NOT set this register

**Potential impact**: Performance monitoring divisor. Unlikely to affect basic DMA operation.

---

### 8. Delay Interrupt Configuration (LOW PRIORITY)

**Location**: `pci_mcu.c:200-206`

**Zouyonghao does**:
```c
mt76_wr(dev, WF_WFDMA_HOST_DMA0_HOST_PER_DLY_INT_CFG_ADDR, MT7927_PER_DLY_INT_CFG_VALUE);
mt76_wr(dev, WF_WFDMA_EXT_WRAP_CSR_WFDMA_DLY_IDX_CFG_0_ADDR, MT7927_DLY_IDX_CFG_RING4_7_VALUE);
```

**Values**:
- Per-delay int: Address `0x0d42E8`, Value `0xF00008`
- Ring 4-7 delay: Address `0x0d70E8`, Value `0x40654065`

**Our test module**: Does NOT set these registers

**Potential impact**: Interrupt coalescing. Unlikely to affect basic DMA operation.

---

## Recommended Fix Order

### Phase 1: High Priority (Try These First)

1. **Add PCIe MAC interrupt routing**:
```c
#define MT_PCIE_MAC_INT_CONFIG     0x010074
#define MT_PCIE_MAC_INT_CONFIG_VALUE   0x08021000

/* Add after IRQ setup, before DMA init */
mt_wr(dev, MT_PCIE_MAC_INT_CONFIG, MT_PCIE_MAC_INT_CONFIG_VALUE);
```

2. **Move PCIE2AP remap to after DMA init** (timing fix)

### Phase 2: Medium Priority (If Phase 1 Doesn't Work)

3. **Add MSI interrupt configuration**:
```c
#define MT_WFDMA_HOST_CONFIG   0x0d7030
#define MT_WFDMA_MSI_CFG0      0x0d70F0
#define MT_WFDMA_MSI_CFG1      0x0d70F4
#define MT_WFDMA_MSI_CFG2      0x0d70F8
#define MT_WFDMA_MSI_CFG3      0x0d70FC

mt_wr(dev, MT_WFDMA_HOST_CONFIG, 0);          /* Single MSI */
mt_wr(dev, MT_WFDMA_MSI_CFG0, 0x00660077);
mt_wr(dev, MT_WFDMA_MSI_CFG1, 0x00001100);
mt_wr(dev, MT_WFDMA_MSI_CFG2, 0x0030004F);
mt_wr(dev, MT_WFDMA_MSI_CFG3, 0x00542200);
```

4. **Set full GLO_CFG_EXT1 value**:
```c
mt_wr(dev, MT_WFDMA0_GLO_CFG_EXT1, 0x8C800404);  /* Full config, not just BIT(28) */
```

5. **Set GLO_CFG_EXT2**:
```c
mt_wr(dev, 0x0d42B8, 0x44);
```

### Phase 3: Low Priority (Cleanup)

6. Add RX thresholds, delay configs, etc.

---

## Initialization Order Comparison

### Zouyonghao Order (from pci.c and pci_mcu.c):

1. PCI enable, BAR mapping
2. Install custom bus_ops (remap functions)
3. **CB_INFRA PCIe remap** (0x74037001)
4. Power control (fw_pmctrl, drv_pmctrl)
5. Get chip revision
6. **WF subsystem reset** (GPIO mode, BT reset, WF reset)
7. IRQ registration
8. **PCIe MAC interrupt config** (0x08021000) ← HIGH PRIORITY MISSING
9. DMA init (queue allocation, prefetch, GLO_CFG)
10. Register device
11. **MCU init** (called from mt7925_register_device):
    - **PCIE2AP remap** (0x18051803) ← TIMING DIFFERENCE
    - **MSI configuration** ← MEDIUM PRIORITY MISSING
    - **GLO_CFG_EXT1/EXT2** ← MEDIUM PRIORITY INCOMPLETE
    - Other extension configs ← LOW PRIORITY MISSING
12. Firmware load

### Our Order (test_fw_load.c):

1. PCI enable, BAR mapping
2. Power control
3. **CB_INFRA PCIe remap**
4. **PCIE2AP remap** ← TOO EARLY?
5. WF subsystem reset
6. Wait for MCU IDLE
7. DMA setup (GLO_CFG, ring config, prefetch)
8. Send firmware

---

## Additional Notes

### Why PCIe MAC Interrupt Config Might Matter

The PCIe MAC sits between the WFDMA and the PCIe bus. Its interrupt routing configuration (0x08021000) likely controls:
- Which DMA events generate PCIe interrupts
- Interrupt assertion/deassertion timing
- Possibly DMA completion signaling back to the WFDMA engine

Without this, the WFDMA might complete a transfer but:
1. Not know how to signal completion (DIDX doesn't advance)
2. Get stuck waiting for an acknowledgment that never comes
3. Report memory errors because the completion path is broken

### Why MSI Config Might Matter Even in Legacy Mode

Even if using legacy INTx interrupts, the MSI_INT_CFG registers might:
- Configure internal ring-to-interrupt mapping
- Enable interrupt sources for specific rings
- Control interrupt aggregation behavior

Ring 16 (FWDL) might need specific configuration in these registers to work.

---

## Files Referenced

- `reference_zouyonghao_mt7927/mt76-outoftree/mt7925/pci.c` (lines 608-612, 752-754)
- `reference_zouyonghao_mt7927/mt76-outoftree/mt7925/pci_mcu.c` (lines 130-237)
- `reference_zouyonghao_mt7927/mt76-outoftree/mt7925/mt7927_regs.h` (register definitions)
- `reference_zouyonghao_mt7927/mt76-outoftree/mt792x_dma.c` (DMA enable sequence)
- `tests/05_dma_impl/test_fw_load.c` (our implementation)

