#!/usr/bin/env python
#
# VM testing base class
#
# Copyright 2017 Red Hat Inc.
#
# Authors:
#  Fam Zheng <famz@redhat.com>
#
# This code is licensed under the GPL version 2 or later.  See
# the COPYING file in the top-level directory.
#

from __future__ import print_function
import os
import sys
import logging
import time
import datetime
sys.path.append(os.path.join(os.path.dirname(__file__), "..", "..", "scripts"))
from qemu import QEMUMachine, kvm_available
import subprocess
import hashlib
import optparse
import atexit
import tempfile
import shutil
import multiprocessing
import traceback

SSH_KEY = open(os.path.join(os.path.dirname(__file__),
               "..", "keys", "id_rsa")).read()
SSH_PUB_KEY = open(os.path.join(os.path.dirname(__file__),
                   "..", "keys", "id_rsa.pub")).read()

class BaseVM(object):
    GUEST_USER = "qemu"
    GUEST_PASS = "qemupass"
    ROOT_PASS = "qemupass"

    # The script to run in the guest that builds QEMU
    BUILD_SCRIPT = ""
    # The guest name, to be overridden by subclasses
    name = "#base"
    # The guest architecture, to be overridden by subclasses
    arch = "#arch"
    def __init__(self, debug=False, vcpus=None):
        self._guest = None
        self._tmpdir = os.path.realpath(tempfile.mkdtemp(prefix="vm-test-",
                                                         suffix=".tmp",
                                                         dir="."))
        atexit.register(shutil.rmtree, self._tmpdir)

        self._ssh_key_file = os.path.join(self._tmpdir, "id_rsa")
        open(self._ssh_key_file, "w").write(SSH_KEY)
        subprocess.check_call(["chmod", "600", self._ssh_key_file])

        self._ssh_pub_key_file = os.path.join(self._tmpdir, "id_rsa.pub")
        open(self._ssh_pub_key_file, "w").write(SSH_PUB_KEY)

        self.debug = debug
        self._stderr = sys.stderr
        self._devnull = open(os.devnull, "w")
        if self.debug:
            self._stdout = sys.stdout
        else:
            self._stdout = self._devnull
        self._args = [ \
            "-nodefaults", "-m", "4G",
            "-cpu", "max",
            "-netdev", "user,id=vnet,hostfwd=:127.0.0.1:0-:22",
            "-device", "virtio-net-pci,netdev=vnet",
            "-vnc", "127.0.0.1:0,to=20",
            "-serial", "file:%s" % os.path.join(self._tmpdir, "serial.out")]
        if vcpus and vcpus > 1:
            self._args += ["-smp", str(vcpus)]
        if kvm_available(self.arch):
            self._args += ["-enable-kvm"]
        else:
            logging.info("KVM not available, not using -enable-kvm")
        self._data_args = []

    def _download_with_cache(self, url, sha256sum=None):
        def check_sha256sum(fname):
            if not sha256sum:
                return True
            checksum = subprocess.check_output(["sha256sum", fname]).split()[0]
            return sha256sum == checksum

        cache_dir = os.path.expanduser("~/.cache/qemu-vm/download")
        if not os.path.exists(cache_dir):
            os.makedirs(cache_dir)
        fname = os.path.join(cache_dir, hashlib.sha1(url).hexdigest())
        if os.path.exists(fname) and check_sha256sum(fname):
            return fname
        logging.debug("Downloading %s to %s...", url, fname)
        subprocess.check_call(["wget", "-c", url, "-O", fname + ".download"],
                              stdout=self._stdout, stderr=self._stderr)
        os.rename(fname + ".download", fname)
        return fname

    def _ssh_do(self, user, cmd, check, interactive=False):
        ssh_cmd = ["ssh", "-q",
                   "-o", "StrictHostKeyChecking=no",
                   "-o", "UserKnownHostsFile=" + os.devnull,
                   "-o", "ConnectTimeout=1",
                   "-p", self.ssh_port, "-i", self._ssh_key_file]
        if interactive:
            ssh_cmd += ['-t']
        assert not isinstance(cmd, str)
        ssh_cmd += ["%s@127.0.0.1" % user] + list(cmd)
        logging.debug("ssh_cmd: %s", " ".join(ssh_cmd))
        r = subprocess.call(ssh_cmd)
        if check and r != 0:
            raise Exception("SSH command failed: %s" % cmd)
        return r

    def ssh(self, *cmd):
        return self._ssh_do(self.GUEST_USER, cmd, False)

    def ssh_interactive(self, *cmd):
        return self._ssh_do(self.GUEST_USER, cmd, False, True)

    def ssh_root(self, *cmd):
        return self._ssh_do("root", cmd, False)

    def ssh_check(self, *cmd):
        self._ssh_do(self.GUEST_USER, cmd, True)

    def ssh_root_check(self, *cmd):
        self._ssh_do("root", cmd, True)

    def build_image(self, img):
        raise NotImplementedError

    def add_source_dir(self, src_dir):
        name = "data-" + hashlib.sha1(src_dir).hexdigest()[:5]
        tarfile = os.path.join(self._tmpdir, name + ".tar")
        logging.debug("Creating archive %s for src_dir dir: %s", tarfile, src_dir)
        subprocess.check_call(["./scripts/archive-source.sh", tarfile],
                              cwd=src_dir, stdin=self._devnull,
                              stdout=self._stdout, stderr=self._stderr)
        self._data_args += ["-drive",
                            "file=%s,if=none,id=%s,cache=writeback,format=raw" % \
                                    (tarfile, name),
                            "-device",
                            "virtio-blk,drive=%s,serial=%s,bootindex=1" % (name, name)]

    def boot(self, img, extra_args=[]):
        args = self._args + [
            "-device", "VGA",
            "-drive", "file=%s,if=none,id=drive0,cache=writeback" % img,
            "-device", "virtio-blk,drive=drive0,bootindex=0"]
        args += self._data_args + extra_args
        logging.debug("QEMU args: %s", " ".join(args))
        qemu_bin = os.environ.get("QEMU", "qemu-system-" + self.arch)
        guest = QEMUMachine(binary=qemu_bin, args=args)
        try:
            guest.launch()
        except:
            logging.error("Failed to launch QEMU, command line:")
            logging.error(" ".join([qemu_bin] + args))
            logging.error("Log:")
            logging.error(guest.get_log())
            logging.error("QEMU version >= 2.10 is required")
            raise
        atexit.register(self.shutdown)
        self._guest = guest
        usernet_info = guest.qmp("human-monitor-command",
                                 command_line="info usernet")
        self.ssh_port = None
        for l in usernet_info["return"].splitlines():
            fields = l.split()
            if "TCP[HOST_FORWARD]" in fields and "22" in fields:
                self.ssh_port = l.split()[3]
        if not self.ssh_port:
            raise Exception("Cannot find ssh port from 'info usernet':\n%s" % \
                            usernet_info)

    def wait_ssh(self, seconds=300):
        starttime = datetime.datetime.now()
        endtime = starttime + datetime.timedelta(seconds=seconds)
        guest_up = False
        while datetime.datetime.now() < endtime:
            if self.ssh("exit 0") == 0:
                guest_up = True
                break
            seconds = (endtime - datetime.datetime.now()).total_seconds()
            logging.debug("%ds before timeout", seconds)
            time.sleep(1)
        if not guest_up:
            raise Exception("Timeout while waiting for guest ssh")

    def shutdown(self):
        self._guest.shutdown()

    def wait(self):
        self._guest.wait()

    def qmp(self, *args, **kwargs):
        return self._guest.qmp(*args, **kwargs)

