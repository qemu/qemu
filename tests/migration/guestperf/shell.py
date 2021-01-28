#
# Migration test command line shell integration
#
# Copyright (c) 2016 Red Hat, Inc.
#
# This library is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation; either
# version 2.1 of the License, or (at your option) any later version.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public
# License along with this library; if not, see <http://www.gnu.org/licenses/>.
#


import argparse
import fnmatch
import os
import os.path
import platform
import sys
import logging

from guestperf.hardware import Hardware
from guestperf.engine import Engine
from guestperf.scenario import Scenario
from guestperf.comparison import COMPARISONS
from guestperf.plot import Plot
from guestperf.report import Report


class BaseShell(object):

    def __init__(self):
        parser = argparse.ArgumentParser(description="Migration Test Tool")

        # Test args
        parser.add_argument("--debug", dest="debug", default=False, action="store_true")
        parser.add_argument("--verbose", dest="verbose", default=False, action="store_true")
        parser.add_argument("--sleep", dest="sleep", default=15, type=int)
        parser.add_argument("--binary", dest="binary", default="/usr/bin/qemu-system-x86_64")
        parser.add_argument("--dst-host", dest="dst_host", default="localhost")
        parser.add_argument("--kernel", dest="kernel", default="/boot/vmlinuz-%s" % platform.release())
        parser.add_argument("--initrd", dest="initrd", default="tests/migration/initrd-stress.img")
        parser.add_argument("--transport", dest="transport", default="unix")


        # Hardware args
        parser.add_argument("--cpus", dest="cpus", default=1, type=int)
        parser.add_argument("--mem", dest="mem", default=1, type=int)
        parser.add_argument("--src-cpu-bind", dest="src_cpu_bind", default="")
        parser.add_argument("--src-mem-bind", dest="src_mem_bind", default="")
        parser.add_argument("--dst-cpu-bind", dest="dst_cpu_bind", default="")
        parser.add_argument("--dst-mem-bind", dest="dst_mem_bind", default="")
        parser.add_argument("--prealloc-pages", dest="prealloc_pages", default=False)
        parser.add_argument("--huge-pages", dest="huge_pages", default=False)
        parser.add_argument("--locked-pages", dest="locked_pages", default=False)

        self._parser = parser

    def get_engine(self, args):
        return Engine(binary=args.binary,
                      dst_host=args.dst_host,
                      kernel=args.kernel,
                      initrd=args.initrd,
                      transport=args.transport,
                      sleep=args.sleep,
                      debug=args.debug,
                      verbose=args.verbose)

    def get_hardware(self, args):
        def split_map(value):
            if value == "":
                return []
            return value.split(",")

        return Hardware(cpus=args.cpus,
                        mem=args.mem,

                        src_cpu_bind=split_map(args.src_cpu_bind),
                        src_mem_bind=split_map(args.src_mem_bind),
                        dst_cpu_bind=split_map(args.dst_cpu_bind),
                        dst_mem_bind=split_map(args.dst_mem_bind),

                        locked_pages=args.locked_pages,
                        huge_pages=args.huge_pages,
                        prealloc_pages=args.prealloc_pages)


class Shell(BaseShell):

    def __init__(self):
        super(Shell, self).__init__()

        parser = self._parser

        parser.add_argument("--output", dest="output", default=None)

        # Scenario args
        parser.add_argument("--max-iters", dest="max_iters", default=30, type=int)
        parser.add_argument("--max-time", dest="max_time", default=300, type=int)
        parser.add_argument("--bandwidth", dest="bandwidth", default=125000, type=int)
        parser.add_argument("--downtime", dest="downtime", default=500, type=int)

        parser.add_argument("--pause", dest="pause", default=False, action="store_true")
        parser.add_argument("--pause-iters", dest="pause_iters", default=5, type=int)

        parser.add_argument("--post-copy", dest="post_copy", default=False, action="store_true")
        parser.add_argument("--post-copy-iters", dest="post_copy_iters", default=5, type=int)

        parser.add_argument("--auto-converge", dest="auto_converge", default=False, action="store_true")
        parser.add_argument("--auto-converge-step", dest="auto_converge_step", default=10, type=int)

        parser.add_argument("--compression-mt", dest="compression_mt", default=False, action="store_true")
        parser.add_argument("--compression-mt-threads", dest="compression_mt_threads", default=1, type=int)

        parser.add_argument("--compression-xbzrle", dest="compression_xbzrle", default=False, action="store_true")
        parser.add_argument("--compression-xbzrle-cache", dest="compression_xbzrle_cache", default=10, type=int)

    def get_scenario(self, args):
        return Scenario(name="perfreport",
                        downtime=args.downtime,
                        bandwidth=args.bandwidth,
                        max_iters=args.max_iters,
                        max_time=args.max_time,

                        pause=args.pause,
                        pause_iters=args.pause_iters,

                        post_copy=args.post_copy,
                        post_copy_iters=args.post_copy_iters,

                        auto_converge=args.auto_converge,
                        auto_converge_step=args.auto_converge_step,

                        compression_mt=args.compression_mt,
                        compression_mt_threads=args.compression_mt_threads,

                        compression_xbzrle=args.compression_xbzrle,
                        compression_xbzrle_cache=args.compression_xbzrle_cache)

    def run(self, argv):
        args = self._parser.parse_args(argv)
        logging.basicConfig(level=(logging.DEBUG if args.debug else
                                   logging.INFO if args.verbose else
                                   logging.WARN))


        engine = self.get_engine(args)
        hardware = self.get_hardware(args)
        scenario = self.get_scenario(args)

        try:
            report = engine.run(hardware, scenario)
            if args.output is None:
                print(report.to_json())
            else:
                with open(args.output, "w") as fh:
                    print(report.to_json(), file=fh)
            return 0
        except Exception as e:
            print("Error: %s" % str(e), file=sys.stderr)
            if args.debug:
                raise
            return 1


