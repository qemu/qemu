#!/usr/bin/env python3
#
# pylint: disable=C0103,E0213,E1135,E1136,E1137,R0902,R0903,R0912,R0913,R0917
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Copyright (C) 2024-2025 Mauro Carvalho Chehab <mchehab+huawei@kernel.org>

"""
Helper classes to be used by ghes_inject command classes.
"""

import json
import sys

from datetime import datetime
from os import path as os_path

try:
    qemu_dir = os_path.abspath(os_path.dirname(os_path.dirname(__file__)))
    sys.path.append(os_path.join(qemu_dir, 'python'))

    from qemu.qmp.legacy import QEMUMonitorProtocol

except ModuleNotFoundError as exc:
    print(f"Module '{exc.name}' not found.")
    print("Try export PYTHONPATH=top-qemu-dir/python or run from top-qemu-dir")
    sys.exit(1)

from base64 import b64encode

class util:
    """
    Ancillary functions to deal with bitmaps, parse arguments,
    generate GUID and encode data on a bytearray buffer.
    """

    #
    # Helper routines to handle multiple choice arguments
    #
    def get_choice(name, value, choices, suffixes=None, bitmask=True):
        """Produce a list from multiple choice argument"""

        new_values = 0

        if not value:
            return new_values

        for val in value.split(","):
            val = val.lower()

            if suffixes:
                for suffix in suffixes:
                    val = val.removesuffix(suffix)

            if val not in choices.keys():
                if suffixes:
                    for suffix in suffixes:
                        if val + suffix in choices.keys():
                            val += suffix
                            break

            if val not in choices.keys():
                sys.exit(f"Error on '{name}': choice '{val}' is invalid.")

            val = choices[val]

            if bitmask:
                new_values |= val
            else:
                if new_values:
                    sys.exit(f"Error on '{name}': only one value is accepted.")

                new_values = val

        return new_values

    def get_array(name, values, max_val=None):
        """Add numbered hashes from integer lists into an array"""

        array = []

        for value in values:
            for val in value.split(","):
                try:
                    val = int(val, 0)
                except ValueError:
                    sys.exit(f"Error on '{name}': {val} is not an integer")

                if val < 0:
                    sys.exit(f"Error on '{name}': {val} is not unsigned")

                if max_val and val > max_val:
                    sys.exit(f"Error on '{name}': {val} is too little")

                array.append(val)

        return array

    def get_mult_array(mult, name, values, allow_zero=False, max_val=None):
        """Add numbered hashes from integer lists"""

        if not allow_zero:
            if not values:
                return
        else:
            if values is None:
                return

            if not values:
                i = 0
                if i not in mult:
                    mult[i] = {}

                mult[i][name] = []
                return

        i = 0
        for value in values:
            for val in value.split(","):
                try:
                    val = int(val, 0)
                except ValueError:
                    sys.exit(f"Error on '{name}': {val} is not an integer")

                if val < 0:
                    sys.exit(f"Error on '{name}': {val} is not unsigned")

                if max_val and val > max_val:
                    sys.exit(f"Error on '{name}': {val} is too little")

                if i not in mult:
                    mult[i] = {}

                if name not in mult[i]:
                    mult[i][name] = []

                mult[i][name].append(val)

            i += 1


    def get_mult_choices(mult, name, values, choices,
                        suffixes=None, allow_zero=False):
        """Add numbered hashes from multiple choice arguments"""

        if not allow_zero:
            if not values:
                return
        else:
            if values is None:
                return

        i = 0
        for val in values:
            new_values = util.get_choice(name, val, choices, suffixes)

            if i not in mult:
                mult[i] = {}

            mult[i][name] = new_values
            i += 1


    def get_mult_int(mult, name, values, allow_zero=False):
        """Add numbered hashes from integer arguments"""
        if not allow_zero:
            if not values:
                return
        else:
            if values is None:
                return

        i = 0
        for val in values:
            try:
                val = int(val, 0)
            except ValueError:
                sys.exit(f"Error on '{name}': {val} is not an integer")

            if val < 0:
                sys.exit(f"Error on '{name}': {val} is not unsigned")

            if i not in mult:
                mult[i] = {}

            mult[i][name] = val
            i += 1


    #
    # Data encode helper functions
    #
    def bit(b):
        """Simple macro to define a bit on a bitmask"""
        return 1 << b


    def data_add(data, value, num_bytes):
        """Adds bytes from value inside a bitarray"""

        data.extend(value.to_bytes(num_bytes, byteorder="little"))  # pylint: disable=E1101

    def dump_bytearray(name, data):
        """Does an hexdump of a byte array, grouping in bytes"""

        print(f"{name} ({len(data)} bytes):")

        for ln_start in range(0, len(data), 16):
            ln_end = min(ln_start + 16, len(data))
            print(f"      {ln_start:08x}  ", end="")
            for i in range(ln_start, ln_end):
                print(f"{data[i]:02x} ", end="")
            for i in range(ln_end, ln_start + 16):
                print("   ", end="")
            print("  ", end="")
            for i in range(ln_start, ln_end):
                if data[i] >= 32 and data[i] < 127:
                    print(chr(data[i]), end="")
                else:
                    print(".", end="")

            print()
        print()

    def time(string):
        """Handle BCD timestamps used on Generic Error Data Block"""

        time = None

        # Formats to be used when parsing time stamps
        formats = [
            "%Y-%m-%d %H:%M:%S",
        ]

        if string == "now":
            time = datetime.now()

        if time is None:
            for fmt in formats:
                try:
                    time = datetime.strptime(string, fmt)
                    break
                except ValueError:
                    pass

            if time is None:
                raise ValueError("Invalid time format")

        return time

