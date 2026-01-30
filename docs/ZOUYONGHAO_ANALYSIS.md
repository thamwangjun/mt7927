# Analysis of zouyonghao MT7927 Working Driver

**Date**: 2026-01-31
**Source**: https://github.com/zouyonghao/mt7927 (successful firmware loading branch)
**Status**: **WORKING** - Successfully loads firmware on MT7927

## Executive Summary

This driver reveals the **ROOT CAUSE** of our DMA blocker: **MT7927 ROM bootloader does NOT support WFDMA mailbox command protocol**. Our driver has been waiting for mailbox responses that the ROM will never send!

---

## Critical Discovery: No Mailbox Protocol

### The Problem We've Had

Our driver follows the standard mt76 firmware loading protocol:
1. Send PATCH_SEM_CONTROL command
2. **Wait for mailbox response** ← **BLOCKS HERE**
3. Send firmware chunks
4. Wait for completion responses
5. Send FW_START command

**MT7927 ROM bootloader doesn't support mailbox responses!** It only processes DMA transfers and never sends acknowledgments.

### The Solution

From `mt7927_fw_load.c`:

```c
/* MT7927 ROM bootloader does NOT support WFDMA mailbox command protocol.
 * We use the standard mt76 firmware loading BUT skip mailbox responses
 * since the ROM doesn't send them.
 */

// Send commands WITHOUT waiting for response
int mt7927_mcu_send_init_cmd(struct mt76_dev *dev, int cmd,
			      const void *data, int len)
{
	/* Send command but don't wait for response - ROM doesn't send them */
	return mt76_mcu_send_msg(dev, cmd, data, len, false);
	                                           // ^^^^^ CRITICAL: false = don't wait
}
```

---

## Firmware Loading Sequence (Working Approach)

### 1. **Skip Semaphore Command** (Line 137)
```c
/* NO SEMAPHORE - MT7927 ROM doesn't support it */
```

Our driver tries to send `PATCH_SEM_CONTROL` and waits forever. This driver **skips it entirely**.

### 2. **Aggressive TX Cleanup** (Lines 63-68, 81-85)
```c
/* Aggressive cleanup BEFORE sending - force=true to free all pending */
if (dev->queue_ops->tx_cleanup) {
	dev->queue_ops->tx_cleanup(dev,
				   dev->q_mcu[MT_MCUQ_FWDL],
				   true);  // force = true
}

/* Send data chunk without waiting for response */
err = mt76_mcu_send_msg(dev, cmd, data, cur_len, false);

/* Cleanup AFTER sending to process what we just sent */
if (dev->queue_ops->tx_cleanup) {
	dev->queue_ops->tx_cleanup(dev,
				   dev->q_mcu[MT_MCUQ_FWDL],
				   true);
}
```

**Key**: Force cleanup before AND after each chunk to ensure descriptors are freed.

### 3. **Polling Delays for ROM Processing** (Lines 91, 147, 167)
```c
/* Brief delay to let MCU process buffer (reduced from 25ms) */
msleep(5);

/* Small delay for ROM to process */
msleep(10);

/* Give ROM time to apply patch */
msleep(50);
```

Instead of waiting for mailbox responses, use **time-based polling** with delays.

### 4. **Skip FW_START Command** (Lines 270-285)
```c
/* MT7927: Skip FW_START - it's a mailbox command that ROM doesn't support.
 * The firmware should already be executing after we sent all regions.
 * As a nudge, set AP2WF SW_INIT_DONE bit to signal host is ready. */

u32 ap2wf = __mt76_rr(dev, 0x7C000140);
__mt76_wr(dev, 0x7C000140, ap2wf | BIT(4));
dev_info(dev->dev, "[MT7927] Set AP2WF SW_INIT_DONE (0x7C000140 |= BIT4)\n");
```

**Manually signal completion** instead of sending FW_START mailbox command.

### 5. **Check MCU Status Register** (Lines 171-173, 262-265, 280-282)
```c
/* Check MCU status after patch */
u32 val = __mt76_rr(dev, 0x7c060204);
dev_info(dev->dev, "[MT7927] MCU status after patch: 0x%08x\n", val);

/* Check MCU ready status in MT_CONN_ON_MISC (0x7c0600f0) */
u32 mcu_ready = __mt76_rr(dev, 0x7c0600f0);
dev_info(dev->dev, "[MT7927] MCU ready register (0x7c0600f0) = 0x%08x\n", mcu_ready);
```

**Poll status registers** instead of waiting for interrupts/mailbox.

---

## Initialization Sequence

