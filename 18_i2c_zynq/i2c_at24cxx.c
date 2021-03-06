/*
 * Xilinx I2C bus driver for the PS I2C Interfaces.
 *
 * 2009-2011 (c) Xilinx, Inc.
 *
 * This program is free software; you can redistribute it
 * and/or modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation;
 * either version 2 of the License, or (at your option) any
 * later version.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the Free
 * Software Foundation, Inc., 675 Mass Ave, Cambridge, MA
 * 02139, USA.
 *
 *
 * Workaround in Receive Mode
 *	If there is only one message to be processed, then based on length of
 *	the message we set the HOLD bit.
 *	If the length is less than the FIFO depth, then we will directly
 *	receive a COMP interrupt and the transaction is done.
 *	If the length is more than the FIFO depth, then we enable the HOLD bit
 *	and write FIFO depth to the transfer size register.
 *	We will receive the DATA interrupt, we calculate the remaining bytes
 *	to receive and write to the transfer size register and we process the
 *	data in FIFO.
 *	In the meantime, we are receiving the complete interrupt also and the
 *	controller waits for the default timeout period before generating a stop
 *	condition even though the HOLD bit is set. So we are unable to generate
 *	the data interrupt again.
 *	To avoid this, we wrote the expected bytes to receive as FIFO depth + 1
 *	instead of FIFO depth. This generated the second DATA interrupt as there
 *	are still outstanding bytes to be received.
 *
 *	The bus hold flag logic provides support for repeated start.
 *
 */

#include <linux/io.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/xilinx_devices.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/of_i2c.h>

/*
 * Register Map
 * Register offsets for the I2C device.
 */
#define XI2CPS_CR_OFFSET	0x00 /* Control Register, RW */
#define XI2CPS_SR_OFFSET	0x04 /* Status Register, RO */
#define XI2CPS_ADDR_OFFSET	0x08 /* I2C Address Register, RW */
#define XI2CPS_DATA_OFFSET	0x0C /* I2C Data Register, RW */
#define XI2CPS_ISR_OFFSET	0x10 /* Interrupt Status Register, RW */
#define XI2CPS_XFER_SIZE_OFFSET 0x14 /* Transfer Size Register, RW */
#define XI2CPS_SLV_PAUSE_OFFSET 0x18 /* Slave monitor pause Register, RW */
#define XI2CPS_TIME_OUT_OFFSET	0x1C /* Time Out Register, RW */
#define XI2CPS_IMR_OFFSET	0x20 /* Interrupt Mask Register, RO */
#define XI2CPS_IER_OFFSET	0x24 /* Interrupt Enable Register, WO */
#define XI2CPS_IDR_OFFSET	0x28 /* Interrupt Disable Register, WO */

/*
 * Control Register Bit mask definitions
 * This register contains various control bits that affect the operation of the
 * I2C controller.
 */
#define XI2CPS_CR_HOLD_BUS_MASK 0x00000010 /* Hold Bus bit */
#define XI2CPS_CR_RW_MASK	0x00000001 /* Read or Write Master transfer
					    * 0= Transmitter, 1= Receiver */
#define XI2CPS_CR_CLR_FIFO_MASK 0x00000040 /* 1 = Auto init FIFO to zeroes */

/*
 * I2C Address Register Bit mask definitions
 * Normal addressing mode uses [6:0] bits. Extended addressing mode uses [9:0]
 * bits. A write access to this register always initiates a transfer if the I2C
 * is in master mode.
 */
#define XI2CPS_ADDR_MASK	0x000003FF /* I2C Address Mask */

/*
 * I2C Interrupt Registers Bit mask definitions
 * All the four interrupt registers (Status/Mask/Enable/Disable) have the same
 * bit definitions.
 */
#define XI2CPS_IXR_ALL_INTR_MASK 0x000002FF /* All ISR Mask */

#define XI2CPS_FIFO_DEPTH	16		/* FIFO Depth */
#define XI2CPS_TIMEOUT		(50 * HZ)	/* Timeout for bus busy check */
#define XI2CPS_ENABLED_INTR	0x2EF		/* Enabled Interrupts */

