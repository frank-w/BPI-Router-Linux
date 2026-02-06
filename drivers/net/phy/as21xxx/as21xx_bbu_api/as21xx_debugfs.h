/* SPDX-License-Identifier: GPL-2.0-only */
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
#ifndef _AS21XX_PROC_H_
#define _AS21XX_PROC_H_

/******************************************************************************
 * MISC Macros
 *****************************************************************************/
#define MAX_BUF     64
#define MAX_CMD_LEN 32
#define MAX_ARGS    10

#define CHAN_NUM 4
#define AS21XX_PHY_NUM 2

/******************************************************************************
 * Common Structure
 *****************************************************************************/
struct parsed_cmd {
	char cmd[MAX_CMD_LEN];
	long args[MAX_ARGS];
	int argc;
};

int as21xxx_debugfs_init(struct phy_device *phydev);
void as21xxx_debugfs_remove(struct phy_device *phydev);

#endif
