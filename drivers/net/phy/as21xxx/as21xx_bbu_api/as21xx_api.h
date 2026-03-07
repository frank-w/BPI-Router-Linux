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

typedef enum {
	DATA_UINT8 = 1,
	DATA_UINT16 = 2,
	DATA_UINT32 = 4,
} ipc_data_type_t;

#define AS21XX_PHY_NUM 2
#define MAX_POLL 100

#if (KERNEL_VERSION(4, 5, 0) > LINUX_VERSION_CODE)
#define phydev_mdio_bus(_dev) (_dev->bus)
#define phydev_addr(_dev) (_dev->addr)
#define phydev_dev(_dev) (&_dev->dev)
#else
#define phydev_mdio_bus(_dev) (_dev->mdio.bus)
#define phydev_addr(_dev) (_dev->mdio.addr)
#define phydev_dev(_dev) (&_dev->mdio.dev)
#endif

#define IPC_DATA_UINT8 1
#define IPC_DATA_UINT16 2
#define IPC_DATA_UINT32 4

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
 * @Regsiter addresses and fields
 * @Registers used in MDIO boot
 */
#define AEON_REG_ADDR_OFFSET 0xEA000000
#define AN_REG_GIGA_STD_STATUS_BASEADDR (AEON_REG_ADDR_OFFSET + 0xFFFC2)
#define IPC_CMD_BASEADDR (AEON_REG_ADDR_OFFSET + 0x3CB002)
#define IPC_STS_BASEADDR (AEON_REG_ADDR_OFFSET + 0x3CB004)
#define IPC_DATA0_BASEADDR (AEON_REG_ADDR_OFFSET + 0x3CB010)

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
#define IPC_CMD_NOOP 0x0
#define IPC_CMD_INFO 0x1
#define IPC_CMD_SYS_CPU 0x2
#define IPC_CMD_RMEM16 0x3
#define IPC_CMD_BULK_DATA 0xA
#define IPC_CMD_BULK_WRITE 0xC
#define IPC_CMD_FLASH_WRITE 0xE
#define IPC_CMD_FLASH_ERASE 0xF
#define IPC_CMD_LOG 0x13
#define IPC_OPCODE_DBGCMD 0x16
#define IPC_OPCODE_POLL 0x17
#define IPC_OPCODE_WBUF 0x18
#define IPC_OPCODE_RBUF 0x19
#define IPC_CMD_CFG_PARAM 0x1A
#define IPC_CMD_CFG_IRQ 0x22
#define IPC_CMD_SET_LED 0x23

/**
 * @name Cfg_module
 * @brief Introduction of modules of sub_commands
 */