#define XI2CPS_DATA_INTR_DEPTH (XI2CPS_FIFO_DEPTH - 2)/* FIFO depth at which
							 * the DATA interrupt
							 * occurs
							 */

#define DRIVER_NAME		"xi2cps"

#define xi2cps_readreg(offset)		__raw_readl(id->membase + offset)
#define xi2cps_writereg(val, offset)	__raw_writel(val, id->membase + offset)

/**
 * struct xi2cps - I2C device private data structure
 * @membase:		Base address of the I2C device
 * @adap:		I2C adapter instance
 * @p_msg:		Message pointer
 * @err_status:		Error status in Interrupt Status Register
 * @xfer_done:		Transfer complete status
 * @p_send_buf:		Pointer to transmit buffer
 * @p_recv_buf:		Pointer to receive buffer
 * @send_count:		Number of bytes still expected to send
 * @recv_count:		Number of bytes still expected to receive
 * @irq:		IRQ number
 * @cur_timeout:	The current timeout value used by the device
 * @input_clk:		Input clock to I2C controller
 * @bus_hold_flag:	Flag used in repeated start for clearing HOLD bit
 */
struct xi2cps {
	void __iomem *membase;
	struct i2c_adapter adap;
	struct i2c_msg	*p_msg;
	int err_status;
	struct completion xfer_done;
	unsigned char *p_send_buf;
	unsigned char *p_recv_buf;
	int send_count;
	int recv_count;
	int irq;
	int cur_timeout;
	unsigned int input_clk;
	unsigned int bus_hold_flag;
};

/**
 * xi2cps_isr - Interrupt handler for the I2C device
 * @irq:	irq number for the I2C device
 * @ptr:	void pointer to xi2cps structure
 *
 * Returns IRQ_HANDLED always
 *
 * This function handles the data interrupt, transfer complete interrupt and
 * the error interrupts of the I2C device.
 */
