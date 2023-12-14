#!/usr/bin/env python3
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
import enum
import tempfile
import re
import signal
import getpass
from tarfile import TarFile, TarInfo
from io import StringIO, BytesIO
from shutil import copy, rmtree
from datetime import datetime, timedelta


FILTERED_ENV_NAMES = ['ftp_proxy', 'http_proxy', 'https_proxy']


DEVNULL = open(os.devnull, 'wb')

class EngineEnum(enum.IntEnum):
    AUTO = 1
    DOCKER = 2
    PODMAN = 3

    def __str__(self):
        return self.name.lower()

    def __repr__(self):
        return str(self)

    @staticmethod
    def argparse(s):
        try:
            return EngineEnum[s.upper()]
        except KeyError:
            return s


USE_ENGINE = EngineEnum.AUTO

def _bytes_checksum(bytes):
    """Calculate a digest string unique to the text content"""
    return hashlib.sha1(bytes).hexdigest()

def _text_checksum(text):
    """Calculate a digest string unique to the text content"""
    return _bytes_checksum(text.encode('utf-8'))

def _read_dockerfile(path):
    return open(path, 'rt', encoding='utf-8').read()

def _file_checksum(filename):
    return _bytes_checksum(open(filename, 'rb').read())


def _guess_engine_command():
    """ Guess a working engine command or raise exception if not found"""
    commands = []

    if USE_ENGINE in [EngineEnum.AUTO, EngineEnum.PODMAN]:
        commands += [["podman"]]
    if USE_ENGINE in [EngineEnum.AUTO, EngineEnum.DOCKER]:
        commands += [["docker"], ["sudo", "-n", "docker"]]
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
    raise Exception("Cannot find working engine command. Tried:\n%s" %
                    commands_txt)


def _copy_with_mkdir(src, root_dir, sub_path='.', name=None):
    """Copy src into root_dir, creating sub_path as needed."""
    dest_dir = os.path.normpath("%s/%s" % (root_dir, sub_path))
    try:
        os.makedirs(dest_dir)
    except OSError:
        # we can safely ignore already created directories
        pass

    dest_file = "%s/%s" % (dest_dir, name if name else os.path.basename(src))

    try:
        copy(src, dest_file)
    except FileNotFoundError:
        print("Couldn't copy %s to %s" % (src, dest_file))
        pass


def _get_so_libs(executable):
    """Return a list of libraries associated with an executable.

    The paths may be symbolic links which would need to be resolved to
    ensure the right data is copied."""

    libs = []
    ldd_re = re.compile(r"(?:\S+ => )?(\S*) \(:?0x[0-9a-f]+\)")
    try:
        ldd_output = subprocess.check_output(["ldd", executable]).decode('utf-8')
        for line in ldd_output.split("\n"):
            search = ldd_re.search(line)
            if search:
                try:
                    libs.append(search.group(1))
                except IndexError:
                    pass
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
            name = os.path.basename(l)
            real_l = os.path.realpath(l)
            _copy_with_mkdir(real_l, dest_dir, so_path, name)


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

    m = re.search(r"interpreter (\S+)\n", entry)
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
    return _read_dockerfile(df)


def _dockerfile_verify_flat(df):
    "Verify we do not include other qemu/ layers"
    for l in df.splitlines():
        if len(l.strip()) == 0 or l.startswith("#"):
            continue
        from_pref = "FROM qemu/"
        if l.startswith(from_pref):
            print("We no longer support multiple QEMU layers.")
            print("Dockerfiles should be flat, ideally created by lcitool")
            return False
    return True


