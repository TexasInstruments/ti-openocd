// SPDX-License-Identifier: GPL-2.0-or-later

/***************************************************************************
 * Copyright (C) 2025 Texas Instruments Incorporated - https://www.ti.com/
 *
 * Common base driver for CC23XX and CC27XX flash drivers from Texas Instruments.
 ***************************************************************************/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "jtag/interface.h"
#include "imp.h"
#include "time.h"
#include "cc_lpf3_base.h"
#include <helper/bits.h>
#include <helper/time_support.h>
#include <target/arm_adi_v5.h>
#include <target/armv7m.h>
#include <target/cortex_m.h>

/* Structure to store chip-specific operations */
struct cc_lpf3_base_priv {
    struct cc_lpf3_chip_ops ops;
};

/*
 *	OpenOCD command interface - Common flash bank command handler
 */
int cc_lpf3_base_flash_bank_command( struct flash_bank *bank)
{
    struct cc_lpf3_flash_bank *cc_lpf3_info;

    switch (bank->base) {
    case LPF3_FLASH_BASE_CCFG:
    case LPF3_FLASH_BASE_SCFG:
    case LPF3_FLASH_BASE_MAIN:
        break;
    default:
        LOG_ERROR("Invalid bank address " TARGET_ADDR_FMT, bank->base);
        return ERROR_FAIL;
    }

    cc_lpf3_info = calloc(sizeof(struct cc_lpf3_flash_bank), 1);
    if (!cc_lpf3_info) {
        LOG_ERROR("%s: Out of memory for cc_lpf3_info!", __func__);
        return ERROR_FAIL;
    }

    bank->driver_priv = cc_lpf3_info;

    if (bank->base == LPF3_FLASH_BASE_SCFG)
        cc_lpf3_info->sector_size = LPF3_SCFG_FLASH_SECTOR_SIZE;
    else
        cc_lpf3_info->sector_size = LPF3_MAIN_FLASH_SECTOR_SIZE;

    /* Store the private data in the cc_lpf3_info structure */
    cc_lpf3_info->name = "unknown"; /* Will be set by chip-specific code */

    return ERROR_OK;
}

/*
 * Register chip-specific operations
 */
void cc_lpf3_base_register_chip_ops(struct flash_bank *bank, const struct cc_lpf3_chip_ops *ops)
{
    struct cc_lpf3_flash_bank *cc_lpf3_info = bank->driver_priv;
    struct cc_lpf3_base_priv *priv = calloc(1, sizeof(struct cc_lpf3_base_priv));

    if (!priv) {
        LOG_ERROR("%s: Out of memory for cc_lpf3_base_priv!", __func__);
        return;
    }

    /* Copy the operations */
    priv->ops = *ops;

    /* Store the private data in the cc_lpf3_info structure */
    cc_lpf3_info->driver_priv = priv;
}

/*
 * Common read_part_info function
 */
static int cc_lpf3_base_read_part_info(struct flash_bank *bank)
{
    struct cc_lpf3_flash_bank *cc_lpf3_info = bank->driver_priv;
    struct cc_lpf3_base_priv *priv = cc_lpf3_info->driver_priv;
    uint32_t did = 0, pid = 0;

    /* Read and parse chip identification register */
    /* Read the device id */
    if (ERROR_OK == cc_lpf3_read_from_AP(bank, DEBUGSS_CFG_AP, CFG_AP_DEVICE_ID_READ, &did))
        cc_lpf3_info->did = did;
    else
        return ERROR_FAIL;

    /* Read the part id */
    if (ERROR_OK == cc_lpf3_read_from_AP(bank, DEBUGSS_CFG_AP, CFG_AP_PART_ID_READ, &pid))
        cc_lpf3_info->pid = pid;
    else
        return ERROR_FAIL;

    /* Call chip-specific function to check device memory info */
    if (priv && priv->ops.check_device_memory_info) {
        if (ERROR_FAIL == priv->ops.check_device_memory_info(cc_lpf3_info, did, pid))
            return ERROR_FAIL;
    } else {
        LOG_ERROR("No chip-specific check_device_memory_info function registered");
        return ERROR_FAIL;
    }

    cc_lpf3_info->did = did;

    /* Flash word size is common for both chip families */
    cc_lpf3_info->flash_word_size_bytes = 8;

    return ERROR_OK;
}

/*
 * Common protect function
 */
int cc_lpf3_base_protect(struct flash_bank *bank, int set, unsigned int first, unsigned int last)
{
    LOG_INFO("Protected Sectors need to be checked in the flashed CCFG");
    return ERROR_OK;
}

