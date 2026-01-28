/******************************************************************************
 *
 * AEONSEMI CONFIDENTIAL
 * _____________________
 *
 * Aeonsemi Inc., 2023
 * All Rights Reserved.
 *
 * NOTICE:  All information contained herein is, and remains the property of
 * Aeonsemi Inc. and its subsidiaries, if any. The intellectual and technical
 * concepts contained herein are proprietary to Aeonsemi Inc. and its
 * subsidiaries and may be covered by U.S. and Foreign Patents, patents in
 * process, and are protected by trade secret or copyright law. Dissemination
 * of this information or reproduction of this material is strictly forbidden
 * unless prior written permission is obtained from Aeonsemi Inc.
 *
 *
 *****************************************************************************/
#ifndef _AS21XX_API_H
#define _AS21XX_API_H

#include <linux/mii.h>
#include <linux/phy.h>
#include <linux/version.h>

/******************************************************************************
 * MISC Macros
 *****************************************************************************/
enum bitwidth_type {
	BW8 = 0,
	BW16 = 1,
	BW32 = 2,
};

#define MDI 1
#define MDIX 0

enum ipc_data_type_t {
	IPC_DATA_UINT8 = 1,
	IPC_DATA_UINT16 = 2,
	IPC_DATA_UINT32 = 4,
};

#define MDI_CFG_SPD_T10 0x2
#define MDI_CFG_SPD_T100 0x4
#define MDI_CFG_SPD_T1G 0x8
#define MDI_CFG_SPD_T2P5G 0x10
#define MDI_CFG_SPD_T5G 0x20
#define MDI_CFG_SPD_T10G 0x40

#define AS21XX_PHY_NUM 2

#if (KERNEL_VERSION(4, 5, 0) > LINUX_VERSION_CODE)
#define phydev_mdio_bus(_dev) (_dev->bus)
#define phydev_addr(_dev) (_dev->addr)
#define phydev_dev(_dev) (&_dev->dev)
#else
#define phydev_mdio_bus(_dev) (_dev->mdio.bus)
#define phydev_addr(_dev) (_dev->mdio.addr)
#define phydev_dev(_dev) (&_dev->mdio.dev)
#endif

/**
 * @brief Flash Image Burning
 */
#define CFG_FSM_NGPHY 0x81
#define CFG_FSM_GPHY 0x8c
#define CFG_FSM_CU_AN 0x8d
#define CFG_FSM_DPC 0x8e
#define CFG_FSM_SS 0x91
#define FLASH_CHIP_SIZE 0x00200000
#define IMAGE1_HDR_OFST 0x10000
#define IMAGE1_OFST 0x10400
#define IMAGE2_HDR_OFST 0x80000
#define IMAGE2_OFST 0x80400
#define MEM_WORD_SIZE 4
#define BOOT_LOADER_BIN "bootloader_all.bin"
#define FLASH_BIN "flash_burn.bin"
#define CLR_FLASH_IMAGE "clr_flash_image.bin"
#define ERASE_MODE_SECTOR 1
#define ERASE_MODE_BLOCK 2
#define FLASH_SECTOR_SIZE 4096

/**
 * @brief ipc related address
 */
#define AEON_REG_ADDR_OFFSET 0xEA000000
#define AN_REG_GIGA_STD_STATUS_BASEADDR (AEON_REG_ADDR_OFFSET + 0xFFFC2)
#define IPC_CMD_BASEADDR (AEON_REG_ADDR_OFFSET + 0x3CB002)
#define IPC_STS_BASEADDR (AEON_REG_ADDR_OFFSET + 0x3CB004)
#define IPC_DATA0_BASEADDR (AEON_REG_ADDR_OFFSET + 0x3CB010)

/**
 * @name Status
 * @brief Introduction of IPC status
 * 16-bit register IPC_STS_BASEADDR is laid out as follows:
 * [1 bit parity][5 bits size][6 bits opcode][4 bits status]
 * The following status indicates the last 4 bits above.
 */
#define IPC_STS_CMD_RCVD 0x1
#define IPC_STS_CMD_PROCESS 0x2
#define IPC_STS_CMD_SUCCESS 0x4
#define IPC_STS_CMD_ERROR 0x8
#define IPC_STS_SYS_BUSY 0xE
#define IPC_STS_SYS_READY 0xF

