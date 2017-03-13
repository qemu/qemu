#!/usr/bin/env python2
#
# Docker controlling module
#
# Copyright (c) 2016 Red Hat Inc.
#
# Authors:
#  Fam Zheng <famz@redhat.com>
#
# This work is licensed under the terms of the GNU GPL, version 2
# or (at your option) any later version. See the COPYING file in
# the top-level directory.

import os
import sys
import subprocess
import json
import hashlib
import atexit
import uuid
import argparse
import tempfile
import re
import signal
from tarfile import TarFile, TarInfo
from StringIO import StringIO
from shutil import copy, rmtree
from pwd import getpwuid


FILTERED_ENV_NAMES = ['ftp_proxy', 'http_proxy', 'https_proxy']


DEVNULL = open(os.devnull, 'wb')


def _text_checksum(text):
    """Calculate a digest string unique to the text content"""
    return hashlib.sha1(text).hexdigest()

def _guess_docker_command():
    """ Guess a working docker command or raise exception if not found"""
    commands = [["docker"], ["sudo", "-n", "docker"]]
    for cmd in commands:
        try:
            if subprocess.call(cmd + ["images"],
                               stdout=DEVNULL, stderr=DEVNULL) == 0:
                return cmd
        except OSError:
            pass
    commands_txt = "\n".join(["  " + " ".join(x) for x in commands])
    raise Exception("Cannot find working docker command. Tried:\n%s" % \
                    commands_txt)

def _copy_with_mkdir(src, root_dir, sub_path):
    """Copy src into root_dir, creating sub_path as needed."""
    dest_dir = os.path.normpath("%s/%s" % (root_dir, sub_path))
    try:
        os.makedirs(dest_dir)
    except OSError:
        # we can safely ignore already created directories
        pass

    dest_file = "%s/%s" % (dest_dir, os.path.basename(src))
    copy(src, dest_file)


def _get_so_libs(executable):
    """Return a list of libraries associated with an executable.

    The paths may be symbolic links which would need to be resolved to
    ensure theright data is copied."""

    libs = []
    ldd_re = re.compile(r"(/.*/)(\S*)")
    try:
        ldd_output = subprocess.check_output(["ldd", executable])
        for line in ldd_output.split("\n"):
            search = ldd_re.search(line)
            if search and len(search.groups()) == 2:
                so_path = search.groups()[0]
                so_lib = search.groups()[1]
                libs.append("%s/%s" % (so_path, so_lib))
    except subprocess.CalledProcessError:
        print "%s had no associated libraries (static build?)" % (executable)

    return libs

def _copy_binary_with_libs(src, dest_dir):
    """Copy a binary executable and all its dependant libraries.

    This does rely on the host file-system being fairly multi-arch
    aware so the file don't clash with the guests layout."""

    _copy_with_mkdir(src, dest_dir, "/usr/bin")

    libs = _get_so_libs(src)
    if libs:
        for l in libs:
            so_path = os.path.dirname(l)
            _copy_with_mkdir(l , dest_dir, so_path)

class Docker(object):
    """ Running Docker commands """
    def __init__(self):
        self._command = _guess_docker_command()
        self._instances = []
        atexit.register(self._kill_instances)
        signal.signal(signal.SIGTERM, self._kill_instances)
        signal.signal(signal.SIGHUP, self._kill_instances)

    def _do(self, cmd, quiet=True, infile=None, **kwargs):
        if quiet:
            kwargs["stdout"] = DEVNULL
        if infile:
            kwargs["stdin"] = infile
        return subprocess.call(self._command + cmd, **kwargs)

    def _do_kill_instances(self, only_known, only_active=True):
        cmd = ["ps", "-q"]
        if not only_active:
            cmd.append("-a")
        for i in self._output(cmd).split():
            resp = self._output(["inspect", i])
            labels = json.loads(resp)[0]["Config"]["Labels"]
            active = json.loads(resp)[0]["State"]["Running"]
            if not labels:
                continue
            instance_uuid = labels.get("com.qemu.instance.uuid", None)
            if not instance_uuid:
                continue
            if only_known and instance_uuid not in self._instances:
                continue
            print "Terminating", i
            if active:
                self._do(["kill", i])
            self._do(["rm", i])

    def clean(self):
        self._do_kill_instances(False, False)
        return 0

    def _kill_instances(self, *args, **kwargs):
        return self._do_kill_instances(True)

    def _output(self, cmd, **kwargs):
        return subprocess.check_output(self._command + cmd,
                                       stderr=subprocess.STDOUT,
                                       **kwargs)

    def get_image_dockerfile_checksum(self, tag):
        resp = self._output(["inspect", tag])
        labels = json.loads(resp)[0]["Config"].get("Labels", {})
        return labels.get("com.qemu.dockerfile-checksum", "")

    def build_image(self, tag, docker_dir, dockerfile,
                    quiet=True, user=False, argv=None):
        if argv == None:
            argv = []

        tmp_df = tempfile.NamedTemporaryFile(dir=docker_dir, suffix=".docker")
        tmp_df.write(dockerfile)

        if user:
            uid = os.getuid()
            uname = getpwuid(uid).pw_name
            tmp_df.write("\n")
            tmp_df.write("RUN id %s 2>/dev/null || useradd -u %d -U %s" %
                         (uname, uid, uname))

        tmp_df.write("\n")
        tmp_df.write("LABEL com.qemu.dockerfile-checksum=%s" %
                     _text_checksum(dockerfile))
        tmp_df.flush()

        self._do(["build", "-t", tag, "-f", tmp_df.name] + argv + \
                 [docker_dir],
                 quiet=quiet)

    def update_image(self, tag, tarball, quiet=True):
        "Update a tagged image using "

        self._do(["build", "-t", tag, "-"], quiet=quiet, infile=tarball)

    def image_matches_dockerfile(self, tag, dockerfile):
        try:
            checksum = self.get_image_dockerfile_checksum(tag)
        except Exception:
            return False
        return checksum == _text_checksum(dockerfile)

    def run(self, cmd, keep, quiet):
        label = uuid.uuid1().hex
        if not keep:
            self._instances.append(label)
        ret = self._do(["run", "--label",
                        "com.qemu.instance.uuid=" + label] + cmd,
                       quiet=quiet)
        if not keep:
            self._instances.remove(label)
        return ret

    def command(self, cmd, argv, quiet):
        return self._do([cmd] + argv, quiet=quiet)

