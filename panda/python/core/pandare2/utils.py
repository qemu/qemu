# Helper utilities functions and classes for use in pypanda.
'''
Misc helper functions
'''

from colorama import Fore, Style
from functools import wraps
from os import devnull
from subprocess import check_call, STDOUT
from sys import platform, stdout
from threading import current_thread, main_thread

#for _find_build_path
from os import dup, getenv, environ, path
from os.path import realpath, isfile, dirname, join as pjoin
# for rr2 format
import  tarfile

# Set to enable pypanda debugging
debug = False

def progress(msg):
    """
    Print a message with a green "[PYPANDA]" prefix if in a tty
    otherwise just print the message
    """
    if stdout.isatty():
        print(Fore.GREEN + '[PYPANDA] ' + Fore.RESET + Style.BRIGHT + msg +Style.RESET_ALL)
    else:
        print(f"[PYPANDA] {msg}")

def warn(msg):
    """
    Print a message with a red "[PYPANDA]" prefix if in a tty
    otherwise just print the message
    """
    if stdout.isatty():
        print(Fore.RED + '[PYPANDA] ' + Fore.RESET + Style.BRIGHT + msg +Style.RESET_ALL)
    else:
        print(f"[PYPANDA] {msg}")

def make_iso(directory, iso_path):
    '''
    Generate an iso from a directory
    '''
    with open(devnull, "w") as DEVNULL:
        if platform.startswith('linux'):
            check_call([
                'genisoimage', '-RJ', '-max-iso9660-filenames', '-o', iso_path, directory
            ], stderr=STDOUT if debug else DEVNULL)
        elif platform == 'darwin':
            check_call([
                'hdiutil', 'makehybrid', '-hfs', '-joliet', '-iso', '-o', iso_path, directory
            ], stderr=STDOUT if debug else DEVNULL)
        else:
            raise NotImplementedError("Unsupported operating system!")

def telescope(panda, cpu, val):
    '''
    Given a value, check if it's a pointer by seeing if we can map it to physical memory.
    If so, recursively print where it points
    to until
    1) It points to a string (then print the string)
    2) It's code (then disassembly the insn)
    3) It's an invalid pointer
    4) It's the 5th time we've done this, break

    TODO Should use memory protections to determine string/code/data
    '''
    for _ in range(5): # Max chain of 5
        print("-> 0x{:0>8x}".format(val), end="\t")

        if val == 0:
            print()
            return
        # Consider that val points to a string. Test and print
        try:
            str_data = panda.virtual_memory_read(cpu, val, 16)
        except ValueError:
            print()
            return

        str_val = ""
        for d in str_data:
            if d >= 0x20 and d < 0x7F:
                str_val += chr(d)
            else:
                break
        if len(str_val) > 2:
            print("== \"{}\"".format(str_val))
            return


        data = str_data[:4] # Truncate to 4 bytes
        val = int.from_bytes(data, byteorder='little')

    print("-> ...")

def blocking(func):
    """
    Decorator to ensure a function isn't run in the main thread
    """
    @wraps(func)
    def wrapper(*args, **kwargs):
        assert (current_thread() is not main_thread()), "Blocking function run in main thread"
        return func(*args, **kwargs)
    wrapper.__blocking__ = True
    wrapper.__name__ = func.__name__ + " (with async thread)"
    return wrapper


def rr2_name(name):
    return name if name.endswith(".rr2") else name + ".rr2"

def rr2_recording(name):
    def is_gzip(name):
        rr = open(name, "rb")
        return rr.read(2) == b'\x1f\x8b'

    rr2_filename = rr2_name(name)
    if isfile(rr2_filename) and is_gzip(rr2_filename):
        return True
    return False

def rr2_contains_member(name, member):
    rr2_filename = rr2_name(name)
    contains_member = False
    if rr2_recording(rr2_filename):
        try:
            tar = tarfile.open(rr2_filename)
            tar.getmember(member)
            contains_member = True
        except (KeyError, IsADirectoryError, FileNotFoundError, tarfile.ReadError):
            pass
    return contains_member