/*
 * Common erase function
 */
int cc_lpf3_base_erase(struct flash_bank *bank, unsigned int first, unsigned int last)
{
    struct cc_lpf3_flash_bank *cc_lpf3_info = bank->driver_priv;
    struct cc_lpf3_base_priv *priv = cc_lpf3_info->driver_priv;

    LOG_INFO("cc_lpf3_base_erase: Chip Erase will be done based on the flash state");

    if (BOOTSTA_BOOT_ENTERED_SACI != cc_lpf3_check_boot_status(bank))
        return ERROR_FAIL;

    /* Call chip-specific function to check if flash operation is allowed */
    if (priv && priv->ops.check_allowed_flash_op) {
        if (priv->ops.check_allowed_flash_op((int)CC_LPF3_FLASH_OP_CHIP_ERASE)) {
            if (ERROR_OK != cc_lpf3_saci_erase(bank)) {
                priv->ops.check_allowed_flash_op((int)CC_LPF3_FLASH_OP_REVERT_STAGE);
                return ERROR_FAIL;
            }
        }
    } else {
        LOG_ERROR("No chip-specific check_allowed_flash_op function registered");
        return ERROR_FAIL;
    }

    return ERROR_OK;
}

/*
 * Common write function
 */
int cc_lpf3_base_write(struct flash_bank *bank, const uint8_t *buffer, uint32_t offset, uint32_t count)
{
    struct cc_lpf3_flash_bank *cc_lpf3_info = bank->driver_priv;
    struct cc_lpf3_base_priv *priv = cc_lpf3_info->driver_priv;

    LOG_INFO("cc_lpf3_base_write : bank->base :" TARGET_ADDR_FMT " offset - 0x%x count - 0x%x", bank->base, offset, count);

    /* Execute the CFG-AP read to make sure device is in the correct state */
    if (ERROR_OK != cc_lpf3_check_device_info(bank))
        return ERROR_TARGET_INIT_FAILED;

    if (ERROR_OK != cc_lpf3_prepare_write(bank))
        /* Device not in SACI mode, so sec-ap command can't be executed */
        return ERROR_TARGET_INIT_FAILED;

    if (cc_lpf3_info->did == 0)
        return ERROR_FLASH_BANK_NOT_PROBED;

    if (offset % cc_lpf3_info->flash_word_size_bytes) {
        LOG_ERROR("%s: Offset 0x%0" PRIx32 " Must be aligned to %d bytes",
                cc_lpf3_info->name, offset, cc_lpf3_info->flash_word_size_bytes);
        return ERROR_FLASH_DST_BREAKS_ALIGNMENT;
    }

    /* Call chip-specific function to check if flash operation is allowed */
    if (!priv || !priv->ops.check_allowed_flash_op) {
        LOG_ERROR("No chip-specific check_allowed_flash_op function registered");
        return ERROR_FAIL;
    }

    /* Program CCFG */
    if (bank->base == LPF3_FLASH_BASE_CCFG &&
        priv->ops.check_allowed_flash_op((int)CC_LPF3_FLASH_OP_PROG_CCFG)) {
        if (ERROR_OK != cc_lpf3_write_ccfg(bank, buffer, offset, count)) {
            priv->ops.check_allowed_flash_op((int)CC_LPF3_FLASH_OP_REVERT_STAGE);
        }
    }

    /* Program SCFG (only for CC27XX) */
    if (bank->base == LPF3_FLASH_BASE_SCFG &&
        priv->ops.check_allowed_flash_op((int)CC_LPF3_FLASH_OP_PROG_SCFG)) {
        if (ERROR_OK != cc_lpf3_write_scfg(bank, buffer, offset, count)) {
            priv->ops.check_allowed_flash_op((int)CC_LPF3_FLASH_OP_REVERT_STAGE);
        }
    }

    /* Program MAIN Bank */
    if (bank->base == LPF3_FLASH_BASE_MAIN &&
        priv->ops.check_allowed_flash_op((int)CC_LPF3_FLASH_OP_PROG_MAIN)) {
        if (ERROR_OK != cc_lpf3_write_main(bank, buffer, offset, count)) {
            priv->ops.check_allowed_flash_op((int)CC_LPF3_FLASH_OP_REVERT_STAGE);
        }
    }

    return ERROR_OK;
}

/*
 * Common read function
 */
int cc_lpf3_base_read(struct flash_bank *bank, uint8_t *buffer, uint32_t offset, uint32_t count)
{
    LOG_INFO("CC LPF3 Devices don't support Read through SACI interface");
    return ERROR_OK;
}

