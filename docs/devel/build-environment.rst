
.. _setup-build-env:

Setup build environment
=======================

QEMU uses a lot of dependencies on the host system. glib2 is used everywhere in
the code base, and most of the other dependencies are optional.

We present here simple instructions to enable native builds on most popular
systems.

You can find additional instructions on `QEMU wiki <https://wiki.qemu.org/>`_:

- `Linux <https://wiki.qemu.org/Hosts/Linux>`_
- `MacOS <https://wiki.qemu.org/Hosts/Mac>`_
- `Windows <https://wiki.qemu.org/Hosts/W32>`_
- `BSD <https://wiki.qemu.org/Hosts/BSD>`_

Note: Installing dependencies using your package manager build dependencies may
miss out on deps that have been newly introduced in qemu.git. In more, it misses
deps the distribution has decided to exclude.

Linux
-----

Fedora
++++++

::

    sudo dnf update && sudo dnf builddep qemu

Debian/Ubuntu
+++++++++++++

You first need to enable `Sources List <https://wiki.debian.org/SourcesList>`_.
Then, use apt to install dependencies:

::

    sudo apt update && sudo apt build-dep qemu

MacOS
-----

You first need to install `Homebrew <https://brew.sh/>`_. Then, use it to
install dependencies:

::

    brew update && brew install $(brew deps --include-build qemu)

Windows
-------

You first need to install `MSYS2 <https://www.msys2.org/>`_.
MSYS2 offers `different environments <https://www.msys2.org/docs/environments/>`_.
x86_64 environments are based on GCC, while aarch64 is based on Clang.

We recommend to use MINGW64 for windows-x86_64 and CLANGARM64 for windows-aarch64
(only available on windows-aarch64 hosts).

Then, you can open a windows shell, and enter msys2 env using:

::

    c:/msys64/msys2_shell.cmd -defterm -here -no-start -mingw64
    # Replace -ucrt64 by -clangarm64 or -ucrt64 for other environments.

MSYS2 package manager does not offer a built-in way to install build
dependencies. You can start with this list of packages using pacman:

Note: Dependencies need to be installed again if you use a different MSYS2
environment.

::

    # update MSYS2 itself, you need to reopen your shell at the end.
    pacman -Syu
    pacman -S \
        base-devel binutils bison diffutils flex git grep make sed \
        ${MINGW_PACKAGE_PREFIX}-toolchain \
        ${MINGW_PACKAGE_PREFIX}-glib2 \
        ${MINGW_PACKAGE_PREFIX}-gtk3 \
        ${MINGW_PACKAGE_PREFIX}-libnfs \
        ${MINGW_PACKAGE_PREFIX}-libssh \
        ${MINGW_PACKAGE_PREFIX}-ninja \
        ${MINGW_PACKAGE_PREFIX}-pixman \
        ${MINGW_PACKAGE_PREFIX}-pkgconf \
        ${MINGW_PACKAGE_PREFIX}-python \
        ${MINGW_PACKAGE_PREFIX}-SDL2 \
        ${MINGW_PACKAGE_PREFIX}-zstd

If you want to install all dependencies, it's possible to use recipe used to
build QEMU in MSYS2 itself.

::

    pacman -S wget
    wget https://raw.githubusercontent.com/msys2/MINGW-packages/refs/heads/master/mingw-w64-qemu/PKGBUILD
    # Some packages may be missing for your environment, installation will still
    # be done though.
    makepkg -s PKGBUILD || true

Build on windows-aarch64
++++++++++++++++++++++++

When trying to cross compile meson for x86_64 using UCRT64 or MINGW64 env,
configure will run into an error because the cpu detected is not correct.

Meson detects x86_64 processes emulated, so you need to manually set the cpu,
and force a cross compilation (with empty prefix).

::

    ./configure --cpu=x86_64 --cross-prefix=

