/******************************************************************************
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
/* FILE NAME:  as21xx_debugfs.c
 * PURPOSE:
 *    It provides as21xx phy debugfs definations.
 *
 * NOTES:
 *
 */

/************************************************************************
*                  I N C L U D E S
*************************************************************************
*/
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/debugfs.h>
#include <linux/printk.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/phy.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include "../as21xxx.h"
#include "as21xx_debugfs.h"
#include "as21xx_api.h"
#include <linux/timekeeping.h>
#include <linux/timex.h>
#include <linux/string.h>

/************************************************************************
*                  D E F I N E S   &   C O N S T A N T S
*************************************************************************
*/
#define MAX_BUF     64
#define MAX_CMD_LEN 32
#define MAX_ARGS    10

struct parsed_cmd {
	char cmd[MAX_CMD_LEN];
	long args[MAX_ARGS];
	int argc;
};

/************************************************************************
*                  D A T A   D E C L A R A T I O N S
*************************************************************************
*/
const char *IMP_TYPE_STR[CHAN_NUM] = {
	"Null", "Open", "Short", "Load"
};

/************************************************************************
*    STATIC    I N L I N E    F U N C T I O N   D E F I N I T I O N S
*************************************************************************
*/
static int parse_cmd_args(const char *input, struct parsed_cmd *result, int max_args)
{
	char buffer[MAX_BUF];
	char *token, *ptr;
	int count = 0;
	size_t len;

	if (!input || !result || max_args > MAX_ARGS)
		return -EINVAL;

	strncpy(buffer, input, sizeof(buffer));
	buffer[sizeof(buffer) - 1] = '\0';

	// clear '\n'
	len = strlen(buffer);
	if (len > 0 && buffer[len - 1] == '\n')
		buffer[len - 1] = '\0';

	ptr = buffer;

	token = strsep(&ptr, " ");
	while (token && *token == '\0')
		token = strsep(&ptr, " ");

	if (!token)
		return -EINVAL;

	strncpy(result->cmd, token, sizeof(result->cmd) - 1);
	result->cmd[sizeof(result->cmd) - 1] = '\0';

	while ((token = strsep(&ptr, " ")) != NULL && count < max_args) {
		if (*token == '\0')
			continue;

		if (kstrtol(token, 0, &result->args[count]) != 0)
			return -EINVAL;

		count++;
	}

	result->argc = count;
	return 0;
}

static inline void printk_force_speed_usage(void)
{
	pr_info("================Please input:===================\n");
	pr_info("Set 10G FD: echo 10Gbps > /sys/kernel/debug/{MDIOBUS}/aeon_set_speed_mode\n");
	pr_info("Set 5G FD: echo 5Gbps > /sys/kernel/debug/{MDIOBUS}/aeon_set_speed_mode\n");
	pr_info("Set 2.5G FD: echo 2.5Gbps > /sys/kernel/debug/{MDIOBUS}/aeon_set_speed_mode\n");
	pr_info("Set 1G FD: echo 1Gbps   > /sys/kernel/debug/{MDIOBUS}/aeon_set_speed_mode\n");
	pr_info("Set 100M FD: echo 100Mbps > /sys/kernel/debug/{MDIOBUS}/aeon_set_speed_mode\n\n");
}

static inline void printk_restart_an_usage(void)
{
	pr_info("================Please input:===================\n");
	pr_info("Restart AN: echo RestartAN > /sys/kernel/debug/{MDIOBUS}/aeon_restart_an\n");
}

static inline void printk_mdi_cfg_usage(void)
{
	pr_info("================Please input:===================\n");
	pr_info("Set FastRetrain: echo FastRetrain [speed] [thp_bypass]> /sys/kernel/debug/{MDIOBUS}/aeon_set_mdi_cfg\n");
	pr_info("Set EEE: echo EEE [speed] > /sys/kernel/debug/{MDIOBUS}/aeon_set_mdi_cfg\n");
	pr_info("Set AeonOUI: echo AeonOUI [pbo_option]> /sys/kernel/debug/{MDIOBUS}/aeon_set_mdi_cfg\n");
	pr_info("Set Manual M/S enable: echo ManualMS [value] > /sys/kernel/debug/{MDIOBUS}/aeon_set_mdi_cfg\n");
	pr_info("Set M/S: echo SetMS [value] > /sys/kernel/debug/{MDIOBUS}/aeon_set_mdi_cfg\n");
	pr_info("Set Port Type: echo PortType [value] > /sys/kernel/debug/{MDIOBUS}/aeon_set_mdi_cfg\n");
	pr_info("Set SmartSpd: echo Smartspd [en] [retry_limit] > /sys/kernel/debug/{MDIOBUS}/aeon_set_mdi_cfg\n");
	pr_info("Set Trd Swap: echo TrdSwap [value] > /sys/kernel/debug/{MDIOBUS}/aeon_set_mdi_cfg\n");
	pr_info("Set CFR: echo CFR [value] > /sys/kernel/debug/{MDIOBUS}/aeon_set_mdi_cfg\n\n");
}

static inline void printk_sds_pcs_cfg_usage(void)
{
	pr_info("================Please input:===================\n");
	pr_info("Set sds pcs cfg: echo SdsPcs [pcsMode] [sdsSpd] > /sys/kernel/debug/{MDIOBUS}/aeon_set_sds_pcs_cfg\n");
	pr_info("Set sds ra enable: echo SdsRA > /sys/kernel/debug/{MDIOBUS}/aeon_set_sds_pcs_cfg\n\n");
}

#ifndef AEON_SEI2
static inline void printk_pkt_chk_cfg_usage(void)
{
	pr_info("================Please input:===================\n");
	pr_info("Set pkt chk: echo PktChk [mode] > /sys/kernel/debug/{MDIOBUS}/aeon_pkt_chk_cfg\n\n");
}

static inline void printk_mdc_timing_usage(void)
{
	pr_info("================Please input:===================\n");
	pr_info("echo MdcTiming [mode] > /sys/kernel/debug/{MDIOBUS}/aeon_mdc_timing\n\n");
}

static inline void printk_auto_eee_cfg_usage(void)
{
	pr_info("================Please input:===================\n");
	pr_info("Set auto-eee: echo AutoEEE [enable] [idle_th] > /sys/kernel/debug/{MDIOBUS}/aeon_auto_eee_cfg\n\n");
}

static inline void printk_sds_wait_eth_cfg_usage(void)
{
	pr_info("================Please input:===================\n");
	pr_info("echo SdsWaitEth [sds_waith_eth] > /sys/kernel/debug/{MDIOBUS}/aeon_sds_wait_eth\n\n");
}

static inline void printk_sds_restart_an_usage(void)
{
	pr_info("================Please input:===================\n");
	pr_info("echo sdsan > /sys/kernel/debug/{MDIOBUS}/aeon_sds_restart_an\n\n");
}

static inline void printk_tx_power_lvl_usage(void)
{
	pr_info("================Please input:===================\n");
	pr_info("echo txgain [val] > /sys/kernel/debug/{MDIOBUS}/aeon_tx_power_lvl\n\n");
}

static inline void printk_sds2nd_cfg_usage(void)
{
	pr_info("================Please input:===================\n");
	pr_info("Enable Second Serdes: echo Sds2ndEn [enable] > /sys/kernel/debug/{MDIOBUS}/aeon_sds2nd_enable\n");
	pr_info("Config Second Serdes Equlization: echo Sds2ndEq [vga] [slc] [ctle] [dfe] > /sys/kernel/debug/{MDIOBUS}/aeon_sds2nd_eq_cfg\n");
	pr_info("Config Second Serdes PCS Mode and Datarate: echo Sds2ndMode [pcsMode] [sdsSpd] > /sys/kernel/debug/{MDIOBUS}/aeon_sds2nd_mode_cfg\n");

}

static inline void printk_sds2nd_eye_diagram_usage(void)
{
	pr_info("================Please input:===================\n");
	pr_info("echo eyescan [part] > /sys/kernel/debug/{MDIOBUS}/aeon_sds2nd_eye_diagram_data\n");
	pr_info("echo eyeshow > /sys/kernel/debug/{MDIOBUS}/aeon_sds2nd_eye_diagram_data\n");
}

static inline void printk_normal_retrain_usage(void)
{
	pr_info("================Please input:===================\n");
	pr_info("echo nr [enable] > /sys/kernel/debug/{MDIOBUS}/aeon_normal_retrain\n\n");
}

static inline void printk_auto_link_usage(void)
{
	pr_info("================Please input:===================\n");
	pr_info("echo AutoLinkEna [enable] > /sys/kernel/debug/{MDIOBUS}/aeon_auto_link\n");
	pr_info("echo AutoLinkCfg [linktype] > /sys/kernel/debug/{MDIOBUS}/aeon_auto_link\n\n");
}

static inline void printk_sds_txfir_usage(void)
{
	pr_info("================Please input:===================\n");
	pr_info("echo SdsTxFir [pre] [main] [post] > /sys/kernel/debug/{MDIOBUS}/aeon_sds_txfir\n\n");
}
#endif

static inline void printk_sds_pma_cfg_usage(void)
{
	pr_info("================Please input:===================\n");
	pr_info("Set sds pma: echo SdsPma [vga_adapt] [ctle_adapt] [dfe_adapt] [slc_adapt]> /sys/kernel/debug/{MDIOBUS}/aeon_set_sds_pma_cfg\n\n");
}

static inline void printk_get_fw_version_usage(void)
{
	pr_info("================Please input:===================\n");
	pr_info("Get fw version: echo ver > /sys/kernel/debug/{MDIOBUS}/aeon_fw_version\n\n");
}

static inline void printk_temp_monitor_usage(void)
{
	pr_info("================Please input:===================\n");
	pr_info("Set temperature monitor: echo temp [subcommand] [param] > /sys/kernel/debug/{MDIOBUS}/aeon_temp_monitor\n\n");
}

static inline void printk_set_led_usage(void)
{
	pr_info("================Please input:===================\n");
	pr_info("Set led: echo led [led0] [led1] [led2] [led3] [led4] [polarity] [blink] > /sys/kernel/debug/{MDIOBUS}/aeon_set_led\n\n");
}

static inline void printk_sys_reboot_usage(void)
{
	pr_info("================Please input:===================\n");
	pr_info("Reboot: echo reboot > /sys/kernel/debug/{MDIOBUS}/aeon_set_sys_reboot\n\n");
}

static inline void printk_read_reg_usage(void)
{
	pr_info("================Please input:===================\n");
	pr_info("echo ReadReg [dev_addr] [phy_reg] > /sys/kernel/debug/{MDIOBUS}/aeon_read_reg\n\n");
}

static inline void printk_write_reg_usage(void)
{
	pr_info("================Please input:===================\n");
	pr_info("echo WriteReg [dev_addr] [phy_reg] [value] > /sys/kernel/debug/{MDIOBUS}/aeon_write_reg\n\n");
}

static inline void printk_eth_status_usage(void)
{
	pr_info("================Please input:===================\n");
	pr_info("echo ethstatus > /sys/kernel/debug/{MDIOBUS}/aeon_get_eth_status\n\n");
}

static inline void printk_phy_enable_usage(void)
{
	pr_info("================Please input:===================\n");
	pr_info("echo phyenable [enable] > /sys/kernel/debug/{MDIOBUS}/aeon_phy_enable\n\n");
}

#ifdef DUAL_FLASH
static inline void printk_sys_dual_flash(void)
{
	pr_info("================Please input:===================\n");
	pr_info("echo flash [include_bootloader] > /sys/kernel/debug/{MDIOBUS}/aeon_burn_flash_image\n\n");
}

static inline void printk_erase_flash(void)
{
	pr_info("================Please input:===================\n");
	pr_info("echo erase [flash_addr] [size] > /sys/kernel/debug/{MDIOBUS}/aeon_erase_flash\n\n");
}
#endif

static inline void printk_test_mode_usage(void)
{
	pr_info("================Please input:===================\n");
	pr_info("Set NG testmode: echo ngtest [speed] [test_mode] [test_tone(if test_mode == 4)] > /sys/kernel/debug/{MDIOBUS}/aeon_test_mode\n");
	pr_info("Set 1G testmode: echo 1gtest [test_mode] > /sys/kernel/debug/{MDIOBUS}/aeon_test_mode\n");
	pr_info("Set 100M testmode: echo 100mtest > /sys/kernel/debug/{MDIOBUS}/aeon_test_mode\n\n");
}

