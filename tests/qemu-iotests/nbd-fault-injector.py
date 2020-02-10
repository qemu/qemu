#!/usr/bin/env python3
# NBD server - fault injection utility
#
# Configuration file syntax:
#   [inject-error "disconnect-neg1"]
#   event=neg1
#   io=readwrite
#   when=before
#
# Note that Python's ConfigParser squashes together all sections with the same
# name, so give each [inject-error] a unique name.
#
# inject-error options:
#   event - name of the trigger event
#           "neg1" - first part of negotiation struct
#           "export" - export struct
#           "neg2" - second part of negotiation struct
#           "request" - NBD request struct
#           "reply" - NBD reply struct
#           "data" - request/reply data
#   io    - I/O direction that triggers this rule:
#           "read", "write", or "readwrite"
#           default: readwrite
#   when  - after how many bytes to inject the fault
#           -1 - inject error after I/O
#           0 - inject error before I/O
#           integer - inject error after integer bytes
#           "before" - alias for 0
#           "after" - alias for -1
#           default: before
#
# Currently the only error injection action is to terminate the server process.
# This resets the TCP connection and thus forces the client to handle
# unexpected connection termination.
#
# Other error injection actions could be added in the future.
#
# Copyright Red Hat, Inc. 2014
#
# Authors:
#   Stefan Hajnoczi <stefanha@redhat.com>
#
# This work is licensed under the terms of the GNU GPL, version 2 or later.
# See the COPYING file in the top-level directory.

import sys
import socket
import struct
import collections
if sys.version_info.major >= 3:
    import configparser
else:
    import ConfigParser as configparser

FAKE_DISK_SIZE = 8 * 1024 * 1024 * 1024 # 8 GB

# Protocol constants
NBD_CMD_READ = 0
NBD_CMD_WRITE = 1
NBD_CMD_DISC = 2
NBD_REQUEST_MAGIC = 0x25609513
NBD_SIMPLE_REPLY_MAGIC = 0x67446698
NBD_PASSWD = 0x4e42444d41474943
NBD_OPTS_MAGIC = 0x49484156454F5054
NBD_CLIENT_MAGIC = 0x0000420281861253
NBD_OPT_EXPORT_NAME = 1 << 0

# Protocol structs
neg_classic_struct = struct.Struct('>QQQI124x')
neg1_struct = struct.Struct('>QQH')
export_tuple = collections.namedtuple('Export', 'reserved magic opt len')
export_struct = struct.Struct('>IQII')
neg2_struct = struct.Struct('>QH124x')
request_tuple = collections.namedtuple('Request', 'magic type handle from_ len')
request_struct = struct.Struct('>IIQQI')
reply_struct = struct.Struct('>IIQ')

def err(msg):
    sys.stderr.write(msg + '\n')
    sys.exit(1)

def recvall(sock, bufsize):
    received = 0
    chunks = []
    while received < bufsize:
        chunk = sock.recv(bufsize - received)
        if len(chunk) == 0:
            raise Exception('unexpected disconnect')
        chunks.append(chunk)
        received += len(chunk)
    return b''.join(chunks)

class Rule(object):
    def __init__(self, name, event, io, when):
        self.name = name
        self.event = event
        self.io = io
        self.when = when

    def match(self, event, io):
        if event != self.event:
            return False
        if io != self.io and self.io != 'readwrite':
            return False
        return True

class FaultInjectionSocket(object):
    def __init__(self, sock, rules):
        self.sock = sock
        self.rules = rules

    def check(self, event, io, bufsize=None):
        for rule in self.rules:
            if rule.match(event, io):
                if rule.when == 0 or bufsize is None:
                    print('Closing connection on rule match %s' % rule.name)
                    self.sock.close()
                    sys.stdout.flush()
                    sys.exit(0)
                if rule.when != -1:
                    return rule.when
        return bufsize

    def send(self, buf, event):
        bufsize = self.check(event, 'write', bufsize=len(buf))
        self.sock.sendall(buf[:bufsize])
        self.check(event, 'write')

    def recv(self, bufsize, event):
        bufsize = self.check(event, 'read', bufsize=bufsize)
        data = recvall(self.sock, bufsize)
        self.check(event, 'read')
        return data

    def close(self):
        self.sock.close()

def negotiate_classic(conn):
    buf = neg_classic_struct.pack(NBD_PASSWD, NBD_CLIENT_MAGIC,
                                  FAKE_DISK_SIZE, 0)
    conn.send(buf, event='neg-classic')

