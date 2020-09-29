#!/usr/bin/python

import os, re
root = "tests/data/acpi"
for machine in os.listdir(root):
    machine_root = os.path.join(root, machine)
    if not os.path.isdir(machine_root):
        continue
    files = os.listdir(machine_root):
    for file in files:
        if file.endswith(".dsl"):
            continue
        extension_prefix = "^[^.]*\."
        if re.match(extension_prefix, file):
            variant = re.sub(extension_prefix, "", file)


for dirpath, dirnames, filenames in os.walk("tests/data/acpi"):
    for file in files:
        if file.endswith(".txt"):
             print(os.path.join(root, file))
