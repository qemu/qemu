# Common utilities and Python wrappers for qemu-iotests
#
# Copyright (C) 2012 IBM Corp.
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.
#

import os
import re
import subprocess
import string
import unittest
import sys; sys.path.append(os.path.join(os.path.dirname(__file__), '..', '..', 'scripts', 'qmp'))
import qmp
import struct

__all__ = ['imgfmt', 'imgproto', 'test_dir' 'qemu_img', 'qemu_io',
           'VM', 'QMPTestCase', 'notrun', 'main']

# This will not work if arguments or path contain spaces but is necessary if we
# want to support the override options that ./check supports.
qemu_img_args = os.environ.get('QEMU_IMG', 'qemu-img').strip().split(' ')
qemu_io_args = os.environ.get('QEMU_IO', 'qemu-io').strip().split(' ')
qemu_args = os.environ.get('QEMU', 'qemu').strip().split(' ')

imgfmt = os.environ.get('IMGFMT', 'raw')
imgproto = os.environ.get('IMGPROTO', 'file')
test_dir = os.environ.get('TEST_DIR', '/var/tmp')
cachemode = os.environ.get('CACHEMODE')

socket_scm_helper = os.environ.get('SOCKET_SCM_HELPER', 'socket_scm_helper')

def qemu_img(*args):
    '''Run qemu-img and return the exit code'''
    devnull = open('/dev/null', 'r+')
    return subprocess.call(qemu_img_args + list(args), stdin=devnull, stdout=devnull)

def qemu_img_verbose(*args):
    '''Run qemu-img without suppressing its output and return the exit code'''
    return subprocess.call(qemu_img_args + list(args))

def qemu_img_pipe(*args):
    '''Run qemu-img and return its output'''
    return subprocess.Popen(qemu_img_args + list(args), stdout=subprocess.PIPE).communicate()[0]

def qemu_io(*args):
    '''Run qemu-io and return the stdout data'''
    args = qemu_io_args + list(args)
    return subprocess.Popen(args, stdout=subprocess.PIPE).communicate()[0]

def compare_images(img1, img2):
    '''Return True if two image files are identical'''
    return qemu_img('compare', '-f', imgfmt,
                    '-F', imgfmt, img1, img2) == 0

def create_image(name, size):
    '''Create a fully-allocated raw image with sector markers'''
    file = open(name, 'w')
    i = 0
    while i < size:
        sector = struct.pack('>l504xl', i / 512, i / 512)
        file.write(sector)
        i = i + 512
    file.close()

class VM(object):
    '''A QEMU VM'''

    def __init__(self):
        self._monitor_path = os.path.join(test_dir, 'qemu-mon.%d' % os.getpid())
        self._qemu_log_path = os.path.join(test_dir, 'qemu-log.%d' % os.getpid())
        self._args = qemu_args + ['-chardev',
                     'socket,id=mon,path=' + self._monitor_path,
                     '-mon', 'chardev=mon,mode=control',
                     '-qtest', 'stdio', '-machine', 'accel=qtest',
                     '-display', 'none', '-vga', 'none']
        self._num_drives = 0

    # This can be used to add an unused monitor instance.
    def add_monitor_telnet(self, ip, port):
        args = 'tcp:%s:%d,server,nowait,telnet' % (ip, port)
        self._args.append('-monitor')
        self._args.append(args)

    def add_drive(self, path, opts=''):
        '''Add a virtio-blk drive to the VM'''
        options = ['if=virtio',
                   'format=%s' % imgfmt,
                   'cache=%s' % cachemode,
                   'file=%s' % path,
                   'id=drive%d' % self._num_drives]
        if opts:
            options.append(opts)

        self._args.append('-drive')
        self._args.append(','.join(options))
        self._num_drives += 1
        return self

    def pause_drive(self, drive, event=None):
        '''Pause drive r/w operations'''
        if not event:
            self.pause_drive(drive, "read_aio")
            self.pause_drive(drive, "write_aio")
            return
        self.qmp('human-monitor-command',
                    command_line='qemu-io %s "break %s bp_%s"' % (drive, event, drive))

    def resume_drive(self, drive):
        self.qmp('human-monitor-command',
                    command_line='qemu-io %s "remove_break bp_%s"' % (drive, drive))

    def hmp_qemu_io(self, drive, cmd):
        '''Write to a given drive using an HMP command'''
        return self.qmp('human-monitor-command',
                        command_line='qemu-io %s "%s"' % (drive, cmd))

    def add_fd(self, fd, fdset, opaque, opts=''):
        '''Pass a file descriptor to the VM'''
        options = ['fd=%d' % fd,
                   'set=%d' % fdset,
                   'opaque=%s' % opaque]
        if opts:
            options.append(opts)

        self._args.append('-add-fd')
        self._args.append(','.join(options))
        return self

    def send_fd_scm(self, fd_file_path):
        # In iotest.py, the qmp should always use unix socket.
        assert self._qmp.is_scm_available()
        bin = socket_scm_helper
        if os.path.exists(bin) == False:
            print "Scm help program does not present, path '%s'." % bin
            return -1
        fd_param = ["%s" % bin,
                    "%d" % self._qmp.get_sock_fd(),
                    "%s" % fd_file_path]
        devnull = open('/dev/null', 'rb')
        p = subprocess.Popen(fd_param, stdin=devnull, stdout=sys.stdout,
                             stderr=sys.stderr)
        return p.wait()

    def launch(self):
        '''Launch the VM and establish a QMP connection'''
        devnull = open('/dev/null', 'rb')
        qemulog = open(self._qemu_log_path, 'wb')
        try:
            self._qmp = qmp.QEMUMonitorProtocol(self._monitor_path, server=True)
            self._popen = subprocess.Popen(self._args, stdin=devnull, stdout=qemulog,
                                           stderr=subprocess.STDOUT)
            self._qmp.accept()
        except:
            os.remove(self._monitor_path)
            raise

    def shutdown(self):
        '''Terminate the VM and clean up'''
        if not self._popen is None:
            self._qmp.cmd('quit')
            self._popen.wait()
            os.remove(self._monitor_path)
            os.remove(self._qemu_log_path)
            self._popen = None

    underscore_to_dash = string.maketrans('_', '-')
    def qmp(self, cmd, **args):
        '''Invoke a QMP command and return the result dict'''
        qmp_args = dict()
        for k in args.keys():
            qmp_args[k.translate(self.underscore_to_dash)] = args[k]

        return self._qmp.cmd(cmd, args=qmp_args)

    def get_qmp_event(self, wait=False):
        '''Poll for one queued QMP events and return it'''
        return self._qmp.pull_event(wait=wait)

    def get_qmp_events(self, wait=False):
        '''Poll for queued QMP events and return a list of dicts'''
        events = self._qmp.get_events(wait=wait)
        self._qmp.clear_events()
        return events

