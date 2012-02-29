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
import unittest
import sys; sys.path.append(os.path.join(os.path.dirname(__file__), '..', '..', 'QMP'))
import qmp

__all__ = ['imgfmt', 'imgproto', 'test_dir' 'qemu_img', 'qemu_io',
           'VM', 'QMPTestCase', 'notrun', 'main']

# This will not work if arguments or path contain spaces but is necessary if we
# want to support the override options that ./check supports.
qemu_img_args = os.environ.get('QEMU_IMG', 'qemu-img').split(' ')
qemu_io_args = os.environ.get('QEMU_IO', 'qemu-io').split(' ')
qemu_args = os.environ.get('QEMU', 'qemu').split(' ')

imgfmt = os.environ.get('IMGFMT', 'raw')
imgproto = os.environ.get('IMGPROTO', 'file')
test_dir = os.environ.get('TEST_DIR', '/var/tmp')

def qemu_img(*args):
    '''Run qemu-img and return the exit code'''
    devnull = open('/dev/null', 'r+')
    return subprocess.call(qemu_img_args + list(args), stdin=devnull, stdout=devnull)

def qemu_io(*args):
    '''Run qemu-io and return the stdout data'''
    args = qemu_io_args + list(args)
    return subprocess.Popen(args, stdout=subprocess.PIPE).communicate()[0]

class VM(object):
    '''A QEMU VM'''

    def __init__(self):
        self._monitor_path = os.path.join(test_dir, 'qemu-mon.%d' % os.getpid())
        self._qemu_log_path = os.path.join(test_dir, 'qemu-log.%d' % os.getpid())
        self._args = qemu_args + ['-chardev',
                     'socket,id=mon,path=' + self._monitor_path,
                     '-mon', 'chardev=mon,mode=control', '-nographic']
        self._num_drives = 0

    def add_drive(self, path, opts=''):
        '''Add a virtio-blk drive to the VM'''
        options = ['if=virtio',
                   'format=%s' % imgfmt,
                   'cache=none',
                   'file=%s' % path,
                   'id=drive%d' % self._num_drives]
        if opts:
            options.append(opts)

        self._args.append('-drive')
        self._args.append(','.join(options))
        self._num_drives += 1
        return self

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
        self._qmp.cmd('quit')
        self._popen.wait()
        os.remove(self._monitor_path)
        os.remove(self._qemu_log_path)

    def qmp(self, cmd, **args):
        '''Invoke a QMP command and return the result dict'''
        return self._qmp.cmd(cmd, args=args)

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

    def assert_qmp(self, d, path, value):
        '''Assert that the value for a specific path in a QMP dict matches'''
        result = self.dictpath(d, path)
        self.assertEqual(result, value, 'values not equal "%s" and "%s"' % (str(result), str(value)))

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
        sys.stderr.write(re.sub(r'Ran (\d+) test[s] in [\d.]+s', r'Ran \1 tests', output.getvalue()))
