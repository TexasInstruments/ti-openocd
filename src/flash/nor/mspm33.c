// SPDX-License-Identifier: GPL-2.0-or-later

/***************************************************************************
 * Copyright (C) 2026 Texas Instruments Incorporated - https://www.ti.com/
 *
 * NOR flash driver for MSPM33 class of uC from Texas Instruments.
 *
 ***************************************************************************/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "imp.h"
#include <helper/bits.h>
#include <helper/time_support.h>

/*
 * MSPM33 Region memory map
 * Mentioned in TRM NVM System
 */
#define MSPM33_FLASH_BASE_NONMAIN        0x80101800
#define MSPM33_FLASH_END_NONMAIN         0x80102000
#define MSPM33_FLASH_BASE_MAIN           0x10000000
#define MSPM33_FLASH_BASE_DATA           0x80000000

/*
 * MSPM33 FACTORYREGION registers
 * Mentioned in sdk:
 * source\ti\devices\msp\peripherals\m33\hw_factoryregion.
 */
#define MSPM33_FACTORYREGION             0x80111000
#define MSPM33_TRACEID                   (MSPM33_FACTORYREGION + 0x000)
#define MSPM33_DID                       (MSPM33_FACTORYREGION + 0x004)
#define MSPM33_USERID                    (MSPM33_FACTORYREGION + 0x008)
#define MSPM33_SRAMFLASH                 (MSPM33_FACTORYREGION + 0x01C)

/*
 * MSPM33 FCTL registers
 * Mentioned in sdk:
 * source\ti\devices\msp\peripherals\m33\hw_flashctl.h
 */
#define FLASH_CONTROL_BASE              0x40042000
#define FCTL_REG_DESC                   (FLASH_CONTROL_BASE + 0x10FC)
#define FCTL_REG_CMDEXEC                (FLASH_CONTROL_BASE + 0x1100)
#define FCTL_REG_CMDTYPE                (FLASH_CONTROL_BASE + 0x1104)
#define FCTL_REG_CMDADDR                (FLASH_CONTROL_BASE + 0x1120)
#define FCTL_REG_CMDBYTEN               (FLASH_CONTROL_BASE + 0x1124)
#define FCTL_REG_CMDDATAINDEX           (FLASH_CONTROL_BASE + 0x112C)
#define FCTL_REG_CMDDATA0               (FLASH_CONTROL_BASE + 0x1130)
#define FCTL_REG_CMDWEPROTA             (FLASH_CONTROL_BASE + 0x11D0)
#define FCTL_REG_CMDWEPROTB             (FLASH_CONTROL_BASE + 0x11D4)
#define FCTL_REG_CMDWEPROTNM            (FLASH_CONTROL_BASE + 0x1210)
#define FCTL_REG_STATCMD                (FLASH_CONTROL_BASE + 0x13D0)

/* MSPM33 GSC registers
 * Mentioned in sdk:
 * source\ti\devices\msp\peripherals\m33\hw_gsc.h */
#define GSC_BASE                        0x40047000
#define GSC_REG_FPC_FLSEMCLR            (GSC_BASE + 0x804)
#define GSC_REG_FPC_FLSEMREQ            (GSC_BASE + 0x800)
#define GSC_REG_FPC_FLSEMSTAT           (GSC_BASE + 0x808)
 
/* Mentioned in TRM Architecture(Factory Constants) Section */
/* DID Register bit field positions */
static const unsigned char did_version_hi = 31;
static const unsigned char did_version_lo = 28;
static const unsigned char did_pnum_hi = 27;
static const unsigned char did_pnum_lo = 12;
static const unsigned char did_manufacturer_hi = 11;
static const unsigned char did_manufacturer_lo = 1;

/* USERID Register bit field positions */
static const unsigned char userid_variant_hi = 23;
static const unsigned char userid_variant_lo = 16;
static const unsigned char userid_part_hi = 15;
static const unsigned char userid_part_lo = 0;

/* SRAMFLASH Register bit field positions */
static const unsigned char sramflash_dataflash_hi = 31;
static const unsigned char sramflash_dataflash_lo = 26;
static const unsigned char sramflash_sram_hi = 25;
static const unsigned char sramflash_sram_lo = 16;
static const unsigned char sramflash_banks_hi = 13;
static const unsigned char sramflash_banks_lo = 12;
static const unsigned char sramflash_mainflash_hi = 11;
static const unsigned char sramflash_mainflash_lo = 0;

/* kilobyte to byte*/
static const unsigned int kilobyte_to_byte = 1024;


/* Mentioned in sdk:
 * source\ti\devices\msp\peripherals\hw_flashctl.h*/

