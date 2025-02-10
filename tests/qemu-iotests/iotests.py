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

import argparse
import atexit
import bz2
from collections import OrderedDict
import faulthandler
import json
import logging
import os
import re
import shutil
import signal
import struct
import subprocess
import sys
import time
from typing import (Any, Callable, Dict, Iterable, Iterator,
                    List, Optional, Sequence, TextIO, Tuple, Type, TypeVar)
import unittest

from contextlib import contextmanager

from qemu.machine import qtest
from qemu.qmp.legacy import QMPMessage, QMPReturnValue, QEMUMonitorProtocol
from qemu.utils import VerboseProcessError

# Use this logger for logging messages directly from the iotests module
logger = logging.getLogger('qemu.iotests')
logger.addHandler(logging.NullHandler())

# Use this logger for messages that ought to be used for diff output.
test_logger = logging.getLogger('qemu.iotests.diff_io')


faulthandler.enable()

# This will not work if arguments contain spaces but is necessary if we
# want to support the override options that ./check supports.
qemu_img_args = [os.environ.get('QEMU_IMG_PROG', 'qemu-img')]
if os.environ.get('QEMU_IMG_OPTIONS'):
    qemu_img_args += os.environ['QEMU_IMG_OPTIONS'].strip().split(' ')

qemu_io_args = [os.environ.get('QEMU_IO_PROG', 'qemu-io')]
if os.environ.get('QEMU_IO_OPTIONS'):
    qemu_io_args += os.environ['QEMU_IO_OPTIONS'].strip().split(' ')

qemu_io_args_no_fmt = [os.environ.get('QEMU_IO_PROG', 'qemu-io')]
if os.environ.get('QEMU_IO_OPTIONS_NO_FMT'):
    qemu_io_args_no_fmt += \
        os.environ['QEMU_IO_OPTIONS_NO_FMT'].strip().split(' ')

qemu_nbd_prog = os.environ.get('QEMU_NBD_PROG', 'qemu-nbd')
qemu_nbd_args = [qemu_nbd_prog]
if os.environ.get('QEMU_NBD_OPTIONS'):
    qemu_nbd_args += os.environ['QEMU_NBD_OPTIONS'].strip().split(' ')

qemu_prog = os.environ.get('QEMU_PROG', 'qemu')
qemu_opts = os.environ.get('QEMU_OPTIONS', '').strip().split(' ')

qsd_prog = os.environ.get('QSD_PROG', 'qemu-storage-daemon')

gdb_qemu_env = os.environ.get('GDB_OPTIONS')
qemu_gdb = []
if gdb_qemu_env:
    qemu_gdb = ['gdbserver'] + gdb_qemu_env.strip().split(' ')

qemu_print = os.environ.get('PRINT_QEMU', False)

imgfmt = os.environ.get('IMGFMT', 'raw')
imgproto = os.environ.get('IMGPROTO', 'file')

try:
    test_dir = os.environ['TEST_DIR']
    sock_dir = os.environ['SOCK_DIR']
    cachemode = os.environ['CACHEMODE']
    aiomode = os.environ['AIOMODE']
    qemu_default_machine = os.environ['QEMU_DEFAULT_MACHINE']
except KeyError:
    # We are using these variables as proxies to indicate that we're
    # not being run via "check". There may be other things set up by
    # "check" that individual test cases rely on.
    sys.stderr.write('Please run this test via the "check" script\n')
    sys.exit(os.EX_USAGE)

qemu_valgrind = []
if os.environ.get('VALGRIND_QEMU') == "y" and \
    os.environ.get('NO_VALGRIND') != "y":
    valgrind_logfile = "--log-file=" + test_dir
    # %p allows to put the valgrind process PID, since
    # we don't know it a priori (subprocess.Popen is
    # not yet invoked)
    valgrind_logfile += "/%p.valgrind"

    qemu_valgrind = ['valgrind', valgrind_logfile, '--error-exitcode=99']

luks_default_secret_object = 'secret,id=keysec0,data=' + \
                             os.environ.get('IMGKEYSECRET', '')
luks_default_key_secret_opt = 'key-secret=keysec0'

sample_img_dir = os.environ['SAMPLE_IMG_DIR']


@contextmanager
def change_log_level(
        logger_name: str, level: int = logging.CRITICAL) -> Iterator[None]:
    """
    Utility function for temporarily changing the log level of a logger.

    This can be used to silence errors that are expected or uninteresting.
    """
    _logger = logging.getLogger(logger_name)
    current_level = _logger.level
    _logger.setLevel(level)

    try:
        yield
    finally:
        _logger.setLevel(current_level)


def unarchive_sample_image(sample, fname):
    sample_fname = os.path.join(sample_img_dir, sample + '.bz2')
    with bz2.open(sample_fname) as f_in, open(fname, 'wb') as f_out:
        shutil.copyfileobj(f_in, f_out)


def qemu_tool_popen(args: Sequence[str],
                    connect_stderr: bool = True) -> 'subprocess.Popen[str]':
    stderr = subprocess.STDOUT if connect_stderr else None
    # pylint: disable=consider-using-with
    return subprocess.Popen(args,
                            stdout=subprocess.PIPE,
                            stderr=stderr,
                            universal_newlines=True)


def qemu_tool_pipe_and_status(tool: str, args: Sequence[str],
                              connect_stderr: bool = True,
                              drop_successful_output: bool = False) \
        -> Tuple[str, int]:
    """
    Run a tool and return both its output and its exit code
    """
    with qemu_tool_popen(args, connect_stderr) as subp:
        output = subp.communicate()[0]
        if subp.returncode < 0:
            cmd = ' '.join(args)
            sys.stderr.write(f'{tool} received signal \
                               {-subp.returncode}: {cmd}\n')
        if drop_successful_output and subp.returncode == 0:
            output = ''
        return (output, subp.returncode)

def qemu_img_create_prepare_args(args: List[str]) -> List[str]:
    if not args or args[0] != 'create':
        return list(args)
    args = args[1:]

    p = argparse.ArgumentParser(allow_abbrev=False)
    # -o option may be specified several times
    p.add_argument('-o', action='append', default=[])
    p.add_argument('-f')
    parsed, remaining = p.parse_known_args(args)

    opts_list = parsed.o

    result = ['create']
    if parsed.f is not None:
        result += ['-f', parsed.f]

    # IMGOPTS most probably contain options specific for the selected format,
    # like extended_l2 or compression_type for qcow2. Test may want to create
    # additional images in other formats that doesn't support these options.
    # So, use IMGOPTS only for images created in imgfmt format.
    imgopts = os.environ.get('IMGOPTS')
    if imgopts and parsed.f == imgfmt:
        opts_list.insert(0, imgopts)

    # default luks support
    if parsed.f == 'luks' and \
            all('key-secret' not in opts for opts in opts_list):
        result += ['--object', luks_default_secret_object]
        opts_list.append(luks_default_key_secret_opt)

    for opts in opts_list:
        result += ['-o', opts]

    result += remaining

    return result


