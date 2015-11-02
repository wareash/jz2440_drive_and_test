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

#define MYLOG_BUF_LEN 100 

static 	struct proc_dir_entry *myentry;
static char mylog_buf [MYLOG_BUF_LEN];
static char temp_buf [MYLOG_BUF_LEN];
static int my_log_r = 0;
static int my_log_w = 0;

static DECLARE_WAIT_QUEUE_HEAD(mymesg_waitq);

static int is_mylog_empty( void)
{
	return (my_log_r==my_log_w);
}
static int is_mylog_full( void)
{
	return ((my_log_w+1) % MYLOG_BUF_LEN == my_log_r);		 
}
static void my_log_putc( char c)
{
	if(is_mylog_full())
	{
		my_log_r = (my_log_r + 1) % MYLOG_BUF_LEN;
	}

	mylog_buf[my_log_r] = c;
	my_log_w = (my_log_w + 1) % MYLOG_BUF_LEN;

	/*唤醒等待进程*/
	wake_up_interruptible(&mymesg_waitq);
	
}
static int my_log_getc(char *p)
{
	if(is_mylog_full())
	{
		return 0;
	}
	*p = mylog_buf[my_log_r];
	my_log_r = (my_log_r + 1) % MYLOG_BUF_LEN;
	return 1;
}
int myprintk(const char *fmt, ...)
{
	va_list args;
	int i;
	int j;
	
	va_start(args, fmt);
	i=vsnprintf(temp_buf, INT_MAX, fmt, args);
	va_end(args);

	for (j = 0;j < i;j++)
		my_log_putc(temp_buf[j]);
	
	return i;
}

static ssize_t mymsg_read(struct file *file, char __user *buf,
			 size_t count, loff_t *ppos)
{
	int error = 0;
	int i = 0;
	char c;

	/*把mylog_buf里面的数据copy_to_usr,return*/
	if ((file->f_flags & O_NONBLOCK) && is_mylog_full())
		return -EAGAIN;

	error = wait_event_interruptible(mymesg_waitq, !is_mylog_empty());

	/*cope_to_user*/
	while (!error && (my_log_getc(&c)) && i < count) {
		__put_user(c,buf);
		buf++;
		i++;
	}
	if (!error)
		error = i;		
	
	return error;
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
	sprintf(mylog_buf,"sadasdadasdsadasdsa");
	
	myentry = create_proc_entry("mymsg", S_IRUSR, &proc_root);
	if (myentry)
		myentry->proc_fops = &proc_mymsg_operations;

	return 0;
}

EXPORT_SYMBOL(myprintk);

void  mymesg_exit(void)
{
	remove_proc_entry("mymsg",&proc_root);
}

module_init(mymesg_int);
module_exit(mymesg_exit);

MODULE_LICENSE("GPL");



