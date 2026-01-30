// SPDX-License-Identifier: GPL-2.0
/*
 * MT7927 TX Ring Scanner (READ-WRITE)
 *
 * Tests TX rings 0-17 by writing and reading back test patterns.
 * This definitively proves which rings are writable (and thus exist).
 *
 * *** CAUTION ***
 * This module WRITES to hardware registers. Previous write tests have
 * caused kernel panics. Safety measures included:
 * 1. Only writes to ring BASE registers (safest)
 * 2. Restores original values after testing
 * 3. Uses memory barriers
 * 4. Optional "dry_run" mode (reads only)
 *
 * To run in safe dry-run mode (reads only):
 *   sudo insmod mt7927_ring_scan_readwrite.ko dry_run=1
 *
 * To run with actual writes:
 *   sudo insmod mt7927_ring_scan_readwrite.ko dry_run=0
 *
 * Default is dry_run=1 for safety.
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

/* Other key registers */
#define MT_WFDMA0_RST       0x100
#define MT_WFDMA0_GLO_CFG   0x208

/* Maximum ring to scan */
#define MAX_TX_RING_SCAN    18

/* Test pattern - distinctive value unlikely to be a real address */
#define TEST_PATTERN        0xDEAD0000

/* Module parameter for safety */
static int dry_run = 1;
module_param(dry_run, int, 0444);
MODULE_PARM_DESC(dry_run, "1=read only (default), 0=perform writes");

struct ring_test_result {
    int ring_num;
    u32 original_base;
    u32 original_cnt;
    u32 read_after_write;
    bool write_succeeded;
    bool is_writable;
};

static void __iomem *bar0;
static struct pci_dev *g_pdev;

static u32 safe_read(void __iomem *base, u32 offset)
{
    return readl(base + MT_WFDMA0_BASE + offset);
}

static void safe_write(void __iomem *base, u32 offset, u32 value)
{
    writel(value, base + MT_WFDMA0_BASE + offset);
    wmb();  /* Ensure write completes */
}

