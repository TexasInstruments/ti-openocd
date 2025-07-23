// SPDX-License-Identifier: GPL-2.0-or-later

/***************************************************************************
 * Copyright (C) 2025 Texas Instruments Incorporated - https://www.ti.com/
 *
 * NOR flash driver for CC23XX from Texas Instruments.
 ***************************************************************************/

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
