# Frequently Asked Questions

## General Questions

### Q: Why not just use the mt7925e driver?

**A**: The mt7925e driver refuses to bind to MT7927's PCI ID (14c3:7927) and adding the ID via new_id fails with "Invalid argument". While MT7927 is related to MT7925 (both are CONNAC3X family), they require different driver configurations.

### Q: Is MT7927 the same as MT7925?

**A**: No. MT7927 is an **MT6639 variant**, not MT7925. While both MT6639 and MT7925 are CONNAC3X family chips (which explains firmware compatibility), MT7927 follows MT6639's architecture:
- MT6639: Sparse ring layout (0,1,2,15,16), single WFDMA0
- MT7925: Dense rings (0-16), has WFDMA1
- MT7927: Follows MT6639 configuration

See [MT6639_ANALYSIS.md](MT6639_ANALYSIS.md) for complete evidence.

### Q: Will this support full WiFi 7 320MHz channels?

**A**: Initially it will work like MT7925 (160MHz). Adding 320MHz support will come after basic functionality works. The chip is capable of 320MHz (advertised as "802.11be 320MHz 2x2"), but driver support requires additional development.

### Q: When will this be in the mainline kernel?

**A**: Once we have a working driver, submission typically takes 2-3 kernel cycles (3-6 months). The timeline depends on:
1. Getting basic functionality working (current focus)
2. Code cleanup for upstream standards
3. Review process on linux-wireless mailing list
4. Maintainer acceptance and merge

## Technical Questions

### Q: What firmware does MT7927 use?

**A**: MT7927 uses MT7925 firmware files:
- `WIFI_MT7925_PATCH_MCU_1_1_hdr.bin` - MCU patch
- `WIFI_RAM_CODE_MT7925_1_1.bin` - RAM code (1.4MB)

This is confirmed through:
1. Windows driver analysis (both drivers load same firmware)
2. MediaTek kernel modules (MT7927 mapped to MT6639, which shares CONNAC3X firmware)
3. Successful firmware file loading in our driver

See [FIRMWARE_ANALYSIS.md](FIRMWARE_ANALYSIS.md) and [WINDOWS_FIRMWARE_ANALYSIS.md](WINDOWS_FIRMWARE_ANALYSIS.md).

### Q: Why are rings 15/16 used instead of 4/5?

**A**: MT7927 follows MT6639 configuration, which uses the CONNAC3X standard:
- Ring 15: MCU_WM (MCU command queue)
- Ring 16: FWDL (firmware download queue)

This is defined in `CONNAC3X_FWDL_TX_RING_IDX` and validated through:
1. MediaTek kernel modules (MT6639 chip configuration)
2. Working zouyonghao driver (uses rings 15/16 successfully)

See [MT6639_ANALYSIS.md](MT6639_ANALYSIS.md) for complete validation.

### Q: Is this safe to test?

