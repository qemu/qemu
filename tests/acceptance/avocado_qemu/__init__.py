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
import uuid
import tempfile

import avocado

from avocado.utils import cloudinit
from avocado.utils import datadrainer
from avocado.utils import network
from avocado.utils import vmimage
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

from qemu.accel import kvm_available
from qemu.accel import tcg_available
from qemu.machine import QEMUMachine

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


def _console_interaction(test, success_message, failure_message,
                         send_string, keep_sending=False, vm=None):
    assert not keep_sending or send_string
    if vm is None:
        vm = test.vm
    console = vm.console_socket.makefile()
    console_logger = logging.getLogger('console')
    while True:
        if send_string:
            vm.console_socket.sendall(send_string.encode())
            if not keep_sending:
                send_string = None # send only once
        msg = console.readline().strip()
        if not msg:
            continue
        console_logger.debug(msg)
        if success_message in msg:
            break
        if failure_message and failure_message in msg:
            console.close()
            fail = 'Failure message found in console: %s' % failure_message
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

        self.machine = self.params.get('machine',
                                       default=self._get_unique_tag_val('machine'))

        default_qemu_bin = pick_default_qemu_bin(arch=self.arch)
        self.qemu_bin = self.params.get('qemu_bin',
                                        default=default_qemu_bin)
        if self.qemu_bin is None:
            self.cancel("No QEMU binary defined or found in the build tree")

    def _new_vm(self, *args):
        self._sd = tempfile.TemporaryDirectory(prefix="avo_qemu_sock_")
        vm = QEMUMachine(self.qemu_bin, sock_dir=self._sd.name)
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
            self._vms[name] = self._new_vm(*args)
            if self.machine is not None:
                self._vms[name].set_machine(self.machine)
        return self._vms[name]

    def tearDown(self):
        for vm in self._vms.values():
            vm.shutdown()
        self._sd = None

    def fetch_asset(self, name,
                    asset_hash=None, algorithm=None,
                    locations=None, expire=None,
                    find_only=False, cancel_on_missing=True):
        return super(Test, self).fetch_asset(name,
                        asset_hash=asset_hash,
                        algorithm=algorithm,
                        locations=locations,
                        expire=expire,
                        find_only=find_only,
                        cancel_on_missing=cancel_on_missing)


class LinuxTest(Test):
    """Facilitates having a cloud-image Linux based available.

    For tests that indend to interact with guests, this is a better choice
    to start with than the more vanilla `Test` class.
    """

    timeout = 900
    chksum = None

    def setUp(self, ssh_pubkey=None):
        super(LinuxTest, self).setUp()
        self.vm.add_args('-smp', '2')
        self.vm.add_args('-m', '1024')
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
        if image_arch == 'ppc64':
            image_arch = 'ppc64le'
        try:
            boot = vmimage.get(
                'fedora', arch=image_arch, version='31',
                checksum=self.chksum,
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
            with open(ssh_pubkey) as pubkey:
                pubkey_content = pubkey.read()
            cloudinit.iso(cloudinit_iso, self.name,
                          username='root',
                          password='password',
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

    def launch_and_wait(self):
        self.vm.set_console()
        self.vm.launch()
        console_drainer = datadrainer.LineLogger(self.vm.console_socket.fileno(),
                                                 logger=self.log.getChild('console'))
        console_drainer.start()
        self.log.info('VM launched, waiting for boot confirmation from guest')
        cloudinit.wait_for_phone_home(('0.0.0.0', self.phone_home_port), self.name)
