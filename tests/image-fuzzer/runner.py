#!/usr/bin/env python

# Tool for running fuzz tests
#
# Copyright (C) 2014 Maria Kustova <maria.k@catit.be>
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.
#

import sys
import os
import signal
import subprocess
import random
import shutil
from itertools import count
import time
import getopt
import StringIO
import resource

try:
    import json
except ImportError:
    try:
        import simplejson as json
    except ImportError:
        print >>sys.stderr, \
            "Warning: Module for JSON processing is not found.\n" \
            "'--config' and '--command' options are not supported."

# Backing file sizes in MB
MAX_BACKING_FILE_SIZE = 10
MIN_BACKING_FILE_SIZE = 1


def multilog(msg, *output):
    """ Write an object to all of specified file descriptors."""
    for fd in output:
        fd.write(msg)
        fd.flush()


def str_signal(sig):
    """ Convert a numeric value of a system signal to the string one
    defined by the current operational system.
    """
    for k, v in signal.__dict__.items():
        if v == sig:
            return k


def run_app(fd, q_args):
    """Start an application with specified arguments and return its exit code
    or kill signal depending on the result of execution.
    """

    class Alarm(Exception):
        """Exception for signal.alarm events."""
        pass

    def handler(*args):
        """Notify that an alarm event occurred."""
        raise Alarm

    signal.signal(signal.SIGALRM, handler)
    signal.alarm(600)
    term_signal = signal.SIGKILL
    devnull = open('/dev/null', 'r+')
    process = subprocess.Popen(q_args, stdin=devnull,
                               stdout=subprocess.PIPE,
                               stderr=subprocess.PIPE)
    try:
        out, err = process.communicate()
        signal.alarm(0)
        fd.write(out)
        fd.write(err)
        fd.flush()
        return process.returncode

    except Alarm:
        os.kill(process.pid, term_signal)
        fd.write('The command was terminated by timeout.\n')
        fd.flush()
        return -term_signal


class TestException(Exception):
    """Exception for errors risen by TestEnv objects."""
    pass