def negotiate_export(conn):
    # Send negotiation part 1
    buf = neg1_struct.pack(NBD_PASSWD, NBD_OPTS_MAGIC, 0)
    conn.send(buf, event='neg1')

    # Receive export option
    buf = conn.recv(export_struct.size, event='export')
    export = export_tuple._make(export_struct.unpack(buf))
    assert export.magic == NBD_OPTS_MAGIC
    assert export.opt == NBD_OPT_EXPORT_NAME
    name = conn.recv(export.len, event='export-name')

    # Send negotiation part 2
    buf = neg2_struct.pack(FAKE_DISK_SIZE, 0)
    conn.send(buf, event='neg2')

def negotiate(conn, use_export):
    '''Negotiate export with client'''
    if use_export:
        negotiate_export(conn)
    else:
        negotiate_classic(conn)

def read_request(conn):
    '''Parse NBD request from client'''
    buf = conn.recv(request_struct.size, event='request')
    req = request_tuple._make(request_struct.unpack(buf))
    assert req.magic == NBD_REQUEST_MAGIC
    return req

def write_reply(conn, error, handle):
    buf = reply_struct.pack(NBD_SIMPLE_REPLY_MAGIC, error, handle)
    conn.send(buf, event='reply')

def handle_connection(conn, use_export):
    negotiate(conn, use_export)
    while True:
        req = read_request(conn)
        if req.type == NBD_CMD_READ:
            write_reply(conn, 0, req.handle)
            conn.send(b'\0' * req.len, event='data')
        elif req.type == NBD_CMD_WRITE:
            _ = conn.recv(req.len, event='data')
            write_reply(conn, 0, req.handle)
        elif req.type == NBD_CMD_DISC:
            break
        else:
            print('unrecognized command type %#02x' % req.type)
            break
    conn.close()

def run_server(sock, rules, use_export):
    while True:
        conn, _ = sock.accept()
        handle_connection(FaultInjectionSocket(conn, rules), use_export)

def parse_inject_error(name, options):
    if 'event' not in options:
        err('missing \"event\" option in %s' % name)
    event = options['event']
    if event not in ('neg-classic', 'neg1', 'export', 'neg2', 'request', 'reply', 'data'):
        err('invalid \"event\" option value \"%s\" in %s' % (event, name))
    io = options.get('io', 'readwrite')
    if io not in ('read', 'write', 'readwrite'):
        err('invalid \"io\" option value \"%s\" in %s' % (io, name))
    when = options.get('when', 'before')
    try:
        when = int(when)
    except ValueError:
        if when == 'before':
            when = 0
        elif when == 'after':
            when = -1
        else:
            err('invalid \"when\" option value \"%s\" in %s' % (when, name))
    return Rule(name, event, io, when)

def parse_config(config):
    rules = []
    for name in config.sections():
        if name.startswith('inject-error'):
            options = dict(config.items(name))
            rules.append(parse_inject_error(name, options))
        else:
            err('invalid config section name: %s' % name)
    return rules

def load_rules(filename):
    config = configparser.RawConfigParser()
    with open(filename, 'rt') as f:
        config.readfp(f, filename)
    return parse_config(config)

def open_socket(path):
    '''Open a TCP or UNIX domain listen socket'''
    if ':' in path:
        host, port = path.split(':', 1)
        sock = socket.socket()
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        sock.bind((host, int(port)))

        # If given port was 0 the final port number is now available
        path = '%s:%d' % sock.getsockname()
    else:
        sock = socket.socket(socket.AF_UNIX)
        sock.bind(path)
    sock.listen(0)
    print('Listening on %s' % path)
    sys.stdout.flush() # another process may be waiting, show message now
    return sock

def usage(args):
    sys.stderr.write('usage: %s [--classic-negotiation] <tcp-port>|<unix-path> <config-file>\n' % args[0])
    sys.stderr.write('Run an fault injector NBD server with rules defined in a config file.\n')
    sys.exit(1)

def main(args):
    if len(args) != 3 and len(args) != 4:
        usage(args)
    use_export = True
    if args[1] == '--classic-negotiation':
        use_export = False
    elif len(args) == 4:
        usage(args)
    sock = open_socket(args[1 if use_export else 2])
    rules = load_rules(args[2 if use_export else 3])
    run_server(sock, rules, use_export)
    return 0

if __name__ == '__main__':
    sys.exit(main(sys.argv))
