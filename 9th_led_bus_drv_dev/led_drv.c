
#include <linux/module.h>

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/timer.h>
#include <linux/init.h>
#include <linux/serial_core.h>
#include <linux/platform_device.h>
#include <asm/uaccess.h>


#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>
#include <asm/arch/fb.h>
#include <asm/hardware.h>
#include <asm/io.h>
#include <asm/irq.h>

/* 分配、设置、注册一个 platform_driver 平台 */

static int major;
static struct class *cls;
static volatile unsigned long *gpio_con;
static volatile unsigned long *gpio_dat;
static int pin;

static int led_open(struct inode *inode, struct file *file)
{
//	printk("first_drv_open\n");
	/*配置GPIO为输出*/
	*gpio_con &= ~(0x3<<(pin*2));  //清零
	*gpio_con |=   0x1<<(pin*2);
	return 0;
}
static ssize_t led_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
	int val;

	copy_from_user(&val, buf, count); //copy_to_user

	if(val==1)
	{
		//点灯	
		*gpio_dat &= ~((1<<pin) );
	}
	else
	{
		//灭灯
		*gpio_dat |=  ((1<<pin ));
	}
	
//	printk("first_drv_write\n");
	return 0;
}

static struct file_operations led_fops = {
		.owner = THIS_MODULE,
		.open   = led_open,
		.write    = led_write,	
};

static int  led_probe(struct platform_device *pdev)
{
	/*根据 platform_device 的资源进行ioremap*/
	struct resource *res;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	gpio_con = ioremap(res->start, res->end - res->start +1);
	gpio_dat = gpio_con +1;

	res = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	pin = res->start;
	
	/*注册字符驱动程序*/
	
	printk("led_probe,found Led \n");

	major = register_chrdev(0, "my_led",& led_fops);

	cls = class_create(THIS_MODULE,"my_led"); 
	
	class_device_create(cls, NULL, MKDEV(major, 0), NULL, "led"); /* /dev/xyz */

	return 0;
}
static int  led_remove(struct platform_device *pdev)
{
	/*根据 platform_device 的资源进行iounmap*/

	/*卸载字符驱动程序*/
	class_device_destroy(cls, MKDEV(major, 0));
	class_destroy(cls);

	unregister_chrdev(major, "my_led");
	iounmap(gpio_con);
	printk("led_remove,remove Led \n");
	return 0;
}



struct platform_driver led_drv = {
	.probe		= led_probe,
	.remove		= led_remove,
	.driver		= {
		.name	= "my_led",
	}
};


static int led_drv_init(void)
{
	platform_driver_register(&led_drv);
	return 0;
}

static void led_drv_exit(void)
{

	platform_driver_unregister(&led_drv);

}

module_init(led_drv_init);
module_exit(led_drv_exit);

MODULE_LICENSE("GPL");


