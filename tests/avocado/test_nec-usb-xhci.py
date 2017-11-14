import copy
import os
import tempfile

from avocado_qemu import test
from avocado.utils import process

class TestNecUsbXhci(test.QemuTest):
    """
    Run with:

        avocado run test_nec-usb-xhci.py \
        -m test_nec-usb-xhci.py.data/parameters.yaml

    :avocado: enable
    :avocado: tags=usbstorage
    """

    def setUp(self):
        usbdevice = os.path.join(self.workdir, 'usb.img')
        self.request_image()
        process.run('dd if=/dev/zero of=%s bs=1M count=10' % usbdevice)
        self.vm.args.extend(['-device', 'pci-bridge,id=bridge1,chassis_nr=1'])
        self.vm.args.extend(['-device', 'nec-usb-xhci,id=xhci1,bus=bridge1,addr=0x3'])
        self.vm.args.extend(['-drive', 'file=%s,format=raw,id=drive_usb,if=none' % usbdevice])
        self.vm.args.extend(['-device', 'usb-storage,drive=drive_usb,id=device_usb,bus=xhci1.0'])
        self.vm.launch()

    def test_available_after_migration(self):
        """
        According to the RHBZ1436616, the issue is: usb-storage device
        under pci-bridge is unusable after migration.

        Fixed in commit 243afe858b95765b98d16a1f0dd50dca262858ad.

        :avocado: tags=migration,RHBZ1436616
        """
        console = self.vm.get_console()
        console.sendline('fdisk -l')
        result = console.read_nonblocking()
        console.close()
        self.assertIn('Disk /dev/sdb: 10 MiB, 10485760 bytes, 20480 sectors',
                      result)

        self.vm_dst = self.vm.migrate()
        console = self.vm_dst.get_console()
        console.sendline('fdisk -l')
        result = console.read_nonblocking()
        console.close()
        self.assertIn('Disk /dev/sdb: 10 MiB, 10485760 bytes, 20480 sectors',
                      result)

    def tearDown(self):
        self.vm.shutdown()
        self.vm_dst.shutdown()
