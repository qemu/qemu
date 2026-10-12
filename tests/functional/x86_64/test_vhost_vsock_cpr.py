#!/usr/bin/env python3
#
# Functional test for vhost-vsock over CPR (cpr-transfer and cpr-exec):
# a host<->guest vsock transfer must survive repeated CPR migrations, in
# either direction.
#
# Copyright (c) 2026 Virtuozzo International GmbH.
#
# SPDX-License-Identifier: GPL-2.0-or-later

import fcntl
import filecmp
import os
import subprocess
import time

from qemu.qmp import ConnectError, ExecInterruptedError
from qemu.qmp.legacy import QEMUMonitorProtocol
from qemu_test import (Asset, LinuxKernelTest, exec_command,
                       exec_command_and_wait_for_pattern, get_qemu_img,
                       skipIfMissingCommands)

GUEST_CID = 4000000010    # we expect this CID to be free on the host
VSOCK_PORT = 5000

TRANSFER_SIZE = 512 * 1024 * 1024    # file size for host<->guest transfer
CPR_INTERVAL = 0.2                   # interval between CPR ops

VHOST_SET_OWNER = 0xAF01      # _IO(VHOST_VIRTIO, 0x01)
VHOST_RESET_OWNER = 0xAF02    # _IO(VHOST_VIRTIO, 0x02)

# What a QMP connection raises when its QEMU goes away underneath it
QMP_GONE = (OSError, EOFError, ConnectError, ExecInterruptedError)


