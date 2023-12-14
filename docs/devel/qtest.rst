========================================
QTest Device Emulation Testing Framework
========================================

.. toctree::

   qgraph

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
Libqos also offers the Qgraph API to increase each test coverage and
automate QEMU command line arguments and devices setup.
Refer to :ref:`qgraph` for Qgraph explanation and API.

Steps to add a new QTest case are:

1. Create a new source file for the test. (More than one file can be added as
   necessary.) For example, ``tests/qtest/foo-test.c``.

2. Write the test code with the glib and libqtest/libqos API. See also existing
   tests and the library headers for reference.

3. Register the new test in ``tests/qtest/meson.build``. Add the test
   executable name to an appropriate ``qtests_*`` variable. There is
   one variable per architecture, plus ``qtests_generic`` for tests
   that can be run for all architectures.  For example::

     qtests_generic = [
       ...
       'foo-test',
       ...
     ]

4. If the test has more than one source file or needs to be linked with any
   dependency other than ``qemuutil`` and ``qos``, list them in the ``qtests``
   dictionary.  For example a test that needs to use the ``QIO`` library
   will have an entry like::

     {
       ...
       'foo-test': [io],
       ...
     }

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

.. kernel-doc:: system/qtest.c
   :doc: QTest Protocol


libqtest API reference
----------------------

.. kernel-doc:: tests/qtest/libqtest.h
