#!/usr/bin/env python3

# SPDX-License-Identifier: GPL-2.0-or-later

"""
get-wraps-from-cargo-registry.py - Update Meson subprojects from a Cargo
registry or from the versions pinned in Cargo.lock.
"""

# Copyright (C) 2025 Red Hat, Inc.
#
# Author: Paolo Bonzini <pbonzini@redhat.com>

import argparse
import configparser
import filecmp
import glob
import os
import shutil
import subprocess
import sys
import tomllib


def get_name_and_semver(namever: str) -> tuple[str, str]:
    """Split a subproject name into its name and semantic version parts"""
    parts = namever.rsplit("-", 1)
    if len(parts) != 2:
        return namever, ""

    return parts[0], parts[1]


class CrateSource:
    """Class for locating crate versions and pointing wrap files at them.
       Subclasses know where the source of a crate comes from and how to
       rewrite the ``[wrap-file]`` to consume it."""

    origin: str

    def find(self, namever: str) -> str | None:
        """Resolve a 'name-semver' prefix to a concrete 'name-version'."""
        raise NotImplementedError

    def rewrite_source(self, section: configparser.SectionProxy, orig_namever: str, source_namever: str) -> bool:
        """Update the download-related keys of a [wrap-file] section."""
        raise NotImplementedError


class CargoRegistry(CrateSource):
    """Locate crates already extracted in a local Cargo registry directory."""

    origin = "the Cargo registry"

    def __init__(self, path: str):
        self.path = path

    def find(self, namever: str) -> str | None:
        """Find installed crate matching name and semver prefix"""
        name, semver = get_name_and_semver(namever)

        # exact version match
        path = os.path.join(self.path, f"{name}-{semver}")
        if os.path.exists(path):
            return f"{name}-{semver}"

        # semver match
        matches = sorted(glob.glob(f"{path}.*"))
        return os.path.basename(matches[0]) if matches else None

    def rewrite_source(self, section: configparser.SectionProxy, orig_namever: str, registry_namever: str) -> bool:
        # the registry already holds the extracted sources, so Meson does not
        # download anything: drop the source_* keys.
        for key in list(section.keys()):
            if key.startswith("source"):
                del section[key]
        return True


class CargoLock(CrateSource):
    """Locate crates by the versions pinned in a Cargo.lock file."""

    origin = "crates.io"

    def __init__(self, path: str):
        with open(path, "rb") as f:
            data = tomllib.load(f)

        self.versions: dict[str, list[str]] = {}
        self.checksums: dict[str, str] = {}
        for pkg in data.get("package", []):
            # workspace members have neither a checksum nor a crates.io tarball
            if "checksum" not in pkg:
                continue
            name, version = pkg["name"], pkg["version"]
            self.versions.setdefault(name, []).append(version)
            self.checksums[f"{name}-{version}"] = pkg["checksum"]

    def find(self, namever: str) -> str | None:
        """Find pinned crate matching name and semver prefix"""
        name, semver = get_name_and_semver(namever)
        versions = sorted(self.versions.get(name, []))

        # exact version match
        if semver in versions:
            return f"{name}-{semver}"

        # semver match
        matches = [v for v in versions if v.startswith(f"{semver}.")]
        return f"{name}-{matches[0]}" if matches else None

    def rewrite_source(self, section: configparser.SectionProxy, orig_namever: str, new_namever: str) -> bool:
        # rewrite the download keys to fetch the pinned version from crates.io.
        if orig_namever == new_namever:
            return False
        name, version = get_name_and_semver(new_namever)
        section["source_url"] = f"https://crates.io/api/v1/crates/{name}/{version}/download"
        section["source_filename"] = f"{new_namever}.tar.gz"
        section["source_hash"] = self.checksums[new_namever]
        return True


