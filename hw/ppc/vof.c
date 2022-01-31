/*
 * QEMU PowerPC Virtual Open Firmware.
 *
 * This implements client interface from OpenFirmware IEEE1275 on the QEMU
 * side to leave only a very basic firmware in the VM.
 *
 * Copyright (c) 2021 IBM Corporation.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu-common.h"
#include "qemu/timer.h"
#include "qemu/range.h"
#include "qemu/units.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "exec/address-spaces.h"
#include "hw/ppc/vof.h"
#include "hw/ppc/fdt.h"
#include "sysemu/runstate.h"
#include "qom/qom-qobject.h"
#include "trace.h"

#include <libfdt.h>

/*
 * OF 1275 "nextprop" description suggests is it 32 bytes max but
 * LoPAPR defines "ibm,query-interrupt-source-number" which is 33 chars long.
 */
#define OF_PROPNAME_LEN_MAX 64

#define VOF_MAX_PATH        256
#define VOF_MAX_SETPROPLEN  2048
#define VOF_MAX_METHODLEN   256
#define VOF_MAX_FORTHCODE   256
#define VOF_VTY_BUF_SIZE    256

typedef struct {
    uint64_t start;
    uint64_t size;
} OfClaimed;

typedef struct {
    char *path; /* the path used to open the instance */
    uint32_t phandle;
} OfInstance;

static int readstr(hwaddr pa, char *buf, int size)
{
    if (VOF_MEM_READ(pa, buf, size) != MEMTX_OK) {
        return -1;
    }
    if (strnlen(buf, size) == size) {
        buf[size - 1] = '\0';
        trace_vof_error_str_truncated(buf, size);
        return -1;
    }
    return 0;
}

static bool cmpservice(const char *s, unsigned nargs, unsigned nret,
                       const char *s1, unsigned nargscheck, unsigned nretcheck)
{
    if (strcmp(s, s1)) {
        return false;
    }
    if ((nargscheck && (nargs != nargscheck)) ||
        (nretcheck && (nret != nretcheck))) {
        trace_vof_error_param(s, nargscheck, nretcheck, nargs, nret);
        return false;
    }

    return true;
}

static void prop_format(char *tval, int tlen, const void *prop, int len)
{
    int i;
    const unsigned char *c;
    char *t;
    const char bin[] = "...";

    for (i = 0, c = prop; i < len; ++i, ++c) {
        if (*c == '\0' && i == len - 1) {
            strncpy(tval, prop, tlen - 1);
            return;
        }
        if (*c < 0x20 || *c >= 0x80) {
            break;
        }
    }

    for (i = 0, c = prop, t = tval; i < len; ++i, ++c) {
        if (t >= tval + tlen - sizeof(bin) - 1 - 2 - 1) {
            strcpy(t, bin);
            return;
        }
        if (i && i % 4 == 0 && i != len - 1) {
            strcat(t, " ");
            ++t;
        }
        t += sprintf(t, "%02X", *c & 0xFF);
    }
}

static int get_path(const void *fdt, int offset, char *buf, int len)
{
    int ret;

    ret = fdt_get_path(fdt, offset, buf, len - 1);
    if (ret < 0) {
        return ret;
    }

    buf[len - 1] = '\0';

    return strlen(buf) + 1;
}

static int phandle_to_path(const void *fdt, uint32_t ph, char *buf, int len)
{
    int ret;

    ret = fdt_node_offset_by_phandle(fdt, ph);
    if (ret < 0) {
        return ret;
    }

    return get_path(fdt, ret, buf, len);
}

static int path_offset(const void *fdt, const char *path)
{
    g_autofree char *p = NULL;
    char *at;

    /*
     * https://www.devicetree.org/open-firmware/bindings/ppc/release/ppc-2_1.html#HDR16
     *
     * "Conversion from numeric representation to text representation shall use
     * the lower case forms of the hexadecimal digits in the range a..f,
     * suppressing leading zeros".
     */
    p = g_strdup(path);
    for (at = strchr(p, '@'); at && *at; ) {
            if (*at == '/') {
                at = strchr(at, '@');
            } else {
                *at = tolower(*at);
                ++at;
            }
    }

    return fdt_path_offset(fdt, p);
}

