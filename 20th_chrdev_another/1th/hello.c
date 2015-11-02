
#include <linux/module.h>
#include <linux/version.h>

#include <linux/init.h>
#include <linux/fs.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/sched.h>
#include <linux/pm.h>
#include <linux/sysctl.h>
#include <linux/proc_fs.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/input.h>
#include <linux/irq.h>

#include <asm/gpio.h>
#include <asm/io.h>
#include <asm/arch/regs-gpio.h>

#include <linux/types.h>
#include <linux/cdev.h>


/*1. 确定主设备号*/
static int major;

static int hello_open (struct inode *inode, struct file *file)
{
	printk("hello_open\n");
	return 0;
}


/*2. 构造file_operations结构体*/
static struct file_operations hello_fops = {
	.owner = THIS_MODULE,
	.open  = hello_open,
};


static struct cdev hello_cdv;
static struct class *cls;

#define HELLO_CNT 2
static int hello_init(void)
{		
	dev_t devid;
	/*3. 告诉内核*/
#if 0
	register_chrdev(0,"hello",&hello_fops); /*(major,0)...(major,255)都对应hello_fops*/
#else	
	if (major) {
			devid = MKDEV(major, 0);
			register_chrdev_region(devid, HELLO_CNT, "hello"); /*(major,0~1)对应hello_fops ，对应(major,2~255)不对应hello_fops*/
		} else {
			alloc_chrdev_region(&devid, 0, HELLO_CNT, "hello"); 
			major = MAJOR(devid);
		}

	cdev_init(&hello_cdv, &hello_fops);
	cdev_add(&hello_cdv, devid, HELLO_CNT);

#endif

	cls = class_create(THIS_MODULE,"hello");
	class_device_create(cls,NULL,MKDEV(major,0),NULL,"hello0");
	class_device_create(cls,NULL,MKDEV(major,1),NULL,"hello1");
	class_device_create(cls,NULL,MKDEV(major,2),NULL,"hello2");

	

	return 0;
}

static void hello_exit(void)
{
	class_device_destroy(cls,MKDEV(major,0));
	class_device_destroy(cls,MKDEV(major,1));
	class_device_destroy(cls,MKDEV(major,2));
	class_destroy(cls);

	cdev_del(&hello_cdv);
	unregister_chrdev_region(0,HELLO_CNT);
	
}
module_init(hello_init);
module_exit(hello_exit);
MODULE_LICENSE("GPL");
