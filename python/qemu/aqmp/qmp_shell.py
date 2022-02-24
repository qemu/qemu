#
# Copyright (C) 2009, 2010 Red Hat Inc.
#
# Authors:
#  Luiz Capitulino <lcapitulino@redhat.com>
#
# This work is licensed under the terms of the GNU GPL, version 2.  See
# the COPYING file in the top-level directory.
#

"""
Low-level QEMU shell on top of QMP.

usage: qmp-shell [-h] [-H] [-N] [-v] [-p] qmp_server

positional arguments:
  qmp_server            < UNIX socket path | TCP address:port >

optional arguments:
  -h, --help            show this help message and exit
  -H, --hmp             Use HMP interface
  -N, --skip-negotiation
                        Skip negotiate (for qemu-ga)
  -v, --verbose         Verbose (echo commands sent and received)
  -p, --pretty          Pretty-print JSON


Start QEMU with:

# qemu [...] -qmp unix:./qmp-sock,server

Run the shell:

$ qmp-shell ./qmp-sock

Commands have the following format:

   < command-name > [ arg-name1=arg1 ] ... [ arg-nameN=argN ]

For example:

(QEMU) device_add driver=e1000 id=net1
{'return': {}}
(QEMU)

key=value pairs also support Python or JSON object literal subset notations,
without spaces. Dictionaries/objects {} are supported as are arrays [].

   example-command arg-name1={'key':'value','obj'={'prop':"value"}}

Both JSON and Python formatting should work, including both styles of
string literal quotes. Both paradigms of literal values should work,
including null/true/false for JSON and None/True/False for Python.


Transactions have the following multi-line format:

   transaction(
   action-name1 [ arg-name1=arg1 ] ... [arg-nameN=argN ]
   ...
   action-nameN [ arg-name1=arg1 ] ... [arg-nameN=argN ]
   )

One line transactions are also supported:

   transaction( action-name1 ... )

For example:

    (QEMU) transaction(
    TRANS> block-dirty-bitmap-add node=drive0 name=bitmap1
    TRANS> block-dirty-bitmap-clear node=drive0 name=bitmap0
    TRANS> )
    {"return": {}}
    (QEMU)

Use the -v and -p options to activate the verbose and pretty-print options,
which will echo back the properly formatted JSON-compliant QMP that is being
sent to QEMU, which is useful for debugging and documentation generation.
"""

import argparse
import ast
import json
import logging
import os
import re
import readline
from subprocess import Popen
import sys
from typing import (
    IO,
    Iterator,
    List,
    NoReturn,
    Optional,
    Sequence,
)

from qemu.aqmp import ConnectError, QMPError, SocketAddrT
from qemu.aqmp.legacy import (
    QEMUMonitorProtocol,
    QMPBadPortError,
    QMPMessage,
    QMPObject,
)


LOG = logging.getLogger(__name__)


class QMPCompleter:
    """
    QMPCompleter provides a readline library tab-complete behavior.
    """
    # NB: Python 3.9+ will probably allow us to subclass list[str] directly,
    # but pylint as of today does not know that List[str] is simply 'list'.
    def __init__(self) -> None:
        self._matches: List[str] = []

    def append(self, value: str) -> None:
        """Append a new valid completion to the list of possibilities."""
        return self._matches.append(value)

    def complete(self, text: str, state: int) -> Optional[str]:
        """readline.set_completer() callback implementation."""
        for cmd in self._matches:
            if cmd.startswith(text):
                if state == 0:
                    return cmd
                state -= 1
        return None


class QMPShellError(QMPError):
    """
    QMP Shell Base error class.
    """


class FuzzyJSON(ast.NodeTransformer):
    """
    This extension of ast.NodeTransformer filters literal "true/false/null"
    values in a Python AST and replaces them by proper "True/False/None" values
    that Python can properly evaluate.
    """

    @classmethod
    def visit_Name(cls,  # pylint: disable=invalid-name
                   node: ast.Name) -> ast.AST:
        """
        Transform Name nodes with certain values into Constant (keyword) nodes.
        """
        if node.id == 'true':
            return ast.Constant(value=True)
        if node.id == 'false':
            return ast.Constant(value=False)
        if node.id == 'null':
            return ast.Constant(value=None)
        return node


