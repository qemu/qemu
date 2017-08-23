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

import errno
import os
import re
import subprocess
import string
import unittest
import sys
sys.path.append(os.path.join(os.path.dirname(__file__), '..', '..', 'scripts'))
import qtest
import struct
import json
import signal


# This will not work if arguments contain spaces but is necessary if we
# want to support the override options that ./check supports.
qemu_img_args = [os.environ.get('QEMU_IMG_PROG', 'qemu-img')]
if os.environ.get('QEMU_IMG_OPTIONS'):
    qemu_img_args += os.environ['QEMU_IMG_OPTIONS'].strip().split(' ')

qemu_io_args = [os.environ.get('QEMU_IO_PROG', 'qemu-io')]
if os.environ.get('QEMU_IO_OPTIONS'):
    qemu_io_args += os.environ['QEMU_IO_OPTIONS'].strip().split(' ')

qemu_nbd_args = [os.environ.get('QEMU_NBD_PROG', 'qemu-nbd')]
if os.environ.get('QEMU_NBD_OPTIONS'):
    qemu_nbd_args += os.environ['QEMU_NBD_OPTIONS'].strip().split(' ')

qemu_prog = os.environ.get('QEMU_PROG', 'qemu')
qemu_opts = os.environ.get('QEMU_OPTIONS', '').strip().split(' ')

imgfmt = os.environ.get('IMGFMT', 'raw')
imgproto = os.environ.get('IMGPROTO', 'file')
test_dir = os.environ.get('TEST_DIR')
output_dir = os.environ.get('OUTPUT_DIR', '.')
cachemode = os.environ.get('CACHEMODE')
qemu_default_machine = os.environ.get('QEMU_DEFAULT_MACHINE')

socket_scm_helper = os.environ.get('SOCKET_SCM_HELPER', 'socket_scm_helper')
debug = False

def qemu_img(*args):
    '''Run qemu-img and return the exit code'''
    devnull = open('/dev/null', 'r+')
    exitcode = subprocess.call(qemu_img_args + list(args), stdin=devnull, stdout=devnull)
    if exitcode < 0:
        sys.stderr.write('qemu-img received signal %i: %s\n' % (-exitcode, ' '.join(qemu_img_args + list(args))))
    return exitcode

def qemu_img_verbose(*args):
    '''Run qemu-img without suppressing its output and return the exit code'''
    exitcode = subprocess.call(qemu_img_args + list(args))
    if exitcode < 0:
        sys.stderr.write('qemu-img received signal %i: %s\n' % (-exitcode, ' '.join(qemu_img_args + list(args))))
    return exitcode

def qemu_img_pipe(*args):
    '''Run qemu-img and return its output'''
    subp = subprocess.Popen(qemu_img_args + list(args),
                            stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT)
    exitcode = subp.wait()
    if exitcode < 0:
        sys.stderr.write('qemu-img received signal %i: %s\n' % (-exitcode, ' '.join(qemu_img_args + list(args))))
    return subp.communicate()[0]

def qemu_io(*args):
    '''Run qemu-io and return the stdout data'''
    args = qemu_io_args + list(args)
    subp = subprocess.Popen(args, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT)
    exitcode = subp.wait()
    if exitcode < 0:
        sys.stderr.write('qemu-io received signal %i: %s\n' % (-exitcode, ' '.join(args)))
    return subp.communicate()[0]

def qemu_nbd(*args):
    '''Run qemu-nbd in daemon mode and return the parent's exit code'''
    return subprocess.call(qemu_nbd_args + ['--fork'] + list(args))

def compare_images(img1, img2, fmt1=imgfmt, fmt2=imgfmt):
    '''Return True if two image files are identical'''
    return qemu_img('compare', '-f', fmt1,
                    '-F', fmt2, img1, img2) == 0

def create_image(name, size):
    '''Create a fully-allocated raw image with sector markers'''
    file = open(name, 'w')
    i = 0
    while i < size:
        sector = struct.pack('>l504xl', i / 512, i / 512)
        file.write(sector)
        i = i + 512
    file.close()

def image_size(img):
    '''Return image's virtual size'''
    r = qemu_img_pipe('info', '--output=json', '-f', imgfmt, img)
    return json.loads(r)['virtual-size']

test_dir_re = re.compile(r"%s" % test_dir)
def filter_test_dir(msg):
    return test_dir_re.sub("TEST_DIR", msg)