#define MAX_POLL 100
#define IPC_PAYLOAD_SIZE 16
#define IPC_PAYLOAD_WORDS (IPC_PAYLOAD_SIZE / 2)
#define IPC_NB_OPCODE 0x6
#define IPC_PAYLOAD_NB 0x5
#define IPC_NB_STATUS 0x4
#define IPC_CMD_PARITY 0x8000
#define IPC_TIMEOUT 2000000000

#define EYE_GRPS       31
#define EYE_COLS_GRP    4
#define EYE_YRES      254
#define EYE_XRES      (EYE_GRPS * EYE_COLS_GRP)
#define EYE_STRIDE    (EYE_COLS_GRP * EYE_YRES)
#define EYE_TOTAL_BYTES (EYE_XRES * EYE_YRES)

/**
 * IPC Dump information
 */
#define IPC_DUMP_CU_AN_NUM 1
#define IPC_DUMP_DPC_NUM 3
#define IPC_DUMP_NGPHY_NUM 5
#define IPC_DBG_DUMP_NUM 9
#define IPC_DP_DUMP_NUM 34
/******************************************************************************
 * IPC Command Macros
 *****************************************************************************/
/**
 * @name Opcode
 * @brief Introduction of opcodes.
 * @brief 16-bit register IPC_CMD_BASEADDR is laid out as follows:
 * @brief [1 bit parity][4 bits reserved][5 bits size][6 bits opcode].
 *
 * The following opcodes indicate the last 6 bits above:
 * @IPC_CMD_NOOP: Do nothing
 * @IPC_CMD_INFO: Get Firmware Version
 * @IPC_CMD_SYS_CPU: SYS_CPU
 * @IPC_CMD_RMEM16: Read date from MEM
 * @IPC_CMD_BULK_DATA: Pass bulk data in ipc registers
 * @IPC_CMD_BULK_WRITE: Write bulk data to memory
 * @IPC_CMD_FLASH_WRITE: Write memory data to flash
 * @IPC_CMD_FLASH_ERASE: Erase flash
 * @IPC_CMD_LOG: Read log
 * @IPC_OPCODE_POLL
 * @IPC_CMD_CFG_PARAM: Write config parameters to memory
 * @IPC_CMD_CFG_IRQ: Cfg IRQ Output
 * @IPC_CMD_SET_LED: Set led
 * @IPC_OPCODE_DBGCMD
 * @IPC_OPCODE_WBUF
 * @IPC_OPCODE_RBUF
 */
enum ipc_top_level_cmd {
	IPC_CMD_NOOP = 0,
	IPC_CMD_INFO = 1,
	IPC_CMD_SYS_CPU = 2,
	IPC_CMD_RMEM16 = 3,
	IPC_CMD_RMEM32 = 4,
	IPC_CMD_WMEM16 = 7,
	IPC_CMD_WMEM32 = 8,
	IPC_CMD_BULK_DATA = 10,
	IPC_CMD_BULK_READ = 11,
	IPC_CMD_BULK_WRITE = 12,
	IPC_CMD_FLASH_READ = 13,
	IPC_CMD_FLASH_WRITE = 14,
	IPC_CMD_FLASH_ERASE = 15,
	IPC_CMD_MEM_CSUM = 16,
	IPC_CMD_LOAD_TRIM = 17,
	IPC_CMD_LOG = 19,
	IPC_CMD_CFG_PRINTF = 20,
	IPC_OPCODE_DBGCMD = 22,
	IPC_OPCODE_POLL = 23,
	IPC_OPCODE_WBUF = 24,
	IPC_OPCODE_RBUF = 25,
	IPC_CMD_CFG_PARAM = 26,
	IPC_CMD_CFG_UART_PRINT = 33,
	IPC_CMD_CFG_IRQ = 34,
	IPC_CMD_SET_LED = 35,
	IPC_CMD_CFG_INDIRECT = 36,
};

enum ipc_second_level_param_cmd {
	IPC_CMD_CFG_DIRECT = 4,
};

enum ipc_second_level_log_cmd {
	CFG_LOG_SIZE = 1,
	CFG_LOG_READ = 2,
	CFG_LOG_CLEAN = 3
};

enum ipc_second_level_irq_cmd {
	IPC_CMD_IRQ_EN = 0,
	IPC_CMD_IRQ_QUERY = 1,
	IPC_CMD_IRQ_CLR = 2,
};