class QMPShell(QEMUMonitorProtocol):
    """
    QMPShell provides a basic readline-based QMP shell.

    :param address: Address of the QMP server.
    :param pretty: Pretty-print QMP messages.
    :param verbose: Echo outgoing QMP messages to console.
    """
    def __init__(self, address: SocketAddrT,
                 pretty: bool = False,
                 verbose: bool = False,
                 server: bool = False,
                 logfile: Optional[str] = None):
        super().__init__(address, server=server)
        self._greeting: Optional[QMPMessage] = None
        self._completer = QMPCompleter()
        self._transmode = False
        self._actions: List[QMPMessage] = []
        self._histfile = os.path.join(os.path.expanduser('~'),
                                      '.qmp-shell_history')
        self.pretty = pretty
        self.verbose = verbose
        self.logfile = None

        if logfile is not None:
            self.logfile = open(logfile, "w", encoding='utf-8')

    def close(self) -> None:
        # Hook into context manager of parent to save shell history.
        self._save_history()
        super().close()

    def _fill_completion(self) -> None:
        cmds = self.cmd('query-commands')
        if 'error' in cmds:
            return
        for cmd in cmds['return']:
            self._completer.append(cmd['name'])

    def _completer_setup(self) -> None:
        self._completer = QMPCompleter()
        self._fill_completion()
        readline.set_history_length(1024)
        readline.set_completer(self._completer.complete)
        readline.parse_and_bind("tab: complete")
        # NB: default delimiters conflict with some command names
        # (eg. query-), clearing everything as it doesn't seem to matter
        readline.set_completer_delims('')
        try:
            readline.read_history_file(self._histfile)
        except FileNotFoundError:
            pass
        except IOError as err:
            msg = f"Failed to read history '{self._histfile}': {err!s}"
            LOG.warning(msg)

    def _save_history(self) -> None:
        try:
            readline.write_history_file(self._histfile)
        except IOError as err:
            msg = f"Failed to save history file '{self._histfile}': {err!s}"
            LOG.warning(msg)

    @classmethod
    def _parse_value(cls, val: str) -> object:
        try:
            return int(val)
        except ValueError:
            pass

        if val.lower() == 'true':
            return True
        if val.lower() == 'false':
            return False
        if val.startswith(('{', '[')):
            # Try first as pure JSON:
            try:
                return json.loads(val)
            except ValueError:
                pass
            # Try once again as FuzzyJSON:
            try:
                tree = ast.parse(val, mode='eval')
                transformed = FuzzyJSON().visit(tree)
                return ast.literal_eval(transformed)
            except (SyntaxError, ValueError):
                pass
        return val

    def _cli_expr(self,
                  tokens: Sequence[str],
                  parent: QMPObject) -> None:
        for arg in tokens:
            (key, sep, val) = arg.partition('=')
            if sep != '=':
                raise QMPShellError(
                    f"Expected a key=value pair, got '{arg!s}'"
                )

            value = self._parse_value(val)
            optpath = key.split('.')
            curpath = []
            for path in optpath[:-1]:
                curpath.append(path)
                obj = parent.get(path, {})
                if not isinstance(obj, dict):
                    msg = 'Cannot use "{:s}" as both leaf and non-leaf key'
                    raise QMPShellError(msg.format('.'.join(curpath)))
                parent[path] = obj
                parent = obj
            if optpath[-1] in parent:
                if isinstance(parent[optpath[-1]], dict):
                    msg = 'Cannot use "{:s}" as both leaf and non-leaf key'
                    raise QMPShellError(msg.format('.'.join(curpath)))
                raise QMPShellError(f'Cannot set "{key}" multiple times')
            parent[optpath[-1]] = value

    def _build_cmd(self, cmdline: str) -> Optional[QMPMessage]:
        """
        Build a QMP input object from a user provided command-line in the
        following format:

            < command-name > [ arg-name1=arg1 ] ... [ arg-nameN=argN ]
        """
        argument_regex = r'''(?:[^\s"']|"(?:\\.|[^"])*"|'(?:\\.|[^'])*')+'''
        cmdargs = re.findall(argument_regex, cmdline)
        qmpcmd: QMPMessage

        # Transactional CLI entry:
        if cmdargs and cmdargs[0] == 'transaction(':
            self._transmode = True
            self._actions = []
            cmdargs.pop(0)

        # Transactional CLI exit:
        if cmdargs and cmdargs[0] == ')' and self._transmode:
            self._transmode = False
            if len(cmdargs) > 1:
                msg = 'Unexpected input after close of Transaction sub-shell'
                raise QMPShellError(msg)
            qmpcmd = {
                'execute': 'transaction',
                'arguments': {'actions': self._actions}
            }
            return qmpcmd

        # No args, or no args remaining
        if not cmdargs:
            return None

        if self._transmode:
            # Parse and cache this Transactional Action
            finalize = False
            action = {'type': cmdargs[0], 'data': {}}
            if cmdargs[-1] == ')':
                cmdargs.pop(-1)
                finalize = True
            self._cli_expr(cmdargs[1:], action['data'])
            self._actions.append(action)
            return self._build_cmd(')') if finalize else None

        # Standard command: parse and return it to be executed.
        qmpcmd = {'execute': cmdargs[0], 'arguments': {}}
        self._cli_expr(cmdargs[1:], qmpcmd['arguments'])
        return qmpcmd

    def _print(self, qmp_message: object, fh: IO[str] = sys.stdout) -> None:
        jsobj = json.dumps(qmp_message,
                           indent=4 if self.pretty else None,
                           sort_keys=self.pretty)
        print(str(jsobj), file=fh)

    def _execute_cmd(self, cmdline: str) -> bool:
        try:
            qmpcmd = self._build_cmd(cmdline)
        except QMPShellError as err:
            print(
                f"Error while parsing command line: {err!s}\n"
                "command format: <command-name> "
                "[arg-name1=arg1] ... [arg-nameN=argN",
                file=sys.stderr
            )
            return True
        # For transaction mode, we may have just cached the action:
        if qmpcmd is None:
            return True
        if self.verbose:
            self._print(qmpcmd)
        resp = self.cmd_obj(qmpcmd)
        if resp is None:
            print('Disconnected')
            return False
        self._print(resp)
        if self.logfile is not None:
            cmd = {**qmpcmd, **resp}
            self._print(cmd, fh=self.logfile)
        return True

    def connect(self, negotiate: bool = True) -> None:
        self._greeting = super().connect(negotiate)
        self._completer_setup()

    def show_banner(self,
                    msg: str = 'Welcome to the QMP low-level shell!') -> None:
        """
        Print to stdio a greeting, and the QEMU version if available.
        """
        print(msg)
        if not self._greeting:
            print('Connected')
            return
        version = self._greeting['QMP']['version']['qemu']
        print("Connected to QEMU {major}.{minor}.{micro}\n".format(**version))

    @property
    def prompt(self) -> str:
        """
        Return the current shell prompt, including a trailing space.
        """
        if self._transmode:
            return 'TRANS> '
        return '(QEMU) '

    def read_exec_command(self) -> bool:
        """
        Read and execute a command.

        @return True if execution was ok, return False if disconnected.
        """
        try:
            cmdline = input(self.prompt)
        except EOFError:
            print()
            return False

        if cmdline == '':
            for event in self.get_events():
                print(event)
            return True

        return self._execute_cmd(cmdline)

    def repl(self) -> Iterator[None]:
        """
        Return an iterator that implements the REPL.
        """
        self.show_banner()
        while self.read_exec_command():
            yield
        self.close()


