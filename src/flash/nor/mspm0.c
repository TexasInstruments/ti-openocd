// SPDX-License-Identifier: GPL-2.0-or-later

/***************************************************************************
 * Copyright (C) 2023-2024 Texas Instruments Incorporated - https://www.ti.com/
 *
 * NOR flash driver for MSPM0L and MSPM0G class of uC from Texas Instruments.
 *
 * See:
 * https://www.ti.com/microcontrollers-mcus-processors/arm-based-microcontrollers/arm-cortex-m0-mcus/overview.html
 ***************************************************************************/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "imp.h"
#include <helper/bits.h>
#include <helper/time_support.h>

/* MPM0 Region memory map */
#define MSPM0_FLASH_BASE_NONMAIN		(0x41c00000)
#define MSPM0_FLASH_END_NONMAIN			(0x41c00400)
#define MSPM0_FLASH_BASE_MAIN			(0x0)
#define MSPM0_FLASH_BASE_DATA			(0x41d00000)

/* MPM0 FACTORYREGION registers */
#define MSPM0_FACTORYREGION				(0x41c40000)
#define MSPM0_TRACEID					(MSPM0_FACTORYREGION + 0x000)
#define MSPM0_DID						(MSPM0_FACTORYREGION + 0x004)
#define MSPM0_USERID					(MSPM0_FACTORYREGION + 0x008)
#define MSPM0_SRAMFLASH					(MSPM0_FACTORYREGION + 0x018)

/* MPM0 FCTL registers */
#define FLASH_CONTROL_BASE				(0x400cd000)
#define FCTL_REG_CMDEXEC				(FLASH_CONTROL_BASE + 0x1100)
#define FCTL_REG_CMDTYPE				(FLASH_CONTROL_BASE + 0x1104)
#define FCTL_REG_CMDADDR				(FLASH_CONTROL_BASE + 0x1120)
#define FCTL_REG_CMDBYTEN				(FLASH_CONTROL_BASE + 0x1124)
#define FCTL_REG_CMDDATA0				(FLASH_CONTROL_BASE + 0x1130)
#define FCTL_REG_CMDWEPROTA				(FLASH_CONTROL_BASE + 0x11D0)
#define FCTL_REG_CMDWEPROTB				(FLASH_CONTROL_BASE + 0x11D4)
#define FCTL_REG_CMDWEPROTNM			(FLASH_CONTROL_BASE + 0x1210)
#define FCTL_REG_STATCMD				(FLASH_CONTROL_BASE + 0x13D0)

/* FCTL_STATCMD[CMDDONE] Bits */
#define FCTL_STATCMD_CMDDONE_MASK		(0x00000001U)
#define FCTL_STATCMD_CMDDONE_STATDONE	(0x00000001U)

/* FCTL_STATCMD[CMDPASS] Bits */
#define FCTL_STATCMD_CMDPASS_MASK		(0x00000002U)
#define FCTL_STATCMD_CMDPASS_STATPASS	(0x00000002U)

/* FCTL_CMDEXEC Bits */
/* FCTL_CMDEXEC[VAL] Bits */
#define FCTL_CMDEXEC_VAL_EXECUTE		(0x00000001U)

/* FCTL_CMDTYPE[COMMAND] Bits */
#define FCTL_CMDTYPE_COMMAND_PROGRAM	(0x00000001U)
#define FCTL_CMDTYPE_COMMAND_ERASE		(0x00000002U)

/* FCTL_CMDTYPE[SIZE] Bits */
#define FCTL_CMDTYPE_SIZE_ONEWORD		(0x00000000U)
#define FCTL_CMDTYPE_SIZE_SECTOR		(0x00000040U)

#define MSPM0_MAX_PROTREGS				(3)

#define MSPM0_FLASH_TIMEOUT_MS			(8000)
#define ERR_STRING_MAX					(255)

/* SYSCTL BASE*/
#define SYSCTL_BASE						(0x400AF000U)
#define SYSCTL_SECCFG_SECSTATUS			(SYSCTL_BASE + 0x00003048U)

struct mspm0_flash_bank {
	/* chip id register */
	uint32_t did;
	/* Device Unique ID register */
	uint32_t traceid;
	uint8_t version;

	/* Pointer to name */
	const char *name;

	/* Decoded flash information */
	uint32_t data_flash_size_kb;
	uint32_t main_flash_size_kb;
	uint32_t main_flash_num_banks;
	uint32_t sector_size;
	/* Decoded SRAM information */
	uint32_t sram_size_kb;

	/* Flash word size: 64 bit = 8, 128bit = 16 bytes */
	uint8_t flash_word_size_bytes;

	/* Protection register stuff */
	uint32_t protect_reg_base;
	uint32_t protect_reg_count;
};

struct mspm0_part_info {
	const char *part_name;
	uint16_t part;
	uint8_t variant;
};

struct mspm0_family_info {
	const char *family_name;
	uint16_t part_num;
	uint8_t part_count;
	const struct mspm0_part_info *part_info;
};