static irqreturn_t xi2cps_isr(int irq, void *ptr)
{
	unsigned int isr_status, avail_bytes;
	unsigned int bytes_to_recv, bytes_to_send;
	unsigned int ctrl_reg = 0;
	struct xi2cps *id = ptr;

	isr_status = xi2cps_readreg(XI2CPS_ISR_OFFSET);

	/* Handling Nack interrupt */
	if (isr_status & 0x00000004)
		complete(&id->xfer_done);

	/* Handling Arbitration lost interrupt */
	if (isr_status & 0x00000200)
		complete(&id->xfer_done);

	/* Handling Data interrupt */
	if (isr_status & 0x00000002) {
		/*
		 * In master mode, if the device has more data to receive.
		 * Calculate received bytes and update the receive count.
		 */
		if ((id->recv_count) > XI2CPS_FIFO_DEPTH) {
			bytes_to_recv = (XI2CPS_FIFO_DEPTH + 1) -
				xi2cps_readreg(XI2CPS_XFER_SIZE_OFFSET);
			id->recv_count -= bytes_to_recv;
		/*
		 * Calculate the expected bytes to be received further and
		 * update in transfer size register. If the expected bytes
		 * count is less than FIFO size then clear hold bit if there
		 * are no further messages to be processed
		 */
			if (id->recv_count > XI2CPS_FIFO_DEPTH)
				xi2cps_writereg(XI2CPS_FIFO_DEPTH + 1,
						XI2CPS_XFER_SIZE_OFFSET);
			else {
				xi2cps_writereg(id->recv_count,
						XI2CPS_XFER_SIZE_OFFSET);
				if (id->bus_hold_flag == 0)
					/* Clear the hold bus bit */
					xi2cps_writereg(
					(xi2cps_readreg(XI2CPS_CR_OFFSET) &
					(~XI2CPS_CR_HOLD_BUS_MASK)),
					XI2CPS_CR_OFFSET);
			}
			/* Process the data received */
			while (bytes_to_recv) {
				*(id->p_recv_buf)++ =
					xi2cps_readreg(XI2CPS_DATA_OFFSET);
				bytes_to_recv = bytes_to_recv - 1;
			}
		}
	}

	/* Handling Transfer Complete interrupt */
	if (isr_status & 0x00000001) {
		if ((id->p_recv_buf) == NULL) {
			/*
			 * If the device is sending data If there is further
			 * data to be sent. Calculate the available space
			 * in FIFO and fill the FIFO with that many bytes.
			 */
			if (id->send_count > 0) {
				avail_bytes = XI2CPS_FIFO_DEPTH -
				xi2cps_readreg(XI2CPS_XFER_SIZE_OFFSET);
				if (id->send_count > avail_bytes)
					bytes_to_send = avail_bytes;
				else
					bytes_to_send = id->send_count;

				while (bytes_to_send--) {
					xi2cps_writereg(
						(*(id->p_send_buf)++),
						 XI2CPS_DATA_OFFSET);
					id->send_count--;
				}
			} else {
		/*
		 * Signal the completion of transaction and clear the hold bus
		 * bit if there are no further messages to be processed.
		 */
				complete(&id->xfer_done);
			}
			if (id->send_count == 0) {
				if (id->bus_hold_flag == 0) {
					/* Clear the hold bus bit */
					ctrl_reg =
					xi2cps_readreg(XI2CPS_CR_OFFSET);
					if ((ctrl_reg & XI2CPS_CR_HOLD_BUS_MASK)
						== XI2CPS_CR_HOLD_BUS_MASK)
						xi2cps_writereg(
						(ctrl_reg &
						(~XI2CPS_CR_HOLD_BUS_MASK)),
						XI2CPS_CR_OFFSET);
				}
			}
		} else {
			if (id->bus_hold_flag == 0) {
				/* Clear the hold bus bit */
				ctrl_reg =
				xi2cps_readreg(XI2CPS_CR_OFFSET);
				if ((ctrl_reg & XI2CPS_CR_HOLD_BUS_MASK)
					== XI2CPS_CR_HOLD_BUS_MASK)
					xi2cps_writereg(
					(ctrl_reg &
					(~XI2CPS_CR_HOLD_BUS_MASK)),
					XI2CPS_CR_OFFSET);
			}
		/*
		 * If the device is receiving data, then signal the completion
		 * of transaction and read the data present in the FIFO.
		 * Signal the completion of transaction.
		 */
			while (xi2cps_readreg(XI2CPS_SR_OFFSET)
							& 0x00000020) {
				*(id->p_recv_buf)++ =
				xi2cps_readreg(XI2CPS_DATA_OFFSET);
				id->recv_count--;
			}
			complete(&id->xfer_done);
		}
	}

	/* Update the status for errors */
	id->err_status = isr_status & 0x000002EC;
	xi2cps_writereg(isr_status, XI2CPS_ISR_OFFSET);
	return IRQ_HANDLED;
}

/**
 * xi2cps_mrecv - Prepare and start a master receive operation
 * @id:		pointer to the i2c device structure
 *
 */
static void xi2cps_mrecv(struct xi2cps *id)
{
	unsigned int ctrl_reg;
	unsigned int isr_status;

	id->p_recv_buf = id->p_msg->buf;
	id->recv_count = id->p_msg->len;

	/*
	 * Set the controller in master receive mode and clear the FIFO.
	 * Set the slave address in address register.
	 * Check for the message size against FIFO depth and set the
	 * HOLD bus bit if it is more than FIFO depth.
	 * Clear the interrupts in interrupt status register.
	 */
	ctrl_reg = xi2cps_readreg(XI2CPS_CR_OFFSET);
	ctrl_reg |= (XI2CPS_CR_RW_MASK | XI2CPS_CR_CLR_FIFO_MASK);

	if (id->recv_count > XI2CPS_FIFO_DEPTH)
		ctrl_reg |= XI2CPS_CR_HOLD_BUS_MASK;

	xi2cps_writereg(ctrl_reg, XI2CPS_CR_OFFSET);

	isr_status = xi2cps_readreg(XI2CPS_ISR_OFFSET);
	xi2cps_writereg(isr_status, XI2CPS_ISR_OFFSET);

	xi2cps_writereg((id->p_msg->addr & XI2CPS_ADDR_MASK),
						XI2CPS_ADDR_OFFSET);
	/*
	 * The no. of bytes to receive is checked against the limit of
	 * FIFO depth. Set transfer size register with no. of bytes to
	 * receive if it is less than FIFO depth and FIFO depth + 1 if
	 * it is more. Enable the interrupts.
	 */
	if (id->recv_count > XI2CPS_FIFO_DEPTH)
		xi2cps_writereg(XI2CPS_FIFO_DEPTH + 1,
				XI2CPS_XFER_SIZE_OFFSET);
	else {
		xi2cps_writereg(id->recv_count, XI2CPS_XFER_SIZE_OFFSET);

	/*
	 * Clear the bus hold flag if bytes to receive is less than FIFO size.
	 */
		if (id->bus_hold_flag == 0) {
			/* Clear the hold bus bit */
			ctrl_reg = xi2cps_readreg(XI2CPS_CR_OFFSET);
			if ((ctrl_reg & XI2CPS_CR_HOLD_BUS_MASK)
				== XI2CPS_CR_HOLD_BUS_MASK)
				xi2cps_writereg(
				(ctrl_reg & (~XI2CPS_CR_HOLD_BUS_MASK)),
				XI2CPS_CR_OFFSET);
		}
	}
	xi2cps_writereg(XI2CPS_ENABLED_INTR, XI2CPS_IER_OFFSET);
}

