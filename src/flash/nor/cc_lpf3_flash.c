//SPDX-License-Identifier: GPL-2.0-or-later

/***************************************************************************
 * Copyright (C) 2025 Texas Instruments Incorporated - https://www.ti.com/
 *
 * LPF3 specific flash driver algorithms from Texas Instruments.
 ***************************************************************************/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "jtag/interface.h"
#include "imp.h"
#include "cc_lpf3_flash.h"
#include <helper/bits.h>
#include <helper/time_support.h>
#include <target/arm_adi_v5.h>
#include <target/armv7m.h>
#include <target/cortex_m.h>
#include <target/image.h>

/*
 * Calculate CRC as per the polynomial mentioned in
 * CC2340R5/CC2745R10 TRM section 9.2(SWCU193A – APRIL 2023 – REVISED AUGUST 2024)
 */
static uint32_t cc_lpf3_calculate_crc(const uint8_t *data_ptr, uint32_t length)
{
	uint32_t data = 0, index = 0, acc = 0xFFFFFFFFU;
	//The LUT is build by selecting every 16th entry in the precalculated CRC32
	//table that has 256 entries.
	static const uint32_t crc_rand32_lut[] = {
		0x00000000U, 0x1DB71064U, 0x3B6E20C8U, 0x26D930ACU,
		0x76DC4190U, 0x6B6B51F4U, 0x4DB26158U, 0x5005713CU,
		0xEDB88320U, 0xF00F9344U, 0xD6D6A3E8U, 0xCB61B38CU,
		0x9B64C2B0U, 0x86D3D2D4U, 0xA00AE278U, 0xBDBDF21CU
	};
	if (data_ptr)
	{
		while (length--) {
			data = *data_ptr;
			index = (acc & 0x0F) ^ (data & 0x0F);
			acc = (acc >> 4) ^ crc_rand32_lut[index];
			index = (acc & 0x0F) ^ (data >> 4);
			acc = (acc >> 4) ^ crc_rand32_lut[index];
			data_ptr++;
		}
	}
	return (acc ^ 0xFFFFFFFFU);
}


/*
 * Flash driver should pass sector aligned data over SACI.
 * SACI_CMD_FLASH_PROG_MAIN_PIPELINED doesnt have length option
 */
uint32_t* cc_lpf3_flash_sector_padding(const uint8_t *buffer, uint32_t *count)
{
	uint32_t *sector_aligned_data = NULL;
	uint32_t bytes_to_pad = 0, start_count = (uint32_t)*count;

	//Allocate maximum size that may be required in case of padding
	//Caller function should make sure allocated memory is freed
	sector_aligned_data = (uint32_t*)malloc(start_count + LPF3_MAIN_FLASH_SECTOR_SIZE);

	if (sector_aligned_data == NULL) {
		LOG_ERROR("Failed to allocate memory for sector aligned data");
		return NULL;
	}
	if (start_count%LPF3_MAIN_FLASH_SECTOR_SIZE)
		bytes_to_pad = LPF3_MAIN_FLASH_SECTOR_SIZE - start_count%LPF3_MAIN_FLASH_SECTOR_SIZE;

	memset((uint8_t*)(sector_aligned_data)+start_count, 0xFF, bytes_to_pad);
	memcpy(sector_aligned_data, buffer, start_count);

	//update the pointer if padding is added
	*count = start_count + bytes_to_pad;

	return sector_aligned_data;
}

/*
 * Response sequence number that should be included in the command
 * this is mainly critical for commands sent without need of response
 */
static uint8_t cc_lpf3_get_resp_seqnum(void)
{
	static uint8_t seq_num = 0;
	return seq_num++;
}

/*
 * Write to AP function can write a single word into the AP specified
 */
static int cc_lpf3_write_to_AP(struct flash_bank *bank, uint64_t ap_num, unsigned int reg, uint32_t value)
{
	struct cortex_m_common *cortex_m = target_to_cm(bank->target);
	struct adiv5_dap *dap = cortex_m->armv7m.arm.dap;
	struct adiv5_ap *ap = dap_get_ap(dap, ap_num);
	int ret_val = ERROR_FAIL;

	if (!ap) {
		LOG_ERROR("write_to_AP: failed to get AP");
		return ret_val;
	}

	ret_val = dap_queue_ap_write(ap, reg, value);
	if (ret_val != ERROR_OK) {
		LOG_ERROR("write_to_AP: failed to queue a write request");
		dap_put_ap(ap);
		return ret_val;
	}

	ret_val = dap_run(dap);
	dap_put_ap(ap);
	if (ret_val != ERROR_OK) {
		LOG_ERROR("write_to_AP: dap_run failed");
		return ret_val;
	}

	return ERROR_OK;
}

/*
 * Read data from AP
 */
int cc_lpf3_read_from_AP(struct flash_bank *bank, uint64_t ap_num, unsigned int reg, uint32_t *data)
{
	struct cortex_m_common *cortex_m = target_to_cm(bank->target);
	struct adiv5_dap *dap = cortex_m->armv7m.arm.dap;
	struct adiv5_ap *ap = dap_get_ap(dap, ap_num);

	if (!ap) {
		LOG_ERROR("DEBUGSS: failed to get AP %d", (uint32_t)ap_num);
		return ERROR_FAIL;
	}

	int ret_val = dap_queue_ap_read(ap, reg, data);
	if (ret_val != ERROR_OK) {
		LOG_INFO("DEBUGSS: failed to queue a read request %x", reg);
		dap_put_ap(ap);
		return ret_val;
	}

	ret_val = dap_run(dap);
	dap_put_ap(ap);
	if (ret_val != ERROR_OK) {
		LOG_INFO("DEBUGSS: dap_run failed reg:%d ret_val:%d", reg, ret_val);
		return ret_val;
	}

	return ERROR_OK;
}