static uint32_t vof_finddevice(const void *fdt, uint32_t nodeaddr)
{
    char fullnode[VOF_MAX_PATH];
    uint32_t ret = PROM_ERROR;
    int offset;

    if (readstr(nodeaddr, fullnode, sizeof(fullnode))) {
        return (uint32_t) ret;
    }

    offset = path_offset(fdt, fullnode);
    if (offset >= 0) {
        ret = fdt_get_phandle(fdt, offset);
    }
    trace_vof_finddevice(fullnode, ret);
    return ret;
}

static const void *getprop(const void *fdt, int nodeoff, const char *propname,
                           int *proplen, bool *write0)
{
    const char *unit, *prop;
    const void *ret = fdt_getprop(fdt, nodeoff, propname, proplen);

    if (ret) {
        if (write0) {
            *write0 = false;
        }
        return ret;
    }

    if (strcmp(propname, "name")) {
        return NULL;
    }
    /*
     * We return a value for "name" from path if queried but property does not
     * exist. @proplen does not include the unit part in this case.
     */
    prop = fdt_get_name(fdt, nodeoff, proplen);
    if (!prop) {
        *proplen = 0;
        return NULL;
    }

    unit = memchr(prop, '@', *proplen);
    if (unit) {
        *proplen = unit - prop;
    }
    *proplen += 1;

    /*
     * Since it might be cut at "@" and there will be no trailing zero
     * in the prop buffer, tell the caller to write zero at the end.
     */
    if (write0) {
        *write0 = true;
    }
    return prop;
}

static uint32_t vof_getprop(const void *fdt, uint32_t nodeph, uint32_t pname,
                            uint32_t valaddr, uint32_t vallen)
{
    char propname[OF_PROPNAME_LEN_MAX + 1];
    uint32_t ret = 0;
    int proplen = 0;
    const void *prop;
    char trval[64] = "";
    int nodeoff = fdt_node_offset_by_phandle(fdt, nodeph);
    bool write0;

    if (nodeoff < 0) {
        return PROM_ERROR;
    }
    if (readstr(pname, propname, sizeof(propname))) {
        return PROM_ERROR;
    }
    prop = getprop(fdt, nodeoff, propname, &proplen, &write0);
    if (prop) {
        const char zero = 0;
        int cb = MIN(proplen, vallen);

        if (VOF_MEM_WRITE(valaddr, prop, cb) != MEMTX_OK ||
            /* if that was "name" with a unit address, overwrite '@' with '0' */
            (write0 &&
             cb == proplen &&
             VOF_MEM_WRITE(valaddr + cb - 1, &zero, 1) != MEMTX_OK)) {
            ret = PROM_ERROR;
        } else {
            /*
             * OF1275 says:
             * "Size is either the actual size of the property, or -1 if name
             * does not exist", hence returning proplen instead of cb.
             */
            ret = proplen;
            /* Do not format a value if tracepoint is silent, for performance */
            if (trace_event_get_state(TRACE_VOF_GETPROP) &&
                qemu_loglevel_mask(LOG_TRACE)) {
                prop_format(trval, sizeof(trval), prop, ret);
            }
        }
    } else {
        ret = PROM_ERROR;
    }
    trace_vof_getprop(nodeph, propname, ret, trval);

    return ret;
}

static uint32_t vof_getproplen(const void *fdt, uint32_t nodeph, uint32_t pname)
{
    char propname[OF_PROPNAME_LEN_MAX + 1];
    uint32_t ret = 0;
    int proplen = 0;
    const void *prop;
    int nodeoff = fdt_node_offset_by_phandle(fdt, nodeph);

    if (nodeoff < 0) {
        return PROM_ERROR;
    }
    if (readstr(pname, propname, sizeof(propname))) {
        return PROM_ERROR;
    }
    prop = getprop(fdt, nodeoff, propname, &proplen, NULL);
    if (prop) {
        ret = proplen;
    } else {
        ret = PROM_ERROR;
    }
    trace_vof_getproplen(nodeph, propname, ret);

    return ret;
}