static inline void printk_tx_fullscale_usage(void)
{
	pr_info("================Please input:===================\n");
	pr_info("echo txfullscale [speed] [val0 val1 val2 val3] > /sys/kernel/debug/{MDIOBUS}/aeon_tx_fullscale\n");
	pr_info("echo get [speed] > /sys/kernel/debug/{MDIOBUS}/aeon_tx_fullscale\n\n");
}

static inline void printk_wol_ctrl_usage(void)
{
	pr_info("================Please input:===================\n");
	pr_info("echo wolctrl [value0] [value1] [value2] [value3]> /sys/kernel/debug/{MDIOBUS}/aeon_wol_ctrl\n\n");
}

static inline void printk_smi_ctrl_usage(void)
{
	pr_info("================Please input:===================\n");
	pr_info("echo smicommand [value0] [value1]> /sys/kernel/debug/{MDIOBUS}/aeon_smi_command\n\n");
}

static inline void printk_set_irq_en_usage(void)
{
	pr_info("================Please input:===================\n");
	pr_info("echo setirqen [value0] [value1] [value2] [value3] > /sys/kernel/debug/{MDIOBUS}/aeon_setirq_en\n\n");
}

static inline void printk_set_irq_clr_usage(void)
{
	pr_info("================Please input:===================\n");
	pr_info("echo setirqclr [value0] > /sys/kernel/debug/{MDIOBUS}/aeon_setirq_clr\n\n");
}

static inline void printk_query_irq_status_usage(void)
{
	pr_info("================Please input:===================\n");
	pr_info("echo queryirq > /sys/kernel/debug/{MDIOBUS}/aeon_query_irq\n\n");
}

static inline void printk_cable_diag_usage(void)
{
	pr_info("================Please input:===================\n");
	pr_info("echo chanlen > /sys/kernel/debug/{MDIOBUS}/aeon_cable_diag\n");
	pr_info("echo ppmofst > /sys/kernel/debug/{MDIOBUS}/aeon_cable_diag\n");
	pr_info("echo snrmargin > /sys/kernel/debug/{MDIOBUS}/aeon_cable_diag\n");
	pr_info("echo chanskew > /sys/kernel/debug/{MDIOBUS}/aeon_cable_diag\n");
	pr_info("echo set [mode] > /sys/kernel/debug/{MDIOBUS}/aeon_cable_diag\n");
	pr_info("echo get > /sys/kernel/debug/{MDIOBUS}/aeon_cable_diag\n\n");
}

static inline void printk_sds_eye_diagram_usage(void)
{
	pr_info("================Please input:===================\n");
	pr_info("echo eyescan [part] > /sys/kernel/debug/{MDIOBUS}/aeon_eye_diagram_data\n");
	pr_info("echo eyeshow > /sys/kernel/debug/{MDIOBUS}/aeon_eye_diagram_data\n");
}

static inline void printk_force_mode_usage(void)
{
	pr_info("================Please input:===================\n");
	pr_info("echo force100M [duplex] > /sys/kernel/debug/{MDIOBUS}/aeon_force_mode\n\n");
}

static inline void printk_parallel_det_usage(void)
{
	pr_info("================Please input:===================\n");
	pr_info("echo paradet [enable] > /proc/tc3162/aeon_parallel_det\n\n");
}

static inline void printk_force_mdi_mode_usage(void)
{
	pr_info("================Please input:===================\n");
	pr_info("echo mdi [val] > /sys/kernel/debug/{MDIOBUS}/aeon_force_mdi_mode\n\n");
}

static inline void printk_synce_master_mode_usage(void)
{
	pr_info("================Please input:===================\n");
	pr_info("echo synce_master [enable] [bw] > /sys/kernel/debug/{MDIOBUS}/aeon_synce_master_mode\n\n");
}

static inline void printk_synce_slave_mode1_usage(void)
{
	pr_info("================Please input:===================\n");
	pr_info("echo synce_slave_mode1 [enable] [oc] > /sys/kernel/debug/{MDIOBUS}/aeon_synce_slave_mode1\n\n");
}

static inline void printk_synce_slave_mode2_usage(void)
{
	pr_info("================Please input:===================\n");
	pr_info("echo synce_slave_mode2 [enable] [bw] [oc] > /sys/kernel/debug/{MDIOBUS}/aeon_synce_slave_mode2\n\n");
}

#ifndef AEON_SEI2
static ssize_t aeon_pkt_chk_cfg_read_proc(struct file *file, char __user *buf,
					  size_t size, loff_t *ppos)
{
	printk_pkt_chk_cfg_usage();
	return 0;
}

static ssize_t aeon_pkt_chk_cfg_write_proc(struct file *file,
					   const char __user *buffer, size_t count,
					   loff_t *pos)
{
	struct phy_device *phydev = file->private_data;
	char val_string[MAX_BUF] = { 0 };
	struct parsed_cmd cmdinfo = { 0 };

	if (count >= sizeof(val_string))
		return -EINVAL;

	if (copy_from_user(val_string, buffer, count))
		return -EFAULT;

	val_string[count] = '\0';

	if (parse_cmd_args(val_string, &cmdinfo, 1) != 0)
		return -EINVAL;

	if (!strcmp(cmdinfo.cmd, "PktChk")) {
		if (cmdinfo.argc != 1)
			return -EINVAL;

		aeon_ipc_sync_parity(phydev);
		aeon_pkt_chk_cfg(cmdinfo.args[0], phydev);
		pr_info("Set Pkt Checker successfully!\n");
	} else
		printk_pkt_chk_cfg_usage();

	return count;
}

static ssize_t aeon_mdc_timing_read_proc(struct file *file, char __user *buf,
					 size_t size, loff_t *ppos)
{
	printk_mdc_timing_usage();
	return 0;
}

static ssize_t aeon_mdc_timing_write_proc(struct file *file, const char __user *buffer,
					  size_t count, loff_t *pos)
{
	struct phy_device *phydev = file->private_data;
	char val_string[MAX_BUF] = { 0 };
	struct parsed_cmd cmdinfo = { 0 };

	if (count >= sizeof(val_string))
		return -EINVAL;

	if (copy_from_user(val_string, buffer, count))
		return -EFAULT;

	val_string[count] = '\0';

	if (parse_cmd_args(val_string, &cmdinfo, 1) != 0)
		return -EINVAL;

	if (!strcmp(cmdinfo.cmd, "MdcTiming")) {
		if (cmdinfo.argc != 1)
			return -EINVAL;

		if (cmdinfo.args[0] == 1) {
			phy_write_mmd(phydev, 0x1E, 0x53, 0xFFFF);
			phy_write_mmd(phydev, 0x1E, 0x54, 0xFFFF);
			phy_write_mmd(phydev, 0x1E, 0x55, 0xFFFF);
		} else {
			phy_write_mmd(phydev, 0x1E, 0x53, 0x0);
			phy_write_mmd(phydev, 0x1E, 0x54, 0x0);
			phy_write_mmd(phydev, 0x1E, 0x55, 0x0);
		}
		pr_info("Set MDC timing successfully!\n");
	} else
		printk_mdc_timing_usage();

	return count;
}

static ssize_t aeon_auto_eee_cfg_read_proc(struct file *file, char __user *buf,
					   size_t size, loff_t *ppos)
{
	printk_auto_eee_cfg_usage();
	return 0;
}

static ssize_t aeon_auto_eee_cfg_write_proc(struct file *file,
					    const char __user *buffer, size_t count,
					    loff_t *pos)
{
	struct phy_device *phydev = file->private_data;
	char val_string[MAX_BUF] = { 0 };
	struct parsed_cmd cmdinfo = { 0 };

	if (count >= sizeof(val_string))
		return -EINVAL;

	if (copy_from_user(val_string, buffer, count))
		return -EFAULT;

	val_string[count] = '\0';

	if (parse_cmd_args(val_string, &cmdinfo, 2) != 0)
		return -EINVAL;

	if (!strcmp(cmdinfo.cmd, "AutoEEE")) {
		if (cmdinfo.argc != 2)
			return -EINVAL;

		aeon_ipc_sync_parity(phydev);
		aeon_auto_eee_cfg(cmdinfo.args[0], cmdinfo.args[1], phydev);
		pr_info("Set Auto EEE successfully!\n");
	} else
		printk_auto_eee_cfg_usage();

	return count;
}

static ssize_t aeon_sds_wait_eth_cfg_read_proc(struct file *file, char __user *buf,
					       size_t size, loff_t *ppos)
{
	printk_sds_wait_eth_cfg_usage();
	return 0;
}

static ssize_t aeon_sds_wait_eth_cfg_write_proc(struct file *file,
						const char __user *buffer,
						size_t count, loff_t *pos)
{
	struct phy_device *phydev = file->private_data;
	char val_string[MAX_BUF] = { 0 };
	struct parsed_cmd cmdinfo = { 0 };

	if (count >= sizeof(val_string))
		return -EINVAL;

	if (copy_from_user(val_string, buffer, count))
		return -EFAULT;

	val_string[count] = '\0';

	if (parse_cmd_args(val_string, &cmdinfo, 1) != 0)
		return -EINVAL;

	if (!strcmp(cmdinfo.cmd, "SdsWaitEth")) {
		if (cmdinfo.argc != 1)
			return -EINVAL;

		aeon_ipc_sync_parity(phydev);
		aeon_sds_wait_eth_cfg(cmdinfo.args[0], phydev);
		pr_info("Set Sds wait Eth cfg successfully!\n");
	} else
		printk_sds_wait_eth_cfg_usage();

	return count;
}

static ssize_t aeon_sds_restart_an_read_proc(struct file *file, char __user *buf,
					     size_t size, loff_t *ppos)
{
	printk_sds_restart_an_usage();

	return 0;
}

static ssize_t aeon_sds_restart_an_write_proc(struct file *file,
					      const char __user *buffer, size_t count,
					      loff_t *pos)
{
	struct phy_device *phydev = file->private_data;
	char val_string[MAX_BUF] = { 0 };
	struct parsed_cmd cmdinfo = { 0 };

	if (count >= sizeof(val_string))
		return -EINVAL;

	if (copy_from_user(val_string, buffer, count))
		return -EFAULT;

	val_string[count] = '\0';

	if (parse_cmd_args(val_string, &cmdinfo, 0) != 0)
		return -EINVAL;

	if (!strcmp(cmdinfo.cmd, "sdsan")) {
		if (cmdinfo.argc != 0)
			return -EINVAL;

		aeon_ipc_sync_parity(phydev);
		aeon_sds_restart_an(phydev);
		pr_info("Restart sds AN successfully!\n");
	} else
		printk_sds_restart_an_usage();

	return count;
}

static ssize_t aeon_tx_power_lvl_read_proc(struct file *file, char __user *buffer,
					   size_t count, loff_t *pos)
{
	printk_tx_power_lvl_usage();

	return 0;
}

static ssize_t aeon_tx_power_lvl_write_proc(struct file *file, const char __user *buffer,
					    size_t count, loff_t *pos)
{
	struct phy_device *phydev = file->private_data;
	char val_string[MAX_BUF] = { 0 };
	struct parsed_cmd cmdinfo = { 0 };

	if (count >= sizeof(val_string))
		return -EINVAL;

	if (copy_from_user(val_string, buffer, count))
		return -EFAULT;

	val_string[count] = '\0';

	if (parse_cmd_args(val_string, &cmdinfo, 1) != 0)
		return -EINVAL;

	if (!strcmp(cmdinfo.cmd, "txgain")) {
		if (cmdinfo.argc != 1)
			return -EINVAL;

		aeon_ipc_sync_parity(phydev);
		aeon_ipc_set_tx_power_lvl(cmdinfo.args[0], phydev);
		pr_info("Set TX power level successfully!\n");
	} else
		printk_tx_power_lvl_usage();

	return count;
}

static ssize_t aeon_sds2nd_enable_read_proc(struct file *file, char __user *buffer,
					    size_t count, loff_t *pos)
{
	printk_sds2nd_cfg_usage();

	return 0;
}

