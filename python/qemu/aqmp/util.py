"""
Miscellaneous Utilities

This module provides asyncio utilities and compatibility wrappers for
Python 3.6 to provide some features that otherwise become available in
Python 3.7+.

Various logging and debugging utilities are also provided, such as
`exception_summary()` and `pretty_traceback()`, used primarily for
adding information into the logging stream.
"""

import asyncio
import sys
import traceback
from typing import (
    Any,
    Coroutine,
    Optional,
    TypeVar,
    cast,
)


T = TypeVar('T')


# --------------------------
# Section: Utility Functions
# --------------------------


async def flush(writer: asyncio.StreamWriter) -> None:
    """
    Utility function to ensure a StreamWriter is *fully* drained.

    `asyncio.StreamWriter.drain` only promises we will return to below
    the "high-water mark". This function ensures we flush the entire
    buffer -- by setting the high water mark to 0 and then calling
    drain. The flow control limits are restored after the call is
    completed.
    """
    transport = cast(asyncio.WriteTransport, writer.transport)

    # https://github.com/python/typeshed/issues/5779
    low, high = transport.get_write_buffer_limits()  # type: ignore
    transport.set_write_buffer_limits(0, 0)
    try:
        await writer.drain()
    finally:
        transport.set_write_buffer_limits(high, low)


def upper_half(func: T) -> T:
    """
    Do-nothing decorator that annotates a method as an "upper-half" method.

    These methods must not call bottom-half functions directly, but can
    schedule them to run.
    """
    return func


def bottom_half(func: T) -> T:
    """
    Do-nothing decorator that annotates a method as a "bottom-half" method.

    These methods must take great care to handle their own exceptions whenever
    possible. If they go unhandled, they will cause termination of the loop.

    These methods do not, in general, have the ability to directly
    report information to a caller’s context and will usually be
    collected as a Task result instead.

    They must not call upper-half functions directly.
    """
    return func


# -------------------------------
# Section: Compatibility Wrappers
# -------------------------------


def create_task(coro: Coroutine[Any, Any, T],
                loop: Optional[asyncio.AbstractEventLoop] = None
                ) -> 'asyncio.Future[T]':
    """
    Python 3.6-compatible `asyncio.create_task` wrapper.

    :param coro: The coroutine to execute in a task.
    :param loop: Optionally, the loop to create the task in.

    :return: An `asyncio.Future` object.
    """
    if sys.version_info >= (3, 7):
        if loop is not None:
            return loop.create_task(coro)
        return asyncio.create_task(coro)  # pylint: disable=no-member

    # Python 3.6:
    return asyncio.ensure_future(coro, loop=loop)


def is_closing(writer: asyncio.StreamWriter) -> bool:
    """
    Python 3.6-compatible `asyncio.StreamWriter.is_closing` wrapper.

    :param writer: The `asyncio.StreamWriter` object.
    :return: `True` if the writer is closing, or closed.
    """
    if sys.version_info >= (3, 7):
        return writer.is_closing()

    # Python 3.6:
    transport = writer.transport
    assert isinstance(transport, asyncio.WriteTransport)
    return transport.is_closing()


async def wait_closed(writer: asyncio.StreamWriter) -> None:
    """
    Python 3.6-compatible `asyncio.StreamWriter.wait_closed` wrapper.

    :param writer: The `asyncio.StreamWriter` to wait on.
    """
    if sys.version_info >= (3, 7):
        await writer.wait_closed()
        return

    # Python 3.6
    transport = writer.transport
    assert isinstance(transport, asyncio.WriteTransport)

    while not transport.is_closing():
        await asyncio.sleep(0)

    # This is an ugly workaround, but it's the best I can come up with.
    sock = transport.get_extra_info('socket')

    if sock is None:
        # Our transport doesn't have a socket? ...
        # Nothing we can reasonably do.
        return

    while sock.fileno() != -1:
        await asyncio.sleep(0)


def asyncio_run(coro: Coroutine[Any, Any, T], *, debug: bool = False) -> T:
    """
    Python 3.6-compatible `asyncio.run` wrapper.

    :param coro: A coroutine to execute now.
    :return: The return value from the coroutine.
    """
    if sys.version_info >= (3, 7):
        return asyncio.run(coro, debug=debug)

    # Python 3.6
    loop = asyncio.get_event_loop()
    loop.set_debug(debug)
    ret = loop.run_until_complete(coro)
    loop.close()

    return ret


# ----------------------------
# Section: Logging & Debugging
# ----------------------------


def exception_summary(exc: BaseException) -> str:
    """
    Return a summary string of an arbitrary exception.

    It will be of the form "ExceptionType: Error Message", if the error
    string is non-empty, and just "ExceptionType" otherwise.
    """
    name = type(exc).__qualname__
    smod = type(exc).__module__
    if smod not in ("__main__", "builtins"):
        name = smod + '.' + name

    error = str(exc)
    if error:
        return f"{name}: {error}"
    return name


def pretty_traceback(prefix: str = "  | ") -> str:
    """
    Formats the current traceback, indented to provide visual distinction.

    This is useful for printing a traceback within a traceback for
    debugging purposes when encapsulating errors to deliver them up the
    stack; when those errors are printed, this helps provide a nice
    visual grouping to quickly identify the parts of the error that
    belong to the inner exception.

    :param prefix: The prefix to append to each line of the traceback.
    :return: A string, formatted something like the following::

      | Traceback (most recent call last):
      |   File "foobar.py", line 42, in arbitrary_example
      |     foo.baz()
      | ArbitraryError: [Errno 42] Something bad happened!
    """
    output = "".join(traceback.format_exception(*sys.exc_info()))

    exc_lines = []
    for line in output.split('\n'):
        exc_lines.append(prefix + line)

    # The last line is always empty, omit it
    return "\n".join(exc_lines[:-1])