static uint32_t vof_setprop(MachineState *ms, void *fdt, Vof *vof,
                            uint32_t nodeph, uint32_t pname,
                            uint32_t valaddr, uint32_t vallen)
{
    char propname[OF_PROPNAME_LEN_MAX + 1];
    uint32_t ret = PROM_ERROR;
    int offset, rc;
    char trval[64] = "";
    char nodepath[VOF_MAX_PATH] = "";
    Object *vmo = object_dynamic_cast(OBJECT(ms), TYPE_VOF_MACHINE_IF);
    VofMachineIfClass *vmc;
    g_autofree char *val = NULL;

    if (vallen > VOF_MAX_SETPROPLEN) {
        goto trace_exit;
    }
    if (readstr(pname, propname, sizeof(propname))) {
        goto trace_exit;
    }
    offset = fdt_node_offset_by_phandle(fdt, nodeph);
    if (offset < 0) {
        goto trace_exit;
    }
    rc = get_path(fdt, offset, nodepath, sizeof(nodepath));
    if (rc <= 0) {
        goto trace_exit;
    }

    val = g_malloc0(vallen);
    if (VOF_MEM_READ(valaddr, val, vallen) != MEMTX_OK) {
        goto trace_exit;
    }

    if (!vmo) {
        goto trace_exit;
    }

    vmc = VOF_MACHINE_GET_CLASS(vmo);
    if (!vmc->setprop || !vmc->setprop(ms, nodepath, propname, val, vallen)) {
        goto trace_exit;
    }

    rc = fdt_setprop(fdt, offset, propname, val, vallen);
    if (rc) {
        goto trace_exit;
    }

    if (trace_event_get_state(TRACE_VOF_SETPROP) &&
        qemu_loglevel_mask(LOG_TRACE)) {
        prop_format(trval, sizeof(trval), val, vallen);
    }
    ret = vallen;

trace_exit:
    trace_vof_setprop(nodeph, propname, trval, vallen, ret);

    return ret;
}

static uint32_t vof_nextprop(const void *fdt, uint32_t phandle,
                             uint32_t prevaddr, uint32_t nameaddr)
{
    int offset, nodeoff = fdt_node_offset_by_phandle(fdt, phandle);
    char prev[OF_PROPNAME_LEN_MAX + 1];
    const char *tmp;

    if (readstr(prevaddr, prev, sizeof(prev))) {
        return PROM_ERROR;
    }

    fdt_for_each_property_offset(offset, fdt, nodeoff) {
        if (!fdt_getprop_by_offset(fdt, offset, &tmp, NULL)) {
            return 0;
        }
        if (prev[0] == '\0' || strcmp(prev, tmp) == 0) {
            if (prev[0] != '\0') {
                offset = fdt_next_property_offset(fdt, offset);
                if (offset < 0) {
                    return 0;
                }
            }
            if (!fdt_getprop_by_offset(fdt, offset, &tmp, NULL)) {
                return 0;
            }

            if (VOF_MEM_WRITE(nameaddr, tmp, strlen(tmp) + 1) != MEMTX_OK) {
                return PROM_ERROR;
            }
            return 1;
        }
    }

    return 0;
}

static uint32_t vof_peer(const void *fdt, uint32_t phandle)
{
    uint32_t ret = 0;
    int rc;

    if (phandle == 0) {
        rc = fdt_path_offset(fdt, "/");
    } else {
        rc = fdt_next_subnode(fdt, fdt_node_offset_by_phandle(fdt, phandle));
    }

    if (rc >= 0) {
        ret = fdt_get_phandle(fdt, rc);
    }

    return ret;
}

static uint32_t vof_child(const void *fdt, uint32_t phandle)
{
    uint32_t ret = 0;
    int rc = fdt_first_subnode(fdt, fdt_node_offset_by_phandle(fdt, phandle));

    if (rc >= 0) {
        ret = fdt_get_phandle(fdt, rc);
    }

    return ret;
}

static uint32_t vof_parent(const void *fdt, uint32_t phandle)
{
    uint32_t ret = 0;
    int rc = fdt_parent_offset(fdt, fdt_node_offset_by_phandle(fdt, phandle));

    if (rc >= 0) {
        ret = fdt_get_phandle(fdt, rc);
    }

    return ret;
}

