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

#define CHAN_NUM 4

int as21xxx_debugfs_init(struct phy_device *phydev);
void as21xxx_debugfs_remove(struct phy_device *phydev);

#endif /**/
