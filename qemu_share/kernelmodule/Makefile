# Makefile for the kernel module
obj-m += cxl_switch_driver.o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(shell pwd) modules


clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(shell pwd) clean


