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

Supported host architectures
----------------------------

Those hosts are officially supported, with various accelerators:

  .. list-table::
   :header-rows: 1

   * - CPU Architecture
     - Accelerators
   * - Arm
     - kvm (64 bit only), tcg, xen
   * - MIPS
     - kvm, tcg
   * - PPC
     - kvm, tcg
   * - RISC-V
     - tcg
   * - s390x
     - kvm, tcg
   * - SPARC
     - tcg
   * - x86
     - hax, hvf (64 bit only), kvm, nvmm, tcg, whpx (64 bit only), xen

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

The project aims to support the most recent major version at all times. Support
for the previous major version will be dropped 2 years after the new major
version is released or when the vendor itself drops support, whichever comes
first. In this context, third-party efforts to extend the lifetime of a distro
are not considered, even when they are endorsed by the vendor (eg. Debian LTS).

For the purposes of identifying supported software versions available on Linux,
the project will look at CentOS, Debian, Fedora, openSUSE, RHEL, SLES and
Ubuntu LTS. Other distros will be assumed to ship similar software versions.

For FreeBSD and OpenBSD, decisions will be made based on the contents of the
respective ports repository, while NetBSD will use the pkgsrc repository.

For macOS, `HomeBrew`_ will be used, although `MacPorts`_ is expected to carry
similar versions.

Windows
-------

The project supports building with current versions of the MinGW toolchain,
hosted on Linux (Debian/Fedora).

The version of the Windows API that's currently targeted is Vista / Server
2008.

.. _HomeBrew: https://brew.sh/
.. _MacPorts: https://www.macports.org/
.. _Repology: https://repology.org/
