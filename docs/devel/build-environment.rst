
.. _setup-build-env:

Setup build environment
=======================

QEMU uses a lot of dependencies on the host system a large number of
which are optional. At a minimum we expect to have a system C library
(usually glibc but others can work), the glib2 library (used heavily
in the code base) and a few other core libraries for interfacing with
code modules and system build descriptions.

We use the ``libvirt-ci`` project to handle the mapping of
dependencies to a wide variety output formats including system install
scripts. For example:

.. code-block:: bash

  # THIS FILE WAS AUTO-GENERATED
  #
  #  $ lcitool buildenvscript debian-13 ./tests/lcitool/projects/qemu-minimal.yml
  #
  # https://gitlab.com/libvirt/libvirt-ci

  function install_buildenv() {
      export DEBIAN_FRONTEND=noninteractive
      apt-get update
      apt-get dist-upgrade -y
      apt-get install --no-install-recommends -y \
              bash \
              bc \
              bison \
              bzip2 \
              ca-certificates \
              ccache \
              findutils \
              flex \
              gcc \
              git \
              libc6-dev \
              libfdt-dev \
              libffi-dev \
              libglib2.0-dev \
              libpixman-1-dev \
              locales \
              make \
              meson \
              ninja-build \
              pkgconf \
              python3 \
              python3-venv \
              sed \
              tar
      sed -Ei 's,^# (en_US\.UTF-8 .*)$,\1,' /etc/locale.gen
      dpkg-reconfigure locales
      rm -f /usr/lib*/python3*/EXTERNALLY-MANAGED
      dpkg-query --showformat '${Package}_${Version}_${Architecture}\n' --show > /packages.txt
      mkdir -p /usr/libexec/ccache-wrappers
      ln -s /usr/bin/ccache /usr/libexec/ccache-wrappers/cc
      ln -s /usr/bin/ccache /usr/libexec/ccache-wrappers/gcc
  }

  export CCACHE_WRAPPERSDIR="/usr/libexec/ccache-wrappers"
  export LANG="en_US.UTF-8"
  export MAKE="/usr/bin/make"
  export NINJA="/usr/bin/ninja"
  export PYTHON="/usr/bin/python3"

If you instead select the ``qemu.yml`` project file you will get all
the dependencies that the project can use.

Using you system package manager
--------------------------------

.. note::

   Installing dependencies using your package manager build dependencies may
   miss out on deps that have been newly introduced in qemu.git. It
   also misses deps the distribution has decided to exclude.

Systems with Package Managers
+++++++++++++++++++++++++++++

.. list-table:: Package Manager Commands
  :widths: 10 50 40
  :header-rows: 1

  * - System
    - Command
    - Notes
  * - Fedora
    - ``sudo dnf update && sudo dnf builddep qemu``
    -
  * - Debian/Ubuntu
    - ``sudo apt update && sudo apt build-dep qemu``
    - Must enable `Sources List
      <https://wiki.debian.org/SourcesList>`_ first
  * - MacOS
    - ``brew update && brew install $(brew deps --include-build qemu)``
    - Using `Homebrew <https://brew.sh/>`_.

Windows
+++++++

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

    pacman -S wget base-devel git
    wget https://raw.githubusercontent.com/msys2/MINGW-packages/refs/heads/master/mingw-w64-qemu/PKGBUILD
    # Some packages may be missing for your environment, installation will still
    # be done though.
    makepkg --syncdeps --nobuild PKGBUILD || true

Build on windows-aarch64
~~~~~~~~~~~~~~~~~~~~~~~~

When trying to cross compile meson for x86_64 using UCRT64 or MINGW64 env,
configure will run into an error because the cpu detected is not correct.

Meson detects x86_64 processes emulated, so you need to manually set the cpu,
and force a cross compilation (with empty prefix).

::

    ./configure --cpu=x86_64 --cross-prefix=
