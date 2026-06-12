obj-m += useeplus_v4l2.o

KDIR ?= /lib/modules/$(shell uname -r)/build
PWD  := $(shell pwd)

ifeq ($(CHECKER),1)
    CHECK_FLAGS := C=1 CHECK="sparse"
endif

default:
	$(MAKE) -C $(KDIR) M=$(PWD) $(CHECK_FLAGS) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
