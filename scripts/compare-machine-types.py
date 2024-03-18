#!/usr/bin/env python3
#
# Script to compare machine type compatible properties (include/hw/boards.h).
# compat_props are applied to the driver during initialization to change
# default values, for instance, to maintain compatibility.
# This script constructs table with machines and values of their compat_props
# to compare and to find places for improvements or places with bugs. If
# during the comparison, some machine type doesn't have a property (it is in
# the comparison table because another machine type has it), then the
# appropriate method will be used to obtain the default value of this driver
# property via qmp command (e.g. query-cpu-model-expansion for x86_64-cpu).
# These methods are defined below in qemu_property_methods.
#
# Copyright (c) Yandex Technologies LLC, 2023
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
# along with this program; if not, see <http://www.gnu.org/licenses/>.

import sys
from os import path
from argparse import ArgumentParser, RawTextHelpFormatter, Namespace
import pandas as pd
from contextlib import ExitStack
from typing import Optional, List, Dict, Generator, Tuple, Union, Any, Set

try:
    qemu_dir = path.abspath(path.dirname(path.dirname(__file__)))
    sys.path.append(path.join(qemu_dir, 'python'))
    from qemu.machine import QEMUMachine
except ModuleNotFoundError as exc:
    print(f"Module '{exc.name}' not found.")
    print("Try export PYTHONPATH=top-qemu-dir/python or run from top-qemu-dir")
    sys.exit(1)


default_qemu_args = '-enable-kvm -machine none'
default_qemu_binary = 'build/qemu-system-x86_64'


# Methods for gettig the right values of drivers properties
#
# Use these methods as a 'whitelist' and add entries only if necessary. It's
# important to be stable and predictable in analysis and tests.
# Be careful:
# * Class must be inherited from 'QEMUObject' and used in new_driver()
# * Class has to implement get_prop method in order to get values
# * Specialization always wins (with the given classes for 'device' and
#   'x86_64-cpu', method of 'x86_64-cpu' will be used for '486-x86_64-cpu')

class Driver():
    def __init__(self, vm: QEMUMachine, name: str, abstract: bool) -> None:
        self.vm = vm
        self.name = name
        self.abstract = abstract
        self.parent: Optional[Driver] = None
        self.property_getter: Optional[Driver] = None

    def get_prop(self, driver: str, prop: str) -> str:
        if self.property_getter:
            return self.property_getter.get_prop(driver, prop)
        else:
            return 'Unavailable method'

    def is_child_of(self, parent: 'Driver') -> bool:
        """Checks whether self is (recursive) child of @parent"""
        cur_parent = self.parent
        while cur_parent:
            if cur_parent is parent:
                return True
            cur_parent = cur_parent.parent

        return False

    def set_implementations(self, implementations: List['Driver']) -> None:
        self.implementations = implementations


class QEMUObject(Driver):
    def __init__(self, vm: QEMUMachine, name: str) -> None:
        super().__init__(vm, name, True)

    def set_implementations(self, implementations: List[Driver]) -> None:
        self.implementations = implementations

        # each implementation of the abstract driver has to use property getter
        # of this abstract driver unless it has specialization. (e.g. having
        # 'device' and 'x86_64-cpu', property getter of 'x86_64-cpu' will be
        # used for '486-x86_64-cpu')
        for impl in implementations:
            if not impl.property_getter or\
                    self.is_child_of(impl.property_getter):
                impl.property_getter = self


class QEMUDevice(QEMUObject):
    def __init__(self, vm: QEMUMachine) -> None:
        super().__init__(vm, 'device')
        self.cached: Dict[str, List[Dict[str, Any]]] = {}

    def get_prop(self, driver: str, prop_name: str) -> str:
        if driver not in self.cached:
            self.cached[driver] = self.vm.cmd('device-list-properties',
                                              typename=driver)
        for prop in self.cached[driver]:
            if prop['name'] == prop_name:
                return str(prop.get('default-value', 'No default value'))

        return 'Unknown property'


class QEMUx86CPU(QEMUObject):
    def __init__(self, vm: QEMUMachine) -> None:
        super().__init__(vm, 'x86_64-cpu')
        self.cached: Dict[str, Dict[str, Any]] = {}

    def get_prop(self, driver: str, prop_name: str) -> str:
        if not driver.endswith('-x86_64-cpu'):
            return 'Wrong x86_64-cpu name'

        # crop last 11 chars '-x86_64-cpu'
        name = driver[:-11]
        if name not in self.cached:
            self.cached[name] = self.vm.cmd(
                'query-cpu-model-expansion', type='full',
                model={'name': name})['model']['props']
        return str(self.cached[name].get(prop_name, 'Unknown property'))


