# SPDX-License-Identifier: GPL-2.0-or-later
#
# A simple interface module built around pygdbmi for handling GDB commands.
#
# Copyright (c) 2025 Linaro Limited
#
# Author:
#  Gustavo Romero <gustavo.romero@linaro.org>
#

import re


class GDB:
    """Provides methods to run and capture GDB command output."""


    def __init__(self, gdb_path, echo=True, suffix='# ', prompt="$ "):
        from pygdbmi.gdbcontroller import GdbController
        from pygdbmi.constants import GdbTimeoutError
        type(self).TimeoutError = GdbTimeoutError

        gdb_cmd = [gdb_path, "-q", "--interpreter=mi2"]
        self.gdbmi = GdbController(gdb_cmd)
        self.echo = echo
        self.suffix = suffix
        self.prompt = prompt
        self.response = None
        self.cmd_output = None


    def get_payload(self, response, kind):
        output = []
        for o in response:
            # Unpack payloads of the same type.
            _type, _, payload, *_ = o.values()
            if _type == kind:
                output += [payload]

        # Some output lines do not end with \n but begin with it,
        # so remove the leading \n and merge them with the next line
        # that ends with \n.
        lines = [line.lstrip('\n') for line in output]
        lines = "".join(lines)
        lines = lines.splitlines(keepends=True)

        return lines


    def cli(self, cmd, timeout=32.0):
        self.response = self.gdbmi.write(cmd, timeout_sec=timeout)
        self.cmd_output = self.get_payload(self.response, kind="console")
        if self.echo:
            print(self.suffix + self.prompt + cmd)

            if len(self.cmd_output) > 0:
                cmd_output = self.suffix.join(self.cmd_output)
                print(self.suffix + cmd_output, end="")

        return self


    def get_addr(self):
        address_pattern = r"0x[0-9A-Fa-f]+"
        cmd_output = "".join(self.cmd_output) # Concat output lines.

        match = re.search(address_pattern, cmd_output)

        return int(match[0], 16) if match else None


    def get_log(self):
        r = self.get_payload(self.response, kind="log")
        r = "".join(r)

        return r


    def get_console(self):
        r = "".join(self.cmd_output)

        return r


    def exit(self):
        self.gdbmi.exit()
