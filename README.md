# MT7927 WiFi 7 Linux Driver

> **⚠️ PROJECT STATUS: STALLED / NON-FUNCTIONAL ⚠️**
>
> This driver development effort has reached an impasse. After 29+ phases of investigation, the driver **does not work** and WiFi functionality is **not available**.
>
> **If you need working MT7927 WiFi on Linux**, please check:
> - [Linux kernel mailing list](https://lore.kernel.org/linux-wireless/) for official upstream progress
> - Your distribution's kernel updates (official support may arrive in future kernels)
> - Windows or other operating systems where MT7927 has vendor support

---

## Current Status (Phase 29c)

### What Works

| Component | Status | Notes |
|-----------|--------|-------|
| PCI enumeration | ✅ Works | Device detected as `14c3:7927` |
| BAR0 memory mapping | ✅ Works | 2MB register space accessible |
| Chip ID read | ✅ Works | Returns `0x00511163` correctly |
| CB_INFRA initialization | ✅ Works | PCIe remap registers configured |
| WiFi subsystem reset | ✅ Works | WFSYS reset sequence completes |
| DMA ring allocation | ✅ Works | Descriptor rings allocated and configured |
| Ring base address config | ✅ Works | Hardware reads correct ring addresses |
| Firmware file loading | ✅ Works | Patch + RAM firmware parsed from disk |
| Firmware transfer (host side) | ✅ Works | DMA descriptors written, indices advanced |

### What Does NOT Work

| Component | Status | Issue |
|-----------|--------|-------|
| Firmware execution | ❌ FAILED | MCU never acknowledges firmware data |
| MCU status updates | ❌ FAILED | Stays at `0x00000000` after all regions |
| FW_N9_RDY flag | ❌ FAILED | Stuck at `0x00000002` (N9 not ready) |
| Mailbox communication | ❌ FAILED | All MCU commands timeout |
| WiFi interface | ❌ FAILED | No `wlan` interface created |
| Scanning/connecting | ❌ FAILED | Cannot scan or connect to networks |

### Root Cause (Unsolved)

**DMA data never reaches device memory.** The host correctly:
1. Allocates DMA buffers
2. Maps them via `dma_map_single()`
3. Writes descriptors to rings
4. Advances CPU index

But the device MCU never receives the data. The MCU status register stays at `0x00000000` through all firmware region transfers. IOMMU page faults have been observed in some runs.

**Hypotheses investigated but not resolved:**
- IOMMU domain mismatch between coherent and streaming DMA
- Missing MediaTek-specific DMA configuration
- Incorrect DMA address mask (32-bit vs 64-bit)
- Missing power/clock gating configuration

---

## Technical Findings

### Architecture Discovery

**MT7927 is an MT6639 variant**, not MT7925 as initially assumed. This was proven via MediaTek's official kernel modules which explicitly map PCI ID `0x7927` to MT6639 driver data.

Both chips are CONNAC3X family and share:
- Same firmware files (MT7925 firmware works)
- Same ring protocol (rings 15/16 for MCU/FWDL)
- Same register layout

### Key Technical Discoveries

1. **ROM bootloader doesn't support mailbox protocol** - Must use polling-based firmware loading
2. **WFDMA base is at 0xd4000**, not 0x2000 (MT7921 legacy address)
3. **CB_INFRA remap required** - Must set `0x74037001` at BAR0+0x1f6554 before DMA works
4. **Sparse ring layout** - Only rings 0,1,2,15,16 are used (not 0-16 like MT7925)

---

## Repository Structure

```
reference_zouyonghao_mt7927/   # Modified mt76 driver (builds, loads, but firmware fails)
├── mt76-outoftree/            # Out-of-tree mt76 fork with MT7927 additions
│   ├── mt7925/                # MT7925/MT7927 driver code
│   │   ├── pci.c              # PCI probe, initialization
│   │   ├── pci_mcu.c          # MCU initialization
│   │   └── ...
│   ├── mt7927_fw_load.c       # Polling-based firmware loader
│   └── ...

src/                           # Original custom driver attempt (abandoned)
tests/                         # Test modules for hardware exploration
diag/                          # Diagnostic modules
docs/                          # Detailed technical documentation
```

### Key Documentation

- **[DEVELOPMENT_LOG.md](DEVELOPMENT_LOG.md)** - Complete 29-phase investigation history
- **[docs/ZOUYONGHAO_ANALYSIS.md](docs/ZOUYONGHAO_ANALYSIS.md)** - DMA debugging analysis
- **[docs/MT6639_ANALYSIS.md](docs/MT6639_ANALYSIS.md)** - Architecture proof
- **[CLAUDE.md](CLAUDE.md)** - Technical context for AI-assisted development

---

## Building (For Research Only)

The driver builds and loads but **does not provide WiFi functionality**.

### Prerequisites

```bash
# Kernel 6.7+ required (tested on 6.18)
uname -r

# Verify MT7927 device present
lspci -nn | grep 14c3:7927

# Install firmware
sudo mkdir -p /lib/firmware/mediatek/mt7925
cd /lib/firmware/mediatek/mt7925
sudo wget https://git.kernel.org/pub/scm/linux/kernel/git/firmware/linux-firmware.git/plain/mediatek/mt7925/WIFI_MT7925_PATCH_MCU_1_1_hdr.bin
sudo wget https://git.kernel.org/pub/scm/linux/kernel/git/firmware/linux-firmware.git/plain/mediatek/mt7925/WIFI_RAM_CODE_MT7925_1_1.bin
```

### Build and Load

```bash
cd reference_zouyonghao_mt7927/mt76-outoftree
make clean && make

# Unload existing modules
sudo rmmod mt7925e mt7925_common mt792x_lib mt76_connac_lib mt76 2>/dev/null

# Load modules
sudo insmod mt76.ko
sudo insmod mt76-connac-lib.ko
sudo insmod mt792x-lib.ko
sudo insmod mt7925/mt7925-common.ko
sudo insmod mt7925/mt7925e.ko

# Check output (will show firmware loading attempts but MCU timeout errors)
sudo dmesg | tail -80
```

---

## Contributing

While this project is stalled, the documentation may be valuable for future efforts. Key blockers that need resolution:

1. **DMA path debugging** - Why doesn't firmware data reach device memory?
2. **IOMMU investigation** - Are there AMD-Vi/Intel VT-d specific issues?
3. **MediaTek documentation** - Official register documentation would help

If you have access to MediaTek internal documentation or working reference implementations, contributions are welcome.

---

## License

GPL v2

---

## Acknowledgments

- **zouyonghao** - Original MT7927 driver research ([github.com/zouyonghao/mt7927](https://github.com/zouyonghao/mt7927))
- **MediaTek** - Vendor kernel modules used for architecture analysis
- **Linux mt76 team** - Foundation driver framework

---

*Last updated: 2026-02-01 (Phase 29c)*
