#!/usr/bin/env python3
#
# pylint: disable=C0301,C0114,R0903,R0912,R0913,R0914,R0915,W0511
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Copyright (C) 2024-2025 Mauro Carvalho Chehab <mchehab+huawei@kernel.org>

# TODO: current implementation has dummy defaults.
#
# For a better implementation, a QMP addition/call is needed to
# retrieve some data for ARM Processor Error injection:
#
#   - ARM registers: power_state, mpidr.

"""
Generate an ARM processor error CPER, compatible with
UEFI 2.9A Errata.

Injecting such errors can be done using:

    $ ./scripts/ghes_inject.py arm
    Error injected.

Produces a simple CPER register, as detected on a Linux guest:

[Hardware Error]: Hardware error from APEI Generic Hardware Error Source: 1
[Hardware Error]: event severity: recoverable
[Hardware Error]:  Error 0, type: recoverable
[Hardware Error]:   section_type: ARM processor error
[Hardware Error]:   MIDR: 0x0000000000000000
[Hardware Error]:   running state: 0x0
[Hardware Error]:   Power State Coordination Interface state: 0
[Hardware Error]:   Error info structure 0:
[Hardware Error]:   num errors: 2
[Hardware Error]:    error_type: 0x02: cache error
[Hardware Error]:    error_info: 0x000000000091000f
[Hardware Error]:     transaction type: Data Access
[Hardware Error]:     cache error, operation type: Data write
[Hardware Error]:     cache level: 2
[Hardware Error]:     processor context not corrupted
[Firmware Warn]: GHES: Unhandled processor error type 0x02: cache error

The ARM Processor Error message can be customized via command line
parameters. For instance:

    $ ./scripts/ghes_inject.py arm --mpidr 0x444 --running --affinity 1 \
        --error-info 12345678 --vendor 0x13,123,4,5,1 --ctx-array 0,1,2,3,4,5 \
        -t cache tlb bus micro-arch tlb,micro-arch
    Error injected.

Injects this error, as detected on a Linux guest:

[Hardware Error]: Hardware error from APEI Generic Hardware Error Source: 1
[Hardware Error]: event severity: recoverable
[Hardware Error]:  Error 0, type: recoverable
[Hardware Error]:   section_type: ARM processor error
[Hardware Error]:   MIDR: 0x0000000000000000
[Hardware Error]:   Multiprocessor Affinity Register (MPIDR): 0x0000000000000000
[Hardware Error]:   error affinity level: 0
[Hardware Error]:   running state: 0x1
[Hardware Error]:   Power State Coordination Interface state: 0
[Hardware Error]:   Error info structure 0:
[Hardware Error]:   num errors: 2
[Hardware Error]:    error_type: 0x02: cache error
[Hardware Error]:    error_info: 0x0000000000bc614e
[Hardware Error]:     cache level: 2
[Hardware Error]:     processor context not corrupted
[Hardware Error]:   Error info structure 1:
[Hardware Error]:   num errors: 2
[Hardware Error]:    error_type: 0x04: TLB error
[Hardware Error]:    error_info: 0x000000000054007f
[Hardware Error]:     transaction type: Instruction
[Hardware Error]:     TLB error, operation type: Instruction fetch
[Hardware Error]:     TLB level: 1
[Hardware Error]:     processor context not corrupted
[Hardware Error]:     the error has not been corrected
[Hardware Error]:     PC is imprecise
[Hardware Error]:   Error info structure 2:
[Hardware Error]:   num errors: 2
[Hardware Error]:    error_type: 0x08: bus error
[Hardware Error]:    error_info: 0x00000080d6460fff
[Hardware Error]:     transaction type: Generic
[Hardware Error]:     bus error, operation type: Generic read (type of instruction or data request cannot be determined)
[Hardware Error]:     affinity level at which the bus error occurred: 1
[Hardware Error]:     processor context corrupted
[Hardware Error]:     the error has been corrected
[Hardware Error]:     PC is imprecise
[Hardware Error]:     Program execution can be restarted reliably at the PC associated with the error.
[Hardware Error]:     participation type: Local processor observed
[Hardware Error]:     request timed out
[Hardware Error]:     address space: External Memory Access
[Hardware Error]:     memory access attributes:0x20
[Hardware Error]:     access mode: secure
[Hardware Error]:   Error info structure 3:
[Hardware Error]:   num errors: 2
[Hardware Error]:    error_type: 0x10: micro-architectural error
[Hardware Error]:    error_info: 0x0000000078da03ff
[Hardware Error]:   Error info structure 4:
[Hardware Error]:   num errors: 2
[Hardware Error]:    error_type: 0x14: TLB error|micro-architectural error
[Hardware Error]:   Context info structure 0:
[Hardware Error]:    register context type: AArch64 EL1 context registers
[Hardware Error]:    00000000: 00000000 00000000
[Hardware Error]:   Vendor specific error info has 5 bytes:
[Hardware Error]:    00000000: 13 7b 04 05 01                                   .{...
[Firmware Warn]: GHES: Unhandled processor error type 0x02: cache error
[Firmware Warn]: GHES: Unhandled processor error type 0x04: TLB error
[Firmware Warn]: GHES: Unhandled processor error type 0x08: bus error
[Firmware Warn]: GHES: Unhandled processor error type 0x10: micro-architectural error
[Firmware Warn]: GHES: Unhandled processor error type 0x14: TLB error|micro-architectural error
"""

