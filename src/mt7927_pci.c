// SPDX-License-Identifier: GPL-2.0
/*
 * MT7927 WiFi 7 Linux Driver - PCI Interface
 * 
 * Handles PCI probe/remove, power management, system reset, and IRQ
 *
 * Copyright (C) 2024 MT7927 Linux Driver Project
 */

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/interrupt.h>

#include "mt7927.h"
#include "mt7927_mcu.h"

/* Default IRQ map */
static const struct mt7927_irq_map mt7927_irq_map = {
    .host_irq_enable = MT_WFDMA0_HOST_INT_ENA,
    .tx = {
        .all_complete_mask = MT_INT_TX_DONE_ALL,
        .mcu_complete_mask = MT_INT_TX_DONE_MCU,
    },
    .rx = {
        .data_complete_mask = HOST_RX_DONE_INT_ENA2,
        .wm_complete_mask = HOST_RX_DONE_INT_ENA0,
        .wm2_complete_mask = HOST_RX_DONE_INT_ENA1,
    },
};

/* ============================================
 * Power Management Control
 * ============================================ */

/**
 * mt7927_mcu_fw_pmctrl - Release power control to firmware
 * 
 * This function tells the chip that the firmware will manage power.
 * Write SET_OWN (BIT 0) and wait for OWN_SYNC (BIT 2) to become set.
 */
int mt7927_mcu_fw_pmctrl(struct mt7927_dev *dev)
{
    int i;
    u32 val;

    /* Check current state */
    val = mt7927_rr(dev, MT_CONN_ON_LPCTL);
    dev_info(dev->dev, "LPCTL before fw_pmctrl: 0x%08x\n", val);

    /* Write SET_OWN to give ownership to firmware */
    dev_info(dev->dev, "Writing SET_OWN to LPCTL...\n");
    mt7927_wr(dev, MT_CONN_ON_LPCTL, PCIE_LPCR_HOST_SET_OWN);

    /* Wait for OWN_SYNC to be set (indicating FW owns) */
    for (i = 0; i < 2000; i++) {
        val = mt7927_rr(dev, MT_CONN_ON_LPCTL);
        if (val & PCIE_LPCR_HOST_OWN_SYNC) {
            dev_info(dev->dev, "FW power control acquired after %d iterations (LPCTL: 0x%08x)\n", i, val);
            return 0;
        }
        usleep_range(500, 1000);
    }

    dev_err(dev->dev, "Timeout waiting for FW power control (LPCTL: 0x%08x)\n", val);
    return -ETIMEDOUT;
}

/**
 * mt7927_mcu_drv_pmctrl - Acquire power control from firmware
 * 
 * This function tells the chip that the driver will manage power.
 * Write CLR_OWN (BIT 1) and wait for OWN_SYNC (BIT 2) to clear.
 */
int mt7927_mcu_drv_pmctrl(struct mt7927_dev *dev)
{
    int i;
    u32 val;

    /* Check current ownership state */
    val = mt7927_rr(dev, MT_CONN_ON_LPCTL);
    dev_info(dev->dev, "LPCTL before drv_pmctrl: 0x%08x\n", val);

    if (!(val & PCIE_LPCR_HOST_OWN_SYNC)) {
        dev_info(dev->dev, "Driver already owns chip\n");
        return 0;
    }

    /* Write CLR_OWN to claim ownership for driver */
    dev_info(dev->dev, "Writing CLR_OWN to LPCTL...\n");
    mt7927_wr(dev, MT_CONN_ON_LPCTL, PCIE_LPCR_HOST_CLR_OWN);

    /* Wait for OWN_SYNC to clear (indicating driver owns) */
    for (i = 0; i < 2000; i++) {
        val = mt7927_rr(dev, MT_CONN_ON_LPCTL);
        if (!(val & PCIE_LPCR_HOST_OWN_SYNC)) {
            dev_info(dev->dev, "Driver power control acquired after %d iterations (LPCTL: 0x%08x)\n", i, val);
            return 0;
        }
        usleep_range(500, 1000);
    }

    dev_err(dev->dev, "Timeout waiting for driver power control (LPCTL: 0x%08x)\n", val);
    return -ETIMEDOUT;
}

/* ============================================
 * WiFi System Reset
 * ============================================ */

/**
 * mt7927_wfsys_reset - Reset the WiFi subsystem
 * 
 * This resets the WiFi firmware and hardware to a clean state,
 * preparing it for firmware loading.
 */
