#!/usr/bin/python3
"""
build helper script for edk2, see
https://gitlab.com/kraxel/edk2-build-config

"""
import os
import sys
import time
import shutil
import argparse
import subprocess
import configparser

rebase_prefix    = ""
version_override = None
release_date     = None

# pylint: disable=unused-variable
def check_rebase():
    """ detect 'git rebase -x edk2-build.py master' testbuilds """
    global rebase_prefix
    global version_override
    gitdir = '.git'

    if os.path.isfile(gitdir):
        with open(gitdir, 'r', encoding = 'utf-8') as f:
            (unused, gitdir) = f.read().split()

    if not os.path.exists(f'{gitdir}/rebase-merge/msgnum'):
        return
    with open(f'{gitdir}/rebase-merge/msgnum', 'r', encoding = 'utf-8') as f:
        msgnum = int(f.read())
    with open(f'{gitdir}/rebase-merge/end', 'r', encoding = 'utf-8') as f:
        end = int(f.read())
    with open(f'{gitdir}/rebase-merge/head-name', 'r', encoding = 'utf-8') as f:
        head = f.read().strip().split('/')

    rebase_prefix = f'[ {int(msgnum/2)} / {int(end/2)} - {head[-1]} ] '
    if msgnum != end and not version_override:
        # fixed version speeds up builds
        version_override = "test-build-patch-series"

def get_coredir(cfg):
    if cfg.has_option('global', 'core'):
        return os.path.abspath(cfg['global']['core'])
    return os.getcwd()

def get_toolchain(cfg, build):
    if cfg.has_option(build, 'tool'):
        return cfg[build]['tool']
    if cfg.has_option('global', 'tool'):
        return cfg['global']['tool']
    return 'GCC5'

def get_version(cfg, silent = False):
    coredir = get_coredir(cfg)
    if version_override:
        version = version_override
        if not silent:
            print('')
            print(f'### version [override]: {version}')
        return version
    if os.environ.get('RPM_PACKAGE_NAME'):
        version = os.environ.get('RPM_PACKAGE_NAME')
        version += '-' + os.environ.get('RPM_PACKAGE_VERSION')
        version += '-' + os.environ.get('RPM_PACKAGE_RELEASE')
        if not silent:
            print('')
            print(f'### version [rpmbuild]: {version}')
        return version
    if os.path.exists(coredir + '/.git'):
        cmdline = [ 'git', 'describe', '--tags', '--abbrev=8',
                    '--match=edk2-stable*' ]
        result = subprocess.run(cmdline, cwd = coredir,
                                stdout = subprocess.PIPE,
                                check = True)
        version = result.stdout.decode().strip()
        if not silent:
            print('')
            print(f'### version [git]: {version}')
        return version
    return None

def pcd_string(name, value):
    return f'{name}=L{value}\\0'

def pcd_version(cfg, silent = False):
    version = get_version(cfg, silent)
    if version is None:
        return []
    return [ '--pcd', pcd_string('PcdFirmwareVersionString', version) ]

def pcd_release_date():
    if release_date is None:
        return []
    return [ '--pcd', pcd_string('PcdFirmwareReleaseDateString', release_date) ]

def build_message(line, line2 = None, silent = False):
    if os.environ.get('TERM') in [ 'xterm', 'xterm-256color' ]:
        # setxterm  title
        start  = '\x1b]2;'
        end    = '\x07'
        print(f'{start}{rebase_prefix}{line}{end}', end = '')

    if silent:
        print(f'### {rebase_prefix}{line}', flush = True)
    else:
        print('')
        print('###')
        print(f'### {rebase_prefix}{line}')
        if line2:
            print(f'### {line2}')
        print('###', flush = True)

