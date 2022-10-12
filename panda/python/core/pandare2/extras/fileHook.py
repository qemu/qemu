#!/usr/bin/env python3

import logging
import sys
from os import path


class FileHook:
    '''
    Class to modify guest memory just before syscalls with filename arguments.
    As the system call is about to be executed, change the data pointed to by the
    filename pointer. When the syscall returns, restore the mutated data to its
    original values.

    This provides a simple, cross-platform interface to redirect file accesses
    just using the OSI plugin.

    usage:
        panda = Panda(...)
        hook = FileHook(panda)
        hook.rename_file("/rename_this", "/to_this")
    '''

    def __init__(self, panda, use_osi=True):
        '''
        Store a reference to the panda object, and register
        the appropriate syscalls2 callbacks for entering and exiting
        from all syscalls that have a char* filename argument.
        '''

        self._panda = panda
        self._renamed_files = {} # old_fname (str): new_fname (bytes)
        self._changed_strs = {} # callback_name: original_data
        self.use_osi = use_osi

        self.logger = logging.getLogger('panda.filehook')
        try:
            import coloredlogs
            coloredlogs.install(level='WARN')
        except ImportError:
            pass
        self.pending_virt_read = None

        panda.load_plugin("syscalls2")

        # For each architecture, we have a different set of syscalls. They all
        # either call our functions with (cpu, pc, filename_ptr, ...)
        # or (cpu, pc, something_else, filename_ptr, ...). Here we
        # Programmatically generate callbacks for all of them

        # These lists were made with commands like the following in syscalls2/generated:
        # grep filename syscall_switch_enter_linux_x86.cpp | grep "\['const char " | grep -o sys_[a-zA-Z0-9]* | grep -o [a-z0-9]*$
        # grep filename syscall_switch_enter_linux_x86.cpp | grep -v "\['const char " | grep -o sys_[a-zA-Z0-9]* | grep -o [a-z0-9]*$
        to_hook = {}
        if panda.arch_name == "i386":
            to_hook[0] = ["open", "execve", "chdir", "mknod", "chmod", "lchown16", "stat", "access", "chroot",
                         "lstat", "newstat", "newlstat", "chown16", "stat64", "lstat64", "lchown", "chown" ]
            to_hook[1] = ["utime", "utimes", "openat", "mknodat", "fchownat", "futimesat", "fstatat64",
                          "fchmodat", "faccessat", "utimensat", "execveat"]

        elif panda.arch_name == "x86_64":
            to_hook[0] = ["open", "newstat", "newlstat", "access", "chdir", "chmod", "chown", "lchown", "mknod", "chroot"]
            to_hook[1] = ["utime", "utimes", "openat", "mknodat", "fchownat", "futimesat", "newfstatat", "fchmodat", "faccessat", "utimensat"]

        elif panda.arch_name == "arm":
            to_hook[0] = ["open", "execve", "chdir", "mknod", "chmod", "lchown16", "access", "chroot", "newstat", "newlstat", "chown16", "stat64", "lstat64", "lchown", "chown"]
            to_hook[1] = ["utime", "utimes", "openat", "mknodat", "fchownat", "futimesat", "fstatat64", "fchmodat", "faccessat", "utimensat", "execveat"]
        else:
            raise ValueError(f"Unsupported PANDA arch: {panda.arch_name}")

        # Register the callbacks
        for arg_offset, names in to_hook.items():
            for name in names:
                self._gen_cb(name, arg_offset)


        # Fallback callback used when syscall with file name isn't mapped into memory
        @self._panda.cb_virt_mem_before_read(enabled=False)
        def before_virt_read(cpu, pc, addr, size):
            '''
            This callback is necessary for the case when we enter a syscall but the filename pointer is paged out.
            When that happens, we enable this (slow) callback which checks every mem-read while we're in that syscall
            to see if the memory has since been paged-in. It should always eventually be paged in. Once it is,
            we mutate the memory and then disable this callback.

            If this hasn't run by the time the callback returns, we give up and disable it
            '''
            if not self.pending_virt_read:
                return

            # Is our pending read a subset of the current read? If so try to read it
            if addr <= self.pending_virt_read and addr+size > self.pending_virt_read:
                try:
                    fname = self._panda.read_str(cpu, self.pending_virt_read)
                except ValueError:
                    return # Still not available. Keep waiting
                self.logger.debug(f"recovered missed filename: {fname}")

                # It is available! Disable this slow callback and rerurn _enter_cb with the data
                fname_ptr = self.pending_virt_read
                self.pending_virt_read = None
                self._panda.disable_callback('before_virt_read')
                self._enter_cb(self.pending_syscall, args=(cpu, pc), fname_ptr=fname_ptr)


    def rename_file(self, old_name, new_name):
        '''
        Mutate a given filename into a new name at the syscall interface
        '''
        assert(old_name not in self._renamed_files), f"Already have a rename rule for {old_name}"

        if not isinstance(new_name, bytes):
            new_name = new_name.encode("utf8")

        if not new_name.endswith(b"\x00"):
            new_name += b"\x00"

        self._renamed_files[old_name] = new_name

    def _get_fname(self, cpu, fd):
        '''
        Use OSI to get the filename behind a file descriptor.
        If not self.use_osi, return None
        '''
        if not self.use_osi:
            return None
        fname_s = None
        proc = self._panda.plugins['osi'].get_current_process(cpu)
        if proc != self._panda.ffi.NULL:
            fname = self._panda.plugins['osi_linux'].osi_linux_fd_to_filename(cpu, proc, self._panda.ffi.cast("int", fd))
            if fname != self._panda.ffi.NULL:
                fname_s = self._panda.ffi.string(fname).decode('utf8', 'ignore')
        return fname_s

    def _gen_cb(self, name, fname_ptr_pos):
        '''
        Register syscalls2 PPP callback on enter and return for the given name
        which has an argument of char* filename at fname_ptr_pos in the arguments list
        '''
        self._panda.ppp("syscalls2", f"on_sys_{name}_enter", name = f"file_hook_enter_{name}")( \
                    lambda *args: self._enter_cb(name, fname_ptr_pos, args=args))
        self._panda.ppp("syscalls2", f"on_sys_{name}_return", name = f"file_hook_return_{name}")( \
                    lambda *args: self._return_cb(name, fname_ptr_pos, args=args))

    def _enter_cb(self, syscall_name, fname_ptr_pos=0, args=None, fname_ptr=None):
        '''
        When we return, check if we mutated the fname buffer. If so,
        we need to restore whatever data was there (we may have written
        past the end of the string).

        if fname_ptr is set, just skip the logic to extract it
        '''

        assert(args)
        (cpu, pc) = args[0:2]

        if not fname_ptr:
            fname_ptr = args[2+fname_ptr_pos] # offset to after (cpu, pc) in callback args

        try:
            fname = self._panda.read_str(cpu, fname_ptr)
        except:
            fname = self._get_fname(cpu, args[2+fname_ptr_pos])

            if fname:
                self.logger.info(f"OSI found fname after simple logic missed it in call to {syscall_name}")
            else:
                self.logger.debug(f"missed filename at 0x{fname_ptr:x} in call to {syscall_name} - trying to find")
                self.pending_virt_read = cpu.env_ptr.regs[0]
                self.pending_syscall = syscall_name
                self._panda.enable_callback('before_virt_read')
                #self._panda_enable_memcb()
                return

        fname = path.normpath(fname) # Normalize it
        #self.logger.info(f"Entering {syscall_name} with file={fname}")

        if fname in self._renamed_files:
            # It matches, now let's take our action! Either rename or callback

            self.logger.debug(f"modifying filename {fname} in {syscall_name} to {self._renamed_files[fname]}")
            assert(syscall_name not in self._changed_strs), "Entering syscall that already has a pending restore"

            # First read a buffer of the same size as our new value. XXX the string we already read might be shorter
            # than what we're inserting so we read again so we can later restore the old data
            try:
                clobbered_data = self._panda.virtual_memory_read(cpu, fname_ptr, len(self._renamed_files[fname]))
            except ValueError:
                self.logger.error(f"Failed to read target buffer at call into {syscall_name}")
                return

            # Now replace those bytes with our new name
            try:
                self._panda.virtual_memory_write(cpu, fname_ptr, self._renamed_files[fname])
            except ValueError:
                self.logger.warn(f"Failed to mutate filename buffer at call into {syscall_name}")
                return

            # If it all worked, save the clobbered data
            asid = self._panda.current_asid(cpu)
            self._changed_strs[(syscall_name, asid)] = clobbered_data

            self._before_modified_enter(cpu, pc, syscall_name, fname)


    def _return_cb(self, syscall_name, fname_ptr_pos, args=None):
        '''
        When we return, check if we mutated the fname buffer. If so,
        we need to restore whatever data was there (we may have written
        past the end of the string)
        '''
        (cpu, pc) = args[0:2]
        if self.pending_virt_read:
            fname_ptr = args[2+fname_ptr_pos] # offset to after (cpu, pc) in callback args

            self.logger.warning(f"missed filename in call to {syscall_name} with fname at 0x{fname_ptr:x}. Ignoring it")

            self._panda.disable_callback('before_virt_read') # No point in continuing this
            self.pending_virt_read = None # Virtual address that we're waiting to read as soon as possible
            return

        asid = self._panda.current_asid(cpu)
        if (syscall_name, asid) in self._changed_strs:
            assert(args)
            fname_ptr = args[2+fname_ptr_pos] # offset to after (cpu, pc) in callback args
            try:
                self._panda.virtual_memory_write(cpu, fname_ptr, self._changed_strs[(syscall_name, asid)])
            except ValueError:
                self.logger.warn(f"Failed to fix filename buffer at return of {syscall_name}")
            del self._changed_strs[(syscall_name, asid)]

            fd = self._panda.arch.get_retval(cpu, convention='syscall')
            self.logger.info(f"Returning from {syscall_name} after modifying argument - modified FD is {fd}")
            self._after_modified_return(cpu, pc, syscall_name, fd=fd)

    def _before_modified_enter(self, cpu, pc, syscall_name, fname):
        '''
        Internal callback run before we enter a syscall where we mutated
        the filename. Exists to be overloaded by subclasses
        '''
        pass

    def _after_modified_return(self, cpu, pc, syscall_name, fd):
        '''
        Internal callback run before we return from a syscall where we mutated
        the filename. Exists to be overloaded by subclasses
        '''
        pass

if __name__ == '__main__':
    from pandare import Panda

    panda = Panda(generic="x86_64")

    # Reads to /does_not_exist should be redirected to /etc/issue
    hook = FileHook(panda)
    hook.rename_file("/does_not_exist", "/etc/issue")

    @panda.queue_blocking
    def read_it():
        panda.revert_sync('root')
        data = panda.run_serial_cmd("cat /does_not_exist")
        assert("Ubuntu" in data), f"Hook failed"
        panda.end_analysis()

    panda.run()
    print("Success")
