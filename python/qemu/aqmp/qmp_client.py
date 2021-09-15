"""
QMP Protocol Implementation

This module provides the `QMPClient` class, which can be used to connect
and send commands to a QMP server such as QEMU. The QMP class can be
used to either connect to a listening server, or used to listen and
accept an incoming connection from that server.
"""

import logging
from typing import (
    Dict,
    List,
    Mapping,
    Optional,
)

from .error import ProtocolError
from .events import Events
from .message import Message
from .models import Greeting
from .protocol import AsyncProtocol
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
    async def _on_message(self, msg: Message) -> None:
        """
        Add an incoming message to the appropriate queue/handler.
        """
        # Incoming messages are not fully parsed/validated here;
        # do only light peeking to know how to route the messages.

        if 'event' in msg:
            await self._event_dispatch(msg)
            return

        # Below, we assume everything left is an execute/exec-oob response.
        # ... Which we'll implement in the next commit!

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

    @classmethod
    def make_execute_msg(cls, cmd: str,
                         arguments: Optional[Mapping[str, object]] = None,
                         oob: bool = False) -> Message:
        """
        Create an executable message to be sent later.

        :param cmd: QMP command name.
        :param arguments: Arguments (if any). Must be JSON-serializable.
        :param oob: If `True`, execute "out of band".

        :return: An executable QMP `Message`.
        """
        msg = Message({'exec-oob' if oob else 'execute': cmd})
        if arguments is not None:
            msg['arguments'] = arguments
        return msg
