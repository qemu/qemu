from avocado import main
from avocado_qemu import test


class TestInfoMemdev(test.QemuTest):
    """
    :avocado: enable
    :avocado: tags=qmp,object_add,device_add,memdev
    """

    def setUp(self):
        self.vm.args.extend(['-m', '4G,slots=32,maxmem=40G'])
        self.vm.launch()

    def test_host_nodes(self):
        """
        According to the RHBZ1431939, the issue is 'host nodes'
        returning '128'. It should return empty value instead.
        Fixed in commit d81d857f4421d205395d55200425daa6591c28a5.

        :avocado: tags=RHBZ1431939
        """

        cmd = 'object_add memory-backend-ram,id=mem1,size=1G'
        res = self.vm.qmp('human-monitor-command', command_line=cmd)
        self.assertEqual('', res['return'])

        cmd = 'device_add pc-dimm,id=dimm1,memdev=mem1'
        res = self.vm.qmp('human-monitor-command', command_line=cmd)
        self.assertEqual('', res['return'])

        cmd = 'info memdev'
        res = self.vm.qmp('human-monitor-command', command_line=cmd)
        self.assertIn('host nodes: \r', res['return'])

    def tearDown(self):
        self.vm.shutdown()

if __name__ == "__main__":
    avocado.main()