### Phase 1: Pre-Init (Before DMA Setup)

From `pci_mcu.c::mt7927e_mcu_pre_init()` (lines 70-128):

```c
1. Force conninfra wakeup (0x7C0601A0 = 0x1)
2. Poll for CONN_INFRA version (0x7C011000, expect 0x03010002)
3. WiFi subsystem reset (0x70028600)
4. Set Crypto MCU ownership (0x70025380)
5. Wait for MCU IDLE state (0x81021604 = 0x1D1E)
```

**Critical**: MCU must reach IDLE (0x1D1E) before firmware loading!

### Phase 2: DMA Setup (Standard mt76)

Standard ring initialization - nothing MT7927-specific here.

### Phase 3: Post-DMA Init

From `pci_mcu.c::mt7927e_mcu_init()` (lines 131-237):

```c
1. Set PCIE2AP remap for mailbox (0x7C021034 = 0x18051803)
2. Configure WFDMA MSI interrupt routing
3. Configure WFDMA extensions (flow control, thresholds)
4. Power management handshake
5. Disable ASPM L0s (MT_PCIE_MAC_PM)
6. Mark MCU as running (skip standard run_firmware)
```

**Note**: Line 218 disables only L0s, NOT L1!

### Phase 4: Firmware Loading (Polling Mode)

From `mt792x_core.c` (lines 925-937):

```c
if (is_mt7927(&dev->mt76)) {
	/* Use MT7927-specific loader that polls DMA instead of using mailbox */
	ret = mt7927_load_patch(&dev->mt76, mt792x_patch_name(dev));
	ret = mt7927_load_ram(&dev->mt76, mt792x_ram_name(dev));
}
```

---

## ASPM Configuration

### What We Found

From `pci.c` line 24-26:
```c
static bool mt7925_disable_aspm;
module_param_named(disable_aspm, mt7925_disable_aspm, bool, 0644);
MODULE_PARM_DESC(disable_aspm, "disable PCI ASPM support");
```

From `pci_mcu.c` line 218:
```c
mt76_rmw_field(dev, MT_PCIE_MAC_PM, MT_PCIE_MAC_PM_L0S_DIS, 1);
```

**Finding**: This driver **only disables L0s**, same as ours! It does NOT disable L1/L1.1/L1.2.

**Implication**: L1 ASPM might not be the blocker after all. The real blocker is the **mailbox protocol assumption**.

---

## Key Registers (from mt7927_regs.h)

### MCU Status Registers
```c
0x81021604  WF_TOP_CFG_ON_ROMCODE_INDEX - MCU state (expect 0x1D1E = IDLE)
0x7c060204  MCU status register
0x7c0600f0  MCU ready status (MT_CONN_ON_MISC)
```

### Power Management
```c
0x7C0601A0  CONN_INFRA_CFG_PWRCTRL0 - Conninfra wakeup
0x7C011000  CONN_INFRA_CFG_VERSION - HW version (expect 0x03010002)
```

### Reset Control
```c
0x70028600  CB_INFRA_RGU_WF_SUBSYS_RST - WiFi subsystem reset
0x70028610  CB_INFRA_RGU_BT_SUBSYS_RST - BT subsystem reset
0x70025380  CB_INFRA_SLP_CTRL_CRYPTO_MCU_OWN_SET
```

### PCIe Remap
```c
0x70026554  CB_INFRA_MISC0_CBTOP_PCIE_REMAP_WF
0x70026558  CB_INFRA_MISC0_CBTOP_PCIE_REMAP_WF_BT
0x7C021034  CONN_BUS_CR_VON_PCIE2AP_REMAP_WF_1_BA
```

### Completion Signaling
```c
0x7C000140  CONN_INFRA_CFG_AP2WF_BUS
  Bit 4: WFSYS_SW_INIT_DONE - Set to signal host ready
```

---

## Comparison with Our Driver

| Aspect | Our Driver | Working Driver | Impact |
|--------|------------|----------------|--------|
| **Semaphore** | Sends PATCH_SEM_CONTROL | Skips entirely | **CRITICAL** - We block here! |
| **Mailbox wait** | Waits for responses | Never waits (false param) | **CRITICAL** - Causes timeout! |
| **TX cleanup** | After send only | Before AND after, force=true | **HIGH** - Descriptor exhaustion? |
| **Delays** | None | 5-50ms between operations | **MEDIUM** - ROM needs processing time |
| **FW_START** | Would send | Skips, sets SW_INIT_DONE | **HIGH** - Wrong completion signal |
| **MCU IDLE check** | Not done | Polls 0x81021604 for 0x1D1E | **HIGH** - Pre-condition not met |
| **ASPM L1** | Enabled | Enabled | **NONE** - Not the blocker! |

