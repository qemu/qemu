# Copyright (c) 2015, Intel Corporation
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#
#     * Redistributions of source code must retain the above copyright notice,
#       this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above copyright notice,
#       this list of conditions and the following disclaimer in the documentation
#       and/or other materials provided with the distribution.
#     * Neither the name of Intel Corporation nor the names of its contributors
#       may be used to endorse or promote products derived from this software
#       without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
# WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
# DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
# ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
# (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
# LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
# ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
# SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

"""Tests for ACPI"""

import acpi
import bits
import bits.mwait
import struct
import testutil
import testsuite
import time

def register_tests():
    testsuite.add_test("ACPI _MAT (Multiple APIC Table Entry) under Processor objects", test_mat, submenu="ACPI Tests")
    testsuite.add_test("ACPI _PSS (Pstate) table conformance tests", test_pss, submenu="ACPI Tests")
    testsuite.add_test("ACPI _PSS (Pstate) runtime tests", test_pstates, submenu="ACPI Tests")
    testsuite.add_test("ACPI DSDT (Differentiated System Description Table)", test_dsdt, submenu="ACPI Tests")
    testsuite.add_test("ACPI FACP (Fixed ACPI Description Table)", test_facp, submenu="ACPI Tests")
    testsuite.add_test("ACPI HPET (High Precision Event Timer Table)", test_hpet, submenu="ACPI Tests")
    testsuite.add_test("ACPI MADT (Multiple APIC Description Table)", test_apic, submenu="ACPI Tests")
    testsuite.add_test("ACPI MPST (Memory Power State Table)", test_mpst, submenu="ACPI Tests")
    testsuite.add_test("ACPI RSDP (Root System Description Pointer Structure)", test_rsdp, submenu="ACPI Tests")
    testsuite.add_test("ACPI XSDT (Extended System Description Table)", test_xsdt, submenu="ACPI Tests")

def test_mat():
    cpupaths = acpi.get_cpupaths()
    apic = acpi.parse_apic()
    procid_apicid = apic.procid_apicid
    uid_x2apicid = apic.uid_x2apicid
    for cpupath in cpupaths:
        # Find the ProcId defined by the processor object
        processor = acpi.evaluate(cpupath)
        # Find the UID defined by the processor object's _UID method
        uid = acpi.evaluate(cpupath + "._UID")
        mat_buffer = acpi.evaluate(cpupath + "._MAT")
        if mat_buffer is None:
            continue
        # Process each _MAT subtable
        mat = acpi._MAT(mat_buffer)
        for index, subtable in enumerate(mat):
            if subtable.subtype == acpi.MADT_TYPE_LOCAL_APIC:
                if subtable.flags.bits.enabled:
                    testsuite.test("{} Processor declaration ProcId = _MAT ProcId".format(cpupath), processor.ProcId == subtable.proc_id)
                    testsuite.print_detail("{} ProcId ({:#02x}) != _MAT ProcId ({:#02x})".format(cpupath, processor.ProcId, subtable.proc_id))
                    testsuite.print_detail("Processor Declaration: {}".format(processor))
                    testsuite.print_detail("_MAT entry[{}]: {}".format(index, subtable))
                    if testsuite.test("{} with local APIC in _MAT has local APIC in MADT".format(cpupath), processor.ProcId in procid_apicid):
                        testsuite.test("{} ApicId derived using Processor declaration ProcId = _MAT ApicId".format(cpupath), procid_apicid[processor.ProcId] == subtable.apic_id)
                        testsuite.print_detail("{} ApicId derived from MADT ({:#02x}) != _MAT ApicId ({:#02x})".format(cpupath, procid_apicid[processor.ProcId], subtable.apic_id))
                        testsuite.print_detail("Processor Declaration: {}".format(processor))
                        testsuite.print_detail("_MAT entry[{}]: {}".format(index, subtable))
            if subtable.subtype == acpi.MADT_TYPE_LOCAL_X2APIC:
                if subtable.flags.bits.enabled:
                    if testsuite.test("{} with x2Apic in _MAT has _UID".format(cpupath), uid is not None):
                        testsuite.test("{}._UID = _MAT UID".format(cpupath), uid == subtable.uid)
                        testsuite.print_detail("{}._UID ({:#x}) != _MAT UID ({:#x})".format(cpupath, uid, subtable.uid))
                        testsuite.print_detail("_MAT entry[{}]: {}".format(index, subtable))
                    if testsuite.test("{} with _MAT x2Apic has x2Apic in MADT".format(cpupath), subtable.uid in uid_x2apicid):
                        testsuite.test("{} x2ApicId derived from MADT using UID = _MAT x2ApicId".format(cpupath), uid_x2apicid[subtable.uid] == subtable.x2apicid)
                        testsuite.print_detail("{} x2ApicId derived from MADT ({:#02x}) != _MAT x2ApicId ({:#02x})".format(cpupath, uid_x2apicid[subtable.uid], subtable.x2apicid))
                        testsuite.print_detail("_MAT entry[{}]: {}".format(index, subtable))