class TestEnv(object):

    """Test object.

    The class sets up test environment, generates backing and test images
    and executes application under tests with specified arguments and a test
    image provided.

    All logs are collected.

    The summary log will contain short descriptions and statuses of tests in
    a run.

    The test log will include application (e.g. 'qemu-img') logs besides info
    sent to the summary log.
    """

    def __init__(self, test_id, seed, work_dir, run_log,
                 cleanup=True, log_all=False):
        """Set test environment in a specified work directory.

        Path to qemu-img and qemu-io will be retrieved from 'QEMU_IMG' and
        'QEMU_IO' environment variables.
        """
        if seed is not None:
            self.seed = seed
        else:
            self.seed = str(random.randint(0, sys.maxint))
        random.seed(self.seed)

        self.init_path = os.getcwd()
        self.work_dir = work_dir
        self.current_dir = os.path.join(work_dir, 'test-' + test_id)
        self.qemu_img = \
            os.environ.get('QEMU_IMG', 'qemu-img').strip().split(' ')
        self.qemu_io = os.environ.get('QEMU_IO', 'qemu-io').strip().split(' ')
        self.commands = [['qemu-img', 'check', '-f', 'qcow2', '$test_img'],
                         ['qemu-img', 'info', '-f', 'qcow2', '$test_img'],
                         ['qemu-io', '$test_img', '-c', 'read $off $len'],
                         ['qemu-io', '$test_img', '-c', 'write $off $len'],
                         ['qemu-io', '$test_img', '-c',
                          'aio_read $off $len'],
                         ['qemu-io', '$test_img', '-c',
                          'aio_write $off $len'],
                         ['qemu-io', '$test_img', '-c', 'flush'],
                         ['qemu-io', '$test_img', '-c',
                          'discard $off $len'],
                         ['qemu-io', '$test_img', '-c',
                          'truncate $off']]
        for fmt in ['raw', 'vmdk', 'vdi', 'qcow2', 'file', 'qed', 'vpc']:
            self.commands.append(
                ['qemu-img', 'convert', '-f', 'qcow2', '-O', fmt,
                 '$test_img', 'converted_image.' + fmt])

        try:
            os.makedirs(self.current_dir)
        except OSError as e:
            print >>sys.stderr, \
                "Error: The working directory '%s' cannot be used. Reason: %s"\
                % (self.work_dir, e[1])
            raise TestException
        self.log = open(os.path.join(self.current_dir, "test.log"), "w")
        self.parent_log = open(run_log, "a")
        self.failed = False
        self.cleanup = cleanup
        self.log_all = log_all

    def _create_backing_file(self):
        """Create a backing file in the current directory.

        Return a tuple of a backing file name and format.

        Format of a backing file is randomly chosen from all formats supported
        by 'qemu-img create'.
        """
        # All formats supported by the 'qemu-img create' command.
        backing_file_fmt = random.choice(['raw', 'vmdk', 'vdi', 'qcow2',
                                          'file', 'qed', 'vpc'])
        backing_file_name = 'backing_img.' + backing_file_fmt
        backing_file_size = random.randint(MIN_BACKING_FILE_SIZE,
                                           MAX_BACKING_FILE_SIZE) * (1 << 20)
        cmd = self.qemu_img + ['create', '-f', backing_file_fmt,
                               backing_file_name, str(backing_file_size)]
        temp_log = StringIO.StringIO()
        retcode = run_app(temp_log, cmd)
        if retcode == 0:
            temp_log.close()
            return (backing_file_name, backing_file_fmt)
        else:
            multilog("Warning: The %s backing file was not created.\n\n"
                     % backing_file_fmt, sys.stderr, self.log, self.parent_log)
            self.log.write("Log for the failure:\n" + temp_log.getvalue() +
                           '\n\n')
            temp_log.close()
            return (None, None)

    def execute(self, input_commands=None, fuzz_config=None):
        """ Execute a test.

        The method creates backing and test images, runs test app and analyzes
        its exit status. If the application was killed by a signal, the test
        is marked as failed.
        """
        if input_commands is None:
            commands = self.commands
        else:
            commands = input_commands

        os.chdir(self.current_dir)
        backing_file_name, backing_file_fmt = self._create_backing_file()
        img_size = image_generator.create_image(
            'test.img', backing_file_name, backing_file_fmt, fuzz_config)
        for item in commands:
            shutil.copy('test.img', 'copy.img')
            # 'off' and 'len' are multiple of the sector size
            sector_size = 512
            start = random.randrange(0, img_size + 1, sector_size)
            end = random.randrange(start, img_size + 1, sector_size)

            if item[0] == 'qemu-img':
                current_cmd = list(self.qemu_img)
            elif item[0] == 'qemu-io':
                current_cmd = list(self.qemu_io)
            else:
                multilog("Warning: test command '%s' is not defined.\n"
                         % item[0], sys.stderr, self.log, self.parent_log)
                continue
            # Replace all placeholders with their real values
            for v in item[1:]:
                c = (v
                     .replace('$test_img', 'copy.img')
                     .replace('$off', str(start))
                     .replace('$len', str(end - start)))
                current_cmd.append(c)

            # Log string with the test header
            test_summary = "Seed: %s\nCommand: %s\nTest directory: %s\n" \
                           "Backing file: %s\n" \
                           % (self.seed, " ".join(current_cmd),
                              self.current_dir, backing_file_name)
            temp_log = StringIO.StringIO()
            try:
                retcode = run_app(temp_log, current_cmd)
            except OSError as e:
                multilog("%sError: Start of '%s' failed. Reason: %s\n\n"
                         % (test_summary, os.path.basename(current_cmd[0]),
                            e[1]),
                         sys.stderr, self.log, self.parent_log)
                raise TestException

            if retcode < 0:
                self.log.write(temp_log.getvalue())
                multilog("%sFAIL: Test terminated by signal %s\n\n"
                         % (test_summary, str_signal(-retcode)),
                         sys.stderr, self.log, self.parent_log)
                self.failed = True
            else:
                if self.log_all:
                    self.log.write(temp_log.getvalue())
                    multilog("%sPASS: Application exited with the code " \
                             "'%d'\n\n" % (test_summary, retcode),
                             sys.stdout, self.log, self.parent_log)
            temp_log.close()
            os.remove('copy.img')

    def finish(self):
        """Restore the test environment after a test execution."""
        self.log.close()
        self.parent_log.close()
        os.chdir(self.init_path)
        if self.cleanup and not self.failed:
            shutil.rmtree(self.current_dir)

