/*
 *参考 Physmap.c
 */

 
#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/map.h>
#include <linux/mtd/partitions.h>
#include <asm/io.h>

static struct map_info *s3c_nor_map;
static struct mtd_info *s3c_nor_mtd;

static int s3c_nor_init(void)
{
	/*1. 分配一个map_info 结构体*/	
	s3c_nor_map = kzalloc(sizeof(struct map_info), GFP_KERNEL);
	
	/*2. 设置: 物理基地址(phys), 大小(size)，位宽(bankwith)，虚拟基地址 (virt)*/
	s3c_nor_map->name = "s3c_nor";
	s3c_nor_map->phys = 0;
	s3c_nor_map->size = 0x1000000; /*>=Nor 的真实大小*/
	s3c_nor_map->bankwidth = 2;
	s3c_nor_map->virt = ioremap(s3c_nor_map->phys, s3c_nor_map->size);

	simple_map_init(s3c_nor_map);
	
	/*3. 使用: 调用NOR FLASH协议层提供的函数；来识别*/
	printk("use cfi_probe\n");
	s3c_nor_mtd = do_map_probe("cfi_probe", s3c_nor_map);

	if(!s3c_nor_mtd)
	{
		printk("use jedec_probe\n");
		s3c_nor_mtd = do_map_probe("jedec_probe", s3c_nor_map);
	}
	if(!s3c_nor_mtd)
	{
		iounmap(s3c_nor_map->virt);
		kfree(s3c_nor_map);
		return -EIO;
	}
	/*4. add_mtd_partitions*/

	return 0;
}

static void s3c_nor_exit(void)
{
	iounmap(s3c_nor_map->virt);
	kfree(s3c_nor_map);
}

module_init(s3c_nor_init);
module_exit(s3c_nor_exit);

MODULE_LICENSE("GPL");

