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

from __future__ import print_function
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
try:
    from StringIO import StringIO
except ImportError:
    from io import StringIO
from shutil import copy, rmtree
from pwd import getpwuid
from datetime import datetime, timedelta


FILTERED_ENV_NAMES = ['ftp_proxy', 'http_proxy', 'https_proxy']


DEVNULL = open(os.devnull, 'wb')


def _text_checksum(text):
    """Calculate a digest string unique to the text content"""
    return hashlib.sha1(text).hexdigest()


def _file_checksum(filename):
    return _text_checksum(open(filename, 'rb').read())


def _guess_docker_command():
    """ Guess a working docker command or raise exception if not found"""
    commands = [["docker"], ["sudo", "-n", "docker"]]
    for cmd in commands:
        try:
            # docker version will return the client details in stdout
            # but still report a status of 1 if it can't contact the daemon
            if subprocess.call(cmd + ["version"],
                               stdout=DEVNULL, stderr=DEVNULL) == 0:
                return cmd
        except OSError:
            pass
    commands_txt = "\n".join(["  " + " ".join(x) for x in commands])
    raise Exception("Cannot find working docker command. Tried:\n%s" %
                    commands_txt)


def _copy_with_mkdir(src, root_dir, sub_path='.'):
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
        print("%s had no associated libraries (static build?)" % (executable))

    return libs


def _copy_binary_with_libs(src, bin_dest, dest_dir):
    """Maybe copy a binary and all its dependent libraries.

    If bin_dest isn't set we only copy the support libraries because
    we don't need qemu in the docker path to run (due to persistent
    mapping). Indeed users may get confused if we aren't running what
    is in the image.

    This does rely on the host file-system being fairly multi-arch
    aware so the file don't clash with the guests layout.
    """

    if bin_dest:
        _copy_with_mkdir(src, dest_dir, os.path.dirname(bin_dest))
    else:
        print("only copying support libraries for %s" % (src))

    libs = _get_so_libs(src)
    if libs:
        for l in libs:
            so_path = os.path.dirname(l)
            _copy_with_mkdir(l, dest_dir, so_path)


def _check_binfmt_misc(executable):
    """Check binfmt_misc has entry for executable in the right place.

    The details of setting up binfmt_misc are outside the scope of
    this script but we should at least fail early with a useful
    message if it won't work.

    Returns the configured binfmt path and a valid flag. For
    persistent configurations we will still want to copy and dependent
    libraries.
    """

    binary = os.path.basename(executable)
    binfmt_entry = "/proc/sys/fs/binfmt_misc/%s" % (binary)

    if not os.path.exists(binfmt_entry):
        print ("No binfmt_misc entry for %s" % (binary))
        return None, False

    with open(binfmt_entry) as x: entry = x.read()

    if re.search("flags:.*F.*\n", entry):
        print("binfmt_misc for %s uses persistent(F) mapping to host binary" %
              (binary))
        return None, True

    m = re.search("interpreter (\S+)\n", entry)
    interp = m.group(1)
    if interp and interp != executable:
        print("binfmt_misc for %s does not point to %s, using %s" %
              (binary, executable, interp))

    return interp, True


def _read_qemu_dockerfile(img_name):
    # special case for Debian linux-user images
    if img_name.startswith("debian") and img_name.endswith("user"):
        img_name = "debian-bootstrap"

    df = os.path.join(os.path.dirname(__file__), "dockerfiles",
                      img_name + ".docker")
    return open(df, "r").read()


def _dockerfile_preprocess(df):
    out = ""
    for l in df.splitlines():
        if len(l.strip()) == 0 or l.startswith("#"):
            continue
        from_pref = "FROM qemu:"
        if l.startswith(from_pref):
            # TODO: Alternatively we could replace this line with "FROM $ID"
            # where $ID is the image's hex id obtained with
            #    $ docker images $IMAGE --format="{{.Id}}"
            # but unfortunately that's not supported by RHEL 7.
            inlining = _read_qemu_dockerfile(l[len(from_pref):])
            out += _dockerfile_preprocess(inlining)
            continue
        out += l + "\n"
    return out


