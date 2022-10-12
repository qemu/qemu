#!/usr/bin/env python3
'''
Simple helper and example to selectively execute callbacks based on a mode string
'''

from functools import wraps

class ModeFilter:
    '''
    Simple, inheritable class to provide a decorator to enable/disable callbacks
    depending on self.mode value.

    It is ill-advised to use on callbacks with high-performance impacts such as
    before_block_exec as this is a pure-Python plugin.

    Example:
        from pandare import Panda
        from pandare.extras import ModeFilter

        class MyClass(ModeFilter):
            def __init__(self, panda)
                self.panda = panda
                self.set_mode("mode1")

                @self.mode_filter("mode1")
                @self.panda.ppp("syscalls2", "on_sys_open_enter")
                def on_open(cpu, pc, fname_ptr, flags, mode):
                    # assert(self.mode == "mode1") # Note decorator ensures this
                    self.set_mode("mode2") # Change mode - so this callback won't run again
            ...
            def run(self):
                self.panda.run()

        p = panda(...)
        mc = MyClass(panda)
        mc.run()
    '''
    mode = "start"

    def mode_filter(self, mode_filter):
        '''
        Decorator to only run a function if self.mode matches the provided string
        '''
        def __mode_filter(func):
            @wraps(func)
            def wrapper(*args, **kwargs):
                if self.mode == mode_filter:
                    # Mode matches - run it!
                    func(*args, **kwargs)
            return wrapper
        return __mode_filter

    def set_mode(self, new):
        '''
        Helper to change mode
        '''
        if new != self.mode:
            print(f"Switching modes from {self.mode} to {new}")
        self.mode = new


class Tester(ModeFilter):
    '''
    Test class to drive a guest running a few commands
    while using mode filters for syscalls analyses
    '''
    def __init__(self, panda_obj):
        self.panda = panda_obj

        # PANDA decorators defined in init
        @self.panda.queue_async
        def driver():
            self.panda.revert_sync("root")
            self.set_mode("mode1")

            for cmd in ["head -n1 /etc/passwd", "whoami", "head -n1 /proc/cpuinfo"]:
                print(f"Starting: '{cmd}'")
                res = self.panda.run_serial_cmd(cmd)
                print(f"\tResult: {res}")

            assert self.mode == "end" # Should be set by on_open2
            self.panda.end_analysis()


        @self.panda.ppp("syscalls2", "on_sys_open_enter")
        @self.mode_filter("mode1")
        def on_open1(cpu, pc, fname_ptr, flags, mode):
            assert self.mode == "mode1"
            try:
                fname = self.panda.read_str(cpu, fname_ptr)
            except ValueError:
                return

            if fname == "/etc/passwd":
                print("Saw PASSWD - switch modes")
                self.set_mode("mode2")

        @self.panda.ppp("syscalls2", "on_sys_open_enter")
        @self.mode_filter("mode2")
        def on_open2(cpu, pc, fname_ptr, flags, mode):
            assert self.mode == "mode2"

            try:
                fname = self.panda.read_str(cpu, fname_ptr)
            except ValueError:
                return

            if fname == "/proc/cpuinfo":
                print("SAW CPUINFO - switch modes")
                self.set_mode("end")
        # End PANDA-decorated functions in init

    def run_guest(self):
        '''
        Run guest
        '''
        self.panda.run()

if __name__ == "__main__":
    from pandare import Panda
    # Note the test is architecture-specific, otherwise different syscalls are issued
    panda = Panda(generic="i386")
    test = Tester(panda)
    test.run_guest()