/* https://www.ti.com/lit/ds/symlink/mspm0l1346.pdf Table 8-13 and so on */
static const struct mspm0_part_info mspm0l_parts[] = {
	{ "MSPM0L1105TDGS20R", 0x51DB, 0x16 },
	{ "MSPM0L1105TDGS28R", 0x51DB, 0x83 },
	{ "MSPM0L1105TDYYR", 0x51DB, 0x54 },
	{ "MSPM0L1105TRGER", 0x51DB, 0x86 },
	{ "MSPM0L1105TRHBR", 0x51DB, 0x68 },
	{ "MSPM0L1106TDGS20R", 0x5552, 0x4B },
	{ "MSPM0L1106TDGS28R", 0x5552, 0x98 },
	{ "MSPM0L1106TDYYR", 0x5552, 0x9D },
	{ "MSPM0L1106TRGER", 0x5552, 0x90 },
	{ "MSPM0L1106TRHBR", 0x5552, 0x53 },
	{ "MSPM0L1303SRGER", 0xef0, 0x17 },
	{ "MSPM0L1303TRGER", 0xef0, 0xe2 },
	{ "MSPM0L1304QDGS20R", 0xd717, 0x91 },
	{ "MSPM0L1304QDGS28R", 0xd717, 0xb6 },
	{ "MSPM0L1304QDYYR", 0xd717, 0xa0 },
	{ "MSPM0L1304QRHBR", 0xd717, 0xa9 },
	{ "MSPM0L1304SDGS20R", 0xd717, 0xfa },
	{ "MSPM0L1304SDGS28R", 0xd717, 0x73 },
	{ "MSPM0L1304SDYYR", 0xd717, 0xb7 },
	{ "MSPM0L1304SRGER", 0xd717, 0x26 },
	{ "MSPM0L1304SRHBR", 0xd717, 0xe4 },
	{ "MSPM0L1304TDGS20R", 0xd717, 0x33 },
	{ "MSPM0L1304TDGS28R", 0xd717, 0xa8 },
	{ "MSPM0L1304TDYYR", 0xd717, 0xf9 },
	{ "MSPM0L1304TRGER", 0xd717, 0xb7 },
	{ "MSPM0L1304TRHBR", 0xd717, 0x5a },
	{ "MSPM0L1305QDGS20R", 0x4d03, 0xb7 },
	{ "MSPM0L1305QDGS28R", 0x4d03, 0x74 },
	{ "MSPM0L1305QDYYR", 0x4d03, 0xec },
	{ "MSPM0L1305QRHBR", 0x4d03, 0x78 },
	{ "MSPM0L1305SDGS20R", 0x4d03, 0xc7 },
	{ "MSPM0L1305SDGS28R", 0x4d03, 0x64 },
	{ "MSPM0L1305SDYYR", 0x4d03, 0x91 },
	{ "MSPM0L1305SRGER", 0x4d03, 0x73 },
	{ "MSPM0L1305SRHBR", 0x4d03, 0x2d },
	{ "MSPM0L1305TDGS20R", 0x4d03, 0xa0 },
	{ "MSPM0L1305TDGS28R", 0x4d03, 0xfb },
	{ "MSPM0L1305TDYYR", 0x4d03, 0xde },
	{ "MSPM0L1305TRGER", 0x4d03, 0xea },
	{ "MSPM0L1305TRHBR", 0x4d03, 0x85 },
	{ "MSPM0L1306QDGS20R", 0xbb70, 0x59 },
	{ "MSPM0L1306QDGS28R", 0xbb70, 0xf7 },
	{ "MSPM0L1306QDYYR", 0xbb70, 0x9f },
	{ "MSPM0L1306QRHBR", 0xbb70, 0xc2 },
	{ "MSPM0L1306SDGS20R", 0xbb70, 0xf4 },
	{ "MSPM0L1306SDGS28R", 0xbb70, 0x5 },
	{ "MSPM0L1306SDYYR", 0xbb70, 0xe },
	{ "MSPM0L1306SRGER", 0xbb70, 0x7f },
	{ "MSPM0L1306SRHBR", 0xbb70, 0x3c },
	{ "MSPM0L1306TDGS20R", 0xbb70, 0xa },
	{ "MSPM0L1306TDGS28R", 0xbb70, 0x63 },
	{ "MSPM0L1306TDYYR", 0xbb70, 0x35 },
	{ "MSPM0L1306TRGER", 0xbb70, 0xaa },
	{ "MSPM0L1306TRHBR", 0xbb70, 0x52 },
	{ "MSPM0L1343TDGS20R", 0xb231, 0x2e },
	{ "MSPM0L1344TDGS20R", 0x40b0, 0xd0 },
	{ "MSPM0L1345TDGS28R", 0x98b4, 0x74 },
	{ "MSPM0L1346TDGS28R", 0xf2b5, 0xef },
};

