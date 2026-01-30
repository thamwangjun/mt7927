# MT7927 Test Modules Documentation

This document provides comprehensive documentation for the DMA implementation test modules located in `tests/05_dma_impl/`. These modules validate individual components of the MT7927 driver initialization sequence before integration into the full driver.

## Table of Contents

1. [Overview](#overview)
2. [test_power_ctrl.c](#test_power_ctrlc)
3. [test_wfsys_reset.c](#test_wfsys_resetc)
4. [test_dma_queues.c](#test_dma_queuesc)
5. [test_fw_load.c](#test_fw_loadc)
6. [Building and Running](#building-and-running)
7. [Test Execution Order](#test-execution-order)
8. [Troubleshooting](#troubleshooting)

---

## Overview

The test modules in `tests/05_dma_impl/` are designed to validate critical initialization components independently. Each module:

- Binds to the MT7927 PCI device (14c3:7927)
- Tests a specific initialization component in isolation
- Provides detailed kernel log output for analysis
- Can be run independently without affecting other components

**Important:** Only one test module should be loaded at a time, as they all bind to the same PCI device.

---

## test_power_ctrl.c

### Purpose

Tests the power management handshake mechanism between the driver and firmware. This is a critical step that must complete before any other initialization can proceed.

### What It Validates

- Reading the current power control state from `MT_CONN_ON_LPCTL` register
- Register remapping functionality for accessing high addresses (0x7c060010)
- Acquiring driver ownership of power control
- Setting firmware ownership of power control
- Power control state transitions and timing

### Test Sequence

1. **PCI Device Setup**
   - Enables PCI device and maps BAR2
   - Sets PCI master mode

2. **Test 1: Read Current Power State**
   - Uses register remapping to access `MT_CONN_ON_LPCTL` (0x7c060010)
   - Reads and displays current LPCTL register value
   - Reports HOST_OWN and FW_OWN bit states

3. **Test 2: Acquire Driver Ownership**
   - Writes `MT_CONN_ON_LPCTL_HOST_OWN` bit to claim driver control
   - Polls for up to 1000ms (100 iterations × 10ms) waiting for FW_OWN to clear
   - Reports success or timeout

4. **Test 3: Set Firmware Ownership**
   - Writes `MT_CONN_ON_LPCTL_FW_OWN` bit to give control to firmware
   - Polls for up to 1000ms waiting for FW_OWN bit to be set
   - Reports final LPCTL register state

### Expected Output

**Success Case:**
```
=== MT7927 Power Control Test ===
Test 1: Reading power control state
  LPCTL value: 0x00000000
  HOST_OWN: 0, FW_OWN: 0
Test 2: Attempting driver power control
  Driver ownership acquired after 9 ms
Test 3: Attempting firmware power control
  Firmware ownership set after 5 ms
  Final LPCTL: 0x00000004
=== Power Control Test Complete ===
```

**Failure Indicators:**
- Timeout waiting for driver ownership (1000ms exceeded)
- Timeout waiting for firmware ownership (1000ms exceeded)
- Register reads return 0xffffffff (chip in error state)

### Pass/Fail Conditions

**PASS:** 
- Driver ownership acquired within 1000ms
- Firmware ownership can be set
- LPCTL register transitions between states correctly

**FAIL:**
- Timeout waiting for ownership transitions
- Register reads return invalid values (0xffffffff)
- Kernel errors during PCI setup

### How to Run

```bash
# Unload any existing driver
sudo rmmod mt7921e mt7925e mt7927 2>/dev/null

# Build the test module
cd tests/05_dma_impl
make -C /lib/modules/$(uname -r)/build M=$(pwd) modules

# Load the module
sudo insmod test_power_ctrl.ko

# View results
sudo dmesg | tail -30

# Unload before running other tests
sudo rmmod test_power_ctrl
```

### Interpreting Results

- **LPCTL value 0x00000000**: Chip is idle, no ownership claimed
- **LPCTL value 0x00000004**: Firmware owns power control (FW_OWN bit set)
- **HOST_OWN=1, FW_OWN=0**: Driver owns power control (expected after Test 2)
- **Ownership acquired quickly (< 50ms)**: Normal operation
- **Ownership timeout (> 1000ms)**: Chip may be in error state or firmware not responding

---

## test_wfsys_reset.c

### Purpose

Tests the WiFi subsystem reset functionality. This reset must complete successfully before firmware can be loaded and activated.

### What It Validates

- Reading WiFi system reset state from `MT_WFSYS_SW_RST_B` register
- Register remapping for accessing WiFi system registers (0x7c000140)
- Asserting WiFi subsystem reset (clearing enable bit)
- Deasserting reset (setting enable bit)
- Reset completion timing and state verification
- Reading `MT_CONN_ON_MISC` register state

### Test Sequence

1. **PCI Device Setup**
   - Enables PCI device and maps BAR2
   - Sets PCI master mode

2. **Test 1: Read Current WiFi System State**
   - Reads `MT_WFSYS_SW_RST_B` register via remapping
   - Displays reset enable bit state
   - Reads and displays `MT_CONN_ON_MISC` register

3. **Test 2: Assert Reset**
   - Clears `MT_WFSYS_SW_RST_B_EN` bit to assert reset
   - Waits 10ms for reset to take effect
   - Verifies reset state by reading register back

4. **Test 3: Deassert Reset**
   - Sets `MT_WFSYS_SW_RST_B_EN` bit to release reset
   - Polls for up to 1000ms waiting for reset to complete
   - Verifies reset enable bit is set

5. **Test 4: Verify Final State**
   - Reads final `MT_WFSYS_SW_RST_B` value
   - Reads final `MT_CONN_ON_MISC` value
   - Confirms system is ready for further initialization

### Expected Output

**Success Case:**
```
=== MT7927 WiFi System Reset Test ===
Test 1: Reading WiFi system state
  WFSYS_SW_RST_B: 0x00000011
  Reset enable: YES
  CONN_ON_MISC: 0x00000000
Test 2: Asserting WiFi system reset
  After assert: WFSYS_SW_RST_B = 0x00000010
Test 3: Deasserting WiFi system reset
  Reset complete after 0 ms
Test 4: Verifying state after reset
  Final WFSYS_SW_RST_B: 0x00000011
  Final CONN_ON_MISC: 0x00000000
=== WiFi System Reset Test Complete ===
```

**Failure Indicators:**
- Reset timeout (> 1000ms)
- Register reads return 0xffffffff
- Reset enable bit doesn't change state

### Pass/Fail Conditions

**PASS:**
- Reset can be asserted (enable bit clears)
- Reset can be deasserted (enable bit sets)
- Reset completes within 1000ms
- Final state shows reset enable bit set

**FAIL:**
- Reset timeout during deassert
- Register reads return invalid values
- Reset enable bit doesn't respond to writes

### How to Run

```bash
# Unload any existing driver
sudo rmmod mt7921e mt7925e mt7927 2>/dev/null

# Build the test module
cd tests/05_dma_impl
make -C /lib/modules/$(uname -r)/build M=$(pwd) modules

# Load the module
sudo insmod test_wfsys_reset.ko

# View results
sudo dmesg | tail -30

# Unload before running other tests
sudo rmmod test_wfsys_reset
```

### Interpreting Results

- **WFSYS_SW_RST_B = 0x00000011**: Reset is deasserted, system ready (bit 0 = enable, bit 4 = INIT_DONE)
- **WFSYS_SW_RST_B = 0x00000010**: Reset is asserted (enable bit cleared)
- **Reset completes quickly (< 50ms)**: Normal operation
- **Reset timeout**: Chip may be stuck in reset state or hardware issue
- **CONN_ON_MISC changes**: May indicate system state transitions

---

## test_dma_queues.c

### Purpose

Tests DMA ring allocation, configuration, and enablement. This validates that DMA descriptors can be properly set up for firmware transfer and MCU communication.

### What It Validates

- Reading current DMA global configuration (`MT_WFDMA0_GLO_CFG`)
- DMA mask configuration (32-bit addressing)
- Allocating coherent DMA memory for descriptor rings
- Configuring TX ring 0 registers (base address, count, indices)
- Resetting DMA TX pointers
- Enabling TX and RX DMA engines
- Verifying register writes persist after DMA enable

### Test Sequence

1. **PCI Device Setup**
   - Enables PCI device and maps BAR2
   - Sets PCI master mode
   - Configures 32-bit DMA mask

2. **Test 1: Read Current DMA State**
   - Reads `MT_WFDMA0_GLO_CFG` register
   - Displays TX_DMA_EN, RX_DMA_EN, TX_DMA_BUSY, RX_DMA_BUSY bits

3. **Test 2: Allocate Descriptor Ring**
   - Allocates coherent DMA memory for 128-entry descriptor ring
   - Zeroes the ring memory
   - Reports DMA address of allocated ring

4. **Test 3: Reset DMA Pointers**
   - Writes 0xffffffff to `MT_WFDMA0_RST_DTX_PTR` to reset all TX pointers
   - Waits 10ms for reset to complete

5. **Test 4: Configure TX Ring 0**
   - Writes ring base address (lower 32 bits) to `MT_WFDMA0_TX_RING0_BASE`
   - Writes ring base address (upper 32 bits) to `MT_WFDMA0_TX_RING0_BASE + 4`
   - Writes ring count (128) to `MT_WFDMA0_TX_RING0_CNT`
   - Sets CPU index (CIDX) to 0
   - Verifies all writes by reading registers back

6. **Test 5: Enable DMA**
   - Sets TX_EN (bit 0), RX_EN (bit 2), and WB_DDONE (bit 6) in GLO_CFG
   - Waits 10ms
   - Verifies DMA enable bits are set
   - Reports success or failure for TX and RX enable

### Expected Output

**Success Case:**
```
=== MT7927 DMA Queue Test ===
Test 1: Current DMA configuration
  GLO_CFG: 0x00000000
  TX_DMA_EN: 0, RX_DMA_EN: 0
  TX_DMA_BUSY: 0, RX_DMA_BUSY: 0
Test 2: Allocating DMA descriptor ring
  Ring allocated at DMA addr 0xff208000
Test 3: Resetting DMA pointers
  DMA pointers reset
Test 4: Configuring TX ring 0
  Ring base read back: 0xff208000 (expected: 0xff208000)
  Ring count read back: 128 (expected: 128)
  CPU index: 0
  DMA index: 0
Test 5: Enabling DMA
  GLO_CFG after enable: 0x00000045
  TX DMA enabled successfully!
  RX DMA enabled successfully!
=== DMA Queue Test Complete ===
```

**Failure Indicators:**
- Ring allocation fails (ENOMEM)
- Ring base address reads back as 0x00000000
- Ring count reads back as 0
- DMA enable bits don't set after write

### Pass/Fail Conditions

**PASS:**
- DMA ring allocated successfully
- Ring base address writes and reads back correctly
- Ring count writes and reads back correctly
- TX_DMA_EN and RX_DMA_EN bits set after enable
- Ring configuration persists after DMA enable

**FAIL:**
- DMA allocation failure
- Ring register writes don't persist (read back as 0x00000000)
- DMA enable bits don't set
- Kernel errors during setup

### How to Run

```bash
# Unload any existing driver
sudo rmmod mt7921e mt7925e mt7927 2>/dev/null

# Build the test module
cd tests/05_dma_impl
make -C /lib/modules/$(uname -r)/build M=$(pwd) modules

# Load the module
sudo insmod test_dma_queues.ko

# View results
sudo dmesg | tail -40

# Unload before running other tests
sudo rmmod test_dma_queues
```

### Interpreting Results

- **Ring DMA address**: Should be a valid 32-bit address (0xffxxxxxx range typical)
- **Ring base read back matches**: Confirms register writability
- **GLO_CFG = 0x00000045**: TX_EN (bit 0), RX_EN (bit 2), WB_DDONE (bit 6) all set
- **Ring base reads back as 0x00000000**: Register may not be writable (check RST state)
- **DMA enable bits don't set**: DMA engine may be in reset or hardware issue

### Important Notes

- The test allocates a ring but doesn't free it until module removal (to keep testing)
- Ring configuration must persist after DMA enable for proper operation
- If ring base reads back as 0x00000000, check that `MT_WFDMA0_RST` register has bits 4-5 set (0x30)

---

## test_fw_load.c

### Purpose

Integration test that validates the complete firmware loading sequence. This combines power control, WiFi system reset, DMA setup, and firmware transfer into a single test.

### What It Validates

- Loading firmware files from `/lib/firmware/mediatek/mt7925/`
- Complete initialization sequence:
  1. Power control handshake
  2. WiFi subsystem reset
  3. DMA queue setup
  4. Firmware transfer via DMA
- Register remapping for high addresses
- DMA descriptor ring configuration for firmware download queue
- Chunked firmware transfer
- Final system state verification

### Test Sequence

1. **PCI Device Setup**
   - Enables PCI device and maps BAR2
   - Sets PCI master mode
   - Configures 32-bit DMA mask

2. **Step 1: Load Firmware Files**
   - Requests `WIFI_MT7925_PATCH_MCU_1_1_hdr.bin` from firmware loader
   - Requests `WIFI_RAM_CODE_MT7925_1_1.bin` from firmware loader
   - Reports firmware sizes

3. **Step 2: Power Control Setup**
   - Calls `setup_power_control()`:
     - Gives control to firmware first
     - Claims driver control
     - Polls for ownership (up to 2000ms)

4. **Step 3: WiFi System Reset**
   - Calls `reset_wfsys()`:
     - Asserts reset (clears enable bit)
     - Waits 10ms
     - Deasserts reset (sets enable bit)
     - Polls for reset completion (up to 1000ms)

5. **Step 4: DMA Setup**
   - Calls `setup_dma()`:
     - Allocates FWDL ring (128 entries) at queue index 16
     - Allocates firmware buffer (64KB)
     - Resets DMA pointers
     - Configures FWDL ring registers
     - Enables DMA (TX_EN, RX_EN, WB_DDONE)

6. **Step 5: Send Firmware via DMA**
   - Calls `send_fw_chunk()` for each 64KB chunk:
     - Copies firmware data to DMA buffer
     - Sets up descriptor (buf0, buf1, ctrl, info)
     - Updates CPU index (CIDX) to kick DMA
     - Waits 10ms per chunk
   - Reports progress (bytes sent / total size)

7. **Step 6: Check Final State**
   - Reads `MT_WFDMA0_GLO_CFG` to verify DMA state
   - Reads `MT_WFDMA0_HOST_INT_STA` to check for interrupts
   - Reports final register values

### Expected Output

**Success Case:**
```
=== MT7927 Firmware Load Test ===
Step 1: Loading firmware files
  Patch loaded: 1024 bytes
  RAM loaded: 1433600 bytes
Step 2: Power control
Setting up power control...
  Driver control acquired
Step 3: WiFi system reset
Resetting WiFi subsystem...
  Reset complete
Step 4: DMA setup
Setting up DMA...
  DMA configured, GLO_CFG=0x00000045
Step 5: Sending firmware via DMA
  Sent 65536 / 1433600 bytes
  Sent 131072 / 1433600 bytes
  ...
  Sent 1433600 / 1433600 bytes
Step 6: Final state
  GLO_CFG: 0x00000045
  INT_STA: 0x00000000
=== Firmware Load Test Complete ===
Note: Full driver needed for complete initialization
```

**Failure Indicators:**
- Firmware files not found
- Power control timeout
- Reset timeout
- DMA allocation failure
- Descriptor writes fail

### Pass/Fail Conditions

**PASS:**
- All firmware files load successfully
- Power control handshake completes
- WiFi system reset completes
- DMA setup succeeds
- Firmware chunks sent without errors
- Final state shows DMA enabled

**FAIL:**
- Firmware files missing or unreadable
- Power control timeout
- Reset timeout
- DMA setup failure
- Kernel errors during transfer

### How to Run

```bash
# Ensure firmware files are installed
ls /lib/firmware/mediatek/mt7925/
# Should show:
#   WIFI_MT7925_PATCH_MCU_1_1_hdr.bin
#   WIFI_RAM_CODE_MT7925_1_1.bin

# Unload any existing driver
sudo rmmod mt7921e mt7925e mt7927 2>/dev/null

# Build the test module
cd tests/05_dma_impl
make -C /lib/modules/$(uname -r)/build M=$(pwd) modules

# Load the module
sudo insmod test_fw_load.ko

# View results
sudo dmesg | tail -50

# Unload before running other tests
sudo rmmod test_fw_load
```

### Interpreting Results

- **Firmware sizes**: Patch ~1KB, RAM ~1.4MB (typical)
- **Power control acquired**: Required before any other operations
- **Reset complete**: System ready for firmware load
- **DMA configured**: Ring and buffer allocated, registers set
- **Firmware sent**: Chunks transferred via DMA descriptors
- **INT_STA = 0x00000000**: No interrupts yet (normal for test module)
- **Note about full driver**: This test only validates the transfer mechanism; full MCU protocol needed for activation

### Important Notes

- This test validates the DMA transfer mechanism but doesn't activate firmware
- Full driver implementation requires MCU command protocol for firmware activation
- The test uses queue index 16 for FWDL (firmware download queue)
- Each firmware chunk is 64KB, sent via DMA descriptors
- Descriptors use `BIT(30)` in ctrl field to mark last segment

---

## Building and Running

### Prerequisites

- Linux kernel 6.7+ (for MT7925 base support)
- Kernel headers installed: `sudo apt install linux-headers-$(uname -r)`
- MT7927 hardware present: `lspci -nn | grep 14c3:7927`
- Firmware files installed (for `test_fw_load.c`)

### Building All Test Modules

From project root:
```bash
make tests
```

Or from test directory:
```bash
cd tests/05_dma_impl
make -C /lib/modules/$(uname -r)/build M=$(pwd) modules
```

### Running Tests

**Critical:** Only load one test module at a time!

```bash
# 1. Unload any existing drivers
sudo rmmod mt7921e mt7925e mt7927 2>/dev/null

# 2. Load test module
sudo insmod test_power_ctrl.ko

# 3. Check results
sudo dmesg | tail -30

# 4. Unload before loading next test
sudo rmmod test_power_ctrl
```

### Viewing Results

All test modules output detailed information via kernel logs. Use:

```bash
# View last 30 lines
sudo dmesg | tail -30

# View all MT7927-related messages
sudo dmesg | grep -E "mt7927|MT7927"

# Follow logs in real-time (in another terminal)
sudo dmesg -w
```

---

## Test Execution Order

For comprehensive validation, run tests in this order:

1. **test_power_ctrl** - Verify power control mechanism works
   - Validates register remapping
   - Confirms ownership handshake

2. **test_wfsys_reset** - Verify reset functionality
   - Confirms WiFi subsystem can be reset
   - Validates reset timing

3. **test_dma_queues** - Verify DMA configuration
   - Confirms ring allocation works
   - Validates register writability
   - Tests DMA enablement

4. **test_fw_load** - Full integration test
   - Combines all components
   - Tests complete initialization sequence
   - Validates firmware transfer mechanism

### Why This Order?

- **Power control** must work before any other operations
- **Reset** prepares the chip for initialization
- **DMA queues** must be configured before firmware transfer
- **Firmware load** validates the complete sequence

---

## Troubleshooting

### Test Module Won't Load

**Error:** `insmod: ERROR: could not insert module: Device or resource busy`

**Solution:**
```bash
# Check for conflicting drivers
lsmod | grep mt79
sudo rmmod mt7921e mt7925e mt7927 2>/dev/null

# Check if device is already bound
lspci -k | grep -A 3 "14c3:7927"
```

### Chip in Error State

**Symptom:** Registers read as `0xffffffff` or `0x00000000`

**Solution:**
```bash
# Remove and rescan PCI device
DEVICE=$(lspci -nn | grep 14c3:7927 | cut -d' ' -f1 | sed 's/:/\\:/')
echo 1 | sudo tee /sys/bus/pci/devices/0000:$DEVICE/remove
sleep 2
echo 1 | sudo tee /sys/bus/pci/rescan

# Reload test module
sudo insmod test_power_ctrl.ko
```

### Firmware Not Found (test_fw_load)

**Error:** `Failed to load patch: mediatek/mt7925/WIFI_MT7925_PATCH_MCU_1_1_hdr.bin`

**Solution:**
```bash
# Download firmware files
mkdir -p ~/mt7927_firmware
cd ~/mt7927_firmware

wget https://git.kernel.org/pub/scm/linux/kernel/git/firmware/linux-firmware.git/plain/mediatek/mt7925/WIFI_MT7925_PATCH_MCU_1_1_hdr.bin
wget https://git.kernel.org/pub/scm/linux/kernel/git/firmware/linux-firmware.git/plain/mediatek/mt7925/WIFI_RAM_CODE_MT7925_1_1.bin

# Install firmware
sudo mkdir -p /lib/firmware/mediatek/mt7925
sudo cp *.bin /lib/firmware/mediatek/mt7925/
sudo update-initramfs -u
```

### DMA Ring Allocation Fails

**Error:** `Failed to allocate ring` or `Failed to allocate FW buffer`

**Solution:**
- Check available memory: `free -h`
- Reduce ring size in test code if needed
- Check DMA mask: Ensure 32-bit addressing is supported

### Register Writes Don't Persist

**Symptom:** Ring base address reads back as `0x00000000` after write

**Solution:**
- Check `MT_WFDMA0_RST` register - bits 4-5 should be set (0x30)
- Ring registers are only writable when RST bits are set
- See `DEVELOPMENT_LOG.md` Phase 4 for details

### Timeout Errors

**Symptom:** "Timeout waiting for driver ownership" or "Reset timeout"

**Possible Causes:**
- Chip is in error state (registers read 0xffffffff)
- Firmware is not responding
- Hardware issue

**Solution:**
- Reset PCI device (see "Chip in Error State" above)
- Check kernel logs for other errors
- Verify hardware is functioning

---

## Related Documentation

- **DEVELOPMENT_LOG.md**: Complete project history and debugging journey
- **diagnostic_modules_documentation.md**: Documentation for diagnostic modules in `/diag/`
- **dma_mcu_documentation.md**: DMA and MCU implementation details
- **mt7927_pci_documentation.md**: PCI initialization and power management

---

## Summary

These test modules provide isolated validation of each critical component in the MT7927 initialization sequence:

- ✅ **test_power_ctrl**: Power management handshake
- ✅ **test_wfsys_reset**: WiFi subsystem reset
- ✅ **test_dma_queues**: DMA ring configuration
- ✅ **test_fw_load**: Complete firmware loading sequence

Each module can be run independently to validate specific functionality before integration into the full driver. The modules provide detailed kernel log output to aid in debugging and understanding the chip's behavior.
