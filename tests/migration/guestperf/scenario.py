#
# Migration test scenario parameter description
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


class Scenario(object):

    def __init__(self, name,
                 downtime=500,
                 bandwidth=125000, # 1000 gig-e, effectively unlimited
                 max_iters=30,
                 max_time=300,
                 pause=False, pause_iters=5,
                 post_copy=False, post_copy_iters=5,
                 auto_converge=False, auto_converge_step=10,
                 compression_mt=False, compression_mt_threads=1,
                 compression_xbzrle=False, compression_xbzrle_cache=10,
                 multifd=False, multifd_channels=2):

        self._name = name

        # General migration tunables
        self._downtime = downtime  # milliseconds
        self._bandwidth = bandwidth # MiB per second
        self._max_iters = max_iters
        self._max_time = max_time # seconds


        # Strategies for ensuring completion
        self._pause = pause
        self._pause_iters = pause_iters

        self._post_copy = post_copy
        self._post_copy_iters = post_copy_iters

        self._auto_converge = auto_converge
        self._auto_converge_step = auto_converge_step # percentage CPU time

        self._compression_mt = compression_mt
        self._compression_mt_threads = compression_mt_threads

        self._compression_xbzrle = compression_xbzrle
        self._compression_xbzrle_cache = compression_xbzrle_cache # percentage of guest RAM

        self._multifd = multifd
        self._multifd_channels = multifd_channels

    def serialize(self):
        return {
            "name": self._name,
            "downtime": self._downtime,
            "bandwidth": self._bandwidth,
            "max_iters": self._max_iters,
            "max_time": self._max_time,
            "pause": self._pause,
            "pause_iters": self._pause_iters,
            "post_copy": self._post_copy,
            "post_copy_iters": self._post_copy_iters,
            "auto_converge": self._auto_converge,
            "auto_converge_step": self._auto_converge_step,
            "compression_mt": self._compression_mt,
            "compression_mt_threads": self._compression_mt_threads,
            "compression_xbzrle": self._compression_xbzrle,
            "compression_xbzrle_cache": self._compression_xbzrle_cache,
            "multifd": self._multifd,
            "multifd_channels": self._multifd_channels,
        }

    @classmethod
    def deserialize(cls, data):
        return cls(
            data["name"],
            data["downtime"],
            data["bandwidth"],
            data["max_iters"],
            data["max_time"],
            data["pause"],
            data["pause_iters"],
            data["post_copy"],
            data["post_copy_iters"],
            data["auto_converge"],
            data["auto_converge_step"],
            data["compression_mt"],
            data["compression_mt_threads"],
            data["compression_xbzrle"],
            data["compression_xbzrle_cache"],
            data["multifd"],
            data["multifd_channels"])
