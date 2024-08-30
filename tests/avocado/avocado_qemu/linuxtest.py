# Test class and utilities for functional Linux-based tests
#
# Copyright (c) 2018 Red Hat, Inc.
#
# Author:
#  Cleber Rosa <crosa@redhat.com>
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later.  See the COPYING file in the top-level directory.

import os
import shutil

from avocado.utils import cloudinit, datadrainer, process, vmimage

from avocado_qemu import LinuxSSHMixIn
from avocado_qemu import QemuSystemTest

if os.path.islink(os.path.dirname(os.path.dirname(__file__))):
    # The link to the avocado tests dir in the source code directory
    lnk = os.path.dirname(os.path.dirname(__file__))
    #: The QEMU root source directory
    SOURCE_DIR = os.path.dirname(os.path.dirname(os.readlink(lnk)))
else:
    SOURCE_DIR = BUILD_DIR

class LinuxDistro:
    """Represents a Linux distribution

    Holds information of known distros.
    """
    #: A collection of known distros and their respective image checksum
    KNOWN_DISTROS = {
        'fedora': {
            '31': {
                'x86_64':
                {'checksum': ('e3c1b309d9203604922d6e255c2c5d09'
                              '8a309c2d46215d8fc026954f3c5c27a0'),
                 'pxeboot_url': ('https://archives.fedoraproject.org/'
                                 'pub/archive/fedora/linux/releases/31/'
                                 'Everything/x86_64/os/images/pxeboot/'),
                 'kernel_params': ('root=UUID=b1438b9b-2cab-4065-a99a-'
                                   '08a96687f73c ro no_timer_check '
                                   'net.ifnames=0 console=tty1 '
                                   'console=ttyS0,115200n8'),
                },
                'aarch64':
                {'checksum': ('1e18d9c0cf734940c4b5d5ec592facae'
                              'd2af0ad0329383d5639c997fdf16fe49'),
                'pxeboot_url': 'https://archives.fedoraproject.org/'
                               'pub/archive/fedora/linux/releases/31/'
                               'Everything/aarch64/os/images/pxeboot/',
                'kernel_params': ('root=UUID=b6950a44-9f3c-4076-a9c2-'
                                  '355e8475b0a7 ro earlyprintk=pl011,0x9000000'
                                  ' ignore_loglevel no_timer_check'
                                  ' printk.time=1 rd_NO_PLYMOUTH'
                                  ' console=ttyAMA0'),
                },
                'ppc64':
                {'checksum': ('7c3528b85a3df4b2306e892199a9e1e4'
                              '3f991c506f2cc390dc4efa2026ad2f58')},
                's390x':
                {'checksum': ('4caaab5a434fd4d1079149a072fdc789'
                              '1e354f834d355069ca982fdcaf5a122d')},
            },
            '32': {
                'aarch64':
                {'checksum': ('b367755c664a2d7a26955bbfff985855'
                              'adfa2ca15e908baf15b4b176d68d3967'),
                'pxeboot_url': ('http://dl.fedoraproject.org/pub/fedora/linux/'
                                'releases/32/Server/aarch64/os/images/'
                                'pxeboot/'),
                'kernel_params': ('root=UUID=3df75b65-be8d-4db4-8655-'
                                  '14d95c0e90c5 ro no_timer_check net.ifnames=0'
                                  ' console=tty1 console=ttyS0,115200n8'),
                },
            },
            '33': {
                'aarch64':
                {'checksum': ('e7f75cdfd523fe5ac2ca9eeece68edc1'
                              'a81f386a17f969c1d1c7c87031008a6b'),
                'pxeboot_url': ('http://dl.fedoraproject.org/pub/fedora/linux/'
                                'releases/33/Server/aarch64/os/images/'
                                'pxeboot/'),
                'kernel_params': ('root=UUID=d20b3ffa-6397-4a63-a734-'
                                  '1126a0208f8a ro no_timer_check net.ifnames=0'
                                  ' console=tty1 console=ttyS0,115200n8'
                                  ' console=tty0'),
                 },
            },
        }
    }

    def __init__(self, name, version, arch):
        self.name = name
        self.version = version
        self.arch = arch
        try:
            info = self.KNOWN_DISTROS.get(name).get(version).get(arch)
        except AttributeError:
            # Unknown distro
            info = None
        self._info = info or {}

    @property
    def checksum(self):
        """Gets the cloud-image file checksum"""
        return self._info.get('checksum', None)

    @checksum.setter
    def checksum(self, value):
        self._info['checksum'] = value

    @property
    def pxeboot_url(self):
        """Gets the repository url where pxeboot files can be found"""
        return self._info.get('pxeboot_url', None)

    @property
    def default_kernel_params(self):
        """Gets the default kernel parameters"""
        return self._info.get('kernel_params', None)


