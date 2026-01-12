#!/usr/bin/env python3
#
# SPDX-License-Identifier: GPL-2.0-or-later
#
'''Python linter tests'''

import os

from pathlib import Path
from qemu_test import QemuBaseTest, skipIfMissingImports


class LinterTest(QemuBaseTest):
    '''
    Run python linters on the test *.py files
    '''

    @skipIfMissingImports("pylint")
    def test_pylint(self):
        '''Check source files with pylint'''
        from pylint.lint import Run as pylint_run
        from pylint.reporters.collecting_reporter import CollectingReporter
        srcdir = os.path.join(Path(__file__).parent.parent, self.arch)
        rcfile = os.path.join(Path(__file__).parent.parent, "pylintrc")
        self.log.info('Checking files in %s with pylint', srcdir)
        reporter = CollectingReporter()
        pylint_run(["--rcfile", rcfile, srcdir], reporter=reporter, exit=False)
        if reporter.messages:
            fmt = '"{path}:{line}: {msg_id}: {msg} ({symbol})"'
            for msg in reporter.messages:
                if msg.category == "error":
                    self.log.error(msg.format(fmt))
                elif msg.category == "warning":
                    self.log.warning(msg.format(fmt))
                else:
                    self.log.info(msg.format(fmt))
            self.fail("Pylint failed, see base.log for details.")


if __name__ == '__main__':
    QemuBaseTest.main()
