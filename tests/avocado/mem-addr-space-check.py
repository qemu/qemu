# Check for crash when using memory beyond the available guest processor
# address space.
#
# Copyright (c) 2023 Red Hat, Inc.
#
# Author:
#  Ani Sinha <anisinha@redhat.com>
#
# SPDX-License-Identifier: GPL-2.0-or-later

from avocado_qemu import QemuSystemTest
import signal
import time

class MemAddrCheck(QemuSystemTest):
    # after launch, in order to generate the logs from QEMU we need to
    # wait for some time. Launching and then immediately shutting down
    # the VM generates empty logs. A delay of 1 second is added for
    # this reason.
    DELAY_Q35_BOOT_SEQUENCE = 1

    # first, lets test some 32-bit processors.
    # for all 32-bit cases, pci64_hole_size is 0.
    def test_phybits_low_pse36(self):
        """
        :avocado: tags=machine:q35
        :avocado: tags=arch:x86_64

        With pse36 feature ON, a processor has 36 bits of addressing. So it can
        access up to a maximum of 64GiB of memory. Memory hotplug region begins
        at 4 GiB boundary when "above_4g_mem_size" is 0 (this would be true when
        we have 0.5 GiB of VM memory, see pc_q35_init()). This means total
        hotpluggable memory size is 60 GiB. Per slot, we reserve 1 GiB of memory
        for dimm alignment for all newer machines (see enforce_aligned_dimm
        property for pc machines and pc_get_device_memory_range()). That leaves
        total hotpluggable actual memory size of 59 GiB. If the VM is started
        with 0.5 GiB of memory, maxmem should be set to a maximum value of
        59.5 GiB to ensure that the processor can address all memory directly.
        Note that 64-bit pci hole size is 0 in this case. If maxmem is set to
        59.6G, QEMU should fail to start with a message "phy-bits are too low".
        If maxmem is set to 59.5G with all other QEMU parameters identical, QEMU
        should start fine.
        """
        self.vm.add_args('-S', '-machine', 'q35', '-m',
                         '512,slots=1,maxmem=59.6G',
                         '-cpu', 'pentium,pse36=on', '-display', 'none',
                         '-object', 'memory-backend-ram,id=mem1,size=1G',
                         '-device', 'pc-dimm,id=vm0,memdev=mem1')
        self.vm.set_qmp_monitor(enabled=False)
        self.vm.launch()
        self.vm.wait()
        self.assertEqual(self.vm.exitcode(), 1, "QEMU exit code should be 1")
        self.assertRegex(self.vm.get_log(), r'phys-bits too low')

    def test_phybits_low_pae(self):
        """
        :avocado: tags=machine:q35
        :avocado: tags=arch:x86_64

        With pae feature ON, a processor has 36 bits of addressing. So it can
        access up to a maximum of 64GiB of memory. Rest is the same as the case
        with pse36 above.
        """
        self.vm.add_args('-S', '-machine', 'q35', '-m',
                         '512,slots=1,maxmem=59.6G',
                         '-cpu', 'pentium,pae=on', '-display', 'none',
                         '-object', 'memory-backend-ram,id=mem1,size=1G',
                         '-device', 'pc-dimm,id=vm0,memdev=mem1')
        self.vm.set_qmp_monitor(enabled=False)
        self.vm.launch()
        self.vm.wait()
        self.assertEqual(self.vm.exitcode(), 1, "QEMU exit code should be 1")
        self.assertRegex(self.vm.get_log(), r'phys-bits too low')

    def test_phybits_ok_pentium_pse36(self):
        """
        :avocado: tags=machine:q35
        :avocado: tags=arch:x86_64

        Setting maxmem to 59.5G and making sure that QEMU can start with the
        same options as the failing case above with pse36 cpu feature.
        """
        self.vm.add_args('-machine', 'q35', '-m',
                         '512,slots=1,maxmem=59.5G',
                         '-cpu', 'pentium,pse36=on', '-display', 'none',
                         '-object', 'memory-backend-ram,id=mem1,size=1G',
                         '-device', 'pc-dimm,id=vm0,memdev=mem1')
        self.vm.set_qmp_monitor(enabled=False)
        self.vm.launch()
        time.sleep(self.DELAY_Q35_BOOT_SEQUENCE)
        self.vm.shutdown()
        self.assertNotRegex(self.vm.get_log(), r'phys-bits too low')

    def test_phybits_ok_pentium_pae(self):
        """
        :avocado: tags=machine:q35
        :avocado: tags=arch:x86_64

        Test is same as above but now with pae cpu feature turned on.
        Setting maxmem to 59.5G and making sure that QEMU can start fine
        with the same options as the case above.
        """
        self.vm.add_args('-machine', 'q35', '-m',
                         '512,slots=1,maxmem=59.5G',
                         '-cpu', 'pentium,pae=on', '-display', 'none',
                         '-object', 'memory-backend-ram,id=mem1,size=1G',
                         '-device', 'pc-dimm,id=vm0,memdev=mem1')
        self.vm.set_qmp_monitor(enabled=False)
        self.vm.launch()
        time.sleep(self.DELAY_Q35_BOOT_SEQUENCE)
        self.vm.shutdown()
        self.assertNotRegex(self.vm.get_log(), r'phys-bits too low')

    def test_phybits_ok_pentium2(self):
        """
        :avocado: tags=machine:q35
        :avocado: tags=arch:x86_64

        Pentium2 has 36 bits of addressing, so its same as pentium
        with pse36 ON.
        """
        self.vm.add_args('-machine', 'q35', '-m',
                         '512,slots=1,maxmem=59.5G',
                         '-cpu', 'pentium2', '-display', 'none',
                         '-object', 'memory-backend-ram,id=mem1,size=1G',
                         '-device', 'pc-dimm,id=vm0,memdev=mem1')
        self.vm.set_qmp_monitor(enabled=False)
        self.vm.launch()
        time.sleep(self.DELAY_Q35_BOOT_SEQUENCE)
        self.vm.shutdown()
        self.assertNotRegex(self.vm.get_log(), r'phys-bits too low')

    def test_phybits_low_nonpse36(self):
        """
        :avocado: tags=machine:q35
        :avocado: tags=arch:x86_64

        Pentium processor has 32 bits of addressing without pse36 or pae
        so it can access physical address up to 4 GiB. Setting maxmem to
        4 GiB should make QEMU fail to start with "phys-bits too low"
        message because the region for memory hotplug is always placed
        above 4 GiB due to the PCI hole and simplicity.
        """
        self.vm.add_args('-S', '-machine', 'q35', '-m',
                         '512,slots=1,maxmem=4G',
                         '-cpu', 'pentium', '-display', 'none',
                         '-object', 'memory-backend-ram,id=mem1,size=1G',
                         '-device', 'pc-dimm,id=vm0,memdev=mem1')
        self.vm.set_qmp_monitor(enabled=False)
        self.vm.launch()
        self.vm.wait()
        self.assertEqual(self.vm.exitcode(), 1, "QEMU exit code should be 1")
        self.assertRegex(self.vm.get_log(), r'phys-bits too low')

    # now lets test some 64-bit CPU cases.
    def test_phybits_low_tcg_q35_70_amd(self):
        """
        :avocado: tags=machine:q35
        :avocado: tags=arch:x86_64

        For q35 7.1 machines and above, there is a HT window that starts at
        1024 GiB and ends at 1 TiB - 1. If the max GPA falls in this range,
        "above_4G" memory is adjusted to start at 1 TiB boundary for AMD cpus
        in the default case. Lets test without that case for machines 7.0.
        For q35-7.0 machines, "above 4G" memory starts are 4G.
        pci64_hole size is 32 GiB. Since TCG_PHYS_ADDR_BITS is defined to
        be 40, TCG emulated CPUs have maximum of 1 TiB (1024 GiB) of
        directly addressible memory.
        Hence, maxmem value at most can be
        1024 GiB - 4 GiB - 1 GiB per slot for alignment - 32 GiB + 0.5 GiB
        which is equal to 987.5 GiB. Setting the value to 988 GiB should
        make QEMU fail with the error message.
        """
        self.vm.add_args('-S', '-machine', 'pc-q35-7.0', '-m',
                         '512,slots=1,maxmem=988G',
                         '-display', 'none',
                         '-object', 'memory-backend-ram,id=mem1,size=1G',
                         '-device', 'pc-dimm,id=vm0,memdev=mem1')
        self.vm.set_qmp_monitor(enabled=False)
        self.vm.launch()
        self.vm.wait()
        self.assertEqual(self.vm.exitcode(), 1, "QEMU exit code should be 1")
        self.assertRegex(self.vm.get_log(), r'phys-bits too low')

    def test_phybits_low_tcg_q35_71_amd(self):
        """
        :avocado: tags=machine:q35
        :avocado: tags=arch:x86_64

        AMD_HT_START is defined to be at 1012 GiB. So for q35 machines
        version > 7.0 and AMD cpus, instead of 1024 GiB limit for 40 bit
        processor address space, it has to be 1012 GiB , that is 12 GiB
        less than the case above in order to accomodate HT hole.
        Make sure QEMU fails when maxmem size is 976 GiB (12 GiB less
        than 988 GiB).
        """
        self.vm.add_args('-S', '-machine', 'pc-q35-7.1', '-m',
                         '512,slots=1,maxmem=976G',
                         '-display', 'none',
                         '-object', 'memory-backend-ram,id=mem1,size=1G',
                         '-device', 'pc-dimm,id=vm0,memdev=mem1')
        self.vm.set_qmp_monitor(enabled=False)
        self.vm.launch()
        self.vm.wait()
        self.assertEqual(self.vm.exitcode(), 1, "QEMU exit code should be 1")
        self.assertRegex(self.vm.get_log(), r'phys-bits too low')

    def test_phybits_ok_tcg_q35_70_amd(self):
        """
        :avocado: tags=machine:q35
        :avocado: tags=arch:x86_64

        Same as q35-7.0 AMD case except that here we check that QEMU can
        successfully start when maxmem is < 988G.
        """
        self.vm.add_args('-S', '-machine', 'pc-q35-7.0', '-m',
                         '512,slots=1,maxmem=987.5G',
                         '-display', 'none',
                         '-object', 'memory-backend-ram,id=mem1,size=1G',
                         '-device', 'pc-dimm,id=vm0,memdev=mem1')
        self.vm.set_qmp_monitor(enabled=False)
        self.vm.launch()
        time.sleep(self.DELAY_Q35_BOOT_SEQUENCE)
        self.vm.shutdown()
        self.assertNotRegex(self.vm.get_log(), r'phys-bits too low')

    def test_phybits_ok_tcg_q35_71_amd(self):
        """
        :avocado: tags=machine:q35
        :avocado: tags=arch:x86_64

        Same as q35-7.1 AMD case except that here we check that QEMU can
        successfully start when maxmem is < 976G.
        """
        self.vm.add_args('-S', '-machine', 'pc-q35-7.1', '-m',
                         '512,slots=1,maxmem=975.5G',
                         '-display', 'none',
                         '-object', 'memory-backend-ram,id=mem1,size=1G',
                         '-device', 'pc-dimm,id=vm0,memdev=mem1')
        self.vm.set_qmp_monitor(enabled=False)
        self.vm.launch()
        time.sleep(self.DELAY_Q35_BOOT_SEQUENCE)
        self.vm.shutdown()
        self.assertNotRegex(self.vm.get_log(), r'phys-bits too low')

    def test_phybits_ok_tcg_q35_71_intel(self):
        """
        :avocado: tags=machine:q35
        :avocado: tags=arch:x86_64

        Same parameters as test_phybits_low_tcg_q35_71_amd() but use
        Intel cpu instead. QEMU should start fine in this case as
        "above_4G" memory starts at 4G.
        """
        self.vm.add_args('-S', '-cpu', 'Skylake-Server',
                         '-machine', 'pc-q35-7.1', '-m',
                         '512,slots=1,maxmem=976G',
                         '-display', 'none',
                         '-object', 'memory-backend-ram,id=mem1,size=1G',
                         '-device', 'pc-dimm,id=vm0,memdev=mem1')
        self.vm.set_qmp_monitor(enabled=False)
        self.vm.launch()
        time.sleep(self.DELAY_Q35_BOOT_SEQUENCE)
        self.vm.shutdown()
        self.assertNotRegex(self.vm.get_log(), r'phys-bits too low')

    def test_phybits_low_tcg_q35_71_amd_41bits(self):
        """
        :avocado: tags=machine:q35
        :avocado: tags=arch:x86_64

        AMD processor with 41 bits. Max cpu hw address = 2 TiB.
        By setting maxram above 1012 GiB  - 32 GiB - 4 GiB = 976 GiB, we can
        force "above_4G" memory to start at 1 TiB for q35-7.1 machines
        (max GPA will be above AMD_HT_START which is defined as 1012 GiB).

        With pci_64_hole size at 32 GiB, in this case, maxmem should be 991.5
        GiB with 1 GiB per slot for alignment and 0.5 GiB as non-hotplug
        memory for the VM (1024 - 32 - 1 + 0.5). With 992 GiB, QEMU should
        fail to start.
        """
        self.vm.add_args('-S', '-cpu', 'EPYC-v4,phys-bits=41',
                         '-machine', 'pc-q35-7.1', '-m',
                         '512,slots=1,maxmem=992G',
                         '-display', 'none',
                         '-object', 'memory-backend-ram,id=mem1,size=1G',
                         '-device', 'pc-dimm,id=vm0,memdev=mem1')
        self.vm.set_qmp_monitor(enabled=False)
        self.vm.launch()
        self.vm.wait()
        self.assertEqual(self.vm.exitcode(), 1, "QEMU exit code should be 1")
        self.assertRegex(self.vm.get_log(), r'phys-bits too low')

    def test_phybits_ok_tcg_q35_71_amd_41bits(self):
        """
        :avocado: tags=machine:q35
        :avocado: tags=arch:x86_64

        AMD processor with 41 bits. Max cpu hw address = 2 TiB.
        Same as above but by setting maxram beween 976 GiB and 992 Gib,
        QEMU should start fine.
        """
        self.vm.add_args('-S', '-cpu', 'EPYC-v4,phys-bits=41',
                         '-machine', 'pc-q35-7.1', '-m',
                         '512,slots=1,maxmem=990G',
                         '-display', 'none',
                         '-object', 'memory-backend-ram,id=mem1,size=1G',
                         '-device', 'pc-dimm,id=vm0,memdev=mem1')
        self.vm.set_qmp_monitor(enabled=False)
        self.vm.launch()
        time.sleep(self.DELAY_Q35_BOOT_SEQUENCE)
        self.vm.shutdown()
        self.assertNotRegex(self.vm.get_log(), r'phys-bits too low')

    def test_phybits_low_tcg_q35_intel_cxl(self):
        """
        :avocado: tags=machine:q35
        :avocado: tags=arch:x86_64

        cxl memory window starts after memory device range. Here, we use 1 GiB
        of cxl window memory. 4G_mem end aligns at 4G. pci64_hole is 32 GiB and
        starts after the cxl memory window.
        So maxmem here should be at most 986 GiB considering all memory boundary
        alignment constraints with 40 bits (1 TiB) of processor physical bits.
        """
        self.vm.add_args('-S', '-cpu', 'Skylake-Server,phys-bits=40',
                         '-machine', 'q35,cxl=on', '-m',
                         '512,slots=1,maxmem=987G',
                         '-display', 'none',
                         '-device', 'pxb-cxl,bus_nr=12,bus=pcie.0,id=cxl.1',
                         '-M', 'cxl-fmw.0.targets.0=cxl.1,cxl-fmw.0.size=1G')
        self.vm.set_qmp_monitor(enabled=False)
        self.vm.launch()
        self.vm.wait()
        self.assertEqual(self.vm.exitcode(), 1, "QEMU exit code should be 1")
        self.assertRegex(self.vm.get_log(), r'phys-bits too low')

    def test_phybits_ok_tcg_q35_intel_cxl(self):
        """
        :avocado: tags=machine:q35
        :avocado: tags=arch:x86_64

        Same as above but here we do not reserve any cxl memory window. Hence,
        with the exact same parameters as above, QEMU should start fine even
        with cxl enabled.
        """
        self.vm.add_args('-S', '-cpu', 'Skylake-Server,phys-bits=40',
                         '-machine', 'q35,cxl=on', '-m',
                         '512,slots=1,maxmem=987G',
                         '-display', 'none',
                         '-device', 'pxb-cxl,bus_nr=12,bus=pcie.0,id=cxl.1')
        self.vm.set_qmp_monitor(enabled=False)
        self.vm.launch()
        time.sleep(self.DELAY_Q35_BOOT_SEQUENCE)
        self.vm.shutdown()
        self.assertNotRegex(self.vm.get_log(), r'phys-bits too low')
