# MediaTek MT7927 Proprietary Driver Analysis

This document provides a comprehensive analysis of the MediaTek proprietary driver implementation for the MT7927 802.11be wireless network adapter, based on the `reference_mtk_modules/` codebase.

## Table of Contents

1. [Overview](#overview)
2. [Device Identification](#device-identification)
3. [Driver Entry Points](#driver-entry-points)
4. [Key Data Structures](#key-data-structures)
5. [Initialization Sequence](#initialization-sequence)
6. [DMA and Ring Buffer Configuration](#dma-and-ring-buffer-configuration)
7. [Interrupt Handling](#interrupt-handling)
8. [Power Management](#power-management)
9. [Firmware Loading](#firmware-loading)
10. [Address Remapping](#address-remapping)
11. [Key Files for Reverse Engineering](#key-files-for-reverse-engineering)

---

## Overview

The MT7927 (Filogic 380) is a 802.11be (Wi-Fi 7) 320MHz 2x2 PCIe wireless network adapter. In MediaTek's proprietary driver, MT7927 shares the driver implementation with MT7925, using the CONNAC3x architecture.

**Key Finding:** MT7927 (Device ID 0x7927) maps to `mt66xx_driver_data_mt6639`, indicating it uses the MT6639 driver path internally, while sharing chip-specific code with MT7925.

### Hardware Specifications (from lspci)

| Property | Value |
|----------|-------|
| PCI Vendor ID | 0x14C3 (MediaTek) |
| PCI Device ID | 0x7927 |
| BAR0 | 2MB (non-prefetchable, 64-bit) |
| BAR2 | 32KB (non-prefetchable, 64-bit) |
| PCIe Link | Gen3 x1 (8GT/s) |
| MSI | Up to 32 vectors |
| ASPM | L0s, L1, L1.1, L1.2 supported |

---

## Device Identification

### PCI Device Table

**File:** `connectivity/wlan/core/gen4m/os/linux/hif/pcie/pcie.c`

```c
// Line 104-105
#define NIC7927_PCIe_DEVICE_ID 0x7927
#define NIC7925_PCIe_DEVICE_ID 0x7925

// Line 113-197: mtk_pci_ids[]
// MT7927 entry (lines 170-171):
{ PCI_DEVICE(MTK_PCI_VENDOR_ID, NIC7927_PCIe_DEVICE_ID),
  .driver_data = (kernel_ulong_t)&mt66xx_driver_data_mt6639 },

// MT7925 entry (lines 188-189):
{ PCI_DEVICE(MTK_PCI_VENDOR_ID, NIC7925_PCIe_DEVICE_ID),
  .driver_data = (kernel_ulong_t)&mt66xx_driver_data_mt7925 },
```

### Driver Data Structures

| Device | Driver Data | Chip Info |
|--------|-------------|-----------|
| MT7927 (0x7927) | `mt66xx_driver_data_mt6639` | Uses MT6639 path |
| MT7925 (0x7925) | `mt66xx_driver_data_mt7925` | `mt66xx_chip_info_mt7925` |

---

## Driver Entry Points

### PCIe Probe Function

**Function:** `mtk_pci_probe()`
**Location:** `pcie.c:1639-1746`

```c
static int mtk_pci_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
    // 1. Enable PCI device (line 1651)
    pcim_enable_device(pdev);

    // 2. Map I/O memory regions - BAR0 (lines 1658-1662)
    pcim_iomap_regions(pdev, BIT(0), pci_name(pdev));

    // 3. Set PCI master bit (line 1683)
    pci_set_master(pdev);

    // 4. Configure MSI interrupts (line 1685)
    mtk_pcie_setup_msi(pdev, prGlueInfo);

    // 5. Set 32-bit DMA mask (line 1689)
    dma_set_mask(&pdev->dev, DMA_BIT_MASK(32));

    // 6. Store CSR base address (lines 1698-1701)
    prChipInfo->pdev = pdev;
    prChipInfo->CSRBaseAddress = pcim_iomap_table(pdev)[0];

    // 7. Initialize EMI memory (line 1710)
    emi_mem_init(prAdapter);

    // 8. Call upper-layer probe (line 1713)
    pfWlanProbe(pdev, id, prChipInfo);

    // 9. Configure ASPM (line 1721)
    mtk_pci_setup_aspm(pdev);
}
```

### Module Registration

**Function:** `glRegisterBus()`
**Location:** `pcie.c:2041`

```c
// Driver structure registration
mtk_pci_driver.probe  = mtk_pci_probe;   // Line 2051
mtk_pci_driver.remove = mtk_pci_remove;  // Line 2052
mtk_pci_driver.suspend = mtk_pci_suspend; // Line 2054
mtk_pci_driver.resume  = mtk_pci_resume;  // Line 2055
```

### Remove Function

**Function:** `mtk_pci_remove()`
**Location:** `pcie.c:1748`

Operations:
1. Call `pfWlanRemove()` for upper-layer cleanup
2. Clean up EMI memory
3. Unmap I/O regions

---

## Key Data Structures

### Chip Info Structure

**Structure:** `mt66xx_chip_info`
**Instance:** `mt66xx_chip_info_mt7925`
**Location:** `chips/mt7925/mt7925.c:688-795`

```c
struct mt66xx_chip_info mt66xx_chip_info_mt7925 = {
    .chip_id = MT7925_CHIP_ID,           // 0x7925 (line 701)
    .bus_info = &mt7925_bus_info,        // Line 689
    .fw_dl_ops = &mt7925_fw_dl_ops,      // Line 691
    .fgIsSupportL0p5Reset = TRUE,        // Line 786
    .is_support_asic_lp = TRUE,          // Line 738
    // ... additional fields
};
```

### Bus Info Structure

**Structure:** `BUS_INFO`
**Instance:** `mt7925_bus_info`
**Location:** `chips/mt7925/mt7925.c:338-477`

| Field | Value | Purpose |
|-------|-------|---------|
| `top_cfg_base` | `MT7925_TOP_CFG_BASE` | Top configuration base |
| `host_dma0_base` | `WF_WFDMA_HOST_DMA0_BASE` | WFDMA host DMA0 base |
| `host_int_status_addr` | `WF_WFDMA_HOST_DMA0_HOST_INT_STA_ADDR` | Interrupt status register |
| `host_tx_ring_base` | `WF_WFDMA_HOST_DMA0_WPDMA_TX_RING0_CTRL0_ADDR` | TX ring base |
| `host_rx_ring_base` | `WF_WFDMA_HOST_DMA0_WPDMA_RX_RING0_CTRL0_ADDR` | RX ring base |
| `tx_ring_fwdl_idx` | 16 (`CONNAC3X_FWDL_TX_RING_IDX`) | Firmware download ring |
| `tx_ring_cmd_idx` | 15 | Command ring index |
| `rx_data_ring_num` | 2 | Number of RX data rings |
| `rx_evt_ring_num` | 2 | Number of RX event rings |
| `u4DmaMask` | 32 | 32-bit DMA addressing |

### HIF Info Structure

**Structure:** `GL_HIF_INFO`
**Location:** `os/linux/hif/pcie/include/hif.h:230-380`

```c
struct GL_HIF_INFO {
    struct pci_dev *pdev;                           // PCI device pointer
    struct RTMP_DMABUF TxDescRing[NUM_OF_TX_RING];  // TX descriptor rings
    struct RTMP_TX_RING TxRing[NUM_OF_TX_RING];     // TX ring structures
    struct RTMP_DMABUF RxDescRing[NUM_OF_RX_RING];  // RX descriptor rings
    struct RTMP_RX_RING RxRing[NUM_OF_RX_RING];     // RX ring structures
    uint32_t u4IntStatus;                           // Interrupt status
    struct HIF_MEM_OPS rMemOps;                     // Memory operation callbacks
};
```

---

## Initialization Sequence

### Phase 1: Hardware Bring-up (PCIe Probe)

```
mtk_pci_probe()
├── pcim_enable_device()
├── pcim_iomap_regions()        → Map BAR0 to kernel virtual address
├── pci_set_master()            → Set PCI master enable bit
├── mtk_pcie_setup_msi()        → MSI initialization (pcie_msi.c)
├── dma_set_mask(32-bit)
├── Store CSRBaseAddress
└── pfWlanProbe()               → Call upper probe (wlan_probe.c)
```

### Phase 2: WLAN Probe

```
wlanProbe()
├── Get chip_info from driver_data
├── Initialize adapter structures
├── asicConnac3xCapInit()       → Chip capability initialization
├── Setup DMA/WFDMA
├── Initialize firmware download operations
└── Enable WiFi functionality
```

### Phase 3: Firmware Download

```
Firmware Download
├── mt7925_ConstructFirmwarePrio()    → Build firmware image list
│   ├── Flavor-specific binary
│   ├── RAM code binary
│   └── Generic fallback
├── wlanDownloadPatch()               → Download patch file
├── wlanConnacFormatDownload()        → Download main firmware
│   ├── DMA format download
│   ├── Via MCU FW path
│   └── Verify download
└── wlanParseRamCodeReleaseManifest() → Parse firmware info
```

---

## DMA and Ring Buffer Configuration

### TX Ring Configuration

**Location:** `chips/mt7925/mt7925.c:245-274`

| Ring Index | Name | Purpose | Address |
|------------|------|---------|---------|
| 0 | P0T0 | AP DATA0 | `WF_WFDMA_HOST_DMA0_WPDMA_TX_RING0_CTRL0_ADDR` |
| 1 | P0T1 | AP DATA1 | TX_RING1 |
| 2 | P0T2 | AP DATA2 | TX_RING2 |
| 3 | P0T3 | AP DATA3 | TX_RING3 |
| 8-11 | P0T8-T11 | MD DATA | TX_RING8-11 |
| 14 | P0T14 | MD CMD | TX_RING14 |
| 15 | P0T15 | AP CMD | TX_RING15 |
| 16 | P0T16 | FWDL | TX_RING16 |

### RX Ring Configuration

| Ring Index | Name | Purpose | Descriptor Count |
|------------|------|---------|------------------|
| 0 | P0R0 | AP EVENT | 128 |
| 1 | P0R1 | AP TDONE (TX Done) | - |
| 2 | P0R2 | AP DATA0 | 3072 |
| 3 | P0R3 | AP DATA1 | 3072 |

### Ring Control Registers

```
TX Ring Control:
├── Base Address: host_tx_ring_base
├── CIDX (CPU Index): host_tx_ring_cidx_addr
├── DIDX (DMA Index): host_tx_ring_didx_addr
├── Count: host_tx_ring_cnt_addr
└── Ext Ctrl: host_tx_ring_ext_ctrl_base

RX Ring Control:
├── Base Address: host_rx_ring_base
├── CIDX: host_rx_ring_cidx_addr
├── DIDX: host_rx_ring_didx_addr
├── Count: host_rx_ring_cnt_addr
└── Ext Ctrl: host_rx_ring_ext_ctrl_base
```

### DMASHDL (DMA Scheduler) Configuration

**Functions:**
- `mt7925DmashdlInit()` - Initialize DMASHDL
- `mt7925UpdateDmashdlQuota()` - Update ring quotas

**Configuration:** `rMt7925DmashdlCfg` (mt7925.c:403)

---

## Interrupt Handling

### MSI Layout

**Location:** `chips/mt7925/mt7925.c:301-335`

```c
struct pcie_msi_layout mt7925_pcie_msi_layout[] = {
    // Entries 0-7: Main HIF interrupts (conn_hif_host_int)
    { .name = "AP_INT", .handler = mtk_pci_isr, .thread = mtk_pci_isr_thread },

    // Entry 16: Watchdog interrupt
    { .name = "wm_conn2ap_wdt_irq" },

    // Entry 17: JTAG detection
    { .name = "wf_mcu_jtag_det_eint" },

    // Entry 20: Software interrupt (AP_MISC_INT)
    { .name = "ccif_wf2ap_sw_irq", .handler = pcie_sw_int_top_handler },

    // ... remaining entries reserved
};
```

### Interrupt Status Registers

**Status Address:** `WF_WFDMA_HOST_DMA0_HOST_INT_STA_ADDR`

**TX Done Bits:**
| Bit | Ring | Purpose |
|-----|------|---------|
| tx_done_int_sts_0 | Ring 0 | Data ring 0 |
| tx_done_int_sts_1 | Ring 1 | Data ring 1 |
| tx_done_int_sts_2 | Ring 2 | Data ring 2 |
| tx_done_int_sts_3 | Ring 3 | Data ring 3 |
| tx_done_int_sts_15 | Ring 15 | CMD ring |
| tx_done_int_sts_16 | Ring 16 | FWDL ring |

**RX Done Bits:**
| Bit | Ring | Purpose |
|-----|------|---------|
| rx_done_int_sts_0 | Ring 0 | Event ring |
| rx_done_int_sts_1 | Ring 1 | TX done ring |
| rx_done_int_sts_2 | Ring 2 | Data ring 0 |
| rx_done_int_sts_3 | Ring 3 | Data ring 1 |

### Interrupt Handlers

| Function | Location | Purpose |
|----------|----------|---------|
| `mtk_pci_isr()` | pcie.c:696 | Top-level ISR |
| `mtk_pci_isr_thread()` | pcie.c:697 | Threaded handler |
| `pcie_sw_int_top_handler()` | pcie.c:702 | Software interrupt |
| `mt7925ReadIntStatus()` | mt7925.c:1255 | Read interrupt status |
| `mt7925ProcessTxInterrupt()` | mt7925.c:1124 | TX completion |
| `mt7925ProcessRxInterrupt()` | mt7925.c:1181 | RX data |

### Interrupt Configuration

**Function:** `mt7925InitPcieInt()` (Line 1500)

Operations:
1. Set `PCIE_MAC_IREG_IMASK_HOST_INT_REQUEST_EN`
2. Set `PCIE_MAC_IREG_IMASK_HOST_P_ATR_EVT_EN`

**MSI Ring Mapping:** `mt7925WpdmaMsiConfig()` (Line 1371)
- Configures `WF_WFDMA_EXT_WRAP_CSR_MSI_INT_CFG0-3` registers

---

## Power Management

### Low Power Ownership Control

The driver uses an ownership model to coordinate access between the host driver and the on-chip firmware.

**Control Register:** `CONNAC3X_BN0_LPCTL_ADDR`

| Function | Location | Purpose |
|----------|----------|---------|
| `asicConnac3xLowPowerOwnRead()` | cmm_asic_connac3x.c:1042 | Check current owner |
| `asicConnac3xLowPowerOwnSet()` | cmm_asic_connac3x.c:1064 | Acquire host ownership |
| `asicConnac3xLowPowerOwnClear()` | cmm_asic_connac3x.c:1088 | Release to firmware |

### Ownership States

| State | Value | Description |
|-------|-------|-------------|
| Driver Own | 0x4 | Host driver has exclusive access |
| FW Own | 0x0 | Firmware owns, can enter low power |

### Set Host Ownership (Acquire)

```c
asicConnac3xLowPowerOwnSet() {
    // Write PCIE_LPCR_HOST_SET_OWN to CONNAC3X_BN0_LPCTL_ADDR
    // Call hwControlVote() if available
    // Purpose: Acquire exclusive access to hardware
}
```

### Clear Host Ownership (Release)

```c
asicConnac3xLowPowerOwnClear() {
    // 1. Call hwControlVote(FALSE) if supported
    // 2. Optional: Call setCrypto() callback
    // 3. Write PCIE_LPCR_HOST_CLR_OWN to CONNAC3X_BN0_LPCTL_ADDR
    // Purpose: Release hardware back to FW for low-power
}
```

### ASPM Configuration

**Function:** `mt7925ConfigPcieAspm()` (Line 119)

Supported states:
- PCIe ASPM L1
- PCIe ASPM L1.2

### Suspend/Resume Flow

**Suspend:**
```
├── Pre-suspend command (halPciePreSuspendDone)
├── Stop WFDMA
├── Release host ownership to FW
└── Save state
```

**Resume:**
```
├── Restore hardware state
├── Acquire host ownership
├── Enable interrupts
└── Resume WFDMA
```

### WFSYS Reset

**Functions:**
- `mt7925HalCbInfraRguWfRst()` - WFSYS reset via INFRA RGU
- `mt7925HalPollWfsysSwInitDone()` - Poll WFSYS init completion

Used during chip reset/L0.5 reset scenarios.

---

## Firmware Loading

### Firmware Download Operations

**Structure:** `FWDL_OPS_T mt7925_fw_dl_ops`
**Location:** `chips/mt7925/mt7925.c:480-509`

| Function Pointer | Implementation | Purpose |
|------------------|----------------|---------|
| `constructFirmwarePrio` | `mt7925_ConstructFirmwarePrio` | Build firmware list |
| `constructPatchName` | `mt7925_ConstructPatchName` | Build patch filename |
| `downloadPatch` | `wlanDownloadPatch` | Download patch |
| `downloadFirmware` | `wlanConnacFormatDownload` | Download main FW |
| `getFwInfo` | `wlanGetConnacFwInfo` | Get FW info |
| `getFwDlInfo` | `asicGetFwDlInfo` | Get download info |

### Firmware Priority Order

**Function:** `mt7925_ConstructFirmwarePrio()` (mt7925.c:909-980)

```
Priority order:
1. mt7925_wifi.bin              (single binary, if CFG_SUPPORT_SINGLE_FW_BINARY)
2. mt7925_wifi_[flavor].bin     (flavor variant)
3. WIFI_RAM_CODE_MT7925_[flavor]_[version].bin (flavor-specific)
4. WIFI_RAM_CODE_7925.bin       (generic fallback)
5. Entry from apucmt7925FwName table
```

### Firmware Download Path

**Main Function:** `wlanConnacFormatDownload()` (fw_dl.c:1920)

```
1. Check firmware availability
2. Download patches (if any)
3. DMA format download to MCU memory:
   - Start address: MT7925_PATCH_START_ADDR (line 712)
   - Method: Connac format via WFDMA
4. Trigger MCU execution
5. Verify download via manifest
```

### Flavor Support

**Function:** `mt7925GetFlavorVer()` (Line 68)

Flavor variants support different PHY configurations. The flavor string is embedded in firmware filenames.

---

## Address Remapping

### Bus-to-Chip Address Mapping

**Location:** `chips/mt7925/mt7925.c:163-219`

| Chip Address | Bus Offset | Size | Purpose |
|--------------|------------|------|---------|
| 0x830c0000 | 0x00000 | 0x1000 | WF_MCU_BUS_CR_REMAP |
| 0x54000000 | 0x02000 | 0x1000 | WFDMA PCIE0 MCU DMA0 |
| 0x55000000 | 0x03000 | 0x1000 | WFDMA PCIE0 MCU DMA1 |
| 0x57000000 | 0x05000 | 0x1000 | WFDMA MCU wrap CR |
| 0x820c0000 | 0x08000 | 0x4000 | WF_UMAC_TOP (PLE) |
| 0x820c8000 | 0x0c000 | 0x2000 | WF_UMAC_TOP (PSE) |
| 0x74030000 | 0x10000 | 0x1000 | PCIe MAC |
| 0x820e0000 | 0x20000 | 0x0400 | WF_LMAC_TOP BN0 (WF_CFG) |
| 0x820e1000 | 0x20400 | 0x0200 | WF_LMAC_TOP BN0 (WF_TRB) |
| 0x40000000 | 0x70000 | 0x10000 | WF_UMAC_SYSRAM |
| 0x00400000 | 0x80000 | 0x10000 | WF_MCU_SYSRAM |
| 0x00410000 | 0x90000 | 0x10000 | WF_MCU_SYSRAM (cfg) |
| 0x7c500000 | MT7925_PCIE2AP_REMAP_BASE | 0x2000000 | PCIE2AP Remap |

### Remapping Structures

**Location:** `chips/mt7925/mt7925.c:223-243`

```c
struct pcie2ap_remap mt7925_pcie2ap_remap = {
    .reg_base = /* PCIE2AP remapping register base */,
    .reg_mask = /* Mask for remapping field */,
    .reg_shift = /* Shift value */,
    .base_addr = MT7925_PCIE2AP_REMAP_BASE_ADDR,
};

struct ap2wf_remap mt7925_ap2wf_remap = {
    .reg_base = /* AP2WF remapping register */,
    .reg_mask = /* Mask */,
    .reg_shift = /* Shift */,
    .base_addr = MT7925_REMAP_BASE_ADDR,
};
```

### Important Registers

| Register | Address | Purpose |
|----------|---------|---------|
| LPCTL | `CONNAC3X_BN0_LPCTL_ADDR` | Power ownership control |
| IRQ_ENA | `CONNAC3X_BN0_IRQ_ENA_ADDR` | Interrupt enable |
| INT_STA | `WF_WFDMA_HOST_DMA0_HOST_INT_STA_ADDR` | Interrupt status |
| WPDMA_GLO_CFG | `CONNAC3X_WPDMA_GLO_CFG_ADDR` | WFDMA global config |
| PCIE_MAC_INT | `PCIE_MAC_IREG_IMASK_HOST_ADDR` | PCIe MAC interrupt mask |
| MSI_INT_CFG | `WF_WFDMA_EXT_WRAP_CSR_MSI_INT_CFG0-3` | MSI configuration |

---

## PSE/PLE Queue Management

### PSE Groups

**Location:** `chips/mt7925/mt7925.c:276-297`

| Group | Purpose |
|-------|---------|
| HIF0 | TX data |
| HIF1 | Talos CMD |
| CPU | I/O r/w |
| PLE | Host report |
| PLE1 | SPL report |
| LMAC0 | RX data |
| LMAC1 | RX_VEC |
| LMAC2 | TXS |
| LMAC3 | TXCMD/RXRPT |
| MDP | Media Data Processor |

Each group has:
- Group status register (`PG_*_GROUP_ADDR`)
- Page info register (`PG_*_PG_INFO_ADDR`)

---

## Key Files for Reverse Engineering

### Priority 1: Core Driver Files

| File | Purpose |
|------|---------|
| `os/linux/hif/pcie/pcie.c` | PCIe HIF layer, probe/remove, interrupts |
| `chips/mt7925/mt7925.c` | MT7925-specific operations, ring config |
| `os/linux/hif/pcie/include/hif.h` | HIF data structures |

### Priority 2: Common Infrastructure

| File | Purpose |
|------|---------|
| `common/cmm_asic_connac3x.c` | CONNAC3x operations, power, DMA |
| `common/fw_dl.c` | Firmware download mechanism |
| `os/linux/hif/pcie/pcie_msi.c` | MSI interrupt setup |

### Priority 3: Chip-Specific Extensions

| File | Purpose |
|------|---------|
| `chips/mt7925/hal_dmashdl_mt7925.c` | DMA scheduling |
| `chips/mt7925/hal_wfsys_reset_mt7925.c` | WiFi system reset |
| `chips/mt7925/dbg_mt7925.c` | Debug support |

---

## Critical Constants

```c
// Chip and Device IDs
MT7925_CHIP_ID           = 0x7925
MT7927_PCIe_DEVICE_ID    = 0x7927
MTK_PCI_VENDOR_ID        = 0x14C3

// DMA Configuration
DMA_MASK                 = 32  // 32-bit addressing

// Ring Configuration
NUM_TX_RINGS             = 17  // Rings 0-16
NUM_RX_RINGS             = 4   // Rings 0-3
RX_DATA_RING_SIZE        = 3072  // Descriptors
RX_EVENT_RING_SIZE       = 128   // Descriptors
TX_RING_FWDL_IDX         = 16  // CONNAC3X_FWDL_TX_RING_IDX
TX_RING_CMD_IDX          = 15
TX_RING_DATA_START       = 0
```

---

## Reverse Engineering Entry Points

### Recommended Starting Points

1. **PCIe Probe Entry:** `mtk_pci_probe()` (pcie.c:1639)
   - Traces hardware initialization
   - Maps memory regions
   - Finds CSR base address

2. **Chip-Specific Init:** `asicConnac3xCapInit()` (cmm_asic_connac3x.c:115)
   - Sets up TX/RX port configurations
   - Initializes interrupt routing
   - Configures DMASHDL

3. **Firmware Download:** `wlanConnacFormatDownload()` (fw_dl.c:1920)
   - DMA transfer mechanism
   - Firmware format parsing
   - MCU synchronization

4. **Interrupt Handling:** `mtk_pci_isr()` (pcie.c:696)
   - ISR routing based on MSI
   - Status bit processing
   - Ring-specific handlers

5. **Power Management:** `asicConnac3xLowPowerOwnClear()` (cmm_asic_connac3x.c:1088)
   - Power transition sequence
   - Register state synchronization

---

## References

- Source: `reference_mtk_modules/connectivity/wlan/core/gen4m/`
- Upstream mt76: `reference_mt76_upstream/mt7925/`
- Linux kernel: `reference_rothko_kernel/drivers/net/wireless/mediatek/mt76/`
