#
# GDB debugging support
#
# Copyright 2012 Red Hat, Inc. and/or its affiliates
#
# Authors:
#  Avi Kivity <avi@redhat.com>
#
# This work is licensed under the terms of the GNU GPL, version 2
# or later.  See the COPYING file in the top-level directory.

import atexit
import gdb
import os
import pty
import re
import struct
import textwrap

from collections import OrderedDict
from copy import deepcopy

VOID_PTR = gdb.lookup_type('void').pointer()

# Registers in the same order they're present in ELF coredump file.
# See asm/ptrace.h
PT_REGS = ['r15', 'r14', 'r13', 'r12', 'rbp', 'rbx', 'r11', 'r10', 'r9',
           'r8', 'rax', 'rcx', 'rdx', 'rsi', 'rdi', 'orig_rax', 'rip', 'cs',
           'eflags', 'rsp', 'ss']

coredump = None


class Coredump:
    _ptregs_suff = '.ptregs'

    def __init__(self, coredump, executable):
        atexit.register(self._cleanup)

        self.coredump = coredump
        self.executable = executable
        self._ptregs_blob = coredump + self._ptregs_suff
        self._dirty = False

        with open(coredump, 'rb') as f:
            while f.read(4) != b'CORE':
                pass
            gdb.write(f'core file {coredump}: found "CORE" at 0x{f.tell():x}\n')

            # Looking for struct elf_prstatus and pr_reg field in it (an array
            # of general purpose registers).  See sys/procfs.h.

            # lseek(f.fileno(), 4, SEEK_CUR): go to elf_prstatus
            f.seek(4, 1)

            # lseek(f.fileno(), 112, SEEK_CUR):
            # offsetof(struct elf_prstatus, pr_reg)
            f.seek(112, 1)

            self._ptregs_offset = f.tell()

            # If binary blob with the name /path/to/coredump + '.ptregs'
            # exists, that means proper cleanup didn't happen during previous
            # GDB session with the same coredump, and registers in the dump
            # itself might've remained patched.  Thus we restore original
            # registers values from this blob
            if os.path.exists(self._ptregs_blob):
                with open(self._ptregs_blob, 'rb') as b:
                    orig_ptregs_bytes = b.read()
                self._dirty = True
            else:
                orig_ptregs_bytes = f.read(len(PT_REGS) * 8)

            values = struct.unpack(f"={len(PT_REGS)}q", orig_ptregs_bytes)
            self._orig_ptregs = OrderedDict(zip(PT_REGS, values))

            if not os.path.exists(self._ptregs_blob):
                gdb.write(f'saving original pt_regs in {self._ptregs_blob}\n')
                with open(self._ptregs_blob, 'wb') as b:
                    b.write(orig_ptregs_bytes)

        gdb.write('\n')

    def patch_regs(self, regs):
        # Set dirty flag early on to make sure regs are restored upon cleanup
        self._dirty = True

        gdb.write(f'patching core file {self.coredump}\n')
        patched_ptregs = deepcopy(self._orig_ptregs)
        int_regs = {k: int(v) for k, v in regs.items()}
        patched_ptregs.update(int_regs)

        with open(self.coredump, 'ab') as f:
            gdb.write(f'assume pt_regs at 0x{self._ptregs_offset:x}\n')
            f.seek(self._ptregs_offset, 0)
            gdb.write('writing regs:\n')
            for reg in self._orig_ptregs.keys():
                if reg in int_regs:
                    gdb.write(f"  {reg}: {int_regs[reg]:#16x}\n")
            f.write(struct.pack(f"={len(PT_REGS)}q", *patched_ptregs.values()))

        gdb.write('\n')

    def restore_regs(self):
        if not self._dirty:
            return

        gdb.write(f'\nrestoring original regs in core file {self.coredump}\n')
        with open(self.coredump, 'ab') as f:
            gdb.write(f'assume pt_regs at 0x{self._ptregs_offset:x}\n')
            f.seek(self._ptregs_offset, 0)
            f.write(struct.pack(f"={len(PT_REGS)}q",
                                *self._orig_ptregs.values()))

        self._dirty = False
        gdb.write('\n')

    def _cleanup(self):
        if os.path.exists(self._ptregs_blob):
            self.restore_regs()
            gdb.write(f'\nremoving saved pt_regs file {self._ptregs_blob}\n')
            os.unlink(self._ptregs_blob)


def pthread_self():
    '''Fetch the base address of TLS.'''
    return gdb.parse_and_eval("$fs_base")

def get_glibc_pointer_guard():
    '''Fetch glibc pointer guard value'''
    fs_base = pthread_self()
    return gdb.parse_and_eval('*(uint64_t*)((uint64_t)%s + 0x30)' % fs_base)