def qemu_tool(*args: str, check: bool = True, combine_stdio: bool = True
              ) -> 'subprocess.CompletedProcess[str]':
    """
    Run a qemu tool and return its status code and console output.

    :param args: full command line to run.
    :param check: Enforce a return code of zero.
    :param combine_stdio: set to False to keep stdout/stderr separated.

    :raise VerboseProcessError:
        When the return code is negative, or on any non-zero exit code
        when 'check=True' was provided (the default). This exception has
        'stdout', 'stderr', and 'returncode' properties that may be
        inspected to show greater detail. If this exception is not
        handled, the command-line, return code, and all console output
        will be included at the bottom of the stack trace.

    :return:
        a CompletedProcess. This object has args, returncode, and stdout
        properties. If streams are not combined, it will also have a
        stderr property.
    """
    subp = subprocess.run(
        args,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT if combine_stdio else subprocess.PIPE,
        universal_newlines=True,
        check=False
    )

    if check and subp.returncode or (subp.returncode < 0):
        raise VerboseProcessError(
            subp.returncode, args,
            output=subp.stdout,
            stderr=subp.stderr,
        )

    return subp


def qemu_img(*args: str, check: bool = True, combine_stdio: bool = True
             ) -> 'subprocess.CompletedProcess[str]':
    """
    Run QEMU_IMG_PROG and return its status code and console output.

    This function always prepends QEMU_IMG_OPTIONS and may further alter
    the args for 'create' commands.

    See `qemu_tool()` for greater detail.
    """
    full_args = qemu_img_args + qemu_img_create_prepare_args(list(args))
    return qemu_tool(*full_args, check=check, combine_stdio=combine_stdio)


def ordered_qmp(qmsg, conv_keys=True):
    # Dictionaries are not ordered prior to 3.6, therefore:
    if isinstance(qmsg, list):
        return [ordered_qmp(atom) for atom in qmsg]
    if isinstance(qmsg, dict):
        od = OrderedDict()
        for k, v in sorted(qmsg.items()):
            if conv_keys:
                k = k.replace('_', '-')
            od[k] = ordered_qmp(v, conv_keys=False)
        return od
    return qmsg

def qemu_img_create(*args: str) -> 'subprocess.CompletedProcess[str]':
    return qemu_img('create', *args)

def qemu_img_json(*args: str) -> Any:
    """
    Run qemu-img and return its output as deserialized JSON.

    :raise CalledProcessError:
        When qemu-img crashes, or returns a non-zero exit code without
        producing a valid JSON document to stdout.
    :raise JSONDecoderError:
        When qemu-img returns 0, but failed to produce a valid JSON document.

    :return: A deserialized JSON object; probably a dict[str, Any].
    """
    try:
        res = qemu_img(*args, combine_stdio=False)
    except subprocess.CalledProcessError as exc:
        # Terminated due to signal. Don't bother.
        if exc.returncode < 0:
            raise

        # Commands like 'check' can return failure (exit codes 2 and 3)
        # to indicate command completion, but with errors found. For
        # multi-command flexibility, ignore the exact error codes and
        # *try* to load JSON.
        try:
            return json.loads(exc.stdout)
        except json.JSONDecodeError:
            # Nope. This thing is toast. Raise the /process/ error.
            pass
        raise

    return json.loads(res.stdout)

def qemu_img_measure(*args: str) -> Any:
    return qemu_img_json("measure", "--output", "json", *args)

def qemu_img_check(*args: str) -> Any:
    return qemu_img_json("check", "--output", "json", *args)

def qemu_img_info(*args: str) -> Any:
    return qemu_img_json('info', "--output", "json", *args)

def qemu_img_map(*args: str) -> Any:
    return qemu_img_json('map', "--output", "json", *args)

def qemu_img_log(*args: str, check: bool = True
                 ) -> 'subprocess.CompletedProcess[str]':
    result = qemu_img(*args, check=check)
    log(result.stdout, filters=[filter_testfiles])
    return result

def img_info_log(filename: str, filter_path: Optional[str] = None,
                 use_image_opts: bool = False, extra_args: Sequence[str] = (),
                 check: bool = True, drop_child_info: bool = True,
                 ) -> None:
    args = ['info']
    if use_image_opts:
        args.append('--image-opts')
    else:
        args += ['-f', imgfmt]
    args += extra_args
    args.append(filename)

    output = qemu_img(*args, check=check).stdout
    if not filter_path:
        filter_path = filename
    log(filter_img_info(output, filter_path, drop_child_info))

def qemu_io_wrap_args(args: Sequence[str]) -> List[str]:
    if '-f' in args or '--image-opts' in args:
        return qemu_io_args_no_fmt + list(args)
    else:
        return qemu_io_args + list(args)

def qemu_io_popen(*args):
    return qemu_tool_popen(qemu_io_wrap_args(args))

def qemu_io(*args: str, check: bool = True, combine_stdio: bool = True
            ) -> 'subprocess.CompletedProcess[str]':
    """
    Run QEMU_IO_PROG and return the status code and console output.

    This function always prepends either QEMU_IO_OPTIONS or
    QEMU_IO_OPTIONS_NO_FMT.
    """
    return qemu_tool(*qemu_io_wrap_args(args),
                     check=check, combine_stdio=combine_stdio)

def qemu_io_log(*args: str, check: bool = True
                ) -> 'subprocess.CompletedProcess[str]':
    result = qemu_io(*args, check=check)
    log(result.stdout, filters=[filter_testfiles, filter_qemu_io])
    return result

class QemuIoInteractive:
    def __init__(self, *args):
        self.args = qemu_io_wrap_args(args)
        # We need to keep the Popen objext around, and not
        # close it immediately. Therefore, disable the pylint check:
        # pylint: disable=consider-using-with
        self._p = subprocess.Popen(self.args, stdin=subprocess.PIPE,
                                   stdout=subprocess.PIPE,
                                   stderr=subprocess.STDOUT,
                                   universal_newlines=True)
        out = self._p.stdout.read(9)
        if out != 'qemu-io> ':
            # Most probably qemu-io just failed to start.
            # Let's collect the whole output and exit.
            out += self._p.stdout.read()
            self._p.wait(timeout=1)
            raise ValueError(out)

    def close(self):
        self._p.communicate('q\n')

    def _read_output(self):
        pattern = 'qemu-io> '
        n = len(pattern)
        pos = 0
        s = []
        while pos != n:
            c = self._p.stdout.read(1)
            # check unexpected EOF
            assert c != ''
            s.append(c)
            if c == pattern[pos]:
                pos += 1
            else:
                pos = 0

        return ''.join(s[:-n])

    def cmd(self, cmd):
        # quit command is in close(), '\n' is added automatically
        assert '\n' not in cmd
        cmd = cmd.strip()
        assert cmd not in ('q', 'quit')
        self._p.stdin.write(cmd + '\n')
        self._p.stdin.flush()
        return self._read_output()


