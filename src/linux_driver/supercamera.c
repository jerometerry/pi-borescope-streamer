#include <linux/init.h>
#include <linux/module.h>
#include <linux/usb.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/delay.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jerome Terry");
MODULE_DESCRIPTION("Linux Kernel Driver for Geek szitman supercamera (com.useeplus.protocol)");
MODULE_VERSION("0.9");

#define USB_TIMEOUT_MS        1000
#define BULK_TRANSFER_COUNT   4
#define BULK_TRANSFER_SIZE    (16 * 1024) 
#define MAX_FRAME_SIZE        (256 * 1024)

#define PROTO_FRAME_HEADER_A       0xAA
#define PROTO_FRAME_HEADER_B       0xBB
#define PROTO_VIDEO_CAMERA_ID      0x0B
#define PROTO_GRAVITY_CAMERA_ID    0x07

static const u8 initialization_tokens[] = { 0xFF, 0x55, 0xFF, 0x55, 0xEE, 0x10 };
static const u8 start_stream_tokens[]    = { 0xBB, 0xAA, 0x05, 0x00, 0x00 };

/* Back to interface ID mapping table so the kernel triggers our probe instantly */
static const struct usb_device_id supercam_table[] = {
	{ USB_DEVICE(0x0329, 0x2022) }, 
	{ USB_DEVICE(0x2ce3, 0x3828) }, 
	{ }                             
};
MODULE_DEVICE_TABLE(usb, supercam_table);

struct __packed usb_packet_header {
	__le16 leHeader;
	u8 leCameraId;
	__le16 leLength;
};

struct __packed usb_payload_header {
	u8 leFrameId;
	u8 leCameraNumber;
	u8 leFlags;
	__le32 leGravitySensor;
};

#define TOTAL_USB_HEADER_SIZE (sizeof(struct usb_packet_header) + sizeof(struct usb_payload_header))

enum parse_state {
	STATE_FIND_HEADER_A,
	STATE_FIND_HEADER_B,
	STATE_READ_PACKET_HEADER,
	STATE_READ_PAYLOAD_HEADER,
	STATE_STREAM_VIDEO,
	STATE_SKIP_TELEMETRY
};

struct usb_supercam {
	struct usb_device *udev;
	struct usb_interface *interface;
	
	struct urb *urbs[BULK_TRANSFER_COUNT];
	u8 *urb_buffers[BULK_TRANSFER_COUNT];
	dma_addr_t urb_dma_addrs[BULK_TRANSFER_COUNT];
	bool streaming;

	enum parse_state fsm_state;
	u8 header_buffer[TOTAL_USB_HEADER_SIZE];
	size_t header_bytes_collected;
	u8 active_camera_id;
	size_t payload_bytes_remaining;
	int last_frame_id;

	u8 *current_frame;
	size_t current_frame_len;
	unsigned int frame_counter;
};

static int supercam_write_msg(struct usb_supercam *dev, u8 endpoint_addr, const u8 *tokens, size_t len)
{
	int retval;
	int actual_length;
	u8 *dma_buffer;

	dma_buffer = kmemdup(tokens, len, GFP_KERNEL);
	if (!dma_buffer)
		return -ENOMEM;

	retval = usb_bulk_msg(dev->udev,
			      usb_sndbulkpipe(dev->udev, endpoint_addr),
			      dma_buffer,
			      len,
			      &actual_length,
			      USB_TIMEOUT_MS);

	kfree(dma_buffer);
	return retval;
}

static void supercam_read_bulk_callback(struct urb *urb)
{
	struct usb_supercam *dev = urb->context;
	int retval;
	int idx;
	u8 *data;

	if (urb->status) {
		if (urb->status == -ENOENT || urb->status == -ECONNRESET || urb->status == -ESHUTDOWN)
			return;
		goto resubmit;
	}

	data = (u8 *)urb->transfer_buffer;

	for (idx = 0; idx < urb->actual_length; ++idx) {
		u8 b = data[idx];

		switch (dev->fsm_state) {
		case STATE_FIND_HEADER_A:
			if (b == PROTO_FRAME_HEADER_A)
				dev->fsm_state = STATE_FIND_HEADER_B;
			break;

		case STATE_FIND_HEADER_B:
			if (b == PROTO_FRAME_HEADER_B) {
				dev->header_buffer = PROTO_FRAME_HEADER_A;
				dev->header_buffer = PROTO_FRAME_HEADER_B;
				dev->header_bytes_collected = 2;
				dev->fsm_state = STATE_READ_PACKET_HEADER;
			} else if (b != PROTO_FRAME_HEADER_A) {
				dev->fsm_state = STATE_FIND_HEADER_A;
			}
			break;

		case STATE_READ_PACKET_HEADER:
			dev->header_buffer[dev->header_bytes_collected++] = b;
			if (dev->header_bytes_collected == sizeof(struct usb_packet_header)) {
				const struct usb_packet_header *pkt = (const struct usb_packet_header *)dev->header_buffer;
				
				dev->active_camera_id = pkt->leCameraId;
				dev->payload_bytes_remaining = le16_to_cpu(pkt->leLength);

				if (dev->active_camera_id == PROTO_VIDEO_CAMERA_ID || 
				    dev->active_camera_id == PROTO_GRAVITY_CAMERA_ID) {
					dev->fsm_state = STATE_READ_PAYLOAD_HEADER;
				} else {
					dev->fsm_state = STATE_FIND_HEADER_A;
				}
			}
			break;

		case STATE_READ_PAYLOAD_HEADER:
			dev->header_buffer[dev->header_bytes_collected++] = b;
			dev->payload_bytes_remaining--;

			if (dev->header_bytes_collected == TOTAL_USB_HEADER_SIZE) {
				const struct usb_payload_header *payload = 
					(const struct usb_payload_header *)(dev->header_buffer + sizeof(struct usb_packet_header));

				if (dev->last_frame_id != -1 && payload->leFrameId != (u8)dev->last_frame_id) {
					if (dev->current_frame_len > 0) {
						dev->frame_counter++;
						dev_info(&dev->interface->dev, 
							 "[STREAMING] Compiled Video Frame #%u (%zu Bytes).\n", 
							 dev->frame_counter, dev->current_frame_len);
						dev->current_frame_len = 0;
					}
				}

				dev->last_frame_id = payload->leFrameId;

				if (dev->active_camera_id == PROTO_VIDEO_CAMERA_ID) {
					dev->fsm_state = STATE_STREAM_VIDEO;
				} else {
					dev->fsm_state = STATE_SKIP_TELEMETRY;
				}
			}
			break;

		case STATE_STREAM_VIDEO:
			if (dev->current_frame_len < MAX_FRAME_SIZE) {
				dev->current_frame[dev->current_frame_len++] = b;
			}
			dev->payload_bytes_remaining--;
			if (dev->payload_bytes_remaining == 0)
				dev->fsm_state = STATE_FIND_HEADER_A;
			break;

		case STATE_SKIP_TELEMETRY:
			dev->payload_bytes_remaining--;
			if (dev->payload_bytes_remaining == 0)
				dev->fsm_state = STATE_FIND_HEADER_A;
			break;
		}
	}

resubmit:
	if (dev->streaming) {
		retval = usb_submit_urb(urb, GFP_ATOMIC);
	}
}

static void supercam_kill_urbs(struct usb_supercam *dev)
{
	int i;
	dev->streaming = false;
	for (i = 0; i < BULK_TRANSFER_COUNT; ++i) {
		if (dev->urbs[i]) {
			usb_kill_urb(dev->urbs[i]);
			if (dev->urb_buffers[i]) {
				usb_free_coherent(dev->udev, BULK_TRANSFER_SIZE, 
						  dev->urb_buffers[i], dev->urb_dma_addrs[i]);
				dev->urb_buffers[i] = NULL;
			}
			usb_free_urb(dev->urbs[i]);
			dev->urbs[i] = NULL;
		}
	}
}

static int supercam_probe(struct usb_interface *interface, const struct usb_device_id *id)
{
	struct usb_device *udev = interface_to_usbdev(interface);
	struct usb_supercam *dev = NULL;
	u8 *drain_buffer;
	int i, retval, actual_len;

	/* Interface filtering layout constraint matching your descriptor requirements */
	if (interface->cur_altsetting->desc.bInterfaceNumber != 1) {
		dev_info(&interface->dev, "Leaving Interface 0 for cross-interface targeting.\n");
		return -ENODEV; 
	}

	dev_info(&interface->dev, "Geek szitman supercamera matching channel identified.\n");

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev) return -ENOMEM;

	dev->udev = udev;
	dev->interface = interface;
	dev->last_frame_id = -1;
	dev->fsm_state = STATE_FIND_HEADER_A;

	dev->current_frame = kzalloc(MAX_FRAME_SIZE, GFP_KERNEL);
	if (!dev->current_frame) {
		retval = -ENOMEM;
		goto error;
	}

	/* ----------------------------------------------------
	 * PHASE 1: DRAIN INTERFACE 0 iAP HEARTBEAT PAYLOADS
	 * ---------------------------------------------------- */
	drain_buffer = kmalloc(512, GFP_KERNEL);
	if (!drain_buffer) {
		retval = -ENOMEM;
		goto error;
	}

	dev_info(&interface->dev, "Clearing cross-interface iAP queue elements on EP 2 IN...\n");
	for (i = 0; i < 30; ++i) {
		usb_bulk_msg(udev, usb_rcvbulkpipe(udev, 0x82), 
			     drain_buffer, 512, &actual_len, 100);
	}
	kfree(drain_buffer);

	/* ----------------------------------------------------
	 * PHASE 2: ALTERNATE SETTING SELECTION
	 * ---------------------------------------------------- */
	retval = usb_set_interface(udev, 1, 1);
	if (retval) {
		dev_err(&interface->dev, "Failed to switch alternate profile indices.\n");
		goto error;
	}

	usb_clear_halt(udev, usb_rcvbulkpipe(udev, 0x81));

	/* ----------------------------------------------------
	 * PHASE 3: PARALLEL ASYNCHRONOUS URB INITIALIZATION
	 * ---------------------------------------------------- */
	for (i = 0; i < BULK_TRANSFER_COUNT; ++i) {
		dev->urbs[i] = usb_alloc_urb(0, GFP_KERNEL);
		if (!dev->urbs[i]) { retval = -ENOMEM; goto error_urbs; }

		dev->urb_buffers[i] = usb_alloc_coherent(udev, BULK_TRANSFER_SIZE, 
							GFP_KERNEL, &dev->urb_dma_addrs[i]);
		if (!dev->urb_buffers[i]) { retval = -ENOMEM; goto error_urbs; }

		usb_fill_bulk_urb(dev->urbs[i], udev,
				  usb_rcvbulkpipe(udev, 0x81), 
				  dev->urb_buffers[i], BULK_TRANSFER_SIZE,
				  supercam_read_bulk_callback, dev);

		dev->urbs[i]->transfer_dma = dev->urb_dma_addrs[i];
		dev->urbs[i]->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;
	}

	usb_set_intfdata(interface, dev);

	/* ----------------------------------------------------
	 * PHASE 4: TARGETED CROSS-INTERFACE TOKEN BURSTS
	 * ---------------------------------------------------- */
	dev_info(&interface->dev, "Sending init tokens to EP 2 OUT (0x02)...\n");
	retval = supercam_write_msg(dev, 0x02, initialization_tokens, sizeof(initialization_tokens));
	if (retval) goto error_sequence;

	dev_info(&interface->dev, "Sending stream start tokens to EP 1 OUT (0x01)...\n");
	retval = supercam_write_msg(dev, 0x01, start_stream_tokens, sizeof(start_stream_tokens));
	if (retval) goto error_sequence;

	dev->streaming = true;
	for (i = 0; i < BULK_TRANSFER_COUNT; ++i) {
		retval = usb_submit_urb(dev->urbs[i], GFP_KERNEL);
		if (retval) goto error_sequence;
	}

	dev_info(&interface->dev, "Borescope engine tracking channels successfully.\n");
	return 0;

error_sequence:
	supercam_kill_urbs(dev);
	usb_set_intfdata(interface, NULL);
error_urbs:
	supercam_kill_urbs(dev);
error:
	if (dev) {
		if (dev->current_frame) kfree(dev->current_frame);
		kfree(dev);
	}
	return retval;
}

static void supercam_disconnect(struct usb_interface *interface)
{
	struct usb_supercam *dev = usb_get_intfdata(interface);
	usb_set_intfdata(interface, NULL);

	if (dev) {
		supercam_kill_urbs(dev);
		if (dev->current_frame) kfree(dev->current_frame);
		dev_info(&interface->dev, "Geek szitman supercamera removed from runtime mesh.\n");
		kfree(dev);
	}
}

/* Standard interface level subsystem configuration blocks */
static struct usb_driver supercam_driver = {
	.name = "supercamera",
	.id_table = supercam_table,
	.probe = supercam_probe,
	.disconnect = supercam_disconnect,
};

module_usb_driver(supercam_driver);
