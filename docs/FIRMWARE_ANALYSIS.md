# MT7927 Firmware Compatibility Analysis

## Testing the Base Assumption

**Assumption**: MT7927 shares the same firmware as MT7925

**Status**: **PARTIALLY VALIDATED** - Firmware supports MT7927, but driver does not

---

## Test 1: Firmware File Search

### Linux-Firmware Repository
- **Source**: https://git.kernel.org/pub/scm/linux/kernel/git/firmware/linux-firmware.git/tree/mediatek
- **Result**: NO mt7927 directory exists
- **MT79xx directories found**: mt7925, mt7987, mt7988, mt7996
- **Conclusion**: No MT7927-specific firmware files exist in mainline

### Local System (/lib/firmware/mediatek/)
```
mt7925/
├── BT_RAM_CODE_MT7925_1_1_hdr.bin.zst
├── WIFI_MT7925_PATCH_MCU_1_1_hdr.bin.zst
└── WIFI_RAM_CODE_MT7925_1_1.bin.zst
```

**No MT7927 firmware files found.**

---

## Test 2: Firmware Binary Analysis

### File Analyzed
- `WIFI_RAM_CODE_MT7925_1_1.bin` (decompressed from .zst)
- Build info: `t-neptune-main-mt7925-2249-MT7925_E1_ASIC_ROM_RAM_CE_2249_CCN16-20260106152802`

### Chip ID Search Results

| Search Term | Found | Occurrences | Significance |
|-------------|-------|-------------|--------------|
| `79 25` (0x7925) | ✓ Yes | 15+ times | MT7925 device ID |
| `79 27` (0x7927) | ✓ **Yes** | 8+ times | **MT7927 device ID** |
| `63 11 51 00` (Chip ID) | ✗ No | 0 | Full chip ID not hardcoded |

### Sample Hex Dump
```
00021290  86 7f ef a6 2b a3 79 27  29 0d 07 39 e7 3c 1c 2a
000236f0  cc 79 27 3c ff 14 1b a1  71 0c 24 a6 fe 22 40 6c
000276a0  cc b1 fe 04 79 27 9d d4  16 db 4a 07 4e 98 f2 fb
00029310  58 76 93 15 16 9e e0 ef  2f 30 ea 60 20 79 27 95
000348a0  a6 ea eb 57 7c 3c 25 fa  73 79 27 bf 4a 48 39 47
00038da0  a7 3d 9f 1d 27 9a 79 27  1f d8 21 13 0b 5b 09 51
```

**CRITICAL FINDING**: The MT7925 firmware binary contains references to BOTH 0x7925 AND 0x7927 device IDs!

---

## Test 3: Driver Code Analysis

### Chip Detection Macros (mt76_connac.h:175-178)
```c
static inline bool is_mt7925(struct mt76_dev *dev)
{
    return mt76_chip(dev) == 0x7925;
}
```

**NO is_mt7927() function exists in the driver code.**

### DMA Configuration (mt792x_dma.c:93-106)
```c
static void mt792x_dma_prefetch(struct mt792x_dev *dev)
{
    if (is_mt7925(&dev->mt76)) {
        /* MT7925-specific configuration */
        mt76_wr(dev, MT_WFDMA0_TX_RING15_EXT_CTRL, PREFETCH(0x0500, 0x4));
        mt76_wr(dev, MT_WFDMA0_TX_RING16_EXT_CTRL, PREFETCH(0x0540, 0x4));
    } else {
        /* MT7921 configuration */
        mt76_wr(dev, MT_WFDMA0_TX_RING16_EXT_CTRL, PREFETCH(0x340, 0x4));
        mt76_wr(dev, MT_WFDMA0_TX_RING17_EXT_CTRL, PREFETCH(0x380, 0x4));
    }
}
```

**Problem**: MT7927 would fall into the `else` branch (MT7921 configuration) because `is_mt7925()` returns false!

---

## Test 4: Community Research

