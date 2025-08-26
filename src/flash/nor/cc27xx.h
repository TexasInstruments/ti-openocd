// SPDX-License-Identifier: GPL-2.0-or-later

/***************************************************************************
 * Copyright (C) 2025 Texas Instruments Incorporated - https://www.ti.com/
 *
 * NOR flash driver for CC27XX from Texas Instruments.
 ***************************************************************************/

/* States are maintained in bit wise check. For cc27xx Erase,
   main and ccfg write will make flash write complete
 */
typedef enum CC27XX_FLASH_STAGE{
	CC27XX_FLASH_STAGE_INIT		= 0x0,
	CC27XX_FLASH_STAGE_ERASE	= 0x1,
	CC27XX_FLASH_STAGE_MAIN		= 0x2,
	CC27XX_FLASH_STAGE_CCFG		= 0x3,
	CC27XX_FLASH_STAGE_SCFG		= 0x4,
	CC27XX_FLASH_STAGE_COMPLETE	= 0x5
}CC27XX_FLASH_STAGE_T;

typedef enum CC27XX_FLASH_OP{
	CC27XX_FLASH_OP_NONE,
	CC27XX_FLASH_OP_CHIP_ERASE,
	CC27XX_FLASH_OP_PROG_MAIN,
	CC27XX_FLASH_OP_PROG_CCFG,
	CC27XX_FLASH_OP_PROG_SCFG,
	CC27XX_FLASH_OP_REVERT_STAGE = 0xFF
}CC27XX_FLASH_OP_T;

#pragma pack(push, 1)
struct cc27xx_part_info {
	const char *partname;
	uint32_t device_id;
	uint32_t part_id;
	uint32_t flash_size;
	uint32_t ram_size;
};
#pragma pack(pop)
