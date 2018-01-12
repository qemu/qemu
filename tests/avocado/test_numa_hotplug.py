import re
import time

from avocado_qemu import test


class TestNumaHotplug(test.QemuTest):
    """
    Verifies that "info numa" and "/sys/devices/system/node/" contains
    correct values before/after inserting memory devices into default
    and then into 13th numa node.

    Associated bug trackers: RHBZ1473203
        https://bugzilla.redhat.com/show_bug.cgi?id=1473203

    Fixed in kernel commit dc421b200f91930c9c6a9586810ff8c232cf10fc.

    :avocado: enable
    :avocado: tags=RHBZ1473203,requires_linux,numa,memory,ppc64le
    """

    def setUp(self):
        self.request_image()
        self.vm.args.extend(["-m", "4G,slots=208,maxmem=80G"])
        self.vm.args.extend(["-numa", "node"] * 16)
        self.vm.launch()

    def check_mem_console(self, console, exp):
        """
        Verifies that memory layout is according to exp using console/ssh

        :param console: session
        :param exp: list of MemTotals per node in MB, tolerance is +-100MB
        """
        out = console.cmd_output_safe("echo /sys/devices/system/node/node*")
        nodes = re.findall(r"/sys/devices/system/node/node\d+", out)
        self.assertEqual(len(nodes), len(exp), "Number of nodes is not "
                         "%s:\n%s" % (len(exp), out))
        for i in xrange(len(exp)):
            out = console.cmd_output_safe("cat /sys/devices/system/node/"
                                          "node%s/meminfo" % i)
            mem = re.search(r"MemTotal:\s*(\d+) kB", out)
            self.assertTrue(mem, "Failed to obtain node%s MemTotal:\n%s"
                            % (i, out))
            _exp = exp[i] * 1024
            mem = int(mem.group(1))
            self.assertGreater(mem, _exp - 102400, "TotalMem of node%s is not "
                               "%s+-51200 kb (%s)" % (i, _exp, mem))
            self.assertLess(mem, _exp + 102400, "TotalMem of node%s is not "
                            "%s+-51200 kb (%s)" % (i, _exp, mem))

    def check_mem_monitor(self, monitor, exp):
        """
        Verifies that memory layout is according to exp using QMP monitor

        :param console: session
        :param exp: list of MemTotals per node in MB, tolerance is +-100MB
        """
        ret = monitor("human-monitor-command", command_line="info numa")
        out = ret["return"]
        self.assertTrue(out.startswith("%s nodes" % len(exp)), "Number of "
                        "nodes is not %s:\n%s" % (len(exp), out))
        for i in xrange(len(exp)):
            _exp = "node %s size: %s MB" % (i, exp[i])
            self.assertIn(_exp, out, "%s is not in 'info numa' output, "
                          "probably wrong memory size reported:\n%s"
                          % (_exp, out))

    @staticmethod
    def _retry_until_timeout(timeout, func, *args, **kwargs):
        """
        Repeat the function until it returns anything ignoring AssertionError.
        After the deadline repeate the function one more time without ignoring
        timeout.
        """
        end = time.time() + timeout
        while time.time() < end:
            try:
                ret = func(*args, **kwargs)
            except AssertionError:
                continue
            break
        else:
            ret = func(*args, **kwargs)
        return ret

    def test_hotplug_mem_into_node(self):
        console = self.vm.get_console()
        exp = [256] * 16
        self.check_mem_monitor(self.vm.qmp, exp)
        self.check_mem_console(console, exp)
        cmd = "object_add memory-backend-ram,id=mem2,size=1G"
        res = self.vm.qmp("human-monitor-command", command_line=cmd)
        self.assertEqual(res["return"], "")
        cmd = "device_add pc-dimm,id=dimm2,memdev=mem2"
        res = self.vm.qmp("human-monitor-command", command_line=cmd)
        self.assertEqual(res["return"], "")
        exp = [1280] + [256] * 15
        self.check_mem_monitor(self.vm.qmp, exp)
        # Wait up to 10s to propagate the changes
        self._retry_until_timeout(10, self.check_mem_console, console, exp)
        cmd = "object_add memory-backend-ram,id=mem8,size=1G"
        res = self.vm.qmp("human-monitor-command", command_line=cmd)
        self.assertEqual(res["return"], "")
        cmd = "device_add pc-dimm,id=dimm8,memdev=mem8,node=13"
        res = self.vm.qmp("human-monitor-command", command_line=cmd)
        self.assertEqual(res["return"], "")
        time.sleep(5)
        exp = [1280] + [256] * 12 + [1280] + [256] * 2
        self.check_mem_monitor(self.vm.qmp, exp)
        # Wait up to 10s to propagate the changes
        self._retry_until_timeout(10, self.check_mem_console, console, exp)
        console.close()

    def tearDown(self):
        self.vm.shutdown()
