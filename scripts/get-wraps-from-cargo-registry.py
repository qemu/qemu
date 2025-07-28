#!/usr/bin/env python3

# SPDX-License-Identifier: GPL-2.0-or-later

"""
get-wraps-from-cargo-registry.py - Update Meson subprojects from a global registry
"""

# Copyright (C) 2025 Red Hat, Inc.
#
# Author: Paolo Bonzini <pbonzini@redhat.com>

import argparse
import configparser
import filecmp
import glob
import os
import subprocess
import sys


def get_name_and_semver(namever: str) -> tuple[str, str]:
    """Split a subproject name into its name and semantic version parts"""
    parts = namever.rsplit("-", 1)
    if len(parts) != 2:
        return namever, ""

    return parts[0], parts[1]


class UpdateSubprojects:
    cargo_registry: str
    top_srcdir: str
    dry_run: bool
    changes: int = 0

    def find_installed_crate(self, namever: str) -> str | None:
        """Find installed crate matching name and semver prefix"""
        name, semver = get_name_and_semver(namever)

        # exact version match
        path = os.path.join(self.cargo_registry, f"{name}-{semver}")
        if os.path.exists(path):
            return f"{name}-{semver}"

        # semver match
        matches = sorted(glob.glob(f"{path}.*"))
        return os.path.basename(matches[0]) if matches else None

    def compare_build_rs(self, orig_dir: str, registry_namever: str) -> None:
        """Warn if the build.rs in the original directory differs from the registry version."""
        orig_build_rs = os.path.join(orig_dir, "build.rs")
        new_build_rs = os.path.join(self.cargo_registry, registry_namever, "build.rs")

        msg = None
        if os.path.isfile(orig_build_rs) != os.path.isfile(new_build_rs):
            if os.path.isfile(orig_build_rs):
                msg = f"build.rs removed in {registry_namever}"
            if os.path.isfile(new_build_rs):
                msg = f"build.rs added in {registry_namever}"

        elif os.path.isfile(orig_build_rs) and not filecmp.cmp(orig_build_rs, new_build_rs):
            msg = f"build.rs changed from {orig_dir} to {registry_namever}"

        if msg:
            print(f"⚠️  Warning: {msg}")
            print("   This may affect the build process - please review the differences.")

    def update_subproject(self, wrap_file: str, registry_namever: str) -> None:
        """Modify [wrap-file] section to point to self.cargo_registry."""
        assert wrap_file.endswith("-rs.wrap")
        wrap_name = wrap_file[:-5]

        env = os.environ.copy()
        env["MESON_PACKAGE_CACHE_DIR"] = self.cargo_registry

        config = configparser.ConfigParser()
        config.read(wrap_file)
        if "wrap-file" not in config:
            return

        # do not download the wrap, always use the local copy
        orig_dir = config["wrap-file"]["directory"]
        if os.path.exists(orig_dir) and orig_dir != registry_namever:
            self.compare_build_rs(orig_dir, registry_namever)

        if self.dry_run:
            if orig_dir == registry_namever:
                print(f"Will install {orig_dir} from registry.")
            else:
                print(f"Will replace {orig_dir} with {registry_namever}.")
            self.changes += 1
            return

        config["wrap-file"]["directory"] = registry_namever
        for key in list(config["wrap-file"].keys()):
            if key.startswith("source"):
                del config["wrap-file"][key]

        # replace existing directory with installed version
        if os.path.exists(orig_dir):
            subprocess.run(
                ["meson", "subprojects", "purge", "--confirm", wrap_name],
                cwd=self.top_srcdir,
                env=env,
                check=True,
            )

        with open(wrap_file, "w") as f:
            config.write(f)

        if orig_dir == registry_namever:
            print(f"Installing {orig_dir} from registry.")
        else:
            print(f"Replacing {orig_dir} with {registry_namever}.")
            patch_dir = config["wrap-file"]["patch_directory"]
            patch_dir = os.path.join("packagefiles", patch_dir)
            _, ver = registry_namever.rsplit("-", 1)
            subprocess.run(
                ["meson", "rewrite", "kwargs", "set", "project", "/", "version", ver],
                cwd=patch_dir,
                env=env,
                check=True,
            )

        subprocess.run(
            ["meson", "subprojects", "download", wrap_name],
            cwd=self.top_srcdir,
            env=env,
            check=True,
        )
        self.changes += 1

    @staticmethod
    def parse_cmdline() -> argparse.Namespace:
        parser = argparse.ArgumentParser(
            description="Replace Meson subprojects with packages in a Cargo registry"
        )
        parser.add_argument(
            "--cargo-registry",
            default=os.environ.get("CARGO_REGISTRY"),
            help="Path to Cargo registry (default: CARGO_REGISTRY env var)",
        )
        parser.add_argument(
            "--dry-run",
            action="store_true",
            default=False,
            help="Do not actually replace anything",
        )

        args = parser.parse_args()
        if not args.cargo_registry:
            print("error: CARGO_REGISTRY environment variable not set and --cargo-registry not provided")
            sys.exit(1)

        return args

    def __init__(self, args: argparse.Namespace):
        self.cargo_registry = args.cargo_registry
        self.dry_run = args.dry_run
        self.top_srcdir = os.getcwd()

    def main(self) -> None:
        if not os.path.exists("subprojects"):
            print("'subprojects' directory not found, nothing to do.")
            return

        os.chdir("subprojects")
        for wrap_file in sorted(glob.glob("*-rs.wrap")):
            namever = wrap_file[:-8]  # Remove '-rs.wrap'

            registry_namever = self.find_installed_crate(namever)
            if not registry_namever:
                print(f"No installed crate found for {wrap_file}")
                continue

            self.update_subproject(wrap_file, registry_namever)

        if self.changes:
            if self.dry_run:
                print("Rerun without --dry-run to apply changes.")
            else:
                print(f"✨ {self.changes} subproject(s) updated!")
        else:
            print("No changes.")


if __name__ == "__main__":
    args = UpdateSubprojects.parse_cmdline()
    UpdateSubprojects(args).main()