win32_re = re.compile(r"\r")
def filter_win32(msg):
    return win32_re.sub("", msg)

qemu_io_re = re.compile(r"[0-9]* ops; [0-9\/:. sec]* \([0-9\/.inf]* [EPTGMKiBbytes]*\/sec and [0-9\/.inf]* ops\/sec\)")
def filter_qemu_io(msg):
    msg = filter_win32(msg)
    return qemu_io_re.sub("X ops; XX:XX:XX.X (XXX YYY/sec and XXX ops/sec)", msg)

chown_re = re.compile(r"chown [0-9]+:[0-9]+")
def filter_chown(msg):
    return chown_re.sub("chown UID:GID", msg)

def filter_qmp_event(event):
    '''Filter a QMP event dict'''
    event = dict(event)
    if 'timestamp' in event:
        event['timestamp']['seconds'] = 'SECS'
        event['timestamp']['microseconds'] = 'USECS'
    return event

def log(msg, filters=[]):
    for flt in filters:
        msg = flt(msg)
    print msg

class Timeout:
    def __init__(self, seconds, errmsg = "Timeout"):
        self.seconds = seconds
        self.errmsg = errmsg
    def __enter__(self):
        signal.signal(signal.SIGALRM, self.timeout)
        signal.setitimer(signal.ITIMER_REAL, self.seconds)
        return self
    def __exit__(self, type, value, traceback):
        signal.setitimer(signal.ITIMER_REAL, 0)
        return False
    def timeout(self, signum, frame):
        raise Exception(self.errmsg)

class VM(qtest.QEMUQtestMachine):
    '''A QEMU VM'''

    def __init__(self, path_suffix=''):
        name = "qemu%s-%d" % (path_suffix, os.getpid())
        super(VM, self).__init__(qemu_prog, qemu_opts, name=name,
                                 test_dir=test_dir,
                                 socket_scm_helper=socket_scm_helper)
        if debug:
            self._debug = True
        self._num_drives = 0

    def add_device(self, opts):
        self._args.append('-device')
        self._args.append(opts)
        return self

    def add_drive_raw(self, opts):
        self._args.append('-drive')
        self._args.append(opts)
        return self

    def add_drive(self, path, opts='', interface='virtio', format=imgfmt):
        '''Add a virtio-blk drive to the VM'''
        options = ['if=%s' % interface,
                   'id=drive%d' % self._num_drives]

        if path is not None:
            options.append('file=%s' % path)
            options.append('format=%s' % format)
            options.append('cache=%s' % cachemode)

        if opts:
            options.append(opts)

        self._args.append('-drive')
        self._args.append(','.join(options))
        self._num_drives += 1
        return self

    def add_blockdev(self, opts):
        self._args.append('-blockdev')
        if isinstance(opts, str):
            self._args.append(opts)
        else:
            self._args.append(','.join(opts))
        return self

    def add_incoming(self, addr):
        self._args.append('-incoming')
        self._args.append(addr)
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

    def flatten_qmp_object(self, obj, output=None, basestr=''):
        if output is None:
            output = dict()
        if isinstance(obj, list):
            for i in range(len(obj)):
                self.flatten_qmp_object(obj[i], output, basestr + str(i) + '.')
        elif isinstance(obj, dict):
            for key in obj:
                self.flatten_qmp_object(obj[key], output, basestr + key + '.')
        else:
            output[basestr[:-1]] = obj # Strip trailing '.'
        return output

    def qmp_to_opts(self, obj):
        obj = self.flatten_qmp_object(obj)
        output_list = list()
        for key in obj:
            output_list += [key + '=' + obj[key]]
        return ','.join(output_list)

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

    def assert_has_block_node(self, node_name=None, file_name=None):
        """Issue a query-named-block-nodes and assert node_name and/or
        file_name is present in the result"""
        def check_equal_or_none(a, b):
            return a == None or b == None or a == b
        assert node_name or file_name
        result = self.vm.qmp('query-named-block-nodes')
        for x in result["return"]:
            if check_equal_or_none(x.get("node-name"), node_name) and \
                    check_equal_or_none(x.get("file"), file_name):
                return
        self.assertTrue(False, "Cannot find %s %s in result:\n%s" % \
                (node_name, file_name, result))

    def assert_json_filename_equal(self, json_filename, reference):
        '''Asserts that the given filename is a json: filename and that its
           content is equal to the given reference object'''
        self.assertEqual(json_filename[:5], 'json:')
        self.assertEqual(self.flatten_qmp_object(json.loads(json_filename[5:])),
                         self.flatten_qmp_object(reference))

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

    def wait_until_completed(self, drive='drive0', check_offset=True):
        '''Wait for a block job to finish, returning the event'''
        completed = False
        while not completed:
            for event in self.vm.get_qmp_events(wait=True):
                if event['event'] == 'BLOCK_JOB_COMPLETED':
                    self.assert_qmp(event, 'data/device', drive)
                    self.assert_qmp_absent(event, 'data/error')
                    if check_offset:
                        self.assert_qmp(event, 'data/offset', event['data']['len'])
                    completed = True

        self.assert_no_active_block_jobs()
        return event

    def wait_ready(self, drive='drive0'):
        '''Wait until a block job BLOCK_JOB_READY event'''
        f = {'data': {'type': 'mirror', 'device': drive } }
        event = self.vm.event_wait(name='BLOCK_JOB_READY', match=f)

    def wait_ready_and_cancel(self, drive='drive0'):
        self.wait_ready(drive=drive)
        event = self.cancel_and_wait(drive=drive)
        self.assertEquals(event['event'], 'BLOCK_JOB_COMPLETED')
        self.assert_qmp(event, 'data/type', 'mirror')
        self.assert_qmp(event, 'data/offset', event['data']['len'])

    def complete_and_wait(self, drive='drive0', wait_ready=True):
        '''Complete a block job and wait for it to finish'''
        if wait_ready:
            self.wait_ready(drive=drive)

        result = self.vm.qmp('block-job-complete', device=drive)
        self.assert_qmp(result, 'return', {})

        event = self.wait_until_completed(drive=drive)
        self.assert_qmp(event, 'data/type', 'mirror')

    def pause_job(self, job_id='job0'):
        result = self.vm.qmp('block-job-pause', device=job_id)
        self.assert_qmp(result, 'return', {})

        with Timeout(1, "Timeout waiting for job to pause"):
            while True:
                result = self.vm.qmp('query-block-jobs')
                for job in result['return']:
                    if job['device'] == job_id and job['paused'] == True and job['busy'] == False:
                        return job


