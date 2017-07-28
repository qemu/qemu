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
import string
import os
import sys
import subprocess
import qmp.qmp


logging.basicConfig()
LOG = logging.getLogger(__name__)


class QEMULaunchError(Exception):
    pass


class QEMUMachine(object):
    '''A QEMU VM'''

    def __init__(self, binary, args=[], wrapper=[], name=None, test_dir="/var/tmp",
                 monitor_address=None, socket_scm_helper=None, debug=False):
        if debug:
            LOG.setLevel(logging.DEBUG)
        else:
            LOG.setLevel(logging.INFO)
        if name is None:
            name = "qemu-%d" % os.getpid()
        if monitor_address is None:
            monitor_address = os.path.join(test_dir, name + "-monitor.sock")
        self._monitor_address = monitor_address
        self._qemu_log_path = os.path.join(test_dir, name + ".log")
        self._qemu_log_fd = None
        self._popen = None
        self._binary = binary
        self._args = list(args) # Force copy args in case we modify them
        self._wrapper = wrapper
        self._events = []
        self._iolog = None
        self._socket_scm_helper = socket_scm_helper
        self._debug = debug
        self._qemu_full_args = None
        self._created_files = []
        self._pending_shutdown = False

    # This can be used to add an unused monitor instance.
    def add_monitor_telnet(self, ip, port):
        args = 'tcp:%s:%d,server,nowait,telnet' % (ip, port)
        self._args.append('-monitor')
        self._args.append(args)

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
        if self._socket_scm_helper is None:
            LOG.error("No path to socket_scm_helper set")
            return -1
        if os.path.exists(self._socket_scm_helper) == False:
            LOG.error("%s does not exist", self._socket_scm_helper)
            return -1
        fd_param = ["%s" % self._socket_scm_helper,
                    "%d" % self._qmp.get_sock_fd(),
                    "%s" % fd_file_path]
        devnull = open(os.path.devnull, 'rb')
        p = subprocess.Popen(fd_param, stdin=devnull, stdout=sys.stdout,
                             stderr=sys.stderr)
        return p.wait()

    @staticmethod
    def _remove_if_exists(path):
        '''Remove file object at path if it exists'''
        try:
            os.remove(path)
        except OSError as exception:
            if exception.errno == errno.ENOENT:
                return
            raise

    def is_running(self):
        return self._popen is not None and (self._popen.poll() is None)

    def exitcode(self):
        if self._popen is None:
            return None
        return self._popen.poll()

    def get_pid(self):
        if not self.is_running():
            return None
        return self._popen.pid

    def _load_io_log(self):
        if os.path.exists(self._qemu_log_path):
            with open(self._qemu_log_path, "r") as fh:
                self._iolog = fh.read()

    def _base_args(self):
        if isinstance(self._monitor_address, tuple):
            moncdev = "socket,id=mon,host=%s,port=%s" % (
                self._monitor_address[0],
                self._monitor_address[1])
        else:
            moncdev = 'socket,id=mon,path=%s' % self._monitor_address
        return ['-chardev', moncdev,
                '-mon', 'chardev=mon,mode=control',
                '-display', 'none', '-vga', 'none']

    def _pre_launch(self):
        if (not isinstance(self._monitor_address, tuple) and
                os.path.exists(self._monitor_address)):
            raise QEMULaunchError('File %s exists. Please remove it.' %
                                  self._monitor_address)

        self._qmp = qmp.qmp.QEMUMonitorProtocol(self._monitor_address,
                                                server=True,
                                                debug=self._debug)
        if not isinstance(self._monitor_address, tuple):
            self._created_files.append(self._monitor_address)

        if os.path.exists(self._qemu_log_path):
            raise QEMULaunchError('File %s exists. Please remove it.' %
                                  self._qemu_log_path)

        self._qemu_log_fd = open(self._qemu_log_path, 'wb')
        self._created_files.append(self._qemu_log_path)

    def _post_launch(self):
        self._qmp.accept()

    def _post_shutdown(self):
        while self._created_files:
            self._remove_if_exists(self._created_files.pop())

    def launch(self):
        '''
        Try to launch the VM and make sure we cleanup and expose the
        command line/output in case of exception.
        '''

        if self.is_running():
            raise QEMULaunchError('VM already running.')

        if self._pending_shutdown:
            raise QEMULaunchError('Shutdown after the previous launch '
                                  'is pending. Please call shutdown() '
                                  'before launching again.')

        try:
            self._iolog = None
            self._qemu_full_args = None
            self._qemu_full_args = (self._wrapper + [self._binary] +
                                    self._base_args() + self._args)
            self._launch()
            self._pending_shutdown = True
        except:
            self.shutdown()
            LOG.debug('Error launching VM.%s%s',
                      ' Command: %r.' % ' '.join(self._qemu_full_args)
                      if self._qemu_full_args else '',
                      ' Output: %r.' % self._iolog
                      if self._iolog else '')
            raise

    def _launch(self):
        '''Launch the VM and establish a QMP connection.'''
        self._pre_launch()
        devnull = open(os.path.devnull, 'rb')
        self._popen = subprocess.Popen(self._qemu_full_args,
                                       stdin=devnull,
                                       stdout=self._qemu_log_fd,
                                       stderr=subprocess.STDOUT,
                                       shell=False)
        self._post_launch()

    def shutdown(self):
        '''Terminate the VM and clean up'''
        if self.is_running():
            try:
                self._qmp.cmd('quit')
                self._qmp.close()
            except:
                self._popen.kill()
            self._popen.wait()

        if self._pending_shutdown:
            exit_code = self.exitcode()
            if exit_code is not None and exit_code < 0:
                LOG.error('qemu received signal %i: %s', -exit_code,
                          ' Command: %r.' % ' '.join(self._qemu_full_args)
                          if self._qemu_full_args else '')

        self._load_io_log()
        self._post_shutdown()
        self._pending_shutdown = False

    underscore_to_dash = string.maketrans('_', '-')
    def qmp(self, cmd, conv_keys=True, **args):
        '''Invoke a QMP command and return the result dict'''
        qmp_args = dict()
        for k in args.keys():
            if conv_keys:
                qmp_args[k.translate(self.underscore_to_dash)] = args[k]
            else:
                qmp_args[k] = args[k]

        return self._qmp.cmd(cmd, args=qmp_args)

    def command(self, cmd, conv_keys=True, **args):
        reply = self.qmp(cmd, conv_keys, **args)
        if reply is None:
            raise Exception("Monitor is closed")
        if "error" in reply:
            raise Exception(reply["error"]["desc"])
        return reply["return"]

    def get_qmp_event(self, wait=False):
        '''Poll for one queued QMP events and return it'''
        if len(self._events) > 0:
            return self._events.pop(0)
        return self._qmp.pull_event(wait=wait)

    def get_qmp_events(self, wait=False):
        '''Poll for queued QMP events and return a list of dicts'''
        events = self._qmp.get_events(wait=wait)
        events.extend(self._events)
        del self._events[:]
        self._qmp.clear_events()
        return events

    def event_wait(self, name, timeout=60.0, match=None):
        # Test if 'match' is a recursive subset of 'event'
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
        return self._iolog