/*
 * Bulk Write to AP function can write upto LPF3_MAIN_FLASH_SECTOR_SIZE words
 * into the AP specified in the argument
 */
static int cc_lpf3_bulk_write_to_AP(struct flash_bank *bank, uint64_t ap_num, unsigned int reg, uint32_t *data,
 uint32_t count)
{
	struct cortex_m_common *cortex_m = target_to_cm(bank->target);
	struct adiv5_dap *dap = cortex_m->armv7m.arm.dap;
	struct adiv5_ap *ap = dap_get_ap(dap, ap_num);
	int ret_val = ERROR_FAIL;

	if (!ap) {
		LOG_ERROR("bulk_write_to_AP: failed to get AP");
		return ret_val;
	}

	if (!data) {
		LOG_ERROR("bulk_write_to_AP: failed, no buffer");
		return ret_val;
	}

	if (count > LPF3_MAIN_FLASH_SECTOR_SIZE)
	{
		LOG_ERROR("bulk_write_to_AP: length more than LPF3_MAIN_FLASH_SECTOR_SIZE");\
		return ret_val;
	}

	for (uint32_t word = 0; word < count; word++)	{
		ret_val = dap_queue_ap_write(ap, reg, data[word]);
		if (ret_val != ERROR_OK) {
			LOG_ERROR("write_to_AP: failed to queue a write request");
			dap_put_ap(ap);
			return ret_val;
		}
	}

	ret_val = dap_run(dap);
	dap_put_ap(ap);
	if (ret_val != ERROR_OK) {
		LOG_ERROR("write_to_AP: dap_run failed");
		return ret_val;
	}

	return ERROR_OK;
}

/*
 * Read device information from the config AP, CFG AP shall have deice and
 * part specific information
 */
int cc_lpf3_check_device_info(struct flash_bank *bank)
{
	//Connect and Read Device Status from CFG AP
	uint32_t result;
	struct cc_lpf3_flash_bank *cc_lpf3_info = bank->driver_priv;

	int ret_val = cc_lpf3_read_from_AP(bank, DEBUGSS_CFG_AP, CFG_AP_DEVICE_STATUS, &result);

	if (ret_val != ERROR_OK) {
		LOG_DEBUG("cc_lpf3_check_device_status: CFG-AP Read Fail");
		return ret_val;
	}

	//can further check more details in the cfg-ap for more device status
	if ( ERROR_OK == cc_lpf3_read_from_AP(bank, DEBUGSS_CFG_AP, CFG_AP_DEVICE_ID_READ, &result))
		cc_lpf3_info->did = result;
	else
		return ERROR_FAIL;

	return ERROR_OK;
}

/*
 * Prepare write by sending NOP over Sec-AP interface
 */
int cc_lpf3_prepare_write(struct flash_bank *bank)
{
	int ret_val;
	SACI_PARAM_T saci_cmd;

	ret_val =  cc_lpf3_check_boot_status(bank);
	if (BOOTSTA_BOOT_ENTERED_SACI != ret_val)
		return ret_val;

	LOG_INFO("cc_lpf3_prepare_write: Device IN SACI Mode");
	memset((uint8_t*)&saci_cmd, 0, sizeof(SACI_PARAM_T));

	saci_cmd.common.cmd.cmd_id = SACI_MISC_NO_OPERATION;
	ret_val = cc_lpf3_saci_send_cmd(bank, saci_cmd);
	if (ret_val != ERROR_OK)	{
		LOG_ERROR("NOP Fail - ret %d", ret_val);
		return ret_val;
	}

	return ERROR_OK;
}

/*
 * Get the exact command length based on the SACI command
 */
