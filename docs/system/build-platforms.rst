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

The Repology site https://repology.org is a useful resource to identify
currently shipped versions of software in various operating systems,
though it does not cover all distros listed below.

Linux OS
--------

For distributions with frequent, short-lifetime releases, the project
will aim to support all versions that are not end of life by their
respective vendors. For the purposes of identifying supported software
versions, the project will look at Fedora, Ubuntu, and openSUSE distros.
Other short- lifetime distros will be assumed to ship similar software
versions.

For distributions with long-lifetime releases, the project will aim to
support the most recent major version at all times. Support for the
previous major version will be dropped 2 years after the new major
version is released, or when it reaches "end of life". For the purposes
of identifying supported software versions, the project will look at
RHEL, Debian, Ubuntu LTS, and SLES distros. Other long-lifetime distros
will be assumed to ship similar software versions.

Windows
-------

The project supports building with current versions of the MinGW
toolchain, hosted on Linux.

macOS
-----

The project supports building with the two most recent versions of
macOS, with the current homebrew package set available.

FreeBSD
-------

The project aims to support the all the versions which are not end of
life.

NetBSD
------

The project aims to support the most recent major version at all times.
Support for the previous major version will be dropped 2 years after the
new major version is released.

OpenBSD
-------

The project aims to support the all the versions which are not end of
life.
