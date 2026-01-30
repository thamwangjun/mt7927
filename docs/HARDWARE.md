# MT7927 Hardware Specifications

## Hardware Information

- **Chip**: MediaTek MT7927 WiFi 7 (802.11be) - also known as "Filogic 380"
- **Full Name**: MT7927 802.11be 320MHz 2x2 PCIe Wireless Network Adapter
- **PCI ID**: 14c3:7927 (vendor: MediaTek, device: MT7927)
- **PCI Location**: Varies by system (example: 01:00.0) - check with `lspci -nn | grep 14c3:7927`
- **Architecture**: **MT6639 variant** (CONNAC3X family) - shares firmware with MT7925
- **Ring Protocol**: CONNAC3X standard (Ring 15: MCU CMD, Ring 16: FWDL)
- **DMA Engine**: Single WFDMA0 (no WFDMA1), sparse ring layout (0,1,2,15,16)

## Memory Map

- **BAR0**: **2MB** total (confirmed via lspci)
  - 0x000000 - 0x0FFFFF: SRAM (chip internal memory)
  - 0x100000 - 0x1FFFFF: Control registers
- **BAR2**: 32KB (read-only shadow of BAR0+0x10000)

## Hardware Configuration (from lspci)

| Property | Value |
|----------|-------|
| Chip ID | **0x00511163** (confirmed 2026-01-31) |
| HW Revision | 0x11885162 |
| PCI Location | 01:00.0 (varies by system) |
| BAR0 | 2MB @ 0x90600000 (64-bit, non-prefetchable) |
| BAR2 | 32KB @ 0x90800000 (64-bit, non-prefetchable) |
| PCIe Link | Gen3 x1 (8GT/s) |
| IRQ | 48 (pin A, legacy INTx mode) |
| MSI | 1/32 vectors available (currently disabled) |
| ASPM L0s | Disabled (by driver) |
| ASPM L1 | Enabled (normal - not a blocker) |
| L1.1 Substate | Enabled (normal) |
| L1.2 Substate | Enabled (normal) |

## Key Register Locations (BAR0 offsets)

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

## Architecture Comparison

| Property | MT6639 (Parent) | MT7925 (Sibling) | MT7927 (This chip) | Validation Status |
|----------|-----------------|------------------|---------------------|-------------------|
| Architecture | CONNAC3X | CONNAC3X | CONNAC3X | **✓ CONFIRMED** |
| TX Ring Count | Sparse (0,1,2,15,16) | 17 (0-16) | 8 physical, sparse layout | **✓ CONFIRMED** |
| FWDL Queue | Ring 16 | Ring 16 | Ring 16 | **✓ CONFIRMED** (2026-01-31) |
| MCU Queue | Ring 15 | Ring 15 | Ring 15 | **✓ CONFIRMED** (2026-01-31) |
| Firmware | MT7925 compatible | Native | MT7925 files | **✓ PROVEN** (Windows analysis) |
| WFDMA1 | TBD | Present | **NOT present** | **✓ CONFIRMED** |
| BAR0 Size | TBD | Unknown | **2MB** (lspci confirmed) | **✓ CONFIRMED** |

## Key Discoveries

1. **MT7927 is MT6639 variant** - MediaTek kernel modules prove architectural relationship
2. **CONNAC3X family** - MT6639 and MT7925 share firmware via common CONNAC3X architecture
3. **Ring protocol validated** - Uses CONNAC3X standard: Ring 15 (MCU_WM), Ring 16 (FWDL)
4. **Sparse ring layout** - 8 physical rings with sparse numbering (0,1,2,15,16)
5. **Single DMA engine** - Only WFDMA0 present (no WFDMA1)

## Firmware Files

Uses MT7925 firmware (confirmed compatible):
```
/lib/firmware/mediatek/mt7925/
├── WIFI_MT7925_PATCH_MCU_1_1_hdr.bin  # MCU patch
└── WIFI_RAM_CODE_MT7925_1_1.bin        # RAM code (1.4MB)
```

Download from: https://git.kernel.org/pub/scm/linux/kernel/git/firmware/linux-firmware.git/tree/mediatek/mt7925

## See Also

- **[MT6639_ANALYSIS.md](MT6639_ANALYSIS.md)** - Proves MT7927 is MT6639 variant
- **[FIRMWARE_ANALYSIS.md](FIRMWARE_ANALYSIS.md)** - Firmware compatibility analysis
- **[WINDOWS_FIRMWARE_ANALYSIS.md](WINDOWS_FIRMWARE_ANALYSIS.md)** - Windows driver firmware sharing proof