if __name__ == '__main__':

    def usage():
        print """
        Usage: runner.py [OPTION...] TEST_DIR IMG_GENERATOR

        Set up test environment in TEST_DIR and run a test in it. A module for
        test image generation should be specified via IMG_GENERATOR.

        Example:
          runner.py -c '[["qemu-img", "info", "$test_img"]]' /tmp/test qcow2

        Optional arguments:
          -h, --help                    display this help and exit
          -d, --duration=NUMBER         finish tests after NUMBER of seconds
          -c, --command=JSON            run tests for all commands specified in
                                        the JSON array
          -s, --seed=STRING             seed for a test image generation,
                                        by default will be generated randomly
          --config=JSON                 take fuzzer configuration from the JSON
                                        array
          -k, --keep_passed             don't remove folders of passed tests
          -v, --verbose                 log information about passed tests

        JSON:

        '--command' accepts a JSON array of commands. Each command presents
        an application under test with all its parameters as a list of strings,
        e.g. ["qemu-io", "$test_img", "-c", "write $off $len"].

        Supported application aliases: 'qemu-img' and 'qemu-io'.

        Supported argument aliases: $test_img for the fuzzed image, $off
        for an offset, $len for length.

        Values for $off and $len will be generated based on the virtual disk
        size of the fuzzed image.

        Paths to 'qemu-img' and 'qemu-io' are retrevied from 'QEMU_IMG' and
        'QEMU_IO' environment variables.

        '--config' accepts a JSON array of fields to be fuzzed, e.g.
        '[["header"], ["header", "version"]]'.

        Each of the list elements can consist of a complex image element only
        as ["header"] or ["feature_name_table"] or an exact field as
        ["header", "version"]. In the first case random portion of the element
        fields will be fuzzed, in the second one the specified field will be
        fuzzed always.

        If '--config' argument is specified, fields not listed in
        the configuration array will not be fuzzed.
        """

    def run_test(test_id, seed, work_dir, run_log, cleanup, log_all,
                 command, fuzz_config):
        """Setup environment for one test and execute this test."""
        try:
            test = TestEnv(test_id, seed, work_dir, run_log, cleanup,
                           log_all)
        except TestException:
            sys.exit(1)

        # Python 2.4 doesn't support 'finally' and 'except' in the same 'try'
        # block
        try:
            try:
                test.execute(command, fuzz_config)
            except TestException:
                sys.exit(1)
        finally:
            test.finish()

    def should_continue(duration, start_time):
        """Return True if a new test can be started and False otherwise."""
        current_time = int(time.time())
        return (duration is None) or (current_time - start_time < duration)

    try:
        opts, args = getopt.gnu_getopt(sys.argv[1:], 'c:hs:kvd:',
                                       ['command=', 'help', 'seed=', 'config=',
                                        'keep_passed', 'verbose', 'duration='])
    except getopt.error as e:
        print >>sys.stderr, \
            "Error: %s\n\nTry 'runner.py --help' for more information" % e
        sys.exit(1)

    command = None
    cleanup = True
    log_all = False
    seed = None
    config = None
    duration = None
    for opt, arg in opts:
        if opt in ('-h', '--help'):
            usage()
            sys.exit()
        elif opt in ('-c', '--command'):
            try:
                command = json.loads(arg)
            except (TypeError, ValueError, NameError) as e:
                print >>sys.stderr, \
                    "Error: JSON array of test commands cannot be loaded.\n" \
                    "Reason: %s" % e
                sys.exit(1)
        elif opt in ('-k', '--keep_passed'):
            cleanup = False
        elif opt in ('-v', '--verbose'):
            log_all = True
        elif opt in ('-s', '--seed'):
            seed = arg
        elif opt in ('-d', '--duration'):
            duration = int(arg)
        elif opt == '--config':
            try:
                config = json.loads(arg)
            except (TypeError, ValueError, NameError) as e:
                print >>sys.stderr, \
                    "Error: JSON array with the fuzzer configuration cannot" \
                    " be loaded\nReason: %s" % e
                sys.exit(1)

    if not len(args) == 2:
        print >>sys.stderr, \
            "Expected two parameters\nTry 'runner.py --help'" \
            " for more information."
        sys.exit(1)

    work_dir = os.path.realpath(args[0])
    # run_log is created in 'main', because multiple tests are expected to
    # log in it
    run_log = os.path.join(work_dir, 'run.log')

    # Add the path to the image generator module to sys.path
    sys.path.append(os.path.realpath(os.path.dirname(args[1])))
    # Remove a script extension from image generator module if any
    generator_name = os.path.splitext(os.path.basename(args[1]))[0]

    try:
        image_generator = __import__(generator_name)
    except ImportError as e:
        print >>sys.stderr, \
            "Error: The image generator '%s' cannot be imported.\n" \
            "Reason: %s" % (generator_name, e)
        sys.exit(1)

    # Enable core dumps
    resource.setrlimit(resource.RLIMIT_CORE, (-1, -1))
    # If a seed is specified, only one test will be executed.
    # Otherwise runner will terminate after a keyboard interruption
    start_time = int(time.time())
    test_id = count(1)
    while should_continue(duration, start_time):
        try:
            run_test(str(test_id.next()), seed, work_dir, run_log, cleanup,
                     log_all, command, config)
        except (KeyboardInterrupt, SystemExit):
            sys.exit(1)

        if seed is not None:
            break
