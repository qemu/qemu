#!/usr/bin/python3
#
# SPDX-License-Identifier: GPL-2.0-or-later
#
# A script to generate a CSV file showing the x86_64 ABI
# compatibility levels for each CPU model.
#

from qemu.qmp.legacy import QEMUMonitorProtocol
import sys

if len(sys.argv) != 2:
    print("syntax: %s QMP-SOCK\n\n" % __file__ +
          "Where QMP-SOCK points to a QEMU process such as\n\n" +
          " # qemu-system-x86_64 -qmp unix:/tmp/qmp,server,nowait " +
          "-display none -accel kvm", file=sys.stderr)
    sys.exit(1)

# Mandatory CPUID features for each microarch ABI level
levels = [
    [ # x86-64 baseline
        "cmov",
        "cx8",
        "fpu",
        "fxsr",
        "mmx",
        "syscall",
        "sse",
        "sse2",
    ],
    [ # x86-64-v2
        "cx16",
        "lahf-lm",
        "popcnt",
        "pni",
        "sse4.1",
        "sse4.2",
        "ssse3",
    ],
    [ # x86-64-v3
        "avx",
        "avx2",
        "bmi1",
        "bmi2",
        "f16c",
        "fma",
        "abm",
        "movbe",
    ],
    [ # x86-64-v4
        "avx512f",
        "avx512bw",
        "avx512cd",
        "avx512dq",
        "avx512vl",
    ],
]

# Assumes externally launched process such as
#
#   qemu-system-x86_64 -qmp unix:/tmp/qmp,server,nowait -display none -accel kvm
#
# Note different results will be obtained with TCG, as
# TCG masks out certain features otherwise present in
# the CPU model definitions, as does KVM.


sock = sys.argv[1]
shell = QEMUMonitorProtocol(sock)
shell.connect()

models = shell.cmd("query-cpu-definitions")

# These QMP props don't correspond to CPUID fatures
# so ignore them
skip = [
    "family",
    "min-level",
    "min-xlevel",
    "vendor",
    "model",
    "model-id",
    "stepping",
]

names = []

for model in models["return"]:
    if "alias-of" in model:
        continue
    names.append(model["name"])

models = {}

for name in sorted(names):
    cpu = shell.cmd("query-cpu-model-expansion",
                     { "type": "static",
                       "model": { "name": name }})

    got = {}
    for (feature, present) in cpu["return"]["model"]["props"].items():
        if present and feature not in skip:
            got[feature] = True

    if name in ["host", "max", "base"]:
        continue

    models[name] = {
        # Dict of all present features in this CPU model
        "features": got,

        # Whether each x86-64 ABI level is satisfied
        "levels": [False, False, False, False],

        # Number of extra CPUID features compared to the x86-64 ABI level
        "distance":[-1, -1, -1, -1],

        # CPUID features present in model, but not in ABI level
        "delta":[[], [], [], []],

        # CPUID features in ABI level but not present in model
        "missing": [[], [], [], []],
    }


# Calculate whether the CPU models satisfy each ABI level
for name in models.keys():
    for level in range(len(levels)):
        got = set(models[name]["features"])
        want = set(levels[level])
        missing = want - got
        match = True
        if len(missing) > 0:
            match = False
        models[name]["levels"][level] = match
        models[name]["missing"][level] = missing

# Cache list of CPU models satisfying each ABI level
abi_models = [
    [],
    [],
    [],
    [],
]

for name in models.keys():
    for level in range(len(levels)):
        if models[name]["levels"][level]:
            abi_models[level].append(name)


for level in range(len(abi_models)):
    # Find the union of features in all CPU models satisfying this ABI
    allfeatures = {}
    for name in abi_models[level]:
        for feat in models[name]["features"]:
            allfeatures[feat] = True

    # Find the intersection of features in all CPU models satisfying this ABI
    commonfeatures = []
    for feat in allfeatures:
        present = True
        for name in models.keys():
            if not models[name]["levels"][level]:
                continue
            if feat not in models[name]["features"]:
                present = False
        if present:
            commonfeatures.append(feat)

    # Determine how many extra features are present compared to the lowest
    # common denominator
    for name in models.keys():
        if not models[name]["levels"][level]:
            continue

        delta = set(models[name]["features"].keys()) - set(commonfeatures)
        models[name]["distance"][level] = len(delta)
        models[name]["delta"][level] = delta

def print_uarch_abi_csv():
    print("# Automatically generated from '%s'" % __file__)
    print("Model,baseline,v2,v3,v4")
    for name in models.keys():
        print(name, end="")
        for level in range(len(levels)):
            if models[name]["levels"][level]:
                print(",✅", end="")
            else:
                print(",", end="")
        print()

print_uarch_abi_csv()
