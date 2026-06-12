obj-m += useeplus_v4l2.o

KDIR ?= /lib/modules/$(shell uname -r)/build
PWD  := $(shell pwd)

default:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

check:
	sparse -D__linux__ -D__KERNEL__ -DCONFIG_64BIT \
		-Wbitwise -Wcontext \
		-I$(KDIR)/include \
		-I$(KDIR)/arch/arm64/include \
		-I$(KDIR)/include/uapi \
		-I$(KDIR)/arch/arm64/include/uapi \
		useeplus_v4l2.c

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