# Now it's stub, because all memory_backend types don't have default values
# but this behaviour can be changed
class QEMUMemoryBackend(QEMUObject):
    def __init__(self, vm: QEMUMachine) -> None:
        super().__init__(vm, 'memory-backend')
        self.cached: Dict[str, List[Dict[str, Any]]] = {}

    def get_prop(self, driver: str, prop_name: str) -> str:
        if driver not in self.cached:
            self.cached[driver] = self.vm.cmd('qom-list-properties',
                                              typename=driver)
        for prop in self.cached[driver]:
            if prop['name'] == prop_name:
                return str(prop.get('default-value', 'No default value'))

        return 'Unknown property'


def new_driver(vm: QEMUMachine, name: str, is_abstr: bool) -> Driver:
    if name == 'object':
        return QEMUObject(vm, 'object')
    elif name == 'device':
        return QEMUDevice(vm)
    elif name == 'x86_64-cpu':
        return QEMUx86CPU(vm)
    elif name == 'memory-backend':
        return QEMUMemoryBackend(vm)
    else:
        return Driver(vm, name, is_abstr)
# End of methods definition


class VMPropertyGetter:
    """It implements the relationship between drivers and how to get their
    properties"""
    def __init__(self, vm: QEMUMachine) -> None:
        self.drivers: Dict[str, Driver] = {}

        qom_all_types = vm.cmd('qom-list-types', abstract=True)
        self.drivers = {t['name']: new_driver(vm, t['name'],
                                              t.get('abstract', False))
                        for t in qom_all_types}

        for t in qom_all_types:
            drv = self.drivers[t['name']]
            if 'parent' in t:
                drv.parent = self.drivers[t['parent']]

        for drv in self.drivers.values():
            imps = vm.cmd('qom-list-types', implements=drv.name)
            # only implementations inherit property getter
            drv.set_implementations([self.drivers[imp['name']]
                                     for imp in imps])

    def get_prop(self, driver: str, prop: str) -> str:
        # wrong driver name or disabled in config driver
        try:
            drv = self.drivers[driver]
        except KeyError:
            return 'Unavailable driver'

        assert not drv.abstract

        return drv.get_prop(driver, prop)

    def get_implementations(self, driver: str) -> List[str]:
        return [impl.name for impl in self.drivers[driver].implementations]


class Machine:
    """A short QEMU machine type description. It contains only processed
    compat_props (properties of abstract classes are applied to its
    implementations)
    """
    # raw_mt_dict - dict produced by `query-machines`
    def __init__(self, raw_mt_dict: Dict[str, Any],
                 qemu_drivers: VMPropertyGetter) -> None:
        self.name = raw_mt_dict['name']
        self.compat_props: Dict[str, Any] = {}
        # properties are applied sequentially and can rewrite values like in
        # QEMU. Also it has to resolve class relationships to apply appropriate
        # values from abstract class to all implementations
        for prop in raw_mt_dict['compat-props']:
            driver = prop['qom-type']
            try:
                # implementation adds only itself, abstract class adds
                #  lementation (abstract classes are uninterestiong)
                impls = qemu_drivers.get_implementations(driver)
                for impl in impls:
                    if impl not in self.compat_props:
                        self.compat_props[impl] = {}
                    self.compat_props[impl][prop['property']] = prop['value']
            except KeyError:
                # QEMU doesn't know this driver thus it has to be saved
                if driver not in self.compat_props:
                    self.compat_props[driver] = {}
                self.compat_props[driver][prop['property']] = prop['value']


