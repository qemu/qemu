# Test class and utilities for functional tests
#
# Copyright (c) 2018 Red Hat, Inc.
#
# Author:
#  Cleber Rosa <crosa@redhat.com>
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later.  See the COPYING file in the top-level directory.

import logging
import os
import shutil
import sys
import tempfile
import time
import uuid

import avocado
from avocado.utils import cloudinit, datadrainer, network, ssh, vmimage
from avocado.utils.path import find_command

#: The QEMU build root directory.  It may also be the source directory
#: if building from the source dir, but it's safer to use BUILD_DIR for
#: that purpose.  Be aware that if this code is moved outside of a source
#: and build tree, it will not be accurate.
BUILD_DIR = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(__file__))))

if os.path.islink(os.path.dirname(os.path.dirname(__file__))):
    # The link to the acceptance tests dir in the source code directory
    lnk = os.path.dirname(os.path.dirname(__file__))
    #: The QEMU root source directory
    SOURCE_DIR = os.path.dirname(os.path.dirname(os.readlink(lnk)))
else:
    SOURCE_DIR = BUILD_DIR

sys.path.append(os.path.join(SOURCE_DIR, 'python'))

from qemu.machine import QEMUMachine
from qemu.utils import (get_info_usernet_hostfwd_port, kvm_available,
                        tcg_available)


def is_readable_executable_file(path):
    return os.path.isfile(path) and os.access(path, os.R_OK | os.X_OK)


def pick_default_qemu_bin(arch=None):
    """
    Picks the path of a QEMU binary, starting either in the current working
    directory or in the source tree root directory.

    :param arch: the arch to use when looking for a QEMU binary (the target
                 will match the arch given).  If None (the default), arch
                 will be the current host system arch (as given by
                 :func:`os.uname`).
    :type arch: str
    :returns: the path to the default QEMU binary or None if one could not
              be found
    :rtype: str or None
    """
    if arch is None:
        arch = os.uname()[4]
    # qemu binary path does not match arch for powerpc, handle it
    if 'ppc64le' in arch:
        arch = 'ppc64'
    qemu_bin_relative_path = "./qemu-system-%s" % arch
    if is_readable_executable_file(qemu_bin_relative_path):
        return qemu_bin_relative_path

    qemu_bin_from_bld_dir_path = os.path.join(BUILD_DIR,
                                              qemu_bin_relative_path)
    if is_readable_executable_file(qemu_bin_from_bld_dir_path):
        return qemu_bin_from_bld_dir_path
    return None


def _console_interaction(test, success_message, failure_message,
                         send_string, keep_sending=False, vm=None):
    assert not keep_sending or send_string
    if vm is None:
        vm = test.vm
    console = vm.console_socket.makefile(mode='rb', encoding='utf-8')
    console_logger = logging.getLogger('console')
    while True:
        if send_string:
            vm.console_socket.sendall(send_string.encode())
            if not keep_sending:
                send_string = None # send only once
        try:
            msg = console.readline().decode().strip()
        except UnicodeDecodeError:
            msg = None
        if not msg:
            continue
        console_logger.debug(msg)
        if success_message is None or success_message in msg:
            break
        if failure_message and failure_message in msg:
            console.close()
            fail = 'Failure message found in console: "%s". Expected: "%s"' % \
                    (failure_message, success_message)
            test.fail(fail)

def interrupt_interactive_console_until_pattern(test, success_message,
                                                failure_message=None,
                                                interrupt_string='\r'):
    """
    Keep sending a string to interrupt a console prompt, while logging the
    console output. Typical use case is to break a boot loader prompt, such:

        Press a key within 5 seconds to interrupt boot process.
        5
        4
        3
        2
        1
        Booting default image...

    :param test: an Avocado test containing a VM that will have its console
                 read and probed for a success or failure message
    :type test: :class:`avocado_qemu.Test`
    :param success_message: if this message appears, test succeeds
    :param failure_message: if this message appears, test fails
    :param interrupt_string: a string to send to the console before trying
                             to read a new line
    """
    _console_interaction(test, success_message, failure_message,
                         interrupt_string, True)

def wait_for_console_pattern(test, success_message, failure_message=None,
                             vm=None):
    """
    Waits for messages to appear on the console, while logging the content

    :param test: an Avocado test containing a VM that will have its console
                 read and probed for a success or failure message
    :type test: :class:`avocado_qemu.Test`
    :param success_message: if this message appears, test succeeds
    :param failure_message: if this message appears, test fails
    """
    _console_interaction(test, success_message, failure_message, None, vm=vm)

