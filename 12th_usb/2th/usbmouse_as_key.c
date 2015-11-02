
/*
 *reference sample:usbmouse.c
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/usb/input.h>
#include <linux/hid.h>

static struct usb_device_id usbmouse_as_key_id_table [] = {
	{ USB_INTERFACE_INFO(USB_INTERFACE_CLASS_HID, USB_INTERFACE_SUBCLASS_BOOT,
		USB_INTERFACE_PROTOCOL_MOUSE) },
	{ }	/* Terminating entry */
};

static int usbmouse_as_key_probe(struct usb_interface *intf, const struct usb_device_id *id)
{
	struct usb_device *dev = interface_to_usbdev(intf);

	printk("found usb mounse !\n");
	
	printk("BCD_USB = %x \n",dev->descriptor.bcdUSB);
	printk("VID_USB = %x \n",dev->descriptor.idVendor);
	printk("PID_USB = %x \n",dev->descriptor.idProduct);
	return 0;
}

static int usbmouse_as_key_disconnect(struct usb_interface *intf, const struct usb_device_id *id)
{
	printk("disconnect usb mounse ! \n");
	return 0;
}

/*1. ����/����USB_driver */
static struct usb_driver usbmouse_as_key_driver = {
	.name		= "usbmouse_as_key",
	.probe		= usbmouse_as_key_probe,
	.disconnect	= usbmouse_as_key_disconnect,
	.id_table		= usbmouse_as_key_id_table,
};

static int usbmouse_as_key_init(void)
{
	/*2. ע��*/
	usb_register(&usbmouse_as_key_driver);

	return 0;
}
static void usbmouse_as_key_exit(void)
{
	usb_deregister(&usbmouse_as_key_driver);
	return 0;
}
module_init(usbmouse_as_key_init);
module_exit(usbmouse_as_key_exit);
MODULE_LICENSE("GPL");