enum custom_direct_cfg_module {
	CFG_NG_PHYCTRL = 1,
	CFG_CU_AN = 2,
	CFG_SDS_PCS = 3,
	CFG_AUTO_EEE = 4,
	CFG_SDS_PMA = 5,
	CFG_DPC_RA = 6,
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

/**
 * @name Cfg_irq
 * @brief Introduction of modules of sub_commands
 */
enum custom_irq_cfg_module {
	IPC_CMD_IRQ_EN = 0x0,
	IPC_CMD_IRQ_QUERY = 0x1,
	IPC_CMD_IRQ_CLR = 0x2,
};

/**
 * @name Cfg_synce
 * @brief Introduction of modules of sub_commands
 */
enum custom_synce_cfg_module {
	IPC_SYNCE_ENABLE = 0x0,
	IPC_SYNCE_MODE = 0x1,
	IPC_SYNCE_USER_BW = 0x2,
	IPC_SYNCE_SLAVE_CLK_OUTPUT_CTRL = 0x3,
};

/**
 * @name Sub_command
 * @brief Introduction of sub_commands of opcodes
 */
/**
 * @name Sub_command
 * @brief Introduction of sub_commands of opcodes
 *
 * @IPC_CMD_INFO_VERSION: Sub-command of IPC_CMD_INFO
 * @IPC_CMD_SYS_REBOOT: Sub-command of IPC_CMD_SYS_CPU, reboot phy
 * @IPC_CMD_SYS_IMAGE_OFST: Sub-command of IPC_CMD_SYS_CPU, get flash image offset
 * @IPC_CMD_SYS_CPU_IMAGE_CHECK: Sub-command of IPC_CMD_SYS_CPU, CRC check after upgrading
 *                               flash image
 * @IPC_CMD_SYS_CPU_PHY_ENABLE: Sub-command of IPC_CMD_SYS_CPU, enable/disable phy
 * @IPC_CMD_CFG_DIRECT: Sub-command of IPC_CMD_CFG_PARAM, configure parameters of AN, DPC, phy_ctrl
 * @IPC_CMD_READ_LOG_CLEAR: Sub-command of IPC_CMD_LOG
 * @CFG_NORMAL_RETRAIN_ABI
 * @CFG_TM5_GAIN_IDX
 * @CFG_SDS_TXFIR_SET: Sub-command of CFG_SDS
 * @CFG_AUTO_LINK_ENA: Sub-command of CFG_AUTO_LINK
 * @CFG_AUTO_LINK_CFG: Sub-command of CFG_AUTO_LINK
 */
#define IPC_CMD_INFO_VERSION 0x1
#define IPC_CMD_SYS_REBOOT 0x3
#define IPC_CMD_SYS_IMAGE_OFST 0x4
#define IPC_CMD_SYS_CPU_IMAGE_CHECK 0x5
#define IPC_CMD_SYS_CPU_PHY_ENABLE 0x6
#define IPC_CMD_CFG_DIRECT 0x4
#define IPC_CMD_READ_LOG_CLEAR 0x3
#define CFG_NORMAL_RETRAIN_ABI 13
#define CFG_TM5_GAIN_IDX 14
#define CFG_SDS_EYE_SCAN 1
#ifndef AEON_SEI2
#define CFG_DPC_SDS_SET_CFG 0
#else
#define CFG_DPC_SDS_SET_CFG 2
#endif
#define CFG_SDS_TXFIR_SET 27
#define CFG_AUTO_LINK_ENA 0
#define CFG_AUTO_LINK_CFG 1
#define IPC_CMD_CABLE_DIAG_CHAN_LEN 0
#define IPC_CMD_CABLE_DIAG_PPM_OFST 1
#define IPC_CMD_CABLE_DIAG_SNR_MARG 2
#define IPC_CMD_CABLE_DIAG_CHAN_SKW 3
#ifndef AEON_SEI2
#define IPC_CMD_CABLE_DIAG_SET 11
#define IPC_CMD_CABLE_DIAG_GET 12
#else
#define IPC_CMD_CABLE_DIAG_SET 10
#define IPC_CMD_CABLE_DIAG_GET 11
#endif
#define IPC_CMD_CU_AN_PARA_DET 8

/**
 * @name Feature
 * @brief Introduction of features of modules
 */
#define IPC_CMD_CFG_PHYCTRL_PAUSED 0x2
#define IPC_CMD_CFG_TX_FULLSCALE 0x3
#define IPC_CMD_CFG_NG_TESTMODE 0x4
#define IPC_CMD_GET_TX_FULLSCALE 0x5
#define MDI_CFG_MAN_MDI 0x0
#define MDI_CFG_CU_AN_ENABLE 0x1
#define MDI_CFG_CU_AN_DUPLEX 0x2
#define MDI_CFG_CU_AN_EEE_SPD 0x3
#define MDI_CFG_CU_AN_FR_SPD 0x4
#define MDI_CFG_CU_AN_DS 0x6
#define MDI_CFG_CU_AN_RESTART 0xa
#define MDI_CFG_CU_AN_AEON_OUI 0xb
#define IPC_CMD_CU_AN_TOP_SPD 0xc
#define IPC_CMD_CU_AN_MS_CFG 0xd
#define IPC_CMD_CU_AN_TRD_SWAP 0xe
#define IPC_CMD_CU_AN_GET_MS_CFG 0xf
#define IPC_CMD_CU_AN_CFR 0x10
#define IPC_CMD_MAC_TOT 0x0
#define IPC_CMD_MAC_CRC 0x1
/** @} */

/**
 * @name Status
 * @brief Introduction of IPC status
 * 16-bit register IPC_STS_BASEADDR is laid out as follows:
 * [1 bit parity][5 bits size][6 bits opcode][4 bits status]
 *
 * The following status indicates the last 4 bits above.
 */
#define IPC_STS_CMD_RCVD 0x1
#define IPC_STS_CMD_PROCESS 0x2
#define IPC_STS_CMD_SUCCESS 0x4
#define IPC_STS_CMD_ERROR 0x8
#define IPC_STS_SYS_BUSY 0xE
#define IPC_STS_SYS_READY 0xF

extern int aeon_cl45_read(struct phy_device *phydev, int dev_addr,
			  unsigned int phy_reg);
extern void aeon_cl45_write(struct phy_device *phydev, int dev_addr,
			    unsigned int phy_reg, unsigned short phy_data);
/** @} */
/** @name IPC Function
 * @brief Introduction of IPC functions
 * @{
 *
 1. **Opcode : IPC_CMD_NOOP**
 *
 * Api : void aeon_ipc_sync_parity(struct phy_device *phydev);
 *
 2. **Opcode : IPC_CMD_INFO**
 *
 * Sub-command : IPC_CMD_INFO_VERSION
 *
 * Api : void aeon_ipc_get_fw_version(char *version, struct phy_device *phydev);
 *
 * param : version The string to get FW version.
 *
 3. **Opcode : IPC_CMD_SYS_CPU**
 *
 * 3.1 Sub-command : IPC_CMD_SYS_REBOOT
 *
 * Api : void aeon_ipc_set_sys_reboot(struct phy_device *phydev);
 *
 * 3.2 Sub-command : IPC_CMD_SYS_CPU_PHY_ENABLE
 *
 * Api : void aeon_ipc_phy_enable_mode(unsigned short enable, struct phy_device *phydev);
 *
 * param : enable Enable/disable PHY.
 *
 * 3.3 Sub-command : IPC_CMD_SYS_CPU_IMAGE_CHECK, IPC_CMD_SYS_IMAGE_OFST
 *
 * Api : int aeon_ipc_sys_cpu_info(unsigned short sub_cmd, unsigned int flash_addr,
 *                                 unsigned int mem_addr, struct phy_device *phydev)
 *
 * param : sub_cmd IPC_CMD_SYS_CPU_IMAGE_CHECK / IPC_CMD_SYS_IMAGE_OFST
 *
 * param : flash_addr Flash address to write.
 *
 * param : mem_addr Memory address to write.
 *
 4. **Opcode : IPC_CMD_BULK_DATA**
 *
 * Api : void aeon_ipc_send_bulk_data(unsigned short bw_type, unsigned short size,
 *                                    void *data, struct phy_device *phydev);
 *
 * param : bw_type Type of bit-width of data.
 *
 * param : size Size of data.
 *
 * param : data Data to write to IPC data registers.
 *
 5. **Opcode : IPC_CMD_BULK_WRITE**
 *
 * Api : void aeon_ipc_send_bulk_write(unsigned int mem_addr, unsigned int size,
 *                                     struct phy_device *phydev);
 *
 * param : mem_addr Memory address to write.
 *
 * param : size Size of data.
 *
 6. **Opcode : IPC_CMD_FLASH_WRITE**
 *
 * Api : void aeon_ipc_write_flash(unsigned int flash_addr, unsigned int mem_addr,
 *                                 unsigned short size, struct phy_device *phydev);
 *
 * param : flash_addr Flash address to write.
 *
 * param : mem_addr Memory address of the data we want to write to flash.
 *
 * param : size Size of data.
 *
 7. **Opcode : IPC_CMD_FLASH_ERASE**
 *
 * Api : void aeon_ipc_erase_flash(unsigned int flash_addr, unsigned int size,
 *                                 unsigned short mode, struct phy_device *phydev);
 *
 * param : flash_addr Flash address to erase.
 *
 * param : size Size of data.
 *
 * param : mode Mode of erasing. 1 : sector erase, 2 : block erase.
 *
 8. **Opcode : IPC_CMD_CFG_PARAM**
 *
 * Sub-command : IPC_CMD_CFG_DIRECT
 *
 * 8.1 Cfg-module : CFG_NG_PHYCTRL
 *
 * 8.1.1 Feature : IPC_CMD_CFG_PHYCTRL_PAUSED
 *
 * Api : void aeon_ipc_set_fsm_mode(unsigned short fsm, unsigned short mode,
 *                                  struct phy_device *phydev);
 *
 * param : fsm Macro of FSMs.
 *
 * param : mode FSM running mode.
 *
 * 8.1.2 Feature : IPC_CMD_CFG_TX_FULLSCALE
 *
 * Api : void aeon_ipc_set_tx_fullscale_delta(unsigned short speed, unsigned short *delta,
 *                                            struct phy_device *phydev);
 *
 * param : 4(100M), 8(1G), 16(2.5G), 32(5G), 64(10G).
 *
 * param : Value to set.
 *
 * 8.1.3 Feature : IPC_CMD_CFG_NG_TESTMODE
 *
 * Api : void aeon_ipc_ng_test_mode(unsigned short test_mode, unsigned short tone,
 *                                  struct phy_device *phydev);
 *
 * param : test_mode Test mode to be set.
 *
 * param : tone Test tone to be set.
 *
 * 8.1.4 Feature : IPC_CMD_GET_TX_FULLSCALE
 *
 * Api : void aeon_ipc_get_tx_fullscale_delta(unsigned short speed, unsigned short *delta,
 *                                            struct phy_device *phydev);
 *
 * param : speed Speed mode.
 *
 * param : delta Array to get tx_fullscale.
 *
 * 8.2 Cfg-module : CFG_CU_AN
 *
 * 8.2.1 Feature : MDI_CFG_CU_AN_EEE_SPD
 *
 * Api : void aeon_cu_an_set_eee_spd(unsigned short speed_mode, struct phy_device *phydev);
 *
 * param : speed_mode 7 bits [10G eee en] [5G eee en] [2.5G eee en] [1G eee en] [100m eee] [0] [0].
 *
 * note : If you set EEE abilities of all speeds, speed_mode = 0b1111100(0x7C).
 *
 * 8.2.2 Feature : MDI_CFG_CU_AN_FR_SPD
 *
 * Api : void aeon_cu_an_set_fast_retrain(unsigned short speed_mode, unsigned short thp_bypass,
 *                                        struct phy_device *phydev);
 *
 * param : speed_mode 7 bits [10G fr] [5G fr] [2.5G fr] [0] [0] [0] [0].
 *
 * param : thp_bypass 2 bits [5G thp_bypass] [2.5G thp_bypass]
 *
 * note : If you set FR abilities and thp_bypass of all speeds, speed_mode = 0b1110000(0x70),
 *        thp_bypass = 3.
 *
 * 8.2.3 Feature : MDI_CFG_CU_AN_DS
 *
 * Api : void aeon_cu_an_enable_downshift(unsigned short enable, unsigned short retry_limit,
 *                                        struct phy_device *phydev);
 *
 * param : enable Enable/disable downshift.
 *
 * param : retry_limit Limited failure times of training.
 *
 * 8.2.4 Feature : MDI_CFG_CU_AN_RESTART
 *
 * Api : void aeon_cu_an_restart(struct phy_device *phydev);
 *
 * 8.2.5 Feature : MDI_CFG_CU_AN_AEON_OUI
 *
 * Api : void aeon_cu_an_enable_aeon_oui(unsigned short nstd_pbo, struct phy_device *phydev);
 *
 * param : nstd_pbo 2 bits [max_pbo] [min_pbo]
 *
 * note : If you want to enable max_pbo and min_pbo, nstd_pbo = 3.
 *
 * 8.2.6 Feature : IPC_CMD_CU_AN_TOP_SPD
 *
 * Fucntion : void aeon_cu_an_set_top_spd(unsigned short top_spd, struct phy_device *phydev);
 *
 * param : top_spd 7 bits [10G] [5G] [2.5G] [1G] [100m] [0] [0].
 *
 * 8.2.7 Feature : IPC_CMD_CU_AN_MS_CFG
 *
 * Api : void aeon_cu_an_set_ms_cfg(unsigned short port_type, unsigned short ms_man_en,
 *                                  unsigned short ms_man_val, struct phy_device *phydev);
 *
 * param : port_type 0: single port, 1: multi port.
 *
 * param : ms_man_en 0: disable manual m/s, 1: enable manual m/s.
 *
 * param : ms_man_val 0: slave, 1: master
 *
 * 8.2.8 Feature : IPC_CMD_CU_AN_TRD_SWAP
 *
 * Api : void aeon_cu_an_set_trd_swap(unsigned short en, unsigned short trd_swap,
 *                                    struct phy_device *phydev);
 *
 * param : trd_swap 1 : enable TRD swap, 0 : disable TRD swap.
 *
 * 8.2.9 Feature : IPC_CMD_CU_AN_GET_MS_CFG
 *
 * Api : void aeon_cu_an_get_ms_cfg(unsigned short *ms_related_cfg, struct phy_device *phydev);
 *
 * param : ms_related_cfg Array to put ms_cfg, ms_related_cfg[0] indicate port_type,
 *         ms_related_cfg[1] indicates ms_man_en, ms_related_cfg[2] indicates ms_man_val.
 *
 * 8.2.10 Feature : IPC_CMD_CU_AN_CFR
 *
 * Api : void aeon_cu_an_set_cfr(unsigned short cfr, struct phy_device *phydev);
 *
 * param : cfr 1 : enable CFR, 0 : disable CFR.
 *
 * 8.3 Cfg-module : CFG_SDS_PCS
 *
 * Api : void aeon_sds_pcs_set_cfg(unsigned short pcs_mode, unsigned short sds_spd,
 *                                 struct phy_device *phydev);
 *
 * param : pcs_mode 1 : 64/66B, 0 : 8B/10B.
 *
 * param : sds_spd 3 : 10G, 2 : 5G, 1 : 2.5G, 0 : 1G.
 *
 * 8.4 Cfg-module : CFG_AUTO_EEE
 *
 * Api : void aeon_auto_eee_cfg(unsigned short enable, unsigned int idle_th,
 *                              struct phy_device *phydev);
 *
 * param : enable 1 : enable auto-eee, 0 : disable auto-eee.
 *
 * param : idle_th idle threshhold
 *
 * 8.5 Cfg-module : CFG_SDS_PMA
 *
 * Api : void aeon_sds_pma_set_cfg(unsigned short vga_adapt, unsigned short slc_adapt,
 *                                 unsigned short ctle_adapt, unsigned short dfe_adapt,
 *                                 struct phy_device *phydev);
 *
 * param : vga_adapt 1 : enable VGA adaptation, 0 : disable VGA adaptation.
 *
 * param : slc_adapt 1 : enable slicer adaptation, 0 : disable slicer adaptation.
 *
 * param : ctle_adapt 1 : enable CTLE adaptation, 0 : disable CTLE adaptation.
 *
 * param : dfe_adapt 1 : enable DFE adaptation, 0 : disable DFE adaptation.
 *
 * 8.6 Cfg-module : CFG_DPC_RA
 *
 * Api : void aeon_dpc_ra_enable(struct phy_device *phydev);
 *
 * 8.7 Cfg-module : CFG_DPC_PKT_CHK
 *
 * Api : void aeon_pkt_chk_cfg(unsigned short enable, struct phy_device *phydev);
 *
 * param : enable Enable/disable packet checker.
 *
 * 8.8 Cfg-module : CFG_DPC_SDS_WAIT_ETH
 *
 * Api : void aeon_sds_wait_eth_cfg(unsigned short sds_wait_eth_delay, struct phy_device *phydev);
 *
 * param : sds_wait_eth_delay Delay.
 *
 * 8.9 Cfg-module : CFG_WDT
 *
 * Api : void aeon_ipc_set_wdt(unsigned short en, struct phy_device *phydev);
 *
 * param : en Enable/disable WDT.
 *
 * 8.10 Cfg-module : CFG_SDS_RESTART_AN
 *
 * Api : void aeon_sds_restart_an(struct phy_device *phydev);
 *
 * 8.11 Cfg-module : IPC_CMD_TEMP_MON
 *
 * Api : void aeon_ipc_temp_monitor(unsigned short sub_cmd, unsigned short params,
 *                                  unsigned short *temperature, struct phy_device *phydev);
 *
 * param : sub_cmd 1 : start, 2 : stop, 3 : set configuration, 4 : get temperature,
 *                 5 : set threshhold
 *
 * param : params For sub_cmd = 3, 1 indicates continuous sampling, 0 indicates a single sample.
 *
 * For sub_cmd = 5, params indicates the temperature threshold to be set.
 *
 * param : temperature To save current temperature got from ipc command.
 *
 * 8.12 Cfg-module : CFG_WOL
 *
 * Api : void aeon_ipc_set_wol(unsigned short en, unsigned short *val, struct phy_device *phydev);
 *
 * param : en 0 : mac addr and passwd return default value, 1 : cfg mac addr, 2 : cfg passwd,
 *                                                          3 : cfg mac addr.
 *
 * param : val mac addr and passwd for wol.
 *
 * 8.13 Cfg-module : CFG_SMI_COMMAND
 *
 * Api : void aeon_ipc_smi_command(unsigned short *val, struct phy_device *phydev);
 *
 * param : val SMI Command Value.
 *
 * 8.15 Cfg-module : CFG_SDS2ND_EN
 *
 * Api : void aeon_sds2nd_enable(unsigned short en, struct phy_device *phydev);
 *
 * param : en 1 : enable 2nd serdes, 0 : 2nd serdes.
 *
 * 8.16 Cfg-module : CFG_SDS2ND_MODE
 *
 * Api : void aeon_sds2nd_mode_cfg(unsigned short pcs_mode, unsigned short sds_spd,
 *                                 unsigned short op_mode, struct phy_device *phydev);
 *
 * param : pcs_mode 1 : 64/66B, 0 : 8B/10B.
 *
 * param : sds_spd 3 : 10G, 2 : 5G, 1 : 2.5G, 0 : 1G.
 *
 * 8.17 Cfg-module : CFG_SDS2ND_EQ
 *
 * Api : void aeon_sds2nd_eq_cfg(unsigned short vga, unsigned short slc, unsigned short ctle,
 *                               unsigned short dfe, struct phy_device *phydev);
 *
 * param : vga 1 : enable VGA adaptation, 0 : disable VGA adaptation.
 *
 * param : slc 1 : enable slicer adaptation, 0 : disable slicer adaptation.
 *
 * param : ctle 1 : enable CTLE adaptation, 0 : disable CTLE adaptation.
 *
 * param : dfe 1 : enable DFE adaptation, 0 : disable DFE adaptation.
 *
 9. **Opcode : IPC_CMD_SET_LED**
 *
 * Api : void aeon_ipc_set_led_cfg(unsigned short led0, unsigned short led1,
 *                                 unsigned short led2, unsigned short led3,
 *                                 unsigned short led4, unsigned short polarity,
 *                                 unsigned short blink, struct phy_device *phydev);
 *
 * param : led0 Behavior of LED0.
 *
 * param : led1 Behavior of LED1.
 *
 * param : led2 Behavior of LED2.
 *
 * param : led3 Behavior of LED3.
 *
 * param : led4 Behavior of LED4.
 *
 * param : led5 Behavior of LED5.
 *
 * param : polarity cfg of these leds.
 *
 * param : blink Blink rate of these leds.
 *
 10. **Opcode : IPC_CMD_CFG_IRQ**
 *
 * 10.1 Cfg-module : IPC_CMD_IRQ_EN
 *
 * Api : aeon_ipc_irq_en(unsigned short *irq, struct phy_device *phydev);
 *
 * param : val[0] enable irq index, val[0] enable irq value.
 *
 * 10.2 Cfg-module : IPC_CMD_IRQ_QUERY
 *
 * Api : aeon_ipc_irq_query(unsigned short *irq, struct phy_device *phydev);
 *
 * param : irq trigger .
 *
 * 10.3 Cfg-module : IPC_CMD_IRQ_CLR
 *
 * Api : void aeon_ipc_irq_clr(unsigned short val, struct phy_device *phydev);
 *
 * param : val clear irq index.
 *
 */
#define IPC_PAYLOAD_SIZE 16
#define IPC_PAYLOAD_WORDS (IPC_PAYLOAD_SIZE / 2)
#define IPC_NB_OPCODE 0x6
#define IPC_PAYLOAD_NB 0x5
#define IPC_NB_STATUS 0x4
#define IPC_CMD_PARITY 0x8000
#define IPC_TIMEOUT 2000000000
/// The MSB of the status bit is used as a parity bit, toggling each time an
/// IPC command is serviced.
#define IPC_STS_PAR_MASK 0x8000
/// The data layout for the sub-fields in the IPC status register
#define IPC_STS_STS_WIDTH 4
/** @} */

#define PCS_SPEED_SEL_T10G 0x0
#define PCS_SPEED_SEL_T5G 0x8
#define PCS_SPEED_SEL_T2P5G 0x7

#define MDI_CFG_SPD_T10 0x2
#define MDI_CFG_SPD_T100 0x4
#define MDI_CFG_SPD_T1G 0x8
#define MDI_CFG_SPD_T2P5G 0x10
#define MDI_CFG_SPD_T5G 0x20
#define MDI_CFG_SPD_T10G 0x40

#define PCS_MODE_8B_10B 0
#define PCS_MODE_64B_66B 1

#define SDS_DATARATE_1G 0
#define SDS_DATARATE_2P5G 1
#define SDS_DATARATE_5G 2
#define SDS_DATARATE_10G 3

#define SDS_RA_XFI 0
#define SDS_RA_USXGMII 1

#define CFG_FSM_NGPHY 0x81
#define CFG_FSM_GPHY 0x8c
#define CFG_FSM_CU_AN 0x8d
#define CFG_FSM_DPC 0x8e
#define CFG_FSM_SS 0x91
#define CFG_CABLE_DIAG 0xa4

#define IPC_DBGCMD_NGPHY 0x80
#define IPC_DBGCMD_DPC 0x8b
#define IPC_DBGCMD_SDS 0x96
#define IPC_DBGCMD_AUTO_LINK 0xA9
#define IPC_DBGCMD_SYNCE 0xAC
#define IPC_DBGCMD_CU_AN 0xA0

#define MDI 1
#define MDIX 0

enum bitwidth_type {
	BW8 = 0,
	BW16 = 1,
	BW32 = 2,
};

// SDS Eye Scan Parameters
#define EYE_GRPS       31
#define EYE_COLS_GRP    4
#define EYE_YRES      254
#define EYE_NSAMP     256
#define EYE_XRES      (EYE_GRPS * EYE_COLS_GRP)
#define EYE_STRIDE    (EYE_COLS_GRP * EYE_YRES)
#define EYE_TOTAL_BYTES (EYE_XRES * EYE_YRES)
#define EYE_PART_0 0
#define EYE_PART_1 1
#define EYE_PART_0_GRPS (EYE_GRPS / 2)
#define EYE_PART_1_GRPS (EYE_GRPS - EYE_PART_0_GRPS)

/** @name AEON's private function related to IPC
 * @note These functions shouldn't be called individually
 * @{
*/
/**
 * @brief Get data from to IPC_DATA registers.
 * @param len Data size.
 * @param data Data array to be assigned.
 */
void aeon_receive_ipc_data(struct phy_device *phydev, unsigned short len,
			   unsigned short *data);

/**
 * @brief Build IPC cmd.
 * @note Construct the full command word.
 * 16-bit register is laid out as follows:
 * [1 cmd par][4 reserved][5 size][6 opcode]
 */
void aeon_ipc_build_cmd(unsigned short *cmd, short opcode, short size);

/**
 * @brief Wait until IPC status handshake returns DONE or READY.
 * @return Current IPC status.
 */
unsigned short aeon_ipc_wait_cmd_done(struct phy_device *phydev,
				      unsigned long *ns,
				      unsigned short *ret_size);

/**
 * @brief Write bulk data to some memory.
 * @param mem_addr The memory address to write data.
 * @param size Data size.
 */
void aeon_ipc_send_bulk_write(unsigned int mem_addr, unsigned int size,
			      struct phy_device *phydev);

/**
 * @brief Send bulk data.
 * @param bw_type Type of bit-width.
 * @param size Data size.
 */
void aeon_ipc_send_bulk_data(unsigned short bw_type, unsigned short size,
			     void *data, struct phy_device *phydev);

/**
 * @brief Send IPC commands.
 */
void aeon_send_ipc_msg(struct phy_device *phydev, unsigned int len,
		       unsigned short *val, short opcode, short size);

/** @} */

/** @name AEON's public function related to IPC
 * @note These functions could be called individually
 * @{
*/
/** 3 : get snr margin, 4 : get channel skew.
 * @brief Send IPC command to sync parity.
 */
void aeon_ipc_sync_parity(struct phy_device *phydev);

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
 * @brief Configure PCS mode and speed of serdes.
 * @param pcs_mode 1 : 64/66B, 0 : 8B/10B.
 * @param sds_spd 3 : 10G, 2 : 5G, 1 : 2.5G, 0 : 1G.
 */
void aeon_sds_pcs_set_cfg(unsigned short pcs_mode, unsigned short sds_spd,
			  struct phy_device *phydev);

/**
 * @brief Set non-standard PBO mode.
 * @param nstd_pbo 2 bits [max_pbo] [min_pbo]
 */
void aeon_cu_an_enable_aeon_oui(unsigned short nstd_pbo,
				struct phy_device *phydev);

/**
 * @brief Set auto-eee configuration.
 * @param enable 1 : enable auto-eee, 0 : disable auto-eee.
 * @param idle_th idle threshhold
 */
void aeon_auto_eee_cfg(unsigned short enable, unsigned int idle_th,
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
 * @brief Set USXGMII mode.
 */
void aeon_dpc_ra_enable(struct phy_device *phydev);

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
 * @brief Send command to enable/disable packet checker.
 * @param enable Enable/disable packet checker.
 */
void aeon_pkt_chk_cfg(unsigned short enable, struct phy_device *phydev);

/**
 * @brief Set sds_wait_eth configuration.
 * @param sds_wait_eth_delay Delay.
 */
void aeon_sds_wait_eth_cfg(unsigned short sds_wait_eth_delay,
			   struct phy_device *phydev);

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
 * @brief Set ipc command for restarting serdes AN.
 */
void aeon_sds_restart_an(struct phy_device *phydev);

/**
 * @brief Set ipc command for NG test mode.
 */
void aeon_ipc_ng_test_mode(unsigned short test_mode, unsigned short tone,
			   struct phy_device *phydev);

/**
 * @brief Enable NG test mode for specific speed mode.
 */
void aeon_ng_test_mode(unsigned short top_spd, unsigned short test_mode,
		       unsigned short tone, struct phy_device *phydev);

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
void aeon_ipc_cable_diag(unsigned short sub_cmd, unsigned short *data,
			 unsigned short mode, struct phy_device *phydev);

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
 * @brief Clear log.
 */
void aeon_ipc_clear_log(struct phy_device *phydev);

/**
 * @brief Set mac count.
 */
void aeon_ipc_set_mac_cnt(unsigned long long mac_tot_cnt, unsigned long long mac_crc_cnt,
			  struct phy_device *phydev);

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
 * @brief Set Seccond Serdes Equalization.
 */
void aeon_sds2nd_eq_cfg(unsigned short vga, unsigned short slc, unsigned short ctle,
			unsigned short dfe,
			struct phy_device *phydev);

/**
 * @brief Set duplex mode.
 */
void aeon_set_man_duplex(unsigned short duplex, struct phy_device *phydev);

/**
 * @brief Enable/disable AN.
 */
void aeon_cu_an_enable(unsigned short enable, struct phy_device *phydev);

/**
 * @brief Serdes Eye Scan.
 */
void aeon_ipc_eye_scan(unsigned char sds_id, unsigned char grp, unsigned char *rev_buf,
		       struct phy_device *phydev);

/**
 * @brief Disable/enable normal retrain.
 */
void aeon_normal_retrain_cfg(unsigned short enable, struct phy_device *phydev);

/**
 * @brief Set Serdes Tx Fir 3 cursors
 */
void aeon_ipc_sds_txfir(unsigned char sds_id, unsigned char pre, unsigned char main,
			unsigned char post, struct phy_device *phydev);

/**
 * @brief Enable Auto Link Detection.
 */
void aeon_ipc_auto_link_ena(unsigned char enable, struct phy_device *phydev);

/**
 * @brief Enable Auto Link Type Configuration.
 */
void aeon_ipc_auto_link_cfg(unsigned char link_type, struct phy_device *phydev);

/**
 * @brief Set gain.
 */
void aeon_ipc_set_tx_power_lvl(unsigned short gain, struct phy_device *phydev);

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
 * @brief Register configuration for test mode.
 */
void aeon_man_configure(struct phy_device *phydev);

/**
 * @brief Set parallel detection.
 */
void aeon_parallel_det(unsigned char enable, struct phy_device *phydev);
#endif