static ssize_t aeon_sds2nd_enable_write_proc(struct file *file, const char __user *buffer,
					     size_t count, loff_t *pos)
{
	struct phy_device *phydev = file->private_data;
	char val_string[MAX_BUF] = { 0 };
	struct parsed_cmd cmdinfo = { 0 };

	if (count >= sizeof(val_string))
		return -EINVAL;

	if (copy_from_user(val_string, buffer, count))
		return -EFAULT;

	val_string[count] = '\0';

	if (parse_cmd_args(val_string, &cmdinfo, 1) != 0)
		return -EINVAL;

	if (!strcmp(cmdinfo.cmd, "Sds2ndEn")) {
		if (cmdinfo.argc != 1)
			return -EINVAL;

		aeon_ipc_sync_parity(phydev);
		aeon_sds2nd_enable(cmdinfo.args[0], phydev);
	} else
		printk_sds2nd_cfg_usage();

	return count;
}

static ssize_t aeon_sds2nd_eq_read_proc(struct file *file, char __user *buffer,
					size_t count, loff_t *pos)
{
	printk_sds2nd_cfg_usage();
	return 0;
}

static ssize_t aeon_sds2nd_eq_write_proc(struct file *file, const char __user *buffer,
					 size_t count, loff_t *pos)
{
	struct phy_device *phydev = file->private_data;
	char val_string[MAX_BUF] = { 0 };
	struct parsed_cmd cmdinfo = { 0 };

	if (count >= sizeof(val_string))
		return -EINVAL;

	if (copy_from_user(val_string, buffer, count))
		return -EFAULT;

	val_string[count] = '\0';

	if (parse_cmd_args(val_string, &cmdinfo, 4) != 0)
		return -EINVAL;

	if (!strcmp(cmdinfo.cmd, "Sds2ndEq")) {
		if (cmdinfo.argc != 4)
			return -EINVAL;

		aeon_ipc_sync_parity(phydev);
		aeon_sds2nd_eq_cfg(cmdinfo.args[0], cmdinfo.args[1], cmdinfo.args[2],
				   cmdinfo.args[3], phydev);
	} else
		printk_sds2nd_cfg_usage();

	return count;
}

static ssize_t aeon_sds2nd_mode_read_proc(struct file *file, char __user *buffer,
					  size_t count, loff_t *pos)
{
	printk_sds2nd_cfg_usage();
	return 0;
}

static ssize_t aeon_sds2nd_mode_write_proc(struct file *file, const char __user *buffer,
					   size_t count, loff_t *pos)
{
	struct phy_device *phydev = file->private_data;
	char val_string[MAX_BUF] = { 0 };
	struct parsed_cmd cmdinfo = { 0 };

	if (count >= sizeof(val_string))
		return -EINVAL;

	if (copy_from_user(val_string, buffer, count))
		return -EFAULT;

	val_string[count] = '\0';

	if (parse_cmd_args(val_string, &cmdinfo, 2) != 0)
		return -EINVAL;

	if (!strcmp(cmdinfo.cmd, "Sds2ndMode")) {
		if (cmdinfo.argc != 2)
			return -EINVAL;

		aeon_ipc_sync_parity(phydev);
		aeon_sds2nd_mode_cfg(cmdinfo.args[0], cmdinfo.args[1], 0, phydev);
	} else
		printk_sds2nd_cfg_usage();

	return count;
}

static ssize_t aeon_sds2nd_eye_diagram_read_proc(struct file *file, char __user *buffer,
						 size_t count, loff_t *pos)
{
	printk_sds2nd_eye_diagram_usage();
	return 0;
}

static ssize_t aeon_sds2nd_eye_diagram_write_proc(struct file *file, const char __user *buffer,
						  size_t count, loff_t *pos)
{
	struct phy_device *phydev = file->private_data;
	struct as21xxx_priv *priv = phydev->priv;
	unsigned char *raw_eye_data = priv->raw_eye_data;
	char val_string[MAX_BUF] = { 0 };
	struct parsed_cmd cmdinfo = { 0 };
	unsigned short ii, grp, part;

	if (count >= sizeof(val_string))
		return -EINVAL;

	if (copy_from_user(val_string, buffer, count))
		return -EFAULT;

	val_string[count] = '\0';

	if (parse_cmd_args(val_string, &cmdinfo, 1) != 0)
		return -EINVAL;

	if (!strcmp(cmdinfo.cmd, "eyescan")) {
		part = cmdinfo.args[0];
		if (part == EYE_PART_0) {
			for (grp = 0; grp < EYE_PART_0_GRPS ; ++grp) {
				aeon_ipc_sync_parity(phydev);
				aeon_ipc_eye_scan(1, grp, &raw_eye_data[grp * EYE_STRIDE], phydev);
			}
		} else if (part == EYE_PART_1) {
			for (grp = EYE_PART_0_GRPS; grp < EYE_GRPS ; ++grp) {
				aeon_ipc_sync_parity(phydev);
				aeon_ipc_eye_scan(1, grp, &raw_eye_data[grp * EYE_STRIDE], phydev);
			}
		} else {
			return -EINVAL;
		}
	} else if (!strcmp(cmdinfo.cmd, "eyeshow")) {
		pr_info("RAW EYE data:\n");
		for (ii = 0; ii < EYE_TOTAL_BYTES; ++ii) {
			if (ii % 16 == 0)
				pr_info("\n");
			pr_info("0x%02x ", raw_eye_data[ii]);
		}
		pr_info("\n");

		memset(raw_eye_data, 0, EYE_TOTAL_BYTES);
	} else
		printk_sds2nd_eye_diagram_usage();

	return count;
}

static ssize_t aeon_normal_retrain_read_proc(struct file *file, char __user *buffer,
					     size_t count, loff_t *pos)
{
	printk_normal_retrain_usage();
	return 0;
}

static ssize_t aeon_normal_retrain_write_proc(struct file *file, const char __user *buffer,
					      size_t count, loff_t *pos)
{
	struct phy_device *phydev = file->private_data;
	char val_string[MAX_BUF] = { 0 };
	struct parsed_cmd cmdinfo = { 0 };

	if (count >= sizeof(val_string))
		return -EINVAL;

	if (copy_from_user(val_string, buffer, count))
		return -EFAULT;

	val_string[count] = '\0';

	if (parse_cmd_args(val_string, &cmdinfo, 1) != 0)
		return -EINVAL;

	if (!strcmp(cmdinfo.cmd, "nr")) {
		if (cmdinfo.argc != 1)
			return -EINVAL;

		aeon_ipc_sync_parity(phydev);
		aeon_normal_retrain_cfg(cmdinfo.args[0], phydev);
		pr_info("Set normal retrain successfully!\n");
	} else
		printk_normal_retrain_usage();

	return count;
}

static ssize_t aeon_auto_link_read_proc(struct file *file, char __user *buffer,
					size_t count, loff_t *pos)
{
	printk_auto_link_usage();
	return 0;
}

static ssize_t aeon_auto_link_write_proc(struct file *file, const char __user *buffer,
					 size_t count, loff_t *pos)
{
	struct phy_device *phydev = file->private_data;
	char val_string[MAX_BUF] = { 0 };
	struct parsed_cmd cmdinfo = { 0 };
	unsigned char enable = 0;
	unsigned char link_type = 0;

	if (count >= sizeof(val_string))
		return -EINVAL;

	if (copy_from_user(val_string, buffer, count))
		return -EFAULT;

	val_string[count] = '\0';

	if (parse_cmd_args(val_string, &cmdinfo, 1) != 0)
		return -EINVAL;

	if (!strcmp(cmdinfo.cmd, "AutoLinkEna")) {
		if (cmdinfo.argc != 1)
			return -EINVAL;
		enable = cmdinfo.args[0];
		aeon_ipc_sync_parity(phydev);
		aeon_ipc_auto_link_ena(enable, phydev);
	} else if (!strcmp(cmdinfo.cmd, "AutoLinkCfg")) {
		if (cmdinfo.argc != 1)
			return -EINVAL;
		link_type = cmdinfo.args[0];
		aeon_ipc_sync_parity(phydev);
		aeon_ipc_auto_link_cfg(link_type, phydev);
	} else
		printk_auto_link_usage();

	return count;
}

static ssize_t aeon_sds_txfir_read_proc(struct file *file, char __user *buffer,
					size_t count, loff_t *pos)
{
	printk_sds_txfir_usage();
	return 0;
}

static ssize_t aeon_sds_txfir_write_proc(struct file *file, const char __user *buffer,
					 size_t count, loff_t *pos)
{
	struct phy_device *phydev = file->private_data;
	char val_string[MAX_BUF] = { 0 };
	struct parsed_cmd cmdinfo = { 0 };
	unsigned short pre, main, post;

	if (count >= sizeof(val_string))
		return -EINVAL;

	if (copy_from_user(val_string, buffer, count))
		return -EFAULT;

	val_string[count] = '\0';

	if (parse_cmd_args(val_string, &cmdinfo, 3) != 0)
		return -EINVAL;

	if (!strcmp(cmdinfo.cmd, "SdsTxFir")) {
		if (cmdinfo.argc != 3)
			return -EINVAL;
		pre = cmdinfo.args[0];
		main = cmdinfo.args[1];
		post = cmdinfo.args[2];
		aeon_ipc_sync_parity(phydev);
		aeon_ipc_sds_txfir(0, pre, main, post, phydev);
	} else
		printk_sds_txfir_usage();

	return count;
}
#endif

static ssize_t aeon_read_reg_read_proc(struct file *file, char __user *buf,
				       size_t size, loff_t *ppos)
{
	printk_read_reg_usage();
	return 0;
}

static ssize_t aeon_read_reg_write_proc(struct file *file, const char __user *buffer,
					size_t count, loff_t *pos)
{
	struct phy_device *phydev = file->private_data;
	char val_string[MAX_BUF] = { 0 };
	struct parsed_cmd cmdinfo = { 0 };
	unsigned short value = 0;

	if (count >= sizeof(val_string))
		return -EINVAL;

	if (copy_from_user(val_string, buffer, count))
		return -EFAULT;

	val_string[count] = '\0';

	if (parse_cmd_args(val_string, &cmdinfo, 2) != 0)
		return -EINVAL;

	if (!strcmp(cmdinfo.cmd, "ReadReg")) {
		if (cmdinfo.argc != 2)
			return -EINVAL;

		value = aeon_cl45_read(phydev, cmdinfo.args[0], cmdinfo.args[1]);
		pr_info("Read register value: 0x%x\n", value);
	} else
		printk_read_reg_usage();

	return count;
}

static ssize_t aeon_write_reg_read_proc(struct file *file, char __user *buf,
					size_t size, loff_t *ppos)
{
	printk_write_reg_usage();
	return 0;
}

static ssize_t aeon_write_reg_write_proc(struct file *file, const char __user *buffer,
					 size_t count, loff_t *pos)
{
	struct phy_device *phydev = file->private_data;
	char val_string[MAX_BUF] = { 0 };
	struct parsed_cmd cmdinfo = { 0 };

	if (count >= sizeof(val_string))
		return -EINVAL;

	if (copy_from_user(val_string, buffer, count))
		return -EFAULT;

	val_string[count] = '\0';

	if (parse_cmd_args(val_string, &cmdinfo, 3) != 0)
		return -EINVAL;

	if (!strcmp(cmdinfo.cmd, "WriteReg")) {
		if (cmdinfo.argc != 3)
			return -EINVAL;

		phy_write_mmd(phydev, cmdinfo.args[0], cmdinfo.args[1], cmdinfo.args[2]);
		pr_info("Write register successfully!\n");
	} else
		printk_write_reg_usage();

	return count;
}

static ssize_t aeon_eth_status_read_proc(struct file *file, char __user *buf,
					 size_t size, loff_t *ppos)
{
	printk_eth_status_usage();
	return 0;
}