class HMPShell(QMPShell):
    """
    HMPShell provides a basic readline-based HMP shell, tunnelled via QMP.

    :param address: Address of the QMP server.
    :param pretty: Pretty-print QMP messages.
    :param verbose: Echo outgoing QMP messages to console.
    """
    def __init__(self, address: SocketAddrT,
                 pretty: bool = False,
                 verbose: bool = False,
                 server: bool = False,
                 logfile: Optional[str] = None):
        super().__init__(address, pretty, verbose, server, logfile)
        self._cpu_index = 0

    def _cmd_completion(self) -> None:
        for cmd in self._cmd_passthrough('help')['return'].split('\r\n'):
            if cmd and cmd[0] != '[' and cmd[0] != '\t':
                name = cmd.split()[0]  # drop help text
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
                self._completer.append('help ' + name)  # help completion

    def _info_completion(self) -> None:
        for cmd in self._cmd_passthrough('info')['return'].split('\r\n'):
            if cmd:
                self._completer.append('info ' + cmd.split()[1])

    def _other_completion(self) -> None:
        # special cases
        self._completer.append('help info')

    def _fill_completion(self) -> None:
        self._cmd_completion()
        self._info_completion()
        self._other_completion()

    def _cmd_passthrough(self, cmdline: str,
                         cpu_index: int = 0) -> QMPMessage:
        return self.cmd_obj({
            'execute': 'human-monitor-command',
            'arguments': {
                'command-line': cmdline,
                'cpu-index': cpu_index
            }
        })

    def _execute_cmd(self, cmdline: str) -> bool:
        if cmdline.split()[0] == "cpu":
            # trap the cpu command, it requires special setting
            try:
                idx = int(cmdline.split()[1])
                if 'return' not in self._cmd_passthrough('info version', idx):
                    print('bad CPU index')
                    return True
                self._cpu_index = idx
            except ValueError:
                print('cpu command takes an integer argument')
                return True
        resp = self._cmd_passthrough(cmdline, self._cpu_index)
        if resp is None:
            print('Disconnected')
            return False
        assert 'return' in resp or 'error' in resp
        if 'return' in resp:
            # Success
            if len(resp['return']) > 0:
                print(resp['return'], end=' ')
        else:
            # Error
            print('%s: %s' % (resp['error']['class'], resp['error']['desc']))
        return True

    def show_banner(self, msg: str = 'Welcome to the HMP shell!') -> None:
        QMPShell.show_banner(self, msg)


