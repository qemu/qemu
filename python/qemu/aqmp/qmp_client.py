"""
QMP Protocol Implementation

This module provides the `QMPClient` class, which can be used to connect
and send commands to a QMP server such as QEMU. The QMP class can be
used to either connect to a listening server, or used to listen and
accept an incoming connection from that server.
"""

import asyncio
import logging
import socket
import struct
from typing import (
    Dict,
    List,
    Mapping,
    Optional,
    Union,
    cast,
)

from .error import AQMPError, ProtocolError
from .events import Events
from .message import Message
from .models import ErrorResponse, Greeting
from .protocol import AsyncProtocol, Runstate, require
from .util import (
    bottom_half,
    exception_summary,
    pretty_traceback,
    upper_half,
)


class _WrappedProtocolError(ProtocolError):
    """
    Abstract exception class for Protocol errors that wrap an Exception.

    :param error_message: Human-readable string describing the error.
    :param exc: The root-cause exception.
    """
    def __init__(self, error_message: str, exc: Exception):
        super().__init__(error_message)
        self.exc = exc

    def __str__(self) -> str:
        return f"{self.error_message}: {self.exc!s}"


class GreetingError(_WrappedProtocolError):
    """
    An exception occurred during the Greeting phase.

    :param error_message: Human-readable string describing the error.
    :param exc: The root-cause exception.
    """


class NegotiationError(_WrappedProtocolError):
    """
    An exception occurred during the Negotiation phase.

    :param error_message: Human-readable string describing the error.
    :param exc: The root-cause exception.
    """


class ExecuteError(AQMPError):
    """
    Exception raised by `QMPClient.execute()` on RPC failure.

    :param error_response: The RPC error response object.
    :param sent: The sent RPC message that caused the failure.
    :param received: The raw RPC error reply received.
    """
    def __init__(self, error_response: ErrorResponse,
                 sent: Message, received: Message):
        super().__init__(error_response.error.desc)
        #: The sent `Message` that caused the failure
        self.sent: Message = sent
        #: The received `Message` that indicated failure
        self.received: Message = received
        #: The parsed error response
        self.error: ErrorResponse = error_response
        #: The QMP error class
        self.error_class: str = error_response.error.class_


class ExecInterruptedError(AQMPError):
    """
    Exception raised by `execute()` (et al) when an RPC is interrupted.

    This error is raised when an `execute()` statement could not be
    completed.  This can occur because the connection itself was
    terminated before a reply was received.

    The true cause of the interruption will be available via `disconnect()`.
    """


class _MsgProtocolError(ProtocolError):
    """
    Abstract error class for protocol errors that have a `Message` object.

    This Exception class is used for protocol errors where the `Message`
    was mechanically understood, but was found to be inappropriate or
    malformed.

    :param error_message: Human-readable string describing the error.
    :param msg: The QMP `Message` that caused the error.
    """
    def __init__(self, error_message: str, msg: Message):
        super().__init__(error_message)
        #: The received `Message` that caused the error.
        self.msg: Message = msg

    def __str__(self) -> str:
        return "\n".join([
            super().__str__(),
            f"  Message was: {str(self.msg)}\n",
        ])


class ServerParseError(_MsgProtocolError):
    """
    The Server sent a `Message` indicating parsing failure.

    i.e. A reply has arrived from the server, but it is missing the "ID"
    field, indicating a parsing error.

    :param error_message: Human-readable string describing the error.
    :param msg: The QMP `Message` that caused the error.
    """


class BadReplyError(_MsgProtocolError):
    """
    An execution reply was successfully routed, but not understood.

    If a QMP message is received with an 'id' field to allow it to be
    routed, but is otherwise malformed, this exception will be raised.

    A reply message is malformed if it is missing either the 'return' or
    'error' keys, or if the 'error' value has missing keys or members of
    the wrong type.

    :param error_message: Human-readable string describing the error.
    :param msg: The malformed reply that was received.
    :param sent: The message that was sent that prompted the error.
    """
    def __init__(self, error_message: str, msg: Message, sent: Message):
        super().__init__(error_message, msg)
        #: The sent `Message` that caused the failure
        self.sent = sent