class UpdateSubprojects:
    cargo_registry: str
    source: CrateSource
    top_srcdir: str
    dry_run: bool
    changes: int = 0

    def compare_build_rs(self, orig_dir: str, source_namever: str) -> None:
        """Warn if the build.rs in the original directory differs from the registry version."""
        orig_build_rs = os.path.join(orig_dir, "build.rs")
        new_build_rs = os.path.join(source_namever, "build.rs")

        msg = None
        if os.path.isfile(orig_build_rs) != os.path.isfile(new_build_rs):
            if os.path.isfile(orig_build_rs):
                msg = f"build.rs removed in {source_namever}"
            if os.path.isfile(new_build_rs):
                msg = f"build.rs added in {source_namever}"

        elif os.path.isfile(orig_build_rs) and not filecmp.cmp(orig_build_rs, new_build_rs, shallow=False):
            msg = f"build.rs changed from {orig_dir} to {source_namever}"
            # diff exits non-zero when the files differ, which is expected here
            subprocess.run(["diff", "-u", orig_build_rs, new_build_rs])

        if msg:
            print(f"⚠️  Warning: {msg}")
            print("   This may affect the build process - please review the differences.")

    def update_subproject(self, wrap_file: str, source_namever: str) -> None:
        """Modify [wrap-file] section to use the crate resolved as `source_namever`."""
        assert wrap_file.endswith("-rs.wrap")
        wrap_name = wrap_file[:-5]

        env = os.environ.copy()
        if self.cargo_registry:
            env["MESON_PACKAGE_CACHE_DIR"] = self.cargo_registry

        config = configparser.ConfigParser()
        config.read(wrap_file)
        if "wrap-file" not in config:
            return

        section = config["wrap-file"]
        orig_dir = section["directory"]

        if self.dry_run:
            if orig_dir != source_namever:
                print(f"Will replace {orig_dir} with {source_namever}.")
            elif not os.path.exists(orig_dir) or self.cargo_registry:
                print(f"Will install {orig_dir} from {self.source.origin}.")
            else:
                print(f"Will update {orig_dir} from cache.")
                return
            self.changes += 1
            return

        section["directory"] = source_namever
        if self.source.rewrite_source(section, orig_dir, source_namever):
            with open(wrap_file, "w") as f:
                config.write(f)

        with open(wrap_file, "w") as f:
            config.write(f)

        if orig_dir != source_namever:
            print(f"👉 Replacing {orig_dir} with {source_namever}.")
        elif not os.path.exists(orig_dir) or self.cargo_registry:
            print(f"👉 Installing {orig_dir} from {self.source.origin}.")
        else:
            print(f"👉 Updating {orig_dir} from cache.")
            subprocess.run(
                ["meson", "subprojects", "update", "--reset", wrap_name],
                cwd=self.top_srcdir,
                env=env,
                check=True,
            )
            return

        subprocess.run(
            ["meson", "subprojects", "download", wrap_name],
            cwd=self.top_srcdir,
            env=env,
            check=True,
        )
        self.changes += 1

        if os.path.exists(orig_dir) and orig_dir != source_namever:
            self.compare_build_rs(orig_dir, source_namever)
            shutil.rmtree(orig_dir)

    @staticmethod
    def parse_cmdline() -> argparse.Namespace:
        parser = argparse.ArgumentParser(
            description="Replace Meson subprojects with packages in a Cargo registry"
        )
        parser.add_argument(
            "--cargo-lock",
            action='store_true',
            default=False,
            help="Update wraps from Cargo.lock",
        )
        parser.add_argument(
            "--cargo-registry",
            default=None,
            help="Path to Cargo registry (default: CARGO_REGISTRY env var)",
        )
        parser.add_argument(
            "--dry-run",
            action="store_true",
            default=False,
            help="Do not actually replace anything",
        )

        args = parser.parse_args()
        if args.cargo_registry and args.cargo_lock:
            print("error: --cargo-registry and --cargo-lock are incompatible")
            sys.exit(1)
        if not args.cargo_registry and not args.cargo_lock:
            args.cargo_registry = os.environ.get("CARGO_REGISTRY")
            if not args.cargo_registry:
                print("error: CARGO_REGISTRY environment variable not set and " +
                      "--cargo-registry or --cargo-lock not provided")
                sys.exit(1)

        return args

    def __init__(self, args: argparse.Namespace):
        self.cargo_registry = args.cargo_registry
        self.dry_run = args.dry_run
        self.top_srcdir = os.getcwd()
        if args.cargo_lock:
            self.source = CargoLock(os.path.join(self.top_srcdir, "Cargo.lock"))
        else:
            self.source = CargoRegistry(args.cargo_registry)

    def main(self) -> None:
        if not os.path.exists("subprojects"):
            print("'subprojects' directory not found, nothing to do.")
            return

        os.chdir("subprojects")
        for wrap_file in sorted(glob.glob("*-rs.wrap")):
            namever = wrap_file[:-8]  # Remove '-rs.wrap'

            source_namever = self.source.find(namever)
            if not source_namever:
                print(f"No crate found for {wrap_file}")
                continue

            self.update_subproject(wrap_file, source_namever)

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
