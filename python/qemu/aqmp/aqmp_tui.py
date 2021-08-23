# Copyright (c) 2021
#
# Authors:
#  Niteesh Babu G S <niteesh.gs@gmail.com>
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later.  See the COPYING file in the top-level directory.
"""
AQMP TUI

AQMP TUI is an asynchronous interface built on top the of the AQMP library.
It is the successor of QMP-shell and is bought-in as a replacement for it.

Example Usage: aqmp-tui <SOCKET | TCP IP:PORT>
Full Usage: aqmp-tui --help
"""

import argparse
import asyncio
import json
import logging
from logging import Handler, LogRecord
import signal
from typing import (
    List,
    Optional,
    Tuple,
    Type,
    Union,
    cast,
)

from pygments import lexers
from pygments import token as Token
import urwid
import urwid_readline

from ..qmp import QEMUMonitorProtocol, QMPBadPortError
from .error import ProtocolError
from .message import DeserializationError, Message, UnexpectedTypeError
from .protocol import ConnectError, Runstate
from .qmp_client import ExecInterruptedError, QMPClient
from .util import create_task, pretty_traceback


# The name of the signal that is used to update the history list
UPDATE_MSG: str = 'UPDATE_MSG'


palette = [
    (Token.Punctuation, '', '', '', 'h15,bold', 'g7'),
    (Token.Text, '', '', '', '', 'g7'),
    (Token.Name.Tag, '', '', '', 'bold,#f88', 'g7'),
    (Token.Literal.Number.Integer, '', '', '', '#fa0', 'g7'),
    (Token.Literal.String.Double, '', '', '', '#6f6', 'g7'),
    (Token.Keyword.Constant, '', '', '', '#6af', 'g7'),
    ('DEBUG', '', '', '', '#ddf', 'g7'),
    ('INFO', '', '', '', 'g100', 'g7'),
    ('WARNING', '', '', '', '#ff6', 'g7'),
    ('ERROR', '', '', '', '#a00', 'g7'),
    ('CRITICAL', '', '', '', '#a00', 'g7'),
    ('background', '', 'black', '', '', 'g7'),
]


def format_json(msg: str) -> str:
    """
    Formats valid/invalid multi-line JSON message into a single-line message.

    Formatting is first tried using the standard json module. If that fails
    due to an decoding error then a simple string manipulation is done to
    achieve a single line JSON string.

    Converting into single line is more asthetically pleasing when looking
    along with error messages.

    Eg:
    Input:
          [ 1,
            true,
            3 ]
    The above input is not a valid QMP message and produces the following error
    "QMP message is not a JSON object."
    When displaying this in TUI in multiline mode we get

        [ 1,
          true,
          3 ]: QMP message is not a JSON object.

    whereas in singleline mode we get the following

        [1, true, 3]: QMP message is not a JSON object.

    The single line mode is more asthetically pleasing.

    :param msg:
        The message to formatted into single line.

    :return: Formatted singleline message.
    """
    try:
        msg = json.loads(msg)
        return str(json.dumps(msg))
    except json.decoder.JSONDecodeError:
        msg = msg.replace('\n', '')
        words = msg.split(' ')
        words = list(filter(None, words))
        return ' '.join(words)


def has_handler_type(logger: logging.Logger,
                     handler_type: Type[Handler]) -> bool:
    """
    The Logger class has no interface to check if a certain type of handler is
    installed or not. So we provide an interface to do so.

    :param logger:
        Logger object
    :param handler_type:
        The type of the handler to be checked.

    :return: returns True if handler of type `handler_type`.
    """
    for handler in logger.handlers:
        if isinstance(handler, handler_type):
            return True
    return False