/* https://www.ti.com/lit/ds/symlink/mspm0g3506.pdf Table 8-20 */
static const struct mspm0_part_info mspm0g_parts[] = {
	{ "MSPM0G1105TPTR", 0x8934, 0xD },
	{ "MSPM0G1105TRGZR", 0x8934, 0xFE },
	{ "MSPM0G1106TPMR", 0x477B, 0xD4 },
	{ "MSPM0G1106TPTR", 0x477B, 0x71 },
	{ "MSPM0G1106TRGZR", 0x477B, 0xBB },
	{ "MSPM0G1106TRHBR", 0x477B, 0x0 },
	{ "MSPM0G1107TDGS28R", 0x807B, 0x82 },
	{ "MSPM0G1107TPMR", 0x807B, 0xB3 },
	{ "MSPM0G1107TPTR", 0x807B, 0x32 },
	{ "MSPM0G1107TRGER", 0x807B, 0x79 },
	{ "MSPM0G1107TRGZR", 0x807B, 0x20 },
	{ "MSPM0G1107TRHBR", 0x807B, 0xBC },
	{ "MSPM0G1505SDGS28R", 0x13C4, 0x73 },
	{ "MSPM0G1505SPMR", 0x13C4, 0x53 },
	{ "MSPM0G1505SPTR", 0x13C4, 0x3E },
	{ "MSPM0G1505SRGER", 0x13C4, 0x47 },
	{ "MSPM0G1505SRGZR", 0x13C4, 0x34 },
	{ "MSPM0G1505SRHBR", 0x13C4, 0x30 },
	{ "MSPM0G1506SDGS28R", 0x5AE0, 0x3A },
	{ "MSPM0G1506SPMR", 0x5AE0, 0xF6 },
	{ "MSPM0G1506SRGER", 0x5AE0, 0x67 },
	{ "MSPM0G1506SRGZR", 0x5AE0, 0x75 },
	{ "MSPM0G1506SRHBR", 0x5AE0, 0x57 },
	{ "MSPM0G1507SDGS28R", 0x2655, 0x6D },
	{ "MSPM0G1507SPMR", 0x2655, 0x97 },
	{ "MSPM0G1507SRGER", 0x2655, 0x83 },
	{ "MSPM0G1507SRGZR", 0x2655, 0xD3 },
	{ "MSPM0G1507SRHBR", 0x2655, 0x4D },
	{ "MSPM0G3105SDGS20R", 0x4749, 0x21 },
	{ "MSPM0G3105SDGS28R", 0x4749, 0xDD },
	{ "MSPM0G3105SRHBR", 0x4749, 0xBE },
	{ "MSPM0G3106SDGS20R", 0x54C7, 0xD2 },
	{ "MSPM0G3106SDGS28R", 0x54C7, 0xB9 },
	{ "MSPM0G3106SRHBR", 0x54C7, 0x67 },
	{ "MSPM0G3107SDGS20R", 0xAB39, 0x5C },
	{ "MSPM0G3107SDGS28R", 0xAB39, 0xCC },
	{ "MSPM0G3107SRHBR", 0xAB39, 0xB7 },
	{ "MSPM0G3505SDGS28R", 0xc504, 0x8e },
	{ "MSPM0G3505SPMR", 0xc504, 0x1d },
	{ "MSPM0G3505SPTR", 0xc504, 0x93 },
	{ "MSPM0G3505SRGZR", 0xc504, 0xc7 },
	{ "MSPM0G3505SRHBR", 0xc504, 0xe7 },
	{ "MSPM0G3505TDGS28R", 0xc504, 0xdf },
	{ "MSPM0G3506SDGS28R", 0x151f, 0x8 },
	{ "MSPM0G3506SPMR", 0x151f, 0xd4 },
	{ "MSPM0G3506SPTR", 0x151f, 0x39 },
	{ "MSPM0G3506SRGZR", 0x151f, 0xfe },
	{ "MSPM0G3506SRHBR", 0x151f, 0xb5 },
	{ "MSPM0G3507SDGS28R", 0xae2d, 0xca },
	{ "MSPM0G3507SPMR", 0xae2d, 0xc7 },
	{ "MSPM0G3507SPTR", 0xae2d, 0x3f },
	{ "MSPM0G3507SRGZR", 0xae2d, 0xf7 },
	{ "MSPM0G3507SRHBR", 0xae2d, 0x4c },
	{ "M0G3107QPMRQ1", 0x4e2f, 0x51 },
	{ "M0G3107QPTRQ1", 0x4e2f, 0xc7},
	{ "M0G3107QRGZRQ1", 0x4e2f, 0x8a },
	{ "M0G3107QRHBRQ1", 0x4e2f, 0x9a},
	{ "M0G3107QDGS28RQ1", 0x4e2f, 0xd5},
	{ "M0G3107QDGS28RQ1", 0x4e2f, 0x67},
	{ "M0G3107QDGS20RQ1", 0x4e2f, 0xfd},
	{ "M0G3106QPMRQ1", 0x54C7, 0x08},
	{ "M0G3105QDGS32RQ1", 0x1349, 0x08},
	{ "M0G3106QPTRQ1", 0x54C7, 0x3F},
	{ "M0G3105QDGS28RQ1", 0x1349, 0x1B},
	{ "M0G3106QRGZRQ1", 0x94AD, 0xE6},
	{ "M0G3105QDGS20RQ1", 0x1349, 0xFB},
	{ "M0G3106QRHBRQ1", 0x94AD, 0x20},
	{ "M0G3106QDGS32RQ1", 0x94AD, 0x8D},
	{ "M0G3106QDGS28RQ1", 0x94AD, 0x03},
	{ "M0G3106QDGS20RQ1", 0x94AD, 0x6F},
	{ "M0G3105QPMRQ1", 0x1349, 0xD0},
	{ "M0G3105QPTRQ1", 0x1349, 0xEF},
	{ "M0G3105QRGZRQ1", 0x1349, 0x70},
	{ "M0G3105QRHBRQ1", 0x1349, 0x01},
};

/* https://www.ti.com/lit/gpn/mspm0c1104 Table 8-12 and so on */
static const struct mspm0_part_info mspm0c_parts[] = {
	{ "MSPS003F4SPW20R", 0x57b3, 0x70},
	{ "MSPM0C1104SDGS20R", 0x57b3, 0x71},
	{ "MSPM0C1104SRUKR", 0x57b3, 0x73},
	{ "MSPM0C1104SDYYR", 0x57b3, 0x75},
	{ "MSPM0C1104SDDFR", 0x57b3, 0x77},
	{ "MSPM0C1104SDSGR", 0x57b3, 0x79},
};

/* https://www.ti.com/lit/gpn/MSPM0L2228 Table 8-16 and so on */
static const struct mspm0_part_info mspm0lx22x_parts[] = {
	{ "MSPM0L1227SRGER", 0x7C32, 0xF1},
	{ "MSPM0L1227SPTR", 0x7C32, 0xC9},
	{ "MSPM0L1227SPMR", 0x7C32, 0x1C},
	{ "MSPM0L1227SPNAR", 0x7C32, 0x91},
	{ "MSPM0L1227SPNR", 0x7C32, 0x39},
	{ "MSPM0L1228SRGER", 0x33F7, 0x13},
	{ "MSPM0L1228SRHBR", 0x33F7, 0x3A},
	{ "MSPM0L1228SRGZR", 0x33F7, 0xBC},
	{ "MSPM0L1228SPTR", 0x33F7, 0xF8},
	{ "MSPM0L1228SPMR", 0x33F7, 0xCE},
	{ "MSPM0L1228SPNAR", 0x33F7, 0x59},
	{ "MSPM0L1228SPNR", 0x33F7, 0x7},
	{ "MSPM0L2227SRGZR", 0x5E8F, 0x90},
	{ "MSPM0L2227SPTR", 0x5E8F, 0xA},
	{ "MSPM0L2227SPMR", 0x5E8F, 0x6D},
	{ "MSPM0L2227SPNAR", 0x5E8F, 0x24},
	{ "MSPM0L2227SPNR", 0x5E8F, 0x68},
	{ "MSPM0L2228SRGZR", 0x2C38, 0xB8},
	{ "MSPM0L2228SPTR", 0x2C38, 0x25},
	{ "MSPM0L2228SPMR", 0x2C38, 0x6E},
	{ "MSPM0L2228SPNAR", 0x2C38, 0x63},
	{ "MSPM0L2228SPNR", 0x2C38, 0x3C},
};

