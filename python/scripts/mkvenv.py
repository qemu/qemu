"""
mkvenv - QEMU pyvenv bootstrapping utility

usage: mkvenv [-h] command ...

QEMU pyvenv bootstrapping utility

options:
  -h, --help  show this help message and exit

Commands:
  command     Description
    create    create a venv

--------------------------------------------------

usage: mkvenv create [-h] target

positional arguments:
  target      Target directory to install virtual environment into.

options:
  -h, --help  show this help message and exit

"""

# Copyright (C) 2022-2023 Red Hat, Inc.
#
# Authors:
#  John Snow <jsnow@redhat.com>
#  Paolo Bonzini <pbonzini@redhat.com>
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later. See the COPYING file in the top-level directory.

import argparse
from importlib.util import find_spec
import logging
import os
from pathlib import Path
import site
import subprocess
import sys
import sysconfig
from types import SimpleNamespace
from typing import Any, Optional, Union
import venv


# Do not add any mandatory dependencies from outside the stdlib:
# This script *must* be usable standalone!

DirType = Union[str, bytes, "os.PathLike[str]", "os.PathLike[bytes]"]
logger = logging.getLogger("mkvenv")


def inside_a_venv() -> bool:
    """Returns True if it is executed inside of a virtual environment."""
    return sys.prefix != sys.base_prefix


class Ouch(RuntimeError):
    """An Exception class we can't confuse with a builtin."""


class QemuEnvBuilder(venv.EnvBuilder):
    """
    An extension of venv.EnvBuilder for building QEMU's configure-time venv.

    The primary difference is that it emulates a "nested" virtual
    environment when invoked from inside of an existing virtual
    environment by including packages from the parent.

    Parameters for base class init:
      - system_site_packages: bool = False
      - clear: bool = False
      - symlinks: bool = False
      - upgrade: bool = False
      - with_pip: bool = False
      - prompt: Optional[str] = None
      - upgrade_deps: bool = False             (Since 3.9)
    """

    def __init__(self, *args: Any, **kwargs: Any) -> None:
        logger.debug("QemuEnvBuilder.__init__(...)")

        # For nested venv emulation:
        self.use_parent_packages = False
        if inside_a_venv():
            # Include parent packages only if we're in a venv and
            # system_site_packages was True.
            self.use_parent_packages = kwargs.pop(
                "system_site_packages", False
            )
            # Include system_site_packages only when the parent,
            # The venv we are currently in, also does so.
            kwargs["system_site_packages"] = sys.base_prefix in site.PREFIXES

        if kwargs.get("with_pip", False):
            check_ensurepip()

        super().__init__(*args, **kwargs)

        # Make the context available post-creation:
        self._context: Optional[SimpleNamespace] = None

    def get_parent_libpath(self) -> Optional[str]:
        """Return the libpath of the parent venv, if applicable."""
        if self.use_parent_packages:
            return sysconfig.get_path("purelib")
        return None

    @staticmethod
    def compute_venv_libpath(context: SimpleNamespace) -> str:
        """
        Compatibility wrapper for context.lib_path for Python < 3.12
        """
        # Python 3.12+, not strictly necessary because it's documented
        # to be the same as 3.10 code below:
        if sys.version_info >= (3, 12):
            return context.lib_path

        # Python 3.10+
        if "venv" in sysconfig.get_scheme_names():
            lib_path = sysconfig.get_path(
                "purelib", scheme="venv", vars={"base": context.env_dir}
            )
            assert lib_path is not None
            return lib_path

        # For Python <= 3.9 we need to hardcode this. Fortunately the
        # code below was the same in Python 3.6-3.10, so there is only
        # one case.
        if sys.platform == "win32":
            return os.path.join(context.env_dir, "Lib", "site-packages")
        return os.path.join(
            context.env_dir,
            "lib",
            "python%d.%d" % sys.version_info[:2],
            "site-packages",
        )

    def ensure_directories(self, env_dir: DirType) -> SimpleNamespace:
        logger.debug("ensure_directories(env_dir=%s)", env_dir)
        self._context = super().ensure_directories(env_dir)
        return self._context

    def create(self, env_dir: DirType) -> None:
        logger.debug("create(env_dir=%s)", env_dir)
        super().create(env_dir)
        assert self._context is not None
        self.post_post_setup(self._context)

    def post_post_setup(self, context: SimpleNamespace) -> None:
        """
        The final, final hook. Enter the venv and run commands inside of it.
        """
        if self.use_parent_packages:
            # We're inside of a venv and we want to include the parent
            # venv's packages.
            parent_libpath = self.get_parent_libpath()
            assert parent_libpath is not None
            logger.debug("parent_libpath: %s", parent_libpath)

            our_libpath = self.compute_venv_libpath(context)
            logger.debug("our_libpath: %s", our_libpath)

            pth_file = os.path.join(our_libpath, "nested.pth")
            with open(pth_file, "w", encoding="UTF-8") as file:
                file.write(parent_libpath + os.linesep)

    def get_value(self, field: str) -> str:
        """
        Get a string value from the context namespace after a call to build.

        For valid field names, see:
        https://docs.python.org/3/library/venv.html#venv.EnvBuilder.ensure_directories
        """
        ret = getattr(self._context, field)
        assert isinstance(ret, str)
        return ret