def test_pss():
    uniques = acpi.parse_cpu_method("_PSS")
    # We special-case None here to avoid a double-failure for CPUs without a _PSS
    testsuite.test("_PSS must be identical for all CPUs", len(uniques) <= 1 or (len(uniques) == 2 and None in uniques))
    for pss, cpupaths in uniques.iteritems():
        if not testsuite.test("_PSS must exist", pss is not None):
            testsuite.print_detail(acpi.factor_commonprefix(cpupaths))
            testsuite.print_detail('No _PSS exists')
            continue

        if not testsuite.test("_PSS must not be empty", pss.pstates):
            testsuite.print_detail(acpi.factor_commonprefix(cpupaths))
            testsuite.print_detail('_PSS is empty')
            continue

        testsuite.print_detail(acpi.factor_commonprefix(cpupaths))
        for index, pstate in enumerate(pss.pstates):
            testsuite.print_detail("P[{}]: {}".format(index, pstate))

        testsuite.test("_PSS must contain at most 16 Pstates", len(pss.pstates) <= 16)
        testsuite.test("_PSS must have no duplicate Pstates", len(pss.pstates) == len(set(pss.pstates)))

        frequencies = [p.core_frequency for p in pss.pstates]
        testsuite.test("_PSS must list Pstates in descending order of frequency", frequencies == sorted(frequencies, reverse=True))

        testsuite.test("_PSS must have Pstates with no duplicate frequencies", len(frequencies) == len(set(frequencies)))

        dissipations = [p.power for p in pss.pstates]
        testsuite.test("_PSS must list Pstates in descending order of power dissipation", dissipations == sorted(dissipations, reverse=True))

def test_pstates():
    """Execute and verify frequency for each Pstate in the _PSS"""
    IA32_PERF_CTL = 0x199
    with bits.mwait.use_hint(), bits.preserve_msr(IA32_PERF_CTL):
        cpupath_procid = acpi.find_procid()
        cpupath_uid = acpi.find_uid()
        apic = acpi.parse_apic()
        procid_apicid = apic.procid_apicid
        uid_x2apicid = apic.uid_x2apicid
        def cpupath_apicid(cpupath):
            if procid_apicid is not None:
                procid = cpupath_procid.get(cpupath, None)
                if procid is not None:
                    apicid = procid_apicid.get(procid, None)
                    if apicid is not None:
                        return apicid
            if uid_x2apicid is not None:
                uid = cpupath_uid.get(cpupath, None)
                if uid is not None:
                    apicid = uid_x2apicid.get(uid, None)
                    if apicid is not None:
                        return apicid
            return bits.cpus()[0]

        bclk = testutil.adjust_to_nearest(bits.bclk(), 100.0/12) * 1000000

        uniques = acpi.parse_cpu_method("_PSS")
        for pss, cpupaths in uniques.iteritems():
            if not testsuite.test("_PSS must exist", pss is not None):
                testsuite.print_detail(acpi.factor_commonprefix(cpupaths))
                testsuite.print_detail('No _PSS exists')
                continue

            for n, pstate in enumerate(pss.pstates):
                for cpupath in cpupaths:
                    apicid = cpupath_apicid(cpupath)
                    if apicid is None:
                        print 'Failed to find apicid for cpupath {}'.format(cpupath)
                        continue
                    bits.wrmsr(apicid, IA32_PERF_CTL, pstate.control)

                # Detecting Turbo frequency requires at least 2 pstates
                # since turbo frequency = max non-turbo frequency + 1
                turbo = False
                if len(pss.pstates) >= 2:
                    turbo = (n == 0 and pstate.core_frequency == (pss.pstates[1].core_frequency + 1))
                    if turbo:
                        # Needs to busywait, not sleep
                        start = time.time()
                        while (time.time() - start < 2):
                            pass

                for duration in (0.1, 1.0):
                    frequency_data = bits.cpu_frequency(duration)
                    # Abort the test if no cpu frequency is not available
                    if frequency_data is None:
                        continue
                    aperf = frequency_data[1]
                    aperf = testutil.adjust_to_nearest(aperf, bclk/2)
                    aperf = int(aperf / 1000000)
                    if turbo:
                        if aperf >= pstate.core_frequency:
                            break
                    else:
                        if aperf == pstate.core_frequency:
                            break

                if turbo:
                    testsuite.test("P{}: Turbo measured frequency {} >= expected {} MHz".format(n, aperf, pstate.core_frequency), aperf >= pstate.core_frequency)
                else:
                    testsuite.test("P{}: measured frequency {} MHz == expected {} MHz".format(n, aperf, pstate.core_frequency), aperf == pstate.core_frequency)