class Configuration():
    """Class contains all necessary components to generate table and is used
    to compare different binaries"""
    def __init__(self, vm: QEMUMachine,
                 req_mt: List[str], all_mt: bool) -> None:
        self._vm = vm
        self._binary = vm.binary
        self._qemu_args = args.qemu_args.split(' ')

        self._qemu_drivers = VMPropertyGetter(vm)
        self.req_mt = get_req_mt(self._qemu_drivers, vm, req_mt, all_mt)

    def get_implementations(self, driver_name: str) -> List[str]:
        return self._qemu_drivers.get_implementations(driver_name)

    def get_table(self, req_props: List[Tuple[str, str]]) -> pd.DataFrame:
        table: List[pd.DataFrame] = []
        for mt in self.req_mt:
            name = f'{self._binary}\n{mt.name}'
            column = []
            for driver, prop in req_props:
                try:
                    # values from QEMU machine type definitions
                    column.append(mt.compat_props[driver][prop])
                except KeyError:
                    # values from QEMU type definitions
                    column.append(self._qemu_drivers.get_prop(driver, prop))
            table.append(pd.DataFrame({name: column}))

        return pd.concat(table, axis=1)


script_desc = """Script to compare machine types (their compat_props).

Examples:
* save info about all machines:  ./scripts/compare-machine-types.py --all \
--format csv --raw > table.csv
* compare machines: ./scripts/compare-machine-types.py --mt pc-q35-2.12 \
pc-q35-3.0
* compare binaries and machines: ./scripts/compare-machine-types.py \
--mt pc-q35-6.2 pc-q35-7.0 --qemu-binary build/qemu-system-x86_64 \
build/qemu-exp
  ╒════════════╤══════════════════════════╤════════════════════════════\
╤════════════════════════════╤══════════════════╤══════════════════╕
  │   Driver   │         Property         │  build/qemu-system-x86_64  \
│  build/qemu-system-x86_64  │  build/qemu-exp  │  build/qemu-exp  │
  │            │                          │         pc-q35-6.2         \
│         pc-q35-7.0         │    pc-q35-6.2    │    pc-q35-7.0    │
  ╞════════════╪══════════════════════════╪════════════════════════════\
╪════════════════════════════╪══════════════════╪══════════════════╡
  │  PIIX4_PM  │ x-not-migrate-acpi-index │            True            \
│           False            │      False       │      False       │
  ├────────────┼──────────────────────────┼────────────────────────────\
┼────────────────────────────┼──────────────────┼──────────────────┤
  │ virtio-mem │  unplugged-inaccessible  │           False            \
│            auto            │      False       │       auto       │
  ╘════════════╧══════════════════════════╧════════════════════════════\
╧════════════════════════════╧══════════════════╧══════════════════╛

If a property from QEMU machine defintion applies to an abstract class (e.g. \
x86_64-cpu) this script will compare all implementations of this class.

"Unavailable method" - means that this script doesn't know how to get \
default values of the driver. To add method use the construction described \
at the top of the script.
"Unavailable driver" - means that this script doesn't know this driver. \
For instance, this can happen if you configure QEMU without this device or \
if machine type definition has error.
"No default value" - means that the appropriate method can't get the default \
value and most likely that this property doesn't have it.
"Unknown property" - means that the appropriate method can't find property \
with this name."""


def parse_args() -> Namespace:
    parser = ArgumentParser(formatter_class=RawTextHelpFormatter,
                            description=script_desc)
    parser.add_argument('--format', choices=['human-readable', 'json', 'csv'],
                        default='human-readable',
                        help='returns table in json format')
    parser.add_argument('--raw', action='store_true',
                        help='prints ALL defined properties without value '
                             'transformation. By default, only rows '
                             'with different values will be printed and '
                             'values will be transformed(e.g. "on" -> True)')
    parser.add_argument('--qemu-args', default=default_qemu_args,
                        help='command line to start qemu. '
                             f'Default: "{default_qemu_args}"')
    parser.add_argument('--qemu-binary', nargs="*", type=str,
                        default=[default_qemu_binary],
                        help='list of qemu binaries that will be compared. '
                             f'Deafult: {default_qemu_binary}')

    mt_args_group = parser.add_mutually_exclusive_group()
    mt_args_group.add_argument('--all', action='store_true',
                               help='prints all available machine types (list '
                                    'of machine types will be ignored)')
    mt_args_group.add_argument('--mt', nargs="*", type=str,
                               help='list of Machine Types '
                                    'that will be compared')

    return parser.parse_args()


def mt_comp(mt: Machine) -> Tuple[str, int, int, int]:
    """Function to compare and sort machine by names.
    It returns socket_name, major version, minor version, revision"""
    # none, microvm, x-remote and etc.
    if '-' not in mt.name or '.' not in mt.name:
        return mt.name, 0, 0, 0

    socket, ver = mt.name.rsplit('-', 1)
    ver_list = list(map(int, ver.split('.', 2)))
    ver_list += [0] * (3 - len(ver_list))
    return socket, ver_list[0], ver_list[1], ver_list[2]