/* FCTL_STATCMD[CMDDONE] Bits */
#define FCTL_STATCMD_CMDDONE_MASK       0x00000001
#define FCTL_STATCMD_CMDDONE_STATDONE   0x00000001

/* FCTL_STATCMD[CMDPASS] Bits */
#define FCTL_STATCMD_CMDPASS_MASK       0x00000002
#define FCTL_STATCMD_CMDPASS_STATPASS   0x00000002

/*
 * FCTL_CMDEXEC Bits
 * FCTL_CMDEXEC[VAL] Bits
 */
#define FCTL_CMDEXEC_VAL_EXECUTE        0x00000001

/* FCTL_CMDTYPE[COMMAND] Bits */
#define FCTL_CMDTYPE_COMMAND_PROGRAM    0x00000001
#define FCTL_CMDTYPE_COMMAND_ERASE      0x00000002

/* FCTL_CMDTYPE[SIZE] Bits */
#define FCTL_CMDTYPE_SIZE_ONEWORD       0x00000000
#define FCTL_CMDTYPE_SIZE_SECTOR        0x00000040

/* GSC FPC FLSEM */
#define FPC_FMSEM_CLEAR                 0x00000001
#define FPC_FMSEM_REQUEST               0x00000001

/* Maximum protection registers */
#define MSPM33_MAX_PROTREGS             2

/* Maximum wait time */
#define MSPM33_FLASH_TIMEOUT_MS         8000

/* FLASH sector size */
#define MSPM33_SECTOR_SIZE_BYTES        0x800
#define MSPM33_SECTOR_SIZE_SHIFT		11

/* FLASH word size */
#define MSPM33_WORD_SIZE_BYTES			16

/* Protection Registers Parameters */
#define MSPM33_WEPROTA_LENGTH			32
#define MSPM33_WEPROTB_SECTOR_PER_BIT	8

/* TI manufacturer ID */
#define TI_MANUFACTURER_ID              0x17

/* Defines for probe status */
#define MSPM33_NO_ID_FOUND               0
#define MSPM33_DEV_ID_FOUND              1
#define MSPM33_DEV_PART_ID_FOUND         2

struct mspm33_flash_bank {
	/* Device ID (JTAG ID)*/
	uint32_t did;
	/* Device Unique ID register */
	uint32_t traceid;
	/*Silicon revision Version*/
	unsigned char version;

	const char *name;

	/* Decoded flash information */
	unsigned int data_flash_size_kb;
	unsigned int main_flash_size_kb;
	unsigned int main_flash_num_banks;
	unsigned int sector_size;
	/* Decoded SRAM information */
	unsigned int sram_size_kb;

	/* Flash word size: 128bit = 16 bytes */
	unsigned char flash_word_size_bytes;

	/* Protection register stuff */
	unsigned int protect_reg_base;
	unsigned int protect_reg_count;
};

struct mspm33_part_info {
	const char *part_name;
	unsigned short part;
	unsigned char variant;
};

struct mspm33_family_info {
	const char *family_name;
	unsigned short part_num;
	unsigned char part_count;
	const struct mspm33_part_info *part_info;
};

/* Mentioned in Datasheet Device Factory Constants Section */
static const struct mspm33_part_info mspm33_parts[] = {
	{ "MSPM33C321ASPZR", 0x43B6, 0x10 },
};

static const struct mspm33_family_info mspm33_finf[] = {
	{ "MSPM33C321X", 0xbbb7, ARRAY_SIZE(mspm33_parts), mspm33_parts },
};

/*
 *	OpenOCD command interface
 */

/*
 * flash_bank mspm33 <base> <size> 0 0 <target#>
 * macro that creates the function signature for this handler
 */
FLASH_BANK_COMMAND_HANDLER(mspm33_flash_bank_command)
{
	struct mspm33_flash_bank *mspm33_info;

	/* Checks for base address and returns error if it does not match one the three below */
	switch (bank->base) {
	case MSPM33_FLASH_BASE_NONMAIN:
	case MSPM33_FLASH_BASE_MAIN:
	case MSPM33_FLASH_BASE_DATA:
		break;
	default:
		LOG_ERROR("Invalid bank address " TARGET_ADDR_FMT, bank->base);
		return ERROR_FAIL;
	}

	mspm33_info = calloc(1, sizeof(struct mspm33_flash_bank));
	if (!mspm33_info) {
		LOG_ERROR("%s: Out of memory for mspm33_info!", __func__);
		return ERROR_FAIL;
	}

	bank->driver_priv = mspm33_info;

	mspm33_info->sector_size = MSPM33_SECTOR_SIZE_BYTES;

	return ERROR_OK;
}

/* 
 * Requesting GSC Semaphore by writing 0x1 at 0x40047800
 */
