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
from pathlib import Path
from typing import Any, Iterable, Mapping, Optional, Set

try:
    import tomllib
except ImportError:
    import tomli as tomllib


class CargoTOML:
    tomldata: Mapping[Any, Any]
    workspace_data: Mapping[Any, Any]
    check_cfg: Set[str]

    def __init__(self, path: Optional[str], workspace: Optional[str]):
        if path is not None:
            with open(path, 'rb') as f:
                self.tomldata = tomllib.load(f)
        else:
            self.tomldata = {"lints": {"workspace": True}}

        if workspace is not None:
            with open(workspace, 'rb') as f:
                self.workspace_data = tomllib.load(f)
            if "workspace" not in self.workspace_data:
                self.workspace_data["workspace"] = {}

        self.check_cfg = set(self.find_check_cfg())

    def find_check_cfg(self) -> Iterable[str]:
        toml_lints = self.lints
        rust_lints = toml_lints.get("rust", {})
        cfg_lint = rust_lints.get("unexpected_cfgs", {})
        return cfg_lint.get("check-cfg", [])

    @property
    def lints(self) -> Mapping[Any, Any]:
        return self.get_table("lints", True)

    def get_table(self, key: str, can_be_workspace: bool = False) -> Mapping[Any, Any]:
        table = self.tomldata.get(key, {})
        if can_be_workspace and table.get("workspace", False) is True:
            table = self.workspace_data["workspace"].get(key, {})

        return table


def generate_cfg_flags(header: str, cargo_toml: CargoTOML) -> Iterable[str]:
    """Converts defines from config[..].h headers to rustc --cfg flags."""

    with open(header, encoding="utf-8") as cfg:
        config = [l.split()[1:] for l in cfg if l.startswith("#define")]

    cfg_list = []
    for cfg in config:
        name = cfg[0]
        if f'cfg({name})' not in cargo_toml.check_cfg:
            continue
        if len(cfg) >= 2 and cfg[1] != "1":
            continue
        cfg_list.append("--cfg")
        cfg_list.append(name)
    return cfg_list


def main() -> None:
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
    parser.add_argument(
        metavar="TOML_FILE",
        action="store",
        dest="cargo_toml",
        help="path to Cargo.toml file",
        nargs='?',
    )
    parser.add_argument(
        "--workspace",
        metavar="DIR",
        action="store",
        dest="workspace",
        help="path to root of the workspace",
        required=False,
        default=None,
    )
    args = parser.parse_args()
    if args.verbose:
        logging.basicConfig(level=logging.DEBUG)
    logging.debug("args: %s", args)

    if args.workspace:
        workspace_cargo_toml = Path(args.workspace, "Cargo.toml").resolve()
        cargo_toml = CargoTOML(args.cargo_toml, str(workspace_cargo_toml))
    else:
        cargo_toml = CargoTOML(args.cargo_toml, None)

    for header in args.config_headers:
        for tok in generate_cfg_flags(header, cargo_toml):
            print(tok)


if __name__ == "__main__":
    main()
