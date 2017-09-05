/* stubs for non-pci builds */

#include "qemu/osdep.h"
#include "qemu-common.h"
#include "cpu.h"
#include "s390-pci-inst.h"
#include "s390-pci-bus.h"

/* target/s390x/ioinst.c */
int pci_chsc_sei_nt2_get_event(void *res)
{
    return 1;
}

int pci_chsc_sei_nt2_have_event(void)
{
    return 0;
}

/* hw/s390x/sclp.c */
void s390_pci_sclp_configure(SCCB *sccb)
{
    sccb->h.response_code = cpu_to_be16(SCLP_RC_ADAPTER_TYPE_NOT_RECOGNIZED);
}

void s390_pci_sclp_deconfigure(SCCB *sccb)
{
    sccb->h.response_code = cpu_to_be16(SCLP_RC_ADAPTER_TYPE_NOT_RECOGNIZED);
}

/* target/s390x/kvm.c */
int clp_service_call(S390CPU *cpu, uint8_t r2)
{
    return -1;
}

int pcilg_service_call(S390CPU *cpu, uint8_t r1, uint8_t r2)
{
    return -1;
}

int pcistg_service_call(S390CPU *cpu, uint8_t r1, uint8_t r2)
{
    return -1;
}

int stpcifc_service_call(S390CPU *cpu, uint8_t r1, uint64_t fiba, uint8_t ar)
{
    return -1;
}

int rpcit_service_call(S390CPU *cpu, uint8_t r1, uint8_t r2)
{
    return -1;
}

int pcistb_service_call(S390CPU *cpu, uint8_t r1, uint8_t r3, uint64_t gaddr,
                        uint8_t ar)
{
    return -1;
}

int mpcifc_service_call(S390CPU *cpu, uint8_t r1, uint64_t fiba, uint8_t ar)
{
    return -1;
}

S390pciState *s390_get_phb(void)
{
    return NULL;
}

S390PCIBusDevice *s390_pci_find_dev_by_target(S390pciState *s,
                                              const char *target)
{
    return NULL;
}