int mt7927_wfsys_reset(struct mt7927_dev *dev)
{
    u32 val;
    int i;

    dev_info(dev->dev, "Resetting WiFi subsystem...\n");

    /* Read current state */
    val = mt7927_rr(dev, MT_WFSYS_SW_RST_B);
    dev_info(dev->dev, "WFSYS_SW_RST_B before: 0x%08x\n", val);

    /* Assert reset - clear bit 0 (matches mt792x_wfsys_reset) */
    mt7927_clear(dev, MT_WFSYS_SW_RST_B, MT_WFSYS_SW_RST_B_EN);
    
    /* Wait 50ms for reset to take effect (matches reference) */
    msleep(50);

    /* Deassert reset - set bit 0 */
    mt7927_set(dev, MT_WFSYS_SW_RST_B, MT_WFSYS_SW_RST_B_EN);

    /* Poll for INIT_DONE (bit 4) - up to 500ms (matches reference) */
    for (i = 0; i < 50; i++) {
        val = mt7927_rr(dev, MT_WFSYS_SW_RST_B);
        if (val & MT_WFSYS_SW_INIT_DONE) {
            dev_info(dev->dev, "WiFi subsystem reset complete (0x%08x after %dms)\n",
                     val, i * 10);
            return 0;
        }
        msleep(10);
    }

    val = mt7927_rr(dev, MT_WFSYS_SW_RST_B);
    dev_err(dev->dev, "WiFi subsystem reset timeout (0x%08x)\n", val);
    return -ETIMEDOUT;
}

/**
 * mt7927_wpdma_reset - Reset WPDMA engine
 * @dev: device structure
 * @force: force reset even if DMA is busy
 */
int mt7927_wpdma_reset(struct mt7927_dev *dev, bool force)
{
    int ret;

    /* Disable DMA first */
    ret = mt7927_dma_disable(dev, force);
    if (ret && !force)
        return ret;

    /* Reset TX DMA pointers */
    mt7927_wr(dev, MT_WFDMA0_RST_DTX_PTR, 0xffffffff);
    
    /* Reset RX DMA pointers */  
    mt7927_wr(dev, MT_WFDMA0_RST_DRX_PTR, 0xffffffff);

    /* Small delay for reset to take effect */
    usleep_range(100, 200);

    return 0;
}

/* ============================================
 * IRQ Handling
 * ============================================ */

/**
 * mt7927_irq_enable - Enable specific interrupts
 */
void mt7927_irq_enable(struct mt7927_dev *dev, u32 mask)
{
    mt7927_set(dev, dev->irq_map->host_irq_enable, mask);
}

/**
 * mt7927_irq_disable - Disable specific interrupts
 */
void mt7927_irq_disable(struct mt7927_dev *dev, u32 mask)
{
    mt7927_clear(dev, dev->irq_map->host_irq_enable, mask);
}

/**
 * mt7927_irq_tasklet - Deferred interrupt handler
 */
void mt7927_irq_tasklet(unsigned long data)
{
    struct mt7927_dev *dev = (struct mt7927_dev *)data;
    u32 intr, mask;

    /* Read and acknowledge interrupts */
    intr = mt7927_rr(dev, MT_WFDMA0_HOST_INT_STA);
    mt7927_wr(dev, MT_WFDMA0_HOST_INT_STA, intr);

    dev_info(dev->dev, "IRQ tasklet: intr=0x%08x\n", intr);

    if (!intr)
        return;

    /* Process TX completion */
    if (intr & dev->irq_map->tx.all_complete_mask) {
        /* TX queue 0 (data) */
        if (intr & MT_INT_TX_DONE_BAND0)
            mt7927_tx_complete(dev, &dev->tx_q[0]);

        /* MCU WM queue (ring 15) - tx_q[1]
         * Fallback: If using rings 4/5, change to HOST_TX_DONE_INT_ENA5 */
        if (intr & HOST_TX_DONE_INT_ENA15)
            mt7927_tx_complete(dev, &dev->tx_q[1]);

        /* FWDL queue (ring 16) - tx_q[2]
         * Fallback: If using rings 4/5, change to HOST_TX_DONE_INT_ENA4 */
        if (intr & HOST_TX_DONE_INT_ENA16)
            mt7927_tx_complete(dev, &dev->tx_q[2]);
    }

    /* Process RX completion */
    if (intr & dev->irq_map->rx.wm_complete_mask) {
        /* MCU RX - firmware responses */
        mt7927_rx_poll(dev, &dev->rx_q[MT7927_RXQ_MCU_WM], 16);
    }

    if (intr & dev->irq_map->rx.data_complete_mask) {
        /* Data RX */
        mt7927_rx_poll(dev, &dev->rx_q[MT7927_RXQ_BAND0], 64);
    }

    /* MCU command notification */
    if (intr & MT_INT_MCU_CMD) {
        wake_up(&dev->mcu.wait);
    }

    /* Re-enable interrupts */
    mask = dev->irq_map->tx.all_complete_mask |
           MT_INT_RX_DONE_ALL |
           MT_INT_MCU_CMD;
    mt7927_irq_enable(dev, mask);
}

