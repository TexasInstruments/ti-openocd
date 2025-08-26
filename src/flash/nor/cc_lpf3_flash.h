//SPDX-License-Identifier: GPL-2.0-or-later

/***************************************************************************
 * Copyright (C) 2025 Texas Instruments Incorporated - https://www.ti.com/
 *
 * NOR flash driver for CC2340R5 from Texas Instruments.
 ***************************************************************************/

#define SIZE_IN_WORDS(x)				(sizeof(x)/sizeof(uint32_t))

#define DEBUGSS_AHB_AP 					0x00
#define DEBUGSS_CFG_AP 					0x01
#define DEBUGSS_SEC_AP 					0x02

#define CFG_AP_DEVICE_ID_READ 			0x00
#define CFG_AP_PART_ID_READ 			0x04
#define CFG_AP_DEVICE_STATUS 			0x0C

#define SEC_AP_TXD	 					0x00
#define SEC_AP_TXCTL 					0x04

//SACI Tx Flags
#define SACI_TXCTL_TXD_FULL	 			(1<<0)
#define SACI_TXCTRL_CMD_START 			(1<<1)
#define SACI_TXCTL_TXD_CLEAR	 		(0)

#define SEC_AP_RXD	 					0x08
#define SEC_AP_RXCTL 					0x0C

//SACI Rx Flags
#define SACI_RXCTL_RXD_FULL 			(1<<0)
#define SACI_RXCTL_CMD_ABORT 			(1<<1)
#define SACI_RXCTL_CMD_WORKING 			(1<<2)
#define SACI_RXCTL_CMD_ERROR 			(1<<3)

#define SACI_TXD_FULL_CHECK_TIMEOUT		(1000)
#define SACI_RXD_READY_CHECK_TIMEOUT	(3000)
#define SACI_EXIT_SACI_HALT_TIMEOUT		(3000)

//*****************************************************************************
//
//Boot status definitions (available through PMCTL::BOOTSTA or CFGAP::DEVICESTATUS bits 15:8)
//
//*****************************************************************************
#define BOOTSTA_MODE_M							0xC0
#define BOOTSTA_MODE_BOOT						0x00
#define BOOTSTA_MODE_BLDR						0x80
#define BOOTSTA_MODE_APP						0xC0

//Boot state reset value
#define BOOTSTA_BOOT_RESET						(BOOTSTA_MODE_BOOT)
//Starting normal cold boot
#define BOOTSTA_BOOT_COLD_BOOT				  	(BOOTSTA_MODE_BOOT | 0x01)
//SRAM repair sequence completed
#define BOOTSTA_BOOT_SRAM_REP_DONE			 	(BOOTSTA_MODE_BOOT | 0x02)
//Boot code has started applying general trims
#define BOOTSTA_BOOT_GENERAL_TRIMS			 	(BOOTSTA_MODE_BOOT | 0x03)
//Halt-in-boot into SACI indication
#define BOOTSTA_BOOT_ENTERED_SACI			  	(BOOTSTA_MODE_BOOT | 0x20)
//Waiting for SWD disconnection before device reset
#define BOOTSTA_BOOT_WAIT_SWD_DISCONNECT	 	(BOOTSTA_MODE_BOOT | 0x36)
//Never entered SACI, SACI timed out, or exit from SACI was requested
#define BOOTSTA_BOOT_EXITED_SACI				(BOOTSTA_MODE_BOOT | 0x37)
//Waiting for debug-probe (flashless modes)
#define BOOTSTA_BOOT_WAITLOOP_DBGPROBE			(BOOTSTA_MODE_BOOT | 0x38)
//SRAM repair failed
#define BOOTSTA_BOOT_FAIL_SRAM_REPAIR		 	(BOOTSTA_MODE_BOOT | 0x3E)
//Fault handler called during boot (before serial bootloader entered)
#define BOOTSTA_BOOT_FAULT_HANDLER			 	(BOOTSTA_MODE_BOOT | 0x3F)

