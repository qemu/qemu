#!/usr/bin/env python3
'''
Module to simplify PANDA command line usage. Use python3 -m pandare.qcows to 
fetch files necessary to run various generic VMs and generate command lines to start them.
Also supports deleting previously-fetched files.

Most of the interesting logic for this is contained in qcows_internal.py.
'''

from os import path, remove
from shlex import split as shlex_split
from sys import exit, stderr

from .utils import find_build_dir
from .panda import Panda
from os import environ
from .qcows_internal import Qcows, SUPPORTED_IMAGES

VM_DIR = path.join(path.expanduser("~"), ".panda")
class Qcows_cli():
    @staticmethod
    def _find_build_dir(arch):
        try:
            build_dir = find_build_dir(arch, find_executable=True)
        except RuntimeError as e:
            # Couldn't find this arch - can we find any?
            try:
                build_dir = find_build_dir(None, find_executable=True)
                print(f"WARNING: You do not appear to have panda-system-{arch}, please build it then try again\n",
                      file=None if stdout.isatty() else stderr)

            except RuntimeError as e2:
                print(f"ERROR: Cannot find any version of PANDA on your system, please build and then try again\n",
                      file=None if stdout.isatty() else stderr)
                raise e
        return build_dir

    @staticmethod
    def remove_image(target):
        try:
            qcow = Qcows.get_qcow(target, download=False, _is_tty=stdout.isatty())
        except ValueError:
            # No QCOW, we're good!
            return

        try:
            image_data = SUPPORTED_IMAGES[target]
        except ValueError:
            # Not a valid image? I guess we're good
            return

        qc = image_data.qcow
        if not qc: # Default, get name from url
            qc = image_data.url.split("/")[-1]
        qcow_path = path.join(VM_DIR, qc)
        if path.isfile(qcow_path):
            print(f"Deleting {qcow_path}")
            remove(qcow_path)

        for extra_file in image_data.extra_files or []:
            extra_file_path = path.join(VM_DIR, extra_file)
            if path.isfile(extra_file_path):
                print(f"Deleting {extra_file_path}")
                remove(extra_file_path)
    @staticmethod
    def cli(target):
        q = Qcows.get_qcow_info(target)
        qcow = Qcows.get_qcow(target, _is_tty=stdout.isatty())
        arch = q.arch
        # User needs to have the specified arch in order to run the command.
        # But if they just want to download/delete files and we find another arch
        # we can fetch/delete the files print a warning about how the generatd command won't work.

        build_dir = Qcows_cli._find_build_dir(arch) # will set find_executable
        panda_args = [path.join(build_dir, f"panda-system-{arch}")]
        biospath = path.realpath(path.join(build_dir, "pc-bios"))
        panda_args.extend(["-L", biospath])
        panda_args.extend(["-os", q.os])

        if arch == 'mips64':
            panda_args.extend(["-drive", f"file={qcow},if=virtio"])
        else:
            panda_args.append(qcow)

        panda_args.extend(['-m', q.default_mem])

        if q.extra_args:
            extra_args = shlex_split(q.extra_args)
            for x in extra_args:
                if " " in x:
                    panda_args.append(repr(x))
                else:
                    panda_args.append(x)

        panda_args.extend(['-loadvm', q.snapshot])

        ret = " ".join(panda_args)

        if "-display none" in ret:
            ret = ret.replace("-display none", "-nographic")

        # Replace /home/username with ~ when we can (TTYs)
        if stdout.isatty() and 'HOME' in environ:
            ret = ret.replace(environ['HOME'], '~')
        return ret

if __name__ == "__main__":
    from sys import argv, stdout
    valid_names = "\n * ".join(SUPPORTED_IMAGES.keys())

    delete_mode = False
    if len(argv) == 3 and argv[1] == 'delete':
        delete_mode = True
        argv.pop(1)

    if len(argv) != 2 or argv[1] not in SUPPORTED_IMAGES:
        print("\n" + f"USAGE: {argv[0]} [target_images]\n" +
                     f"   or: {argv[0]} delete [target_image]\n\n" +
                      "The required files for the specified images will be downloaded and the PANDA command line to emulate that guest will be printed.\n" +
                      "If the \"delete\" argument is passed, any files related to the image will be deleted and no command line will be printed"
                     f"Where target_images is one of:\n * {valid_names}\n")
        exit(1)

    if delete_mode:
        Qcows_cli.remove_image(argv[1])

    else:
        cmd = Qcows_cli.cli(argv[1])
        if stdout.isatty():
            print(f"Run the generic {argv[1]} PANDA guest interactively with the following command:\n{cmd}")
        else:
            print(cmd)