def check_ensurepip() -> None:
    """
    Check that we have ensurepip.

    Raise a fatal exception with a helpful hint if it isn't available.
    """
    if not find_spec("ensurepip"):
        msg = (
            "Python's ensurepip module is not found.\n"
            "It's normally part of the Python standard library, "
            "maybe your distribution packages it separately?\n"
            "Either install ensurepip, or alleviate the need for it in the "
            "first place by installing pip and setuptools for "
            f"'{sys.executable}'.\n"
            "(Hint: Debian puts ensurepip in its python3-venv package.)"
        )
        raise Ouch(msg)

    # ensurepip uses pyexpat, which can also go missing on us:
    if not find_spec("pyexpat"):
        msg = (
            "Python's pyexpat module is not found.\n"
            "It's normally part of the Python standard library, "
            "maybe your distribution packages it separately?\n"
            "Either install pyexpat, or alleviate the need for it in the "
            "first place by installing pip and setuptools for "
            f"'{sys.executable}'.\n\n"
            "(Hint: NetBSD's pkgsrc debundles this to e.g. 'py310-expat'.)"
        )
        raise Ouch(msg)


def make_venv(  # pylint: disable=too-many-arguments
    env_dir: Union[str, Path],
    system_site_packages: bool = False,
    clear: bool = True,
    symlinks: Optional[bool] = None,
    with_pip: bool = True,
) -> None:
    """
    Create a venv using `QemuEnvBuilder`.

    This is analogous to the `venv.create` module-level convenience
    function that is part of the Python stdblib, except it uses
    `QemuEnvBuilder` instead.

    :param env_dir: The directory to create/install to.
    :param system_site_packages:
        Allow inheriting packages from the system installation.
    :param clear: When True, fully remove any prior venv and files.
    :param symlinks:
        Whether to use symlinks to the target interpreter or not. If
        left unspecified, it will use symlinks except on Windows to
        match behavior with the "venv" CLI tool.
    :param with_pip:
        Whether to install "pip" binaries or not.
    """
    logger.debug(
        "%s: make_venv(env_dir=%s, system_site_packages=%s, "
        "clear=%s, symlinks=%s, with_pip=%s)",
        __file__,
        str(env_dir),
        system_site_packages,
        clear,
        symlinks,
        with_pip,
    )

    if symlinks is None:
        # Default behavior of standard venv CLI
        symlinks = os.name != "nt"

    builder = QemuEnvBuilder(
        system_site_packages=system_site_packages,
        clear=clear,
        symlinks=symlinks,
        with_pip=with_pip,
    )

    style = "non-isolated" if builder.system_site_packages else "isolated"
    nested = ""
    if builder.use_parent_packages:
        nested = f"(with packages from '{builder.get_parent_libpath()}') "
    print(
        f"mkvenv: Creating {style} virtual environment"
        f" {nested}at '{str(env_dir)}'",
        file=sys.stderr,
    )

    try:
        logger.debug("Invoking builder.create()")
        try:
            builder.create(str(env_dir))
        except SystemExit as exc:
            # Some versions of the venv module raise SystemExit; *nasty*!
            # We want the exception that prompted it. It might be a subprocess
            # error that has output we *really* want to see.
            logger.debug("Intercepted SystemExit from EnvBuilder.create()")
            raise exc.__cause__ or exc.__context__ or exc
        logger.debug("builder.create() finished")
    except subprocess.CalledProcessError as exc:
        logger.error("mkvenv subprocess failed:")
        logger.error("cmd: %s", exc.cmd)
        logger.error("returncode: %d", exc.returncode)

        def _stringify(data: Union[str, bytes]) -> str:
            if isinstance(data, bytes):
                return data.decode()
            return data

        lines = []
        if exc.stdout:
            lines.append("========== stdout ==========")
            lines.append(_stringify(exc.stdout))
            lines.append("============================")
        if exc.stderr:
            lines.append("========== stderr ==========")
            lines.append(_stringify(exc.stderr))
            lines.append("============================")
        if lines:
            logger.error(os.linesep.join(lines))

        raise Ouch("VENV creation subprocess failed.") from exc

    # print the python executable to stdout for configure.
    print(builder.get_value("env_exe"))


def _add_create_subcommand(subparsers: Any) -> None:
    subparser = subparsers.add_parser("create", help="create a venv")
    subparser.add_argument(
        "target",
        type=str,
        action="store",
        help="Target directory to install virtual environment into.",
    )


def main() -> int:
    """CLI interface to make_qemu_venv. See module docstring."""
    if os.environ.get("DEBUG") or os.environ.get("GITLAB_CI"):
        # You're welcome.
        logging.basicConfig(level=logging.DEBUG)
    elif os.environ.get("V"):
        logging.basicConfig(level=logging.INFO)

    parser = argparse.ArgumentParser(
        prog="mkvenv",
        description="QEMU pyvenv bootstrapping utility",
    )
    subparsers = parser.add_subparsers(
        title="Commands",
        dest="command",
        metavar="command",
        help="Description",
    )

    _add_create_subcommand(subparsers)

    args = parser.parse_args()
    try:
        if args.command == "create":
            make_venv(
                args.target,
                system_site_packages=True,
                clear=True,
            )
        logger.debug("mkvenv.py %s: exiting", args.command)
    except Ouch as exc:
        print("\n*** Ouch! ***\n", file=sys.stderr)
        print(str(exc), "\n\n", file=sys.stderr)
        return 1
    except SystemExit:
        raise
    except:  # pylint: disable=bare-except
        logger.exception("mkvenv did not complete successfully:")
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