//Boot sequence completed
#define BOOTSTA_BOOT_COMPLETE					(BOOTSTA_MODE_BLDR)
//Waiting for debug-probe to connect
#define BOOTSTA_BLDR_WAITLOOP_DBGPROBE			(BOOTSTA_MODE_BLDR | 0x01)
//Bootloader has started
#define BOOTSTA_BLDR_STARTED					(BOOTSTA_MODE_BLDR | 0x3A)
//Bootloader is idle, waiting for a CMD
#define BOOTSTA_BLDR_CMD_IDLE					(BOOTSTA_MODE_BLDR | 0x3B)
//Bootloader has begun processing a CMD
#define BOOTSTA_BLDR_CMD_PROCESSING				(BOOTSTA_MODE_BLDR | 0x3C)
//Bootloader was not started from device boot context
#define BOOTSTA_BLDR_FAIL_EXECUTION_CONTEXT 	(BOOTSTA_MODE_BLDR | 0x3D)
//Boot ran past transferring control to application (should never happen)
#define BOOTSTA_BLDR_FAIL_APPTRANSFER			(BOOTSTA_MODE_BLDR | 0x3E)
//Fault handler called during serial bootloader execution
#define BOOTSTA_BLDR_FAULT_HANDLER				(BOOTSTA_MODE_BLDR | 0x3F)

//ROM serial bootloader complete
#define BOOTSTA_BLDR_COMPLETE					(BOOTSTA_MODE_APP)
//Waiting for debug-probe to connect
#define BOOTSTA_APP_WAITLOOP_DBGPROBE		 	(BOOTSTA_MODE_APP | 0x01)
//No application entry-point defined in CCFG (should never happen)
#define BOOTSTA_APP_FAIL_NOAPP				  	(BOOTSTA_MODE_APP | 0x3D)
//Serial bootloader ran past transferring control to application (should never happen)
#define BOOTSTA_APP_FAIL_APPTRANSFER		  	(BOOTSTA_MODE_APP | 0x3E)
//Fault handler called after ROM serial bootloader completed
#define BOOTSTA_APP_FAULT_HANDLER			  	(BOOTSTA_MODE_APP | 0x3F)

//ERROR in SACI
#define SACI_ERROR_TXD_FULL_TO 			(-1)
#define SACI_EXIT_HALT_TO 				(-2)

//Magic key used by Flash Commands
#define FLASH_KEY						  (0xB7E3A08F)

#define SACI_CMD_SPECIFIC_BIT_START		((uint16_t)(1<<0))

#define CMD_CHIP_ERASE_RETAIN_SECTORS	(SACI_CMD_SPECIFIC_BIT_START)
#define CMD_CHIP_DEBUG_AUTH				(SACI_CMD_SPECIFIC_BIT_START)
#define CMD_PROG_CCFG_SKIP_USER_REC		(SACI_CMD_SPECIFIC_BIT_START)
#define CMD_PROG_MAIN_BYTE_COUNT		(SACI_CMD_SPECIFIC_BIT_START)
#define CMD_VERIFY_CCFG_CHECK_EXP_CRC	(SACI_CMD_SPECIFIC_BIT_START)
#define CMD_VERIFY_CCFG_SKIP_USR_REC	((uint16_t)(1 << 1))
#define CMD_VERIFY_CCFG_DO_BLANK_CHECK	((uint16_t)(1 << 15))
#define CMD_VERIFY_MAIN_BYTE_COUNT		((uint16_t)(1 << 15))
#define CMD_BLDR_RESET_W4_SWD_DISCON 	(SACI_CMD_SPECIFIC_BIT_START)

#define SACI_ERASE_CHIP_RETAIN_WORD_CNT (3)	//Keeping the max value of chip erase retain words. To be checked for each devices
#define SACI_GET_TEST_ID_WORD_CNT		(4)	//Keeping the max value of word count. To be checked for each devices

/// Size of one MAIN flash sector, in number of bytes
#define LPF3_MAIN_FLASH_SECTOR_SIZE 	(0x800U) //2Kb
#define LPF3_SCFG_FLASH_SECTOR_SIZE 	(0x400U) //1Kb

