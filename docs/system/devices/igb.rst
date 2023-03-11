.. SPDX-License-Identifier: GPL-2.0-or-later
.. _igb:

igb
---

igb is a family of Intel's gigabit ethernet controllers. In QEMU, 82576
emulation is implemented in particular. Its datasheet is available at [1]_.

This implementation is expected to be useful to test SR-IOV networking without
requiring physical hardware.

Limitations
===========

This igb implementation was tested with Linux Test Project [2]_ and Windows HLK
[3]_ during the initial development. The command used when testing with LTP is:

.. code-block:: shell

  network.sh -6mta

Be aware that this implementation lacks many functionalities available with the
actual hardware, and you may experience various failures if you try to use it
with a different operating system other than Linux and Windows or if you try
functionalities not covered by the tests.

Using igb
=========

Using igb should be nothing different from using another network device. See
:ref:`pcsys_005fnetwork` in general.

However, you may also need to perform additional steps to activate SR-IOV
feature on your guest. For Linux, refer to [4]_.

Developing igb
==============

igb is the successor of e1000e, and e1000e is the successor of e1000 in turn.
As these devices are very similar, if you make a change for igb and the same
change can be applied to e1000e and e1000, please do so.

Please do not forget to run tests before submitting a change. As tests included
in QEMU is very minimal, run some application which is likely to be affected by
the change to confirm it works in an integrated system.

Testing igb
===========

A qtest of the basic functionality is available. Run the below at the build
directory:

.. code-block:: shell

  meson test qtest-x86_64/qos-test

ethtool can test register accesses, interrupts, etc. It is automated as an
Avocado test and can be ran with the following command:

.. code:: shell

  make check-avocado AVOCADO_TESTS=tests/avocado/igb.py

References
==========

.. [1] https://www.intel.com/content/dam/www/public/us/en/documents/datasheets/82576eb-gigabit-ethernet-controller-datasheet.pdf
.. [2] https://github.com/linux-test-project/ltp
.. [3] https://learn.microsoft.com/en-us/windows-hardware/test/hlk/
.. [4] https://docs.kernel.org/PCI/pci-iov-howto.html