class App(QMPClient):
    """
    Implements the AQMP TUI.

    Initializes the widgets and starts the urwid event loop.

    :param address:
        Address of the server to connect to.
    :param num_retries:
        The number of times to retry before stopping to reconnect.
    :param retry_delay:
        The delay(sec) before each retry
    """
    def __init__(self, address: Union[str, Tuple[str, int]], num_retries: int,
                 retry_delay: Optional[int]) -> None:
        urwid.register_signal(type(self), UPDATE_MSG)
        self.window = Window(self)
        self.address = address
        self.aloop: Optional[asyncio.AbstractEventLoop] = None
        self.num_retries = num_retries
        self.retry_delay = retry_delay if retry_delay else 2
        self.retry: bool = False
        self.exiting: bool = False
        super().__init__()

    def add_to_history(self, msg: str, level: Optional[str] = None) -> None:
        """
        Appends the msg to the history list.

        :param msg:
            The raw message to be appended in string type.
        """
        urwid.emit_signal(self, UPDATE_MSG, msg, level)

    def _cb_outbound(self, msg: Message) -> Message:
        """
        Callback: outbound message hook.

        Appends the outgoing messages to the history box.

        :param msg: raw outbound message.
        :return: final outbound message.
        """
        str_msg = str(msg)

        if not has_handler_type(logging.getLogger(), TUILogHandler):
            logging.debug('Request: %s', str_msg)
        self.add_to_history('<-- ' + str_msg)
        return msg

    def _cb_inbound(self, msg: Message) -> Message:
        """
        Callback: outbound message hook.

        Appends the incoming messages to the history box.

        :param msg: raw inbound message.
        :return: final inbound message.
        """
        str_msg = str(msg)

        if not has_handler_type(logging.getLogger(), TUILogHandler):
            logging.debug('Request: %s', str_msg)
        self.add_to_history('--> ' + str_msg)
        return msg

    async def _send_to_server(self, msg: Message) -> None:
        """
        This coroutine sends the message to the server.
        The message has to be pre-validated.

        :param msg:
            Pre-validated message to be to sent to the server.

        :raise Exception: When an unhandled exception is caught.
        """
        try:
            await self._raw(msg, assign_id='id' not in msg)
        except ExecInterruptedError as err:
            logging.info('Error server disconnected before reply %s', str(err))
            self.add_to_history('Server disconnected before reply', 'ERROR')
        except Exception as err:
            logging.error('Exception from _send_to_server: %s', str(err))
            raise err

    def cb_send_to_server(self, raw_msg: str) -> None:
        """
        Validates and sends the message to the server.
        The raw string message is first converted into a Message object
        and is then sent to the server.

        :param raw_msg:
            The raw string message to be sent to the server.

        :raise Exception: When an unhandled exception is caught.
        """
        try:
            msg = Message(bytes(raw_msg, encoding='utf-8'))
            create_task(self._send_to_server(msg))
        except (DeserializationError, UnexpectedTypeError) as err:
            raw_msg = format_json(raw_msg)
            logging.info('Invalid message: %s', err.error_message)
            self.add_to_history(f'{raw_msg}: {err.error_message}', 'ERROR')

    def unhandled_input(self, key: str) -> None:
        """
        Handle's keys which haven't been handled by the child widgets.

        :param key:
            Unhandled key
        """
        if key == 'esc':
            self.kill_app()

    def kill_app(self) -> None:
        """
        Initiates killing of app. A bridge between asynchronous and synchronous
        code.
        """
        create_task(self._kill_app())

    async def _kill_app(self) -> None:
        """
        This coroutine initiates the actual disconnect process and calls
        urwid.ExitMainLoop() to kill the TUI.

        :raise Exception: When an unhandled exception is caught.
        """
        self.exiting = True
        await self.disconnect()
        logging.debug('Disconnect finished. Exiting app')
        raise urwid.ExitMainLoop()

    async def disconnect(self) -> None:
        """
        Overrides the disconnect method to handle the errors locally.
        """
        try:
            await super().disconnect()
        except (OSError, EOFError) as err:
            logging.info('disconnect: %s', str(err))
            self.retry = True
        except ProtocolError as err:
            logging.info('disconnect: %s', str(err))
        except Exception as err:
            logging.error('disconnect: Unhandled exception %s', str(err))
            raise err

    def _set_status(self, msg: str) -> None:
        """
        Sets the message as the status.

        :param msg:
            The message to be displayed in the status bar.
        """
        self.window.footer.set_text(msg)

    def _get_formatted_address(self) -> str:
        """
        Returns a formatted version of the server's address.

        :return: formatted address
        """
        if isinstance(self.address, tuple):
            host, port = self.address
            addr = f'{host}:{port}'
        else:
            addr = f'{self.address}'
        return addr

    async def _initiate_connection(self) -> Optional[ConnectError]:
        """
        Tries connecting to a server a number of times with a delay between
        each try. If all retries failed then return the error faced during
        the last retry.

        :return: Error faced during last retry.
        """
        current_retries = 0
        err = None

        # initial try
        await self.connect_server()
        while self.retry and current_retries < self.num_retries:
            logging.info('Connection Failed, retrying in %d', self.retry_delay)
            status = f'[Retry #{current_retries} ({self.retry_delay}s)]'
            self._set_status(status)

            await asyncio.sleep(self.retry_delay)

            err = await self.connect_server()
            current_retries += 1
        # If all retries failed report the last error
        if err:
            logging.info('All retries failed: %s', err)
            return err
        return None

    async def manage_connection(self) -> None:
        """
        Manage the connection based on the current run state.

        A reconnect is issued when the current state is IDLE and the number
        of retries is not exhausted.
        A disconnect is issued when the current state is DISCONNECTING.
        """
        while not self.exiting:
            if self.runstate == Runstate.IDLE:
                err = await self._initiate_connection()
                # If retry is still true then, we have exhausted all our tries.
                if err:
                    self._set_status(f'[Error: {err.error_message}]')
                else:
                    addr = self._get_formatted_address()
                    self._set_status(f'[Connected {addr}]')
            elif self.runstate == Runstate.DISCONNECTING:
                self._set_status('[Disconnected]')
                await self.disconnect()
                # check if a retry is needed
                if self.runstate == Runstate.IDLE:
                    continue
            await self.runstate_changed()

    async def connect_server(self) -> Optional[ConnectError]:
        """
        Initiates a connection to the server at address `self.address`
        and in case of a failure, sets the status to the respective error.
        """
        try:
            await self.connect(self.address)
            self.retry = False
        except ConnectError as err:
            logging.info('connect_server: ConnectError %s', str(err))
            self.retry = True
            return err
        return None

    def run(self, debug: bool = False) -> None:
        """
        Starts the long running co-routines and the urwid event loop.

        :param debug:
            Enables/Disables asyncio event loop debugging
        """
        screen = urwid.raw_display.Screen()
        screen.set_terminal_properties(256)

        self.aloop = asyncio.get_event_loop()
        self.aloop.set_debug(debug)

        # Gracefully handle SIGTERM and SIGINT signals
        cancel_signals = [signal.SIGTERM, signal.SIGINT]
        for sig in cancel_signals:
            self.aloop.add_signal_handler(sig, self.kill_app)

        event_loop = urwid.AsyncioEventLoop(loop=self.aloop)
        main_loop = urwid.MainLoop(urwid.AttrMap(self.window, 'background'),
                                   unhandled_input=self.unhandled_input,
                                   screen=screen,
                                   palette=palette,
                                   handle_mouse=True,
                                   event_loop=event_loop)

        create_task(self.manage_connection(), self.aloop)
        try:
            main_loop.run()
        except Exception as err:
            logging.error('%s\n%s\n', str(err), pretty_traceback())
            raise err


