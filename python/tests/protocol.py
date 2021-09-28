import asyncio
from contextlib import contextmanager
import os
import socket
from tempfile import TemporaryDirectory

import avocado

from qemu.aqmp import ConnectError, Runstate
from qemu.aqmp.protocol import AsyncProtocol, StateError
from qemu.aqmp.util import asyncio_run, create_task


class NullProtocol(AsyncProtocol[None]):
    """
    NullProtocol is a test mockup of an AsyncProtocol implementation.

    It adds a fake_session instance variable that enables a code path
    that bypasses the actual connection logic, but still allows the
    reader/writers to start.

    Because the message type is defined as None, an asyncio.Event named
    'trigger_input' is created that prohibits the reader from
    incessantly being able to yield None; this event can be poked to
    simulate an incoming message.

    For testing symmetry with do_recv, an interface is added to "send" a
    Null message.

    For testing purposes, a "simulate_disconnection" method is also
    added which allows us to trigger a bottom half disconnect without
    injecting any real errors into the reader/writer loops; in essence
    it performs exactly half of what disconnect() normally does.
    """
    def __init__(self, name=None):
        self.fake_session = False
        self.trigger_input: asyncio.Event
        super().__init__(name)

    async def _establish_session(self):
        self.trigger_input = asyncio.Event()
        await super()._establish_session()

    async def _do_accept(self, address, ssl=None):
        if not self.fake_session:
            await super()._do_accept(address, ssl)

    async def _do_connect(self, address, ssl=None):
        if not self.fake_session:
            await super()._do_connect(address, ssl)

    async def _do_recv(self) -> None:
        await self.trigger_input.wait()
        self.trigger_input.clear()

    def _do_send(self, msg: None) -> None:
        pass

    async def send_msg(self) -> None:
        await self._outgoing.put(None)

    async def simulate_disconnect(self) -> None:
        """
        Simulates a bottom-half disconnect.

        This method schedules a disconnection but does not wait for it
        to complete. This is used to put the loop into the DISCONNECTING
        state without fully quiescing it back to IDLE. This is normally
        something you cannot coax AsyncProtocol to do on purpose, but it
        will be similar to what happens with an unhandled Exception in
        the reader/writer.

        Under normal circumstances, the library design requires you to
        await on disconnect(), which awaits the disconnect task and
        returns bottom half errors as a pre-condition to allowing the
        loop to return back to IDLE.
        """
        self._schedule_disconnect()


class LineProtocol(AsyncProtocol[str]):
    def __init__(self, name=None):
        super().__init__(name)
        self.rx_history = []

    async def _do_recv(self) -> str:
        raw = await self._readline()
        msg = raw.decode()
        self.rx_history.append(msg)
        return msg

    def _do_send(self, msg: str) -> None:
        assert self._writer is not None
        self._writer.write(msg.encode() + b'\n')

    async def send_msg(self, msg: str) -> None:
        await self._outgoing.put(msg)


def run_as_task(coro, allow_cancellation=False):
    """
    Run a given coroutine as a task.

    Optionally, wrap it in a try..except block that allows this
    coroutine to be canceled gracefully.
    """
    async def _runner():
        try:
            await coro
        except asyncio.CancelledError:
            if allow_cancellation:
                return
            raise
    return create_task(_runner())


@contextmanager
def jammed_socket():
    """
    Opens up a random unused TCP port on localhost, then jams it.
    """
    socks = []

    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        sock.bind(('127.0.0.1', 0))
        sock.listen(1)
        address = sock.getsockname()

        socks.append(sock)

        # I don't *fully* understand why, but it takes *two* un-accepted
        # connections to start jamming the socket.
        for _ in range(2):
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.connect(address)
            socks.append(sock)

        yield address

    finally:
        for sock in socks:
            sock.close()


