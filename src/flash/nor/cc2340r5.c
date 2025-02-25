// SPDX-License-Identifier: GPL-2.0-or-later

/***************************************************************************
 * Copyright (C) 2025 Texas Instruments Incorporated - https://www.ti.com/
 *
 * NOR flash driver for CC2340R5 from Texas Instruments.
 * TRM : https://www.ti.com/lit/pdf/swcu193
 * Datasheet : https://www.ti.com/lit/gpn/cc2340r5
 ***************************************************************************/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "jtag/interface.h"
#include "imp.h"
#include "time.h"
#include "cc2340r5.h"
#include "cc_lpf3_flash.h"
#include <helper/bits.h>
#include <helper/time_support.h>
#include <target/arm_adi_v5.h>
#include <target/armv7m.h>
#include <target/cortex_m.h>

//*** OPN *** DEVICEID(28bits) *** PARTID *** FLASH *** RAM//
static const struct cc23xx_part_info cc23xx_parts[] = {
	{"CC2340R52E0RGER", 0xBB8402F, 0x800F2DDA, 512, 36},
	{"CC2340R52E0RKPR", 0xBB8402F, 0x803B2DDA, 512, 36},
	{"CC2340R53E0RKPR", 0xBBAE02F, 0x804D1A96, 512, 64},
	{"CC2340R53E0YBGR", 0xBBAE02F, 0x802A1A96, 512, 64}
};

/*
 * Update the flash stage CC23xx/CC27xx devices
 */
int cc_lpf3_check_device_memory_info(struct cc_lpf3_flash_bank *cc_lpf3_info, uint32_t device_id, uint32_t part_id){
	//padding shuld be taken care
	uint8_t total_parts = sizeof(cc23xx_parts)/sizeof(struct cc23xx_part_info);

	while(total_parts--){
		if(cc23xx_parts[total_parts].device_id == (device_id & 0x0FFFFFFF) &&
			cc23xx_parts[total_parts].part_id == part_id ) {
			cc_lpf3_info->main_flash_size_kb = cc23xx_parts[total_parts].flash_size;
			cc_lpf3_info->sram_size_kb = cc23xx_parts[total_parts].ram_size;
			cc_lpf3_info->name = cc23xx_parts[total_parts].partname;
			return ERROR_OK;
		}
	}

	return ERROR_FAIL;
}

/*
 *	OpenOCD command interface
 */

