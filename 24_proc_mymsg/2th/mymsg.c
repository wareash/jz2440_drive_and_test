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
#include <linux/proc_fs.h>

static 	struct proc_dir_entry *myentry;

static ssize_t mymsg_read(struct file *file, char __user *buf,
			 size_t count, loff_t *ppos)
{
	printk("mymseg read\n");
	return 0;
}

const struct file_operations proc_mymsg_operations = {
	.read		= mymsg_read,
//	.poll		= kmsg_poll,
//	.open		= kmsg_open,
//	.release	= kmsg_release,
};


int major;
static int mymesg_int(void)
{
	myentry = create_proc_entry("mymsg", S_IRUSR, &proc_root);
	if (myentry)
		myentry->proc_fops = &proc_mymsg_operations;

	return 0;
}


void  mymesg_exit(void)
{
	remove_proc_entry("mymsg",&proc_root);
}

module_init(mymesg_int);
module_exit(mymesg_exit);

MODULE_LICENSE("GPL");



