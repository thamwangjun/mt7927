# Troubleshooting Guide

## Driver Won't Load

### Symptoms
- `insmod` fails with error
- Module loads but immediately unloads
- Kernel logs show binding errors

### Solutions

```bash
# Check for conflicts
lsmod | grep mt79
sudo rmmod mt7921e mt7925e  # Remove any conflicting drivers

# Check kernel messages
sudo dmesg | grep -E "mt7927|0a:00"

# Verify kernel version (need 6.7+ for mt7925 base)
uname -r  # Should show 6.7 or higher
```

## Chip in Error State

### Symptoms
- Chip ID reads as `0xffffffff`
- All register reads return `0xffffffff`
- Module probe fails with -EIO

### Solution: PCI Rescan

```bash
# Bash:
DEVICE=$(lspci -nn | grep 14c3:7927 | cut -d' ' -f1)
echo 1 | sudo tee /sys/bus/pci/devices/0000:$DEVICE/remove
sleep 2
echo 1 | sudo tee /sys/bus/pci/rescan

# Fish:
set DEVICE (lspci -nn | grep 14c3:7927 | cut -d' ' -f1)
echo 1 | sudo tee /sys/bus/pci/devices/0000:$DEVICE/remove
sleep 2
echo 1 | sudo tee /sys/bus/pci/rescan

# Or use Makefile target:
make recover
```

### When PCI Rescan Doesn't Work

If the chip remains unresponsive after PCI rescan:
1. **Full system reboot** (most reliable)
2. Check for hardware issues (ensure card is properly seated)
3. Try different PCI slot if available

## Firmware Not Found

### Symptoms
- Module loads but fails with "firmware not found"
- dmesg shows "request_firmware failed"

### Solution

```bash
# Verify firmware is installed
ls -la /lib/firmware/mediatek/mt7925/
# Should show WIFI_MT7925_PATCH_MCU_1_1_hdr.bin and WIFI_RAM_CODE_MT7925_1_1.bin

# If missing, download and install:
mkdir -p ~/mt7927_firmware
cd ~/mt7927_firmware

wget https://git.kernel.org/pub/scm/linux/kernel/git/firmware/linux-firmware.git/plain/mediatek/mt7925/WIFI_MT7925_PATCH_MCU_1_1_hdr.bin
wget https://git.kernel.org/pub/scm/linux/kernel/git/firmware/linux-firmware.git/plain/mediatek/mt7925/WIFI_RAM_CODE_MT7925_1_1.bin

sudo mkdir -p /lib/firmware/mediatek/mt7925
sudo cp *.bin /lib/firmware/mediatek/mt7925/
sudo update-initramfs -u
```

## Build Failures

### Symptoms
- `make` fails with compilation errors
- Missing kernel headers

### Solutions

```bash
# Install kernel headers
sudo apt install linux-headers-$(uname -r)  # Debian/Ubuntu
sudo dnf install kernel-devel kernel-headers  # Fedora
sudo pacman -S linux-headers  # Arch Linux

# Clean and rebuild
make clean
make
```

## Module Won't Bind to Device

### Symptoms
- Module loads but device shows "no driver" in lspci -k
- Driver claims wrong device

### Solutions

```bash
# Check device is present
lspci -nn | grep 14c3:7927

# Manually bind (if needed)
echo "14c3 7927" | sudo tee /sys/bus/pci/drivers/mt7927/new_id

# Check binding status
lspci -k | grep -A 3 "14c3:7927"
```

## Device State Corrupted After Testing

### Symptoms
- Chip ID reads as 0x00000000 (wrong register or corrupted)
- Register values differ from expected fresh-boot values
- DMA rings show unexpected configuration
- Module loads fail with -EIO or timeout errors

### Recovery Options

**Option 1: PCI Rescan** (quick, often sufficient)
```bash
# See "Chip in Error State" section above
make recover
```

**Option 2: Full System Reboot** (recommended when in doubt)

