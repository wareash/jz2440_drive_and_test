

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/irq.h>
#include <asm/uaccess.h>
#include <asm/irq.h>
#include <asm/io.h>
#include <asm/arch/regs-gpio.h>
#include <asm/hardware.h>
#include <linux/poll.h>

static int major = 0;
static  struct class *cls;

#define MEM_COY_NO_DMA 0
#define MEM_CPY_DMA    1

static char *src;
static u32 src_phys;

static char *dst;
static u32 dst_phys;

#define BUF_SIZE (512*1024)

static int s3c_dma_ioctl (struct inode *inode, struct file *file, unsigned int cmd, unsigned long long);
{
	int i;

	memset(src, 0xAA, BUF_SIZE);
	memset(dst, 0x55, BUF_SIZE);
	
	switch (cmd)
	{
		case MEM_COY_NO_DMA:
		{
			for (i=0; i<BUF_SIZE; i++)
				dst[i] = src[i];
			if(memcmp(src, dst, BUF_SIZE) == 0)
			{
				printk("MEM_CPY_NO_DMA OK !\n");
			}
			else
			{
				printk("MEM_CPY_NO_DMA error !\n");
			}
			
			break;
		}
		case MEM_CPY_DMA:
		{
			break;
		}
	}
	
	return 0;
}

static struct file_operations dma_ops = {
	.owner = THIS_MODULE,
	.ioctl = s3c_dma_ioctl,

};
static int s3c_dma_init(void)
{
	/*分配SRC,DST对应的缓冲区 ，不能用kmalloc*/
	src = dma_alloc_writecombine(NULL, BUF_SIZE, &src_phys, GFP_KERNEL);
	if(NULL == src) 
	{
		printk("can't alloc buffer for src\n");
		return -ENOMEM;
	}
	
	dst = dma_alloc_writecombine(NULL, BUF_SIZE, &dst_phys, GFP_KERNEL);
	if(NULL == dst) 
	{
		dma_free_writecombine(NULL, BUF_SIZE, src , &src_phys);
		printk("can't alloc buffer for dst\n");
		return -ENOMEM;
	}
	
	major = register_chrdev(0,"s3c_dma",&dma_ops);

	/*为了自动创建设备节点*/
	cls = class_create(THIS_MODULE,"s3c_dma");
	class_device_create(cls, NULL, MKDEV(major,0), NULL, "dma");  /* /dev/dma */
	
	return 0;
}

static void s3c_dma_exit(void)
{
	class_device_destroy(cls, MKDEV(major,0));
	class_destroy( cls);
	unregister_chrdev(0, "s3c_dma");
	dma_free_writecombine(NULL, BUF_SIZE, dst , &dst_phys);
	dma_free_writecombine(NULL, BUF_SIZE, src , &src_phys);
}

module_init(s3c_dma_init);
module_exit(s3c_dma_exit);
MODULE_LICENSE("GPL");