def build_run(cmdline, name, section, silent = False, nologs = False):
    if silent:
        logfile = f'{section}.log'
        if nologs:
            print(f'### building in silent mode [no log] ...', flush = True)
        else:
            print(f'### building in silent mode [{logfile}] ...', flush = True)
        start = time.time()
        result = subprocess.run(cmdline, check = False,
                                stdout = subprocess.PIPE,
                                stderr = subprocess.STDOUT)
        if not nologs:
            with open(logfile, 'wb') as f:
                f.write(result.stdout)

        if result.returncode:
            print('### BUILD FAILURE')
            print('### cmdline')
            print(cmdline)
            print('### output')
            print(result.stdout.decode())
            print(f'### exit code: {result.returncode}')
        else:
            secs = int(time.time() - start)
            print(f'### OK ({int(secs/60)}:{secs%60:02d})')
    else:
        print(cmdline, flush = True)
        result = subprocess.run(cmdline, check = False)
    if result.returncode:
        print(f'ERROR: {cmdline[0]} exited with {result.returncode}'
              f' while building {name}')
        sys.exit(result.returncode)

def build_copy(plat, tgt, toolchain, dstdir, copy):
    srcdir = f'Build/{plat}/{tgt}_{toolchain}'
    names = copy.split()
    srcfile = names[0]
    if len(names) > 1:
        dstfile = names[1]
    else:
        dstfile = os.path.basename(srcfile)
    print(f'# copy: {srcdir} / {srcfile}  =>  {dstdir} / {dstfile}')

    src = srcdir + '/' + srcfile
    dst = dstdir + '/' + dstfile
    os.makedirs(os.path.dirname(dst), exist_ok = True)
    shutil.copy(src, dst)

def pad_file(dstdir, pad):
    args = pad.split()
    if len(args) < 2:
        raise RuntimeError(f'missing arg for pad ({args})')
    name = args[0]
    size = args[1]
    cmdline = [
        'truncate',
        '--size', size,
        dstdir + '/' + name,
    ]
    print(f'# padding: {dstdir} / {name}  =>  {size}')
    subprocess.run(cmdline, check = True)

# pylint: disable=too-many-branches
def build_one(cfg, build, jobs = None, silent = False, nologs = False):
    b = cfg[build]

    cmdline  = [ 'build' ]
    cmdline += [ '-t', get_toolchain(cfg, build) ]
    cmdline += [ '-p', b['conf'] ]

    if (b['conf'].startswith('OvmfPkg/') or
        b['conf'].startswith('ArmVirtPkg/')):
        cmdline += pcd_version(cfg, silent)
        cmdline += pcd_release_date()

    if jobs:
        cmdline += [ '-n', jobs ]
    for arch in b['arch'].split():
        cmdline += [ '-a', arch ]
    if 'opts' in b:
        for name in b['opts'].split():
            section = 'opts.' + name
            for opt in cfg[section]:
                cmdline += [ '-D', opt + '=' + cfg[section][opt] ]
    if 'pcds' in b:
        for name in b['pcds'].split():
            section = 'pcds.' + name
            for pcd in cfg[section]:
                cmdline += [ '--pcd', pcd + '=' + cfg[section][pcd] ]
    if 'tgts' in b:
        tgts = b['tgts'].split()
    else:
        tgts = [ 'DEBUG' ]
    for tgt in tgts:
        desc = None
        if 'desc' in b:
            desc = b['desc']
        build_message(f'building: {b["conf"]} ({b["arch"]}, {tgt})',
                      f'description: {desc}',
                      silent = silent)
        build_run(cmdline + [ '-b', tgt ],
                  b['conf'],
                  build + '.' + tgt,
                  silent,
                  nologs)

        if 'plat' in b:
            # copy files
            for cpy in b:
                if not cpy.startswith('cpy'):
                    continue
                build_copy(b['plat'], tgt,
                           get_toolchain(cfg, build),
                           b['dest'], b[cpy])
            # pad builds
            for pad in b:
                if not pad.startswith('pad'):
                    continue
                pad_file(b['dest'], b[pad])

def build_basetools(silent = False, nologs = False):
    build_message('building: BaseTools', silent = silent)
    basedir = os.environ['EDK_TOOLS_PATH']
    cmdline = [ 'make', '-C', basedir ]
    build_run(cmdline, 'BaseTools', 'build.basetools', silent, nologs)

