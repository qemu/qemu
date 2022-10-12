#!/usr/bin/env python3

import shutil
from pathlib import Path

class ProcWriteCapture():

    '''
    Set console_capture = True to capture all console output to file, including boot messages.
    Set proc_name = "name_of_proc" to, for a named process, capture stdout/stderr and any file writes from the hypervisor, mirror results to log directory.
    Can be stacked with console capture.
    '''

    def __init__(self, panda, console_capture = False, proc_name = None, log_dir = None, rm_existing_logs = False):

        self._panda = panda
        self._files_written = set()
        self._rm = rm_existing_logs
        self._console_capture = console_capture
        self._proc_name = proc_name
        self._proc_printed_err = False
        self._console_printed_err = False

        if log_dir == None:
            self._console_log_dir = Path.cwd()
            if proc_name:
                self._proc_log_dir = Path.cwd() / self._proc_name
        else:
            self._console_log_dir = Path(log_dir)
            if proc_name:
                self._proc_log_dir = Path(log_dir).joinpath(self._proc_name)

        # Setup logging dir
        self._console_log_dir.mkdir(parents=True, exist_ok=True)
        if proc_name:
            self._proc_log_dir.mkdir(parents=True, exist_ok=True)
        if self._rm:
            if proc_name:
                shutil.rmtree(self._proc_log_dir)
            shutil.rmtree(self._console_log_dir)

        # Mirror writes
        @self._panda.ppp("syscalls2", "on_sys_write_enter")
        def proc_write_capture_on_sys_write_enter(cpu, pc, fd, buf, cnt):

            try_read = False

            # Capture console output
            if self._console_capture:

                # Fun trick: lazy eval of OSI
                # Based on the idea that a non-POSIX FD will only be used after boot is finished an OSI is functional
                # Note: doesn't capture boot logs (would require hooking kernel's printk, not write syscall)
                if (fd == 1) or (fd == 2) or (fd == 3):
                    try_read = True
                else:
                    curr_proc = panda.plugins['osi'].get_current_process(cpu)
                    file_name_ptr = panda.plugins['osi_linux'].osi_linux_fd_to_filename(cpu, curr_proc, fd)
                    file_path = panda.ffi.string(file_name_ptr).decode()
                    if ("tty" in file_path):
                        try_read = True

                if try_read:

                    try:
                        data = panda.virtual_memory_read(cpu, buf, cnt)
                    except ValueError:
                        raise RuntimeError(f"Failed to read buffer: addr 0x{buf:016x}")

                    if fd == 2:
                        self._console_printed_err = True

                    log_file = self._console_log_dir.joinpath("console.out")
                    with open(log_file, "ab") as f:
                        f.write(data)

                    self._files_written.add(str(log_file))

            # Use OSI to capture logs for a named process
            if self._proc_name:

                curr_proc = panda.plugins['osi'].get_current_process(cpu)
                curr_proc_name = panda.ffi.string(curr_proc.name).decode()

                if self._proc_name == curr_proc_name:

                    if not try_read: # If we didn't already read this data in once for console capture
                        try:
                            data = panda.virtual_memory_read(cpu, buf, cnt)
                        except ValueError:
                            raise RuntimeError(f"Failed to read buffer: proc \'{curr_proc_name}\', addr 0x{buf:016x}")

                    file_name_ptr = panda.plugins['osi_linux'].osi_linux_fd_to_filename(cpu, curr_proc, fd)
                    file_path = panda.ffi.string(file_name_ptr).decode()

                    # For informational purposes only, collection not reliant on this exact mapping
                    if fd == 1: # POSIX stdout
                        file_path += ".stdout"
                    elif fd == 2: # POSIX stderr
                        file_path += ".stderr"
                        self._proc_printed_err = True

                    log_file = self._proc_log_dir.joinpath(file_path.replace("//", "_").replace("/", "_"))
                    with open(log_file, "ab") as f:
                        f.write(data)

                    self._files_written.add(str(log_file))

    def proc_printed_err(self):
        return self._proc_printed_err

    def console_printed_post_boot_err(self):
        return self._console_printed_err

    def get_files_written(self):
        return self._files_written

if __name__ == "__main__":
    import os
    from pandare import Panda
    panda = Panda(generic="x86_64")
    test = ProcWriteCapture(panda, console_capture = True)

    @panda.queue_blocking
    def driver():
        panda.revert_sync('root')
        data = panda.run_serial_cmd("whoami")
        panda.end_analysis()

    panda.run()

    outfile = "console.out"
    assert(os.path.isfile(outfile)), "Missing file"
    with open(outfile) as f:
        data = f.readlines()
    os.remove(outfile)

    assert 'whoami\n' in data, "Incorrect output"
    print("Success")