static int __init mt7927_ring_scan_rw_init(void)
{
    struct ring_test_result results[MAX_TX_RING_SCAN];
    int i, writable_count = 0;
    u32 rst, glo_cfg;
    u32 test_value;

    pr_info("================================================\n");
    pr_info("MT7927 TX Ring Scanner (READ-WRITE)\n");
    pr_info("================================================\n");
    pr_info("Mode: %s\n", dry_run ? "DRY RUN (read-only)" : "WRITE TEST");

    if (!dry_run) {
        pr_warn("*** WARNING: Write mode enabled! ***\n");
        pr_warn("*** Writes to non-existent rings may cause issues ***\n");
    }

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

    /* Map BAR0 */
    bar0 = pci_iomap(g_pdev, 0, 0x3000);
    if (!bar0) {
        pr_err("MT7927: Failed to map BAR0\n");
        pci_disable_device(g_pdev);
        pci_dev_put(g_pdev);
        return -ENOMEM;
    }

    /* Check chip state */
    pr_info("\n--- Pre-test State ---\n");
    pr_info("Chip ID:  0x%08x\n", readl(bar0 + 0x0000));

    rst = safe_read(bar0, MT_WFDMA0_RST);
    glo_cfg = safe_read(bar0, MT_WFDMA0_GLO_CFG);

    pr_info("RST:      0x%08x\n", rst);
    pr_info("GLO_CFG:  0x%08x\n", glo_cfg);

    /*
     * IMPORTANT: Ring registers are only writable when RST bits 4,5 are SET.
     * If RST=0x00, writes will silently fail.
     */
    if (!(rst & 0x30)) {
        pr_warn("\n*** WARNING: RST=0x%08x (bits 4,5 clear) ***\n", rst);
        pr_warn("*** Ring registers may be READ-ONLY in this state! ***\n");
        pr_warn("*** Results may show all rings as non-writable ***\n\n");
    }

    /* Scan all rings */
    pr_info("\n--- Ring Scan ---\n");
    pr_info("Ring | Orig BASE  | Orig CNT | ");
    if (!dry_run)
        pr_cont("After Write | Restored | Writable\n");
    else
        pr_cont("(dry run - no writes)\n");

    pr_info("-----|------------|----------|");
    if (!dry_run)
        pr_cont("------------|----------|----------\n");
    else
        pr_cont("\n");

    for (i = 0; i < MAX_TX_RING_SCAN; i++) {
        results[i].ring_num = i;
        results[i].original_base = safe_read(bar0, MT_TX_RING_BASE(i));
        results[i].original_cnt = safe_read(bar0, MT_TX_RING_CNT(i));
        results[i].write_succeeded = false;
        results[i].is_writable = false;

        if (dry_run) {
            /* Dry run - just report what we read */
            pr_info("%4d | 0x%08x | %8d | (skipped)\n",
                    i, results[i].original_base, results[i].original_cnt);
            continue;
        }

        /* Write test pattern */
        test_value = TEST_PATTERN | i;  /* Include ring number for uniqueness */

        safe_write(bar0, MT_TX_RING_BASE(i), test_value);
        udelay(10);  /* Small delay for register to settle */

        /* Read back */
        results[i].read_after_write = safe_read(bar0, MT_TX_RING_BASE(i));

        /* Check if write succeeded */
        results[i].write_succeeded = (results[i].read_after_write == test_value);
        results[i].is_writable = results[i].write_succeeded;

        if (results[i].is_writable)
            writable_count++;

        /* Restore original value */
        safe_write(bar0, MT_TX_RING_BASE(i), results[i].original_base);
        udelay(10);

        /* Verify restore */
        {
            u32 restored = safe_read(bar0, MT_TX_RING_BASE(i));
            pr_info("%4d | 0x%08x | %8d | 0x%08x | 0x%08x | %s\n",
                    i,
                    results[i].original_base,
                    results[i].original_cnt,
                    results[i].read_after_write,
                    restored,
                    results[i].is_writable ? "YES" : "NO");
        }
    }

    /* Summary */
    pr_info("\n--- Summary ---\n");

    if (dry_run) {
        pr_info("Dry run mode - no write tests performed.\n");
        pr_info("Rings with CNT != 0 (likely valid): ");
        for (i = 0; i < MAX_TX_RING_SCAN; i++) {
            if (results[i].original_cnt != 0 &&
                results[i].original_cnt != 0xFFFFFFFF)
                pr_cont("%d ", i);
        }
        pr_cont("\n");
    } else {
        pr_info("Writable rings: %d\n", writable_count);
        pr_info("Writable ring list: ");
        for (i = 0; i < MAX_TX_RING_SCAN; i++) {
            if (results[i].is_writable)
                pr_cont("%d ", i);
        }
        pr_cont("\n");

        pr_info("Non-writable ring list: ");
        for (i = 0; i < MAX_TX_RING_SCAN; i++) {
            if (!results[i].is_writable)
                pr_cont("%d ", i);
        }
        pr_cont("\n");

        /* Analysis */
        pr_info("\n--- Analysis ---\n");
        if (writable_count == 8) {
            pr_info("CONFIRMED: MT7927 has exactly 8 writable TX rings\n");

            /* Check which rings are writable */
            bool has_0_7 = true;
            for (i = 0; i < 8; i++) {
                if (!results[i].is_writable) {
                    has_0_7 = false;
                    break;
                }
            }
            if (has_0_7) {
                pr_info("Writable rings are 0-7 as expected.\n");
                pr_info("MCU rings should use from this set.\n");
            }
        } else if (writable_count == 0) {
            pr_info("NO WRITABLE RINGS - check RST register state!\n");
            pr_info("RST bits 4,5 must be SET for ring registers to be writable.\n");
        } else {
            pr_info("Found %d writable rings (expected 8)\n", writable_count);
        }

        /* Specific check for rings 4,5 (current MCU assumption) */
        pr_info("\n--- MCU Ring Check ---\n");
        pr_info("Ring 4 (FWDL):   %s\n",
                results[4].is_writable ? "WRITABLE" : "NOT WRITABLE");
        pr_info("Ring 5 (MCU_WM): %s\n",
                results[5].is_writable ? "WRITABLE" : "NOT WRITABLE");

        /* Check rings 15,16 (MT7925-style) */
        pr_info("Ring 15 (MT7925 MCU_WM): %s\n",
                results[15].is_writable ? "WRITABLE (unexpected!)" : "NOT WRITABLE");
        pr_info("Ring 16 (MT7925 FWDL):   %s\n",
                results[16].is_writable ? "WRITABLE (unexpected!)" : "NOT WRITABLE");
    }

    pr_info("\n================================================\n");
    pr_info("Scan complete - module will now unload\n");
    pr_info("================================================\n");

    /* Cleanup */
    pci_iounmap(g_pdev, bar0);
    pci_disable_device(g_pdev);
    pci_dev_put(g_pdev);

    return -EAGAIN;  /* Prevent module from staying loaded */
}

static void __exit mt7927_ring_scan_rw_exit(void)
{
    /* Nothing to do - cleanup done in init */
}

module_init(mt7927_ring_scan_rw_init);
module_exit(mt7927_ring_scan_rw_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("MT7927 TX Ring Scanner (Read-Write with Safety)");
MODULE_AUTHOR("MT7927 Development");