static int cc_lpf3_get_cmd_word_length(SACI_PARAM_T cmd)
{
	switch (cmd.common.cmd.cmd_id) {
	case SACI_MISC_NO_OPERATION:
		return SIZE_IN_WORDS(SACI_PARAM_COMMON_T);
		break;
	case SACI_MISC_GET_TEST_ID:
		return SIZE_IN_WORDS(SACI_PARAM_COMMON_T);
		break;
	case SACI_MISC_GET_DIE_ID:
		return SIZE_IN_WORDS(SACI_PARAM_COMMON_T);
		break;
	case SACI_MISC_GET_CCFG_USER_REC:
		return SIZE_IN_WORDS(SACI_PARAM_COMMON_T);
		break;
	case SACI_DEBUG_REQ_PWD_ID:
		return SIZE_IN_WORDS(SACI_PARAM_COMMON_T);
		break;
	case SACI_DEBUG_SUBMIT_AUTH:
		return SIZE_IN_WORDS(SACI_PARAM_DEBUG_SUBMIT_AUTH_T);
		break;
	case SACI_DEBUG_EXIT_SACI_HALT:
		return SIZE_IN_WORDS(SACI_PARAM_COMMON_T);
		break;
	case SACI_DEBUG_EXIT_SACI_SHUTDOWN:
		return SIZE_IN_WORDS(SACI_PARAM_COMMON_T);
		break;
	case SACI_FLASH_ERASE_CHIP:
		return SIZE_IN_WORDS(SACI_PARAM_FLASH_ERASE_CHIP_T);
		break;
	case SACI_FLASH_PROG_CCFG_SECTOR:
		return SIZE_IN_WORDS(SACI_PARAM_FLASH_PROG_CCFG_SECTOR_T);
		break;
	case SACI_FLASH_PROG_SCFG_SECTOR:
		return SIZE_IN_WORDS(SACI_PARAM_FLASH_PROG_SCFG_SECTOR_T);
		break;
	case SACI_FLASH_PROG_CCFG_USER_REC:
		return SIZE_IN_WORDS(SACI_PARAM_FLASH_PROG_CCFG_USER_REC_T);
		break;
	case SACI_FLASH_PROG_MAIN_PIPELINED:
		return SIZE_IN_WORDS(SACI_PARAM_FLASH_PROG_MAIN_PIPELINED_T);
		break;
	case SACI_FLASH_VERIFY_MAIN_SECTORS:
		return SIZE_IN_WORDS(SACI_PARAM_FLASH_VERIFY_MAIN_SECTORS_T);
		break;
	case SACI_FLASH_VERIFY_CCFG_SECTOR:
		return SIZE_IN_WORDS(SACI_PARAM_FLASH_VERIFY_CCFG_SECTOR_T);
		break;
	case SACI_FLASH_VERIFY_SCFG_SECTOR:
		return SIZE_IN_WORDS(SACI_PARAM_FLASH_VERIFY_SCFG_SECTOR_T);
		break;
	case SACI_LIFECYCLE_INCR_STATE:
		return SIZE_IN_WORDS(SACI_PARAM_LIFECYCLE_INCR_STATE_T);
		break;
	case SACI_LIFECYCLE_REQ_FIRST_BDAY:
		return SIZE_IN_WORDS(SACI_PARAM_LIFECYCLE_REQ_FIRST_BDAY_T);
		break;
	case SACI_BLDR_APP_RESET_DEVICE:
		return SIZE_IN_WORDS(SACI_PARAM_BLDR_APP_RESET_DEVICE_T);
		break;
	case SACI_BLDR_APP_EXIT_SACI_RUN:
		return SIZE_IN_WORDS(SACI_PARAM_COMMON_T);
		break;
	case SACI_MODE_REQ_TOOLS_CLIENT:
		return SIZE_IN_WORDS(SACI_PARAM_COMMON_T);
		break;
	default:
		return ERROR_FAIL;
		break;

	}

	return ERROR_OK;
}

/*
 * Update the first word of the command with required details
 */
static void cc_lpf3_update_cmd_word(SACI_CMD_ID_T cmd_id, SACI_PARAM_T *cmd,	uint16_t cmd_specific)
{
	cmd->common.cmd.cmd_id = cmd_id;
	cmd->common.cmd.resp_seq_num = cc_lpf3_get_resp_seqnum();
	cmd->common.cmd.cmd_specific = cmd_specific;
}

/*
 * Check RXD_FULL flag through Sec-AP interface to understand if the device
 * has data to send to the host
 */
static int cc_lpf3_wait_rx_data_ready(struct flash_bank *bank)
{
	uint64_t total_sleep = SACI_RXD_READY_CHECK_TIMEOUT;
	uint64_t check_interval = total_sleep/10;
	uint32_t value;

	cc_lpf3_read_from_AP(bank, DEBUGSS_SEC_AP, SEC_AP_RXCTL, &value);

	//check the RXD_FULL == 1
	while  (((value & SACI_RXCTL_RXD_FULL) != SACI_RXCTL_RXD_FULL) && (total_sleep > 0)) {
		total_sleep -= check_interval;
		alive_sleep(check_interval);
		cc_lpf3_read_from_AP(bank, DEBUGSS_SEC_AP, SEC_AP_RXCTL, &value);
	}

	//Timeout but rxflag is still not cleared
	if ((value & SACI_RXCTL_RXD_FULL) != SACI_RXCTL_RXD_FULL) {
		LOG_ERROR("cc_lpf3_wait_rx_data_ready: Timeout : value 0x%x", value);
		return SACI_ERROR_TXD_FULL_TO;
	}

	return ERROR_OK;
}

/*
 * Check TXD_FULL flag through Sec-AP interface to understand if the device
 * processed the previous command or can accept more data
 */
static int cc_lpf3_wait_tx_data_clear(struct flash_bank *bank)
{
	uint64_t total_sleep = SACI_TXD_FULL_CHECK_TIMEOUT;
	uint64_t check_interval = total_sleep/10;
	uint32_t value;

	cc_lpf3_read_from_AP(bank, DEBUGSS_SEC_AP, SEC_AP_TXCTL, &value);

	//check the TXD_FULL == 0
	while (((value & SACI_TXCTL_TXD_FULL) == SACI_TXCTL_TXD_FULL) && (total_sleep > 0))	{
		total_sleep -= check_interval;
		alive_sleep(check_interval);
		cc_lpf3_read_from_AP(bank, DEBUGSS_SEC_AP, SEC_AP_TXCTL, &value);
	}

	//timeout but txflag is still not cleared
	if ((value & SACI_TXCTL_TXD_FULL) == SACI_TXCTL_TXD_FULL) {
		LOG_ERROR("cc_lpf3_wait_tx_data_clear: Timeout : value 0x%x", value);
		return SACI_ERROR_TXD_FULL_TO;
	}

	return ERROR_OK;
}

/*
 * Check RXD status flag through Sec-AP interface to understand if the device
 * had data to be sent to the host and read from RXD
 */