#define LPF3_FLASH_BASE_CCFG			(0x4E020000)
#define LPF3_FLASH_BASE_SCFG			(0x4E040000)
#define LPF3_FLASH_BASE_MAIN			(0x0)

/// Size of one MAIN flash sector, in number of bytes
#define MAIN_SECTOR_SIZE_WORDS			(512)
/// Size of one SCFG flash sector, in number of bytes
#define SCFG_SECTOR_SIZE_WORDS			(256)

/// The maximum CCFG size of all devices that uses SACI.
#define MAX_CCFG_SIZE				MAIN_SECTOR_SIZE_WORDS
#define MAX_SCFG_SIZE				SCFG_SECTOR_SIZE_WORDS
#define MAX_CCFG_SIZE_IN_BYTES			(MAX_CCFG_SIZE * 4)
#define MAX_SCFG_SIZE_IN_BYTES			(MAX_SCFG_SIZE * 4)

#define BOOT_CCFG_START_IDX				(0x0)
#define CENTRAL_CCFG_START_IDX			(0x10)
#define DEBUG_CCFG_START_IDX			(0x7D0)

#define BOOT_CCFG_CRC_LEN				(0x0C)
#define CENTRAL_CCFG_CRC_LEN			(0x73C)
#define DEBUG_CCFG_CRC_LEN				(0x2C)

#define SCFG_BYTE_COUNT					(0xE4)
#define SCFG_DATA_WORDS					(SCFG_BYTE_COUNT/4)

/// The maximum user record size of all devices that uses SACI.
#define MAX_CCFG_USER_RECORD_SIZE		(128)
#define MAX_CCFG_USER_RECORD_SIZE_WORDS (32)

//SACI command result
typedef enum SACI_CMD_RESULT{
	SCR_SUCCESS							= 0x00, //Command executed successfully
	SCR_INVALID_CMD_ID				  	= 0x80, //Invalid command ID
	SCR_INVALID_ADDRESS_PARAM		 	= 0x81, //Invalid address parameter
	SCR_INVALID_SIZE_PARAM			 	= 0x82, //Invalid size parameter
	SCR_INVALID_KEY_PARAM			  	= 0x83, //Invalid key parameter
	SCR_FLASH_FSM_ERROR				 	= 0x84, //Flash hardware FSM error
	SCR_PARAM_BUFFER_OVERFLOW		 	= 0x85, //Parameter data buffer overflow (host must slow down)
	SCR_NOT_ALLOWED					  	= 0x86, //Command is not allowed due to restrictions
	SCR_CRC32_MISMATCH				  	= 0x87, //Calculated CRC32 does not match expected CRC32
	SCR_INVALID_PWD_PARAM			  	= 0x88, //Invalid password parameter
	SCR_BLANK_CHECK_FAILED			 	= 0x89, //Blank check detected one or more flash bits that were zero
	SCR_INVALID_DBG_AUTH_LVL_PARAM  	= 0x8A, //Invalid auth level parameter
	SCR_INVALID_DBG_AUTH_CONFIG	  		= 0x8B, //Invalid auth configuration
	SCR_CHALLENGE_RSP_VERIFY_FAIL		= 0x8C, //Challenge response verification failed
	SCR_KEY_HASH_MISMATCH			  	= 0x8D, //Calculated key hash does not match provided the expected key hash
	SCR_HSM_BOOT_FAILED				 	= 0x8E, //HSM failed to boot
	SCR_HSM_FW_HDR_INVALID			 	= 0x8F, //HSM FW update failed due to invalid HDR contents
	SCR_HSM_FW_VER_INVALID			 	= 0x90, //HSM FW update failed due to invalid version number (anti-rollback)
	SCR_HSM_FW_CRYPTO_FAIL			 	= 0x91, //HSM FW update failed during either signature verification or decryption
	SCR_CMD_FAILED						= 0xFF, //Unspecified command failure
} SACI_CMD_RESULT_T;

