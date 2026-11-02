"""Run a bare-metal R5900 test and check its result

The guest runs until test_done(); a non-zero status means the test
failed: test_failed_line holds the line of a failed assert(), and
test_failed_message the last message the test tried to print.

SPDX-License-Identifier: GPL-2.0-or-later
"""

import gdb
from test_gdbstub import main, report


def run_test():
    gdb.execute("break test_done")
    gdb.execute("continue")

    status = int(gdb.parse_and_eval("status"))
    details = ""
    if status:
        line = int(gdb.parse_and_eval("test_failed_line"))
        message = gdb.parse_and_eval("test_failed_message")
        if line:
            details = f" (assert failed at line {line})"
        elif int(message):
            details = f" ({message.string().strip()})"
    report(status == 0, "guest test" + details)


main(run_test)