static uint32_t vof_do_open(void *fdt, Vof *vof, int offset, const char *path)
{
    uint32_t ret = PROM_ERROR;
    OfInstance *inst = NULL;

    if (vof->of_instance_last == 0xFFFFFFFF) {
        /* We do not recycle ihandles yet */
        goto trace_exit;
    }

    inst = g_new0(OfInstance, 1);
    inst->phandle = fdt_get_phandle(fdt, offset);
    g_assert(inst->phandle);
    ++vof->of_instance_last;

    inst->path = g_strdup(path);
    g_hash_table_insert(vof->of_instances,
                        GINT_TO_POINTER(vof->of_instance_last),
                        inst);
    ret = vof->of_instance_last;

trace_exit:
    trace_vof_open(path, inst ? inst->phandle : 0, ret);

    return ret;
}

uint32_t vof_client_open_store(void *fdt, Vof *vof, const char *nodename,
                               const char *prop, const char *path)
{
    int offset, node = fdt_path_offset(fdt, nodename);
    uint32_t inst;

    offset = fdt_path_offset(fdt, path);
    if (offset < 0) {
        trace_vof_error_unknown_path(path);
        return PROM_ERROR;
    }

    inst = vof_do_open(fdt, vof, offset, path);

    return fdt_setprop_cell(fdt, node, prop, inst) >= 0 ? 0 : PROM_ERROR;
}

static uint32_t vof_open(void *fdt, Vof *vof, uint32_t pathaddr)
{
    char path[VOF_MAX_PATH];
    int offset;

    if (readstr(pathaddr, path, sizeof(path))) {
        return PROM_ERROR;
    }

    offset = path_offset(fdt, path);
    if (offset < 0) {
        trace_vof_error_unknown_path(path);
        return PROM_ERROR;
    }

    return vof_do_open(fdt, vof, offset, path);
}

static void vof_close(Vof *vof, uint32_t ihandle)
{
    if (!g_hash_table_remove(vof->of_instances, GINT_TO_POINTER(ihandle))) {
        trace_vof_error_unknown_ihandle_close(ihandle);
    }
}

static uint32_t vof_instance_to_package(Vof *vof, uint32_t ihandle)
{
    gpointer instp = g_hash_table_lookup(vof->of_instances,
                                         GINT_TO_POINTER(ihandle));
    uint32_t ret = PROM_ERROR;

    if (instp) {
        ret = ((OfInstance *)instp)->phandle;
    }
    trace_vof_instance_to_package(ihandle, ret);

    return ret;
}

static uint32_t vof_package_to_path(const void *fdt, uint32_t phandle,
                                    uint32_t buf, uint32_t len)
{
    int rc;
    char tmp[VOF_MAX_PATH] = "";

    rc = phandle_to_path(fdt, phandle, tmp, sizeof(tmp));
    if (rc > 0) {
        if (VOF_MEM_WRITE(buf, tmp, rc) != MEMTX_OK) {
            rc = -1;
        }
    }

    trace_vof_package_to_path(phandle, tmp, rc);

    return rc > 0 ? (uint32_t)rc : PROM_ERROR;
}

static uint32_t vof_instance_to_path(void *fdt, Vof *vof, uint32_t ihandle,
                                     uint32_t buf, uint32_t len)
{
    int rc = -1;
    uint32_t phandle = vof_instance_to_package(vof, ihandle);
    char tmp[VOF_MAX_PATH] = "";

    if (phandle != -1) {
        rc = phandle_to_path(fdt, phandle, tmp, sizeof(tmp));
        if (rc > 0) {
            if (VOF_MEM_WRITE(buf, tmp, rc) != MEMTX_OK) {
                rc = -1;
            }
        }
    }
    trace_vof_instance_to_path(ihandle, phandle, tmp, rc);

    return rc > 0 ? (uint32_t)rc : PROM_ERROR;
}

static uint32_t vof_write(Vof *vof, uint32_t ihandle, uint32_t buf,
                          uint32_t len)
{
    char tmp[VOF_VTY_BUF_SIZE];
    unsigned cb;
    OfInstance *inst = (OfInstance *)
        g_hash_table_lookup(vof->of_instances, GINT_TO_POINTER(ihandle));

    if (!inst) {
        trace_vof_error_write(ihandle);
        return PROM_ERROR;
    }

    for ( ; len > 0; len -= cb) {
        cb = MIN(len, sizeof(tmp) - 1);
        if (VOF_MEM_READ(buf, tmp, cb) != MEMTX_OK) {
            return PROM_ERROR;
        }

        /* FIXME: there is no backend(s) yet so just call a trace */
        if (trace_event_get_state(TRACE_VOF_WRITE) &&
            qemu_loglevel_mask(LOG_TRACE)) {
            tmp[cb] = '\0';
            trace_vof_write(ihandle, cb, tmp);
        }
    }

    return len;
}

