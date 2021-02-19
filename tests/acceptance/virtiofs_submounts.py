import logging
import re
import os
import subprocess
import time

from avocado import skipUnless
from avocado_qemu import LinuxTest, BUILD_DIR
from avocado_qemu import wait_for_console_pattern
from avocado.utils import ssh


def run_cmd(args):
    subp = subprocess.Popen(args,
                            stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE,
                            universal_newlines=True)
    stdout, stderr = subp.communicate()
    ret = subp.returncode

    return (stdout, stderr, ret)

def has_cmd(name, args=None):
    """
    This function is for use in a @avocado.skipUnless decorator, e.g.:

        @skipUnless(*has_cmd('sudo -n', ('sudo', '-n', 'true')))
        def test_something_that_needs_sudo(self):
            ...
    """

    if args is None:
        args = ('which', name)

    try:
        _, stderr, exitcode = run_cmd(args)
    except Exception as e:
        exitcode = -1
        stderr = str(e)

    if exitcode != 0:
        cmd_line = ' '.join(args)
        err = f'{name} required, but "{cmd_line}" failed: {stderr.strip()}'
        return (False, err)
    else:
        return (True, '')

def has_cmds(*cmds):
    """
    This function is for use in a @avocado.skipUnless decorator and
    allows checking for the availability of multiple commands, e.g.:

        @skipUnless(*has_cmds(('cmd1', ('cmd1', '--some-parameter')),
                              'cmd2', 'cmd3'))
        def test_something_that_needs_cmd1_and_cmd2(self):
            ...
    """

    for cmd in cmds:
        if isinstance(cmd, str):
            cmd = (cmd,)

        ok, errstr = has_cmd(*cmd)
        if not ok:
            return (False, errstr)

    return (True, '')


