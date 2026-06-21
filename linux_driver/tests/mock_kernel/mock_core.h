#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <assert.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Alias standard C limits to Linux kernel limits */
#define U16_MAX UINT16_MAX

/* --- Core Types --- */
typedef uint8_t	 u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef uint16_t __le16;
typedef uint32_t __le32;
typedef uint64_t dma_addr_t;

/* --- Endianness (macOS / Linux compat) --- */
#if defined(__APPLE__)
#include <libkern/OSByteOrder.h>
#define le16_to_cpu(x) OSSwapLittleToHostInt16(x)
#define htole16(x) OSSwapHostToLittleInt16(x)
#else
#include <endian.h>
#define le16_to_cpu(x) le16toh(x)
#define htole16(x) htole16(x)
#endif

/* --- Macros & Annotations --- */
#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)
#define __packed __attribute__((packed))

#define min_t(type, x, y)                          \
	({                                         \
		type __min1 = (x);                 \
		type __min2 = (y);                 \
		__min1 < __min2 ? __min1 : __min2; \
	})

#define static_assert _Static_assert
#define BUILD_BUG_ON(condition) ((void)sizeof(char[1 - 2 * !!(condition)]))

#define dev_err_ratelimited(dev, fmt, ...) \
	do {                               \
	} while (0)

/* --- Concurrency (No-ops for single-threaded testing) --- */
typedef int spinlock_t;
struct mutex {
	int dummy;
};
#define spin_lock_irqsave(lock, flags) \
	do {                           \
		(void)(flags);         \
	} while (0)
#define spin_unlock_irqrestore(lock, flags) \
	do {                                \
		(void)(flags);              \
	} while (0)

/* --- Linux Linked List API (Fully mocked for testing) --- */
struct list_head {
	struct list_head *next, *prev;
};

static inline void INIT_LIST_HEAD(struct list_head *list)
{
	list->next = list;
	list->prev = list;
}

static inline void list_add_tail(struct list_head *new_node,
				 struct list_head *head)
{
	new_node->prev = head->prev;
	new_node->next = head;
	head->prev->next = new_node;
	head->prev = new_node;
}

static inline int list_empty(const struct list_head *head)
{
	return head->next == head;
}

static inline void list_del(struct list_head *entry)
{
	entry->next->prev = entry->prev;
	entry->prev->next = entry->next;
}

#ifndef offsetof
#define offsetof(TYPE, MEMBER) ((size_t)&((TYPE *)0)->MEMBER)
#endif
#define container_of(ptr, type, member)                            \
	({                                                         \
		const typeof(((type *)0)->member) *__mptr = (ptr); \
		(type *)((char *)__mptr - offsetof(type, member)); \
	})
#define list_first_entry(ptr, type, member) \
	container_of((ptr)->next, type, member)

/* --- Dummy Structs & Workqueues --- */
#define DECLARE_KFIFO_PTR(fifo, type) type *fifo
struct workqueue_struct {
	int dummy;
};
struct work_struct {
	int dummy;
};
struct usb_interface {
	int dummy;
};
struct usb_device {
	int dummy;
};
struct urb {
	int dummy;
};
struct v4l2_device {
	int dummy;
};
struct video_device {
	int dummy;
};
struct vb2_queue {
	int dummy;
};

/* --- V4L2 Memory Management --- */
#define VB2_BUF_STATE_DONE 0
#define VB2_BUF_STATE_ERROR 1

struct vb2_buffer {
	uint64_t timestamp;
	void *mock_vaddr; /* MOCK ONLY: Test sets this to a valid destination array */
};

struct vb2_v4l2_buffer {
	struct vb2_buffer vb2_buf;
	uint32_t	  sequence;
};

static inline void *vb2_plane_vaddr(struct vb2_buffer *vb,
				    unsigned int       plane_no)
{
	(void)plane_no;
	return vb->mock_vaddr;
}
static inline void vb2_set_plane_payload(struct vb2_buffer *vb,
					 unsigned int	    plane_no,
					 unsigned long	    size)
{
	(void)vb;
	(void)plane_no;
	(void)size;
}
static inline void vb2_buffer_done(struct vb2_buffer *vb, int state)
{
	(void)vb;
	(void)state;
}
static inline uint64_t ktime_get_ns(void)
{
	return 0;
}

#ifdef __cplusplus
}
#endif