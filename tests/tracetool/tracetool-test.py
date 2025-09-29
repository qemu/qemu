#!/usr/bin/python3
# SPDX-License-Identifier: GPL-2.0-or-later

import os
from pathlib import Path
from shutil import copyfile
from subprocess import check_call
import sys
import tempfile


def get_formats(backend):
    formats = [
        "c",
        "h",
    ]
    if backend in {"ftrace", "log", "simple", "syslog"}:
        formats += ["rs"]
    if backend == "dtrace":
        formats += [
            "d",
            "log-stap",
            "simpletrace-stap",
            "stap",
        ]
    if backend == "ust":
        formats += [
            "ust-events-c",
            "ust-events-h",
        ]
    return formats


def test_tracetool_one(tracetool, backend, fmt, src_dir, build_dir):
    rel_filename = backend + "." + fmt
    actual_file = Path(build_dir, rel_filename)
    expect_file = Path(src_dir, rel_filename)

    args = [tracetool, f"--format={fmt}", f"--backends={backend}", "--group=testsuite"]

    if fmt.find("stap") != -1:
        args += ["--binary=qemu", "--probe-prefix=qemu"]

    # Use relative files for both, as these filenames end
    # up in '#line' statements in the output
    args += ["trace-events", rel_filename]

    try:
        check_call(args, cwd=build_dir)
        actual = actual_file.read_text()
    finally:
        actual_file.unlink()

    if os.getenv("QEMU_TEST_REGENERATE", False):
        print(f"# regenerate {expect_file}")
        expect_file.write_text(actual)

    expect = expect_file.read_text()

    assert expect == actual


def test_tracetool(tracetool, backend, source_dir, build_dir):
    fail = False
    scenarios = len(get_formats(backend))

    print(f"1..{scenarios}")

    src_events = Path(source_dir, "trace-events")
    build_events = Path(build_dir, "trace-events")

    try:
        # We need a stable relative filename under build dir
        # for the '#line' statements, so copy over the input
        copyfile(src_events, build_events)

        num = 1
        for fmt in get_formats(backend):
            status = "not ok"
            hint = ""
            try:
                test_tracetool_one(tracetool, backend, fmt, source_dir, build_dir)
                status = "ok"
            except Exception as e:
                print(f"# {e}")
                fail = True
                hint = (
                    " (set QEMU_TEST_REGENERATE=1 to recreate reference "
                    + "output if tracetool generator was intentionally changed)"
                )
            finally:
                print(f"{status} {num} - {backend}.{fmt}{hint}")
    finally:
        build_events.unlink()

    return fail


if __name__ == "__main__":
    if len(sys.argv) != 5:
        argv0 = sys.argv[0]
        print("syntax: {argv0} TRACE-TOOL BACKEND SRC-DIR BUILD-DIR", file=sys.stderr)
        sys.exit(1)

    with tempfile.TemporaryDirectory(prefix=sys.argv[4]) as tmpdir:
        fail = test_tracetool(sys.argv[1], sys.argv[2], sys.argv[3], tmpdir)
        if fail:
            sys.exit(1)
    sys.exit(0)