enum dbg_second_level_dbgcmd_cmd {
	IPC_DBGCMD_NGPHY = 0x80,
	IPC_DBGCMD_DPC = 0x8b,
	IPC_DBGCMD_SDS = 0x96,
	IPC_DBGCMD_CU_AN = 0xA0,
	CFG_CABLE_DIAG  = 0xa4,
	IPC_DBGCMD_AUTO_LINK = 0xA9,
	IPC_DBGCMD_SYNCE = 0xAC,
};

enum ipc_second_level_info_cmd {
	IPC_CMD_INFO_VERSION = 1,
};

enum ipc_second_level_cpu_cmd {
	IPC_CMD_SYS_REBOOT = 3,
	IPC_CMD_SYS_IMAGE_OFST = 4,
	IPC_CMD_SYS_CPU_IMAGE_CHECK = 5,
	IPC_CMD_SYS_CPU_PHY_ENABLE = 6,
};

enum ipc_third_level_dirct_cmd {
	CFG_NG_PHYCTRL = 1,
	CFG_CU_AN = 2,
	CFG_SDS_PCS = 3,
	CFG_AUTO_EEE = 4,
	CFG_SDS_PMA = 5,
	CFG_DPC_PKT_CHK = 7,
	CFG_DPC_SDS_WAIT_ETH = 8,
	CFG_WDT = 9,
	CFG_SDS_RESTART_AN = 10,
	CFG_TEMP_MON = 11,
	CFG_WOL = 12,
	CFG_SMI_COMMAND = 13,
	CFG_EYE_DRAW = 15,
	CFG_MAC_CNT = 16,
	CFG_SDS2ND_EN = 17,
	CFG_SDS2ND_MODE = 18,
	CFG_SDS2ND_EQ = 19,
};

enum dbg_third_level_synce_cmd {
	IPC_SYNCE_ENABLE = 0,
	IPC_SYNCE_MODE = 1,
	IPC_SYNCE_USER_BW = 2,
	IPC_SYNCE_SLAVE_CLK_OUTPUT_CTRL = 3,
};

#ifdef AEON_SEI2
enum dbg_third_level_dpc_cmd {
	CFG_DPC_SET_CFG = 0,
	CFG_DPC_GET_CFG = 1,
	CFG_DPC_SDS_SET_CFG = 2,
	CFG_DPC_SDS_GET_CFG = 3,
	CFG_DPC_BUFFER_SET_CFG = 4,
	CFG_DPC_BUFFER_GET_CFG = 5,
	CFG_DPC_EEE_SET_CFG = 6,
	CFG_DPC_EEE_GET_CFG = 7,
	CFG_ETH_STS_HW_UPD_CFG = 8,
	CFG_ETH_STS_CFG = 9,
	CFG_BUFFER_EEE_CFG = 10,
	CFG_ETH_STS_CFG_GET = 11,
	CFG_EEE_CLK_STOP_CAP = 12,
	CFG_SUPPORT_FC_SET = 13,
	CFG_SUPPORT_FC_GET = 14,
	CFG_DPC_RX_PKT_RATE = 15,
	CFG_GET_TMR_BIAS = 16,
	CFG_DP_STOCK_RESTART = 17,
	CFG_DP_PKT_MON_SET = 18,
	CFG_DP_PKT_DUMP = 19,
	CFG_DP_PKT_CLR = 20,
	CFG_PKT_FIFO_FULL_TH = 21,
	CFG_PKT_FIFO_FULL_TH_GET = 22,
};

enum dbg_third_level_sds_cmd {
	CFG_PHY_SET_SDS_EQ = 0,
	CFG_SDS_EYE_SCAN = 1,
	CFG_SDS_EYE_SCAN_CFG_GET = 2,
	CFG_PHY_GET_SDS_EQ = 3,
	DBG_TEST_SDS_VGA = 4,
	DBG_TEST_SDS_EDGE = 5,
	DBG_TEST_SDS_CTLE = 6,
	DBG_TEST_SDS_DFE = 7,
	DBG_TEST_SDS_LOAD = 8,
	DBG_TEST_SDS_EEE = 9,
	DBG_SDS_SET_SDS_HW_EQ = 10,
	DBG_SDS_GET_SDS_HW_EQ = 11,
	DBG_SDS_PHY_ENABLE = 12,
	DBG_HWEQ_ITMAX = 13,
	DBG_HWEQ_FSM_OVRD = 14,
	DBG_HWEQ_SUBFSM_BRKP = 15,
};