/*
 * Common verify function
 */
int cc_lpf3_base_verify(struct flash_bank *bank, const uint8_t *buffer, uint32_t offset, uint32_t count)
{
    int retval;

    if (bank->base == LPF3_FLASH_BASE_CCFG || bank->base == LPF3_FLASH_BASE_SCFG) {
        retval = cc_lpf3_saci_verify_ccfg(bank, buffer);
    } else if (bank->base == LPF3_FLASH_BASE_MAIN) {
        if (count % LPF3_MAIN_FLASH_SECTOR_SIZE) {
            count = count + (LPF3_MAIN_FLASH_SECTOR_SIZE - count % LPF3_MAIN_FLASH_SECTOR_SIZE);
        }
        retval = cc_lpf3_saci_verify_main(bank, buffer, count);
    } else {
        LOG_ERROR("Host requesting wrong banks to verify");
        return ERROR_FAIL;
    }

    return retval;
}

/*
 * Common probe function
 */
int cc_lpf3_base_probe(struct flash_bank *bank)
{
    struct cc_lpf3_flash_bank *cc_lpf3_info = bank->driver_priv;
    int retval;

    /* Check boot status */
    cc_lpf3_check_boot_status(bank);

    /*
     * If this is a cc_lpf3 chip, it has flash; probe() is just
     * to figure out how much is present. Only do it once.
     */
    if (cc_lpf3_info->did != 0)
        return ERROR_OK;

    /*
     * cc_lpf3_base_read_part_info() already handled error checking and
     * reporting. Note that it doesn't write, so we don't care about
     * whether the target is halted or not.
     */
    retval = cc_lpf3_base_read_part_info(bank);
    if (retval != ERROR_OK)
        return retval;

    if (bank->sectors) {
        free(bank->sectors);
        bank->sectors = NULL;
    }

    switch (bank->base) {
    case LPF3_FLASH_BASE_CCFG:
        bank->size = LPF3_MAIN_FLASH_SECTOR_SIZE;
        bank->num_sectors = 0x1;
        break;
    case LPF3_FLASH_BASE_SCFG:
        bank->size = LPF3_SCFG_FLASH_SECTOR_SIZE;
        bank->num_sectors = 0x1;
        break;
    case LPF3_FLASH_BASE_MAIN:
        /* For CC23XX, main_flash_num_banks is 1, for CC27XX it's 2 */
        bank->size = (cc_lpf3_info->main_flash_size_kb * 1024) / cc_lpf3_info->main_flash_num_banks;
        bank->num_sectors = (bank->size) / (LPF3_MAIN_FLASH_SECTOR_SIZE);
        break;
    default:
        LOG_ERROR("%s: Invalid bank address " TARGET_ADDR_FMT, cc_lpf3_info->name, bank->base);
        return ERROR_FAIL;
    }

    bank->sectors = calloc(bank->num_sectors, sizeof(struct flash_sector));
    if (!bank->sectors) {
        LOG_ERROR("%s: Out of memory for sectors!", cc_lpf3_info->name);
        return ERROR_FAIL;
    }

    for (unsigned int i = 0; i < bank->num_sectors; i++) {
        bank->sectors[i].offset = i * cc_lpf3_info->sector_size;
        bank->sectors[i].size = cc_lpf3_info->sector_size;
        bank->sectors[i].is_erased = -1;
    }

    LOG_INFO("Device: %s, Flash: %dkb, RAM: %dkb", cc_lpf3_info->name, cc_lpf3_info->main_flash_size_kb, cc_lpf3_info->sram_size_kb);

    /* Check boot status again */
    cc_lpf3_check_boot_status(bank);

    return ERROR_OK;
}

/*
 * Common get_info function
 */
int cc_lpf3_base_get_info(struct flash_bank *bank, struct command_invocation *cmd)
{
    struct cc_lpf3_flash_bank *cc_lpf3_info = bank->driver_priv;

    if (cc_lpf3_info->did == 0)
        return ERROR_FLASH_BANK_NOT_PROBED;

    command_print_sameline(cmd,
            "\nTI CC LPF3 information: Chip is "
            "%s Device Unique ID: %d\n",
            cc_lpf3_info->name,
            cc_lpf3_info->version);
    command_print_sameline(cmd,
            "main flash: %dKB in %d bank(s), sram: %dKB\n",
            cc_lpf3_info->main_flash_size_kb,
            cc_lpf3_info->main_flash_num_banks,
            cc_lpf3_info->sram_size_kb);

    return ERROR_OK;
}