index_re = re.compile(r'([^\[]+)\[([^\]]+)\]')

class QMPTestCase(unittest.TestCase):
    '''Abstract base class for QMP test cases'''

    def dictpath(self, d, path):
        '''Traverse a path in a nested dict'''
        for component in path.split('/'):
            m = index_re.match(component)
            if m:
                component, idx = m.groups()
                idx = int(idx)

            if not isinstance(d, dict) or component not in d:
                self.fail('failed path traversal for "%s" in "%s"' % (path, str(d)))
            d = d[component]

            if m:
                if not isinstance(d, list):
                    self.fail('path component "%s" in "%s" is not a list in "%s"' % (component, path, str(d)))
                try:
                    d = d[idx]
                except IndexError:
                    self.fail('invalid index "%s" in path "%s" in "%s"' % (idx, path, str(d)))
        return d

    def assert_qmp_absent(self, d, path):
        try:
            result = self.dictpath(d, path)
        except AssertionError:
            return
        self.fail('path "%s" has value "%s"' % (path, str(result)))

    def assert_qmp(self, d, path, value):
        '''Assert that the value for a specific path in a QMP dict matches'''
        result = self.dictpath(d, path)
        self.assertEqual(result, value, 'values not equal "%s" and "%s"' % (str(result), str(value)))

    def assert_no_active_block_jobs(self):
        result = self.vm.qmp('query-block-jobs')
        self.assert_qmp(result, 'return', [])

    def cancel_and_wait(self, drive='drive0', force=False, resume=False):
        '''Cancel a block job and wait for it to finish, returning the event'''
        result = self.vm.qmp('block-job-cancel', device=drive, force=force)
        self.assert_qmp(result, 'return', {})

        if resume:
            self.vm.resume_drive(drive)

        cancelled = False
        result = None
        while not cancelled:
            for event in self.vm.get_qmp_events(wait=True):
                if event['event'] == 'BLOCK_JOB_COMPLETED' or \
                   event['event'] == 'BLOCK_JOB_CANCELLED':
                    self.assert_qmp(event, 'data/device', drive)
                    result = event
                    cancelled = True

        self.assert_no_active_block_jobs()
        return result

    def wait_until_completed(self, drive='drive0'):
        '''Wait for a block job to finish, returning the event'''
        completed = False
        while not completed:
            for event in self.vm.get_qmp_events(wait=True):
                if event['event'] == 'BLOCK_JOB_COMPLETED':
                    self.assert_qmp(event, 'data/device', drive)
                    self.assert_qmp_absent(event, 'data/error')
                    self.assert_qmp(event, 'data/offset', self.image_len)
                    self.assert_qmp(event, 'data/len', self.image_len)
                    completed = True

        self.assert_no_active_block_jobs()
        return event

def notrun(reason):
    '''Skip this test suite'''
    # Each test in qemu-iotests has a number ("seq")
    seq = os.path.basename(sys.argv[0])

    open('%s.notrun' % seq, 'wb').write(reason + '\n')
    print '%s not run: %s' % (seq, reason)
    sys.exit(0)

def main(supported_fmts=[]):
    '''Run tests'''

    if supported_fmts and (imgfmt not in supported_fmts):
        notrun('not suitable for this image format: %s' % imgfmt)

    # We need to filter out the time taken from the output so that qemu-iotest
    # can reliably diff the results against master output.
    import StringIO
    output = StringIO.StringIO()

    class MyTestRunner(unittest.TextTestRunner):
        def __init__(self, stream=output, descriptions=True, verbosity=1):
            unittest.TextTestRunner.__init__(self, stream, descriptions, verbosity)

    # unittest.main() will use sys.exit() so expect a SystemExit exception
    try:
        unittest.main(testRunner=MyTestRunner)
    finally:
        sys.stderr.write(re.sub(r'Ran (\d+) tests? in [\d.]+s', r'Ran \1 tests', output.getvalue()))