enum dbg_third_level_ngphy_cmd {
	CFG_NORMAL_RETRAIN_ABI = 13,
	CFG_TM5_GAIN_IDX = 14,
};
#else
enum dbg_third_level_ngphy_cmd {
	CFG_NGPHY_CNT_DUMP = 4,
	CFG_NGPHY_CNT_CLR = 5,
	CFG_NORMAL_RETRAIN_ABI = 13,
	CFG_TM5_GAIN_IDX = 14,
};

enum dbg_third_level_dpc_cmd {
	CFG_DPC_SET_CFG = 0,
	CFG_DPC_SDS_SET_CFG = 0,
	CFG_DPC_GET_CFG = 1,
	IPC_CMD_RA_SET_CFG = 2,
	IPC_CMD_RA_GET_CFG  = 7,
	CFG_DPC_CNT_DUMP = 13,
	CFG_DPC_CNT_CLR = 14,
};

enum dbg_third_level_sds_cmd {
	CFG_SDS_EYE_SCAN = 1,
	CFG_SDS_RST = 13,
	CFG_SDS_TXFIR_SET = 27,
};

enum dbg_third_level_autolink_cmd {
	CFG_AUTO_LINK_ENA = 0,
	CFG_AUTO_LINK_CFG = 1,
};
#endif

enum dbg_third_level_diag_cmd {
	IPC_CMD_CABLE_DIAG_CHAN_LEN = 0,
	IPC_CMD_CABLE_DIAG_PPM_OFST = 1,
	IPC_CMD_CABLE_DIAG_SNR_MARG = 2,
	IPC_CMD_CABLE_DIAG_CHAN_SKW = 3,
#ifndef AEON_SEI2
	IPC_CMD_CABLE_DIAG_SET = 11,
	IPC_CMD_CABLE_DIAG_GET = 12,
#else
	IPC_CMD_CABLE_DIAG_SET = 10,
	IPC_CMD_CABLE_DIAG_GET = 11,
#endif
};

enum dbg_third_level_an_cmd {
	IPC_CMD_CU_AN_ENABLE = 0,
	IPC_CMD_CU_AN_PARA_DET = 8,
	IPC_CMD_CU_AN_CNT_GET = 10,
	IPC_CMD_CU_AN_CNT_CLR = 11,
};

enum ipc_fourth_level_an_cmd {
	MDI_CFG_MAN_MDI = 0,
	MDI_CFG_CU_AN_ENABLE = 1,
	MDI_CFG_CU_AN_DUPLEX = 2,
	MDI_CFG_CU_AN_EEE_SPD = 3,
	MDI_CFG_CU_AN_FR_SPD = 4,
	MDI_CFG_CU_AN_DS = 6,
	MDI_CFG_CU_AN_RESTART = 10,
	MDI_CFG_CU_AN_AEON_OUI = 11,
	IPC_CMD_CU_AN_TOP_SPD = 12,
	IPC_CMD_CU_AN_MS_CFG = 13,
	IPC_CMD_CU_AN_TRD_SWAP = 14,
	IPC_CMD_CU_AN_GET_MS_CFG = 15,
	IPC_CMD_CU_AN_CFR = 16,
};

enum ipc_fourth_level_phyctrl_cmd {
	IPC_CMD_CFG_PHYCTRL_PAUSED = 2,
	IPC_CMD_CFG_TX_FULLSCALE = 3,
	IPC_CMD_CFG_NG_TESTMODE = 4,
	IPC_CMD_GET_TX_FULLSCALE = 5,
};

enum ipc_fourth_level_cnt_cmd {
	IPC_CMD_MAC_TOT = 0,
	IPC_CMD_MAC_CRC = 1,
};

/******************************************************************************
 * Function Definition Macros
 *****************************************************************************/
extern int aeon_cl45_read(struct phy_device *phydev, int dev_addr,
			  unsigned int phy_reg);
extern void aeon_cl45_write(struct phy_device *phydev, int dev_addr,
			    unsigned int phy_reg, unsigned short phy_data);

#ifndef AEON_SEI2
/**
 * @brief Send command to enable/disable packet checker.
 * @param enable Enable/disable packet checker.
 */
void aeon_pkt_chk_cfg(unsigned short enable, struct phy_device *phydev);

/**
 * @brief Set auto-eee configuration.
 * @param enable 1 : enable auto-eee, 0 : disable auto-eee.
 * @param idle_th idle threshhold
 */