static int cc_lpf3_saci_read_response(struct flash_bank *bank, SACI_RESP_T *cmd_resp)
{
	uint8_t resp_len = 0;
	int ret_val = cc_lpf3_wait_rx_data_ready(bank);

	if (ret_val != ERROR_OK)	{
		LOG_ERROR("Rx Ctrl Error: %d", ret_val);
		return ERROR_FAIL;
	}

	// Allocate a uint32_t* variable to read the SACI response
	uint32_t *resp = (uint32_t*)malloc(sizeof(SACI_RESP_T));

	if (resp == NULL)
	{
		return ERROR_FAIL;
	}
	// clear allocated memory
	memset (resp, 0, sizeof(SACI_RESP_T));

	// response can be read
	ret_val = cc_lpf3_read_from_AP(bank, DEBUGSS_SEC_AP, SEC_AP_RXD, resp);

	if (ret_val != ERROR_OK)
	{
		free(resp);
		return ERROR_FAIL;
	}
	memcpy(cmd_resp, resp, sizeof(SACI_RESP_T));
	free(resp);

	if ((cmd_resp->data_word_count) & 0xFF) {
		resp_len = (cmd_resp->data_word_count) & 0xFF;
		for (uint8_t resp_word_idx = 0; resp_word_idx < resp_len; resp_word_idx++) {
			if (ERROR_OK != cc_lpf3_wait_rx_data_ready(bank)) {
				LOG_ERROR("Multi RX Fail");
				return ERROR_FAIL;
			}
			cc_lpf3_read_from_AP(bank, DEBUGSS_SEC_AP, SEC_AP_RXD, &cmd_resp->status_flag);
		}
	}

	return ERROR_OK;
}

/*
 * send tx data sector by sector
 */
static int cc_lpf3_saci_send_sector_tx(struct flash_bank *bank, uint32_t *tx_data,	uint32_t length, SACI_PARAM_T *cmd)
{
	uint32_t sector_index=0;
	uint32_t num_sectors= (length + LPF3_MAIN_FLASH_SECTOR_SIZE -1)/(LPF3_MAIN_FLASH_SECTOR_SIZE);
	uint32_t base_resp_seq_number = cmd->flash_prog_main_pipelined.resp_seq_num;
	uint32_t curr_resp_seq_num, last_resp_seq_num = base_resp_seq_number - 1;
	SACI_RESP_T cmd_resp;
	int ret_val = ERROR_OK;

	LOG_INFO("Total length:%d sectors to be programmed:%d", length, num_sectors);

	for (sector_index = 0; sector_index < num_sectors; ++sector_index) {
		//send data over saci
		cc_lpf3_saci_send_tx_words(bank, (tx_data + (sector_index * MAIN_SECTOR_SIZE_WORDS)), MAIN_SECTOR_SIZE_WORDS);

		if (last_resp_seq_num < (base_resp_seq_number + sector_index -1)) {
			//Wait until SACI have finished programming the sector before reading the response
			ret_val = cc_lpf3_saci_read_response(bank, &cmd_resp);
			if (ret_val != ERROR_OK){
				LOG_INFO("ReadResp Fail: %d", ret_val);
				ret_val = ERROR_FAIL;
				break;
			}

			if(cmd_resp.cmd_id != SACI_FLASH_PROG_MAIN_PIPELINED) {
				ret_val = ERROR_FAIL;
				break;
			}

			if(cmd_resp.result != SCR_SUCCESS) {
				ret_val = ERROR_FAIL;
				break;
			}

			if(cmd_resp.data_word_count != 0) {
				LOG_INFO("cmd_resp.data_word_count - %d", cmd_resp.data_word_count);
				ret_val = ERROR_FAIL;
				break;
			}

			curr_resp_seq_num = cmd_resp.resp_seq_num;
			if (curr_resp_seq_num < (last_resp_seq_num & 0xFF))
				curr_resp_seq_num = (last_resp_seq_num & 0xFFFFFF00) + 0x100 + curr_resp_seq_num;

			if((curr_resp_seq_num != (base_resp_seq_number + sector_index))
				 && (curr_resp_seq_num != (base_resp_seq_number + sector_index - 1))) {
				LOG_INFO("Received unexpected sequence number from SACI during flash programming :index:%d curr_resp_seq_num:%d base_resp_seq_number : %d",
				sector_index, curr_resp_seq_num, base_resp_seq_number);
				ret_val = ERROR_FAIL;
				break;
			}
			last_resp_seq_num = curr_resp_seq_num;
		}
	}

	while(last_resp_seq_num != base_resp_seq_number + num_sectors - 1)
	{
		//Wait until SACI have finished programming the sector before reading the response
		cc_lpf3_saci_read_response(bank, &cmd_resp);
		if(cmd_resp.cmd_id != SACI_FLASH_PROG_MAIN_PIPELINED) {
			ret_val = ERROR_FAIL;
			break;
		}

		if(cmd_resp.result != SCR_SUCCESS) {
			ret_val = ERROR_FAIL;
			break;
		}

		if(cmd_resp.data_word_count != 0) {
			ret_val = ERROR_FAIL;
			break;
		}

		curr_resp_seq_num = cmd_resp.resp_seq_num;
		if (curr_resp_seq_num < (last_resp_seq_num & 0xFF))
			curr_resp_seq_num = (last_resp_seq_num & 0xFFFFFF00) + 0x100 + curr_resp_seq_num;

		if ((curr_resp_seq_num != (base_resp_seq_number + num_sectors - 1))
			&& (curr_resp_seq_num != (base_resp_seq_number + num_sectors - 2))) {
			LOG_INFO("Received unexpected sequence number from SACI during flash programming");
			ret_val = ERROR_FAIL;
			break;
		}

		last_resp_seq_num = curr_resp_seq_num;
	}

	return ret_val;
}