static const struct mspm0_family_info mspm0_finf[] = {
	{ "MSPM0L", 0xbb82, ARRAY_SIZE(mspm0l_parts), mspm0l_parts },
	{ "MPSM0Lx22x", 0xbb9f, ARRAY_SIZE(mspm0lx22x_parts), mspm0lx22x_parts },
	{ "MSPM0G", 0xbb88, ARRAY_SIZE(mspm0g_parts), mspm0g_parts },
	{ "MSPM0C", 0xbba1, ARRAY_SIZE(mspm0c_parts), mspm0c_parts },
};

/*
 *	OpenOCD command interface
 */

/*
 * flash_bank mspm0 <base> <size> 0 0 <target#>
 */
FLASH_BANK_COMMAND_HANDLER(mspm0_flash_bank_command)
{
	struct mspm0_flash_bank *mspm0_info;

	switch (bank->base) {
	case MSPM0_FLASH_BASE_NONMAIN:
	case MSPM0_FLASH_BASE_MAIN:
	case MSPM0_FLASH_BASE_DATA:
		break;
	default:
		LOG_ERROR("Invalid bank address " TARGET_ADDR_FMT, bank->base);
		return ERROR_FAIL;
	}

	mspm0_info = calloc(sizeof(struct mspm0_flash_bank), 1);
	if (!mspm0_info) {
		LOG_ERROR("%s: Out of memory for mspm0_info!", __func__);
		return ERROR_FAIL;
	}

	bank->driver_priv = mspm0_info;

	mspm0_info->sector_size = 0x400;

	return ERROR_OK;
}

/*
 * Chip identification and status
 */
static int get_mspm0_info(struct flash_bank *bank, struct command_invocation *cmd)
{
	struct mspm0_flash_bank *mspm0_info = bank->driver_priv;

	if (mspm0_info->did == 0)
		return ERROR_FLASH_BANK_NOT_PROBED;

	command_print_sameline(cmd,
			       "\nTI MSPM0 information: Chip is "
			       "%s rev %d Device Unique ID: %d\n",
			       mspm0_info->name, mspm0_info->version,
			       mspm0_info->traceid);
	command_print_sameline(cmd,
			       "main flash: %dKiB in %d bank(s), sram: %dKiB, data flash: %dKiB",
			       mspm0_info->main_flash_size_kb,
			       mspm0_info->main_flash_num_banks, mspm0_info->sram_size_kb,
			       mspm0_info->data_flash_size_kb);

	return ERROR_OK;
}

/* Extract a bitfield helper */
static uint32_t mspm0_extract_val(uint32_t var, uint8_t hi, uint8_t lo)
{
	return (var & GENMASK(hi, lo)) >> lo;
}

static int mspm0_read_part_info(struct flash_bank *bank)
{
	struct mspm0_flash_bank *mspm0_info = bank->driver_priv;
	struct target *target = bank->target;
	uint32_t did, userid, flashram;
	uint8_t minfo_idx = 0xff;
	uint8_t pinfo_idx = 0xff;
	uint16_t pnum, part;
	uint8_t variant, version;
	const struct mspm0_family_info *minfo = NULL;

	/* Read and parse chip identification register */
	target_read_u32(target, MSPM0_DID, &did);
	target_read_u32(target, MSPM0_TRACEID, &mspm0_info->traceid);
	target_read_u32(target, MSPM0_USERID, &userid);
	target_read_u32(target, MSPM0_SRAMFLASH, &flashram);

	version = mspm0_extract_val(did, 31, 28);
	pnum = mspm0_extract_val(did, 27, 12);
	variant = mspm0_extract_val(userid, 23, 16);
	part = mspm0_extract_val(userid, 15, 0);

	/* Valid DIEID? - check the ALWAYS_1 bit to be 1 */
	if (!(did & BIT(0))) {
		LOG_WARNING("Unknown Device ID[0x%" PRIx32 "], cannot identify target",
			    did);
		LOG_DEBUG("did 0x%" PRIx32 ", traceid 0x%" PRIx32 ", userid 0x%" PRIx32
			  ", flashram 0x%" PRIx32 "", did, mspm0_info->traceid, userid,
			  flashram);
		return ERROR_FLASH_OPERATION_FAILED;
	}

	/* Check if we at least know the family of devices */
	for (int i = 0; i < (int)ARRAY_SIZE(mspm0_finf); i++) {
		if (mspm0_finf[i].part_num == pnum) {
			minfo_idx = i;
			minfo = &mspm0_finf[i];
			break;
		}
	}

	if (minfo_idx == 0xff) {
		LOG_WARNING("Unsupported DeviceID[0x%" PRIx32 "], cannot identify target",
			    pnum);
		LOG_DEBUG("did 0x%" PRIx32 ", traceid 0x%" PRIx32 ", userid 0x%" PRIx32
			  ", flashram 0x%" PRIx32 "", did, mspm0_info->traceid, userid,
			  flashram);
		LOG_DEBUG("Part 0x%" PRIx32 ", Part Num 0x%" PRIx32 ", Variant 0x%" PRIx32
			  ", version 0x%" PRIx32, part, pnum, variant, version);
		return ERROR_FLASH_OPERATION_FAILED;
	}

	/* Can we specifically identify the chip */
	for (int i = 0; i < minfo->part_count; i++) {
		if (minfo->part_info[i].part == part
		    && minfo->part_info[i].variant == variant) {
			pinfo_idx = i;
			break;
		}
	}
	if (minfo_idx == 0xff) {
		mspm0_info->name = mspm0_finf[minfo_idx].family_name;
		LOG_WARNING("Unidentified PART[0x%" PRIx32 "]/variant[0x%" PRIx32
			    "], known DeviceID[0x%" PRIx32
			    "]. Attempting to proceed as %s.", part, variant, pnum,
			    mspm0_info->name);
	} else {
		mspm0_info->name = mspm0_finf[minfo_idx].part_info[pinfo_idx].part_name;
		LOG_DEBUG("Part: %s detected", mspm0_info->name);
	}

	mspm0_info->did = did;
	mspm0_info->version = version;
	mspm0_info->data_flash_size_kb = mspm0_extract_val(flashram, 31, 26);
	mspm0_info->main_flash_size_kb = mspm0_extract_val(flashram, 11, 0);
	mspm0_info->main_flash_num_banks = mspm0_extract_val(flashram, 13, 12) + 1;
	mspm0_info->sram_size_kb = mspm0_extract_val(flashram, 25, 16);

	/*
	 * Hardcode flash_word_size unless we find some other pattern
	 * See section 7.7 (Foot note mentions the flash word size).
	 * almost all values seem to be 8 bytes, but if there are variance,
	 * then we should update mspm0_part_info structure with this info.
	 */
	mspm0_info->flash_word_size_bytes = 8;

	LOG_DEBUG("Detected: main flash: %dKb in %d banks, sram: %dKb, data flash: %dKb",
		  mspm0_info->main_flash_size_kb, mspm0_info->main_flash_num_banks,
		  mspm0_info->sram_size_kb, mspm0_info->data_flash_size_kb);

	return ERROR_OK;
}

