#!/usr/bin/env python
# Install with python setup.py (develop|install)

from setuptools import setup, find_packages
from setuptools.command.install import install as install_orig
from setuptools.command.develop import develop as develop_orig
from setuptools.dist import Distribution
from subprocess import check_output

import os
import shutil
################################################
# 1) Copy panda object files: libpanda-XYZ.so, #
#    pc-bios/*, all .so files for plugins,     #
#    pypanda's include directory, llvm-helpers #
################################################

root_dir = os.path.join(*[os.path.dirname(__file__), "..", "..", ".."]) # panda-git/ root dir

pypi_build = False # Set to true if trying to minimize size for pypi package upload. Note this disables some architectures

lib_dir = os.path.join("pandare", "data")
def copy_objs():
    '''
    Run to copy objects into a (local and temporary) python module before installing to the system.
    Shouldn't be run if you're just installing in develop mode
    '''
    build_root = os.path.join(root_dir, "build")

    if os.path.isdir(lib_dir):
        assert('panda' in lib_dir), "Refusing to rm -rf directory without 'panda' in it"
        shutil.rmtree(lib_dir)
    os.mkdir(lib_dir)

    # Copy bios directory
    biosdir = os.path.join(root_dir, "pc-bios")
    if not os.path.isdir(biosdir):
        raise RuntimeError(f"Could not find PC-bios directory at {biosdir}")
    shutil.copytree(biosdir, lib_dir+"/pc-bios")

    # Copy pypanda's include directory (different than core panda's) into a datadir
    pypanda_inc = os.path.join(*[root_dir, "panda", "python", "core", "pandare", "include"])
    if not os.path.isdir(pypanda_inc):
        raise RuntimeError(f"Could not find pypanda include directory at {pypanda_inc}")
    pypanda_inc_dest = os.path.join(*["pandare", "data", "pypanda", "include"])
    if os.path.isdir(pypanda_inc_dest):
        shutil.rmtree(pypanda_inc_dest)
    shutil.copytree(pypanda_inc, pypanda_inc_dest)

    # Check if we have llvm-support
    with open(os.path.join(*[build_root, 'config-host.mak']), 'r') as cfg:
        llvm_enabled = True if 'CONFIG_LLVM=y' in cfg.read() else False

    # For each arch, copy library, plugins, plog_pb2.py and llvm-helpers
    arches = ['arm', 'aarch64', 'i386', 'x86_64', 'ppc', 'mips', 'mipsel', 'mips64']
    if pypi_build:
        # Nobody really wants mips32 anymore, shrink our distribution size by dropping
        arches = ['arm', 'aarch64', 'i386', 'x86_64', 'ppc', 'mips64']

    for arch in arches:
        libname = "libpanda-"+arch+".so"
        softmmu = arch+"-softmmu"
        path      = os.path.join(*[build_root, softmmu, libname])
        plugindir = os.path.join(*[build_root, softmmu, "panda", "plugins"])
        llvm1     = os.path.join(*[build_root, softmmu, "llvm-helpers.bc1"])
        llvm2     = os.path.join(*[build_root, softmmu, f"llvm-helpers-{arch}.bc"])

        if os.path.isfile(path) is False:
            print(("Missing file {} - did you run build.sh from panda/build directory?\n"
                   "Skipping building pypanda for {}").format(path, arch))
            continue

        os.mkdir(os.path.join(lib_dir, softmmu))

        new_plugindir = os.path.join(lib_dir, softmmu, "panda/plugins")
        os.mkdir(os.path.dirname(new_plugindir)) # When we copy the whole tree, it will make the plugins directory

        shutil.copy(        path,       os.path.join(lib_dir, softmmu))
        if llvm_enabled:
            shutil.copy(    llvm1,      os.path.join(lib_dir, softmmu))
            shutil.copy(    llvm2,      os.path.join(lib_dir, softmmu))

        shutil.copytree(plugindir,  new_plugindir, ignore=shutil.ignore_patterns('*.o', '*.d'))

    # Strip libpandas and plugins to save space (Need <100mb for pypi)
    if pypi_build:
        check_output(f"find {lib_dir} -type f -executable -exec strip {{}} \;", shell=True)


#########################
# 3)  Build the package #
#########################

from setuptools.command.install import install as install_orig
from setuptools.command.develop import develop as develop_orig
class custom_develop(develop_orig):
    '''
    Install as a local module (not to system) by
        1) Creating datatype files for local-use
        2) Running regular setup tools logic
    '''
    def run(self):
        # Delete pandare/data in the case of `setup.py develop`
        # Don't copy objects, use them in the current path
        if os.path.isdir(lib_dir):
            assert('panda' in lib_dir), "Refusing to rm -rf directory without 'panda' in it"
            shutil.rmtree(lib_dir)
        from create_panda_datatypes import main as create_datatypes
        create_datatypes(install=False)
        print("made datatypes")
        super().run()

class custom_install(install_orig):
    '''
    We're going to install to the system. Two possible states to handle
    1) Running from within the panda repo with panda built - need to create_datatypes
    2) Running from a python sdist where all the files are already prepared

    Install to the system by:
        1) Creating datatype files for an install
        2) Copying objects into local module
        3) Running regular setup tools logic
    '''
    def run(self):
        try:
            from create_panda_datatypes import main as create_datatypes
            create_datatypes(install=True)
            copy_objs()
        except ImportError:
            assert(os.path.isfile("pandare/include/panda_datatypes.h")), \
                            "panda_datatypes.h missing and can't be generated"
            assert(os.path.isfile("pandare/autogen/panda_datatypes.py")), \
                            "panda_datatypes.py missing and can't be generated"
        super().run()

# To build a package for pip:
# python setup.py install
# python setup.py sdist

with open("README.md", "r") as fh:
    long_description = fh.read()

# Determine system version of protoc - we'll need python protobuf of matching version
from subprocess import check_output, CalledProcessError
try:
    pc_version = check_output(["protoc", "--version"]).decode()
    pc_version = pc_version.split(" ")[1]
    print(f"Detected protoc version {pc_version}")
except CalledProcessError:
    print("ERROR: protoc not installed. Install with sudo apt install protoc")
    raise

setup(name='pandare',
      version='0.1.1.5',
      description='Python Interface to PANDA',
      long_description=long_description,
      long_description_content_type="text/markdown",
      author='Andrew Fasano, Luke Craig, and Tim Leek',
      author_email='fasano@mit.edu',
      url='https://github.com/panda-re/panda/',
      packages=find_packages(),
      package_data = { 'pandare': ['data/**/*', # Copy everything (fails?)
          'data/*-softmmu/libpanda-*.so',     # Libpandas
          'data/*-softmmu/llvm-helpers*.bc*', # Llvm-helpers
          'data/*-softmmu/panda/plugins/*',   # All plugins
          'data/*-softmmu/panda/plugins/**/*',# All plugin files
          'data/pypanda/include/*.h',         # Includes files
          'data/pc-bios/*',                   # BIOSes
          ]},
      install_requires=[ 'cffi>=1.14.3', 'colorama', 'protobuf=='+pc_version],
      python_requires='>=3.6',
      cmdclass={'install': custom_install, 'develop': custom_develop},
     )