/**
 * mt7927_irq_handler - Top-half interrupt handler
 */
irqreturn_t mt7927_irq_handler(int irq, void *dev_instance)
{
    struct mt7927_dev *dev = dev_instance;
    u32 intr;

    /* Quick check for our interrupt */
    intr = mt7927_rr(dev, MT_WFDMA0_HOST_INT_STA);
    if (!intr)
        return IRQ_NONE;

    /* Disable further interrupts and schedule tasklet */
    mt7927_wr(dev, dev->irq_map->host_irq_enable, 0);
    tasklet_schedule(&dev->irq_tasklet);

    return IRQ_HANDLED;
}

/* ============================================
 * PCI Driver Interface
 * ============================================ */

static int mt7927_pci_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
    struct mt7927_dev *dev;
    int ret;
    u16 cmd;

    dev_info(&pdev->dev, "MT7927 WiFi 7 device found (PCI ID: %04x:%04x)\n",
             pdev->vendor, pdev->device);

    /* Allocate device structure */
    dev = devm_kzalloc(&pdev->dev, sizeof(*dev), GFP_KERNEL);
    if (!dev)
        return -ENOMEM;

    dev->pdev = pdev;
    dev->dev = &pdev->dev;
    dev->irq_map = &mt7927_irq_map;
    pci_set_drvdata(pdev, dev);

    /* Initialize locks */
    spin_lock_init(&dev->lock);
    mutex_init(&dev->mutex);

    /* Initialize MCU state */
    skb_queue_head_init(&dev->mcu.res_q);
    init_waitqueue_head(&dev->mcu.wait);
    dev->mcu.timeout = 3 * HZ;

    /* Enable PCI device */
    ret = pcim_enable_device(pdev);
    if (ret) {
        dev_err(&pdev->dev, "Failed to enable PCI device\n");
        return ret;
    }

    /* Map BAR regions */
    ret = pcim_iomap_regions(pdev, BIT(0) | BIT(2), pci_name(pdev));
    if (ret) {
        dev_err(&pdev->dev, "Failed to request PCI regions\n");
        return ret;
    }

    /* Ensure memory access is enabled */
    pci_read_config_word(pdev, PCI_COMMAND, &cmd);
    if (!(cmd & PCI_COMMAND_MEMORY)) {
        cmd |= PCI_COMMAND_MEMORY;
        pci_write_config_word(pdev, PCI_COMMAND, cmd);
    }

    /* Enable bus mastering for DMA */
    pci_set_master(pdev);

    /* Set up DMA mask (32-bit) */
    ret = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));
    if (ret) {
        dev_err(&pdev->dev, "Failed to set DMA mask\n");
        return ret;
    }

    /* Allocate IRQ vector */
    ret = pci_alloc_irq_vectors(pdev, 1, 1, PCI_IRQ_ALL_TYPES);
    if (ret < 0) {
        dev_err(&pdev->dev, "Failed to allocate IRQ vectors\n");
        return ret;
    }
    dev->irq = pci_irq_vector(pdev, 0);

    /* Map BAR regions */
    dev->mem = pcim_iomap_table(pdev)[0];   /* BAR0: Memory */
    dev->regs = pcim_iomap_table(pdev)[2];  /* BAR2: Registers */

    if (!dev->mem || !dev->regs) {
        dev_err(&pdev->dev, "Failed to map BARs\n");
        ret = -ENOMEM;
        goto err_free_irq_vectors;
    }

    /* Initialize tasklet for IRQ handling */
    tasklet_init(&dev->irq_tasklet, mt7927_irq_tasklet, (unsigned long)dev);

    /* Debug: Read raw values from both BARs */
    dev_info(&pdev->dev, "BAR0[0x000]: 0x%08x, BAR2[0x000]: 0x%08x\n",
             readl(dev->mem), readl(dev->regs));
    dev_info(&pdev->dev, "BAR0[0x200]: 0x%08x, BAR2[0x200]: 0x%08x\n",
             readl(dev->mem + 0x200), readl(dev->regs + 0x200));
    dev_info(&pdev->dev, "BAR0[0x208]: 0x%08x, BAR2[0x208]: 0x%08x\n",
             readl(dev->mem + 0x208), readl(dev->regs + 0x208));

    /* Read chip ID and revision (via address translation) */
    dev->chip_id = mt7927_rr(dev, MT_HW_CHIPID);
    dev->hw_rev = mt7927_rr(dev, MT_HW_REV) & 0xff;
    dev_info(&pdev->dev, "Chip ID: 0x%08x, HW Rev: 0x%02x\n",
             dev->chip_id, dev->hw_rev);

    /* Set EMI control for sleep protection */
    mt7927_rmw_field(dev, MT_HW_EMI_CTL, MT_HW_EMI_CTL_SLPPROT_EN, 1);

    /* Step 1: Release power control to firmware */
    ret = mt7927_mcu_fw_pmctrl(dev);
    if (ret) {
        dev_warn(&pdev->dev, "FW power control failed (may be expected)\n");
        /* Continue anyway - this might fail on first init */
    }

    /* Step 2: Acquire power control for driver */
    ret = mt7927_mcu_drv_pmctrl(dev);
    if (ret) {
        dev_warn(&pdev->dev, "Driver power control failed (may be expected)\n");
        /* Continue anyway */
    }

    /* Step 3: Reset WiFi subsystem */
    ret = mt7927_wfsys_reset(dev);
    if (ret) {
        dev_warn(&pdev->dev, "WiFi reset failed, continuing...\n");
    }

    /* Disable all interrupts initially */
    mt7927_wr(dev, dev->irq_map->host_irq_enable, 0);
    mt7927_wr(dev, MT_PCIE_MAC_INT_ENABLE, 0xff);

    /* Request IRQ - use request_irq for explicit control in error path */
    ret = request_irq(dev->irq, mt7927_irq_handler,
                      IRQF_SHARED, "mt7927", dev);
    if (ret) {
        dev_err(&pdev->dev, "Failed to request IRQ %d\n", dev->irq);
        goto err_tasklet;
    }

    /* Step 4: Initialize DMA */
    ret = mt7927_dma_init(dev);
    if (ret) {
        dev_err(&pdev->dev, "DMA initialization failed\n");
        goto err_free_irq;
    }

    /* Step 5: Initialize MCU and load firmware */
    ret = mt7927_mcu_init(dev);
    if (ret) {
        dev_err(&pdev->dev, "MCU initialization failed\n");
        goto err_dma;
    }

    /* Mark device as initialized */
    set_bit(MT7927_STATE_INITIALIZED, &dev->state);
    dev->hw_init_done = true;

    dev_info(&pdev->dev, "MT7927 driver initialized successfully\n");
    return 0;