static void vof_claimed_dump(GArray *claimed)
{
    int i;
    OfClaimed c;

    if (trace_event_get_state(TRACE_VOF_CLAIMED) &&
        qemu_loglevel_mask(LOG_TRACE)) {

        for (i = 0; i < claimed->len; ++i) {
            c = g_array_index(claimed, OfClaimed, i);
            trace_vof_claimed(c.start, c.start + c.size, c.size);
        }
    }
}

static bool vof_claim_avail(GArray *claimed, uint64_t virt, uint64_t size)
{
    int i;
    OfClaimed c;

    for (i = 0; i < claimed->len; ++i) {
        c = g_array_index(claimed, OfClaimed, i);
        if (ranges_overlap(c.start, c.size, virt, size)) {
            return false;
        }
    }

    return true;
}

static void vof_claim_add(GArray *claimed, uint64_t virt, uint64_t size)
{
    OfClaimed newclaim;

    newclaim.start = virt;
    newclaim.size = size;
    g_array_append_val(claimed, newclaim);
}

static gint of_claimed_compare_func(gconstpointer a, gconstpointer b)
{
    return ((OfClaimed *)a)->start - ((OfClaimed *)b)->start;
}

static void vof_dt_memory_available(void *fdt, GArray *claimed, uint64_t base)
{
    int i, n, offset, proplen = 0, sc, ac;
    target_ulong mem0_end;
    const uint8_t *mem0_reg;
    g_autofree uint8_t *avail = NULL;
    uint8_t *availcur;

    if (!fdt || !claimed) {
        return;
    }

    offset = fdt_path_offset(fdt, "/");
    _FDT(offset);
    ac = fdt_address_cells(fdt, offset);
    g_assert(ac == 1 || ac == 2);
    sc = fdt_size_cells(fdt, offset);
    g_assert(sc == 1 || sc == 2);

    offset = fdt_path_offset(fdt, "/memory@0");
    _FDT(offset);

    mem0_reg = fdt_getprop(fdt, offset, "reg", &proplen);
    g_assert(mem0_reg && proplen == sizeof(uint32_t) * (ac + sc));
    if (sc == 2) {
        mem0_end = be64_to_cpu(*(uint64_t *)(mem0_reg + sizeof(uint32_t) * ac));
    } else {
        mem0_end = be32_to_cpu(*(uint32_t *)(mem0_reg + sizeof(uint32_t) * ac));
    }

    g_array_sort(claimed, of_claimed_compare_func);
    vof_claimed_dump(claimed);

    /*
     * VOF resides in the first page so we do not need to check if there is
     * available memory before the first claimed block
     */
    g_assert(claimed->len && (g_array_index(claimed, OfClaimed, 0).start == 0));

    avail = g_malloc0(sizeof(uint32_t) * (ac + sc) * claimed->len);
    for (i = 0, n = 0, availcur = avail; i < claimed->len; ++i) {
        OfClaimed c = g_array_index(claimed, OfClaimed, i);
        uint64_t start, size;

        start = c.start + c.size;
        if (i < claimed->len - 1) {
            OfClaimed cn = g_array_index(claimed, OfClaimed, i + 1);

            size = cn.start - start;
        } else {
            size = mem0_end - start;
        }

        if (ac == 2) {
            *(uint64_t *) availcur = cpu_to_be64(start);
        } else {
            *(uint32_t *) availcur = cpu_to_be32(start);
        }
        availcur += sizeof(uint32_t) * ac;
        if (sc == 2) {
            *(uint64_t *) availcur = cpu_to_be64(size);
        } else {
            *(uint32_t *) availcur = cpu_to_be32(size);
        }
        availcur += sizeof(uint32_t) * sc;

        if (size) {
            trace_vof_avail(c.start + c.size, c.start + c.size + size, size);
            ++n;
        }
    }
    _FDT((fdt_setprop(fdt, offset, "available", avail, availcur - avail)));
}

