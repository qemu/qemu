#!/usr/bin/env python3
#
# Functional tests for the IBM PPE42 processor
#
# Copyright (c) 2025, IBM Corporation
#
# SPDX-License-Identifier: GPL-2.0-or-later

import asyncio
from qemu_test import QemuSystemTest, Asset


class Ppe42Machine(QemuSystemTest):

    timeout = 90
    poll_period = 1.0

    ASSET_PPE42_TEST_IMAGE = Asset(
        ('https://github.com/milesg-github/ppe42-tests/raw/refs/heads/main/'
         'images/ppe42-test.out'),
        '03c1ac0fb7f6c025102a02776a93b35101dae7c14b75e4eab36a337e39042ea8')

    def _test_completed(self):
        self.log.info("Checking for test completion...")
        try:
            output = self.vm.cmd('human-monitor-command',
                                 command_line='info registers')
        except Exception as err:
            self.log.debug(f"'info registers' cmd failed due to {err=},"
                            " {type(err)=}")
            raise

        self.log.info(output)
        if "NIP fff80200" not in output:
            self.log.info("<test not completed>")
            return False

        self.log.info("<test completed>")
        return True

    def _wait_pass_fail(self, timeout):
        while not self._test_completed():
            if timeout >= self.poll_period:
                timeout = timeout - self.poll_period
                self.log.info(f"Waiting {self.poll_period} seconds for test"
                               " to complete...")
                e = None
                try:
                    e = self.vm.event_wait('STOP', self.poll_period)

                except asyncio.TimeoutError:
                    self.log.info("Poll period ended.")

                except Exception as err:
                    self.log.debug(f"event_wait() failed due to {err=},"
                                    " {type(err)=}")
                    raise

                if e is not None:
                    self.log.debug(f"Execution stopped: {e}")
                    self.log.debug("Exiting due to test failure")
                    self.fail("Failure detected!")
                    break
            else:
                self.fail("Timed out waiting for test completion.")

    def test_ppe42_instructions(self):
        self.set_machine('ppe42_machine')
        self.require_accelerator("tcg")
        image_path = self.ASSET_PPE42_TEST_IMAGE.fetch()
        self.vm.add_args('-nographic')
        self.vm.add_args('-device', f'loader,file={image_path}')
        self.vm.add_args('-device', 'loader,addr=0xfff80040,cpu-num=0')
        self.vm.add_args('-action', 'panic=pause')
        self.vm.launch()
        self._wait_pass_fail(self.timeout)

if __name__ == '__main__':
    QemuSystemTest.main()
