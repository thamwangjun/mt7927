# MT7927 WiFi 7 Linux Driver Development - Session Bootstrap

## Critical Discovery

**MT7927 = MT7925 + 320MHz channels**. The MT7927 is architecturally identical to MT7925 (which has full Linux support since kernel 6.7). We can adapt the existing mt7925 driver rather than reverse engineering from scratch. MT7925 firmware files are compatible with MT7927.

## Current Status

**Status**: BLOCKED on DMA descriptor processing  
**Last Updated**: January 2026

### What's Working ✅
- Driver successfully binds to MT7927 hardware (PCI ID: 14c3:7927)
- Power management handshake completes (LPCTL: 0x04 → 0x00)
- WiFi subsystem reset completes (INIT_DONE achieved)
- DMA descriptor rings allocate and configure correctly
- Ring base addresses verified (TX0, TX4, TX5 all correct)
- L0S power saving disabled
- SWDEF_MODE set to normal
- Firmware files load into kernel memory (1.4MB RAM code + patch)
- Interrupts registered and enabled

### Current Blocker 🚧

**The DMA hardware does not process TX descriptors:**
- `TX Q5: CIDX=1 DIDX=0` — CPU writes descriptor (CIDX advances), but hardware never picks it up (DIDX stays 0)
- First MCU command `PATCH_SEM_CONTROL (0x0010)` times out after 3 seconds
- This prevents firmware activation

**The Catch-22 Situation:**
- When `MT_WFDMA0_RST = 0x30` (reset bits SET): Ring registers are writable, but DMA doesn't process descriptors
- When `MT_WFDMA0_RST = 0x00` (reset bits CLEAR): DMA could process, but ring configuration gets wiped immediately

**Key Observation:** The reference MT7925 driver leaves RST=0x30 and DMA works. MT7927 behaves differently.

---

## Project Structure

```
mt7927/
├── README.md                          # Project overview
├── AGENTS.md                          # This file - session bootstrap
├── DEVELOPMENT_LOG.md                 # Detailed chronological development history
├── Makefile                           # Main build system
│
├── src/                               # Production driver source
│   ├── mt7927.h                       # Main header: device structures, register access
│   ├── mt7927_regs.h                  # Register definitions, queue IDs, address mapping
│   ├── mt7927_mcu.h                   # MCU protocol definitions
│   ├── mt7927_pci.c                   # PCI probe/remove, power management, reset
│   ├── mt7927_dma.c                   # DMA queue management, ring configuration
│   └── mt7927_mcu.c                   # MCU communication, firmware loading
│
├── tests/                             # Test modules
│   ├── Kbuild                         # Kernel build configuration
│   └── 05_dma_impl/                   # DMA implementation tests
│       ├── test_power_ctrl.c          # Power management handshake test
│       ├── test_wfsys_reset.c         # WiFi subsystem reset test
│       ├── test_dma_queues.c          # DMA ring configuration test
│       └── test_fw_load.c             # Complete firmware loading sequence
│
├── diag/                              # Diagnostic modules
│   ├── mt7927_diag.c                  # Safe read-only diagnostic
│   ├── mt7927_minimal_scan.c          # Minimal BAR0 scan
│   ├── mt7927_power_unlock.c          # Power handshake testing
│   ├── mt7927_ring_test.c             # Ring writability testing
│   └── ... (more diagnostic modules)
│
├── docs/                              # Documentation
│   ├── README.md                      # Documentation index
│   ├── dma_transfer_implementation.plan.md  # Implementation plan with task status
│   ├── headers_documentation.md       # Header file documentation
│   ├── mt7927_pci_documentation.md    # PCI driver documentation
│   ├── dma_mcu_documentation.md       # DMA and MCU documentation
│   ├── diagnostic_modules_documentation.md
│   ├── test_modules_documentation.md
│   └── TEST_RESULTS_SUMMARY.md
│
└── reference/                         # Reference driver source (not committed)
    └── linux/drivers/net/wireless/mediatek/mt76/
```

---

## Key Technical Context

### Hardware Architecture

| Property | Value |
|----------|-------|
| Chip | MediaTek MT7927 WiFi 7 (802.11be) |
| PCI ID | 14c3:7927 |
| BAR0 | 2MB memory region (main registers) |
| BAR2 | 32KB window (read-only shadow at BAR0+0x10000) |
| TX Rings | 8 (0-7) — NOT 17 like MT7925 |
| RX Rings | 16 (0-15) |
| WFDMA1 | Does NOT exist on MT7927 |

### Critical Register Locations

| Register | BAR0 Offset | Purpose |
|----------|-------------|---------|
| WFDMA0_BASE | 0x2000 | Real writable DMA registers |
| WFDMA0_RST | 0x2100 | DMA reset control (bits 4,5) |
| WFDMA0_GLO_CFG | 0x2208 | DMA global config (TX/RX enable) |
| TX_RING_BASE(n) | 0x2300 + n*0x10 | TX ring configuration |
| RX_RING_BASE(n) | 0x2500 + n*0x10 | RX ring configuration |
| LPCTL | 0x7c060010 | Power management handshake |
| WFSYS_SW_RST_B | 0x7c000140 | WiFi subsystem reset |
| PCIE_MAC_PM | 0x74030194 | PCIe power management (L0S) |
| SWDEF_MODE | 0x0041f23c | Firmware mode register |

### Queue Assignments (MT7927-specific)

