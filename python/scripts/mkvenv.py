"""
mkvenv - QEMU pyvenv bootstrapping utility

usage: mkvenv [-h] command ...

QEMU pyvenv bootstrapping utility

options:
  -h, --help  show this help message and exit

Commands:
  command     Description
    create    create a venv
    post_init
              post-venv initialization
    ensuregroup
              Ensure that the specified package group is installed.

--------------------------------------------------

usage: mkvenv create [-h] target

positional arguments:
  target      Target directory to install virtual environment into.

options:
  -h, --help  show this help message and exit

--------------------------------------------------

usage: mkvenv post_init [-h]

options:
  -h, --help         show this help message and exit

--------------------------------------------------

usage: mkvenv ensuregroup [-h] [--online] [--dir DIR] file group...

positional arguments:
  file        pointer to a TOML file
  group       section name in the TOML file

options:
  -h, --help  show this help message and exit
  --online    Install packages from PyPI, if necessary.
  --dir DIR   Path to vendored packages where we may install from.

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
from importlib.metadata import (
    Distribution,
    EntryPoint,
    PackageNotFoundError,
    distribution,
    version,
)
from importlib.util import find_spec
import logging
import os
from pathlib import Path
import re
import shutil
import site
import subprocess
import sys
import sysconfig
from types import SimpleNamespace
from typing import (
    Any,
    Dict,
    Iterator,
    Optional,
    Sequence,
    Tuple,
    Union,
)
import venv


# Try to load distlib, with a fallback to pip's vendored version.
# HAVE_DISTLIB is checked below, just-in-time, so that mkvenv does not fail
# outside the venv or before a potential call to ensurepip in checkpip().
HAVE_DISTLIB = True
try:
    import distlib.scripts
    import distlib.version
except ImportError:
    try:
        # Reach into pip's cookie jar.  pylint and flake8 don't understand
        # that these imports will be used via distlib.xxx.
        from pip._vendor import distlib
        import pip._vendor.distlib.scripts  # noqa, pylint: disable=unused-import
        import pip._vendor.distlib.version  # noqa, pylint: disable=unused-import
    except ImportError:
        HAVE_DISTLIB = False

# Try to load tomllib, with a fallback to tomli.
# HAVE_TOMLLIB is checked below, just-in-time, so that mkvenv does not fail
# outside the venv or before a potential call to ensurepip in checkpip().
HAVE_TOMLLIB = True
try:
    import tomllib
except ImportError:
    try:
        import tomli as tomllib
    except ImportError:
        HAVE_TOMLLIB = False

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
    environment by including packages from the parent.  Also,
    "ensurepip" is replaced if possible with just recreating pip's
    console_scripts inside the virtual environment.

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

        # ensurepip is slow: venv creation can be very fast for cases where
        # we allow the use of system_site_packages. Therefore, ensurepip is
        # replaced with our own script generation once the virtual environment
        # is setup.
        self.want_pip = kwargs.get("with_pip", False)
        if self.want_pip:
            if (
                kwargs.get("system_site_packages", False)
                and not need_ensurepip()
            ):
                kwargs["with_pip"] = False
            else:
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

        if self.want_pip:
            args = [
                context.env_exe,
                __file__,
                "post_init",
            ]
            subprocess.run(args, check=True)

    def get_value(self, field: str) -> str:
        """
        Get a string value from the context namespace after a call to build.

        For valid field names, see:
        https://docs.python.org/3/library/venv.html#venv.EnvBuilder.ensure_directories
        """
        ret = getattr(self._context, field)
        assert isinstance(ret, str)
        return ret


def need_ensurepip() -> bool:
    """
    Tests for the presence of setuptools and pip.

    :return: `True` if we do not detect both packages.
    """
    # Don't try to actually import them, it's fraught with danger:
    # https://github.com/pypa/setuptools/issues/2993
    if find_spec("setuptools") and find_spec("pip"):
        return False
    return True


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
            # pylint 3.3 bug:
            # pylint: disable=raising-non-exception, raise-missing-from

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


def _get_entry_points(packages: Sequence[str]) -> Iterator[str]:

    def _generator() -> Iterator[str]:
        for package in packages:
            try:
                entry_points: Iterator[EntryPoint] = \
                    iter(distribution(package).entry_points)
            except PackageNotFoundError:
                continue

            # The EntryPoints type is only available in 3.10+,
            # treat this as a vanilla list and filter it ourselves.
            entry_points = filter(
                lambda ep: ep.group == "console_scripts", entry_points
            )

            for entry_point in entry_points:
                yield f"{entry_point.name} = {entry_point.value}"

    return _generator()


def generate_console_scripts(
    packages: Sequence[str],
    python_path: Optional[str] = None,
    bin_path: Optional[str] = None,
) -> None:
    """
    Generate script shims for console_script entry points in @packages.
    """
    if python_path is None:
        python_path = sys.executable
    if bin_path is None:
        bin_path = sysconfig.get_path("scripts")
        assert bin_path is not None

    logger.debug(
        "generate_console_scripts(packages=%s, python_path=%s, bin_path=%s)",
        packages,
        python_path,
        bin_path,
    )

    if not packages:
        return

    maker = distlib.scripts.ScriptMaker(None, bin_path)
    maker.variants = {""}
    maker.clobber = False

    for entry_point in _get_entry_points(packages):
        for filename in maker.make(entry_point):
            logger.debug("wrote console_script '%s'", filename)


def pkgname_from_depspec(dep_spec: str) -> str:
    """
    Parse package name out of a PEP-508 depspec.

    See https://peps.python.org/pep-0508/#names
    """
    match = re.match(
        r"^([A-Z0-9]([A-Z0-9._-]*[A-Z0-9])?)", dep_spec, re.IGNORECASE
    )
    if not match:
        raise ValueError(
            f"dep_spec '{dep_spec}'"
            " does not appear to contain a valid package name"
        )
    return match.group(0)


def _path_is_prefix(prefix: Optional[str], path: str) -> bool:
    try:
        return (
            prefix is not None and os.path.commonpath([prefix, path]) == prefix
        )
    except ValueError:
        return False


def _is_system_package(dist: Distribution) -> bool:
    path = str(dist.locate_file("."))
    return not (
        _path_is_prefix(sysconfig.get_path("purelib"), path)
        or _path_is_prefix(sysconfig.get_path("platlib"), path)
    )


def diagnose(
    dep_spec: str,
    online: bool,
    wheels_dir: Optional[Union[str, Path]],
    prog: Optional[str],
) -> Tuple[str, bool]:
    """
    Offer a summary to the user as to why a package failed to be installed.

    :param dep_spec: The package we tried to ensure, e.g. 'meson>=0.61.5'
    :param online: Did we allow PyPI access?
    :param prog:
        Optionally, a shell program name that can be used as a
        bellwether to detect if this program is installed elsewhere on
        the system. This is used to offer advice when a program is
        detected for a different python version.
    :param wheels_dir:
        Optionally, a directory that was searched for vendored packages.
    """
    # pylint: disable=too-many-branches

    # Some errors are not particularly serious
    bad = False

    pkg_name = pkgname_from_depspec(dep_spec)
    pkg_version: Optional[str] = None
    try:
        pkg_version = version(pkg_name)
    except PackageNotFoundError:
        pass

    lines = []

    if pkg_version:
        lines.append(
            f"Python package '{pkg_name}' version '{pkg_version}' was found,"
            " but isn't suitable."
        )
    else:
        lines.append(
            f"Python package '{pkg_name}' was not found nor installed."
        )

    if wheels_dir:
        lines.append(
            "No suitable version found in, or failed to install from"
            f" '{wheels_dir}'."
        )
        bad = True

    if online:
        lines.append("A suitable version could not be obtained from PyPI.")
        bad = True
    else:
        lines.append(
            "mkvenv was configured to operate offline and did not check PyPI."
        )

    if prog and not pkg_version:
        which = shutil.which(prog)
        if which:
            if sys.base_prefix in site.PREFIXES:
                pypath = Path(sys.executable).resolve()
                lines.append(
                    f"'{prog}' was detected on your system at '{which}', "
                    f"but the Python package '{pkg_name}' was not found by "
                    f"this Python interpreter ('{pypath}'). "
                    f"Typically this means that '{prog}' has been installed "
                    "against a different Python interpreter on your system."
                )
            else:
                lines.append(
                    f"'{prog}' was detected on your system at '{which}', "
                    "but the build is using an isolated virtual environment."
                )
            bad = True

    lines = [f" • {line}" for line in lines]
    if bad:
        lines.insert(0, f"Could not provide build dependency '{dep_spec}':")
    else:
        lines.insert(0, f"'{dep_spec}' not found:")
    return os.linesep.join(lines), bad


def pip_install(
    args: Sequence[str],
    online: bool = False,
    wheels_dir: Optional[Union[str, Path]] = None,
) -> None:
    """
    Use pip to install a package or package(s) as specified in @args.
    """
    loud = bool(
        os.environ.get("DEBUG")
        or os.environ.get("GITLAB_CI")
        or os.environ.get("V")
    )

    full_args = [
        sys.executable,
        "-m",
        "pip",
        "install",
        "--disable-pip-version-check",
        "-v" if loud else "-q",
    ]
    if not online:
        full_args += ["--no-index"]
    if wheels_dir:
        full_args += ["--find-links", f"file://{str(wheels_dir)}"]
    full_args += list(args)
    subprocess.run(
        full_args,
        check=True,
    )


def _make_version_constraint(info: Dict[str, str], install: bool) -> str:
    """
    Construct the version constraint part of a PEP 508 dependency
    specification (for example '>=0.61.5') from the accepted and
    installed keys of the provided dictionary.

    :param info: A dictionary corresponding to a TOML key-value list.
    :param install: True generates install constraints, False generates
        presence constraints
    """
    if install and "installed" in info:
        return "==" + info["installed"]

    dep_spec = info.get("accepted", "")
    dep_spec = dep_spec.strip()
    # Double check that they didn't just use a version number
    if dep_spec and dep_spec[0] not in "!~><=(":
        raise Ouch(
            "invalid dependency specifier " + dep_spec + " in dependency file"
        )

    return dep_spec


def _do_ensure(
    group: Dict[str, Dict[str, str]],
    online: bool = False,
    wheels_dir: Optional[Union[str, Path]] = None,
) -> Optional[Tuple[str, bool]]:
    """
    Use pip to ensure we have the packages specified in @group.

    If the packages are already installed, do nothing. If online and
    wheels_dir are both provided, prefer packages found in wheels_dir
    first before connecting to PyPI.

    :param group: A dictionary of dictionaries, corresponding to a
        section in a pythondeps.toml file.
    :param online: If True, fall back to PyPI.
    :param wheels_dir: If specified, search this path for packages.
    """
    absent = []
    present = []
    canary = None
    for name, info in group.items():
        constraint = _make_version_constraint(info, False)
        matcher = distlib.version.LegacyMatcher(name + constraint)
        print(f"mkvenv: checking for {matcher}", file=sys.stderr)

        dist: Optional[Distribution] = None
        try:
            dist = distribution(matcher.name)
        except PackageNotFoundError:
            pass

        if (
            dist is None
            # Always pass installed package to pip, so that they can be
            # updated if the requested version changes
            or not _is_system_package(dist)
            or not matcher.match(distlib.version.LegacyVersion(dist.version))
        ):
            absent.append(name + _make_version_constraint(info, True))
            if len(absent) == 1:
                canary = info.get("canary", None)
        else:
            logger.info("found %s %s", name, dist.version)
            present.append(name)

    if present:
        generate_console_scripts(present)

    if absent:
        if online or wheels_dir:
            # Some packages are missing or aren't a suitable version,
            # install a suitable (possibly vendored) package.
            print(f"mkvenv: installing {', '.join(absent)}", file=sys.stderr)
            try:
                pip_install(args=absent, online=online, wheels_dir=wheels_dir)
                return None
            except subprocess.CalledProcessError:
                pass

        return diagnose(
            absent[0],
            online,
            wheels_dir,
            canary,
        )

    return None


def _parse_groups(file: str) -> Dict[str, Dict[str, Any]]:
    if not HAVE_TOMLLIB:
        if sys.version_info < (3, 11):
            raise Ouch("found no usable tomli, please install it")

        raise Ouch(
            "Python >=3.11 does not have tomllib... what have you done!?"
        )

    # Use loads() to support both tomli v1.2.x (Ubuntu 22.04,
    # Debian bullseye-backports) and v2.0.x
    with open(file, "r", encoding="ascii") as depfile:
        contents = depfile.read()
        return tomllib.loads(contents)  # type: ignore


def ensure_group(
    file: str,
    groups: Sequence[str],
    online: bool = False,
    wheels_dir: Optional[Union[str, Path]] = None,
) -> None:
    """
    Use pip to ensure we have the package specified by @dep_specs.

    If the package is already installed, do nothing. If online and
    wheels_dir are both provided, prefer packages found in wheels_dir
    first before connecting to PyPI.

    :param dep_specs:
        PEP 508 dependency specifications. e.g. ['meson>=0.61.5'].
    :param online: If True, fall back to PyPI.
    :param wheels_dir: If specified, search this path for packages.
    """

    if not HAVE_DISTLIB:
        raise Ouch("found no usable distlib, please install it")

    parsed_deps = _parse_groups(file)

    to_install: Dict[str, Dict[str, str]] = {}
    for group in groups:
        try:
            to_install.update(parsed_deps[group])
        except KeyError as exc:
            raise Ouch(f"group {group} not defined") from exc

    result = _do_ensure(to_install, online, wheels_dir)
    if result:
        # Well, that's not good.
        if result[1]:
            raise Ouch(result[0])
        raise SystemExit(f"\n{result[0]}\n\n")


def post_venv_setup() -> None:
    """
    This is intended to be run *inside the venv* after it is created.
    """
    logger.debug("post_venv_setup()")
    # Generate a 'pip' script so the venv is usable in a normal
    # way from the CLI. This only happens when we inherited pip from a
    # parent/system-site and haven't run ensurepip in some way.
    generate_console_scripts(["pip"])


def _add_create_subcommand(subparsers: Any) -> None:
    subparser = subparsers.add_parser("create", help="create a venv")
    subparser.add_argument(
        "target",
        type=str,
        action="store",
        help="Target directory to install virtual environment into.",
    )


def _add_post_init_subcommand(subparsers: Any) -> None:
    subparsers.add_parser("post_init", help="post-venv initialization")


def _add_ensuregroup_subcommand(subparsers: Any) -> None:
    subparser = subparsers.add_parser(
        "ensuregroup",
        help="Ensure that the specified package group is installed.",
    )
    subparser.add_argument(
        "--online",
        action="store_true",
        help="Install packages from PyPI, if necessary.",
    )
    subparser.add_argument(
        "--dir",
        type=str,
        action="store",
        help="Path to vendored packages where we may install from.",
    )
    subparser.add_argument(
        "file",
        type=str,
        action="store",
        help=("Path to a TOML file describing package groups"),
    )
    subparser.add_argument(
        "group",
        type=str,
        action="store",
        help="One or more package group names",
        nargs="+",
    )


def main() -> int:
    """CLI interface to make_qemu_venv. See module docstring."""
    if os.environ.get("DEBUG") or os.environ.get("GITLAB_CI"):
        # You're welcome.
        logging.basicConfig(level=logging.DEBUG)
    else:
        if os.environ.get("V"):
            logging.basicConfig(level=logging.INFO)

    parser = argparse.ArgumentParser(
        prog="mkvenv",
        description="QEMU pyvenv bootstrapping utility",
    )
    subparsers = parser.add_subparsers(
        title="Commands",
        dest="command",
        required=True,
        metavar="command",
        help="Description",
    )

    _add_create_subcommand(subparsers)
    _add_post_init_subcommand(subparsers)
    _add_ensuregroup_subcommand(subparsers)

    args = parser.parse_args()
    try:
        if args.command == "create":
            make_venv(
                args.target,
                system_site_packages=True,
                clear=True,
            )
        if args.command == "post_init":
            post_venv_setup()
        if args.command == "ensuregroup":
            ensure_group(
                file=args.file,
                groups=args.group,
                online=args.online,
                wheels_dir=args.dir,
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
