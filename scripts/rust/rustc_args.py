#!/usr/bin/env python3

"""Generate rustc arguments for meson rust builds.

This program generates --cfg compile flags for the configuration headers passed
as arguments.

Copyright (c) 2024 Linaro Ltd.

Authors:
 Manos Pitsidianakis <manos.pitsidianakis@linaro.org>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
"""

import argparse
import logging

from typing import List


def generate_cfg_flags(header: str) -> List[str]:
    """Converts defines from config[..].h headers to rustc --cfg flags."""

    def cfg_name(name: str) -> str:
        """Filter function for C #defines"""
        if (
            name.startswith("CONFIG_")
            or name.startswith("TARGET_")
            or name.startswith("HAVE_")
        ):
            return name
        return ""

    with open(header, encoding="utf-8") as cfg:
        config = [l.split()[1:] for l in cfg if l.startswith("#define")]

    cfg_list = []
    for cfg in config:
        name = cfg_name(cfg[0])
        if not name:
            continue
        if len(cfg) >= 2 and cfg[1] != "1":
            continue
        cfg_list.append("--cfg")
        cfg_list.append(name)
    return cfg_list


def main() -> None:
    # pylint: disable=missing-function-docstring
    parser = argparse.ArgumentParser()
    parser.add_argument("-v", "--verbose", action="store_true")
    parser.add_argument(
        "--config-headers",
        metavar="CONFIG_HEADER",
        action="append",
        dest="config_headers",
        help="paths to any configuration C headers (*.h files), if any",
        required=False,
        default=[],
    )
    args = parser.parse_args()
    if args.verbose:
        logging.basicConfig(level=logging.DEBUG)
    logging.debug("args: %s", args)
    for header in args.config_headers:
        for tok in generate_cfg_flags(header):
            print(tok)


if __name__ == "__main__":
    main()