def binary_exists(name):
    for pdir in os.environ['PATH'].split(':'):
        if os.path.exists(pdir + '/' + name):
            return True
    return False

def prepare_env(cfg, silent = False):
    """ mimic Conf/BuildEnv.sh """
    workspace = os.getcwd()
    packages = [ workspace, ]
    path = os.environ['PATH'].split(':')
    dirs = [
        'BaseTools/Bin/Linux-x86_64',
        'BaseTools/BinWrappers/PosixLike'
    ]

    if cfg.has_option('global', 'pkgs'):
        for pkgdir in cfg['global']['pkgs'].split():
            packages.append(os.path.abspath(pkgdir))
    coredir = get_coredir(cfg)
    if coredir != workspace:
        packages.append(coredir)

    # add basetools to path
    for pdir in dirs:
        p = coredir + '/' + pdir
        if not os.path.exists(p):
            continue
        if p in path:
            continue
        path.insert(0, p)

    # run edksetup if needed
    toolsdef = coredir + '/Conf/tools_def.txt'
    if not os.path.exists(toolsdef):
        os.makedirs(os.path.dirname(toolsdef), exist_ok = True)
        build_message('running BaseTools/BuildEnv', silent = silent)
        cmdline = [ 'bash', 'BaseTools/BuildEnv' ]
        subprocess.run(cmdline, cwd = coredir, check = True)

    # set variables
    os.environ['PATH'] = ':'.join(path)
    os.environ['PACKAGES_PATH'] = ':'.join(packages)
    os.environ['WORKSPACE'] = workspace
    os.environ['EDK_TOOLS_PATH'] = coredir + '/BaseTools'
    os.environ['CONF_PATH'] = coredir + '/Conf'
    os.environ['PYTHON_COMMAND'] = '/usr/bin/python3'
    os.environ['PYTHONHASHSEED'] = '1'

    # for cross builds
    if binary_exists('arm-linux-gnueabi-gcc'):
        # ubuntu
        os.environ['GCC5_ARM_PREFIX'] = 'arm-linux-gnueabi-'
        os.environ['GCC_ARM_PREFIX'] = 'arm-linux-gnueabi-'
    elif binary_exists('arm-linux-gnu-gcc'):
        # fedora
        os.environ['GCC5_ARM_PREFIX'] = 'arm-linux-gnu-'
        os.environ['GCC_ARM_PREFIX'] = 'arm-linux-gnu-'
    if binary_exists('loongarch64-linux-gnu-gcc'):
        os.environ['GCC5_LOONGARCH64_PREFIX'] = 'loongarch64-linux-gnu-'
        os.environ['GCC_LOONGARCH64_PREFIX'] = 'loongarch64-linux-gnu-'

    hostarch = os.uname().machine
    if binary_exists('aarch64-linux-gnu-gcc') and hostarch != 'aarch64':
        os.environ['GCC5_AARCH64_PREFIX'] = 'aarch64-linux-gnu-'
        os.environ['GCC_AARCH64_PREFIX'] = 'aarch64-linux-gnu-'
    if binary_exists('riscv64-linux-gnu-gcc') and hostarch != 'riscv64':
        os.environ['GCC5_RISCV64_PREFIX'] = 'riscv64-linux-gnu-'
        os.environ['GCC_RISCV64_PREFIX'] = 'riscv64-linux-gnu-'
    if binary_exists('x86_64-linux-gnu-gcc') and hostarch != 'x86_64':
        os.environ['GCC5_IA32_PREFIX'] = 'x86_64-linux-gnu-'
        os.environ['GCC5_X64_PREFIX'] = 'x86_64-linux-gnu-'
        os.environ['GCC5_BIN'] = 'x86_64-linux-gnu-'
        os.environ['GCC_IA32_PREFIX'] = 'x86_64-linux-gnu-'
        os.environ['GCC_X64_PREFIX'] = 'x86_64-linux-gnu-'
        os.environ['GCC_BIN'] = 'x86_64-linux-gnu-'