err_dma:
    mt7927_dma_cleanup(dev);
err_free_irq:
    /* Disable interrupts at hardware level first */
    mt7927_wr(dev, dev->irq_map->host_irq_enable, 0);
    synchronize_irq(dev->irq);
    free_irq(dev->irq, dev);
err_tasklet:
    tasklet_kill(&dev->irq_tasklet);
err_free_irq_vectors:
    pci_free_irq_vectors(pdev);
    return ret;
}

static void mt7927_pci_remove(struct pci_dev *pdev)
{
    struct mt7927_dev *dev = pci_get_drvdata(pdev);

    dev_info(&pdev->dev, "Removing MT7927 device\n");

    /* Disable interrupts */
    mt7927_wr(dev, dev->irq_map->host_irq_enable, 0);

    /* Stop MCU */
    mt7927_mcu_exit(dev);

    /* Cleanup DMA */
    mt7927_dma_cleanup(dev);

    /* Free IRQ - must be before pci_free_irq_vectors */
    free_irq(dev->irq, dev);

    /* Kill tasklet */
    tasklet_kill(&dev->irq_tasklet);

    /* Free IRQ vectors */
    pci_free_irq_vectors(pdev);

    /* Release firmware */
    if (dev->fw_ram)
        release_firmware(dev->fw_ram);
    if (dev->fw_patch)
        release_firmware(dev->fw_patch);
}

static void mt7927_pci_shutdown(struct pci_dev *pdev)
{
    mt7927_pci_remove(pdev);
}

/* PCI device table */
static const struct pci_device_id mt7927_pci_table[] = {
    { PCI_DEVICE(MT7927_VENDOR_ID, MT7927_DEVICE_ID) },
    { }
};
MODULE_DEVICE_TABLE(pci, mt7927_pci_table);

/* PCI driver structure */
static struct pci_driver mt7927_pci_driver = {
    .name       = "mt7927",
    .id_table   = mt7927_pci_table,
    .probe      = mt7927_pci_probe,
    .remove     = mt7927_pci_remove,
    .shutdown   = mt7927_pci_shutdown,
};

module_pci_driver(mt7927_pci_driver);

MODULE_AUTHOR("MT7927 Linux Driver Project");
MODULE_DESCRIPTION("MediaTek MT7927 WiFi 7 PCIe Driver");
MODULE_LICENSE("GPL");
MODULE_FIRMWARE(MT7927_FIRMWARE_WM);
MODULE_FIRMWARE(MT7927_ROM_PATCH);