static ssize_t aeon_eth_status_write_proc(struct file *file, const char __user *buffer,
					  size_t count, loff_t *pos)
{
	struct phy_device *phydev = file->private_data;
	char val_string[MAX_BUF] = { 0 };
	struct parsed_cmd cmdinfo = { 0 };
	unsigned short link_status, speed, value;

	if (count >= sizeof(val_string))
		return -EINVAL;

	if (copy_from_user(val_string, buffer, count))
		return -EFAULT;

	val_string[count] = '\0';

	if (parse_cmd_args(val_string, &cmdinfo, 0) != 0)
		return -EINVAL;

	if (!strcmp(cmdinfo.cmd, "ethstatus")) {
		if (cmdinfo.argc != 0)
			return -EINVAL;

		value = aeon_cl45_read(phydev, 0x7, 0x8005);
		link_status = ((value & 0xF000) >> 12) == 9;
		pr_info("Link Status : %d\n", link_status);
		if (link_status) {
			value = aeon_cl45_read(phydev, 0x1E, 0x4002);
			speed = value & 0xFF;
			if (speed == 0x3)
				pr_info("Link up at 10G\n");
			else if (speed == 0x5)
				pr_info("Link up at 5G\n");
			else if (speed == 0x9)
				pr_info("Link up at 2.5G\n");
			else if (speed == 0x10)
				pr_info("Link up at 1G\n");
			else if (speed == 0x20)
				pr_info("Link up at 100M\n");
		}
	} else
		printk_eth_status_usage();

	return count;
}

static ssize_t aeon_restart_an_read_proc(struct file *file, char __user *buf,
					 size_t size, loff_t *ppos)
{
	printk_restart_an_usage();
	return 0;
}

static ssize_t aeon_restart_an_write_proc(struct file *file, const char __user *buffer,
					  size_t count, loff_t *pos)
{
	struct phy_device *phydev = file->private_data;
	struct as21xxx_priv *priv = phydev->priv;
	struct an_mdi_cfg *__priv_data = &priv->mdi_cfg;
	char val_string[MAX_BUF] = { 0 };
	struct parsed_cmd cmdinfo = { 0 };
	unsigned short ms_related_cfg[4] = { 0 };

	if (count >= sizeof(val_string))
		return -EINVAL;

	if (copy_from_user(val_string, buffer, count))
		return -EFAULT;

	val_string[count] = '\0';

	if (parse_cmd_args(val_string, &cmdinfo, 0) != 0)
		return -EINVAL;

	if (!strcmp(cmdinfo.cmd, "RestartAN")) {
		if (cmdinfo.argc != 0)
			return -EINVAL;

		aeon_cu_an_get_ms_cfg(ms_related_cfg, phydev);
		if (__priv_data->top_spd != 0xF)
			aeon_cu_an_set_top_spd(__priv_data->top_spd, phydev);
		if (__priv_data->eee_spd != 0xFF)
			aeon_cu_an_set_eee_spd(__priv_data->eee_spd, phydev);
		if (__priv_data->ms_en != 0xF) {
			ms_related_cfg[1] = __priv_data->ms_en;
			aeon_cu_an_set_ms_cfg(ms_related_cfg[0],
				ms_related_cfg[1], ms_related_cfg[2], phydev);
		}
		if (__priv_data->ms_config != 0xF) {
			ms_related_cfg[2] = __priv_data->ms_config;
			aeon_cu_an_set_ms_cfg(ms_related_cfg[0],
				ms_related_cfg[1], ms_related_cfg[2], phydev);
		}
		if (__priv_data->port_type != 0xF) {
			ms_related_cfg[0] = __priv_data->port_type;
			aeon_cu_an_set_ms_cfg(ms_related_cfg[0],
				ms_related_cfg[1], ms_related_cfg[2], phydev);
		}
		if ((__priv_data->smt_spd.enable != 0xF) ||
		    (__priv_data->smt_spd.retry_limit != 0xF)) {
			aeon_cu_an_enable_downshift(__priv_data->smt_spd.enable,
				__priv_data->smt_spd.retry_limit, phydev);
		}
		if (__priv_data->nstd_pbo != 0xFF)
			aeon_cu_an_enable_aeon_oui(__priv_data->nstd_pbo, phydev);
		if ((__priv_data->trd_swap != 0xF) ||
		    (__priv_data->trd_ovrd != 0xF))
			aeon_cu_an_set_trd_swap(__priv_data->trd_ovrd, __priv_data->trd_swap,
						phydev);
		if (__priv_data->cfr != 0xF)
			aeon_cu_an_set_cfr(__priv_data->cfr, phydev);
		aeon_ipc_sync_parity(phydev);
		aeon_cu_an_restart(phydev);
		pr_info("AN-related CFG finish, restart AN successfully!\n");
	} else
		printk_restart_an_usage();

	return count;
}

static ssize_t aeon_speed_mode_read_proc(struct file *file, char __user *buf,
					 size_t size, loff_t *ppos)
{
	printk_force_speed_usage();
	return 0;
}

static ssize_t aeon_speed_mode_write_proc(struct file *file, const char __user *buffer,
					  size_t count, loff_t *pos)
{
	struct phy_device *phydev = file->private_data;
	struct as21xxx_priv *priv = phydev->priv;
	struct an_mdi_cfg *__priv_data = &priv->mdi_cfg;
	char val_string[MAX_BUF] = { 0 };
	struct parsed_cmd cmdinfo = { 0 };

	if (count >= sizeof(val_string))
		return -EINVAL;

	if (copy_from_user(val_string, buffer, count))
		return -EFAULT;

	val_string[count] = '\0';

	if (parse_cmd_args(val_string, &cmdinfo, 0) != 0)
		return -EINVAL;

	if (cmdinfo.argc != 0)
		return -EINVAL;

	if (!strcmp(cmdinfo.cmd, "10Gbps")) {
		__priv_data->top_spd = MDI_CFG_SPD_T10G;
		pr_info("Set 10Gbps successfully!\n");
	} else if (!strcmp(cmdinfo.cmd, "5Gbps")) {
		__priv_data->top_spd = MDI_CFG_SPD_T5G;
		pr_info("Set 5Gbps successfully!\n");
	} else if (!strcmp(cmdinfo.cmd, "2.5Gbps")) {
		__priv_data->top_spd = MDI_CFG_SPD_T2P5G;
		pr_info("Set 2.5Gbps successfully!\n");
	} else if (!strcmp(cmdinfo.cmd, "1Gbps")) {
		__priv_data->top_spd = MDI_CFG_SPD_T1G;
		pr_info("Set 1Gbps successfully!\n");
	} else if (!strcmp(cmdinfo.cmd, "100Mbps")) {
		__priv_data->top_spd = MDI_CFG_SPD_T100;
		pr_info("Set 100Mbps successfully!\n");
	} else
		printk_force_speed_usage();

	return count;
}

static ssize_t aeon_mdi_cfg_read_proc(struct file *file, char __user *buf, size_t size,
				      loff_t *ppos)
{
	printk_mdi_cfg_usage();
	return 0;
}

static ssize_t aeon_mdi_cfg_write_proc(struct file *file, const char __user *buffer,
				       size_t count, loff_t *pos)
{
	struct phy_device *phydev = file->private_data;
	struct as21xxx_priv *priv = phydev->priv;
	struct an_mdi_cfg *__priv_data = &priv->mdi_cfg;
	char val_string[MAX_BUF] = { 0 };
	struct parsed_cmd cmdinfo = { 0 };

	if (count >= sizeof(val_string))
		return -EINVAL;

	if (copy_from_user(val_string, buffer, count))
		return -EFAULT;

	val_string[count] = '\0';

	if (parse_cmd_args(val_string, &cmdinfo, 2) != 0)
		return -EINVAL;

	if (!strcmp(cmdinfo.cmd, "FastRetrain")) {
		if (cmdinfo.argc != 2)
			return -EINVAL;
		__priv_data->fr_spd = cmdinfo.args[0];
		__priv_data->thp_byp = cmdinfo.args[1];
		aeon_cu_an_set_fast_retrain(__priv_data->fr_spd, __priv_data->thp_byp, phydev);
		pr_info("CFG Fast Retrain successfully!\n");
	} else if (!strcmp(cmdinfo.cmd, "EEE")) {
		if (cmdinfo.argc != 1)
			return -EINVAL;
		__priv_data->eee_spd = cmdinfo.args[0];
		pr_info("CFG EEE successfully!\n");
	} else if (!strcmp(cmdinfo.cmd, "ManualMS")) {
		if (cmdinfo.argc != 1)
			return -EINVAL;
		__priv_data->ms_en = cmdinfo.args[0];
		pr_info("CFG Manual M/S enable successfully!\n");
	} else if (!strcmp(cmdinfo.cmd, "SetMS")) {
		if (cmdinfo.argc != 1)
			return -EINVAL;
		__priv_data->ms_config = cmdinfo.args[0];
		pr_info("CFG M/S successfully!\n");
	} else if (!strcmp(cmdinfo.cmd, "PortType")) {
		if (cmdinfo.argc != 1)
			return -EINVAL;
		__priv_data->port_type = cmdinfo.args[0];
		pr_info("CFG Port Type successfully!\n");
	} else if (!strcmp(cmdinfo.cmd, "Smartspd")) {
		if (cmdinfo.argc != 2)
			return -EINVAL;
		__priv_data->smt_spd.enable = cmdinfo.args[0];
		__priv_data->smt_spd.retry_limit = cmdinfo.args[1];
		pr_info("CFG Smartspd successfully!\n");
	} else if (!strcmp(cmdinfo.cmd, "AeonOUI")) {
		if (cmdinfo.argc != 1)
			return -EINVAL;
		__priv_data->nstd_pbo = cmdinfo.args[0];
		pr_info("CFG aeon oui successfully!\n");
	} else if (!strcmp(cmdinfo.cmd, "TrdSwap")) {
		if (cmdinfo.argc != 2)
			return -EINVAL;
		__priv_data->trd_ovrd = cmdinfo.args[0];
		__priv_data->trd_swap = cmdinfo.args[1];
		pr_info("CFG Trd Swap successfully!\n");
	} else if (!strcmp(cmdinfo.cmd, "CFR")) {
		if (cmdinfo.argc != 1)
			return -EINVAL;
		__priv_data->cfr = cmdinfo.args[0];
		pr_info("CFG CFR successfully!\n");
	} else
		printk_mdi_cfg_usage();

	return count;
}

static ssize_t aeon_sds_pcs_cfg_read_proc(struct file *file, char __user *buf,
					  size_t size, loff_t *ppos)
{
	printk_sds_pcs_cfg_usage();
	return 0;
}

static ssize_t aeon_sds_pcs_cfg_write_proc(struct file *file,
					   const char __user *buffer, size_t count,
					   loff_t *pos)
{
	struct phy_device *phydev = file->private_data;
	char val_string[MAX_BUF] = { 0 };
	struct parsed_cmd cmdinfo = { 0 };

	if (count >= sizeof(val_string))
		return -EINVAL;

	if (copy_from_user(val_string, buffer, count))
		return -EFAULT;

	val_string[count] = '\0';

	if (parse_cmd_args(val_string, &cmdinfo, 2) != 0)
		return -EINVAL;

	if (!strcmp(cmdinfo.cmd, "SdsRA")) {
		if (cmdinfo.argc != 0)
			return -EINVAL;

		aeon_ipc_sync_parity(phydev);
		aeon_dpc_ra_enable(phydev);
		pr_info("Set Sds Rate Adaptation successfully!\n");
	} else if (!strcmp(cmdinfo.cmd, "SdsPcs")) {
		if (cmdinfo.argc != 2)
			return -EINVAL;

		aeon_ipc_sync_parity(phydev);
		aeon_sds_pcs_set_cfg(cmdinfo.args[0], cmdinfo.args[1], phydev);
		pr_info("Set Sds Pcs successfully!\n");
	} else
		printk_sds_pcs_cfg_usage();

	return count;
}

static ssize_t aeon_phy_enable_read_proc(struct file *file, char __user *buf,
					 size_t size, loff_t *ppos)
{
	printk_phy_enable_usage();
	return 0;
}

static ssize_t aeon_phy_enable_write_proc(struct file *file, const char __user *buffer,
					  size_t count, loff_t *pos)
{
	struct phy_device *phydev = file->private_data;
	char val_string[MAX_BUF] = { 0 };
	struct parsed_cmd cmdinfo = { 0 };

	if (count >= sizeof(val_string))
		return -EINVAL;

	if (copy_from_user(val_string, buffer, count))
		return -EFAULT;

	val_string[count] = '\0';

	if (parse_cmd_args(val_string, &cmdinfo, 1) != 0)
		return -EINVAL;

	if (!strcmp(cmdinfo.cmd, "phyenable")) {
		if (cmdinfo.argc != 1)
			return -EINVAL;

		aeon_ipc_sync_parity(phydev);
		aeon_ipc_phy_enable_mode(cmdinfo.args[0], phydev);
		pr_info("Set phy successfully!\n");
	} else
		printk_phy_enable_usage();

	return count;
}

