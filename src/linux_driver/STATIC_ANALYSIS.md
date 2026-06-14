# Static Analysis of Useeplus USB Linux Driver

## Analysis Tools

### Checkpath

checkpath.pl

### Sparse

```bash
sudo apt install sparse
make clean
make CHECKER=1
```

### Smatch (C static analysis tool)

```bash
git clone git://repo.or.cz/smatch.git
sudo apt update
sudo apt install gcc make sqlite3 libsqlite3-dev libdbd-sqlite3-perl libssl-dev libtry-tiny-perl
cd smatch
make

```

## Code Analysis

### Includes

```c
// Includes
#include <linux/init.h>
#include <linux/module.h>
#include <linux/usb.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/delay.h>
#include <linux/spinlock.h>
#include <linux/unaligned.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-fh.h>
#include <media/videobuf2-v4l2.h>
#include <media/videobuf2-vmalloc.h>
```

### Symbols Used

```c
_vb2_fop_release
container_of
dev_dbg
dev_err
dev_warn
EINVAL
ENODEV
ENOMEM
get_unaligned_le16
GFP_KERNEL
INIT_LIST_HEAD
interface_to_usbdev
kfree
kmalloc
kmemdup
ktime_get_ns
kzalloc
list_add_tail
list_del
list_empty
list_first_entry
memcpy
memmove
min
min_t
MODULE_AUTHOR
MODULE_DESCRIPTION
MODULE_DEVICE_TABLE
MODULE_LICENSE
module_usb_driver
MODULE_VERSION
mutex_init
sizeof
spin_lock_init
spin_lock_irqsave
spin_unlock_irqrestore
strscpy
symbol
to_vb2_v4l2_buffer
usb_alloc_coherent
usb_alloc_urb
usb_bulk_msg
usb_clear_halt
usb_driver
usb_fill_bulk_urb
usb_free_coherent
usb_free_urb
usb_get_intfdata
usb_kill_urb
usb_make_path
usb_rcvbulkpipe
usb_set_interface
usb_set_intfdata
usb_sndbulkpipe
usb_submit_urb
useeplus_kill_urbs
V4L2_BUF_TYPE_VIDEO_CAPTURE
V4L2_CAP_TIMEPERFRAME
V4L2_COLORSPACE_SRGB
v4l2_device_register
v4l2_device_unregister
v4l2_fh_open
V4L2_FIELD_NONE
v4l2_file_operations
V4L2_INPUT_TYPE_CAMERA
v4l2_ioctl_ops
V4L2_PIX_FMT_MJPEG
vb2_buffer_done
vb2_get_drv_priv
vb2_ops
vb2_plane_size
vb2_plane_vaddr
vb2_queue_init
vb2_set_plane_payload
vfree
video_drvdata
video_register_device
video_set_drvdata
video_unregister_device
vzalloc
```

### Function Signatures

```c
static int useeplus_queue_setup(struct vb2_queue *vq, unsigned int *nbuffers, unsigned int *nplanes,
								unsigned int sizes[], struct device *alloc_devs[]);
static int useeplus_buf_prepare(struct vb2_buffer *vb);
static void useeplus_buf_queue(struct vb2_buffer *vb);
static int useeplus_start_streaming(struct vb2_queue *vq, unsigned int count);
static void useeplus_stop_streaming(struct vb2_queue *vq);
static int useeplus_v4l2_open(struct file *file);
static int useeplus_v4l2_release(struct file *file);
static int useeplus_vidioc_querycap(struct file *file, void *priv, struct v4l2_capability *cap);
static int useeplus_vidioc_fmt_vid_cap(struct file *file, void *priv, struct v4l2_format *f);
static int useeplus_vidioc_enum_fmt_vid_cap(struct file *file, void *priv, struct v4l2_fmtdesc *f);
static int useeplus_vidioc_enum_input(struct file *file, void *priv, struct v4l2_input *inp);
static int useeplus_vidioc_g_input(struct file *file, void *priv, unsigned int *i);
static int useeplus_vidioc_s_input(struct file *file, void *priv, unsigned int i);
static int useeplus_vidioc_g_parm(struct file *file, void *priv, struct v4l2_streamparm *sp);
static int useeplus_vidioc_s_parm(struct file *file, void *priv, struct v4l2_streamparm *sp);
static int useeplus_write_msg(struct usb_useeplus *dev, u8 endpoint_addr, const u8 *tokens, size_t len);
static void useeplus_read_bulk_callback(struct urb *urb);
static void useeplus_kill_urbs(struct usb_useeplus *dev);
static int useeplus_probe(struct usb_interface *interface, const struct usb_device_id *id);
static void useeplus_disconnect(struct usb_interface *interface);
```