/*
 * Do blank check on the device
 */
int cc_lpf3_do_blank_check(struct flash_bank *bank)
{
	int ret_val;

	if (bank->base == LPF3_FLASH_BASE_CCFG) {
		ret_val = cc_lpf3_saci_verify_ccfg(bank, NULL);
	} else if (bank->base == LPF3_FLASH_BASE_MAIN) {
		ret_val = cc_lpf3_saci_verify_main(bank, NULL, 0);
	} else {
		LOG_ERROR("ERROR : Unknown bank for blank check");
		return ERROR_FAIL;
	}

	return ret_val;
}

/*
 * CCFG verify command
 */
int cc_lpf3_saci_verify_ccfg(struct flash_bank *bank, const uint8_t* buffer)
{
	SACI_PARAM_T cmd;
	SACI_RESP_T cmd_resp;
	int ret_val;

	memset((uint8_t*)&cmd, 0, sizeof(SACI_PARAM_T));

	cc_lpf3_update_cmd_word(SACI_FLASH_VERIFY_CCFG_SECTOR, &cmd, 0);

	if (buffer) {
		cmd.flash_verify_ccfg_sector.check_exp_crc = 1;
		cmd.flash_verify_ccfg_sector.skip_user_rec = 1;
		cmd.flash_verify_ccfg_sector.exp_boot_config_crc32 = cc_lpf3_calculate_crc(buffer, BOOT_CCFG_CRC_LEN);
		cmd.flash_verify_ccfg_sector.exp_central_crc32 = cc_lpf3_calculate_crc(buffer+CENTRAL_CCFG_START_IDX, CENTRAL_CCFG_CRC_LEN);
		cmd.flash_verify_ccfg_sector.exp_debug_cfg_crc32 = cc_lpf3_calculate_crc(buffer+DEBUG_CCFG_START_IDX, DEBUG_CCFG_CRC_LEN);
	} else {
		cmd.flash_verify_ccfg_sector.blank_check = 1;
	}

	ret_val = cc_lpf3_saci_send_cmd(bank, cmd);
	if (ret_val != ERROR_OK) {
		LOG_ERROR("VERIFY CCFG Send Fail: %d", ret_val);
		return ERROR_FAIL;
	}

	//check the cmd response
	ret_val = cc_lpf3_saci_read_response(bank, &cmd_resp);
	LOG_INFO("Verify CCFG Result: 0x%x Blank Check %d", cmd_resp.result, cmd.flash_verify_ccfg_sector.blank_check);
	if (ret_val != ERROR_OK || cmd_resp.result != SCR_SUCCESS) {
		LOG_ERROR("CMD Resp : 0x%x", ret_val);
		if (cmd_resp.result == SCR_CRC32_MISMATCH)
			LOG_ERROR("Make sure FW is built with post build script to include CRC values in CCFG section");
		return ERROR_FAIL;
	}

	return ERROR_OK;
}

/*
 * SCFG verify command
 */
int cc_lpf3_saci_verify_scfg(struct flash_bank *bank, const uint8_t* buffer, uint32_t count)
{
    SACI_PARAM_T cmd;
    SACI_RESP_T cmd_resp;
    int ret_val;

    memset((uint8_t*)&cmd, 0, sizeof(SACI_PARAM_T));

	cc_lpf3_update_cmd_word(SACI_FLASH_VERIFY_SCFG_SECTOR, &cmd, 0);

    if (buffer) {
		cmd.flash_verify_scfg_sector.check_exp_crc = 1;  // Check against expected CRC
		cmd.flash_verify_scfg_sector.expected_crc32 = cc_lpf3_calculate_crc(buffer, SCFG_BYTE_COUNT);
    }

    ret_val = cc_lpf3_saci_send_cmd(bank, cmd);
    if (ret_val != ERROR_OK) {
        LOG_ERROR("SCFG Verify Send Cmd Fail");
        return ERROR_FAIL;
    }

    ret_val = cc_lpf3_saci_read_response(bank, &cmd_resp);
	LOG_INFO("Verify SCFG Result: 0x%x", cmd_resp.result);
    if (ret_val != ERROR_OK) {
        LOG_ERROR("SCFG Verify Read Response Fail");
        return ERROR_FAIL;
    }

    if (cmd_resp.result != SCR_SUCCESS) {
        LOG_ERROR("SCFG Verify Failed with result: 0x%x", cmd_resp.result);
        return ERROR_FAIL;
    }

    return ERROR_OK;
}

/*
 * Main Flash bank verify command
 */