class LinuxTest(LinuxSSHMixIn, QemuSystemTest):
    """Facilitates having a cloud-image Linux based available.

    For tests that intend to interact with guests, this is a better choice
    to start with than the more vanilla `QemuSystemTest` class.
    """

    distro = None
    username = 'root'
    password = 'password'
    smp = '2'
    memory = '1024'

    def _set_distro(self):
        distro_name = self.params.get(
            'distro',
            default=self._get_unique_tag_val('distro'))
        if not distro_name:
            distro_name = 'fedora'

        distro_version = self.params.get(
            'distro_version',
            default=self._get_unique_tag_val('distro_version'))
        if not distro_version:
            distro_version = '31'

        self.distro = LinuxDistro(distro_name, distro_version, self.arch)

        # The distro checksum behaves differently than distro name and
        # version. First, it does not respect a tag with the same
        # name, given that it's not expected to be used for filtering
        # (distro name versions are the natural choice).  Second, the
        # order of precedence is: parameter, attribute and then value
        # from KNOWN_DISTROS.
        distro_checksum = self.params.get('distro_checksum',
                                          default=None)
        if distro_checksum:
            self.distro.checksum = distro_checksum

    def setUp(self, ssh_pubkey=None, network_device_type='virtio-net'):
        super().setUp()
        self.require_netdev('user')
        self._set_distro()
        self.vm.add_args('-smp', self.smp)
        self.vm.add_args('-m', self.memory)
        # The following network device allows for SSH connections
        self.vm.add_args('-netdev', 'user,id=vnet,hostfwd=:127.0.0.1:0-:22',
                         '-device', '%s,netdev=vnet' % network_device_type)
        self.set_up_boot()
        if ssh_pubkey is None:
            ssh_pubkey, self.ssh_key = self.set_up_existing_ssh_keys()
        self.set_up_cloudinit(ssh_pubkey)

    def set_up_existing_ssh_keys(self):
        ssh_public_key = os.path.join(SOURCE_DIR, 'tests', 'keys', 'id_rsa.pub')
        source_private_key = os.path.join(SOURCE_DIR, 'tests', 'keys', 'id_rsa')
        ssh_dir = os.path.join(self.workdir, '.ssh')
        os.mkdir(ssh_dir, mode=0o700)
        ssh_private_key = os.path.join(ssh_dir,
                                       os.path.basename(source_private_key))
        shutil.copyfile(source_private_key, ssh_private_key)
        os.chmod(ssh_private_key, 0o600)
        return (ssh_public_key, ssh_private_key)

    def download_boot(self):
        # Set the qemu-img binary.
        # If none is available, the test will cancel.
        vmimage.QEMU_IMG = super().get_qemu_img()

        self.log.info('Downloading/preparing boot image')
        # Fedora 31 only provides ppc64le images
        image_arch = self.arch
        if self.distro.name == 'fedora':
            if image_arch == 'ppc64':
                image_arch = 'ppc64le'

        try:
            boot = vmimage.get(
                self.distro.name, arch=image_arch, version=self.distro.version,
                checksum=self.distro.checksum,
                algorithm='sha256',
                cache_dir=self.cache_dirs[0],
                snapshot_dir=self.workdir)
        except:
            self.cancel('Failed to download/prepare boot image')
        return boot.path

    def prepare_cloudinit(self, ssh_pubkey=None):
        self.log.info('Preparing cloudinit image')
        try:
            cloudinit_iso = os.path.join(self.workdir, 'cloudinit.iso')
            pubkey_content = None
            if ssh_pubkey:
                with open(ssh_pubkey) as pubkey:
                    pubkey_content = pubkey.read()
            cloudinit.iso(cloudinit_iso, self.name,
                          username=self.username,
                          password=self.password,
                          # QEMU's hard coded usermode router address
                          phone_home_host='10.0.2.2',
                          phone_home_port=self.phone_server.server_port,
                          authorized_key=pubkey_content)
        except Exception:
            self.cancel('Failed to prepare the cloudinit image')
        return cloudinit_iso

    def set_up_boot(self):
        path = self.download_boot()
        self.vm.add_args('-drive', 'file=%s' % path)

    def set_up_cloudinit(self, ssh_pubkey=None):
        self.phone_server = cloudinit.PhoneHomeServer(('0.0.0.0', 0),
                                                      self.name)
        cloudinit_iso = self.prepare_cloudinit(ssh_pubkey)
        self.vm.add_args('-drive', 'file=%s,format=raw' % cloudinit_iso)

    def launch_and_wait(self, set_up_ssh_connection=True):
        self.vm.set_console()
        self.vm.launch()
        console_drainer = datadrainer.LineLogger(self.vm.console_socket.fileno(),
                                                 logger=self.log.getChild('console'))
        console_drainer.start()
        self.log.info('VM launched, waiting for boot confirmation from guest')
        while not self.phone_server.instance_phoned_back:
            self.phone_server.handle_request()

        if set_up_ssh_connection:
            self.log.info('Setting up the SSH connection')
            self.ssh_connect(self.username, self.ssh_key)