def glibc_ptr_demangle(val, pointer_guard):
    '''Undo effect of glibc's PTR_MANGLE()'''
    return gdb.parse_and_eval('(((uint64_t)%s >> 0x11) | ((uint64_t)%s << (64 - 0x11))) ^ (uint64_t)%s' % (val, val, pointer_guard))

def get_jmpbuf_regs(jmpbuf):
    JB_RBX  = 0
    JB_RBP  = 1
    JB_R12  = 2
    JB_R13  = 3
    JB_R14  = 4
    JB_R15  = 5
    JB_RSP  = 6
    JB_PC   = 7

    pointer_guard = get_glibc_pointer_guard()
    return {'rbx': jmpbuf[JB_RBX],
        'rbp': glibc_ptr_demangle(jmpbuf[JB_RBP], pointer_guard),
        'rsp': glibc_ptr_demangle(jmpbuf[JB_RSP], pointer_guard),
        'r12': jmpbuf[JB_R12],
        'r13': jmpbuf[JB_R13],
        'r14': jmpbuf[JB_R14],
        'r15': jmpbuf[JB_R15],
        'rip': glibc_ptr_demangle(jmpbuf[JB_PC], pointer_guard) }

def symbol_lookup(addr):
    # Example: "__clone3 + 44 in section .text of /lib64/libc.so.6"
    result = gdb.execute(f"info symbol {hex(addr)}", to_string=True).strip()
    try:
        if "+" in result:
            (func, result) = result.split(" + ")
            (offset, result) = result.split(" in ")
        else:
            offset = "0"
            (func, result) = result.split(" in ")
        func_str = f"{func}<+{offset}> ()"
    except:
        return f"??? ({result})"

    # Example: Line 321 of "../util/coroutine-ucontext.c" starts at address
    # 0x55cf3894d993 <qemu_coroutine_switch+99> and ends at 0x55cf3894d9ab
    # <qemu_coroutine_switch+123>.
    result = gdb.execute(f"info line *{hex(addr)}", to_string=True).strip()
    if not result.startswith("Line "):
        return func_str
    result = result[5:]

    try:
        result = result.split(" starts ")[0]
        (line, path) = result.split(" of ")
        path = path.replace("\"", "")
    except:
        return func_str

    return f"{func_str} at {path}:{line}"

def run_with_pty(cmd):
    # Create a PTY pair
    master_fd, slave_fd = pty.openpty()

    pid = os.fork()
    if pid == 0:  # Child
        os.close(master_fd)
        # Attach stdin/stdout/stderr to the PTY slave side
        os.dup2(slave_fd, 0)
        os.dup2(slave_fd, 1)
        os.dup2(slave_fd, 2)
        os.close(slave_fd)
        os.execvp("gdb", cmd) # Runs gdb and doesn't return

    # Parent
    os.close(slave_fd)

    output = bytearray()
    try:
        while True:
            data = os.read(master_fd, 65536)
            if not data:
                break
            output.extend(data)
    except OSError: # in case subprocess exits and we get EBADF on read()
        pass
    finally:
        try:
            os.close(master_fd)
        except OSError: # in case we get EBADF on close()
            pass

    # Wait for child to finish (reap zombie)
    os.waitpid(pid, 0)

    return output.decode('utf-8')

def dump_backtrace_patched(regs):
    cmd = ['gdb', '-batch',
           '-ex', 'set debuginfod enabled off',
           '-ex', 'set complaints 0',
           '-ex', 'set style enabled on',
           '-ex', 'python print("----split----")',
           '-ex', 'bt', coredump.executable, coredump.coredump]

    coredump.patch_regs(regs)
    out = run_with_pty(cmd).split('----split----')[1]
    gdb.write(out)

def dump_backtrace(regs):
    '''
    Backtrace dump with raw registers, mimic GDB command 'bt'.
    '''
    # Here only rbp and rip that matter..
    rbp = regs['rbp']
    rip = regs['rip']
    i = 0

    while rbp:
        # For all return addresses on stack, we want to look up symbol/line
        # on the CALL command, because the return address is the next
        # instruction instead of the CALL.  Here -1 would work for any
        # sized CALL instruction.
        print(f"#{i}  {hex(rip)} in {symbol_lookup(rip if i == 0 else rip-1)}")
        rip = gdb.parse_and_eval(f"*(uint64_t *)(uint64_t)({hex(rbp)} + 8)")
        rbp = gdb.parse_and_eval(f"*(uint64_t *)(uint64_t)({hex(rbp)})")
        i += 1

def dump_backtrace_live(regs):
    '''
    Backtrace dump with gdb's 'bt' command, only usable in a live session.
    '''
    old = dict()

    # remember current stack frame and select the topmost
    # so that register modifications don't wreck it
    selected_frame = gdb.selected_frame()
    gdb.newest_frame().select()

    for i in regs:
        old[i] = gdb.parse_and_eval('(uint64_t)$%s' % i)

    for i in regs:
        gdb.execute('set $%s = %s' % (i, regs[i]))

    gdb.execute('bt')

    for i in regs:
        gdb.execute('set $%s = %s' % (i, old[i]))

    selected_frame.select()

