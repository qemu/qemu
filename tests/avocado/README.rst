This directory is hosting functional tests written using Avocado Testing
Framework. To install Avocado, follow the instructions from this link::

    http://avocado-framework.readthedocs.io/en/latest/GetStartedGuide.html#installing-avocado

Tests here are written keeping the minimum amount of dependencies. So
far, you only need the Avocado core package (`python-avocado`) to run
the tests. Extra dependencies introduced in the future should be
documented here in this README.rst file.

In this directory, an ``avocado_qemu`` package is provided, containing
the ``test`` module, which inherits from ``avocado.Test`` and provides
a builtin and easy-to-use qemu virtual machine. Here's a template that
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

    avocado run tess_my_test.py

To execute all tests, run::

    avocado run .

If you don't specify the qemu binary to use, the ``avocado_qemu``
package will automatically probe it. The probe will try to use the qemu
binary from the git tree build directory, from the same architecture as
the local system (if the architecture is not specified). If the qemu
binary is not available in the git tree build directory, the next try is
to use the system installed qemu binary.

To customize the architecture of the qemu binary, you can set the
``arch`` parameter in a yaml file::

    $ cat parameters.yaml
    arch: x86_64

Run it with::

    $ avocado run tess_my_test.py -m parameters.yaml

You can also include more architectures to run you test with::

    $ cat parameters.yaml
    !mux
    x86_64:
        arch: x86_64
    i386:
        arch: i386

The ``parameters.yaml`` file above will make Avocado to execute your test
once with each defined architecture.

You can define a specific path to the qemu binary to skip the probe
process::

    $ cat config.yaml
    qemu_bin: /usr/libexec/qemu-kvm

Run it with::

    $ avocado run tess_my_test.py -m config.yaml

See ``avocado run --help`` and ``man avocado`` for several other
options, such as ``--filter-by-tags``, ``--show-job-log``,
``--failfast``, etc.
