/*
 * MTK spi nand controller
 *
 * Copyright (c) 2017 Mediatek
 * Authors:	Bayi Cheng		<bayi.cheng@mediatek.com>
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 */

#ifndef __MTK_SNAND_K_H__
#define __MTK_SNAND_K_H__

/***************************************************************
*                                                              *
* Interface BMT need to use                                    *
*                                                              *
***************************************************************/
extern bool mtk_nand_exec_read_page(struct mtd_info *mtd, u32 row,
				    u32 page_size, u8 *dat, u8 *oob);
extern int mtk_nand_block_bad_hw(struct mtd_info *mtd, loff_t ofs);
extern int mtk_nand_erase_hw(struct mtd_info *mtd, int page);
extern int mtk_nand_block_markbad_hw(struct mtd_info *mtd, loff_t ofs);
extern int mtk_nand_exec_write_page(struct mtd_info *mtd, u32 row,
				    u32 page_size, u8 *dat, u8 *oob);

#endif