/*
 * Decode error values
 */
const struct {
	const uint8_t bit_offset;
	const char *fail_string;
} mspm0_fctl_fail_decode_strings[] = {
	{ 2, "CMDINPROGRESS" },
	{ 4, "FAILWEPROT" },
	{ 5, "FAILVERIFY" },
	{ 6, "FAILILLADDR" },
	{ 7, "FAILMODE" },
	{ 12, "FAILMISC" },
};

static void msmp0_fctl_translate_ret_err(uint32_t return_code, char *ret_str)
{
	for (unsigned long i = 0; i < ARRAY_SIZE(mspm0_fctl_fail_decode_strings); i++) {
		if (return_code & BIT(mspm0_fctl_fail_decode_strings[i].bit_offset)) {
			strncat(ret_str, mspm0_fctl_fail_decode_strings[i].fail_string,
				ERR_STRING_MAX);
			strncat(ret_str, " ", ERR_STRING_MAX);
		}
	}
}

static int mspm0_fctl_get_sector_reg(struct flash_bank *bank, uint32_t addr,
									 uint32_t *reg, uint32_t *sector_mask)
{
	struct mspm0_flash_bank *mspm0_info = bank->driver_priv;
	struct target *target = bank->target;
	uint32_t sector_num = (addr >> 10);
	uint32_t sector_in_bank = sector_num;
	uint32_t phys_sector_num = sector_num;
	uint32_t sysctl_sec_status;
	uint32_t exec_upper_bank;

	/*
	 * If the device has dual banks we will need to check if it is configured
	 * to execute from the upper bank. In the scenario that we are executing
	 * from upper bank then we will need to protect it using CMDWEPROTA rather
	 * than CMDWEPROTB. We also need to take into account what sector
	 * we're using when going between banks.
	 */
	if (mspm0_info->main_flash_num_banks > 1) {
		target_read_u32(target, SYSCTL_SECCFG_SECSTATUS, &sysctl_sec_status);
		exec_upper_bank = mspm0_extract_val(sysctl_sec_status, 12, 12);
		if (exec_upper_bank) {
			if (sector_num > (mspm0_info->main_flash_size_kb / 2)) {
				phys_sector_num =
				    sector_num - (mspm0_info->main_flash_size_kb / 2);
			} else {
				phys_sector_num =
				    sector_num + (mspm0_info->main_flash_size_kb / 2);
			}
		}
		sector_in_bank =
		    sector_num % (mspm0_info->main_flash_size_kb /
				    mspm0_info->main_flash_num_banks);
	}

	/*
	 * NOTE: All MSPM0 devices will use CMDWEPROTA and CMDWEPROTB for MAIN
	 * flash. CMDWEPROTC is included in the TRM/DATASHEET but for all
	 * practical purposes, it is considered reserved.
	 */

	if (sector_num < mspm0_info->main_flash_size_kb) {
		/* Use CMDWEPROTA */
		if (phys_sector_num < (uint32_t)32) {
			*sector_mask = (uint32_t)1 << phys_sector_num;
			*reg = FCTL_REG_CMDWEPROTA;
			return ERROR_OK;
		}

		/* Use CMDWEPROTB */
		if (sector_in_bank < (uint32_t)256) {
			/* Dual bank system */
			if (mspm0_info->main_flash_num_banks > 1) {
				*sector_mask = (uint32_t)1 << (sector_in_bank / 8);
			} else {	/* Single bank system */
				*sector_mask = (uint32_t)1 << ((sector_in_bank - 32) / 8);
			}
			*reg = FCTL_REG_CMDWEPROTB;
			return ERROR_OK;
		}
	} else {
		/*
		 * Due to the consequences of NONMAIN memory we will perform an
		 * additional check to confirm that the address being passed through
		 * is NONMAIN flash. If the address passed through is not proper we
		 * will return an error.
		 */
		if (addr >= MSPM0_FLASH_BASE_NONMAIN && addr <= MSPM0_FLASH_END_NONMAIN) {
			*sector_mask = 1 << (sector_num % 32);
			*reg = FCTL_REG_CMDWEPROTNM;
			return ERROR_OK;
		}
	}

	LOG_ERROR("%s: Unable to map sector protect reg for address 0x%08" PRIx32,
			  mspm0_info->name, addr);
	return ERROR_FLASH_DST_OUT_OF_BANK;
}

static int mspm0_fctl_unprotect_sector(struct flash_bank *bank, uint32_t addr)
{
	struct target *target = bank->target;
	uint32_t reg = 0x0;
	uint32_t sector_mask = 0x0;
	int ret;

	ret = mspm0_fctl_get_sector_reg(bank, addr, &reg, &sector_mask);
	if (ret == ERROR_OK)
		target_write_u32(target, reg, ~sector_mask);

	return ret;
}