class StatusBar(urwid.Text):
    """
    A simple statusbar modelled using the Text widget. The status can be
    set using the set_text function. All text set is aligned to right.

    :param text: Initial text to be displayed. Default is empty str.
    """
    def __init__(self, text: str = ''):
        super().__init__(text, align='right')


class Editor(urwid_readline.ReadlineEdit):
    """
    A simple editor modelled using the urwid_readline.ReadlineEdit widget.
    Mimcs GNU readline shortcuts and provides history support.

    The readline shortcuts can be found below:
    https://github.com/rr-/urwid_readline#features

    Along with the readline features, this editor also has support for
    history. Pressing the 'up'/'down' switches between the prev/next messages
    available in the history.

    Currently there is no support to save the history to a file. The history of
    previous commands is lost on exit.

    :param parent: Reference to the TUI object.
    """
    def __init__(self, parent: App) -> None:
        super().__init__(caption='> ', multiline=True)
        self.parent = parent
        self.history: List[str] = []
        self.last_index: int = -1
        self.show_history: bool = False

    def keypress(self, size: Tuple[int, int], key: str) -> Optional[str]:
        """
        Handles the keypress on this widget.

        :param size:
            The current size of the widget.
        :param key:
            The key to be handled.

        :return: Unhandled key if any.
        """
        msg = self.get_edit_text()
        if key == 'up' and not msg:
            # Show the history when 'up arrow' is pressed with no input text.
            # NOTE: The show_history logic is necessary because in 'multiline'
            # mode (which we use) 'up arrow' is used to move between lines.
            if not self.history:
                return None
            self.show_history = True
            last_msg = self.history[self.last_index]
            self.set_edit_text(last_msg)
            self.edit_pos = len(last_msg)
        elif key == 'up' and self.show_history:
            self.last_index = max(self.last_index - 1, -len(self.history))
            self.set_edit_text(self.history[self.last_index])
            self.edit_pos = len(self.history[self.last_index])
        elif key == 'down' and self.show_history:
            if self.last_index == -1:
                self.set_edit_text('')
                self.show_history = False
            else:
                self.last_index += 1
                self.set_edit_text(self.history[self.last_index])
                self.edit_pos = len(self.history[self.last_index])
        elif key == 'meta enter':
            # When using multiline, enter inserts a new line into the editor
            # send the input to the server on alt + enter
            self.parent.cb_send_to_server(msg)
            self.history.append(msg)
            self.set_edit_text('')
            self.last_index = -1
            self.show_history = False
        else:
            self.show_history = False
            self.last_index = -1
            return cast(Optional[str], super().keypress(size, key))
        return None


