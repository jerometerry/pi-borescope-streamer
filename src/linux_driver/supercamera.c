#include <linux/init.h>
#include <linux/module.h>
#include <linux/usb.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/spinlock.h>
#include <linux/dma-mapping.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-fh.h>
#include <media/videobuf2-v4l2.h>
#include <media/videobuf2-dma-contig.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jerome Terry");
MODULE_DESCRIPTION("Production DMA-Compliant Linux 6.18+ V4L2 Driver for Geek szitman supercamera");
MODULE_VERSION("1.9");

#define USB_TIMEOUT_MS        1000
#define BULK_TRANSFER_COUNT   4
#define BULK_TRANSFER_SIZE    (16 * 1024) 
#define MAX_FRAME_SIZE        (256 * 1024)
#define MIN_VALID_FRAME_SIZE  (2 * 1024)  

#define PROTO_FRAME_HEADER_A       0xAA
#define PROTO_FRAME_HEADER_B       0xBB
#define PROTO_VIDEO_CAMERA_ID      0x0B
#define PROTO_GRAVITY_CAMERA_ID    0x07

#define JPEG_MARKER_BOUNDARY       0xFF
#define JPEG_MARKER_EOI            0xD9

static const u8 initialization_tokens[] = { 0xFF, 0x55, 0xFF, 0x55, 0xEE, 0x10 };
static const u8 start_stream_tokens[]    = { 0xBB, 0xAA, 0x05, 0x00, 0x00 };

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

struct supercam_buffer {
	struct vb2_v4l2_buffer vb;
	struct list_head list;
};

struct usb_supercam {
	struct usb_device *udev;
	struct usb_interface *interface;
	
	struct v4l2_device v4l2_dev;
	struct video_device vdev;
	struct mutex v4l2_lock; 

	struct vb2_queue vb_vidq;
	struct list_head rdy_queue;
	spinlock_t q_lock; 
	u64 sequence;

	struct urb *urbs[BULK_TRANSFER_COUNT];
	u8 *urb_buffers[BULK_TRANSFER_COUNT];
	dma_addr_t urb_dma_addrs[BULK_TRANSFER_COUNT];
	bool streaming;

	enum parse_state fsm_state;
	u8 header_buffer[TOTAL_USB_HEADER_SIZE];
	size_t header_bytes_collected;
	u8 active_camera_id;
	size_t payload_bytes_remaining;

	u8 *current_frame;
	size_t current_frame_len;
	unsigned int frame_counter;
	u8 trailing_byte;
};

static int supercam_queue_setup(struct vb2_queue *vq, unsigned int *nbuffers,
				unsigned int *nplanes, unsigned int sizes[],
				struct device *alloc_devs[])
{
	if (*nplanes)
		return sizes[0] < MAX_FRAME_SIZE ? -EINVAL : 0;
	
	*nplanes = 1;
	sizes[0] = MAX_FRAME_SIZE; /* Fixed: Correct index 0 array element syntax */
	return 0;
}

static int supercam_buf_prepare(struct vb2_buffer *vb)
{
	if (vb2_plane_size(vb, 0) < MAX_FRAME_SIZE)
		return -EINVAL;
	vb2_set_plane_payload(vb, 0, MAX_FRAME_SIZE);
	return 0;
}

static void supercam_buf_queue(struct vb2_buffer *vb)
{
	struct usb_supercam *dev = vb2_get_drv_priv(vb->vb2_queue);
	struct supercam_buffer *buf = container_of(vb, struct supercam_buffer, vb.vb2_buf);
	unsigned long flags;

	spin_lock_irqsave(&dev->q_lock, flags);
	list_add_tail(&buf->list, &dev->rdy_queue);
	spin_unlock_irqrestore(&dev->q_lock, flags);
}

static int supercam_start_streaming(struct vb2_queue *vq, unsigned int count)
{
	return 0;
}

static void supercam_stop_streaming(struct vb2_queue *vq)
{
	struct usb_supercam *dev = vb2_get_drv_priv(vq);
	struct supercam_buffer *buf;
	unsigned long flags;

	spin_lock_irqsave(&dev->q_lock, flags);
	while (!list_empty(&dev->rdy_queue)) {
		buf = list_first_entry(&dev->rdy_queue, struct supercam_buffer, list);
		list_del(&buf->list);
		vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_ERROR);
	}
	spin_unlock_irqrestore(&dev->q_lock, flags);
}

static const struct vb2_ops supercam_vb2_ops = {
	.queue_setup    = supercam_queue_setup,
	.buf_prepare    = supercam_buf_prepare,
	.buf_queue      = supercam_buf_queue,
	.start_streaming = supercam_start_streaming,
	.stop_streaming = supercam_stop_streaming,
	.wait_prepare   = vb2_ops_wait_prepare,
	.wait_finish    = vb2_ops_wait_finish,
};

