obj-m += useeplus_v4l2.o

KDIR ?= /lib/modules/$(shell uname -r)/build
PWD  := $(shell pwd)

ifeq ($(CHECKER),1)
    export C := 1
    export CHECK := sparse
endif

default:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