---

## Root Cause Analysis

### Why Our Driver Blocks

1. **PATCH_SEM_CONTROL sent** → ROM ignores it (doesn't understand mailbox protocol)
2. **Driver waits for response** → ROM never responds
3. **DMA_DIDX never advances** → Because we're waiting in wrong place!
4. **Timeout after 5 seconds** → MCU command never completes

### Why DMA Looks "Stuck"

DMA is actually **working correctly**! The problem is:
- We send a command the ROM doesn't understand
- We wait for a response that will never come
- We time out before trying the next operation
- We never see DMA process because **we're using the wrong protocol**

### The Real Blocker

**Not ASPM L1. Not ring assignments. Not DMA configuration.**

**It's the mailbox protocol assumption!**

MT7927 ROM bootloader is a **minimalist firmware** that:
- Processes DMA descriptors ✓
- Executes firmware loading commands ✓
- Does **NOT** send mailbox responses ✗
- Does **NOT** support semaphore protocol ✗

---

## Implementation Requirements for Our Driver

### Must Change

1. **Remove semaphore command** - Don't send PATCH_SEM_CONTROL
2. **Never wait for mailbox responses** - Set `wait_resp = false` in all mcu_send_msg calls
3. **Add aggressive TX cleanup** - Force cleanup before and after each chunk
4. **Add polling delays** - 5-10ms between operations for ROM processing
5. **Skip FW_START** - Manually set SW_INIT_DONE bit instead
6. **Check MCU IDLE** - Poll 0x81021604 for 0x1D1E before firmware loading
7. **Poll status registers** - Check 0x7c060204 and 0x7c0600f0 for completion

### Can Keep

1. **ASPM L0s disable** - Same as working driver
2. **Ring assignments (15/16)** - Validated via MT6639 analysis
3. **DMA ring configuration** - Already correct
4. **Power management** - LPCTL handshake works
5. **WFSYS reset** - Already working

---

## Quick Win: Minimal Changes Test

To test the mailbox hypothesis with minimal code changes:

```c
// In mt7927_mcu.c - change this one line:
// OLD:
ret = mt76_mcu_send_and_get_msg(&dev->mt76, MCU_CMD_PATCH_SEM_CONTROL,
                                &req, sizeof(req), true, &skb);
// NEW:
ret = mt76_mcu_send_msg(&dev->mt76, MCU_CMD_PATCH_SEM_CONTROL,
                       &req, sizeof(req), false);  // Don't wait!

// Skip the response parsing
// Just proceed with firmware loading
```

**Expected Result**: DMA_DIDX should advance because we're not blocking on mailbox response!

---

## Confidence Assessment

| Finding | Confidence | Evidence |
|---------|------------|----------|
| Mailbox protocol is the blocker | **99%** | Explicit comments in working code |
| DMA is actually working | **95%** | No DMA-specific fixes in working driver |
| ASPM L1 is NOT the blocker | **90%** | Working driver doesn't disable L1 |
| Polling delays are necessary | **85%** | ROM needs time to process |
| MCU IDLE check is important | **80%** | Pre-init phase checks this |

---

## Next Steps

### Priority 1: Test Mailbox Hypothesis
1. Modify `mt7927_mcu.c` to skip mailbox waits
2. Add polling delays (5-10ms)
3. Skip semaphore command entirely
4. Test if DMA_DIDX advances

### Priority 2: Implement Full Polling Protocol
1. Create `mt7927_fw_load.c` based on zouyonghao code
2. Add MCU IDLE pre-check
3. Implement aggressive TX cleanup
4. Add status register polling

### Priority 3: Verify Completion
1. Check 0x81021604 for MCU IDLE
2. Poll 0x7c060204 for MCU status
3. Set 0x7C000140 bit 4 for SW_INIT_DONE
4. Verify network interface creation

---

## References

- Working driver: https://github.com/zouyonghao/mt7927
- `mt7927_fw_load.c` - Polling-based firmware loader (NO mailbox)
- `pci_mcu.c` - MT7927-specific MCU initialization
- `mt7927_regs.h` - MT6639/MT7927 register definitions

## Conclusion

**The DMA system works fine.** We've been using the wrong communication protocol. MT7927 ROM bootloader is a simple firmware that processes DMA but doesn't implement mailbox responses. Switching to a polling-based approach (like zouyonghao's driver) should immediately resolve our "DMA blocker".