**A**: Yes, with precautions:
- We're using proven MT7925 code paths
- Driver operates at PCI level (can't damage hardware)
- Worst case: driver doesn't initialize (current state)
- Recovery: PCI rescan or reboot restores chip to working state

**Best practices**:
- Start with test modules, not production driver
- Use `mt7927_diag.ko` first to verify device state
- Keep backup kernel (in case of module issues)
- Know how to boot into recovery mode

### Q: What's the current blocker?

**A**: **ROOT CAUSE FOUND (2026-01-31)**: MT7927 ROM bootloader does NOT support mailbox command protocol!

Our driver:
1. Sends PATCH_SEM_CONTROL command
2. **Waits for mailbox response** ← **BLOCKS HERE**
3. ROM bootloader never sends responses (mailbox not implemented)
4. Driver times out after 5 seconds
5. Never proceeds to firmware loading

**Solution**: Polling-based firmware loading (no mailbox waits), validated by zouyonghao working driver.

See [ZOUYONGHAO_ANALYSIS.md](ZOUYONGHAO_ANALYSIS.md) for complete root cause analysis.

### Q: Why does DMA appear stuck?

**A**: DMA hardware works correctly! The problem is:
- We send a command the ROM doesn't understand (semaphore)
- We wait for a response that will never come (mailbox)
- We time out before trying the next operation
- DMA_DIDX stays at 0 because we never get past the first command

This is a **protocol issue**, not a DMA hardware issue.

### Q: Is ASPM L1 the problem?

**A**: **No**. Initially we suspected L1 ASPM power states were blocking DMA, but the working zouyonghao driver has L1 **enabled** (same as ours). The driver only disables L0s, not L1.

Root cause is the mailbox protocol assumption, not ASPM.

## Development Questions

### Q: How can I help?

**A**: See [CONTRIBUTING.md](CONTRIBUTING.md) for detailed contribution guidelines.

Quick ways to help:
1. **Test on your hardware** - Build and test modules, report results
2. **Implementation** - Implement polling-based firmware loader (see ZOUYONGHAO_ANALYSIS.md)
3. **Documentation** - Improve docs, add examples, clarify confusing sections
4. **Code review** - Review changes, suggest improvements
5. **Hardware analysis** - If you have MT7927 hardware, run diagnostics

### Q: What reference code should I study?

**A**: Priority order:
1. **zouyonghao MT7927 driver** - Working implementation (reference_zouyonghao_mt7927/)
2. **MT6639 code** - Architectural parent (reference_mtk_modules/connectivity/.../mt6639/)
3. **MT7925 driver** - CONNAC3X sibling (Linux kernel tree)
4. **mt76 framework** - Shared infrastructure (Linux kernel tree)

### Q: Where is the complete development history?

**A**: See [../DEVELOPMENT_LOG.md](../DEVELOPMENT_LOG.md) for complete chronological history:
- Phase 1-15: Initial development, hardware exploration, DMA investigation
- Phase 16: MT6639 discovery (architectural foundation)
- Phase 17: Root cause discovery (mailbox protocol issue)

### Q: What needs to be implemented?

**A**: Two approaches:

**Quick test** (30 min - validate hypothesis):
```c
// In src/mt7927_mcu.c - change one line:
ret = mt76_mcu_send_msg(&dev->mt76, MCU_CMD_PATCH_SEM_CONTROL,
                       &req, sizeof(req), false);  // Don't wait!
```

**Full solution** (2-3 hours - complete implementation):
1. Create `src/mt7927_fw_load.c` based on zouyonghao reference
2. Implement polling-based protocol (no mailbox waits)
3. Add aggressive TX cleanup and polling delays
4. Add MCU IDLE pre-check and status register polling
5. Manual completion signaling via SW_INIT_DONE

See [ZOUYONGHAO_ANALYSIS.md](ZOUYONGHAO_ANALYSIS.md) for complete implementation details.

## Build and Testing Questions

### Q: Build fails - what to check?

**A**: Common issues:
1. **Missing kernel headers**: `sudo apt install linux-headers-$(uname -r)`
2. **Wrong kernel version**: Need 6.7+ for mt7925 base infrastructure
3. **Stale build**: Try `make clean && make`

See [TROUBLESHOOTING.md](TROUBLESHOOTING.md) for complete build troubleshooting.

### Q: Which module should I load for testing?

**A**: Recommended testing order:
1. **mt7927_diag.ko** - Verify device state and register access
2. **test_fw_load.ko** - Complete initialization test (most comprehensive)
3. **mt7927.ko** - Production driver (after test modules work)

Each module tests different aspects. Start with diag to verify chip is responsive.

### Q: What does a successful test look like?

**A**: Currently (before mailbox fix):
```
[   10.123] mt7927: Chip ID: 0x00511163
[   10.124] mt7927: BAR0 mapped, size=1048576
[   10.125] mt7927: Power management: Host claimed DMA
[   10.126] mt7927: WFSYS reset complete
[   10.127] mt7927: DMA rings allocated (8 TX, 16 RX)
[   10.128] mt7927: MCU ready (status=0x00000001)
[   10.129] mt7927: Sending patch semaphore command...
[   15.130] mt7927: ERROR: MCU command timeout  ← Expected blocker
```

After mailbox fix (expected):
```
[   10.123] mt7927: Chip ID: 0x00511163
[   ...same as above...]
[   10.129] mt7927: Loading patch (polling mode)
[   10.145] mt7927: Patch sent successfully
[   10.146] mt7927: Loading RAM firmware
[   10.230] mt7927: Firmware loaded successfully
[   10.231] mt7927: Network interface created: wlan0
```

### Q: My chip shows 0xffffffff - what happened?

**A**: Chip is in error/hung state. This can happen after:
- Failed module loads
- Write tests to wrong registers
- Incomplete initialization sequences
- System crashes during testing

**Recovery**: PCI rescan or reboot (see [TROUBLESHOOTING.md](TROUBLESHOOTING.md))

## Architecture Questions

### Q: What is CONNAC3X?

**A**: CONNAC3X is MediaTek's third-generation connectivity architecture family. It includes:
- MT6639 (architectural parent of MT7927)
- MT7925 (WiFi 7 sibling)
- MT7927 (this chip - MT6639 variant)

These chips share:
- Firmware format and files
- Ring protocol (rings 15/16 for MCU/FWDL)
- DMA descriptor format
- MCU message structures

But differ in:
- Physical ring count (MT7925: 17 rings, MT6639/MT7927: sparse layout)
- DMA engine configuration (MT7925: WFDMA0+1, MT6639/MT7927: WFDMA0 only)
- Some register offsets and capabilities

### Q: Why does MediaTek map MT7927 to MT6639?

**A**: Evidence from MediaTek kernel modules shows MT7927 uses MT6639 driver data directly:
```c
{0x7927, &mt6639_data},  // Direct mapping in module code
```

This proves MT7927 is an MT6639 variant, not a standalone chip or MT7925 variant. The architectural relationship explains why MT7927 follows MT6639's:
- Sparse ring layout
- Single WFDMA0 configuration
- Register addressing
- Initialization sequence

## Debugging Questions

### Q: How do I check if ASPM is disabled?

**A**:
```bash
# Check current ASPM state
lspci -vv -s $(lspci -nn | grep 14c3:7927 | cut -d' ' -f1) | grep -i aspm

# Disable all ASPM (for testing)
DEVICE=$(lspci -nn | grep 14c3:7927 | cut -d' ' -f1)
sudo setpci -s $DEVICE CAP_EXP+10.w=0x0000

# Verify
lspci -vv -s $DEVICE | grep -i aspm
# Should show: LnkCtl: ASPM Disabled
```

### Q: How do I capture diagnostic logs?

**A**:
```bash
# Clear old logs
sudo dmesg -C

# Load module
sudo insmod diag/mt7927_diag.ko

# Capture output
sudo dmesg | tee mt7927_diag_output.txt

# Clean up
sudo rmmod mt7927_diag
```

Send `mt7927_diag_output.txt` when reporting issues.

### Q: What information should I include in bug reports?

**A**:
1. **Hardware**: Output of `lspci -vvv -s $(lspci -nn | grep 14c3:7927 | cut -d' ' -f1)`
2. **Kernel version**: `uname -a`
3. **Module version**: Git commit hash or release tag
4. **Full dmesg**: From module load to error
5. **Diagnostic output**: From mt7927_diag.ko
6. **Steps to reproduce**: What you did before the issue
7. **Expected vs actual**: What you expected vs what happened

## License and Legal

### Q: What license is this code under?

**A**: GPL v2 - Same as Linux kernel. This driver is intended for upstream submission to the mainline kernel.

### Q: Can I use this in commercial products?

**A**: Yes, under GPL v2 terms. You must:
1. Provide source code to recipients
2. Keep GPL v2 license intact
3. Document any modifications
4. Allow redistribution under same terms

See LICENSE file for complete terms.

### Q: Who maintains this driver?

**A**: Currently community-maintained. Once merged into mainline kernel, it will be maintained by Linux wireless subsystem maintainers.

## Additional Resources

- **Documentation index**: [README.md](README.md)
- **Root cause analysis**: [ZOUYONGHAO_ANALYSIS.md](ZOUYONGHAO_ANALYSIS.md)
- **Architecture proof**: [MT6639_ANALYSIS.md](MT6639_ANALYSIS.md)
- **Development history**: [../DEVELOPMENT_LOG.md](../DEVELOPMENT_LOG.md)
- **Hardware details**: [HARDWARE.md](HARDWARE.md)
- **Troubleshooting**: [TROUBLESHOOTING.md](TROUBLESHOOTING.md)