void aeon_auto_eee_cfg(unsigned short enable, unsigned int idle_th,
		       struct phy_device *phydev);

/**
 * @brief Set sds_wait_eth configuration.
 * @param sds_wait_eth_delay Delay.
 */
void aeon_sds_wait_eth_cfg(unsigned short sds_wait_eth_delay,
			   struct phy_device *phydev);

/**
 * @brief Set ipc command for restarting serdes AN.
 */
void aeon_sds_restart_an(struct phy_device *phydev);

/**
 * @brief Set gain.
 */
void aeon_ipc_set_tx_power_lvl(unsigned short gain, struct phy_device *phydev);

/**
 * @brief Enable second Serdes
 */
void aeon_sds2nd_enable(unsigned short en, struct phy_device *phydev);

/**
 * @brief Set Seccond Serdes PCS, Datarate and Operation Mode.
 */
void aeon_sds2nd_mode_cfg(unsigned short pcs_mode, unsigned short sds_spd, unsigned short op_mode,
			  struct phy_device *phydev);

/**
 * @brief Disable/enable normal retrain.
 */
void aeon_normal_retrain_cfg(unsigned short enable, struct phy_device *phydev);

/**
 * @brief Enable Auto Link Detection.
 */
void aeon_ipc_auto_link_ena(unsigned char enable, struct phy_device *phydev);

/**
 * @brief Enable Auto Link Type Configuration.
 */
void aeon_ipc_auto_link_cfg(unsigned char link_type, struct phy_device *phydev);

/**
 * @brief Set Serdes Tx Fir 3 cursors
 */
void aeon_ipc_sds_txfir(unsigned char sds_id, unsigned char pre, unsigned char main,
			unsigned char post, struct phy_device *phydev);

/**
 * @brief get PCS mode and speed of serdes.
 * @param pcs_mode 1 : 64/66B, 0 : 8B/10B.
 * @param sds_spd 3 : 10G, 2 : 5G, 1 : 2.5G, 0 : 1G.
 * @param op_mode Normal, Sds_Pkt_Gen, Eth_Pkt_Gen, Eth_Rmt_Lpbk
 */
void aeon_sds_pcs_get_cfg(unsigned char *pcs_sel, unsigned char *sds_spd,
			  struct phy_device *phydev);

/**
 * @brief ra mode(xfi/usxgmii) shift.
 * @param enable ra
 * @param phydev phy device
 */
void aeon_ra_mode_shift(unsigned char enable, struct phy_device *phydev);

/**
 * @brief get ra mode.
 * @param cfg ra mode
 * @param phydev phy device
 */
void aeon_ra_mode_get(unsigned char *cfg, struct phy_device *phydev);

/**
 * @brief set sds loopback.
 * @param phydev phy device
 */
void aeon_ipc_enable_sds_loopback(struct phy_device *phydev);

/**
 * @brief set sds restart.
 * @param phydev phy device
 * @param sds_id sds id
 */
void aeon_sds_restart(unsigned char sds_id, struct phy_device *phydev);
#else
/**
 * @brief get Sds pcs cfg
 */
void aeon_sds_pcs_get_cfg(unsigned char *pcs_sel, unsigned char *sds_spd,
			  struct phy_device *phydev);

/**
 * @brief set dpc fc cfg
 */
void aeon_dpc_fc_cfg(unsigned char enable, struct phy_device *phydev);

/**
 * @brief get dpc fc cfg
 */
void aeon_dpc_fc_cfg_get(unsigned char *enable, struct phy_device *phydev);

/**
 * @brief set dpc eee mode
 */
void aeon_dpc_eee_mode(unsigned char eee_mode, struct phy_device *phydev);

/**
 * @brief get dpc eee mode
 */
void aeon_dpc_eee_mode_get(unsigned char *eee_mode, struct phy_device *phydev);

/**
 * @brief set dpc buffer mode
 */
void aeon_dpc_buffer_mode(unsigned char buffer_mode, struct phy_device *phydev);

/**
 * @brief set dpc buffer mode get
 */
void aeon_dpc_buffer_mode_get(unsigned char *buffer_mode, struct phy_device *phydev);

/**
 * @brief set dpc eee clk mode
 */
void aeon_dpc_eee_clk_mode(unsigned char clk_mode, struct phy_device *phydev);

/**
 * @brief set dpc eq cfg
 */
