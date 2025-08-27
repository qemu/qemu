#!/usr/bin/env python3
#
# Simple functional tests for VNC functionality
#
# Copyright 2018, 2024 Red Hat, Inc.
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later.  See the COPYING file in the top-level directory.

import fcntl
import os
import socket

from .config import BUILD_DIR
from typing import List


class Ports():

    PORTS_ADDR = '127.0.0.1'
    PORTS_RANGE_SIZE = 1024
    PORTS_START = 49152 + ((os.getpid() * PORTS_RANGE_SIZE) % 16384)
    PORTS_END = PORTS_START + PORTS_RANGE_SIZE

    def __enter__(self):
        lock_file = os.path.join(BUILD_DIR, "tests", "functional",
                                 f".port_lock.{self.PORTS_START}")
        self.lock_fh = os.open(lock_file, os.O_CREAT, mode=0o666)
        fcntl.flock(self.lock_fh, fcntl.LOCK_EX)
        return self

    def __exit__(self, exc_type, exc_value, traceback):
        fcntl.flock(self.lock_fh, fcntl.LOCK_UN)
        os.close(self.lock_fh)

    def check_bind(self, port: int) -> bool:
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
            try:
                sock.bind((self.PORTS_ADDR, port))
            except OSError:
                return False

        return True

    def find_free_ports(self, count: int) -> List[int]:
        result = []
        for port in range(self.PORTS_START, self.PORTS_END):
            if self.check_bind(port):
                result.append(port)
                if len(result) >= count:
                    break
        assert len(result) == count
        return result

    def find_free_port(self) -> int:
        return self.find_free_ports(1)[0]
