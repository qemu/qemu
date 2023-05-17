"""
Generic Asynchronous Message-based Protocol Support

This module provides a generic framework for sending and receiving
messages over an asyncio stream. `AsyncProtocol` is an abstract class
that implements the core mechanisms of a simple send/receive protocol,
and is designed to be extended.

In this package, it is used as the implementation for the `QMPClient`
class.
"""

# It's all the docstrings ... ! It's long for a good reason ^_^;
# pylint: disable=too-many-lines

import asyncio
from asyncio import StreamReader, StreamWriter
from enum import Enum
from functools import wraps
import logging
import socket
from ssl import SSLContext
from typing import (
    Any,
    Awaitable,
    Callable,
    Generic,
    List,
    Optional,
    Tuple,
    TypeVar,
    Union,
    cast,
)

from .error import QMPError
from .util import (
    bottom_half,
    create_task,
    exception_summary,
    flush,
    is_closing,
    pretty_traceback,
    upper_half,
    wait_closed,
)


T = TypeVar('T')
_U = TypeVar('_U')
_TaskFN = Callable[[], Awaitable[None]]  # aka ``async def func() -> None``

InternetAddrT = Tuple[str, int]
UnixAddrT = str
SocketAddrT = Union[UnixAddrT, InternetAddrT]


class Runstate(Enum):
    """Protocol session runstate."""

    #: Fully quiesced and disconnected.
    IDLE = 0
    #: In the process of connecting or establishing a session.
    CONNECTING = 1
    #: Fully connected and active session.
    RUNNING = 2
    #: In the process of disconnecting.
    #: Runstate may be returned to `IDLE` by calling `disconnect()`.
    DISCONNECTING = 3


class ConnectError(QMPError):
    """
    Raised when the initial connection process has failed.

    This Exception always wraps a "root cause" exception that can be
    interrogated for additional information.

    :param error_message: Human-readable string describing the error.
    :param exc: The root-cause exception.
    """
    def __init__(self, error_message: str, exc: Exception):
        super().__init__(error_message)
        #: Human-readable error string
        self.error_message: str = error_message
        #: Wrapped root cause exception
        self.exc: Exception = exc

    def __str__(self) -> str:
        cause = str(self.exc)
        if not cause:
            # If there's no error string, use the exception name.
            cause = exception_summary(self.exc)
        return f"{self.error_message}: {cause}"


class StateError(QMPError):
    """
    An API command (connect, execute, etc) was issued at an inappropriate time.

    This error is raised when a command like
    :py:meth:`~AsyncProtocol.connect()` is issued at an inappropriate
    time.

    :param error_message: Human-readable string describing the state violation.
    :param state: The actual `Runstate` seen at the time of the violation.
    :param required: The `Runstate` required to process this command.
    """
    def __init__(self, error_message: str,
                 state: Runstate, required: Runstate):
        super().__init__(error_message)
        self.error_message = error_message
        self.state = state
        self.required = required


F = TypeVar('F', bound=Callable[..., Any])  # pylint: disable=invalid-name


# Don't Panic.
def require(required_state: Runstate) -> Callable[[F], F]:
    """
    Decorator: protect a method so it can only be run in a certain `Runstate`.

    :param required_state: The `Runstate` required to invoke this method.
    :raise StateError: When the required `Runstate` is not met.
    """
    def _decorator(func: F) -> F:
        # _decorator is the decorator that is built by calling the
        # require() decorator factory; e.g.:
        #
        # @require(Runstate.IDLE) def foo(): ...
        # will replace 'foo' with the result of '_decorator(foo)'.

        @wraps(func)
        def _wrapper(proto: 'AsyncProtocol[Any]',
                     *args: Any, **kwargs: Any) -> Any:
            # _wrapper is the function that gets executed prior to the
            # decorated method.

            name = type(proto).__name__

            if proto.runstate != required_state:
                if proto.runstate == Runstate.CONNECTING:
                    emsg = f"{name} is currently connecting."
                elif proto.runstate == Runstate.DISCONNECTING:
                    emsg = (f"{name} is disconnecting."
                            " Call disconnect() to return to IDLE state.")
                elif proto.runstate == Runstate.RUNNING:
                    emsg = f"{name} is already connected and running."
                elif proto.runstate == Runstate.IDLE:
                    emsg = f"{name} is disconnected and idle."
                else:
                    assert False
                raise StateError(emsg, proto.runstate, required_state)
            # No StateError, so call the wrapped method.
            return func(proto, *args, **kwargs)

        # Return the decorated method;
        # Transforming Func to Decorated[Func].
        return cast(F, _wrapper)

    # Return the decorator instance from the decorator factory. Phew!
    return _decorator


