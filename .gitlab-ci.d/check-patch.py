#!/usr/bin/env python3
#
# check-patch.py: run checkpatch.pl across all commits in a branch
#
# Copyright (C) 2020 Red Hat, Inc.
#
# SPDX-License-Identifier: GPL-2.0-or-later

import os
import os.path
import sys
import subprocess

namespace = "qemu-project"
if len(sys.argv) >= 2:
    namespace = sys.argv[1]

cwd = os.getcwd()
reponame = os.path.basename(cwd)
repourl = "https://gitlab.com/%s/%s.git" % (namespace, reponame)

print(f"adding upstream git repo @ {repourl}")
# GitLab CI environment does not give us any direct info about the
# base for the user's branch. We thus need to figure out a common
# ancestor between the user's branch and current git master.
subprocess.check_call(["git", "remote", "add", "check-patch", repourl])
subprocess.check_call(["git", "fetch", "--refetch", "check-patch", "master"])

ancestor = subprocess.check_output(["git", "merge-base",
                                    "check-patch/master", "HEAD"],
                                   universal_newlines=True)

ancestor = ancestor.strip()

log = subprocess.check_output(["git", "log", "--format=%H %s",
                               ancestor + "..."],
                              universal_newlines=True)

subprocess.check_call(["git", "remote", "rm", "check-patch"])

if log == "":
    print("\nNo commits since %s, skipping checks\n" % ancestor)
    sys.exit(0)

errors = False

print("\nChecking all commits since %s...\n" % ancestor, flush=True)

ret = subprocess.run(["scripts/checkpatch.pl", "--terse", ancestor + "..."])

if ret.returncode != 0:
    print("    ❌ FAIL one or more commits failed scripts/checkpatch.pl")
    sys.exit(1)

sys.exit(0)