/**
 * xi2cps_msend - Prepare and start a master send operation
 * @id:		pointer to the i2c device
 *
 */
static void xi2cps_msend(struct xi2cps *id)
{
	unsigned int avail_bytes;
	unsigned int bytes_to_send;
	unsigned int ctrl_reg;
	unsigned int isr_status;

	id->p_recv_buf = NULL;
	id->p_send_buf = id->p_msg->buf;
	id->send_count = id->p_msg->len;

	/*
	 * Set the controller in Master transmit mode and clear the FIFO.
	 * Set the slave address in address register.
	 * Check for the message size against FIFO depth and set the
	 * HOLD bus bit if it is more than FIFO depth.
	 * Clear the interrupts in interrupt status register.
	 */
	ctrl_reg = xi2cps_readreg(XI2CPS_CR_OFFSET);
	ctrl_reg &= ~XI2CPS_CR_RW_MASK;
	ctrl_reg |= XI2CPS_CR_CLR_FIFO_MASK;

	if ((id->send_count) > XI2CPS_FIFO_DEPTH)
		ctrl_reg |= XI2CPS_CR_HOLD_BUS_MASK;
	xi2cps_writereg(ctrl_reg, XI2CPS_CR_OFFSET);

	isr_status = xi2cps_readreg(XI2CPS_ISR_OFFSET);
	xi2cps_writereg(isr_status, XI2CPS_ISR_OFFSET);

	/*
	 * Calculate the space available in FIFO. Check the message length
	 * against the space available, and fill the FIFO accordingly.
	 * Enable the interrupts.
	 */
	avail_bytes = XI2CPS_FIFO_DEPTH -
				xi2cps_readreg(XI2CPS_XFER_SIZE_OFFSET);

	if (id->send_count > avail_bytes)
		bytes_to_send = avail_bytes;
	else
		bytes_to_send = id->send_count;

	while (bytes_to_send--) {
		xi2cps_writereg((*(id->p_send_buf)++), XI2CPS_DATA_OFFSET);
		id->send_count--;
	}

	xi2cps_writereg((id->p_msg->addr & XI2CPS_ADDR_MASK),
						XI2CPS_ADDR_OFFSET);

	/*
	 * Clear the bus hold flag if there is no more data
	 * and if it is the last message.
	 */
	if (id->bus_hold_flag == 0 && id->send_count == 0) {
		/* Clear the hold bus bit */
		ctrl_reg = xi2cps_readreg(XI2CPS_CR_OFFSET);
		if ((ctrl_reg & XI2CPS_CR_HOLD_BUS_MASK)
			== XI2CPS_CR_HOLD_BUS_MASK)
			xi2cps_writereg(
			(ctrl_reg & (~XI2CPS_CR_HOLD_BUS_MASK)),
			XI2CPS_CR_OFFSET);
	}
	xi2cps_writereg(XI2CPS_ENABLED_INTR, XI2CPS_IER_OFFSET);
}