def exec_command(test, command):
    """
    Send a command to a console (appending CRLF characters), while logging
    the content.

    :param test: an Avocado test containing a VM.
    :type test: :class:`avocado_qemu.Test`
    :param command: the command to send
    :type command: str
    """
    _console_interaction(test, None, None, command + '\r')

def exec_command_and_wait_for_pattern(test, command,
                                      success_message, failure_message=None):
    """
    Send a command to a console (appending CRLF characters), then wait
    for success_message to appear on the console, while logging the.
    content. Mark the test as failed if failure_message is found instead.

    :param test: an Avocado test containing a VM that will have its console
                 read and probed for a success or failure message
    :type test: :class:`avocado_qemu.Test`
    :param command: the command to send
    :param success_message: if this message appears, test succeeds
    :param failure_message: if this message appears, test fails
    """
    _console_interaction(test, success_message, failure_message, command + '\r')

class Test(avocado.Test):
    def _get_unique_tag_val(self, tag_name):
        """
        Gets a tag value, if unique for a key
        """
        vals = self.tags.get(tag_name, [])
        if len(vals) == 1:
            return vals.pop()
        return None

    def require_accelerator(self, accelerator):
        """
        Requires an accelerator to be available for the test to continue

        It takes into account the currently set qemu binary.

        If the check fails, the test is canceled.  If the check itself
        for the given accelerator is not available, the test is also
        canceled.

        :param accelerator: name of the accelerator, such as "kvm" or "tcg"
        :type accelerator: str
        """
        checker = {'tcg': tcg_available,
                   'kvm': kvm_available}.get(accelerator)
        if checker is None:
            self.cancel("Don't know how to check for the presence "
                        "of accelerator %s" % accelerator)
        if not checker(qemu_bin=self.qemu_bin):
            self.cancel("%s accelerator does not seem to be "
                        "available" % accelerator)

    def setUp(self):
        self._vms = {}

        self.arch = self.params.get('arch',
                                    default=self._get_unique_tag_val('arch'))

        self.cpu = self.params.get('cpu',
                                   default=self._get_unique_tag_val('cpu'))

        self.machine = self.params.get('machine',
                                       default=self._get_unique_tag_val('machine'))

        default_qemu_bin = pick_default_qemu_bin(arch=self.arch)
        self.qemu_bin = self.params.get('qemu_bin',
                                        default=default_qemu_bin)
        if self.qemu_bin is None:
            self.cancel("No QEMU binary defined or found in the build tree")

    def _new_vm(self, name, *args):
        self._sd = tempfile.TemporaryDirectory(prefix="avo_qemu_sock_")
        vm = QEMUMachine(self.qemu_bin, base_temp_dir=self.workdir,
                         sock_dir=self._sd.name, log_dir=self.logdir)
        self.log.debug('QEMUMachine "%s" created', name)
        self.log.debug('QEMUMachine "%s" temp_dir: %s', name, vm.temp_dir)
        self.log.debug('QEMUMachine "%s" log_dir: %s', name, vm.log_dir)
        if args:
            vm.add_args(*args)
        return vm

    @property
    def vm(self):
        return self.get_vm(name='default')

    def get_vm(self, *args, name=None):
        if not name:
            name = str(uuid.uuid4())
        if self._vms.get(name) is None:
            self._vms[name] = self._new_vm(name, *args)
            if self.cpu is not None:
                self._vms[name].add_args('-cpu', self.cpu)
            if self.machine is not None:
                self._vms[name].set_machine(self.machine)
        return self._vms[name]

    def set_vm_arg(self, arg, value):
        """
        Set an argument to list of extra arguments to be given to the QEMU
        binary. If the argument already exists then its value is replaced.

        :param arg: the QEMU argument, such as "-cpu" in "-cpu host"
        :type arg: str
        :param value: the argument value, such as "host" in "-cpu host"
        :type value: str
        """
        if not arg or not value:
            return
        if arg not in self.vm.args:
            self.vm.args.extend([arg, value])
        else:
            idx = self.vm.args.index(arg) + 1
            if idx < len(self.vm.args):
                self.vm.args[idx] = value
            else:
                self.vm.args.append(value)

    def tearDown(self):
        for vm in self._vms.values():
            vm.shutdown()
        self._sd = None
        super().tearDown()

    def fetch_asset(self, name,
                    asset_hash=None, algorithm=None,
                    locations=None, expire=None,
                    find_only=False, cancel_on_missing=True):
        return super().fetch_asset(name,
                        asset_hash=asset_hash,
                        algorithm=algorithm,
                        locations=locations,
                        expire=expire,
                        find_only=find_only,
                        cancel_on_missing=cancel_on_missing)


