.. _Supported-build-platforms:

Supported build platforms
=========================

QEMU aims to support building and executing on multiple host OS
platforms. This appendix outlines which platforms are the major build
targets. These platforms are used as the basis for deciding upon the
minimum required versions of 3rd party software QEMU depends on. The
supported platforms are the targets for automated testing performed by
the project when patches are submitted for review, and tested before and
after merge.

If a platform is not listed here, it does not imply that QEMU won't
work. If an unlisted platform has comparable software versions to a
listed platform, there is every expectation that it will work. Bug
reports are welcome for problems encountered on unlisted platforms
unless they are clearly older vintage than what is described here.

Note that when considering software versions shipped in distros as
support targets, QEMU considers only the version number, and assumes the
features in that distro match the upstream release with the same
version. In other words, if a distro backports extra features to the
software in their distro, QEMU upstream code will not add explicit
support for those backports, unless the feature is auto-detectable in a
manner that works for the upstream releases too.

The `Repology`_ site is a useful resource to identify
currently shipped versions of software in various operating systems,
though it does not cover all distros listed below.

You can find how to install build dependencies for different systems on the
:ref:`setup-build-env` page.

Supported host architectures
----------------------------

Those hosts are officially supported, with various accelerators:

  .. list-table::
   :header-rows: 1

   * - CPU Architecture
     - Accelerators
   * - Arm
     - hvf (64 bit only), kvm (64 bit only), tcg, xen
   * - MIPS (64 bit little endian only)
     - kvm, tcg
   * - PPC
     - kvm, tcg
   * - RISC-V
     - kvm, tcg
   * - s390x
     - kvm, tcg
   * - SPARC
     - tcg
   * - x86
     - hvf (64 bit only), kvm, nvmm, tcg, whpx (64 bit only), xen

Other host architectures are not supported. It is possible to build QEMU system
emulation on an unsupported host architecture using the configure
``--enable-tcg-interpreter`` option to enable the TCI support, but note that
this is very slow and is not recommended for normal use. QEMU user emulation
requires host-specific support for signal handling, therefore TCI won't help
on unsupported host architectures.

Non-supported architectures may be removed in the future following the
:ref:`deprecation process<Deprecated features>`.

Linux OS, macOS, FreeBSD, NetBSD, OpenBSD
-----------------------------------------

The project aims to support the most recent major version at all times for
up to five years after its initial release. Support
for the previous major version will be dropped 2 years after the new major
version is released or when the vendor itself drops support, whichever comes
first. In this context, third-party efforts to extend the lifetime of a distro
are not considered, even when they are endorsed by the vendor (eg. Debian LTS);
the same is true of repositories that contain packages backported from later
releases (e.g. Debian backports). Within each major release, only the most
recent minor release is considered.

For the purposes of identifying supported software versions available on Linux,
the project will look at CentOS, Debian, Fedora, openSUSE, RHEL, SLES and
Ubuntu LTS. Other distros will be assumed to ship similar software versions.

For FreeBSD and OpenBSD, decisions will be made based on the contents of the
respective ports repository, while NetBSD will use the pkgsrc repository.

For macOS, `Homebrew`_ will be used, although `MacPorts`_ is expected to carry
similar versions.

Some build dependencies may follow less conservative rules:

Python runtime
  Distributions with long-term support often provide multiple versions
  of the Python runtime.  While QEMU will initially aim to support the
  distribution's default runtime, it may later increase its minimum version
  to any newer python that is available as an option from the vendor.
  In this case, it will be necessary to use the ``--python`` command line
  option of the ``configure`` script to point QEMU to a supported
  version of the Python runtime.

  As of QEMU |version|, the minimum supported version of Python is 3.8.

Python build dependencies
  Some of QEMU's build dependencies are written in Python.  Usually these
  are only packaged by distributions for the default Python runtime.
  If QEMU bumps its minimum Python version and a non-default runtime is
  required, it may be necessary to fetch python modules from the Python
  Package Index (PyPI) via ``pip``, in order to build QEMU.

Rust build dependencies
  QEMU is generally conservative in adding new Rust dependencies, and all
  of them are included in the distributed tarballs.  One exception is the
  bindgen tool, which is too big to package and distribute.  The minimum
  supported version of bindgen is 0.60.x.  For distributions that do not
  include bindgen or have an older version, it is recommended to install
  a newer version using ``cargo install bindgen-cli``.

  Developers may want to use Cargo-based tools in the QEMU source tree;
  this requires Cargo 1.74.0.  Note that Cargo is not required in order
  to build QEMU.

Optional build dependencies
  Build components whose absence does not affect the ability to build QEMU
  may not be available in distros, or may be too old for our requirements.
  Many of these, such as additional modules for the functional testing
  framework or various linters, are written in Python and therefore can
  also be installed using ``pip``.  Cross compilers are another example
  of optional build-time dependency; in this case it is possible to
  download them from repositories such as EPEL, to use container-based
  cross compilation using ``docker`` or ``podman``, or to use pre-built
  binaries distributed with QEMU.

Windows
-------

The project aims to support the two most recent versions of Windows that are
still supported by the vendor. The minimum Windows API that is currently
targeted is "Windows 8", so theoretically the QEMU binaries can still be run
on older versions of Windows, too. However, such old versions of Windows are
not tested anymore, so it is recommended to use one of the latest versions of
Windows instead.

The project supports building QEMU with current versions of the MinGW
toolchain, either hosted on Linux (Debian/Fedora) or via `MSYS2`_ on Windows.
A more recent Windows version is always preferred as it is less likely to have
problems with building via MSYS2. The building process of QEMU involves some
Python scripts that call os.symlink() which needs special attention for the
build process to successfully complete. On newer versions of Windows 10,
unprivileged accounts can create symlinks if Developer Mode is enabled.
When Developer Mode is not available/enabled, the SeCreateSymbolicLinkPrivilege
privilege is required, or the process must be run as an administrator.

Only 64-bit Windows is supported.

.. _Homebrew: https://brew.sh/
.. _MacPorts: https://www.macports.org/
.. _MSYS2: https://www.msys2.org/
.. _Repology: https://repology.org/
