# Copyright (c) 2012, Intel Corporation
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

"""Tests and helpers for CPUID."""

import bits
import testsuite
import testutil

def cpuid_helper(function, index=None, shift=0, mask=~0, eax_mask=~0, ebx_mask=~0, ecx_mask=~0, edx_mask=~0):
    if index is None:
        index = 0
        indexdesc = ""
    else:
        indexdesc = " index {0:#x}".format(index)

    def find_mask(m):
        if m == ~0:
            return mask
        return m
    masks = map(find_mask, [eax_mask, ebx_mask, ecx_mask, edx_mask])

    uniques = {}
    for cpu in bits.cpus():
        regs = bits.cpuid_result(*[(r >> shift) & m for r, m in zip(bits.cpuid(cpu, function, index), masks)])
        uniques.setdefault(regs, []).append(cpu)

    desc = ["CPUID function {:#x}{}".format(function, indexdesc)]

    if shift != 0:
        desc.append("Register values have been shifted by {}".format(shift))
    if mask != ~0 or eax_mask != ~0 or ebx_mask != ~0 or ecx_mask != ~0 or edx_mask != ~0:
        desc.append("Register values have been masked:")
        shifted_masks = bits.cpuid_result(*[m << shift for m in masks])
        desc.append("Masks:           eax={eax:#010x} ebx={ebx:#010x} ecx={ecx:#010x} edx={edx:#010x}".format(**shifted_masks._asdict()))

    if len(uniques) > 1:
        regvalues = zip(*uniques.iterkeys())
        common_masks = bits.cpuid_result(*map(testutil.find_common_mask, regvalues))
        common_values = bits.cpuid_result(*[v[0] & m for v, m in zip(regvalues, common_masks)])
        desc.append('Register values are not unique across all logical processors')
        desc.append("Common bits:     eax={eax:#010x} ebx={ebx:#010x} ecx={ecx:#010x} edx={edx:#010x}".format(**common_values._asdict()))
        desc.append("Mask of common bits: {eax:#010x}     {ebx:#010x}     {ecx:#010x}     {edx:#010x}".format(**common_masks._asdict()))

    for regs in sorted(uniques.iterkeys()):
        cpus = uniques[regs]
        desc.append("Register value:  eax={eax:#010x} ebx={ebx:#010x} ecx={ecx:#010x} edx={edx:#010x}".format(**regs._asdict()))
        desc.append("On {0} CPUs: {1}".format(len(cpus), testutil.apicid_list(cpus)))

    return uniques, desc

def test_cpuid_consistency(text, function, index=None, shift=0, mask=~0, eax_mask=~0, ebx_mask=~0, ecx_mask=~0, edx_mask=~0):
    uniques, desc = cpuid_helper(function, index, shift, mask, eax_mask, ebx_mask, ecx_mask, edx_mask)
    desc[0] += " Consistency Check"
    if text:
        desc.insert(0, text)
    status = testsuite.test(desc[0], len(uniques) == 1)
    for line in desc[1:]:
        testsuite.print_detail(line)
    return status