class Smoke(avocado.Test):

    def setUp(self):
        self.proto = NullProtocol()

    def test__repr__(self):
        self.assertEqual(
            repr(self.proto),
            "<NullProtocol runstate=IDLE>"
        )

    def testRunstate(self):
        self.assertEqual(
            self.proto.runstate,
            Runstate.IDLE
        )

    def testDefaultName(self):
        self.assertEqual(
            self.proto.name,
            None
        )

    def testLogger(self):
        self.assertEqual(
            self.proto.logger.name,
            'qemu.aqmp.protocol'
        )

    def testName(self):
        self.proto = NullProtocol('Steve')

        self.assertEqual(
            self.proto.name,
            'Steve'
        )

        self.assertEqual(
            self.proto.logger.name,
            'qemu.aqmp.protocol.Steve'
        )

        self.assertEqual(
            repr(self.proto),
            "<NullProtocol name='Steve' runstate=IDLE>"
        )


class TestBase(avocado.Test):

    def setUp(self):
        self.proto = NullProtocol(type(self).__name__)
        self.assertEqual(self.proto.runstate, Runstate.IDLE)
        self.runstate_watcher = None

    def tearDown(self):
        self.assertEqual(self.proto.runstate, Runstate.IDLE)

    async def _asyncSetUp(self):
        pass

    async def _asyncTearDown(self):
        if self.runstate_watcher:
            await self.runstate_watcher

    @staticmethod
    def async_test(async_test_method):
        """
        Decorator; adds SetUp and TearDown to async tests.
        """
        async def _wrapper(self, *args, **kwargs):
            loop = asyncio.get_event_loop()
            loop.set_debug(True)

            await self._asyncSetUp()
            await async_test_method(self, *args, **kwargs)
            await self._asyncTearDown()

        return _wrapper

    # Definitions

    # The states we expect a "bad" connect/accept attempt to transition through
    BAD_CONNECTION_STATES = (
        Runstate.CONNECTING,
        Runstate.DISCONNECTING,
        Runstate.IDLE,
    )

    # The states we expect a "good" session to transition through
    GOOD_CONNECTION_STATES = (
        Runstate.CONNECTING,
        Runstate.RUNNING,
        Runstate.DISCONNECTING,
        Runstate.IDLE,
    )

    # Helpers

    async def _watch_runstates(self, *states):
        """
        This launches a task alongside (most) tests below to confirm that
        the sequence of runstate changes that occur is exactly as
        anticipated.
        """
        async def _watcher():
            for state in states:
                new_state = await self.proto.runstate_changed()
                self.assertEqual(
                    new_state,
                    state,
                    msg=f"Expected state '{state.name}'",
                )

        self.runstate_watcher = create_task(_watcher())
        # Kick the loop and force the task to block on the event.
        await asyncio.sleep(0)


class State(TestBase):

    @TestBase.async_test
    async def testSuperfluousDisconnect(self):
        """
        Test calling disconnect() while already disconnected.
        """
        await self._watch_runstates(
            Runstate.DISCONNECTING,
            Runstate.IDLE,
        )
        await self.proto.disconnect()


