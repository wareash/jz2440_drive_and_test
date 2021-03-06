   
#include <linux/module.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/ioport.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/clk.h>

#include <linux/mtd/mtd.h>
#include <linux/mtd/nand.h>
#include <linux/mtd/nand_ecc.h>
#include <linux/mtd/partitions.h>

#include <asm/io.h>

#include <asm/arch/regs-nand.h>
#include <asm/arch/nand.h>

static struct nand_chip *s3c_nand;
static struct mtd_info *s3c_mtd;


static void s3c_nand_select_chip(struct mtd_info *mtd, int chipnr)
{
	if(chipnr == -1) 
		{
			/*取消选中 : NFCONT[1]设为 0 */
//			chip->cmd_ctrl(mtd, NAND_CMD_NONE, 0 | NAND_CTRL_CHANGE);
//			break;
		}
	else
		{
			/*选中 : NFCONT[1]设为 1 */
		}

}
static void s3c_nand_cmd_ctrl(struct mtd_info *mtd, int dat, unsigned int ctrl)
{

	if (ctrl & NAND_CLE)
	{
		/* 发命令 : NFCMMD = dat */ 
		;
	}
	else
	{
		/* 发地址 : NFCADDR = dat */
		;
	}
}
static int s3c_nand_device_ready(struct mtd_info *mtd)
{

	return "NFSTAT的bit0";
}
static int s3c_nand_init(void)
{
	/*1. 分配一个nand_chip */
	s3c_nand= kzalloc(sizeof(struct nand_chip), GFP_KERNEL);
	
	/*2. 设置 */
	/*设置nand_chip是给nand_scan函数使用的，如果不知道怎么设置，先看nand_scan怎么使用*/
	s3c_nand->select_chip = s3c_nand_select_chip;
	s3c_nand->cmd_ctrl = s3c_nand_cmd_ctrl;
	s3c_nand->IO_ADDR_R = "NFDATA的虚拟地址";
	s3c_nand->IO_ADDR_W = "";
	s3c_nand->dev_ready = s3c_nand_device_ready;
	
	/*3. 硬件相关设置 */
	
	/*4. 使用:nand_scan */
	s3c_mtd = kzalloc(sizeof(struct mtd_info), GFP_KERNEL);
	s3c_mtd->priv = s3c_nand;
	s3c_mtd->owner = THIS_MODULE;
	
	nand_scan(s3c_mtd,1);   /*设别NAND FLASH ,构造mtd_info */
	
	/*5. add_mtd_partitions */
	return 0;
}

static void s3c_nand_exit(void)
{
}

module_init(s3c_nand_init);
module_exit(s3c_nand_exit);

MODULE_LICENSE("GPL");
