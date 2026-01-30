// SPDX-License-Identifier: GPL-2.0
/*
 * MT7927 Power Unlock Test
 * 
 * Tests if the power handshake sequence unlocks ring registers.
 * Follows exact mt7925 probe sequence.
 */

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/delay.h>

#define MT7927_VENDOR_ID    0x14c3
#define MT7927_DEVICE_ID    0x7927

/* Register addresses */
#define MT_CONN_ON_LPCTL        0x7c060010
#define PCIE_LPCR_HOST_SET_OWN  BIT(0)
#define PCIE_LPCR_HOST_CLR_OWN  BIT(1)
#define PCIE_LPCR_HOST_OWN_SYNC BIT(2)

#define MT_HW_CHIPID            0x70010200
#define MT_HW_REV               0x70010204

/* WFDMA at BAR0+0x2000 */
#define WFDMA0_BASE             0x2000
#define WFDMA0_RST              (WFDMA0_BASE + 0x100)
#define WFDMA0_GLO_CFG          (WFDMA0_BASE + 0x208)
#define WFDMA0_TX0_BASE         (WFDMA0_BASE + 0x300)
#define WFDMA0_TX0_CNT          (WFDMA0_BASE + 0x304)

static void __iomem *bar0;
static struct pci_dev *pdev;

/* Fixed map for address translation (simplified) */
static u32 translate_addr(u32 addr)
{
    /* For addresses >= 0x70000000, need L1/L2 remap */
    /* For now, just handle the key ones */
    if (addr >= 0x70000000 && addr < 0x80000000) {
        /* CONN_INFRA region - use fixed offset 0xE0000 in BAR0 */
        return 0xE0000 + (addr & 0xFFFF);
    }
    if (addr >= 0x7c000000 && addr < 0x7d000000) {
        /* LPCTL region - need special handling */
        /* Based on mt792x fixed_map: 0x7c000000 -> 0x0E0000 */
        return 0xE0000 + (addr & 0xFFFFF);
    }
    return addr;
}

static u32 reg_read(u32 addr)
{
    u32 phys = translate_addr(addr);
    return readl(bar0 + phys);
}

static void reg_write(u32 addr, u32 val)
{
    u32 phys = translate_addr(addr);
    writel(val, bar0 + phys);
    wmb();
}

static bool poll_reg(u32 addr, u32 mask, u32 expected, int timeout_ms)
{
    int i;
    for (i = 0; i < timeout_ms; i++) {
        u32 val = reg_read(addr);
        if ((val & mask) == expected)
            return true;
        usleep_range(1000, 2000);
    }
    return false;
}

static int do_fw_pmctrl(void)
{
    u32 val;
    int i;
    
    pr_info("Step 1: Give ownership to firmware (SET_OWN)\n");
    
    for (i = 0; i < 10; i++) {
        reg_write(MT_CONN_ON_LPCTL, PCIE_LPCR_HOST_SET_OWN);
        
        if (poll_reg(MT_CONN_ON_LPCTL, PCIE_LPCR_HOST_OWN_SYNC, 
                     PCIE_LPCR_HOST_OWN_SYNC, 50)) {
            val = reg_read(MT_CONN_ON_LPCTL);
            pr_info("  FW ownership acquired (LPCTL=0x%08x)\n", val);
            return 0;
        }
    }
    
    val = reg_read(MT_CONN_ON_LPCTL);
    pr_err("  FW ownership FAILED (LPCTL=0x%08x)\n", val);
    return -ETIMEDOUT;
}

static int do_drv_pmctrl(void)
{
    u32 val;
    int i;
    
    pr_info("Step 2: Claim ownership for driver (CLR_OWN)\n");
    
    for (i = 0; i < 10; i++) {
        reg_write(MT_CONN_ON_LPCTL, PCIE_LPCR_HOST_CLR_OWN);
        usleep_range(2000, 3000);
        
        if (poll_reg(MT_CONN_ON_LPCTL, PCIE_LPCR_HOST_OWN_SYNC, 0, 50)) {
            val = reg_read(MT_CONN_ON_LPCTL);
            pr_info("  Driver ownership acquired (LPCTL=0x%08x)\n", val);
            return 0;
        }
    }
    
    val = reg_read(MT_CONN_ON_LPCTL);
    pr_err("  Driver ownership FAILED (LPCTL=0x%08x)\n", val);
    return -ETIMEDOUT;
}