static void mspm33_request_gsc_semaphore(struct flash_bank *bank)
{
	struct target *target = bank->target;
	target_write_u32(target, GSC_REG_FPC_FLSEMREQ, FPC_FMSEM_REQUEST);
}

/* 
 * CLearing GSC Semaphore by writing 0x1 at 0x40047804
 */
static void mspm33_clear_gsc_semaphore(struct flash_bank *bank)
{
	struct target *target = bank->target;
	target_write_u32(target, GSC_REG_FPC_FLSEMCLR, FPC_FMSEM_CLEAR);
}

/*
 * Chip identification and status
 */
static int get_mspm33_info(struct flash_bank *bank,
	struct command_invocation *cmd)
{
	struct mspm33_flash_bank *mspm33_info = bank->driver_priv;

	if (!mspm33_info->did)
		return ERROR_FLASH_BANK_NOT_PROBED;

	command_print_sameline(cmd,
		"\nTI mspm33 information: Chip is "
		"%s rev %d Device Unique ID: 0x%" PRIu32 "\n",
		mspm33_info->name, mspm33_info->version,
		mspm33_info->traceid);
	command_print_sameline(cmd,
		"main flash: %uKiB in %u bank(s), sram: %uKiB, "
		"data flash: %uKiB",
		mspm33_info->main_flash_size_kb,
		mspm33_info->main_flash_num_banks, mspm33_info->sram_size_kb,
		mspm33_info->data_flash_size_kb);

	return ERROR_OK;
}

/* Extract a bitfield helper */
static unsigned int mspm33_extract_val(unsigned int var, unsigned char hi,
	unsigned char lo)
{
	return (var & GENMASK(hi, lo)) >> lo;
}

