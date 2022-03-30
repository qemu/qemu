QEMU Python Tooling
===================

This package provides QEMU tooling used by the QEMU project to build,
configure, and test QEMU. It is not a fully-fledged SDK and it is subject
to change at any time.

Usage
-----

The ``qemu.qmp`` subpackage provides a library for communicating with
QMP servers. The ``qemu.machine`` subpackage offers rudimentary
facilities for launching and managing QEMU processes. Refer to each
package's documentation
(``>>> help(qemu.qmp)``, ``>>> help(qemu.machine)``)
for more information.

Contributing
------------

This package is maintained by John Snow <jsnow@redhat.com> as part of
the QEMU source tree. Contributions are welcome and follow the `QEMU
patch submission process
<https://wiki.qemu.org/Contribute/SubmitAPatch>`_, which involves
sending patches to the QEMU development mailing list.

John maintains a `GitLab staging branch
<https://gitlab.com/jsnow/qemu/-/tree/python>`_, and there is an
official `GitLab mirror <https://gitlab.com/qemu-project/qemu>`_.

Please report bugs on the `QEMU issue tracker
<https://gitlab.com/qemu-project/qemu/-/issues>`_ and tag ``@jsnow`` in
the report.

Optional packages necessary for running code quality analysis for this
package can be installed with the optional dependency group "devel":
``pip install qemu[devel]``.

``make develop`` can be used to install this package in editable mode
(to the current environment) *and* bring in testing dependencies in one
command.

``make check`` can be used to run the available tests.
