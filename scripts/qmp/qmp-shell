#!/usr/bin/python
#
# Low-level QEMU shell on top of QMP.
#
# Copyright (C) 2009, 2010 Red Hat Inc.
#
# Authors:
#  Luiz Capitulino <lcapitulino@redhat.com>
#
# This work is licensed under the terms of the GNU GPL, version 2.  See
# the COPYING file in the top-level directory.
#
# Usage:
#
# Start QEMU with:
#
# # qemu [...] -qmp unix:./qmp-sock,server
#
# Run the shell:
#
# $ qmp-shell ./qmp-sock
#
# Commands have the following format:
#
#    < command-name > [ arg-name1=arg1 ] ... [ arg-nameN=argN ]
#
# For example:
#
# (QEMU) device_add driver=e1000 id=net1
# {u'return': {}}
# (QEMU)

import qmp
import json
import readline
import sys
import pprint

class QMPCompleter(list):
    def complete(self, text, state):
        for cmd in self:
            if cmd.startswith(text):
                if not state:
                    return cmd
                else:
                    state -= 1

class QMPShellError(Exception):
    pass

class QMPShellBadPort(QMPShellError):
    pass

# TODO: QMPShell's interface is a bit ugly (eg. _fill_completion() and
#       _execute_cmd()). Let's design a better one.
class QMPShell(qmp.QEMUMonitorProtocol):
    def __init__(self, address, pp=None):
        qmp.QEMUMonitorProtocol.__init__(self, self.__get_address(address))
        self._greeting = None
        self._completer = None
        self._pp = pp

    def __get_address(self, arg):
        """
        Figure out if the argument is in the port:host form, if it's not it's
        probably a file path.
        """
        addr = arg.split(':')
        if len(addr) == 2:
            try:
                port = int(addr[1])
            except ValueError:
                raise QMPShellBadPort
            return ( addr[0], port )
        # socket path
        return arg

    def _fill_completion(self):
        for cmd in self.cmd('query-commands')['return']:
            self._completer.append(cmd['name'])

    def __completer_setup(self):
        self._completer = QMPCompleter()
        self._fill_completion()
        readline.set_completer(self._completer.complete)
        readline.parse_and_bind("tab: complete")
        # XXX: default delimiters conflict with some command names (eg. query-),
        # clearing everything as it doesn't seem to matter
        readline.set_completer_delims('')

    def __build_cmd(self, cmdline):
        """
        Build a QMP input object from a user provided command-line in the
        following format:

            < command-name > [ arg-name1=arg1 ] ... [ arg-nameN=argN ]
        """
        cmdargs = cmdline.split()
        qmpcmd = { 'execute': cmdargs[0], 'arguments': {} }
        for arg in cmdargs[1:]:
            opt = arg.split('=')
            try:
                if(len(opt) > 2):
                    opt[1] = '='.join(opt[1:])
                value = int(opt[1])
            except ValueError:
                if opt[1] == 'true':
                    value = True
                elif opt[1] == 'false':
                    value = False
                elif opt[1].startswith('{'):
                    value = json.loads(opt[1])
                else:
                    value = opt[1]
            optpath = opt[0].split('.')
            parent = qmpcmd['arguments']
            curpath = []
            for p in optpath[:-1]:
                curpath.append(p)
                d = parent.get(p, {})
                if type(d) is not dict:
                    raise QMPShellError('Cannot use "%s" as both leaf and non-leaf key' % '.'.join(curpath))
                parent[p] = d
                parent = d
            if optpath[-1] in parent:
                if type(parent[optpath[-1]]) is dict:
                    raise QMPShellError('Cannot use "%s" as both leaf and non-leaf key' % '.'.join(curpath))
                else:
                    raise QMPShellError('Cannot set "%s" multiple times' % opt[0])
            parent[optpath[-1]] = value
        return qmpcmd

    def _execute_cmd(self, cmdline):
        try:
            qmpcmd = self.__build_cmd(cmdline)
        except Exception, e:
            print 'Error while parsing command line: %s' % e
            print 'command format: <command-name> ',
            print '[arg-name1=arg1] ... [arg-nameN=argN]'
            return True
        resp = self.cmd_obj(qmpcmd)
        if resp is None:
            print 'Disconnected'
            return False

        if self._pp is not None:
            self._pp.pprint(resp)
        else:
            print resp
        return True

    def connect(self):
        self._greeting = qmp.QEMUMonitorProtocol.connect(self)
        self.__completer_setup()

    def show_banner(self, msg='Welcome to the QMP low-level shell!'):
        print msg
        version = self._greeting['QMP']['version']['qemu']
        print 'Connected to QEMU %d.%d.%d\n' % (version['major'],version['minor'],version['micro'])

    def read_exec_command(self, prompt):
        """
        Read and execute a command.

        @return True if execution was ok, return False if disconnected.
        """
        try:
            cmdline = raw_input(prompt)
        except EOFError:
            print
            return False
        if cmdline == '':
            for ev in self.get_events():
                print ev
            self.clear_events()
            return True
        else:
            return self._execute_cmd(cmdline)