static ssize_t aeon_set_led_read_proc(struct file *file, char __user *buf, size_t size,
				      loff_t *ppos)
{
	printk_set_led_usage();
	return 0;
}

static ssize_t aeon_set_led_write_proc(struct file *file, const char __user *buffer,
				       size_t count, loff_t *pos)
{
	struct phy_device *phydev = file->private_data;
	char val_string[MAX_BUF] = { 0 };
	struct parsed_cmd cmdinfo = { 0 };

	if (count >= sizeof(val_string))
		return -EINVAL;

	if (copy_from_user(val_string, buffer, count))
		return -EFAULT;

	val_string[count] = '\0';

	if (parse_cmd_args(val_string, &cmdinfo, 7) != 0)
		return -EINVAL;

	if (!strcmp(cmdinfo.cmd, "led")) {
		if (cmdinfo.argc != 7)
			return -EINVAL;

		aeon_ipc_sync_parity(phydev);
		aeon_ipc_set_led_cfg(cmdinfo.args[0], cmdinfo.args[1], cmdinfo.args[2],
				     cmdinfo.args[3], cmdinfo.args[4], cmdinfo.args[5],
				     cmdinfo.args[6], phydev);
		pr_info("Set LED successfully!\n");
	} else
		printk_set_led_usage();

	return count;
}

static ssize_t aeon_sds_pma_cfg_read_proc(struct file *file, char __user *buf,
					  size_t size, loff_t *ppos)
{
	printk_sds_pma_cfg_usage();
	return 0;
}

static ssize_t aeon_sds_pma_cfg_write_proc(struct file *file,
					   const char __user *buffer, size_t count,
					   loff_t *pos)
{
	struct phy_device *phydev = file->private_data;
	char val_string[MAX_BUF] = { 0 };
	struct parsed_cmd cmdinfo = { 0 };

	if (count >= sizeof(val_string))
		return -EINVAL;

	if (copy_from_user(val_string, buffer, count))
		return -EFAULT;

	val_string[count] = '\0';

	if (parse_cmd_args(val_string, &cmdinfo, 4) != 0)
		return -EINVAL;

	if (!strcmp(cmdinfo.cmd, "SdsPma")) {
		if (cmdinfo.argc != 4)
			return -EINVAL;

		aeon_ipc_sync_parity(phydev);
		aeon_sds_pma_set_cfg(cmdinfo.args[0], cmdinfo.args[1], cmdinfo.args[2],
				     cmdinfo.args[3], phydev);
		pr_info("Set Sds PMA successfully!\n");
	} else
		printk_sds_pma_cfg_usage();

	return count;
}

static ssize_t aeon_fw_version_read_proc(struct file *file, char __user *buf,
					 size_t size, loff_t *ppos)
{
	printk_get_fw_version_usage();
	return 0;
}

static ssize_t aeon_fw_version_write_proc(struct file *file, const char __user *buffer,
					  size_t count, loff_t *pos)
{
	struct phy_device *phydev = file->private_data;
	char val_string[MAX_BUF] = { 0 }, version1[16] = { 0 };
	struct parsed_cmd cmdinfo = { 0 };

	if (count >= sizeof(val_string))
		return -EINVAL;

	if (copy_from_user(val_string, buffer, count))
		return -EFAULT;

	val_string[count] = '\0';

	if (parse_cmd_args(val_string, &cmdinfo, 0) != 0)
		return -EINVAL;

	if (!strcmp(cmdinfo.cmd, "ver")) {
		if (cmdinfo.argc != 0)
			return -EINVAL;

		aeon_ipc_sync_parity(phydev);
		aeon_ipc_get_fw_version(version1, phydev);
		pr_info("Get FW version : %32s\n", version1);
	} else
		printk_get_fw_version_usage();

	return count;
}

static ssize_t aeon_temp_monitor_read_proc(struct file *file, char __user *buf,
					   size_t size, loff_t *ppos)
{
	printk_temp_monitor_usage();
	return 0;
}

static ssize_t aeon_temp_monitor_write_proc(struct file *file,
					    const char __user *buffer, size_t count,
					    loff_t *pos)
{
	struct phy_device *phydev = file->private_data;
	char val_string[MAX_BUF] = { 0 };
	struct parsed_cmd cmdinfo = { 0 };
	unsigned short tempnow[16] = { 0 }, temperature = 0;
	unsigned int params;

	if (count >= sizeof(val_string))
		return -EINVAL;

	if (copy_from_user(val_string, buffer, count))
		return -EFAULT;

	val_string[count] = '\0';

	if (parse_cmd_args(val_string, &cmdinfo, 2) != 0)
		return -EINVAL;

	if (!strcmp(cmdinfo.cmd, "temp")) {
		aeon_ipc_sync_parity(phydev);
		aeon_ipc_temp_monitor(cmdinfo.args[0], cmdinfo.args[1], tempnow, phydev);
		pr_info("Set temperature monitor successfully!\n");
		if (cmdinfo.args[0] == 0x4) {
			params = (unsigned long)(tempnow[1] | (tempnow[2] << 16));
			temperature = params / 65536;
			if (temperature > 32768)
				temperature = temperature - 1 - 0xFFFF;
			pr_info("Get temperature : %u celsius\n", temperature);
		}
	} else
		printk_temp_monitor_usage();

	return count;
}

static ssize_t aeon_sys_reboot_read_proc(struct file *file, char __user *buf,
					 size_t size, loff_t *ppos)
{
	printk_sys_reboot_usage();
	return 0;
}

static ssize_t aeon_sys_reboot_write_proc(struct file *file, const char __user *buffer,
					  size_t count, loff_t *pos)
{
	struct phy_device *phydev = file->private_data;
	char val_string[MAX_BUF] = { 0 };
	struct parsed_cmd cmdinfo = { 0 };

	if (count >= sizeof(val_string))
		return -EINVAL;

	if (copy_from_user(val_string, buffer, count))
		return -EFAULT;

	val_string[count] = '\0';

	if (parse_cmd_args(val_string, &cmdinfo, 0) != 0)
		return -EINVAL;

	if (!strcmp(cmdinfo.cmd, "reboot")) {
		if (cmdinfo.argc != 0)
			return -EINVAL;

		aeon_ipc_sync_parity(phydev);
		aeon_ipc_set_sys_reboot(phydev);
		pr_info("Reboot successfully!\n");
	} else
		printk_sys_reboot_usage();

	return count;
}

#ifdef DUAL_FLASH
static ssize_t aeon_burn_flash_read_proc(struct file *file, char __user *buf,
					 size_t size, loff_t *ppos)
{
	printk_sys_dual_flash();
	return 0;
}

static ssize_t aeon_burn_flash_write_proc(struct file *file, const char __user *buffer,
					  size_t count, loff_t *pos)
{
	struct phy_device *phydev = file->private_data;
	char val_string[MAX_BUF] = { 0 };
	struct parsed_cmd cmdinfo = { 0 };

	if (count >= sizeof(val_string))
		return -EINVAL;

	if (copy_from_user(val_string, buffer, count))
		return -EFAULT;

	val_string[count] = '\0';

	if (parse_cmd_args(val_string, &cmdinfo, 1) != 0)
		return -EINVAL;

	if (!strcmp(cmdinfo.cmd, "flash")) {
		if (cmdinfo.argc != 1)
			return -EINVAL;

		aeon_ipc_sync_parity(phydev);
		aeon_burn_image(cmdinfo.args[0], phydev);
		pr_info("Set flash_burning successfully!\n");
	} else
		printk_sys_dual_flash();

	return count;
}

static ssize_t aeon_erase_flash_read_proc(struct file *file, char __user *buf,
					  size_t size, loff_t *ppos)
{
	printk_erase_flash();
	return 0;
}

static ssize_t aeon_erase_flash_write_proc(struct file *file,
					   const char __user *buffer, size_t count,
					   loff_t *pos)
{
	struct phy_device *phydev = file->private_data;
	char val_string[MAX_BUF] = { 0 };
	struct parsed_cmd cmdinfo = { 0 };

	if (count >= sizeof(val_string))
		return -EINVAL;

	if (copy_from_user(val_string, buffer, count))
		return -EFAULT;

	val_string[count] = '\0';

	if (parse_cmd_args(val_string, &cmdinfo, 2) != 0)
		return -EINVAL;

	if (!strcmp(cmdinfo.cmd, "erase")) {
		if (cmdinfo.argc != 2)
			return -EINVAL;

		aeon_ipc_sync_parity(phydev);
		aeon_ipc_erase_flash(cmdinfo.args[0], cmdinfo.args[1], 2, phydev);
		pr_info("Erase flash successfully!\n");
	} else
		printk_erase_flash();

	return count;
}
#endif

static ssize_t aeon_test_mode_read_proc(struct file *file, char __user *buf,
					size_t size, loff_t *ppos)
{
	printk_test_mode_usage();
	return 0;
}

static ssize_t aeon_test_mode_write_proc(struct file *file, const char __user *buffer,
					 size_t count, loff_t *pos)
{
	struct phy_device *phydev = file->private_data;
	char val_string[MAX_BUF] = { 0 };
	struct parsed_cmd cmdinfo = { 0 };
	unsigned short input1;

	if (count >= sizeof(val_string))
		return -EINVAL;

	if (copy_from_user(val_string, buffer, count))
		return -EFAULT;

	val_string[count] = '\0';

	if (parse_cmd_args(val_string, &cmdinfo, 3) != 0)
		return -EINVAL;

	if (!strcmp(cmdinfo.cmd, "100mtest")) {
		if (cmdinfo.argc != 0)
			return -EINVAL;
		aeon_ipc_sync_parity(phydev);
		aeon_100m_test_mode(phydev);
		pr_info("Set 100M test mode successfully!\n");
	} else if (!strcmp(cmdinfo.cmd, "1gtest")) {
		if (cmdinfo.argc != 1)
			return -EINVAL;
		aeon_ipc_sync_parity(phydev);
		aeon_1g_test_mode(cmdinfo.args[0], phydev);
		pr_info("Set 1G test mode successfully!\n");
	} else if (!strcmp(cmdinfo.cmd, "ngtest")) {
		// Get top_speed
		if (cmdinfo.args[0] == 1)
			input1 = MDI_CFG_SPD_T2P5G;
		else if (cmdinfo.args[0] == 2)
			input1 = MDI_CFG_SPD_T5G;
		else if (cmdinfo.args[0] == 3)
			input1 = MDI_CFG_SPD_T10G;
		if ((cmdinfo.args[1] == 4) && (cmdinfo.argc == 2)) {
			pr_info("Please input test tone!\n");
			printk_test_mode_usage();
			return -EINVAL;
		}
		if ((cmdinfo.argc == 3) && (cmdinfo.args[1] != 4)) {
			pr_info("Test tone is useless here!\n");
			printk_test_mode_usage();
			return -EINVAL;
		}
		aeon_ipc_sync_parity(phydev);
		aeon_ng_test_mode(input1, cmdinfo.args[1], cmdinfo.args[2], phydev);
		pr_info("Set NG test mode successfully!\n");
	} else
		printk_test_mode_usage();

	return count;
}

static ssize_t aeon_tx_fullscale_read_proc(struct file *file, char __user *buffer,
					   size_t count, loff_t *pos)
{
	printk_tx_fullscale_usage();
	return 0;
}