typedef enum SACI_CMD_ID{
	SACI_MISC_NO_OPERATION				= 0x01, //Miscellaneous: No operation
	SACI_MISC_GET_TEST_ID				= 0x02, //Miscellaneous: Get test ID
	SACI_MISC_GET_DIE_ID				= 0x03, //Miscellaneous: Get die ID
	SACI_MISC_GET_CCFG_USER_REC			= 0x04, //Miscellaneous: Get non-read protected part of user record in CCFG
	SACI_DEBUG_REQ_PWD_ID				= 0x05, //Debug: Request password ID for debug authentication
	SACI_DEBUG_SUBMIT_AUTH				= 0x06, //Debug: Submit debug authentication (password)
	SACI_DEBUG_EXIT_SACI_HALT			= 0x07, //Debug: Exit SACI, and halt at bootloader/application entry
	SACI_DEBUG_EXIT_SACI_SHUTDOWN		= 0x08, //Debug: Exit SACI, and enter shutdown mode
	SACI_FLASH_ERASE_CHIP				= 0x09, //Flash programming: Erase CCFG and all MAIN sectors (key)
	SACI_FLASH_PROG_CCFG_SECTOR			= 0x0C, //Flash programming: Program CCFG sector (option to skip user record) (key)
	SACI_FLASH_PROG_CCFG_USER_REC		= 0x0D, //Flash programming: Program user record in CCFG sector (key)
	SACI_FLASH_PROG_MAIN_SECTOR			= 0x0E, //Flash programming: Program all or a part of one MAIN sector (key)
	SACI_FLASH_PROG_MAIN_PIPELINED		= 0x0F, //Flash programming: Program one or more whole MAIN sectors (key)
	SACI_FLASH_VERIFY_MAIN_SECTORS		= 0x10, //Flash programming: Verify a range of MAIN sectors
	SACI_FLASH_VERIFY_CCFG_SECTOR		= 0x11, //Flash programming: Verify CCFG sector
	SACI_LIFECYCLE_INCR_STATE			= 0x12, //Device lifecycle: Increment state (including RTF) (password)
	SACI_LIFECYCLE_REQ_FIRST_BDAY		= 0x13, //Device lifecycle: Request first birthday lifecycle (password)
	SACI_BLDR_APP_RESET_DEVICE			= 0x14, //Bootloader/application: Reset the device
	SACI_BLDR_APP_EXIT_SACI_RUN			= 0x15, //Bootloader/application: Exit SACI, and run bootloader/application
	SACI_MODE_REQ_FLASHLESS_TEST		= 0x16, //Device mode: Request flashless test mode (password)
	SACI_MODE_REQ_TOOLS_CLIENT			= 0x17, //Device mode: Request flashless tools client mode
	SACI_FLASH_VERIFY_FCFG_SECTOR		= 0x18, //Flash programming: Verify FCFG sector
	SACI_FLASH_PROG_SCFG_SECTOR			= 0x1A, //Program the entire SCFG sector with option to leave the Scfg.keyRingCfg region unprogrammed
	SACI_FLASH_VERIFY_SCFG_SECTOR		= 0x1B  //Verify the contents of records within the SCFG sector against supplied CRC32 values.
}SACI_CMD_ID_T;

#pragma pack(push, 1)
//SACI first parameter word common formatting
typedef struct {
	uint8_t			cmd_id;				//Command ID
	uint8_t			resp_seq_num;		//Optional sequence number, included in the response header
	uint16_t		cmd_specific;		//Command specific
} SACI_PARAM_COMMON;

typedef union{
	SACI_PARAM_COMMON 	cmd;
	uint32_t 			val;
} SACI_PARAM_COMMON_T;

//SC_DEBUG_SUBMIT_AUTH command parameters
typedef struct {
	uint8_t				cmd_id;				//Command ID
	uint8_t				resp_seq_num;		//Optional sequence number, included in the response header
	uint16_t		  	word_count;			//Length of password in 32-bit words (min 3, max 16)
	uint32_t		  	pwd[16];			//Password
} SACI_PARAM_DEBUG_SUBMIT_AUTH_T;

