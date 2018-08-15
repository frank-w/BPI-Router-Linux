only testing to kick wmt-tools

some thoughts:

Wifi-name (AP)
 
gl_p2p_init.c
````
44 	#if CFG_TC1_FEATURE
45 	#define AP_MODE_INF_NAME "wlan%d"
46 	#else
47 	#define AP_MODE_INF_NAME "ap%d"//<<<<<<
48 	#endif
49 	/* #define MAX_INF_NAME_LEN 15 */
… 	
102 	VOID p2pCheckInterfaceName(VOID)
103 	{
104 	
105 	if (mode) {
106 	mode = RUNNING_AP_MODE;
107 	ifname = AP_MODE_INF_NAME;//<<<<
108 	}
109 	#if 0
BPI-SINOVOIP/BPI-R2-bsp – gl_p2p.c
Showing the top match Last indexed on 1 Sep 2017
C
1341 	if (kalStrnCmp(gprP2pWdev->netdev->name, AP_MODE_INF_NAME, 2)) {//<<<<<
1342 	rtnl_lock();
1343 	dev_change_name(gprP2pWdev->netdev->name, AP_MODE_INF_NAME); //<<<<<
1344 	rtnl_unlock();
1345 	}
1346 	#endif
1347 	} else {
1348 	#if CFG_SUPPORT_PERSIST_NETDEV
````
Activate wifi-ap:
 
https://github.com/frank-w/BPI-R2-4.14/blob/b514a6a62ed8d088f740ba8f376bdc440d865359/drivers/misc/mediatek/connectivity/common/conn_soc/linux/pub/wmt_chrdev_wifi.c#L519
 
 
WMTLOADER:
https://github.com/BPI-SINOVOIP/BPI-R2-bsp/blob/d94f55022a9192cb181d380b1a6699949a36f30c/vendor/mediatek/connectivity/tools/src/wmt_loader.c#L56
 

    COMBO_IOCTL_GET_SOC_CHIP_ID
    COMBO_IOCTL_SET_CHIP_ID chipId
    COMBO_IOCTL_DO_MODULE_INIT chipId

 
https://github.com/BPI-SINOVOIP/BPI-R2-bsp/blob/81776dada7beacfe4efbe3fbb16fbf909f94fe2e/linux-mt/drivers/misc/mediatek/connectivity/common/common_detect/wmt_detect.c#L133
wmt_plat_get_soc_chipid();
mtk_wcn_wmt_set_chipid(arg);
 
https://github.com/BPI-SINOVOIP/BPI-R2-bsp/blob/81776dada7beacfe4efbe3fbb16fbf909f94fe2e/linux-mt/drivers/misc/mediatek/connectivity/common/common_detect/drv_init/conn_drv_init.c#L41
= do_connectivity_driver_init(int chip_id)

````
stp_uart_launcher -p /etc/firmware
 
ROMv2_lm_patch_1_0_hdr.bin
ROMv2_lm_patch_1_1_hdr.bin
WIFI_RAM_CODE_7623
````
 
https://github.com/BPI-SINOVOIP/BPI-R2-bsp/blob/d94f55022a9192cb181d380b1a6699949a36f30c/vendor/mediatek/connectivity/tools/src/stp_uart_launcher.c#L1609


````
sStpParaConfig.pPatchPath = gPatchFolder;
 
setHifInfo(chipId, sStpParaConfig.pPatchPath);
````
 wmt_dev.c
````
73 	#define WMT_IOCTL_SET_PATCH_NUM _IOW(WMT_IOC_MAGIC, 14, int)
74 	#define WMT_IOCTL_SET_PATCH_INFO _IOW(WMT_IOC_MAGIC, 15, char*)
75 	#define WMT_IOCTL_PORT_NAME _IOWR(WMT_IOC_MAGIC, 20, char*)
… 	
1203 	WMT_ERR_FUNC("allocate memory fail!\n");
1204 	break;
1205 	}
1206 	}
1207 	break;
1208 	
1209 	case WMT_IOCTL_SET_PATCH_INFO:{
1210 	WMT_PATCH_INFO wMtPatchInfo;
````
https://github.com/BPI-SINOVOIP/BPI-R2-bsp/blob/81776dada7beacfe4efbe3fbb16fbf909f94fe2e/linux-mt/drivers/misc/mediatek/connectivity/common/combo/linux/wmt_dev.c#L1209

crash:
drivers/misc/mediatek/connectivity/common/conn_soc/linux/pri/wmt_dev.c
````
1559     /* load patch file from fs */
1560     iRet = wmt_dev_read_file(pPatchName, (const PPUINT8)&pfw->data, 0, pad$
1561     set_fs(orig_fs);
1562
1563     cred->fsuid.val = orig_uid;
1564     cred->fsgid.val = orig_gid;
1565
1566
1567     if (iRet > 0) {
1568         pfw->size = iRet;
1569         *ppPatch = pfw;
1570         WMT_DBG_FUNC("load (%s) to addr(0x%p) success\n", pPatchName, pfw-$
1571         return 0;
1572     }
1573     kfree(pfw);
1574     *ppPatch = NULL;
1575     WMT_ERR_FUNC("load file (%s) fail, iRet(%d)\n", pPatchName, iRet);
1576     return -1;
````
drivers/misc/mediatek/connectivity/common/conn_soc/core/wmt_ctrl.c
````
 673     if (0 == wmt_dev_patch_get(pFileName, &pNvram, 0)) {
 674         *ppBuf = (PUINT8) (pNvram)->data;
 675         *pSize = (pNvram)->size;
 676         gDevWmt.pNvram = pNvram;
 677         return 0;
 678     }
````
