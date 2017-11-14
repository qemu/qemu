========================================
 QEMU tests using the Avocado Framework
========================================

This directory hosts functional tests written using Avocado Testing
Framework.

Installation
============

To install Avocado and the dependencies needed for these tests, run::

    pip install --user avocado-framework avocado-framework-plugin-varianter-yaml-to-mux aexpect

Alternatively, follow the instructions on this link::

    http://avocado-framework.readthedocs.io/en/latest/GetStartedGuide.html#installing-avocado

Overview
========

In this directory, an ``avocado_qemu`` package is provided, containing
the ``test`` module, which inherits from ``avocado.Test`` and provides
a builtin and easy-to-use Qemu virtual machine. Here's a template that
can be used as reference to start writing your own tests::

    from avocado_qemu import test

    class MyTest(test.QemuTest):
        """
        :avocado: enable
        """

        def setUp(self):
            self.vm.args.extend(['-m', '512'])
            self.vm.launch()

        def test_01(self):
            res = self.vm.qmp('human-monitor-command',
                              command_line='info version')
            self.assertIn('v2.9.0', res['return'])

        def tearDown(self):
            self.vm.shutdown()

To execute your test, run::

    avocado run test_my_test.py

To execute all tests, run::

    avocado run .

If you don't specify the Qemu binary to use, the ``avocado_qemu``
package will automatically probe it. The probe will try to use the Qemu
binary from the git tree build directory, using the same architecture as
the local system (if the architecture is not specified). If the Qemu
binary is not available in the git tree build directory, the next try is
to use the system installed Qemu binary.

You can define a number of optional parameters, providing them via YAML
file using the Avocado parameters system:

- ``qemu_bin``: Use a given Qemu binary, skipping the automatic
  probe. Example: ``qemu_bin: /usr/libexec/qemu-kvm``.
- ``qemu_dst_bin``: Use a given Qemu binary to create the destination VM
  when the migration process takes place. If it's not provided, the same
  binary used in the source VM will be used for the destination VM.
  Example: ``qemu_dst_bin: /usr/libexec/qemu-kvm-binary2``.
- ``arch``: Probe the Qemu binary from a given architecture. It has no
  effect if ``qemu_bin`` is specified. If not provided, the binary probe
  will use the system architecture. Example: ``arch: x86_64``
- ``image_path``: When a test requires (usually a bootable) image, this
  parameter is used to define where the image is located. When undefined
  it uses ``$QEMU_ROOT/bootable_image_$arch.qcow2``. The image is added
  to the qemu command __only__ when the test requires an image. By
  default ``,snapshot=on`` is used, but it can be altered by
  ``image_snapshot`` parameter.
- ``image_user`` and ``image_pass``: When using a ``image_path``, if you
  want to get the console from the Guest OS you have to define the Guest
  OS credentials. Example: ``image_user: root`` and
  ``image_pass: p4ssw0rd``. By default it uses ``root`` and ``123456``.
- ``machine_type``: Use this option to define a machine type for the VM.
  Example: ``machine_type: pc``
- ``machine_accel``: Use this option to define a machine acceleration
  for the VM. Example: ``machine_accel: kvm``.
- ``machine_kvm_type``: Use this option to select the KVM type when the
  ``accel`` is ``kvm`` and there are more than one KVM types available.
  Example: ``machine_kvm_type: PR``

Run the test with::

    $ avocado run test_my_test.py -m parameters.yaml

Additionally, you can use a variants file to to set different values
for each parameter. Using the YAML tag ``!mux`` Avocado will execute the
tests once per combination of parameters. Example::

    $ cat variants.yaml
    architecture: !mux
        x86_64:
            arch: x86_64
        i386:
            arch: i386

Run it the with::

    $ avocado run test_my_test.py -m variants.yaml

You can use both the parameters file and the variants file in the same
command line::

    $ avocado run test_my_test.py -m parameters.yaml variants.yaml

Avocado will then merge the parameters from both files and create the
proper variants.

See ``avocado run --help`` and ``man avocado`` for several other
options, such as ``--filter-by-tags``, ``--show-job-log``,
``--failfast``, etc.

Uninstallation
==============

If you've followed the installation instructions above, you can easily
uninstall Avocado.  Start by listing the packages you have installed::

    pip list --user

And remove any package you want with::

    pip uninstall <package_name>