/*
 * OF1275:
 * "Allocates size bytes of memory. If align is zero, the allocated range
 * begins at the virtual address virt. Otherwise, an aligned address is
 * automatically chosen and the input argument virt is ignored".
 *
 * In other words, exactly one of @virt and @align is non-zero.
 */
uint64_t vof_claim(Vof *vof, uint64_t virt, uint64_t size,
                   uint64_t align)
{
    uint64_t ret;

    if (size == 0) {
        ret = -1;
    } else if (align == 0) {
        if (!vof_claim_avail(vof->claimed, virt, size)) {
            ret = -1;
        } else {
            ret = virt;
        }
    } else {
        vof->claimed_base = QEMU_ALIGN_UP(vof->claimed_base, align);
        while (1) {
            if (vof->claimed_base >= vof->top_addr) {
                error_report("Out of RMA memory for the OF client");
                return -1;
            }
            if (vof_claim_avail(vof->claimed, vof->claimed_base, size)) {
                break;
            }
            vof->claimed_base += size;
        }
        ret = vof->claimed_base;
    }

    if (ret != -1) {
        vof->claimed_base = MAX(vof->claimed_base, ret + size);
        vof_claim_add(vof->claimed, ret, size);
    }
    trace_vof_claim(virt, size, align, ret);

    return ret;
}

static uint32_t vof_release(Vof *vof, uint64_t virt, uint64_t size)
{
    uint32_t ret = PROM_ERROR;
    int i;
    GArray *claimed = vof->claimed;
    OfClaimed c;

    for (i = 0; i < claimed->len; ++i) {
        c = g_array_index(claimed, OfClaimed, i);
        if (c.start == virt && c.size == size) {
            g_array_remove_index(claimed, i);
            ret = 0;
            break;
        }
    }

    trace_vof_release(virt, size, ret);

    return ret;
}

static void vof_instantiate_rtas(Error **errp)
{
    error_setg(errp, "The firmware should have instantiated RTAS");
}

static uint32_t vof_call_method(MachineState *ms, Vof *vof, uint32_t methodaddr,
                                uint32_t ihandle, uint32_t param1,
                                uint32_t param2, uint32_t param3,
                                uint32_t param4, uint32_t *ret2)
{
    uint32_t ret = PROM_ERROR;
    char method[VOF_MAX_METHODLEN] = "";
    OfInstance *inst;

    if (!ihandle) {
        goto trace_exit;
    }

    inst = (OfInstance *)g_hash_table_lookup(vof->of_instances,
                                             GINT_TO_POINTER(ihandle));
    if (!inst) {
        goto trace_exit;
    }

    if (readstr(methodaddr, method, sizeof(method))) {
        goto trace_exit;
    }

    if (strcmp(inst->path, "/") == 0) {
        if (strcmp(method, "ibm,client-architecture-support") == 0) {
            Object *vmo = object_dynamic_cast(OBJECT(ms), TYPE_VOF_MACHINE_IF);

            if (vmo) {
                VofMachineIfClass *vmc = VOF_MACHINE_GET_CLASS(vmo);

                g_assert(vmc->client_architecture_support);
                ret = (uint32_t)vmc->client_architecture_support(ms, first_cpu,
                                                                 param1);
            }

            *ret2 = 0;
        }
    } else if (strcmp(inst->path, "/rtas") == 0) {
        if (strcmp(method, "instantiate-rtas") == 0) {
            vof_instantiate_rtas(&error_fatal);
            ret = 0;
            *ret2 = param1; /* rtas-base */
        }
    } else {
        trace_vof_error_unknown_method(method);
    }

trace_exit:
    trace_vof_method(ihandle, method, param1, ret, *ret2);

    return ret;
}

static uint32_t vof_call_interpret(uint32_t cmdaddr, uint32_t param1,
                                   uint32_t param2, uint32_t *ret2)
{
    uint32_t ret = PROM_ERROR;
    char cmd[VOF_MAX_FORTHCODE] = "";

    /* No interpret implemented so just call a trace */
    readstr(cmdaddr, cmd, sizeof(cmd));
    trace_vof_interpret(cmd, param1, param2, ret, *ret2);

    return ret;
}

