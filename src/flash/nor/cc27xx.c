// SPDX-License-Identifier: GPL-2.0-or-later

/***************************************************************************
 * Copyright (C) 2025 Texas Instruments Incorporated - https://www.ti.com/
 *
 * NOR flash driver for CC27XX from Texas Instruments.
 * TRM : https://www.ti.com/lit/ug/swcu195a/swcu195a.pdf
 * Addition device documentation: https://dev.ti.com/tirex/explore?devices=CC27X0
 ***************************************************************************/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "jtag/interface.h"
#include "imp.h"
#include "time.h"
#include "cc27xx.h"
#include "cc_lpf3_base.h"
#include <helper/bits.h>
#include <helper/time_support.h>
#include <target/arm_adi_v5.h>
#include <target/armv7m.h>
#include <target/cortex_m.h>

//*** OPN *** DEVICEID(28bits) *** PARTID *** FLASH *** RAM ***//
static const struct cc_lpf3_part_info cc27xx_parts[] = {
	{"CC2745R10E0WRHARQ1", 0xBB9802F, 0x80E8AF23, 1024, 162},
	{"CC2745P10E0WRHARQ1", 0xBB9802F, 0x80DEAF23, 1024, 162},
	{"CC2745R74E0WRHARQ1", 0xBB9802F, 0x8087AF23, 768, 128}
};

/* CC27XX specific flash stage state */
static CC_LPF3_FLASH_STAGE_T flash_stage = CC_LPF3_FLASH_STAGE_INIT;

/* Global flags to track programming state */
static bool b_ccfg = false, b_scfg = false, b_main = false;

/*
 * Update the flash stage CC27xx devices
 */
static int cc27xx_check_device_memory_info(struct cc_lpf3_flash_bank *cc_lpf3_info, uint32_t device_id, uint32_t part_id)
{
	//padding should be taken care
	uint8_t total_parts = sizeof(cc27xx_parts)/sizeof(struct cc_lpf3_part_info);
	while(total_parts--) {
		if(cc27xx_parts[total_parts].device_id == (device_id & 0x0FFFFFFF) &&
			cc27xx_parts[total_parts].part_id == part_id ) {
			cc_lpf3_info->main_flash_size_kb = cc27xx_parts[total_parts].flash_size;
			cc_lpf3_info->sram_size_kb = cc27xx_parts[total_parts].ram_size;
			cc_lpf3_info->name = cc27xx_parts[total_parts].partname;
			cc_lpf3_info->main_flash_num_banks = 2;
			return ERROR_OK;
		}
	}

	return ERROR_FAIL;
}

/*
 * Update the flash stage CC27xx devices
 */