class HMPShell(QMPShell):
    def __init__(self, address):
        QMPShell.__init__(self, address)
        self.__cpu_index = 0

    def __cmd_completion(self):
        for cmd in self.__cmd_passthrough('help')['return'].split('\r\n'):
            if cmd and cmd[0] != '[' and cmd[0] != '\t':
                name = cmd.split()[0] # drop help text
                if name == 'info':
                    continue
                if name.find('|') != -1:
                    # Command in the form 'foobar|f' or 'f|foobar', take the
                    # full name
                    opt = name.split('|')
                    if len(opt[0]) == 1:
                        name = opt[1]
                    else:
                        name = opt[0]
                self._completer.append(name)
                self._completer.append('help ' + name) # help completion

    def __info_completion(self):
        for cmd in self.__cmd_passthrough('info')['return'].split('\r\n'):
            if cmd:
                self._completer.append('info ' + cmd.split()[1])

    def __other_completion(self):
        # special cases
        self._completer.append('help info')

    def _fill_completion(self):
        self.__cmd_completion()
        self.__info_completion()
        self.__other_completion()

    def __cmd_passthrough(self, cmdline, cpu_index = 0):
        return self.cmd_obj({ 'execute': 'human-monitor-command', 'arguments':
                              { 'command-line': cmdline,
                                'cpu-index': cpu_index } })

    def _execute_cmd(self, cmdline):
        if cmdline.split()[0] == "cpu":
            # trap the cpu command, it requires special setting
            try:
                idx = int(cmdline.split()[1])
                if not 'return' in self.__cmd_passthrough('info version', idx):
                    print 'bad CPU index'
                    return True
                self.__cpu_index = idx
            except ValueError:
                print 'cpu command takes an integer argument'
                return True
        resp = self.__cmd_passthrough(cmdline, self.__cpu_index)
        if resp is None:
            print 'Disconnected'
            return False
        assert 'return' in resp or 'error' in resp
        if 'return' in resp:
            # Success
            if len(resp['return']) > 0:
                print resp['return'],
        else:
            # Error
            print '%s: %s' % (resp['error']['class'], resp['error']['desc'])
        return True

    def show_banner(self):
        QMPShell.show_banner(self, msg='Welcome to the HMP shell!')

def die(msg):
    sys.stderr.write('ERROR: %s\n' % msg)
    sys.exit(1)

def fail_cmdline(option=None):
    if option:
        sys.stderr.write('ERROR: bad command-line option \'%s\'\n' % option)
    sys.stderr.write('qemu-shell [ -p ] [ -H ] < UNIX socket path> | < TCP address:port >\n')
    sys.exit(1)

def main():
    addr = ''
    qemu = None
    hmp = False
    pp = None

    try:
        for arg in sys.argv[1:]:
            if arg == "-H":
                if qemu is not None:
                    fail_cmdline(arg)
                hmp = True
            elif arg == "-p":
                if pp is not None:
                    fail_cmdline(arg)
                pp = pprint.PrettyPrinter(indent=4)
            else:
                if qemu is not None:
                    fail_cmdline(arg)
                if hmp:
                    qemu = HMPShell(arg)
                else:
                    qemu = QMPShell(arg, pp)
                addr = arg

        if qemu is None:
            fail_cmdline()
    except QMPShellBadPort:
        die('bad port number in command-line')

    try:
        qemu.connect()
    except qmp.QMPConnectError:
        die('Didn\'t get QMP greeting message')
    except qmp.QMPCapabilitiesError:
        die('Could not negotiate capabilities')
    except qemu.error:
        die('Could not connect to %s' % addr)

    qemu.show_banner()
    while qemu.read_exec_command('(QEMU) '):
        pass
    qemu.close()

if __name__ == '__main__':
    main()