class EditorWidget(urwid.Filler):
    """
    Wrapper around the editor widget.

    The Editor is a flow widget and has to wrapped inside a box widget.
    This class wraps the Editor inside filler widget.

    :param parent: Reference to the TUI object.
    """
    def __init__(self, parent: App) -> None:
        super().__init__(Editor(parent), valign='top')


class HistoryBox(urwid.ListBox):
    """
    This widget is modelled using the ListBox widget, contains the list of
    all messages both QMP messages and log messsages to be shown in the TUI.

    The messages are urwid.Text widgets. On every append of a message, the
    focus is shifted to the last appended message.

    :param parent: Reference to the TUI object.
    """
    def __init__(self, parent: App) -> None:
        self.parent = parent
        self.history = urwid.SimpleFocusListWalker([])
        super().__init__(self.history)

    def add_to_history(self,
                       history: Union[str, List[Tuple[str, str]]]) -> None:
        """
        Appends a message to the list and set the focus to the last appended
        message.

        :param history:
            The history item(message/event) to be appended to the list.
        """
        self.history.append(urwid.Text(history))
        self.history.set_focus(len(self.history) - 1)

    def mouse_event(self, size: Tuple[int, int], _event: str, button: float,
                    _x: int, _y: int, focus: bool) -> None:
        # Unfortunately there are no urwid constants that represent the mouse
        # events.
        if button == 4:  # Scroll up event
            super().keypress(size, 'up')
        elif button == 5:  # Scroll down event
            super().keypress(size, 'down')


