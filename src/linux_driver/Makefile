obj-m += useeplus_v4l2.o
useeplus_v4l2-y := useeplus_v4l2_core.o useeplus.o

MODULE_NAME := useeplus_v4l2
GIT_VERSION := $(shell git rev-parse --short HEAD 2>/dev/null || echo "no-git")
BUILD_DATE  := $(shell date +"%Y-%m-%d_%H:%M:%S")
VERSION_STR := $(BUILD_DATE)-$(GIT_VERSION)
KDIR ?= /lib/modules/$(shell uname -r)/build
SMATCH := ~/github/smatch/smatch
PWD  := $(shell pwd)

ccflags-y += -DBUILD_VER='"$(VERSION_STR)"'

ifeq ($(DEBUG),1)
ccflags-y += -DDEBUG
endif

default:
	$(MAKE) C=1 -C $(KDIR) M=$(PWD) W=1 modules

smatch:
	$(MAKE) CHECK=$(SMATCH) C=1 -C $(KDIR) M=$(PWD) modules

coccicheck:
	$(MAKE) -C $(KDIR) M=$(PWD) W=1 coccicheck MODE=report

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean

install: clean default
	@echo "=== Unloading existing module ==="
	-sudo rmmod $(MODULE_NAME) 2>/dev/null || echo "No module to unload."
	@echo "=== Handling Dependencies ==="
	sudo depmod -a
	@echo "=== Loading New Module ==="
	sudo insmod $(MODULE_NAME).ko
	@echo "=== Verification ==="
	@lsmod | grep -q $(MODULE_NAME) && echo "SUCCESS: $(MODULE_NAME) is loaded into memory." || echo "FAILED to load module."
	@echo "--- Last 5 Kernel Messages ---"
	@dmesg | tail -n 5

install-debug:
	$(MAKE) install DEBUG=1

check:
	sparse -D__linux__ -D__KERNEL__ -DCONFIG_64BIT -DCONFIG_SMP \
		-DKBUILD_MODNAME=\"$(MODULE_NAME)\" \
		-DKBUILD_MODFILE=\"$(MODULE_NAME)\" \
		-Wbitwise -Wcontext \
		-I$(KDIR)/include \
		-I$(KDIR)/arch/arm64/include \
		-I$(KDIR)/arch/arm64/include/generated \
		-I$(KDIR)/include/generated \
		-I$(KDIR)/include/uapi \
		-I$(KDIR)/arch/arm64/include/uapi \
		useeplus_v4l2_core.c useeplus.c

style:
	$(KDIR)/scripts/checkpatch.pl --file ./*.c ./*.h
