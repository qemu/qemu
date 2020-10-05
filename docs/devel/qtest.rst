========================================
QTest Device Emulation Testing Framework
========================================

QTest is a device emulation testing framework.  It can be very useful to test
device models; it could also control certain aspects of QEMU (such as virtual
clock stepping), with a special purpose "qtest" protocol.  Refer to
:ref:`qtest-protocol` for more details of the protocol.

QTest cases can be executed with

.. code::

   make check-qtest

The QTest library is implemented by ``tests/qtest/libqtest.c`` and the API is
defined in ``tests/qtest/libqtest.h``.

Consider adding a new QTest case when you are introducing a new virtual
hardware, or extending one if you are adding functionalities to an existing
virtual device.

On top of libqtest, a higher level library, ``libqos``, was created to
encapsulate common tasks of device drivers, such as memory management and
communicating with system buses or devices. Many virtual device tests use
libqos instead of directly calling into libqtest.

Steps to add a new QTest case are:

1. Create a new source file for the test. (More than one file can be added as
   necessary.) For example, ``tests/qtest/foo-test.c``.

2. Write the test code with the glib and libqtest/libqos API. See also existing
   tests and the library headers for reference.

3. Register the new test in ``tests/qtest/Makefile.include``. Add the test
   executable name to an appropriate ``check-qtest-*-y`` variable. For example:

   ``check-qtest-generic-y = tests/qtest/foo-test$(EXESUF)``

4. Add object dependencies of the executable in the Makefile, including the
   test source file(s) and other interesting objects. For example:

   ``tests/qtest/foo-test$(EXESUF): tests/qtest/foo-test.o $(libqos-obj-y)``

Debugging a QTest failure is slightly harder than the unit test because the
tests look up QEMU program names in the environment variables, such as
``QTEST_QEMU_BINARY`` and ``QTEST_QEMU_IMG``, and also because it is not easy
to attach gdb to the QEMU process spawned from the test. But manual invoking
and using gdb on the test is still simple to do: find out the actual command
from the output of

.. code::

  make check-qtest V=1

which you can run manually.


.. _qtest-protocol:

QTest Protocol
--------------

.. kernel-doc:: softmmu/qtest.c
   :doc: QTest Protocol


libqtest API reference
----------------------

.. kernel-doc:: tests/qtest/libqos/libqtest.h
