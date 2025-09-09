// SPDX-License-Identifier: GPL-2.0-or-later

/***************************************************************************
 * Copyright (C) 2025 Texas Instruments Incorporated - https://www.ti.com/
 *
 * Common base driver for CC23XX and CC27XX flash drivers from Texas Instruments.
 ***************************************************************************/

#ifndef CC_LPF3_BASE_H
#define CC_LPF3_BASE_H

#include "imp.h"
#include "cc_lpf3_flash.h"

/* Common flash stage states for both CC23XX and CC27XX */
typedef enum CC_LPF3_FLASH_STAGE {
    CC_LPF3_FLASH_STAGE_INIT     = 0x0,
    CC_LPF3_FLASH_STAGE_ERASE    = 0x1,
    CC_LPF3_FLASH_STAGE_MAIN     = 0x2,
    CC_LPF3_FLASH_STAGE_CCFG     = 0x3,
    CC_LPF3_FLASH_STAGE_SCFG     = 0x4,  /* Only used by CC27XX */
    CC_LPF3_FLASH_STAGE_COMPLETE = 0x5
} CC_LPF3_FLASH_STAGE_T;

/* Common flash operations for both CC23XX and CC27XX */
typedef enum CC_LPF3_FLASH_OP {
    CC_LPF3_FLASH_OP_NONE,
    CC_LPF3_FLASH_OP_CHIP_ERASE,
    CC_LPF3_FLASH_OP_PROG_MAIN,
    CC_LPF3_FLASH_OP_PROG_CCFG,
    CC_LPF3_FLASH_OP_PROG_SCFG,  /* Only used by CC27XX */
    CC_LPF3_FLASH_OP_REVERT_STAGE = 0xFF
} CC_LPF3_FLASH_OP_T;

/* Common part info structure for both CC23XX and CC27XX */
#pragma pack(push, 1)
struct cc_lpf3_part_info {
    const char *partname;
    uint32_t device_id;
    uint32_t part_id;
    uint32_t flash_size;
    uint32_t ram_size;
};
#pragma pack(pop)

/* Function pointer type for chip-specific check_allowed_flash_op implementation */
typedef bool (*check_allowed_flash_op_fn)(int op);

/* Function pointer type for chip-specific check_device_memory_info implementation */
typedef int (*check_device_memory_info_fn)(struct cc_lpf3_flash_bank *cc_lpf3_info, uint32_t device_id, uint32_t part_id);

/* Structure to hold chip-specific function pointers */
struct cc_lpf3_chip_ops {
    check_allowed_flash_op_fn check_allowed_flash_op;
    check_device_memory_info_fn check_device_memory_info;
};

/* Common flash bank command handler */
int cc_lpf3_base_flash_bank_command( struct flash_bank *bank);

/* Common functions for both CC23XX and CC27XX */
int cc_lpf3_base_protect(struct flash_bank *bank, int set, unsigned int first, unsigned int last);
int cc_lpf3_base_erase(struct flash_bank *bank, unsigned int first, unsigned int last);
int cc_lpf3_base_write(struct flash_bank *bank, const uint8_t *buffer, uint32_t offset, uint32_t count);
int cc_lpf3_base_read(struct flash_bank *bank, uint8_t *buffer, uint32_t offset, uint32_t count);
int cc_lpf3_base_verify(struct flash_bank *bank, const uint8_t *buffer, uint32_t offset, uint32_t count);
int cc_lpf3_base_probe(struct flash_bank *bank);
int cc_lpf3_base_get_info(struct flash_bank *bank, struct command_invocation *cmd);

/* Register chip-specific operations */
void cc_lpf3_base_register_chip_ops(struct flash_bank *bank, const struct cc_lpf3_chip_ops *ops);

#endif /* CC_LPF3_BASE_H */
