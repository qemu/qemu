"""
This module simply contains the Panda class.
"""

from sys import version_info, exit

if version_info[0] < 3:
    print("Please run with Python 3!")
    exit(0)

import socket
import select
import threading

import readline
# XXX: readline is unused but necessary. We need to load python
# readline, to preempt PANDA loading another version. PANDA
# seems to work with python's but not vice-versa. This allows for
# stdio interactions later (e.g., pdb, input())  without segfaults

from os.path import realpath, exists, abspath, isfile, dirname, isdir, join as pjoin
from os import dup, getenv, environ, path
from random import randint
from inspect import signature
from tempfile import NamedTemporaryFile
from time import time
from math import ceil
from inspect import signature
from struct import pack_into
from shlex import quote as shlex_quote, split as shlex_split
from time import sleep
from cffi import FFI

from .utils import progress, warn, make_iso, debug, blocking, GArrayIterator, plugin_list, find_build_dir, rr2_recording, rr2_contains_member
from .taint import TaintQuery
from .panda_expect import Expect
from .asyncthread import AsyncThread
from .qcows_internal import Qcows
from .qemu_logging import QEMU_Log_Manager
from .arch import ArmArch, Aarch64Arch, MipsArch, Mips64Arch, X86Arch, X86_64Arch, PowerPCArch
from .cosi import Cosi
from dataclasses import dataclass

# Might be worth importing and auto-initilizing a PLogReader
# object within Panda for the current architecture?
#from .plog import PLogReader