class BatchShell(BaseShell):

    def __init__(self):
        super(BatchShell, self).__init__()

        parser = self._parser

        parser.add_argument("--filter", dest="filter", default="*")
        parser.add_argument("--output", dest="output", default=os.getcwd())

    def run(self, argv):
        args = self._parser.parse_args(argv)
        logging.basicConfig(level=(logging.DEBUG if args.debug else
                                   logging.INFO if args.verbose else
                                   logging.WARN))


        engine = self.get_engine(args)
        hardware = self.get_hardware(args)

        try:
            for comparison in COMPARISONS:
                compdir = os.path.join(args.output, comparison._name)
                for scenario in comparison._scenarios:
                    name = os.path.join(comparison._name, scenario._name)
                    if not fnmatch.fnmatch(name, args.filter):
                        if args.verbose:
                            print("Skipping %s" % name)
                        continue

                    if args.verbose:
                        print("Running %s" % name)

                    dirname = os.path.join(args.output, comparison._name)
                    filename = os.path.join(dirname, scenario._name + ".json")
                    if not os.path.exists(dirname):
                        os.makedirs(dirname)
                    report = engine.run(hardware, scenario)
                    with open(filename, "w") as fh:
                        print(report.to_json(), file=fh)
        except Exception as e:
            print("Error: %s" % str(e), file=sys.stderr)
            if args.debug:
                raise


class PlotShell(object):

    def __init__(self):
        super(PlotShell, self).__init__()

        self._parser = argparse.ArgumentParser(description="Migration Test Tool")

        self._parser.add_argument("--output", dest="output", default=None)

        self._parser.add_argument("--debug", dest="debug", default=False, action="store_true")
        self._parser.add_argument("--verbose", dest="verbose", default=False, action="store_true")

        self._parser.add_argument("--migration-iters", dest="migration_iters", default=False, action="store_true")
        self._parser.add_argument("--total-guest-cpu", dest="total_guest_cpu", default=False, action="store_true")
        self._parser.add_argument("--split-guest-cpu", dest="split_guest_cpu", default=False, action="store_true")
        self._parser.add_argument("--qemu-cpu", dest="qemu_cpu", default=False, action="store_true")
        self._parser.add_argument("--vcpu-cpu", dest="vcpu_cpu", default=False, action="store_true")

        self._parser.add_argument("reports", nargs='*')

    def run(self, argv):
        args = self._parser.parse_args(argv)
        logging.basicConfig(level=(logging.DEBUG if args.debug else
                                   logging.INFO if args.verbose else
                                   logging.WARN))


        if len(args.reports) == 0:
            print("At least one report required", file=sys.stderr)
            return 1

        if not (args.qemu_cpu or
                args.vcpu_cpu or
                args.total_guest_cpu or
                args.split_guest_cpu):
            print("At least one chart type is required", file=sys.stderr)
            return 1

        reports = []
        for report in args.reports:
            reports.append(Report.from_json_file(report))

        plot = Plot(reports,
                    args.migration_iters,
                    args.total_guest_cpu,
                    args.split_guest_cpu,
                    args.qemu_cpu,
                    args.vcpu_cpu)

        plot.generate(args.output)