int cc_lpf3_saci_verify_main(struct flash_bank *bank, const uint8_t* buffer, uint32_t count)
{
	SACI_PARAM_T cmd;
	SACI_RESP_T cmd_resp;
	int ret_val;

	memset((uint8_t*)&cmd, 0, sizeof(SACI_PARAM_T));
	cc_lpf3_update_cmd_word(SACI_FLASH_VERIFY_MAIN_SECTORS, &cmd, 0);
	cmd.flash_verify_main_sectors.first_sector_addr = (uint32_t)bank->base;

	// if data is there it should be sector aligned, otherwise just do blank check
	if (buffer && (count%LPF3_MAIN_FLASH_SECTOR_SIZE == 0)) {
		cmd.flash_verify_main_sectors.byte_count = count;
		/*calculate crc32 using cc_lpf3_calculate_crc function*/
		cmd.flash_verify_main_sectors.expected_crc32 = cc_lpf3_calculate_crc(buffer, count);
	} else {
		cmd.flash_verify_main_sectors.byte_count = bank->size;
		cmd.flash_verify_main_sectors.blank_check = 1;
	}

	cc_lpf3_saci_send_cmd(bank, cmd);

	//check the cmd response
	ret_val = cc_lpf3_saci_read_response(bank, &cmd_resp);
	LOG_INFO("Verify Main Result: 0x%x Blank Check: %d", cmd_resp.result, cmd.flash_verify_main_sectors.blank_check);
	if (ret_val != ERROR_OK || cmd_resp.result != SCR_SUCCESS) {
		LOG_ERROR("CMD Resp : 0x%x", ret_val);
		return ERROR_FAIL;
	}

	return ERROR_OK;
}

/*
 * Erase command - ccfg and main
 */
int cc_lpf3_saci_erase(struct flash_bank *bank)
{
	SACI_PARAM_T cmd;
	SACI_RESP_T cmd_resp;
	int ret_val;

	memset((uint8_t*)&cmd, 0, sizeof(SACI_PARAM_T));
	cc_lpf3_update_cmd_word(SACI_FLASH_ERASE_CHIP, &cmd, 0);
	cmd.flash_erase_chip.key = FLASH_KEY;
	ret_val = cc_lpf3_saci_send_cmd(bank, cmd);
	if (ret_val != ERROR_OK) {
		LOG_ERROR("Erase Command Failure");
		return ERROR_FAIL;
	}
	//check the cmd response
	ret_val = cc_lpf3_saci_read_response(bank, &cmd_resp);
	if (ret_val != ERROR_OK) {
		LOG_ERROR("ReadResp Fail for erase: %d", ret_val);
		return ERROR_FAIL;
	}

	return ERROR_OK;
}

/*
 * Send data words
 */
int cc_lpf3_saci_send_tx_words(struct flash_bank *bank, uint32_t *tx_data, uint32_t length)
{
	int ret_val = 0;

	//Set TXD (0x200) with command
	ret_val = cc_lpf3_bulk_write_to_AP(bank, DEBUGSS_SEC_AP, SEC_AP_TXD, tx_data, length);
	if (ret_val != ERROR_OK) {
		LOG_ERROR("Tx Write returned with error resp: %d", ret_val);
		return ERROR_FAIL;
	}

	return ERROR_OK;
}

/*
 * Program CCFG
 */
int cc_lpf3_write_ccfg(struct flash_bank *bank, const uint8_t *buffer,
			   uint32_t offset, uint32_t count)
{
	SACI_PARAM_T cmd;
	SACI_RESP_T cmd_resp;
	uint32_t *tx_words = NULL;
	int ret_val;

	//make sure the buffer is sector aligned
	if (buffer)
	{
		tx_words = malloc(MAX_CCFG_SIZE_IN_BYTES);
		if (!tx_words) {
			LOG_ERROR("Memory Allocation Fail");
			return ERROR_FAIL;
		}
		memset(tx_words, 0xFF, MAX_CCFG_SIZE);
		memcpy(tx_words, buffer, count);
	} else {
		return ERROR_FAIL;
	}

	memset((uint8_t*)&cmd, 0, sizeof(SACI_PARAM_T));

	cmd.flash_prog_ccfg_sector.cmd_id = SACI_FLASH_PROG_CCFG_SECTOR;
	cmd.flash_prog_ccfg_sector.key = FLASH_KEY;
	cmd.flash_prog_ccfg_sector.skip_user_rec = 1;

	ret_val = cc_lpf3_saci_send_cmd(bank, cmd);
	if (ret_val != ERROR_OK) {
		LOG_ERROR("CCFG Cmd Fail");
		ret_val = ERROR_FAIL;
		goto FREE_AND_RETURN;
	}

	ret_val = cc_lpf3_saci_send_tx_words(bank, tx_words, MAX_CCFG_SIZE);
	if (ret_val != ERROR_OK) {
		LOG_ERROR("CCFG Write Fail");
		ret_val = ERROR_FAIL;
		goto FREE_AND_RETURN;
	}

	ret_val = cc_lpf3_saci_read_response(bank, &cmd_resp);
	if (ret_val != ERROR_OK) {
		LOG_ERROR("CCFG Resp Fail");
		ret_val = ERROR_FAIL;
		goto FREE_AND_RETURN;
	}

	if(cmd_resp.result != 0){
		LOG_ERROR("CCFG Write Fail.");
		ret_val = ERROR_FAIL;
		goto FREE_AND_RETURN;
	}

	ret_val = cc_lpf3_saci_verify_ccfg(bank, buffer);
	if (ret_val != ERROR_OK) {
		LOG_ERROR("CCFG Verify Fail");
		ret_val = ERROR_FAIL;
		goto FREE_AND_RETURN;
	}

FREE_AND_RETURN:
	if (tx_words)
		free(tx_words);

	return ret_val;
}

/*
 * Program SCFG
 */
