#
# Migration test output result reporting
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

import json

from guestperf.hardware import Hardware
from guestperf.scenario import Scenario
from guestperf.progress import Progress
from guestperf.timings import Timings

class Report(object):

    def __init__(self,
                 hardware,
                 scenario,
                 progress_history,
                 guest_timings,
                 qemu_timings,
                 vcpu_timings,
                 binary,
                 dst_host,
                 kernel,
                 initrd,
                 transport,
                 sleep):

        self._hardware = hardware
        self._scenario = scenario
        self._progress_history = progress_history
        self._guest_timings = guest_timings
        self._qemu_timings = qemu_timings
        self._vcpu_timings = vcpu_timings
        self._binary = binary
        self._dst_host = dst_host
        self._kernel = kernel
        self._initrd = initrd
        self._transport = transport
        self._sleep = sleep

    def serialize(self):
        return {
            "hardware": self._hardware.serialize(),
            "scenario": self._scenario.serialize(),
            "progress_history": [progress.serialize() for progress in self._progress_history],
            "guest_timings": self._guest_timings.serialize(),
            "qemu_timings": self._qemu_timings.serialize(),
            "vcpu_timings": self._vcpu_timings.serialize(),
            "binary": self._binary,
            "dst_host": self._dst_host,
            "kernel": self._kernel,
            "initrd": self._initrd,
            "transport": self._transport,
            "sleep": self._sleep,
        }

    @classmethod
    def deserialize(cls, data):
        return cls(
            Hardware.deserialize(data["hardware"]),
            Scenario.deserialize(data["scenario"]),
            [Progress.deserialize(record) for record in data["progress_history"]],
            Timings.deserialize(data["guest_timings"]),
            Timings.deserialize(data["qemu_timings"]),
            Timings.deserialize(data["vcpu_timings"]),
            data["binary"],
            data["dst_host"],
            data["kernel"],
            data["initrd"],
            data["transport"],
            data["sleep"])

    def to_json(self):
        return json.dumps(self.serialize(), indent=4)

    @classmethod
    def from_json(cls, data):
        return cls.deserialize(json.loads(data))

    @classmethod
    def from_json_file(cls, filename):
        with open(filename, "r") as fh:
            return cls.deserialize(json.load(fh))