class Docker(object):
    """ Running Docker commands """
    def __init__(self):
        self._command = _guess_docker_command()
        self._instances = []
        atexit.register(self._kill_instances)
        signal.signal(signal.SIGTERM, self._kill_instances)
        signal.signal(signal.SIGHUP, self._kill_instances)

    def _do(self, cmd, quiet=True, **kwargs):
        if quiet:
            kwargs["stdout"] = DEVNULL
        return subprocess.call(self._command + cmd, **kwargs)

    def _do_check(self, cmd, quiet=True, **kwargs):
        if quiet:
            kwargs["stdout"] = DEVNULL
        return subprocess.check_call(self._command + cmd, **kwargs)

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
            print("Terminating", i)
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

    def inspect_tag(self, tag):
        try:
            return self._output(["inspect", tag])
        except subprocess.CalledProcessError:
            return None

    def get_image_creation_time(self, info):
        return json.loads(info)[0]["Created"]

    def get_image_dockerfile_checksum(self, tag):
        resp = self.inspect_tag(tag)
        labels = json.loads(resp)[0]["Config"].get("Labels", {})
        return labels.get("com.qemu.dockerfile-checksum", "")

    def build_image(self, tag, docker_dir, dockerfile,
                    quiet=True, user=False, argv=None, extra_files_cksum=[]):
        if argv is None:
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
                     _text_checksum(_dockerfile_preprocess(dockerfile)))
        for f, c in extra_files_cksum:
            tmp_df.write("LABEL com.qemu.%s-checksum=%s" % (f, c))

        tmp_df.flush()

        self._do_check(["build", "-t", tag, "-f", tmp_df.name] + argv +
                       [docker_dir],
                       quiet=quiet)

    def update_image(self, tag, tarball, quiet=True):
        "Update a tagged image using "

        self._do_check(["build", "-t", tag, "-"], quiet=quiet, stdin=tarball)

    def image_matches_dockerfile(self, tag, dockerfile):
        try:
            checksum = self.get_image_dockerfile_checksum(tag)
        except Exception:
            return False
        return checksum == _text_checksum(_dockerfile_preprocess(dockerfile))

    def run(self, cmd, keep, quiet):
        label = uuid.uuid1().hex
        if not keep:
            self._instances.append(label)
        ret = self._do_check(["run", "--label",
                             "com.qemu.instance.uuid=" + label] + cmd,
                             quiet=quiet)
        if not keep:
            self._instances.remove(label)
        return ret

    def command(self, cmd, argv, quiet):
        return self._do([cmd] + argv, quiet=quiet)


class SubCommand(object):
    """A SubCommand template base class"""
    name = None  # Subcommand name

    def shared_args(self, parser):
        parser.add_argument("--quiet", action="store_true",
                            help="Run quietly unless an error occurred")

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
    """ Build docker image out of a dockerfile. Arg: <tag> <dockerfile>"""
    name = "build"

    def args(self, parser):
        parser.add_argument("--include-executable", "-e",
                            help="""Specify a binary that will be copied to the
                            container together with all its dependent
                            libraries""")
        parser.add_argument("--extra-files", "-f", nargs='*',
                            help="""Specify files that will be copied in the
                            Docker image, fulfilling the ADD directive from the
                            Dockerfile""")
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
        if "--no-cache" not in argv and \
           dkr.image_matches_dockerfile(tag, dockerfile):
            if not args.quiet:
                print("Image is up to date.")
        else:
            # Create a docker context directory for the build
            docker_dir = tempfile.mkdtemp(prefix="docker_build")

            # Validate binfmt_misc will work
            if args.include_executable:
                qpath, enabled = _check_binfmt_misc(args.include_executable)
                if not enabled:
                    return 1

            # Is there a .pre file to run in the build context?
            docker_pre = os.path.splitext(args.dockerfile)[0]+".pre"
            if os.path.exists(docker_pre):
                stdout = DEVNULL if args.quiet else None
                rc = subprocess.call(os.path.realpath(docker_pre),
                                     cwd=docker_dir, stdout=stdout)
                if rc == 3:
                    print("Skip")
                    return 0
                elif rc != 0:
                    print("%s exited with code %d" % (docker_pre, rc))
                    return 1

            # Copy any extra files into the Docker context. These can be
            # included by the use of the ADD directive in the Dockerfile.
            cksum = []
            if args.include_executable:
                # FIXME: there is no checksum of this executable and the linked
                # libraries, once the image built any change of this executable
                # or any library won't trigger another build.
                _copy_binary_with_libs(args.include_executable,
                                       qpath, docker_dir)

            for filename in args.extra_files or []:
                _copy_with_mkdir(filename, docker_dir)
                cksum += [(filename, _file_checksum(filename))]

            argv += ["--build-arg=" + k.lower() + "=" + v
                     for k, v in os.environ.iteritems()
                     if k.lower() in FILTERED_ENV_NAMES]
            dkr.build_image(tag, docker_dir, dockerfile,
                            quiet=args.quiet, user=args.user, argv=argv,
                            extra_files_cksum=cksum)

            rmtree(docker_dir)

        return 0


