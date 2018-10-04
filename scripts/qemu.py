# QEMU library
#
# Copyright (C) 2015-2016 Red Hat Inc.
# Copyright (C) 2012 IBM Corp.
#
# Authors:
#  Fam Zheng <famz@redhat.com>
#
# This work is licensed under the terms of the GNU GPL, version 2.  See
# the COPYING file in the top-level directory.
#
# Based on qmp.py.
#

import errno
import logging
import os
import subprocess
import qmp.qmp
import re
import shutil
import socket
import tempfile


LOG = logging.getLogger(__name__)


def kvm_available(target_arch=None):
    if target_arch and target_arch != os.uname()[4]:
        return False
    return os.access("/dev/kvm", os.R_OK | os.W_OK)


#: Maps machine types to the preferred console device types
CONSOLE_DEV_TYPES = {
    r'^clipper$': 'isa-serial',
    r'^malta': 'isa-serial',
    r'^(pc.*|q35.*|isapc)$': 'isa-serial',
    r'^(40p|powernv|prep)$': 'isa-serial',
    r'^pseries.*': 'spapr-vty',
    r'^s390-ccw-virtio.*': 'sclpconsole',
    }


class QEMUMachineError(Exception):
    """
    Exception called when an error in QEMUMachine happens.
    """


class QEMUMachineAddDeviceError(QEMUMachineError):
    """
    Exception raised when a request to add a device can not be fulfilled

    The failures are caused by limitations, lack of information or conflicting
    requests on the QEMUMachine methods.  This exception does not represent
    failures reported by the QEMU binary itself.
    """

class MonitorResponseError(qmp.qmp.QMPError):
    """
    Represents erroneous QMP monitor reply
    """
    def __init__(self, reply):
        try:
            desc = reply["error"]["desc"]
        except KeyError:
            desc = reply
        super(MonitorResponseError, self).__init__(desc)
        self.reply = reply


