"""
This is an internal module to run a thread in parallel to QEMU's main cpu loop.
It enables queuing up python functions from main thread and vice versa.
"""

import threading
import functools
from queue import Queue, Empty
from time import sleep, time
from colorama import Fore, Style
from pandare.utils import debug, warn


def progress(msg):
    print(Fore.CYAN + '[asyncthread.py] ' + Fore.RESET + Style.BRIGHT + msg +Style.RESET_ALL)

class AsyncThread:
    """
    Create a single worker thread which runs commands from a queue
    """

    def __init__(self, panda_started):
        # Attributes are configured by thread
        self.running = True
        self.panda_started = panda_started

        # Thread in which users can queue fns
        self.task_queue = Queue()
        self.athread = threading.Thread(target=self.run, args=(self.task_queue, False))
        self.athread.daemon = True # Quit on main quit
        self.warned = False # Did we print a warning about empty queue?
        self.ending = False # Is main PANDA execution ending?
        self.empty_at = None # Last time when our task queue went from full to empty
        self.last_called = None # Name of the last function called
        self.athread.start()

        # Internal thread which only pypanda should use
        # This allows us to exit even when the main athread is blocking on some slow task
        # Unfortunately we haven't found a cleaner way to just terminate whatever
        # function is running and then add internal tasks to the main queue
        self._task_queue = Queue()
        self._athread = threading.Thread(target=self.run, args=(self._task_queue, True))
        self._athread.daemon = True # Quit on main quit
        self._athread.start()

    def stop(self):
        self.running = False
        self.athread.join()

    def queue(self, func, internal=False): # Queue a function to be run soon. Must be @blocking
        if not func:
            raise RuntimeError("Queued up an undefined function")
        if not (hasattr(func, "__blocking__")) or not func.__blocking__:
            raise RuntimeError("Refusing to queue function '{}' without @blocking decorator".format(func.__name__))
        if internal:
            self._task_queue.put_nowait(func)
        else:
            self.task_queue.put_nowait(func)

    def run(self, task_queue, internal=False): # Run functions from queue
        #name = threading.get_ident()
        while self.running: # Note setting this to false will take some time
            try: # Try to get an item repeatedly, but also check if we want to stop running
                func = task_queue.get(True, 1) # Implicit (blocking) wait for 1s
                if not internal:
                    self.empty_at = None
                self.last_called = func.__name__.replace(" (with async thread)", "")
            except Empty:
                # If we've been empty for 5s without shutdown, warn (just once). *Unless* we're
                # in a replay or we've never queued up a serial command (e.g., the guest is
                # actually booting instead of being driven from a snapshot). In either of
                # these cases, self.last_called will be None
                if not internal and self.last_called is not None:
                    if self.empty_at is None:
                        self.empty_at = time()
                    else:
                        if time() - self.empty_at > 5 and not self.warned and not self.ending:
                            warn(f"PANDA finished all the queued functions but emulation was left running. You may have forgotten to call to panda.end_analysis() in the last queued function '{self.last_called}'")
                            self.warned = True
                continue

            # Don't interact with guest if it isn't running
            # Wait for self.panda_started, but also abort if running becomes false
            while not self.panda_started.is_set() and self.running:
                try:
                    self.panda_started.wait(timeout=1.0)
                except Empty:
                    continue

            if not self.running:
                break
            try:
                if debug:
                    print("Calling {}".format(func.__name__))
                # XXX: If running become false while func is running we need a way to kill it
                func()
            except Exception as e:
                print("exception {}".format(e))
                raise
            finally:
                task_queue.task_done()
                self.last_called = None

def test1():
    # Basic test: create an AsyncThread and run a coroutine 3 times
    # Should output t0 three times, then maybe t1 three times, then shutdown
    from time import sleep

    started = threading.Event()
    a = AsyncThread(started)

    def afunc():
        for x in range(3):
            print("afunc: t{}".format(x))
            sleep(1)

    afunc.__blocking__ = "placeholder" # Hack to pretend it's decorated

    print("\nQueuing up functions...")
    a.queue(afunc)
    a.queue(afunc)
    a.queue(afunc)

    started.set() # Begin

    print("\nAll queued. Wait 5s")
    sleep(5)

    print("\nBegin shutdown")
    a.stop()

    # Expected output: t0, t1, t2, t0, t1


def test2():
    # Second test: hang in the main queue and exit in the internal
    from time import sleep
    import sys
    started = threading.Event()

    b = AsyncThread(started)
    def hang_func():
        print("Main hanging")
        sleep(1000)
    hang_func.__blocking__ = "placeholder" # Hack to pretend it's decorated

    def internal_func():
        print("Internal running")
    internal_func.__blocking__ = "placeholder" # Hack to pretend it's decorated

    b.queue(hang_func)
    b.queue(internal_func, internal=True)

    # Make sure we have time to run both fns
    started.set()
    sleep(1)
    print("Finished")

    # Expected output: Main hanging, internal running

def test3():
    # Second test: hang in the main queue and exit in the internal
    from time import sleep
    import sys
    started = threading.Event()

    b = AsyncThread(started)
    def slow_func():
        print("Main slow_func sleeping")
        sleep(10)
        print("Main done")
    slow_func.__blocking__ = "placeholder" # Hack to pretend it's decorated

    b.queue(slow_func)

    # Make sure we have time to run both fns
    started.set()
    sleep(10)
    # Expected output: slow_func runs for 10s and no warning is printed

if __name__ == '__main__':
    #test1()
    test2() # Exits on finish
    #test3() # Exits on finish
