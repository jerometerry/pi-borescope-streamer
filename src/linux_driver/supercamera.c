#include <linux/init.h>
#include <linux/module.h>
#include <linux/usb.h>
#include <linux/kernel.h>
#include <linux/slab.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jerome Terry");
MODULE_DESCRIPTION("Linux Kernel Driver for Geek szitman supercamera (com.useeplus.protocol)");
MODULE_VERSION("0.2");

#define USB_TIMEOUT_MS 1000

/* Custom token sequences from Jerome's UsbProtocol configuration */
static const u8 initialization_tokens[] = { 0xFF, 0x55, 0xFF, 0x55, 0xEE, 0x10 };
static const u8 start_stream_tokens[]    = { 0xBB, 0xAA, 0x05, 0x00, 0x00 };

static const struct usb_device_id supercam_table[] = {
	{ USB_DEVICE(0x0329, 0x2022) }, 
	{ USB_DEVICE(0x2ce3, 0x3828) }, 
	{ }                             
};
MODULE_DEVICE_TABLE(usb, supercam_table);

struct usb_supercam {
	struct usb_device *udev;
	struct usb_interface *interface;
	size_t bulk_in_size;
	__u8 bulk_in_endpointAddr;
	__u8 bulk_out_endpointAddr;
};

/* 
 * Helper function to send synchronous control token payloads down EP 1 OUT
 */
static int supercam_send_tokens(struct usb_supercam *dev, const u8 *tokens, size_t len)
{
	int retval;
	int actual_length;
	u8 *dma_buffer;

	/* 
	 * CRITICAL KERNEL CONSTRAINT: You cannot pass pointers from the stack or 
	 * executable constant pages directly to a USB transmission endpoint. 
	 * The host controller uses DMA (Direct Memory Access), requiring a dedicated 
	 * heap-allocated, cache-aligned buffer chunk.
	 */
	dma_buffer = kmemdup(tokens, len, GFP_KERNEL);
	if (!dma_buffer)
		return -ENOMEM;

	retval = usb_bulk_msg(dev->udev,
			      usb_sndbulkpipe(dev->udev, dev->bulk_out_endpointAddr),
			      dma_buffer,
			      len,
			      &actual_length,
			      USB_TIMEOUT_MS);

	if (retval) {
		dev_err(&dev->interface->dev, "Bulk token transfer failed! Error: %d\n", retval);
	} else if (actual_length != len) {
		dev_warn(&dev->interface->dev, "Token mismatch! Sent %d of %zu bytes\n", actual_length, len);
		retval = -EIO;
	}

	kfree(dma_buffer);
	return retval;
}

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

		if (!dev->bulk_in_endpointAddr && usb_endpoint_is_bulk_in(endpoint)) {
			dev->bulk_in_endpointAddr = endpoint->bEndpointAddress;
			dev->bulk_in_size = usb_endpoint_maxp(endpoint); 
		}

		if (!dev->bulk_out_endpointAddr && usb_endpoint_is_bulk_out(endpoint)) {
			dev->bulk_out_endpointAddr = endpoint->bEndpointAddress;
		}
	}

	if (!(dev->bulk_in_endpointAddr && dev->bulk_out_endpointAddr)) {
		dev_err(&interface->dev, "Could not find both bulk IN and bulk OUT endpoints\n");
		retval = -ENODEV;
		goto error;
	}

	usb_set_intfdata(interface, dev);

	/* ----------------------------------------------------
	 * HARDWARE WAKEUP PHASE: Send the reverse-engineered initialization sequence
	 * ---------------------------------------------------- */
	dev_info(&interface->dev, "Sending hardware initialization tokens...\n");
	retval = supercam_send_tokens(dev, initialization_tokens, sizeof(initialization_tokens));
	if (retval)
		goto error_sequence;

	dev_info(&interface->dev, "Sending streaming request tokens...\n");
	retval = supercam_send_tokens(dev, start_stream_tokens, sizeof(start_stream_tokens));
	if (retval)
		goto error_sequence;

	dev_info(&interface->dev, "Hardware pipes successfully initialized and streaming.\n");
	return 0;

error_sequence:
	usb_set_intfdata(interface, NULL);
error:
	if (dev) {
		usb_put_dev(dev->udev);
		kfree(dev);
	}
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