void aeon_dpc_eq_cfg(unsigned char vga, unsigned char slc, unsigned char ctle,
		     unsigned char dfe, unsigned char ffe, struct phy_device *phydev);

/**
 * @brief set dpc eq cfg
 */
void aeon_dpc_eq_cfg_get(unsigned char *eq_cfg, struct phy_device *phydev);

/**
 * @brief set fifo full tx value
 */
void aeon_pkt_fifo_full_th(unsigned short enable, unsigned short rx_th, unsigned short tx_th,
			   struct phy_device *phydev);

/**
 * @brief get fifo full tx value
 */
void aeon_pkt_fifo_full_th_get(unsigned short *rx_th, unsigned short *tx_th,
			       struct phy_device *phydev);

/**
 * @brief set testmode5 gain idx
 */
void aeon_ipc_testmode5_gain_idx(unsigned char gain_idx, struct phy_device *phydev);
#endif

/**
 * @brief Configure PCS mode and speed of serdes.
 * @param pcs_mode 1 : 64/66B, 0 : 8B/10B.
 * @param sds_spd 3 : 10G, 2 : 5G, 1 : 2.5G, 0 : 1G.
 * @param op_mode Normal, Sds_Pkt_Gen, Eth_Pkt_Gen, Eth_Rmt_Lpbk
 */
void aeon_sds_pcs_set_cfg(unsigned char pcs_sel, unsigned char sds_spd,
			  unsigned char op_mode, struct phy_device *phydev);

/**
 * @brief Send IPC command to sync parity.
 */
void aeon_ipc_sync_parity(struct phy_device *phydev);

/**
 * @brief get as21xxx log size.
 * @param msg_size log size.
 * @param phydev phy device.
 */
void aeon_ipc_log_size(unsigned short *msg_size, struct phy_device *phydev);

/**
 * @brief get as21xxx log data.
 * @param size log size.
 * @param pos log size.
 * @param buf storage log data
 * @param phydev phy device
 */
unsigned int aeon_ipc_read_log(unsigned short size, unsigned short pos, char *buf,
			       struct phy_device *phydev);

/**
 * @brief clean as21xxx log data.
 * @param phydev phy device
 */
void aeon_ipc_log_clean(struct phy_device *phydev);

/**
 * @brief Send IPC command to get FW version.
 */
void aeon_ipc_get_fw_version(char *version, struct phy_device *phydev);

/**
 * @brief Set eee abilities.
 * @param speed_mode 7 bits [10G eee] [5G eee] [2.5G eee] [1G eee] [100m eee] [0] [0].
 */
void aeon_cu_an_set_eee_spd(unsigned short speed_mode,
			    struct phy_device *phydev);

/**
 * @brief Set fast retrain abilities.
 * @param speed_mode 7 bits [10G fr] [5G fr] [2.5G fr] [0] [0] [0] [0].
 * @param thp_bypass 2 bits [5G thp_bypass] [2.5G thp_bypass]
 */
void aeon_cu_an_set_fast_retrain(unsigned short speed_mode,
				 unsigned short thp_bypass,
				 struct phy_device *phydev);

/**
 * @brief Configure donwshift.
 * @param enable Enable/disable downshift.
 * @param retry_limit Limited failure times of training.
 */
void aeon_cu_an_enable_downshift(unsigned short enable,
				 unsigned short retry_limit,
				 struct phy_device *phydev);

/**
 * @brief Send command to restart AN.
 */
void aeon_cu_an_restart(struct phy_device *phydev);

/**
 * @brief Set non-standard PBO mode.
 * @param nstd_pbo 2 bits [max_pbo] [min_pbo]
 */
void aeon_cu_an_enable_aeon_oui(unsigned short nstd_pbo,
				struct phy_device *phydev);

/**
 * @brief Set ipc command to temperature monitor.
 * @param sub_cmd 1 : start, 2 : stop, 3 : set configuration,
 *  4 : get temperature, 5 : set threshhold
 * @param params For sub_cmd = 3, 1 indicates continuous sampling, 0 indicates a single sample.
 * For sub_cmd = 5, params indicates the temperature threshold to be set.
 * @param temperature To save current temperature got from ipc command.
 */
void aeon_ipc_temp_monitor(unsigned short sub_cmd, unsigned short params,
			   unsigned short *temperature,
			   struct phy_device *phydev);

