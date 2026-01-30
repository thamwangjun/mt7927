// SPDX-License-Identifier: GPL-2.0
/*
 * MT7927 TX Ring Scanner (READ-ONLY)
 *
 * Safely scans TX rings 0-17 to determine which rings exist on MT7927.
 * This module only READS registers - no writes are performed.
 *
 * Expected result: Rings 0-7 should show valid register patterns,
 * Rings 8-17 should show 0x00000000 or 0xFFFFFFFF (non-existent).
 *
 * Usage:
 *   sudo insmod mt7927_ring_scan_readonly.ko
 *   sudo dmesg | grep -A 50 "MT7927 TX Ring Scanner"
 *   sudo rmmod mt7927_ring_scan_readonly
 */

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/delay.h>

#define MT7927_VENDOR_ID    0x14c3
#define MT7927_DEVICE_ID    0x7927

/* WFDMA0 base at BAR0 + 0x2000 */
#define MT_WFDMA0_BASE      0x2000

/* TX Ring register offsets (relative to WFDMA0_BASE) */
#define MT_TX_RING_BASE(n)  (0x300 + (n) * 0x10)
#define MT_TX_RING_CNT(n)   (0x304 + (n) * 0x10)
#define MT_TX_RING_CIDX(n)  (0x308 + (n) * 0x10)
#define MT_TX_RING_DIDX(n)  (0x30c + (n) * 0x10)

/* TX Ring EXT_CTRL registers (for prefetch config) */
#define MT_TX_RING_EXT_CTRL(n)  (0x600 + (n) * 0x04)

/* Other key registers */
#define MT_WFDMA0_RST       0x100
#define MT_WFDMA0_GLO_CFG   0x208
#define MT_WFDMA0_INT_STA   0x200
#define MT_WFDMA0_INT_ENA   0x204

/* Maximum ring to scan (MT7925 uses up to ring 17) */
#define MAX_TX_RING_SCAN    18

struct ring_info {
    int ring_num;
    u32 base;
    u32 cnt;
    u32 cidx;
    u32 didx;
    u32 ext_ctrl;
    bool likely_valid;
};

static void __iomem *bar0;
static struct pci_dev *g_pdev;

static u32 safe_read(void __iomem *base, u32 offset)
{
    return readl(base + MT_WFDMA0_BASE + offset);
}

