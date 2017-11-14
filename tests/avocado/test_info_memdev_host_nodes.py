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

    def test_hotplug_memory_default_policy(self):
        """
        According to the RHBZ1431939, the issue is 'host nodes'
        returning '128'. It should return empty value when memory
        hotplug default policy is used.

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
        self.assertIn('policy: default\r', res['return'])
        self.assertIn('host nodes: \r', res['return'])

    def test_hotplug_memory_bind_policy(self):
        """
        According to the RHBZ1431939, the issue is 'host nodes'
        returning '128'. It should return 0 when memory hotplug
        bind policy is used.

        Fixed in commit d81d857f4421d205395d55200425daa6591c28a5.
        :avocado: tags=RHBZ1431939
        """

        cmd = 'object_add memory-backend-ram,id=mem1,host-nodes=0,size=2G,policy=bind'
        res = self.vm.qmp('human-monitor-command', command_line=cmd)
        self.assertEqual('', res['return'])

        cmd = 'device_add pc-dimm,id=dimm1,memdev=mem1'
        res = self.vm.qmp('human-monitor-command', command_line=cmd)
        self.assertEqual('', res['return'])

        cmd = 'info memdev'
        res = self.vm.qmp('human-monitor-command', command_line=cmd)
        self.assertIn('policy: bind\r', res['return'])
        self.assertIn('host nodes: 0\r', res['return'])

    def tearDown(self):
        self.vm.shutdown()

if __name__ == "__main__":
    avocado.main()
