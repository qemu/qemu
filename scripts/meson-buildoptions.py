#! /usr/bin/env python3

# Generate configure command line options handling code, based on Meson's
# user build options introspection data
#
# Copyright (C) 2021 Red Hat, Inc.
#
# Author: Paolo Bonzini <pbonzini@redhat.com>
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2, or (at your option)
# any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <https://www.gnu.org/licenses/>.

import json
import textwrap
import shlex
import sys

# Options with nonstandard names (e.g. --with/--without) or OS-dependent
# defaults.  Try not to add any.
SKIP_OPTIONS = {
    "default_devices",
    "fuzzing_engine",
}

# Options whose name doesn't match the option for backwards compatibility
# reasons, because Meson gives them a funny name, or both
OPTION_NAMES = {
    "b_coverage": "gcov",
    "b_lto": "lto",
    "coroutine_backend": "with-coroutine",
    "debug": "debug-info",
    "malloc": "enable-malloc",
    "pkgversion": "with-pkgversion",
    "qemu_firmwarepath": "firmwarepath",
    "qemu_suffix": "with-suffix",
    "trace_backends": "enable-trace-backends",
    "trace_file": "with-trace-file",
}

# Options that configure autodetects, even though meson defines them as boolean
AUTO_OPTIONS = {
    "plugins",
    "werror",
}

# Builtin options that should be definable via configure.  Some of the others
# we really do not want (e.g. c_args is defined via the native file, not
# via -D, because it's a mix of CFLAGS and --extra-cflags); for specific
# cases "../configure -D" can be used as an escape hatch.
BUILTIN_OPTIONS = {
    "b_coverage",
    "b_lto",
    "bindir",
    "datadir",
    "debug",
    "includedir",
    "libdir",
    "libexecdir",
    "localedir",
    "localstatedir",
    "mandir",
    "prefix",
    "strip",
    "sysconfdir",
    "werror",
}

LINE_WIDTH = 76


# Convert the default value of an option to the string used in
# the help message
def get_help(opt):
    if opt["name"] == "libdir":
        return 'system default'
    value = opt["value"]
    if isinstance(value, list):
        return ",".join(value)
    if isinstance(value, bool):
        return "enabled" if value else "disabled"
    return str(value)


def wrap(left, text, indent):
    spaces = " " * indent
    if len(left) >= indent:
        yield left
        left = spaces
    else:
        left = (left + spaces)[0:indent]
    yield from textwrap.wrap(
        text, width=LINE_WIDTH, initial_indent=left, subsequent_indent=spaces
    )


def sh_print(line=""):
    print('  printf "%s\\n"', shlex.quote(line))


def help_line(left, opt, indent, long):
    right = f'{opt["description"]}'
    if long:
        value = get_help(opt)
        if value != "auto" and value != "":
            right += f" [{value}]"
    if "choices" in opt and long:
        choices = "/".join(sorted(opt["choices"]))
        right += f" (choices: {choices})"
    for x in wrap("  " + left, right, indent):
        sh_print(x)


# Return whether the option (a dictionary) can be used with
# arguments.  Booleans can never be used with arguments;
# combos allow an argument only if they accept other values
# than "auto", "enabled", and "disabled".
def allow_arg(opt):
    if opt["type"] == "boolean":
        return False
    if opt["type"] != "combo":
        return True
    return not (set(opt["choices"]) <= {"auto", "disabled", "enabled"})


# Return whether the option (a dictionary) can be used without
# arguments.  Booleans can only be used without arguments;
# combos require an argument if they accept neither "enabled"
# nor "disabled"
def require_arg(opt):
    if opt["type"] == "boolean":
        return False
    if opt["type"] != "combo":
        return True
    return not ({"enabled", "disabled"}.intersection(opt["choices"]))


def filter_options(json):
    if ":" in json["name"]:
        return False
    if json["section"] == "user":
        return json["name"] not in SKIP_OPTIONS
    else:
        return json["name"] in BUILTIN_OPTIONS


def load_options(json):
    json = [x for x in json if filter_options(x)]
    return sorted(json, key=lambda x: x["name"])


def cli_option(opt):
    name = opt["name"]
    if name in OPTION_NAMES:
        return OPTION_NAMES[name]
    return name.replace("_", "-")


def cli_help_key(opt):
    key = cli_option(opt)
    if require_arg(opt):
        return key
    if opt["type"] == "boolean" and opt["value"]:
        return f"disable-{key}"
    return f"enable-{key}"


def cli_metavar(opt):
    if opt["type"] == "string":
        return "VALUE"
    if opt["type"] == "array":
        return "CHOICES" if "choices" in opt else "VALUES"
    return "CHOICE"


def print_help(options):
    print("meson_options_help() {")
    feature_opts = []
    for opt in sorted(options, key=cli_help_key):
        key = cli_help_key(opt)
        # The first section includes options that have an arguments,
        # and booleans (i.e., only one of enable/disable makes sense)
        if require_arg(opt):
            metavar = cli_metavar(opt)
            left = f"--{key}={metavar}"
            help_line(left, opt, 27, True)
        elif opt["type"] == "boolean" and opt["name"] not in AUTO_OPTIONS:
            left = f"--{key}"
            help_line(left, opt, 27, False)
        elif allow_arg(opt):
            if opt["type"] == "combo" and "enabled" in opt["choices"]:
                left = f"--{key}[=CHOICE]"
            else:
                left = f"--{key}=CHOICE"
            help_line(left, opt, 27, True)
        else:
            feature_opts.append(opt)

    sh_print()
    sh_print("Optional features, enabled with --enable-FEATURE and")
    sh_print("disabled with --disable-FEATURE, default is enabled if available")
    sh_print("(unless built with --without-default-features):")
    sh_print()
    for opt in sorted(feature_opts, key=cli_option):
        key = cli_option(opt)
        help_line(key, opt, 18, False)
    print("}")


def print_parse(options):
    print("_meson_option_parse() {")
    print("  case $1 in")
    for opt in options:
        key = cli_option(opt)
        name = opt["name"]
        if require_arg(opt):
            if opt["type"] == "array" and not "choices" in opt:
                print(f'    --{key}=*) quote_sh "-D{name}=$(meson_option_build_array $2)" ;;')
            else:
                print(f'    --{key}=*) quote_sh "-D{name}=$2" ;;')
        elif opt["type"] == "boolean":
            print(f'    --enable-{key}) printf "%s" -D{name}=true ;;')
            print(f'    --disable-{key}) printf "%s" -D{name}=false ;;')
        else:
            if opt["type"] == "combo" and "enabled" in opt["choices"]:
                print(f'    --enable-{key}) printf "%s" -D{name}=enabled ;;')
            if opt["type"] == "combo" and "disabled" in opt["choices"]:
                print(f'    --disable-{key}) printf "%s" -D{name}=disabled ;;')
            if allow_arg(opt):
                print(f'    --enable-{key}=*) quote_sh "-D{name}=$2" ;;')
    print("    *) return 1 ;;")
    print("  esac")
    print("}")


options = load_options(json.load(sys.stdin))
print("# This file is generated by meson-buildoptions.py, do not edit!")
print_help(options)
print_parse(options)