static void test_ring_writeability(const char *phase)
{
    u32 before, after, test_val = 0xCAFEBABE;
    
    pr_info("Testing ring writeability (%s):\n", phase);
    
    /* Test TX0_BASE */
    before = readl(bar0 + WFDMA0_TX0_BASE);
    writel(test_val, bar0 + WFDMA0_TX0_BASE);
    wmb();
    after = readl(bar0 + WFDMA0_TX0_BASE);
    pr_info("  TX0_BASE: before=0x%08x, wrote=0x%08x, read=0x%08x -> %s\n",
            before, test_val, after,
            (after == test_val) ? "WRITABLE!" : "read-only");
    
    /* Restore */
    writel(before, bar0 + WFDMA0_TX0_BASE);
    wmb();
    
    /* Test TX0_CNT */
    before = readl(bar0 + WFDMA0_TX0_CNT);
    writel(test_val, bar0 + WFDMA0_TX0_CNT);
    wmb();
    after = readl(bar0 + WFDMA0_TX0_CNT);
    pr_info("  TX0_CNT:  before=0x%08x, wrote=0x%08x, read=0x%08x -> %s\n",
            before, test_val, after,
            (after == test_val) ? "WRITABLE!" : "read-only");
    
    /* Restore */
    writel(before, bar0 + WFDMA0_TX0_CNT);
    wmb();
}

static int __init mt7927_power_unlock_init(void)
{
    int ret;
    u32 chip_id, rev, lpctl;
    
    pr_info("MT7927 Power Unlock Test\n");
    pr_info("========================\n");
    
    pdev = pci_get_device(MT7927_VENDOR_ID, MT7927_DEVICE_ID, NULL);
    if (!pdev) {
        pr_err("MT7927: Device not found\n");
        return -ENODEV;
    }
    
    ret = pci_enable_device(pdev);
    if (ret) {
        pr_err("MT7927: Failed to enable device\n");
        pci_dev_put(pdev);
        return ret;
    }
    
    pci_set_master(pdev);
    
    /* Map enough of BAR0 to access LPCTL at 0xE0000+ */
    bar0 = pci_iomap(pdev, 0, 0x100000);  /* 1MB should be enough */
    if (!bar0) {
        pr_err("MT7927: Failed to map BAR0\n");
        pci_disable_device(pdev);
        pci_dev_put(pdev);
        return -ENOMEM;
    }
    
    /* Check initial state */
    pr_info("Initial state:\n");
    pr_info("  RST:     0x%08x\n", readl(bar0 + WFDMA0_RST));
    pr_info("  GLO_CFG: 0x%08x\n", readl(bar0 + WFDMA0_GLO_CFG));
    
    lpctl = reg_read(MT_CONN_ON_LPCTL);
    pr_info("  LPCTL:   0x%08x\n", lpctl);
    
    /* Test ring writeability BEFORE power handshake */
    test_ring_writeability("BEFORE power handshake");
    
    /* Do power handshake following mt7925 sequence */
    ret = do_fw_pmctrl();
    if (ret) {
        pr_warn("FW pmctrl failed, continuing anyway...\n");
    }
    
    ret = do_drv_pmctrl();
    if (ret) {
        pr_warn("Driver pmctrl failed, continuing anyway...\n");
    }
    
    /* Check state after power handshake */
    pr_info("\nAfter power handshake:\n");
    pr_info("  RST:     0x%08x\n", readl(bar0 + WFDMA0_RST));
    pr_info("  GLO_CFG: 0x%08x\n", readl(bar0 + WFDMA0_GLO_CFG));
    
    /* Try to read chip ID (uses translated address) */
    chip_id = reg_read(MT_HW_CHIPID);
    rev = reg_read(MT_HW_REV);
    pr_info("  Chip ID: 0x%08x (via translated addr)\n", chip_id);
    pr_info("  HW Rev:  0x%08x\n", rev);
    
    /* Test ring writeability AFTER power handshake */
    test_ring_writeability("AFTER power handshake");
    
    /* Cleanup */
    pci_iounmap(pdev, bar0);
    pci_disable_device(pdev);
    pci_dev_put(pdev);
    
    pr_info("\nPower unlock test complete\n");
    
    return -ENODEV;
}

static void __exit mt7927_power_unlock_exit(void)
{
}

module_init(mt7927_power_unlock_init);
module_exit(mt7927_power_unlock_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("MT7927 Power Unlock Test");
MODULE_AUTHOR("MT7927 Development");