def test_psd_thread_scope():
    uniques = acpi.parse_cpu_method("_PSD")
    if not testsuite.test("_PSD (P-State Dependency) must exist for each processor", None not in uniques):
        testsuite.print_detail(acpi.factor_commonprefix(uniques[None]))
        testsuite.print_detail('No _PSD exists')
        return
    unique_num_dependencies = {}
    unique_num_entries = {}
    unique_revision = {}
    unique_domain = {}
    unique_coordination_type = {}
    unique_num_processors = {}
    for value, cpupaths in uniques.iteritems():
        unique_num_dependencies.setdefault(len(value.dependencies), []).extend(cpupaths)
        unique_num_entries.setdefault(value.dependencies[0].num_entries, []).extend(cpupaths)
        unique_revision.setdefault(value.dependencies[0].revision, []).extend(cpupaths)
        unique_domain.setdefault(value.dependencies[0].domain, []).extend(cpupaths)
        unique_coordination_type.setdefault(value.dependencies[0].coordination_type, []).extend(cpupaths)
        unique_num_processors.setdefault(value.dependencies[0].num_processors, []).extend(cpupaths)
    def detail(d, fmt):
        for value, cpupaths in sorted(d.iteritems(), key=(lambda (k,v): v)):
            testsuite.print_detail(acpi.factor_commonprefix(cpupaths))
            testsuite.print_detail(fmt.format(value))

    testsuite.test('Dependency count for each processor must be 1', unique_num_dependencies.keys() == [1])
    detail(unique_num_dependencies, 'Dependency count for each processor = {} (Expected 1)')
    testsuite.test('_PSD.num_entries must be 5', unique_num_entries.keys() == [5])
    detail(unique_num_entries, 'num_entries = {} (Expected 5)')
    testsuite.test('_PSD.revision must be 0', unique_revision.keys() == [0])
    detail(unique_revision, 'revision = {}')
    testsuite.test('_PSD.coordination_type must be 0xFE (HW_ALL)', unique_coordination_type.keys() == [0xfe])
    detail(unique_coordination_type, 'coordination_type = {:#x} (Expected 0xFE HW_ALL)')
    testsuite.test('_PSD.domain must be unique (thread-scoped) for each processor', len(unique_domain) == len(acpi.get_cpupaths()))
    detail(unique_domain, 'domain = {:#x} (Expected a unique value for each processor)')
    testsuite.test('_PSD.num_processors must be 1', unique_num_processors.keys() == [1])
    detail(unique_num_processors, 'num_processors = {} (Expected 1)')

def test_table_checksum(data):
    csum = sum(ord(c) for c in data) % 0x100
    testsuite.test('ACPI table cumulative checksum must equal 0', csum == 0)
    testsuite.print_detail("Cumulative checksum = {} (Expected 0)".format(csum))

def test_apic():
    data = acpi.get_table("APIC")
    if data is None:
        return
    test_table_checksum(data)
    apic = acpi.parse_apic()

def test_dsdt():
    data = acpi.get_table("DSDT")
    if data is None:
        return
    test_table_checksum(data)

def test_facp():
    data = acpi.get_table("FACP")
    if data is None:
        return
    test_table_checksum(data)
    facp = acpi.parse_facp()

def test_hpet():
    data = acpi.get_table("HPET")
    if data is None:
        return
    test_table_checksum(data)
    hpet = acpi.parse_hpet()

def test_mpst():
    data = acpi.get_table("MPST")
    if data is None:
        return
    test_table_checksum(data)
    mpst = acpi.MPST(data)

def test_rsdp():
    data = acpi.get_table("RSD PTR ")
    if data is None:
        return

    # Checksum the first 20 bytes per ACPI 1.0
    csum = sum(ord(c) for c in data[:20]) % 0x100
    testsuite.test('ACPI 1.0 table first 20 bytes cummulative checksum must equal 0', csum == 0)
    testsuite.print_detail("Cummulative checksum = {} (Expected 0)".format(csum))

    test_table_checksum(data)
    rsdp = acpi.parse_rsdp()

def test_xsdt():
    data = acpi.get_table("XSDT")
    if data is None:
        return
    test_table_checksum(data)
    xsdt = acpi.parse_xsdt()
