#
# Migration test hardware configuration description
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


class Hardware(object):
    def __init__(self, cpus=1, mem=1,
                 src_cpu_bind=None, src_mem_bind=None,
                 dst_cpu_bind=None, dst_mem_bind=None,
                 prealloc_pages = False,
                 huge_pages=False, locked_pages=False,
                 dirty_ring_size=0):
        self._cpus = cpus
        self._mem = mem # GiB
        self._src_mem_bind = src_mem_bind # List of NUMA nodes
        self._src_cpu_bind = src_cpu_bind # List of pCPUs
        self._dst_mem_bind = dst_mem_bind # List of NUMA nodes
        self._dst_cpu_bind = dst_cpu_bind # List of pCPUs
        self._prealloc_pages = prealloc_pages
        self._huge_pages = huge_pages
        self._locked_pages = locked_pages
        self._dirty_ring_size = dirty_ring_size


    def serialize(self):
        return {
            "cpus": self._cpus,
            "mem": self._mem,
            "src_mem_bind": self._src_mem_bind,
            "dst_mem_bind": self._dst_mem_bind,
            "src_cpu_bind": self._src_cpu_bind,
            "dst_cpu_bind": self._dst_cpu_bind,
            "prealloc_pages": self._prealloc_pages,
            "huge_pages": self._huge_pages,
            "locked_pages": self._locked_pages,
            "dirty_ring_size": self._dirty_ring_size,
        }

    @classmethod
    def deserialize(cls, data):
        return cls(
            data["cpus"],
            data["mem"],
            data["src_cpu_bind"],
            data["src_mem_bind"],
            data["dst_cpu_bind"],
            data["dst_mem_bind"],
            data["prealloc_pages"],
            data["huge_pages"],
            data["locked_pages"],
            data["dirty_ring_size"])
