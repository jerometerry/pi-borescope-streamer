.PHONY: default smatch coccicheck clean \
        install install-debug install-with-deps \
        rmmod modprobe-r status insmod \
        check-deps check-stack check-patch check-includes

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
	@echo "=== Loading New Module ==="
	sudo insmod $(MODULE_NAME).ko
	@echo "=== Verification ==="
	@lsmod | grep -q $(MODULE_NAME) && echo "SUCCESS: $(MODULE_NAME) is loaded into memory." || echo "FAILED to load module."
	@echo "--- Last 5 Kernel Messages ---"
	@dmesg | tail -n 5

install-debug:
	$(MAKE) install DEBUG=1

install-with-deps: clean default
	@echo "=== Unloading existing module ==="
	-sudo modprobe -r $(MODULE_NAME) 2>/dev/null || echo "No module to unload."
	@echo "=== Copying to Kernel Tree ==="
	sudo mkdir -p /lib/modules/$(shell uname -r)/updates/dkms
	sudo cp $(MODULE_NAME).ko /lib/modules/$(shell uname -r)/updates/dkms/
	@echo "=== Building Dependency Tree ==="
	sudo depmod -a
	@echo "=== Loading Module & Dependencies ==="
	sudo modprobe $(MODULE_NAME)
	@echo "=== Verification ==="
	@lsmod | grep -q $(MODULE_NAME) && echo "SUCCESS: $(MODULE_NAME) and all deps are loaded." || echo "FAILED to load module."

uninstall:
	@-sudo modprobe -r $(MODULE_NAME) 2>/dev/null || \
	echo "Module $(MODULE_NAME) not loaded or not found."

rmmod:
	@-sudo rmmod $(MODULE_NAME) 2>/dev/null || \
	echo "Module $(MODULE_NAME) not loaded or not found."

status:
	lsmod | grep $(MODULE_NAME)

insmod: default
	sudo insmod ./$(MODULE_NAME).ko

check-deps: default
	@echo "=== Required Dependencies ==="
	@deps=$$(modinfo -F depends $(MODULE_NAME).ko); \
	if [ -z "$$deps" ]; then \
		echo "No external dependencies found."; \
	else \
		echo "Dependencies found: $$deps"; \
		echo "--- Status ---"; \
		echo "$$deps" | tr ',' '\n' | while read dep; do \
			if lsmod | grep -q "^$$dep"; then \
				echo "  [OK]      $$dep is currently loaded."; \
			else \
				echo "  [MISSING] $$dep is NOT loaded!"; \
			fi; \
		done; \
	fi

check-stack: default
	objdump -d useeplus_v4l2.ko | $(KDIR)/scripts/checkstack.pl aarch64

check-patch:
	$(KDIR)/scripts/checkpatch.pl --strict --file ./*.c ./*.h

check-includes:
	$(KDIR)/scripts/checkincludes.pl *.c *.h