class SubCommand(object):
    """A SubCommand template base class"""
    name = None # Subcommand name
    def shared_args(self, parser):
        parser.add_argument("--quiet", action="store_true",
                            help="Run quietly unless an error occured")

    def args(self, parser):
        """Setup argument parser"""
        pass
    def run(self, args, argv):
        """Run command.
        args: parsed argument by argument parser.
        argv: remaining arguments from sys.argv.
        """
        pass

class RunCommand(SubCommand):
    """Invoke docker run and take care of cleaning up"""
    name = "run"
    def args(self, parser):
        parser.add_argument("--keep", action="store_true",
                            help="Don't remove image when command completes")
    def run(self, args, argv):
        return Docker().run(argv, args.keep, quiet=args.quiet)

class BuildCommand(SubCommand):
    """ Build docker image out of a dockerfile. Arguments: <tag> <dockerfile>"""
    name = "build"
    def args(self, parser):
        parser.add_argument("--include-executable", "-e",
                            help="""Specify a binary that will be copied to the
                            container together with all its dependent
                            libraries""")
        parser.add_argument("--add-current-user", "-u", dest="user",
                            action="store_true",
                            help="Add the current user to image's passwd")
        parser.add_argument("tag",
                            help="Image Tag")
        parser.add_argument("dockerfile",
                            help="Dockerfile name")

    def run(self, args, argv):
        dockerfile = open(args.dockerfile, "rb").read()
        tag = args.tag

        dkr = Docker()
        if dkr.image_matches_dockerfile(tag, dockerfile):
            if not args.quiet:
                print "Image is up to date."
        else:
            # Create a docker context directory for the build
            docker_dir = tempfile.mkdtemp(prefix="docker_build")

            # Is there a .pre file to run in the build context?
            docker_pre = os.path.splitext(args.dockerfile)[0]+".pre"
            if os.path.exists(docker_pre):
                stdout = DEVNULL if args.quiet else None
                rc = subprocess.call(os.path.realpath(docker_pre),
                                     cwd=docker_dir, stdout=stdout)
                if rc == 3:
                    print "Skip"
                    return 0
                elif rc != 0:
                    print "%s exited with code %d" % (docker_pre, rc)
                    return 1

            # Do we include a extra binary?
            if args.include_executable:
                _copy_binary_with_libs(args.include_executable,
                                       docker_dir)

            argv += ["--build-arg=" + k.lower() + "=" + v
                        for k, v in os.environ.iteritems()
                        if k.lower() in FILTERED_ENV_NAMES]
            dkr.build_image(tag, docker_dir, dockerfile,
                            quiet=args.quiet, user=args.user, argv=argv)

            rmtree(docker_dir)

        return 0

class UpdateCommand(SubCommand):
    """ Update a docker image with new executables. Arguments: <tag> <executable>"""
    name = "update"
    def args(self, parser):
        parser.add_argument("tag",
                            help="Image Tag")
        parser.add_argument("executable",
                            help="Executable to copy")

    def run(self, args, argv):
        # Create a temporary tarball with our whole build context and
        # dockerfile for the update
        tmp = tempfile.NamedTemporaryFile(suffix="dckr.tar.gz")
        tmp_tar = TarFile(fileobj=tmp, mode='w')

        # Add the executable to the tarball
        bn = os.path.basename(args.executable)
        ff = "/usr/bin/%s" % bn
        tmp_tar.add(args.executable, arcname=ff)

        # Add any associated libraries
        libs = _get_so_libs(args.executable)
        if libs:
            for l in libs:
                tmp_tar.add(os.path.realpath(l), arcname=l)

        # Create a Docker buildfile
        df = StringIO()
        df.write("FROM %s\n" % args.tag)
        df.write("ADD . /\n")
        df.seek(0)

        df_tar = TarInfo(name="Dockerfile")
        df_tar.size = len(df.buf)
        tmp_tar.addfile(df_tar, fileobj=df)

        tmp_tar.close()

        # reset the file pointers
        tmp.flush()
        tmp.seek(0)

        # Run the build with our tarball context
        dkr = Docker()
        dkr.update_image(args.tag, tmp, quiet=args.quiet)

        return 0

class CleanCommand(SubCommand):
    """Clean up docker instances"""
    name = "clean"
    def run(self, args, argv):
        Docker().clean()
        return 0

class ImagesCommand(SubCommand):
    """Run "docker images" command"""
    name = "images"
    def run(self, args, argv):
        return Docker().command("images", argv, args.quiet)

def main():
    parser = argparse.ArgumentParser(description="A Docker helper",
            usage="%s <subcommand> ..." % os.path.basename(sys.argv[0]))
    subparsers = parser.add_subparsers(title="subcommands", help=None)
    for cls in SubCommand.__subclasses__():
        cmd = cls()
        subp = subparsers.add_parser(cmd.name, help=cmd.__doc__)
        cmd.shared_args(subp)
        cmd.args(subp)
        subp.set_defaults(cmdobj=cmd)
    args, argv = parser.parse_known_args()
    return args.cmdobj.run(args, argv)

if __name__ == "__main__":
    sys.exit(main())