def get_mt_definitions(qemu_drivers: VMPropertyGetter,
                       vm: QEMUMachine) -> List[Machine]:
    """Constructs list of machine definitions (primarily compat_props) via
    info from QEMU"""
    raw_mt_defs = vm.cmd('query-machines', compat_props=True)
    mt_defs = []
    for raw_mt in raw_mt_defs:
        mt_defs.append(Machine(raw_mt, qemu_drivers))

    mt_defs.sort(key=mt_comp)
    return mt_defs


def get_req_mt(qemu_drivers: VMPropertyGetter, vm: QEMUMachine,
               req_mt: Optional[List[str]], all_mt: bool) -> List[Machine]:
    """Returns list of requested by user machines"""
    mt_defs = get_mt_definitions(qemu_drivers, vm)
    if all_mt:
        return mt_defs

    if req_mt is None:
        print('Enter machine types for comparision')
        exit(0)

    matched_mt = []
    for mt in mt_defs:
        if mt.name in req_mt:
            matched_mt.append(mt)

    return matched_mt


def get_affected_props(configs: List[Configuration]) -> Generator[Tuple[str,
                                                                        str],
                                                                  None, None]:
    """Helps to go through all affected in machine definitions drivers
    and properties"""
    driver_props: Dict[str, Set[Any]] = {}
    for config in configs:
        for mt in config.req_mt:
            compat_props = mt.compat_props
            for driver, prop in compat_props.items():
                if driver not in driver_props:
                    driver_props[driver] = set()
                driver_props[driver].update(prop.keys())

    for driver, props in sorted(driver_props.items()):
        for prop in sorted(props):
            yield driver, prop


def transform_value(value: str) -> Union[str, bool]:
    true_list = ['true', 'on']
    false_list = ['false', 'off']

    out = value.lower()

    if out in true_list:
        return True

    if out in false_list:
        return False

    return value


def simplify_table(table: pd.DataFrame) -> pd.DataFrame:
    """transforms values to make it easier to compare it and drops rows
    with the same values for all columns"""

    table = table.map(transform_value)

    return table[~table.iloc[:, 3:].eq(table.iloc[:, 2], axis=0).all(axis=1)]


# constructs table in the format:
#
# Driver  | Property  | binary1  | binary1  | ...
#         |           | machine1 | machine2 | ...
# ------------------------------------------------------ ...
# driver1 | property1 |  value1  |  value2  | ...
# driver1 | property2 |  value3  |  value4  | ...
# driver2 | property3 |  value5  |  value6  | ...
#   ...   |    ...    |   ...    |   ...    | ...
#
def fill_prop_table(configs: List[Configuration],
                    is_raw: bool) -> pd.DataFrame:
    req_props = list(get_affected_props(configs))
    if not req_props:
        print('No drivers to compare. Check machine names')
        exit(0)

    driver_col, prop_col = tuple(zip(*req_props))
    table = [pd.DataFrame({'Driver': driver_col}),
             pd.DataFrame({'Property': prop_col})]

    table.extend([config.get_table(req_props) for config in configs])

    df_table = pd.concat(table, axis=1)

    if is_raw:
        return df_table

    return simplify_table(df_table)


def print_table(table: pd.DataFrame, table_format: str) -> None:
    if table_format == 'json':
        print(comp_table.to_json())
    elif table_format == 'csv':
        print(comp_table.to_csv())
    else:
        print(comp_table.to_markdown(index=False, stralign='center',
                                     colalign=('center',), headers='keys',
                                     tablefmt='fancy_grid',
                                     disable_numparse=True))


if __name__ == '__main__':
    args = parse_args()
    with ExitStack() as stack:
        vms = [stack.enter_context(QEMUMachine(binary=binary, qmp_timer=15,
               args=args.qemu_args.split(' '))) for binary in args.qemu_binary]

        configurations = []
        for vm in vms:
            vm.launch()
            configurations.append(Configuration(vm, args.mt, args.all))

        comp_table = fill_prop_table(configurations, args.raw)
        if not comp_table.empty:
            print_table(comp_table, args.format)