@skipIfMissingCommands('socat')
class VhostVsockCPR(LinuxKernelTest):
    # This should have passwordless root login, vsock support + socat
    ASSET_DISKIMAGE = Asset(
        ('https://cloud.debian.org/images/cloud/bookworm/20231210-1590/'
         'debian-12-nocloud-amd64-20231210-1590.qcow2'),
        'b94e3f34db59988a815b33dffc24f5a58502540efece2cc1ed15e0ac8749bb2e')

    def require_vhost_vsock_cpr(self):
        try:
            fd = os.open('/dev/vhost-vsock', os.O_RDWR)
        except OSError:
            self.skipTest('/dev/vhost-vsock is not available')
        try:
            for name, request in (('VHOST_SET_OWNER', VHOST_SET_OWNER),
                                  ('VHOST_RESET_OWNER', VHOST_RESET_OWNER)):
                try:
                    fcntl.ioctl(fd, request)
                except OSError:
                    self.skipTest(f'{name} is not supported for vhost-vsock')
        finally:
            os.close(fd)

    def setUp(self):
        super().setUp()
        self.require_accelerator('kvm')
        self.require_device('vhost-vsock-pci')
        self.require_vhost_vsock_cpr()
        self.set_machine('q35')

        # Use Debian cloud image as a backing.  During host<->guest
        # transfer the guest will write data to its disk, so we start
        # it with an overlay
        self.disk = self.scratch_file('disk.qcow2')
        subprocess.check_call([get_qemu_img(self), 'create', '-q', '-f',
                               'qcow2', '-b', self.ASSET_DISKIMAGE.fetch(),
                               '-F', 'qcow2', self.disk])

        self.payload = self.scratch_file('payload')
        with open(self.payload, 'wb') as f:
            for _ in range(TRANSFER_SIZE >> 20):
                f.write(os.urandom(1 << 20))

        self.sock_dir = self.socket_dir().name
        self.cpr_count = 0

        self.guest = None
        self.qmp_path = None

    def prepare_vm(self, vm, name, *extra_args):
        # We don't want QEMUMachine.launch() set up QMP for us.  In this
        # case launch() waits for greeting - which won't be sent cause
        # CPR target is reading CPR migration channel before QMP
        qmp_path = os.path.join(self.sock_dir, f'{name}-qmp.sock')
        vm.set_qmp_monitor(False)
        vm.set_console()
        vm.add_args('-accel', 'kvm', '-m', '1G',
                    '-object', 'memory-backend-memfd,id=ram,share=on,size=1G',
                    '-machine', 'memory-backend=ram,aux-ram-share=on',
                    '-drive', f'file={self.disk},if=none,id=disk,format=qcow2',
                    '-device', 'virtio-blk-pci,drive=disk',
                    '-device', f'vhost-vsock-pci,guest-cid={GUEST_CID}',
                    '-qmp', f'unix:{qmp_path},server=on,wait=off',
                    *extra_args)
        return qmp_path

    def wait_for(self, what, cond, timeout=60):
        """Poll cond() until it returns something true, and return that"""
        deadline = time.monotonic() + timeout
        while True:
            value = cond()
            if value:
                return value
            self.assertLess(time.monotonic(), deadline,
                            f'timed out waiting for {what}')
            time.sleep(0.2)

    def qmp_connect(self, qmp_path):
        """Connect to a QEMU which may still be starting up"""
        def attempt():
            try:
                qmp = QEMUMonitorProtocol(qmp_path)
                qmp.connect()
                return qmp
            except (OSError, ConnectError):
                return None
        return self.wait_for('QMP to come up', attempt)

    def wait_migration(self, qmp):
        def completed():
            status = qmp.cmd('query-migrate').get('status')
            self.assertNotEqual(status, 'failed', 'migration failed')
            return status == 'completed'
        self.wait_for('migration completion', completed)

    def wait_running(self, qmp_path):
        # Make sure we're dealing with the new QEMU after CPR
        while True:
            try:
                with self.qmp_connect(qmp_path) as qmp:
                    self.wait_for('the VM to resume', lambda: qmp.cmd(
                        'query-status')['status'] == 'running')
                return
            except QMP_GONE:
                pass

    def boot(self):
        self.guest = self.vm
        self.qmp_path = self.prepare_vm(self.guest, 'source')
        self.guest.launch()
        self.wait_for_console_pattern('login:')
        exec_command_and_wait_for_pattern(self, 'root', 'root@localhost:~#')

    def transfer(self, guest_cmd, host_cmd, cpr):
        """
        Run a socat transfer between the guest and the host, and keep
        performing CPR migrations as long as it's in progress
        """
        exec_command(self, f'{guest_cmd} &', vm=self.guest)
        with subprocess.Popen(host_cmd) as socat:
            while socat.poll() is None:
                time.sleep(CPR_INTERVAL)
                cpr()
        self.assertEqual(socat.returncode, 0, 'host socat failed')
        exec_command_and_wait_for_pattern(self, 'wait %1; echo RC_$?',
                                          'RC_0', vm=self.guest)

    def transfer_both_ways(self, cpr):
        """
        Send the payload to a file on the guest's disk, get it back into
        another file on the host, then compare.
        """
        echo = self.scratch_file('echo')
        connect = f'VSOCK-CONNECT:{GUEST_CID}:{VSOCK_PORT},retry=10'

        self.transfer(f'socat -u VSOCK-LISTEN:{VSOCK_PORT} CREATE:/root/data',
                      ['socat', '-u', f'FILE:{self.payload}', connect], cpr)
        self.transfer(f'socat -u FILE:/root/data VSOCK-LISTEN:{VSOCK_PORT}',
                      ['socat', '-u', connect, f'CREATE:{echo}'], cpr)

        self.log.info('%d CPR migrations performed', self.cpr_count)
        self.assertTrue(filecmp.cmp(self.payload, echo, shallow=False),
                        'the data came back different')

    def test_cpr_transfer(self):
        """
        Perform consecutive cpr-transfer migrations during vsock file
        transfer, starting a new QEMU instance on each one, migrating into
        it and shutting down the source
        """
        def cpr():
            n = self.cpr_count
            mig_sock = os.path.join(self.sock_dir, f'mig{n}.sock')
            cpr_sock = os.path.join(self.sock_dir, f'cpr{n}.sock')
            target = self.get_vm(name=f'target{n}')
            target_qmp_path = self.prepare_vm(
                target, f'target{n}',
                '-incoming', f'unix:{mig_sock}',
                '-incoming', 'cpr,addr.transport=socket,addr.type=unix,'
                             f'addr.path={cpr_sock}')

            # The target listens on the CPR channel: wait for it to be there
            target.launch()
            self.wait_for('the CPR socket', lambda: os.path.exists(cpr_sock))

            with self.qmp_connect(self.qmp_path) as qmp:
                qmp.cmd('migrate-set-parameters', mode='cpr-transfer')
                qmp.cmd('migrate', channels=[
                    {'channel-type': 'main',
                     'addr': {'transport': 'socket', 'type': 'unix',
                              'path': mig_sock}},
                    {'channel-type': 'cpr',
                     'addr': {'transport': 'socket', 'type': 'unix',
                              'path': cpr_sock}}])
                self.wait_migration(qmp)
            self.wait_running(target_qmp_path)

            self.guest.shutdown()
            self.guest, self.qmp_path = target, target_qmp_path
            self.cpr_count += 1

        self.boot()
        self.transfer_both_ways(cpr)

    def test_cpr_exec(self):
        """
        Perform consecutive cpr-exec migration during vsock file transfer,
        saving devices' state to a file
        """
        state = self.scratch_file('cpr.state')

        def cpr():
            try:
                with self.qmp_connect(self.qmp_path) as qmp:
                    qmp.cmd('migrate-set-parameters', **{
                        'mode': 'cpr-exec', 'cpr-exec-command': exec_cmd})
                    qmp.cmd('migrate', uri=f'file:{state}')
                    self.wait_migration(qmp)
            except QMP_GONE:
                pass
            self.wait_running(self.qmp_path)
            self.cpr_count += 1

        self.boot()
        exec_cmd = [*self.guest._qemu_full_args, '-incoming', f'file:{state}']
        self.transfer_both_ways(cpr)


if __name__ == '__main__':
    LinuxKernelTest.main()