class Docker(object):
    """ Running Docker commands """
    def __init__(self):
        self._command = _guess_engine_command()

        if ("docker" in self._command and
            "TRAVIS" not in os.environ and
            "GITLAB_CI" not in os.environ):
            os.environ["DOCKER_BUILDKIT"] = "1"
            self._buildkit = True
        else:
            self._buildkit = False

        self._instance = None
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

        filter = "--filter=label=com.qemu.instance.uuid"
        if only_known:
            if self._instance:
                filter += "=%s" % (self._instance)
            else:
                # no point trying to kill, we finished
                return

        print("filter=%s" % (filter))
        cmd.append(filter)
        for i in self._output(cmd).split():
            self._do(["rm", "-f", i])

    def clean(self):
        self._do_kill_instances(False, False)
        return 0

    def _kill_instances(self, *args, **kwargs):
        return self._do_kill_instances(True)

    def _output(self, cmd, **kwargs):
        try:
            return subprocess.check_output(self._command + cmd,
                                           stderr=subprocess.STDOUT,
                                           encoding='utf-8',
                                           **kwargs)
        except TypeError:
            # 'encoding' argument was added in 3.6+
            return subprocess.check_output(self._command + cmd,
                                           stderr=subprocess.STDOUT,
                                           **kwargs).decode('utf-8')


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
                    quiet=True, user=False, argv=None, registry=None,
                    extra_files_cksum=[]):
        if argv is None:
            argv = []

        if not _dockerfile_verify_flat(dockerfile):
            return -1

        checksum = _text_checksum(dockerfile)

        tmp_df = tempfile.NamedTemporaryFile(mode="w+t",
                                             encoding='utf-8',
                                             dir=docker_dir, suffix=".docker")
        tmp_df.write(dockerfile)

        if user:
            uid = os.getuid()
            uname = getpass.getuser()
            tmp_df.write("\n")
            tmp_df.write("RUN id %s 2>/dev/null || useradd -u %d -U %s" %
                         (uname, uid, uname))

        tmp_df.write("\n")
        tmp_df.write("LABEL com.qemu.dockerfile-checksum=%s\n" % (checksum))
        for f, c in extra_files_cksum:
            tmp_df.write("LABEL com.qemu.%s-checksum=%s\n" % (f, c))

        tmp_df.flush()

        build_args = ["build", "-t", tag, "-f", tmp_df.name]
        if self._buildkit:
            build_args += ["--build-arg", "BUILDKIT_INLINE_CACHE=1"]

        if registry is not None:
            pull_args = ["pull", "%s/%s" % (registry, tag)]
            self._do(pull_args, quiet=quiet)
            cache = "%s/%s" % (registry, tag)
            build_args += ["--cache-from", cache]
        build_args += argv
        build_args += [docker_dir]

        self._do_check(build_args,
                       quiet=quiet)

    def update_image(self, tag, tarball, quiet=True):
        "Update a tagged image using "

        self._do_check(["build", "-t", tag, "-"], quiet=quiet, stdin=tarball)

    def image_matches_dockerfile(self, tag, dockerfile):
        try:
            checksum = self.get_image_dockerfile_checksum(tag)
        except Exception:
            return False
        return checksum == _text_checksum(dockerfile)

    def run(self, cmd, keep, quiet, as_user=False):
        label = uuid.uuid4().hex
        if not keep:
            self._instance = label

        if as_user:
            uid = os.getuid()
            cmd = [ "-u", str(uid) ] + cmd
            # podman requires a bit more fiddling
            if self._command[0] == "podman":
                cmd.insert(0, '--userns=keep-id')

        ret = self._do_check(["run", "--rm", "--label",
                             "com.qemu.instance.uuid=" + label] + cmd,
                             quiet=quiet)
        if not keep:
            self._instance = None
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
        parser.add_argument("--run-as-current-user", action="store_true",
                            help="Run container using the current user's uid")

    def run(self, args, argv):
        return Docker().run(argv, args.keep, quiet=args.quiet,
                            as_user=args.run_as_current_user)


class BuildCommand(SubCommand):
    """ Build docker image out of a dockerfile. Arg: <tag> <dockerfile>"""
    name = "build"

    def args(self, parser):
        parser.add_argument("--include-executable", "-e",
                            help="""Specify a binary that will be copied to the
                            container together with all its dependent
                            libraries""")
        parser.add_argument("--skip-binfmt",
                            action="store_true",
                            help="""Skip binfmt entry check (used for testing)""")
        parser.add_argument("--extra-files", nargs='*',
                            help="""Specify files that will be copied in the
                            Docker image, fulfilling the ADD directive from the
                            Dockerfile""")
        parser.add_argument("--add-current-user", "-u", dest="user",
                            action="store_true",
                            help="Add the current user to image's passwd")
        parser.add_argument("--registry", "-r",
                            help="cache from docker registry")
        parser.add_argument("-t", dest="tag",
                            help="Image Tag")
        parser.add_argument("-f", dest="dockerfile",
                            help="Dockerfile name")

    def run(self, args, argv):
        dockerfile = _read_dockerfile(args.dockerfile)
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
            if args.skip_binfmt:
                qpath = args.include_executable
            elif args.include_executable:
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
                     for k, v in os.environ.items()
                     if k.lower() in FILTERED_ENV_NAMES]
            dkr.build_image(tag, docker_dir, dockerfile,
                            quiet=args.quiet, user=args.user,
                            argv=argv, registry=args.registry,
                            extra_files_cksum=cksum)

            rmtree(docker_dir)

        return 0

