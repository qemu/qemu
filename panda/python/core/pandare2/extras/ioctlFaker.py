#!/usr/bin/env python3

import sys
import logging

from cffi import FFI
ffi = FFI()

# TODO: only for logger, should probably move it to a separate file
if __name__ == '__main__': # Script run directly
    from pandare.extras import FileHook
else:
    from .fileHook import FileHook

# TODO: Ability to fake buffers for specific commands

ioctl_initialized = False
def do_ioctl_init(panda):

    '''
    One-time init for arch-specific bit-packed ioctl cmd struct.
    '''

    # Default config (x86, x86-64, ARM, AArch 64) with options for PPC
    global ioctl_initialized
    if ioctl_initialized:
        return

    ioctl_initialized = True
    TYPE_BITS = 8
    CMD_BITS = 8
    SIZE_BITS = 14 if panda.arch_name != "ppc" else 13
    DIR_BITS = 2 if panda.arch_name != "ppc" else 3

    ffi.cdef("""
    struct IoctlCmdBits {
        uint8_t type_num:%d;
        uint8_t cmd_num:%d;
        uint16_t arg_size:%d;
        uint8_t direction:%d;
    };

    union IoctlCmdUnion {
        struct IoctlCmdBits bits;
        uint32_t asUnsigned32;
    };

    enum ioctl_direction {
        IO = 0,
        IOW = 1,
        IOR = 2,
        IOWR = 3
    };
    """ % (TYPE_BITS, CMD_BITS, SIZE_BITS, DIR_BITS), packed=True)

class Ioctl():

    '''
    Unpacked ioctl command with optional buffer.
    '''

    def __init__(self, panda, cpu, fd, cmd, guest_ptr, use_osi_linux = False):

        '''
        Do unpacking, optionally using OSI for process and file name info.
        '''

        do_ioctl_init(panda)
        self.cmd = ffi.new("union IoctlCmdUnion*")
        self.cmd.asUnsigned32 = cmd
        self.original_ret_code = None
        self.osi = use_osi_linux

        # Optional syscall argument: pointer to buffer
        if (self.cmd.bits.arg_size > 0):
            try:
                self.has_buf = True
                self.guest_ptr = guest_ptr
                self.guest_buf = panda.virtual_memory_read(cpu, self.guest_ptr, self.cmd.bits.arg_size)
            except ValueError:
                self.guest_buf = None
                #raise RuntimeError("Failed to read guest buffer: ioctl({})".format(str(self.cmd)))
        else:
            self.has_buf = False
            self.guest_ptr = None
            self.guest_buf = None

        # Optional OSI usage: process and file name
        if self.osi:
            proc = panda.plugins['osi'].get_current_process(cpu)
            proc_name_ptr = proc.name
            file_name_ptr = panda.plugins['osi_linux'].osi_linux_fd_to_filename(cpu, proc, panda.ffi.cast("int", fd))
            self.proc_name = panda.ffi.string(proc_name_ptr).decode(errors="ignore") if proc_name_ptr != panda.ffi.NULL else "unknown"
            self.file_name = panda.ffi.string(file_name_ptr).decode(errors="ignore") if file_name_ptr != panda.ffi.NULL else "unknown"
        else:
            self.proc_name = None
            self.file_name = None

    def get_ret_code(self, panda, cpu):

        ''''
        Helper retrive original return code, handles arch-specifc ABI
        '''

        if panda.arch_name == "mipsel" or panda.arch_name == "mips":
            # Note: return values are in $v0, $v1 (regs 2 and 3 respectively), but here we only use first
            self.original_ret_code = panda.from_unsigned_guest(cpu.env_ptr.active_tc.gpr[2])
        elif panda.arch_name == "aarch64":
            self.original_ret_code = panda.from_unsigned_guest(cpu.env_ptr.xregs[0])
        elif panda.arch_name == "ppc":
            raise RuntimeError("PPC currently unsupported!")
        else: # x86/x64/ARM
            self.original_ret_code = panda.from_unsigned_guest(cpu.env_ptr.regs[0])

    def __str__(self):

        if self.osi:
            self_str = "\'{}\' using \'{}\' - ".format(self.proc_name, self.file_name)
        else:
            self_str = ""

        bits = self.cmd.bits
        direction = ffi.string(ffi.cast("enum ioctl_direction", bits.direction))
        ioctl_desc = f"dir={direction},arg_size={bits.arg_size:x},cmd=0x{bits.cmd_num:x},type=0x{bits.type_num:x}"
        if (self.guest_ptr == None):
            self_str += f"ioctl({ioctl_desc}) -> {self.original_ret_code}"
        else:
            self_str += f"ioctl({ioctl_desc},ptr={self.guest_ptr:08x},buf={self.guest_buf}) -> {self.original_ret_code}"
        return self_str

    def __eq__(self, other):

        return (
            self.__class__ == other.__class__ and
            self.cmd.asUnsigned32 == other.cmd.asUnsigned32 and
            self.has_buf == other.has_buf and
            self.guest_ptr == other.guest_ptr and
            self.guest_buf == other.guest_buf and
            self.proc_name == self.proc_name and
            self.file_name == self.file_name
        )

    def __hash__(self):

        return hash((self.cmd.asUnsigned32, self.has_buf, self.guest_ptr, self.guest_buf, self.proc_name, self.file_name))