//SC_FLASH_ERASE_CHIP command parameters
typedef struct {
	uint8_t    			cmd_id;						//Command ID
	uint8_t				resp_seq_num;				//Optional sequence number, included in the response header
	uint16_t		  	retain_main_sectors : 1;	//If set, retain sectors as defined by CCfg.flashProt.chipEraseRetain
	uint16_t		  	reserved0 : 15;				//Reserved
	uint32_t		  	key;						//Key used to avoid accidental flash operation (\see FLASH_API_KEY)
} SACI_PARAM_FLASH_ERASE_CHIP_T;

//SC_FLASH_PROG_CCFG_SECTOR command parameters
typedef struct {
	uint8_t    			cmd_id;					 //Command ID
	uint8_t				resp_seq_num;			 //Optional sequence number, included in the response header
	uint16_t		  	skip_user_rec : 1;		 //Skip user record part
	uint16_t		  	reserved0 : 15;			 //Reserved
	uint32_t		  	key;					 //Key used to avoid accidental flash operation (\see FLASH_API_KEY)
} SACI_PARAM_FLASH_PROG_CCFG_SECTOR_T;

//SC_FLASH_PROG_SCFG_SECTOR command parameters
typedef struct {
	uint8_t    			cmd_id;					 //Command ID
	uint8_t				resp_seq_num;			 //Optional sequence number, included in the response header
	uint16_t		  	byte_count;				 //Number of bytes to program (pData must be padded to N x 32-bit)
	uint32_t		  	key;					 //Key used to avoid accidental flash operation (\see FLASH_API_KEY)
} SACI_PARAM_FLASH_PROG_SCFG_SECTOR_T;

//SC_FLASH_PROG_CCFG_USER_REC command parameters
typedef struct {
	uint8_t    			cmd_id;				//Command ID
	uint8_t				resp_seq_num;		//Optional sequence number, included in the response header
	uint16_t		  	reserved0;			//Reserved
	uint32_t		  	key;				//Key used to avoid accidental flash operation (\see FLASH_API_KEY)
	uint8_t				data[MAX_CCFG_USER_RECORD_SIZE]; //Data to be programmed
} SACI_PARAM_FLASH_PROG_CCFG_USER_REC_T;

//SC_FLASH_PROG_MAIN_SECTOR command parameters
typedef struct {
	uint8_t    			cmd_id;				//Command ID
	uint8_t				resp_seq_num;		//Optional sequence number, included in the response header
	uint16_t		  	byte_count;			//Number of bytes to program (pData must be padded to N x 32-bit)
	uint32_t		  	key;				//Key used to avoid accidental flash operation (\see FLASH_API_KEY)
	uint32_t		  	first_byte_addr;	//Address of the first byte to be programmed
} SACI_PARAM_FLASH_PROG_MAIN_SECTOR_T;

//SC_FLASH_PROG_MAIN_PIPELINED command parameters (flash sector data stored separately)
typedef struct {
	uint8_t    			cmd_id;				//Command ID
	uint8_t				resp_seq_num;		//Optional sequence number, included in the response header
	uint16_t		  	reserved0;			//Reserved
	uint32_t		  	key;				//Key used to avoid accidental flash operation (\see FLASH_API_KEY)
	uint32_t		  	first_sector_addr;	//Address of the first sector to be programmed
} SACI_PARAM_FLASH_PROG_MAIN_PIPELINED_T;

//SC_FLASH_VERIFY_MAIN_SECTORS command parameters
typedef struct {
	uint8_t    			cmd_id;				//Command ID
	uint8_t				resp_seq_num;		//Optional sequence number, included in the response header
	uint16_t		  	reserved0 : 15;	  	//Reserved
	uint16_t		  	blank_check : 1;	//Check if range is blank (all ones) instead of CRC checks
	uint32_t		  	first_sector_addr;	//Address of the first sector to be verified
	uint32_t		  	byte_count;			//Number of bytes to verify, whole # of sectors (-4 bytes)
	uint32_t		  	expected_crc32;		//Expected CRC32
} SACI_PARAM_FLASH_VERIFY_MAIN_SECTORS_T;

