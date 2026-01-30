# MT7927 Driver Header Files Documentation

This document provides comprehensive reference documentation for the main header files of the MT7927 WiFi 7 Linux driver.

## Table of Contents

1. [mt7927.h - Main Device Header](#mt7927h---main-device-header)
2. [mt7927_regs.h - Register Definitions](#mt7927_regsh---register-definitions)
3. [mt7927_mcu.h - MCU Protocol Definitions](#mt7927_mcuh---mcu-protocol-definitions)

---

# mt7927.h - Main Device Header

## File Overview

`mt7927.h` is the primary header file for the MT7927 driver. It defines the core device structures, DMA descriptors, queue management, register access functions, and function declarations for all major driver subsystems.

**Key Components:**
- Device identification constants
- DMA descriptor and queue structures
- Main device structure (`mt7927_dev`)
- Register access functions with address translation
- Function declarations for PCI, DMA, MCU, and IRQ subsystems

---

## Device Identification Macros

### `MT7927_VENDOR_ID`
- **Value:** `0x14c3`
- **Description:** PCI vendor ID for MediaTek devices

### `MT7927_DEVICE_ID`
- **Value:** `0x7927`
- **Description:** PCI device ID for the MT7927 chip

### `MT7927_FIRMWARE_WM`
- **Value:** `"mediatek/mt7925/WIFI_RAM_CODE_MT7925_1_1.bin"`
- **Description:** Path to the WiFi RAM code firmware file (WM = Wireless Manager)

### `MT7927_ROM_PATCH`
- **Value:** `"mediatek/mt7925/WIFI_MT7925_PATCH_MCU_1_1_hdr.bin"`
- **Description:** Path to the ROM patch firmware file for MCU initialization

---

## Structures

### `struct mt7927_desc`

DMA descriptor structure used for both TX and RX operations.

**Fields:**
- `__le32 buf0` - Buffer pointer (low 32 bits). Physical address of the data buffer for DMA transfer.
- `__le32 ctrl` - Control word containing:
  - Length of the data segment
  - Last segment flag
  - DMA done status
- `__le32 buf1` - Buffer pointer (high 32 bits). Used for 64-bit DMA addressing.
- `__le32 info` - Additional information field. Contains metadata about the descriptor.

**Alignment:** `__packed __aligned(4)` - Packed structure with 4-byte alignment

---

### `struct mt7927_queue`

DMA queue structure managing a ring buffer for TX or RX operations.

**Fields:**
- `struct mt7927_desc *desc` - Pointer to the descriptor ring buffer array
- `dma_addr_t desc_dma` - DMA address of the descriptor ring (for hardware access)
- `int ndesc` - Number of descriptors in the ring
- `struct sk_buff **skb` - Array of socket buffer pointers, one per descriptor
- `dma_addr_t *dma_addr` - Array of DMA addresses for each socket buffer
- `int head` - CPU write index. Points to the next descriptor the driver will write to.
- `int tail` - DMA read index. Points to the next descriptor the hardware will process (read from hardware register).
- `int hw_idx` - Hardware queue index. Identifies which hardware queue this structure represents.
- `bool stopped` - Queue stopped flag. When true, the queue is paused and not processing packets.
- `spinlock_t lock` - Spinlock protecting queue access from concurrent operations

---

### `struct mt7927_irq_map`

Interrupt mapping structure defining which interrupts are enabled and their masks.

**Fields:**
- `u32 host_irq_enable` - Overall host interrupt enable mask
- `struct { ... } tx` - TX interrupt masks:
  - `u32 all_complete_mask` - Mask for all TX completion interrupts
  - `u32 mcu_complete_mask` - Mask for MCU TX completion interrupts
- `struct { ... } rx` - RX interrupt masks:
  - `u32 data_complete_mask` - Mask for data RX completion interrupts
  - `u32 wm_complete_mask` - Mask for WM (Wireless Manager) RX completion interrupts
  - `u32 wm2_complete_mask` - Mask for WM2 RX completion interrupts

---

### `struct mt7927_dev`

Main device structure containing all driver state and hardware references.

**Fields:**

**PCI and Device:**
- `struct pci_dev *pdev` - Pointer to the PCI device structure
- `struct device *dev` - Pointer to the generic device structure

**Memory Mapped I/O:**
- `void __iomem *regs` - BAR2: Control registers (read-only shadow, not used for writes)
- `void __iomem *mem` - BAR0: Memory region (2MB, contains full register space)

**Register Remapping:**
- `u32 backup_l1` - Backup of L1 remapping register before modification
- `u32 backup_l2` - Backup of L2 remapping register before modification

**DMA Queues:**
- `struct mt7927_queue tx_q[4]` - Array of 4 TX queues for data transmission
- `struct mt7927_queue rx_q[4]` - Array of 4 RX queues for data reception
- `struct mt7927_queue *q_mcu[__MT_MCUQ_MAX]` - Array of MCU queue pointers (WM, WA, FWDL)

**Firmware:**
- `const struct firmware *fw_ram` - Loaded RAM code firmware
- `const struct firmware *fw_patch` - Loaded patch firmware

**MCU Communication:**
- `struct { ... } mcu` - MCU subsystem state:
  - `struct sk_buff_head res_q` - Response queue for MCU messages
  - `wait_queue_head_t wait` - Wait queue for blocking MCU responses
  - `u32 timeout` - MCU response timeout in jiffies
  - `u8 seq` - Sequence number for MCU command tracking
  - `enum mt7927_mcu_state state` - Current MCU state

**IRQ Handling:**
- `struct tasklet_struct irq_tasklet` - Bottom-half tasklet for interrupt processing
- `const struct mt7927_irq_map *irq_map` - Pointer to interrupt mapping configuration
- `int irq` - IRQ number assigned to the device

**Hardware Information:**
- `u32 chip_id` - Chip identification register value
- `u32 hw_rev` - Hardware revision number
- `u32 fw_ver` - Firmware version number

**State Flags:**
- `unsigned long state` - Device state flags (see state flag macros below)
- `bool hw_init_done` - Flag indicating hardware initialization is complete
- `bool fw_assert` - Flag indicating firmware assertion/error state

**Work Structures:**
- `struct work_struct reset_work` - Work queue item for device reset
- `struct work_struct init_work` - Work queue item for device initialization

**Synchronization:**
- `spinlock_t lock` - Spinlock for device-level critical sections
- `struct mutex mutex` - Mutex for device-level synchronization

---

## Enums

### `enum mt7927_mcu_state`

MCU (Microcontroller Unit) state machine states.

**Values:**
- `MT7927_MCU_STATE_INIT` (0) - Initial state, MCU not initialized
- `MT7927_MCU_STATE_FW_LOADED` - Firmware has been loaded into MCU memory
- `MT7927_MCU_STATE_RUNNING` - MCU is running and ready to process commands
- `MT7927_MCU_STATE_ERROR` - MCU is in an error state

---

## State Flag Macros

### `MT7927_STATE_INITIALIZED`
- **Bit:** `BIT(0)`
- **Description:** Device has been initialized

### `MT7927_STATE_MCU_RUNNING`
- **Bit:** `BIT(1)`
- **Description:** MCU is running and operational

### `MT7927_STATE_RESET`
- **Bit:** `BIT(2)`
- **Description:** Device is in reset state

---

## Register Access Functions

### `mt7927_rr_raw()`

Raw register read without address translation.

```c
static inline u32 mt7927_rr_raw(struct mt7927_dev *dev, u32 offset)
```

**Parameters:**
- `dev` - Device structure pointer
- `offset` - Register offset in BAR0 address space

**Returns:** 32-bit register value

**Description:** Always uses BAR0 (mem) which contains the full 2MB register space. No address translation is performed.

---

### `mt7927_wr_raw()`

Raw register write without address translation.

```c
static inline void mt7927_wr_raw(struct mt7927_dev *dev, u32 offset, u32 val)
```

**Parameters:**
- `dev` - Device structure pointer
- `offset` - Register offset in BAR0 address space
- `val` - 32-bit value to write

**Description:** Always uses BAR0 (mem) which contains the full 2MB register space. No address translation is performed.

---

### `mt7927_reg_map_l1()`

Remap register address through L1 window.

```c
static inline u32 mt7927_reg_map_l1(struct mt7927_dev *dev, u32 addr)
```

**Parameters:**
- `dev` - Device structure pointer
- `addr` - Logical register address

**Returns:** Translated BAR0 offset

**Description:** 
- Extracts offset and base from the address
- Backs up current L1 remapping register
- Sets new base in L1 remapping register
- Returns the translated address in L1 remapping window

---

### `mt7927_reg_map_l2()`

Remap register address through L2 window.

```c
static inline u32 mt7927_reg_map_l2(struct mt7927_dev *dev, u32 addr)
```

**Parameters:**
- `dev` - Device structure pointer
- `addr` - Logical register address

**Returns:** Translated BAR0 offset

**Description:**
- Backs up current L1 remapping register
- Sets L1 base for L2 window
- Writes address to L2 remapping register
- Returns the translated address

---

### `mt7927_reg_remap_restore()`

Restore register remapping to original state.

```c
static inline void mt7927_reg_remap_restore(struct mt7927_dev *dev)
```

**Parameters:**
- `dev` - Device structure pointer

**Description:** Restores both L1 and L2 remapping registers from backup values stored in the device structure.

---

### `mt7927_reg_addr()`

Translate logical address to BAR0 offset.

```c
static inline u32 mt7927_reg_addr(struct mt7927_dev *dev, u32 addr)
```

**Parameters:**
- `dev` - Device structure pointer
- `addr` - Logical register address

**Returns:** BAR0 offset for register access

**Description:**
- Direct access for addresses < 0x200000
- Checks fixed mapping table for known address ranges
- Uses L1 remapping for specific address ranges (0x18000000-0x18c00000, 0x70000000-0x78000000, 0x7c000000-0x7c400000)
- Uses L2 remapping for other addresses

---

### `mt7927_rr()`

Read register with address translation.

```c
static inline u32 mt7927_rr(struct mt7927_dev *dev, u32 offset)
```

**Parameters:**
- `dev` - Device structure pointer
- `offset` - Logical register offset

**Returns:** 32-bit register value

**Description:** Translates logical address to BAR0 offset, then reads from BAR0 (mem).

---

### `mt7927_wr()`

Write register with address translation.

```c
static inline void mt7927_wr(struct mt7927_dev *dev, u32 offset, u32 val)
```

**Parameters:**
- `dev` - Device structure pointer
- `offset` - Logical register offset
- `val` - 32-bit value to write

**Description:** Translates logical address to BAR0 offset, then writes to BAR0 (mem).

---

### `mt7927_rmw()`

Read-modify-write register.

```c
static inline u32 mt7927_rmw(struct mt7927_dev *dev, u32 offset, u32 mask, u32 val)
```

**Parameters:**
- `dev` - Device structure pointer
- `offset` - Register offset
- `mask` - Bits to modify
- `val` - New value for masked bits

**Returns:** Original register value before modification

**Description:** Reads register, clears masked bits, sets new value, writes back.

---

### `mt7927_set()`

Set bits in register.

```c
static inline void mt7927_set(struct mt7927_dev *dev, u32 offset, u32 val)
```

**Parameters:**
- `dev` - Device structure pointer
- `offset` - Register offset
- `val` - Bits to set

**Description:** Sets specified bits in register (OR operation).

---

### `mt7927_clear()`

Clear bits in register.

```c
static inline void mt7927_clear(struct mt7927_dev *dev, u32 offset, u32 val)
```

**Parameters:**
- `dev` - Device structure pointer
- `offset` - Register offset
- `val` - Bits to clear

**Description:** Clears specified bits in register (AND with NOT).

---

### `mt7927_rmw_field()`

Modify a field in a register.

```c
#define mt7927_rmw_field(dev, offset, field, val)
```

**Parameters:**
- `dev` - Device structure pointer
- `offset` - Register offset
- `field` - Field mask (e.g., `GENMASK(7, 0)`)
- `val` - New field value

**Description:** Macro that uses `FIELD_PREP` to set a specific field in a register.

---

### `mt7927_poll()`

Poll register until condition or timeout.

```c
static inline bool mt7927_poll(struct mt7927_dev *dev, u32 offset,
                               u32 mask, u32 val, int timeout_us)
```

**Parameters:**
- `dev` - Device structure pointer
- `offset` - Register offset
- `mask` - Bits to check
- `val` - Expected value (after masking)
- `timeout_us` - Timeout in microseconds

**Returns:** `true` if condition met, `false` on timeout

**Description:** Polls register every 10 microseconds until masked bits match expected value or timeout expires.

---

## Function Declarations

### Power Management Functions

#### `mt7927_mcu_fw_pmctrl()`
```c
int mt7927_mcu_fw_pmctrl(struct mt7927_dev *dev);
```
**Description:** Request firmware to take power management control.
**Returns:** 0 on success, negative error code on failure

#### `mt7927_mcu_drv_pmctrl()`
```c
int mt7927_mcu_drv_pmctrl(struct mt7927_dev *dev);
```
**Description:** Request driver to take power management control from firmware.
**Returns:** 0 on success, negative error code on failure

---

### WiFi System Reset Functions

#### `mt7927_wfsys_reset()`
```c
int mt7927_wfsys_reset(struct mt7927_dev *dev);
```
**Description:** Reset the WiFi system (WFSYS).
**Returns:** 0 on success, negative error code on failure

#### `mt7927_wpdma_reset()`
```c
int mt7927_wpdma_reset(struct mt7927_dev *dev, bool force);
```
**Description:** Reset the WPDMA (WiFi Packet DMA) subsystem.
**Parameters:**
- `dev` - Device structure pointer
- `force` - Force reset even if DMA is busy
**Returns:** 0 on success, negative error code on failure

---

### DMA Functions

#### `mt7927_dma_init()`
```c
int mt7927_dma_init(struct mt7927_dev *dev);
```
**Description:** Initialize DMA subsystem and allocate queues.
**Returns:** 0 on success, negative error code on failure

#### `mt7927_dma_cleanup()`
```c
void mt7927_dma_cleanup(struct mt7927_dev *dev);
```
**Description:** Cleanup DMA subsystem and free all resources.

#### `mt7927_dma_enable()`
```c
int mt7927_dma_enable(struct mt7927_dev *dev);
```
**Description:** Enable DMA engines for TX and RX.
**Returns:** 0 on success, negative error code on failure

#### `mt7927_dma_disable()`
```c
int mt7927_dma_disable(struct mt7927_dev *dev, bool force);
```
**Description:** Disable DMA engines.
**Parameters:**
- `dev` - Device structure pointer
- `force` - Force disable even if DMA is busy
**Returns:** 0 on success, negative error code on failure

#### `mt7927_queue_alloc()`
```c
int mt7927_queue_alloc(struct mt7927_dev *dev, struct mt7927_queue *q,
                       int idx, int ndesc, int buf_size, u32 ring_base);
```
**Description:** Allocate and initialize a DMA queue.
**Parameters:**
- `dev` - Device structure pointer
- `q` - Queue structure to initialize
- `idx` - Hardware queue index
- `ndesc` - Number of descriptors in ring
- `buf_size` - Size of each buffer
- `ring_base` - Hardware register base address for ring
**Returns:** 0 on success, negative error code on failure

#### `mt7927_queue_free()`
```c
void mt7927_queue_free(struct mt7927_dev *dev, struct mt7927_queue *q);
```
**Description:** Free a DMA queue and all associated resources.
**Parameters:**
- `dev` - Device structure pointer
- `q` - Queue structure to free

#### `mt7927_tx_queue_skb()`
```c
int mt7927_tx_queue_skb(struct mt7927_dev *dev, struct mt7927_queue *q,
                        struct sk_buff *skb);
```
**Description:** Queue a socket buffer for transmission.
**Parameters:**
- `dev` - Device structure pointer
- `q` - TX queue to use
- `skb` - Socket buffer to transmit
**Returns:** 0 on success, negative error code on failure

#### `mt7927_tx_complete()`
```c
void mt7927_tx_complete(struct mt7927_dev *dev, struct mt7927_queue *q);
```
**Description:** Process completed TX descriptors and free buffers.
**Parameters:**
- `dev` - Device structure pointer
- `q` - TX queue to process

#### `mt7927_rx_poll()`
```c
int mt7927_rx_poll(struct mt7927_dev *dev, struct mt7927_queue *q, int budget);
```
**Description:** Poll RX queue and process received packets.
**Parameters:**
- `dev` - Device structure pointer
- `q` - RX queue to poll
- `budget` - Maximum number of packets to process
**Returns:** Number of packets processed

---

### MCU Functions

#### `mt7927_mcu_init()`
```c
int mt7927_mcu_init(struct mt7927_dev *dev);
```
**Description:** Initialize MCU subsystem and communication queues.
**Returns:** 0 on success, negative error code on failure

#### `mt7927_mcu_exit()`
```c
void mt7927_mcu_exit(struct mt7927_dev *dev);
```
**Description:** Cleanup MCU subsystem and free resources.

#### `mt7927_mcu_send_msg()`
```c
int mt7927_mcu_send_msg(struct mt7927_dev *dev, int cmd,
                        const void *data, int len, bool wait_resp);
```
**Description:** Send MCU command message.
**Parameters:**
- `dev` - Device structure pointer
- `cmd` - MCU command ID
- `data` - Command payload data
- `len` - Length of payload
- `wait_resp` - Whether to wait for response
**Returns:** 0 on success, negative error code on failure

#### `mt7927_mcu_send_and_get_msg()`
```c
int mt7927_mcu_send_and_get_msg(struct mt7927_dev *dev, int cmd,
                                const void *data, int len,
                                bool wait_resp, struct sk_buff **ret_skb);
```
**Description:** Send MCU command and retrieve response.
**Parameters:**
- `dev` - Device structure pointer
- `cmd` - MCU command ID
- `data` - Command payload data
- `len` - Length of payload
- `wait_resp` - Whether to wait for response
- `ret_skb` - Pointer to store response socket buffer
**Returns:** 0 on success, negative error code on failure

#### `mt7927_load_firmware()`
```c
int mt7927_load_firmware(struct mt7927_dev *dev);
```
**Description:** Load firmware files (RAM code and patch).
**Returns:** 0 on success, negative error code on failure

#### `mt7927_load_patch()`
```c
int mt7927_load_patch(struct mt7927_dev *dev);
```
**Description:** Load and apply patch firmware.
**Returns:** 0 on success, negative error code on failure

#### `mt7927_load_ram()`
```c
int mt7927_load_ram(struct mt7927_dev *dev);
```
**Description:** Load RAM code firmware into MCU memory.
**Returns:** 0 on success, negative error code on failure

---

### IRQ Handling Functions

#### `mt7927_irq_handler()`
```c
irqreturn_t mt7927_irq_handler(int irq, void *dev_instance);
```
**Description:** Top-half interrupt handler.
**Parameters:**
- `irq` - IRQ number
- `dev_instance` - Device instance pointer
**Returns:** `IRQ_HANDLED` or `IRQ_NONE`

#### `mt7927_irq_tasklet()`
```c
void mt7927_irq_tasklet(unsigned long data);
```
**Description:** Bottom-half tasklet for interrupt processing.
**Parameters:**
- `data` - Device structure pointer (cast from unsigned long)

#### `mt7927_irq_enable()`
```c
void mt7927_irq_enable(struct mt7927_dev *dev, u32 mask);
```
**Description:** Enable specified interrupts.
**Parameters:**
- `dev` - Device structure pointer
- `mask` - Interrupt mask to enable

#### `mt7927_irq_disable()`
```c
void mt7927_irq_disable(struct mt7927_dev *dev, u32 mask);
```
**Description:** Disable specified interrupts.
**Parameters:**
- `dev` - Device structure pointer
- `mask` - Interrupt mask to disable

---

### Device Registration Functions

#### `mt7927_register_device()`
```c
int mt7927_register_device(struct mt7927_dev *dev);
```
**Description:** Register device with kernel subsystems (networking, etc.).
**Returns:** 0 on success, negative error code on failure

#### `mt7927_unregister_device()`
```c
void mt7927_unregister_device(struct mt7927_dev *dev);
```
**Description:** Unregister device from kernel subsystems.

---

# mt7927_regs.h - Register Definitions

## File Overview

`mt7927_regs.h` contains all register offset definitions, bit field masks, and hardware configuration constants for the MT7927 chip. It includes WFDMA (WiFi DMA) registers, interrupt registers, power management registers, and address mapping tables.

**Key Components:**
- WFDMA base offsets and configuration registers
- Interrupt enable/disable registers and masks
- TX/RX ring register definitions
- Power management and reset registers
- Register remapping definitions
- Queue ID enumerations
- Fixed address mapping table

---

## WFDMA Base Offsets

### `MT_WFDMA0_BASE`
- **Value:** `0xd4000`
- **Description:** Base offset for WFDMA HOST DMA0 registers in BAR0 address space (chip address 0x7C024000). This is where firmware download rings 15/16 are configured.

### `MT_WFDMA0(ofs)`
- **Macro:** `(MT_WFDMA0_BASE + (ofs))`
- **Description:** Constructs WFDMA0 register address from offset. For example, `MT_WFDMA0(0x208)` = 0xd4208 for GLO_CFG.

---

## WFDMA Global Configuration

### `MT_WFDMA0_GLO_CFG`
- **Address:** `MT_WFDMA0(0x208)`
- **Description:** Global configuration register for WFDMA0.

**Bit Fields:**
- `MT_WFDMA0_GLO_CFG_TX_DMA_EN` (BIT(0)) - Enable TX DMA engine
- `MT_WFDMA0_GLO_CFG_TX_DMA_BUSY` (BIT(1)) - TX DMA busy status (read-only)
- `MT_WFDMA0_GLO_CFG_RX_DMA_EN` (BIT(2)) - Enable RX DMA engine
- `MT_WFDMA0_GLO_CFG_RX_DMA_BUSY` (BIT(3)) - RX DMA busy status (read-only)
- `MT_WFDMA0_GLO_CFG_TX_WB_DDONE` (BIT(6)) - TX write-back done
- `MT_WFDMA0_GLO_CFG_FIFO_LITTLE_ENDIAN` (BIT(12)) - FIFO little-endian mode
- `MT_WFDMA0_GLO_CFG_CSR_DISP_BASE_PTR_CHAIN_EN` (BIT(15)) - Enable CSR dispatch base pointer chaining
- `MT_WFDMA0_GLO_CFG_OMIT_RX_INFO_PFET2` (BIT(21)) - Omit RX info for PFET2
- `MT_WFDMA0_GLO_CFG_OMIT_RX_INFO` (BIT(27)) - Omit RX info field
- `MT_WFDMA0_GLO_CFG_OMIT_TX_INFO` (BIT(28)) - Omit TX info field
- `MT_WFDMA0_GLO_CFG_EXT_EN` (BIT(26)) - Extended mode enable

### `MT_WFDMA0_RST_DTX_PTR`
- **Address:** `MT_WFDMA0(0x20c)`
- **Description:** Reset TX descriptor pointer register.

### `MT_WFDMA0_RST_DRX_PTR`
- **Address:** `MT_WFDMA0(0x280)`
- **Description:** Reset RX descriptor pointer register.

### Additional GLO_CFG Bits
- `MT_WFDMA0_GLO_CFG_CLK_GAT_DIS` (BIT(5)) - Disable clock gating
- `MT_WFDMA0_GLO_CFG_RX_WB_DDONE` (BIT(7)) - RX write-back done
- `MT_WFDMA0_GLO_CFG_FIFO_DIS_CHECK` (BIT(18)) - Disable FIFO check
- `MT_WFDMA0_GLO_CFG_DMA_SIZE` (GENMASK(17, 16)) - DMA size configuration

---

## Interrupt Registers

### `MT_WFDMA0_HOST_INT_STA`
- **Address:** `MT_WFDMA0(0x200)`
- **Description:** Host interrupt status register (read-only). Shows which interrupts are pending.

### `MT_WFDMA0_HOST_INT_ENA`
- **Address:** `MT_WFDMA0(0x204)`
- **Description:** Host interrupt enable register. Write 1 to enable interrupts.

### `MT_WFDMA0_HOST_INT_DIS`
- **Address:** `MT_WFDMA0(0x22c)`
- **Description:** Host interrupt disable register. Write 1 to disable interrupts.

---

## TX Done Interrupt Masks

### Individual TX Interrupt Bits
- `HOST_TX_DONE_INT_ENA0` (BIT(0)) - Band0 Data TX done
- `HOST_TX_DONE_INT_ENA1` (BIT(1)) - TX done interrupt 1
- `HOST_TX_DONE_INT_ENA2` (BIT(2)) - TX done interrupt 2
- `HOST_TX_DONE_INT_ENA3` (BIT(3)) - TX done interrupt 3
- `HOST_TX_DONE_INT_ENA4` (BIT(4)) - MT7927: FWDL (Firmware Download) TX done
- `HOST_TX_DONE_INT_ENA5` (BIT(5)) - MT7927: MCU WM (Wireless Manager) TX done
- `HOST_TX_DONE_INT_ENA6` (BIT(6)) - TX done interrupt 6
- `HOST_TX_DONE_INT_ENA7` (BIT(7)) - TX done interrupt 7
- `HOST_TX_DONE_INT_ENA15` (BIT(25)) - Not used on MT7927
- `HOST_TX_DONE_INT_ENA16` (BIT(26)) - Not used on MT7927
- `HOST_TX_DONE_INT_ENA17` (BIT(27)) - Not used on MT7927

### Combined TX Interrupt Masks
- `MT_INT_TX_DONE_MCU_WM` - MCU WM TX done mask
- `MT_INT_TX_DONE_FWDL` - Firmware download TX done mask
- `MT_INT_TX_DONE_BAND0` - Band0 data TX done mask
- `MT_INT_TX_DONE_MCU` - All MCU TX done interrupts
- `MT_INT_TX_DONE_ALL` - All TX done interrupts

---

## RX Done Interrupt Masks

### Individual RX Interrupt Bits
- `HOST_RX_DONE_INT_ENA0` (BIT(16)) - MCU WM RX done
- `HOST_RX_DONE_INT_ENA1` (BIT(17)) - MCU WM2 RX done
- `HOST_RX_DONE_INT_ENA2` (BIT(18)) - Band0 Data RX done
- `HOST_RX_DONE_INT_ENA3` (BIT(19)) - Band1 Data RX done
- `HOST_RX_DONE_INT_ENA4` (BIT(12)) - RX done interrupt 4
- `HOST_RX_DONE_INT_ENA5` (BIT(13)) - RX done interrupt 5

### Combined RX Interrupt Masks
- `MT_INT_RX_DONE_DATA` - Data RX done mask
- `MT_INT_RX_DONE_WM` - WM RX done mask
- `MT_INT_RX_DONE_WM2` - WM2 RX done mask
- `MT_INT_RX_DONE_ALL` - All RX done interrupts

### MCU Command Interrupt
- `MT_INT_MCU_CMD` (BIT(29)) - MCU command interrupt

---

## TX Ring Registers

### Ring 0 (Band0 Data)
- `MT_WFDMA0_TX_RING0_BASE` - Base address register for TX ring 0
- `MT_WFDMA0_TX_RING0_CNT` - Descriptor count register for TX ring 0
- `MT_WFDMA0_TX_RING0_CIDX` - CPU index register (driver write pointer)
- `MT_WFDMA0_TX_RING0_DIDX` - DMA index register (hardware read pointer)

### Ring Extension Control
- `MT_WFDMA0_TX_RING0_EXT_CTRL` - Extended control for TX ring 0 (36-bit DMA, prefetch)
- `MT_WFDMA0_TX_RING15_EXT_CTRL` - Extended control for TX ring 15
- `MT_WFDMA0_TX_RING16_EXT_CTRL` - Extended control for TX ring 16

### Generic TX Ring Macros
- `MT_WFDMA0_TX_RING_BASE(n)` - Base address for TX ring n
- `MT_WFDMA0_TX_RING_CNT(n)` - Descriptor count for TX ring n
- `MT_WFDMA0_TX_RING_CIDX(n)` - CPU index for TX ring n
- `MT_WFDMA0_TX_RING_DIDX(n)` - DMA index for TX ring n
- `MT_WFDMA0_TX_RING_EXT_CTRL(n)` - Extended control for TX ring n

---

## RX Ring Registers

### Generic RX Ring Macros
- `MT_WFDMA0_RX_RING_BASE(n)` - Base address for RX ring n
- `MT_WFDMA0_RX_RING_CNT(n)` - Descriptor count for RX ring n
- `MT_WFDMA0_RX_RING_CIDX(n)` - CPU index for RX ring n
- `MT_WFDMA0_RX_RING_DIDX(n)` - DMA index for RX ring n
- `MT_WFDMA0_RX_RING_EXT_CTRL(n)` - Extended control for RX ring n

### Base Addresses
- `MT_RX_EVENT_RING_BASE` - Base for event rings
- `MT_RX_DATA_RING_BASE` - Base for data rings

---

## DMA Reset and Configuration

### `MT_WFDMA0_RST`
- **Address:** `MT_WFDMA0(0x100)`
- **Description:** WFDMA0 reset register.

**Bit Fields:**
- `MT_WFDMA0_RST_LOGIC_RST` (BIT(4)) - Logic reset
- `MT_WFDMA0_RST_DMASHDL_ALL_RST` (BIT(5)) - Reset all DMA schedulers

### `MT_WFDMA0_PRI_DLY_INT_CFG0`
- **Address:** `MT_WFDMA0(0x2f0)`
- **Description:** Priority delay interrupt configuration register 0.

---

## MCU Registers

### `MT_MCU2HOST_SW_INT_ENA`
- **Address:** `MT_WFDMA0(0x1f4)`
- **Description:** MCU to host software interrupt enable register.

**Bit Fields:**
- `MT_MCU_CMD_WAKE_RX_PCIE` (BIT(0)) - MCU command wake RX PCIe

---

## Power Management Registers

### `MT_CONN_ON_LPCTL`
- **Address:** `0x7c060010`
- **Description:** Low Power Control register for power management handshake.

**Bit Fields:**
- `PCIE_LPCR_HOST_SET_OWN` (BIT(0)) - Write: give ownership to firmware
- `PCIE_LPCR_HOST_CLR_OWN` (BIT(1)) - Write: claim ownership for driver
- `PCIE_LPCR_HOST_OWN_SYNC` (BIT(2)) - Read: 1=FW owns, 0=driver owns

**Legacy Names:**
- `MT_CONN_ON_LPCTL_HOST_OWN` - Alias for SET_OWN
- `MT_CONN_ON_LPCTL_FW_OWN` - Alias for CLR_OWN

---

## PCIe MAC Registers

### `MT_PCIE_MAC_BASE`
- **Address:** `0x74030000`
- **Description:** Base address for PCIe MAC registers (logical address, translated via fixed_map).

### `MT_PCIE_MAC(ofs)`
- **Macro:** `(MT_PCIE_MAC_BASE + (ofs))`
- **Description:** Constructs PCIe MAC register address.

### PCIe MAC Registers
- `MT_PCIE_MAC_INT_ENABLE` - PCIe MAC interrupt enable register
- `MT_PCIE_MAC_PM` - PCIe MAC power management register
  - `MT_PCIE_MAC_PM_L0S_DIS` (BIT(8)) - Disable L0s power state

---

## Hardware Control Registers

### `MT_HW_CHIPID`
- **Address:** `0x0000`
- **Description:** Hardware chip ID register.

### `MT_HW_REV`
- **Address:** `0x0004`
- **Description:** Hardware revision register.

### `MT_HW_EMI_CTL`
- **Address:** `0x0110`
- **Description:** EMI (External Memory Interface) control register.
- `MT_HW_EMI_CTL_SLPPROT_EN` (BIT(0)) - Sleep protection enable

---

## WiFi System Reset

### `MT_WFSYS_SW_RST_B`
- **Address:** `0x7c000140`
- **Description:** WiFi system software reset register.

**Bit Fields:**
- `MT_WFSYS_SW_RST_B_EN` (BIT(0)) - Enable software reset
- `MT_WFSYS_SW_INIT_DONE` (BIT(4)) - Software initialization done (read-only)

---

## Register Remapping

### L1 Remapping
- `MT_HIF_REMAP_L1` - Address: `0x155024`
- `MT_HIF_REMAP_L1_MASK` - GENMASK(31, 16) - Base address mask
- `MT_HIF_REMAP_L1_OFFSET` - GENMASK(15, 0) - Offset mask
- `MT_HIF_REMAP_L1_BASE` - GENMASK(31, 16) - Base address field
- `MT_HIF_REMAP_BASE_L1` - `0x130000` - L1 remapping window base

### L2 Remapping
- `MT_HIF_REMAP_L2` - Address: `0x0120`
- `MT_HIF_REMAP_BASE_L2` - `0x18500000` - L2 remapping base address

---

## Firmware Status Registers

### `MT_CONN_ON_MISC`
- **Address:** `0x7c0600f0`
- **Description:** Miscellaneous connection-on register.

**Bit Fields:**
- `MT_TOP_MISC2_FW_N9_RDY` - GENMASK(1, 0) - Firmware N9 ready status
- `MT_TOP_MISC2_FW_N9_RDY_VAL` - `0x1` - Expected value when firmware is ready

---

## Scratch Registers

### `MT_SWDEF_BASE`
- **Address:** `0x00401400`
- **Description:** Base address for scratch/definition registers.

### `MT_SWDEF_MODE`
- **Address:** `MT_SWDEF_BASE + 0x3c`
- **Description:** Mode register.

**Bit Fields:**
- `MT_SWDEF_MODE_MT7927_MASK` - GENMASK(15, 0) - MT7927 mode mask
- `MT_SWDEF_NORMAL_MODE` - `0` - Normal operation mode

---

## DMA Descriptor Definitions

### Descriptor Sizes
- `MT_TXD_SIZE` - `32` - TX descriptor size in bytes
- `MT_RXD_SIZE` - `32` - RX descriptor size in bytes

### DMA Control Bits
- `MT_DMA_CTL_SD_LEN0` - GENMASK(15, 0) - Segment 0 length
- `MT_DMA_CTL_SD_LEN1` - GENMASK(29, 16) - Segment 1 length
- `MT_DMA_CTL_LAST_SEC0` - BIT(30) - Last segment flag for segment 0
- `MT_DMA_CTL_LAST_SEC1` - BIT(31) - Last segment flag for segment 1
- `MT_DMA_CTL_DMA_DONE` - BIT(31) - DMA done flag
- `MT_DMA_CTL_TO_HOST` - BIT(8) - Transfer to host flag
- `MT_DMA_CTL_TO_HOST_V2` - BIT(31) - Transfer to host flag (version 2)
- `MT_DMA_PPE_CPU_REASON` - GENMASK(15, 11) - PPE CPU reason code
- `MT_DMA_PPE_ENTRY` - GENMASK(30, 16) - PPE entry index

### DMA Info Field
- `MT_DMA_INFO_DMA_FRAG` - BIT(9) - DMA fragment flag

---

## Enums

### `enum mt7927_txq_id`

TX Queue IDs for MT7927.

**Values:**
- `MT7927_TXQ_BAND0` (0) - Band0 data TX queue
- `MT7927_TXQ_BAND1` (1) - Band1 data TX queue (if needed)
- `MT7927_TXQ_FWDL` (4) - Firmware download queue (ring 4)
- `MT7927_TXQ_MCU_WM` (5) - MCU Wireless Manager command queue (ring 5)

**Note:** MT7927 only has 8 TX rings (0-7), not 17 like MT7925.

---

### `enum mt7927_rxq_id`

RX Queue IDs for MT7927.

**Values:**
- `MT7927_RXQ_MCU_WM` (0) - MCU Wireless Manager RX queue
- `MT7927_RXQ_MCU_WM2` (1) - MCU Wireless Manager 2 RX queue
- `MT7927_RXQ_BAND0` (2) - Band0 data RX queue
- `MT7927_RXQ_BAND1` (3) - Band1 data RX queue

---

### `enum mt7927_mcu_queue_id`

MCU Queue IDs.

**Values:**
- `MT_MCUQ_WM` (0) - Wireless Manager queue
- `MT_MCUQ_WA` (1) - Wireless Assistant queue
- `MT_MCUQ_FWDL` (2) - Firmware download queue
- `__MT_MCUQ_MAX` (3) - Maximum number of MCU queues

---

## Ring Sizes

### TX Ring Sizes
- `MT7927_TX_RING_SIZE` - `2048` - Size of data TX ring
- `MT7927_TX_MCU_RING_SIZE` - `256` - Size of MCU TX ring
- `MT7927_TX_FWDL_RING_SIZE` - `128` - Size of firmware download TX ring

### RX Ring Sizes
- `MT7927_RX_RING_SIZE` - `1536` - Size of data RX ring
- `MT7927_RX_MCU_RING_SIZE` - `512` - Size of MCU RX ring

### Buffer Sizes
- `MT_RX_BUF_SIZE` - `2048` - RX buffer size in bytes
- `MT_TX_TOKEN_SIZE` - `8192` - TX token size

---

## Structures

### `struct mt7927_reg_map`

Address mapping table entry for register remapping.

**Fields:**
- `u32 phys` - Physical/logical address (address seen by software)
- `u32 maps` - Mapped BAR0 offset (actual register location)
- `u32 size` - Region size in bytes

**Description:** Used in the fixed mapping table to translate logical addresses to BAR0 offsets.

---

### `mt7927_fixed_map[]`

Static array of register mapping entries.

**Description:** Fixed address mapping table from mt7925 driver, containing mappings for:
- WFDMA registers
- LMAC/UMAC registers
- PCIe MAC registers
- CONN_INFRA registers
- System RAM regions
- And many other subsystems

**Termination:** Array ends with `{ 0x0, 0x0, 0x0 }` entry.

---

# mt7927_mcu.h - MCU Protocol Definitions

## File Overview

`mt7927_mcu.h` defines the MCU (Microcontroller Unit) communication protocol structures, command IDs, event IDs, and message formats used for communication between the host driver and the firmware running on the MT7927 chip.

**Key Components:**
- MCU command and event IDs
- TX/RX descriptor structures for MCU messages
- Firmware download structures
- Patch header structures
- Helper macros for building MCU commands

---

## Constants

### MCU Header Size
- `MT_MCU_HDR_SIZE` - Size of MCU command header (`sizeof(struct mt7927_mcu_txd)`)

### Max Message Size
- `MT_MCU_MSG_MAX_SIZE` - `2048` - Maximum MCU message size in bytes

---

## MCU Command IDs

### Legacy Commands
- `MCU_CMD_FW_SCATTER` (0x0f) - Firmware scatter download command
- `MCU_CMD_PATCH_SEM_CONTROL` (0x10) - Patch semaphore control
- `MCU_CMD_PATCH_FINISH_REQ` (0x11) - Patch finish request
- `MCU_CMD_PATCH_START_REQ` (0x14) - Patch start request
- `MCU_CMD_START_FIRMWARE` (0x15) - Start firmware command
- `MCU_CMD_RESTART_DL` (0x18) - Restart download command

### MCU UNI (Unified Interface) Command IDs
- `MCU_UNI_CMD_DEV_INFO_UPDATE` (0x01) - Device info update
- `MCU_UNI_CMD_BSS_INFO_UPDATE` (0x02) - BSS (Basic Service Set) info update
- `MCU_UNI_CMD_STA_REC_UPDATE` (0x03) - Station record update
- `MCU_UNI_CMD_SUSPEND` (0x04) - Suspend command
- `MCU_UNI_CMD_OFFLOAD` (0x06) - Offload command
- `MCU_UNI_CMD_HIF_CTRL` (0x07) - Host interface control
- `MCU_UNI_CMD_BAND_CONFIG` (0x08) - Band configuration
- `MCU_UNI_CMD_REPT_MUAR` (0x09) - Report MUAR (Multi-User Aggregation Report)
- `MCU_UNI_CMD_REG_ACCESS` (0x0d) - Register access command

---

## MCU Event IDs

- `MCU_EVENT_FW_READY` (0x01) - Firmware ready event
- `MCU_EVENT_RESTART_DL` (0x02) - Restart download event
- `MCU_EVENT_PATCH_SEM` (0x04) - Patch semaphore event
- `MCU_EVENT_GENERIC` (0x05) - Generic event

---

## Command Field Bits

- `MCU_CMD_FIELD_ID` - GENMASK(7, 0) - Command ID field (bits 0-7)
- `MCU_CMD_FIELD_EXT_ID` - GENMASK(15, 8) - Extended command ID field (bits 8-15)
- `MCU_CMD_FIELD_QUERY` - BIT(16) - Query flag (1 = query, 0 = set)
- `MCU_CMD_FIELD_UNI` - BIT(17) - Unified interface flag
- `MCU_CMD_FIELD_WM` - BIT(19) - Wireless Manager flag

---

## Source/Destination Indices

- `S2D_IDX_MCU` (0) - Source to destination index for MCU
- `D2S_IDX_MCU` (0) - Destination to source index for MCU

---

## Structures

### `struct mt7927_mcu_txd`

MCU TX descriptor (command header) structure.

**Fields:**
- `__le32 txd[8]` - TX descriptor words (8 x 32-bit words)
  - Word 0: `MT_TXD0_Q_IDX` - Queue index (bits 31-25)
  - Word 0: `MT_TXD0_PKT_FMT` - Packet format (bits 24-23)
  - Word 0: `MT_TXD0_ETH_TYPE_OFFSET` - Ethernet type offset (bits 22-16)
  - Word 0: `MT_TXD0_TX_BYTES` - TX bytes (bits 15-0)
  - Word 1: `MT_TXD1_OWN_MAC` - Own MAC address (bits 31-26)
  - Word 1: `MT_TXD1_HDR_FORMAT` - Header format (bits 7-5)
  - Word 1: `MT_TXD1_TID` - Traffic ID (bits 4-0)
- `__le16 len` - Message length (payload size)
- `__le16 pq_id` - Packet queue ID
- `u8 cid` - Command ID
- `u8 pkt_type` - Packet type (see packet type values below)
- `u8 set_query` - Set/Query flag (0: set, 1: query)
- `u8 seq` - Sequence number
- `u8 uc_d2b0_rev` - UC D2B0 revision
- `u8 ext_cid` - Extended command ID
- `u8 s2d_index` - Source to destination index
- `u8 ext_cid_ack` - Extended CID acknowledgment
- `u32 rsv[5]` - Reserved fields (5 x 32-bit words)

**Alignment:** `__packed __aligned(4)` - Packed structure with 4-byte alignment

---

### Packet Type Values

- `MT_PKT_TYPE_TXD` (0) - TX descriptor type
- `MT_PKT_TYPE_FW` (1) - Firmware packet type
- `MT_PKT_TYPE_CMD` (2) - Command packet type
- `MT_PKT_TYPE_EVENT` (3) - Event packet type

---

### Set/Query Values

- `MCU_SET` (0) - Set operation
- `MCU_QUERY` (1) - Query operation

---

### `struct mt7927_mcu_rxd`

MCU RX descriptor (event header) structure.

**Fields:**
- `__le32 rxd[8]` - RX descriptor words (8 x 32-bit words)
- `__le16 len` - Message length (payload size)
- `__le16 pkt_type_id` - Packet type ID
- `u8 eid` - Event ID
- `u8 seq` - Sequence number (matches TX sequence)
- `u8 option` - Options field
- `u8 rsv0` - Reserved byte 0
- `u8 ext_eid` - Extended event ID
- `u8 rsv1[2]` - Reserved bytes 1-2
- `u8 s2d_index` - Source to destination index
- `u8 tlv[]` - TLV (Type-Length-Value) data follows (flexible array member)

**Alignment:** `__packed __aligned(4)` - Packed structure with 4-byte alignment

---

### `struct mt7927_mcu_uni_event`

UNI event structure for unified interface events.

**Fields:**
- `u8 cid` - Command ID that triggered this event
- `u8 pad[3]` - Padding bytes
- `__le32 status` - Status code (0: success, others: failure)

**Alignment:** `__packed` - Packed structure

---

## Firmware Download Structures

### Patch Semaphore Values
- `PATCH_SEM_RELEASE` (0) - Release patch semaphore
- `PATCH_SEM_GET` (1) - Acquire patch semaphore

---

### `struct mt7927_patch_hdr`

Patch firmware header structure.

**Fields:**
- `char build_date[16]` - Build date string
- `char platform[4]` - Platform identifier string
- `__le32 hw_sw_ver` - Hardware/software version
- `__le32 patch_ver` - Patch version
- `__le16 checksum` - Header checksum
- `__le16 rsv0` - Reserved word 0
- `struct { ... } sec_info` - Section information:
  - `__le32 patch_ver` - Patch version in section info
  - `__le32 subsys` - Subsystem identifier
  - `__le32 feature` - Feature flags
  - `__le32 n_region` - Number of regions
  - `__le32 crc` - CRC checksum
  - `__le32 rsv[11]` - Reserved array (11 words)
- `u8 rsv1[108]` - Reserved bytes (108 bytes)

**Alignment:** `__packed` - Packed structure

---

### `struct mt7927_patch_sec`

Patch section structure.

**Fields:**
- `__le32 type` - Section type
- `__le32 offs` - Section offset
- `__le32 size` - Section size
- `union { ... }` - Section-specific data:
  - `__le32 spec[13]` - Generic specification array
  - `struct { ... } info` - Detailed info structure:
    - `__le32 addr` - Load address
    - `__le32 len` - Length
    - `__le32 sec_key_idx` - Section key index
    - `__le32 align_len` - Aligned length
    - `__le32 rsv[9]` - Reserved array (9 words)

**Alignment:** `__packed` - Packed structure

---

### `struct mt7927_fw_trailer`

Firmware trailer structure (end of firmware file).

**Fields:**
- `u8 chip_id` - Chip ID
- `u8 eco_code` - ECO (Engineering Change Order) code
- `u8 n_region` - Number of regions
- `u8 format_ver` - Format version
- `u8 format_flag` - Format flags
- `u8 rsv[2]` - Reserved bytes
- `char fw_ver[10]` - Firmware version string
- `char build_date[15]` - Build date string
- `__le32 crc` - CRC checksum

**Alignment:** `__packed` - Packed structure

---

### `struct mt7927_fw_region`

Firmware region structure.

**Fields:**
- `__le32 decomp_crc` - Decompressed CRC
- `__le32 decomp_len` - Decompressed length
- `__le32 decomp_blk_sz` - Decompressed block size
- `u8 rsv0[4]` - Reserved bytes 0
- `__le32 addr` - Load address
- `__le32 len` - Length
- `u8 feature_set` - Feature set identifier
- `u8 type` - Region type
- `u8 rsv1[14]` - Reserved bytes 1
- `char name[32]` - Region name string

**Alignment:** `__packed` - Packed structure

---

### `struct mt7927_fw_scatter`

Firmware scatter download command structure.

**Fields:**
- `__le32 addr` - Target address
- `__le32 len` - Length of data
- `__le32 mode` - Download mode (see firmware modes below)
- `u8 rsv[4]` - Reserved bytes

**Alignment:** `__packed` - Packed structure

---

### Firmware Download Modes
- `FW_MODE_DL` (0) - Download mode
- `FW_MODE_START` (1) - Start mode
- `FW_MODE_READY` (2) - Ready mode

---

## MCU Message Structures

### `struct mt7927_patch_sem_req`

Patch semaphore request structure.

**Fields:**
- `u8 op` - Operation (`PATCH_SEM_GET` or `PATCH_SEM_RELEASE`)
- `u8 rsv[3]` - Reserved bytes

**Alignment:** `__packed` - Packed structure

---

### Patch Semaphore Response Values
- `PATCH_SEM_NOT_READY` (0) - Semaphore not ready
- `PATCH_SEM_READY` (1) - Semaphore ready
- `PATCH_SEM_ERROR` (2) - Semaphore error

---

### `struct mt7927_start_fw_req`

Start firmware request structure.

**Fields:**
- `__le32 override` - Override flags
- `__le32 addr` - Firmware start address

**Alignment:** `__packed` - Packed structure

---

### `struct mt7927_restart_dl_req`

Restart download request structure.

**Fields:**
- `u8 rsv[4]` - Reserved bytes (no parameters needed)

**Alignment:** `__packed` - Packed structure

---

## Helper Macros

### MCU Command Building Macros

#### `MCU_CMD(cmd)`
- **Definition:** `((cmd) & MCU_CMD_FIELD_ID)`
- **Description:** Extract base command ID from command value

#### `MCU_EXT_CMD(cmd)`
- **Definition:** `(((cmd) << 8) | MCU_CMD_FIELD_EXT_ID)`
- **Description:** Build extended command with ID in extended field

#### `MCU_UNI_CMD(cmd)`
- **Definition:** `((cmd) | MCU_CMD_FIELD_UNI)`
- **Description:** Build unified interface command

#### `MCU_WM_CMD(cmd)`
- **Definition:** `((cmd) | MCU_CMD_FIELD_WM)`
- **Description:** Build Wireless Manager command

#### `MCU_WM_UNI_CMD(cmd)`
- **Definition:** `((cmd) | MCU_CMD_FIELD_UNI | MCU_CMD_FIELD_WM)`
- **Description:** Build Wireless Manager unified interface command

#### `MCU_WM_UNI_CMD_QUERY(cmd)`
- **Definition:** `((cmd) | MCU_CMD_FIELD_UNI | MCU_CMD_FIELD_WM | MCU_CMD_FIELD_QUERY)`
- **Description:** Build Wireless Manager unified interface query command

---

### MCU Command Extraction Macros

#### `MCU_CMD_ID(cmd)`
- **Definition:** `FIELD_GET(MCU_CMD_FIELD_ID, cmd)`
- **Description:** Extract command ID from full command value

#### `MCU_CMD_EXT_ID(cmd)`
- **Definition:** `FIELD_GET(MCU_CMD_FIELD_EXT_ID, cmd)`
- **Description:** Extract extended command ID from full command value

---

## Function Declarations

### `mt7927_mcu_fill_message()`
```c
int mt7927_mcu_fill_message(struct mt7927_dev *dev, struct sk_buff *skb,
                            int cmd, int *seq);
```
**Description:** Fill socket buffer with MCU message header and command.
**Parameters:**
- `dev` - Device structure pointer
- `skb` - Socket buffer to fill
- `cmd` - MCU command ID
- `seq` - Pointer to sequence number (updated on return)
**Returns:** 0 on success, negative error code on failure

---

### `mt7927_mcu_patch_sem_ctrl()`
```c
int mt7927_mcu_patch_sem_ctrl(struct mt7927_dev *dev, bool get);
```
**Description:** Control patch semaphore (acquire or release).
**Parameters:**
- `dev` - Device structure pointer
- `get` - `true` to acquire semaphore, `false` to release
**Returns:** 0 on success, negative error code on failure

---

### `mt7927_mcu_start_patch()`
```c
int mt7927_mcu_start_patch(struct mt7927_dev *dev);
```
**Description:** Start patch firmware loading process.
**Returns:** 0 on success, negative error code on failure

---

### `mt7927_mcu_start_firmware()`
```c
int mt7927_mcu_start_firmware(struct mt7927_dev *dev, u32 addr);
```
**Description:** Start firmware execution at specified address.
**Parameters:**
- `dev` - Device structure pointer
- `addr` - Firmware start address
**Returns:** 0 on success, negative error code on failure

---

### `mt7927_mcu_send_firmware()`
```c
int mt7927_mcu_send_firmware(struct mt7927_dev *dev, int cmd,
                             const void *data, int len);
```
**Description:** Send firmware data to MCU.
**Parameters:**
- `dev` - Device structure pointer
- `cmd` - Firmware command ID (e.g., `MCU_CMD_FW_SCATTER`)
- `data` - Firmware data pointer
- `len` - Length of firmware data
**Returns:** 0 on success, negative error code on failure

---

## Notes

### Register Layout Discovery

The MT7927 driver uses a critical discovery about register layout:
- **BAR0 (2MB):** Main register space - use for all register access
  - `0x00000-0x01FFF`: Reserved/unused
  - `0x02000-0x02FFF`: WFDMA0 registers (REAL writable DMA control)
  - `0x03000+`: Other subsystems per fixed_map
  - `0x10000+`: Shadow of BAR2 (read-only status mirrors)
  - `0xE0000+`: CONN_INFRA (power control, LPCTL)
  - `0xF0000+`: CONN_INFRA (WFSYS reset)

- **BAR2 (32KB):** Window into BAR0+0x10000 - READ-ONLY shadow registers
  - Do NOT use for control writes - they won't take effect!

**Always use BAR0 (mem) for register access.**

---

## Summary

This documentation covers all structures, enums, macros, and function declarations in the three main header files of the MT7927 driver. Use this as a reference when working with the driver codebase.