class QMPClient(AsyncProtocol[Message], Events):
    """
    Implements a QMP client connection.

    QMP can be used to establish a connection as either the transport
    client or server, though this class always acts as the QMP client.

    :param name: Optional nickname for the connection, used for logging.

    Basic script-style usage looks like this::

      qmp = QMPClient('my_virtual_machine_name')
      await qmp.connect(('127.0.0.1', 1234))
      ...
      res = await qmp.execute('block-query')
      ...
      await qmp.disconnect()

    Basic async client-style usage looks like this::

      class Client:
          def __init__(self, name: str):
              self.qmp = QMPClient(name)

          async def watch_events(self):
              try:
                  async for event in self.qmp.events:
                      print(f"Event: {event['event']}")
              except asyncio.CancelledError:
                  return

          async def run(self, address='/tmp/qemu.socket'):
              await self.qmp.connect(address)
              asyncio.create_task(self.watch_events())
              await self.qmp.runstate_changed.wait()
              await self.disconnect()

    See `aqmp.events` for more detail on event handling patterns.
    """
    #: Logger object used for debugging messages.
    logger = logging.getLogger(__name__)

    # Read buffer limit; large enough to accept query-qmp-schema
    _limit = (256 * 1024)

    # Type alias for pending execute() result items
    _PendingT = Union[Message, ExecInterruptedError]

    def __init__(self, name: Optional[str] = None) -> None:
        super().__init__(name)
        Events.__init__(self)

        #: Whether or not to await a greeting after establishing a connection.
        self.await_greeting: bool = True

        #: Whether or not to perform capabilities negotiation upon connection.
        #: Implies `await_greeting`.
        self.negotiate: bool = True

        # Cached Greeting, if one was awaited.
        self._greeting: Optional[Greeting] = None

        # Command ID counter
        self._execute_id = 0

        # Incoming RPC reply messages.
        self._pending: Dict[
            Union[str, None],
            'asyncio.Queue[QMPClient._PendingT]'
        ] = {}

    @property
    def greeting(self) -> Optional[Greeting]:
        """The `Greeting` from the QMP server, if any."""
        return self._greeting

    @upper_half
    async def _establish_session(self) -> None:
        """
        Initiate the QMP session.

        Wait for the QMP greeting and perform capabilities negotiation.

        :raise GreetingError: When the greeting is not understood.
        :raise NegotiationError: If the negotiation fails.
        :raise EOFError: When the server unexpectedly hangs up.
        :raise OSError: For underlying stream errors.
        """
        self._greeting = None
        self._pending = {}

        if self.await_greeting or self.negotiate:
            self._greeting = await self._get_greeting()

        if self.negotiate:
            await self._negotiate()

        # This will start the reader/writers:
        await super()._establish_session()

    @upper_half
    async def _get_greeting(self) -> Greeting:
        """
        :raise GreetingError: When the greeting is not understood.
        :raise EOFError: When the server unexpectedly hangs up.
        :raise OSError: For underlying stream errors.

        :return: the Greeting object given by the server.
        """
        self.logger.debug("Awaiting greeting ...")

        try:
            msg = await self._recv()
            return Greeting(msg)
        except (ProtocolError, KeyError, TypeError) as err:
            emsg = "Did not understand Greeting"
            self.logger.error("%s: %s", emsg, exception_summary(err))
            self.logger.debug("%s:\n%s\n", emsg, pretty_traceback())
            raise GreetingError(emsg, err) from err
        except BaseException as err:
            # EOFError, OSError, or something unexpected.
            emsg = "Failed to receive Greeting"
            self.logger.error("%s: %s", emsg, exception_summary(err))
            self.logger.debug("%s:\n%s\n", emsg, pretty_traceback())
            raise

    @upper_half
    async def _negotiate(self) -> None:
        """
        Perform QMP capabilities negotiation.

        :raise NegotiationError: When negotiation fails.
        :raise EOFError: When the server unexpectedly hangs up.
        :raise OSError: For underlying stream errors.
        """
        self.logger.debug("Negotiating capabilities ...")

        arguments: Dict[str, List[str]] = {'enable': []}
        if self._greeting and 'oob' in self._greeting.QMP.capabilities:
            arguments['enable'].append('oob')
        msg = self.make_execute_msg('qmp_capabilities', arguments=arguments)

        # It's not safe to use execute() here, because the reader/writers
        # aren't running. AsyncProtocol *requires* that a new session
        # does not fail after the reader/writers are running!
        try:
            await self._send(msg)
            reply = await self._recv()
            assert 'return' in reply
            assert 'error' not in reply
        except (ProtocolError, AssertionError) as err:
            emsg = "Negotiation failed"
            self.logger.error("%s: %s", emsg, exception_summary(err))
            self.logger.debug("%s:\n%s\n", emsg, pretty_traceback())
            raise NegotiationError(emsg, err) from err
        except BaseException as err:
            # EOFError, OSError, or something unexpected.
            emsg = "Negotiation failed"
            self.logger.error("%s: %s", emsg, exception_summary(err))
            self.logger.debug("%s:\n%s\n", emsg, pretty_traceback())
            raise

    @bottom_half
    async def _bh_disconnect(self) -> None:
        try:
            await super()._bh_disconnect()
        finally:
            if self._pending:
                self.logger.debug("Cancelling pending executions")
            keys = self._pending.keys()
            for key in keys:
                self.logger.debug("Cancelling execution '%s'", key)
                self._pending[key].put_nowait(
                    ExecInterruptedError("Disconnected")
                )

            self.logger.debug("QMP Disconnected.")

    @upper_half
    def _cleanup(self) -> None:
        super()._cleanup()
        assert not self._pending

    @bottom_half
    async def _on_message(self, msg: Message) -> None:
        """
        Add an incoming message to the appropriate queue/handler.

        :raise ServerParseError: When Message indicates server parse failure.
        """
        # Incoming messages are not fully parsed/validated here;
        # do only light peeking to know how to route the messages.

        if 'event' in msg:
            await self._event_dispatch(msg)
            return

        # Below, we assume everything left is an execute/exec-oob response.

        exec_id = cast(Optional[str], msg.get('id'))

        if exec_id in self._pending:
            await self._pending[exec_id].put(msg)
            return

        # We have a message we can't route back to a caller.

        is_error = 'error' in msg
        has_id = 'id' in msg

        if is_error and not has_id:
            # This is very likely a server parsing error.
            # It doesn't inherently belong to any pending execution.
            # Instead of performing clever recovery, just terminate.
            # See "NOTE" in qmp-spec.txt, section 2.4.2
            raise ServerParseError(
                ("Server sent an error response without an ID, "
                 "but there are no ID-less executions pending. "
                 "Assuming this is a server parser failure."),
                msg
            )

        # qmp-spec.txt, section 2.4:
        # 'Clients should drop all the responses
        # that have an unknown "id" field.'
        self.logger.log(
            logging.ERROR if is_error else logging.WARNING,
            "Unknown ID '%s', message dropped.",
            exec_id,
        )
        self.logger.debug("Unroutable message: %s", str(msg))

    @upper_half
    @bottom_half
    async def _do_recv(self) -> Message:
        """
        :raise OSError: When a stream error is encountered.
        :raise EOFError: When the stream is at EOF.
        :raise ProtocolError:
            When the Message is not understood.
            See also `Message._deserialize`.

        :return: A single QMP `Message`.
        """
        msg_bytes = await self._readline()
        msg = Message(msg_bytes, eager=True)
        return msg

    @upper_half
    @bottom_half
    def _do_send(self, msg: Message) -> None:
        """
        :raise ValueError: JSON serialization failure
        :raise TypeError: JSON serialization failure
        :raise OSError: When a stream error is encountered.
        """
        assert self._writer is not None
        self._writer.write(bytes(msg))

    @upper_half
    def _get_exec_id(self) -> str:
        exec_id = f"__aqmp#{self._execute_id:05d}"
        self._execute_id += 1
        return exec_id

    @upper_half
    async def _issue(self, msg: Message) -> Union[None, str]:
        """
        Issue a QMP `Message` and do not wait for a reply.

        :param msg: The QMP `Message` to send to the server.

        :return: The ID of the `Message` sent.
        """
        msg_id: Optional[str] = None
        if 'id' in msg:
            assert isinstance(msg['id'], str)
            msg_id = msg['id']

        self._pending[msg_id] = asyncio.Queue(maxsize=1)
        await self._outgoing.put(msg)

        return msg_id

    @upper_half
    async def _reply(self, msg_id: Union[str, None]) -> Message:
        """
        Await a reply to a previously issued QMP message.

        :param msg_id: The ID of the previously issued message.

        :return: The reply from the server.
        :raise ExecInterruptedError:
            When the reply could not be retrieved because the connection
            was lost, or some other problem.
        """
        queue = self._pending[msg_id]
        result = await queue.get()

        try:
            if isinstance(result, ExecInterruptedError):
                raise result
            return result
        finally:
            del self._pending[msg_id]

    @upper_half
    async def _execute(self, msg: Message, assign_id: bool = True) -> Message:
        """
        Send a QMP `Message` to the server and await a reply.

        This method *assumes* you are sending some kind of an execute
        statement that *will* receive a reply.

        An execution ID will be assigned if assign_id is `True`. It can be
        disabled, but this requires that an ID is manually assigned
        instead. For manually assigned IDs, you must not use the string
        '__aqmp#' anywhere in the ID.

        :param msg: The QMP `Message` to execute.
        :param assign_id: If True, assign a new execution ID.

        :return: Execution reply from the server.
        :raise ExecInterruptedError:
            When the reply could not be retrieved because the connection
            was lost, or some other problem.
        """
        if assign_id:
            msg['id'] = self._get_exec_id()
        elif 'id' in msg:
            assert isinstance(msg['id'], str)
            assert '__aqmp#' not in msg['id']

        exec_id = await self._issue(msg)
        return await self._reply(exec_id)

    @upper_half
    @require(Runstate.RUNNING)
    async def _raw(
            self,
            msg: Union[Message, Mapping[str, object], bytes],
            assign_id: bool = True,
    ) -> Message:
        """
        Issue a raw `Message` to the QMP server and await a reply.

        :param msg:
            A Message to send to the server. It may be a `Message`, any
            Mapping (including Dict), or raw bytes.
        :param assign_id:
            Assign an arbitrary execution ID to this message. If
            `False`, the existing id must either be absent (and no other
            such pending execution may omit an ID) or a string. If it is
            a string, it must not start with '__aqmp#' and no other such
            pending execution may currently be using that ID.

        :return: Execution reply from the server.

        :raise ExecInterruptedError:
            When the reply could not be retrieved because the connection
            was lost, or some other problem.
        :raise TypeError:
            When assign_id is `False`, an ID is given, and it is not a string.
        :raise ValueError:
            When assign_id is `False`, but the ID is not usable;
            Either because it starts with '__aqmp#' or it is already in-use.
        """
        # 1. convert generic Mapping or bytes to a QMP Message
        # 2. copy Message objects so that we assign an ID only to the copy.
        msg = Message(msg)

        exec_id = msg.get('id')
        if not assign_id and 'id' in msg:
            if not isinstance(exec_id, str):
                raise TypeError(f"ID ('{exec_id}') must be a string.")
            if exec_id.startswith('__aqmp#'):
                raise ValueError(
                    f"ID ('{exec_id}') must not start with '__aqmp#'."
                )

        if not assign_id and exec_id in self._pending:
            raise ValueError(
                f"ID '{exec_id}' is in-use and cannot be used."
            )

        return await self._execute(msg, assign_id=assign_id)

    @upper_half
    @require(Runstate.RUNNING)
    async def execute_msg(self, msg: Message) -> object:
        """
        Execute a QMP command and return its value.

        :param msg: The QMP `Message` to execute.

        :return:
            The command execution return value from the server. The type of
            object returned depends on the command that was issued,
            though most in QEMU return a `dict`.
        :raise ValueError:
            If the QMP `Message` does not have either the 'execute' or
            'exec-oob' fields set.
        :raise ExecuteError: When the server returns an error response.
        :raise ExecInterruptedError: if the connection was terminated early.
        """
        if not ('execute' in msg or 'exec-oob' in msg):
            raise ValueError("Requires 'execute' or 'exec-oob' message")

        # Copy the Message so that the ID assigned by _execute() is
        # local to this method; allowing the ID to be seen in raised
        # Exceptions but without modifying the caller's held copy.
        msg = Message(msg)
        reply = await self._execute(msg)

        if 'error' in reply:
            try:
                error_response = ErrorResponse(reply)
            except (KeyError, TypeError) as err:
                # Error response was malformed.
                raise BadReplyError(
                    "QMP error reply is malformed", reply, msg,
                ) from err

            raise ExecuteError(error_response, msg, reply)

        if 'return' not in reply:
            raise BadReplyError(
                "QMP reply is missing a 'error' or 'return' member",
                reply, msg,
            )

        return reply['return']

    @classmethod
    def make_execute_msg(cls, cmd: str,
                         arguments: Optional[Mapping[str, object]] = None,
                         oob: bool = False) -> Message:
        """
        Create an executable message to be sent by `execute_msg` later.

        :param cmd: QMP command name.
        :param arguments: Arguments (if any). Must be JSON-serializable.
        :param oob: If `True`, execute "out of band".

        :return: An executable QMP `Message`.
        """
        msg = Message({'exec-oob' if oob else 'execute': cmd})
        if arguments is not None:
            msg['arguments'] = arguments
        return msg

    @upper_half
    async def execute(self, cmd: str,
                      arguments: Optional[Mapping[str, object]] = None,
                      oob: bool = False) -> object:
        """
        Execute a QMP command and return its value.

        :param cmd: QMP command name.
        :param arguments: Arguments (if any). Must be JSON-serializable.
        :param oob: If `True`, execute "out of band".

        :return:
            The command execution return value from the server. The type of
            object returned depends on the command that was issued,
            though most in QEMU return a `dict`.
        :raise ExecuteError: When the server returns an error response.
        :raise ExecInterruptedError: if the connection was terminated early.
        """
        msg = self.make_execute_msg(cmd, arguments, oob=oob)
        return await self.execute_msg(msg)

    @upper_half
    @require(Runstate.RUNNING)
    def send_fd_scm(self, fd: int) -> None:
        """
        Send a file descriptor to the remote via SCM_RIGHTS.
        """
        assert self._writer is not None
        sock = self._writer.transport.get_extra_info('socket')

        if sock.family != socket.AF_UNIX:
            raise AQMPError("Sending file descriptors requires a UNIX socket.")

        # Void the warranty sticker.
        # Access to sendmsg in asyncio is scheduled for removal in Python 3.11.
        sock = sock._sock  # pylint: disable=protected-access
        sock.sendmsg(
            [b' '],
            [(socket.SOL_SOCKET, socket.SCM_RIGHTS, struct.pack('@i', fd))]
        )