static int mspm33_read_part_info(struct flash_bank *bank)
{
	struct mspm33_flash_bank *mspm33_info = bank->driver_priv;
	struct target *target = bank->target;
	const struct mspm33_family_info *minfo = NULL;

	/* Read and parse chip identification and flash version register */
	uint32_t did;
	int retval = target_read_u32(target, MSPM33_DID, &did);
	if (retval != ERROR_OK) {
		LOG_ERROR("Failed to read device ID");
		return retval;
	}
	retval = target_read_u32(target, MSPM33_TRACEID, &mspm33_info->traceid);
	if (retval != ERROR_OK) {
		LOG_ERROR("Failed to read trace ID");
		return retval;
	}
	uint32_t userid;
	retval = target_read_u32(target, MSPM33_USERID, &userid);
	if (retval != ERROR_OK) {
		LOG_ERROR("Failed to read user ID");
		return retval;
	}
	uint32_t flashram;
	retval = target_read_u32(target, MSPM33_SRAMFLASH, &flashram);
	if (retval != ERROR_OK) {
		LOG_ERROR("Failed to read sramflash register");
		return retval;
	}

	unsigned char version = mspm33_extract_val(did, did_version_hi,
		did_version_lo);
	unsigned short pnum = mspm33_extract_val(did, did_pnum_hi,
		did_pnum_lo);
	unsigned char variant = mspm33_extract_val(userid, userid_variant_hi,
		userid_variant_lo);
	unsigned short part = mspm33_extract_val(userid, userid_part_hi,
		userid_part_lo);
	unsigned short manufacturer = mspm33_extract_val(did,
		did_manufacturer_hi, did_manufacturer_lo);

	/*
	 * Valid DIE and manufacturer ID?
	 * Check the ALWAYS_1 bit to be 1 and manufacturer to be 0x17. All MSPM33
	 * devices within the Device ID field of the factory constants will
	 * always read 0x17 as it is TI's JEDEC bank and company code. If 1
	 * and 0x17 is not read from their respective registers then it truly
	 * is not a mspm33 device so we will return an error instead of
	 * going any further.
	 */
	if (!(did & BIT(0)) || manufacturer != TI_MANUFACTURER_ID) {
		LOG_WARNING("Unknown Device ID[0x%" PRIx32 "], cannot identify target",
			did);
		LOG_DEBUG("did 0x%" PRIx32 ", traceid 0x%" PRIx32 ", userid 0x%" PRIx32
			", flashram 0x%" PRIx32, did, mspm33_info->traceid, userid,
			flashram);
		return ERROR_FLASH_OPERATION_FAILED;
	}

	/* Initialize master index selector and probe status*/
	unsigned char minfo_idx = 0xff;
	unsigned char probe_status = MSPM33_NO_ID_FOUND;

	/* Check if we at least know the family of devices */
	for (unsigned int i = 0; i < ARRAY_SIZE(mspm33_finf); i++) {
		if (mspm33_finf[i].part_num == pnum) {
			minfo_idx = i;
			minfo = &mspm33_finf[i];
			probe_status = MSPM33_DEV_ID_FOUND;
			break;
		}
	}

	/* Initialize part index selector*/
	unsigned char pinfo_idx = 0xff;

	/*
	 * If we can identify the part number then we will attempt to identify
	 * the specific chip. Otherwise, if we do not know the part number then
	 * it would be useless to identify the specific chip.
	 */
	if (probe_status == MSPM33_DEV_ID_FOUND) {
		/* Can we specifically identify the chip */
		for (unsigned int i = 0; i < minfo->part_count; i++) {
			if (minfo->part_info[i].part == part
				&& minfo->part_info[i].variant == variant) {
				pinfo_idx = i;
				probe_status = MSPM33_DEV_PART_ID_FOUND;
				break;
			}
		}
	}

	/*
	 * We will check the status of our probe within this switch-case statement
	 * using these three scenarios.
	 *
	 * 1) Device, part, and variant ID is unknown.
	 * 2) Device ID is known but the part/variant ID is unknown.
	 * 3) Device ID and part/variant ID is known
	 *
	 * For scenario 1, we allow the user to continue because if the
	 * manufacturer matches TI's JEDEC value and ALWAYS_1 from the device ID
	 * field is correct then the assumption the user is using an mspm33 device
	 * can be made.
	 */
	switch (probe_status) {
	case MSPM33_NO_ID_FOUND:
		mspm33_info->name = "mspm33";
		LOG_INFO("Unidentified PART[0x%x]/variant[0x%x"
			"], unknown DeviceID[0x%x"
			"]. Attempting to proceed as %s.", part, variant, pnum,
			mspm33_info->name);
		break;
	case MSPM33_DEV_ID_FOUND:
		mspm33_info->name = mspm33_finf[minfo_idx].family_name;
		LOG_INFO("Unidentified PART[0x%x]/variant[0x%x"
			"], known DeviceID[0x%x"
			"]. Attempting to proceed as %s.", part, variant, pnum,
			mspm33_info->name);
		break;
	case MSPM33_DEV_PART_ID_FOUND:
	default:
		mspm33_info->name = mspm33_finf[minfo_idx].part_info[pinfo_idx].part_name;
		LOG_DEBUG("Part: %s detected", mspm33_info->name);
		break;
	}


	mspm33_info->did = did;
	mspm33_info->version = version;
	mspm33_info->data_flash_size_kb = mspm33_extract_val(flashram,
		sramflash_dataflash_hi, sramflash_dataflash_lo);
	mspm33_info->main_flash_size_kb = mspm33_extract_val(flashram,
		sramflash_mainflash_hi, sramflash_mainflash_lo);
	mspm33_info->main_flash_num_banks = mspm33_extract_val(flashram,
		sramflash_banks_hi, sramflash_banks_lo);
	mspm33_info->sram_size_kb = mspm33_extract_val(flashram,
		sramflash_sram_hi, sramflash_sram_lo);

	/*
	 * Hardcode flash_word_size unless we find some other pattern
	 * See section 5.1.1 (Key Features mentions the flash word size).
	 * almost all values seem to be 16 bytes, but if there are variance,
	 * then we should update mspm33_part_info structure with this info.
	 */
	mspm33_info->flash_word_size_bytes = MSPM33_WORD_SIZE_BYTES;

	LOG_DEBUG("Detected: main flash: %uKb in %u banks, sram: %uKb, "
		"data flash: %uKb", mspm33_info->main_flash_size_kb,
		mspm33_info->main_flash_num_banks, mspm33_info->sram_size_kb,
		mspm33_info->data_flash_size_kb);

	return ERROR_OK;
}

/*
 * Decode error values
 */
static const struct {
	const unsigned char bit_offset;
	const char *fail_string;
} mspm33_fctl_fail_decode_strings[] = {
	{ 2, "CMDINPROGRESS" },
	{ 4, "FAILWEPROT" },
	{ 5, "FAILVERIFY" },
	{ 6, "FAILILLADDR" },
	{ 7, "FAILMODE" },
	{ 12, "FAILMISC" },
};

static const char *mspm33_fctl_translate_ret_err(unsigned int return_code)
{
	for (unsigned int i = 0;
		i < ARRAY_SIZE(mspm33_fctl_fail_decode_strings); i++) {
		if (return_code &
			BIT(mspm33_fctl_fail_decode_strings[i].bit_offset))
			return mspm33_fctl_fail_decode_strings[i].fail_string;
	}

	/* If unknown error notify the user */
	return "FAILUNKNOWN";
}