static int __init mt7927_ring_scan_init(void)
{
    struct ring_info rings[MAX_TX_RING_SCAN];
    int i, valid_count = 0, invalid_count = 0;
    u32 rst, glo_cfg, int_sta;

    pr_info("========================================\n");
    pr_info("MT7927 TX Ring Scanner (READ-ONLY)\n");
    pr_info("========================================\n");

    g_pdev = pci_get_device(MT7927_VENDOR_ID, MT7927_DEVICE_ID, NULL);
    if (!g_pdev) {
        pr_err("MT7927: Device not found\n");
        return -ENODEV;
    }

    if (pci_enable_device(g_pdev)) {
        pr_err("MT7927: Failed to enable device\n");
        pci_dev_put(g_pdev);
        return -EIO;
    }

    /* Map BAR0 - need at least 0x2700 for ring 17 EXT_CTRL */
    bar0 = pci_iomap(g_pdev, 0, 0x3000);
    if (!bar0) {
        pr_err("MT7927: Failed to map BAR0\n");
        pci_disable_device(g_pdev);
        pci_dev_put(g_pdev);
        return -ENOMEM;
    }

    /* Read chip identification */
    pr_info("\n--- Chip Identification ---\n");
    pr_info("Chip ID:    0x%08x\n", readl(bar0 + 0x0000));
    pr_info("HW Rev:     0x%08x\n", readl(bar0 + 0x0004));

    /* Read DMA state registers */
    pr_info("\n--- DMA State ---\n");
    rst = safe_read(bar0, MT_WFDMA0_RST);
    glo_cfg = safe_read(bar0, MT_WFDMA0_GLO_CFG);
    int_sta = safe_read(bar0, MT_WFDMA0_INT_STA);

    pr_info("RST:        0x%08x (bits 4,5 = logic/dmashdl reset)\n", rst);
    pr_info("GLO_CFG:    0x%08x (bit0=TX_EN, bit2=RX_EN)\n", glo_cfg);
    pr_info("INT_STA:    0x%08x\n", int_sta);
    pr_info("INT_ENA:    0x%08x\n", safe_read(bar0, MT_WFDMA0_INT_ENA));

    /* Scan all TX rings 0-17 */
    pr_info("\n--- TX Ring Scan (Rings 0-%d) ---\n", MAX_TX_RING_SCAN - 1);
    pr_info("Ring | BASE       | CNT    | CIDX | DIDX | EXT_CTRL   | Status\n");
    pr_info("-----|------------|--------|------|------|------------|--------\n");

    for (i = 0; i < MAX_TX_RING_SCAN; i++) {
        rings[i].ring_num = i;
        rings[i].base = safe_read(bar0, MT_TX_RING_BASE(i));
        rings[i].cnt = safe_read(bar0, MT_TX_RING_CNT(i));
        rings[i].cidx = safe_read(bar0, MT_TX_RING_CIDX(i));
        rings[i].didx = safe_read(bar0, MT_TX_RING_DIDX(i));
        rings[i].ext_ctrl = safe_read(bar0, MT_TX_RING_EXT_CTRL(i));

        /*
         * Heuristic for "likely valid" ring:
         * - CNT should be non-zero and reasonable (not 0xFFFFFFFF)
         * - Or BASE/CNT/CIDX/DIDX all zero (unconfigured but exists)
         * - Invalid rings typically read as all 0x00000000 or garbage
         *
         * Key insight: On MT7925, unconfigured rings show CNT=0x200 (default)
         * Invalid (non-existent) rings show CNT=0x00000000
         */
        rings[i].likely_valid = (rings[i].cnt != 0 &&
                                 rings[i].cnt != 0xFFFFFFFF &&
                                 rings[i].cnt <= 0x10000);

        if (rings[i].likely_valid)
            valid_count++;
        else
            invalid_count++;

        pr_info("%4d | 0x%08x | %6d | %4d | %4d | 0x%08x | %s\n",
                i,
                rings[i].base,
                rings[i].cnt,
                rings[i].cidx,
                rings[i].didx,
                rings[i].ext_ctrl,
                rings[i].likely_valid ? "VALID" : "INVALID");
    }

    /* Summary */
    pr_info("\n--- Summary ---\n");
    pr_info("Rings that appear VALID:   %d\n", valid_count);
    pr_info("Rings that appear INVALID: %d\n", invalid_count);

    pr_info("\nValid ring list: ");
    for (i = 0; i < MAX_TX_RING_SCAN; i++) {
        if (rings[i].likely_valid)
            pr_cont("%d ", i);
    }
    pr_cont("\n");

    pr_info("Invalid ring list: ");
    for (i = 0; i < MAX_TX_RING_SCAN; i++) {
        if (!rings[i].likely_valid)
            pr_cont("%d ", i);
    }
    pr_cont("\n");

    /* Analysis for MCU ring selection */
    pr_info("\n--- Analysis for MCU Ring Selection ---\n");
    if (valid_count == 8) {
        pr_info("CONFIRMED: MT7927 has exactly 8 TX rings (0-7)\n");
        pr_info("Available for MCU: Rings 2-7 (0-1 typically for data)\n");
        pr_info("Current assumption: FWDL=4, MCU_WM=5\n");
    } else if (valid_count < 8) {
        pr_info("WARNING: Found only %d valid rings - fewer than expected!\n", valid_count);
    } else {
        pr_info("UNEXPECTED: Found %d valid rings - more than 8!\n", valid_count);
        pr_info("MT7925-style rings 15/16 might exist after all?\n");
    }

    pr_info("\n========================================\n");
    pr_info("Scan complete - module will now unload\n");
    pr_info("========================================\n");

    /* Cleanup */
    pci_iounmap(g_pdev, bar0);
    pci_disable_device(g_pdev);
    pci_dev_put(g_pdev);

    return -EAGAIN;  /* Prevent module from staying loaded */
}

static void __exit mt7927_ring_scan_exit(void)
{
    /* Nothing to do - cleanup done in init */
}

module_init(mt7927_ring_scan_init);
module_exit(mt7927_ring_scan_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("MT7927 TX Ring Scanner (Read-Only)");
MODULE_AUTHOR("MT7927 Development");