class Panda():
    '''
    This is the object used to interact with PANDA. Initializing it creates a virtual machine to interact with.
    '''

    def __init__(self, arch="i386", mem="128M",
            expect_prompt=None, # Regular expression describing the prompt exposed by the guest on a serial console. Used so we know when a running command has finished with its output
            serial_kwargs=None,
            os_version=None,
            qcow=None, # Qcow file to load
            os="linux",
            generic=None, # Helper: specify a generic qcow to use and set other arguments. Supported values: arm/ppc/x86_64/i386. Will download qcow automatically
            raw_monitor=False, # When set, don't specify a -monitor. arg Allows for use of -nographic in args with ctrl-A+C for interactive qemu prompt.
            extra_args=None,
            catch_exceptions=True, # Should we catch and end_analysis() when python code raises an exception?
            libpanda_path=None,
            biospath=None,
            plugin_path=None,
            nproc=1,
            ):
        '''
        Construct a new `Panda` object.  Note that multiple Panda objects cannot coexist in the same Python instance.
        Args:
            arch: architecture string (e.g. "i386", "x86_64", "arm", "mips", "mipsel")
            generic: specify a generic qcow to use from `pandare.qcows.SUPPORTED_IMAGES` and set all subsequent arguments. Will automatically download qcow if necessary.
            mem: size of memory for machine (e.g. "128M", "1G")
            expect_prompt: Regular expression describing the prompt exposed by the guest
                    on a serial console. Used so we know when a running command has finished
                    with its output.
            serial_kwargs: dict of additional arguments to pass to pandare.Expect (see signature of its constructor).
                    Note that `expect_prompt` is already passed to Expect as "expectation".
                    If not explicitly given, "unansi" is set to True (simulates a subset of ANSI codes and attempts to
                    remove command strings repeated by the shell from the shell output).
            os_version: analagous to PANDA's -os argument (e.g, linux-32-debian:3.2.0-4-686-pae")
            os: type of OS (e.g. "linux")
            qcow: path to a qcow file to load
            catch_exceptions: Should we catch exceptions raised by python code and end_analysis() and then print a backtrace (Default: True)
            raw_monitor: When set, don't specify a -monitor. arg Allows for use of
                    -nographic in args with ctrl-A+C for interactive qemu prompt. Experts only!
            extra_args: extra arguments to pass to PANDA as either a string or an
                    array. (e.g. "-nographic" or ["-nographic", "-net", "none"])
            libpanda_path: path to panda shared object to load
            biospath: directory that contains "pc-bios" files
            plugin_path: directory that contains panda plugins
        Returns:
            Panda: the created panda object
        '''
        self.arch_name = arch
        self.mem = mem
        self.os = os_version
        self.os_type = os
        self.qcow = qcow
        self.plugins = plugin_list(self)
        self.expect_prompt = expect_prompt
        self.lambda_cnt = 0
        self.__sighandler = None
        self.ending = False # True during end_analysis
        self.cdrom = None
        self.catch_exceptions=catch_exceptions
        self.qlog = QEMU_Log_Manager(self)
        self.build_dir = None
        self.plugin_path = plugin_path

        self.serial_unconsumed_data = b''

        if isinstance(extra_args, str): # Extra args can be a string or array. Use shlex to preserve quoted substrings
            extra_args = shlex_split(extra_args)
        elif extra_args is None:
            extra_args = []

        # If specified, use a generic (x86_64, i386, arm, etc) qcow from MIT and ignore
        if generic:                                 # other args. See details in qcows.py
            print("using generic " +str(generic))
            q = Qcows.get_qcow_info(generic)
            self.arch_name     = q.arch
            self.os       = q.os
            self.mem      = q.default_mem # Might clobber a specified argument, but required if you want snapshots
            self.qcow     = Qcows.get_qcow(generic)
            self.expect_prompt = q.prompt
            self.cdrom    = q.cdrom
            if q.extra_args:
                extra_args.extend(shlex_split(q.extra_args))

        if self.qcow: # Otherwise we shuld be able to do a replay with no qcow but this is probably broken
            if not (exists(self.qcow)):
                print("Missing qcow '{}' Please go create that qcow and give it to the PANDA maintainers".format(self.qcow))

        # panda.arch is a subclass with architecture-specific functions

        self.arch = None # Keep this with the following docstring such that pydoc generats good docs for it; this is a useful variable!
        """
        A reference to an auto-instantiated `pandare.arch.PandaArch` subclass (e.g., `pandare.arch.X86Arch`)
        """

        if self.arch_name == "i386":
            self.arch = X86Arch(self)
        elif self.arch_name == "x86_64":
            self.arch = X86_64Arch(self)
        elif self.arch_name in ["arm"]:
            self.arch = ArmArch(self)
        elif self.arch_name in ["aarch64"]:
            self.arch = Aarch64Arch(self)
        elif self.arch_name in ["mips", "mipsel"]:
            self.arch = MipsArch(self)
        elif self.arch_name in ["mips64", "mips64el"]:
            self.arch = Mips64Arch(self)
        elif self.arch_name in ["ppc"]: 
            self.arch = PowerPCArch(self)
        else:
            raise ValueError(f"Unsupported architecture {self.arch_name}")
        self.bits, self.endianness, self.register_size = self.arch._determine_bits()
        self.target = "softmmu"

        if libpanda_path:
            environ["PANDA_LIB"] = self.libpanda_path = libpanda_path
        else:
            build_dir = self.get_build_dir()
            lib_paths = ["libpanda-{0}.so".format(self.arch_name), "libpanda-{0}-{1}.so".format(self.arch_name, self.target)]
            # Select the first path that exists - we'll have libpanda-{arch}.so for a system install versus arch-softmmu/libpanda-arch.so for a build
            for p in lib_paths:
                if isfile(pjoin(build_dir, p)):
                    self.libpanda_path = pjoin(build_dir, p)
                    break
            else:
                raise RuntimeError("Couldn't find libpanda-{0}.so in {1} (in either root or {0}-libpanda directory)".format(self.arch_name, build_dir))

        self.panda = self.libpanda_path # Necessary for realpath to work inside core-panda, may cause issues?

        self.ffi = self._do_types_import()

        self.libpanda = self.ffi.dlopen(self.libpanda_path, self.ffi.RTLD_GLOBAL)
        self.C = self.ffi.dlopen(None)

        # set OS name if we have one
        if self.os:
            self.set_os_name(self.os)

        # Setup argv for panda
        self.panda_args = [self.panda]
        plugin_interface = realpath(pjoin(self.panda, "../contrib/plugins/libpanda_plugin_interface.so"))
        self.plugin_interface = self.ffi.dlopen(plugin_interface, self.ffi.RTLD_GLOBAL)
        self.panda_args.extend(["-plugin", plugin_interface])
        if nproc != 1:
            self.panda_args.extend(["-smp", str(nproc)])

        if biospath is None:
            # hack since pc-bios is in the core not build now
            biospath = realpath(pjoin(self.get_build_dir(), "..", "pc-bios")) # XXX: necessary for network drivers for arm/mips, so 'pc-bios' is a misleading name
        self.panda_args.append("-L")
        self.panda_args.append(biospath)

        if self.qcow:
            if self.arch_name in ['mips64', 'mips64el']:
                # XXX: mips64 needs virtio interface for the qcow
                self.panda_args.extend(["-drive", f"file={self.qcow},if=virtio"])
            else:
                self.panda_args.append(self.qcow)

        self.panda_args += extra_args

        # Configure memory options
        self.panda_args.extend(['-m', self.mem])

        # Configure serial - if we have an expect_prompt set. Otherwise how can we know what guest cmds are outputting?
        if self.expect_prompt or (serial_kwargs is not None and serial_kwargs.get('expectation')):
            self.serial_file = NamedTemporaryFile(prefix="pypanda_s").name
            self.serial_socket = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            expect_kwargs = {'expectation': self.expect_prompt, 'consume_first': False, 'unansi': True}
            if serial_kwargs:
                expect_kwargs.update(serial_kwargs)
            self.serial_console = Expect('serial', **expect_kwargs)
            self.panda_args.extend(['-serial', 'unix:{},server,nowait'.format(self.serial_file)])
        else:
            self.serial_file = None
            self.serial_socket = None
            self.serial_console = None

        # Configure monitor - Always enabled for now
        self.monitor_file = NamedTemporaryFile(prefix="pypanda_m").name
        self.monitor_socket = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.raw_monitor = raw_monitor
        if not self.raw_monitor:
            # XXX don't forget to escape expectation regex parens!
            self.monitor_console = Expect('monitor', expectation=rb"\(qemu\) ", consume_first=True)
            self.panda_args.extend(['-monitor', 'unix:{},server,nowait'.format(self.monitor_file)])

        self.running = threading.Event()
        self.started = threading.Event()
        self.initializing = threading.Event()
        self.athread = AsyncThread(self.started) # athread manages actions that need to occur outside qemu's CPU loop

        # Callbacks
        self.register_cb_decorators()
        self.plugin_register_count = 0
        self.registered_callbacks = {} # name -> {procname: "bash", enabled: False, callback: None}

        # Register asid_changed CB if and only if a callback requires procname
        self._registered_asid_changed_internal_cb = False
        self._registered_mmap_cb = False

        self._initialized_panda = False
        self.disabled_tb_chaining = False
        self.named_hooks = {}
        self.hook_list = []
        self.hook_list2 = {}
        self.mem_hooks = {}
        self.sr_hooks = []
        self.hypercalls = {}

        # Asid stuff
        self.current_asid_name = None
        self.asid_mapping = {}

        # Shutdown stuff
        self.exception = None # When set to an exn, we'll raise and exit
        self._in_replay = False

        # cosi
        self.cosi = Cosi(self)

        # main_loop_wait functions and callbacks
        self.main_loop_wait_fnargs = [] # [(fn, args), ...]
        progress ("Panda args: [" + (" ".join(self.panda_args)) + "]")
    # /__init__

    def get_plugin_path(self):
        if self.plugin_path is None:
            build_dir = self.get_build_dir()
            rel_dir = pjoin(*[build_dir, "panda", "plugins"])

            if build_dir == "/usr/local/bin/":
                # Installed - use /usr/local/lib/panda/plugins
                self.plugin_path = f"/usr/local/lib/panda/{self.arch_name}"
            elif isdir(rel_dir):
                self.plugin_path = rel_dir
            else:
                raise ValueError(f"Could not find plugin path. Build dir={build_dir}")
        return self.plugin_path

    def get_build_dir(self):
        if self.build_dir is None:
            self.build_dir  = find_build_dir(self.arch_name)
            environ["PANDA_DIR"] = self.build_dir
        return self.build_dir

    def _do_types_import(self):
        '''
        Import objects from panda_datatypes which are configured by the environment variables(?)
        Check the DATATTYPES_VERSION to detect if panda_datatypes.py has gotten stale.

        Store these objects in self.callback and self.callback_dictionary

        Returns a handle to the FFI object for the libpanda object
        '''

        required_datatypes_version = 1.1
        version_err = "Your panda_datatypes.py is out of date (has version {} but PANDA " \
                      "requires version {}). Please reinstall pypanda or re-run "\
                      "create_panda_datatypes.py."
        # try:
            # from .autogen.panda_datatypes import DATATYPES_VERSION
        # except ImportError:
            # raise RuntimeError(version_err.format(None, required_datatypes_version))

        # if required_datatypes_version != DATATYPES_VERSION:
            # raise RuntimeError(version_err.format(DATATYPES_VERSION, required_datatypes_version))


        from importlib import import_module
        panda_arch_support = import_module(f".autogen._pandare_ffi_{self.arch_name}_{self.target}",package='pandare2')
        from collections import namedtuple

        ffi = panda_arch_support.ffi
        pcwc = ffi.new("panda_cb*")

        cb_list = dir(pcwc)
        cb_list.remove("cbaddr")
        PandaCB = namedtuple("PandaCB", cb_list)
        pcb  = PandaCB(**({i: ffi.callback(ffi.typeof(getattr(pcwc, i))) for i in cb_list}))
        pandacbtype = namedtuple("pandacbtype", "name number")

        C = ffi.dlopen(None)

        callback_dictionary = {
            getattr(pcb, m): pandacbtype(m,getattr(C, f"PANDA_CB_{m.upper()}")) for m in cb_list 
        }
        self.callback, self.callback_dictionary = (pcb, callback_dictionary)

        return ffi

    def _initialize_panda(self):
        '''
        After initializing the class, the user has a chance to do something
        (TODO: what? register callbacks? It's something important...) before we finish initializing
        '''
        # self.libpanda._panda_set_library_mode(True)

        cenvp = self.ffi.new("char**", self.ffi.new("char[]", b""))
        len_cargs = self.ffi.cast("int", len(self.panda_args))
        panda_args_ffi = [self.ffi.new("char[]", bytes(str(i),"utf-8")) for i in self.panda_args]
        self.libpanda.panda_init(len_cargs, panda_args_ffi, cenvp)
        # Now we've run qemu init so we can connect to the sockets for the monitor and serial
        if self.serial_console and not self.serial_console.is_connected():
            self.serial_socket.connect(self.serial_file)
            self.serial_socket.settimeout(None)
            self.serial_console.connect(self.serial_socket)
        if not self.raw_monitor and not self.monitor_console.is_connected():
            self.monitor_socket.connect(self.monitor_file)
            self.monitor_console.connect(self.monitor_socket)

        # Register __main_loop_wait_callback
        self.register_callback(self.callback.main_loop_wait,
                self.callback.main_loop_wait(self.__main_loop_wait_cb), '__main_loop_wait')

        self._initialized_panda = True

    def __main_loop_wait_cb(self):
        '''
        __main_loop_wait_cb is called at the start of the main cpu loop in qemu.
        This is a fairly safe place to call into qemu internals but watch out for deadlocks caused
        by your request blocking on the guest's execution. Here any functions in main_loop_wait_fnargs will be called
        '''
        try:
            # Then run any and all requested commands
            if len(self.main_loop_wait_fnargs) == 0: return
            #progress("Entering main_loop_wait_cb")
            for fnargs in self.main_loop_wait_fnargs:
                (fn, args) = fnargs
                ret = fn(*args)
            self.main_loop_wait_fnargs = []
        except KeyboardInterrupt:
            self.end_analysis()

    def queue_main_loop_wait_fn(self, fn, args=[]):
        '''
        Queue a function to run at the next main loop
        fn is a function we want to run, args are arguments to apss to it
        '''
        self.main_loop_wait_fnargs.append((fn, args))

    def exit_cpu_loop(self):
        '''
        Stop cpu execution at nearest juncture.
        '''
        self.libpanda.panda_exit_loop = True

    def revert_async(self, snapshot_name): # In the next main loop, revert
        '''
        Request a snapshot revert, eventually. This is fairly dangerous
        because you don't know when it finishes. You should be using revert_sync
        from a blocking function instead
        '''
        if not hasattr(self, 'warned_async'):
            self.warned_async = True
            print("WARNING: panda.revert_async may be deprecated in the near future")
        if debug:
            progress ("Loading snapshot " + snapshot_name)

        # Stop guest, queue up revert, then continue
        timer_start = time()
        self.vm_stop()
        charptr = self.ffi.new("char[]", bytes(snapshot_name, "utf-8"))
        self.queue_main_loop_wait_fn(self.libpanda.panda_revert, [charptr])
        self.queue_main_loop_wait_fn(self.libpanda.panda_cont)
        if debug:
            self.queue_main_loop_wait_fn(self._finish_timer, [timer_start, "Loaded snapshot"])

    def reset(self):
        """In the next main loop, reset to boot"""
        if debug:
            progress ("Resetting machine to start state")

        # Stop guest, queue up revert, then continue
        self.vm_stop()
        self.queue_main_loop_wait_fn(self.libpanda.panda_reset)
        self.queue_main_loop_wait_fn(self.libpanda.panda_cont)

    def cont(self):
        ''' Continue execution (run after vm_stop) '''
        self.libpanda.panda_cont()
        self.running.set()

    def vm_stop(self, code=4):
        ''' Stop execution, default code means RUN_STATE_PAUSED '''
        self.libpanda.panda_stop(code)

    def snap(self, snapshot_name):
        '''
        Create snapshot with specified name

        Args:
            snapshot_name (str): name of the snapshot

        Returns:
            None
        '''
        if debug:
            progress ("Creating snapshot " + snapshot_name)

        # Stop guest execution, queue up a snapshot, then continue
        timer_start = time()
        self.vm_stop()
        charptr = self.ffi.new("char[]", bytes(snapshot_name, "utf-8"))
        self.queue_main_loop_wait_fn(self.libpanda.panda_snap, [charptr])
        self.queue_main_loop_wait_fn(self.libpanda.panda_cont)
        if debug:
            self.queue_main_loop_wait_fn(self._finish_timer, [timer_start, "Saved snapshot"])

    def delvm(self, snapshot_name):
        '''
        Delete snapshot with specified name
        Args:
            snapshot_name (str): name of the snapshot

        Returns:
            None
        '''

        if debug:
            progress ("Deleting snapshot " + snapshot_name)

        # Stop guest, queue up delete, then continue
        self.vm_stop()
        charptr = self.ffi.new("char[]", bytes(snapshot_name, "utf-8"))
        self.queue_main_loop_wait_fn(self.libpanda.panda_delvm, [charptr])

    def _finish_timer(self, start, msg):
        ''' Print how long some (main_loop_wait) task took '''
        t = time() - start
        print("{} in {:.08f} seconds".format(msg, t))


    def enable_tb_chaining(self):
        '''
        This function enables translation block chaining in QEMU
        '''
        if debug:
            progress("Enabling TB chaining")
        self.disabled_tb_chaining = False
        self.libpanda.panda_enable_tb_chaining()

    def disable_tb_chaining(self):
        '''
        This function disables translation block chaining in QEMU
        '''
        if not self.disabled_tb_chaining:
            if debug:
                progress("Disabling TB chaining")
            self.disabled_tb_chaining = True
            self.libpanda.panda_disable_tb_chaining()

    def _setup_internal_signal_handler(self, signal_handler=None):
        def SigHandler(SIG,a,b):
            from signal import SIGINT, SIGHUP, SIGTERM
            if SIG == SIGINT:
                self.exit_exception = KeyboardInterrupt
                self.end_analysis()
            elif SIG == SIGHUP:
                self.exit_exception = KeyboardInterrupt
                self.end_analysis()
            elif SIG == SIGTERM:
                self.exit_exception = KeyboardInterrupt
                self.end_analysis()
            else:
                print(f"PyPanda Signal handler received unhandled signal {SIG}")


        if signal_handler is not None:
            # store custom signal handler if requested1
            self.__sighandler = signal_handler

        if self._initialized_panda:
            # initialize and register signal handler only if panda is initialized
            self.__sighandler = (self.ffi.callback("void(int,void*,void*)", SigHandler)
                       if signal_handler is None and self.__sighandler is None
                       else self.ffi.callback("void(int,void*,void*)", self.__sighandler))

            self.libpanda.panda_setup_signal_handling(self.__sighandler)


    def run(self):
        '''
        This function starts our running PANDA instance from Python. At termination this function returns and the script continues to run after it.

        This function starts execution of the guest. It blocks until guest finishes.
        It also initializes panda object, clears main_loop_wait fns, and sets up internal callbacks.

        Args:
            None

        Returns:
            None: When emulation has finished due to guest termination, replay conclusion or a call to `Panda.end_analysis`
        '''

        if len(self.main_loop_wait_fnargs):
            if debug:
                print("Clearing prior main_loop_wait fns:", self.main_loop_wait_fnargs)
            self.main_loop_wait_fnargs = [] # [(fn, args), ...]

        self.ending = False

        if debug:
            progress ("Running")

        self.initializing.set()
        if not self._initialized_panda:
            self._initialize_panda()
        self.initializing.clear()

        if not self.started.is_set():
            self.started.set()

        self.athread.ending = False

        # Ensure our internal CBs are always enabled
        self.enable_internal_callbacks()
        self._setup_internal_signal_handler()
        self.running.set()
        self.libpanda.panda_run() # Give control to panda
        self.running.clear() # Back from panda's execution (due to shutdown or monitor quit)
        self.unload_plugins() # Unload pyplugins and C plugins
        self.delete_callbacks() # Unload any registered callbacks
        self.plugins = plugin_list(self)
        # Write PANDALOG, if any
        #self.libpanda.panda_cleanup_record()
        if self._in_replay:
            self.reset()
        if hasattr(self, "exit_exception"):
            saved_exception = self.exit_exception
            del self.exit_exception
            raise saved_exception


    def end_analysis(self):
        '''
        Stop running machine.

        Call from any thread to unload all plugins and stop all queued functions.
        If called from async thread or a callback, it will also unblock panda.run()

        Note here we use the async class's internal thread to process these
        without needing to wait for tasks in the main async thread
        '''
        self.athread.ending = True
        self.ending = True
        self.unload_plugins()
        if self.running.is_set() or self.initializing.is_set():

            # If we were running, stop the execution and check if we crashed
            self.queue_async(self.stop_run, internal=True)

    def record(self, recording_name, snapshot_name=None):
        """Begins active recording with name provided.

        Args:
            recording_name (string): name of recording to save.
            snapshot_name (string, optional): Before recording starts restore to this snapshot name. Defaults to None.

        Raises:
            Exception: raises exception if there was an error starting recording.
        """
        if snapshot_name == None:
            snapshot_name_ffi = self.ffi.NULL
        else:
            snapshot_name_ffi = self.ffi.new("char[]",snapshot_name.encode())
        recording_name_ffi = self.ffi.new("char[]", recording_name.encode())
        result = self.libpanda.panda_record_begin(recording_name_ffi,snapshot_name_ffi)
        res_string_enum = self.ffi.string(self.ffi.cast("RRCTRL_ret",result))
        if res_string_enum != "RRCTRL_OK":
           raise Exception(f"record method failed with RTCTL_ret {res_string_enum} ({result})")

    def end_record(self):
        """Stop active recording.

        Raises:
            Exception: raises exception if there was an error stopping recording.
        """
        result = self.libpanda.panda_record_end()
        res_string_enum = self.ffi.string(self.ffi.cast("RRCTRL_ret",result))
        if res_string_enum != "RRCTRL_OK":
           raise Exception(f"record method failed with RTCTL_ret {res_string_enum} ({result})")

    def recording_exists(self, name):
        '''
        Checks if a recording file exists on disk.

        Args:
            name (str): name of the recording to check for (e.g., `foo` which uses `foo-rr-snp` and `foo-rr-nondet.log`)
        
        Returns:
            boolean: true if file exists, false otherwise
        '''
        if exists(name + "-rr-snp") or rr2_contains_member(name, "snapshot"):
            return True

    def run_replay(self, replaypfx):
        '''
        Load a replay and run it. Starts PANDA execution and returns after end of VM execution.

        Args:
            replaypfx (str): Replay name/path (e.g., "foo" or "./dir/foo")

        Returns:
            None
        '''
        if (not isfile(replaypfx+"-rr-snp") or not isfile(replaypfx+"-rr-nondet.log")) and not rr2_recording(replaypfx):
            raise ValueError("Replay files not present to run replay of {}".format(replaypfx))

        self.ending = False

        if debug:
            progress ("Replaying %s" % replaypfx)

        charptr = self.ffi.new("char[]",bytes(replaypfx,"utf-8"))
        self.libpanda.panda_replay_begin(charptr)
        self._in_replay = True
        self.run()
        self._in_replay = False


    def end_replay(self):
        '''
        Terminates a currently running replay

            Returns:
                None

            Raises:
                Exception: raises exception if no replay is active or termination failed.
        '''

        if self._in_replay is False:
            raise Exception("Tried to terminate replay while not in replay mode!")

        result = self.libpanda.panda_replay_end()

        res_string_enum = self.ffi.string(self.ffi.cast("RRCTRL_ret",result))
        if res_string_enum != "RRCTRL_OK":
           raise Exception(f"ending record method failed with RTCTL_ret {res_string_enum} ({result})")


    def require(self, name):
        '''
        Load a C plugin with no arguments. Deprecated. Use load_plugin
        '''
        self.load_plugin(name, args={})
    
    def _plugin_loaded(self, name):
        name_c = self.ffi.new("char[]", bytes(name, "utf-8"))
        return self.libpanda.panda_get_plugin_by_name(name_c) != self.ffi.NULL

    def load_plugin(self, name, args={}):
        '''
        Load a C plugin, optionally with arguments

        Args:
            name (str): Name of plugin
            args (dict): Arguments matching key to value. e.g. {"key": "value"} sets option `key` to `value`.

        Returns:
            None.
        '''
        if debug:
            progress ("Loading plugin %s" % name),

        argstrs_ffi = []
        if isinstance(args, dict):
            for k,v in args.items():
                this_arg_s = "{}={}".format(k,v)
                this_arg = self.ffi.new("char[]", bytes(this_arg_s, "utf-8"))
                argstrs_ffi.append(this_arg)

            n = len(args.keys())
        elif isinstance(args, list):
            for arg in args:
                this_arg = self.ffi.new("char[]", bytes(arg, "utf-8"))
                argstrs_ffi.append(this_arg)
            n = len(args)

        else:
            raise ValueError("Arguments to load plugin must be a list or dict of key/value pairs")

        # First set qemu_path so plugins can load (may be unnecessary after the first time)
        assert(self.panda), "Unknown location of PANDA"
        panda_name_ffi = self.ffi.new("char[]", bytes(self.panda,"utf-8"))
        self.libpanda.panda_set_qemu_path(panda_name_ffi)

        if len(argstrs_ffi):
            plugin_args = argstrs_ffi
        else:
            plugin_args = self.ffi.NULL

        charptr = self.ffi.new("char[]", bytes(name,"utf-8"))
        self.libpanda.panda_require_from_library(charptr, plugin_args, len(argstrs_ffi))
        self._load_plugin_library(name)

    def _procname_changed(self, cpu, name):
        for cb_name, cb in self.registered_callbacks.items():
            if not cb["procname"]:
                continue
            if name == cb["procname"] and not cb['enabled']:
                self.enable_callback(cb_name)
            if name != cb["procname"] and cb['enabled']:
                self.disable_callback(cb_name)

    def unload_plugin(self, name):
        '''
        Unload plugin with given name.

        Args:
            name (str): Name of plug

        Returns:
            None
        '''
        if debug:
            progress ("Unloading plugin %s" % name),
        name_ffi = self.ffi.new("char[]", bytes(name,"utf-8"))
        self.libpanda.panda_unload_plugin_by_name(name_ffi)

    def _unload_pyplugins(self):
        '''
        Unload Python plugins first.
        
        We have to be careful to not remove __main_loop_wait because we're executing inside of __main_loop_wait and it more work to do
        
        We achieve this by first popping main loop wait and then re-adding it after unloading all other callbacks
        '''
        mlw = self.registered_callbacks.pop("__main_loop_wait")

        # First unload python plugins, should be safe to do anytime
        while self.registered_callbacks:
            try:
                self.delete_callback(list(self.registered_callbacks.keys())[0])
            except IndexError:
                continue
    
        self.registered_callbacks["__main_loop_wait"] = mlw

        # Next, unload any pyplugins
        if hasattr(self, "_pyplugin_manager"):
            self.pyplugins.unload_all()

    def unload_plugins(self):
        '''
        Disable all python plugins and request to unload all c plugins
        at the next main_loop_wait.

        XXX: If called during shutdown/exit, c plugins won't be unloaded
        because the next main_loop_wait will never happen. Instead, call
        panda.panda_finish directly (which is done at the end of panda.run())
        '''
        if debug:
            progress ("Disabling all python plugins, unloading all C plugins")

        # In next main loop wait, unload all python plugin
        self.queue_main_loop_wait_fn(self._unload_pyplugins)

        # Then unload C plugins. May be unsafe to do except from the top of the main loop (taint segfaults otherwise)
        self.queue_main_loop_wait_fn(self.libpanda.panda_unload_plugins)

    def memsavep(self, file_out):
        '''
        Calls QEMU memsavep on your specified python file.
        '''
        # this part was largely copied from https://cffi.readthedocs.io/en/latest/ref.html#support-for-file

        file_out.flush()                 # make sure the file is flushed
        newfd = dup(file_out.fileno())   # make a copy of the file descriptor
        fileptr = self.C.fdopen(newfd, b"w")
        self.libpanda.panda_memsavep(fileptr)
        self.C.fclose(fileptr)

    def physical_memory_read(self, addr, length, fmt='bytearray'):
        '''
        Read guest physical memory. In the specified format. Note that the `ptrlist` format
        returns a list of integers, each of the specified architecture's pointer size.

        Args:
            addr (int): Address
            length (int): length of array you would like returned
            fmt (str): format for returned array. Options: 'bytearray', 'int', 'str', 'ptrlist'

        Returns:
            Union[bytearray, int, str, list[int]]: memory data

        Raises:
            ValueError if memory access fails or fmt is unsupported
        '''
        return self._memory_read(None, addr, length, physical=True, fmt=fmt)

    def virtual_memory_read(self, cpu, addr, length, fmt='bytearray'):
        '''
        Read guest virtual memory.

        Args:
            cpu (CPUState): CPUState structure
            addr (int): Address
            length (int): length of data you would like returned
            fmt: format for returned array. See `physical_memory_read`.

        Returns:
            Union[bytearray, int, str, list[int]]: memory data

        Raises:
            ValueError if memory access fails or fmt is unsupported
        '''

        return self._memory_read(cpu, addr, length, physical=False, fmt=fmt)

    def _memory_read(self, env, addr, length, physical=False, fmt='bytearray'):
        '''
        Read but with an autogen'd buffer
        Supports physical or virtual addresses
        Raises ValueError if read fails
        '''
        if not isinstance(addr, int):
            raise ValueError(f"Unsupported read from address {repr(addr)}")

        buf = self.ffi.new("char[]", length)

        # Force CFFI to parse addr as an unsigned value. Otherwise we get OverflowErrors
        # when it decides that it's negative
        ptr_typ = f'uint{self.bits}_t'
        addr_u = int(self.ffi.cast(ptr_typ, addr))

        buf_a = self.ffi.cast("char*", buf)
        length_a = self.ffi.cast("int", length)
        if physical:
            err = self.libpanda.panda_physical_memory_read_external(addr_u, buf_a, length_a)
        else:
            if "osi_linux" in self.plugins.keys() or self._plugin_loaded("osi_linux"):
                err = self.plugins["osi_linux"].osi_linux_virtual_memory_read(env, addr_u, buf_a, length_a)
            else:
                err = self.libpanda.panda_virtual_memory_read_external(env, addr_u, buf_a, length_a)

        if err < 0:
            # TODO: We should support a custom error class instead of a generic ValueError
            raise ValueError(f"Failed to read guest memory at {addr:x} got err={err}")

        r = self.ffi.unpack(buf, length)
        if fmt == 'bytearray':
            return r
        elif fmt=='int':
            return int.from_bytes(r, byteorder=self.endianness)  # XXX size better be small enough to pack into an int!
        elif fmt=='str':
            return self.ffi.string(buf, length)
        elif fmt=='ptrlist':
            # This one is weird. Chunk the memory into byte-sequences of (self.bits/8) bytes and flip endianness as approperiate
            # return a list
            bytelen = int(self.bits/8)
            if (length % bytelen != 0):
                raise ValueError(f"Memory of size {length} does not evenly divide into {bytelen} byte chunks")
            chunks = []
            for start in range(0, length, bytelen):
                data = r[start:start+bytelen]
                int_data = int.from_bytes(data, byteorder=self.endianness)
                chunks.append(int_data)
            return chunks

        else:
            raise ValueError("fmt={} unsupported".format(fmt))

    def physical_memory_write(self, addr, buf):
        '''
        Write guest physical memory.

        Args:
            addr (int): Address
            buf (bytestring):  byte string to write into memory

        Returns:
            None

        Raises:
            ValueError if the call to panda.physical_memory_write fails (e.g., if you pass a pointer to an invalid memory region)
        '''
        self._memory_write(None, addr, buf, physical=True)

    def virtual_memory_write(self, cpu, addr, buf):
        '''
        Write guest virtual memory.

        Args:
            cpu (CPUState): CPUState structure
            address (int): Address
            buf (bytestr): byte string to write into memory

        Returns:
            None

        Raises:
            ValueError if the call to panda.virtual_memory_write fails (e.g., if you pass a pointer to an unmapped page)
        '''
        self._memory_write(cpu, addr, buf, physical=False)

    def _memory_write(self, cpu, addr, buf, physical=False):
        '''
        Write a bytearray into memory at the specified physical/virtual address
        '''
        length = len(buf)
        c_buf = self.ffi.new("char[]",buf)
        buf_a = self.ffi.cast("char*", c_buf)
        length_a = self.ffi.cast("int", length)

        if not hasattr(self, "_memcb"): # XXX: Why do we enable memcbs for memory writes?
            self.enable_memcb()

        if physical:
            err = self.libpanda.panda_physical_memory_write_external(addr, buf_a, length_a)
        else:
            err = self.libpanda.panda_virtual_memory_write_external(cpu, addr, buf_a, length_a)

        if err < 0:
            raise ValueError(f"Memory write failed with err={err}") # TODO: make a PANDA Exn class

    def callstack_callers(self, lim, cpu): # XXX move into new directory, 'callstack' ?
        '''
        Helper function for callstack_instr plugin
        Handle conversion and return get_callers from callstack_instr.
        '''
        if not "callstack_instr" in self.plugins:
            progress("enabling callstack_instr plugin")
            self.load_plugin("callstack_instr")

        callers = self.ffi.new("uint%d_t[%d]" % (self.bits, lim))
        n = self.plugins['callstack_instr'].get_callers(callers, lim, cpu)
        c = []
        for pc in callers:
            c.append(pc)
        return c

    def _load_plugin_library(self, name):
        if hasattr(self,"__did_load_libpanda"):
            libpanda_path_chr = self.ffi.new("char[]",bytes(self.libpanda_path, "UTF-8"))
            self.__did_load_libpanda = self.libpanda.panda_load_libpanda(libpanda_path_chr)
        if not name in self.plugins.keys():
            plugin = pjoin(*[self.get_plugin_path(), f"libpanda-{name}_{self.arch_name}-{self.target}.so"])
            assert(isfile(plugin))
            self.plugins[name] = self.ffi.dlopen(plugin)

    def queue_async(self, f, internal=False):
        '''
        Explicitly queue work in the asynchronous work queue.

        Args:
            f: A python function with no arguments to be called at a later time. The function should
            be decorated with `@pandare.blocking`. You generally want to use `panda.queue_blocking` over this function.

        Returns:
            None
        '''

        # this takes the blocking function and handles errors
        @blocking
        def wrapper():
            try:
                f()
            except Exception as e:
                if self.catch_exceptions:
                    self.exit_exception = e
                    self.end_analysis()
                else:
                    raise e

        # Keep the original function name instead of replacing it with 'wrapper'
        wrapper.__name__ = f.__name__
        self.athread.queue(wrapper, internal=internal)

    def map_memory(self, name, size, address):

        '''
        Make a new memory region.

        Args:
            name (str): This is an internal reference name for this region. Must be unique.
            size (int): number of bytes the region should be.
            address (int): start address of region

        Returns:
            None
        '''

        name_c = self.ffi.new("char[]", bytes(name, "utf-8"))
        size = ceil(size/1024)*1024 # Must be page-aligned
        return self.libpanda.map_memory(name_c, size, address)

    def read_str(self, cpu, ptr, max_length=None):
        '''
        Helper to read a null-terminated string from guest memory given a pointer and CPU state
        May return an exception if the call to panda.virtual_memory_read fails (e.g., if you pass a
        pointer to an unmapped page)

        Args:
            cpu (CPUState): CPUState structure
            ptr (int): Pointer to start of string
            max_length (int): Optional length to stop reading at

        Returns:
            string: Data read from memory

        '''
        r = b""
        idx = 0
        while (max_length is None or idx < max_length):
            next_char = self.virtual_memory_read(cpu, ptr, 1) # If this raises an exn, don't mask it
            if next_char == b"\x00":
                break
            r += next_char
            ptr += 1
            idx += 1
        return r.decode("utf8", "ignore")

    def to_unsigned_guest(self, x):
        '''
        Convert a singed python int to an unsigned int32/unsigned int64
        depending on guest bit-size

        Args:
            x (int): Python integer

        Returns:
            int: Python integer representing x as an unsigned value in the guest's pointer-size.
        '''
        import ctypes
        if self.bits == 32:
            return ctypes.c_uint32(x).value
        elif self.bits == 64:
            return ctypes.c_uint64(x).value
        else:
            raise ValueError("Unsupported number of bits")

    def from_unsigned_guest(self, x):
        '''
        Convert an unsigned int32/unsigned int64 from the guest
        (depending on guest bit-size) to a (signed) python int

        Args:
            x (int): Python integer representing an unsigned value in the guest's pointer-size

        Returns:
            int: Python integer representing x as a signed value
        '''
        if x >= 2**(self.bits-1): # If highest bit is set, it's negative
            return (x - 2**self.bits)
        else: # Else it's positive
            return x

    def queue_blocking(self, func, queue=True):
        """
        Decorator to mark a function as `blocking`, and (by default) queue it to run asynchronously.
        This should be used to mark functions that will drive guest execution. Functions will be run
        in the order they are defined. For more precise control, use `panda.queue_async`.


        ```
        @panda.queue_blocking
        def do_something():
            panda.revert_sync('root')
            print(panda.run_serial_cmd('whoami'))
            panda.end_analysis()
        ```

        is equivalent to

        ```
        @blocking
        def run_whoami():
            panda.revert_sync('root')
            print(panda.run_serial_cmd('whoami'))
            panda.end_analysis()

        panda.queue_async(run_whoami)
        ```

        Args:
            func (function): Function to queue
            queue (bool): Should function automatically be queued

        Returns:
            None

        """
        f = blocking(func)
        if queue:
            self.queue_async(f)
        return f

    # PyPlugin helpers
    @property
    def pyplugins(self):
        """
        A reference to an auto-instantiated `pandare.pyplugin.PyPluginManager` class.
        """
        if not hasattr(self, "_pyplugin_manager"):
            from .pypluginmanager import PyPluginManager
            self._pyplugin_manager = PyPluginManager(self)
        return self._pyplugin_manager


    ########################## LIBPANDA FUNCTIONS ########################
    # Methods that directly pass data to/from PANDA with no extra logic beyond argument reformatting.
    def set_pandalog(self, name):
        '''
        Enable recording to a pandalog (plog) named `name`

        Args:
            name (str): filename to output data to

        Returns:
            None
        '''
        charptr = self.ffi.new("char[]", bytes(name, "utf-8"))
        self.libpanda.panda_start_pandalog(charptr)

    def enable_memcb(self):
        '''
        Enable memory callbacks. Must be called for memory callbacks to work.
        pypanda enables this automatically with some callbacks.
        '''
        self._memcb = True
        self.libpanda.panda_enable_memcb()

    def disable_memcb(self):
        '''
        Disable memory callbacks. Must be enabled for memory callbacks to work.
        pypanda enables this automatically with some callbacks.
        '''
        self._memcb = False
        self.libpanda.panda_disable_memcb()

    def virt_to_phys(self, cpu, addr):
        '''
        Convert virtual address to physical address.

        Args:
            cpu (CPUState): CPUState struct
            addr (int): virtual address to convert

        Return:
            int: physical address
        '''
        if "osi_linux" in self.plugins.keys() or self._plugin_loaded("osi_linux"):
            return self.plugins["osi_linux"].osi_linux_virt_to_phys(cpu, addr)
        else:
            return self.libpanda.panda_virt_to_phys_external(cpu, addr)

    def enable_plugin(self, handle):
        '''
        Enable plugin.

        Args:
            handle (int): pointer to handle returned by plugin

        Return:
            None
        '''
        self.libpanda.panda_enable_plugin(handle)

    def disable_plugin(self, handle):
        '''
        Disable plugin.

        Args:
            handle (int): pointer to handle returned by plugin

        Return:
            None
        '''
        self.libpanda.panda_disable_plugin(handle)

    def enable_llvm(self):
        '''
        Enables the use of the LLVM JIT in replacement of the TCG (QEMU intermediate language and compiler) backend.
        '''
        self.libpanda.panda_enable_llvm()

    def disable_llvm(self):
        '''
        Disables the use of the LLVM JIT in replacement of the TCG (QEMU intermediate language and compiler) backend.
        '''
        self.libpanda.panda_disable_llvm()

    def enable_llvm_helpers(self):
        '''
        Enables the use of Helpers for the LLVM JIT in replacement of the TCG (QEMU intermediate language and compiler) backend.
        '''
        self.libpanda.panda_enable_llvm_helpers()

    def disable_llvm_helpers(self):
        '''
        Disables the use of Helpers for the LLVM JIT in replacement of the TCG (QEMU intermediate language and compiler) backend.
        '''
        self.libpanda.panda_disable_llvm_helpers()

    def flush_tb(self):
        '''
        This function requests that the translation block cache be flushed as soon as possible. If running with translation block chaining turned off (e.g. when in LLVM mode or replay mode), this will happen when the current translation block is done executing.
        Flushing the translation block cache is additionally necessary if the plugin makes changes to the way code is translated. For example, by using panda_enable_precise_pc.
        '''
        return self.libpanda.panda_do_flush_tb()

    def break_exec(self):
        '''
        If called from a start block exec callback, will cause the emulation to bail *before* executing
        the rest of the current block.
        '''
        return self.libpanda.panda_do_break_exec()

    def enable_precise_pc(self):
        '''
        By default, QEMU does not update the program counter after every instruction.
        This function enables precise tracking of the program counter. After enabling precise PC tracking, the program counter will be available in env->panda_guest_pc and can be assumed to accurately reflect the guest state.
        '''
        self.libpanda.panda_enable_precise_pc()

    def disable_precise_pc(self):
        '''
        By default, QEMU does not update the program counter after every instruction.
        This function disables precise tracking of the program counter.
        '''
        self.libpanda.panda_disable_precise_pc()

    def in_kernel(self, cpustate):
        '''
        Returns true if the processor is in the privilege level corresponding to kernel mode for any of the PANDA supported architectures.
        Legacy alias for in_kernel_mode().
        '''
        return self.libpanda.panda_in_kernel_external(cpustate)

    def in_kernel_mode(self, cpustate):
        '''
        Check if the processor is running in priviliged mode.

        Args:
            cpu (CPUState): CPUState structure

        Returns:
            Bool: If the processor is in the privilege level corresponding to kernel mode
                  for the given architecture
        '''
        return self.libpanda.panda_in_kernel_mode_external(cpustate)

    def in_kernel_code_linux(self, cpustate):
        '''
        Check if the processor is running in linux kernelspace.

        Args:
            cpu (CPUState): CPUState structure

        Returns:
            Bool: If the processor is running in Linux kernel space code.
        '''
        return self.libpanda.panda_in_kernel_code_linux_external(cpustate)

    def g_malloc0(self, size):
        '''
        Helper function to call glib malloc

        Args:
            size (int): size to call with malloc

        Returns:
            buffer of the requested size from g_malloc
        '''
        return self.libpanda.g_malloc0(size)

    def current_sp(self, cpu):
        '''
        Get current stack pointer

        Args:
            cpu (CPUState): CPUState structure

        Return:
            int: Value of stack pointer
        '''
        return self.libpanda.panda_current_sp_external(cpu)

    def current_pc(self, cpu):
        '''
        Get current program counter

        Args:
            cpu (CPUState): CPUState structure

        Return:
            integer value of current program counter

        .. Deprecated:: Use panda.arch.get_pc(cpu) instead
        '''
        return self.libpanda.panda_current_pc(cpu)

    def current_asid(self, cpu):
        '''
        Get current Application Specific ID

        Args:
            cpu (CPUState): CPUState structure

        Returns:
            integer: value of current ASID
        '''
        return self.libpanda.panda_current_asid(cpu)
    
    def get_id(self, cpu):
        '''
        Get current hw_proc_id ID

        Args:
            cpu (CPUState): CPUState structure
        
        Returns:
            integer: value of current hw_proc_id
        '''
        return self.plugins["hw_proc_id"].get_id(cpu)

    def disas2(self, code, size):
        '''
        Call panda_disas to diasassemble an amount of code at a pointer.
        FIXME: seem to not match up to PANDA definition
        '''
        self.libpanda.panda_disas(code, size)

    def cleanup(self):
        '''
        Unload all plugins and close pandalog.

        Returns:
            None
        '''
        self.libpanda.panda_cleanup()

    def was_aborted(self):
        '''
        Returns true if panda was aborted.
        '''
        return self.libpanda.panda_was_aborted()

    def get_cpu(self):
        '''
        This function returns first_cpu CPUState object from QEMU.
        XXX: You rarely want this

        Returns:
            CPUState: cpu
        '''
        return self.libpanda.get_cpu()

    def garray_len(self, garray):
        '''
        Convenience function to get array length of glibc array.

        Args:
            g (garray): Pointer to a glibc array
                
        Returns:
            int: length of the array
        '''
        return self.libpanda.garray_len(garray)

    def panda_finish(self):
        '''
        Final stage call to underlying panda_finish with initialization.
        '''
        return self.libpanda.panda_finish()

    def rr_get_guest_instr_count(self):
        '''
        Returns record/replay guest instruction count.

        Returns:
            int: Current instruction count
        '''
        return self.libpanda.rr_get_guest_instr_count_external()

    ################### LIBQEMU Functions ############
    #Methods that directly pass data to/from QEMU with no extra logic beyond argument reformatting.
    #All QEMU function can be directly accessed by Python. These are here for convenience.
    # It's usally better to find a function name and look at the QEMU source for these functions.

    def drive_get(self, blocktype, bus, unit):
        '''
        Gets DriveInfo struct from user specified information.

        Args:
            blocktype: BlockInterfaceType structure
            bus: integer bus
            unit: integer unit

        Returns:
            DriveInfo struct
        '''
        return self.libpanda.drive_get(blocktype,bus,unit)

    def sysbus_create_varargs(self, name, addr):
        '''
        Returns DeviceState struct from user specified information
        Calls sysbus_create_varargs QEMU function.

        Args:
            name (str):
            addr (int): hwaddr

        Returns:
            DeviceState struct
        '''
        return self.libpanda.sysbus_create_varargs(name,addr, self.ffi.NULL)

    def cpu_class_by_name(self, name, cpu_model):
        '''
        Gets cpu class from name.
        Calls cpu_class_by_name QEMU function.

        Args:
            name: typename from python string
            cpu_model: string specified cpu model

        Returns:
            ObjectClass struct
        '''
        return self.libpanda.cpu_class_by_name(name, cpu_model)

    def object_class_by_name(self, name):
        '''
        Returns class as ObjectClass from name specified.
        Calls object_class_by_name QEMU function.

        Args
            name (str): string defined by user

        Returns:
            struct as specified by name
        '''
        return self.libpanda.object_class_by_name(name)

    def object_property_set_bool(self, obj, value, name):
        '''
        Writes a bool value to a property.
        Calls object_property_set_bool QEMU function.

        Args::
            value: the value to be written to the property
            name: the name of the property
            errp: returns an error if this function fails

        Returns:
            None
        '''
        return self.libpanda.object_property_set_bool(obj,value,name,self.libpanda.error_abort)

    def object_class_get_name(self, objclass):
        '''
        Gets String QOM typename from object class.
        Calls object_class_get_name QEMU function.

        Args::
            objclass: class to obtain the QOM typename for.

        Returns:
            String QOM typename for klass.
        '''
        return self.libpanda.object_class_get_name(objclass)

    def object_new(self, name):
        '''
        Creates a new QEMU object from typename.
        This function will initialize a new object using heap allocated memory.
        The returned object has a reference count of 1, and will be freed when
        the last reference is dropped.
        Calls object_new QEMU function.

        Args:
            name (str): The name of the type of the object to instantiate.

        Returns:
            The newly allocated and instantiated object.
        '''
        return self.libpanda.object_new(name)

    def object_property_get_bool(self, obj, name):
        '''
        Pull boolean from object.
        Calls object_property_get_bool QEMU function.

        Args:
            obj: the object
            name: the name of the property

        Returns:
            the value of the property, converted to a boolean, or NULL if an error occurs (including when the property value is not a bool).
        '''
        return self.libpanda.object_property_get_bool(obj,name,self.libpanda.error_abort)

    def object_property_set_int(self,obj, value, name):
        '''
        Set integer in QEMU object. Writes an integer value to a property.
        Calls object_property_set_int QEMU function.

        Args:
            value: the value to be written to the property
            name: the name of the property

        Returns:
            None
        '''
        return self.libpanda.object_property_set_int(obj, value, name, self.libpanda.error_abort)

    def object_property_get_int(self, obj, name):
        '''
        Gets integer in QEMU object. Reads an integer value from this property.
        Calls object_property_get_int QEMU function.

            Paramaters:
                obj: the object
                name: the name of the property

            Returns:
                the value of the property, converted to an integer, or negative if an error occurs (including when the property value is not an integer).
        '''
        return self.libpanda.object_property_get_int(obj, name, self.libpanda.error_abort)

    def object_property_set_link(self, obj, val, name):
        '''
        Writes an object's canonical path to a property.
        Calls object_property_set_link QEMU function.

        Args:
            value: the value to be written to the property
            name: the name of the property
            errp: returns an error if this function fails

        Returns:
            None
        '''
        return self.libpanda.object_property_set_link(obj,val,name,self.libpanda.error_abort)

    def object_property_get_link(self, obj, name):
        '''
        Reads an object's canonical path to a property.
        Calls object_property_get_link QEMU function.

        Args:
            obj: the object
            name: the name of the property
            errp: returns an error if this function fails

        Returns:
            the value of the property, resolved from a path to an Object, or NULL if an error occurs (including when the property value is not a string or not a valid object path).
        '''
        return self.libpanda.object_property_get_link(obj,name,self.libpanda.error_abort)

    def object_property_find(self, obj, name):
        '''
        Look up a property for an object and return its #ObjectProperty if found.
        Calls object_property_find QEMU function.

        Args:
            obj: the object
            name: the name of the property
            errp: returns an error if this function fails

        Returns:
            struct ObjectProperty pointer
        '''
        return self.libpanda.object_property_find(obj,name, self.ffi.NULL)

    def memory_region_allocate_system_memory(self, mr, obj, name, ram_size):
        '''
        Allocates Memory region by user specificiation.
        Calls memory_region_allocation_system_memory QEMU function.

        Args:
            mr: MemoryRegion struct
            obj: Object struct
            name (str): Region name
            ram_size (int): RAM size

        Returns:
            None
        '''
        return self.libpanda.memory_region_allocate_system_memory(mr, obj, name, ram_size)

    def memory_region_add_subregion(self, mr, offset, sr):
        '''
        Calls memory_region_add_subregion from QEMU.
        memory_region_add_subregion: Add a subregion to a container.

        Adds a subregion at @offset.  The subregion may not overlap with other
        subregions (except for those explicitly marked as overlapping).  A region
        may only be added once as a subregion (unless removed with
        memory_region_del_subregion()); use memory_region_init_alias() if you
        want a region to be a subregion in multiple locations.

        Args:
            mr: the region to contain the new subregion; must be a container initialized with memory_region_init().
            offset: the offset relative to @mr where @subregion is added.
            subregion: the subregion to be added.

        Returns:
            None
        '''
        return self.libpanda.memory_region_add_subregion(mr,offset,sr)

    def memory_region_init_ram_from_file(self, mr, owner, name, size, share, path):
        '''
        Calls memory_region_init_ram_from_file from QEMU.
        memory_region_init_ram_from_file:  Initialize RAM memory region with a mmap-ed backend.

        Args:
            mr: the #MemoryRegion to be initialized.
            owner: the object that tracks the region's reference count
            name: the name of the region.
            size: size of the region.
            share: %true if memory must be mmaped with the MAP_SHARED flag
            path: the path in which to allocate the RAM.
            errp: pointer to Error*, to store an error if it happens.

        Returns:
            None
        '''
        return self.libpanda.memory_region_init_ram_from_file(mr, owner, name, size, share, path, self.libpanda.error_fatal)

    def create_internal_gic(self, vbi, irqs, gic_vers):
        return self.libpanda.create_internal_gic(vbi, irqs, gic_vers)

    def create_one_flash(self, name, flashbase, flashsize, filename, mr):
        return self.libpanda.create_one_flash(name, flashbase, flashsize, filename, mr)

    def create_external_gic(self, vbi, irqs, gic_vers, secure):
        return self.libpanda.create_external_gic(vbi, irqs, gic_vers, secure)

    def create_virtio_devices(self, vbi, pic):
        return self.libpanda.create_virtio_devices(vbi, pic)

    def arm_load_kernel(self, cpu, bootinfo):
        return self.libpanda.arm_load_kernel(cpu, bootinfo)

    def error_report(self, s):
        return self.libpanda.error_report(s)

    def get_system_memory(self):
        return self.libpanda.get_system_memory()

    def lookup_gic(self,n):
        return self.libpanda.lookup_gic(n)

    ##################### OSI FUNCTIONS ###########
    #Convenience functions to interact with the Operating System Instrospection (OSI) class of plugins.

    def set_os_name(self, os_name):
        """
        Set OS target. Equivalent to "-os" flag on the command line. Matches the form of:

            "windows[-_]32[-_]xpsp[23]",
            "windows[-_]32[-_]2000",
            "windows[-_]32[-_]7sp[01]",
            "windows[-_]64[-_]7sp[01]",
            "linux[-_]32[-_].+",
            "linux[-_]64[-_].+",
            "freebsd[-_]32[-_].+",
            "freebsd[-_]64[-_].+",

            Args:
                os_name (str): Name that matches the format for the os flag.

            Returns:
                None
        """
        print ("os_name=[%s]" % os_name)
        os_name_new = self.ffi.new("char[]", bytes(os_name, "utf-8"))
        self.libpanda.panda_set_os_name(os_name_new)

    def get_os_family(self):
        '''
        Get the current OS family name. Valid values are the entries in `OSFamilyEnum`

        Returns:
            string: one of OS_UNKNOWN, OS_WINDOWS, OS_LINUX, OS_FREEBSD
        '''

        family_num = self.libpanda.panda_os_familyno
        family_name = self.ffi.string(self.ffi.cast("PandaOsFamily", family_num))
        return family_name
    
    def get_file_name(self, cpu, fd):
        '''
        Get the name of a file from a file descriptor.

        Returns:
            string: file name
            None: on failure
        '''
        proc = self.plugins['osi'].get_current_process(cpu)
        if proc == self.ffi.NULL:
            return None
        try:
            fname_ptr = self.plugins['osi_linux'].osi_linux_fd_to_filename(cpu, proc, fd)
        except OverflowError:
            return None
        if fname_ptr == self.ffi.NULL:
            return None
        return self.ffi.string(fname_ptr)

    def get_current_process(self, cpu):
        '''
        Get the current process as an OsiProc struct.

        Returns:
            string: process name
            None: on failure
        '''
        proc = self.plugins['osi'].get_current_process(cpu)
        if proc == self.ffi.NULL:
            return None
        return proc

    def get_mappings(self, cpu):
        '''
        Get all active memory mappings in the system.

        Requires: OSI

        Args:
            cpu: CPUState struct

        Returns:
            pandare.utils.GArrayIterator: iterator of OsiModule structures
        '''
        current = self.plugins['osi'].get_current_process(cpu)
        maps = self.plugins['osi'].get_mappings(cpu, current)
        map_len = self.garray_len(maps)
        return GArrayIterator(self.plugins['osi'].get_one_module, maps, map_len, self.plugins['osi'].cleanup_garray)

    def get_mapping_by_addr(self, cpu, addr):
        '''
        Return the OSI mapping that matches the address specified.

        Requires: OSI

        Args:
            cpu: CPUState struct
            addr: int

        Returns:
            OsiModule: dataclass representation of OsiModule structure with strings converted to python strings
                Note that the strings will be None if their pointer was null
            None: on failure
        '''
        @dataclass
        class OsiModule:
            '''dataclass representation of OsiModule structu'''
            base: int
            file: str
            modd: int
            name: str
            size: int
        mappings = self.get_mappings(cpu)
        for m in mappings:
            if m == self.ffi.NULL:
                continue
            if addr >= m.base and addr < m.base+m.size:
                if m.name != self.ffi.NULL:
                    name = self.ffi.string(m.name).decode("utf-8")
                else:
                    name = None
                if m.file != self.ffi.NULL:
                    file = self.ffi.string(m.file).decode("utf-8")
                else:
                    file = None
                return OsiModule(m.base, file, m.modd, name, m.size)
        return None

    def get_processes(self, cpu):
        '''
        Get all running processes in the system. Includes kernel modules on Linux.

        Requires: OSI

        Args:
            cpu: CPUState struct

        Returns:
            pandare.utils.GArrayIterator: iterator of OsiProc structures
        '''
        processes = self.plugins['osi'].get_processes(cpu)
        processes_len = self.garray_len(processes)
        return GArrayIterator(self.plugins['osi'].get_one_proc, processes, processes_len, self.plugins['osi'].cleanup_garray)

    def get_processes_dict(self, cpu):
        '''
        Get all running processes for the system at this moment in time as a dictionary.

        The dictionary maps proceses by their PID. Each mapping returns a dictionary containing the process name, its pid,
        and its parent pid (ppid).

        Requires: OSI

        Args:
            cpu: CPUState struct

        Returns:
            Dict: processes as described above
        '''

        procs = {} #pid: {name: X, pid: Y, parent_pid: Z})

        for proc in self.get_processes(cpu):
            assert(proc != self.ffi.NULL)
            assert(proc.pid not in procs)
            procs[proc.pid] = {'name': self.ffi.string(proc.name).decode('utf8', 'ignore'),
                               'pid': proc.pid,
                               'parent_pid': proc.ppid,
                               'create_time': proc.create_time}
            assert(not (proc.pid != 0 and proc.pid == proc.ppid)) # No cycles allowed other than at 0
        return procs

    def get_process_name(self, cpu):
        '''
        Get the name of the current process. May return None if OSI cannot identify the current process
        '''
        proc = self.plugins['osi'].get_current_process(cpu)
        if proc == self.ffi.NULL or proc.name == self.ffi.NULL:
            return None

        procname = self.ffi.string(proc.name).decode('utf8', 'ignore')
        return self.ffi.string(proc.name).decode('utf8', 'ignore')


    ################## PYPERIPHERAL FUNCTIONS #####################
    # Pyperipherals are objects which handle mmio read/writes using the PANDA callback infrastructure.
    # Under the hood, they use the cb_unassigned_io_read/cb_unassigned_io_write callbacks.
    # A python peripheral itself is an object which exposes the following functions:
    #     write_memory(self, address, size, value)
    #     read_memory(self, address, size)
    # And has at least the following attributes:
    #     address
    #     size

    # One example for such a python object are avatar2's AvatarPeripheral.
    def _addr_to_pyperipheral(self, address):
        """
        Returns the python peripheral for a given address, or None if no
        peripheral is registered for that address
        """

        for pp in self.pyperipherals:
            if pp.address <= address < pp.address + pp.size:
                return pp
        return None

    def _validate_object(self, object):
        # This function makes sure that the object exposes the right interfaces

        if not hasattr(object, "address") or not isinstance(object.address, int):
            raise RuntimeError(
                (
                    "Registering PyPeripheral {} failed:\n"
                    "Missing or non-int `address` attribute"
                ).format(str(object.__repr__()))
            )

        if not hasattr(object, "size") or not isinstance(object.size, int):
            raise RuntimeError(
                (
                    "Registering PyPeripheral {} failed:\n"
                    "Missing or non-int `address` attribute"
                ).format(object.__repr__())
            )

        if not hasattr(object, "read_memory"):
            raise RuntimeError(
                (
                    "Registering PyPeripheral {} failed:\n"
                    "Missing read_memory function"
                ).format(object.__repr__())
            )

        params = list(signature(object.read_memory).parameters)
        if params[0] != "address" or params[1] != "size":
            raise RuntimeError(
                (
                    "Registering PyPeripheral {} failed:\n"
                    "Invalid function signature for read_memory"
                ).format(object.__repr__())
            )

        if not hasattr(object, "write_memory"):
            raise RuntimeError(
                (
                    "Registering PyPeripheral {} failed:\n"
                    "Missing write_memory function"
                ).format(object.__repr__())
            )

        params = list(signature(object.write_memory).parameters)
        if params[0] != "address" or params[1] != "size" or params[2] != "value":
            raise RuntimeError(
                (
                    "Registering PyPeripheral {} failed:\n"
                    "Invalid function signature for write_memory"
                ).format(object.__repr__())
            )

        # Ensure object is not overlapping with any other pyperipheral
        if (
            self._addr_to_pyperipheral(object.address) is not None
            or self._addr_to_pyperipheral(object.address + object.size) is not None
        ):
            raise RuntimeError(
                (
                    "Registering PyPeripheral {} failed:\n" "Overlapping memories!"
                ).format(object.__repr__())
            )

        return True

    def pyperiph_read_cb(self, cpu, pc, physaddr, size, val_ptr):
        pp = self._addr_to_pyperipheral(physaddr)
        if pp is None:
            return False

        val = pp.read_memory(physaddr, size)
        buf = self.ffi.buffer(val_ptr, size)

        fmt = "{}{}".format(self._end2fmt[self.endianness], self._num2fmt[size])

        pack_into(fmt, buf, 0, val)

        return True

    def pyperiph_write_cb(self, cpu, pc, physaddr, size, val):
        pp = self._addr_to_pyperipheral(physaddr)
        if pp is None:
            return False

        pp.write_memory(physaddr, size, val)
        return True

    def register_pyperipheral(self, object):
        """
        Registers a python peripheral, and the necessary attributes to the
        panda-object, if not present yet.
        """

        # if we are the first pyperipheral, register the pp-dict
        if not hasattr(self, "pyperipherals"):
            self.pyperipherals = []
            self.pyperipherals_registered_cb = False
            self._num2fmt = {1: "B", 2: "H", 4: "I", 8: "Q"}
            self._end2fmt = {"little": "<", "big": ">"}

        self._validate_object(object)

        if self.pyperipherals_registered_cb is False:
            self.register_callback(
                self.callback.unassigned_io_read,
                self.callback.unassigned_io_read(self.pyperiph_read_cb),
                "pyperipheral_read_callback",
            )

            self.register_callback(
                self.callback.unassigned_io_write,
                self.callback.unassigned_io_write(self.pyperiph_write_cb),
                "pyperipheral_write_callback",
            )

            self.pyperipherals_registered_cb = True

        self.pyperipherals.append(object)

    def unregister_pyperipheral(self, pyperiph):
        """
        deregisters a python peripheral.
        The pyperiph parameter can be either an object, or an address
        Returns true if the pyperipheral was successfully removed, else false.
        """

        if isinstance(pyperiph, int) is True:
            pp = self._addr_to_pyperipheral(pyperiph)
            if pp is None:
                return False
        else:
            if pyperiph not in self.pyperipherals:
                return False
            pp = pyperiph

        self.pyperipherals.remove(pp)

        # If we dont have any pyperipherals left, unregister callbacks
        if len(self.pyperipherals) == 0:
            self.disable_callback("pyperipheral_read_callback", forever=True)
            self.disable_callback("pyperipheral_write_callback", forever=True)
            self.pyperipherals_registered_cb = False
        return True
    

    ############## TAINT FUNCTIONS ###############
    # Convenience methods for interacting with the taint subsystem.

    def taint_enabled(self):
        '''
        Checks to see if taint2 plugin has been loaded
        '''
        return self._plugin_loaded("taint2") and self.plugins["taint2"].taint2_enabled()

    def taint_enable(self):
        '''
        Enable taint.
        '''
        self.plugins["taint2"].taint2_enable_taint()
    
    def _assert_taint_enabled(self):
        if not self.taint_enabled():
            raise Exception("taint2 must be loaded before tainting values")

    def taint_label_reg(self, reg_num, label):
        '''
        Labels taint register reg_num with label.
        '''
        self._assert_taint_enabled()
        for i in range(self.register_size):
            self.plugins["taint2"].taint2_label_reg(reg_num, i, label)

    def taint_label_ram(self, addr, label):
        '''
        Labels ram at address with label.
        '''
        self._assert_taint_enabled()
        self.plugins["taint2"].taint2_label_ram(addr, label)

    def taint_check_reg(self, reg_num):
        '''
        Checks if register reg_num is tainted. Returns boolean.
        '''
        self._assert_taint_enabled()
        for offset in range(self.register_size):
            if self.plugins['taint2'].taint2_query_reg(reg_num, offset) > 0:
                return True
        return False

    def taint_check_ram(self, addr):
        '''
        returns boolean representing if physical address is tainted.
        '''
        self._assert_taint_enabled()
        return self.plugins['taint2'].taint2_query_ram(addr) > 0

    def taint_get_reg(self, reg_num):
        '''
        Returns array of results, one for each byte in this register
        None if no taint.  QueryResult struct otherwise
        '''
        self._assert_taint_enabled()
        res = []
        for offset in range(self.register_size):
            if self.plugins['taint2'].taint2_query_reg(reg_num, offset) > 0:
                query_res = self.ffi.new("QueryResult *")
                self.plugins['taint2'].taint2_query_reg_full(reg_num, offset, query_res)
                tq = TaintQuery(query_res, self.plugins['taint2'], self.ffi)
                res.append(tq)
            else:
                res.append(None)
        return res

    def taint_get_ram(self, addr):
        '''
        returns array of results, one for each byte in this register
        None if no taint.  QueryResult struct otherwise
        '''
        self._assert_taint_enabled()
        if self.plugins['taint2'].taint2_query_ram(addr) > 0:
            query_res = self.ffi.new("QueryResult *")
            self.plugins['taint2'].taint2_query_ram_full(addr, query_res)
            tq = TaintQuery(query_res, self.plugins['taint2'], self.ffi)
            return tq
        else:
            return None

    def taint_check_laddr(self, addr, off):
        '''
        returns boolean result checking if this laddr is tainted
        '''
        self._assert_taint_enabled()
        return self.plugins['taint2'].taint2_query_laddr(addr, off) > 0

    def taint_get_laddr(self, addr, offset):
        '''
        returns array of results, one for each byte in this laddr
        None if no taint.  QueryResult struct otherwise
        '''
        self._assert_taint_enabled()
        if self.plugins['taint2'].taint2_query_laddr(addr, offset) > 0:
            query_res = self.ffi.new("QueryResult *")
            self.plugins['taint2'].taint2_query_laddr_full(addr, offset, query_res)
            tq = TaintQuery(query_res, self.plugins['taint2'], self.ffi)
            return tq
        else:
            return None
    
    def address_to_ram_offset(self, hwaddr, is_write):
        '''
        Convert physical address to ram offset

        Args:
            hwaddr (int): physical address
            is_write (bool): boolean representing if this is a write

        Returns:
            ram offset (int)

        Raises:
            ValueError if memory access fails or fmt is unsupported
        '''
        
        out = self.ffi.new("ram_addr_t*", self.ffi.cast("ram_addr_t", 0))
        value = self.libpanda.PandaPhysicalAddressToRamOffset_external(out, hwaddr, is_write)
        if value != 0:
            raise ValueError(f"address_to_ram_offset returned {value}")
        return out[0]

    # enables symbolic tracing
    def taint_sym_enable(self):
        """
        Inform python that taint is enabled.
        """
        if not self.taint_enabled():
            self.taint_enable()
            progress("taint symbolic not enabled -- enabling")
        self.plugins["taint2"].taint2_enable_sym()
    
    def _assert_taint_sym_enabled(self):
        self._assert_taint_enabled()
        self.plugins['taint2'].taint2_enable_sym()

    def taint_sym_label_ram(self, addr, label):
        self._assert_taint_sym_enabled()
        self.plugins['taint2'].taint2_sym_label_ram(addr,label)

    def taint_sym_label_reg(self, reg_num, label):
        # label all bytes in this register.
        # or at least four of them
        # XXX label must increment by panda.register_size after the call
        self._assert_taint_sym_enabled()
        self.taint_sym_enable()
        for i in range(self.register_size):
            self.plugins['taint2'].taint2_sym_label_reg(reg_num, i, label+i)
    
    # Deserialize a z3 solver
    # Lazy import z3. 
    def string_to_solver(self, string: str):
        from z3 import Solver
        s = Solver()
        s.from_string(string)
        return s

    # Get the first condition in serialized solver str
    def string_to_condition(self, string: str):
        s = self.string_to_solver(string)
        asrts = s.assertions()
        if len(asrts) == 0:
            return None 
        return asrts[0]

    # Get the expr in serialized solver str
    # (https://github.com/Z3Prover/z3/issues/2674) 
    def string_to_expr(self, string: str):
        eq = self.string_to_condition(string)
        if eq and len(eq.children()) > 0:
            return eq.children()[0]
        return None

    # Query the ram addr with given size
    def taint_sym_query_ram(self, addr, size=1):
        # Prepare ptr for returned string
        str_ptr_ffi = self.ffi.new('char**')
        # Prepare ptr for string size
        n_ptr_ffi = self.ffi.new('uint32_t *', 0)

        self.plugins['taint2'].taint2_sym_query_ram(addr, size, n_ptr_ffi, str_ptr_ffi)
        # Unpack size
        n = self.ffi.unpack(n_ptr_ffi, 1)[0]
        if n == 0:
            return None
        # Unpack cstr
        str_ptr = self.ffi.unpack(str_ptr_ffi, 1)[0]
        str_bs = self.ffi.unpack(str_ptr, n)
        expr_str = str(str_bs, 'utf-8')
        return self.string_to_expr(expr_str)

    # Query all bytes in this register.
    def taint_sym_query_reg(self, addr):
        # Prepare ptr for returned string
        str_ptr_ffi = self.ffi.new('char**')
        # Prepare ptr for string size
        n_ptr_ffi = self.ffi.new('uint32_t *', 0)

        self.plugins['taint2'].taint2_sym_query_reg(addr, n_ptr_ffi, str_ptr_ffi)
        # Unpack size
        n = self.ffi.unpack(n_ptr_ffi, 1)[0]
        if n == 0:
            return None
        # Unpack cstr
        str_ptr = self.ffi.unpack(str_ptr_ffi, 1)[0]
        str_bs = self.ffi.unpack(str_ptr, n)
        expr_str = str(str_bs, 'utf-8')
        return self.string_to_expr(expr_str)

    def taint_sym_path_constraints(self):
        # Prepare ptr for returned string
        str_ptr_ffi = self.ffi.new('char**')
        # Prepare ptr for string size
        n_ptr_ffi = self.ffi.new('uint32_t *', 0)

        self.plugins['taint2'].taint2_sym_path_constraints(n_ptr_ffi, str_ptr_ffi)
        # Unpack size
        n = self.ffi.unpack(n_ptr_ffi, 1)[0]
        if n == 0:
            return []
        # Unpack cstr
        str_ptr = self.ffi.unpack(str_ptr_ffi, 1)[0]
        str_bs = self.ffi.unpack(str_ptr, n)
        expr_str = str(str_bs, 'utf-8')
        solver = self.string_to_solver(expr_str)
        return solver.assertions() if solver != None else []

    def taint_sym_branch_meta(self):
        branch_meta_ptr_ffi = self.ffi.new('SymbolicBranchMeta **')
        n_ptr_ffi = self.ffi.new('uint32_t *', 0)

        self.plugins['taint2'].taint2_sym_branch_meta(n_ptr_ffi, branch_meta_ptr_ffi)
        # Unpack size
        n = self.ffi.unpack(n_ptr_ffi, 1)[0]
        if n == 0:
            return []
        meta_ptr = self.ffi.unpack(branch_meta_ptr_ffi, 1)[0]
        metas_ffi = self.ffi.unpack(meta_ptr, n)
        # Meta only has a pc field now
        metas = [
            meta_ffi.pc
            for meta_ffi in metas_ffi
        ]
        return metas



    ############ Volatility mixins
    """
    Utilities to integrate Volatility with PANDA. Highly experimental.
    """

    def make_panda_file_handler(self, debug=False):
        '''
        Constructs a file and file handler that volatility can't ignore to back by PANDA physical memory
        '''
        from urllib.request import BaseHandler
        if 'PandaFileHandler' in globals():  # already initialized
            return
        panda = self

        class PandaFile(object):
            def __init__(self, length, panda):
                self.pos = 0
                self.length = length
                self.closed = False
                self.mode = "rb"
                self.name = "/tmp/panda.panda"
                self.panda = panda
                self.classname = type(self).__name__

            def readable(self):
                return self.closed

            def read(self, size=1):
                if self.panda.bits == 32 and self.panda.arch_name == "i386":
                    data = self.panda.physical_memory_read(
                        self.pos & 0xfffffff, size)
                else:
                    data = self.panda.physical_memory_read(self.pos, size)
                if debug:
                    print(self.classname+": Reading " +
                          str(size)+" bytes from "+hex(self.pos))
                self.pos += size
                return data

            def peek(self, size=1):
                return self.panda.physical_memory_read(self.pos, size)

            def seek(self, pos, whence=0):
                if whence == 0:
                    self.pos = pos
                elif whence == 1:
                    self.pos += pos
                else:
                    self.pos = self.length - pos
                if self.pos > self.length:
                    print(self.classname+": We've gone off the deep end")
                if debug:
                    print(self.classname+" Seeking to address "+hex(self.pos))

            def tell(self):
                return self.pos

            def close(self):
                self.closed = True

        class PandaFileHandler(BaseHandler):
            def default_open(self, req):
                if 'panda.panda' in req.full_url:
                    length = panda.libpanda.ram_size
                    if length > 0xc0000000:
                        length += 0x40000000  # 3GB hole
                    if debug:
                        print(type(self).__name__ +
                              ": initializing PandaFile with length="+hex(length))
                    return PandaFile(length=length, panda=panda)
                else:
                    return None

            def file_close(self):
                return True

        globals()["PandaFileHandler"] = PandaFileHandler

    def get_volatility_symbols(self, debug=False):
        try:
            from .volatility_cli_classes import CommandLineMoreEfficient
            from volatility.framework import contexts
            from volatility.framework.layers.linear import LinearlyMappedLayer
            from volatility.framework.automagic import linux
        except ImportError:
            print("Warning: Failed to import volatility")
            return None
        if "linux" in self.os_type:
            if not hasattr(self, "_vmlinux"):
                self.make_panda_file_handler(debug=debug)
                constructed_original = CommandLineMoreEfficient().run()
                linux.LinuxUtilities.aslr_mask_symbol_table(
                    constructed_original.context, constructed_original.config['vmlinux'], constructed_original.config['primary'])
                self._vmlinux = contexts.Module(
                    constructed_original.context, constructed_original.config['vmlinux'], constructed_original.config['primary'], 0)
            else:
                LinearlyMappedLayer.read.cache_clear()  # smearing technique
            return self._vmlinux
        else:
            print("Unsupported.")
            return None

    def run_volatility(self, plugin, debug=False):
        try:
            from .volatility_cli_classes import CommandLineRunFullCommand, StringTextRenderer
        except ImportError:
            print("Warning: Failed to import volatility")
            return None
        self.make_panda_file_handler(debug=debug)
        cmd = CommandLineRunFullCommand().run("-q -f panda.panda " + plugin)
        output = StringTextRenderer().render(cmd.run())
        return output

    ########## BLOCKING MIXINS ############
    '''
    Utilities to provide blocking interactions with PANDA. This includes serial and monitor interactions as well as file copy to the guest.
    XXX: Do not call any of the following from the main thread- they depend on the CPU loop running
    '''
    @blocking
    def stop_run(self):
        '''
        From a blocking thread, request vl.c loop to break. Returns control flow in main thread.
        In other words, once this is called, panda.run() will finish and your main thread will continue.
        If you also want to unload plugins, use end_analysis instead

        XXX: This doesn't work in replay mode
        '''
        reason = self.libpanda.SHUTDOWN_CAUSE_GUEST_SHUTDOWN
        self.libpanda.qemu_system_shutdown_request(reason)

    @blocking
    def run_serial_cmd(self, cmd, no_timeout=False, timeout=None):
        '''
        Run a command inside the guest through a terminal exposed over a serial port. Can only be used if your guest is configured in this way

        Guest output will be analyzed until we see the expect_prompt regex printed (i.e., the PS1 prompt)

        Args:
            cmd: command to run.
            timeout: maximum time to wait for the command to finish
            no_timeout: if set, don't ever timeout

        Returns:
            String: all the output (stdout + stderr) printed after typing your command and pressing enter until the next prompt was printed.
        '''

        if timeout is None:
            timeout = 30

        if self.serial_console is None:
            raise RuntimeError("Cannot run serial commands without providing PANDA an expect_prompt")
        self.running.wait() # Can only run serial when guest is running
        self.serial_console.sendline(cmd.encode("utf8"))
        if no_timeout:
            result = self.serial_console.expect(timeout=9999) # "Don't ever timeout" above is a bit of an exaggeration
        else:
            result = self.serial_console.expect(timeout=timeout)
        return result

    @blocking
    def serial_read_until(self, byte_sequence):
        if len(self.serial_unconsumed_data) > 0:
            found_idx = self.serial_unconsumed_data.find(byte_sequence)
            if found_idx >= 0:
                match = self.serial_unconsumed_data[ : found_idx]
                self.serial_unconsumed_data = self.serial_unconsumed_data[found_idx + 1 : ]
                return match
        while self.serial_socket != None:
            try:
                readable, _, _ = select.select([self.serial_socket], [], [], 0.5)
                if len(readable) == 0:
                    continue
                data = self.serial_socket.recv(65535)
            except Exception as e:
                if '[Errno 11]' in str(e) or '[Errno 35]' in str(e):
                    # EAGAIN
                    continue
                raise Exception("Data Read Error: {}".format(e.message))
            if not data:
                raise Exception('Connection Closed by Server')

            self.serial_unconsumed_data += data
            found_idx = self.serial_unconsumed_data.find(byte_sequence)
            if found_idx >= 0:
                match = self.serial_unconsumed_data[ : found_idx]
                self.serial_unconsumed_data = self.serial_unconsumed_data[found_idx + 1 : ]
                return match
        return None
            
    
    @blocking
    def run_serial_cmd_async(self, cmd, delay=1):
        '''
        Type a command and press enter in the guest. Return immediately. No results available
        Only use this if you know what you're doing!
        '''
        self.running.wait() # Can only run serial when guest is running
        self.serial_console.sendline(cmd.encode("utf8"))
        if delay:
            sleep(delay) # Ensure it has a chance to run

    @blocking
    def type_serial_cmd(self, cmd):
        #Can send message into socket without guest running (no self.running.wait())
        if isinstance(cmd, str):
            cmd = cmd.encode('utf8')
        self.serial_console.send(cmd) # send, not sendline

    def finish_serial_cmd(self):
        result = self.serial_console.send_eol()
        result = self.serial_console.expect()
        return result

    @blocking
    def run_monitor_cmd(self, cmd):
        self.monitor_console.sendline(cmd.encode("utf8"))
        result = self.monitor_console.expect()
        return result

    @blocking
    def revert_sync(self, snapshot_name):
        '''
        Args:
            snapshot_name: name of snapshot in the current qcow to load

        Returns:
            String: error message. Empty on success.
        '''
        result = self.run_monitor_cmd("loadvm {}".format(snapshot_name))
        # On success we should get no result

        if result.startswith("Length mismatch"):
            raise RuntimeError("QEMU machine's RAM size doesn't match snapshot RAM size!")

        if "does not have the requested snapshot" in result:
            raise ValueError(f"Snapshot '{snapshot_name}' not present in {self.qcow}")

        result = result.strip()
        if len(result):
            warn(f"snapshot load returned error {result}")

        return result

    @blocking
    def delvm_sync(self, snapshot_name):
        self.run_monitor_cmd("delvm {}".format(snapshot_name))

    @blocking
    def copy_to_guest(self, copy_directory, iso_name=None, absolute_paths=False, setup_script="setup.sh", timeout=None, cdrom=None):
        '''

        Copy a directory from the host into the guest by
        1) Creating an .iso image of the directory on the host
        2) Run a bash command to mount it at the exact same path + .ro and then copy the files to the provided path
        3) If the directory contains setup.sh, run it

        Args:
            copy_directory: Local directory to copy into guest
            iso_name: Name of iso file that will be generated. Defaults to [copy_directory].iso
            absolute_paths: is copy_directory an absolute or relative path
            seutp_script: name of a script which, if present inside copy_directory, will be automatically run after the copy
            timeout: maximum time each copy command will be allowed to run for, will use the `run_serial_cmd` default value unless another is provided

        Returns:
            None
        '''

        if not iso_name:
            iso_name = copy_directory + '.iso'
        make_iso(copy_directory, iso_name)

        if not absolute_paths:
            copy_directory = path.split(copy_directory)[-1] # Copy directory relative, not absolutely


        # Drive the guest to mount the drive
        # setup_sh:
        #   Make sure cdrom didn't automount
        #   Make sure guest path mirrors host path
        #   if there is a setup.sh script in the directory,
        #   then run that setup.sh script first (good for scripts that need to
        #   prep guest environment before script runs)
        mount_dir = shlex_quote(copy_directory)

        mkdir_result = self.run_serial_cmd(f"mkdir -p {mount_dir} {mount_dir}.ro && echo \"mkdir_ok\"; echo \"exit code $?\"", timeout=timeout)

        if 'mkdir_ok' not in mkdir_result:
            raise RuntimeError(f"Failed to create mount directories inside guest: {mkdir_result}")

        # Tell panda to we insert the CD drive
        # TODO: the cd-drive name should be a config option, see the values in qcow.py

        cd_drive_name = cdrom
        if cdrom is None:
            if self.cdrom is not None:
                cd_drive_name = self.cdrom
            else:
                cd_drive_name = "ide1-cd0"

        errs = self.run_monitor_cmd("change {} \"{}\"".format(cd_drive_name, iso_name))
        if len(errs):
            warn(f"Warning encountered when connecting media to guest: {errs}")

        try:
            mount_status = "bad"
            for _ in range(10):
                if 'mount_ok' in mount_status:
                    break
                mount_status = self.run_serial_cmd(f"mount /dev/cdrom {mount_dir}.ro && echo 'mount_ok' || (umount /dev/cdrom; echo 'bad')", timeout=timeout)
                sleep(1)
            else:
                # Didn't ever break
                raise RuntimeError(f"Failed to mount media inside guest: {mount_status}")

            # Note the . after our src/. directory - that's special syntax for cp -a
            copy_result = self.run_serial_cmd(f"cp -a {mount_dir}.ro/. {mount_dir} && echo 'copyok'", timeout=timeout)
            
            # NB: exact match here causing issues so making things more flexible
            if not ('copyok' in copy_result):
                raise RuntimeError(f"Copy to rw directory failed: {copy_result}")

        finally:
            # Ensure we disconnect the CD drive after the mount + copy, even if it fails
            self.run_serial_cmd("umount /dev/cdrom") # This can fail and that's okay, we'll forece eject
            sleep(1)
            errs = self.run_monitor_cmd(f"eject -f {cd_drive_name}")
            if len(errs):
                warn(f"Warning encountered when disconnecting media from guest: {errs}")

        if isfile(pjoin(copy_directory, setup_script)):
            setup_result = self.run_serial_cmd(f"{mount_dir}/{setup_script}", timeout=timeout)
            progress(f"[Setup command]: {setup_result}")

    @blocking
    def record_cmd(self, guest_command, copy_directory=None, iso_name=None, setup_command=None, recording_name="recording", snap_name="root", ignore_errors=False):
        '''
        Take a recording as follows:
            0) Revert to the specified snapshot name if one is set. By default 'root'. Set to `None` if you have already set up the guest and are ready to record with no revert
            1) Create an ISO of files that need to be copied into the guest if copy_directory is specified. Copy them in
            2) Run the setup_command in the guest, if provided
            3) Type the command you wish to record but do not press enter to begin execution. This avoids the recording capturing the command being typed
            4) Begin the recording (name controlled by recording_name)
            5) Press enter in the guest to begin the command. Wait until it finishes.
            6) End the recording
        '''
        # 0) Revert to the specified snapshot
        if snap_name is not None:
            self.revert_sync(snap_name) # Can't use self.revert because that would would run async and we'd keep going before the revert happens

        # 1) Make copy_directory into an iso and copy it into the guest - It will end up at the exact same path
        if copy_directory: # If there's a directory, build an ISO and put it in the cddrive
            # Make iso
            self.copy_to_guest(copy_directory, iso_name)

        # 2) Run setup_command, if provided before we start the recording (good place to CD or install, etc)
        if setup_command:
            print(f"Running setup command {setup_command}")
            r = self.run_serial_cmd(setup_command)
            print(f"Setup command results: {r}")

        # 3) type commmand (note we type command, start recording, finish command)
        self.type_serial_cmd(guest_command)

        # 4) start recording
        self.run_monitor_cmd("begin_record {}".format(recording_name))

        # 5) finish command
        result = self.finish_serial_cmd()

        if debug:
            progress("Result of `{}`:".format(guest_command))
            print("\t"+"\n\t".join(result.split("\n"))+"\n")

        if "No such file or directory" in result and not ignore_errors:
            print("Bad output running command: {}".format(result))
            raise RuntimeError("Command not found while taking recording")

        if "cannot execute binary file" in result and not ignore_errors:
            print("Bad output running command: {}".format(result))
            raise RuntimeError("Could not execute binary while taking recording")

        # 6) End recording
        self.run_monitor_cmd("end_record")

        print("Finished recording")

    @blocking
    def interact(self, confirm_quit=True):
        '''
        Expose console interactively until user types pandaquit
        Must be run in blocking thread.

        TODO: This should probably repace self.serial_console with something
        that directly renders output to the user. Then we don't have to handle
        buffering and other problems. But we will need to re-enable the serial_console
        interface after this returns
        '''
        print("PANDA: entering interactive mode. Type pandaquit to exit")
        prompt = self.expect_prompt.decode("utf8") if self.expect_prompt and isinstance(self.expect_prompt, bytes) else "$ "
        if not prompt.endswith(" "): prompt += " "
        while True:
            cmd = input(prompt) # TODO: Strip all control characters - Ctrl-L breaks things
            if cmd.strip() == 'pandaquit':
                if confirm_quit:
                    q = input("PANDA: Quitting interactive mode. Are you sure? (y/n) ")
                    if len(q) and q.lower()[0] == 'y':
                        break
                    else:
                        continue
                else: # No confirm - just break
                    break
            r = self.run_serial_cmd(cmd) # XXX: may timeout
            print(r)

    @blocking
    def do_panda_finish(self):
        '''
        Call panda_finish. Note this isn't really blocking - the
        guest should have exited by now, but queue this after
        (blocking) shutdown commands in our internal async queue
        so it must also be labeled as blocking.
        '''
