# MT7927 WiFi 7 Linux Driver

Linux kernel driver for MediaTek MT7927 WiFi 7 (802.11be) chipset.

## Quick Start

### Prerequisites

```bash
# Kernel 6.7+ required
uname -r

# Verify MT7927 device present
lspci -nn | grep 14c3:7927
```

### Install Firmware

```bash
# Download MT7925 firmware (compatible with MT7927)
mkdir -p ~/mt7927_firmware && cd ~/mt7927_firmware

wget https://git.kernel.org/pub/scm/linux/kernel/git/firmware/linux-firmware.git/plain/mediatek/mt7925/WIFI_MT7925_PATCH_MCU_1_1_hdr.bin
wget https://git.kernel.org/pub/scm/linux/kernel/git/firmware/linux-firmware.git/plain/mediatek/mt7925/WIFI_RAM_CODE_MT7925_1_1.bin

# Install
sudo mkdir -p /lib/firmware/mediatek/mt7925
sudo cp *.bin /lib/firmware/mediatek/mt7925/
sudo update-initramfs -u
```

### Build and Test

```bash
# Build
make clean && make

# Test (choose one)
sudo insmod tests/05_dma_impl/test_fw_load.ko    # Recommended
sudo insmod src/mt7927.ko                         # Production driver
sudo insmod diag/mt7927_diag.ko                   # Diagnostics

# View output
sudo dmesg | tail -40
```

## Documentation

Complete documentation in `docs/` directory:

**Essential reading**:
- [docs/ZOUYONGHAO_ANALYSIS.md](docs/ZOUYONGHAO_ANALYSIS.md) - **ROOT CAUSE & SOLUTION** ⚠️
- [docs/MT6639_ANALYSIS.md](docs/MT6639_ANALYSIS.md) - Architecture proof
- [docs/ROADMAP.md](docs/ROADMAP.md) - Current status and plan
- [docs/CONTRIBUTING.md](docs/CONTRIBUTING.md) - How to contribute

**Additional resources**:
- [docs/README.md](docs/README.md) - Documentation index
- [docs/HARDWARE.md](docs/HARDWARE.md) - Hardware specifications
- [docs/TROUBLESHOOTING.md](docs/TROUBLESHOOTING.md) - Common issues
- [docs/FAQ.md](docs/FAQ.md) - Frequently asked questions
- [DEVELOPMENT_LOG.md](DEVELOPMENT_LOG.md) - Complete history (26 phases)

## Build System

```bash
make           # Build production driver (src/)
make tests     # Build test modules (tests/)
make diag      # Build diagnostic modules (diag/)
make clean     # Clean build artifacts
make check     # Check device state
make recover   # Recover from error state
```

## License

GPL v2 - Intended for upstream Linux kernel submission

---

For detailed information, troubleshooting, and contribution guidelines, see [docs/README.md](docs/README.md).