class VirtiofsSubmountsTest(LinuxTest):
    """
    :avocado: tags=arch:x86_64
    """

    def get_portfwd(self):
        port = None

        res = self.vm.command('human-monitor-command',
                              command_line='info usernet')
        for line in res.split('\r\n'):
            match = \
                re.search(r'TCP.HOST_FORWARD.*127\.0\.0\.1\s+(\d+)\s+10\.',
                          line)
            if match is not None:
                port = int(match[1])
                break

        self.assertIsNotNone(port)
        self.assertGreater(port, 0)
        self.log.debug('sshd listening on port: %d', port)
        return port

    def ssh_connect(self, username, keyfile):
        self.ssh_logger = logging.getLogger('ssh')
        port = self.get_portfwd()
        self.ssh_session = ssh.Session('127.0.0.1', port=port,
                                       user=username, key=keyfile)
        for i in range(10):
            try:
                self.ssh_session.connect()
                return
            except:
                time.sleep(4)
                pass
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

    def run(self, args, ignore_error=False):
        stdout, stderr, ret = run_cmd(args)

        if ret != 0:
            cmdline = ' '.join(args)
            if not ignore_error:
                self.fail(f'{cmdline}: Returned {ret}: {stderr}')
            else:
                self.log.warn(f'{cmdline}: Returned {ret}: {stderr}')

        return (stdout, stderr, ret)

    def set_up_shared_dir(self):
        self.shared_dir = os.path.join(self.workdir, 'virtiofs-shared')

        os.mkdir(self.shared_dir)

        self.run(('cp', self.get_data('guest.sh'),
                 os.path.join(self.shared_dir, 'check.sh')))

        self.run(('cp', self.get_data('guest-cleanup.sh'),
                 os.path.join(self.shared_dir, 'cleanup.sh')))

    def set_up_virtiofs(self):
        attmp = os.getenv('AVOCADO_TESTS_COMMON_TMPDIR')
        self.vfsdsock = os.path.join(attmp, 'vfsdsock')

        self.run(('sudo', '-n', 'rm', '-f', self.vfsdsock), ignore_error=True)

        self.virtiofsd = \
            subprocess.Popen(('sudo', '-n',
                              'tools/virtiofsd/virtiofsd',
                              f'--socket-path={self.vfsdsock}',
                              '-o', f'source={self.shared_dir}',
                              '-o', 'cache=always',
                              '-o', 'xattr',
                              '-o', 'announce_submounts',
                              '-f'),
                             stdout=subprocess.DEVNULL,
                             stderr=subprocess.PIPE,
                             universal_newlines=True)

        while not os.path.exists(self.vfsdsock):
            if self.virtiofsd.poll() is not None:
                self.fail('virtiofsd exited prematurely: ' +
                          self.virtiofsd.communicate()[1])
            time.sleep(0.1)

        self.run(('sudo', '-n', 'chmod', 'go+rw', self.vfsdsock))

        self.vm.add_args('-chardev',
                         f'socket,id=vfsdsock,path={self.vfsdsock}',
                         '-device',
                         'vhost-user-fs-pci,queue-size=1024,chardev=vfsdsock' \
                             ',tag=host',
                         '-object',
                         'memory-backend-file,id=mem,size=1G,' \
                             'mem-path=/dev/shm,share=on',
                         '-numa',
                         'node,memdev=mem')

    def launch_vm(self):
        self.launch_and_wait()
        self.ssh_connect('root', self.ssh_key)

    def set_up_nested_mounts(self):
        scratch_dir = os.path.join(self.shared_dir, 'scratch')
        try:
            os.mkdir(scratch_dir)
        except FileExistsError:
            pass

        args = ['bash', self.get_data('host.sh'), scratch_dir]
        if self.seed:
            args += [self.seed]

        out, _, _ = self.run(args)
        seed = re.search(r'^Seed: \d+', out)
        self.log.info(seed[0])

    def mount_in_guest(self):
        self.ssh_command('mkdir -p /mnt/host')
        self.ssh_command('mount -t virtiofs host /mnt/host')

    def check_in_guest(self):
        self.ssh_command('bash /mnt/host/check.sh /mnt/host/scratch/share')

    def live_cleanup(self):
        self.ssh_command('bash /mnt/host/cleanup.sh /mnt/host/scratch')

        # It would be nice if the above was sufficient to make virtiofsd clear
        # all references to the mounted directories (so they can be unmounted
        # on the host), but unfortunately it is not.  To do so, we have to
        # resort to a remount.
        self.ssh_command('mount -o remount /mnt/host')

        scratch_dir = os.path.join(self.shared_dir, 'scratch')
        self.run(('bash', self.get_data('cleanup.sh'), scratch_dir))

    @skipUnless(*has_cmds(('sudo -n', ('sudo', '-n', 'true')),
                          'ssh-keygen', 'bash', 'losetup', 'mkfs.xfs', 'mount'))
    def setUp(self):
        vmlinuz = self.params.get('vmlinuz')
        if vmlinuz is None:
            """
            The Linux kernel supports FUSE auto-submounts only as of 5.10.
            boot_linux.py currently provides Fedora 31, whose kernel is too
            old, so this test cannot pass with the on-image kernel (you are
            welcome to try, hence the option to force such a test with
            -p vmlinuz='').  Therefore, for now the user must provide a
            sufficiently new custom kernel, or effectively explicitly
            request failure with -p vmlinuz=''.
            Once an image with a sufficiently new kernel is available
            (probably Fedora 34), we can make -p vmlinuz='' the default, so
            that this parameter no longer needs to be specified.
            """
            self.cancel('vmlinuz parameter not set; you must point it to a '
                        'Linux kernel binary to test (to run this test with ' \
                        'the on-image kernel, set it to an empty string)')

        self.seed = self.params.get('seed')

        self.ssh_key = os.path.join(self.workdir, 'id_ed25519')

        self.run(('ssh-keygen', '-N', '', '-t', 'ed25519', '-f', self.ssh_key))

        pubkey = open(self.ssh_key + '.pub').read()

        super(VirtiofsSubmountsTest, self).setUp(pubkey)

        if len(vmlinuz) > 0:
            self.vm.add_args('-kernel', vmlinuz,
                             '-append', 'console=ttyS0 root=/dev/sda1')

        # Allow us to connect to SSH
        self.vm.add_args('-netdev', 'user,id=vnet,hostfwd=:127.0.0.1:0-:22',
                         '-device', 'virtio-net,netdev=vnet')

        self.require_accelerator("kvm")
        self.vm.add_args('-accel', 'kvm')

    def tearDown(self):
        try:
            self.vm.shutdown()
        except:
            pass

        scratch_dir = os.path.join(self.shared_dir, 'scratch')
        self.run(('bash', self.get_data('cleanup.sh'), scratch_dir),
                 ignore_error=True)

    def test_pre_virtiofsd_set_up(self):
        self.set_up_shared_dir()

        self.set_up_nested_mounts()

        self.set_up_virtiofs()
        self.launch_vm()
        self.mount_in_guest()
        self.check_in_guest()

    def test_pre_launch_set_up(self):
        self.set_up_shared_dir()
        self.set_up_virtiofs()

        self.set_up_nested_mounts()

        self.launch_vm()
        self.mount_in_guest()
        self.check_in_guest()

    def test_post_launch_set_up(self):
        self.set_up_shared_dir()
        self.set_up_virtiofs()
        self.launch_vm()

        self.set_up_nested_mounts()

        self.mount_in_guest()
        self.check_in_guest()

    def test_post_mount_set_up(self):
        self.set_up_shared_dir()
        self.set_up_virtiofs()
        self.launch_vm()
        self.mount_in_guest()

        self.set_up_nested_mounts()

        self.check_in_guest()

    def test_two_runs(self):
        self.set_up_shared_dir()

        self.set_up_nested_mounts()

        self.set_up_virtiofs()
        self.launch_vm()
        self.mount_in_guest()
        self.check_in_guest()

        self.live_cleanup()
        self.set_up_nested_mounts()

        self.check_in_guest()