def build_list(cfg):
    for build in cfg.sections():
        if not build.startswith('build.'):
            continue
        name = build.lstrip('build.')
        desc = 'no description'
        if 'desc' in cfg[build]:
            desc = cfg[build]['desc']
        print(f'# {name:20s} - {desc}')

def main():
    parser = argparse.ArgumentParser(prog = 'edk2-build',
                                     description = 'edk2 build helper script')
    parser.add_argument('-c', '--config', dest = 'configfile',
                        type = str, default = '.edk2.builds', metavar = 'FILE',
                        help = 'read configuration from FILE (default: .edk2.builds)')
    parser.add_argument('-C', '--directory', dest = 'directory', type = str,
                        help = 'change to DIR before building', metavar = 'DIR')
    parser.add_argument('-j', '--jobs', dest = 'jobs', type = str,
                        help = 'allow up to JOBS parallel build jobs',
                        metavar = 'JOBS')
    parser.add_argument('-m', '--match', dest = 'match',
                        type = str, action = 'append',
                        help = 'only run builds matching INCLUDE (substring)',
                        metavar = 'INCLUDE')
    parser.add_argument('-x', '--exclude', dest = 'exclude',
                        type = str, action = 'append',
                        help = 'skip builds matching EXCLUDE (substring)',
                        metavar = 'EXCLUDE')
    parser.add_argument('-l', '--list', dest = 'list',
                        action = 'store_true', default = False,
                        help = 'list build configs available')
    parser.add_argument('--silent', dest = 'silent',
                        action = 'store_true', default = False,
                        help = 'write build output to logfiles, '
                        'write to console only on errors')
    parser.add_argument('--no-logs', dest = 'nologs',
                        action = 'store_true', default = False,
                        help = 'do not write build log files (with --silent)')
    parser.add_argument('--core', dest = 'core', type = str, metavar = 'DIR',
                        help = 'location of the core edk2 repository '
                        '(i.e. where BuildTools are located)')
    parser.add_argument('--pkg', '--package', dest = 'pkgs',
                        type = str, action = 'append', metavar = 'DIR',
                        help = 'location(s) of additional packages '
                        '(can be specified multiple times)')
    parser.add_argument('-t', '--toolchain', dest = 'toolchain',
                        type = str, metavar = 'NAME',
                        help = 'tool chain to be used to build edk2')
    parser.add_argument('--version-override', dest = 'version_override',
                        type = str, metavar = 'VERSION',
                        help = 'set firmware build version')
    parser.add_argument('--release-date', dest = 'release_date',
                        type = str, metavar = 'DATE',
                        help = 'set firmware build release date (in MM/DD/YYYY format)')
    options = parser.parse_args()

    if options.directory:
        os.chdir(options.directory)

    if not os.path.exists(options.configfile):
        print(f'config file "{options.configfile}" not found')
        return 1

    cfg = configparser.ConfigParser()
    cfg.optionxform = str
    cfg.read(options.configfile)

    if options.list:
        build_list(cfg)
        return 0

    if not cfg.has_section('global'):
        cfg.add_section('global')
    if options.core:
        cfg.set('global', 'core', options.core)
    if options.pkgs:
        cfg.set('global', 'pkgs', ' '.join(options.pkgs))
    if options.toolchain:
        cfg.set('global', 'tool', options.toolchain)

    global version_override
    global release_date
    check_rebase()
    if options.version_override:
        version_override = options.version_override
    if options.release_date:
        release_date = options.release_date

    prepare_env(cfg, options.silent)
    build_basetools(options.silent, options.nologs)
    for build in cfg.sections():
        if not build.startswith('build.'):
            continue
        if options.match:
            matching = False
            for item in options.match:
                if item in build:
                    matching = True
            if not matching:
                print(f'# skipping "{build}" (not matching "{"|".join(options.match)}")')
                continue
        if options.exclude:
            exclude = False
            for item in options.exclude:
                if item in build:
                    print(f'# skipping "{build}" (matching "{item}")')
                    exclude = True
            if exclude:
                continue
        build_one(cfg, build, options.jobs, options.silent, options.nologs)

    return 0

if __name__ == '__main__':
    sys.exit(main())
