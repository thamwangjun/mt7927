# MT7927 WiFi 7 Linux Driver

Linux kernel driver for MediaTek MT7927 WiFi 7 (802.11be) chipset.

## Status: ROOT CAUSE FOUND! 🎯

**Discovery (2026-01-31)**: MT7927 ROM bootloader does NOT support mailbox command protocol!

Our driver has been waiting for mailbox responses that the ROM will never send. DMA hardware works correctly - we're using the wrong communication protocol. Solution validated by working zouyonghao reference driver.

**Next step**: Implement polling-based firmware loader (no mailbox waits).

See **[docs/ZOUYONGHAO_ANALYSIS.md](docs/ZOUYONGHAO_ANALYSIS.md)** for complete root cause analysis and implementation guide.

## Key Discoveries

1. **MT7927 is MT6639 variant** - Proven via MediaTek kernel modules (NOT MT7925!)
2. **CONNAC3X family** - Shares firmware with MT7925 via common architecture
3. **Root cause identified** - ROM bootloader doesn't implement mailbox protocol
4. **Solution validated** - Polling-based firmware loading works (zouyonghao driver)

## Quick Start

### Prerequisites

```bash
# Check kernel version (need 6.7+)
uname -r

# Verify MT7927 device
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
# Clone and build
git clone https://github.com/[your-username]/mt7927-linux-driver
cd mt7927-linux-driver
make clean && make

# Option 1: Test module (recommended for development)
sudo insmod tests/05_dma_impl/test_fw_load.ko
sudo dmesg | tail -40

# Option 2: Production driver
sudo insmod src/mt7927.ko
sudo dmesg | tail -40

# Option 3: Diagnostic module
sudo insmod diag/mt7927_diag.ko
sudo dmesg | tail -20
```

### Expected Output (Current State)

Before mailbox fix:
```
[   10.123] mt7927: Chip ID: 0x00511163
[   10.124] mt7927: BAR0 mapped, size=1048576
[   10.125] mt7927: Power management: Host claimed DMA
[   10.126] mt7927: WFSYS reset complete
[   10.127] mt7927: DMA rings allocated
[   10.128] mt7927: MCU ready (status=0x00000001)
[   10.129] mt7927: Sending patch semaphore command...
[   15.130] mt7927: ERROR: MCU command timeout  ← Expected blocker
```

After mailbox fix (expected):
```
[   10.129] mt7927: Loading patch (polling mode)
[   10.145] mt7927: Patch sent successfully
[   10.146] mt7927: Loading RAM firmware
[   10.230] mt7927: Firmware loaded successfully
[   10.231] mt7927: Network interface created: wlan0
```

## Project Structure

```
mt7927-linux-driver/
├── src/                    # Production driver
│   ├── mt7927_pci.c       # PCI probe, power management
│   ├── mt7927_dma.c       # DMA queue management
│   ├── mt7927_mcu.c       # MCU protocol, firmware loading
│   └── *.h                # Headers and register definitions
│
├── tests/05_dma_impl/     # Test modules
│   ├── test_power_ctrl.ko
│   ├── test_wfsys_reset.ko
│   ├── test_dma_queues.ko
│   └── test_fw_load.ko
│
├── diag/                  # Diagnostic modules (18 total)
│   └── mt7927_diag.ko
│
└── docs/                  # Complete documentation
    ├── ZOUYONGHAO_ANALYSIS.md  ← ROOT CAUSE & SOLUTION
    ├── MT6639_ANALYSIS.md      ← Architecture proof
    ├── ROADMAP.md              ← Current status
    ├── CONTRIBUTING.md         ← How to help
    ├── HARDWARE.md             ← Specifications
    ├── TROUBLESHOOTING.md      ← Common issues
    └── FAQ.md                  ← Questions
```

## Documentation

**Start here**:
- **[docs/ZOUYONGHAO_ANALYSIS.md](docs/ZOUYONGHAO_ANALYSIS.md)** - Root cause and solution ⚠️ **CRITICAL**
- **[docs/MT6639_ANALYSIS.md](docs/MT6639_ANALYSIS.md)** - Architecture proof (MT7927 = MT6639 variant)
- **[docs/ROADMAP.md](docs/ROADMAP.md)** - Current status and implementation plan
- **[docs/CONTRIBUTING.md](docs/CONTRIBUTING.md)** - How to contribute

**Additional resources**:
- **[DEVELOPMENT_LOG.md](DEVELOPMENT_LOG.md)** - Complete development history (17 phases)
- **[docs/README.md](docs/README.md)** - Documentation index and navigation
- **[docs/TROUBLESHOOTING.md](docs/TROUBLESHOOTING.md)** - Common issues and solutions
- **[docs/FAQ.md](docs/FAQ.md)** - Frequently asked questions
- **[docs/HARDWARE.md](docs/HARDWARE.md)** - Hardware specifications

## What's Working ✅

- PCI enumeration and BAR mapping
- Power management handshake
- WiFi subsystem reset
- DMA ring allocation and configuration
- Firmware file loading
- MCU ready status
- **Ring protocol validated** (15/16 confirmed)
- **Architectural foundation confirmed** (MT6639 variant)
- **Root cause identified** (mailbox protocol)

## What's Not Working ❌

- Firmware loading (blocked on mailbox protocol implementation)
- Network interface creation (blocked on firmware loading)

## How to Help

**Priority #1: Implement polling-based firmware loader**

See **[docs/CONTRIBUTING.md](docs/CONTRIBUTING.md)** for detailed contribution guidelines.

Quick test (30 min - validate hypothesis):
```c
// File: src/mt7927_mcu.c - change one line
ret = mt76_mcu_send_msg(&dev->mt76, MCU_CMD_PATCH_SEM_CONTROL,
                       &req, sizeof(req), false);  // Don't wait!
```

Full solution (2-3 hours):
- Create `src/mt7927_fw_load.c` based on zouyonghao reference
- Skip mailbox waits, use polling delays
- Add aggressive TX cleanup
- Manual completion signaling

See **[docs/ZOUYONGHAO_ANALYSIS.md](docs/ZOUYONGHAO_ANALYSIS.md)** lines 274-315 for complete implementation requirements.

## Hardware Requirements

- MediaTek MT7927 WiFi 7 chipset (PCI ID: 14c3:7927)
- Linux kernel 6.7+
- MT7925 firmware files

## Build System

```bash
make clean     # Clean build artifacts
make           # Build production driver
make tests     # Build test modules
make diag      # Build diagnostic modules
make check     # Check device state
make recover   # Recover from error state (PCI rescan)
```

## License

GPL v2 - Intended for upstream Linux kernel submission

## Support

- **GitHub Issues**: Bug reports and discussions
- **Documentation**: Complete guides in `docs/` directory
- **Linux Wireless**: [Mailing list](http://vger.kernel.org/vger-lists.html#linux-wireless) (for upstream submission)

## References

- **Working driver**: `reference_zouyonghao_mt7927/` - Polling-based firmware loading
- **MT6639 code**: `reference_mtk_modules/` - Architectural parent
- **MT7925 driver**: Linux kernel tree - CONNAC3X sibling
- **mt76 framework**: OpenWrt repository - Shared infrastructure

---

**Current Status (2026-01-31)**:

ROOT CAUSE FOUND: MT7927 ROM bootloader doesn't support mailbox protocol. Implementation needed: polling-based firmware loader (validated by zouyonghao working driver).

See **[docs/ZOUYONGHAO_ANALYSIS.md](docs/ZOUYONGHAO_ANALYSIS.md)** for complete analysis and implementation guide.
