"""
Sync QMP Wrapper

This class pretends to be qemu.qmp.QEMUMonitorProtocol.
"""

import asyncio
from typing import (
    Any,
    Awaitable,
    Dict,
    List,
    Optional,
    TypeVar,
    Union,
)

import qemu.qmp

from .error import QMPError
from .protocol import Runstate, SocketAddrT
from .qmp_client import QMPClient


# (Temporarily) Re-export QMPBadPortError
QMPBadPortError = qemu.qmp.QMPBadPortError

#: QMPMessage is an entire QMP message of any kind.
QMPMessage = Dict[str, Any]

#: QMPReturnValue is the 'return' value of a command.
QMPReturnValue = object

#: QMPObject is any object in a QMP message.
QMPObject = Dict[str, object]

# QMPMessage can be outgoing commands or incoming events/returns.
# QMPReturnValue is usually a dict/json object, but due to QAPI's
# 'returns-whitelist', it can actually be anything.
#
# {'return': {}} is a QMPMessage,
# {} is the QMPReturnValue.


# pylint: disable=missing-docstring


class QEMUMonitorProtocol(qemu.qmp.QEMUMonitorProtocol):
    def __init__(self, address: SocketAddrT,
                 server: bool = False,
                 nickname: Optional[str] = None):

        # pylint: disable=super-init-not-called
        self._aqmp = QMPClient(nickname)
        self._aloop = asyncio.get_event_loop()
        self._address = address
        self._timeout: Optional[float] = None

        if server:
            self._sync(self._aqmp.start_server(self._address))

    _T = TypeVar('_T')

    def _sync(
            self, future: Awaitable[_T], timeout: Optional[float] = None
    ) -> _T:
        return self._aloop.run_until_complete(
            asyncio.wait_for(future, timeout=timeout)
        )

    def _get_greeting(self) -> Optional[QMPMessage]:
        if self._aqmp.greeting is not None:
            # pylint: disable=protected-access
            return self._aqmp.greeting._asdict()
        return None

    # __enter__ and __exit__ need no changes
    # parse_address needs no changes

    def connect(self, negotiate: bool = True) -> Optional[QMPMessage]:
        self._aqmp.await_greeting = negotiate
        self._aqmp.negotiate = negotiate

        self._sync(
            self._aqmp.connect(self._address)
        )
        return self._get_greeting()

    def accept(self, timeout: Optional[float] = 15.0) -> QMPMessage:
        self._aqmp.await_greeting = True
        self._aqmp.negotiate = True

        self._sync(self._aqmp.accept(), timeout)

        ret = self._get_greeting()
        assert ret is not None
        return ret

    def cmd_obj(self, qmp_cmd: QMPMessage) -> QMPMessage:
        return dict(
            self._sync(
                # pylint: disable=protected-access

                # _raw() isn't a public API, because turning off
                # automatic ID assignment is discouraged. For
                # compatibility with iotests *only*, do it anyway.
                self._aqmp._raw(qmp_cmd, assign_id=False),
                self._timeout
            )
        )

    # Default impl of cmd() delegates to cmd_obj

    def command(self, cmd: str, **kwds: object) -> QMPReturnValue:
        return self._sync(
            self._aqmp.execute(cmd, kwds),
            self._timeout
        )

    def pull_event(self,
                   wait: Union[bool, float] = False) -> Optional[QMPMessage]:
        if not wait:
            # wait is False/0: "do not wait, do not except."
            if self._aqmp.events.empty():
                return None

        # If wait is 'True', wait forever. If wait is False/0, the events
        # queue must not be empty; but it still needs some real amount
        # of time to complete.
        timeout = None
        if wait and isinstance(wait, float):
            timeout = wait

        return dict(
            self._sync(
                self._aqmp.events.get(),
                timeout
            )
        )

    def get_events(self, wait: Union[bool, float] = False) -> List[QMPMessage]:
        events = [dict(x) for x in self._aqmp.events.clear()]
        if events:
            return events

        event = self.pull_event(wait)
        return [event] if event is not None else []

    def clear_events(self) -> None:
        self._aqmp.events.clear()

    def close(self) -> None:
        self._sync(
            self._aqmp.disconnect()
        )

    def settimeout(self, timeout: Optional[float]) -> None:
        self._timeout = timeout

    def send_fd_scm(self, fd: int) -> None:
        self._aqmp.send_fd_scm(fd)

    def __del__(self) -> None:
        if self._aqmp.runstate == Runstate.IDLE:
            return

        if not self._aloop.is_running():
            self.close()
        else:
            # Garbage collection ran while the event loop was running.
            # Nothing we can do about it now, but if we don't raise our
            # own error, the user will be treated to a lot of traceback
            # they might not understand.
            raise QMPError(
                "QEMUMonitorProtocol.close()"
                " was not called before object was garbage collected"
            )