class UpdateCommand(SubCommand):
    """ Update a docker image with new executables. Args: <tag> <executable>"""
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

        # Add the executable to the tarball, using the current
        # configured binfmt_misc path. If we don't get a path then we
        # only need the support libraries copied
        ff, enabled = _check_binfmt_misc(args.executable)

        if not enabled:
            print("binfmt_misc not enabled, update disabled")
            return 1

        if ff:
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


class ProbeCommand(SubCommand):
    """Probe if we can run docker automatically"""
    name = "probe"

    def run(self, args, argv):
        try:
            docker = Docker()
            if docker._command[0] == "docker":
                print("yes")
            elif docker._command[0] == "sudo":
                print("sudo")
        except Exception:
            print("no")

        return


class CcCommand(SubCommand):
    """Compile sources with cc in images"""
    name = "cc"

    def args(self, parser):
        parser.add_argument("--image", "-i", required=True,
                            help="The docker image in which to run cc")
        parser.add_argument("--cc", default="cc",
                            help="The compiler executable to call")
        parser.add_argument("--user",
                            help="The user-id to run under")
        parser.add_argument("--source-path", "-s", nargs="*", dest="paths",
                            help="""Extra paths to (ro) mount into container for
                            reading sources""")

    def run(self, args, argv):
        if argv and argv[0] == "--":
            argv = argv[1:]
        cwd = os.getcwd()
        cmd = ["--rm", "-w", cwd,
               "-v", "%s:%s:rw" % (cwd, cwd)]
        if args.paths:
            for p in args.paths:
                cmd += ["-v", "%s:%s:ro,z" % (p, p)]
        if args.user:
            cmd += ["-u", args.user]
        cmd += [args.image, args.cc]
        cmd += argv
        return Docker().command("run", cmd, args.quiet)


class CheckCommand(SubCommand):
    """Check if we need to re-build a docker image out of a dockerfile.
    Arguments: <tag> <dockerfile>"""
    name = "check"

    def args(self, parser):
        parser.add_argument("tag",
                            help="Image Tag")
        parser.add_argument("dockerfile", default=None,
                            help="Dockerfile name", nargs='?')
        parser.add_argument("--checktype", choices=["checksum", "age"],
                            default="checksum", help="check type")
        parser.add_argument("--olderthan", default=60, type=int,
                            help="number of minutes")

    def run(self, args, argv):
        tag = args.tag

        try:
            dkr = Docker()
        except subprocess.CalledProcessError:
            print("Docker not set up")
            return 1

        info = dkr.inspect_tag(tag)
        if info is None:
            print("Image does not exist")
            return 1

        if args.checktype == "checksum":
            if not args.dockerfile:
                print("Need a dockerfile for tag:%s" % (tag))
                return 1

            dockerfile = open(args.dockerfile, "rb").read()

            if dkr.image_matches_dockerfile(tag, dockerfile):
                if not args.quiet:
                    print("Image is up to date")
                return 0
            else:
                print("Image needs updating")
                return 1
        elif args.checktype == "age":
            timestr = dkr.get_image_creation_time(info).split(".")[0]
            created = datetime.strptime(timestr, "%Y-%m-%dT%H:%M:%S")
            past = datetime.now() - timedelta(minutes=args.olderthan)
            if created < past:
                print ("Image created @ %s more than %d minutes old" %
                       (timestr, args.olderthan))
                return 1
            else:
                if not args.quiet:
                    print ("Image less than %d minutes old" % (args.olderthan))
                return 0


def main():
    parser = argparse.ArgumentParser(description="A Docker helper",
                                     usage="%s <subcommand> ..." %
                                     os.path.basename(sys.argv[0]))
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