static int msmp0_fctl_wait_cmd_ok(struct flash_bank *bank)
{
	struct target *target = bank->target;
	struct mspm0_flash_bank *mspm0_info = bank->driver_priv;
	uint32_t return_code = 0;
	long long start_ms;
	long long elapsed_ms;

	int retval = ERROR_OK;

	start_ms = timeval_ms();
	while ((return_code & FCTL_STATCMD_CMDDONE_MASK) != FCTL_STATCMD_CMDDONE_STATDONE) {
		retval = target_read_u32(target, FCTL_REG_STATCMD, &return_code);
		if (retval != ERROR_OK)
			return retval;

		elapsed_ms = timeval_ms() - start_ms;
		if (elapsed_ms > 500)
			keep_alive();
		if (elapsed_ms > MSPM0_FLASH_TIMEOUT_MS)
			break;
	}

	if ((return_code & FCTL_STATCMD_CMDPASS_MASK) != FCTL_STATCMD_CMDPASS_STATPASS) {
		char *error_string = calloc(sizeof(char), ERR_STRING_MAX + 1);
		if (error_string) {
			msmp0_fctl_translate_ret_err(return_code, error_string);
			LOG_ERROR("%s: Flash command failed: %s", mspm0_info->name,
				  error_string);
			free(error_string);
		} else {
			LOG_ERROR("%s: Flash command failed: 0x%" PRIx32,
				  mspm0_info->name, return_code);
		}
		return ERROR_FAIL;
	}

	return ERROR_OK;
}

static int mspm0_protect_reg_mainmap(struct flash_bank *bank, uint32_t sector,
				     uint32_t *protect_reg_offset,
				     uint32_t *protect_reg_bit)
{
	struct mspm0_flash_bank *mspm0_info = bank->driver_priv;
	uint32_t bank_size, sector_in_bank;

	if (sector < 32) {
		*protect_reg_offset = 0;
		*protect_reg_bit = sector % 32;
		return ERROR_OK;
	}

	bank_size = mspm0_info->main_flash_size_kb / mspm0_info->main_flash_num_banks;
	sector_in_bank = sector & (bank_size - 1);

	if (sector_in_bank < 256) {
		*protect_reg_offset = 1;
		if (mspm0_info->main_flash_num_banks == 1)
			*protect_reg_bit = ((sector_in_bank - 32) / 8);
		else
			*protect_reg_bit = (sector_in_bank) / 8;
		return ERROR_OK;
	}

	if (sector_in_bank >= 512) {
		LOG_ERROR("%s: Invalid sector_in_bank %d at bank " TARGET_ADDR_FMT,
			  mspm0_info->name, sector_in_bank, bank->base);
		return ERROR_FAIL;
	}
	*protect_reg_offset = 2;
	*protect_reg_bit = (sector_in_bank - 256) / 8;
	return ERROR_OK;
}

static int mspm0_protect_reg_map(struct flash_bank *bank, uint32_t sector,
				 uint32_t *protect_reg_offset,
				 uint32_t *protect_reg_bit)
{
	int retval;
	struct mspm0_flash_bank *mspm0_info = bank->driver_priv;

	switch (bank->base) {
	case MSPM0_FLASH_BASE_NONMAIN:
		*protect_reg_offset = sector / 32;
		*protect_reg_bit = sector % 32;
		break;
	case MSPM0_FLASH_BASE_MAIN:
		retval =
		    mspm0_protect_reg_mainmap(bank, sector, protect_reg_offset,
					      protect_reg_bit);
		if (retval)
			return retval;
		break;
	case MSPM0_FLASH_BASE_DATA:
		LOG_ERROR("%s: Bank protection not available " TARGET_ADDR_FMT,
			  mspm0_info->name, bank->base);
		return ERROR_FAIL;
	default:
		LOG_ERROR("%s: Invalid bank address " TARGET_ADDR_FMT, mspm0_info->name,
			  bank->base);
		return ERROR_FAIL;
	}

	/* Basic sanity checks */
	if (*protect_reg_offset >= mspm0_info->protect_reg_count) {
		LOG_ERROR("%s: sector %d address overflows protection regs: "
			  TARGET_ADDR_FMT, mspm0_info->name, sector, bank->base);
		return ERROR_FAIL;
	}
	if (*protect_reg_bit >= 32) {
		LOG_ERROR
		    ("%s: sector %d address causes DRIVER BUG for reg bit %d on bank: "
		     TARGET_ADDR_FMT, mspm0_info->name, sector, *protect_reg_bit,
		     bank->base);
		return ERROR_FAIL;
	}

	return ERROR_OK;
}

static int mspm0_protect_check(struct flash_bank *bank)
{
	struct target *target = bank->target;
	struct mspm0_flash_bank *mspm0_info = bank->driver_priv;
	uint32_t protect_reg_cache[MSPM0_MAX_PROTREGS];
	uint32_t protect_reg_offset, protect_reg_bit;
	unsigned int i;

	if (mspm0_info->did == 0)
		return ERROR_FLASH_BANK_NOT_PROBED;

	for (i = 0; i < bank->num_sectors; i++)
		bank->sectors[i].is_protected = -1;

	if (!mspm0_info->protect_reg_count)
		return ERROR_OK;

	/* Do a single scan read of regs before we set the status */
	for (i = 0; i < mspm0_info->protect_reg_count; i++) {
		target_read_u32(target,
				mspm0_info->protect_reg_base + (i * 4),
				&protect_reg_cache[i]);
	}

	for (i = 0; i < bank->num_sectors; i++) {
		int retval = mspm0_protect_reg_map(bank, i, &protect_reg_offset,
						   &protect_reg_bit);
		if (retval) {
			LOG_ERROR("%s: sector %d: protect reg decode err: %d",
				  mspm0_info->name, i, retval);
			bank->sectors[i].is_protected = -1;
			continue;
		}
		bank->sectors[i].is_protected =
		    protect_reg_cache[protect_reg_offset] & BIT(protect_reg_bit) ? 1 : 0;
	}

	return ERROR_OK;
}