def parse_args(vmcls):

    def get_default_jobs():
        if kvm_available(vmcls.arch):
            return multiprocessing.cpu_count() / 2
        else:
            return 1

    parser = optparse.OptionParser(
        description="VM test utility.  Exit codes: "
                    "0 = success, "
                    "1 = command line error, "
                    "2 = environment initialization failed, "
                    "3 = test command failed")
    parser.add_option("--debug", "-D", action="store_true",
                      help="enable debug output")
    parser.add_option("--image", "-i", default="%s.img" % vmcls.name,
                      help="image file name")
    parser.add_option("--force", "-f", action="store_true",
                      help="force build image even if image exists")
    parser.add_option("--jobs", type=int, default=get_default_jobs(),
                      help="number of virtual CPUs")
    parser.add_option("--verbose", "-V", action="store_true",
                      help="Pass V=1 to builds within the guest")
    parser.add_option("--build-image", "-b", action="store_true",
                      help="build image")
    parser.add_option("--build-qemu",
                      help="build QEMU from source in guest")
    parser.add_option("--interactive", "-I", action="store_true",
                      help="Interactively run command")
    parser.add_option("--snapshot", "-s", action="store_true",
                      help="run tests with a snapshot")
    parser.disable_interspersed_args()
    return parser.parse_args()

def main(vmcls):
    try:
        args, argv = parse_args(vmcls)
        if not argv and not args.build_qemu and not args.build_image:
            print("Nothing to do?")
            return 1
        logging.basicConfig(level=(logging.DEBUG if args.debug
                                   else logging.WARN))
        vm = vmcls(debug=args.debug, vcpus=args.jobs)
        if args.build_image:
            if os.path.exists(args.image) and not args.force:
                sys.stderr.writelines(["Image file exists: %s\n" % args.image,
                                      "Use --force option to overwrite\n"])
                return 1
            return vm.build_image(args.image)
        if args.build_qemu:
            vm.add_source_dir(args.build_qemu)
            cmd = [vm.BUILD_SCRIPT.format(
                   configure_opts = " ".join(argv),
                   jobs=args.jobs,
                   verbose = "V=1" if args.verbose else "")]
        else:
            cmd = argv
        img = args.image
        if args.snapshot:
            img += ",snapshot=on"
        vm.boot(img)
        vm.wait_ssh()
    except Exception as e:
        if isinstance(e, SystemExit) and e.code == 0:
            return 0
        sys.stderr.write("Failed to prepare guest environment\n")
        traceback.print_exc()
        return 2

    if args.interactive:
        if vm.ssh_interactive(*cmd) == 0:
            return 0
        vm.ssh_interactive()
        return 3
    else:
        if vm.ssh(*cmd) != 0:
            return 3