def notrun(reason):
    '''Skip this test suite'''
    # Each test in qemu-iotests has a number ("seq")
    seq = os.path.basename(sys.argv[0])

    open('%s/%s.notrun' % (output_dir, seq), 'wb').write(reason + '\n')
    print '%s not run: %s' % (seq, reason)
    sys.exit(0)

def verify_image_format(supported_fmts=[]):
    if supported_fmts and (imgfmt not in supported_fmts):
        notrun('not suitable for this image format: %s' % imgfmt)

def verify_platform(supported_oses=['linux']):
    if True not in [sys.platform.startswith(x) for x in supported_oses]:
        notrun('not suitable for this OS: %s' % sys.platform)

def supports_quorum():
    return 'quorum' in qemu_img_pipe('--help')

def verify_quorum():
    '''Skip test suite if quorum support is not available'''
    if not supports_quorum():
        notrun('quorum support missing')

def main(supported_fmts=[], supported_oses=['linux']):
    '''Run tests'''

    global debug

    # We are using TEST_DIR and QEMU_DEFAULT_MACHINE as proxies to
    # indicate that we're not being run via "check". There may be
    # other things set up by "check" that individual test cases rely
    # on.
    if test_dir is None or qemu_default_machine is None:
        sys.stderr.write('Please run this test via the "check" script\n')
        sys.exit(os.EX_USAGE)

    debug = '-d' in sys.argv
    verbosity = 1
    verify_image_format(supported_fmts)
    verify_platform(supported_oses)

    # We need to filter out the time taken from the output so that qemu-iotest
    # can reliably diff the results against master output.
    import StringIO
    if debug:
        output = sys.stdout
        verbosity = 2
        sys.argv.remove('-d')
    else:
        output = StringIO.StringIO()

    class MyTestRunner(unittest.TextTestRunner):
        def __init__(self, stream=output, descriptions=True, verbosity=verbosity):
            unittest.TextTestRunner.__init__(self, stream, descriptions, verbosity)

    # unittest.main() will use sys.exit() so expect a SystemExit exception
    try:
        unittest.main(testRunner=MyTestRunner)
    finally:
        if not debug:
            sys.stderr.write(re.sub(r'Ran (\d+) tests? in [\d.]+s', r'Ran \1 tests', output.getvalue()))
