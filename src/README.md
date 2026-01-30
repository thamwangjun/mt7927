# MT7927 WiFi 7 Linux Driver

This directory contains the main driver implementation for the MediaTek MT7927 WiFi 7 chip.

## Files

| File | Description |
|------|-------------|
| `mt7927.h` | Main header with device structures and function declarations |
| `mt7927_regs.h` | Register definitions and address mapping table |
| `mt7927_mcu.h` | MCU protocol definitions and message structures |
| `mt7927_pci.c` | PCI driver interface, power management, reset, IRQ handling |
| `mt7927_dma.c` | DMA queue allocation, TX/RX ring management |
| `mt7927_mcu.c` | MCU communication and firmware loading |
| `Makefile` | Build configuration |

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                     mt7927_pci.c                            │
│  ┌──────────────┐  ┌───────────────┐  ┌──────────────────┐ │
│  │ PCI Probe    │  │ Power Mgmt    │  │ IRQ Handler      │ │
│  │ - BAR mapping│  │ - fw_pmctrl   │  │ - irq_handler    │ │
│  │ - DMA setup  │  │ - drv_pmctrl  │  │ - irq_tasklet    │ │
│  └──────────────┘  └───────────────┘  └──────────────────┘ │
└─────────────────────────────────────────────────────────────┘
                              │
        ┌─────────────────────┴─────────────────────┐
        │                                           │
        ▼                                           ▼
┌───────────────────────┐               ┌───────────────────────┐
│     mt7927_dma.c      │               │     mt7927_mcu.c      │
│ ┌───────────────────┐ │               │ ┌───────────────────┐ │
│ │ Queue Management  │ │               │ │ Message Protocol  │ │
│ │ - queue_alloc     │ │               │ │ - send_msg        │ │
│ │ - queue_free      │ │◄─────────────►│ │ - wait_response   │ │
│ │ - tx_queue_skb    │ │               │ │ - parse_response  │ │
│ │ - tx_complete     │ │               │ └───────────────────┘ │
│ │ - rx_poll         │ │               │ ┌───────────────────┐ │
│ └───────────────────┘ │               │ │ Firmware Loading  │ │
│ ┌───────────────────┐ │               │ │ - load_patch      │ │
│ │ DMA Control       │ │               │ │ - load_ram        │ │
│ │ - dma_init        │ │               │ │ - start_firmware  │ │
│ │ - dma_enable      │ │               │ └───────────────────┘ │
│ │ - dma_disable     │ │               └───────────────────────┘
│ └───────────────────┘ │
└───────────────────────┘
```

## Initialization Sequence

1. **PCI Probe** - Enable device, map BARs, setup DMA mask
2. **Power Management** - Handshake with firmware for power control
3. **WiFi System Reset** - Reset chip to clean state
4. **DMA Initialization** - Allocate TX/RX queues
5. **IRQ Setup** - Register interrupt handler
6. **MCU Initialization** - Load firmware and start MCU
7. **Firmware Loading**:
   - Acquire patch semaphore
   - Load ROM patch via DMA
   - Signal patch complete
   - Release patch semaphore
   - Load RAM code via DMA
   - Start firmware execution

## Building

From the project root:

```bash
# Build driver only
make driver

# Build everything (driver + tests)
make all

# Clean
make clean
```

Or from this directory:

```bash
make
```

## Loading

```bash
# Remove conflicting drivers
sudo rmmod mt7921e mt7925e 2>/dev/null

# Load driver
sudo insmod mt7927.ko

# Check status
sudo dmesg | tail -30
lspci -k | grep -A3 14c3:7927
```

## Required Firmware

The driver uses MT7925 firmware (confirmed compatible with MT7927):

```
/lib/firmware/mediatek/mt7925/WIFI_MT7925_PATCH_MCU_1_1_hdr.bin
/lib/firmware/mediatek/mt7925/WIFI_RAM_CODE_MT7925_1_1.bin
```

Install firmware:

```bash
wget https://git.kernel.org/pub/scm/linux/kernel/git/firmware/linux-firmware.git/plain/mediatek/mt7925/WIFI_MT7925_PATCH_MCU_1_1_hdr.bin
wget https://git.kernel.org/pub/scm/linux/kernel/git/firmware/linux-firmware.git/plain/mediatek/mt7925/WIFI_RAM_CODE_MT7925_1_1.bin

sudo mkdir -p /lib/firmware/mediatek/mt7925
sudo cp *.bin /lib/firmware/mediatek/mt7925/
```

## Development Notes

### Register Access

The MT7927 uses a register remapping scheme. Always use the provided access functions:

```c
// Read register with address translation
u32 val = mt7927_rr(dev, MT_SOME_REGISTER);

// Write register with address translation
mt7927_wr(dev, MT_SOME_REGISTER, value);

// Read-modify-write
mt7927_rmw(dev, MT_REGISTER, mask, value);

// Poll for condition
bool ok = mt7927_poll(dev, MT_REG, mask, expected, timeout_us);
```

### MCU Communication

Send commands to the MCU using:

```c
// Fire-and-forget
ret = mt7927_mcu_send_msg(dev, MCU_CMD(CMD_ID), data, len, false);

// Wait for response
ret = mt7927_mcu_send_msg(dev, MCU_CMD(CMD_ID), data, len, true);

// Get response data
struct sk_buff *skb;
ret = mt7927_mcu_send_and_get_msg(dev, cmd, data, len, true, &skb);
```

### DMA Queues

- TX Queue 0: Data (Band0)
- TX Queue 15: MCU WM commands
- TX Queue 16: Firmware download
- RX Queue 0: MCU responses
- RX Queue 2: Data (Band0)

## Troubleshooting

### Driver won't load

Check for conflicting drivers:

```bash
lsmod | grep mt79
sudo rmmod mt7921e mt7925e
```

### Chip in error state

Reset via PCI:

```bash
echo 1 | sudo tee /sys/bus/pci/devices/0000:0a:00.0/remove
sleep 2
echo 1 | sudo tee /sys/bus/pci/rescan
```

### Firmware load fails

1. Verify firmware files exist in `/lib/firmware/mediatek/mt7925/`
2. Check `dmesg` for specific error messages
3. Try running individual test modules in `tests/05_dma_impl/`

## License

GPL v2 - Intended for upstream Linux kernel submission