static void vof_quiesce(MachineState *ms, void *fdt, Vof *vof)
{
    Object *vmo = object_dynamic_cast(OBJECT(ms), TYPE_VOF_MACHINE_IF);
    /* After "quiesce", no change is expected to the FDT, pack FDT to ensure */
    int rc = fdt_pack(fdt);

    assert(rc == 0);

    if (vmo) {
        VofMachineIfClass *vmc = VOF_MACHINE_GET_CLASS(vmo);

        if (vmc->quiesce) {
            vmc->quiesce(ms);
        }
    }

    vof_claimed_dump(vof->claimed);
}

static uint32_t vof_client_handle(MachineState *ms, void *fdt, Vof *vof,
                                  const char *service,
                                  uint32_t *args, unsigned nargs,
                                  uint32_t *rets, unsigned nrets)
{
    uint32_t ret = 0;

    /* @nrets includes the value which this function returns */
#define cmpserv(s, a, r) \
    cmpservice(service, nargs, nrets, (s), (a), (r))

    if (cmpserv("finddevice", 1, 1)) {
        ret = vof_finddevice(fdt, args[0]);
    } else if (cmpserv("getprop", 4, 1)) {
        ret = vof_getprop(fdt, args[0], args[1], args[2], args[3]);
    } else if (cmpserv("getproplen", 2, 1)) {
        ret = vof_getproplen(fdt, args[0], args[1]);
    } else if (cmpserv("setprop", 4, 1)) {
        ret = vof_setprop(ms, fdt, vof, args[0], args[1], args[2], args[3]);
    } else if (cmpserv("nextprop", 3, 1)) {
        ret = vof_nextprop(fdt, args[0], args[1], args[2]);
    } else if (cmpserv("peer", 1, 1)) {
        ret = vof_peer(fdt, args[0]);
    } else if (cmpserv("child", 1, 1)) {
        ret = vof_child(fdt, args[0]);
    } else if (cmpserv("parent", 1, 1)) {
        ret = vof_parent(fdt, args[0]);
    } else if (cmpserv("open", 1, 1)) {
        ret = vof_open(fdt, vof, args[0]);
    } else if (cmpserv("close", 1, 0)) {
        vof_close(vof, args[0]);
    } else if (cmpserv("instance-to-package", 1, 1)) {
        ret = vof_instance_to_package(vof, args[0]);
    } else if (cmpserv("package-to-path", 3, 1)) {
        ret = vof_package_to_path(fdt, args[0], args[1], args[2]);
    } else if (cmpserv("instance-to-path", 3, 1)) {
        ret = vof_instance_to_path(fdt, vof, args[0], args[1], args[2]);
    } else if (cmpserv("write", 3, 1)) {
        ret = vof_write(vof, args[0], args[1], args[2]);
    } else if (cmpserv("claim", 3, 1)) {
        uint64_t ret64 = vof_claim(vof, args[0], args[1], args[2]);

        if (ret64 < 0x100000000UL) {
            vof_dt_memory_available(fdt, vof->claimed, vof->claimed_base);
            ret = (uint32_t)ret64;
        } else {
            if (ret64 != -1) {
                vof_release(vof, ret, args[1]);
            }
            ret = PROM_ERROR;
        }
    } else if (cmpserv("release", 2, 0)) {
        ret = vof_release(vof, args[0], args[1]);
        if (ret != PROM_ERROR) {
            vof_dt_memory_available(fdt, vof->claimed, vof->claimed_base);
        }
    } else if (cmpserv("call-method", 0, 0)) {
        ret = vof_call_method(ms, vof, args[0], args[1], args[2], args[3],
                              args[4], args[5], rets);
    } else if (cmpserv("interpret", 0, 0)) {
        ret = vof_call_interpret(args[0], args[1], args[2], rets);
    } else if (cmpserv("milliseconds", 0, 1)) {
        ret = qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL);
    } else if (cmpserv("quiesce", 0, 0)) {
        vof_quiesce(ms, fdt, vof);
    } else if (cmpserv("exit", 0, 0)) {
        error_report("Stopped as the VM requested \"exit\"");
        vm_stop(RUN_STATE_PAUSED);
    } else {
        trace_vof_error_unknown_service(service, nargs, nrets);
        ret = -1;
    }

#undef cmpserv

    return ret;
}