/**
 * xi2cps_master_xfer - The main i2c transfer function
 * @adap:	pointer to the i2c adapter driver instance
 * @msgs:	pointer to the i2c message structure
 * @num:	the number of messages to transfer
 *
 * Returns number of msgs processed on success, negative error otherwise
 *
 * This function waits for the bus idle condition and updates the timeout if
 * modified by user. Then initiates the send/recv activity based on the
 * transfer message received.
 */
static int xi2cps_master_xfer(struct i2c_adapter *adap, struct i2c_msg *msgs,
				int num)
{
	struct xi2cps *id = adap->algo_data;
	unsigned int count, retries;
	unsigned long timeout;

	/* Waiting for bus-ready. If bus not ready, it returns after timeout */
	timeout = jiffies + XI2CPS_TIMEOUT;
	while ((xi2cps_readreg(XI2CPS_SR_OFFSET)) & 0x00000100) {
		if (time_after(jiffies, timeout)) {
			dev_warn(id->adap.dev.parent,
					"timedout waiting for bus ready\n");
			return -ETIMEDOUT;
		}
		schedule_timeout(1);
	}


	/* The bus is free. Set the new timeout value if updated */
	if (id->adap.timeout != id->cur_timeout) {
		xi2cps_writereg((id->adap.timeout & 0xFF),
					XI2CPS_TIME_OUT_OFFSET);
		id->cur_timeout = id->adap.timeout;
	}

	/*
	 * Set the flag to one when multiple messages are to be
	 * processed with a repeated start.
	 */
	if (num > 1) {
		id->bus_hold_flag = 1;
		xi2cps_writereg((xi2cps_readreg(XI2CPS_CR_OFFSET) |
				XI2CPS_CR_HOLD_BUS_MASK), XI2CPS_CR_OFFSET);
	} else
		id->bus_hold_flag = 0;

	/* Process the msg one by one */
	for (count = 0; count < num; count++, msgs++) {

		if (count == (num - 1))
			id->bus_hold_flag = 0;
		retries = adap->retries;
retry:
		id->err_status = 0;
		id->p_msg = msgs;
		init_completion(&id->xfer_done);

		/* Check for the TEN Bit mode on each msg */
		if (msgs->flags & I2C_M_TEN)
			xi2cps_writereg((xi2cps_readreg(XI2CPS_CR_OFFSET) &
					(~0x00000004)), XI2CPS_CR_OFFSET);
		else {
			if ((xi2cps_readreg(XI2CPS_CR_OFFSET) & 0x00000004)
								== 0)
				xi2cps_writereg(
					(xi2cps_readreg(XI2CPS_CR_OFFSET) |
					 (0x00000004)), XI2CPS_CR_OFFSET);
		}

		/* Check for the R/W flag on each msg */
		if (msgs->flags & I2C_M_RD)
			xi2cps_mrecv(id);
		else
			xi2cps_msend(id);

		/* Wait for the signal of completion */
		wait_for_completion_interruptible(&id->xfer_done);
		xi2cps_writereg(XI2CPS_IXR_ALL_INTR_MASK, XI2CPS_IDR_OFFSET);

		/* If it is bus arbitration error, try again */
		if (id->err_status & 0x00000200) {
			dev_dbg(id->adap.dev.parent,
				 "Lost ownership on bus, trying again\n");
			if (retries--) {
				mdelay(2);
				goto retry;
			}
			dev_err(id->adap.dev.parent,
					 "Retries completed, exit\n");
			num = -EREMOTEIO;
			break;
		}
		/* Report the other error interrupts to application as EIO */
		if (id->err_status & 0x000000E4) {
			num = -EIO;
			break;
		}
	}

	id->p_msg = NULL;
	id->err_status = 0;

	return num;
}

/**
 * xi2cps_func - Returns the supported features of the I2C driver
 * @adap:	pointer to the i2c adapter structure
 *
 * Returns 32 bit value, each bit corresponding to a feature
 */
