import os
import shutil
import sys

from avocado import main
from avocado_qemu import test


class TestOvmfVcpus(test.QemuTest):
    """
    Run with:

        avocado run test_ovmf_with_240_vcpus.py \
        -m test_ovmf_with_240_vcpus.py.data/parameters.yaml

    :avocado: enable
    :avocado: tags=ovmf
    """

    def setUp(self):
        ovmf_code_path = self.params.get('OVMF_CODE',
                                         default='/usr/share/edk2/ovmf/OVMF_CODE.secboot.fd')
        ovmf_vars_path = self.params.get('OVMF_VARS',
                                         default='/usr/share/edk2/ovmf/OVMF_VARS.fd')
        if not ovmf_code_path or not os.path.exists(ovmf_code_path):
            basename = os.path.basename(__file__)
            self.cancel('OVMF_CODE file not found. Set the correct '
                        'path on "%s.data/parameters.yaml" and run this test '
                        'with: "avocado run %s -m %s.data/parameters.yaml"' %
                        (basename, basename, basename))
        if not ovmf_vars_path or not os.path.exists(ovmf_vars_path):
            basename = os.path.basename(__file__)
            self.cancel('OVMF_VARS file not found. Set the correct '
                        'path on "%s.data/parameters.yaml" and run this test '
                        'with: "avocado run %s -m %s.data/parameters.yaml"' %
                        (basename, basename, basename))

        ovmf_code_tmp = os.path.join(self.teststmpdir,
                                     os.path.basename(ovmf_code_path))
        ovmf_vars_tmp = os.path.join(self.teststmpdir,
                                     os.path.basename(ovmf_vars_path))
        if not os.path.exists(ovmf_code_tmp):
            shutil.copy(ovmf_code_path, self.teststmpdir)
        if not os.path.exists(ovmf_vars_tmp):
            shutil.copy(ovmf_vars_path, self.teststmpdir)

        self.vm.args.extend(['-smp', '240'])
        self.vm.args.extend(['-drive', 'file=%s' % ovmf_code_tmp])
        self.vm.args.extend(['-drive', 'file=%s' % ovmf_vars_tmp])

    def test_run_vm(self):
        """
        According to the RHBZ1447027, the issue is: Guest cannot boot
        with 240 or above vcpus when using ovmf.
        Fixed in commit e85c0d14014514a2f0faeae5b4c23fab5b234de4.

        :avocado: tags=RHBZ1447027
        """

        try:
            self.vm.launch()
        except:
            self.fail("Error starting VM")

    def tearDown(self):
        self.vm.shutdown()

if __name__ == "__main__":
    avocado.main()
