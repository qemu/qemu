obj-m += acx.o

acx-obj-y += pci.o
acx-obj-y += usb.o

acx-objs := wlan.o conv.o ioctl.o common.o $(acx-obj-y)

# Use this if you have proper Kconfig integration:

#obj-$(CONFIG_ACX) += acx.o
#
#acx-obj-$(CONFIG_ACX_PCI) += pci.o
#acx-obj-$(CONFIG_ACX_USB) += usb.o
#
#acx-objs := wlan.o conv.o ioctl.o common.o $(acx-obj-y)