static u32 xi2cps_func(struct i2c_adapter *adap)
{
	return I2C_FUNC_I2C | I2C_FUNC_10BIT_ADDR | \
		(I2C_FUNC_SMBUS_EMUL & ~I2C_FUNC_SMBUS_QUICK);
}

static const struct i2c_algorithm xi2cps_algo = {
	.master_xfer	= xi2cps_master_xfer,
	.functionality	= xi2cps_func,
};


/**
 * xi2cps_setclk - This function sets the serial clock rate for the I2C device
 * @fscl:	The clock frequency in Hz
 * @id:		Pointer to the I2C device structure
 *
 * Returns zero on success, negative error otherwise
 *
 * The device must be idle rather than busy transferring data before setting
 * these device options.
 * The data rate is set by values in the control register.
 * The formula for determining the correct register values is
 *	Fscl = Fpclk/(22 x (divisor_a+1) x (divisor_b+1))
 * See the hardware data sheet for a full explanation of setting the serial
 * clock rate. The clock can not be faster than the input clock divide by 22.
 * The two most common clock rates are 100KHz and 400KHz.
 */
static int xi2cps_setclk(int fscl, struct xi2cps *id)
{
	unsigned int div_a, div_b, calc_div_a = 0;
	unsigned int best_div_a = 0, best_div_b = 0;
	unsigned int last_error = 0, current_error = 0;
	unsigned int actual_fscl, temp;
	unsigned int ctrl_reg;


	/* Assume div_a is 0 and calculate (divisor_a+1) x (divisor_b+1) */
	temp = id->input_clk / (22 * fscl);

	/*
	 * If the calculated value is negative or 0, the fscl input is out of
	 * range. Return error.
	 */
	if (temp == 0)
		return -EINVAL;
	last_error = fscl;
	for (div_b = 0; div_b < 64; div_b++) {
		calc_div_a = (temp / (div_b + 1));
		if (calc_div_a == 0)
			div_a = calc_div_a;
		else
			div_a = calc_div_a - 1;
		actual_fscl = id->input_clk / (22 * (div_a + 1) * (div_b + 1));

		if (div_a > 3)
			continue;
		current_error = ((actual_fscl > fscl) ? (actual_fscl - fscl) :
							(fscl - actual_fscl));
		if ((last_error > current_error) && (actual_fscl <= fscl)) {
			best_div_a = div_a;
			best_div_b = div_b;
			last_error = current_error;
		}
	}

	ctrl_reg = xi2cps_readreg(XI2CPS_CR_OFFSET);
	ctrl_reg &= ~(0x0000C000 | 0x00003F00);
	ctrl_reg |= ((best_div_a << 14) | (best_div_b << 8));
	xi2cps_writereg(ctrl_reg, XI2CPS_CR_OFFSET);

	return 0;
}

/************************/
/* Platform bus binding */
/************************/

/**
 * xi2cps_probe - Platform registration call
 * @pdev:	Handle to the platform device structure
 *
 * Returns zero on success, negative error otherwise
 *
 * This function does all the memory allocation and registration for the i2c
 * device. User can modify the address mode to 10 bit address mode using the
 * ioctl call with option I2C_TENBIT.
 */