class AsyncProtocol(Generic[T]):
    """
    AsyncProtocol implements a generic async message-based protocol.

    This protocol assumes the basic unit of information transfer between
    client and server is a "message", the details of which are left up
    to the implementation. It assumes the sending and receiving of these
    messages is full-duplex and not necessarily correlated; i.e. it
    supports asynchronous inbound messages.

    It is designed to be extended by a specific protocol which provides
    the implementations for how to read and send messages. These must be
    defined in `_do_recv()` and `_do_send()`, respectively.

    Other callbacks have a default implementation, but are intended to be
    either extended or overridden:

     - `_establish_session`:
         The base implementation starts the reader/writer tasks.
         A protocol implementation can override this call, inserting
         actions to be taken prior to starting the reader/writer tasks
         before the super() call; actions needing to occur afterwards
         can be written after the super() call.
     - `_on_message`:
         Actions to be performed when a message is received.
     - `_cb_outbound`:
         Logging/Filtering hook for all outbound messages.
     - `_cb_inbound`:
         Logging/Filtering hook for all inbound messages.
         This hook runs *before* `_on_message()`.

    :param name:
        Name used for logging messages, if any. By default, messages
        will log to 'qemu.qmp.protocol', but each individual connection
        can be given its own logger by giving it a name; messages will
        then log to 'qemu.qmp.protocol.${name}'.
    """
    # pylint: disable=too-many-instance-attributes

    #: Logger object for debugging messages from this connection.
    logger = logging.getLogger(__name__)

    # Maximum allowable size of read buffer
    _limit = 64 * 1024

    # -------------------------
    # Section: Public interface
    # -------------------------

    def __init__(self, name: Optional[str] = None) -> None:
        #: The nickname for this connection, if any.
        self.name: Optional[str] = name
        if self.name is not None:
            self.logger = self.logger.getChild(self.name)

        # stream I/O
        self._reader: Optional[StreamReader] = None
        self._writer: Optional[StreamWriter] = None

        # Outbound Message queue
        self._outgoing: asyncio.Queue[T]

        # Special, long-running tasks:
        self._reader_task: Optional[asyncio.Future[None]] = None
        self._writer_task: Optional[asyncio.Future[None]] = None

        # Aggregate of the above two tasks, used for Exception management.
        self._bh_tasks: Optional[asyncio.Future[Tuple[None, None]]] = None

        #: Disconnect task. The disconnect implementation runs in a task
        #: so that asynchronous disconnects (initiated by the
        #: reader/writer) are allowed to wait for the reader/writers to
        #: exit.
        self._dc_task: Optional[asyncio.Future[None]] = None

        self._runstate = Runstate.IDLE
        self._runstate_changed: Optional[asyncio.Event] = None

        # Server state for start_server() and _incoming()
        self._server: Optional[asyncio.AbstractServer] = None
        self._accepted: Optional[asyncio.Event] = None

    def __repr__(self) -> str:
        cls_name = type(self).__name__
        tokens = []
        if self.name is not None:
            tokens.append(f"name={self.name!r}")
        tokens.append(f"runstate={self.runstate.name}")
        return f"<{cls_name} {' '.join(tokens)}>"

    @property  # @upper_half
    def runstate(self) -> Runstate:
        """The current `Runstate` of the connection."""
        return self._runstate

    @upper_half
    async def runstate_changed(self) -> Runstate:
        """
        Wait for the `runstate` to change, then return that runstate.
        """
        await self._runstate_event.wait()
        return self.runstate

    @upper_half
    @require(Runstate.IDLE)
    async def start_server_and_accept(
            self, address: SocketAddrT,
            ssl: Optional[SSLContext] = None
    ) -> None:
        """
        Accept a connection and begin processing message queues.

        If this call fails, `runstate` is guaranteed to be set back to `IDLE`.
        This method is precisely equivalent to calling `start_server()`
        followed by `accept()`.

        :param address:
            Address to listen on; UNIX socket path or TCP address/port.
        :param ssl: SSL context to use, if any.

        :raise StateError: When the `Runstate` is not `IDLE`.
        :raise ConnectError:
            When a connection or session cannot be established.

            This exception will wrap a more concrete one. In most cases,
            the wrapped exception will be `OSError` or `EOFError`. If a
            protocol-level failure occurs while establishing a new
            session, the wrapped error may also be an `QMPError`.
        """
        await self.start_server(address, ssl)
        await self.accept()
        assert self.runstate == Runstate.RUNNING

    @upper_half
    @require(Runstate.IDLE)
    async def start_server(self, address: SocketAddrT,
                           ssl: Optional[SSLContext] = None) -> None:
        """
        Start listening for an incoming connection, but do not wait for a peer.

        This method starts listening for an incoming connection, but
        does not block waiting for a peer. This call will return
        immediately after binding and listening on a socket. A later
        call to `accept()` must be made in order to finalize the
        incoming connection.

        :param address:
            Address to listen on; UNIX socket path or TCP address/port.
        :param ssl: SSL context to use, if any.

        :raise StateError: When the `Runstate` is not `IDLE`.
        :raise ConnectError:
            When the server could not start listening on this address.

            This exception will wrap a more concrete one. In most cases,
            the wrapped exception will be `OSError`.
        """
        await self._session_guard(
            self._do_start_server(address, ssl),
            'Failed to establish connection')
        assert self.runstate == Runstate.CONNECTING

    @upper_half
    @require(Runstate.CONNECTING)
    async def accept(self) -> None:
        """
        Accept an incoming connection and begin processing message queues.

        If this call fails, `runstate` is guaranteed to be set back to `IDLE`.

        :raise StateError: When the `Runstate` is not `CONNECTING`.
        :raise QMPError: When `start_server()` was not called yet.
        :raise ConnectError:
            When a connection or session cannot be established.

            This exception will wrap a more concrete one. In most cases,
            the wrapped exception will be `OSError` or `EOFError`. If a
            protocol-level failure occurs while establishing a new
            session, the wrapped error may also be an `QMPError`.
        """
        if self._accepted is None:
            raise QMPError("Cannot call accept() before start_server().")
        await self._session_guard(
            self._do_accept(),
            'Failed to establish connection')
        await self._session_guard(
            self._establish_session(),
            'Failed to establish session')
        assert self.runstate == Runstate.RUNNING

    @upper_half
    @require(Runstate.IDLE)
    async def connect(self, address: Union[SocketAddrT, socket.socket],
                      ssl: Optional[SSLContext] = None) -> None:
        """
        Connect to the server and begin processing message queues.

        If this call fails, `runstate` is guaranteed to be set back to `IDLE`.

        :param address:
            Address to connect to; UNIX socket path or TCP address/port.
        :param ssl: SSL context to use, if any.

        :raise StateError: When the `Runstate` is not `IDLE`.
        :raise ConnectError:
            When a connection or session cannot be established.

            This exception will wrap a more concrete one. In most cases,
            the wrapped exception will be `OSError` or `EOFError`. If a
            protocol-level failure occurs while establishing a new
            session, the wrapped error may also be an `QMPError`.
        """
        await self._session_guard(
            self._do_connect(address, ssl),
            'Failed to establish connection')
        await self._session_guard(
            self._establish_session(),
            'Failed to establish session')
        assert self.runstate == Runstate.RUNNING

    @upper_half
    async def disconnect(self) -> None:
        """
        Disconnect and wait for all tasks to fully stop.

        If there was an exception that caused the reader/writers to
        terminate prematurely, it will be raised here.

        :raise Exception: When the reader or writer terminate unexpectedly.
        """
        self.logger.debug("disconnect() called.")
        self._schedule_disconnect()
        await self._wait_disconnect()

    # --------------------------
    # Section: Session machinery
    # --------------------------

    async def _session_guard(self, coro: Awaitable[None], emsg: str) -> None:
        """
        Async guard function used to roll back to `IDLE` on any error.

        On any Exception, the state machine will be reset back to
        `IDLE`. Most Exceptions will be wrapped with `ConnectError`, but
        `BaseException` events will be left alone (This includes
        asyncio.CancelledError, even prior to Python 3.8).

        :param error_message:
            Human-readable string describing what connection phase failed.

        :raise BaseException:
            When `BaseException` occurs in the guarded block.
        :raise ConnectError:
            When any other error is encountered in the guarded block.
        """
        # Note: After Python 3.6 support is removed, this should be an
        # @asynccontextmanager instead of accepting a callback.
        try:
            await coro
        except BaseException as err:
            self.logger.error("%s: %s", emsg, exception_summary(err))
            self.logger.debug("%s:\n%s\n", emsg, pretty_traceback())
            try:
                # Reset the runstate back to IDLE.
                await self.disconnect()
            except:
                # We don't expect any Exceptions from the disconnect function
                # here, because we failed to connect in the first place.
                # The disconnect() function is intended to perform
                # only cannot-fail cleanup here, but you never know.
                emsg = (
                    "Unexpected bottom half exception. "
                    "This is a bug in the QMP library. "
                    "Please report it to <qemu-devel@nongnu.org> and "
                    "CC: John Snow <jsnow@redhat.com>."
                )
                self.logger.critical("%s:\n%s\n", emsg, pretty_traceback())
                raise

            # CancelledError is an Exception with special semantic meaning;
            # We do NOT want to wrap it up under ConnectError.
            # NB: CancelledError is not a BaseException before Python 3.8
            if isinstance(err, asyncio.CancelledError):
                raise

            # Any other kind of error can be treated as some kind of connection
            # failure broadly. Inspect the 'exc' field to explore the root
            # cause in greater detail.
            if isinstance(err, Exception):
                raise ConnectError(emsg, err) from err

            # Raise BaseExceptions un-wrapped, they're more important.
            raise

    @property
    def _runstate_event(self) -> asyncio.Event:
        # asyncio.Event() objects should not be created prior to entrance into
        # an event loop, so we can ensure we create it in the correct context.
        # Create it on-demand *only* at the behest of an 'async def' method.
        if not self._runstate_changed:
            self._runstate_changed = asyncio.Event()
        return self._runstate_changed

    @upper_half
    @bottom_half
    def _set_state(self, state: Runstate) -> None:
        """
        Change the `Runstate` of the protocol connection.

        Signals the `runstate_changed` event.
        """
        if state == self._runstate:
            return

        self.logger.debug("Transitioning from '%s' to '%s'.",
                          str(self._runstate), str(state))
        self._runstate = state
        self._runstate_event.set()
        self._runstate_event.clear()

    @bottom_half
    async def _stop_server(self) -> None:
        """
        Stop listening for / accepting new incoming connections.
        """
        if self._server is None:
            return

        try:
            self.logger.debug("Stopping server.")
            self._server.close()
            await self._server.wait_closed()
            self.logger.debug("Server stopped.")
        finally:
            self._server = None

    @bottom_half  # However, it does not run from the R/W tasks.
    async def _incoming(self,
                        reader: asyncio.StreamReader,
                        writer: asyncio.StreamWriter) -> None:
        """
        Accept an incoming connection and signal the upper_half.

        This method does the minimum necessary to accept a single
        incoming connection. It signals back to the upper_half ASAP so
        that any errors during session initialization can occur
        naturally in the caller's stack.

        :param reader: Incoming `asyncio.StreamReader`
        :param writer: Incoming `asyncio.StreamWriter`
        """
        peer = writer.get_extra_info('peername', 'Unknown peer')
        self.logger.debug("Incoming connection from %s", peer)

        if self._reader or self._writer:
            # Sadly, we can have more than one pending connection
            # because of https://bugs.python.org/issue46715
            # Close any extra connections we don't actually want.
            self.logger.warning("Extraneous connection inadvertently accepted")
            writer.close()
            return

        # A connection has been accepted; stop listening for new ones.
        assert self._accepted is not None
        await self._stop_server()
        self._reader, self._writer = (reader, writer)
        self._accepted.set()

    @upper_half
    async def _do_start_server(self, address: SocketAddrT,
                               ssl: Optional[SSLContext] = None) -> None:
        """
        Start listening for an incoming connection, but do not wait for a peer.

        This method starts listening for an incoming connection, but does not
        block waiting for a peer. This call will return immediately after
        binding and listening to a socket. A later call to accept() must be
        made in order to finalize the incoming connection.

        :param address:
            Address to listen on; UNIX socket path or TCP address/port.
        :param ssl: SSL context to use, if any.

        :raise OSError: For stream-related errors.
        """
        assert self.runstate == Runstate.IDLE
        self._set_state(Runstate.CONNECTING)

        self.logger.debug("Awaiting connection on %s ...", address)
        self._accepted = asyncio.Event()

        if isinstance(address, tuple):
            coro = asyncio.start_server(
                self._incoming,
                host=address[0],
                port=address[1],
                ssl=ssl,
                backlog=1,
                limit=self._limit,
            )
        else:
            coro = asyncio.start_unix_server(
                self._incoming,
                path=address,
                ssl=ssl,
                backlog=1,
                limit=self._limit,
            )

        # Allow runstate watchers to witness 'CONNECTING' state; some
        # failures in the streaming layer are synchronous and will not
        # otherwise yield.
        await asyncio.sleep(0)

        # This will start the server (bind(2), listen(2)). It will also
        # call accept(2) if we yield, but we don't block on that here.
        self._server = await coro
        self.logger.debug("Server listening on %s", address)

    @upper_half
    async def _do_accept(self) -> None:
        """
        Wait for and accept an incoming connection.

        Requires that we have not yet accepted an incoming connection
        from the upper_half, but it's OK if the server is no longer
        running because the bottom_half has already accepted the
        connection.
        """
        assert self._accepted is not None
        await self._accepted.wait()
        assert self._server is None
        self._accepted = None

        self.logger.debug("Connection accepted.")

    @upper_half
    async def _do_connect(self, address: Union[SocketAddrT, socket.socket],
                          ssl: Optional[SSLContext] = None) -> None:
        """
        Acting as the transport client, initiate a connection to a server.

        :param address:
            Address to connect to; UNIX socket path or TCP address/port.
        :param ssl: SSL context to use, if any.

        :raise OSError: For stream-related errors.
        """
        assert self.runstate == Runstate.IDLE
        self._set_state(Runstate.CONNECTING)

        # Allow runstate watchers to witness 'CONNECTING' state; some
        # failures in the streaming layer are synchronous and will not
        # otherwise yield.
        await asyncio.sleep(0)

        if isinstance(address, socket.socket):
            self.logger.debug("Connecting with existing socket: "
                              "fd=%d, family=%r, type=%r",
                              address.fileno(), address.family, address.type)
            connect = asyncio.open_connection(
                limit=self._limit,
                ssl=ssl,
                sock=address,
            )
        elif isinstance(address, tuple):
            self.logger.debug("Connecting to %s ...", address)
            connect = asyncio.open_connection(
                address[0],
                address[1],
                ssl=ssl,
                limit=self._limit,
            )
        else:
            self.logger.debug("Connecting to file://%s ...", address)
            connect = asyncio.open_unix_connection(
                path=address,
                ssl=ssl,
                limit=self._limit,
            )

        self._reader, self._writer = await connect
        self.logger.debug("Connected.")

    @upper_half
    async def _establish_session(self) -> None:
        """
        Establish a new session.

        Starts the readers/writer tasks; subclasses may perform their
        own negotiations here. The Runstate will be RUNNING upon
        successful conclusion.
        """
        assert self.runstate == Runstate.CONNECTING

        self._outgoing = asyncio.Queue()

        reader_coro = self._bh_loop_forever(self._bh_recv_message, 'Reader')
        writer_coro = self._bh_loop_forever(self._bh_send_message, 'Writer')

        self._reader_task = create_task(reader_coro)
        self._writer_task = create_task(writer_coro)

        self._bh_tasks = asyncio.gather(
            self._reader_task,
            self._writer_task,
        )

        self._set_state(Runstate.RUNNING)
        await asyncio.sleep(0)  # Allow runstate_event to process

    @upper_half
    @bottom_half
    def _schedule_disconnect(self) -> None:
        """
        Initiate a disconnect; idempotent.

        This method is used both in the upper-half as a direct
        consequence of `disconnect()`, and in the bottom-half in the
        case of unhandled exceptions in the reader/writer tasks.

        It can be invoked no matter what the `runstate` is.
        """
        if not self._dc_task:
            self._set_state(Runstate.DISCONNECTING)
            self.logger.debug("Scheduling disconnect.")
            self._dc_task = create_task(self._bh_disconnect())

    @upper_half
    async def _wait_disconnect(self) -> None:
        """
        Waits for a previously scheduled disconnect to finish.

        This method will gather any bottom half exceptions and re-raise
        the one that occurred first; presuming it to be the root cause
        of any subsequent Exceptions. It is intended to be used in the
        upper half of the call chain.

        :raise Exception:
            Arbitrary exception re-raised on behalf of the reader/writer.
        """
        assert self.runstate == Runstate.DISCONNECTING
        assert self._dc_task

        aws: List[Awaitable[object]] = [self._dc_task]
        if self._bh_tasks:
            aws.insert(0, self._bh_tasks)
        all_defined_tasks = asyncio.gather(*aws)

        # Ensure disconnect is done; Exception (if any) is not raised here:
        await asyncio.wait((self._dc_task,))

        try:
            await all_defined_tasks  # Raise Exceptions from the bottom half.
        finally:
            self._cleanup()
            self._set_state(Runstate.IDLE)

    @upper_half
    def _cleanup(self) -> None:
        """
        Fully reset this object to a clean state and return to `IDLE`.
        """
        def _paranoid_task_erase(task: Optional['asyncio.Future[_U]']
                                 ) -> Optional['asyncio.Future[_U]']:
            # Help to erase a task, ENSURING it is fully quiesced first.
            assert (task is None) or task.done()
            return None if (task and task.done()) else task

        assert self.runstate == Runstate.DISCONNECTING
        self._dc_task = _paranoid_task_erase(self._dc_task)
        self._reader_task = _paranoid_task_erase(self._reader_task)
        self._writer_task = _paranoid_task_erase(self._writer_task)
        self._bh_tasks = _paranoid_task_erase(self._bh_tasks)

        self._reader = None
        self._writer = None
        self._accepted = None

        # NB: _runstate_changed cannot be cleared because we still need it to
        # send the final runstate changed event ...!

    # ----------------------------
    # Section: Bottom Half methods
    # ----------------------------

    @bottom_half
    async def _bh_disconnect(self) -> None:
        """
        Disconnect and cancel all outstanding tasks.

        It is designed to be called from its task context,
        :py:obj:`~AsyncProtocol._dc_task`. By running in its own task,
        it is free to wait on any pending actions that may still need to
        occur in either the reader or writer tasks.
        """
        assert self.runstate == Runstate.DISCONNECTING

        def _done(task: Optional['asyncio.Future[Any]']) -> bool:
            return task is not None and task.done()

        # If the server is running, stop it.
        await self._stop_server()

        # Are we already in an error pathway? If either of the tasks are
        # already done, or if we have no tasks but a reader/writer; we
        # must be.
        #
        # NB: We can't use _bh_tasks to check for premature task
        # completion, because it may not yet have had a chance to run
        # and gather itself.
        tasks = tuple(filter(None, (self._writer_task, self._reader_task)))
        error_pathway = _done(self._reader_task) or _done(self._writer_task)
        if not tasks:
            error_pathway |= bool(self._reader) or bool(self._writer)

        try:
            # Try to flush the writer, if possible.
            # This *may* cause an error and force us over into the error path.
            if not error_pathway:
                await self._bh_flush_writer()
        except BaseException as err:
            error_pathway = True
            emsg = "Failed to flush the writer"
            self.logger.error("%s: %s", emsg, exception_summary(err))
            self.logger.debug("%s:\n%s\n", emsg, pretty_traceback())
            raise
        finally:
            # Cancel any still-running tasks (Won't raise):
            if self._writer_task is not None and not self._writer_task.done():
                self.logger.debug("Cancelling writer task.")
                self._writer_task.cancel()
            if self._reader_task is not None and not self._reader_task.done():
                self.logger.debug("Cancelling reader task.")
                self._reader_task.cancel()

            # Close out the tasks entirely (Won't raise):
            if tasks:
                self.logger.debug("Waiting for tasks to complete ...")
                await asyncio.wait(tasks)

            # Lastly, close the stream itself. (*May raise*!):
            await self._bh_close_stream(error_pathway)
            self.logger.debug("Disconnected.")

    @bottom_half
    async def _bh_flush_writer(self) -> None:
        if not self._writer_task:
            return

        self.logger.debug("Draining the outbound queue ...")
        await self._outgoing.join()
        if self._writer is not None:
            self.logger.debug("Flushing the StreamWriter ...")
            await flush(self._writer)

    @bottom_half
    async def _bh_close_stream(self, error_pathway: bool = False) -> None:
        # NB: Closing the writer also implicitly closes the reader.
        if not self._writer:
            return

        if not is_closing(self._writer):
            self.logger.debug("Closing StreamWriter.")
            self._writer.close()

        self.logger.debug("Waiting for StreamWriter to close ...")
        try:
            await wait_closed(self._writer)
        except Exception:  # pylint: disable=broad-except
            # It's hard to tell if the Stream is already closed or
            # not. Even if one of the tasks has failed, it may have
            # failed for a higher-layered protocol reason. The
            # stream could still be open and perfectly fine.
            # I don't know how to discern its health here.

            if error_pathway:
                # We already know that *something* went wrong. Let's
                # just trust that the Exception we already have is the
                # better one to present to the user, even if we don't
                # genuinely *know* the relationship between the two.
                self.logger.debug(
                    "Discarding Exception from wait_closed:\n%s\n",
                    pretty_traceback(),
                )
            else:
                # Oops, this is a brand-new error!
                raise
        finally:
            self.logger.debug("StreamWriter closed.")

    @bottom_half
    async def _bh_loop_forever(self, async_fn: _TaskFN, name: str) -> None:
        """
        Run one of the bottom-half methods in a loop forever.

        If the bottom half ever raises any exception, schedule a
        disconnect that will terminate the entire loop.

        :param async_fn: The bottom-half method to run in a loop.
        :param name: The name of this task, used for logging.
        """
        try:
            while True:
                await async_fn()
        except asyncio.CancelledError:
            # We have been cancelled by _bh_disconnect, exit gracefully.
            self.logger.debug("Task.%s: cancelled.", name)
            return
        except BaseException as err:
            self.logger.log(
                logging.INFO if isinstance(err, EOFError) else logging.ERROR,
                "Task.%s: %s",
                name, exception_summary(err)
            )
            self.logger.debug("Task.%s: failure:\n%s\n",
                              name, pretty_traceback())
            self._schedule_disconnect()
            raise
        finally:
            self.logger.debug("Task.%s: exiting.", name)

    @bottom_half
    async def _bh_send_message(self) -> None:
        """
        Wait for an outgoing message, then send it.

        Designed to be run in `_bh_loop_forever()`.
        """
        msg = await self._outgoing.get()
        try:
            await self._send(msg)
        finally:
            self._outgoing.task_done()

    @bottom_half
    async def _bh_recv_message(self) -> None:
        """
        Wait for an incoming message and call `_on_message` to route it.

        Designed to be run in `_bh_loop_forever()`.
        """
        msg = await self._recv()
        await self._on_message(msg)

    # --------------------
    # Section: Message I/O
    # --------------------

    @upper_half
    @bottom_half
    def _cb_outbound(self, msg: T) -> T:
        """
        Callback: outbound message hook.

        This is intended for subclasses to be able to add arbitrary
        hooks to filter or manipulate outgoing messages. The base
        implementation does nothing but log the message without any
        manipulation of the message.

        :param msg: raw outbound message
        :return: final outbound message
        """
        self.logger.debug("--> %s", str(msg))
        return msg

    @upper_half
    @bottom_half
    def _cb_inbound(self, msg: T) -> T:
        """
        Callback: inbound message hook.

        This is intended for subclasses to be able to add arbitrary
        hooks to filter or manipulate incoming messages. The base
        implementation does nothing but log the message without any
        manipulation of the message.

        This method does not "handle" incoming messages; it is a filter.
        The actual "endpoint" for incoming messages is `_on_message()`.

        :param msg: raw inbound message
        :return: processed inbound message
        """
        self.logger.debug("<-- %s", str(msg))
        return msg

    @upper_half
    @bottom_half
    async def _readline(self) -> bytes:
        """
        Wait for a newline from the incoming reader.

        This method is provided as a convenience for upper-layer
        protocols, as many are line-based.

        This method *may* return a sequence of bytes without a trailing
        newline if EOF occurs, but *some* bytes were received. In this
        case, the next call will raise `EOFError`. It is assumed that
        the layer 5 protocol will decide if there is anything meaningful
        to be done with a partial message.

        :raise OSError: For stream-related errors.
        :raise EOFError:
            If the reader stream is at EOF and there are no bytes to return.
        :return: bytes, including the newline.
        """
        assert self._reader is not None
        msg_bytes = await self._reader.readline()

        if not msg_bytes:
            if self._reader.at_eof():
                raise EOFError

        return msg_bytes

    @upper_half
    @bottom_half
    async def _do_recv(self) -> T:
        """
        Abstract: Read from the stream and return a message.

        Very low-level; intended to only be called by `_recv()`.
        """
        raise NotImplementedError

    @upper_half
    @bottom_half
    async def _recv(self) -> T:
        """
        Read an arbitrary protocol message.

        .. warning::
            This method is intended primarily for `_bh_recv_message()`
            to use in an asynchronous task loop. Using it outside of
            this loop will "steal" messages from the normal routing
            mechanism. It is safe to use prior to `_establish_session()`,
            but should not be used otherwise.

        This method uses `_do_recv()` to retrieve the raw message, and
        then transforms it using `_cb_inbound()`.

        :return: A single (filtered, processed) protocol message.
        """
        message = await self._do_recv()
        return self._cb_inbound(message)

    @upper_half
    @bottom_half
    def _do_send(self, msg: T) -> None:
        """
        Abstract: Write a message to the stream.

        Very low-level; intended to only be called by `_send()`.
        """
        raise NotImplementedError

    @upper_half
    @bottom_half
    async def _send(self, msg: T) -> None:
        """
        Send an arbitrary protocol message.

        This method will transform any outgoing messages according to
        `_cb_outbound()`.

        .. warning::
            Like `_recv()`, this method is intended to be called by
            the writer task loop that processes outgoing
            messages. Calling it directly may circumvent logic
            implemented by the caller meant to correlate outgoing and
            incoming messages.

        :raise OSError: For problems with the underlying stream.
        """
        msg = self._cb_outbound(msg)
        self._do_send(msg)

    @bottom_half
    async def _on_message(self, msg: T) -> None:
        """
        Called to handle the receipt of a new message.

        .. caution::
            This is executed from within the reader loop, so be advised
            that waiting on either the reader or writer task will lead
            to deadlock. Additionally, any unhandled exceptions will
            directly cause the loop to halt, so logic may be best-kept
            to a minimum if at all possible.

        :param msg: The incoming message, already logged/filtered.
        """
        # Nothing to do in the abstract case.
