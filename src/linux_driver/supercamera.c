#include <linux/init.h>
#include <linux/module.h>
#include <linux/usb.h>
#include <linux/kernel.h>
#include <linux/slab.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jerome Terry");
MODULE_DESCRIPTION("Linux Kernel Driver for Geek szitman supercamera (com.useeplus.protocol)");
MODULE_VERSION("0.1");

static const struct usb_device_id supercam_table[] = {
	{ USB_DEVICE(0x0329, 0x2022) },
	{ USB_DEVICE(0x2ce3, 0x3828) },
	{ }
};
MODULE_DEVICE_TABLE(usb, supercam_table);

struct usb_supercam {
	struct usb_device *udev;
	struct usb_interface *interface;
	unsigned char *bulk_in_buffer;
	size_t bulk_in_size;
	__u8 bulk_in_endpointAddr;
	__u8 bulk_out_endpointAddr;
};

static int supercam_probe(struct usb_interface *interface, const struct usb_device_id *id)
{
	struct usb_device *udev = interface_to_usbdev(interface);
	struct usb_host_interface *iface_desc;
	struct usb_endpoint_descriptor *endpoint;
	struct usb_supercam *dev = NULL;
	int i;
	int retval = -ENOMEM;

	if (interface->cur_altsetting->desc.bInterfaceNumber != 1) {
		dev_info(&interface->dev, "Ignoring Interface 0 (iAP authentication layer)\n");
		return -ENODEV; 
	}

	dev_info(&interface->dev, "Geek szitman supercamera core Interface 1 detected.\n");

	retval = usb_set_interface(udev, 1, 1);
	if (retval) {
		dev_err(&interface->dev, "Failed to activate Alternate Setting 1 (Error: %d)\n", retval);
		return retval;
	}

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;

	dev->udev = usb_get_dev(udev);
	dev->interface = interface;

	iface_desc = interface->cur_altsetting;
	for (i = 0; i < iface_desc->desc.bNumEndpoints; ++i) {
		endpoint = &iface_desc->endpoint[i].desc;

		if (!dev->bulk_in_endpointAddr &&
		    usb_endpoint_is_bulk_in(endpoint)) {
			/* Found EP 1 IN (0x81) */
			dev->bulk_in_endpointAddr = endpoint->bEndpointAddress;
			dev->bulk_in_size = usb_endpoint_maxp(endpoint); // Should resolve to 512B
		}

		if (!dev->bulk_out_endpointAddr &&
		    usb_endpoint_is_bulk_out(endpoint)) {
			/* Found EP 1 OUT (0x01) */
			dev->bulk_out_endpointAddr = endpoint->bEndpointAddress;
		}
	}

	if (!(dev->bulk_in_endpointAddr && dev->bulk_out_endpointAddr)) {
		dev_err(&interface->dev, "Could not find both bulk IN and bulk OUT endpoints\n");
		retval = -ENODEV;
		goto error;
	}

	usb_set_intfdata(interface, dev);
	dev_info(&interface->dev, "Driver successfully bound to hardware pipelines.\n");
	
	return 0;

error:
	usb_set_intfdata(interface, NULL);
	usb_put_dev(dev->udev);
	kfree(dev);
	return retval;
}

static void supercam_disconnect(struct usb_interface *interface)
{
	struct usb_supercam *dev;

	dev = usb_get_intfdata(interface);
	usb_set_intfdata(interface, NULL);

	if (dev) {
		dev_info(&interface->dev, "Geek szitman supercamera disconnected.\n");
		usb_put_dev(dev->udev);
		kfree(dev);
	}
}

static struct usb_driver supercam_driver = {
	.name = "supercamera",
	.id_table = supercam_table,
	.probe = supercam_probe,
	.disconnect = supercam_disconnect,
};

module_usb_driver(supercam_driver);