static int __devinit xi2cps_probe(struct platform_device *pdev)
{
	struct resource *r_mem = NULL;
	struct xi2cps *id;
	unsigned int i2c_clk;
	int ret;

	struct xi2cps_platform_data *pdata;

	/*
	 * Allocate memory for xi2cps structure.
	 * Initialize the structure to zero and set the platform data.
	 * Obtain the resource base address from platform data and remap it.
	 * Get the irq resource from platform data.Initialize the adapter
	 * structure members and also xi2cps structure.
	 */
	id = kzalloc(sizeof(struct xi2cps), GFP_KERNEL);
	if (!id) {
		dev_err(&pdev->dev, "no mem for i2c private data\n");
		return -ENOMEM;
	}
	memset((void *)id, 0, sizeof(struct xi2cps));
	platform_set_drvdata(pdev, id);


	r_mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!r_mem) {
		dev_err(&pdev->dev, "no mmio resources\n");
		ret = -ENODEV;
		goto err_free_mem;
	}

	id->membase = ioremap(r_mem->start, r_mem->end - r_mem->start + 1);
	if (id->membase == NULL) {
		dev_err(&pdev->dev, "Couldn't ioremap memory at 0x%08lx\n",
			(unsigned long)r_mem->start);
		ret = -ENOMEM;
		goto err_free_mem;
	}

	id->irq = platform_get_irq(pdev, 0);
	if (id->irq < 0) {
		dev_err(&pdev->dev, "no IRQ resource:%d\n", id->irq);
		ret = -ENXIO;
		goto err_unmap;
	}

	id->adap.nr = pdev->id;

	id->adap.algo = (struct i2c_algorithm *) &xi2cps_algo;
	id->adap.timeout = 0x1F;	/* Default timeout value */
	id->adap.retries = 3;		/* Default retry value. */
	id->adap.algo_data = id;
	id->adap.dev.parent = &pdev->dev;
	snprintf(id->adap.name, sizeof(id->adap.name),
		 "XILINX I2C at %08lx", (unsigned long)r_mem->start);

	id->cur_timeout = id->adap.timeout;

	id->input_clk = pdata->input_clk;
	i2c_clk = pdata->i2c_clk;


	/*
	 * Set Master Mode,Normal addressing mode (7 bit address),
	 * Enable Transmission of Ack in Control Register.
	 * Set the timeout and I2C clock and request the IRQ(ISR mapped).
	 * Call to the i2c_add_numbered_adapter registers the adapter.
	 */
	xi2cps_writereg(0x0000000E, XI2CPS_CR_OFFSET);
	xi2cps_writereg(id->adap.timeout, XI2CPS_TIME_OUT_OFFSET);

	ret = xi2cps_setclk(i2c_clk, id);
	if (ret < 0) {
		dev_err(&pdev->dev, "invalid SCL clock: %dkHz\n", i2c_clk);
		ret = -EINVAL;
		goto err_unmap;
	}

	if (request_irq(id->irq, xi2cps_isr, 0, DRIVER_NAME, id)) {
		dev_err(&pdev->dev, "cannot get irq %d\n", id->irq);
		ret = -EINVAL;
		goto err_unmap;
	}

	ret = i2c_add_numbered_adapter(&id->adap);
	if (ret < 0) {
		dev_err(&pdev->dev, "reg adap failed: %d\n", ret);
		goto err_free_irq;
	}


	dev_info(&pdev->dev, "%d kHz mmio %08lx irq %d\n",
		 i2c_clk/1000, (unsigned long)r_mem->start, id->irq);

	return 0;

err_free_irq:
	free_irq(id->irq, id);
err_unmap:
	iounmap(id->membase);
err_free_mem:
	kfree(id);
	return ret;
}

/**
 * xi2cps_remove - Unregister the device after releasing the resources
 * @pdev:	Handle to the platform device structure
 *
 * Returns zero always
 *
 * This function frees all the resources allocated to the device.
 */
static int __devexit xi2cps_remove(struct platform_device *pdev)
{
	struct xi2cps *id = platform_get_drvdata(pdev);

	i2c_del_adapter(&id->adap);
	free_irq(id->irq, id);
	iounmap(id->membase);
	kfree(id);
	platform_set_drvdata(pdev, NULL);

	return 0;
}


static struct platform_driver xi2cps_drv = {
	.driver = {
		.name  = DRIVER_NAME,
		.owner = THIS_MODULE,
	},
	.probe  = xi2cps_probe,
	.remove = __devexit_p(xi2cps_remove),
};

/**
 * xi2cps_init - Initial driver registration function
 *
 * Returns zero on success, otherwise negative error.
 */
static int __init xi2cps_init(void)
{
	return platform_driver_register(&xi2cps_drv);
}

/**
 * xi2cps_exit - Driver Un-registration function
 */
static void __exit xi2cps_exit(void)
{
	platform_driver_unregister(&xi2cps_drv);
}

module_init(xi2cps_init);
module_exit(xi2cps_exit);

MODULE_AUTHOR("Xilinx, Inc.");
MODULE_DESCRIPTION("Xilinx PS I2C bus driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:" DRIVER_NAME);