#        assert (not self.running.is_set()), "Can't finish while still running"
        self.panda_finish()

    ################## CALLBACK FUNCTIONS ################
    # Mixin for handling callbacks and generation of decorators that allow users to register their own callbacks
    # such as panda.cb_before_block_exec()
    def register_cb_decorators(self):
        '''
        Setup callbacks and generate self.cb_XYZ functions for cb decorators
        XXX Don't add any other methods with names starting with 'cb_'
        Callbacks can be called as @panda.cb_XYZ in which case they'll take default arguments and be named the same as the decorated function
        Or they can be called as @panda.cb_XYZ(name='A', procname='B', enabled=True). Defaults: name is function name, procname=None, enabled=True unless procname set
        '''
        for cb_name, pandatype in zip(self.callback._fields, self.callback):
            def closure(closed_cb_name, closed_pandatype): # Closure on cb_name and pandatype
                def f(*args, **kwargs):
                    if len(args): # Called as @panda.cb_XYZ without ()s- no arguments to decorator but we get the function name instead
                        # Call our decorator with only a name argument ON the function itself
                        fun = args[0]
                        return self._generated_callback(closed_pandatype, **{"name": fun.__name__})(fun)
                    else:
                        # Otherwise, we were called as @panda.cb_XYZ() with potential args - Just return the decorator and it's applied to the function
                        return self._generated_callback(closed_pandatype, *args, **kwargs)
                return f

            setattr(self, 'cb_'+cb_name, closure(cb_name, pandatype))

    def _generated_callback(self, pandatype, name=None, procname=None, enabled=True, **kwargs):
        '''
        Actual implementation of self.cb_XYZ. pandatype is pcb.XYZ
        name must uniquely describe a callback
        if procname is specified, callback will only be enabled when that asid is running (requires OSI support)
        '''

        # if procname:
        #     enabled = False # Process won't be running at time 0 (probably)
        #     self._register_internal_asid_changed_cb()

        def decorator(fun):
            local_name = name  # We need a new varaible otherwise we have scoping issues with _generated_callback's name
            if name is None:
                local_name = fun.__name__

            # 0 works for all callbacks except void. We check later on
            # to see if we need to return None otherwise we return 0
            return_from_exception = 0

            def _run_and_catch(*args, **kwargs): # Run function but if it raises an exception, stop panda and raise it
                if hasattr(self, "exit_exception"):
                    # An exception has been raised previously - do not even run the function. But we need to match the expected
                    # return type or we'll raise more errors.
                    if return_type:
                        return self.ffi.cast(return_type, 0)
                else:
                    try:
                        r = fun(*args, **kwargs)
                        #print(pandatype, type(r)) # XXX Can we use pandatype to determine requried return and assert if incorrect
                        #assert(isinstance(r, int)), "Invalid return type?"
                        #print(fun, r) # Stuck with TypeError in _run_and_catch? Enable this to find where the bug is.
                        if return_type:
                            try:
                                return self.ffi.cast(return_type, r)
                            except TypeError:
                                # consider throwing an exception
                                return self.ffi.cast(return_type, 0)
                    except Exception as e:
                        # exceptions wont work in our thread. Therefore we print it here and then throw it after the
                        # machine exits.
                        if self.catch_exceptions:
                            self.exit_exception = e
                            raise e
                            print(e)
                            self.end_analysis()
                        else:
                            raise e
                        if return_type is not None:
                            return self.ffi.cast(return_type, 0)

            cast_rc = pandatype(_run_and_catch)
            return_type = self.ffi.typeof(cast_rc).result
            
            if return_type.cname == "void":
                return_type = None

            self.register_callback(pandatype, cast_rc, local_name, enabled=enabled, procname=procname, **kwargs)
            def wrapper(*args, **kw):
                return _run_and_catch(*args, **kw)
            return wrapper
        return decorator

    def _register_internal_asid_changed_cb(self):
        '''
        Call this function if you need procname filtering for callbacks. It enables
        an internal callback on asid_changed (and sometimes an after_block_exec cb)
        which will deteremine when the process name changes and enable/disable other callbacks
        that filter on process name.
        '''
        if self._registered_asid_changed_internal_cb: # Already registered these callbacks
            return

        @self.ppp("syscalls2", "on_sys_brk_enter")
        def on_sys_brk_enter(cpu, pc, brk):
            name = self.get_process_name(cpu)
            asid = self.libpanda.panda_current_asid(cpu)
            if self.asid_mapping.get(asid, None) != name:
                self.asid_mapping[asid] = name
                self._procname_changed(cpu, name)

        @self.callback.after_block_exec
        def __get_pending_procname_change(cpu, tb, exit_code):
            if exit_code: # Didn't actually execute block
                return None
            if not self.in_kernel(cpu): # Once we're out of kernel code, grab procname
                process = self.plugins['osi'].get_current_process(cpu)
                if process != self.ffi.NULL:
                    name = self.ffi.string(process.name).decode("utf8", "ignore")
                else:
                    return None # Couldn't figure out the process
                asid = self.libpanda.panda_current_asid(cpu)
                self.asid_mapping[asid] = name
                self._procname_changed(cpu, name)
                self.disable_callback('__get_pending_procname_change') # Disabled to begin


        # Local function def
        @self.callback.asid_changed
        def __asid_changed(cpustate, old_asid, new_asid):
            '''
            When the ASID changes, check if we know its procname (in self.asid_mapping),
            if so, call panda._procname_changed(cpu, name). Otherwise, we enable __get_pending_procname_change CB, which
            waits until the procname changes. Then we grab the new procname, update self.asid_mapping and call
            panda._procname_changed(cpu, name)
            '''
            if old_asid == new_asid:
                return 0

            if new_asid not in self.asid_mapping: # We don't know this ASID->procname - turn on __get_pending_procname_change
                if not self.is_callback_enabled('__get_pending_procname_change'):
                    self.enable_callback('__get_pending_procname_change')
            else: # We do know this ASID->procname, just call procname_changed
                self._procname_changed(cpustate, self.asid_mapping[new_asid])

            return 0

        self.register_callback(self.callback.asid_changed, __asid_changed, "__asid_changed") # Always call on ASID change

        # This internal callback is only enabled on-demand (later) when we need to figure out ASID->procname mappings
        self.register_callback(self.callback.after_block_exec, __get_pending_procname_change, "__get_pending_procname_change", enabled=False)

        self._registered_asid_changed_internal_cb = True

    def register_callback(self, callback, function, name, enabled=True, procname=None):
        # CB   = self.callback.main_loop_wait
        # func = main_loop_wait_cb
        # name = main_loop_wait

        if name in self.registered_callbacks:
            print(f"Warning: replacing existing callback '{name}' since it was re-registered")
            self.delete_callback(name)

        cb = self.callback_dictionary[callback]

        # Generate a unique handle for each callback type using the number of previously registered CBs of that type added to a constant
        self.plugin_register_count += 1
        handle = self.ffi.cast('void *', self.plugin_register_count)

        # XXX: We should have another layer of indirection here so we can catch
        #      exceptions raised during execution of the CB and abort analysis
        pcb = self.ffi.new("panda_cb *", {cb.name:function})

        if debug:
            progress("Registered function '{}' to run on callback {}".format(name, cb.name))

        self.libpanda.panda_register_callback_helper(handle, cb.number, pcb)
        self.registered_callbacks[name] = {"procname": procname, "enabled": True, "callback": cb,
                           "handle": handle, "pcb": pcb, "function": function} # XXX: if function is not saved here it gets GC'd and everything breaks! Watch out!

        if not enabled: # Note the registered_callbacks dict starts with enabled true and then we update it to false as necessary here
            self.disable_callback(name)

        if "block" in cb.name and "start" not in cb.name and "end" not in cb.name:
            if not self.disabled_tb_chaining:
                print("Warning: disabling TB chaining to support {} callback".format(cb.name))
                self.disable_tb_chaining()


    def is_callback_enabled(self, name):
        if name not in self.registered_callbacks.keys():
            raise RuntimeError("No callback has been registered with name '{}'".format(name))
        return self.registered_callbacks[name]['enabled']

    def enable_internal_callbacks(self):
        '''
        Enable all our internal callbacks that start with __ such as __main_loop_wait
        and __asid_changed. Important in case user has done a panda.end_analysis()
        and then (re)called run
        '''
        for name in self.registered_callbacks.keys():
            if name.startswith("__") and not self.registered_callbacks[name]['enabled']:
                self.enable_callback(name)

    def enable_all_callbacks(self):
        '''
        Enable all python callbacks that have been disabled
        '''
        for name in self.registered_callbacks.keys():
            self.enable_callback(name)

    def enable_callback(self, name):
        '''
        Enable a panda plugin using its handle and cb.number as a unique ID
        '''

        # During shutdown callback may be deleted before a request to enable comes through
        if self.ending:
            return

        if name not in self.registered_callbacks.keys():
            raise RuntimeError("No callback has been registered with name '{}'".format(name))

        self.registered_callbacks[name]['enabled'] = True
        handle = self.registered_callbacks[name]['handle']
        cb = self.registered_callbacks[name]['callback']
        pcb = self.registered_callbacks[name]['pcb']
        #progress("Enabling callback '{}' on '{}' handle = {}".format(name, cb.name, handle))
        self.libpanda.panda_enable_callback_helper(handle, cb.number, pcb)

    def disable_callback(self, name, forever=False):
        '''
        Disable a panda plugin using its handle and cb.number as a unique ID
        If forever is specified, we'll never reenable the call- useful when
        you want to really turn off something with a procname filter.
        '''
        # During shutdown callback may be deleted before a request to enable comes through
        if self.ending:
            return

        if name not in self.registered_callbacks.keys():
            raise RuntimeError("No callback has been registered with name '{}'".format(name))
        self.registered_callbacks[name]['enabled'] = False
        handle = self.registered_callbacks[name]['handle']
        cb = self.registered_callbacks[name]['callback']
        pcb = self.registered_callbacks[name]['pcb']
        #progress("Disabling callback '{}' on '{}' handle={}".format(name, cb.name, handle))
        self.libpanda.panda_disable_callback_helper(handle, cb.number, pcb)

        if forever:
            del self.registered_callbacks[name]

    def delete_callback(self, name):
        '''
        Completely delete a registered panda callback by name
        '''
        if name not in self.registered_callbacks.keys():
            raise ValueError("No callback has been registered with name '{}'".format(name))

        handle = self.registered_callbacks[name]['handle']
        self.libpanda.panda_unregister_callbacks(handle)
        if not hasattr(self,"old_cb_list"):
            self.old_cb_list = []
        self.old_cb_list.append(self.registered_callbacks[name])
        del self.registered_callbacks[name]['handle']
        del self.registered_callbacks[name]

    def delete_callbacks(self):
        #for name in self.registered_callbacks.keys():
        while len(self.registered_callbacks.keys()) > 0:
            self.delete_callback(list(self.registered_callbacks.keys())[0])

        # Disable PPP callbacks
        for name in list(self.ppp_registered_cbs) if hasattr(self, 'ppp_registered_cbs') else []:
            self.disable_ppp(name)

    ###########################
    ### PPP-style callbacks ###
    ###########################

    def ppp(self, plugin_name, attr, name=None, autoload=True):
        '''
        Decorator for plugin-to-plugin interface. Note this isn't in decorators.py
        becuase it uses the panda object.

        Example usage to register my_run with syscalls2 as a 'on_sys_open_return'
        @ppp("syscalls2", "on_sys_open_return")
        def my_fun(cpu, pc, filename, flags, mode):
            ...
        '''

        if plugin_name not in self.plugins and autoload: # Could automatically load it?
            print(f"PPP automatically loaded plugin {plugin_name}")

        if not hasattr(self, "ppp_registered_cbs"):
            self.ppp_registered_cbs = {}
            # We use this to traak fn_names->fn_pointers so we can later disable by name

            # XXX: if  we don't save the cffi generated callbacks somewhere in Python,
            # they may get garbage collected even though the c-code could still has a
            # reference to them  which will lead to a crash. If we stop using this to track
            # function names, we need to keep it or something similar to ensure the reference
            # count remains >0 in python

        def decorator(fun):
            local_name = name  # We need a new varaible otherwise we have scoping issues, maybe
            if local_name is None:
                local_name = fun.__name__

            def _run_and_catch(*args, **kwargs): # Run function but if it raises an exception, stop panda and raise it
                if not hasattr(self, "exit_exception"):
                    try:
                        r = fun(*args, **kwargs)
                        #print(pandatype, type(r)) # XXX Can we use pandatype to determine requried return and assert if incorrect
                        #assert(isinstance(r, int)), "Invalid return type?"
                        if return_type is not None:
                            try:
                                return self.ffi.cast(return_type, r)
                            except TypeError:
                                # consider throwing an exception
                                return self.ffi.cast(return_type, 0)
                    except Exception as e:
                        # exceptions wont work in our thread. Therefore we print it here and then throw it after the
                        # machine exits.
                        if self.catch_exceptions:
                            self.exit_exception = e
                            self.end_analysis()
                        else:
                            raise e
                        # this works in all current callback cases. CFFI auto-converts to void, bool, int, and int32_t
                        if return_type is not None:
                            return self.ffi.cast(return_type, 0)

            cast_rc = self.ffi.callback(attr+"_t")(_run_and_catch)  # Wrap the python fn in a c-callback.
            return_type = self.ffi.typeof(cast_rc).result
            
            if return_type.cname == "void":
                return_type = None

            if local_name == "<lambda>":
                local_name = f"<lambda_{self.lambda_cnt}>"
                self.lambda_cnt += 1

            if local_name in self.ppp_registered_cbs:
                print(f"Warning: replacing existing PPP callback '{local_name}' since it was re-registered")
                self.disable_ppp(local_name)

            assert (local_name not in self.ppp_registered_cbs), f"Two callbacks with conflicting name: {local_name}"

            # Ensure function isn't garbage collected, and keep the name->(fn, plugin_name, attr) map for disabling
            self.ppp_registered_cbs[local_name] = (cast_rc, plugin_name, attr)

            getattr(self.plugins[plugin_name], f'ppp_add_cb_{attr}')(cast_rc) # All PPP  cbs start with this string.
            return cast_rc
        return decorator


    def disable_ppp(self, name):
        '''
        Disable a ppp-style callback by name.
        Unlike regular panda callbacks which can be enabled/disabled/deleted, PPP callbacks are only enabled/deleted (which we call disabled)

        Example usage to register my_run with syscalls2 as a 'on_sys_open_return' and then disable:
        ```
        @ppp("syscalls2", "on_sys_open_return")
        def my_fun(cpu, pc, filename, flags, mode):
            ...

        panda.disable_ppp("my_fun")
        ```

        -- OR --

        ```
        @ppp("syscalls2", "on_sys_open_return", name="custom")
        def my_fun(cpu, pc, filename, flags, mode):
            ...
        ```

        panda.disable_ppp("custom")
        '''

        (f, plugin_name, attr) = self.ppp_registered_cbs[name]
        getattr(self.plugins[plugin_name], f'ppp_remove_cb_{attr}')(f) # All PPP cbs start with this string.
        del self.ppp_registered_cbs[name] # It's now safe to be garbage collected

    ########## GDB MIXINS ##############
    """
    Provides the ability to interact with a QEMU attached gdb session by setting and clearing breakpoints. Experimental.
    """

    def set_breakpoint(self, cpu, pc):
        '''
        Set a GDB breakpoint such that when the guest hits PC, execution is paused and an attached
        GDB instance can introspect on guest memory. Requires starting panda with -s, at least for now
        '''
        BP_GDB = 0x10
        self.libpanda.cpu_breakpoint_insert(cpu, pc, BP_GDB, self.ffi.NULL)

    def clear_breakpoint(self, cpu, pc):
        '''
        Remove a breakpoint
        '''
        BP_GDB = 0x10
        self.libpanda.cpu_breakpoint_remove(cpu, pc, BP_GDB)

    ############# HOOKING MIXINS ###############

    def hook(self, addr, enabled=True, kernel=None, asid=None, cb_type="start_block_exec"):
        '''
        Decorate a function to setup a hook: when a guest goes to execute a basic block beginning with addr,
        the function will be called with args (CPUState, TranslationBlock)
        '''

        def decorator(fun):
            if cb_type == "before_tcg_codegen" or cb_type == "after_block_translate" or cb_type == "before_block_exec" or cb_type == "start_block_exec" or cb_type == "end_block_exec":
                hook_cb_type = self.ffi.callback("void(CPUState*, TranslationBlock* , struct hook *)")
            elif cb_type == "after_block_exec":
                hook_cb_type = self.ffi.callback("void(CPUState*, TranslationBlock* , uint8_t, struct hook *)")
            elif cb_type == "before_block_translate":
                hook_cb_type = self.ffi.callback("void(CPUState* env, target_ptr_t pc, struct hook*)")
            elif cb_type == "before_block_exec_invalidate_opt":
                hook_cb_type = self.ffi.callback("bool(CPUState* env, TranslationBlock*, struct hook*)")
            else:
                print("function type not supported")
                return
            type_num = getattr(self.libpanda, "PANDA_CB_"+cb_type.upper())

            if debug:
                print("Registering breakpoint at 0x{:x} -> {} == {}".format(addr, fun, 'cdata_cb'))
            
            def _run_and_catch(*args, **kwargs): # Run function but if it raises an exception, stop panda and raise it
                if not hasattr(self, "exit_exception"):
                    try:
                        r = fun(*args, **kwargs)
                        #print(pandatype, type(r)) # XXX Can we use pandatype to determine requried return and assert if incorrect
                        #assert(isinstance(r, int)), "Invalid return type?"
                        #print(fun, r) # Stuck with TypeError in _run_and_catch? Enable this to find where the bug is.
                        return r
                    except Exception as e:
                        # exceptions wont work in our thread. Therefore we print it here and then throw it after the
                        # machine exits.
                        if self.catch_exceptions:
                            self.exit_exception = e
                            self.end_analysis()
                        else:
                            raise e
                        return 0

            # Inform the plugin that it has a new breakpoint at addr
            hook_cb_passed = hook_cb_type(_run_and_catch)
            new_hook = self.ffi.new("struct hook*")
            new_hook.type = type_num
            new_hook.addr = addr
            if kernel or asid is None:
                new_hook.asid = 0
            else:
                new_hook.asid = asid

            setattr(new_hook.cb,cb_type, hook_cb_passed)
            if kernel:
                new_hook.km = self.libpanda.MODE_KERNEL_ONLY
            elif kernel == False:
                new_hook.km = self.libpanda.MODE_USER_ONLY
            else:
                new_hook.km = self.libpanda.MODE_ANY
            new_hook.enabled = enabled

            self.plugins['hooks'].add_hook(new_hook)
            self.hook_list.append((new_hook, hook_cb_passed))

            def wrapper(*args, **kw):
                return _run_and_catch(args,kw)
            return wrapper
        return decorator

    def hook_symbol_resolution(self, libraryname, symbol, name=None):
        '''
        Decorate a function to setup a hook: when a guest process resolves a symbol
        the function will be called with args (CPUState, struct hook_symbol_resolve, struct symbol, OsiModule)

        Args:
            libraryname (string): Name of library containing symbol to be hooked. May be None to match any.
            symbol (string, int): Name of symbol or offset into library to hook
            name (string): name of hook, defaults to function name

        Returns:
            None: Decorated function is called when guest resolves the specified symbol in the specified library.
        '''
        #Mostly based on hook_symbol below
        def decorator(fun):
            sh = self.ffi.new("struct hook_symbol_resolve*")
            sh.hook_offset = False
            if symbol is not None:
                if isinstance(symbol, int):
                    sh.offset = symbol
                    sh.hook_offset = True
                    symbolname_ffi = self.ffi.new("char[]",bytes("\x00\x00\x00\x00","utf-8"))
                else:
                    symbolname_ffi = self.ffi.new("char[]",bytes(symbol,"utf-8"))
            else:
                symbolname_ffi = self.ffi.new("char[]",bytes("\x00\x00\x00\x00","utf-8"))
            self.ffi.memmove(sh.name,symbolname_ffi,len(symbolname_ffi))

            if libraryname is not None:
                libname_ffi = self.ffi.new("char[]",bytes(libraryname,"utf-8"))
            else:
                libname_ffi = self.ffi.new("char[]",bytes("\x00\x00\x00\x00","utf-8"))
            self.ffi.memmove(sh.section,libname_ffi,len(libname_ffi))

            #sh.id #not used here
            sh.enabled = True
            def _run_and_catch(*args, **kwargs): # Run function but if it raises an exception, stop panda and raise it
                if not hasattr(self, "exit_exception"):
                    try:
                        r = fun(*args, **kwargs)
                        return r
                    except Exception as e:
                        # exceptions wont work in our thread. Therefore we print it here and then throw it after the
                        # machine exits.
                        if self.catch_exceptions:
                            self.exit_exception = e
                            self.end_analysis()
                        else:
                            raise e
                        return None

            sr_hook_cb_type = self.ffi.callback("void (struct hook_symbol_resolve *sh, struct symbol s, target_ulong asid)")
            sr_hook_cb_ptr = sr_hook_cb_type(_run_and_catch)
            sh.cb = sr_hook_cb_ptr
            hook_ptr = self.plugins['dynamic_symbols'].hook_symbol_resolution(sh)
            self.sr_hooks.append((sh, sr_hook_cb_ptr, hook_ptr))

            def wrapper(*args, **kw):
                _run_and_catch(args,kw)
            return wrapper
        return decorator

    def hook_symbol(self, libraryname, symbol, kernel=False, name=None, cb_type="start_block_exec"):
        '''
        Decorate a function to setup a hook: when a guest goes to execute a basic block beginning with addr,
        the function will be called with args (CPUState, TranslationBlock, struct hook)

        Args:
            libraryname (string): Name of library containing symbol to be hooked. May be None to match any.
            symbol (string, int): Name of symbol or offset into library to hook
            kernel (bool): if hook should be applied exclusively in kernel mode
            name (string): name of hook, defaults to function name
            cb_type (string): callback-type, defaults to start_block_exec

        Returns:
            None: Decorated function is called when (before/after is determined by cb_type) guest goes to call
                  the specified symbol in the specified library.
        '''

        def decorator(fun):
            if cb_type == "before_tcg_codegen" or cb_type == "after_block_translate" or cb_type == "before_block_exec" or cb_type == "start_block_exec" or cb_type == "end_block_exec":
                hook_cb_type = self.ffi.callback("void(CPUState*, TranslationBlock* , struct hook *)")
            elif cb_type == "after_block_exec":
                hook_cb_type = self.ffi.callback("void(CPUState*, TranslationBlock* , uint8_t, struct hook *)")
            elif cb_type == "before_block_translate":
                hook_cb_type = self.ffi.callback("void(CPUState* env, target_ptr_t pc, struct hook*)")
            elif cb_type == "before_block_exec_invalidate_opt":
                hook_cb_type = self.ffi.callback("bool(CPUState* env, TranslationBlock*, struct hook*)")
            else:
                print("function type not supported")
                return

            def _run_and_catch(*args, **kwargs): # Run function but if it raises an exception, stop panda and raise it
                if not hasattr(self, "exit_exception"):
                    try:
                        r = fun(*args, **kwargs)
                        return r
                    except Exception as e:
                        # exceptions wont work in our thread. Therefore we print it here and then throw it after the
                        # machine exits.
                        if self.catch_exceptions:
                            self.exit_exception = e
                            self.end_analysis()
                        else:
                            raise e
                        if cb_type == "before_block_exec_invalidate_opt":
                            return False
                        return None


            # Inform the plugin that it has a new breakpoint at addr
            hook_cb_passed = hook_cb_type(_run_and_catch)
            new_hook = self.ffi.new("struct symbol_hook*")
            type_num = getattr(self.libpanda, "PANDA_CB_"+cb_type.upper())
            new_hook.type = type_num
            if libraryname is not None:
                libname_ffi = self.ffi.new("char[]",bytes(libraryname,"utf-8"))
            else:
                libname_ffi = self.ffi.new("char[]",bytes("\x00\x00\x00\x00","utf-8"))
            self.ffi.memmove(new_hook.section,libname_ffi,len(libname_ffi))

            new_hook.hook_offset = False
            if symbol is not None:
                if isinstance(symbol, int):
                    new_hook.offset = symbol
                    new_hook.hook_offset = True
                    symbolname_ffi = self.ffi.new("char[]",bytes("\x00\x00\x00\x00","utf-8"))
                else:
                    symbolname_ffi = self.ffi.new("char[]",bytes(symbol,"utf-8"))
                    new_hook.hook_offset = False
            else:
                symbolname_ffi = self.ffi.new("char[]",bytes("\x00\x00\x00\x00","utf-8"))
            self.ffi.memmove(new_hook.name,symbolname_ffi,len(symbolname_ffi))
            setattr(new_hook.cb,cb_type, hook_cb_passed)
            hook_ptr = self.plugins['hooks'].add_symbol_hook(new_hook)
            if name is not None:
                self.named_hooks[name] = hook_ptr
            self.hook_list.append((fun, new_hook,hook_cb_passed, hook_ptr))

            def wrapper(*args, **kw):
                _run_and_catch(args,kw)
            return wrapper
        return decorator

    def get_best_matching_symbol(self, cpu, pc=None, asid=None):
        '''
        Use the dynamic symbols plugin to get the best matching symbol for a given program counter.

        Args:
            cpu (CPUState): CPUState structure
            pc (int): program counter, defaults to current
            asid (int): ASID, defaults to current
        '''
        if asid is None:
            asid = self.current_asid(cpu)
        if pc is None:
            pc = self.current_pc(cpu)
        return self.plugins['dynamic_symbols'].get_best_matching_symbol(cpu, pc, asid)


    ################### Hooks2 Functions ############
    # Provides the ability to interact with the hooks2 plugin and receive callbacks based on user-provided criteria.

    def enable_hook2(self,hook_name):
        '''
        Set a hook2-plugin hook's status to active.

        .. Deprecated:: Use the hooks plugin instead.
        '''
        if hook_name in self.hook_list2:
            self.plugins['hooks2'].enable_hooks2(self.hook_list2[hook_name])
        else:
            print("ERROR: Your hook name was not in the hook list")

    def disable_hook2(self,hook_name):
        '''
        Set a hook2-plugin hook's status to inactive.

        .. Deprecated:: Use the hooks plugin instead.
        '''
        if hook_name in self.hook_list2:
            self.plugins['hooks2'].disable_hooks2(self.hook_list2[hook_name])
        else:
            print("ERROR: Your hook name was not in the hook list")

    def hook2(self,name, kernel=True, procname=None, libname=None, trace_start=0, trace_stop=0, range_begin=0, range_end=0):
        '''
        Decorator to create a hook with the hooks2 plugin.

        .. Deprecated:: Use the hooks plugin instead.
        '''

        if procname == None:
            procname = self.ffi.NULL
        if libname == None:
            libname = self.ffi.NULL


        if procname != self.ffi.NULL:
            procname = self.ffi.new("char[]",bytes(procname,"utf-8"))
        if libname != self.ffi.NULL:
            libname = self.ffi.new("char[]",bytes(libname,"utf-8"))
        '''
        Decorate a function to setup a hook: when a guest goes to execute a basic block beginning with addr,
        the function will be called with args (CPUState, TranslationBlock)
        '''
        def decorator(fun):
            # Ultimately, our hook resolves as a before_block_exec_invalidate_opt callback so we must match its args
            hook_cb_type = self.ffi.callback("bool (CPUState*, TranslationBlock*, void*)")
            # Inform the plugin that it has a new breakpoint at addr

            def _run_and_catch(*args, **kwargs): # Run function but if it raises an exception, stop panda and raise it
                if not hasattr(self, "exit_exception"):
                    try:
                        r = fun(*args, **kwargs)
                        #print(pandatype, type(r)) # XXX Can we use pandatype to determine requried return and assert if incorrect
                        #assert(isinstance(r, int)), "Invalid return type?"
                        #print(fun, r) # Stuck with TypeError in _run_and_catch? Enable this to find where the bug is.
                        return r
                    except Exception as e:
                        # exceptions wont work in our thread. Therefore we print it here and then throw it after the
                        # machine exits.
                        if self.catch_exceptions:
                            self.exit_exception = e
                            self.end_analysis()
                        else:
                            raise e
                        return True


            hook_cb_passed = hook_cb_type(_run_and_catch)
            if not hasattr(self, "hook_gc_list"):
                self.hook_gc_list = [hook_cb_passed]
            else:
                self.hook_gc_list.append(hook_cb_passed)

            # I don't know what this is/does
            cb_data =self.ffi.NULL
            hook_number = self.plugins['hooks2'].add_hooks2(hook_cb_passed, cb_data, kernel, \
                procname, libname, trace_start, trace_stop, range_begin,range_end)

            self.hook_list2[name] = hook_number

            def wrapper(*args, **kw):
                return _run_and_catch(*args, **kw)
            return wrapper
        return decorator

    def hook2_single_insn(self, name, pc, kernel=False, procname=None, libname=None):
        '''
        Helper function to hook a single instruction with the hooks2 plugin.

        .. Deprecated:: Use the hooks plugin instead.
        '''
        if procname == None:
            procname = self.ffi.NULL
        if libname == None:
            libname = self.ffi.NULL
        return self.hook2(name, kernel=kernel, procname=procname,libname=libname,range_begin=pc, range_end=pc)

    # MEM HOOKS
    def _hook_mem(self, start_address, end_address, before, after, read, write, virtual, physical, enabled):
        def decorator(fun):
            mem_hook_cb_type = self.ffi.callback("mem_hook_func_t")
            # Inform the plugin that it has a new breakpoint at addr
            
            def _run_and_catch(*args, **kwargs): # Run function but if it raises an exception, stop panda and raise it
                if not hasattr(self, "exit_exception"):
                    try:
                        r = fun(*args, **kwargs)
                        return r
                    except Exception as e:
                        # exceptions wont work in our thread. Therefore we print it here and then throw it after the
                        # machine exits.
                        if self.catch_exceptions:
                            self.exit_exception = e
                            self.end_analysis()
                        else:
                            raise e
                        return None

            hook_cb_passed = mem_hook_cb_type(_run_and_catch)
            mem_reg = self.ffi.new("struct memory_hooks_region*")
            mem_reg.start_address = start_address
            mem_reg.stop_address = end_address
            mem_reg.on_before = before
            mem_reg.on_after = after
            mem_reg.on_read = read
            mem_reg.on_write = write
            mem_reg.on_virtual = virtual
            mem_reg.on_physical = physical
            mem_reg.enabled = enabled
            mem_reg.cb = hook_cb_passed

            hook = self.plugins['mem_hooks'].add_mem_hook(mem_reg)

            self.mem_hooks[hook] = [mem_reg, hook_cb_passed]


            def wrapper(*args, **kw):
                _run_and_catch(args,kw)


            return wrapper
        return decorator

    def hook_mem(self, start_address, end_address, on_before, on_after, on_read, on_write, on_virtual, on_physical, enabled):
        '''
        Decorator to hook a memory range with the mem_hooks plugin

        .. todo:: Fully document mem-hook decorators
        '''
        return self._hook_mem(start_address,end_address,on_before,on_after,on_read, on_write, on_virtual, on_physical, enabled)

    def hook_phys_mem_read(self, start_address, end_address, on_before=True, on_after=False, enabled=True):
        '''
        Decorator to hook physical memory reads with the mem_hooks plugin
        '''
        return self._hook_mem(start_address,end_address,on_before,on_after, True, False, False, True, True)

    def hook_phys_mem_write(self, start_address, end_address, on_before=True, on_after=False):
        '''
        Decorator to hook physical memory writes with the mem_hooks plugin
        '''
        return self._hook_mem(start_address,end_address,on_before,on_after, False, True, False, True, True)

    def hook_virt_mem_read(self, start_address, end_address, on_before=True, on_after=False):
        '''
        Decorator to hook virtual memory reads with the mem_hooks plugin
        '''
        return self._hook_mem(start_address,end_address,on_before,on_after, True, False, True, False, True)

    def hook_virt_mem_write(self, start_address, end_address, on_before=True, on_after=False):
        '''
        Decorator to hook virtual memory writes with the mem_hooks plugin
        '''
        return self._hook_mem(start_address,end_address,on_before,on_after, False, True, True, False, True)
    
    # HYPERCALLS
    def hypercall(self, magic):
        def decorator(fun):
            hypercall_cb_type = self.ffi.callback("hypercall_t")
            
            def _run_and_catch(*args, **kwargs): # Run function but if it raises an exception, stop panda and raise it
                if not hasattr(self, "exit_exception"):
                    try:
                        fun(*args, **kwargs)
                    except Exception as e:
                        # exceptions wont work in our thread. Therefore we print it here and then throw it after the
                        # machine exits.
                        if self.catch_exceptions:
                            self.exit_exception = e
                            self.end_analysis()
                        else:
                            raise e

            hook_cb_passed = hypercall_cb_type(_run_and_catch)
            if type(magic) is int:
                self.plugins['hypercaller'].register_hypercall(magic, hook_cb_passed)
            elif type(magic) is list:
                for m in magic:
                    if type(m) is int:
                        self.plugins['hypercaller'].register_hypercall(m, hook_cb_passed)
                    else:
                        raise TypeError("Magic list must consist of integers")
            else:
                raise TypeError("Magics must be either an int or list of ints")

            def wrapper(*args, **kw):
                _run_and_catch(args,kw)
            self.hypercalls[wrapper] = [hook_cb_passed,magic]
            return wrapper
        return decorator
    
    def disable_hypercall(self, fn):
        if fn in self.hypercalls:
            magic = self.hypercalls[fn][1]
            if type(magic) is int:
                self.plugins['hypercaller'].unregister_hypercall(magic)
            elif type(magic) is list:
                for m in magic:
                    self.plugins['hypercaller'].unregister_hypercall(m)
        else:
            breakpoint()
            print("ERROR: Your hypercall was not in the hook list")

# vim: expandtab:tabstop=4:
