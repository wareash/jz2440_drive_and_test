#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <asm/uaccess.h>
#include <asm/irq.h>
#include <asm/io.h>
#include <asm/arch/regs-gpio.h>
#include <asm/hardware.h>

static struct class *firstdrv_class;
static struct class_device *firstdrv_class_dev;

volatile unsigned long *gpfcon = NULL ;
volatile unsigned long *gpfdat = NULL ;
static int first_drv_open(struct inode *inode, struct file *file)
{
	printk("first_drv_open\n");
	/*配置GPIO456为输出*/
	*gpfcon &= ~((0x3<<(4*2)) |(0x3<<(5*2)) | (0x3<<(6*2)) );  //清零
	*gpfcon |=   ((0x1<<(4*2)) |(0x1<<(5*2)) | (0x1<<(6*2)) );
	return 0;
}



static ssize_t first_drv_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
	int val;

	copy_from_user(&val, buf, count); //copy_to_user

	while(1);
	
	if(val==1)
	{
		//点灯	
		*gpfdat &= ~((1<<4) | (1<<5) | (1<<6));
	}
	else
	{
		//灭灯

		*gpfdat |= ((1<<4) | (1<<5) | (1<<6));

	}
	
	
	printk("first_drv_write\n");
	return 0;
}


static struct file_operations first_drv_fops  = {
		.owner = THIS_MODULE,
		.open   = first_drv_open,
		.write    = first_drv_write,
};

int major;
static int first_drv_int(void)
{
	major=register_chrdev(0, "first_dev", &first_drv_fops);

	firstdrv_class = class_create(THIS_MODULE,"first_dev"); 
	
	firstdrv_class_dev = class_device_create(firstdrv_class, NULL, MKDEV(major, 0), NULL, "xyz"); /* /dev/xyz */

	gpfcon = (volatile unsigned long*) ioremap(0x56000050, 16);

	gpfdat = gpfcon + 1;

	
	printk("first_drv_init\n");

	return 0;
}

asm_do_IRQ
void  first_drv_exit(void)
{
	unregister_chrdev(major, "first_dev");

	class_device_unregister(firstdrv_class_dev);

	class_destroy(firstdrv_class);

	iounmap(gpfcon);
	
	printk("first_drv_exit\n");

	return 0;
}

module_init(first_drv_int);
module_exit(first_drv_exit);

MODULE_LICENSE("GPL");