### When to Reboot

- After kernel oops or panic related to MT7927
- After multiple failed test iterations
- Before running critical validation tests
- When PCI rescan doesn't restore expected register values
- When comparing results across different test scenarios

**Best Practice**: Start each test session with a fresh reboot to ensure known-good device state.

## ASPM Issues

### ASPM L1 is NOT the DMA Blocker

**Status (2026-01-31)**: ASPM L1 hypothesis was **DISPROVEN**. The working zouyonghao driver has L1 enabled (same as ours). The real blocker was the mailbox protocol assumption.

### Disabling ASPM for Testing (Usually Not Needed)

```bash
# Disable L1 ASPM (NOT required - but useful for debugging)
# Bash:
DEVICE=$(lspci -nn | grep 14c3:7927 | cut -d' ' -f1)
sudo setpci -s $DEVICE CAP_EXP+10.w=0x0000

# Fish:
set DEVICE (lspci -nn | grep 14c3:7927 | cut -d' ' -f1)
sudo setpci -s $DEVICE CAP_EXP+10.w=0x0000
```

**Note**: ASPM L1 being enabled is normal and expected. The working driver doesn't disable it.

## Kernel Oops or Panic

### Symptoms
- System freezes or crashes when loading module
- Kernel panic messages in dmesg
- Page fault errors

### Common Causes
1. **Wrong BAR mapping** - Using BAR2 instead of BAR0
2. **Out-of-bounds register access** - Accessing registers beyond BAR size
3. **NULL pointer dereference** - Missing initialization checks

### Solutions
1. Check module is using BAR0 (2MB) not BAR2 (32KB)
2. Verify all register offsets are within BAR0 range
3. Always check for chip error state (0xffffffff) before register access
4. Add safety checks for NULL pointers

### Emergency Recovery
If system is unstable:
1. Reboot into single-user mode or recovery console
2. Remove offending .ko file from filesystem
3. Blacklist module if it auto-loads: `echo "blacklist mt7927" | sudo tee /etc/modprobe.d/mt7927-blacklist.conf`

## DMA Issues

### Symptoms
- DMA_DIDX stuck at 0
- MCU command timeouts
- Firmware transfer fails

### Investigation Steps

1. **Check ring indices**:
   ```bash
   sudo insmod tests/05_dma_impl/test_dma_queues.ko
   sudo dmesg | tail -40
   ```

2. **Verify descriptor memory**: Look for "coherent DMA allocation" messages in dmesg

3. **Monitor RST register**: Should be 0x30 during config

4. **Check MCU ready status**:
   ```bash
   sudo insmod diag/mt7927_diag.ko
   sudo dmesg | tail -20
   # FW_OWN_REQ_SET should return 0x1
   ```

5. **Look for interrupt activity**: Check for "IRQ handler" messages in dmesg

### Known Issue: Mailbox Protocol

**ROOT CAUSE FOUND (2026-01-31)**: MT7927 ROM bootloader does NOT support mailbox command protocol!

See **[ZOUYONGHAO_ANALYSIS.md](ZOUYONGHAO_ANALYSIS.md)** for complete root cause analysis and solution.

## Getting More Help

1. **Read root cause analysis**: [ZOUYONGHAO_ANALYSIS.md](ZOUYONGHAO_ANALYSIS.md) - Critical!
2. **Check development log**: [../DEVELOPMENT_LOG.md](../DEVELOPMENT_LOG.md) - Phase 17 documents root cause
3. **Review architecture**: [MT6639_ANALYSIS.md](MT6639_ANALYSIS.md) - Proves MT7927 is MT6639 variant
4. **Ask in GitHub issues**: Report findings and discuss approaches
5. **Study reference drivers**:
   - Primary: zouyonghao MT7927 in `reference_zouyonghao_mt7927/`
   - Secondary: MT6639 in `reference_mtk_modules/`
   - Tertiary: MT7925 in kernel tree