static ssize_t aeon_tx_fullscale_write_proc(struct file *file,
					    const char __user *buffer, size_t count,
					    loff_t *pos)
{
	struct phy_device *phydev = file->private_data;
	char val_string[MAX_BUF] = { 0 };
	struct parsed_cmd cmdinfo = { 0 };
	unsigned short short_delta[4] = { 0 }, speed_all[5] = {4, 8, 16, 32, 64}, i = 0;

	if (count >= sizeof(val_string))
		return -EINVAL;

	if (copy_from_user(val_string, buffer, count))
		return -EFAULT;

	val_string[count] = '\0';

	if (parse_cmd_args(val_string, &cmdinfo, 5) != 0)
		return -EINVAL;

	if (!strcmp(cmdinfo.cmd, "txfullscale")) {
		if (cmdinfo.argc != 5)
			return -EINVAL;

		for (i = 0; i < 4; i++)
			short_delta[i] = (unsigned short)(cmdinfo.args[i+1] + 32678);
		aeon_ipc_sync_parity(phydev);
		aeon_ipc_set_tx_fullscale_delta(cmdinfo.args[0], short_delta, phydev);
		pr_info("Set TX full scale successfully!\n");
	} else if (!strcmp(cmdinfo.cmd, "get")) {
		if (cmdinfo.argc != 0)
			return -EINVAL;

		for (i = 0; i < 5; i++) {
			aeon_ipc_sync_parity(phydev);
			aeon_ipc_get_tx_fullscale_delta(speed_all[i], short_delta, phydev);
			pr_info("speed : %u, tx_fullscale : %d %d %d %d\n", speed_all[i],
					(short)short_delta[0], (short)short_delta[1],
					(short)short_delta[2], (short)short_delta[3]);
		}
		pr_info("Get TX full scale successfully!\n");
	} else
		printk_tx_fullscale_usage();

	return count;
}

static ssize_t aeon_wol_ctrl_read_proc(struct file *file, char __user *buffer,
				       size_t count, loff_t *pos)
{
	printk_wol_ctrl_usage();
	return 0;
}

static ssize_t aeon_wol_ctrl_write_proc(struct file *file, const char __user *buffer,
					size_t count, loff_t *pos)
{
	struct phy_device *phydev = file->private_data;
	char val_string[MAX_BUF] = { 0 };
	struct parsed_cmd cmdinfo = { 0 };
	unsigned short short_delta[3] = { 0 }, i = 0;

	if (count >= sizeof(val_string))
		return -EINVAL;

	if (copy_from_user(val_string, buffer, count))
		return -EFAULT;

	val_string[count] = '\0';

	if (parse_cmd_args(val_string, &cmdinfo, 4) != 0)
		return -EINVAL;

	if (!strcmp(cmdinfo.cmd, "wolctrl")) {
		if (cmdinfo.argc != 4)
			return -EINVAL;

		for (i = 0; i < 3; i++)
			short_delta[i] = (unsigned short)cmdinfo.args[i+1];
		aeon_ipc_sync_parity(phydev);
		aeon_ipc_set_wol(cmdinfo.args[0], short_delta, phydev);
		pr_info("Set wol successfully!\n");
	} else
		printk_wol_ctrl_usage();

	return count;
}

static ssize_t aeon_smi_command_read_proc(struct file *file, char __user *buffer,
					  size_t count, loff_t *pos)
{
	printk_smi_ctrl_usage();
	return 0;
}

static ssize_t aeon_smi_command_write_proc(struct file *file,
					   const char __user *buffer, size_t count,
					   loff_t *pos)
{
	struct phy_device *phydev = file->private_data;
	char val_string[MAX_BUF] = { 0 };
	struct parsed_cmd cmdinfo = { 0 };
	unsigned short short_delta[3] = { 0 }, i = 0;

	if (count >= sizeof(val_string))
		return -EINVAL;

	if (copy_from_user(val_string, buffer, count))
		return -EFAULT;

	val_string[count] = '\0';

	if (parse_cmd_args(val_string, &cmdinfo, 2) != 0)
		return -EINVAL;

	if (!strcmp(cmdinfo.cmd, "smicommand")) {
		if (cmdinfo.argc != 2)
			return -EINVAL;

		for (i = 0; i < 2; i++)
			short_delta[i] = (unsigned short)cmdinfo.args[i];
		aeon_ipc_sync_parity(phydev);
		aeon_ipc_smi_command(short_delta, phydev);
		pr_info("Set SMI Command successfully!\n");
	} else
		printk_smi_ctrl_usage();

	return count;
}

static ssize_t aeon_set_irq_en_read_proc(struct file *file, char __user *buffer,
					 size_t count, loff_t *pos)
{
	printk_set_irq_en_usage();
	return 0;
}

static ssize_t aeon_set_irq_en_write_proc(struct file *file, const char __user *buffer,
					  size_t count, loff_t *pos)
{
	struct phy_device *phydev = file->private_data;
	char val_string[MAX_BUF] = { 0 };
	struct parsed_cmd cmdinfo = { 0 };
	unsigned short short_delta[6] = { 0 }, i = 0;

	if (count >= sizeof(val_string))
		return -EINVAL;

	if (copy_from_user(val_string, buffer, count))
		return -EFAULT;

	val_string[count] = '\0';

	if (parse_cmd_args(val_string, &cmdinfo, 4) != 0)
		return -EINVAL;

	if (!strcmp(cmdinfo.cmd, "setirqen")) {
		if (cmdinfo.argc != 4)
			return -EINVAL;

		for (i = 0; i < 5; i++)
			short_delta[i] = (unsigned short)cmdinfo.args[i];
		aeon_ipc_sync_parity(phydev);
		aeon_ipc_irq_en(short_delta, phydev);
		pr_info("Set irq en successfully!\n");
	} else
		printk_set_irq_en_usage();

	return count;
}

static ssize_t aeon_set_irq_clr_read_proc(struct file *file, char __user *buffer,
					  size_t count, loff_t *pos)
{
	printk_set_irq_clr_usage();
	return 0;
}

static ssize_t aeon_set_irq_clr_write_proc(struct file *file,
					   const char __user *buffer, size_t count,
					   loff_t *pos)
{
	struct phy_device *phydev = file->private_data;
	char val_string[MAX_BUF] = { 0 };
	struct parsed_cmd cmdinfo = { 0 };

	if (count >= sizeof(val_string))
		return -EINVAL;

	if (copy_from_user(val_string, buffer, count))
		return -EFAULT;

	val_string[count] = '\0';

	if (parse_cmd_args(val_string, &cmdinfo, 1) != 0)
		return -EINVAL;

	if (!strcmp(cmdinfo.cmd, "setirqclr")) {
		if (cmdinfo.argc != 1)
			return -EINVAL;

		aeon_ipc_sync_parity(phydev);
		aeon_ipc_irq_clr(cmdinfo.args[0], phydev);
		pr_info("Set irq clr successfully!\n");
	} else
		printk_set_irq_clr_usage();

	return count;
}

static ssize_t aeon_query_irq_read_proc(struct file *file, char __user *buffer,
					size_t count, loff_t *pos)
{
	printk_query_irq_status_usage();

	return 0;
}

static ssize_t aeon_query_irq_write_proc(struct file *file, const char __user *buffer,
					 size_t count, loff_t *pos)
{
	struct phy_device *phydev = file->private_data;
	char val_string[MAX_BUF] = { 0 };
	struct parsed_cmd cmdinfo = { 0 };
	unsigned short param = 0;
	irq_stats_t stats;

	if (count >= sizeof(val_string))
		return -EINVAL;

	if (copy_from_user(val_string, buffer, count))
		return -EFAULT;

	val_string[count] = '\0';

	if (parse_cmd_args(val_string, &cmdinfo, 0) != 0)
		return -EINVAL;

	if (!strcmp(cmdinfo.cmd, "queryirq")) {
		if (cmdinfo.argc != 0)
			return -EINVAL;

		aeon_ipc_sync_parity(phydev);
		aeon_ipc_irq_query(&param, phydev);
		pr_info("query irq status is 0x%x!\n", param);
		*(unsigned char *)&stats = (unsigned char)param;
		pr_info("wol_sts: %u, link_sts: %u\n", stats.wol_sts, stats.link_sts);
	} else
		printk_query_irq_status_usage();

	return count;
}

static ssize_t aeon_cable_diag_read_proc(struct file *file, char __user *buffer,
					 size_t count, loff_t *pos)
{
	printk_cable_diag_usage();
	return 0;
}

static ssize_t aeon_cable_diag_write_proc(struct file *file, const char __user *buffer,
					  size_t count, loff_t *pos)
{
	struct phy_device *phydev = file->private_data;
	char val_string[MAX_BUF] = { 0 };
	struct parsed_cmd cmdinfo = { 0 };
	unsigned short ii = 0, temp[8] = { 0 }, mode = 0;
	int output = 0;

	if (count >= sizeof(val_string))
		return -EINVAL;

	if (copy_from_user(val_string, buffer, count))
		return -EFAULT;

	val_string[count] = '\0';

	if (parse_cmd_args(val_string, &cmdinfo, 1) != 0)
		return -EINVAL;

	if (cmdinfo.argc > 1)
		return -EINVAL;

	if (!strcmp(cmdinfo.cmd, "chanlen")) {
		aeon_ipc_sync_parity(phydev);
		aeon_ipc_cable_diag(IPC_CMD_CABLE_DIAG_CHAN_LEN, temp, mode, phydev);
		pr_info("channel length(m) : ");
		for (ii = 0; ii < CHAN_NUM/2; ii++)
			pr_info("%u  %u  ", temp[ii] & 0xff, (temp[ii] >> 8));
		pr_info("\n");
	} else if (!strcmp(cmdinfo.cmd, "ppmofst")) {
		aeon_ipc_sync_parity(phydev);
		aeon_ipc_cable_diag(IPC_CMD_CABLE_DIAG_PPM_OFST, temp, mode, phydev);
		output = temp[0] | (temp[1] << 16);
		pr_info("frequency offset : %d\n", output);
	} else if (!strcmp(cmdinfo.cmd, "snrmargin")) {
		aeon_ipc_sync_parity(phydev);
		aeon_ipc_cable_diag(IPC_CMD_CABLE_DIAG_SNR_MARG, temp, mode, phydev);
		pr_info("SNR margin : ");
		for (ii = 0; ii < 2 * CHAN_NUM; ii++) {
			output = temp[ii];
			++ii;
			output |= (temp[ii] << 16);
			pr_info("%d  ", output);
		}
		pr_info("\n");
	} else if (!strcmp(cmdinfo.cmd, "chanskew")) {
		aeon_ipc_sync_parity(phydev);
		aeon_ipc_cable_diag(IPC_CMD_CABLE_DIAG_CHAN_SKW, temp, mode, phydev);
		pr_info("channel skew : ");
		for (ii = 0; ii < 2 * CHAN_NUM; ii++) {
			output = temp[ii];
			++ii;
			output |= (temp[ii] << 16);
			pr_info("%d  ", output);
		}
		pr_info("\n");
	} else if (!strcmp(cmdinfo.cmd, "set")) {
		mode = cmdinfo.args[0];
		aeon_ipc_sync_parity(phydev);
		aeon_ipc_cable_diag(IPC_CMD_CABLE_DIAG_SET, temp, mode, phydev);
	} else if (!strcmp(cmdinfo.cmd, "get")) {
		aeon_ipc_sync_parity(phydev);
		aeon_ipc_cable_diag(IPC_CMD_CABLE_DIAG_GET, temp, mode, phydev);
		pr_info("channel length(m) : ");
		for (ii = 0; ii < CHAN_NUM/2; ii++)
			pr_info("%u  %u  ", temp[ii] & 0xff, (temp[ii] >> 8));
		pr_info("\nimp_type : ");
		for (ii = 0; ii < CHAN_NUM/2; ii++) {
			pr_info("%s  %s  ", IMP_TYPE_STR[temp[ii+CHAN_NUM/2] & 0xff],
				IMP_TYPE_STR[(temp[ii+CHAN_NUM/2] >> 8)]);
		}
		pr_info("\nres_conf : ");
		pr_info("0x%x\n", temp[CHAN_NUM] & 0xff);
	} else {
		printk_cable_diag_usage();
	}

	return count;
}


static ssize_t aeon_eye_diagram_read_proc(struct file *file, char __user *buffer,
					  size_t count, loff_t *pos)
{
	printk_sds_eye_diagram_usage();
	return 0;
}