class IoctlFaker():

    '''
    Interpose ioctl() syscall returns, forcing successes for specific error codes to simulate missing drivers/peripherals.
    Bin all returns into failures (needed forcing) and successes, store for later retrival/analysis.
    '''

    def __init__(
            self,
            panda,
            use_osi_linux = False,
            log = False,
            ignore = [],
            intercept_ret_vals = [-25],
            intercept_all_non_zero = False
        ):

        '''
        Log enables/disables logging.
        ignore contains a list of tuples (filename, cmd#) to be ignored.
        intercept_ret_vals is a list of ioctl return values that should be intercepted. By default
          we just intercept just -25 which indicates that a driver is not present to handle the ioctl.
        intercept_all_non_zero is aggressive setting that takes precedence if set - any non-zero return code id changed to zero.
        '''

        self.osi = use_osi_linux
        self._panda = panda
        self._panda.load_plugin("syscalls2")
        self._log = log
        self.ignore = ignore
        self.intercept_ret_vals = intercept_ret_vals
        self.intercept_all_non_zero = intercept_all_non_zero

        if self.osi:
            self._panda.load_plugin("osi")
            self._panda.load_plugin("osi_linux")

        if self._log:
            self._logger = logging.getLogger('panda.ioctls')
            self._logger.setLevel(logging.DEBUG)

        # Track ioctls in two sets: modified (forced_returns) and unmodified
        self._forced_returns = set()
        self._unmodified_returns = set()

        # Force success returns for missing drivers/peripherals
        @self._panda.ppp("syscalls2", "on_sys_ioctl_return")
        def ioctl_faker_on_sys_ioctl_return(cpu, pc, fd, cmd, arg):

            ioctl = Ioctl(self._panda, cpu, fd, cmd, arg, self.osi)
            ioctl.get_ret_code(self._panda, cpu)

            # Modify
            if (self.intercept_all_non_zero and ioctl.original_ret_code != 0) or \
                ioctl.original_ret_code in self.intercept_ret_vals and \
                        (ioctl.file_name, ioctl.cmd.bits.cmd_num) not in self.ignore: # Allow ignoring specific commands on specific files

                if panda.arch_name == "mipsel" or panda.arch_name == "mips":
                    cpu.env_ptr.active_tc.gpr[2] = 0
                elif panda.arch_name == "aarch64":
                    cpu.env_ptr.xregs[0] = 0
                elif panda.arch_name == "ppc":
                    raise RuntimeError("PPC currently unsupported!")
                else: # x86/x64/ARM
                    cpu.env_ptr.regs[0] = 0

                self._forced_returns.add(ioctl)

                if ioctl.has_buf and self._log:
                    self._logger.warning("Forcing success return for data-containing {}".format(ioctl))
                elif self._log:
                    self._logger.info("Forcing success return for data-less {}".format(ioctl))

            # Don't modify
            else:
                self._unmodified_returns.add(ioctl)

    def _get_returns(self, source, with_buf_only):

        if with_buf_only:
            return list(filter(lambda i: (i.has_buf == True), source))
        else:
            return source

    def get_forced_returns(self, with_buf_only = False):

        '''
        Retrieve ioctls whose error codes where overwritten
        '''

        return self._get_returns(self._forced_returns, with_buf_only)

    def get_unmodified_returns(self, with_buf_only = False):

        '''
        Retrieve ioctl that completed normally
        '''

        return self._get_returns(self._unmodified_returns, with_buf_only)

if __name__ == "__main__":

    '''
    Bash will issue ioctls on /dev/ttys0 - this is just a simple test to make sure they're being captured
    '''

    from pandare import Panda

    # No arguments, x86_64. Otherwise argument should be guest arch
    generic_type = sys.argv[1] if len(sys.argv) > 1 else "x86_64"
    panda = Panda(generic=generic_type)

    @panda.queue_blocking
    def run_cmd():

        # Setup faker
        ioctl_faker = IoctlFaker(panda, use_osi_linux=True)

        # First revert to root snapshot, then issue an IOCTL directly through perl - which is junk
        # so the faker should fake it
        panda.revert_sync("root")
        panda.run_serial_cmd("""perl -e 'require "sys/ioctl.ph"; ioctl(1, 0, 1);'""")

        # Check faker's results
        faked_rets = ioctl_faker.get_forced_returns()
        normal_rets = ioctl_faker.get_unmodified_returns()
        assert(len(faked_rets)), "No returns faked"
        assert(len(normal_rets)), "No normal returns"
        panda.end_analysis()

    panda.run()
    print("Success")