static int supercam_v4l2_open(struct file *file)
{
	return v4l2_fh_open(file);
}

static int supercam_v4l2_release(struct file *file)
{
	return _vb2_fop_release(file, NULL);
}

static const struct v4l2_file_operations supercam_v4l2_fops = {
	.owner          = THIS_MODULE,
	.open           = supercam_v4l2_open,
	.release        = supercam_v4l2_release,
	.read           = vb2_fop_read,
	.poll           = vb2_fop_poll,
	.mmap           = vb2_fop_mmap,
	.unlocked_ioctl = video_ioctl2, 
};

static int supercam_vidioc_querycap(struct file *file, void *priv, struct v4l2_capability *cap)
{
	struct usb_supercam *dev = video_drvdata(file);
	strscpy(cap->driver, "supercamera", sizeof(cap->driver));
	strscpy(cap->card, "Geek szitman supercamera", sizeof(cap->card));
	usb_make_path(dev->udev, cap->bus_info, sizeof(cap->bus_info));
	cap->capabilities = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING | V4L2_CAP_DEVICE_CAPS;
	cap->device_caps = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING;
	return 0;
}

static int supercam_vidioc_fmt_vid_cap(struct file *file, void *priv, struct v4l2_format *f)
{
	f->fmt.pix.width        = 1280;
	f->fmt.pix.height       = 720;
	f->fmt.pix.pixelformat  = V4L2_PIX_FMT_MJPEG;
	f->fmt.pix.field        = V4L2_FIELD_NONE;
	f->fmt.pix.bytesperline = 0;
	f->fmt.pix.sizeimage    = MAX_FRAME_SIZE;
	f->fmt.pix.colorspace   = V4L2_COLORSPACE_SRGB;
	return 0;
}

static int supercam_vidioc_enum_fmt_vid_cap(struct file *file, void *priv, struct v4l2_fmtdesc *f)
{
	if (f->index > 0)
		return -EINVAL;
	f->pixelformat = V4L2_PIX_FMT_MJPEG;
	return 0;
}

static int supercam_vidioc_enum_input(struct file *file, void *priv, struct v4l2_input *inp)
{
	if (inp->index > 0)
		return -EINVAL;
	inp->type = V4L2_INPUT_TYPE_CAMERA;
	strscpy(inp->name, "Borescope Lens Channel 0", sizeof(inp->name));
	return 0;
}

static int supercam_vidioc_g_input(struct file *file, void *priv, unsigned int *i)
{
	*i = 0; 
	return 0;
}

static int supercam_vidioc_s_input(struct file *file, void *priv, unsigned int i)
{
	return i == 0 ? 0 : -EINVAL;
}

static int supercam_vidioc_g_parm(struct file *file, void *priv, struct v4l2_streamparm *sp)
{
	if (sp->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;
	sp->parm.capture.capability = V4L2_CAP_TIMEPERFRAME;
	sp->parm.capture.timeperframe.numerator = 1;
	sp->parm.capture.timeperframe.denominator = 30; 
	return 0;
}

static const struct v4l2_ioctl_ops supercam_v4l2_ioctl_ops = {
	.vidioc_querycap        = supercam_vidioc_querycap,
	.vidioc_g_fmt_vid_cap   = supercam_vidioc_fmt_vid_cap,
	.vidioc_s_fmt_vid_cap   = supercam_vidioc_fmt_vid_cap,
	.vidioc_try_fmt_vid_cap = supercam_vidioc_fmt_vid_cap,
	.vidioc_enum_fmt_vid_cap = supercam_vidioc_enum_fmt_vid_cap,
	.vidioc_enum_input      = supercam_vidioc_enum_input,
	.vidioc_g_input         = supercam_vidioc_g_input,
	.vidioc_s_input         = supercam_vidioc_s_input,
	.vidioc_g_parm          = supercam_vidioc_g_parm, 
	.vidioc_reqbufs         = vb2_ioctl_reqbufs,
	.vidioc_querybuf        = vb2_ioctl_querybuf,
	.vidioc_qbuf            = vb2_ioctl_qbuf,
	.vidioc_dqbuf           = vb2_ioctl_dqbuf,
	.vidioc_streamon        = vb2_ioctl_streamon,
	.vidioc_streamoff       = vb2_ioctl_streamoff,
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

static void supercam_read_bulk_callback(struct urb *urb) {
  struct usb_supercam *dev = urb->context;
  struct supercam_buffer *vbuf;
  int retval;
  int idx;
  u8 *data;
  unsigned long flags;

  if (urb->status) {
    if (urb->status == -ENOENT || urb->status == -ECONNRESET ||
        urb->status == -ESHUTDOWN)
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
        dev->header_buffer[0] = PROTO_FRAME_HEADER_A;
        dev->header_buffer[1] = PROTO_FRAME_HEADER_B;
        dev->header_bytes_collected = 2;
        dev->fsm_state = STATE_READ_PACKET_HEADER;
      } else if (b != PROTO_FRAME_HEADER_A) {
        dev->fsm_state = STATE_FIND_HEADER_A;
      }
      break;

    case STATE_READ_PACKET_HEADER:
      dev->header_buffer[dev->header_bytes_collected++] = b;
      if (dev->header_bytes_collected == sizeof(struct usb_packet_header)) {
        const struct usb_packet_header *pkt =
            (const struct usb_packet_header *)dev->header_buffer;
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
        if (dev->trailing_byte == JPEG_MARKER_BOUNDARY &&
            b == JPEG_MARKER_EOI) {
          if (dev->current_frame_len >= MIN_VALID_FRAME_SIZE) {
            dev->frame_counter++;
            spin_lock_irqsave(&dev->q_lock, flags);
            if (!list_empty(&dev->rdy_queue)) {
              vbuf = list_first_entry(&dev->rdy_queue, struct supercam_buffer,
                                      list);
              list_del(&vbuf->list);
              void *vaddr = vb2_plane_vaddr(&vbuf->vb.vb2_buf, 0);
              if (vaddr) {
                memcpy(vaddr, dev->current_frame, dev->current_frame_len);
                vb2_set_plane_payload(&vbuf->vb.vb2_buf, 0,
                                      dev->current_frame_len);
                vbuf->vb.vb2_buf.timestamp = ktime_get_ns();
                vbuf->vb.sequence = dev->sequence++;
                vbuf->vb.field = V4L2_FIELD_NONE;
                vb2_buffer_done(&vbuf->vb.vb2_buf, VB2_BUF_STATE_DONE);
              } else {
                vb2_buffer_done(&vbuf->vb.vb2_buf, VB2_BUF_STATE_ERROR);
              }
            }
            spin_unlock_irqrestore(&dev->q_lock, flags);
          }
          dev->current_frame_len = 0;
        }
        dev->trailing_byte = b;
      }
      dev->payload_bytes_remaining--;
      if (dev->payload_bytes_remaining == 0) {
        dev->fsm_state = STATE_FIND_HEADER_A;
      }
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
static void supercam_kill_urbs(struct usb_supercam *dev) {
  int i;
  dev->streaming = false;
  for (i = 0; i < BULK_TRANSFER_COUNT; ++i) {
    if (dev->urbs[i]) {
      usb_kill_urb(dev->urbs[i]);
      if (dev->urb_buffers[i]) {
        usb_free_coherent(dev->udev, BULK_TRANSFER_SIZE, dev->urb_buffers[i],
                          dev->urb_dma_addrs[i]);
        dev->urb_buffers[i] = NULL;
      }
      usb_free_urb(dev->urbs[i]);
      dev->urbs[i] = NULL;
    }
  }
}
static int supercam_probe(struct usb_interface *interface,
                          const struct usb_device_id *id) {
  struct usb_device *udev = interface_to_usbdev(interface);
  struct usb_supercam *dev = NULL;
  struct vb2_queue *q;
  u8 *drain_buffer;
  int i, retval, actual_len;
  if (interface->cur_altsetting->desc.bInterfaceNumber != 1) {
    return -ENODEV;
  } /** CRITICAL FIXED DMA ROUTING ENDPOINT:* Point mask allocation
       configuration straight to the parent platform physical device controller
       tree* (udev->bus->controller) instead of interface container context.
       This resolves error -5 (EIO).*/
  retval = dma_set_mask_and_coherent(udev->bus->controller, DMA_BIT_MASK(32));
  if (retval) {
    dev_err(
        &interface->dev,
        "Hardware physical platform core DMA mask initialization failed (%d)\n",
        retval);
    return retval;
  }
  dev_info(&interface->dev,
           "Geek szitman supercamera matching channel identified.\n");
  dev = kzalloc(sizeof(*dev), GFP_KERNEL);
  if (!dev)
    return -ENOMEM;
  dev->udev = udev;
  dev->interface = interface;
  dev->fsm_state = STATE_FIND_HEADER_A;
  dev->trailing_byte = 0;
  dev->sequence = 0;
  mutex_init(&dev->v4l2_lock);
  spin_lock_init(&dev->q_lock);
  INIT_LIST_HEAD(&dev->rdy_queue);
  dev->current_frame = kzalloc(MAX_FRAME_SIZE, GFP_KERNEL);
  if (!dev->current_frame) {
    retval = -ENOMEM;
    goto error;
  }
  retval = v4l2_device_register(&interface->dev, &dev->v4l2_dev);
  if (retval)
    goto error;
  q = &dev->vb_vidq;
  q->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  q->io_modes = VB2_MMAP | VB2_USERPTR | VB2_READ;
  q->drv_priv = dev;
  q->buf_struct_size = sizeof(struct supercam_buffer);
  q->ops = &supercam_vb2_ops;
  q->mem_ops = &vb2_dma_contig_memops;
  q->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
  q->min_queued_buffers = 2;
  q->lock = &dev->v4l2_lock; /* Map the vb2 queue allocations straight to the
                                physical controller dev */
  q->dev = udev->bus->controller;
  retval = vb2_queue_init(q);
  if (retval) {
    dev_err(&interface->dev, "vb2_queue_init failed\n");
    goto error_unreg_v4l2;
  }
  strscpy(dev->vdev.name, "supercamera-video", sizeof(dev->vdev.name));
  dev->vdev.v4l2_dev = &dev->v4l2_dev;
  dev->vdev.fops = &supercam_v4l2_fops;
  dev->vdev.ioctl_ops = &supercam_v4l2_ioctl_ops;
  dev->vdev.release = video_device_release_empty;
  dev->vdev.lock = &dev->v4l2_lock;
  dev->vdev.queue = q;
  dev->vdev.device_caps = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING;
  video_set_drvdata(&dev->vdev, dev);
  drain_buffer = kmalloc(512, GFP_KERNEL);
  if (!drain_buffer) {
    retval = -ENOMEM;
    goto error_unreg_v4l2;
  }
  for (i = 0; i < 30; ++i) {
    usb_bulk_msg(udev, usb_rcvbulkpipe(udev, 0x82), drain_buffer, 512,
                 &actual_len, 100);
  }
  kfree(drain_buffer);
  retval = usb_set_interface(udev, 1, 1);
  if (retval)
    goto error_unreg_v4l2;
  usb_clear_halt(udev, usb_rcvbulkpipe(udev, 0x81));
  for (i = 0; i < BULK_TRANSFER_COUNT; ++i) {
    dev->urbs[i] = usb_alloc_urb(0, GFP_KERNEL);
    if (!dev->urbs[i]) {
      retval = -ENOMEM;
      goto error_urbs;
    }
    dev->urb_buffers[i] = usb_alloc_coherent(
        udev, BULK_TRANSFER_SIZE, GFP_KERNEL, &dev->urb_dma_addrs[i]);
    if (!dev->urb_buffers[i]) {
      retval = -ENOMEM;
      goto error_urbs;
    }
    usb_fill_bulk_urb(dev->urbs[i], udev, usb_rcvbulkpipe(udev, 0x81),
                      dev->urb_buffers[i], BULK_TRANSFER_SIZE,
                      supercam_read_bulk_callback, dev);
    dev->urbs[i]->transfer_dma = dev->urb_dma_addrs[i];
    dev->urbs[i]->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;
  }
  usb_set_intfdata(interface, dev);
  retval = supercam_write_msg(dev, 0x02, initialization_tokens,
                              sizeof(initialization_tokens));
  if (retval)
    goto error_sequence;
  retval = supercam_write_msg(dev, 0x01, start_stream_tokens,
                              sizeof(start_stream_tokens));
  if (retval)
    goto error_sequence;
  retval = video_register_device(&dev->vdev, VFL_TYPE_VIDEO, -1);
  if (retval)
    goto error_sequence;
  dev_info(&interface->dev, "Production V4L2 device deployed successfully.\n");
  dev->streaming = true;
  for (i = 0; i < BULK_TRANSFER_COUNT; ++i) {
    retval = usb_submit_urb(dev->urbs[i], GFP_KERNEL);
    if (retval)
      goto error_unreg_video;
  }
  return 0;
error_unreg_video:
  video_unregister_device(&dev->vdev);
error_sequence:
  supercam_kill_urbs(dev);
  usb_set_intfdata(interface, NULL);
error_urbs:
  supercam_kill_urbs(dev);
error_unreg_v4l2:
  v4l2_device_unregister(&dev->v4l2_dev);
error:
  if (dev) {
    if (dev->current_frame)
      kfree(dev->current_frame);
    kfree(dev);
  }
  return retval;
}
static void supercam_disconnect(struct usb_interface *interface) {
  struct usb_supercam *dev = usb_get_intfdata(interface);
  usb_set_intfdata(interface, NULL);
  if (dev) {
    supercam_kill_urbs(dev);
    video_unregister_device(&dev->vdev);
    v4l2_device_unregister(&dev->v4l2_dev);
    if (dev->current_frame)
      kfree(dev->current_frame);
    dev_info(&interface->dev,
             "Geek szitman supercamera completely detached.\n");
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