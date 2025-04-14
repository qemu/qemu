#!/usr/bin/env python3
#
# Copyright (C) 2020 Red Hat, Inc.
#
# SPDX-License-Identifier: GPL-2.0-or-later

import argparse
import glob
import os
import shutil
import subprocess
import tempfile


def signcode(path):
    cmd = os.environ.get("SIGNCODE")
    if not cmd:
        return
    subprocess.run([cmd, path])

def find_deps(exe_or_dll, search_path, analyzed_deps):
    deps = [exe_or_dll]
    output = subprocess.check_output(["objdump", "-p", exe_or_dll], text=True)
    output = output.split("\n")
    for line in output:
        if not line.lstrip().startswith("DLL Name: "):
            continue

        dep = line.split("DLL Name: ")[1].strip()
        if dep in analyzed_deps:
            continue

        dll = os.path.join(search_path, dep)
        if not os.path.exists(dll):
            # assume it's a Windows provided dll, skip it
            continue

        analyzed_deps.add(dep)
        # locate the dll dependencies recursively
        analyzed_deps, rdeps = find_deps(dll, search_path, analyzed_deps)
        deps.extend(rdeps)

    return analyzed_deps, deps

def main():
    parser = argparse.ArgumentParser(description="QEMU NSIS build helper.")
    parser.add_argument("outfile")
    parser.add_argument("prefix")
    parser.add_argument("srcdir")
    parser.add_argument("dlldir")
    parser.add_argument("cpu")
    parser.add_argument("nsisargs", nargs="*")
    args = parser.parse_args()

    # canonicalize the Windows native prefix path
    prefix = os.path.splitdrive(args.prefix)[1]
    destdir = tempfile.mkdtemp()
    try:
        subprocess.run(["make", "install", "DESTDIR=" + destdir])
        with open(
            os.path.join(destdir + prefix, "system-emulations.nsh"), "w"
        ) as nsh, open(
            os.path.join(destdir + prefix, "system-mui-text.nsh"), "w"
        ) as muinsh:
            for exe in sorted(glob.glob(
                os.path.join(destdir + prefix, "qemu-system-*.exe")
            )):
                exe = os.path.basename(exe)
                arch = exe[12:-4]
                nsh.write(
                    """
                Section "{0}" Section_{0}
                SetOutPath "$INSTDIR"
                File "${{BINDIR}}\\{1}"
                SectionEnd
                """.format(
                        arch, exe
                    )
                )
                if arch.endswith('w'):
                    desc = arch[:-1] + " emulation (GUI)."
                else:
                    desc = arch + " emulation."

                muinsh.write(
                    """
                !insertmacro MUI_DESCRIPTION_TEXT ${{Section_{0}}} "{1}"
                """.format(arch, desc))

        search_path = args.dlldir
        print("Searching '%s' for the dependent dlls ..." % search_path)
        dlldir = os.path.join(destdir + prefix, "dll")
        os.mkdir(dlldir)

        analyzed_deps = set()
        for exe in glob.glob(os.path.join(destdir + prefix, "*.exe")):
            signcode(exe)

            # find all dll dependencies
            analyzed_deps, deps = find_deps(exe, search_path, analyzed_deps)
            deps = set(deps)
            deps.remove(exe)

            # copy all dlls to the DLLDIR
            for dep in deps:
                dllfile = os.path.join(dlldir, os.path.basename(dep))
                print("Copying '%s' to '%s'" % (dep, dllfile))
                shutil.copy(dep, dllfile)

        makensis = [
            "makensis",
            "-V2",
            "-NOCD",
            "-DSRCDIR=" + args.srcdir,
            "-DBINDIR=" + destdir + prefix,
        ]
        if args.cpu == "x86_64":
            makensis += ["-DW64"]
        makensis += ["-DDLLDIR=" + dlldir]

        makensis += ["-DOUTFILE=" + args.outfile] + args.nsisargs
        subprocess.run(makensis)
        signcode(args.outfile)
    finally:
        shutil.rmtree(destdir)


if __name__ == "__main__":
    main()
