#include <linux/module.h>

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/timer.h>
#include <linux/init.h>
#include <linux/serial_core.h>
#include <linux/platform_device.h>


#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>
#include <asm/arch/fb.h>
#include <asm/hardware.h>
#include <asm/io.h>
#include <asm/irq.h>
/* 分配、设置、注册一个 platform_device 平台 */

static struct resource led_resource[] = {
	[0] = {
		.start = 0x56000050,
		.end   = 0x56000050 +8 - 1 ,
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.start = 4,
		.end   = 4,
		.flags = IORESOURCE_IRQ,
	}

};

static void  led_release (struct device * dev)
{
}

struct platform_device led_dev = {
	.name		  = "my_led",
	.id		  = -1,
	.num_resources	  = ARRAY_SIZE(led_resource),
	.resource	  = led_resource,
	.dev    	= { 
		.release = led_release, 
	},
};


static int led_dev_init(void)
{
	platform_device_register(&led_dev);
	return 0;
}

static void led_dev_exit(void)
{
	platform_device_unregister(&led_dev);
}

module_init(led_dev_init);
module_exit(led_dev_exit);

MODULE_LICENSE("GPL");