def bt_jmpbuf(jmpbuf, detailed=False):
    '''Backtrace a jmpbuf'''
    regs = get_jmpbuf_regs(jmpbuf)
    try:
        # This reuses gdb's "bt" command, which can be slightly prettier
        # but only works with live sessions.
        dump_backtrace_live(regs)
    except:
        if detailed:
            # Obtain detailed trace by patching regs in copied coredump
            dump_backtrace_patched(regs)
        else:
            # If above doesn't work, fallback to poor man's unwind
            dump_backtrace(regs)

def co_cast(co):
    return co.cast(gdb.lookup_type('CoroutineUContext').pointer())

def coroutine_to_jmpbuf(co):
    coroutine_pointer = co_cast(co)
    return coroutine_pointer['env']['__jmpbuf']

def init_coredump():
    global coredump

    files = gdb.execute('info files', False, True).split('\n')

    if not 'core dump' in files[1]:
        return False

    core_path = re.search("`(.*)'", files[2]).group(1)
    exec_path = re.match('^Symbols from "(.*)".$', files[0]).group(1)

    if coredump is None:
        coredump = Coredump(core_path, exec_path)

    return True

class CoroutineCommand(gdb.Command):
    __doc__ = textwrap.dedent("""\
        Display coroutine backtrace

        Usage: qemu coroutine COROPTR [--detailed]
        Show backtrace for a coroutine specified by COROPTR

          --detailed       obtain detailed trace by copying coredump, patching
                           regs in it, and runing gdb subprocess to get
                           backtrace from the patched coredump
        """)

    def __init__(self):
        gdb.Command.__init__(self, 'qemu coroutine', gdb.COMMAND_DATA,
                             gdb.COMPLETE_NONE)

    def _usage(self):
        gdb.write('usage: qemu coroutine <coroutine-pointer> [--detailed]\n')
        return

    def invoke(self, arg, from_tty):
        argv = gdb.string_to_argv(arg)
        argc = len(argv)
        if argc == 0 or argc > 2 or (argc == 2 and argv[1] != '--detailed'):
            return self._usage()
        detailed = True if argc == 2 else False

        is_coredump = init_coredump()
        if detailed and not is_coredump:
            gdb.write('--detailed is only valid when debugging core dumps\n')
            return

        try:
            bt_jmpbuf(coroutine_to_jmpbuf(gdb.parse_and_eval(argv[0])),
                      detailed=detailed)
        finally:
            coredump.restore_regs()

class CoroutineBt(gdb.Command):
    __doc__ = textwrap.dedent("""\
        Display backtrace including coroutine switches

        Usage: qemu bt [--detailed]

          --detailed       obtain detailed trace by copying coredump, patching
                           regs in it, and runing gdb subprocess to get
                           backtrace from the patched coredump
        """)

    def __init__(self):
        gdb.Command.__init__(self, 'qemu bt', gdb.COMMAND_STACK,
                             gdb.COMPLETE_NONE)

    def _usage(self):
        gdb.write('usage: qemu bt [--detailed]\n')
        return

    def invoke(self, arg, from_tty):
        argv = gdb.string_to_argv(arg)
        argc = len(argv)
        if argc > 1 or (argc == 1 and argv[0] != '--detailed'):
            return self._usage()
        detailed = True if argc == 1 else False

        is_coredump = init_coredump()
        if detailed and not is_coredump:
            gdb.write('--detailed is only valid when debugging core dumps\n')
            return

        gdb.execute("bt")

        try:
            # This only works with a live session
            co_ptr = gdb.parse_and_eval("qemu_coroutine_self()")
        except:
            # Fallback to use hard-coded ucontext vars if it's coredump
            co_ptr = gdb.parse_and_eval("co_tls_current")

        if co_ptr == False:
            return

        try:
            while True:
                co = co_cast(co_ptr)
                co_ptr = co["base"]["caller"]
                if co_ptr == 0:
                    break
                gdb.write("\nCoroutine at " + str(co_ptr) + ":\n")
                bt_jmpbuf(coroutine_to_jmpbuf(co_ptr), detailed=detailed)
        finally:
            coredump.restore_regs()

class CoroutineSPFunction(gdb.Function):
    def __init__(self):
        gdb.Function.__init__(self, 'qemu_coroutine_sp')

    def invoke(self, addr):
        return get_jmpbuf_regs(coroutine_to_jmpbuf(addr))['rsp'].cast(VOID_PTR)

class CoroutinePCFunction(gdb.Function):
    def __init__(self):
        gdb.Function.__init__(self, 'qemu_coroutine_pc')

    def invoke(self, addr):
        return get_jmpbuf_regs(coroutine_to_jmpbuf(addr))['rip'].cast(VOID_PTR)