//SC_FLASH_VERIFY_CCFG_SECTOR command parameters
typedef struct {
	uint8_t    			cmd_id;				//Command ID
	uint8_t				resp_seq_num;		//Optional sequence number, included in the response header
	uint16_t		  	check_exp_crc : 1;	//0: Validity check of embedded CRCs only; 1: also check CRCs against reference values
	uint16_t		  	skip_user_rec : 1;	//Skip CRC check of the user record part of CCFG
	uint16_t		  	reserved1 : 13;	  	//Reserved
	uint16_t		  	blank_check : 1;	//Check if entire CCFG sector is blank (all ones) instead of CRC checks
	uint32_t		  	exp_boot_config_crc32;	//Expected CRC32 of boot configuration part of CCFG, used if check_exp_crc = 1
	uint32_t		  	exp_central_crc32;	 	//Expected CRC32 of central part of CCFG, used if check_exp_crc = 1
	uint32_t		  	exp_userrec_crc32;	 	//Expected CRC32 of user record part of CCFG, used if skip_user_rec = 0 and check_exp_crc = 1
	uint32_t		  	exp_debug_cfg_crc32; 	//Expected CRC32 of debug configuration part of CCFG, used if check_exp_crc = 1
} SACI_PARAM_FLASH_VERIFY_CCFG_SECTOR_T;

//SC_FLASH_VERIFY_SCFG_SECTOR command parameters
typedef struct {
	uint8_t    			cmd_id;				//Command ID
	uint8_t				resp_seq_num;		//Optional sequence number, included in the response header
	uint16_t		  	check_exp_crc : 1;	//0: Validity check of embedded CRCs only; 1: also check CRCs against reference values
	uint16_t		  	reserved0 : 15;	  	//Reserved
	uint32_t		  	expected_crc32;		//Expected CRC32
} SACI_PARAM_FLASH_VERIFY_SCFG_SECTOR_T;

//SC_LIFECYCLE_INCR_STATE command parameters
typedef struct {
	uint8_t    			cmd_id;				//Command ID
	uint8_t				resp_seq_num;		//Optional sequence number, included in the response header
	uint16_t		  	new_state;			//New device lifecycle state
	uint32_t		  	pwd[4];				//128-bit Failure Analysis (FA) password
} SACI_PARAM_LIFECYCLE_INCR_STATE_T;

//SC_LIFECYCLE_REQ_FIRST_BDAY command parameters
typedef struct {
	uint8_t    			cmd_id;				//Command ID
	uint8_t				resp_seq_num;		//Optional sequence number, included in the response header
	uint16_t		  	reserved0;			//Reserved
	uint32_t		  	first_bday_pwd[8];	//256-bit First Birthday password
} SACI_PARAM_LIFECYCLE_REQ_FIRST_BDAY_T;

//SC_BLDR_APP_RESET_DEVICE command parameters
typedef struct {
	uint8_t    			cmd_id;						//Command ID
	uint8_t				resp_seq_num;				//Optional sequence number, included in the response header
	uint16_t		  	wait_swd_disconnect : 1;	//Wait for SWD disconnection sequence before resetting?
	uint16_t		  	reserved0 : 15;				//Reserved
} SACI_PARAM_BLDR_APP_RESET_DEVICE_T;

//SACI command response header
typedef struct {
	uint8_t    				cmd_id;				 //Command ID
	uint8_t					resp_seq_num;		 //Optional sequence number, copied from the first command parameter word, incr. by 1 per sector for  SC_FLASH_PROG_MAIN_PIPELINED
	uint8_t					result;				 //Command result
	uint8_t				 	data_word_count;	 //Size of additional response data, in number of 32-bit words
	union {
		uint32_t 	misc_get_test_id[SACI_GET_TEST_ID_WORD_CNT]; 		//SC_MISC_GET_TEST_ID : Up to 128-bit die ID
		uint32_t 	misc_get_die_id[4];								  	//SC_MISC_GET_DIE_ID : 128-bit die ID
		uint8_t  	misc_get_ccfg_userrec[128];							//SC_MISC_GET_CCFG_USER_REC : Up to 128 bytes of non-read protected CCFG user record (whole number of 16 bytes)
		uint32_t 	debug_req_pwdid[2];								 	//SC_DEBUG_REQ_PWD_ID : 64-bit debug request password ID
		uint32_t 	flash_erase_chip_retain[SACI_ERASE_CHIP_RETAIN_WORD_CNT];   //SC_FLASH_ERASE_CHIP : When retaining MAIN sectors, bit-vectors indicating retained sectors
		uint32_t 	debug_request_key_id[2];								 	//SC_DEBUG_REQ_KEY_ID : 64-bit debug request key ID
		uint32_t 	status_flag;										  		//SC_GET_SECBOOT_HSMFWUPDATE_STATUS: 32-bit status flag
	 };
} SACI_RESP_T;