static int mspm33_fctl_get_sector_reg(struct flash_bank *bank, unsigned int addr,
	unsigned int *reg, unsigned int *sector_mask)
{
	struct mspm33_flash_bank *mspm33_info = bank->driver_priv;
	int ret = ERROR_OK;
	/* Here we do an 11 bit shift to get the sector number 
	 * eg. if address if 0x800, after bitshift it becomes 1
	 * which gives the correct sector number which is 1
	 */
	unsigned int sector_num = (addr >> MSPM33_SECTOR_SIZE_SHIFT);
	unsigned int sector_in_bank = sector_num;
	/* Get sector_in_bank */
	if (mspm33_info->main_flash_num_banks > 1 &&
		bank->base == MSPM33_FLASH_BASE_MAIN) {
		sector_in_bank =
			sector_num % (mspm33_info->main_flash_size_kb /
			(2 * mspm33_info->main_flash_num_banks));
	}

	/*
	 * NOTE: mspm33 devices will use CMDWEPROTA and CMDWEPROTB
	 * for MAIN flash.
	 */
	switch (bank->base) {
	case MSPM33_FLASH_BASE_MAIN:
	case MSPM33_FLASH_BASE_DATA:
		/* Use CMDWEPROTA 
		 * Protection Register A controlls first 32 sectors
		 * Each bit corresponds to 1 Sector
		 * Bit for Bank 0 and 1 are the same 
		 */
		if (sector_in_bank < MSPM33_WEPROTA_LENGTH) {
			*sector_mask = BIT(sector_in_bank);
			*reg = FCTL_REG_CMDWEPROTA;
		}

		/* Use CMDWEPROTB 
		 * Protection Register B controlls 33-256 (224) sectors
		 * Each bit corresponds to 8 Sector, and total of 28 bit control starting from LSB
		 * Bit for Bank 0 and 1 are the same 
		 * Therefore we subtract 32 and then divide by 8 
		 */
		else {
			*sector_mask = BIT((sector_in_bank - MSPM33_WEPROTA_LENGTH ) / MSPM33_WEPROTB_SECTOR_PER_BIT);
			*reg = FCTL_REG_CMDWEPROTB;
		}
		break;
	case MSPM33_FLASH_BASE_NONMAIN:
		*sector_mask = BIT(sector_num % 4);
		*reg = FCTL_REG_CMDWEPROTNM;
		break;
	default:
		/*
		 * Not expected to reach here due to check in mspm33_address_check()
		 * but adding it as another layer of safety.
		 */
		ret = ERROR_FLASH_DST_OUT_OF_BANK;
		break;
	}

	if (ret != ERROR_OK)
		LOG_ERROR("Unable to map sector protect reg for address 0x%08x", addr);

	return ret;
}

static int mspm33_address_check(struct flash_bank *bank, unsigned int addr)
{
	struct mspm33_flash_bank *mspm33_info = bank->driver_priv;
	unsigned int flash_main_size = mspm33_info->main_flash_size_kb * kilobyte_to_byte;
	unsigned int flash_data_size = mspm33_info->data_flash_size_kb * kilobyte_to_byte;
	int ret = ERROR_FLASH_SECTOR_INVALID;

	/*
	 * Before unprotecting any memory lets make sure that the address and
	 * bank given is a known bank and whether or not the address falls under
	 * the proper bank.
	 */
	switch (bank->base) {
	case MSPM33_FLASH_BASE_MAIN:
		if (addr <= (MSPM33_FLASH_BASE_MAIN + flash_main_size))
			ret = ERROR_OK;
		break;
	case MSPM33_FLASH_BASE_NONMAIN:
		if (addr >= MSPM33_FLASH_BASE_NONMAIN && addr <= MSPM33_FLASH_END_NONMAIN)
			ret = ERROR_OK;
		break;
	case MSPM33_FLASH_BASE_DATA:
		if (addr >= MSPM33_FLASH_BASE_DATA &&
		addr <= (MSPM33_FLASH_BASE_DATA + flash_data_size))
			ret = ERROR_OK;
		break;
	default:
		ret = ERROR_FLASH_DST_OUT_OF_BANK;
		break;
	}

	return ret;
}

static int mspm33_fctl_unprotect_sector(struct flash_bank *bank, unsigned int addr)
{
	struct target *target = bank->target;
	unsigned int reg = 0x0;
	uint32_t sector_mask = 0x0;
	int ret;

	ret = mspm33_address_check(bank, addr);
	switch (ret) {
	case ERROR_FLASH_SECTOR_INVALID:
		LOG_ERROR("Unable to map sector protect reg for address 0x%08x", addr);
		break;
	case ERROR_FLASH_DST_OUT_OF_BANK:
		LOG_ERROR("Unable to determine which bank to use 0x%08x", addr);
		break;
	default:
		mspm33_fctl_get_sector_reg(bank, addr, &reg, &sector_mask);
		ret = target_write_u32(target, reg, ~sector_mask);
		break;
	}

	return ret;
}