static ssize_t aeon_eye_diagram_write_proc(struct file *file, const char __user *buffer,
					   size_t count, loff_t *pos)
{
	struct phy_device *phydev = file->private_data;
	struct as21xxx_priv *priv = phydev->priv;
	unsigned char *raw_eye_data = priv->raw_eye_data;
	char val_string[MAX_BUF] = {  0};
	struct parsed_cmd cmdinfo = { 0 };
	unsigned short ii, grp, part;

	if (count >= sizeof(val_string))
		return -EINVAL;

	if (copy_from_user(val_string, buffer, count))
		return -EFAULT;

	val_string[count] = '\0';

	if (parse_cmd_args(val_string, &cmdinfo, 1) != 0)
		return -EINVAL;

	if (!strcmp(cmdinfo.cmd, "eyescan")) {
		part = cmdinfo.args[0];
		if (part == EYE_PART_0) {
			for (grp = 0; grp < EYE_PART_0_GRPS ; ++grp) {
				aeon_ipc_sync_parity(phydev);
				aeon_ipc_eye_scan(0, grp, &raw_eye_data[grp * EYE_STRIDE], phydev);
			}
		} else if (part == EYE_PART_1) {
			for (grp = EYE_PART_0_GRPS; grp < EYE_GRPS ; ++grp) {
				aeon_ipc_sync_parity(phydev);
				aeon_ipc_eye_scan(0, grp, &raw_eye_data[grp * EYE_STRIDE], phydev);
			}
		} else {
			return -EINVAL;
		}
	} else if (!strcmp(cmdinfo.cmd, "eyeshow")) {
		pr_info("RAW EYE data:\n");
		for (ii = 0; ii < EYE_TOTAL_BYTES; ++ii) {
			if (ii % 16 == 0)
				pr_info("\n");

			pr_info("0x%02x ", raw_eye_data[ii]);
		}
		pr_info("\n");

		memset(raw_eye_data, 0, EYE_TOTAL_BYTES);
	} else {
		printk_sds_eye_diagram_usage();
	}
	return count;
}

static ssize_t aeon_force_mode_read_proc(struct file *file, char __user *buffer,
					 size_t count, loff_t *pos)
{
	printk_force_mode_usage();

	return 0;
}

static ssize_t aeon_force_mode_write_proc(struct file *file, const char __user *buffer,
					  size_t count, loff_t *pos)
{
	struct phy_device *phydev = file->private_data;
	char val_string[MAX_BUF] = { 0 };
	struct parsed_cmd cmdinfo = { 0 };

	if (count >= sizeof(val_string))
		return -EINVAL;

	if (copy_from_user(val_string, buffer, count))
		return -EFAULT;

	val_string[count] = '\0';

	if (parse_cmd_args(val_string, &cmdinfo, 1) != 0)
		return -EINVAL;

	if (!strcmp(cmdinfo.cmd, "force100M")) {
		if (cmdinfo.argc != 1)
			return -EINVAL;

		aeon_ipc_sync_parity(phydev);
		aeon_set_man_duplex(cmdinfo.args[0], phydev);
		// switch speed
		aeon_cu_an_set_top_spd(MDI_CFG_SPD_T100, phydev);
		// enable AN
		aeon_cu_an_enable(0, phydev);
		pr_info("Force 100M successfully!\n");
	} else
		printk_force_mode_usage();

	return count;
}

static ssize_t aeon_parallel_det_read_proc(struct file *file, char __user *buffer,
					   size_t count, loff_t *pos)
{
	printk_parallel_det_usage();
	return 0;
}

static ssize_t aeon_parallel_det_write_proc(struct file *file, const char __user *buffer,
					    size_t count, loff_t *pos)
{
	struct phy_device *phydev = file->private_data;
	char val_string[MAX_BUF] = { 0 };
	struct parsed_cmd cmdinfo = { 0 };

	if (count >= sizeof(val_string))
		return -EINVAL;

	if (copy_from_user(val_string, buffer, count))
		return -EFAULT;

	val_string[count] = '\0';

	if (parse_cmd_args(val_string, &cmdinfo, 1) != 0)
		return -EINVAL;

	if (!strcmp(cmdinfo.cmd, "paradet")) {
		if (cmdinfo.argc != 1)
			return -EINVAL;

		aeon_parallel_det(cmdinfo.args[0], phydev);
		pr_info("Set parallel detection successfully!\n");
	} else
		printk_parallel_det_usage();

	return count;
}

static ssize_t aeon_force_mdi_mode_read_proc(struct file *file, char __user *buffer,
					     size_t count, loff_t *pos)
{
	printk_force_mdi_mode_usage();

	return 0;
}

static ssize_t aeon_force_mdi_mode_write_proc(struct file *file, const char __user *buffer,
					      size_t count, loff_t *pos)
{
	struct phy_device *phydev = file->private_data;
	char val_string[MAX_BUF] = { 0 };
	struct parsed_cmd cmdinfo = { 0 };

	if (count >= sizeof(val_string))
		return -EINVAL;

	if (copy_from_user(val_string, buffer, count))
		return -EFAULT;

	val_string[count] = '\0';

	if (parse_cmd_args(val_string, &cmdinfo, 1) != 0)
		return -EINVAL;

	if (!strcmp(cmdinfo.cmd, "mdi")) {
		if (cmdinfo.argc != 1)
			return -EINVAL;

		aeon_ipc_sync_parity(phydev);
		if (cmdinfo.args[0] == 1)
			aeon_set_man_mdi(phydev);
		else if (cmdinfo.args[0] == 0)
			aeon_set_man_mdix(phydev);
		else
			return -EINVAL;

		pr_info("Force mdi/mdix mode successfully!\n");
	} else
		printk_force_mdi_mode_usage();


	return count;
}

static ssize_t aeon_synce_master_mode_proc(struct file *file, char __user *buffer,
					   size_t count, loff_t *pos)
{
	printk_synce_master_mode_usage();

	return 0;
}

static ssize_t aeon_synce_master_mode_write_proc(struct file *file, const char __user *buffer,
						 size_t count, loff_t *pos)
{
	struct phy_device *phydev = file->private_data;
	char val_string[MAX_BUF] = { 0 };
	struct parsed_cmd cmdinfo = { 0 };

	if (count >= sizeof(val_string))
		return -EINVAL;

	if (copy_from_user(val_string, buffer, count))
		return -EFAULT;

	val_string[count] = '\0';

	if (parse_cmd_args(val_string, &cmdinfo, 2) != 0)
		return -EINVAL;

	if (!strcmp(cmdinfo.cmd, "synce_master_mode")) {
		if (cmdinfo.argc != 2)
			return -EINVAL;

		if ((cmdinfo.args[0] < 0) || (cmdinfo.args[0] > 1)) {
			pr_info("Set synce enable fail, enable value is from 0 to 1!\n");
			return -EFAULT;
		}

		if ((cmdinfo.args[1] < 1) || (cmdinfo.args[1] > 4)) {
			pr_info("Set user bw fail, enable value is from 1 to 4!\n");
			return -EFAULT;
		}

		aeon_ipc_sync_parity(phydev);
		aeon_synce_mode_cfg(1, phydev);
		aeon_synce_user_bw(cmdinfo.args[1], phydev);
		aeon_synce_enable_cfg(cmdinfo.args[0], phydev);
		pr_info("Set synce master mode successfully!\n");
	} else
		printk_synce_master_mode_usage();

	return count;
}

static ssize_t aeon_synce_slave_mode1_proc(struct file *file, char __user *buffer,
					   size_t count, loff_t *pos)
{
	printk_synce_slave_mode1_usage();

	return 0;
}

static ssize_t aeon_synce_slave_mode1_write_proc(struct file *file, const char __user *buffer,
						 size_t count, loff_t *pos)
{
	struct phy_device *phydev = file->private_data;
	char val_string[MAX_BUF] = { 0 };
	struct parsed_cmd cmdinfo = { 0 };

	if (count >= sizeof(val_string))
		return -EINVAL;

	if (copy_from_user(val_string, buffer, count))
		return -EFAULT;

	val_string[count] = '\0';

	if (parse_cmd_args(val_string, &cmdinfo, 2) != 0)
		return -EINVAL;

	if (!strcmp(cmdinfo.cmd, "synce_slave_mode1")) {
		if (cmdinfo.argc != 2)
			return -EINVAL;

		if ((cmdinfo.args[0] < 0) || (cmdinfo.args[0] > 1)) {
			pr_info("Set synce enable fail, enable value is from 0 to 1!\n");
			return -EFAULT;
		}

		if ((cmdinfo.args[1] < 0) || (cmdinfo.args[1] > 4)) {
			pr_info("Set synce output pin fail, enable value is from 0 to 4!\n");
			return -EFAULT;
		}

		aeon_ipc_sync_parity(phydev);
		aeon_synce_mode_cfg(0, phydev);
		aeon_synce_user_bw(0, phydev);
		aeon_synce_slave_output_ctrl_cfg(cmdinfo.args[1], phydev);
		aeon_synce_enable_cfg(cmdinfo.args[0], phydev);
		pr_info("Set synce slave mode1 successfully!\n");
	} else
		printk_synce_slave_mode1_usage();

	return count;
}

static ssize_t aeon_synce_slave_mode2_read_proc(struct file *file, char __user *buffer,
						size_t count, loff_t *pos)
{
	printk_synce_slave_mode2_usage();

	return 0;
}

static ssize_t aeon_synce_slave_mode2_write_proc(struct file *file, const char __user *buffer,
						 size_t count, loff_t *pos)
{
	struct phy_device *phydev = file->private_data;
	char val_string[MAX_BUF] = { 0 };
	struct parsed_cmd cmdinfo = { 0 };

	if (count >= sizeof(val_string))
		return -EINVAL;

	if (copy_from_user(val_string, buffer, count))
		return -EFAULT;

	val_string[count] = '\0';

	if (parse_cmd_args(val_string, &cmdinfo, 3) != 0)
		return -EINVAL;


	if (!strcmp(cmdinfo.cmd, "synce_slave_mode2")) {
		if (cmdinfo.argc != 3)
			return -EINVAL;

		if ((cmdinfo.args[0] < 0) || (cmdinfo.args[0] > 1)) {
			pr_info("Set synce enable fail, enable value is from 0 to 1!\n");
			return -EFAULT;
		}

		if ((cmdinfo.args[1] < 0) || ((cmdinfo.args[1] > 6) && (cmdinfo.args[1] != 10))) {
			pr_info("Set synce bw fail, enable value is from 0 to 6 and 10!\n");
			return -EFAULT;
		}

		if ((cmdinfo.args[2] < 0) || (cmdinfo.args[2] > 1)) {
			pr_info("Set synce output pin fail, enable value is from 0 to 1!\n");
			return -EFAULT;
		}

		aeon_ipc_sync_parity(phydev);
		aeon_synce_mode_cfg(2, phydev);
		aeon_synce_user_bw(cmdinfo.args[1], phydev);
		aeon_synce_slave_output_ctrl_cfg(cmdinfo.args[2], phydev);
		aeon_synce_enable_cfg(cmdinfo.args[0], phydev);
		pr_info("Set synce slave mode2 successfully!\n");
	} else
		printk_synce_slave_mode2_usage();

	return count;
}

static const struct file_operations aeon_set_speed_mode_fops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.read = aeon_speed_mode_read_proc,
	.write = aeon_speed_mode_write_proc,
};

static const struct file_operations aeon_set_mdi_cfg_fops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.read = aeon_mdi_cfg_read_proc,
	.write = aeon_mdi_cfg_write_proc,
};

static const struct file_operations aeon_set_sds_pcs_cfg_fops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.read = aeon_sds_pcs_cfg_read_proc,
	.write = aeon_sds_pcs_cfg_write_proc,
};

static const struct file_operations aeon_set_sds_pma_cfg_fops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.read = aeon_sds_pma_cfg_read_proc,
	.write = aeon_sds_pma_cfg_write_proc,
};

static const struct file_operations aeon_fw_version_fops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.read = aeon_fw_version_read_proc,
	.write = aeon_fw_version_write_proc,
};

static const struct file_operations aeon_restart_an_fops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.read = aeon_restart_an_read_proc,
	.write = aeon_restart_an_write_proc,
};

static const struct file_operations aeon_set_led_fops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.read = aeon_set_led_read_proc,
	.write = aeon_set_led_write_proc,
};

static const struct file_operations aeon_temp_monitor_fops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.read = aeon_temp_monitor_read_proc,
	.write = aeon_temp_monitor_write_proc,
};

static const struct file_operations aeon_set_sys_reboot_fops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.read = aeon_sys_reboot_read_proc,
	.write = aeon_sys_reboot_write_proc,
};

static const struct file_operations aeon_read_reg_fops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.read = aeon_read_reg_read_proc,
	.write = aeon_read_reg_write_proc,
};

static const struct file_operations aeon_write_reg_fops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.read = aeon_write_reg_read_proc,
	.write = aeon_write_reg_write_proc,
};

