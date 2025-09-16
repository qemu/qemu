"""
Miscellaneous Utilities

This module provides asyncio and various logging and debugging
utilities, such as `exception_summary()` and `pretty_traceback()`, used
primarily for adding information into the logging stream.
"""

import asyncio
import sys
import traceback
from typing import TypeVar, cast
import warnings


T = TypeVar('T')


# --------------------------
# Section: Utility Functions
# --------------------------


def get_or_create_event_loop() -> asyncio.AbstractEventLoop:
    """
    Return this thread's current event loop, or create a new one.

    This function behaves similarly to asyncio.get_event_loop() in
    Python<=3.13, where if there is no event loop currently associated
    with the current context, it will create and register one. It should
    generally not be used in any asyncio-native applications.
    """
    try:
        with warnings.catch_warnings():
            # Python <= 3.13 will trigger deprecation warnings if no
            # event loop is set, but will create and set a new loop.
            warnings.simplefilter("ignore")
            loop = asyncio.get_event_loop()
    except RuntimeError:
        # Python 3.14+: No event loop set for this thread,
        # create and set one.
        loop = asyncio.new_event_loop()
        # Set this loop as the current thread's loop, to be returned
        # by calls to get_event_loop() in the future.
        asyncio.set_event_loop(loop)

    return loop


async def flush(writer: asyncio.StreamWriter) -> None:
    """
    Utility function to ensure an `asyncio.StreamWriter` is *fully* drained.

    `asyncio.StreamWriter.drain` only promises we will return to below
    the "high-water mark". This function ensures we flush the entire
    buffer -- by setting the high water mark to 0 and then calling
    drain. The flow control limits are restored after the call is
    completed.
    """
    transport = cast(  # type: ignore[redundant-cast]
        asyncio.WriteTransport, writer.transport
    )

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
    collected as an `asyncio.Task` result instead.

    They must not call upper-half functions directly.
    """
    return func


# ----------------------------
# Section: Logging & Debugging
# ----------------------------


def exception_summary(exc: BaseException) -> str:
    """
    Return a summary string of an arbitrary exception.

    It will be of the form "ExceptionType: Error Message" if the error
    string is non-empty, and just "ExceptionType" otherwise.

    This code is based on CPython's implementation of
    `traceback.TracebackException.format_exception_only`.
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