class Connect(TestBase):
    """
    Tests primarily related to calling Connect().
    """
    async def _bad_connection(self, family: str):
        assert family in ('INET', 'UNIX')

        if family == 'INET':
            await self.proto.connect(('127.0.0.1', 0))
        elif family == 'UNIX':
            await self.proto.connect('/dev/null')

    async def _hanging_connection(self):
        with jammed_socket() as addr:
            await self.proto.connect(addr)

    async def _bad_connection_test(self, family: str):
        await self._watch_runstates(*self.BAD_CONNECTION_STATES)

        with self.assertRaises(ConnectError) as context:
            await self._bad_connection(family)

        self.assertIsInstance(context.exception.exc, OSError)
        self.assertEqual(
            context.exception.error_message,
            "Failed to establish connection"
        )

    @TestBase.async_test
    async def testBadINET(self):
        """
        Test an immediately rejected call to an IP target.
        """
        await self._bad_connection_test('INET')

    @TestBase.async_test
    async def testBadUNIX(self):
        """
        Test an immediately rejected call to a UNIX socket target.
        """
        await self._bad_connection_test('UNIX')

    @TestBase.async_test
    async def testCancellation(self):
        """
        Test what happens when a connection attempt is aborted.
        """
        # Note that accept() cannot be cancelled outright, as it isn't a task.
        # However, we can wrap it in a task and cancel *that*.
        await self._watch_runstates(*self.BAD_CONNECTION_STATES)
        task = run_as_task(self._hanging_connection(), allow_cancellation=True)

        state = await self.proto.runstate_changed()
        self.assertEqual(state, Runstate.CONNECTING)

        # This is insider baseball, but the connection attempt has
        # yielded *just* before the actual connection attempt, so kick
        # the loop to make sure it's truly wedged.
        await asyncio.sleep(0)

        task.cancel()
        await task

    @TestBase.async_test
    async def testTimeout(self):
        """
        Test what happens when a connection attempt times out.
        """
        await self._watch_runstates(*self.BAD_CONNECTION_STATES)
        task = run_as_task(self._hanging_connection())

        # More insider baseball: to improve the speed of this test while
        # guaranteeing that the connection even gets a chance to start,
        # verify that the connection hangs *first*, then await the
        # result of the task with a nearly-zero timeout.

        state = await self.proto.runstate_changed()
        self.assertEqual(state, Runstate.CONNECTING)
        await asyncio.sleep(0)

        with self.assertRaises(asyncio.TimeoutError):
            await asyncio.wait_for(task, timeout=0)

    @TestBase.async_test
    async def testRequire(self):
        """
        Test what happens when a connection attempt is made while CONNECTING.
        """
        await self._watch_runstates(*self.BAD_CONNECTION_STATES)
        task = run_as_task(self._hanging_connection(), allow_cancellation=True)

        state = await self.proto.runstate_changed()
        self.assertEqual(state, Runstate.CONNECTING)

        with self.assertRaises(StateError) as context:
            await self._bad_connection('UNIX')

        self.assertEqual(
            context.exception.error_message,
            "NullProtocol is currently connecting."
        )
        self.assertEqual(context.exception.state, Runstate.CONNECTING)
        self.assertEqual(context.exception.required, Runstate.IDLE)

        task.cancel()
        await task

    @TestBase.async_test
    async def testImplicitRunstateInit(self):
        """
        Test what happens if we do not wait on the runstate event until
        AFTER a connection is made, i.e., connect()/accept() themselves
        initialize the runstate event. All of the above tests force the
        initialization by waiting on the runstate *first*.
        """
        task = run_as_task(self._hanging_connection(), allow_cancellation=True)

        # Kick the loop to coerce the state change
        await asyncio.sleep(0)
        assert self.proto.runstate == Runstate.CONNECTING

        # We already missed the transition to CONNECTING
        await self._watch_runstates(Runstate.DISCONNECTING, Runstate.IDLE)

        task.cancel()
        await task


class Accept(Connect):
    """
    All of the same tests as Connect, but using the accept() interface.
    """
    async def _bad_connection(self, family: str):
        assert family in ('INET', 'UNIX')

        if family == 'INET':
            await self.proto.accept(('example.com', 1))
        elif family == 'UNIX':
            await self.proto.accept('/dev/null')

    async def _hanging_connection(self):
        with TemporaryDirectory(suffix='.aqmp') as tmpdir:
            sock = os.path.join(tmpdir, type(self.proto).__name__ + ".sock")
            await self.proto.accept(sock)


