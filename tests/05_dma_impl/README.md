# MT7927 DMA Implementation Tests

This directory contains test modules for verifying the DMA implementation components.

## Test Modules

### test_power_ctrl.ko
Tests the power management handshake between driver and firmware.

**What it tests:**
- Reading current power control state (LPCTL register)
- Acquiring driver ownership
- Setting firmware ownership
- Power control register remapping

**Expected output:**
- Shows current HOST_OWN and FW_OWN bits
- Reports success/timeout for ownership transitions

### test_wfsys_reset.ko
Tests the WiFi subsystem reset functionality.

**What it tests:**
- Reading WiFi system reset state
- Asserting reset (clearing enable bit)
- Deasserting reset (setting enable bit)
- Verifying system state after reset

**Expected output:**
- Shows WFSYS_SW_RST_B register values
- Reports reset timing

### test_dma_queues.ko
Tests DMA ring allocation and configuration.

**What it tests:**
- Current DMA global configuration
- Allocating coherent DMA memory for descriptor ring
- Configuring TX ring registers (base, count, index)
- Enabling TX and RX DMA
- Verifying register writes

**Expected output:**
- Shows GLO_CFG before and after
- Reports DMA enable success/failure

### test_fw_load.ko
Integration test for firmware loading via DMA.

**What it tests:**
- Loading firmware files from /lib/firmware
- Complete initialization sequence:
  1. Power control setup
  2. WiFi system reset
  3. DMA queue setup
  4. Firmware transfer via DMA

**Expected output:**
- Shows firmware sizes
- Reports progress for each step
- Shows final register states

## Building

From the project root:

```bash
make tests
```

Or specifically for this directory:

```bash
cd tests/05_dma_impl
make -C /lib/modules/$(uname -r)/build M=$(pwd) modules
```

## Running Tests

**Important:** Only load one test module at a time, as they all bind to the same PCI device.

```bash
# Unload any existing driver
sudo rmmod mt7921e mt7925e mt7927 2>/dev/null

# Load a test module
sudo insmod test_power_ctrl.ko

# Check results
sudo dmesg | tail -30

# Unload before loading next test
sudo rmmod test_power_ctrl
```

## Test Order

For comprehensive testing, run in this order:

1. **test_power_ctrl** - Verify power control works
2. **test_wfsys_reset** - Verify reset works  
3. **test_dma_queues** - Verify DMA can be configured
4. **test_fw_load** - Full integration test

## Recovery

If the chip enters an error state (registers read 0xffffffff):

```bash
# Remove and rescan PCI device
echo 1 | sudo tee /sys/bus/pci/devices/0000:0a:00.0/remove
sleep 2
echo 1 | sudo tee /sys/bus/pci/rescan
```

Replace `0000:0a:00.0` with your device's PCI address.

## Success Criteria

- **test_power_ctrl**: Driver ownership acquired, firmware ownership set
- **test_wfsys_reset**: Reset completes without timeout
- **test_dma_queues**: DMA enabled bits set in GLO_CFG
- **test_fw_load**: Firmware chunks sent, no DMA errors

## Notes

These tests validate individual components. The full driver in `src/` combines all these components with proper interrupt handling and MCU message protocol for complete firmware loading.