FLASH_BANK_COMMAND_HANDLER(cc_lpf3_flash_bank_command)
{
	struct cc_lpf3_flash_bank *cc_lpf3_info;

	switch (bank->base) {
	case CC23XX_FLASH_BASE_CCFG:
	case CC23XX_FLASH_BASE_MAIN:
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

	cc_lpf3_info->sector_size = CC2340R5_MAIN_FLASH_SECTOR_SIZE;

	return ERROR_OK;
}

/*
 * Update the flash stage CC23xx/CC27xx devices
 */
bool cc_lpf3_check_allowed_flash_op(CC23XX_FLASH_OP_T op)
{
	static CC23XX_FLASH_STAGE_T flash_stage = CC23XX_FLASH_STAGE_INIT;
	bool op_allowed = 0;

	switch (flash_stage) {
	case CC23XX_FLASH_STAGE_INIT:
		if(op == CC23XX_FLASH_OP_CHIP_ERASE) {
			op_allowed =1;
			flash_stage = CC23XX_FLASH_STAGE_ERASE;
			LOG_INFO("Performing Chip Erase");
		}
		break;

	case CC23XX_FLASH_STAGE_ERASE:
		if(op == CC23XX_FLASH_OP_PROG_CCFG) {
			op_allowed =1;
			flash_stage = CC23XX_FLASH_STAGE_CCFG;
		}else if (op == CC23XX_FLASH_OP_PROG_MAIN){
			op_allowed =1;
			flash_stage = CC23XX_FLASH_STAGE_MAIN;
		}
		break;

	case (CC23XX_FLASH_STAGE_CCFG):
		if(op == CC23XX_FLASH_OP_PROG_MAIN) {
			op_allowed =1;
			flash_stage = CC23XX_FLASH_STAGE_COMPLETE;
		}
		break;

	case (CC23XX_FLASH_STAGE_MAIN):
		if(op == CC23XX_FLASH_OP_PROG_CCFG) {
			op_allowed =1;
			flash_stage = CC23XX_FLASH_STAGE_COMPLETE;
		}
		break;

	default:
		LOG_INFO("State: UNKNOWN");
		break;
	}

	if (flash_stage == CC23XX_FLASH_STAGE_COMPLETE)
	{
		flash_stage = CC23XX_FLASH_STAGE_INIT;
		LOG_INFO("MAIN and CCFG Programmed");
	}

	if(op == CC23XX_FLASH_OP_CHIP_ERASE && op_allowed == 0)
	{
		LOG_INFO("Erase request discarded as main OR ccfg section is programmed");
	}

	return op_allowed;
}
/*
 * Chip identification and status
 */
static int get_cc_lpf3_info(struct flash_bank *bank, struct command_invocation *cmd)
{
	struct cc_lpf3_flash_bank *cc_lpf3_info = bank->driver_priv;
	printf("Get info\n");
	printf("%5s\n", cc_lpf3_info->name);

	if (cc_lpf3_info->did == 0)
		return ERROR_FLASH_BANK_NOT_PROBED;

	command_print_sameline(cmd,
			"\nTI CC2340R5 information: Chip is "
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

static int cc_lpf3_read_part_info(struct flash_bank *bank)
{
	struct cc_lpf3_flash_bank *cc_lpf3_info = bank->driver_priv;
	uint32_t did = 0, pid = 0;

	/* Read and parse chip identification register */
	//read the device id
	if (ERROR_OK == cc_lpf3_read_from_AP(bank, DEBUGSS_CFG_AP, CFG_AP_DEVICE_ID_READ, &did))
		cc_lpf3_info->did = did;
	else
		return ERROR_FAIL;

	//read the device id
	if (ERROR_OK == cc_lpf3_read_from_AP(bank, DEBUGSS_CFG_AP, CFG_AP_PART_ID_READ, &pid))
		cc_lpf3_info->pid = pid;
	else
		return ERROR_FAIL;

	if (ERROR_FAIL == cc_lpf3_check_device_memory_info(cc_lpf3_info, did, pid))
		return ERROR_FAIL;

	cc_lpf3_info->did = did;
	cc_lpf3_info->main_flash_num_banks = 1;
	cc_lpf3_info->flash_word_size_bytes = 8;

	return ERROR_OK;
}

static int cc_lpf3_protect(struct flash_bank *bank, int set,
			 unsigned int first, unsigned int last)
{
	LOG_INFO("cc23xx-Protected Sectors need to be checked in the flashed CCFG");

	return ERROR_OK;
}

static int cc_lpf3_erase(struct flash_bank *bank, unsigned int first, unsigned int last)
{
	LOG_INFO("cc_lpf3_erase: Chip Erase will be done based on the flash state");

	if (BOOTSTA_BOOT_ENTERED_SACI != cc_lpf3_check_boot_status(bank))
		return ERROR_FAIL;

	if (cc_lpf3_check_allowed_flash_op(CC23XX_FLASH_OP_CHIP_ERASE)) {
		cc_lpf3_saci_erase(bank);
	}

	return ERROR_OK;
}

static int cc_lpf3_write(struct flash_bank *bank, const uint8_t *buffer,
			uint32_t offset, uint32_t count)
{
	struct cc_lpf3_flash_bank *cc_lpf3_info = bank->driver_priv;

	LOG_INFO("cc_lpf3_write : bank->base :"TARGET_ADDR_FMT" offset - 0x%x count - 0x%x", bank->base, offset, count);

	// Execute the CFG-AP read to make sure device is in the correct state
	cc_lpf3_check_device_info(bank);

	if (ERROR_OK != cc_lpf3_prepare_write(bank))
		//device not in SACI mode, so sec-ap command can't be executed
		return ERROR_TARGET_INIT_FAILED;

	if (cc_lpf3_info->did == 0)
		return ERROR_FLASH_BANK_NOT_PROBED;

	if (offset % cc_lpf3_info->flash_word_size_bytes) {
		LOG_ERROR("%s: Offset 0x%0" PRIx32 " Must be aligned to %d bytes",
			  cc_lpf3_info->name, offset, cc_lpf3_info->flash_word_size_bytes);
		return ERROR_FLASH_DST_BREAKS_ALIGNMENT;
	}

	//Program CCFG
	if (bank->base == CC23XX_FLASH_BASE_CCFG && cc_lpf3_check_allowed_flash_op(CC23XX_FLASH_OP_PROG_CCFG) ) {
		if (ERROR_OK != cc_lpf3_write_ccfg(bank, buffer, offset, count))
		{
			//Revert Stage
		}
	}

	//Program MAIN Bank
	if (bank->base == CC23XX_FLASH_BASE_MAIN && cc_lpf3_check_allowed_flash_op(CC23XX_FLASH_OP_PROG_MAIN)) {
		if (ERROR_OK == cc_lpf3_write_main(bank, buffer, offset, count))
		{
			//revert stage
		}
	}
	return ERROR_OK;
}

static int cc_lpf3_read(struct flash_bank *bank,
			uint8_t *buffer, uint32_t offset, uint32_t count)
{
	LOG_INFO("CC23xx Devices doesnt support Read through SACI interface");
	return ERROR_OK;
}

static int cc_lpf3_verify(struct flash_bank *bank,
			const uint8_t *buffer, uint32_t offset, uint32_t count)
{
	int retval;

	if (bank->base == CC23XX_FLASH_BASE_CCFG) {
		retval = cc_lpf3_saci_verify_ccfg(bank, buffer);
	} else if (bank->base == CC23XX_FLASH_BASE_MAIN) {
		if (count%CC2340R5_MAIN_FLASH_SECTOR_SIZE)
		{
			count = count+ (CC2340R5_MAIN_FLASH_SECTOR_SIZE - count%CC2340R5_MAIN_FLASH_SECTOR_SIZE);
		}
		retval = cc_lpf3_saci_verify_main(bank, buffer, count);
	} else {
		LOG_ERROR("Host requesting wrong banks to verify");
		return ERROR_FAIL;
	}
	return retval;
}

static int cc_lpf3_probe(struct flash_bank *bank)
{
	struct cc_lpf3_flash_bank *cc_lpf3_info = bank->driver_priv;
	int retval;

	/*
	 * If this is a cc_lpf3 chip, it has flash; probe() is just
	 * to figure out how much is present.  Only do it once.
	 */
	if (cc_lpf3_info->did != 0)
		return ERROR_OK;
	/*
	 * cc_lpf3_read_part_info() already handled error checking and
	 * reporting.  Note that it doesn't write, so we don't care about
	 * whether the target is halted or not.
	 */
	retval = cc_lpf3_read_part_info(bank);
	if (retval != ERROR_OK)
		return retval;

	if (bank->sectors) {
		free(bank->sectors);
		bank->sectors = NULL;
	}

	switch (bank->base) {
	case CC23XX_FLASH_BASE_CCFG:
		bank->size = CC2340R5_MAIN_FLASH_SECTOR_SIZE;
		bank->num_sectors = 0x1;
		break;
	case CC23XX_FLASH_BASE_MAIN:
		bank->size = (cc_lpf3_info->main_flash_size_kb * 1024);
		bank->num_sectors = (CC2340R5_MAIN_FLASH_SIZE) / (CC2340R5_MAIN_FLASH_SECTOR_SIZE);
		break;
	default:
		LOG_ERROR("%s: Invalid bank address " TARGET_ADDR_FMT, cc_lpf3_info->name,
			  bank->base);
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

	return ERROR_OK;
}

COMMAND_HANDLER(cc23xx_reset_halt_command)
{
	struct flash_bank *bank;
	int retval;

	retval = CALL_COMMAND_HANDLER(flash_command_get_bank, 0, &bank);
	LOG_INFO("reset-halt get bank %d", retval);
	if (retval != ERROR_OK)
		return retval;

	//exit saci halt command
	retval = cc_lpf3_exit_saci_halt(bank);

	return ERROR_OK;
}

COMMAND_HANDLER(cc23xx_reset_run_command)
{
	struct flash_bank *bank;
	int retval;

	retval = CALL_COMMAND_HANDLER(flash_command_get_bank, 0, &bank);
	LOG_INFO("reset-run get bank %d", retval);
	if (retval != ERROR_OK)
		return retval;

	while ( retval == BOOTSTA_BOOT_ENTERED_SACI) {
		//send NOP also
		retval = cc_lpf3_prepare_write(bank);
		if (retval != BOOTSTA_BOOT_ENTERED_SACI)
			LOG_INFO("Enter SACI attempt Fail current BOOTSTA %d", retval);
	}

	//exit saci run command
	cc_lpf3_exit_saci_run(bank);

	retval =  cc_lpf3_check_boot_status(bank);
	LOG_INFO("reset_run boot status 0x%x", retval);
	return ERROR_OK;
}

static const struct command_registration cc2340r5_exec_command_handlers[] = {
	{
		.name = "reset_run",
		.handler = cc23xx_reset_run_command,
		.mode = COMMAND_EXEC,
		.help = "Exit SACI and Run",
		.usage = "bank_id",
	},
	{
		.name = "reset_halt",
		.handler = cc23xx_reset_halt_command,
		.mode = COMMAND_EXEC,
		.help = "Exit SACI and halt in first instruction.",
		.usage = "bank_id",
	},

	COMMAND_REGISTRATION_DONE
};

static const struct command_registration cc_lpf3_command_handlers[] = {
	{
		.name = "cc2340r5",
		.mode = COMMAND_EXEC,
		.help = "cc2340r5 flash command group",
		.usage = "",
		.chain = cc2340r5_exec_command_handlers,
	},
	COMMAND_REGISTRATION_DONE
};

const struct flash_driver cc2340r5_flash = {
	.name = "cc2340r5",
	.flash_bank_command = cc_lpf3_flash_bank_command,
	.commands = cc_lpf3_command_handlers,
	.erase = cc_lpf3_erase,
	.protect = cc_lpf3_protect,
	.write = cc_lpf3_write,
	.read = cc_lpf3_read,
	.probe = cc_lpf3_probe,
	.verify = cc_lpf3_verify,
	.auto_probe = cc_lpf3_probe,
	.erase_check = default_flash_blank_check,
	.info = get_cc_lpf3_info,
	.free_driver_priv = default_flash_free_driver_priv,
};