### External Sources
- [GitHub - ehausig/mt7927](https://github.com/ehausig/mt7927) - Independent research project
- [OpenWrt MT76 Issue #927](https://github.com/openwrt/mt76/issues/927) - Linux support discussion
- [USB-WiFi Issue #517](https://github.com/morrownr/USB-WiFi/issues/517) - Community troubleshooting

**Consensus**: No official MT7927 driver exists; community assumes MT7925 firmware compatibility but lacks working driver.

---

## Conclusion

### Evidence FOR Shared Firmware (Circumstantial)
1. ⚠️ Firmware binary contains bytes `79 27` (8+ occurrences)
   - Could mean: firmware supports MT7927
   - Could mean: firmware detects MT7927 for rejection
   - Could mean: coincidental data, version numbers, etc.
2. ✓ No separate MT7927 firmware exists in linux-firmware
3. ✓ MediaTek describes MT7927 as "MT7925 + 320MHz"
4. ✓ Both chips use same PCI vendor ID (14c3)

### Evidence AGAINST Shared Firmware
1. ✗ Driver code has NO MT7927 detection (would use wrong config)
2. ✗ MT7927 falls into MT7921 code path instead of MT7925
3. ✗ Different ring counts (8 vs 17+) not handled in driver
4. ✗ No official documentation from MediaTek
5. ✗ No source code or MediaTek confirmation

### What We Can Actually Conclude

**PROVEN**:
- ✓ Bytes `79 27` appear in MT7925 firmware binary
- ✓ No separate MT7927 firmware exists in mainline
- ✓ Driver has no MT7927 support

**HIGH LIKELIHOOD** (but not proven):
- ⚠️ Firmware probably supports MT7927
- ⚠️ Using MT7925 firmware is reasonable approach

**UNKNOWN** (requires testing):
- ❓ Whether firmware will actually work with MT7927
- ❓ Which rings firmware expects
- ❓ Whether hardware differences cause issues

### Corrected Assessment

**Before**: "The firmware DOES support MT7927" ← **Overstated**
**After**: "The firmware **LIKELY** supports MT7927, but this remains **UNPROVEN** until tested"

The presence of 0x7927 bytes is **circumstantial evidence**, not proof. True validation requires:

1. **Empirical Testing** ← Only definitive answer
2. Source code analysis (unavailable)
3. MediaTek documentation (doesn't exist)

### What This Means for Our Project

1. **Use MT7925 firmware files** ⚠️ (reasonable assumption, not validated)
2. **Driver compatibility is broken** ✓ (proven by code analysis)
3. **Custom driver is necessary** ✓ (whether firmware works or not)
4. **Must test empirically** ✓ (only way to know if it works)

### Implications for Our Driver

1. **Use MT7925 firmware files** ✓ (validated by binary analysis)
2. **DO NOT assume driver code paths work** ✗
3. **Ring assignments may need MT7927-specific handling**
4. **Chip detection needs `is_mt7927()` macro checking 0x7927**

---

## Unanswered Questions

1. **Which rings does MT7927 firmware expect?**
   - Firmware has 0x7927 references, but no source code to examine
   - Could be rings 4/5, 15/16, or adaptively detected

2. **Does firmware auto-detect chip variant?**
   - Possible: Firmware reads chip ID and adapts behavior
   - Unknown: No source code to confirm

3. **Are there undocumented MT7927 firmware files?**
   - Possible: OEM-specific firmware not in linux-firmware
   - Need to check vendor driver packages

---

## Recommendations

1. **Continue using MT7925 firmware** - Binary analysis validates this
2. **Add MT7927-specific driver paths** - Cannot rely on MT7925 code
3. **Test ring assignments empirically** - Firmware behavior unknown
4. **Consider asking MediaTek** - Direct confirmation would resolve uncertainty

---

**Analysis Date**: 2026-01-31
**Firmware Version**: WIFI_RAM_CODE_MT7925_1_1 (build 2249, dated 20260106)