struct cc_lpf3_flash_bank {
    /* chip id register */
    uint32_t did;
    /* Device Unique ID register */
    uint32_t pid;
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
	void *driver_priv; /**< Private driver storage pointer */
};
#pragma pack(pop)

//SACI command parameters union
typedef union {
	SACI_PARAM_COMMON_T							common;
	SACI_PARAM_DEBUG_SUBMIT_AUTH_T				debug_submit_auth;
	SACI_PARAM_FLASH_ERASE_CHIP_T				flash_erase_chip;
	SACI_PARAM_FLASH_PROG_CCFG_SECTOR_T			flash_prog_ccfg_sector;
	SACI_PARAM_FLASH_PROG_SCFG_SECTOR_T			flash_prog_scfg_sector;
	SACI_PARAM_FLASH_PROG_CCFG_USER_REC_T		flash_prog_ccfg_user_rec;
	SACI_PARAM_FLASH_PROG_MAIN_SECTOR_T			flash_prog_main_sector;
	SACI_PARAM_FLASH_PROG_MAIN_PIPELINED_T		flash_prog_main_pipelined;
	SACI_PARAM_FLASH_VERIFY_MAIN_SECTORS_T		flash_verify_main_sectors;
	SACI_PARAM_FLASH_VERIFY_CCFG_SECTOR_T		flash_verify_ccfg_sector;
	SACI_PARAM_FLASH_VERIFY_SCFG_SECTOR_T		flash_verify_scfg_sector;
	SACI_PARAM_LIFECYCLE_INCR_STATE_T			life_cycle_incr_state;
	SACI_PARAM_LIFECYCLE_REQ_FIRST_BDAY_T		life_cycle_first_bday;
	SACI_PARAM_BLDR_APP_RESET_DEVICE_T			bldr_app_reset_device;
} SACI_PARAM_T;

int cc_lpf3_check_device_info(struct flash_bank *bank);
int cc_lpf3_prepare_write(struct flash_bank *bank);
int cc_lpf3_write_ccfg(struct flash_bank *bank, const uint8_t *buffer,
				 uint32_t offset, uint32_t count);
int cc_lpf3_write_scfg(struct flash_bank *bank, const uint8_t *buffer,
				 uint32_t offset, uint32_t count);
int cc_lpf3_write_main(struct flash_bank *bank, const uint8_t *buffer,
				 uint32_t offset, uint32_t count);
int cc_lpf3_saci_send_cmd(struct flash_bank *bank, SACI_PARAM_T txCmd);
int cc_lpf3_check_boot_status(struct flash_bank *bank);
int cc_lpf3_read_from_AP(struct flash_bank *bank, uint64_t ap_num,
									 unsigned int reg, uint32_t *data);
int cc_lpf3_saci_erase(struct flash_bank *bank);
int cc_lpf3_saci_send_tx_words(struct flash_bank *bank, uint32_t *tx_data, uint32_t length);
int cc_lpf3_saci_verify_ccfg(struct flash_bank *bank, const uint8_t* buffer);
int cc_lpf3_saci_verify_scfg(struct flash_bank *bank, const uint8_t* buffer, uint32_t count);
int cc_lpf3_saci_verify_main(struct flash_bank *bank, const uint8_t* buffer, uint32_t count);
int cc_lpf3_do_blank_check(struct flash_bank *bank);
int cc_lpf3_exit_saci_run(struct flash_bank *bank);
int cc_lpf3_exit_saci_halt(struct flash_bank *bank);