static int mspm33_fctl_cfg_command(struct flash_bank *bank,
	uint32_t addr,
	uint32_t cmd,
	uint32_t byte_en)
{
	struct target *target = bank->target;

	/*
	 * Configure the flash operation within the CMDTYPE register, byte_en
	 * bits if needed, and then set the address where the flash operation
	 * will execute.
	 */
	int retval = target_write_u32(target, FCTL_REG_CMDTYPE, cmd);
	if (retval != ERROR_OK)
		return retval;
	if (byte_en) {
		retval = target_write_u32(target, FCTL_REG_CMDBYTEN, byte_en);
		if (retval != ERROR_OK)
			return retval;
	}

	return target_write_u32(target, FCTL_REG_CMDADDR, addr);
}

static int mspm33_fctl_wait_cmd_ok(struct flash_bank *bank)
{
	struct target *target = bank->target;
	uint32_t return_code = 0;
	int64_t start_ms;
	int64_t elapsed_ms;

	start_ms = timeval_ms();
	while ((return_code & FCTL_STATCMD_CMDDONE_MASK) !=
		FCTL_STATCMD_CMDDONE_STATDONE) {
		int retval = target_read_u32(target, FCTL_REG_STATCMD, &return_code);
		if (retval != ERROR_OK)
			return retval;

		elapsed_ms = timeval_ms() - start_ms;
		if (elapsed_ms > MSPM33_FLASH_TIMEOUT_MS)
			break;

		keep_alive();
	}

	if ((return_code & FCTL_STATCMD_CMDPASS_MASK) !=
		FCTL_STATCMD_CMDPASS_STATPASS) {
		LOG_ERROR("Flash command failed: %s",
			mspm33_fctl_translate_ret_err(return_code));
		return ERROR_FAIL;
	}

	return ERROR_OK;
}

static int mspm33_fctl_sector_erase(struct flash_bank *bank, uint32_t addr)
{
	struct target *target = bank->target;

	/*
	 * TRM Says:
	 * Note that the CMDWEPROTx registers are reset to a protected state
	 * at the end of all program and erase operations.  These registers
	 * must be re-configured by software before a new operation is
	 * initiated.
	 *
	 * This means that as we start erasing sector by sector, the protection
	 * registers are reset and need to be unprotected *again* for the next
	 * erase operation. Unfortunately, this means that we cannot do a unitary
	 * unprotect operation independent of flash erase operation
	 */
	int retval = mspm33_fctl_unprotect_sector(bank, addr);
	if (retval != ERROR_OK) {
		LOG_ERROR("Unprotecting sector of memory at address 0x%08" PRIx32
			" failed", addr);
		return retval;
	}

	LOG_DEBUG("Unprotected sector at address 0x%08" PRIx32, addr);

	/* Actual erase operation */
	retval = mspm33_fctl_cfg_command(bank, addr,
		(FCTL_CMDTYPE_COMMAND_ERASE | FCTL_CMDTYPE_SIZE_SECTOR), 0);
	if (retval != ERROR_OK)
		return retval;
	retval = target_write_u32(target, FCTL_REG_CMDEXEC, FCTL_CMDEXEC_VAL_EXECUTE);
	if (retval != ERROR_OK)
		return retval;

	return mspm33_fctl_wait_cmd_ok(bank);
}

static int mspm33_protect_check(struct flash_bank *bank)
{
	struct mspm33_flash_bank *mspm33_info = bank->driver_priv;

	if (!mspm33_info->did)
		return ERROR_FLASH_BANK_NOT_PROBED;

	/*
	 * TRM Says:
	 * Note that the CMDWEPROTx registers are reset to a protected state
	 * at the end of all program and erase operations.  These registers
	 * must be re-configured by software before a new operation is
	 * initiated.
	 *
	 * This means that when any flash operation is performed at a block level,
	 * the block is locked back again. This prevents usage where we can set a
	 * protection level once at the flash level and then do erase / write
	 * operation without touching the protection register (since it is
	 * reset by hardware automatically). In effect, we cannot use the hardware
	 * defined protection scheme in openOCD.
	 *
	 * To deal with this protection scheme, the CMDWEPROTx register that
	 * correlates to the sector is modified at the time of operation and as far
	 * openOCD is concerned, the flash operates as completely un-protected
	 * flash.
	 */
	for (unsigned int i = 0; i < bank->num_sectors; i++)
		bank->sectors[i].is_protected = 0;

	return ERROR_OK;
}

