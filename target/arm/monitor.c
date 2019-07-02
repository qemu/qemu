/*
 * QEMU monitor.c for ARM.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "hw/boards.h"
#include "kvm_arm.h"
#include "qapi/qapi-commands-misc-target.h"

static GICCapability *gic_cap_new(int version)
{
    GICCapability *cap = g_new0(GICCapability, 1);
    cap->version = version;
    /* by default, support none */
    cap->emulated = false;
    cap->kernel = false;
    return cap;
}

static GICCapabilityList *gic_cap_list_add(GICCapabilityList *head,
                                           GICCapability *cap)
{
    GICCapabilityList *item = g_new0(GICCapabilityList, 1);
    item->value = cap;
    item->next = head;
    return item;
}

static inline void gic_cap_kvm_probe(GICCapability *v2, GICCapability *v3)
{
#ifdef CONFIG_KVM
    int fdarray[3];

    if (!kvm_arm_create_scratch_host_vcpu(NULL, fdarray, NULL)) {
        return;
    }

    /* Test KVM GICv2 */
    if (kvm_device_supported(fdarray[1], KVM_DEV_TYPE_ARM_VGIC_V2)) {
        v2->kernel = true;
    }

    /* Test KVM GICv3 */
    if (kvm_device_supported(fdarray[1], KVM_DEV_TYPE_ARM_VGIC_V3)) {
        v3->kernel = true;
    }

    kvm_arm_destroy_scratch_host_vcpu(fdarray);
#endif
}

GICCapabilityList *qmp_query_gic_capabilities(Error **errp)
{
    GICCapabilityList *head = NULL;
    GICCapability *v2 = gic_cap_new(2), *v3 = gic_cap_new(3);

    v2->emulated = true;
    v3->emulated = true;

    gic_cap_kvm_probe(v2, v3);

    head = gic_cap_list_add(head, v2);
    head = gic_cap_list_add(head, v3);

    return head;
}