static bool cc27xx_check_allowed_flash_op(int op)
{
	bool op_allowed = 0;
	CC_LPF3_FLASH_OP_T cc_op = (CC_LPF3_FLASH_OP_T)op;

	switch (flash_stage) {
	case CC_LPF3_FLASH_STAGE_INIT:
		if(cc_op == CC_LPF3_FLASH_OP_CHIP_ERASE) {
			op_allowed = 1;
			flash_stage = CC_LPF3_FLASH_STAGE_ERASE;
			LOG_INFO("Performing Chip Erase");
		}
		break;

	case CC_LPF3_FLASH_STAGE_ERASE:
		if(cc_op == CC_LPF3_FLASH_OP_REVERT_STAGE) {
			op_allowed = 1;
			flash_stage = CC_LPF3_FLASH_STAGE_INIT;
		} else if(cc_op == CC_LPF3_FLASH_OP_PROG_CCFG) {
			op_allowed = 1;
			flash_stage = CC_LPF3_FLASH_STAGE_CCFG;
		} else if(cc_op == CC_LPF3_FLASH_OP_PROG_SCFG) {
			op_allowed = 1;
			flash_stage = CC_LPF3_FLASH_STAGE_SCFG;
		} else if (cc_op == CC_LPF3_FLASH_OP_PROG_MAIN) {
			op_allowed = 1;
			flash_stage = CC_LPF3_FLASH_STAGE_MAIN;
		}
		break;
	case CC_LPF3_FLASH_STAGE_CCFG:
		if(cc_op == CC_LPF3_FLASH_OP_REVERT_STAGE) {
			op_allowed = 1;
			flash_stage = CC_LPF3_FLASH_STAGE_ERASE;
		} else if(cc_op == CC_LPF3_FLASH_OP_PROG_MAIN && b_scfg) {
			op_allowed = 1;
			flash_stage = CC_LPF3_FLASH_STAGE_COMPLETE;
		} else if(cc_op == CC_LPF3_FLASH_OP_PROG_MAIN && !b_scfg) {
			op_allowed = 1;
			flash_stage = CC_LPF3_FLASH_STAGE_MAIN;
		} else if(cc_op == CC_LPF3_FLASH_OP_PROG_SCFG && b_main) {
			op_allowed = 1;
			flash_stage = CC_LPF3_FLASH_STAGE_COMPLETE;
		} else if(cc_op == CC_LPF3_FLASH_OP_PROG_SCFG && !b_main) {
			op_allowed = 1;
			flash_stage = CC_LPF3_FLASH_STAGE_SCFG;
		}
		break;
	case CC_LPF3_FLASH_STAGE_SCFG:
		if(cc_op == CC_LPF3_FLASH_OP_REVERT_STAGE) {
			op_allowed = 1;
			flash_stage = CC_LPF3_FLASH_STAGE_ERASE;
		} else if(cc_op == CC_LPF3_FLASH_OP_PROG_MAIN && b_ccfg) {
			op_allowed = 1;
			flash_stage = CC_LPF3_FLASH_STAGE_COMPLETE;
		} else if(cc_op == CC_LPF3_FLASH_OP_PROG_MAIN && !b_ccfg) {
			op_allowed = 1;
			flash_stage = CC_LPF3_FLASH_STAGE_MAIN;
		} else if(cc_op == CC_LPF3_FLASH_OP_PROG_CCFG && b_main) {
			op_allowed = 1;
			flash_stage = CC_LPF3_FLASH_STAGE_COMPLETE;
		} else if(cc_op == CC_LPF3_FLASH_OP_PROG_CCFG && !b_main) {
			op_allowed = 1;
			flash_stage = CC_LPF3_FLASH_STAGE_CCFG;
		}
		break;
	case CC_LPF3_FLASH_STAGE_MAIN:
		if(cc_op == CC_LPF3_FLASH_OP_REVERT_STAGE) {
			op_allowed = 1;
			flash_stage = CC_LPF3_FLASH_STAGE_ERASE;
		} else if(cc_op == CC_LPF3_FLASH_OP_PROG_CCFG && b_scfg) {
			op_allowed = 1;
			flash_stage = CC_LPF3_FLASH_STAGE_COMPLETE;
		} else if(cc_op == CC_LPF3_FLASH_OP_PROG_CCFG && !b_scfg) {
			op_allowed = 1;
			flash_stage = CC_LPF3_FLASH_STAGE_CCFG;
		} else if(cc_op == CC_LPF3_FLASH_OP_PROG_SCFG && b_ccfg) {
			op_allowed = 1;
			flash_stage = CC_LPF3_FLASH_STAGE_COMPLETE;
		} else if(cc_op == CC_LPF3_FLASH_OP_PROG_SCFG && !b_ccfg) {
			op_allowed = 1;
			flash_stage = CC_LPF3_FLASH_STAGE_SCFG;
		}
		break;

	default:
		LOG_INFO("State: UNKNOWN");
		break;
	}

	if (flash_stage == CC_LPF3_FLASH_STAGE_COMPLETE)
	{
		flash_stage = CC_LPF3_FLASH_STAGE_INIT;
		LOG_INFO("MAIN, CCFG and SCFG Programmed");
	}

	if(cc_op == CC_LPF3_FLASH_OP_CHIP_ERASE && op_allowed == 0)
	{
		LOG_INFO("Erase request discarded as main OR ccfg OR scfg section is programmed");
	}

	return op_allowed;
}

/*
 *	OpenOCD command interface
 */

FLASH_BANK_COMMAND_HANDLER(cc27xx_flash_bank_command)
{
	int retval = cc_lpf3_base_flash_bank_command(bank);
	if (retval != ERROR_OK)
		return retval;

	/* Register CC27XX specific operations */
	struct cc_lpf3_chip_ops ops = {
		.check_allowed_flash_op = cc27xx_check_allowed_flash_op,
		.check_device_memory_info = cc27xx_check_device_memory_info
	};
	cc_lpf3_base_register_chip_ops(bank, &ops);

	return ERROR_OK;
}