class guid:
    """
    Simple class to handle GUID fields.
    """

    def __init__(self, time_low, time_mid, time_high, nodes):
        """Initialize a GUID value"""

        assert len(nodes) == 8

        self.time_low = time_low
        self.time_mid = time_mid
        self.time_high = time_high
        self.nodes = nodes

    @classmethod
    def UUID(cls, guid_str):
        """Initialize a GUID using a string on its standard format"""

        if len(guid_str) != 36:
            print("Size not 36")
            raise ValueError('Invalid GUID size')

        # It is easier to parse without separators. So, drop them
        guid_str = guid_str.replace('-', '')

        if len(guid_str) != 32:
            print("Size not 32", guid_str, len(guid_str))
            raise ValueError('Invalid GUID hex size')

        time_low = 0
        time_mid = 0
        time_high = 0
        nodes = []

        for i in reversed(range(16, 32, 2)):
            h = guid_str[i:i + 2]
            value = int(h, 16)
            nodes.insert(0, value)

        time_high = int(guid_str[12:16], 16)
        time_mid = int(guid_str[8:12], 16)
        time_low = int(guid_str[0:8], 16)

        return cls(time_low, time_mid, time_high, nodes)

    def __str__(self):
        """Output a GUID value on its default string representation"""

        clock = self.nodes[0] << 8 | self.nodes[1]

        node = 0
        for i in range(2, len(self.nodes)):
            node = node << 8 | self.nodes[i]

        s = f"{self.time_low:08x}-{self.time_mid:04x}-"
        s += f"{self.time_high:04x}-{clock:04x}-{node:012x}"
        return s

    def to_bytes(self):
        """Output a GUID value in bytes"""

        data = bytearray()

        util.data_add(data, self.time_low, 4)
        util.data_add(data, self.time_mid, 2)
        util.data_add(data, self.time_high, 2)
        data.extend(bytearray(self.nodes))

        return data

