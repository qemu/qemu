#
# Migration test scenario comparison mapping
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

from guestperf.scenario import Scenario

class Comparison(object):
    def __init__(self, name, scenarios):
        self._name = name
        self._scenarios = scenarios

COMPARISONS = [
    # Looking at effect of pausing guest during migration
    # at various stages of iteration over RAM
    Comparison("pause-iters", scenarios = [
        Scenario("pause-iters-0",
                 pause=True, pause_iters=0),
        Scenario("pause-iters-1",
                 pause=True, pause_iters=1),
        Scenario("pause-iters-5",
                 pause=True, pause_iters=5),
        Scenario("pause-iters-20",
                 pause=True, pause_iters=20),
    ]),


    # Looking at use of post-copy in relation to bandwidth
    # available for migration
    Comparison("post-copy-bandwidth", scenarios = [
        Scenario("post-copy-bw-100mbs",
                 post_copy=True, bandwidth=12),
        Scenario("post-copy-bw-300mbs",
                 post_copy=True, bandwidth=37),
        Scenario("post-copy-bw-1gbs",
                 post_copy=True, bandwidth=125),
        Scenario("post-copy-bw-10gbs",
                 post_copy=True, bandwidth=1250),
        Scenario("post-copy-bw-100gbs",
                 post_copy=True, bandwidth=12500),
    ]),


    # Looking at effect of starting post-copy at different
    # stages of the migration
    Comparison("post-copy-iters", scenarios = [
        Scenario("post-copy-iters-0",
                 post_copy=True, post_copy_iters=0),
        Scenario("post-copy-iters-1",
                 post_copy=True, post_copy_iters=1),
        Scenario("post-copy-iters-5",
                 post_copy=True, post_copy_iters=5),
        Scenario("post-copy-iters-20",
                 post_copy=True, post_copy_iters=20),
    ]),


    # Looking at effect of auto-converge with different
    # throttling percentage step rates
    Comparison("auto-converge-iters", scenarios = [
        Scenario("auto-converge-step-5",
                 auto_converge=True, auto_converge_step=5),
        Scenario("auto-converge-step-10",
                 auto_converge=True, auto_converge_step=10),
        Scenario("auto-converge-step-20",
                 auto_converge=True, auto_converge_step=20),
    ]),


    # Looking at use of auto-converge in relation to bandwidth
    # available for migration
    Comparison("auto-converge-bandwidth", scenarios = [
        Scenario("auto-converge-bw-100mbs",
                 auto_converge=True, bandwidth=12),
        Scenario("auto-converge-bw-300mbs",
                 auto_converge=True, bandwidth=37),
        Scenario("auto-converge-bw-1gbs",
                 auto_converge=True, bandwidth=125),
        Scenario("auto-converge-bw-10gbs",
                 auto_converge=True, bandwidth=1250),
        Scenario("auto-converge-bw-100gbs",
                 auto_converge=True, bandwidth=12500),
    ]),


    # Looking at effect of multi-thread compression with
    # varying numbers of threads
    Comparison("compr-mt", scenarios = [
        Scenario("compr-mt-threads-1",
                 compression_mt=True, compression_mt_threads=1),
        Scenario("compr-mt-threads-2",
                 compression_mt=True, compression_mt_threads=2),
        Scenario("compr-mt-threads-4",
                 compression_mt=True, compression_mt_threads=4),
    ]),


    # Looking at effect of xbzrle compression with varying
    # cache sizes
    Comparison("compr-xbzrle", scenarios = [
        Scenario("compr-xbzrle-cache-5",
                 compression_xbzrle=True, compression_xbzrle_cache=5),
        Scenario("compr-xbzrle-cache-10",
                 compression_xbzrle=True, compression_xbzrle_cache=10),
        Scenario("compr-xbzrle-cache-20",
                 compression_xbzrle=True, compression_xbzrle_cache=10),
        Scenario("compr-xbzrle-cache-50",
                 compression_xbzrle=True, compression_xbzrle_cache=50),
    ]),
]