/*
 * Chip identification and status - CC27XX specific implementation
 */
static int cc27xx_get_info(struct flash_bank *bank, struct command_invocation *cmd)
{
	struct cc_lpf3_flash_bank *cc_lpf3_info = bank->driver_priv;

	if (cc_lpf3_info->did == 0)
		return ERROR_FLASH_BANK_NOT_PROBED;

	command_print_sameline(cmd,
			"\nTI CC27XX information: Chip is "
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

/*
 * CC27XX specific write function to handle the global flags
 */
static int cc27xx_write(struct flash_bank *bank, const uint8_t *buffer, uint32_t offset, uint32_t count)
{
	int retval = cc_lpf3_base_write(bank, buffer, offset, count);

	// Update global flags based on successful writes
	if (retval == ERROR_OK) {
		if (bank->base == LPF3_FLASH_BASE_CCFG) {
			b_ccfg = true;
		} else if (bank->base == LPF3_FLASH_BASE_SCFG) {
			b_scfg = true;
		} else if (bank->base == LPF3_FLASH_BASE_MAIN) {
			b_main = true;
		}
	}

	return retval;
}

COMMAND_HANDLER(cc27xx_reset_halt_command)
{
	struct flash_bank *bank;
	int retval;

	retval = CALL_COMMAND_HANDLER(flash_command_get_bank, 0, &bank);
	LOG_INFO("reset-halt get bank %d", retval);
	if (retval != ERROR_OK)
		return retval;

	//exit saci halt command
	retval = cc_lpf3_exit_saci_halt(bank);

	// Print the return value so it can be captured by TCL scripts using command substitution
	command_print(CMD, "%d", retval);
	return retval;
}

COMMAND_HANDLER(cc27xx_reset_run_command)
{
	struct flash_bank *bank;
	int retval;

	retval = CALL_COMMAND_HANDLER(flash_command_get_bank, 0, &bank);
	LOG_INFO("reset-run get bank %d", retval);
	if (retval != ERROR_OK)
		return retval;

	while (retval == BOOTSTA_BOOT_ENTERED_SACI) {
		//send NOP also
		retval = cc_lpf3_prepare_write(bank);
		if (retval != BOOTSTA_BOOT_ENTERED_SACI)
			LOG_INFO("Enter SACI attempt Fail current BOOTSTA %d", retval);
	}

	//exit saci run command
	cc_lpf3_exit_saci_run(bank);

	retval = cc_lpf3_check_boot_status(bank);
	LOG_INFO("reset_run boot status 0x%x", retval);
	return ERROR_OK;
}

static const struct command_registration cc27xx_exec_command_handlers[] = {
	{
		.name = "reset_run",
		.handler = cc27xx_reset_run_command,
		.mode = COMMAND_EXEC,
		.help = "Exit SACI and Run",
		.usage = "bank_id",
	},
	{
		.name = "reset_halt",
		.handler = cc27xx_reset_halt_command,
		.mode = COMMAND_EXEC,
		.help = "Exit SACI and halt in first instruction.",
		.usage = "bank_id",
	},

	COMMAND_REGISTRATION_DONE
};

static const struct command_registration cc27xx_command_handlers[] = {
	{
		.name = "cc27xx",
		.mode = COMMAND_EXEC,
		.help = "cc27xx flash command group",
		.usage = "",
		.chain = cc27xx_exec_command_handlers,
	},
	COMMAND_REGISTRATION_DONE
};

const struct flash_driver cc27xx_flash = {
	.name = "cc27xx",
	.flash_bank_command = cc27xx_flash_bank_command,
	.commands = cc27xx_command_handlers,
	.erase = cc_lpf3_base_erase,
	.protect = cc_lpf3_base_protect,
	.write = cc27xx_write,
	.read = cc_lpf3_base_read,
	.probe = cc_lpf3_base_probe,
	.verify = cc_lpf3_base_verify,
	.auto_probe = cc_lpf3_base_probe,
	.erase_check = default_flash_blank_check,
	.info = cc27xx_get_info,
	.free_driver_priv = default_flash_free_driver_priv,
	.usage = "<bank_id> cc27xx <base> <size> <chip_width> <bus_width> <target>",
};
