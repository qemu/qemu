#
# Migration test timing records
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


class TimingRecord(object):

    def __init__(self, tid, timestamp, value):

        self._tid = tid
        self._timestamp = timestamp
        self._value = value

    def serialize(self):
        return {
            "tid": self._tid,
            "timestamp": self._timestamp,
            "value": self._value
        }

    @classmethod
    def deserialize(cls, data):
        return cls(
            data["tid"],
            data["timestamp"],
            data["value"])


class Timings(object):

    def __init__(self, records):

        self._records = records

    def serialize(self):
        return [record.serialize() for record in self._records]

    @classmethod
    def deserialize(cls, data):
        return Timings([TimingRecord.deserialize(record) for record in data])