class FakeSession(TestBase):

    def setUp(self):
        super().setUp()
        self.proto.fake_session = True

    async def _asyncSetUp(self):
        await super()._asyncSetUp()
        await self._watch_runstates(*self.GOOD_CONNECTION_STATES)

    async def _asyncTearDown(self):
        await self.proto.disconnect()
        await super()._asyncTearDown()

    ####

    @TestBase.async_test
    async def testFakeConnect(self):

        """Test the full state lifecycle (via connect) with a no-op session."""
        await self.proto.connect('/not/a/real/path')
        self.assertEqual(self.proto.runstate, Runstate.RUNNING)

    @TestBase.async_test
    async def testFakeAccept(self):
        """Test the full state lifecycle (via accept) with a no-op session."""
        await self.proto.accept('/not/a/real/path')
        self.assertEqual(self.proto.runstate, Runstate.RUNNING)

    @TestBase.async_test
    async def testFakeRecv(self):
        """Test receiving a fake/null message."""
        await self.proto.accept('/not/a/real/path')

        logname = self.proto.logger.name
        with self.assertLogs(logname, level='DEBUG') as context:
            self.proto.trigger_input.set()
            self.proto.trigger_input.clear()
            await asyncio.sleep(0)  # Kick reader.

        self.assertEqual(
            context.output,
            [f"DEBUG:{logname}:<-- None"],
        )

    @TestBase.async_test
    async def testFakeSend(self):
        """Test sending a fake/null message."""
        await self.proto.accept('/not/a/real/path')

        logname = self.proto.logger.name
        with self.assertLogs(logname, level='DEBUG') as context:
            # Cheat: Send a Null message to nobody.
            await self.proto.send_msg()
            # Kick writer; awaiting on a queue.put isn't sufficient to yield.
            await asyncio.sleep(0)

        self.assertEqual(
            context.output,
            [f"DEBUG:{logname}:--> None"],
        )

    async def _prod_session_api(
            self,
            current_state: Runstate,
            error_message: str,
            accept: bool = True
    ):
        with self.assertRaises(StateError) as context:
            if accept:
                await self.proto.accept('/not/a/real/path')
            else:
                await self.proto.connect('/not/a/real/path')

        self.assertEqual(context.exception.error_message, error_message)
        self.assertEqual(context.exception.state, current_state)
        self.assertEqual(context.exception.required, Runstate.IDLE)

    @TestBase.async_test
    async def testAcceptRequireRunning(self):
        """Test that accept() cannot be called when Runstate=RUNNING"""
        await self.proto.accept('/not/a/real/path')

        await self._prod_session_api(
            Runstate.RUNNING,
            "NullProtocol is already connected and running.",
            accept=True,
        )

    @TestBase.async_test
    async def testConnectRequireRunning(self):
        """Test that connect() cannot be called when Runstate=RUNNING"""
        await self.proto.accept('/not/a/real/path')

        await self._prod_session_api(
            Runstate.RUNNING,
            "NullProtocol is already connected and running.",
            accept=False,
        )

    @TestBase.async_test
    async def testAcceptRequireDisconnecting(self):
        """Test that accept() cannot be called when Runstate=DISCONNECTING"""
        await self.proto.accept('/not/a/real/path')

        # Cheat: force a disconnect.
        await self.proto.simulate_disconnect()

        await self._prod_session_api(
            Runstate.DISCONNECTING,
            ("NullProtocol is disconnecting."
             " Call disconnect() to return to IDLE state."),
            accept=True,
        )

    @TestBase.async_test
    async def testConnectRequireDisconnecting(self):
        """Test that connect() cannot be called when Runstate=DISCONNECTING"""
        await self.proto.accept('/not/a/real/path')

        # Cheat: force a disconnect.
        await self.proto.simulate_disconnect()

        await self._prod_session_api(
            Runstate.DISCONNECTING,
            ("NullProtocol is disconnecting."
             " Call disconnect() to return to IDLE state."),
            accept=False,
        )


class SimpleSession(TestBase):

    def setUp(self):
        super().setUp()
        self.server = LineProtocol(type(self).__name__ + '-server')

    async def _asyncSetUp(self):
        await super()._asyncSetUp()
        await self._watch_runstates(*self.GOOD_CONNECTION_STATES)

    async def _asyncTearDown(self):
        await self.proto.disconnect()
        try:
            await self.server.disconnect()
        except EOFError:
            pass
        await super()._asyncTearDown()

    @TestBase.async_test
    async def testSmoke(self):
        with TemporaryDirectory(suffix='.aqmp') as tmpdir:
            sock = os.path.join(tmpdir, type(self.proto).__name__ + ".sock")
            server_task = create_task(self.server.accept(sock))

            # give the server a chance to start listening [...]
            await asyncio.sleep(0)
            await self.proto.connect(sock)