class GArrayIterator():
    '''
    Iterator which will run a function on each iteration incrementing
    the second argument. Useful for GArrays with an accessor function
    that takes arguments of the GArray and list index. e.g., osi's
    get_one_module.
    '''
    def __init__(self, func, garray, garray_len, cleanup_fn):
        self.garray = garray
        self.garray_len = garray_len
        self.current_idx = 0
        self.func = func
        self.cleanup_func = cleanup_fn

    def __iter__(self):
        self.current_idx = 0
        return self

    def __next__(self):
        if self.current_idx >= self.garray_len:
            raise StopIteration
        # Would need to make this configurable before using MappingIter with other types
        ret = self.func(self.garray, self.current_idx)
        self.current_idx += 1
        return ret

    def __del__(self):
        self.cleanup_func(self.garray)

class plugin_list(dict):
    '''
    Wrapper class around list of active C plugins
    '''
    def __init__(self,panda):
        self._panda = panda
        super().__init__()
    def __getitem__(self,plugin_name):
        if plugin_name not in self:
            self._panda.load_plugin(plugin_name)
        return super().__getitem__(plugin_name)

def _find_build_dir(arch_name, find_executable=False):
    '''
    Internal function to return the build directory for the specified architecture
    '''

    system_build = "/usr/local/bin/"
    python_package = pjoin(*[dirname(__file__), "data"])
    local_build = realpath(pjoin(dirname(__file__), "../../../../build"))


    arch_dir = f"{arch_name}-softmmu"
    file_name = f"panda-system-{arch_name}" if find_executable else \
                f"libpanda-{arch_name}.so"

    # system path could have panda-system-X or libpanda-X.so. Others would have an arch_name - softmmu directory
    pot_paths = [system_build,
                 pjoin(python_package, arch_dir),
                 pjoin(local_build, arch_dir)]

    if find_executable and 'PATH' in environ:
        # If we're looking for the panda executable, also search the user's path
        pot_paths.extend(environ.get('PATH').split(":"))

    for potential_path in pot_paths:
        if isfile(pjoin(potential_path, file_name)):
            if not find_executable:
                # potential_path may contain [arch]-softmmu/ which
                # we shouldn't return unless we're looking for an executable's
                # build dir
                potential_path = potential_path.replace(arch_dir, "")
            return potential_path

    searched_paths = "\n".join(["\t"+p for p in  pot_paths])
    raise RuntimeError((f"Couldn't find {file_name}\n"
                        f"Did you built PANDA for this architecture?\n"
                        f"Searched for {arch_dir}/{file_name} in:\n{searched_paths}"))


def find_build_dir(arch_name=None, find_executable=False):
    '''
    Find directory containing the binaries we care about (i.e., ~git/panda/build). If
    find_executable is False, we're looking for [arch]-softmmu/libpanda-[arch].so. If
    find_executable is True, we're looking for [arch]-softmmu/panda-system-[arch] and we'll return
    the parent dir of the executable (i.e., ~/git/panda/build/x86_64-softmmu/)

    We do this by searching paths in the following order:
        1) Check relative to file (in the case of installed packages)
        2) Check in../ ../../../build/
        2) Search path if user is looking for an executable instead of a library
        3) Raise RuntimeError if we find nothing

    If arch_name is none, we'll search for any supported architecture and return the first
    one we find.
    '''
    arches = ['i386', 'x86_64', 'arm', 'aarch64', 'ppc', 'mips', 'mipsel', 'mips64', 'mips64el']

    if arch_name is None:
        e = None
        for arch in arches:
            try:
                return _find_build_dir(arch, find_executable)
            except RuntimeError as _e:
                e = _e
        if e:
            raise e

    elif arch_name not in arches:
        raise ValueError(f"Unsupported architecture name: {arch_name}, allowed values are: {arches}")
    return _find_build_dir(arch_name, find_executable)