class HistoryWindow(urwid.Frame):
    """
    This window composes the HistoryBox and EditorWidget in a horizontal split.
    By default the first focus is given to the history box.

    :param parent: Reference to the TUI object.
    """
    def __init__(self, parent: App) -> None:
        self.parent = parent
        self.editor_widget = EditorWidget(parent)
        self.editor = urwid.LineBox(self.editor_widget)
        self.history = HistoryBox(parent)
        self.body = urwid.Pile([('weight', 80, self.history),
                                ('weight', 20, self.editor)])
        super().__init__(self.body)
        urwid.connect_signal(self.parent, UPDATE_MSG, self.cb_add_to_history)

    def cb_add_to_history(self, msg: str, level: Optional[str] = None) -> None:
        """
        Appends a message to the history box

        :param msg:
            The message to be appended to the history box.
        :param level:
            The log level of the message, if it is a log message.
        """
        formatted = []
        if level:
            msg = f'[{level}]: {msg}'
            formatted.append((level, msg))
        else:
            lexer = lexers.JsonLexer()  # pylint: disable=no-member
            for token in lexer.get_tokens(msg):
                formatted.append(token)
        self.history.add_to_history(formatted)


class Window(urwid.Frame):
    """
    This window is the top most widget of the TUI and will contain other
    windows. Each child of this widget is responsible for displaying a specific
    functionality.

    :param parent: Reference to the TUI object.
    """
    def __init__(self, parent: App) -> None:
        self.parent = parent
        footer = StatusBar()
        body = HistoryWindow(parent)
        super().__init__(body, footer=footer)


class TUILogHandler(Handler):
    """
    This handler routes all the log messages to the TUI screen.
    It is installed to the root logger to so that the log message from all
    libraries begin used is routed to the screen.

    :param tui: Reference to the TUI object.
    """
    def __init__(self, tui: App) -> None:
        super().__init__()
        self.tui = tui

    def emit(self, record: LogRecord) -> None:
        """
        Emits a record to the TUI screen.

        Appends the log message to the TUI screen
        """
        level = record.levelname
        msg = record.getMessage()
        self.tui.add_to_history(msg, level)


def main() -> None:
    """
    Driver of the whole script, parses arguments, initialize the TUI and
    the logger.
    """
    parser = argparse.ArgumentParser(description='AQMP TUI')
    parser.add_argument('qmp_server', help='Address of the QMP server. '
                        'Format <UNIX socket path | TCP addr:port>')
    parser.add_argument('--num-retries', type=int, default=10,
                        help='Number of times to reconnect before giving up.')
    parser.add_argument('--retry-delay', type=int,
                        help='Time(s) to wait before next retry. '
                        'Default action is to wait 2s between each retry.')
    parser.add_argument('--log-file', help='The Log file name')
    parser.add_argument('--log-level', default='WARNING',
                        help='Log level <CRITICAL|ERROR|WARNING|INFO|DEBUG|>')
    parser.add_argument('--asyncio-debug', action='store_true',
                        help='Enable debug mode for asyncio loop. '
                        'Generates lot of output, makes TUI unusable when '
                        'logs are logged in the TUI. '
                        'Use only when logging to a file.')
    args = parser.parse_args()

    try:
        address = QEMUMonitorProtocol.parse_address(args.qmp_server)
    except QMPBadPortError as err:
        parser.error(str(err))

    app = App(address, args.num_retries, args.retry_delay)

    root_logger = logging.getLogger()
    root_logger.setLevel(logging.getLevelName(args.log_level))

    if args.log_file:
        root_logger.addHandler(logging.FileHandler(args.log_file))
    else:
        root_logger.addHandler(TUILogHandler(app))

    app.run(args.asyncio_debug)


if __name__ == '__main__':
    main()