/**
 * @brief Set serdes adaptations.
 * @param vga_adapt 1 : enable VGA adaptation, 0 : disable VGA adaptation.
 * @param slc_adapt 1 : enable slicer adaptation, 0 : disable slicer adaptation.
 * @param ctle_adapt 1 : enable CTLE adaptation, 0 : disable CTLE adaptation.
 * @param dfe_adapt 1 : enable DFE adaptation, 0 : disable DFE adaptation.
 */
void aeon_sds_pma_set_cfg(unsigned short vga_adapt, unsigned short slc_adapt,
			  unsigned short ctle_adapt, unsigned short dfe_adapt,
			  struct phy_device *phydev);

/**
 * @brief Set led configuration.
 */
void aeon_ipc_set_led_cfg(unsigned short led0, unsigned short led1,
			  unsigned short led2, unsigned short led3,
			  unsigned short led4, unsigned short polarity,
			  unsigned short blink, struct phy_device *phydev);

/**
 * @brief Send command to set top_speed.
 * @param top_spd 7 bits [10G] [5G] [2.5G] [1G] [100m] [0] [0].
 */
void aeon_cu_an_set_top_spd(unsigned short top_spd, struct phy_device *phydev);

/**
 * @brief Send command to set trd_swap mode.
 * @param en 1 : override, 0 : no overriding.
 * @param trd_swap 1 : enable TRD swap, 0 : disable TRD swap.
 */
void aeon_cu_an_set_trd_swap(unsigned short en, unsigned short trd_swap,
			     struct phy_device *phydev);

/**
 * @brief Send command to set manual master/slave.
 * @param port_type 0: single port, 1: multi port.
 * @param ms_man_en 0: disable manual m/s, 1: enable manual m/s.
 * @param ms_man_val 0: slave, 1: master
 */
void aeon_cu_an_set_ms_cfg(unsigned short port_type, unsigned short ms_man_en,
			   unsigned short ms_man_val,
			   struct phy_device *phydev);

/**
 * @brief Send command to get m/s-related configuration.
 */
void aeon_cu_an_get_ms_cfg(unsigned short *ms_related_cfg,
			   struct phy_device *phydev);

/**
 * @brief Send command to reboot phy.
 */
void aeon_ipc_set_sys_reboot(struct phy_device *phydev);

/**
 * @brief Enable/disable phy.
 */
void aeon_ipc_phy_enable_mode(unsigned short enable, struct phy_device *phydev);

/**
 * @brief Write data from memory to flash.
 * @param flash_addr Flash address to write to.
 * @param mem_addr Memory address of data to write.
 * @param size Size of data.
 */
void aeon_ipc_write_flash(unsigned int flash_addr, unsigned int mem_addr,
			  unsigned short size, struct phy_device *phydev);

/**
 * @brief Erase flash.
 * @param flash_addr Flash address to erase.
 * @param size Size of data.
 * @param mode Mode of erasing. 1 : sector erase, 2 : block erase.
 */
void aeon_ipc_erase_flash(unsigned int flash_addr, unsigned int size,
			  unsigned short mode, struct phy_device *phydev);

/**
 * @brief Set WDT.
 * @param en Enable/disable WDT.
 */
void aeon_ipc_set_wdt(unsigned short en, struct phy_device *phydev);

/**
 * @brief Update image.
 * @param include_bootloader 1/0.
 * @note This a demo for dual flash programming feature.
 */
void aeon_burn_image(unsigned char include_bootloader,
		     struct phy_device *phydev);

/**
 * @brief Send short config parameter list to firmware directly.
 */
void aeon_ipc_cfg_param_direct(unsigned int data_len, unsigned short *data,
			       struct phy_device *phydev);

/**
 * @brief Set IPC commands related to CPU INFO.
 */
int aeon_ipc_sys_cpu_info(unsigned short sub_cmd, unsigned int flash_addr,
			  unsigned int mem_addr, struct phy_device *phydev);

/**
 * @brief Set FW FSM running mode.
 */
void aeon_ipc_set_fsm_mode(unsigned short fsm, unsigned short mode,
			   struct phy_device *phydev);

/**
 * @brief Update flash image.
 */
void aeon_update_flash(const char *firmware, unsigned int flash_start,
		       struct phy_device *phydev);

/**
 * @brief Set ipc command for NG test mode.
 */
void aeon_ipc_ng_test_mode(unsigned short test_mode, unsigned short tone,
			   struct phy_device *phydev);

/**
 * @brief Enable NG test mode for specific speed mode.
 */
