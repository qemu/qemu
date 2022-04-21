"""
QMP Error Classes

This package seeks to provide semantic error classes that are intended
to be used directly by clients when they would like to handle particular
semantic failures (e.g. "failed to connect") without needing to know the
enumeration of possible reasons for that failure.

QMPError serves as the ancestor for all exceptions raised by this
package, and is suitable for use in handling semantic errors from this
library. In most cases, individual public methods will attempt to catch
and re-encapsulate various exceptions to provide a semantic
error-handling interface.

.. admonition:: QMP Exception Hierarchy Reference

 |   `Exception`
 |    +-- `QMPError`
 |         +-- `ConnectError`
 |         +-- `StateError`
 |         +-- `ExecInterruptedError`
 |         +-- `ExecuteError`
 |         +-- `ListenerError`
 |         +-- `ProtocolError`
 |              +-- `DeserializationError`
 |              +-- `UnexpectedTypeError`
 |              +-- `ServerParseError`
 |              +-- `BadReplyError`
 |              +-- `GreetingError`
 |              +-- `NegotiationError`
"""


class QMPError(Exception):
    """Abstract error class for all errors originating from this package."""


class ProtocolError(QMPError):
    """
    Abstract error class for protocol failures.

    Semantically, these errors are generally the fault of either the
    protocol server or as a result of a bug in this library.

    :param error_message: Human-readable string describing the error.
    """
    def __init__(self, error_message: str):
        super().__init__(error_message)
        #: Human-readable error message, without any prefix.
        self.error_message: str = error_message