int cc_lpf3_write_scfg(struct flash_bank *bank, const uint8_t *buffer,
               uint32_t offset, uint32_t count)
{
    SACI_PARAM_T cmd;
    SACI_RESP_T cmd_resp;
    uint32_t *tx_words = NULL;
    int ret_val;


    // Make sure the buffer is sector aligned
    if (buffer)
    {
        tx_words = malloc(count);
        if (!tx_words) {
            LOG_ERROR("Memory Allocation Fail for SCFG");
            return ERROR_FAIL;
        }
        memset(tx_words, 0xFF, count);
        memcpy(tx_words, buffer, count);
    } else {
        return ERROR_FAIL;
    }

    memset((uint8_t*)&cmd, 0, sizeof(SACI_PARAM_T));

    cmd.common.cmd.cmd_id = SACI_FLASH_PROG_SCFG_SECTOR;
    cmd.common.cmd.resp_seq_num = cc_lpf3_get_resp_seqnum();
    cmd.common.cmd.cmd_specific = 0;  // No specific flags for SCFG
    cmd.flash_prog_scfg_sector.key = FLASH_KEY;
	cmd.flash_prog_scfg_sector.byte_count = count;

    ret_val = cc_lpf3_saci_send_cmd(bank, cmd);
    if (ret_val != ERROR_OK) {
        LOG_ERROR("SCFG Cmd Fail");
        ret_val = ERROR_FAIL;
        goto FREE_AND_RETURN;
    }

	// Here count means byte count of the buffer that is going to be written into the flash.
	uint32_t tx_words_count = count/4;

    // Send only the SCFG data
    ret_val = cc_lpf3_saci_send_tx_words(bank, tx_words, tx_words_count);
    if (ret_val != ERROR_OK) {
        LOG_ERROR("SCFG Write Fail");
        ret_val = ERROR_FAIL;
        goto FREE_AND_RETURN;
    }

    ret_val = cc_lpf3_saci_read_response(bank, &cmd_resp);
	if (ret_val != ERROR_OK) {
		LOG_ERROR("SCFG Resp Fail");
		ret_val = ERROR_FAIL;
		goto FREE_AND_RETURN;
	}
	if(cmd_resp.result != 0){
		LOG_ERROR("SCFG write fail");
		ret_val = ERROR_FAIL;
        goto FREE_AND_RETURN;
	}

    ret_val = cc_lpf3_saci_verify_scfg(bank, buffer, count);
    if (ret_val != ERROR_OK) {
        LOG_ERROR("SCFG Verify Fail");
        ret_val = ERROR_FAIL;
        goto FREE_AND_RETURN;
    }

FREE_AND_RETURN:
	if (tx_words)
		free(tx_words);

	return ret_val;
}

/*
 * Program Main Flash
 */
int cc_lpf3_write_main(struct flash_bank *bank, const uint8_t *buffer,
			   uint32_t offset, uint32_t count)
{
	SACI_PARAM_T cmd;
	int ret_val;

	memset((uint8_t*)&cmd, 0, sizeof(SACI_PARAM_T));
	cc_lpf3_update_cmd_word(SACI_FLASH_PROG_MAIN_PIPELINED, &cmd, 0);
	cmd.flash_prog_main_pipelined.key = FLASH_KEY;
	cmd.flash_prog_main_pipelined.first_sector_addr = (uint32_t)bank->base;

	/*Program Main through pipeline Command*/
	ret_val = cc_lpf3_saci_send_cmd(bank, cmd);
	if (ret_val != ERROR_OK) {
		LOG_ERROR("Main Flash cmd failed");
		return ERROR_FAIL;
	}

	uint32_t *tx_words = cc_lpf3_flash_sector_padding(buffer, (uint32_t*)&count);

	if (tx_words) {
		ret_val = cc_lpf3_saci_send_sector_tx(bank, tx_words, count, &cmd);
	} else {
		LOG_ERROR("Memory Allocation Fail");
		ret_val = ERROR_FAIL;
	}

	if (ret_val != ERROR_OK)
		LOG_ERROR("Flash Sector programming failure");
	else {
		uint8_t	 *tx_bytes = (uint8_t*)tx_words;
		ret_val = cc_lpf3_saci_verify_main(bank, tx_bytes, count);
	}

	if (ret_val != ERROR_OK)
		LOG_ERROR("Verify Main failure");

	if (tx_words)
		free(tx_words);
	return ret_val;
}

/*
 * Common function to send SACI command
 */
