hw_usb_modules = {}

# usb subsystem core
softmmu_ss.add(when: 'CONFIG_USB', if_true: files(
  'bus.c',
  'combined-packet.c',
  'core.c',
  'desc.c',
  'desc-msos.c',
  'libhw.c',
  'pcap.c',
))

# usb host adapters
softmmu_ss.add(when: 'CONFIG_USB_UHCI', if_true: files('hcd-uhci.c'))
softmmu_ss.add(when: 'CONFIG_USB_OHCI', if_true: files('hcd-ohci.c'))
softmmu_ss.add(when: 'CONFIG_USB_OHCI_PCI', if_true: files('hcd-ohci-pci.c'))
softmmu_ss.add(when: 'CONFIG_USB_EHCI', if_true: files('hcd-ehci.c'))
softmmu_ss.add(when: 'CONFIG_USB_EHCI_PCI', if_true: files('hcd-ehci-pci.c'))
softmmu_ss.add(when: 'CONFIG_USB_EHCI_SYSBUS', if_true: files('hcd-ehci.c', 'hcd-ehci-sysbus.c'))
softmmu_ss.add(when: 'CONFIG_USB_XHCI', if_true: files('hcd-xhci.c'))
softmmu_ss.add(when: 'CONFIG_USB_XHCI_PCI', if_true: files('hcd-xhci-pci.c'))
softmmu_ss.add(when: 'CONFIG_USB_XHCI_SYSBUS', if_true: files('hcd-xhci-sysbus.c'))
softmmu_ss.add(when: 'CONFIG_USB_XHCI_NEC', if_true: files('hcd-xhci-nec.c'))
softmmu_ss.add(when: 'CONFIG_USB_MUSB', if_true: files('hcd-musb.c'))
softmmu_ss.add(when: 'CONFIG_USB_DWC2', if_true: files('hcd-dwc2.c'))
softmmu_ss.add(when: 'CONFIG_USB_DWC3', if_true: files('hcd-dwc3.c'))

softmmu_ss.add(when: 'CONFIG_TUSB6010', if_true: files('tusb6010.c'))
softmmu_ss.add(when: 'CONFIG_IMX', if_true: files('chipidea.c'))
softmmu_ss.add(when: 'CONFIG_IMX_USBPHY', if_true: files('imx-usb-phy.c'))
softmmu_ss.add(when: 'CONFIG_VT82C686', if_true: files('vt82c686-uhci-pci.c'))
softmmu_ss.add(when: 'CONFIG_XLNX_VERSAL', if_true: files('xlnx-versal-usb2-ctrl-regs.c'))
softmmu_ss.add(when: 'CONFIG_XLNX_USB_SUBSYS', if_true: files('xlnx-usb-subsystem.c'))

# emulated usb devices
softmmu_ss.add(when: 'CONFIG_USB', if_true: files('dev-hub.c'))
softmmu_ss.add(when: 'CONFIG_USB', if_true: files('dev-hid.c'))
softmmu_ss.add(when: 'CONFIG_USB_TABLET_WACOM', if_true: files('dev-wacom.c'))
softmmu_ss.add(when: 'CONFIG_USB_STORAGE_CORE', if_true: files('dev-storage.c'))
softmmu_ss.add(when: 'CONFIG_USB_STORAGE_BOT', if_true: files('dev-storage-bot.c'))
softmmu_ss.add(when: 'CONFIG_USB_STORAGE_CLASSIC', if_true: files('dev-storage-classic.c'))
softmmu_ss.add(when: 'CONFIG_USB_STORAGE_UAS', if_true: files('dev-uas.c'))
softmmu_ss.add(when: 'CONFIG_USB_AUDIO', if_true: files('dev-audio.c'))
softmmu_ss.add(when: 'CONFIG_USB_SERIAL', if_true: files('dev-serial.c'))
softmmu_ss.add(when: 'CONFIG_USB_NETWORK', if_true: files('dev-network.c'))
softmmu_ss.add(when: ['CONFIG_POSIX', 'CONFIG_USB_STORAGE_MTP'], if_true: files('dev-mtp.c'))

# smartcard
softmmu_ss.add(when: 'CONFIG_USB_SMARTCARD', if_true: files('dev-smartcard-reader.c'))

if cacard.found()
  usbsmartcard_ss = ss.source_set()
  usbsmartcard_ss.add(when: 'CONFIG_USB_SMARTCARD',
                      if_true: [cacard, files('ccid-card-emulated.c', 'ccid-card-passthru.c')])
  hw_usb_modules += {'smartcard': usbsmartcard_ss}
endif

# U2F
softmmu_ss.add(when: 'CONFIG_USB_U2F', if_true: files('u2f.c'))
softmmu_ss.add(when: ['CONFIG_LINUX', 'CONFIG_USB_U2F'], if_true: [libudev, files('u2f-passthru.c')])
if u2f.found()
  softmmu_ss.add(when: 'CONFIG_USB_U2F', if_true: [u2f, files('u2f-emulated.c')])
endif

# CanoKey
if canokey.found()
  softmmu_ss.add(when: 'CONFIG_USB_CANOKEY', if_true: [canokey, files('canokey.c')])
endif

# usb redirect
if usbredir.found()
  usbredir_ss = ss.source_set()
  usbredir_ss.add(when: 'CONFIG_USB',
                  if_true: [usbredir, files('redirect.c', 'quirks.c')])
  hw_usb_modules += {'redirect': usbredir_ss}
endif

# usb pass-through
if libusb.found()
  usbhost_ss = ss.source_set()
  usbhost_ss.add(when: ['CONFIG_USB', libusb],
                 if_true: files('host-libusb.c'))
  hw_usb_modules += {'host': usbhost_ss}
endif

softmmu_ss.add(when: ['CONFIG_USB', 'CONFIG_XEN', libusb], if_true: files('xen-usb.c'))

modules += { 'hw-usb': hw_usb_modules }