```c
// MT7927 only has 8 TX rings (0-7), NOT 17 like MT7925
MT7927_TXQ_BAND0   = 0,   // Band0 data
MT7927_TXQ_FWDL    = 4,   // Firmware download (was 16 on MT7925)
MT7927_TXQ_MCU_WM  = 5,   // MCU commands (was 15 on MT7925)
```

---

## What Was Already Tried

### Phase 1-4: Ring Configuration Issues
1. **Rings 15/16** (copied from MT7925) → Don't exist on MT7927
2. **Changed to rings 4/5** → Worked, but RST=0x00 wiped config
3. **Keep RST=0x30** → Rings survive, but DMA doesn't process
4. **Clear RST after config** → Immediately wipes ring configuration

### Phase 5-8: Initialization Fixes Applied
5. **Fixed WFSYS reset** → Wait 50ms, poll INIT_DONE (bit 4)
6. **Power control in MCU init** → Already acquired (no change)
7. **Disabled L0S power saving** → PCIE_MAC_PM bit 8 set
8. **Set SWDEF_MODE=0** → Normal mode before firmware download

**All these fixes are already in the code. The DMA processing issue persists.**

---

## Hypotheses for Next Investigation

1. **RST State Conflict**
   - Reference driver leaves RST=0x30, but maybe MT7927 needs different handling
   - Try: Find if there's a "soft" way to transition from config mode to run mode

2. **Different Register Offsets**
   - MT7927 might have different DMA control register layout
   - Try: Study MT7996 driver (newer chip, might have similar quirks)

3. **Missing Doorbell/Kick Register**
   - Some DMA engines need explicit trigger beyond CIDX write
   - Try: Look for additional registers that trigger DMA processing

4. **Descriptor Format Differences**
   - MT7927 might expect different TXD (TX descriptor) format
   - Try: Compare descriptor structure with MT7996

5. **Firmware Pre-presence Required**
   - Maybe firmware needs to be partially active before DMA works
   - Try: Check if there's a bootstrap mode we're missing

6. **Analyze 0xffff10f1 Status**
   - This value at BAR2[0x200] indicates chip state
   - May contain clues about what's blocking DMA

---

## Working Commands

```bash
# Check MT7927 detection
lspci -nn | grep 14c3:7927

# Build drivers/tests
cd ~/development/github/mt7927
make clean && make tests

# Load production driver
sudo rmmod mt7927 2>/dev/null
sudo insmod src/mt7927.ko
sudo dmesg | tail -50

# Check binding status
lspci -k | grep -A 3 "14c3:7927"

# If chip enters error state (registers read 0xffffffff)
DEVICE=$(lspci -nn | grep 14c3:7927 | cut -d' ' -f1)
echo 1 | sudo tee /sys/bus/pci/devices/0000:$DEVICE/remove
sleep 2
echo 1 | sudo tee /sys/bus/pci/rescan

# View specific diagnostic
sudo insmod diag/mt7927_minimal_scan.ko
sudo dmesg | tail -30
sudo rmmod mt7927_minimal_scan
```

---

## How to Assist

### When User Asks About Status
1. Driver binds, power/reset work, rings configure correctly
2. **BLOCKER**: DMA hardware doesn't process TX descriptors (DIDX stays 0)
3. First MCU command (0x0010 PATCH_SEM_CONTROL) times out
4. Root cause unknown — need to find what enables DMA processing

### When User Wants to Debug
1. Read `DEVELOPMENT_LOG.md` for full history and all attempts
2. Check `docs/` for detailed code documentation
3. Current driver is in `src/` directory
4. Test modules are in `tests/05_dma_impl/`
5. Safe diagnostics are in `diag/`

### When User Wants to Test New Theory
1. Create focused test module for specific hypothesis
2. Always check register states before/after changes
3. Use `dmesg | tail -50` to see kernel output
4. Keep tests small and incremental
5. Document findings in DEVELOPMENT_LOG.md

---

## Key Documentation to Read

| Document | Purpose |
|----------|---------|
| `DEVELOPMENT_LOG.md` | Complete chronological history, all attempts, all findings |
| `docs/dma_transfer_implementation.plan.md` | Implementation plan with task statuses |
| `docs/dma_mcu_documentation.md` | DMA and MCU subsystem technical details |
| `docs/mt7927_pci_documentation.md` | PCI initialization and power management |
| `docs/headers_documentation.md` | Register definitions and data structures |

---

## Reference Driver Location

The MT7925 driver (our reference implementation) is in the kernel source:
```
drivers/net/wireless/mediatek/mt76/mt7925/
├── pci.c         # PCI probe and initialization sequence
├── mcu.c         # MCU communication and firmware loading
├── init.c        # Hardware initialization
└── dma.c         # DMA setup and transfer
```

Also check `reference/` directory if local copy exists.

---

## Project Mission

**Goal**: Find the initialization sequence that enables MT7927's DMA engine to process TX descriptors, allowing firmware transfer and chip activation.

**Once DMA works**, the existing MCU code should successfully:
1. Acquire patch semaphore
2. Send firmware in chunks via FWDL queue
3. Signal patch completion
4. Load RAM code
5. Start firmware execution

The hardware works. The firmware is compatible. We just need to find what unlocks DMA processing on this specific chip variant.

---

## Quick Checklist for New Session

- [ ] Read this AGENTS.md for context
- [ ] Check `DEVELOPMENT_LOG.md` for detailed history
- [ ] Look at `docs/dma_transfer_implementation.plan.md` for task status
- [ ] Review last `dmesg` output if available
- [ ] Check if any new theories emerged from previous session
- [ ] Reference MT7925 source in kernel or `reference/` directory