static const struct file_operations aeon_get_eth_status_fops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.read = aeon_eth_status_read_proc,
	.write = aeon_eth_status_write_proc,
};

#ifndef AEON_SEI2
static const struct file_operations aeon_mdc_timing_fops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.read = aeon_mdc_timing_read_proc,
	.write = aeon_mdc_timing_write_proc,
};

static const struct file_operations aeon_auto_eee_cfg_fops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.read = aeon_auto_eee_cfg_read_proc,
	.write = aeon_auto_eee_cfg_write_proc,
};

static const struct file_operations aeon_pkt_chk_cfg_fops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.read = aeon_pkt_chk_cfg_read_proc,
	.write = aeon_pkt_chk_cfg_write_proc,
};

static const struct file_operations aeon_sds_wait_eth_fops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.read = aeon_sds_wait_eth_cfg_read_proc,
	.write = aeon_sds_wait_eth_cfg_write_proc,
};

static const struct file_operations aeon_sds_restart_an_fops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.read = aeon_sds_restart_an_read_proc,
	.write = aeon_sds_restart_an_write_proc,
};

static const struct file_operations aeon_tx_power_lvl = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.read = aeon_tx_power_lvl_read_proc,
	.write = aeon_tx_power_lvl_write_proc,
};

static const struct file_operations aeon_sds2nd_enable_cfg = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.read = aeon_sds2nd_enable_read_proc,
	.write = aeon_sds2nd_enable_write_proc,
};

static const struct file_operations aeon_sds2nd_eq = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.read = aeon_sds2nd_eq_read_proc,
	.write = aeon_sds2nd_eq_write_proc,
};

static const struct file_operations aeon_sds2nd_mode = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.read = aeon_sds2nd_mode_read_proc,
	.write = aeon_sds2nd_mode_write_proc,
};

static const struct file_operations aeon_sds2nd_eye_diagram_data = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.read = aeon_sds2nd_eye_diagram_read_proc,
	.write = aeon_sds2nd_eye_diagram_write_proc,
};

static const struct file_operations aeon_normal_retrain = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.read = aeon_normal_retrain_read_proc,
	.write = aeon_normal_retrain_write_proc,
};

static const struct file_operations aeon_auto_link = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.read = aeon_auto_link_read_proc,
	.write = aeon_auto_link_write_proc,
};

static const struct file_operations aeon_sds_txfir = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.read = aeon_sds_txfir_read_proc,
	.write = aeon_sds_txfir_write_proc,
};
#endif

static const struct file_operations aeon_phy_enable_fops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.read = aeon_phy_enable_read_proc,
	.write = aeon_phy_enable_write_proc,
};
#ifdef DUAL_FLASH
static const struct file_operations aeon_burn_flash_image_fops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.read = aeon_burn_flash_read_proc,
	.write = aeon_burn_flash_write_proc,
};
static const struct file_operations aeon_erase_flash_fops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.read = aeon_erase_flash_read_proc,
	.write = aeon_erase_flash_write_proc,
};
#endif
static const struct file_operations aeon_test_mode_fops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.read = aeon_test_mode_read_proc,
	.write = aeon_test_mode_write_proc,
};

static const struct file_operations aeon_tx_fullscale_fops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.read = aeon_tx_fullscale_read_proc,
	.write = aeon_tx_fullscale_write_proc,
};

static const struct file_operations aeon_wol_ctrl = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.read = aeon_wol_ctrl_read_proc,
	.write = aeon_wol_ctrl_write_proc,
};

static const struct file_operations aeon_smi_command = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.read = aeon_smi_command_read_proc,
	.write = aeon_smi_command_write_proc,
};

static const struct file_operations aeon_setirq_en = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.read = aeon_set_irq_en_read_proc,
	.write = aeon_set_irq_en_write_proc,
};

static const struct file_operations aeon_setirq_clr = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.read = aeon_set_irq_clr_read_proc,
	.write = aeon_set_irq_clr_write_proc,
};

static const struct file_operations aeon_query_irq = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.read = aeon_query_irq_read_proc,
	.write = aeon_query_irq_write_proc,
};

static const struct file_operations aeon_cable_diag = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.read = aeon_cable_diag_read_proc,
	.write = aeon_cable_diag_write_proc,
};

static const struct file_operations aeon_eye_diagram_data = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.read = aeon_eye_diagram_read_proc,
	.write = aeon_eye_diagram_write_proc,
};

static const struct file_operations aeon_force_mode = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.read = aeon_force_mode_read_proc,
	.write = aeon_force_mode_write_proc,
};

static const struct file_operations aeon_parallel_det_cfg = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.read = aeon_parallel_det_read_proc,
	.write = aeon_parallel_det_write_proc,
};

static const struct file_operations aeon_force_mdi_mode = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.read = aeon_force_mdi_mode_read_proc,
	.write = aeon_force_mdi_mode_write_proc,
};

static const struct file_operations aeon_synce_master = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.read = aeon_synce_master_mode_proc,
	.write = aeon_synce_master_mode_write_proc,
};

static const struct file_operations aeon_synce_slave_mode1 = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.read = aeon_synce_slave_mode1_proc,
	.write = aeon_synce_slave_mode1_write_proc,
};

static const struct file_operations aeon_synce_slave_mode2 = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.read = aeon_synce_slave_mode2_read_proc,
	.write = aeon_synce_slave_mode2_write_proc,
};

int as21xxx_debugfs_init(struct phy_device *phydev)
{
	struct as21xxx_priv *priv = phydev->priv;
	struct an_mdi_cfg *__priv_data = &priv->mdi_cfg;
	struct dentry *dir = priv->debugfs_root;
	int ret = 0;

	dir = debugfs_create_dir(dev_name(&phydev->mdio.dev), NULL);
	if (!dir) {
		phydev_err(phydev, "%s:err at %d\n", __func__, __LINE__);
		ret = -ENOMEM;
	}

	// init the data structure
	__priv_data->top_spd = 0xF;
	__priv_data->eee_spd = 0xFF;
	__priv_data->fr_spd = 0xF;
	__priv_data->thp_byp = 0xF;
	__priv_data->port_type = 0xF;
	__priv_data->ms_en = 0xF;
	__priv_data->ms_config = 0xF;
	__priv_data->nstd_pbo = 0xFF;
	__priv_data->smt_spd.enable = 0xF;
	__priv_data->smt_spd.retry_limit = 0xF;
	__priv_data->trd_ovrd = 0xF;
	__priv_data->trd_swap = 0xF;
	__priv_data->cfr = 0xF;

	debugfs_create_file("aeon_set_speed_mode", 0644, dir, phydev,
			    &aeon_set_speed_mode_fops);
	debugfs_create_file("aeon_set_mdi_cfg", 0644, dir, phydev,
			    &aeon_set_mdi_cfg_fops);
	debugfs_create_file("aeon_set_sds_pcs_cfg", 0644, dir, phydev,
			    &aeon_set_sds_pcs_cfg_fops);
	debugfs_create_file("aeon_set_sds_pma_cfg", 0644, dir, phydev,
			    &aeon_set_sds_pma_cfg_fops);
	debugfs_create_file("aeon_fw_version", 0644, dir, phydev,
			    &aeon_fw_version_fops);
	debugfs_create_file("aeon_restart_an", 0644, dir, phydev,
			    &aeon_restart_an_fops);
	debugfs_create_file("aeon_set_led", 0644, dir, phydev,
			    &aeon_set_led_fops);
	debugfs_create_file("aeon_temp_monitor", 0644, dir, phydev,
			    &aeon_temp_monitor_fops);
	debugfs_create_file("aeon_set_sys_reboot", 0644, dir, phydev,
			    &aeon_set_sys_reboot_fops);
#ifndef AEON_SEI2
	debugfs_create_file("aeon_mdc_timing", 0644, dir, phydev,
			    &aeon_mdc_timing_fops);
	debugfs_create_file("aeon_auto_eee_cfg", 0644, dir, phydev,
			    &aeon_auto_eee_cfg_fops);
	debugfs_create_file("aeon_pkt_chk_cfg", 0644, dir, phydev,
			    &aeon_pkt_chk_cfg_fops);
	debugfs_create_file("aeon_sds_wait_eth", 0644, dir, phydev,
			    &aeon_sds_wait_eth_fops);
	debugfs_create_file("aeon_sds_restart_an", 0644, dir, phydev,
			    &aeon_sds_restart_an_fops);
	debugfs_create_file("aeon_tx_power_lvl", 0644, dir, phydev,
			    &aeon_tx_power_lvl);
	debugfs_create_file("aeon_sds2nd_eye_diagram_data", 0644, dir, phydev,
			    &aeon_sds2nd_eye_diagram_data);
	debugfs_create_file("aeon_sds2nd_enable", 0644, dir, phydev,
				&aeon_sds2nd_enable_cfg);
	debugfs_create_file("aeon_sds2nd_eq_cfg", 0644, dir, phydev,
			    &aeon_sds2nd_eq);
	debugfs_create_file("aeon_sds2nd_mode_cfg", 0644, dir, phydev,
			    &aeon_sds2nd_mode);
	debugfs_create_file("aeon_normal_retrain", 0644, dir, phydev,
			    &aeon_normal_retrain);
	debugfs_create_file("aeon_auto_link", 0644, dir, phydev,
			    &aeon_auto_link);
	debugfs_create_file("aeon_sds_txfir", 0644, dir, phydev,
			    &aeon_sds_txfir);
#endif
	debugfs_create_file("aeon_read_reg", 0644, dir, phydev,
			    &aeon_read_reg_fops);
	debugfs_create_file("aeon_write_reg", 0644, dir, phydev,
			    &aeon_write_reg_fops);
	debugfs_create_file("aeon_get_eth_status", 0644, dir, phydev,
			    &aeon_get_eth_status_fops);
	debugfs_create_file("aeon_phy_enable", 0644, dir, phydev,
			    &aeon_phy_enable_fops);
#ifdef DUAL_FLASH
	debugfs_create_file("aeon_burn_flash_image", 0644, dir, phydev,
			    &aeon_burn_flash_image_fops);
	debugfs_create_file("aeon_erase_flash", 0644, dir, phydev,
			    &aeon_erase_flash_fops);
#endif
	debugfs_create_file("aeon_test_mode", 0644, dir, phydev,
			    &aeon_test_mode_fops);
	debugfs_create_file("aeon_tx_fullscale", 0644, dir, phydev,
			    &aeon_tx_fullscale_fops);
	debugfs_create_file("aeon_wol_ctrl", 0644, dir, phydev,
			    &aeon_wol_ctrl);
	debugfs_create_file("aeon_smi_command", 0644, dir, phydev,
			    &aeon_smi_command);
	debugfs_create_file("aeon_setirq_en", 0644, dir, phydev,
			    &aeon_setirq_en);
	debugfs_create_file("aeon_setirq_clr", 0644, dir, phydev,
			    &aeon_setirq_clr);
	debugfs_create_file("aeon_query_irq", 0644, dir, phydev,
			    &aeon_query_irq);
	debugfs_create_file("aeon_cable_diag", 0644, dir, phydev,
			    &aeon_cable_diag);
	debugfs_create_file("aeon_eye_diagram_data", 0644, dir, phydev,
			    &aeon_eye_diagram_data);
	debugfs_create_file("aeon_force_mode", 0644, dir, phydev,
			    &aeon_force_mode);
	debugfs_create_file("aeon_parallel_det", 0644, dir, phydev,
			    &aeon_parallel_det_cfg);
	debugfs_create_file("aeon_force_mdi_mode", 0644, dir, phydev,
			    &aeon_force_mdi_mode);
	debugfs_create_file("aeon_synce_master", 0644, dir, phydev,
			    &aeon_synce_master);
	debugfs_create_file("aeon_synce_slave_mode1", 0644, dir, phydev,
			    &aeon_synce_slave_mode1);
	debugfs_create_file("aeon_synce_slave_mode2", 0644, dir, phydev,
			    &aeon_synce_slave_mode2);

	priv->debugfs_root = dir;

	return ret;
}

void as21xxx_debugfs_remove(struct phy_device *phydev)
{
	struct as21xxx_priv *priv = phydev->priv;

	debugfs_remove_recursive(priv->debugfs_root);
	priv->debugfs_root = NULL;
}