static int mspm33_erase(struct flash_bank *bank, unsigned int first, unsigned int last)
{
	struct mspm33_flash_bank *mspm33_info = bank->driver_priv;
	int retval = ERROR_OK;

	/* Request GSC semaphore for erasing flash */
	mspm33_request_gsc_semaphore(bank);

	if (bank->target->state != TARGET_HALTED) {
		LOG_ERROR("Please halt target for erasing flash");
		return ERROR_TARGET_NOT_HALTED;
	}

	if (!mspm33_info->did)
		return ERROR_FLASH_BANK_NOT_PROBED;

	switch (bank->base) {
	case MSPM33_FLASH_BASE_MAIN:
		for (unsigned int csa = first; csa <= last; csa++) {
			unsigned int addr = csa * mspm33_info->sector_size;
			retval = mspm33_fctl_sector_erase(bank, addr);
			if (retval != ERROR_OK)
				LOG_ERROR("Sector erase on MAIN failed at address 0x%08x "
						"(sector: %u)", addr, csa);
		}
		break;
	case MSPM33_FLASH_BASE_NONMAIN:
		retval = mspm33_fctl_sector_erase(bank, MSPM33_FLASH_BASE_NONMAIN);
		if (retval != ERROR_OK)
			LOG_ERROR("Sector erase on NONMAIN failed");
		break;
	case MSPM33_FLASH_BASE_DATA:
		for (unsigned int csa = first; csa <= last; csa++) {
			unsigned int addr = (MSPM33_FLASH_BASE_DATA +
			(csa * mspm33_info->sector_size));
			retval = mspm33_fctl_sector_erase(bank, addr);
			if (retval != ERROR_OK)
				LOG_ERROR("Sector erase on DATA bank failed at address 0x%08x "
						"(sector: %u)", addr, csa);
		}
		break;
	default:
		LOG_ERROR("Invalid memory region access");
		retval = ERROR_FLASH_BANK_INVALID;
		break;
	}

	/* If there were any issues in our checks, return the error */
	if (retval != ERROR_OK)
		return retval;

	/*
	 * TRM Says:
	 * Note that the CMDWEPROTx registers are reset to a protected state
	 * at the end of all program and erase operations.  These registers
	 * must be re-configured by software before a new operation is
	 * initiated
	 */

	mspm33_clear_gsc_semaphore(bank);

	return retval;
}

static int mspm33_write(struct flash_bank *bank, const unsigned char *buffer,
	unsigned int offset, unsigned int count)
{
	struct target *target = bank->target;
	struct mspm33_flash_bank *mspm33_info = bank->driver_priv;
	int retval;

	mspm33_request_gsc_semaphore(bank);

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
		LOG_ERROR("Please halt target for programming flash");
		return ERROR_TARGET_NOT_HALTED;
	}

	if (!mspm33_info->did)
		return ERROR_FLASH_BANK_NOT_PROBED;

	/* Add proper memory offset for bank being written to */
	unsigned int addr = bank->base + offset;

	while (count) {
		unsigned int num_bytes_to_write;
		uint32_t bytes_en;
		if (count < mspm33_info->flash_word_size_bytes)
			num_bytes_to_write = count;
		else
			num_bytes_to_write = mspm33_info->flash_word_size_bytes;

		/* Data bytes to write */
		bytes_en = (1 << num_bytes_to_write) - 1;
		/* ECC chunks to write */
		switch (mspm33_info->flash_word_size_bytes) {
		case 8:
			bytes_en |= BIT(8);
			break;
		case 16:
			bytes_en |= BIT(16);
			bytes_en |= (num_bytes_to_write > 8) ? BIT(17) : 0;
			break;
		default:
			LOG_ERROR("Invalid flash_word_size_bytes %d",
				mspm33_info->flash_word_size_bytes);
			return ERROR_FAIL;
		}

		retval = mspm33_fctl_cfg_command(bank, addr,
			(FCTL_CMDTYPE_COMMAND_PROGRAM | FCTL_CMDTYPE_SIZE_ONEWORD),
			bytes_en);
		if (retval != ERROR_OK)
			return retval;

		retval = mspm33_fctl_unprotect_sector(bank, addr);
		if (retval != ERROR_OK)
			return retval;

		/*
		 * Calculate flash word index (0-3) within 64-byte block:
		 * offset in block / 16-byte word size. This allows us to write
		 * into the flash while only using the first 4 flash data
		 * registers, instead of using the entire 16 data registers.
		 */
		uint32_t index = ((addr - bank->base) % 64) / 16;
		retval = target_write_u32(target, FCTL_REG_CMDDATAINDEX, index);
		if (retval != ERROR_OK)
			return retval;

		/* Actual command that pushes data into flash control register */
		retval = target_write_buffer(target, FCTL_REG_CMDDATA0,
			num_bytes_to_write, buffer);
		if (retval != ERROR_OK)
			return retval;

		addr += num_bytes_to_write;
		buffer += num_bytes_to_write;
		count -= num_bytes_to_write;

		retval = target_write_u32(target, FCTL_REG_CMDEXEC,
			FCTL_CMDEXEC_VAL_EXECUTE);
		if (retval != ERROR_OK)
			return retval;

		retval = mspm33_fctl_wait_cmd_ok(bank);
		if (retval != ERROR_OK) {
			LOG_ERROR("Flash command failed at addr 0x%08x",
				addr - num_bytes_to_write);
			return retval;
		}
	}

	/*
	 * TRM Says:
	 * Note that the CMDWEPROTx registers are reset to a protected state
	 * at the end of all program and erase operations.  These registers
	 * must be re-configured by software before a new operation is
	 * initiated
	 */

	mspm33_clear_gsc_semaphore(bank);

	return ERROR_OK;
}

