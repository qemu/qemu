# QEMU Monitor Protocol Python class
#
# Copyright (C) 2009, 2010 Red Hat Inc.
#
# Authors:
#  Luiz Capitulino <lcapitulino@redhat.com>
#
# This work is licensed under the terms of the GNU GPL, version 2.  See
# the COPYING file in the top-level directory.

import json
import errno
import socket
import sys

class QMPError(Exception):
    pass

class QMPConnectError(QMPError):
    pass

class QMPCapabilitiesError(QMPError):
    pass

class QMPTimeoutError(QMPError):
    pass

class QEMUMonitorProtocol:
    def __init__(self, address, server=False, debug=False):
        """
        Create a QEMUMonitorProtocol class.

        @param address: QEMU address, can be either a unix socket path (string)
                        or a tuple in the form ( address, port ) for a TCP
                        connection
        @param server: server mode listens on the socket (bool)
        @raise socket.error on socket connection errors
        @note No connection is established, this is done by the connect() or
              accept() methods
        """
        self.__events = []
        self.__address = address
        self._debug = debug
        self.__sock = self.__get_sock()
        if server:
            self.__sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            self.__sock.bind(self.__address)
            self.__sock.listen(1)

    def __get_sock(self):
        if isinstance(self.__address, tuple):
            family = socket.AF_INET
        else:
            family = socket.AF_UNIX
        return socket.socket(family, socket.SOCK_STREAM)

    def __negotiate_capabilities(self):
        greeting = self.__json_read()
        if greeting is None or not greeting.has_key('QMP'):
            raise QMPConnectError
        # Greeting seems ok, negotiate capabilities
        resp = self.cmd('qmp_capabilities')
        if "return" in resp:
            return greeting
        raise QMPCapabilitiesError

    def __json_read(self, only_event=False):
        while True:
            data = self.__sockfile.readline()
            if not data:
                return
            resp = json.loads(data)
            if 'event' in resp:
                if self._debug:
                    print >>sys.stderr, "QMP:<<< %s" % resp
                self.__events.append(resp)
                if not only_event:
                    continue
            return resp

    error = socket.error

    def __get_events(self, wait=False):
        """
        Check for new events in the stream and cache them in __events.

        @param wait (bool): block until an event is available.
        @param wait (float): If wait is a float, treat it as a timeout value.

        @raise QMPTimeoutError: If a timeout float is provided and the timeout
                                period elapses.
        @raise QMPConnectError: If wait is True but no events could be retrieved
                                or if some other error occurred.
        """

        # Check for new events regardless and pull them into the cache:
        self.__sock.setblocking(0)
        try:
            self.__json_read()
        except socket.error as err:
            if err[0] == errno.EAGAIN:
                # No data available
                pass
        self.__sock.setblocking(1)

        # Wait for new events, if needed.
        # if wait is 0.0, this means "no wait" and is also implicitly false.
        if not self.__events and wait:
            if isinstance(wait, float):
                self.__sock.settimeout(wait)
            try:
                ret = self.__json_read(only_event=True)
            except socket.timeout:
                raise QMPTimeoutError("Timeout waiting for event")
            except:
                raise QMPConnectError("Error while reading from socket")
            if ret is None:
                raise QMPConnectError("Error while reading from socket")
            self.__sock.settimeout(None)

    def connect(self, negotiate=True):
        """
        Connect to the QMP Monitor and perform capabilities negotiation.

        @return QMP greeting dict
        @raise socket.error on socket connection errors
        @raise QMPConnectError if the greeting is not received
        @raise QMPCapabilitiesError if fails to negotiate capabilities
        """
        self.__sock.connect(self.__address)
        self.__sockfile = self.__sock.makefile()
        if negotiate:
            return self.__negotiate_capabilities()

    def accept(self):
        """
        Await connection from QMP Monitor and perform capabilities negotiation.

        @return QMP greeting dict
        @raise socket.error on socket connection errors
        @raise QMPConnectError if the greeting is not received
        @raise QMPCapabilitiesError if fails to negotiate capabilities
        """
        self.__sock.settimeout(15)
        self.__sock, _ = self.__sock.accept()
        self.__sockfile = self.__sock.makefile()
        return self.__negotiate_capabilities()

    def cmd_obj(self, qmp_cmd):
        """
        Send a QMP command to the QMP Monitor.

        @param qmp_cmd: QMP command to be sent as a Python dict
        @return QMP response as a Python dict or None if the connection has
                been closed
        """
        if self._debug:
            print >>sys.stderr, "QMP:>>> %s" % qmp_cmd
        try:
            self.__sock.sendall(json.dumps(qmp_cmd))
        except socket.error as err:
            if err[0] == errno.EPIPE:
                return
            raise socket.error(err)
        resp = self.__json_read()
        if self._debug:
            print >>sys.stderr, "QMP:<<< %s" % resp
        return resp

    def cmd(self, name, args=None, id=None):
        """
        Build a QMP command and send it to the QMP Monitor.

        @param name: command name (string)
        @param args: command arguments (dict)
        @param id: command id (dict, list, string or int)
        """
        qmp_cmd = { 'execute': name }
        if args:
            qmp_cmd['arguments'] = args
        if id:
            qmp_cmd['id'] = id
        return self.cmd_obj(qmp_cmd)

    def command(self, cmd, **kwds):
        ret = self.cmd(cmd, kwds)
        if ret.has_key('error'):
            raise Exception(ret['error']['desc'])
        return ret['return']

    def pull_event(self, wait=False):
        """
        Get and delete the first available QMP event.

        @param wait (bool): block until an event is available.
        @param wait (float): If wait is a float, treat it as a timeout value.

        @raise QMPTimeoutError: If a timeout float is provided and the timeout
                                period elapses.
        @raise QMPConnectError: If wait is True but no events could be retrieved
                                or if some other error occurred.

        @return The first available QMP event, or None.
        """
        self.__get_events(wait)

        if self.__events:
            return self.__events.pop(0)
        return None

    def get_events(self, wait=False):
        """
        Get a list of available QMP events.

        @param wait (bool): block until an event is available.
        @param wait (float): If wait is a float, treat it as a timeout value.

        @raise QMPTimeoutError: If a timeout float is provided and the timeout
                                period elapses.
        @raise QMPConnectError: If wait is True but no events could be retrieved
                                or if some other error occurred.

        @return The list of available QMP events.
        """
        self.__get_events(wait)
        return self.__events

    def clear_events(self):
        """
        Clear current list of pending events.
        """
        self.__events = []

    def close(self):
        self.__sock.close()
        self.__sockfile.close()

    timeout = socket.timeout

    def settimeout(self, timeout):
        self.__sock.settimeout(timeout)

    def get_sock_fd(self):
        return self.__sock.fileno()

    def is_scm_available(self):
        return self.__sock.family == socket.AF_UNIX