/* Defined as Big Endian */
struct prom_args {
    uint32_t service;
    uint32_t nargs;
    uint32_t nret;
    uint32_t args[10];
} QEMU_PACKED;

int vof_client_call(MachineState *ms, Vof *vof, void *fdt,
                    target_ulong args_real)
{
    struct prom_args args_be;
    uint32_t args[ARRAY_SIZE(args_be.args)];
    uint32_t rets[ARRAY_SIZE(args_be.args)] = { 0 }, ret;
    char service[64];
    unsigned nargs, nret, i;

    if (VOF_MEM_READ(args_real, &args_be, sizeof(args_be)) != MEMTX_OK) {
        return -EINVAL;
    }
    nargs = be32_to_cpu(args_be.nargs);
    if (nargs >= ARRAY_SIZE(args_be.args)) {
        return -EINVAL;
    }

    if (VOF_MEM_READ(be32_to_cpu(args_be.service), service, sizeof(service)) !=
        MEMTX_OK) {
        return -EINVAL;
    }
    if (strnlen(service, sizeof(service)) == sizeof(service)) {
        /* Too long service name */
        return -EINVAL;
    }

    for (i = 0; i < nargs; ++i) {
        args[i] = be32_to_cpu(args_be.args[i]);
    }

    nret = be32_to_cpu(args_be.nret);
    if (nret > ARRAY_SIZE(args_be.args) - nargs) {
        return -EINVAL;
    }
    ret = vof_client_handle(ms, fdt, vof, service, args, nargs, rets, nret);
    if (!nret) {
        return 0;
    }

    /* @nrets includes the value which this function returns */
    args_be.args[nargs] = cpu_to_be32(ret);
    for (i = 1; i < nret; ++i) {
        args_be.args[nargs + i] = cpu_to_be32(rets[i - 1]);
    }

    if (VOF_MEM_WRITE(args_real + offsetof(struct prom_args, args[nargs]),
                      args_be.args + nargs, sizeof(args_be.args[0]) * nret) !=
        MEMTX_OK) {
        return -EINVAL;
    }

    return 0;
}

static void vof_instance_free(gpointer data)
{
    OfInstance *inst = (OfInstance *)data;

    g_free(inst->path);
    g_free(inst);
}

void vof_init(Vof *vof, uint64_t top_addr, Error **errp)
{
    vof_cleanup(vof);

    vof->of_instances = g_hash_table_new_full(g_direct_hash, g_direct_equal,
                                              NULL, vof_instance_free);
    vof->claimed = g_array_new(false, false, sizeof(OfClaimed));

    /* Keep allocations in 32bit as CLI ABI can only return cells==32bit */
    vof->top_addr = MIN(top_addr, 4 * GiB);
    if (vof_claim(vof, 0, vof->fw_size, 0) == -1) {
        error_setg(errp, "Memory for firmware is in use");
    }
}

void vof_cleanup(Vof *vof)
{
    if (vof->claimed) {
        g_array_unref(vof->claimed);
    }
    if (vof->of_instances) {
        g_hash_table_unref(vof->of_instances);
    }
    vof->claimed = NULL;
    vof->of_instances = NULL;
}

void vof_build_dt(void *fdt, Vof *vof)
{
    uint32_t phandle = fdt_get_max_phandle(fdt);
    int offset, proplen = 0;
    const void *prop;

    /* Assign phandles to nodes without predefined phandles (like XICS/XIVE) */
    for (offset = fdt_next_node(fdt, -1, NULL);
         offset >= 0;
         offset = fdt_next_node(fdt, offset, NULL)) {
        prop = fdt_getprop(fdt, offset, "phandle", &proplen);
        if (prop) {
            continue;
        }
        ++phandle;
        _FDT(fdt_setprop_cell(fdt, offset, "phandle", phandle));
    }

    vof_dt_memory_available(fdt, vof->claimed, vof->claimed_base);
}

static const TypeInfo vof_machine_if_info = {
    .name = TYPE_VOF_MACHINE_IF,
    .parent = TYPE_INTERFACE,
    .class_size = sizeof(VofMachineIfClass),
};

static void vof_machine_if_register_types(void)
{
    type_register_static(&vof_machine_if_info);
}
type_init(vof_machine_if_register_types)