static int mspm33_probe(struct flash_bank *bank)
{
	struct mspm33_flash_bank *mspm33_info = bank->driver_priv;

	mspm33_request_gsc_semaphore(bank);

	/*
	 * If this is a mspm33 chip, it has flash; probe() is just
	 * to figure out how much is present.  Only do it once.
	 */
	if (mspm33_info->did)
		return ERROR_OK;

	/*
	 * mspm33_read_part_info() already handled error checking and
	 * reporting.  Note that it doesn't write, so we don't care about
	 * whether the target is halted or not.
	 */
	int retval = mspm33_read_part_info(bank);
	if (retval != ERROR_OK)
		return retval;

	if (bank->sectors) {
		free(bank->sectors);
		bank->sectors = NULL;
	}

	bank->write_start_alignment = 4;
	bank->write_end_alignment = 4;

	switch (bank->base) {
	case MSPM33_FLASH_BASE_NONMAIN:
		bank->size = 2048;
		bank->num_sectors = 0x1;
		mspm33_info->protect_reg_base = FCTL_REG_CMDWEPROTNM;
		mspm33_info->protect_reg_count = 1;
		break;
	case MSPM33_FLASH_BASE_MAIN:
		bank->size = (mspm33_info->main_flash_size_kb * 1024);
		bank->num_sectors = bank->size / mspm33_info->sector_size;
		/* use CMDWEPROTA and CMDWEPROTB for MAIN memory protection */
		mspm33_info->protect_reg_base = FCTL_REG_CMDWEPROTA;
		mspm33_info->protect_reg_count = 2;
		break;
	case MSPM33_FLASH_BASE_DATA:
		if (!mspm33_info->data_flash_size_kb) {
			LOG_INFO("Data region NOT available!");
			bank->size = 0x0;
			bank->num_sectors = 0x0;
			return ERROR_OK;
		}
		/*
		 * Since data bank is treated like MAIN memory, it will
		 * also use CMDWEPROTA and CMDWEPROTB for protection.
		 */
		bank->size = (mspm33_info->data_flash_size_kb * 1024);
		bank->num_sectors = bank->size / mspm33_info->sector_size;
		mspm33_info->protect_reg_base = FCTL_REG_CMDWEPROTA;
		mspm33_info->protect_reg_count = 2;
		break;
	default:
		LOG_ERROR("Invalid bank address " TARGET_ADDR_FMT,
			bank->base);
		return ERROR_FAIL;
	}

	bank->sectors = calloc(bank->num_sectors, sizeof(struct flash_sector));
	if (!bank->sectors) {
		LOG_ERROR("Out of memory for sectors!");
		return ERROR_FAIL;
	}
	for (unsigned int i = 0; i < bank->num_sectors; i++) {
		bank->sectors[i].offset = i * mspm33_info->sector_size;
		bank->sectors[i].size = mspm33_info->sector_size;
		bank->sectors[i].is_erased = -1;
	}

	mspm33_clear_gsc_semaphore(bank);

	return ERROR_OK;
}

const struct flash_driver mspm33_flash = {
	.name = "mspm33",
	.flash_bank_command = mspm33_flash_bank_command,
	.erase = mspm33_erase,
	.protect = NULL,
	.write = mspm33_write,
	.read = default_flash_read,
	.probe = mspm33_probe,
	.auto_probe = mspm33_probe,
	.erase_check = default_flash_blank_check,
	.protect_check = mspm33_protect_check,
	.info = get_mspm33_info,
	.free_driver_priv = default_flash_free_driver_priv,
};