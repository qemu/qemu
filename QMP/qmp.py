# QEMU Monitor Protocol Python class
# 
# Copyright (C) 2009 Red Hat Inc.
#
# Authors:
#  Luiz Capitulino <lcapitulino@redhat.com>
#
# This work is licensed under the terms of the GNU GPL, version 2.  See
# the COPYING file in the top-level directory.

import socket, json

class QMPError(Exception):
    pass

class QMPConnectError(QMPError):
    pass

class QEMUMonitorProtocol:
    def connect(self):
        self.sock.connect(self.filename)
        data = self.__json_read()
        if data == None:
            raise QMPConnectError
        if not data.has_key('QMP'):
            raise QMPConnectError
        return data['QMP']['capabilities']

    def close(self):
        self.sock.close()

    def send_raw(self, line):
        self.sock.send(str(line))
        return self.__json_read()

    def send(self, cmdline):
        cmd = self.__build_cmd(cmdline)
        self.__json_send(cmd)
        resp = self.__json_read()
        if resp == None:
            return
        elif resp.has_key('error'):
            return resp['error']
        else:
            return resp['return']

    def __build_cmd(self, cmdline):
        cmdargs = cmdline.split()
        qmpcmd = { 'execute': cmdargs[0], 'arguments': {} }
        for arg in cmdargs[1:]:
            opt = arg.split('=')
            try:
                value = int(opt[1])
            except ValueError:
                value = opt[1]
            qmpcmd['arguments'][opt[0]] = value
        return qmpcmd

    def __json_send(self, cmd):
        # XXX: We have to send any additional char, otherwise
        # the Server won't read our input
        self.sock.send(json.dumps(cmd) + ' ')

    def __json_read(self):
        try:
            while True:
                line = json.loads(self.sockfile.readline())
                if not 'event' in line:
                    return line
        except ValueError:
            return

    def __init__(self, filename):
        self.filename = filename
        self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.sockfile = self.sock.makefile()
