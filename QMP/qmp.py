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

class QMPError(Exception):
    pass

class QMPConnectError(QMPError):
    pass

class QMPCapabilitiesError(QMPError):
    pass

class QEMUMonitorProtocol:
    def __init__(self, address):
        """
        Create a QEMUMonitorProtocol class.

        @param address: QEMU address, can be either a unix socket path (string)
                        or a tuple in the form ( address, port ) for a TCP
                        connection
        @note No connection is established, this is done by the connect() method
        """
        self.__events = []
        self.__address = address
        self.__sock = self.__get_sock()
        self.__sockfile = self.__sock.makefile()

    def __get_sock(self):
        if isinstance(self.__address, tuple):
            family = socket.AF_INET
        else:
            family = socket.AF_UNIX
        return socket.socket(family, socket.SOCK_STREAM)

    def __json_read(self, only_event=False):
        while True:
            data = self.__sockfile.readline()
            if not data:
                return
            resp = json.loads(data)
            if 'event' in resp:
                self.__events.append(resp)
                if not only_event:
                    continue
            return resp

    error = socket.error

    def connect(self):
        """
        Connect to the QMP Monitor and perform capabilities negotiation.

        @return QMP greeting dict
        @raise socket.error on socket connection errors
        @raise QMPConnectError if the greeting is not received
        @raise QMPCapabilitiesError if fails to negotiate capabilities
        """
        self.__sock.connect(self.__address)
        greeting = self.__json_read()
        if greeting is None or not greeting.has_key('QMP'):
            raise QMPConnectError
        # Greeting seems ok, negotiate capabilities
        resp = self.cmd('qmp_capabilities')
        if "return" in resp:
            return greeting
        raise QMPCapabilitiesError

    def cmd_obj(self, qmp_cmd):
        """
        Send a QMP command to the QMP Monitor.

        @param qmp_cmd: QMP command to be sent as a Python dict
        @return QMP response as a Python dict or None if the connection has
                been closed
        """
        try:
            self.__sock.sendall(json.dumps(qmp_cmd))
        except socket.error, err:
            if err[0] == errno.EPIPE:
                return
            raise socket.error(err)
        return self.__json_read()

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

    def get_events(self, wait=False):
        """
        Get a list of available QMP events.

        @param wait: block until an event is available (bool)
        """
        self.__sock.setblocking(0)
        try:
            self.__json_read()
        except socket.error, err:
            if err[0] == errno.EAGAIN:
                # No data available
                pass
        self.__sock.setblocking(1)
        if not self.__events and wait:
            self.__json_read(only_event=True)
        return self.__events

    def clear_events(self):
        """
        Clear current list of pending events.
        """
        self.__events = []

    def close(self):
        self.__sock.close()
        self.__sockfile.close()
