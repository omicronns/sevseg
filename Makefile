ifneq ($(KERNELRELEASE),)
	obj-m := sevseg.o
else

KERNELDIR ?= /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

default:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules

install:
	xz -zkf sevseg.ko
	cp sevseg.ko.xz /lib/modules/$(shell uname -r)/kernel/drivers/auxdisplay/
	cp sevseg.dtbo /boot/firmware/overlays/
	dtoverlay sevseg.dtbo
	depmod

uninstall:
	rm /lib/modules/$(shell uname -r)/kernel/drivers/auxdisplay/sevseg.ko.xz
	rm /boot/firmware/overlays/sevseg.dtbo
	depmod

dts:
	dtc -I dts -O dtb -o sevseg.dtbo sevseg-overlay.dts

clean:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) clean

check:
	clear
	$(KERNELDIR)/scripts/checkpatch.pl -f sevseg.c

all: default dts
endif