class QemuStorageDaemon:
    _qmp: Optional[QEMUMonitorProtocol] = None
    _qmpsock: Optional[str] = None
    # Python < 3.8 would complain if this type were not a string literal
    # (importing `annotations` from `__future__` would work; but not on <= 3.6)
    _p: 'Optional[subprocess.Popen[bytes]]' = None

    def __init__(self, *args: str, instance_id: str = 'a', qmp: bool = False):
        assert '--pidfile' not in args
        self.pidfile = os.path.join(test_dir, f'qsd-{instance_id}-pid')
        all_args = [qsd_prog] + list(args) + ['--pidfile', self.pidfile]

        if qmp:
            self._qmpsock = os.path.join(sock_dir, f'qsd-{instance_id}.sock')
            all_args += ['--chardev',
                         f'socket,id=qmp-sock,path={self._qmpsock}',
                         '--monitor', 'qmp-sock']

            self._qmp = QEMUMonitorProtocol(self._qmpsock, server=True)

        # Cannot use with here, we want the subprocess to stay around
        # pylint: disable=consider-using-with
        self._p = subprocess.Popen(all_args)
        if self._qmp is not None:
            self._qmp.accept()
        while not os.path.exists(self.pidfile):
            if self._p.poll() is not None:
                cmd = ' '.join(all_args)
                raise RuntimeError(
                    'qemu-storage-daemon terminated with exit code ' +
                    f'{self._p.returncode}: {cmd}')

            time.sleep(0.01)

        with open(self.pidfile, encoding='utf-8') as f:
            self._pid = int(f.read().strip())

        assert self._pid == self._p.pid

    def qmp(self, cmd: str, args: Optional[Dict[str, object]] = None) \
            -> QMPMessage:
        assert self._qmp is not None
        return self._qmp.cmd_raw(cmd, args)

    def get_qmp(self) -> QEMUMonitorProtocol:
        assert self._qmp is not None
        return self._qmp

    def cmd(self, cmd: str, args: Optional[Dict[str, object]] = None) \
            -> QMPReturnValue:
        assert self._qmp is not None
        return self._qmp.cmd(cmd, **(args or {}))

    def stop(self, kill_signal=15):
        self._p.send_signal(kill_signal)
        self._p.wait()
        self._p = None

        if self._qmp:
            self._qmp.close()

        if self._qmpsock is not None:
            try:
                os.remove(self._qmpsock)
            except OSError:
                pass
        try:
            os.remove(self.pidfile)
        except OSError:
            pass

    def __del__(self):
        if self._p is not None:
            self.stop(kill_signal=9)


def qemu_nbd(*args):
    '''Run qemu-nbd in daemon mode and return the parent's exit code'''
    return subprocess.call(qemu_nbd_args + ['--fork'] + list(args))

def qemu_nbd_early_pipe(*args: str) -> Tuple[int, str]:
    '''Run qemu-nbd in daemon mode and return both the parent's exit code
       and its output in case of an error'''
    full_args = qemu_nbd_args + ['--fork'] + list(args)
    output, returncode = qemu_tool_pipe_and_status('qemu-nbd', full_args,
                                                   connect_stderr=False)
    return returncode, output if returncode else ''

def qemu_nbd_list_log(*args: str) -> str:
    '''Run qemu-nbd to list remote exports'''
    full_args = [qemu_nbd_prog, '-L'] + list(args)
    output, _ = qemu_tool_pipe_and_status('qemu-nbd', full_args)
    log(output, filters=[filter_testfiles, filter_nbd_exports])
    return output

@contextmanager
def qemu_nbd_popen(*args):
    '''Context manager running qemu-nbd within the context'''
    pid_file = file_path("qemu_nbd_popen-nbd-pid-file")

    assert not os.path.exists(pid_file)

    cmd = list(qemu_nbd_args)
    cmd.extend(('--persistent', '--pid-file', pid_file))
    cmd.extend(args)

    log('Start NBD server')
    with subprocess.Popen(cmd) as p:
        try:
            while not os.path.exists(pid_file):
                if p.poll() is not None:
                    raise RuntimeError(
                        "qemu-nbd terminated with exit code {}: {}"
                        .format(p.returncode, ' '.join(cmd)))

                time.sleep(0.01)
            yield
        finally:
            if os.path.exists(pid_file):
                os.remove(pid_file)
            log('Kill NBD server')
            p.kill()
            p.wait()

def compare_images(img1: str, img2: str,
                   fmt1: str = imgfmt, fmt2: str = imgfmt) -> bool:
    """
    Compare two images with QEMU_IMG; return True if they are identical.

    :raise CalledProcessError:
        when qemu-img crashes or returns a status code of anything other
        than 0 (identical) or 1 (different).
    """
    try:
        qemu_img('compare', '-f', fmt1, '-F', fmt2, img1, img2)
        return True
    except subprocess.CalledProcessError as exc:
        if exc.returncode == 1:
            return False
        raise