def die(msg: str) -> NoReturn:
    """Write an error to stderr, then exit with a return code of 1."""
    sys.stderr.write('ERROR: %s\n' % msg)
    sys.exit(1)


def main() -> None:
    """
    qmp-shell entry point: parse command line arguments and start the REPL.
    """
    parser = argparse.ArgumentParser()
    parser.add_argument('-H', '--hmp', action='store_true',
                        help='Use HMP interface')
    parser.add_argument('-N', '--skip-negotiation', action='store_true',
                        help='Skip negotiate (for qemu-ga)')
    parser.add_argument('-v', '--verbose', action='store_true',
                        help='Verbose (echo commands sent and received)')
    parser.add_argument('-p', '--pretty', action='store_true',
                        help='Pretty-print JSON')
    parser.add_argument('-l', '--logfile',
                        help='Save log of all QMP messages to PATH')

    default_server = os.environ.get('QMP_SOCKET')
    parser.add_argument('qmp_server', action='store',
                        default=default_server,
                        help='< UNIX socket path | TCP address:port >')

    args = parser.parse_args()
    if args.qmp_server is None:
        parser.error("QMP socket or TCP address must be specified")

    shell_class = HMPShell if args.hmp else QMPShell

    try:
        address = shell_class.parse_address(args.qmp_server)
    except QMPBadPortError:
        parser.error(f"Bad port number: {args.qmp_server}")
        return  # pycharm doesn't know error() is noreturn

    with shell_class(address, args.pretty, args.verbose, args.logfile) as qemu:
        try:
            qemu.connect(negotiate=not args.skip_negotiation)
        except ConnectError as err:
            if isinstance(err.exc, OSError):
                die(f"Couldn't connect to {args.qmp_server}: {err!s}")
            die(str(err))

        for _ in qemu.repl():
            pass


def main_wrap() -> None:
    """
    qmp-shell-wrap entry point: parse command line arguments and
    start the REPL.
    """
    parser = argparse.ArgumentParser()
    parser.add_argument('-H', '--hmp', action='store_true',
                        help='Use HMP interface')
    parser.add_argument('-v', '--verbose', action='store_true',
                        help='Verbose (echo commands sent and received)')
    parser.add_argument('-p', '--pretty', action='store_true',
                        help='Pretty-print JSON')
    parser.add_argument('-l', '--logfile',
                        help='Save log of all QMP messages to PATH')

    parser.add_argument('command', nargs=argparse.REMAINDER,
                        help='QEMU command line to invoke')

    args = parser.parse_args()

    cmd = args.command
    if len(cmd) != 0 and cmd[0] == '--':
        cmd = cmd[1:]
    if len(cmd) == 0:
        cmd = ["qemu-system-x86_64"]

    sockpath = "qmp-shell-wrap-%d" % os.getpid()
    cmd += ["-qmp", "unix:%s" % sockpath]

    shell_class = HMPShell if args.hmp else QMPShell

    try:
        address = shell_class.parse_address(sockpath)
    except QMPBadPortError:
        parser.error(f"Bad port number: {sockpath}")
        return  # pycharm doesn't know error() is noreturn

    try:
        with shell_class(address, args.pretty, args.verbose,
                         True, args.logfile) as qemu:
            with Popen(cmd):

                try:
                    qemu.accept()
                except ConnectError as err:
                    if isinstance(err.exc, OSError):
                        die(f"Couldn't connect to {args.qmp_server}: {err!s}")
                    die(str(err))

                for _ in qemu.repl():
                    pass
    finally:
        os.unlink(sockpath)


if __name__ == '__main__':
    main()