static int mspm0_protect(struct flash_bank *bank, int set,
			 unsigned int first, unsigned int last)
{
	struct target *target = bank->target;
	struct mspm0_flash_bank *mspm0_info = bank->driver_priv;
	uint32_t protect_reg_cache[MSPM0_MAX_PROTREGS];
	uint32_t protect_reg_offset, protect_reg_bit;
	unsigned int i;
	int retval;

	if (mspm0_info->did == 0)
		return ERROR_FLASH_BANK_NOT_PROBED;

	if (!mspm0_info->protect_reg_count)
		return ERROR_OK;

	/*
	 * Don't trust the protection status set in bank->sectors[i].is_protected
	 * Driver might have changed the flash protection scheme.
	 * So, just rescan and update
	 */

	/* Do a single scan read of regs before we set the status */
	for (i = 0; i < mspm0_info->protect_reg_count; i++) {
		target_read_u32(target,
				mspm0_info->protect_reg_base + (i * 4),
				&protect_reg_cache[i]);
	}
	/* Flip to binary value */
	set = !!set;
	/* Now set the bits that we need to set with */
	for (i = first; i <= last; i++) {
		retval =
		    mspm0_protect_reg_map(bank, i, &protect_reg_offset, &protect_reg_bit);

		/* Don't proceed unless all OK */
		if (retval) {
			LOG_ERROR("%s: Sector %d protect regmap fail: %d",
				  mspm0_info->name, i, retval);
			return retval;
		}
		if (set)
			protect_reg_cache[protect_reg_offset] |= BIT(protect_reg_bit);
		else
			protect_reg_cache[protect_reg_offset] &= ~BIT(protect_reg_bit);
	}

	for (i = 0; i < mspm0_info->protect_reg_count; i++) {
		target_write_u32(target,
				 mspm0_info->protect_reg_base + (i * 4),
				 protect_reg_cache[i]);
	}

	/*
	 * Update our local state data base, since single bit can protect up to
	 * 8 sectors in some banks
	 */
	for (i = 0; i < bank->num_sectors; i++) {
		retval =
		    mspm0_protect_reg_map(bank, i, &protect_reg_offset, &protect_reg_bit);
		if (retval) {
			bank->sectors[i].is_protected = -1;
			/* Ideally we should never be here as this was checked above. */
			LOG_DEBUG("%s: Sector %d protect regmap fail: %d",
				  mspm0_info->name, i, retval);
			continue;
		}
		bank->sectors[i].is_protected =
		    protect_reg_cache[protect_reg_offset] & BIT(protect_reg_bit);
	}

	return ERROR_OK;
}

static int mspm0_erase(struct flash_bank *bank, unsigned int first, unsigned int last)
{
	struct target *target = bank->target;
	struct mspm0_flash_bank *mspm0_info = bank->driver_priv;
	unsigned int i;
	uint32_t protect_reg_cache[MSPM0_MAX_PROTREGS];

	if (bank->target->state != TARGET_HALTED) {
		LOG_ERROR("%s: Please halt target for erasing flash", mspm0_info->name);
		return ERROR_TARGET_NOT_HALTED;
	}

	if (mspm0_info->did == 0)
		return ERROR_FLASH_BANK_NOT_PROBED;

	/* Pick a copy of the current protection config for later restoration */
	for (i = 0; i < mspm0_info->protect_reg_count; i++) {
		target_read_u32(target,
				mspm0_info->protect_reg_base + (i * 4),
				&protect_reg_cache[i]);
	}

	for (uint32_t csa = first; csa < last; csa++) {
		int retval;
		uint32_t addr = csa * mspm0_info->sector_size;

		retval = mspm0_fctl_unprotect_sector(bank, addr);
		if (retval) {
			LOG_ERROR("%s: Illegal access at address 0x%08" PRIx32
				  "(sector: %d)", mspm0_info->name, addr, csa);
			return retval;
		}
		target_write_u32(target, FCTL_REG_CMDTYPE,
				 (FCTL_CMDTYPE_COMMAND_ERASE | FCTL_CMDTYPE_SIZE_SECTOR));
		target_write_u32(target, FCTL_REG_CMDADDR, addr);
		target_write_u32(target, FCTL_REG_CMDEXEC, FCTL_CMDEXEC_VAL_EXECUTE);
		retval = msmp0_fctl_wait_cmd_ok(bank);
		if (retval) {
			LOG_ERROR("%s: Failed Erasing at address 0x%08" PRIx32
				  "(sector: %d)", mspm0_info->name, addr, csa);
			return retval;
		}
		/*
		 * TRM Says:
		 * Note that the CMDWEPROTx registers are reset to a protected state
		 * at the end of all program and erase operations.  These registers
		 * must be re-configured by software before a new operation is
		 * initiated
		 * Let us just Dump the protection registers back to the system.
		 * That way we retain the protection status as requested by the user
		 */
		for (i = 0; i < mspm0_info->protect_reg_count; i++) {
			target_write_u32(target, mspm0_info->protect_reg_base + (i * 4),
					 protect_reg_cache[i]);
		}
	}

	return ERROR_OK;
}