import argparse
import re

from qmp_helper import qmp, util, cper_guid


class ArmProcessorEinj:
    """
    Implements ARM Processor Error injection via GHES
    """

    DESC = """
    Generates an ARM processor error CPER, compatible with
    UEFI 2.9A Errata.
    """

    ACPI_GHES_ARM_CPER_LENGTH = 40
    ACPI_GHES_ARM_CPER_PEI_LENGTH = 32

    # Context types
    CONTEXT_AARCH32_EL1 = 1
    CONTEXT_AARCH64_EL1 = 5
    CONTEXT_MISC_REG = 8

    def __init__(self, subparsers):
        """Initialize the error injection class and add subparser"""

        # Valid choice values
        self.arm_valid_bits = {
            "mpidr":    util.bit(0),
            "affinity": util.bit(1),
            "running":  util.bit(2),
            "vendor":   util.bit(3),
        }

        self.pei_flags = {
            "first":        util.bit(0),
            "last":         util.bit(1),
            "propagated":   util.bit(2),
            "overflow":     util.bit(3),
        }

        self.pei_error_types = {
            "cache":        util.bit(1),
            "tlb":          util.bit(2),
            "bus":          util.bit(3),
            "micro-arch":   util.bit(4),
        }

        self.pei_valid_bits = {
            "multiple-error":   util.bit(0),
            "flags":            util.bit(1),
            "error-info":       util.bit(2),
            "virt-addr":        util.bit(3),
            "phy-addr":         util.bit(4),
        }

        self.data = bytearray()

        parser = subparsers.add_parser("arm", description=self.DESC)

        arm_valid_bits = ",".join(self.arm_valid_bits.keys())
        flags = ",".join(self.pei_flags.keys())
        error_types = ",".join(self.pei_error_types.keys())
        pei_valid_bits = ",".join(self.pei_valid_bits.keys())

        # UEFI N.16 ARM Validation bits
        g_arm = parser.add_argument_group("ARM processor")
        g_arm.add_argument("--arm", "--arm-valid",
                           help=f"ARM valid bits: {arm_valid_bits}")
        g_arm.add_argument("-a", "--affinity",  "--level", "--affinity-level",
                           type=lambda x: int(x, 0),
                           help="Affinity level (when multiple levels apply)")
        g_arm.add_argument("-l", "--mpidr", type=lambda x: int(x, 0),
                           help="Multiprocessor Affinity Register")
        g_arm.add_argument("-i", "--midr", type=lambda x: int(x, 0),
                           help="Main ID Register")
        g_arm.add_argument("-r", "--running",
                           action=argparse.BooleanOptionalAction,
                           default=None,
                           help="Indicates if the processor is running or not")
        g_arm.add_argument("--psci", "--psci-state",
                           type=lambda x: int(x, 0),
                           help="Power State Coordination Interface - PSCI state")

        # TODO: Add vendor-specific support

        # UEFI N.17 bitmaps (type and flags)
        g_pei = parser.add_argument_group("ARM Processor Error Info (PEI)")
        g_pei.add_argument("-t", "--type", nargs="+",
                        help=f"one or more error types: {error_types}")
        g_pei.add_argument("-f", "--flags", nargs="*",
                        help=f"zero or more error flags: {flags}")
        g_pei.add_argument("-V", "--pei-valid", "--error-valid", nargs="*",
                        help=f"zero or more PEI valid bits: {pei_valid_bits}")

        # UEFI N.17 Integer values
        g_pei.add_argument("-m", "--multiple-error", nargs="+",
                        help="Number of errors: 0: Single error, 1: Multiple errors, 2-65535: Error count if known")
        g_pei.add_argument("-e", "--error-info", nargs="+",
                        help="Error information (UEFI 2.10 tables N.18 to N.20)")
        g_pei.add_argument("-p", "--physical-address",  nargs="+",
                        help="Physical address")
        g_pei.add_argument("-v", "--virtual-address",  nargs="+",
                        help="Virtual address")

        # UEFI N.21 Context
        g_ctx = parser.add_argument_group("Processor Context")
        g_ctx.add_argument("--ctx-type", "--context-type", nargs="*",
                        help="Type of the context (0=ARM32 GPR, 5=ARM64 EL1, other values supported)")
        g_ctx.add_argument("--ctx-size", "--context-size", nargs="*",
                        help="Minimal size of the context")
        g_ctx.add_argument("--ctx-array", "--context-array", nargs="*",
                        help="Comma-separated arrays for each context")

        # Vendor-specific data
        g_vendor = parser.add_argument_group("Vendor-specific data")
        g_vendor.add_argument("--vendor", "--vendor-specific", nargs="+",
                        help="Vendor-specific byte arrays of data")

        # Add arguments for Generic Error Data
        qmp.argparse(parser)

        parser.set_defaults(func=self.send_cper)

    def send_cper(self, args):
        """Parse subcommand arguments and send a CPER via QMP"""

        qmp_cmd = qmp(args.host, args.port, args.debug)

        # Handle Generic Error Data arguments if any
        qmp_cmd.set_args(args)

        is_cpu_type = re.compile(r"^([\w+]+\-)?arm\-cpu$")
        cpus = qmp_cmd.search_qom("/machine/unattached/device",
                                  "type", is_cpu_type)

        cper = {}
        pei = {}
        ctx = {}
        vendor = {}

        arg = vars(args)

        # Handle global parameters
        if args.arm:
            arm_valid_init = False
            cper["valid"] = util.get_choice(name="valid",
                                       value=args.arm,
                                       choices=self.arm_valid_bits,
                                       suffixes=["-error", "-err"])
        else:
            cper["valid"] = 0
            arm_valid_init = True

        if "running" in arg:
            if args.running:
                cper["running-state"] = util.bit(0)
            else:
                cper["running-state"] = 0
        else:
            cper["running-state"] = 0

        if arm_valid_init:
            if args.affinity:
                cper["valid"] |= self.arm_valid_bits["affinity"]

            if args.mpidr:
                cper["valid"] |= self.arm_valid_bits["mpidr"]

            if "running-state" in cper:
                cper["valid"] |= self.arm_valid_bits["running"]

            if args.psci:
                cper["valid"] |= self.arm_valid_bits["running"]

        # Handle PEI
        if not args.type:
            args.type = ["cache-error"]

        util.get_mult_choices(
            pei,
            name="valid",
            values=args.pei_valid,
            choices=self.pei_valid_bits,
            suffixes=["-valid", "--addr"],
        )
        util.get_mult_choices(
            pei,
            name="type",
            values=args.type,
            choices=self.pei_error_types,
            suffixes=["-error", "-err"],
        )
        util.get_mult_choices(
            pei,
            name="flags",
            values=args.flags,
            choices=self.pei_flags,
            suffixes=["-error", "-cap"],
        )
        util.get_mult_int(pei, "error-info", args.error_info)
        util.get_mult_int(pei, "multiple-error", args.multiple_error)
        util.get_mult_int(pei, "phy-addr", args.physical_address)
        util.get_mult_int(pei, "virt-addr", args.virtual_address)

        # Handle context
        util.get_mult_int(ctx, "type", args.ctx_type, allow_zero=True)
        util.get_mult_int(ctx, "minimal-size", args.ctx_size, allow_zero=True)
        util.get_mult_array(ctx, "register", args.ctx_array, allow_zero=True)

        util.get_mult_array(vendor, "bytes", args.vendor, max_val=255)

        # Store PEI
        pei_data = bytearray()
        default_flags  = self.pei_flags["first"]
        default_flags |= self.pei_flags["last"]

        error_info_num = 0

        for i, p in pei.items():        # pylint: disable=W0612
            error_info_num += 1

            # UEFI 2.10 doesn't define how to encode error information
            # when multiple types are raised. So, provide a default only
            # if a single type is there
            if "error-info" not in p:
                if p["type"] == util.bit(1):
                    p["error-info"] = 0x0091000F
                if p["type"] == util.bit(2):
                    p["error-info"] = 0x0054007F
                if p["type"] == util.bit(3):
                    p["error-info"] = 0x80D6460FFF
                if p["type"] == util.bit(4):
                    p["error-info"] = 0x78DA03FF

            if "valid" not in p:
                p["valid"] = 0
                if "multiple-error" in p:
                    p["valid"] |= self.pei_valid_bits["multiple-error"]

                if "flags" in p:
                    p["valid"] |= self.pei_valid_bits["flags"]

                if "error-info" in p:
                    p["valid"] |= self.pei_valid_bits["error-info"]

                if "phy-addr" in p:
                    p["valid"] |= self.pei_valid_bits["phy-addr"]

                if "virt-addr" in p:
                    p["valid"] |= self.pei_valid_bits["virt-addr"]

            # Version
            util.data_add(pei_data, 0, 1)

            util.data_add(pei_data,
                         self.ACPI_GHES_ARM_CPER_PEI_LENGTH, 1)

            util.data_add(pei_data, p["valid"], 2)
            util.data_add(pei_data, p["type"], 1)
            util.data_add(pei_data, p.get("multiple-error", 1), 2)
            util.data_add(pei_data, p.get("flags", default_flags), 1)
            util.data_add(pei_data, p.get("error-info", 0), 8)
            util.data_add(pei_data, p.get("virt-addr", 0xDEADBEEF), 8)
            util.data_add(pei_data, p.get("phy-addr", 0xABBA0BAD), 8)

        # Store Context
        ctx_data = bytearray()
        context_info_num = 0

        if ctx:
            ret = qmp_cmd.send_cmd("query-target", may_open=True)

            default_ctx = self.CONTEXT_MISC_REG

            if "arch" in ret:
                if ret["arch"] == "aarch64":
                    default_ctx = self.CONTEXT_AARCH64_EL1
                elif ret["arch"] == "arm":
                    default_ctx = self.CONTEXT_AARCH32_EL1

            for k in sorted(ctx.keys()):
                context_info_num += 1

                if "type" not in ctx[k]:
                    ctx[k]["type"] = default_ctx

                if "register" not in ctx[k]:
                    ctx[k]["register"] = []

                reg_size = len(ctx[k]["register"])
                size = 0

                if "minimal-size" in ctx:
                    size = ctx[k]["minimal-size"]

                size = max(size, reg_size)

                size = (size + 1) % 0xFFFE

                # Version
                util.data_add(ctx_data, 0, 2)

                util.data_add(ctx_data, ctx[k]["type"], 2)

                util.data_add(ctx_data, 8 * size, 4)

                for r in ctx[k]["register"]:
                    util.data_add(ctx_data, r, 8)

                for i in range(reg_size, size):   # pylint: disable=W0612
                    util.data_add(ctx_data, 0, 8)

        # Vendor-specific bytes are not grouped
        vendor_data = bytearray()
        if vendor:
            for k in sorted(vendor.keys()):
                for b in vendor[k]["bytes"]:
                    util.data_add(vendor_data, b, 1)

        # Encode ARM Processor Error
        data = bytearray()

        util.data_add(data, cper["valid"], 4)

        util.data_add(data, error_info_num, 2)
        util.data_add(data, context_info_num, 2)

        # Calculate the length of the CPER data
        cper_length = self.ACPI_GHES_ARM_CPER_LENGTH
        cper_length += len(pei_data)
        cper_length += len(vendor_data)
        cper_length += len(ctx_data)
        util.data_add(data, cper_length, 4)

        util.data_add(data, arg.get("affinity-level", 0), 1)

        # Reserved
        util.data_add(data, 0, 3)

        if "midr-el1" not in arg:
            if cpus:
                cmd_arg = {
                    'path': cpus[0],
                    'property': "midr"
                }
                ret = qmp_cmd.send_cmd("qom-get", cmd_arg, may_open=True)
                if isinstance(ret, int):
                    arg["midr-el1"] = ret

        util.data_add(data, arg.get("mpidr-el1", 0), 8)
        util.data_add(data, arg.get("midr-el1", 0), 8)
        util.data_add(data, cper["running-state"], 4)
        util.data_add(data, arg.get("psci-state", 0), 4)

        # Add PEI
        data.extend(pei_data)
        data.extend(ctx_data)
        data.extend(vendor_data)

        self.data = data

        qmp_cmd.send_cper(cper_guid.CPER_PROC_ARM, self.data)