class LinuxSSHMixIn:
    """Contains utility methods for interacting with a guest via SSH."""

    def ssh_connect(self, username, credential, credential_is_key=True):
        self.ssh_logger = logging.getLogger('ssh')
        res = self.vm.command('human-monitor-command',
                              command_line='info usernet')
        port = get_info_usernet_hostfwd_port(res)
        self.assertIsNotNone(port)
        self.assertGreater(port, 0)
        self.log.debug('sshd listening on port: %d', port)
        if credential_is_key:
            self.ssh_session = ssh.Session('127.0.0.1', port=port,
                                           user=username, key=credential)
        else:
            self.ssh_session = ssh.Session('127.0.0.1', port=port,
                                           user=username, password=credential)
        for i in range(10):
            try:
                self.ssh_session.connect()
                return
            except:
                time.sleep(i)
        self.fail('ssh connection timeout')

    def ssh_command(self, command):
        self.ssh_logger.info(command)
        result = self.ssh_session.cmd(command)
        stdout_lines = [line.rstrip() for line
                        in result.stdout_text.splitlines()]
        for line in stdout_lines:
            self.ssh_logger.info(line)
        stderr_lines = [line.rstrip() for line
                        in result.stderr_text.splitlines()]
        for line in stderr_lines:
            self.ssh_logger.warning(line)

        self.assertEqual(result.exit_status, 0,
                         f'Guest command failed: {command}')
        return stdout_lines, stderr_lines

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


class LinuxTest(LinuxSSHMixIn, Test):
    """Facilitates having a cloud-image Linux based available.

    For tests that indend to interact with guests, this is a better choice
    to start with than the more vanilla `Test` class.
    """

    timeout = 900
    distro = None
    username = 'root'
    password = 'password'

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
        self._set_distro()
        self.vm.add_args('-smp', '2')
        self.vm.add_args('-m', '1024')
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
        self.log.debug('Looking for and selecting a qemu-img binary to be '
                       'used to create the bootable snapshot image')
        # If qemu-img has been built, use it, otherwise the system wide one
        # will be used.  If none is available, the test will cancel.
        qemu_img = os.path.join(BUILD_DIR, 'qemu-img')
        if not os.path.exists(qemu_img):
            qemu_img = find_command('qemu-img', False)
        if qemu_img is False:
            self.cancel('Could not find "qemu-img", which is required to '
                        'create the bootable image')
        vmimage.QEMU_IMG = qemu_img

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
            self.phone_home_port = network.find_free_port()
            pubkey_content = None
            if ssh_pubkey:
                with open(ssh_pubkey) as pubkey:
                    pubkey_content = pubkey.read()
            cloudinit.iso(cloudinit_iso, self.name,
                          username=self.username,
                          password=self.password,
                          # QEMU's hard coded usermode router address
                          phone_home_host='10.0.2.2',
                          phone_home_port=self.phone_home_port,
                          authorized_key=pubkey_content)
        except Exception:
            self.cancel('Failed to prepare the cloudinit image')
        return cloudinit_iso

    def set_up_boot(self):
        path = self.download_boot()
        self.vm.add_args('-drive', 'file=%s' % path)

    def set_up_cloudinit(self, ssh_pubkey=None):
        cloudinit_iso = self.prepare_cloudinit(ssh_pubkey)
        self.vm.add_args('-drive', 'file=%s,format=raw' % cloudinit_iso)

    def launch_and_wait(self, set_up_ssh_connection=True):
        self.vm.set_console()
        self.vm.launch()
        console_drainer = datadrainer.LineLogger(self.vm.console_socket.fileno(),
                                                 logger=self.log.getChild('console'))
        console_drainer.start()
        self.log.info('VM launched, waiting for boot confirmation from guest')
        cloudinit.wait_for_phone_home(('0.0.0.0', self.phone_home_port), self.name)
        if set_up_ssh_connection:
            self.log.info('Setting up the SSH connection')
            self.ssh_connect(self.username, self.ssh_key)
