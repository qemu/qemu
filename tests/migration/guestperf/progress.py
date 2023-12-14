#
# Migration test migration operation progress
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


class ProgressStats(object):

    def __init__(self,
                 transferred_bytes,
                 remaining_bytes,
                 total_bytes,
                 duplicate_pages,
                 skipped_pages,
                 normal_pages,
                 normal_bytes,
                 dirty_rate_pps,
                 transfer_rate_mbs,
                 iterations):
        self._transferred_bytes = transferred_bytes
        self._remaining_bytes = remaining_bytes
        self._total_bytes = total_bytes
        self._duplicate_pages = duplicate_pages
        self._skipped_pages = skipped_pages
        self._normal_pages = normal_pages
        self._normal_bytes = normal_bytes
        self._dirty_rate_pps = dirty_rate_pps
        self._transfer_rate_mbs = transfer_rate_mbs
        self._iterations = iterations

    def serialize(self):
        return {
            "transferred_bytes": self._transferred_bytes,
            "remaining_bytes": self._remaining_bytes,
            "total_bytes": self._total_bytes,
            "duplicate_pages": self._duplicate_pages,
            "skipped_pages": self._skipped_pages,
            "normal_pages": self._normal_pages,
            "normal_bytes": self._normal_bytes,
            "dirty_rate_pps": self._dirty_rate_pps,
            "transfer_rate_mbs": self._transfer_rate_mbs,
            "iterations": self._iterations,
        }

    @classmethod
    def deserialize(cls, data):
        return cls(
            data["transferred_bytes"],
            data["remaining_bytes"],
            data["total_bytes"],
            data["duplicate_pages"],
            data["skipped_pages"],
            data["normal_pages"],
            data["normal_bytes"],
            data["dirty_rate_pps"],
            data["transfer_rate_mbs"],
            data["iterations"])


class Progress(object):

    def __init__(self,
                 status,
                 ram,
                 now,
                 duration,
                 downtime,
                 downtime_expected,
                 setup_time,
                 throttle_pcent,
                 dirty_limit_throttle_time_per_round,
                 dirty_limit_ring_full_time):

        self._status = status
        self._ram = ram
        self._now = now
        self._duration = duration
        self._downtime = downtime
        self._downtime_expected = downtime_expected
        self._setup_time = setup_time
        self._throttle_pcent = throttle_pcent
        self._dirty_limit_throttle_time_per_round = \
            dirty_limit_throttle_time_per_round
        self._dirty_limit_ring_full_time = \
            dirty_limit_ring_full_time

    def serialize(self):
        return {
            "status": self._status,
            "ram": self._ram.serialize(),
            "now": self._now,
            "duration": self._duration,
            "downtime": self._downtime,
            "downtime_expected": self._downtime_expected,
            "setup_time": self._setup_time,
            "throttle_pcent": self._throttle_pcent,
            "dirty_limit_throttle_time_per_round":
                self._dirty_limit_throttle_time_per_round,
            "dirty_limit_ring_full_time":
                self._dirty_limit_ring_full_time,
        }

    @classmethod
    def deserialize(cls, data):
        return cls(
            data["status"],
            ProgressStats.deserialize(data["ram"]),
            data["now"],
            data["duration"],
            data["downtime"],
            data["downtime_expected"],
            data["setup_time"],
            data["throttle_pcent"],
            data["dirty_limit_throttle_time_per_round"],
            data["dirty_limit_ring_full_time"])