void aeon_ng_test_mode(unsigned short top_spd, unsigned short test_mode, unsigned short tone,
		       struct phy_device *phydev);

/**
 * @brief Enable 1G test mode.
 */
void aeon_1g_test_mode(unsigned short test_mode, struct phy_device *phydev);

/**
 * @brief Enable 100M test mode.
 */
void aeon_100m_test_mode(struct phy_device *phydev);

/**
 * @brief Set WOL.
 * @param en Enable/disable WOL.
 */
void aeon_ipc_set_wol(unsigned short en, unsigned short *val,
	struct phy_device *phydev);

/**
 * @brief Set SMI Command.
 * @param val command value.
 */
void aeon_ipc_smi_command(unsigned short *val, struct phy_device *phydev);

/**
 * @brief Set irq enable.
 * @param val irq index and en.
 */
void aeon_ipc_irq_en(unsigned short *val, struct phy_device *phydev);

/**
 * @brief Set irq clr.
 * @param val irq index.
 */
void aeon_ipc_irq_clr(unsigned short val, struct phy_device *phydev);

/**
 * @brief get irq status.
 * @param val irq status.
 */
void aeon_ipc_irq_query(unsigned short *irq, struct phy_device *phydev);

/**
 * @brief Set IPC commands related to cable diag lite.
 */
void aeon_ipc_cable_diag(unsigned short sub_cmd, unsigned short *data, unsigned short mode,
			 struct phy_device *phydev);

/**
 * @brief Configure CISCO fast-retrain.
 */
void aeon_cu_an_set_cfr(unsigned short cfr, struct phy_device *phydev);

/**
 * @brief Set tx fullscale.
 */
void aeon_ipc_set_tx_fullscale_delta(unsigned short speed, unsigned short *delta,
				     struct phy_device *phydev);

/**
 * @brief Get tx_full scale.
 */
void aeon_ipc_get_tx_fullscale_delta(unsigned short speed, unsigned short *delta,
				     struct phy_device *phydev);

/**
 * @brief Get data from memory.
 */
void aeon_ipc_read_mem(unsigned short addr1, unsigned short addr2, unsigned short num,
		       unsigned short *params, struct phy_device *phydev);

/**
 * @brief Set mac count.
 */
void aeon_ipc_set_mac_cnt(unsigned long long mac_tot_cnt, unsigned long long mac_crc_cnt,
			  struct phy_device *phydev);

/**
 * @brief Set Seccond Serdes Equalization.
 */
void aeon_sds2nd_eq_cfg(unsigned short vga, unsigned short slc, unsigned short ctle,
			unsigned short dfe, struct phy_device *phydev);

/**
 * @brief Set duplex mode.
 */
void aeon_set_man_duplex(unsigned short duplex, struct phy_device *phydev);

/**
 * @brief Enable/disable AN.
 */
void aeon_cu_an_enable(unsigned char enable, struct phy_device *phydev);

/**
 * @brief Serdes Eye Scan.
 */
void aeon_ipc_eye_scan(unsigned char sds_id, unsigned char grp, unsigned short *rev_buf,
		       struct phy_device *phydev);

/**
 * @brief Force MDI mode.
 */
void aeon_set_man_mdi(struct phy_device *phydev);

/**
 * @brief Force MDIX mode.
 */
void aeon_set_man_mdix(struct phy_device *phydev);

/**
 * @brief Set synce enable.
 */
void aeon_synce_enable_cfg(unsigned char enable, struct phy_device *phydev);

/**
 * @brief Set synce master/slave.
 */
void aeon_synce_mode_cfg(unsigned char ms, struct phy_device *phydev);

/**
 * @brief Set synce user bw
 */
void aeon_synce_user_bw(unsigned char bw, struct phy_device *phydev);

/**
 * @brief Set synce slave output pin
 */
void aeon_synce_slave_output_ctrl_cfg(unsigned char oc, struct phy_device *phydev);
/** @} */

/**
 * @brief Get phy link status.
 */
int aeon_read_status(struct phy_device *phydev);

/**
 * @brief Set parallel detection.
 */
void aeon_parallel_det(unsigned char enable, struct phy_device *phydev);

/**
 * @brief Dump PHY Count.
 */
void aeon_ipc_cnt_dump(unsigned int *revc_buf, struct phy_device *phydev);
/**
 * @brief Clear PHY Count.
 */
void aeon_ipc_cnt_clr(struct phy_device *phydev);
#endif
