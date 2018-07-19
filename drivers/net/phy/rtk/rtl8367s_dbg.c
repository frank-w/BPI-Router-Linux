#include <linux/trace_seq.h>
#include <linux/seq_file.h>
#include <linux/proc_fs.h>
#include <linux/u64_stats_sync.h>

#include  "./rtl8367c/include/rtk_switch.h"
#include  "./rtl8367c/include/port.h"
#include  "./rtl8367c/include/vlan.h"
#include  "./rtl8367c/include/rtl8367c_asicdrv_port.h"
#include  "./rtl8367c/include/stat.h"

static struct proc_dir_entry *proc_reg_dir;
static struct proc_dir_entry *proc_esw_cnt;
static struct proc_dir_entry *proc_vlan_cnt;

#define PROCREG_ESW_CNT         "esw_cnt"
#define PROCREG_VLAN            "vlan"
#define PROCREG_DIR             "rtk_gsw"

#define RTK_SW_VID_RANGE        16

static void rtk_dump_mib_type(rtk_stat_port_type_t cntr_idx)
{
	rtk_port_t port;
	rtk_stat_counter_t Cntr;

	for (port = UTP_PORT0; port < (UTP_PORT0 + 5); port++) {
		rtk_stat_port_get(port, cntr_idx, &Cntr);
		printk("%8llu", Cntr);
	}

	for (port = EXT_PORT0; port < (EXT_PORT0 + 2); port++) {
		rtk_stat_port_get(port, cntr_idx, &Cntr);
		printk("%8llu", Cntr);
	}
	
	printk("\n");
}
static void rtk_hal_dump_mib(void)
{

	printk("==================%8s%8s%8s%8s%8s%8s%8s\n", "Port0", "Port1",
	       "Port2", "Port3", "Port4", "Port16", "Port17");
	/* Get TX Unicast Pkts */
	printk("TX Unicast Pkts  :");
	rtk_dump_mib_type(STAT_IfOutUcastPkts);
	/* Get TX Multicast Pkts */
	printk("TX Multicast Pkts:");
	rtk_dump_mib_type(STAT_IfOutMulticastPkts);
	/* Get TX BroadCast Pkts */
	printk("TX BroadCast Pkts:");
	rtk_dump_mib_type(STAT_IfOutBroadcastPkts);
	/* Get TX Collisions */
	/* Get TX Puase Frames */
	printk("TX Pause Frames  :");
	rtk_dump_mib_type(STAT_Dot3OutPauseFrames);
	/* Get TX Drop Events */
	/* Get RX Unicast Pkts */
	printk("RX Unicast Pkts  :");
	rtk_dump_mib_type(STAT_IfInUcastPkts);
	/* Get RX Multicast Pkts */
	printk("RX Multicast Pkts:");
	rtk_dump_mib_type(STAT_IfInMulticastPkts);
	/* Get RX Broadcast Pkts */
	printk("RX Broadcast Pkts:");
	rtk_dump_mib_type(STAT_IfInBroadcastPkts);
	/* Get RX FCS Erros */
	printk("RX FCS Errors    :");
	rtk_dump_mib_type(STAT_Dot3StatsFCSErrors);
	/* Get RX Undersize Pkts */
	printk("RX Undersize Pkts:");
	rtk_dump_mib_type(STAT_EtherStatsUnderSizePkts);
	/* Get RX Discard Pkts */
	printk("RX Discard Pkts  :");
	rtk_dump_mib_type(STAT_Dot1dTpPortInDiscards);
	/* Get RX Fragments */
	printk("RX Fragments     :");
	rtk_dump_mib_type(STAT_EtherStatsFragments);
	/* Get RX Oversize Pkts */
	printk("RX Oversize Pkts :");
	rtk_dump_mib_type(STAT_EtherOversizeStats);
	/* Get RX Jabbers */
	printk("RX Jabbers       :");
	rtk_dump_mib_type(STAT_EtherStatsJabbers);
	/* Get RX Pause Frames */
	printk("RX Pause Frames  :");
	rtk_dump_mib_type(STAT_Dot3InPauseFrames);
	/* clear MIB */
	rtk_stat_global_reset();
	
}

static int rtk_hal_dump_vlan(void)
{
	rtk_vlan_cfg_t vlan;
	int i;

	printk("vid    portmap\n");
	for (i = 0; i < RTK_SW_VID_RANGE; i++) {
		rtk_vlan_get(i, &vlan);
		printk("%3d    ", i);
		printk("%c",
		       RTK_PORTMASK_IS_PORT_SET(vlan.mbr,
	                                UTP_PORT0) ? '1' : '-');
		printk("%c",
	       	RTK_PORTMASK_IS_PORT_SET(vlan.mbr,
	                                UTP_PORT1) ? '1' : '-');
		printk("%c",
		       RTK_PORTMASK_IS_PORT_SET(vlan.mbr,
	                                UTP_PORT2) ? '1' : '-');
		printk("%c",
		       RTK_PORTMASK_IS_PORT_SET(vlan.mbr,
	                                UTP_PORT3) ? '1' : '-');
		printk("%c",
	       	RTK_PORTMASK_IS_PORT_SET(vlan.mbr,
	                                UTP_PORT4) ? '1' : '-');
		printk("%c",
		       RTK_PORTMASK_IS_PORT_SET(vlan.mbr,
	                                EXT_PORT0) ? '1' : '-');
		printk("%c",
		       RTK_PORTMASK_IS_PORT_SET(vlan.mbr,
	                                EXT_PORT1) ? '1' : '-');
		printk("\n");
	}
	
	return 0;
}


static int esw_cnt_read(struct seq_file *seq, void *v)
{
	rtk_hal_dump_mib();
	return 0;
}

static int vlan_read(struct seq_file *seq, void *v)
{
	rtk_hal_dump_vlan();
	return 0;
}

static int switch_count_open(struct inode *inode, struct file *file)
{
	return single_open(file, esw_cnt_read, 0);
}

static int switch_vlan_open(struct inode *inode, struct file *file)
{
	return single_open(file, vlan_read, 0);
}

static const struct file_operations switch_count_fops = {
	.owner = THIS_MODULE,
	.open = switch_count_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release
};

static const struct file_operations switch_vlan_fops = {
	.owner = THIS_MODULE,
	.open = switch_vlan_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release
};


int gsw_debug_proc_init(void)
{

	if (!proc_reg_dir)
		proc_reg_dir = proc_mkdir(PROCREG_DIR, NULL);

	proc_esw_cnt =
	proc_create(PROCREG_ESW_CNT, 0, proc_reg_dir, &switch_count_fops);

	if (!proc_esw_cnt)
		pr_err("!! FAIL to create %s PROC !!\n", PROCREG_ESW_CNT);

	proc_vlan_cnt =
	proc_create(PROCREG_VLAN, 0, proc_reg_dir, &switch_vlan_fops);

	if (!proc_vlan_cnt)
		pr_err("!! FAIL to create %s PROC !!\n", PROCREG_VLAN);

	return 0;
}

void gsw_debug_proc_exit(void)
{
	if (proc_esw_cnt)
		remove_proc_entry(PROCREG_ESW_CNT, proc_reg_dir);
}