class QEMUMachine(object):
    """
    A QEMU VM

    Use this object as a context manager to ensure the QEMU process terminates::

        with VM(binary) as vm:
            ...
        # vm is guaranteed to be shut down here
    """

    def __init__(self, binary, args=None, wrapper=None, name=None,
                 test_dir="/var/tmp", monitor_address=None,
                 socket_scm_helper=None):
        '''
        Initialize a QEMUMachine

        @param binary: path to the qemu binary
        @param args: list of extra arguments
        @param wrapper: list of arguments used as prefix to qemu binary
        @param name: prefix for socket and log file names (default: qemu-PID)
        @param test_dir: where to create socket and log file
        @param monitor_address: address for QMP monitor
        @param socket_scm_helper: helper program, required for send_fd_scm()
        @note: Qemu process is not started until launch() is used.
        '''
        if args is None:
            args = []
        if wrapper is None:
            wrapper = []
        if name is None:
            name = "qemu-%d" % os.getpid()
        self._name = name
        self._monitor_address = monitor_address
        self._vm_monitor = None
        self._qemu_log_path = None
        self._qemu_log_file = None
        self._popen = None
        self._binary = binary
        self._args = list(args)     # Force copy args in case we modify them
        self._wrapper = wrapper
        self._events = []
        self._iolog = None
        self._socket_scm_helper = socket_scm_helper
        self._qmp = None
        self._qemu_full_args = None
        self._test_dir = test_dir
        self._temp_dir = None
        self._launched = False
        self._machine = None
        self._console_device_type = None
        self._console_address = None
        self._console_socket = None

        # just in case logging wasn't configured by the main script:
        logging.basicConfig()

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.shutdown()
        return False

    # This can be used to add an unused monitor instance.
    def add_monitor_telnet(self, ip, port):
        args = 'tcp:%s:%d,server,nowait,telnet' % (ip, port)
        self._args.append('-monitor')
        self._args.append(args)

    def add_fd(self, fd, fdset, opaque, opts=''):
        """
        Pass a file descriptor to the VM
        """
        options = ['fd=%d' % fd,
                   'set=%d' % fdset,
                   'opaque=%s' % opaque]
        if opts:
            options.append(opts)

        # This did not exist before 3.4, but since then it is
        # mandatory for our purpose
        if hasattr(os, 'set_inheritable'):
            os.set_inheritable(fd, True)

        self._args.append('-add-fd')
        self._args.append(','.join(options))
        return self

    # Exactly one of fd and file_path must be given.
    # (If it is file_path, the helper will open that file and pass its
    # own fd)
    def send_fd_scm(self, fd=None, file_path=None):
        # In iotest.py, the qmp should always use unix socket.
        assert self._qmp.is_scm_available()
        if self._socket_scm_helper is None:
            raise QEMUMachineError("No path to socket_scm_helper set")
        if not os.path.exists(self._socket_scm_helper):
            raise QEMUMachineError("%s does not exist" %
                                   self._socket_scm_helper)

        # This did not exist before 3.4, but since then it is
        # mandatory for our purpose
        if hasattr(os, 'set_inheritable'):
            os.set_inheritable(self._qmp.get_sock_fd(), True)
            if fd is not None:
                os.set_inheritable(fd, True)

        fd_param = ["%s" % self._socket_scm_helper,
                    "%d" % self._qmp.get_sock_fd()]

        if file_path is not None:
            assert fd is None
            fd_param.append(file_path)
        else:
            assert fd is not None
            fd_param.append(str(fd))

        devnull = open(os.path.devnull, 'rb')
        proc = subprocess.Popen(fd_param, stdin=devnull, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, close_fds=False)
        output = proc.communicate()[0]
        if output:
            LOG.debug(output)

        return proc.returncode

    @staticmethod
    def _remove_if_exists(path):
        """
        Remove file object at path if it exists
        """
        try:
            os.remove(path)
        except OSError as exception:
            if exception.errno == errno.ENOENT:
                return
            raise

    def is_running(self):
        return self._popen is not None and self._popen.poll() is None

    def exitcode(self):
        if self._popen is None:
            return None
        return self._popen.poll()

    def get_pid(self):
        if not self.is_running():
            return None
        return self._popen.pid

    def _load_io_log(self):
        if self._qemu_log_path is not None:
            with open(self._qemu_log_path, "r") as iolog:
                self._iolog = iolog.read()

    def _base_args(self):
        if isinstance(self._monitor_address, tuple):
            moncdev = "socket,id=mon,host=%s,port=%s" % (
                self._monitor_address[0],
                self._monitor_address[1])
        else:
            moncdev = 'socket,id=mon,path=%s' % self._vm_monitor
        args = ['-chardev', moncdev,
                '-mon', 'chardev=mon,mode=control',
                '-display', 'none', '-vga', 'none']
        if self._machine is not None:
            args.extend(['-machine', self._machine])
        if self._console_device_type is not None:
            self._console_address = os.path.join(self._temp_dir,
                                                 self._name + "-console.sock")
            chardev = ('socket,id=console,path=%s,server,nowait' %
                       self._console_address)
            device = '%s,chardev=console' % self._console_device_type
            args.extend(['-chardev', chardev, '-device', device])
        return args

    def _pre_launch(self):
        self._temp_dir = tempfile.mkdtemp(dir=self._test_dir)
        if self._monitor_address is not None:
            self._vm_monitor = self._monitor_address
        else:
            self._vm_monitor = os.path.join(self._temp_dir,
                                            self._name + "-monitor.sock")
        self._qemu_log_path = os.path.join(self._temp_dir, self._name + ".log")
        self._qemu_log_file = open(self._qemu_log_path, 'wb')

        self._qmp = qmp.qmp.QEMUMonitorProtocol(self._vm_monitor,
                                                server=True)

    def _post_launch(self):
        self._qmp.accept()

    def _post_shutdown(self):
        if self._qemu_log_file is not None:
            self._qemu_log_file.close()
            self._qemu_log_file = None

        self._qemu_log_path = None

        if self._console_socket is not None:
            self._console_socket.close()
            self._console_socket = None

        if self._temp_dir is not None:
            shutil.rmtree(self._temp_dir)
            self._temp_dir = None

    def launch(self):
        """
        Launch the VM and make sure we cleanup and expose the
        command line/output in case of exception
        """

        if self._launched:
            raise QEMUMachineError('VM already launched')

        self._iolog = None
        self._qemu_full_args = None
        try:
            self._launch()
            self._launched = True
        except:
            self.shutdown()

            LOG.debug('Error launching VM')
            if self._qemu_full_args:
                LOG.debug('Command: %r', ' '.join(self._qemu_full_args))
            if self._iolog:
                LOG.debug('Output: %r', self._iolog)
            raise

    def _launch(self):
        """
        Launch the VM and establish a QMP connection
        """
        devnull = open(os.path.devnull, 'rb')
        self._pre_launch()
        self._qemu_full_args = (self._wrapper + [self._binary] +
                                self._base_args() + self._args)
        self._popen = subprocess.Popen(self._qemu_full_args,
                                       stdin=devnull,
                                       stdout=self._qemu_log_file,
                                       stderr=subprocess.STDOUT,
                                       shell=False,
                                       close_fds=False)
        self._post_launch()

    def wait(self):
        """
        Wait for the VM to power off
        """
        self._popen.wait()
        self._qmp.close()
        self._load_io_log()
        self._post_shutdown()

    def shutdown(self):
        """
        Terminate the VM and clean up
        """
        if self.is_running():
            try:
                self._qmp.cmd('quit')
                self._qmp.close()
            except:
                self._popen.kill()
            self._popen.wait()

        self._load_io_log()
        self._post_shutdown()

        exitcode = self.exitcode()
        if exitcode is not None and exitcode < 0:
            msg = 'qemu received signal %i: %s'
            if self._qemu_full_args:
                command = ' '.join(self._qemu_full_args)
            else:
                command = ''
            LOG.warn(msg, exitcode, command)

        self._launched = False

    def qmp(self, cmd, conv_keys=True, **args):
        """
        Invoke a QMP command and return the response dict
        """
        qmp_args = dict()
        for key, value in args.items():
            if conv_keys:
                qmp_args[key.replace('_', '-')] = value
            else:
                qmp_args[key] = value

        return self._qmp.cmd(cmd, args=qmp_args)

    def command(self, cmd, conv_keys=True, **args):
        """
        Invoke a QMP command.
        On success return the response dict.
        On failure raise an exception.
        """
        reply = self.qmp(cmd, conv_keys, **args)
        if reply is None:
            raise qmp.qmp.QMPError("Monitor is closed")
        if "error" in reply:
            raise MonitorResponseError(reply)
        return reply["return"]

    def get_qmp_event(self, wait=False):
        """
        Poll for one queued QMP events and return it
        """
        if len(self._events) > 0:
            return self._events.pop(0)
        return self._qmp.pull_event(wait=wait)

    def get_qmp_events(self, wait=False):
        """
        Poll for queued QMP events and return a list of dicts
        """
        events = self._qmp.get_events(wait=wait)
        events.extend(self._events)
        del self._events[:]
        self._qmp.clear_events()
        return events

    def event_wait(self, name, timeout=60.0, match=None):
        """
        Wait for specified timeout on named event in QMP; optionally filter
        results by match.

        The 'match' is checked to be a recursive subset of the 'event'; skips
        branch processing on match's value None
           {"foo": {"bar": 1}} matches {"foo": None}
           {"foo": {"bar": 1}} does not matches {"foo": {"baz": None}}
        """
        def event_match(event, match=None):
            if match is None:
                return True

            for key in match:
                if key in event:
                    if isinstance(event[key], dict):
                        if not event_match(event[key], match[key]):
                            return False
                    elif event[key] != match[key]:
                        return False
                else:
                    return False

            return True

        # Search cached events
        for event in self._events:
            if (event['event'] == name) and event_match(event, match):
                self._events.remove(event)
                return event

        # Poll for new events
        while True:
            event = self._qmp.pull_event(wait=timeout)
            if (event['event'] == name) and event_match(event, match):
                return event
            self._events.append(event)

        return None

    def get_log(self):
        """
        After self.shutdown or failed qemu execution, this returns the output
        of the qemu process.
        """
        return self._iolog

    def add_args(self, *args):
        """
        Adds to the list of extra arguments to be given to the QEMU binary
        """
        self._args.extend(args)

    def set_machine(self, machine_type):
        """
        Sets the machine type

        If set, the machine type will be added to the base arguments
        of the resulting QEMU command line.
        """
        self._machine = machine_type

    def set_console(self, device_type=None):
        """
        Sets the device type for a console device

        If set, the console device and a backing character device will
        be added to the base arguments of the resulting QEMU command
        line.

        This is a convenience method that will either use the provided
        device type, of if not given, it will used the device type set
        on CONSOLE_DEV_TYPES.

        The actual setting of command line arguments will be be done at
        machine launch time, as it depends on the temporary directory
        to be created.

        @param device_type: the device type, such as "isa-serial"
        @raises: QEMUMachineAddDeviceError if the device type is not given
                 and can not be determined.
        """
        if device_type is None:
            if self._machine is None:
                raise QEMUMachineAddDeviceError("Can not add a console device:"
                                                " QEMU instance without a "
                                                "defined machine type")
            for regex, device in CONSOLE_DEV_TYPES.items():
                if re.match(regex, self._machine):
                    device_type = device
                    break
            if device_type is None:
                raise QEMUMachineAddDeviceError("Can not add a console device:"
                                                " no matching console device "
                                                "type definition")
        self._console_device_type = device_type

    @property
    def console_socket(self):
        """
        Returns a socket connected to the console
        """
        if self._console_socket is None:
            self._console_socket = socket.socket(socket.AF_UNIX,
                                                 socket.SOCK_STREAM)
            self._console_socket.connect(self._console_address)
        return self._console_socket