def create_image(name, size):
    '''Create a fully-allocated raw image with sector markers'''
    with open(name, 'wb') as file:
        i = 0
        while i < size:
            sector = struct.pack('>l504xl', i // 512, i // 512)
            file.write(sector)
            i = i + 512

def image_size(img: str) -> int:
    """Return image's virtual size"""
    value = qemu_img_info('-f', imgfmt, img)['virtual-size']
    if not isinstance(value, int):
        type_name = type(value).__name__
        raise TypeError("Expected 'int' for 'virtual-size', "
                        f"got '{value}' of type '{type_name}'")
    return value

def is_str(val):
    return isinstance(val, str)

test_dir_re = re.compile(r"%s" % test_dir)
def filter_test_dir(msg):
    return test_dir_re.sub("TEST_DIR", msg)

win32_re = re.compile(r"\r")
def filter_win32(msg):
    return win32_re.sub("", msg)

qemu_io_re = re.compile(r"[0-9]* ops; [0-9\/:. sec]* "
                        r"\([0-9\/.inf]* [EPTGMKiBbytes]*\/sec "
                        r"and [0-9\/.inf]* ops\/sec\)")
def filter_qemu_io(msg):
    msg = filter_win32(msg)
    return qemu_io_re.sub("X ops; XX:XX:XX.X "
                          "(XXX YYY/sec and XXX ops/sec)", msg)

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

def filter_qmp(qmsg, filter_fn):
    '''Given a string filter, filter a QMP object's values.
    filter_fn takes a (key, value) pair.'''
    # Iterate through either lists or dicts;
    if isinstance(qmsg, list):
        items = enumerate(qmsg)
    elif isinstance(qmsg, dict):
        items = qmsg.items()
    else:
        return filter_fn(None, qmsg)

    for k, v in items:
        if isinstance(v, (dict, list)):
            qmsg[k] = filter_qmp(v, filter_fn)
        else:
            qmsg[k] = filter_fn(k, v)
    return qmsg

def filter_testfiles(msg):
    pref1 = os.path.join(test_dir, "%s-" % (os.getpid()))
    pref2 = os.path.join(sock_dir, "%s-" % (os.getpid()))
    return msg.replace(pref1, 'TEST_DIR/PID-').replace(pref2, 'SOCK_DIR/PID-')

def filter_qmp_testfiles(qmsg):
    def _filter(_key, value):
        if is_str(value):
            return filter_testfiles(value)
        return value
    return filter_qmp(qmsg, _filter)

def filter_virtio_scsi(output: str) -> str:
    return re.sub(r'(virtio-scsi)-(ccw|pci)', r'\1', output)

def filter_qmp_virtio_scsi(qmsg):
    def _filter(_key, value):
        if is_str(value):
            return filter_virtio_scsi(value)
        return value
    return filter_qmp(qmsg, _filter)

def filter_generated_node_ids(msg):
    return re.sub("#block[0-9]+", "NODE_NAME", msg)

def filter_qmp_generated_node_ids(qmsg):
    def _filter(_key, value):
        if is_str(value):
            return filter_generated_node_ids(value)
        return value
    return filter_qmp(qmsg, _filter)

def filter_img_info(output: str, filename: str,
                    drop_child_info: bool = True) -> str:
    lines = []
    drop_indented = False
    for line in output.split('\n'):
        if 'disk size' in line or 'actual-size' in line:
            continue

        # Drop child node info
        if drop_indented:
            if line.startswith(' '):
                continue
            drop_indented = False
        if drop_child_info and "Child node '/" in line:
            drop_indented = True
            continue

        line = line.replace(filename, 'TEST_IMG')
        line = filter_testfiles(line)
        line = line.replace(imgfmt, 'IMGFMT')
        line = re.sub('iters: [0-9]+', 'iters: XXX', line)
        line = re.sub('uuid: [-a-f0-9]+',
                      'uuid: XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX',
                      line)
        line = re.sub('cid: [0-9]+', 'cid: XXXXXXXXXX', line)
        line = re.sub('(compression type: )(zlib|zstd)', r'\1COMPRESSION_TYPE',
                      line)
        lines.append(line)
    return '\n'.join(lines)

def filter_imgfmt(msg):
    return msg.replace(imgfmt, 'IMGFMT')

def filter_qmp_imgfmt(qmsg):
    def _filter(_key, value):
        if is_str(value):
            return filter_imgfmt(value)
        return value
    return filter_qmp(qmsg, _filter)

def filter_nbd_exports(output: str) -> str:
    return re.sub(r'((min|opt|max) block): [0-9]+', r'\1: XXX', output)


Msg = TypeVar('Msg', Dict[str, Any], List[Any], str)

def log(msg: Msg,
        filters: Iterable[Callable[[Msg], Msg]] = (),
        indent: Optional[int] = None) -> None:
    """
    Logs either a string message or a JSON serializable message (like QMP).
    If indent is provided, JSON serializable messages are pretty-printed.
    """
    for flt in filters:
        msg = flt(msg)
    if isinstance(msg, (dict, list)):
        # Don't sort if it's already sorted
        do_sort = not isinstance(msg, OrderedDict)
        test_logger.info(json.dumps(msg, sort_keys=do_sort, indent=indent))
    else:
        test_logger.info(msg)

class Timeout:
    def __init__(self, seconds, errmsg="Timeout"):
        self.seconds = seconds
        self.errmsg = errmsg
    def __enter__(self):
        if qemu_gdb or qemu_valgrind:
            return self
        signal.signal(signal.SIGALRM, self.timeout)
        signal.setitimer(signal.ITIMER_REAL, self.seconds)
        return self
    def __exit__(self, exc_type, value, traceback):
        if qemu_gdb or qemu_valgrind:
            return False
        signal.setitimer(signal.ITIMER_REAL, 0)
        return False
    def timeout(self, signum, frame):
        raise TimeoutError(self.errmsg)

def file_pattern(name):
    return "{0}-{1}".format(os.getpid(), name)

class FilePath:
    """
    Context manager generating multiple file names. The generated files are
    removed when exiting the context.

    Example usage:

        with FilePath('a.img', 'b.img') as (img_a, img_b):
            # Use img_a and img_b here...

        # a.img and b.img are automatically removed here.

    By default images are created in iotests.test_dir. To create sockets use
    iotests.sock_dir:

       with FilePath('a.sock', base_dir=iotests.sock_dir) as sock:

    For convenience, calling with one argument yields a single file instead of
    a tuple with one item.

    """
    def __init__(self, *names, base_dir=test_dir):
        self.paths = [os.path.join(base_dir, file_pattern(name))
                      for name in names]

    def __enter__(self):
        if len(self.paths) == 1:
            return self.paths[0]
        else:
            return self.paths

    def __exit__(self, exc_type, exc_val, exc_tb):
        for path in self.paths:
            try:
                os.remove(path)
            except OSError:
                pass
        return False


def try_remove(img):
    try:
        os.remove(img)
    except OSError:
        pass

def file_path_remover():
    for path in reversed(file_path_remover.paths):
        try_remove(path)


def file_path(*names, base_dir=test_dir):
    ''' Another way to get auto-generated filename that cleans itself up.

    Use is as simple as:

    img_a, img_b = file_path('a.img', 'b.img')
    sock = file_path('socket')
    '''

    if not hasattr(file_path_remover, 'paths'):
        file_path_remover.paths = []
        atexit.register(file_path_remover)

    paths = []
    for name in names:
        filename = file_pattern(name)
        path = os.path.join(base_dir, filename)
        file_path_remover.paths.append(path)
        paths.append(path)

    return paths[0] if len(paths) == 1 else paths

def remote_filename(path):
    if imgproto == 'file':
        return path
    elif imgproto == 'ssh':
        return "ssh://%s@127.0.0.1:22%s" % (os.environ.get('USER'), path)
    else:
        raise ValueError("Protocol %s not supported" % (imgproto))

class VM(qtest.QEMUQtestMachine):
    '''A QEMU VM'''

    def __init__(self, path_suffix=''):
        name = "qemu%s-%d" % (path_suffix, os.getpid())
        timer = 15.0 if not (qemu_gdb or qemu_valgrind) else None
        if qemu_gdb and qemu_valgrind:
            sys.stderr.write('gdb and valgrind are mutually exclusive\n')
            sys.exit(1)
        wrapper = qemu_gdb if qemu_gdb else qemu_valgrind
        super().__init__(qemu_prog, qemu_opts, wrapper=wrapper,
                         name=name,
                         base_temp_dir=test_dir,
                         qmp_timer=timer)
        self._num_drives = 0

    def _post_shutdown(self) -> None:
        super()._post_shutdown()
        if not qemu_valgrind or not self._popen:
            return
        valgrind_filename = f"{test_dir}/{self._popen.pid}.valgrind"
        if self.exitcode() == 99:
            with open(valgrind_filename, encoding='utf-8') as f:
                print(f.read())
        else:
            os.remove(valgrind_filename)

    def _pre_launch(self) -> None:
        super()._pre_launch()
        if qemu_print:
            # set QEMU binary output to stdout
            self._close_qemu_log_file()

    def add_object(self, opts):
        self._args.append('-object')
        self._args.append(opts)
        return self

    def add_device(self, opts):
        self._args.append('-device')
        self._args.append(opts)
        return self

    def add_drive_raw(self, opts):
        self._args.append('-drive')
        self._args.append(opts)
        return self

    def add_drive(self, path, opts='', interface='virtio', img_format=imgfmt):
        '''Add a virtio-blk drive to the VM'''
        options = ['if=%s' % interface,
                   'id=drive%d' % self._num_drives]

        if path is not None:
            options.append('file=%s' % path)
            options.append('format=%s' % img_format)
            options.append('cache=%s' % cachemode)
            options.append('aio=%s' % aiomode)

        if opts:
            options.append(opts)

        if img_format == 'luks' and 'key-secret' not in opts:
            # default luks support
            if luks_default_secret_object not in self._args:
                self.add_object(luks_default_secret_object)

            options.append(luks_default_key_secret_opt)

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

    def hmp(self, command_line: str, use_log: bool = False) -> QMPMessage:
        cmd = 'human-monitor-command'
        kwargs: Dict[str, Any] = {'command-line': command_line}
        if use_log:
            return self.qmp_log(cmd, **kwargs)
        else:
            return self.qmp(cmd, **kwargs)

    def pause_drive(self, drive: str, event: Optional[str] = None) -> None:
        """Pause drive r/w operations"""
        if not event:
            self.pause_drive(drive, "read_aio")
            self.pause_drive(drive, "write_aio")
            return
        self.hmp(f'qemu-io {drive} "break {event} bp_{drive}"')

    def resume_drive(self, drive: str) -> None:
        """Resume drive r/w operations"""
        self.hmp(f'qemu-io {drive} "remove_break bp_{drive}"')

    def hmp_qemu_io(self, drive: str, cmd: str,
                    use_log: bool = False, qdev: bool = False) -> QMPMessage:
        """Write to a given drive using an HMP command"""
        d = '-d ' if qdev else ''
        return self.hmp(f'qemu-io {d}{drive} "{cmd}"', use_log=use_log)

    def flatten_qmp_object(self, obj, output=None, basestr=''):
        if output is None:
            output = {}
        if isinstance(obj, list):
            for i, item in enumerate(obj):
                self.flatten_qmp_object(item, output, basestr + str(i) + '.')
        elif isinstance(obj, dict):
            for key in obj:
                self.flatten_qmp_object(obj[key], output, basestr + key + '.')
        else:
            output[basestr[:-1]] = obj # Strip trailing '.'
        return output

    def qmp_to_opts(self, obj):
        obj = self.flatten_qmp_object(obj)
        output_list = []
        for key in obj:
            output_list += [key + '=' + obj[key]]
        return ','.join(output_list)

    def get_qmp_events_filtered(self, wait=60.0):
        result = []
        for ev in self.get_qmp_events(wait=wait):
            result.append(filter_qmp_event(ev))
        return result

    def qmp_log(self, cmd, filters=(), indent=None, **kwargs):
        full_cmd = OrderedDict((
            ("execute", cmd),
            ("arguments", ordered_qmp(kwargs))
        ))
        log(full_cmd, filters, indent=indent)
        result = self.qmp(cmd, **kwargs)
        log(result, filters, indent=indent)
        return result

    # Returns None on success, and an error string on failure
    def run_job(self, job: str, auto_finalize: bool = True,
                auto_dismiss: bool = False,
                pre_finalize: Optional[Callable[[], None]] = None,
                cancel: bool = False, wait: float = 60.0,
                filters: Iterable[Callable[[Any], Any]] = (),
                ) -> Optional[str]:
        """
        run_job moves a job from creation through to dismissal.

        :param job: String. ID of recently-launched job
        :param auto_finalize: Bool. True if the job was launched with
                              auto_finalize. Defaults to True.
        :param auto_dismiss: Bool. True if the job was launched with
                             auto_dismiss=True. Defaults to False.
        :param pre_finalize: Callback. A callable that takes no arguments to be
                             invoked prior to issuing job-finalize, if any.
        :param cancel: Bool. When true, cancels the job after the pre_finalize
                       callback.
        :param wait: Float. Timeout value specifying how long to wait for any
                     event, in seconds. Defaults to 60.0.
        """
        match_device = {'data': {'device': job}}
        match_id = {'data': {'id': job}}
        events = [
            ('BLOCK_JOB_COMPLETED', match_device),
            ('BLOCK_JOB_CANCELLED', match_device),
            ('BLOCK_JOB_ERROR', match_device),
            ('BLOCK_JOB_READY', match_device),
            ('BLOCK_JOB_PENDING', match_id),
            ('JOB_STATUS_CHANGE', match_id)
        ]
        error = None
        while True:
            ev = filter_qmp_event(self.events_wait(events, timeout=wait))
            if ev['event'] != 'JOB_STATUS_CHANGE':
                log(ev, filters=filters)
                continue
            status = ev['data']['status']
            if status == 'aborting':
                result = self.qmp('query-jobs')
                for j in result['return']:
                    if j['id'] == job:
                        error = j['error']
                        log('Job failed: %s' % (j['error']), filters=filters)
            elif status == 'ready':
                self.qmp_log('job-complete', id=job, filters=filters)
            elif status == 'pending' and not auto_finalize:
                if pre_finalize:
                    pre_finalize()
                if cancel:
                    self.qmp_log('job-cancel', id=job, filters=filters)
                else:
                    self.qmp_log('job-finalize', id=job, filters=filters)
            elif status == 'concluded' and not auto_dismiss:
                self.qmp_log('job-dismiss', id=job, filters=filters)
            elif status == 'null':
                return error

    # Returns None on success, and an error string on failure
    def blockdev_create(self, options, job_id='job0', filters=None):
        if filters is None:
            filters = [filter_qmp_testfiles]
        result = self.qmp_log('blockdev-create', filters=filters,
                              job_id=job_id, options=options)

        if 'return' in result:
            assert result['return'] == {}
            job_result = self.run_job(job_id, filters=filters)
        else:
            job_result = result['error']

        log("")
        return job_result

    def enable_migration_events(self, name):
        log('Enabling migration QMP events on %s...' % name)
        log(self.qmp('migrate-set-capabilities', capabilities=[
            {
                'capability': 'events',
                'state': True
            }
        ]))

    def wait_migration(self, expect_runstate: Optional[str]) -> bool:
        while True:
            event = self.event_wait('MIGRATION')
            # We use the default timeout, and with a timeout, event_wait()
            # never returns None
            assert event

            log(event, filters=[filter_qmp_event])
            if event['data']['status'] in ('completed', 'failed'):
                break

        if event['data']['status'] == 'completed':
            # The event may occur in finish-migrate, so wait for the expected
            # post-migration runstate
            runstate = None
            while runstate != expect_runstate:
                runstate = self.qmp('query-status')['return']['status']
            return True
        else:
            return False

    def node_info(self, node_name):
        nodes = self.qmp('query-named-block-nodes')
        for x in nodes['return']:
            if x['node-name'] == node_name:
                return x
        return None

    def query_bitmaps(self):
        res = self.qmp("query-named-block-nodes")
        return {device['node-name']: device['dirty-bitmaps']
                for device in res['return'] if 'dirty-bitmaps' in device}

    def get_bitmap(self, node_name, bitmap_name, recording=None, bitmaps=None):
        """
        get a specific bitmap from the object returned by query_bitmaps.
        :param recording: If specified, filter results by the specified value.
        :param bitmaps: If specified, use it instead of call query_bitmaps()
        """
        if bitmaps is None:
            bitmaps = self.query_bitmaps()

        for bitmap in bitmaps[node_name]:
            if bitmap.get('name', '') == bitmap_name:
                if recording is None or bitmap.get('recording') == recording:
                    return bitmap
        return None

    def check_bitmap_status(self, node_name, bitmap_name, fields):
        ret = self.get_bitmap(node_name, bitmap_name)

        return fields.items() <= ret.items()

    def assert_block_path(self, root, path, expected_node, graph=None):
        """
        Check whether the node under the given path in the block graph
        is @expected_node.

        @root is the node name of the node where the @path is rooted.

        @path is a string that consists of child names separated by
        slashes.  It must begin with a slash.

        Examples for @root + @path:
          - root="qcow2-node", path="/backing/file"
          - root="quorum-node", path="/children.2/file"

        Hypothetically, @path could be empty, in which case it would
        point to @root.  However, in practice this case is not useful
        and hence not allowed.

        @expected_node may be None.  (All elements of the path but the
        leaf must still exist.)

        @graph may be None or the result of an x-debug-query-block-graph
        call that has already been performed.
        """
        if graph is None:
            graph = self.qmp('x-debug-query-block-graph')['return']

        iter_path = iter(path.split('/'))

        # Must start with a /
        assert next(iter_path) == ''

        node = next((node for node in graph['nodes'] if node['name'] == root),
                    None)

        # An empty @path is not allowed, so the root node must be present
        assert node is not None, 'Root node %s not found' % root

        for child_name in iter_path:
            assert node is not None, 'Cannot follow path %s%s' % (root, path)

            try:
                node_id = next(edge['child'] for edge in graph['edges']
                               if (edge['parent'] == node['id'] and
                                   edge['name'] == child_name))

                node = next(node for node in graph['nodes']
                            if node['id'] == node_id)

            except StopIteration:
                node = None

        if node is None:
            assert expected_node is None, \
                   'No node found under %s (but expected %s)' % \
                   (path, expected_node)
        else:
            assert node['name'] == expected_node, \
                   'Found node %s under %s (but expected %s)' % \
                   (node['name'], path, expected_node)

index_re = re.compile(r'([^\[]+)\[([^\]]+)\]')

class QMPTestCase(unittest.TestCase):
    '''Abstract base class for QMP test cases'''

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        # Many users of this class set a VM property we rely on heavily
        # in the methods below.
        self.vm = None

    def dictpath(self, d, path):
        '''Traverse a path in a nested dict'''
        for component in path.split('/'):
            m = index_re.match(component)
            if m:
                component, idx = m.groups()
                idx = int(idx)

            if not isinstance(d, dict) or component not in d:
                self.fail(f'failed path traversal for "{path}" in "{d}"')
            d = d[component]

            if m:
                if not isinstance(d, list):
                    self.fail(f'path component "{component}" in "{path}" '
                              f'is not a list in "{d}"')
                try:
                    d = d[idx]
                except IndexError:
                    self.fail(f'invalid index "{idx}" in path "{path}" '
                              f'in "{d}"')
        return d

    def assert_qmp_absent(self, d, path):
        try:
            result = self.dictpath(d, path)
        except AssertionError:
            return
        self.fail('path "%s" has value "%s"' % (path, str(result)))

    def assert_qmp(self, d, path, value):
        '''Assert that the value for a specific path in a QMP dict
           matches.  When given a list of values, assert that any of
           them matches.'''

        result = self.dictpath(d, path)

        # [] makes no sense as a list of valid values, so treat it as
        # an actual single value.
        if isinstance(value, list) and value != []:
            for v in value:
                if result == v:
                    return
            self.fail('no match for "%s" in %s' % (str(result), str(value)))
        else:
            self.assertEqual(result, value,
                             '"%s" is "%s", expected "%s"'
                             % (path, str(result), str(value)))

    def assert_no_active_block_jobs(self):
        result = self.vm.qmp('query-block-jobs')
        self.assert_qmp(result, 'return', [])

    def assert_has_block_node(self, node_name=None, file_name=None):
        """Issue a query-named-block-nodes and assert node_name and/or
        file_name is present in the result"""
        def check_equal_or_none(a, b):
            return a is None or b is None or a == b
        assert node_name or file_name
        result = self.vm.qmp('query-named-block-nodes')
        for x in result["return"]:
            if check_equal_or_none(x.get("node-name"), node_name) and \
                    check_equal_or_none(x.get("file"), file_name):
                return
        self.fail("Cannot find %s %s in result:\n%s" %
                  (node_name, file_name, result))

    def assert_json_filename_equal(self, json_filename, reference):
        '''Asserts that the given filename is a json: filename and that its
           content is equal to the given reference object'''
        self.assertEqual(json_filename[:5], 'json:')
        self.assertEqual(
            self.vm.flatten_qmp_object(json.loads(json_filename[5:])),
            self.vm.flatten_qmp_object(reference)
        )

    def cancel_and_wait(self, drive='drive0', force=False,
                        resume=False, wait=60.0):
        '''Cancel a block job and wait for it to finish, returning the event'''
        self.vm.cmd('block-job-cancel', device=drive, force=force)

        if resume:
            self.vm.resume_drive(drive)

        cancelled = False
        result = None
        while not cancelled:
            for event in self.vm.get_qmp_events(wait=wait):
                if event['event'] == 'BLOCK_JOB_COMPLETED' or \
                   event['event'] == 'BLOCK_JOB_CANCELLED':
                    self.assert_qmp(event, 'data/device', drive)
                    result = event
                    cancelled = True
                elif event['event'] == 'JOB_STATUS_CHANGE':
                    self.assert_qmp(event, 'data/id', drive)


        self.assert_no_active_block_jobs()
        return result

    def wait_until_completed(self, drive='drive0', check_offset=True,
                             wait=60.0, error=None):
        '''Wait for a block job to finish, returning the event'''
        while True:
            for event in self.vm.get_qmp_events(wait=wait):
                if event['event'] == 'BLOCK_JOB_COMPLETED':
                    self.assert_qmp(event, 'data/device', drive)
                    if error is None:
                        self.assert_qmp_absent(event, 'data/error')
                        if check_offset:
                            self.assert_qmp(event, 'data/offset',
                                            event['data']['len'])
                    else:
                        self.assert_qmp(event, 'data/error', error)
                    self.assert_no_active_block_jobs()
                    return event
                if event['event'] == 'JOB_STATUS_CHANGE':
                    self.assert_qmp(event, 'data/id', drive)

    def wait_ready(self, drive='drive0'):
        """Wait until a BLOCK_JOB_READY event, and return the event."""
        return self.vm.events_wait([
            ('BLOCK_JOB_READY',
             {'data': {'type': 'mirror', 'device': drive}}),
            ('BLOCK_JOB_READY',
             {'data': {'type': 'commit', 'device': drive}})
        ])

    def wait_ready_and_cancel(self, drive='drive0'):
        self.wait_ready(drive=drive)
        event = self.cancel_and_wait(drive=drive)
        self.assertEqual(event['event'], 'BLOCK_JOB_COMPLETED')
        self.assert_qmp(event, 'data/type', 'mirror')
        self.assert_qmp(event, 'data/offset', event['data']['len'])

    def complete_and_wait(self, drive='drive0', wait_ready=True,
                          completion_error=None):
        '''Complete a block job and wait for it to finish'''
        if wait_ready:
            self.wait_ready(drive=drive)

        self.vm.cmd('block-job-complete', device=drive)

        event = self.wait_until_completed(drive=drive, error=completion_error)
        self.assertTrue(event['data']['type'] in ['mirror', 'commit'])

    def pause_wait(self, job_id='job0'):
        with Timeout(3, "Timeout waiting for job to pause"):
            while True:
                result = self.vm.qmp('query-block-jobs')
                found = False
                for job in result['return']:
                    if job['device'] == job_id:
                        found = True
                        if job['paused'] and not job['busy']:
                            return job
                        break
                assert found

    def pause_job(self, job_id='job0', wait=True):
        self.vm.cmd('block-job-pause', device=job_id)
        if wait:
            self.pause_wait(job_id)

    def case_skip(self, reason):
        '''Skip this test case'''
        case_notrun(reason)
        self.skipTest(reason)


def notrun(reason):
    '''Skip this test suite'''
    # Each test in qemu-iotests has a number ("seq")
    seq = os.path.basename(sys.argv[0])

    with open('%s/%s.notrun' % (test_dir, seq), 'w', encoding='utf-8') \
            as outfile:
        outfile.write(reason + '\n')
    logger.warning("%s not run: %s", seq, reason)
    sys.exit(0)

def case_notrun(reason):
    '''Mark this test case as not having been run (without actually
    skipping it, that is left to the caller).  See
    QMPTestCase.case_skip() for a variant that actually skips the
    current test case.'''

    # Each test in qemu-iotests has a number ("seq")
    seq = os.path.basename(sys.argv[0])

    with open('%s/%s.casenotrun' % (test_dir, seq), 'a', encoding='utf-8') \
            as outfile:
        outfile.write('    [case not run] ' + reason + '\n')

def _verify_image_format(supported_fmts: Sequence[str] = (),
                         unsupported_fmts: Sequence[str] = ()) -> None:
    if 'generic' in supported_fmts and \
            os.environ.get('IMGFMT_GENERIC', 'true') == 'true':
        # similar to
        #   _supported_fmt generic
        # for bash tests
        supported_fmts = ()

    not_sup = supported_fmts and (imgfmt not in supported_fmts)
    if not_sup or (imgfmt in unsupported_fmts):
        notrun('not suitable for this image format: %s' % imgfmt)

    if imgfmt == 'luks':
        verify_working_luks()

def _verify_protocol(supported: Sequence[str] = (),
                     unsupported: Sequence[str] = ()) -> None:
    assert not (supported and unsupported)

    if 'generic' in supported:
        return

    not_sup = supported and (imgproto not in supported)
    if not_sup or (imgproto in unsupported):
        notrun('not suitable for this protocol: %s' % imgproto)

def _verify_platform(supported: Sequence[str] = (),
                     unsupported: Sequence[str] = ()) -> None:
    if any((sys.platform.startswith(x) for x in unsupported)):
        notrun('not suitable for this OS: %s' % sys.platform)

    if supported:
        if not any((sys.platform.startswith(x) for x in supported)):
            notrun('not suitable for this OS: %s' % sys.platform)

def _verify_cache_mode(supported_cache_modes: Sequence[str] = ()) -> None:
    if supported_cache_modes and (cachemode not in supported_cache_modes):
        notrun('not suitable for this cache mode: %s' % cachemode)

def _verify_aio_mode(supported_aio_modes: Sequence[str] = ()) -> None:
    if supported_aio_modes and (aiomode not in supported_aio_modes):
        notrun('not suitable for this aio mode: %s' % aiomode)

def _verify_formats(required_formats: Sequence[str] = ()) -> None:
    usf_list = list(set(required_formats) - set(supported_formats()))
    if usf_list:
        notrun(f'formats {usf_list} are not whitelisted')


def _verify_virtio_blk() -> None:
    out = qemu_pipe('-M', 'none', '-device', 'help')
    if 'virtio-blk' not in out:
        notrun('Missing virtio-blk in QEMU binary')

def verify_virtio_scsi_pci_or_ccw() -> None:
    out = qemu_pipe('-M', 'none', '-device', 'help')
    if 'virtio-scsi-pci' not in out and 'virtio-scsi-ccw' not in out:
        notrun('Missing virtio-scsi-pci or virtio-scsi-ccw in QEMU binary')


def _verify_imgopts(unsupported: Sequence[str] = ()) -> None:
    imgopts = os.environ.get('IMGOPTS')
    # One of usage examples for IMGOPTS is "data_file=$TEST_IMG.ext_data_file"
    # but it supported only for bash tests. We don't have a concept of global
    # TEST_IMG in iotests.py, not saying about somehow parsing $variables.
    # So, for simplicity let's just not support any IMGOPTS with '$' inside.
    unsup = list(unsupported) + ['$']
    if imgopts and any(x in imgopts for x in unsup):
        notrun(f'not suitable for this imgopts: {imgopts}')


def supports_quorum() -> bool:
    return 'quorum' in qemu_img('--help').stdout

def verify_quorum():
    '''Skip test suite if quorum support is not available'''
    if not supports_quorum():
        notrun('quorum support missing')

def has_working_luks() -> Tuple[bool, str]:
    """
    Check whether our LUKS driver can actually create images
    (this extends to LUKS encryption for qcow2).

    If not, return the reason why.
    """

    img_file = f'{test_dir}/luks-test.luks'
    res = qemu_img('create', '-f', 'luks',
                   '--object', luks_default_secret_object,
                   '-o', luks_default_key_secret_opt,
                   '-o', 'iter-time=10',
                   img_file, '1G',
                   check=False)
    try:
        os.remove(img_file)
    except OSError:
        pass

    if res.returncode:
        reason = res.stdout
        for line in res.stdout.splitlines():
            if img_file + ':' in line:
                reason = line.split(img_file + ':', 1)[1].strip()
                break

        return (False, reason)
    else:
        return (True, '')

def verify_working_luks():
    """
    Skip test suite if LUKS does not work
    """
    (working, reason) = has_working_luks()
    if not working:
        notrun(reason)

def supports_qcow2_zstd_compression() -> bool:
    img_file = f'{test_dir}/qcow2-zstd-test.qcow2'
    res = qemu_img('create', '-f', 'qcow2', '-o', 'compression_type=zstd',
                   img_file, '0',
                   check=False)
    try:
        os.remove(img_file)
    except OSError:
        pass

    if res.returncode == 1 and \
            "'compression-type' does not accept value 'zstd'" in res.stdout:
        return False
    else:
        return True

def verify_qcow2_zstd_compression():
    if not supports_qcow2_zstd_compression():
        notrun('zstd compression not supported')

def qemu_pipe(*args: str) -> str:
    """
    Run qemu with an option to print something and exit (e.g. a help option).

    :return: QEMU's stdout output.
    """
    full_args = [qemu_prog] + qemu_opts + list(args)
    output, _ = qemu_tool_pipe_and_status('qemu', full_args)
    return output

def supported_formats(read_only=False):
    '''Set 'read_only' to True to check ro-whitelist
       Otherwise, rw-whitelist is checked'''

    if not hasattr(supported_formats, "formats"):
        supported_formats.formats = {}

    if read_only not in supported_formats.formats:
        format_message = qemu_pipe("-drive", "format=help")
        line = 1 if read_only else 0
        supported_formats.formats[read_only] = \
            format_message.splitlines()[line].split(":")[1].split()

    return supported_formats.formats[read_only]

def skip_if_unsupported(required_formats=(), read_only=False):
    '''Skip Test Decorator
       Runs the test if all the required formats are whitelisted'''
    def skip_test_decorator(func):
        def func_wrapper(test_case: QMPTestCase, *args: List[Any],
                         **kwargs: Dict[str, Any]) -> None:
            if callable(required_formats):
                fmts = required_formats(test_case)
            else:
                fmts = required_formats

            usf_list = list(set(fmts) - set(supported_formats(read_only)))
            if usf_list:
                msg = f'{test_case}: formats {usf_list} are not whitelisted'
                test_case.case_skip(msg)
            else:
                func(test_case, *args, **kwargs)
        return func_wrapper
    return skip_test_decorator

def skip_for_formats(formats: Sequence[str] = ()) \
    -> Callable[[Callable[[QMPTestCase, List[Any], Dict[str, Any]], None]],
                Callable[[QMPTestCase, List[Any], Dict[str, Any]], None]]:
    '''Skip Test Decorator
       Skips the test for the given formats'''
    def skip_test_decorator(func):
        def func_wrapper(test_case: QMPTestCase, *args: List[Any],
                         **kwargs: Dict[str, Any]) -> None:
            if imgfmt in formats:
                msg = f'{test_case}: Skipped for format {imgfmt}'
                test_case.case_skip(msg)
            else:
                func(test_case, *args, **kwargs)
        return func_wrapper
    return skip_test_decorator

def skip_if_user_is_root(func):
    '''Skip Test Decorator
       Runs the test only without root permissions'''
    def func_wrapper(*args, **kwargs):
        if os.getuid() == 0:
            case_notrun('{}: cannot be run as root'.format(args[0]))
            return None
        else:
            return func(*args, **kwargs)
    return func_wrapper

# We need to filter out the time taken from the output so that
# qemu-iotest can reliably diff the results against master output,
# and hide skipped tests from the reference output.

class ReproducibleTestResult(unittest.TextTestResult):
    def addSkip(self, test, reason):
        # Same as TextTestResult, but print dot instead of "s"
        unittest.TestResult.addSkip(self, test, reason)
        if self.showAll:
            self.stream.writeln("skipped {0!r}".format(reason))
        elif self.dots:
            self.stream.write(".")
            self.stream.flush()

class ReproducibleStreamWrapper:
    def __init__(self, stream: TextIO):
        self.stream = stream

    def __getattr__(self, attr):
        if attr in ('stream', '__getstate__'):
            raise AttributeError(attr)
        return getattr(self.stream, attr)

    def write(self, arg=None):
        arg = re.sub(r'Ran (\d+) tests? in [\d.]+s', r'Ran \1 tests', arg)
        arg = re.sub(r' \(skipped=\d+\)', r'', arg)
        self.stream.write(arg)

class ReproducibleTestRunner(unittest.TextTestRunner):
    def __init__(
        self,
        stream: Optional[TextIO] = None,
        resultclass: Type[unittest.TextTestResult] =
        ReproducibleTestResult,
        **kwargs: Any
    ) -> None:
        rstream = ReproducibleStreamWrapper(stream or sys.stdout)
        super().__init__(stream=rstream,           # type: ignore
                         descriptions=True,
                         resultclass=resultclass,
                         **kwargs)

def execute_unittest(argv: List[str], debug: bool = False) -> None:
    """Executes unittests within the calling module."""

    # Some tests have warnings, especially ResourceWarnings for unclosed
    # files and sockets.  Ignore them for now to ensure reproducibility of
    # the test output.
    unittest.main(argv=argv,
                  testRunner=ReproducibleTestRunner,
                  verbosity=2 if debug else 1,
                  warnings=None if sys.warnoptions else 'ignore')

def execute_setup_common(supported_fmts: Sequence[str] = (),
                         supported_platforms: Sequence[str] = (),
                         supported_cache_modes: Sequence[str] = (),
                         supported_aio_modes: Sequence[str] = (),
                         unsupported_fmts: Sequence[str] = (),
                         supported_protocols: Sequence[str] = (),
                         unsupported_protocols: Sequence[str] = (),
                         required_fmts: Sequence[str] = (),
                         unsupported_imgopts: Sequence[str] = ()) -> bool:
    """
    Perform necessary setup for either script-style or unittest-style tests.

    :return: Bool; Whether or not debug mode has been requested via the CLI.
    """
    # Note: Python 3.6 and pylint do not like 'Collection' so use 'Sequence'.

    debug = '-d' in sys.argv
    if debug:
        sys.argv.remove('-d')
    logging.basicConfig(level=(logging.DEBUG if debug else logging.WARN))

    _verify_image_format(supported_fmts, unsupported_fmts)
    _verify_protocol(supported_protocols, unsupported_protocols)
    _verify_platform(supported=supported_platforms)
    _verify_cache_mode(supported_cache_modes)
    _verify_aio_mode(supported_aio_modes)
    _verify_formats(required_fmts)
    _verify_virtio_blk()
    _verify_imgopts(unsupported_imgopts)

    return debug

def execute_test(*args, test_function=None, **kwargs):
    """Run either unittest or script-style tests."""

    debug = execute_setup_common(*args, **kwargs)
    if not test_function:
        execute_unittest(sys.argv, debug)
    else:
        test_function()

def activate_logging():
    """Activate iotests.log() output to stdout for script-style tests."""
    handler = logging.StreamHandler(stream=sys.stdout)
    formatter = logging.Formatter('%(message)s')
    handler.setFormatter(formatter)
    test_logger.addHandler(handler)
    test_logger.setLevel(logging.INFO)
    test_logger.propagate = False

# This is called from script-style iotests without a single point of entry
def script_initialize(*args, **kwargs):
    """Initialize script-style tests without running any tests."""
    activate_logging()
    execute_setup_common(*args, **kwargs)

# This is called from script-style iotests with a single point of entry
def script_main(test_function, *args, **kwargs):
    """Run script-style tests outside of the unittest framework"""
    activate_logging()
    execute_test(*args, test_function=test_function, **kwargs)

# This is called from unittest style iotests
def main(*args, **kwargs):
    """Run tests using the unittest framework"""
    execute_test(*args, **kwargs)
