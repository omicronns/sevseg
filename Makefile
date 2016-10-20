ifneq ($(KERNELRELEASE),)
	obj-m := sevseg.o
else

KERNELDIR ?= /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

default:
	clear
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules
	
insm:
	insmod sevseg.ko

rmm:
	rmmod sevseg

dts:
	dtc -I dts -O dtb -o sevseg.dtbo sevseg-overlay.dts

clean:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) clean

check:
	clear
	$(KERNELDIR)/scripts/checkpatch.pl -f sevseg.c

endif
