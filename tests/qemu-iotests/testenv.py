# TestEnv class to manage test environment variables.
#
# Copyright (c) 2020-2021 Virtuozzo International GmbH
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.
#

import os
import sys
import tempfile
from pathlib import Path
import shutil
import collections
import contextlib
import random
import subprocess
import glob
from typing import List, Dict, Any, Optional


DEF_GDB_OPTIONS = 'localhost:12345'

def isxfile(path: str) -> bool:
    return os.path.isfile(path) and os.access(path, os.X_OK)


def get_default_machine(qemu_prog: str) -> str:
    outp = subprocess.run([qemu_prog, '-machine', 'help'], check=True,
                          universal_newlines=True,
                          stdout=subprocess.PIPE).stdout

    machines = outp.split('\n')
    try:
        default_machine = next(m for m in machines if ' (default)' in m)
    except StopIteration:
        return ''
    default_machine = default_machine.split(' ', 1)[0]

    alias_suf = ' (alias of {})'.format(default_machine)
    alias = next((m for m in machines if m.endswith(alias_suf)), None)
    if alias is not None:
        default_machine = alias.split(' ', 1)[0]

    return default_machine


class TestEnv(contextlib.AbstractContextManager['TestEnv']):
    """
    Manage system environment for running tests

    The following variables are supported/provided. They are represented by
    lower-cased TestEnv attributes.
    """

    # We store environment variables as instance attributes, and there are a
    # lot of them. Silence pylint:
    # pylint: disable=too-many-instance-attributes

    env_variables = ['PYTHONPATH', 'TEST_DIR', 'SOCK_DIR', 'SAMPLE_IMG_DIR',
                     'PYTHON', 'QEMU_PROG', 'QEMU_IMG_PROG',
                     'QEMU_IO_PROG', 'QEMU_NBD_PROG', 'QSD_PROG',
                     'QEMU_OPTIONS', 'QEMU_IMG_OPTIONS',
                     'QEMU_IO_OPTIONS', 'QEMU_IO_OPTIONS_NO_FMT',
                     'QEMU_NBD_OPTIONS', 'IMGOPTS', 'IMGFMT', 'IMGPROTO',
                     'AIOMODE', 'CACHEMODE', 'VALGRIND_QEMU',
                     'CACHEMODE_IS_DEFAULT', 'IMGFMT_GENERIC', 'IMGOPTSSYNTAX',
                     'IMGKEYSECRET', 'QEMU_DEFAULT_MACHINE', 'MALLOC_PERTURB_',
                     'GDB_OPTIONS', 'PRINT_QEMU']

    def prepare_subprocess(self, args: List[str]) -> Dict[str, str]:
        if self.debug:
            args.append('-d')

        with open(args[0], encoding="utf-8") as f:
            try:
                if f.readline().rstrip() == '#!/usr/bin/env python3':
                    args.insert(0, self.python)
            except UnicodeDecodeError:  # binary test? for future.
                pass

        os_env = os.environ.copy()
        os_env.update(self.get_env())
        return os_env

    def get_env(self) -> Dict[str, str]:
        env = {}
        for v in self.env_variables:
            val = getattr(self, v.lower(), None)
            if val is not None:
                env[v] = val

        return env

    def init_directories(self) -> None:
        """Init directory variables:
             PYTHONPATH
             TEST_DIR
             SOCK_DIR
             SAMPLE_IMG_DIR
        """

        # Path where qemu goodies live in this source tree.
        qemu_srctree_path = Path(__file__, '../../../python').resolve()

        self.pythonpath = os.pathsep.join(filter(None, (
            self.source_iotests,
            str(qemu_srctree_path),
            os.getenv('PYTHONPATH'),
        )))

        self.test_dir = os.getenv('TEST_DIR',
                                  os.path.join(os.getcwd(), 'scratch'))
        Path(self.test_dir).mkdir(parents=True, exist_ok=True)

        try:
            self.sock_dir = os.environ['SOCK_DIR']
            self.tmp_sock_dir = False
            Path(self.sock_dir).mkdir(parents=True, exist_ok=True)
        except KeyError:
            self.sock_dir = tempfile.mkdtemp(prefix="qemu-iotests-")
            self.tmp_sock_dir = True

        self.sample_img_dir = os.getenv('SAMPLE_IMG_DIR',
                                        os.path.join(self.source_iotests,
                                                     'sample_images'))

    def init_binaries(self) -> None:
        """Init binary path variables:
             PYTHON (for bash tests)
             QEMU_PROG, QEMU_IMG_PROG, QEMU_IO_PROG, QEMU_NBD_PROG, QSD_PROG
        """
        self.python = sys.executable

        def root(*names: str) -> str:
            return os.path.join(self.build_root, *names)

        arch = os.uname().machine
        if 'ppc64' in arch:
            arch = 'ppc64'

        self.qemu_prog = os.getenv('QEMU_PROG', root(f'qemu-system-{arch}'))
        if not os.path.exists(self.qemu_prog):
            pattern = root('qemu-system-*')
            try:
                progs = sorted(glob.iglob(pattern))
                self.qemu_prog = next(p for p in progs if isxfile(p))
            except StopIteration:
                sys.exit("Not found any Qemu executable binary by pattern "
                         f"'{pattern}'")

        self.qemu_img_prog = os.getenv('QEMU_IMG_PROG', root('qemu-img'))
        self.qemu_io_prog = os.getenv('QEMU_IO_PROG', root('qemu-io'))
        self.qemu_nbd_prog = os.getenv('QEMU_NBD_PROG', root('qemu-nbd'))
        self.qsd_prog = os.getenv('QSD_PROG', root('storage-daemon',
                                                   'qemu-storage-daemon'))

        for b in [self.qemu_img_prog, self.qemu_io_prog, self.qemu_nbd_prog,
                  self.qemu_prog, self.qsd_prog]:
            if not os.path.exists(b):
                sys.exit('No such file: ' + b)
            if not isxfile(b):
                sys.exit('Not executable: ' + b)

    def __init__(self, source_dir: str, build_dir: str,
                 imgfmt: str, imgproto: str, aiomode: str,
                 cachemode: Optional[str] = None,
                 imgopts: Optional[str] = None,
                 misalign: bool = False,
                 debug: bool = False,
                 valgrind: bool = False,
                 gdb: bool = False,
                 qprint: bool = False,
                 dry_run: bool = False) -> None:
        self.imgfmt = imgfmt
        self.imgproto = imgproto
        self.aiomode = aiomode
        self.imgopts = imgopts
        self.misalign = misalign
        self.debug = debug

        if qprint:
            self.print_qemu = 'y'

        if gdb:
            self.gdb_options = os.getenv('GDB_OPTIONS', DEF_GDB_OPTIONS)
            if not self.gdb_options:
                # cover the case 'export GDB_OPTIONS='
                self.gdb_options = DEF_GDB_OPTIONS
        elif 'GDB_OPTIONS' in os.environ:
            # to not propagate it in prepare_subprocess()
            del os.environ['GDB_OPTIONS']

        if valgrind:
            self.valgrind_qemu = 'y'

        if cachemode is None:
            self.cachemode_is_default = 'true'
            self.cachemode = 'writeback'
        else:
            self.cachemode_is_default = 'false'
            self.cachemode = cachemode

        # Initialize generic paths: build_root, build_iotests, source_iotests,
        # which are needed to initialize some environment variables. They are
        # used by init_*() functions as well.

        self.source_iotests = source_dir
        self.build_iotests = build_dir

        self.build_root = Path(self.build_iotests).parent.parent

        self.init_directories()

        if dry_run:
            return

        self.init_binaries()

        self.malloc_perturb_ = os.getenv('MALLOC_PERTURB_',
                                         str(random.randrange(1, 255)))

        # QEMU_OPTIONS
        self.qemu_options = '-nodefaults -display none -accel qtest'
        machine_map = (
            ('arm', 'virt'),
            ('aarch64', 'virt'),
            ('avr', 'mega2560'),
            ('m68k', 'virt'),
            ('or1k', 'virt'),
            ('riscv32', 'virt'),
            ('riscv64', 'virt'),
            ('rx', 'gdbsim-r5f562n8'),
            ('sh4', 'r2d'),
            ('sh4eb', 'r2d'),
            ('tricore', 'tricore_testboard')
        )
        for suffix, machine in machine_map:
            if self.qemu_prog.endswith(f'qemu-system-{suffix}'):
                self.qemu_options += f' -machine {machine}'

        # QEMU_DEFAULT_MACHINE
        self.qemu_default_machine = get_default_machine(self.qemu_prog)

        self.qemu_img_options = os.getenv('QEMU_IMG_OPTIONS')
        self.qemu_nbd_options = os.getenv('QEMU_NBD_OPTIONS')

        is_generic = self.imgfmt not in ['bochs', 'cloop', 'dmg', 'vvfat']
        self.imgfmt_generic = 'true' if is_generic else 'false'

        self.qemu_io_options = f'--cache {self.cachemode} --aio {self.aiomode}'
        if self.misalign:
            self.qemu_io_options += ' --misalign'

        self.qemu_io_options_no_fmt = self.qemu_io_options

        if self.imgfmt == 'luks':
            self.imgoptssyntax = 'true'
            self.imgkeysecret = '123456'
            if not self.imgopts:
                self.imgopts = 'iter-time=10'
            elif 'iter-time=' not in self.imgopts:
                self.imgopts += ',iter-time=10'
        else:
            self.imgoptssyntax = 'false'
            self.qemu_io_options += ' -f ' + self.imgfmt

        if self.imgfmt == 'vmdk':
            if not self.imgopts:
                self.imgopts = 'zeroed_grain=on'
            elif 'zeroed_grain=' not in self.imgopts:
                self.imgopts += ',zeroed_grain=on'

    def close(self) -> None:
        if self.tmp_sock_dir:
            shutil.rmtree(self.sock_dir)

    def __enter__(self) -> 'TestEnv':
        return self

    def __exit__(self, exc_type: Any, exc_value: Any, traceback: Any) -> None:
        self.close()

    def print_env(self, prefix: str = '') -> None:
        template = """\
{prefix}QEMU          -- "{QEMU_PROG}" {QEMU_OPTIONS}
{prefix}QEMU_IMG      -- "{QEMU_IMG_PROG}" {QEMU_IMG_OPTIONS}
{prefix}QEMU_IO       -- "{QEMU_IO_PROG}" {QEMU_IO_OPTIONS}
{prefix}QEMU_NBD      -- "{QEMU_NBD_PROG}" {QEMU_NBD_OPTIONS}
{prefix}IMGFMT        -- {IMGFMT}{imgopts}
{prefix}IMGPROTO      -- {IMGPROTO}
{prefix}PLATFORM      -- {platform}
{prefix}TEST_DIR      -- {TEST_DIR}
{prefix}SOCK_DIR      -- {SOCK_DIR}
{prefix}GDB_OPTIONS   -- {GDB_OPTIONS}
{prefix}VALGRIND_QEMU -- {VALGRIND_QEMU}
{prefix}PRINT_QEMU_OUTPUT -- {PRINT_QEMU}
{prefix}"""

        args = collections.defaultdict(str, self.get_env())

        if 'IMGOPTS' in args:
            args['imgopts'] = f" ({args['IMGOPTS']})"

        u = os.uname()
        args['platform'] = f'{u.sysname}/{u.machine} {u.nodename} {u.release}'
        args['prefix'] = prefix
        print(template.format_map(args))