class FetchCommand(SubCommand):
    """ Fetch a docker image from the registry. Args: <tag> <registry>"""
    name = "fetch"

    def args(self, parser):
        parser.add_argument("tag",
                            help="Local tag for image")
        parser.add_argument("registry",
                            help="Docker registry")

    def run(self, args, argv):
        dkr = Docker()
        dkr.command(cmd="pull", quiet=args.quiet,
                    argv=["%s/%s" % (args.registry, args.tag)])
        dkr.command(cmd="tag", quiet=args.quiet,
                    argv=["%s/%s" % (args.registry, args.tag), args.tag])


class UpdateCommand(SubCommand):
    """ Update a docker image. Args: <tag> <actions>"""
    name = "update"

    def args(self, parser):
        parser.add_argument("tag",
                            help="Image Tag")
        parser.add_argument("--executable",
                            help="Executable to copy")
        parser.add_argument("--add-current-user", "-u", dest="user",
                            action="store_true",
                            help="Add the current user to image's passwd")

    def run(self, args, argv):
        # Create a temporary tarball with our whole build context and
        # dockerfile for the update
        tmp = tempfile.NamedTemporaryFile(suffix="dckr.tar.gz")
        tmp_tar = TarFile(fileobj=tmp, mode='w')

        # Create a Docker buildfile
        df = StringIO()
        df.write(u"FROM %s\n" % args.tag)

        if args.executable:
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
                    so_path = os.path.dirname(l)
                    name = os.path.basename(l)
                    real_l = os.path.realpath(l)
                    try:
                        tmp_tar.add(real_l, arcname="%s/%s" % (so_path, name))
                    except FileNotFoundError:
                        print("Couldn't add %s/%s to archive" % (so_path, name))
                        pass

            df.write(u"ADD . /\n")

        if args.user:
            uid = os.getuid()
            uname = getpass.getuser()
            df.write("\n")
            df.write("RUN id %s 2>/dev/null || useradd -u %d -U %s" %
                     (uname, uid, uname))

        df_bytes = BytesIO(bytes(df.getvalue(), "UTF-8"))

        df_tar = TarInfo(name="Dockerfile")
        df_tar.size = df_bytes.getbuffer().nbytes
        tmp_tar.addfile(df_tar, fileobj=df_bytes)

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
                print("docker")
            elif docker._command[0] == "sudo":
                print("sudo docker")
            elif docker._command[0] == "podman":
                print("podman")
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
        parser.add_argument("--source-path", "-s", nargs="*", dest="paths",
                            help="""Extra paths to (ro) mount into container for
                            reading sources""")

    def run(self, args, argv):
        if argv and argv[0] == "--":
            argv = argv[1:]
        cwd = os.getcwd()
        cmd = ["-w", cwd,
               "-v", "%s:%s:rw" % (cwd, cwd)]
        if args.paths:
            for p in args.paths:
                cmd += ["-v", "%s:%s:ro,z" % (p, p)]
        cmd += [args.image, args.cc]
        cmd += argv
        return Docker().run(cmd, False, quiet=args.quiet,
                            as_user=True)


def main():
    global USE_ENGINE

    parser = argparse.ArgumentParser(description="A Docker helper",
                                     usage="%s <subcommand> ..." %
                                     os.path.basename(sys.argv[0]))
    parser.add_argument("--engine", type=EngineEnum.argparse, choices=list(EngineEnum),
                        help="specify which container engine to use")
    subparsers = parser.add_subparsers(title="subcommands", help=None)
    for cls in SubCommand.__subclasses__():
        cmd = cls()
        subp = subparsers.add_parser(cmd.name, help=cmd.__doc__)
        cmd.shared_args(subp)
        cmd.args(subp)
        subp.set_defaults(cmdobj=cmd)
    args, argv = parser.parse_known_args()
    if args.engine:
        USE_ENGINE = args.engine
    return args.cmdobj.run(args, argv)


if __name__ == "__main__":
    sys.exit(main())