int cc_lpf3_saci_send_cmd(struct flash_bank *bank, SACI_PARAM_T tx_cmd)
{
	uint16_t cmd_length = cc_lpf3_get_cmd_word_length(tx_cmd);
	//Read TXCTL
	int ret_val = cc_lpf3_wait_tx_data_clear(bank);

	if (ret_val != ERROR_OK) {
		LOG_ERROR("saci_send_cmd: TxCtrl  %d", ret_val);
		return ERROR_FAIL;
	}

	//Set bit 1 of TXCTL (0x204): CMD_START
	//Indicates that TXD contains the first word of a command
	ret_val = cc_lpf3_write_to_AP(bank, DEBUGSS_SEC_AP, SEC_AP_TXCTL, SACI_TXCTRL_CMD_START);
	if (ret_val != ERROR_OK)
		LOG_ERROR("saci_send_cmd: cmd Start Fail: %d", ret_val);

	//Set TXD (0x200) with command
	ret_val = cc_lpf3_write_to_AP(bank, DEBUGSS_SEC_AP, SEC_AP_TXD, tx_cmd.common.val);
	if (ret_val != ERROR_OK)
		LOG_ERROR("saci_send_cmd:cmd_id-%d Write Failed : %d", tx_cmd.common.cmd.cmd_id, ret_val);

	if (cmd_length > (sizeof(SACI_PARAM_COMMON_T)/sizeof(uint32_t))) {
		ret_val = cc_lpf3_wait_tx_data_clear(bank);
		if (ret_val != ERROR_OK) {
			LOG_ERROR("saci_send_cmd : Cmd Clear Fail: %d", ret_val);
			return ERROR_FAIL;
		}

		//Clear bit 1 of TXCTL (0x204): CMD_START
		//Indicates that TXD contains the first word of a command
		ret_val = cc_lpf3_write_to_AP(bank, DEBUGSS_SEC_AP, SEC_AP_TXCTL, 0);//~SACI_TXCTRL_CMD_START);
		if (ret_val != ERROR_OK) {
			LOG_ERROR("write_multi_param : Cmd Start Clear Fail: %d", ret_val);
			return ERROR_FAIL;
		}

		uint32_t *param_words = (uint32_t*)malloc(sizeof(SACI_PARAM_T));
		if (param_words == NULL)
		{
			LOG_ERROR("Param words memory allocation failure");
			return ERROR_FAIL;
		}
		// clear allocated memory
		memset(param_words, 0, sizeof(SACI_PARAM_T));
		memcpy(param_words, &tx_cmd, sizeof(SACI_PARAM_T));
		for (uint8_t cmd_word = 1; cmd_word<cmd_length; cmd_word++) {
			//Set TXD (0x200) with command
			ret_val = cc_lpf3_write_to_AP(bank, DEBUGSS_SEC_AP, SEC_AP_TXD, param_words[cmd_word]);
			if (ret_val != ERROR_OK) {
				LOG_ERROR("saci_send_cmd:cmd_id-%d Write Failed : %d", tx_cmd.common.cmd.cmd_id, ret_val);
				free(param_words);
				return ERROR_FAIL;
			}
		}
		free(param_words);
	}

	//Read TXCTL
	ret_val = cc_lpf3_wait_tx_data_clear(bank);
	if(ret_val != ERROR_OK) {
		LOG_ERROR("Tx Ctrl Error: %d", ret_val);
		return ERROR_FAIL;
	}

	return ERROR_OK;
}

/*
 * Check the boot status of the CCLPF3 device
 */
int cc_lpf3_check_boot_status(struct flash_bank *bank)
{
	uint32_t result;
	uint8_t bootsta;
	int ret_val;

	//Connect and Read from CFG AP
	/************************************************
	** BOOTSTA[6] ** BOOTSTA[7] ****** mode *********
	**	 0				0			In boot code	*
	**	 0				1			In boot loader	*
	**	 1				1			In application	*
	*************************************************/
	ret_val = cc_lpf3_read_from_AP(bank, DEBUGSS_CFG_AP, CFG_AP_DEVICE_STATUS, &result);
	if (ret_val != ERROR_OK)
		LOG_INFO("Read Error in BootStatus");

	//CFG-AP: DEVICESTATUS:BOOTSTA (bit 15:8 in the DEVICESTATUS register in CFG-AP)
	bootsta = (uint8_t)((result>>8) & 0xFF);
	LOG_INFO("DEVICESTATUS:	bootsta - 0x%x lifecycle - 0x%x swdsel - 0x%x msb 16bit - 0x%x",
	(result >> 8) & 0xFF,
	(result) & 0xFF,
	(result >> 16) & 0x1,
	(result >> 16) & 0xFFFF );

	return bootsta;
}

/*
 * Issue Exit Halt SACI command on the CCLPF3 device
 */
int cc_lpf3_exit_saci_run(struct flash_bank *bank)
{
	SACI_PARAM_T cmd;
	int ret_val;

	memset((uint8_t*)&cmd, 0, sizeof(SACI_PARAM_T));

	LOG_INFO("Exit SACI and Run");

	cc_lpf3_update_cmd_word(SACI_BLDR_APP_EXIT_SACI_RUN, &cmd, 0);
	ret_val = cc_lpf3_saci_send_cmd(bank, cmd);
	if (ret_val != ERROR_OK)
		return ret_val;

	cc_lpf3_check_boot_status(bank);

	return ERROR_OK;
}


/*
 * Issue Exit Halt SACI command on the CCLPF3 device
 */
int cc_lpf3_exit_saci_halt(struct flash_bank *bank)
{
	SACI_PARAM_T cmd;
	int ret_val;
	uint64_t exit_halt_timeout = SACI_EXIT_SACI_HALT_TIMEOUT;
	uint64_t check_interval = exit_halt_timeout/10;

	memset((uint8_t*)&cmd, 0, sizeof(SACI_PARAM_T));

	LOG_INFO("Exit SACI and Halt");

	cc_lpf3_update_cmd_word(SACI_DEBUG_EXIT_SACI_HALT, &cmd, 0);
	ret_val = cc_lpf3_saci_send_cmd(bank, cmd);

	//check the bootsta
	while (!(ret_val == BOOTSTA_APP_WAITLOOP_DBGPROBE ||
				ret_val == BOOTSTA_BLDR_WAITLOOP_DBGPROBE) && (exit_halt_timeout > 0)) {
		exit_halt_timeout -= check_interval;
		alive_sleep(check_interval);
		ret_val =  cc_lpf3_check_boot_status(bank);
	}

	//Timeout but rxflag is still not cleared
	if (!(ret_val == BOOTSTA_APP_WAITLOOP_DBGPROBE ||
			 ret_val == BOOTSTA_BLDR_WAITLOOP_DBGPROBE)) {
		LOG_ERROR("Exit SACI Halt Timeout without entering Debug Probe loop - bootsta:%x", ret_val);
		return SACI_EXIT_HALT_TO;
	}

	return ERROR_OK;
}
