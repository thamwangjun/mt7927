# MT7927 PCI Driver Documentation

## Table of Contents

1. [Overview](#overview)
2. [Power Management](#power-management)
3. [WiFi System Reset](#wifi-system-reset)
4. [IRQ Handling](#irq-handling)
5. [PCI Driver Interface](#pci-driver-interface)
6. [Register Access Functions](#register-access-functions)
7. [Driver Initialization Flow](#driver-initialization-flow)

---

## Overview

The `mt7927_pci.c` file implements the PCI interface layer for the MediaTek MT7927 WiFi 7 chipset driver. This module handles:

- **PCI device probe and removal**: Device discovery, resource allocation, and cleanup
- **Power management**: Handshake protocol between driver and firmware for power control
- **System reset**: WiFi subsystem reset and initialization
- **Interrupt handling**: Top-half and bottom-half interrupt processing
- **Hardware initialization**: Register mapping, DMA setup, and MCU initialization

The driver follows a layered architecture where the PCI layer provides low-level hardware access, while higher layers (DMA, MCU) handle protocol-specific operations.

---

## Power Management

The MT7927 uses a power management handshake protocol via the `MT_CONN_ON_LPCTL` register. This allows the driver and firmware to coordinate power control ownership, ensuring only one entity manages the chip's power state at a time.

### `mt7927_mcu_fw_pmctrl`

**Purpose**: Release power control to the firmware, allowing the firmware to manage the chip's power state.

**Function Signature**:
```c
int mt7927_mcu_fw_pmctrl(struct mt7927_dev *dev)
```

**Parameters**:
- `dev`: Pointer to the MT7927 device structure

**Return Values**:
- `0`: Success - firmware has acquired power control
- `-ETIMEDOUT`: Timeout waiting for firmware to acknowledge ownership

**Line-by-Line Explanation**:

```c
val = mt7927_rr(dev, MT_CONN_ON_LPCTL);
```
Reads the current state of the Low Power Control register to check ownership status before attempting the handoff.

```c
mt7927_wr(dev, MT_CONN_ON_LPCTL, PCIE_LPCR_HOST_SET_OWN);
```
Writes `SET_OWN` (bit 0) to signal the hardware that firmware should take ownership. This is a write-only control bit that triggers the handoff process.

```c
for (i = 0; i < 2000; i++) {
    val = mt7927_rr(dev, MT_CONN_ON_LPCTL);
    if (val & PCIE_LPCR_HOST_OWN_SYNC) {
        return 0;
    }
    usleep_range(500, 1000);
}
```
Polls the `OWN_SYNC` bit (bit 2) up to 2000 times with 500-1000μs delays. When `OWN_SYNC` is set, it indicates firmware has successfully acquired ownership. The timeout is approximately 1-2 seconds total.

**Hardware Interaction**:
- The `MT_CONN_ON_LPCTL` register is located at logical address `0x7c060010` (CONN_INFRA region)
- The register uses a write-to-set/clear protocol: writing `SET_OWN` doesn't directly set a bit, but triggers a state machine
- `OWN_SYNC` is a read-only status bit that reflects the current owner

**Usage Context**:
Called during driver initialization before firmware loading, and potentially during suspend/resume operations. The function may fail on first initialization if firmware isn't ready, which is handled gracefully.

---

### `mt7927_mcu_drv_pmctrl`

**Purpose**: Acquire power control from firmware, allowing the driver to manage the chip's power state.

**Function Signature**:
```c
int mt7927_mcu_drv_pmctrl(struct mt7927_dev *dev)
```

**Parameters**:
- `dev`: Pointer to the MT7927 device structure

**Return Values**:
- `0`: Success - driver has acquired power control
- `-ETIMEDOUT`: Timeout waiting for driver ownership acknowledgment

**Line-by-Line Explanation**:

```c
val = mt7927_rr(dev, MT_CONN_ON_LPCTL);
dev_info(dev->dev, "LPCTL before drv_pmctrl: 0x%08x\n", val);
```
Reads and logs the current ownership state for debugging purposes.

```c
if (!(val & PCIE_LPCR_HOST_OWN_SYNC)) {
    dev_info(dev->dev, "Driver already owns chip\n");
    return 0;
}
```
Early return optimization: if `OWN_SYNC` is already clear (driver owns), skip the handoff process. This prevents unnecessary register writes and polling.

```c
mt7927_wr(dev, MT_CONN_ON_LPCTL, PCIE_LPCR_HOST_CLR_OWN);
```
Writes `CLR_OWN` (bit 1) to claim ownership for the driver. This signals the hardware that the driver wants to take control.

```c
for (i = 0; i < 2000; i++) {
    val = mt7927_rr(dev, MT_CONN_ON_LPCTL);
    if (!(val & PCIE_LPCR_HOST_OWN_SYNC)) {
        return 0;
    }
    usleep_range(500, 1000);
}
```
Polls until `OWN_SYNC` clears, indicating driver ownership. The loop waits up to 1-2 seconds for firmware to release control.

**Hardware Interaction**:
- Similar to `fw_pmctrl`, but uses `CLR_OWN` instead of `SET_OWN`
- The firmware must cooperatively release ownership; if firmware is hung, this will timeout
- Used before hardware operations that require driver control (reset, register access, DMA configuration)

**Usage Context**:
Called during driver initialization after attempting to release to firmware, and before performing hardware resets or DMA operations. Essential for ensuring the driver has exclusive control during critical operations.

---

## WiFi System Reset

The WiFi subsystem reset prepares the chip for firmware loading by resetting both the hardware state machine and firmware execution environment.

### `mt7927_wfsys_reset`

**Purpose**: Reset the WiFi subsystem to a clean state, preparing it for firmware loading and initialization.

**Function Signature**:
```c
int mt7927_wfsys_reset(struct mt7927_dev *dev)
```

**Parameters**:
- `dev`: Pointer to the MT7927 device structure

**Return Values**:
- `0`: Success - WiFi subsystem reset complete
- `-ETIMEDOUT`: Timeout waiting for initialization to complete

**Line-by-Line Explanation**:

```c
val = mt7927_rr(dev, MT_WFSYS_SW_RST_B);
dev_info(dev->dev, "WFSYS_SW_RST_B before: 0x%08x\n", val);
```
Reads the WiFi System Software Reset register (`0x7c000140`) to check current state. This register controls the reset state machine.

```c
mt7927_clear(dev, MT_WFSYS_SW_RST_B, MT_WFSYS_SW_RST_B_EN);
```
Clears bit 0 (`MT_WFSYS_SW_RST_B_EN`) to assert reset. This puts the WiFi subsystem into reset state, halting all operations.

```c
msleep(50);
```
Waits 50ms for the reset to propagate through the hardware. This delay ensures all state machines and flip-flops have stabilized in the reset state.

```c
mt7927_set(dev, MT_WFSYS_SW_RST_B, MT_WFSYS_SW_RST_B_EN);
```
Sets bit 0 to deassert reset, allowing the WiFi subsystem to begin initialization. The hardware will start its internal boot sequence.

```c
for (i = 0; i < 50; i++) {
    val = mt7927_rr(dev, MT_WFSYS_SW_RST_B);
    if (val & MT_WFSYS_SW_INIT_DONE) {
        dev_info(dev->dev, "WiFi subsystem reset complete (0x%08x after %dms)\n",
                 val, i * 10);
        return 0;
    }
    msleep(10);
}
```
Polls bit 4 (`MT_WFSYS_SW_INIT_DONE`) up to 50 times with 10ms delays (500ms total timeout). When set, this bit indicates the WiFi subsystem has completed its internal initialization and is ready for firmware loading.

**Hardware Interaction**:
- The `MT_WFSYS_SW_RST_B` register is in the CONN_INFRA region at `0x7c000140`
- Bit 0 controls reset assertion/deassertion
- Bit 4 (`INIT_DONE`) is a status bit set by hardware when initialization completes
- The reset sequence must complete before firmware can be loaded

**Usage Context**:
Called during driver probe after acquiring power control. This reset is essential before loading firmware, as it ensures the chip starts from a known state. The function may timeout if hardware is malfunctioning, but the driver continues with a warning.

---

### `mt7927_wpdma_reset`

**Purpose**: Reset the WPDMA (WiFi Packet DMA) engine, clearing DMA pointers and preparing for DMA reinitialization.

**Function Signature**:
```c
int mt7927_wpdma_reset(struct mt7927_dev *dev, bool force)
```

**Parameters**:
- `dev`: Pointer to the MT7927 device structure
- `force`: If true, reset even if DMA is busy; if false, fail if DMA cannot be disabled cleanly

**Return Values**:
- `0`: Success - DMA reset complete
- Negative error code: Failed to disable DMA (only if `force` is false)

**Line-by-Line Explanation**:

```c
ret = mt7927_dma_disable(dev, force);
if (ret && !force)
    return ret;
```
Disables DMA operations first. If disabling fails and `force` is false, return the error. If `force` is true, continue with reset anyway (useful for recovery scenarios).

```c
mt7927_wr(dev, MT_WFDMA0_RST_DTX_PTR, 0xffffffff);
```
Writes `0xffffffff` to the TX DMA pointer reset register (`MT_WFDMA0(0x20c)`). This resets all TX ring pointers to their initial state.

```c
mt7927_wr(dev, MT_WFDMA0_RST_DRX_PTR, 0xffffffff);
```
Similarly resets RX DMA pointers via `MT_WFDMA0(0x280)`. Both TX and RX rings are reset to ensure clean state.

```c
usleep_range(100, 200);
```
Short delay to allow hardware to process the pointer resets before continuing.

**Hardware Interaction**:
- These registers are in the WFDMA HOST DMA0 region at BAR0+0xd4000
- Writing `0xffffffff` triggers hardware to reset internal DMA pointers
- Must be called after disabling DMA to avoid race conditions

**Usage Context**:
Called during DMA initialization or error recovery. Used to reset DMA state when reinitializing queues or recovering from DMA errors.

---

## IRQ Handling

The MT7927 uses a two-level interrupt handling scheme: a top-half handler (`mt7927_irq_handler`) that quickly acknowledges interrupts and schedules a bottom-half tasklet (`mt7927_irq_tasklet`) for deferred processing.

### `mt7927_irq_enable` / `mt7927_irq_disable`

**Purpose**: Enable or disable specific interrupt sources.

**Function Signatures**:
```c
void mt7927_irq_enable(struct mt7927_dev *dev, u32 mask)
void mt7927_irq_disable(struct mt7927_dev *dev, u32 mask)
```

**Parameters**:
- `dev`: Pointer to the MT7927 device structure
- `mask`: Bitmask of interrupt bits to enable/disable

**Return Values**: None

**Line-by-Line Explanation**:

```c
mt7927_set(dev, dev->irq_map->host_irq_enable, mask);
```
For `irq_enable`: Sets bits in `MT_WFDMA0_HOST_INT_ENA` register to enable interrupts. The mask can contain multiple interrupt types (TX done, RX done, MCU command, etc.).

```c
mt7927_clear(dev, dev->irq_map->host_irq_enable, mask);
```
For `irq_disable`: Clears bits in the interrupt enable register to disable specific interrupts.

**Hardware Interaction**:
- `MT_WFDMA0_HOST_INT_ENA` is at `MT_WFDMA0(0x204)` (BAR0+0x2204)
- Each bit corresponds to a specific interrupt source
- Setting/clearing bits immediately affects interrupt generation

**Usage Context**:
- `irq_enable`: Called after processing interrupts to re-enable them, or during initialization to enable specific interrupt types
- `irq_disable`: Called before critical sections or during shutdown to prevent interrupts

---

### `mt7927_irq_handler`

**Purpose**: Top-half interrupt handler that quickly acknowledges interrupts and schedules deferred processing.

**Function Signature**:
```c
irqreturn_t mt7927_irq_handler(int irq, void *dev_instance)
```

**Parameters**:
- `irq`: Linux IRQ number (unused, but required by kernel API)
- `dev_instance`: Pointer to `struct mt7927_dev` passed during IRQ registration

**Return Values**:
- `IRQ_HANDLED`: Interrupt was for this device
- `IRQ_NONE`: Interrupt was not for this device (spurious interrupt)

**Line-by-Line Explanation**:

```c
struct mt7927_dev *dev = dev_instance;
u32 intr;
```
Extracts the device pointer from the callback argument.

```c
intr = mt7927_rr(dev, MT_WFDMA0_HOST_INT_STA);
if (!intr)
    return IRQ_NONE;
```
Reads the interrupt status register (`MT_WFDMA0(0x200)`). If no interrupts are pending, return `IRQ_NONE` to indicate this wasn't our interrupt (spurious interrupt handling).

```c
mt7927_wr(dev, dev->irq_map->host_irq_enable, 0);
```
Disables all interrupts immediately to prevent interrupt storms. This is a critical step - if interrupts fire faster than they can be processed, the system can hang.

```c
tasklet_schedule(&dev->irq_tasklet);
```
Schedules the bottom-half tasklet for deferred interrupt processing. The tasklet runs in softirq context and can safely sleep or perform longer operations.

```c
return IRQ_HANDLED;
```
Returns `IRQ_HANDLED` to inform the kernel that this interrupt was successfully handled.

**Hardware Interaction**:
- Must read `MT_WFDMA0_HOST_INT_STA` to check interrupt status
- Disabling interrupts prevents re-entry while processing
- The tasklet will re-enable interrupts after processing completes

**Usage Context**:
Registered with `request_irq()` during PCI probe. Called by the kernel's interrupt subsystem whenever the PCI device asserts an interrupt line. Must execute quickly to avoid delaying other interrupts.

---

### `mt7927_irq_tasklet`

**Purpose**: Bottom-half interrupt handler that processes interrupt types and handles TX/RX completion.

**Function Signature**:
```c
void mt7927_irq_tasklet(unsigned long data)
```

**Parameters**:
- `data`: Cast to `struct mt7927_dev *` (legacy tasklet API)

**Return Values**: None (void function)

**Line-by-Line Explanation**:

```c
struct mt7927_dev *dev = (struct mt7927_dev *)data;
u32 intr, mask;
```
Extracts device pointer from the tasklet data parameter.

```c
intr = mt7927_rr(dev, MT_WFDMA0_HOST_INT_STA);
mt7927_wr(dev, MT_WFDMA0_HOST_INT_STA, intr);
```
Reads interrupt status and writes it back to acknowledge/clear interrupts. Writing the status register clears the interrupt bits (write-1-to-clear semantics).

```c
dev_info(dev->dev, "IRQ tasklet: intr=0x%08x\n", intr);
```
Logs the interrupt status for debugging. In production, this might be rate-limited or removed.

```c
if (!intr)
    return;
```
Early return if no interrupts are pending (shouldn't happen, but defensive programming).

```c
if (intr & dev->irq_map->tx.all_complete_mask) {
    if (intr & MT_INT_TX_DONE_BAND0)
        mt7927_tx_complete(dev, &dev->tx_q[0]);
    
    if (intr & HOST_TX_DONE_INT_ENA4)
        mt7927_tx_complete(dev, &dev->tx_q[1]);
    
    if (intr & HOST_TX_DONE_INT_ENA5)
        mt7927_tx_complete(dev, &dev->tx_q[2]);
}
```
Processes TX completion interrupts:
- `MT_INT_TX_DONE_BAND0` (bit 0): Data TX queue 0 completion
- `HOST_TX_DONE_INT_ENA4` (bit 4): Firmware download queue completion
- `HOST_TX_DONE_INT_ENA5` (bit 5): MCU command queue completion

Each calls `mt7927_tx_complete()` which processes completed descriptors, frees buffers, and updates queue indices.

```c
if (intr & dev->irq_map->rx.wm_complete_mask) {
    mt7927_rx_poll(dev, &dev->rx_q[MT7927_RXQ_MCU_WM], 16);
}
```
Processes MCU RX interrupts (`HOST_RX_DONE_INT_ENA0`, bit 16). Polls the MCU WM RX queue with a budget of 16 packets to process firmware responses.

```c
if (intr & dev->irq_map->rx.data_complete_mask) {
    mt7927_rx_poll(dev, &dev->rx_q[MT7927_RXQ_BAND0], 64);
}
```
Processes data RX interrupts (`HOST_RX_DONE_INT_ENA2`, bit 18). Polls the Band0 data RX queue with a budget of 64 packets for WiFi data frames.

```c
if (intr & MT_INT_MCU_CMD) {
    wake_up(&dev->mcu.wait);
}
```
Handles MCU command notification (bit 29). Wakes up any threads waiting for MCU responses (used by synchronous MCU command functions).

```c
mask = dev->irq_map->tx.all_complete_mask |
       MT_INT_RX_DONE_ALL |
       MT_INT_MCU_CMD;
mt7927_irq_enable(dev, mask);
```
Re-enables interrupts after processing completes. The mask includes all interrupt types that were disabled in the top-half handler.

**Hardware Interaction**:
- Interrupt status register uses write-1-to-clear semantics
- Each interrupt bit corresponds to a specific hardware event
- Processing must complete before re-enabling interrupts to avoid missing events

**Usage Context**:
Scheduled by `mt7927_irq_handler` whenever interrupts occur. Runs in softirq context, so it can perform longer operations than the top-half handler. Must complete processing and re-enable interrupts before returning.

---

## PCI Driver Interface

The PCI driver interface handles device discovery, resource allocation, initialization sequencing, and cleanup.

### `mt7927_pci_probe`

**Purpose**: Initialize the MT7927 PCI device when discovered by the kernel.

**Function Signature**:
```c
static int mt7927_pci_probe(struct pci_dev *pdev, const struct pci_device_id *id)
```

**Parameters**:
- `pdev`: Pointer to the PCI device structure
- `id`: Pointer to matching PCI device ID entry (unused)

**Return Values**:
- `0`: Success - device initialized
- Negative error code: Initialization failed

**Line-by-Line Explanation**:

#### Device Structure Allocation

```c
dev = devm_kzalloc(&pdev->dev, sizeof(*dev), GFP_KERNEL);
if (!dev)
    return -ENOMEM;
```
Allocates the device structure using `devm_kzalloc` (managed allocation that auto-frees on device removal). Initializes all fields to zero.

```c
dev->pdev = pdev;
dev->dev = &pdev->dev;
dev->irq_map = &mt7927_irq_map;
pci_set_drvdata(pdev, dev);
```
Stores PCI device pointer, generic device pointer, and IRQ map. `pci_set_drvdata()` associates the device structure with the PCI device for later retrieval.

```c
spin_lock_init(&dev->lock);
mutex_init(&dev->mutex);
```
Initializes synchronization primitives: spinlock for interrupt-safe access, mutex for sleepable operations.

```c
skb_queue_head_init(&dev->mcu.res_q);
init_waitqueue_head(&dev->mcu.wait);
dev->mcu.timeout = 3 * HZ;
```
Initializes MCU communication structures:
- SKB queue for MCU response packets
- Wait queue for threads waiting for MCU responses
- Timeout set to 3 seconds (3 * HZ jiffies)

#### PCI Resource Setup

```c
ret = pcim_enable_device(pdev);
if (ret) {
    dev_err(&pdev->dev, "Failed to enable PCI device\n");
    return ret;
}
```
Enables the PCI device using `pcim_enable_device()` (managed version). This enables I/O and memory space decoding.

```c
ret = pcim_iomap_regions(pdev, BIT(0) | BIT(2), pci_name(pdev));
if (ret) {
    dev_err(&pdev->dev, "Failed to request PCI regions\n");
    return ret;
}
```
Maps PCI BAR regions:
- `BIT(0)`: BAR0 (2MB memory region - main register space)
- `BIT(2)`: BAR2 (32KB memory region - read-only shadow)
- Uses `pcim_iomap_regions()` for managed mapping (auto-unmapped on removal)

```c
pci_read_config_word(pdev, PCI_COMMAND, &cmd);
if (!(cmd & PCI_COMMAND_MEMORY)) {
    cmd |= PCI_COMMAND_MEMORY;
    pci_write_config_word(pdev, PCI_COMMAND, cmd);
}
```
Ensures memory access is enabled in PCI command register. Some systems may have this disabled initially.

```c
pci_set_master(pdev);
```
Enables bus mastering for DMA operations. Required for the device to perform DMA transfers.

```c
ret = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));
if (ret) {
    dev_err(&pdev->dev, "Failed to set DMA mask\n");
    return ret;
}
```
Sets DMA addressing capability to 32-bit. The MT7927 uses 32-bit DMA addresses, so 64-bit addressing isn't needed.

```c
ret = pci_alloc_irq_vectors(pdev, 1, 1, PCI_IRQ_ALL_TYPES);
if (ret < 0) {
    dev_err(&pdev->dev, "Failed to allocate IRQ vectors\n");
    return ret;
}
dev->irq = pci_irq_vector(pdev, 0);
```
Allocates a single MSI/MSI-X interrupt vector. Falls back to legacy INTx if MSI isn't available. Retrieves the Linux IRQ number.

```c
dev->mem = pcim_iomap_table(pdev)[0];   /* BAR0: Memory */
dev->regs = pcim_iomap_table(pdev)[2];  /* BAR2: Registers */
```
Stores pointers to mapped BAR regions:
- `dev->mem`: BAR0 (2MB) - primary register space, always use this for writes
- `dev->regs`: BAR2 (32KB) - read-only shadow, mainly for debugging

#### Hardware Initialization

```c
tasklet_init(&dev->irq_tasklet, mt7927_irq_tasklet, (unsigned long)dev);
```
Initializes the interrupt tasklet with the bottom-half handler function.

```c
dev->chip_id = mt7927_rr(dev, MT_HW_CHIPID);
dev->hw_rev = mt7927_rr(dev, MT_HW_REV) & 0xff;
dev_info(&pdev->dev, "Chip ID: 0x%08x, HW Rev: 0x%02x\n",
         dev->chip_id, dev->hw_rev);
```
Reads chip identification registers:
- `MT_HW_CHIPID` (offset 0x0000): Chip model identifier
- `MT_HW_REV` (offset 0x0004): Hardware revision (lower 8 bits)

```c
mt7927_rmw_field(dev, MT_HW_EMI_CTL, MT_HW_EMI_CTL_SLPPROT_EN, 1);
```
Enables sleep protection in EMI (External Memory Interface) control register. This prevents memory corruption during sleep states.

#### Power Management Sequence

```c
ret = mt7927_mcu_fw_pmctrl(dev);
if (ret) {
    dev_warn(&pdev->dev, "FW power control failed (may be expected)\n");
}
```
Step 1: Attempt to release power control to firmware. This may fail on first initialization (firmware not loaded yet), which is expected and handled gracefully.

```c
ret = mt7927_mcu_drv_pmctrl(dev);
if (ret) {
    dev_warn(&pdev->dev, "Driver power control failed (may be expected)\n");
}
```
Step 2: Acquire power control for the driver. This ensures the driver has exclusive control for hardware operations.

```c
ret = mt7927_wfsys_reset(dev);
if (ret) {
    dev_warn(&pdev->dev, "WiFi reset failed, continuing...\n");
}
```
Step 3: Reset the WiFi subsystem to prepare for firmware loading.

#### Interrupt Setup

```c
mt7927_wr(dev, dev->irq_map->host_irq_enable, 0);
mt7927_wr(dev, MT_PCIE_MAC_INT_ENABLE, 0xff);
```
Disables all WFDMA interrupts initially. Enables PCIe MAC interrupts (for link state changes, etc.).

```c
ret = request_irq(dev->irq, mt7927_irq_handler,
                  IRQF_SHARED, "mt7927", dev);
if (ret) {
    dev_err(&pdev->dev, "Failed to request IRQ %d\n", dev->irq);
    goto err_tasklet;
}
```
Registers the interrupt handler. Uses `IRQF_SHARED` flag (though MT7927 typically uses dedicated interrupts). On failure, jumps to cleanup.

#### DMA and MCU Initialization

```c
ret = mt7927_dma_init(dev);
if (ret) {
    dev_err(&pdev->dev, "DMA initialization failed\n");
    goto err_free_irq;
}
```
Step 4: Initialize DMA queues (TX and RX rings). Allocates descriptor rings and buffers.

```c
ret = mt7927_mcu_init(dev);
if (ret) {
    dev_err(&pdev->dev, "MCU initialization failed\n");
    goto err_dma;
}
```
Step 5: Initialize MCU and load firmware. This loads the ROM patch and RAM code, then starts the firmware.

```c
set_bit(MT7927_STATE_INITIALIZED, &dev->state);
dev->hw_init_done = true;
```
Marks the device as initialized. Other driver components can check these flags before accessing hardware.

#### Error Handling

```c
err_dma:
    mt7927_dma_cleanup(dev);
err_free_irq:
    mt7927_wr(dev, dev->irq_map->host_irq_enable, 0);
    synchronize_irq(dev->irq);
    free_irq(dev->irq, dev);
err_tasklet:
    tasklet_kill(&dev->irq_tasklet);
err_free_irq_vectors:
    pci_free_irq_vectors(pdev);
    return ret;
```
Cleanup on error: releases resources in reverse order of allocation. `synchronize_irq()` ensures no interrupt handlers are running before freeing the IRQ.

**Hardware Interaction**:
- Accesses multiple PCI configuration registers
- Maps and accesses PCI BAR memory regions
- Performs power management handshake
- Resets WiFi subsystem
- Configures interrupt system

**Usage Context**:
Called by the Linux PCI subsystem when a matching device is found (`pci_device_id` match). Must handle all error paths cleanly to avoid resource leaks.

---

### `mt7927_pci_remove`

**Purpose**: Clean up and remove the MT7927 PCI device.

**Function Signature**:
```c
static void mt7927_pci_remove(struct pci_dev *pdev)
```

**Parameters**:
- `pdev`: Pointer to the PCI device structure

**Return Values**: None (void function)

**Line-by-Line Explanation**:

```c
struct mt7927_dev *dev = pci_get_drvdata(pdev);
```
Retrieves the device structure associated with the PCI device.

```c
mt7927_wr(dev, dev->irq_map->host_irq_enable, 0);
```
Disables all interrupts to prevent new interrupts during shutdown.

```c
mt7927_mcu_exit(dev);
```
Shuts down MCU communication, stops firmware, and cleans up MCU state.

```c
mt7927_dma_cleanup(dev);
```
Frees all DMA queues, descriptor rings, and buffers.

```c
free_irq(dev->irq, dev);
```
Frees the interrupt handler. Must be called before `pci_free_irq_vectors()`.

```c
tasklet_kill(&dev->irq_tasklet);
```
Kills the tasklet to ensure it's not running during cleanup.

```c
pci_free_irq_vectors(pdev);
```
Frees the IRQ vector allocation.

```c
if (dev->fw_ram)
    release_firmware(dev->fw_ram);
if (dev->fw_patch)
    release_firmware(dev->fw_patch);
```
Releases firmware images loaded from the filesystem.

**Hardware Interaction**:
- Disables interrupts at hardware level
- Stops MCU and firmware
- Cleans up DMA state

**Usage Context**:
Called by the Linux PCI subsystem when the device is removed (hot-unplug) or during module unload. Must clean up all resources to prevent leaks.

---

### `mt7927_pci_shutdown`

**Purpose**: Handle system shutdown by performing the same cleanup as remove.

**Function Signature**:
```c
static void mt7927_pci_shutdown(struct pci_dev *pdev)
```

**Parameters**:
- `pdev`: Pointer to the PCI device structure

**Return Values**: None

**Implementation**:
```c
mt7927_pci_remove(pdev);
```
Simply calls the remove function to perform cleanup during system shutdown.

**Usage Context**:
Called during system shutdown to ensure the device is properly stopped before power-off.

---

## Register Access Functions

The driver uses a sophisticated register remapping system to access registers across different address spaces. These functions are defined in `mt7927.h` but are critical to understanding PCI driver operations.

### Register Address Translation

The MT7927 uses multiple address spaces that must be translated to BAR0 offsets:

1. **Direct addresses** (< 0x200000): Mapped directly to BAR0
2. **Fixed mappings**: Common register regions mapped via a lookup table
3. **L1/L2 remapping**: Dynamic remapping for less common regions

### `mt7927_rr` / `mt7927_wr`

**Purpose**: Read/write registers with automatic address translation.

**Function Signatures**:
```c
static inline u32 mt7927_rr(struct mt7927_dev *dev, u32 offset)
static inline void mt7927_wr(struct mt7927_dev *dev, u32 offset, u32 val)
```

**Parameters**:
- `dev`: Device structure
- `offset`: Logical register address
- `val`: Value to write (write only)

**Implementation Flow**:

1. `mt7927_reg_addr()` translates logical address to BAR0 offset
2. Accesses `dev->mem + addr` (always uses BAR0, never BAR2)
3. Uses `readl()`/`writel()` for memory-mapped I/O

**Key Points**:
- **Always uses BAR0** (`dev->mem`) for register access
- BAR2 (`dev->regs`) is read-only and should not be used for control writes
- Address translation handles CONN_INFRA, WFDMA, and other regions automatically
- Remapping state is saved/restored to avoid conflicts

**Usage Context**:
Used throughout the driver for all register access. The translation is transparent to most code - you specify the logical address and the function handles the mapping.

---

## Driver Initialization Flow

The complete initialization sequence follows this order:

1. **PCI Discovery**: Kernel finds device, calls `mt7927_pci_probe()`
2. **Resource Allocation**: Allocate device structure, map BARs, allocate IRQ
3. **Hardware Setup**: Read chip ID, enable EMI sleep protection
4. **Power Management**: Release to FW → Acquire for driver
5. **System Reset**: Reset WiFi subsystem, wait for INIT_DONE
6. **Interrupt Setup**: Disable interrupts, register handler, initialize tasklet
7. **DMA Initialization**: Allocate TX/RX rings, configure hardware
8. **MCU Initialization**: Load firmware (patch + RAM), start MCU
9. **Completion**: Mark device initialized, enable interrupts

### Error Recovery

If any step fails:
- Clean up in reverse order
- Free IRQ before freeing vectors
- Kill tasklet before freeing device structure
- Use managed allocations (`devm_*`) where possible for automatic cleanup

### Power Management Flow

```
Driver Start
    ↓
fw_pmctrl() → Release to firmware (may fail on first init)
    ↓
drv_pmctrl() → Acquire for driver (required)
    ↓
wfsys_reset() → Reset hardware
    ↓
[Hardware operations]
    ↓
fw_pmctrl() → Release to firmware (on suspend/exit)
```

---

## MediaTek-Specific Hardware Interactions

### CONN_INFRA Region

The CONN_INFRA (Connection Infrastructure) region contains:
- **Power management** (`0x7c060010`): LPCTL register for power handshake
- **WiFi system reset** (`0x7c000140`): WFSYS reset control
- **Miscellaneous control** (`0x7c0600f0`): Firmware status, etc.

These registers are accessed via the fixed mapping table, which maps `0x7c000000-0x7c0fffff` to `BAR0+0xf0000`.

### WFDMA Region

The WFDMA HOST DMA0 region at `BAR0+0xd4000` (chip address 0x7C024000) contains:
- **Interrupt registers**: Status, enable, disable
- **Ring configuration**: Base addresses, counts, indices for TX rings 0-20 and RX rings 0-11
- **DMA control**: Reset, global configuration

**Critical**: Always use `BAR0+0xd4000+offset` for WFDMA writes. `BAR2` (which maps to `BAR0+0x10000`) is read-only.

### Register Remapping

For addresses not in the fixed map:
- **L1 remapping**: Used for `0x18000000-0x18c00000`, `0x70000000-0x78000000`, `0x7c000000-0x7c400000`
- **L2 remapping**: Used for other addresses via two-level remapping

The remapping system saves/restores state to avoid conflicts when accessing different regions.

---

## Summary

The `mt7927_pci.c` file provides the foundation for the MT7927 WiFi driver by:

1. **Managing PCI resources**: BAR mapping, IRQ allocation, DMA setup
2. **Coordinating power control**: Handshake protocol with firmware
3. **Resetting hardware**: WiFi subsystem initialization
4. **Handling interrupts**: Efficient two-level interrupt processing
5. **Initializing subsystems**: DMA and MCU setup in correct order

Understanding these functions is essential for debugging hardware issues, adding features, or porting to similar MediaTek chipsets. The driver follows Linux kernel best practices with proper error handling, resource management, and hardware abstraction.