static int mspm0_write(struct flash_bank *bank, const uint8_t *buffer,
		       uint32_t offset, uint32_t count)
{
	struct target *target = bank->target;
	struct mspm0_flash_bank *mspm0_info = bank->driver_priv;
	unsigned int i;
	uint32_t protect_reg_cache[MSPM0_MAX_PROTREGS];

	/*
	 * XXX: TRM Says:
	 * The number of program operations applied to a given word line must be
	 * monitored to ensure that the maximum word line program limit before
	 * erase is not violated.
	 *
	 * There is no reasonable way we can maintain that state in OpenOCD. So,
	 * Let the manufacturing path figure this out.
	 */

	if (bank->target->state != TARGET_HALTED) {
		LOG_ERROR("%s: Please halt target for programming flash",
			  mspm0_info->name);
		return ERROR_TARGET_NOT_HALTED;
	}

	if (mspm0_info->did == 0)
		return ERROR_FLASH_BANK_NOT_PROBED;

	if (offset % mspm0_info->flash_word_size_bytes) {
		LOG_ERROR("%s: Offset 0x%0" PRIx32 " Must be aligned to %d bytes",
			  mspm0_info->name, offset, mspm0_info->flash_word_size_bytes);
		return ERROR_FLASH_DST_BREAKS_ALIGNMENT;
	}

	/*
	 * Pick a copy of the current protection config for later restoration
	 * We need to restore these regs after every write, so instead of trying
	 * to figure things out on the fly, we just context save and restore
	 */
	for (i = 0; i < mspm0_info->protect_reg_count; i++) {
		target_read_u32(target,
				mspm0_info->protect_reg_base + (i * 4),
				&protect_reg_cache[i]);
	}

	while (count) {
		uint32_t num_bytes_to_write;
		uint32_t data_reg = FCTL_REG_CMDDATA0;
		uint32_t bytes_en;
		int retval;

		/*
		 * If count is not 64 bit aligned, we will do byte wise op to keep things simple
		 * Usually this might mean we need to additional write ops towards
		 * trailing edge, but that is a tiny penalty for image downloads.
		 * NOTE: we are going to assume the device does not support multi-word
		 * programming - there does not seem to be discoverability!
		 */
		if (count < mspm0_info->flash_word_size_bytes)
			num_bytes_to_write = count % mspm0_info->flash_word_size_bytes;
		else
			num_bytes_to_write = mspm0_info->flash_word_size_bytes;

		/* Data bytes to write */
		bytes_en = (1 << num_bytes_to_write) - 1;
		/* ECC chunks to write */
		switch (mspm0_info->flash_word_size_bytes) {
		case 8:
			bytes_en |= BIT(8);
			break;
		case 16:
			bytes_en |= BIT(16);
			bytes_en |= (num_bytes_to_write > 8) ? BIT(17) : 0;
			break;
		default:
			LOG_ERROR("%s: Invalid flash_word_size_bytes %d",
				  mspm0_info->name, mspm0_info->flash_word_size_bytes);
			return ERROR_FAIL;
		}

		target_write_u32(target, FCTL_REG_CMDTYPE,
				 (FCTL_CMDTYPE_COMMAND_PROGRAM |
				  FCTL_CMDTYPE_SIZE_ONEWORD));

		/* When writing to part of flash_word - set the bit fields */
		target_write_u32(target, FCTL_REG_CMDBYTEN, bytes_en);

		target_write_u32(target, FCTL_REG_CMDADDR, offset);

		retval = mspm0_fctl_unprotect_sector(bank, offset);
		if (retval)
			return retval;

		while (num_bytes_to_write) {
			uint32_t sub_count;
			uint32_t write_value;

			/* Make sure alignments are handled correctly */
			(void)memcpy(&write_value, buffer, sizeof(uint32_t));

			target_write_u32(target, data_reg, write_value);
			sub_count =
			    (num_bytes_to_write <
			     sizeof(uint32_t)) ? num_bytes_to_write : 4;
			buffer += sub_count;
			data_reg += sub_count;
			num_bytes_to_write -= sub_count;
			offset += sub_count;
			count -= sub_count;
		}

		target_write_u32(target, FCTL_REG_CMDEXEC, FCTL_CMDEXEC_VAL_EXECUTE);

		retval = msmp0_fctl_wait_cmd_ok(bank);
		if (retval)
			return retval;
		/*
		 * TRM Says:
		 * Note that the CMDWEPROTx registers are reset to a protected state
		 * at the end of all program and erase operations.  These registers
		 * must be re-configured by software before a new operation is
		 * initiated
		 * Let us just Dump the protection registers back to the system.
		 * That way we retain the protection status as requested by the user
		 */
		for (i = 0; i < mspm0_info->protect_reg_count; i++) {
			target_write_u32(target,
					 mspm0_info->protect_reg_base + (i * 4),
					 protect_reg_cache[i]);
		}
	}

	return ERROR_OK;
}

static int mspm0_probe(struct flash_bank *bank)
{
	struct mspm0_flash_bank *mspm0_info = bank->driver_priv;
	int retval;

	/*
	 * If this is a mspm0 chip, it has flash; probe() is just
	 * to figure out how much is present.  Only do it once.
	 */
	if (mspm0_info->did != 0)
		return ERROR_OK;

	/*
	 * mspm0_read_part_info() already handled error checking and
	 * reporting.  Note that it doesn't write, so we don't care about
	 * whether the target is halted or not.
	 */
	retval = mspm0_read_part_info(bank);
	if (retval != ERROR_OK)
		return retval;

	if (bank->sectors) {
		free(bank->sectors);
		bank->sectors = NULL;
	}

	switch (bank->base) {
	case MSPM0_FLASH_BASE_NONMAIN:
		bank->size = 512;
		bank->num_sectors = 0x1;
		mspm0_info->protect_reg_base = FCTL_REG_CMDWEPROTNM;
		mspm0_info->protect_reg_count = 1;
		break;
	case MSPM0_FLASH_BASE_MAIN:
		bank->size = (mspm0_info->main_flash_size_kb * 1024);
		bank->num_sectors = bank->size / mspm0_info->sector_size;
		mspm0_info->protect_reg_base = FCTL_REG_CMDWEPROTA;
		mspm0_info->protect_reg_count = 3;
		break;
	case MSPM0_FLASH_BASE_DATA:
		if (!mspm0_info->data_flash_size_kb) {
			LOG_INFO("%s: Data region NOT available!", mspm0_info->name);
			bank->size = 0x0;
			bank->num_sectors = 0x0;
			return ERROR_OK;
		}
		bank->size = (mspm0_info->main_flash_size_kb * 1024);
		bank->num_sectors = bank->size / mspm0_info->sector_size;
		bank->num_prot_blocks = 0;	/* There is no protection here */
		break;
	default:
		LOG_ERROR("%s: Invalid bank address " TARGET_ADDR_FMT, mspm0_info->name,
			  bank->base);
		return ERROR_FAIL;
	}
	bank->sectors = calloc(bank->num_sectors, sizeof(struct flash_sector));
	if (!bank->sectors) {
		LOG_ERROR("%s: Out of memory for sectors!", mspm0_info->name);
		return ERROR_FAIL;
	}
	for (unsigned int i = 0; i < bank->num_sectors; i++) {
		bank->sectors[i].offset = i * mspm0_info->sector_size;
		bank->sectors[i].size = mspm0_info->sector_size;
		bank->sectors[i].is_erased = -1;
	}

	return ERROR_OK;
}

const struct flash_driver mspm0_flash = {
	.name = "mspm0",
	.flash_bank_command = mspm0_flash_bank_command,
	.erase = mspm0_erase,
	.protect = mspm0_protect,
	.write = mspm0_write,
	.read = default_flash_read,
	.probe = mspm0_probe,
	.auto_probe = mspm0_probe,
	.erase_check = default_flash_blank_check,
	.protect_check = mspm0_protect_check,
	.info = get_mspm0_info,
	.free_driver_priv = default_flash_free_driver_priv,
};
