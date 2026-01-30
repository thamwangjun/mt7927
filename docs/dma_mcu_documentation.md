# MT7927 DMA and MCU Documentation

This document provides comprehensive documentation for the MT7927 WiFi 7 driver's DMA (Direct Memory Access) and MCU (Microcontroller Unit) subsystems.

## Table of Contents

1. [DMA Subsystem](#dma-subsystem)
   - [Architecture Overview](#dma-architecture-overview)
   - [Queue Management](#dma-queue-management)
   - [TX Operations](#dma-tx-operations)
   - [RX Operations](#dma-rx-operations)
   - [DMA Control](#dma-control)
2. [MCU Subsystem](#mcu-subsystem)
   - [Architecture Overview](#mcu-architecture-overview)
   - [Message Protocol](#mcu-message-protocol)
   - [Firmware Loading](#mcu-firmware-loading)
   - [MCU Initialization](#mcu-initialization)

---

## DMA Subsystem

### DMA Architecture Overview

The MT7927 uses a ring-based DMA architecture where the driver and hardware share descriptor rings in coherent memory. The hardware automatically processes descriptors and signals completion via interrupts.

#### Descriptor Ring Structure

Each DMA queue uses a circular buffer of descriptors (`struct mt7927_desc`):

```c
struct mt7927_desc {
    __le32 buf0;        /* Buffer pointer (low 32 bits) */
    __le32 ctrl;        /* Control: length, last segment, DMA done */
    __le32 buf1;        /* Buffer pointer (high 32 bits, for 64-bit DMA) */
    __le32 info;        /* Additional info */
} __packed __aligned(4);
```

**Key Fields:**
- `buf0/buf1`: 64-bit DMA address of the data buffer (little-endian)
- `ctrl`: Control word containing:
  - `MT_DMA_CTL_SD_LEN0` (bits 15:0): Data length
  - `MT_DMA_CTL_LAST_SEC0` (bit 30): Last segment flag
  - `MT_DMA_CTL_DMA_DONE` (bit 31): DMA completion flag (set by hardware)

#### Ring Indices

- **head**: CPU write index (driver adds new descriptors here)
- **tail**: DMA read index (hardware processes descriptors up to here)
- Hardware maintains its own `DIDX` (DMA index) register

#### Queue Types

- **TX Queues**: For transmitting data/commands to hardware
  - Queue 0: Band0 data transmission
  - Queue 15: MCU command/response (MCU_WM) - **CONFIRMED via MT6639 analysis**
  - Queue 16: Firmware download (FWDL) - **CONFIRMED via MT6639 analysis**

- **RX Queues**: For receiving data/responses from hardware
  - Queue 0: MCU responses (WM)
  - Queue 2: Band0 data reception

**⚠️ IMPORTANT**: MT7927 has 8 physical TX rings (0-7), but uses sparse ring numbering (0,1,2,15,16) like MT6639. Rings 15/16 exist in hardware despite CNT=0 in uninitialized state.

### DMA Queue Management

#### `mt7927_queue_alloc`

**Purpose:** Allocate and initialize a DMA queue with descriptor ring and buffers.

**Parameters:**
- `dev`: Device structure pointer
- `q`: Queue structure to initialize
- `idx`: Hardware queue index
- `ndesc`: Number of descriptors in the ring
- `buf_size`: Buffer size for RX queues (0 for TX queues)
- `ring_base`: Base register offset for this ring's configuration

**Return Value:**
- `0`: Success
- `-ENOMEM`: Memory allocation failure

**Line-by-Line Logic:**

```c
Lines 35-40: Initialize queue structure
- Initialize spinlock for queue protection
- Set hardware index and descriptor count
- Initialize head/tail indices to 0
- Mark queue as not stopped

Lines 43-49: Allocate descriptor ring
- Calculate size: ndesc * sizeof(struct mt7927_desc)
- Use dma_alloc_coherent() for hardware-accessible memory
- Zero-initialize descriptors
- Error handling: free and return on failure

Lines 52-56: Allocate SKB pointer array
- Array to track which SKB corresponds to each descriptor
- Used for cleanup and completion handling

Lines 59-63: Allocate DMA address array
- Track DMA addresses for each descriptor
- Required for unmapping on completion

Lines 66-93: Pre-allocate RX buffers (if buf_size > 0)
- For RX queues, pre-allocate and map buffers
- Each buffer is allocated via dev_alloc_skb()
- DMA mapped with DMA_FROM_DEVICE direction
- Descriptor initialized with buffer address and size
- buf0: lower 32 bits of DMA address
- buf1: upper 32 bits (for 64-bit DMA)
- ctrl: buffer size (hardware will update with received length)

Lines 108-111: Configure hardware ring registers
- ring_base + 0x00: Descriptor ring base address (low 32 bits)
- ring_base + 0x04: Ring size (descriptor count)
- ring_base + 0x08: CPU index (initialized to 0)
- ring_base + 0x0c: DMA index (initialized to 0)
- wmb(): Memory barrier to ensure writes are visible to hardware

Lines 115-121: Verify ring configuration
- Read back base address to confirm write succeeded
- Warn if readback doesn't match (indicates write protection issue)
```

**Hardware Register Interactions:**

The function writes to ring configuration registers at `ring_base`:

| Offset | Register | Value | Purpose |
|--------|----------|-------|---------|
| +0x00 | `DESC_BASE` | `lower_32_bits(desc_dma)` | Ring base address (low) |
| +0x04 | `RING_SIZE` | `ndesc` | Number of descriptors |
| +0x08 | `CPU_IDX` | `0` | CPU write pointer |
| +0x0c | `DMA_IDX` | `0` | DMA read pointer |

**Important Notes:**
- Descriptors must be in coherent DMA memory (hardware accesses directly)
- For 64-bit DMA addresses, high bits are written to `EXT_CTRL` registers separately
- Ring registers are only writable when DMA is in reset state (`MT_WFDMA0_RST` bits set)

#### `mt7927_queue_free`

**Purpose:** Free all resources associated with a DMA queue.

**Parameters:**
- `dev`: Device structure pointer
- `q`: Queue structure to free

**Return Value:** None

**Line-by-Line Logic:**

```c
Lines 152-153: Early return if queue not allocated
- Check if descriptor ring exists before proceeding

Lines 156-163: Free RX buffers and DMA mappings
- Iterate through all descriptors
- For each SKB:
  - Unmap DMA address (dma_unmap_single)
  - Free SKB buffer
- Only needed for RX queues (TX queues free on completion)

Lines 165-166: Free tracking arrays
- kfree() for dma_addr and skb arrays

Lines 168-169: Free descriptor ring
- dma_free_coherent() releases coherent memory
- Hardware must be stopped before calling

Line 171: Zero-initialize queue structure
- Prevents use-after-free errors
```

**Hardware Register Interactions:** None (hardware should be stopped before cleanup)

### DMA TX Operations

#### `mt7927_tx_queue_skb`

**Purpose:** Queue an SKB for transmission via DMA.

**Parameters:**
- `dev`: Device structure pointer
- `q`: TX queue structure
- `skb`: Socket buffer to transmit

**Return Value:**
- `0`: Success
- `-ENOSPC`: Queue full
- `-ENOMEM`: DMA mapping failure

**Line-by-Line Logic:**

```c
Lines 189-196: Check queue space
- Acquire queue lock (spin_lock_irqsave)
- Calculate next head index
- Check if queue is full: (head + 1) % ndesc == tail
- If full, release lock and return -ENOSPC

Lines 199-203: Map SKB for DMA
- dma_map_single() with DMA_TO_DEVICE direction
- Maps SKB data buffer for hardware access
- Error handling: release lock and return on failure

Lines 206-207: Store SKB and DMA address
- Track SKB pointer for completion handling
- Store DMA address for unmapping later

Lines 210-214: Set up descriptor
- desc->buf0: Lower 32 bits of DMA address
- desc->buf1: Upper 32 bits (for 64-bit systems)
- desc->ctrl: SKB length | MT_DMA_CTL_LAST_SEC0
  - LAST_SEC0 indicates this is the last segment
- desc->info: Additional info (set to 0)
- wmb(): Ensure descriptor is written before updating index

Lines 218-221: Update head and notify hardware
- Increment head index (wrap around using modulo)
- Write head to MT_WFDMA0_TX_RING_CIDX(q->hw_idx)
  - Hardware reads this to know new descriptors are available
- Hardware will process descriptors from tail to head

Lines 224-231: Debug verification (optional)
- Read back CIDX, DIDX, BASE, CNT registers
- Useful for debugging DMA issues

Lines 233: Release lock and return
```

**Hardware Register Interactions:**

| Register | Purpose | Value |
|----------|---------|-------|
| `MT_WFDMA0_TX_RING_CIDX(q->hw_idx)` | CPU index | Updated `head` value |

**Important Notes:**
- Hardware processes descriptors in order from `tail` to `head`
- Descriptor must be fully written before updating `CIDX`
- Memory barrier (`wmb()`) ensures ordering

#### `mt7927_tx_complete`

**Purpose:** Process completed TX descriptors and free resources.

**Parameters:**
- `dev`: Device structure pointer
- `q`: TX queue structure

**Return Value:** None

**Line-by-Line Logic:**

```c
Lines 247-255: Check for completed descriptors
- Acquire queue lock
- Loop while tail != head (descriptors pending)
- Read descriptor at tail index
- Check MT_DMA_CTL_DMA_DONE bit in ctrl field
  - Hardware sets this when transmission completes
- Break if descriptor not yet completed

Lines 258-264: Free completed descriptor
- Unmap DMA address (dma_unmap_single)
- Free SKB buffer (dev_kfree_skb_irq for IRQ context)
- Clear tracking arrays (set to NULL/0)
- Clear descriptor ctrl field

Lines 270: Advance tail
- Increment tail index (wrap around)
- Hardware has finished processing this descriptor

Lines 274-277: Wake queue if stopped
- If queue was previously stopped due to full condition
- Mark as not stopped
- Signal that queue space is available
```

**Hardware Register Interactions:** None (reads descriptor memory only)

**Important Notes:**
- Called from interrupt context (IRQ handler/tasklet)
- Hardware sets `MT_DMA_CTL_DMA_DONE` when transmission completes
- Must unmap DMA before freeing SKB

### DMA RX Operations

#### `mt7927_rx_poll`

**Purpose:** Poll RX queue for received packets and process them.

**Parameters:**
- `dev`: Device structure pointer
- `q`: RX queue structure
- `budget`: Maximum number of packets to process

**Return Value:** Number of packets processed

**Line-by-Line Logic:**

```c
Lines 297-305: Check for received packets
- Acquire queue lock
- Loop up to budget times
- Read descriptor at tail index
- Check MT_DMA_CTL_DMA_DONE bit
  - Hardware sets this when packet received
- Break if no packet ready

Lines 308-313: Extract received length
- Read length from MT_DMA_CTL_SD_LEN0 field
- Get SKB pointer for this descriptor
- Skip if SKB missing (shouldn't happen)

Lines 316-317: Unmap received buffer
- dma_unmap_single() with DMA_FROM_DEVICE
- Hardware has finished reading the buffer

Lines 320-331: Allocate replacement buffer
- Try to allocate new SKB for next packet
- If allocation fails, reuse old buffer
- Remap old buffer and update descriptor
- Continue to next descriptor

Lines 334-347: Map new buffer
- Map new SKB buffer for DMA
- If mapping fails, reuse old buffer
- Update descriptor with new buffer address

Lines 350-366: Process received packet
- Set SKB length to received length (skb_put)
- For MCU queue: Add to response queue and wake waiters
- For data queue: Would pass to mac80211 (currently just freed)
- Increment processed count

Lines 371-377: Recycle descriptor
- Clear DMA done flag and reset length
- Set ctrl to buffer size (hardware will update on next RX)
- Update descriptor with new buffer address
- wmb(): Ensure descriptor written before updating index
- Update tail index
- Write tail to MT_WFDMA0_RX_RING_CIDX to notify hardware
```

**Hardware Register Interactions:**

| Register | Purpose | Value |
|----------|---------|-------|
| `MT_WFDMA0_RX_RING_CIDX(q->hw_idx)` | CPU index | Updated `tail` value |

**Important Notes:**
- Hardware writes received length to `ctrl` field
- Must allocate replacement buffer before processing packet
- If allocation fails, reuse old buffer to avoid losing RX capability
- MCU responses are queued for `mt7927_mcu_send_and_get_msg` to consume

### DMA Control

#### `mt7927_dma_prefetch`

**Purpose:** Configure DMA prefetch settings for optimal performance.

**Parameters:**
- `dev`: Device structure pointer (implicit via static function)

**Return Value:** None

**Line-by-Line Logic:**

```c
Lines 398-401: Configure RX ring prefetch
- Set prefetch base and depth for each RX ring
- Format: PREFETCH(base, depth) = (base << 16) | depth
- RX rings use smaller depth (0x4) for lower latency

Lines 404-409: Configure TX ring prefetch
- Set prefetch for TX rings
- TX rings use larger depth (0x10) for higher throughput
- Special handling for MCU rings (15, 16) with smaller depth

Line 411: Log configuration completion
```

**Hardware Register Interactions:**

| Register | Purpose | Value |
|----------|---------|-------|
| `MT_WFDMA0_RX_RING_EXT_CTRL(n)` | RX ring prefetch | `(base << 16) \| depth` |
| `MT_WFDMA0_TX_RING_EXT_CTRL(n)` | TX ring prefetch | `(base << 16) \| depth` |

**Important Notes:**
- Prefetch settings optimize DMA performance
- Must be called before enabling DMA
- Different depths for RX vs TX based on latency requirements

#### `mt7927_dma_disable`

**Purpose:** Disable DMA engine and optionally reset it.

**Parameters:**
- `dev`: Device structure pointer
- `force`: If true, force disable without waiting for idle

**Return Value:**
- `0`: Success
- `-ETIMEDOUT`: Timeout waiting for DMA to become idle

**Line-by-Line Logic:**

```c
Lines 427-430: Clear DMA enable bits
- Clear TX_DMA_EN and RX_DMA_EN bits
- Clear CSR_DISP_BASE_PTR_CHAIN_EN
- Hardware stops processing descriptors

Lines 433-445: Wait for DMA idle (if not forcing)
- Poll MT_WFDMA0_GLO_CFG register
- Check TX_DMA_BUSY and RX_DMA_BUSY bits
- Wait up to 100ms (1000 iterations * 100-200us)
- Return error on timeout

Lines 448-475: Force reset (if force=true)
- Read current reset state
- CRITICAL DISCOVERY: Ring registers only writable when RST bits are SET
- On fresh boot, RST=0x30 and registers work
- When RST cleared, registers become read-only
- Solution: Keep RST bits SET for ring configuration
- Only pulse reset if not already in reset state
- Leave RST bits SET for proper operation
```

**Hardware Register Interactions:**

| Register | Purpose | Action |
|----------|---------|--------|
| `MT_WFDMA0_GLO_CFG` | Global config | Clear enable bits |
| `MT_WFDMA0_RST` | Reset control | Set reset bits (keep set) |

**Important Notes:**
- **Critical Discovery**: Ring configuration registers are only writable when `MT_WFDMA0_RST` bits are SET
- Must keep reset bits set for proper ring configuration
- DMA enable will work correctly with reset bits set

#### `mt7927_dma_enable`

**Purpose:** Enable DMA engine with proper configuration.

**Parameters:**
- `dev`: Device structure pointer

**Return Value:**
- `0`: Success
- `-EIO`: Failed to enable DMA

**Line-by-Line Logic:**

```c
Line 488: Configure prefetch settings
- Call mt7927_dma_prefetch() first
- Must be done before enabling DMA

Lines 491-492: Reset DMA pointers
- Write 0xffffffff to reset TX and RX pointers
- Clears any stale indices

Line 495: Configure delay interrupt
- Set delay interrupt configuration to 0
- Controls interrupt coalescing

Lines 498-503: Verify reset state
- Read MT_WFDMA0_RST register
- IMPORTANT: Do NOT clear reset bits here
- Reference driver leaves RST=0x30
- Ring configuration requires RST bits set
- DMA works correctly with RST bits set

Lines 506-507: Read current GLO_CFG state
- For debugging and verification

Lines 510-518: Configure global DMA settings
- TX_DMA_EN: Enable TX DMA
- RX_DMA_EN: Enable RX DMA
- TX_WB_DDONE: Write-back DMA done flag
- RX_WB_DDONE: Write-back RX done flag
- FIFO_LITTLE_ENDIAN: Little-endian byte order
- CLK_GAT_DIS: Disable clock gating
- FIFO_DIS_CHECK: Disable FIFO check
- CSR_DISP_BASE_PTR_CHAIN_EN: Enable base pointer chaining
- DMA_SIZE: Set to 3 (64KB descriptor size)

Lines 521-522: Write configuration
- Use mt7927_set() to set bits
- wmb() ensures write ordering

Lines 525-533: Verify DMA enabled
- Read back GLO_CFG register
- Check that enable bits are set
- Return error if not enabled (write-protected?)
- Log firmware status for debugging

Lines 538-547: Verify ring configuration survived
- Read back ring base addresses
- Check that configuration wasn't wiped
- Warn if rings were cleared

Lines 550-554: Enable interrupts
- Enable TX done, RX done, and MCU command interrupts
- Hardware will signal completion via interrupts
```

**Hardware Register Interactions:**

| Register | Purpose | Value |
|----------|---------|-------|
| `MT_WFDMA0_RST_DTX_PTR` | Reset TX pointers | `0xffffffff` |
| `MT_WFDMA0_RST_DRX_PTR` | Reset RX pointers | `0xffffffff` |
| `MT_WFDMA0_PRI_DLY_INT_CFG0` | Delay interrupt | `0` |
| `MT_WFDMA0_GLO_CFG` | Global config | Multiple bits set |
| `MT_WFDMA0_HOST_INT_ENA` | Interrupt enable | TX/RX/MCU masks |

**Important Notes:**
- Prefetch must be configured before enabling DMA
- Reset bits (`MT_WFDMA0_RST`) should remain SET
- Ring configuration must survive DMA enable
- Interrupts must be enabled for completion handling

#### `mt7927_dma_init`

**Purpose:** Initialize all DMA queues and enable DMA subsystem.

**Parameters:**
- `dev`: Device structure pointer

**Return Value:**
- `0`: Success
- Negative error code on failure

**Line-by-Line Logic:**

```c
Line 573: Disable DMA first
- Call mt7927_dma_disable(dev, true) to force reset
- Ensures clean state

Line 578: Reset WPDMA
- Call mt7927_wpdma_reset(dev, true)
- Additional reset step

Lines 585-591: Allocate TX Queue 0 (Band0 Data)
- Allocate 2048 descriptors
- Ring base: MT_WFDMA0_TX_RING_BASE(0)
- Not strictly needed for firmware load but allocated anyway

Lines 594-601: Allocate TX Queue 1 (MCU WM)
- Allocate 256 descriptors for MCU commands
- Ring base: MT_WFDMA0_TX_RING_BASE(5)
- Store pointer in dev->q_mcu[MT_MCUQ_WM]

Lines 604-611: Allocate TX Queue 2 (FWDL)
- Allocate 128 descriptors for firmware download
- Ring base: MT_WFDMA0_TX_RING_BASE(4)
- Store pointer in dev->q_mcu[MT_MCUQ_FWDL]

Lines 614-616: Configure TX ring extension control
- Set 36-bit DMA address extension
- Value 0x4 enables high address bits

Lines 621-627: Allocate RX Queue 0 (MCU WM)
- Allocate 512 descriptors
- Pre-allocate 2048-byte buffers
- Ring base: MT_WFDMA0_RX_RING_BASE(0)
- For MCU command responses

Lines 630-636: Allocate RX Queue 2 (Band0 Data)
- Allocate 1536 descriptors
- Pre-allocate 2048-byte buffers
- Ring base: MT_WFDMA0_RX_RING_BASE(2)
- For data packet reception

Line 639: Enable DMA
- Call mt7927_dma_enable()
- Starts DMA engine processing
```

**Hardware Register Interactions:** See `mt7927_queue_alloc` and `mt7927_dma_enable`

**Important Notes:**
- Queues must be allocated before enabling DMA
- MCU queues are stored in `dev->q_mcu[]` array
- RX queues pre-allocate buffers for zero-copy operation
- Extension control registers configure 64-bit DMA addressing

#### `mt7927_dma_cleanup`

**Purpose:** Cleanup all DMA queues and disable DMA.

**Parameters:**
- `dev`: Device structure pointer

**Return Value:** None

**Line-by-Line Logic:**

```c
Line 661: Disable DMA
- Call mt7927_dma_disable(dev, true)
- Force disable without waiting

Lines 664-665: Free TX queues
- Iterate through all TX queues
- Call mt7927_queue_free() for each

Lines 668-669: Free RX queues
- Iterate through all RX queues
- Call mt7927_queue_free() for each

Lines 672-673: Clear MCU queue pointers
- Set all q_mcu[] pointers to NULL
- Prevents use-after-free
```

**Hardware Register Interactions:** See `mt7927_dma_disable` and `mt7927_queue_free`

---

## MCU Subsystem

### MCU Architecture Overview

The MCU (Microcontroller Unit) subsystem handles communication between the host driver and the WiFi firmware running on the chip. It uses a message-based protocol over DMA queues.

**⚠️ CRITICAL WARNING**: MT7927 ROM bootloader does NOT support mailbox command protocol! The mailbox-based functions documented below only work AFTER firmware has loaded and the MCU is running. For firmware loading, use **polling-based** approach (see [ZOUYONGHAO_ANALYSIS.md](ZOUYONGHAO_ANALYSIS.md)).

#### MCU Queues

- **TX Queue 15 (MCU_WM)**: Send MCU commands - **CONFIRMED via MT6639 analysis**
- **TX Queue 16 (FWDL)**: Send firmware data - **CONFIRMED via MT6639 analysis**
- **RX Queue 0 (WM)**: Receive MCU responses (only works after firmware loads)

#### MCU Message Format

MCU messages consist of:
1. **TX Descriptor** (`struct mt7927_mcu_txd`): Command header
2. **Payload**: Command-specific data
3. **Response** (`struct mt7927_mcu_rxd`): Response header + data

#### Sequence Numbers

- 4-bit sequence numbers (0-15) for matching requests/responses
- Incremented for each command
- Wrapped using `seq & 0xf`

### MCU Message Protocol

#### `mt7927_mcu_fill_message`

**Purpose:** Fill MCU message header with command information.

**Parameters:**
- `dev`: Device structure pointer
- `skb`: Socket buffer containing message data
- `cmd`: Command ID (may include extended ID)
- `seq`: Pointer to store sequence number

**Return Value:**
- `0`: Success
- Negative error code on failure

**Line-by-Line Logic:**

```c
Lines 42-43: Reserve header space
- skb_push() to make room for mt7927_mcu_txd header
- Zero-initialize header structure

Lines 46-47: Get sequence number
- Increment dev->mcu.seq
- Wrap to 4-bit range (0-15)
- Store in *seq for response matching

Lines 50-52: Set TX descriptor word 0
- MT_TXD0_TX_BYTES: Total message length (skb->len)
- MT_TXD0_PKT_FMT: Packet format (MT_PKT_TYPE_CMD)
- Convert to little-endian

Lines 55-60: Fill MCU header fields
- len: Message length (little-endian)
- pq_id: Priority queue ID (0x8000 = high priority)
- cid: Command ID (extracted from cmd)
- pkt_type: Packet type (MT_PKT_TYPE_CMD)
- set_query: MCU_SET (0) for commands
- seq: Sequence number for response matching

Lines 62-65: Handle extended command ID
- If cmd has MCU_CMD_FIELD_EXT_ID bit set
- Extract extended ID and store in ext_cid
- Set ext_cid_ack flag

Line 67: Set source-to-destination index
- S2D_IDX_MCU (0) indicates MCU destination
```

**Hardware Register Interactions:** None (prepares message in memory)

**Important Notes:**
- Header must be filled before queuing message
- Sequence number used to match responses
- Extended commands use ext_cid field

#### `mt7927_mcu_send_msg`

**Purpose:** Send MCU message (simplified wrapper).

**Parameters:**
- `dev`: Device structure pointer
- `cmd`: Command ID
- `data`: Message payload data
- `len`: Payload length
- `wait_resp`: Whether to wait for response

**Return Value:**
- `0`: Success
- Negative error code on failure

**Implementation:** Calls `mt7927_mcu_send_and_get_msg` with `ret_skb=NULL`

#### `mt7927_mcu_send_and_get_msg`

**Purpose:** Send MCU message and optionally get response.

**Parameters:**
- `dev`: Device structure pointer
- `cmd`: Command ID
- `data`: Message payload data
- `len`: Payload length
- `wait_resp`: Whether to wait for response
- `ret_skb`: Pointer to store response SKB (NULL if not needed)

**Return Value:**
- `0`: Success
- `-ENOMEM`: Memory allocation failure
- `-EINVAL`: Invalid queue
- `-ETIMEDOUT`: Response timeout
- `-EIO`: Response queue error

**Line-by-Line Logic:**

```c
Lines 106-110: Allocate SKB for message
- Allocate with extra space for header and alignment
- Reserve space for MCU header + padding
- skb_reserve() aligns data properly

Lines 113-114: Copy payload data
- If data provided, copy to SKB
- skb_put_data() appends data to SKB

Lines 117-121: Fill message header
- Call mt7927_mcu_fill_message()
- Fills header and gets sequence number
- Error handling: free SKB on failure

Lines 124-133: Select appropriate queue
- MCU_CMD_FW_SCATTER uses FWDL queue
- Other commands use WM queue
- Check queue is initialized
- Error handling: free SKB if queue missing

Lines 136-141: Queue message for transmission
- Call mt7927_tx_queue_skb()
- Hardware will DMA message to MCU
- Error handling: free SKB on failure

Lines 144-176: Wait for response (if requested)
- Wait on dev->mcu.wait wait queue
- Timeout: dev->mcu.timeout jiffies
- Check if response queue has data
- Return error on timeout

Lines 158-162: Get response from queue
- skb_dequeue() removes response SKB
- Error if queue empty (shouldn't happen)

Lines 165-169: Validate response sequence
- Compare rxd->seq with request seq
- Warn on mismatch (may be out-of-order response)

Lines 172-175: Return or free response
- If ret_skb provided, store response SKB
- Otherwise free response immediately
```

**Hardware Register Interactions:** None (uses DMA TX queue)

**Important Notes:**
- Response arrives via RX queue and is processed by `mt7927_rx_poll`
- MCU responses are queued in `dev->mcu.res_q`
- Sequence number matching ensures correct response pairing
- Timeout prevents indefinite blocking

### MCU Firmware Loading

**⚠️ CRITICAL**: The sequence below describes the **mailbox-based** protocol used by MT7925. **MT7927 ROM bootloader does NOT support mailbox protocol!** For MT7927, use the polling-based approach documented in [ZOUYONGHAO_ANALYSIS.md](ZOUYONGHAO_ANALYSIS.md):

1. Skip semaphore command (ROM doesn't support it)
2. Send firmware chunks WITHOUT waiting for responses (`wait_resp=false`)
3. Use time-based delays (5-50ms) instead of mailbox waits
4. Set SW_INIT_DONE bit manually instead of FW_START command
5. Poll status registers for completion

#### Firmware Loading Sequence (Mailbox-Based - MT7925 Style)

**Note**: This sequence works for MT7925 but NOT for MT7927 ROM bootloader.

1. **Acquire Patch Semaphore**: Request permission to load patch
2. **Load ROM Patch**: Download patch firmware in chunks
3. **Signal Patch Complete**: Notify MCU patch is loaded
4. **Release Patch Semaphore**: Release semaphore
5. **Load RAM Code**: Download main firmware
6. **Start Firmware**: Begin firmware execution

#### `mt7927_mcu_patch_sem_ctrl`

**Purpose:** Control patch semaphore (acquire/release).

**Parameters:**
- `dev`: Device structure pointer
- `get`: `true` to acquire, `false` to release

**Return Value:**
- `0`: Success (acquired or released)
- `1`: Patch already loaded (acquire only)
- Negative error code on failure

**Line-by-Line Logic:**

```c
Lines 192-194: Prepare request
- Create mt7927_patch_sem_req structure
- Set op to PATCH_SEM_GET or PATCH_SEM_RELEASE

Lines 199-202: Send command and wait for response
- Send MCU_CMD_PATCH_SEM_CONTROL command
- Wait for response (wait_resp=true)
- Error handling: return on failure

Lines 205-208: Parse response
- Extract response from RX descriptor
- Response is single byte status code

Lines 210-220: Handle acquire response
- PATCH_SEM_READY (1): Successfully acquired
- PATCH_SEM_NOT_READY (0): Patch already loaded
- Other: Error acquiring semaphore

Line 223: Handle release response
- Return status (should be 0 for success)
```

**Hardware Register Interactions:** None (MCU command)

**Important Notes:**
- Semaphore prevents concurrent patch loading
- If patch already loaded, can skip to RAM loading
- Must release semaphore after patch loading

#### `mt7927_mcu_start_patch`

**Purpose:** Signal that patch loading is complete.

**Parameters:**
- `dev`: Device structure pointer

**Return Value:**
- `0`: Success
- Negative error code on failure

**Implementation:** Sends `MCU_CMD_PATCH_FINISH_REQ` with empty payload

#### `mt7927_mcu_start_firmware`

**Purpose:** Start firmware execution.

**Parameters:**
- `dev`: Device structure pointer
- `addr`: Firmware entry point address (0 for default)

**Return Value:**
- `0`: Success
- Negative error code on failure

**Line-by-Line Logic:**

```c
Lines 244-247: Prepare start request
- override: 1 if addr provided, 0 for default
- addr: Entry point address (little-endian)

Line 249: Send MCU_CMD_START_FIRMWARE command
- Wait for response
- MCU will begin executing firmware
```

**Hardware Register Interactions:** None (MCU command)

#### `mt7927_mcu_send_firmware`

**Purpose:** Send firmware data chunk via scatter command.

**Parameters:**
- `dev`: Device structure pointer
- `cmd`: Scatter command ID
- `data`: Firmware data chunk
- `len`: Chunk length

**Return Value:**
- `0`: Success
- Negative error code on failure

**Line-by-Line Logic:**

```c
Lines 269-273: Allocate SKB for firmware chunk
- Allocate with header space
- Reserve space for alignment

Line 276: Copy firmware data
- skb_put_data() appends data to SKB

Lines 279-280: Fill header
- Push header space
- Zero-initialize header

Lines 282-283: Get sequence number
- Increment and wrap sequence

Lines 285-287: Set TX descriptor word 0
- TX_BYTES: Total length
- PKT_FMT: MT_PKT_TYPE_FW (firmware packet)

Lines 289-294: Fill MCU header
- len: Message length
- pq_id: Priority queue (0x8000)
- cid: Command ID (MCU_CMD_FW_SCATTER)
- pkt_type: MT_PKT_TYPE_FW
- seq: Sequence number
- s2d_index: S2D_IDX_MCU

Line 297: Queue via FWDL queue
- Use dev->q_mcu[MT_MCUQ_FWDL]
- Hardware DMAs firmware to MCU
```

**Hardware Register Interactions:** None (uses DMA TX queue)

**Important Notes:**
- Uses FWDL queue (not WM queue)
- Packet type is `MT_PKT_TYPE_FW` (not `MT_PKT_TYPE_CMD`)
- No response expected for firmware chunks

#### `mt7927_load_patch`

**Purpose:** Load ROM patch firmware.

**Parameters:**
- `dev`: Device structure pointer

**Return Value:**
- `0`: Success
- `-EINVAL`: Invalid firmware format
- Negative error code on failure

**Line-by-Line Logic:**

```c
Lines 313-323: Validate firmware
- Check firmware pointer exists
- Check size >= patch header size
- Parse patch header structure

Line 326: Extract number of regions
- n_region from sec_info field

Lines 331-332: Calculate section offset
- Offset after header + section headers
- Get pointer to section array

Lines 334-341: Process each region
- Loop through n_region sections
- Extract address and length from section info
- Validate offset doesn't exceed firmware size
- Get data pointer for this region

Lines 348-378: Send region data in chunks
- Loop while len > 0
- Calculate chunk size (max MT7927_FW_CHUNK_SIZE = 64KB)
- Create scatter command structure:
  - addr: Target address (little-endian)
  - len: Chunk length (little-endian)
  - mode: FW_MODE_DL (download mode)
- Send scatter header (mt7927_mcu_send_msg)
- Send firmware chunk (mt7927_mcu_send_firmware)
- Advance data pointer and address
- Small delay between chunks (100-200us)

Line 380: Update offset for next region
```

**Hardware Register Interactions:** None (MCU commands)

**Important Notes:**
- Patch firmware has header + section headers + data
- Each region loaded to specific address
- Chunks sent sequentially with delays
- No response expected for firmware chunks

#### `mt7927_load_ram`

**Purpose:** Load RAM code firmware.

**Parameters:**
- `dev`: Device structure pointer

**Return Value:**
- `0`: Success
- `-EINVAL`: Invalid firmware format
- Negative error code on failure

**Line-by-Line Logic:**

```c
Lines 391-401: Validate firmware
- Check firmware pointer exists
- Check size >= trailer size
- Trailer is at end of firmware file

Lines 404-405: Parse trailer
- Trailer contains region count and version
- Located at fw->data + fw->size - sizeof(trailer)

Lines 411-412: Calculate region header offset
- Region headers before trailer
- offset = fw->size - trailer_size - (n_region * region_size)

Lines 415-423: Process each region
- Loop through n_region regions
- Extract address and length
- Validate offset doesn't exceed firmware size
- Get data pointer for this region

Lines 431-460: Send region data in chunks
- Same chunking logic as patch loading
- Scatter command with FW_MODE_DL
- Send header then data chunk
- Delay between chunks

Line 462: Update offset for next region
```

**Hardware Register Interactions:** None (MCU commands)

**Important Notes:**
- RAM firmware has trailer at end (not header at start)
- Region headers before trailer
- Same chunking mechanism as patch loading
- Version string in trailer for verification

#### `mt7927_load_firmware`

**Purpose:** Complete firmware loading sequence.

**Parameters:**
- `dev`: Device structure pointer

**Return Value:**
- `0`: Success
- Negative error code on failure

**Line-by-Line Logic:**

```c
Lines 478-483: Request ROM patch firmware
- request_firmware() loads from filesystem
- File: MT7927_ROM_PATCH
- Store in dev->fw_patch

Lines 485-490: Request RAM firmware
- request_firmware() loads from filesystem
- File: MT7927_FIRMWARE_WM
- Store in dev->fw_ram
- Error handling: release patch on failure

Lines 493-503: Acquire patch semaphore
- Call mt7927_mcu_patch_sem_ctrl(dev, true)
- If ret == 1, patch already loaded (skip to RAM)
- Error handling: release firmware on failure

Lines 506-510: Load ROM patch
- Call mt7927_load_patch()
- Error handling: release semaphore and firmware

Lines 513-517: Signal patch complete
- Call mt7927_mcu_start_patch()
- Error handling: release semaphore and firmware

Lines 520-524: Release patch semaphore
- Call mt7927_mcu_patch_sem_ctrl(dev, false)
- Error handling: release firmware

Lines 528-532: Load RAM code
- Call mt7927_load_ram()
- Error handling: release firmware

Lines 535-539: Start firmware execution
- Call mt7927_mcu_start_firmware(dev, 0)
- Default entry point (addr=0)
- Error handling: release firmware

Lines 542-545: Wait for firmware ready
- msleep(100) to allow firmware initialization
- Set MCU state to FW_LOADED
- Set device state flag
```

**Hardware Register Interactions:** None (MCU commands)

**Important Notes:**
- Complete sequence must be followed in order
- Semaphore prevents concurrent patch loading
- Firmware files loaded from `/lib/firmware/`
- State tracking for MCU initialization status

### MCU Initialization

#### `mt7927_mcu_init`

**Purpose:** Initialize MCU subsystem and load firmware.

**Parameters:**
- `dev`: Device structure pointer

**Return Value:**
- `0`: Success
- Negative error code on failure

**Line-by-Line Logic:**

```c
Lines 581-586: Driver power control
- Call mt7927_mcu_drv_pmctrl()
- Ensures driver owns power control
- Continue on failure (non-fatal)

Lines 589-593: Disable L0S power saving
- Read MT_PCIE_MAC_PM register
- Set MT_PCIE_MAC_PM_L0S_DIS bit
- L0S can interfere with DMA operation
- Ensures stable DMA communication

Lines 596-600: Set firmware mode to normal
- Read MT_SWDEF_MODE register
- Write MT_SWDEF_NORMAL_MODE (0)
- Required before firmware download
- Prevents firmware from interfering

Lines 603-605: Enable MCU interrupts
- Enable TX complete interrupt (MCU commands)
- Enable RX complete interrupt (MCU responses)
- Enable MCU command interrupt
- Hardware will signal MCU events

Lines 608-610: Load firmware
- Call mt7927_load_firmware()
- Complete firmware loading sequence
- Error handling: return on failure

Lines 612-613: Set MCU state
- Set state to MT7927_MCU_STATE_RUNNING
- Set MT7927_STATE_MCU_RUNNING device flag
- MCU is now operational
```

**Hardware Register Interactions:**

| Register | Purpose | Value |
|----------|---------|-------|
| `MT_PCIE_MAC_PM` | PCIe power management | Set `L0S_DIS` bit |
| `MT_SWDEF_MODE` | Firmware mode | `MT_SWDEF_NORMAL_MODE` (0) |
| Interrupt enable registers | MCU interrupts | Enable MCU masks |

**Important Notes:**
- Power control must be established before MCU init
- L0S power saving disabled for DMA stability
- Firmware mode must be normal before download
- Interrupts must be enabled for MCU communication

#### `mt7927_mcu_exit`

**Purpose:** Shutdown MCU subsystem.

**Parameters:**
- `dev`: Device structure pointer

**Return Value:** None

**Line-by-Line Logic:**

```c
Line 627: Purge response queue
- skb_queue_purge() removes all pending responses
- Prevents memory leaks

Lines 630-631: Clear MCU state
- Clear MT7927_STATE_MCU_RUNNING flag
- Reset state to MT7927_MCU_STATE_INIT
```

**Hardware Register Interactions:** None

---

## Summary

### DMA Subsystem Key Points

1. **Ring-Based Architecture**: Circular descriptor rings shared between CPU and hardware
2. **Coherent Memory**: Descriptors must be in DMA-coherent memory
3. **Reset State**: Ring registers only writable when `MT_WFDMA0_RST` bits are SET
4. **Prefetch Configuration**: Must be set before enabling DMA
5. **Zero-Copy RX**: Pre-allocated buffers for efficient packet reception

### MCU Subsystem Key Points

1. **Message Protocol**: Command/response with sequence number matching
2. **Two-Stage Firmware**: ROM patch + RAM code loading
3. **Semaphore Control**: Prevents concurrent patch loading
4. **Scatter Download**: Firmware sent in 64KB chunks
5. **Queue Selection**: FWDL queue for firmware, WM queue for commands

### Critical Discoveries

1. **Ring Register Writable State**: Ring configuration registers are only writable when DMA reset bits are SET. This is counter-intuitive but critical for proper operation.

2. **Reset Bits Remain Set**: The reference driver leaves `MT_WFDMA0_RST` bits SET during normal operation. DMA works correctly in this state.

3. **BAR0 vs BAR2**: Real writable WFDMA registers are at BAR0+0x2000, not BAR2. BAR2 is read-only shadow.

---

## References

- Source files: `src/mt7927_dma.c`, `src/mt7927_mcu.c`
- Header files: `src/mt7927.h`, `src/mt7927_regs.h`, `src/mt7927_mcu.h`
- Reference driver: `reference/linux/drivers/net/wireless/mediatek/mt76/`
