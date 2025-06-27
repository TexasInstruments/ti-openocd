// SPDX-License-Identifier: GPL-2.0-or-later

/***************************************************************************
 * Copyright (C) 2025 Texas Instruments Incorporated - https://www.ti.com/
 *
 * NOR flash driver for CC2340R5 from Texas Instruments.
 ***************************************************************************/

/// Size of one MAIN flash sector, in number of bytes
#define MAIN_SECTOR_SIZE_WORDS			(512)

/// The maximum CCFG size of all devices that uses SACI.
#define MAX_CCFG_SIZE				MAIN_SECTOR_SIZE_WORDS
#define MAX_CCFG_SIZE_IN_BYTES			(MAX_CCFG_SIZE * 4)

/// Size of one MAIN flash sector, in number of bytes
#define CC2340R5_MAIN_FLASH_SIZE		(0x80000U) //512Kb
#define CC2340R5_MAIN_FLASH_SECTOR_SIZE (0x800U) //2Kb

#define BOOT_CCFG_START_IDX				(0x0)
#define CENTRAL_CCFG_START_IDX			(0x10)
#define DEBUG_CCFG_START_IDX			(0x7D0)

#define BOOT_CCFG_CRC_LEN				(0x0C)
#define CENTRAL_CCFG_CRC_LEN			(0x73C)
#define DEBUG_CCFG_CRC_LEN				(0x2C)

/// The maximum user record size of all devices that uses SACI.
#define MAX_CCFG_USER_RECORD_SIZE		(128)
#define MAX_CCFG_USER_RECORD_SIZE_WORDS (32)

#define CC2340R5_DEVICE_ID				(0x1BB8402F)

/* CC2340R5 Region memory map */
#define CC23XX_FLASH_BASE_CCFG			(0x4E020000)
#define CC23XX_FLASH_BASE_MAIN			(0x0)

#define CC2340R5_FLASH_SIZE				(0x00080000)
#define CC2340R5_SRAM_SIZE				(0x00009000)

/* States are maintained in bit wise check. For cc23xx Erase,
   main and ccfg write will make flash write complete
 */
typedef enum CC23XX_FLASH_STAGE{
	CC23XX_FLASH_STAGE_INIT		= 0x0,
	CC23XX_FLASH_STAGE_ERASE	= 0x1,
	CC23XX_FLASH_STAGE_MAIN		= 0x2,
	CC23XX_FLASH_STAGE_CCFG		= 0x3,
	CC23XX_FLASH_STAGE_COMPLETE	= 0x4
}CC23XX_FLASH_STAGE_T;

typedef enum CC23XX_FLASH_OP{
	CC23XX_FLASH_OP_NONE,
	CC23XX_FLASH_OP_CHIP_ERASE,
	CC23XX_FLASH_OP_PROG_MAIN,
	CC23XX_FLASH_OP_PROG_CCFG,
	CC23XX_FLASH_OP_REVERT_STAGE = 0xFF
}CC23XX_FLASH_OP_T;

#pragma pack(push, 1)
struct cc23xx_part_info {
	const char *partname;
	uint32_t device_id;
	uint32_t part_id;
	uint32_t flash_size;
	uint32_t ram_size;
};
#pragma pack(pop)