class qmp:
    """
    Opens a connection and send/receive QMP commands.
    """

    def send_cmd(self, command, args=None, may_open=False, return_error=True):
        """Send a command to QMP, optinally opening a connection"""

        if may_open:
            self._connect()
        elif not self.connected:
            return False

        msg = { 'execute': command }
        if args:
            msg['arguments'] = args

        try:
            obj = self.qmp_monitor.cmd_obj(msg)
        # Can we use some other exception class here?
        except Exception as e:                         # pylint: disable=W0718
            print(f"Command: {command}")
            print(f"Failed to inject error: {e}.")
            return None

        if "return" in obj:
            if isinstance(obj.get("return"), dict):
                if obj["return"]:
                    return obj["return"]
                return "OK"

            return obj["return"]

        if isinstance(obj.get("error"), dict):
            error = obj["error"]
            if return_error:
                print(f"Command: {msg}")
                print(f'{error["class"]}: {error["desc"]}')
        else:
            print(json.dumps(obj))

        return None

    def _close(self):
        """Shutdown and close the socket, if opened"""
        if not self.connected:
            return

        self.qmp_monitor.close()
        self.connected = False

    def _connect(self):
        """Connect to a QMP TCP/IP port, if not connected yet"""

        if self.connected:
            return True

        try:
            self.qmp_monitor.connect(negotiate=True)
        except ConnectionError:
            sys.exit(f"Can't connect to QMP host {self.host}:{self.port}")

        self.connected = True

        return True

    BLOCK_STATUS_BITS = {
        "uncorrectable":            util.bit(0),
        "correctable":              util.bit(1),
        "multi-uncorrectable":      util.bit(2),
        "multi-correctable":        util.bit(3),
    }

    ERROR_SEVERITY = {
        "recoverable":  0,
        "fatal":        1,
        "corrected":    2,
        "none":         3,
    }

    VALIDATION_BITS = {
        "fru-id":       util.bit(0),
        "fru-text":     util.bit(1),
        "timestamp":    util.bit(2),
    }

    GEDB_FLAGS_BITS = {
        "recovered":    util.bit(0),
        "prev-error":   util.bit(1),
        "simulated":    util.bit(2),
    }

    GENERIC_DATA_SIZE = 72

    def argparse(parser):
        """Prepare a parser group to query generic error data"""

        block_status_bits = ",".join(qmp.BLOCK_STATUS_BITS.keys())
        error_severity_enum = ",".join(qmp.ERROR_SEVERITY.keys())
        validation_bits = ",".join(qmp.VALIDATION_BITS.keys())
        gedb_flags_bits = ",".join(qmp.GEDB_FLAGS_BITS.keys())

        g_gen = parser.add_argument_group("Generic Error Data")  # pylint: disable=E1101
        g_gen.add_argument("--block-status",
                           help=f"block status bits: {block_status_bits}")
        g_gen.add_argument("--raw-data", nargs="+",
                        help="Raw data inside the Error Status Block")
        g_gen.add_argument("--error-severity", "--severity",
                           help=f"error severity: {error_severity_enum}")
        g_gen.add_argument("--gen-err-valid-bits",
                           "--generic-error-validation-bits",
                           help=f"validation bits: {validation_bits}")
        g_gen.add_argument("--fru-id", type=guid.UUID,
                           help="GUID representing a physical device")
        g_gen.add_argument("--fru-text",
                           help="ASCII string identifying the FRU hardware")
        g_gen.add_argument("--timestamp", type=util.time,
                           help="Time when the error info was collected")
        g_gen.add_argument("--precise", "--precise-timestamp",
                           action='store_true',
                           help="Marks the timestamp as precise if --timestamp is used")
        g_gen.add_argument("--gedb-flags",
                           help=f"General Error Data Block flags: {gedb_flags_bits}")

    def set_args(self, args):
        """Set the arguments optionally defined via self.argparse()"""

        if args.block_status:
            self.block_status = util.get_choice(name="block-status",
                                                value=args.block_status,
                                                choices=self.BLOCK_STATUS_BITS,
                                                bitmask=False)
        if args.raw_data:
            self.raw_data = util.get_array("raw-data", args.raw_data,
                                           max_val=255)
            print(self.raw_data)

        if args.error_severity:
            self.error_severity = util.get_choice(name="error-severity",
                                                  value=args.error_severity,
                                                  choices=self.ERROR_SEVERITY,
                                                  bitmask=False)

        if args.fru_id:
            self.fru_id = args.fru_id.to_bytes()
            if not args.gen_err_valid_bits:
                self.validation_bits |= self.VALIDATION_BITS["fru-id"]

        if args.fru_text:
            text = bytearray(args.fru_text.encode('ascii'))
            if len(text) > 20:
                sys.exit("FRU text is too big to fit")

            self.fru_text = text
            if not args.gen_err_valid_bits:
                self.validation_bits |= self.VALIDATION_BITS["fru-text"]

        if args.timestamp:
            time = args.timestamp
            century = int(time.year / 100)

            bcd = bytearray()
            util.data_add(bcd, (time.second // 10) << 4 | (time.second % 10), 1)
            util.data_add(bcd, (time.minute // 10) << 4 | (time.minute % 10), 1)
            util.data_add(bcd, (time.hour // 10) << 4 | (time.hour % 10), 1)

            if args.precise:
                util.data_add(bcd, 1, 1)
            else:
                util.data_add(bcd, 0, 1)

            util.data_add(bcd, (time.day // 10) << 4 | (time.day % 10), 1)
            util.data_add(bcd, (time.month // 10) << 4 | (time.month % 10), 1)
            util.data_add(bcd,
                          ((time.year % 100) // 10) << 4 | (time.year % 10), 1)
            util.data_add(bcd, ((century % 100) // 10) << 4 | (century % 10), 1)

            self.timestamp = bcd
            if not args.gen_err_valid_bits:
                self.validation_bits |= self.VALIDATION_BITS["timestamp"]

        if args.gen_err_valid_bits:
            self.validation_bits = util.get_choice(name="validation",
                                                   value=args.gen_err_valid_bits,
                                                   choices=self.VALIDATION_BITS)

    def __init__(self, host, port, debug=False):
        """Initialize variables used by the QMP send logic"""

        self.connected = False
        self.host = host
        self.port = port
        self.debug = debug

        # ACPI 6.1: 18.3.2.7.1 Generic Error Data: Generic Error Status Block
        self.block_status = self.BLOCK_STATUS_BITS["uncorrectable"]
        self.raw_data = []
        self.error_severity = self.ERROR_SEVERITY["recoverable"]

        # ACPI 6.1: 18.3.2.7.1 Generic Error Data: Generic Error Data Entry
        self.validation_bits = 0
        self.flags = 0
        self.fru_id = bytearray(16)
        self.fru_text = bytearray(20)
        self.timestamp = bytearray(8)

        self.qmp_monitor = QEMUMonitorProtocol(address=(self.host, self.port))

    #
    # Socket QMP send command
    #
    def send_cper_raw(self, cper_data):
        """Send a raw CPER data to QEMU though QMP TCP socket"""

        data = b64encode(bytes(cper_data)).decode('ascii')

        cmd_arg = {
            'cper': data
        }

        self._connect()

        if self.send_cmd("inject-ghes-v2-error", cmd_arg):
            print("Error injected.")

    def send_cper(self, notif_type, payload):
        """Send commands to QEMU though QMP TCP socket"""

        # Fill CPER record header

        # NOTE: bits 4 to 13 of block status contain the number of
        # data entries in the data section. This is currently unsupported.

        cper_length = len(payload)
        data_length = cper_length + len(self.raw_data) + self.GENERIC_DATA_SIZE

        #  Generic Error Data Entry
        gede = bytearray()

        gede.extend(notif_type.to_bytes())
        util.data_add(gede, self.error_severity, 4)
        util.data_add(gede, 0x300, 2)
        util.data_add(gede, self.validation_bits, 1)
        util.data_add(gede, self.flags, 1)
        util.data_add(gede, cper_length, 4)
        gede.extend(self.fru_id)
        gede.extend(self.fru_text)
        gede.extend(self.timestamp)

        # Generic Error Status Block
        gebs = bytearray()

        if self.raw_data:
            raw_data_offset = len(gebs)
        else:
            raw_data_offset = 0

        util.data_add(gebs, self.block_status, 4)
        util.data_add(gebs, raw_data_offset, 4)
        util.data_add(gebs, len(self.raw_data), 4)
        util.data_add(gebs, data_length, 4)
        util.data_add(gebs, self.error_severity, 4)

        cper_data = bytearray()
        cper_data.extend(gebs)
        cper_data.extend(gede)
        cper_data.extend(bytearray(self.raw_data))
        cper_data.extend(bytearray(payload))

        if self.debug:
            print(f"GUID: {notif_type}")

            util.dump_bytearray("Generic Error Status Block", gebs)
            util.dump_bytearray("Generic Error Data Entry", gede)

            if self.raw_data:
                util.dump_bytearray("Raw data", bytearray(self.raw_data))

            util.dump_bytearray("Payload", payload)

        self.send_cper_raw(cper_data)


    def search_qom(self, path, prop, regex):
        """
        Return a list of devices that match path array like:

            /machine/unattached/device
            /machine/peripheral-anon/device
            ...
        """

        found = []

        i = 0
        while 1:
            dev = f"{path}[{i}]"
            args = {
                'path': dev,
                'property': prop
            }
            ret = self.send_cmd("qom-get", args, may_open=True,
                                return_error=False)
            if not ret:
                break

            if isinstance(ret, str):
                if regex.search(ret):
                    found.append(dev)

            i += 1
            if i > 10000:
                print("Too many objects returned by qom-get!")
                break

        return found

class cper_guid:
    """
    Contains CPER GUID, as per:
    https://uefi.org/specs/UEFI/2.10/Apx_N_Common_Platform_Error_Record.html
    """

    CPER_PROC_GENERIC =  guid(0x9876CCAD, 0x47B4, 0x4bdb,
                              [0xB6, 0x5E, 0x16, 0xF1,
                               0x93, 0xC4, 0xF3, 0xDB])

    CPER_PROC_X86 = guid(0xDC3EA0B0, 0xA144, 0x4797,
                         [0xB9, 0x5B, 0x53, 0xFA,
                          0x24, 0x2B, 0x6E, 0x1D])

    CPER_PROC_ITANIUM = guid(0xe429faf1, 0x3cb7, 0x11d4,
                             [0xbc, 0xa7, 0x00, 0x80,
                              0xc7, 0x3c, 0x88, 0x81])

    CPER_PROC_ARM = guid(0xE19E3D16, 0xBC11, 0x11E4,
                         [0x9C, 0xAA, 0xC2, 0x05,
                          0x1D, 0x5D, 0x46, 0xB0])

    CPER_PLATFORM_MEM = guid(0xA5BC1114, 0x6F64, 0x4EDE,
                             [0xB8, 0x63, 0x3E, 0x83,
                              0xED, 0x7C, 0x83, 0xB1])

    CPER_PLATFORM_MEM2 = guid(0x61EC04FC, 0x48E6, 0xD813,
                              [0x25, 0xC9, 0x8D, 0xAA,
                               0x44, 0x75, 0x0B, 0x12])

    CPER_PCIE = guid(0xD995E954, 0xBBC1, 0x430F,
                     [0xAD, 0x91, 0xB4, 0x4D,
                      0xCB, 0x3C, 0x6F, 0x35])

    CPER_PCI_BUS = guid(0xC5753963, 0x3B84, 0x4095,
                        [0xBF, 0x78, 0xED, 0xDA,
                         0xD3, 0xF9, 0xC9, 0xDD])

    CPER_PCI_DEV = guid(0xEB5E4685, 0xCA66, 0x4769,
                        [0xB6, 0xA2, 0x26, 0x06,
                         0x8B, 0x00, 0x13, 0x26])

    CPER_FW_ERROR = guid(0x81212A96, 0x09ED, 0x4996,
                         [0x94, 0x71, 0x8D, 0x72,
                          0x9C, 0x8E, 0x69, 0xED])

    CPER_DMA_GENERIC = guid(0x5B51FEF7, 0xC79D, 0x4434,
                            [0x8F, 0x1B, 0xAA, 0x62,
                             0xDE, 0x3E, 0x2C, 0x64])

    CPER_DMA_VT = guid(0x71761D37, 0x32B2, 0x45cd,
                       [0xA7, 0xD0, 0xB0, 0xFE,
                        0xDD, 0x93, 0xE8, 0xCF])

    CPER_DMA_IOMMU = guid(0x036F84E1, 0x7F37, 0x428c,
                         [0xA7, 0x9E, 0x57, 0x5F,
                          0xDF, 0xAA, 0x84, 0xEC])

    CPER_CCIX_PER = guid(0x91335EF6, 0xEBFB, 0x4478,
                         [0xA6, 0xA6, 0x88, 0xB7,
                          0x28, 0xCF, 0x75, 0xD7])

    CPER_CXL_PROT_ERR = guid(0x80B9EFB4, 0x52B5, 0x4DE3,
                             [0xA7, 0x77, 0x68, 0x78,
                              0x4B, 0x77, 0x10, 0x48])
