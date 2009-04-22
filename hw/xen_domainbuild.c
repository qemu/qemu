#include <signal.h>
#include "xen_backend.h"
#include "xen_domainbuild.h"
#include "sysemu.h"
#include "qemu-timer.h"

#include <xenguest.h>

static int xenstore_domain_mkdir(char *path)
{
    struct xs_permissions perms_ro[] = {{
            .id    = 0, /* set owner: dom0 */
        },{
            .id    = xen_domid,
            .perms = XS_PERM_READ,
        }};
    struct xs_permissions perms_rw[] = {{
            .id    = 0, /* set owner: dom0 */
        },{
            .id    = xen_domid,
            .perms = XS_PERM_READ | XS_PERM_WRITE,
        }};
    const char *writable[] = { "device", "control", "error", NULL };
    char subpath[256];
    int i;

    if (!xs_mkdir(xenstore, 0, path)) {
        fprintf(stderr, "%s: xs_mkdir %s: failed\n", __FUNCTION__, path);
	return -1;
    }
    if (!xs_set_permissions(xenstore, 0, path, perms_ro, 2)) {
        fprintf(stderr, "%s: xs_set_permissions failed\n", __FUNCTION__);
	return -1;
    }

    for (i = 0; writable[i]; i++) {
        snprintf(subpath, sizeof(subpath), "%s/%s", path, writable[i]);
        if (!xs_mkdir(xenstore, 0, subpath)) {
            fprintf(stderr, "%s: xs_mkdir %s: failed\n", __FUNCTION__, subpath);
            return -1;
        }
        if (!xs_set_permissions(xenstore, 0, subpath, perms_rw, 2)) {
            fprintf(stderr, "%s: xs_set_permissions failed\n", __FUNCTION__);
            return -1;
        }
    }
    return 0;
}

int xenstore_domain_init1(const char *kernel, const char *ramdisk,
                          const char *cmdline)
{
    char *dom, uuid_string[42], vm[256], path[256];
    int i;

    snprintf(uuid_string, sizeof(uuid_string), UUID_FMT,
             qemu_uuid[0], qemu_uuid[1], qemu_uuid[2], qemu_uuid[3],
             qemu_uuid[4], qemu_uuid[5], qemu_uuid[6], qemu_uuid[7],
             qemu_uuid[8], qemu_uuid[9], qemu_uuid[10], qemu_uuid[11],
             qemu_uuid[12], qemu_uuid[13], qemu_uuid[14], qemu_uuid[15]);
    dom = xs_get_domain_path(xenstore, xen_domid);
    snprintf(vm,  sizeof(vm),  "/vm/%s", uuid_string);

    xenstore_domain_mkdir(dom);

    xenstore_write_str(vm, "image/ostype",  "linux");
    if (kernel)
        xenstore_write_str(vm, "image/kernel",  kernel);
    if (ramdisk)
        xenstore_write_str(vm, "image/ramdisk", ramdisk);
    if (cmdline)
        xenstore_write_str(vm, "image/cmdline", cmdline);

    /* name + id */
    xenstore_write_str(vm,  "name",   qemu_name ? qemu_name : "no-name");
    xenstore_write_str(vm,  "uuid",   uuid_string);
    xenstore_write_str(dom, "name",   qemu_name ? qemu_name : "no-name");
    xenstore_write_int(dom, "domid",  xen_domid);
    xenstore_write_str(dom, "vm",     vm);

    /* memory */
    xenstore_write_int(dom, "memory/target", ram_size >> 10);  // kB
    xenstore_write_int(vm, "memory",         ram_size >> 20);  // MB
    xenstore_write_int(vm, "maxmem",         ram_size >> 20);  // MB

    /* cpus */
    for (i = 0; i < smp_cpus; i++) {
	snprintf(path, sizeof(path), "cpu/%d/availability",i);
	xenstore_write_str(dom, path, "online");
    }
    xenstore_write_int(vm, "vcpu_avail",  smp_cpus);
    xenstore_write_int(vm, "vcpus",       smp_cpus);

    /* vnc password */
    xenstore_write_str(vm, "vncpassword", "" /* FIXME */);

    free(dom);
    return 0;
}

int xenstore_domain_init2(int xenstore_port, int xenstore_mfn,
                          int console_port, int console_mfn)
{
    char *dom;

    dom = xs_get_domain_path(xenstore, xen_domid);

    /* signal new domain */
    xs_introduce_domain(xenstore,
                        xen_domid,
                        xenstore_mfn,
                        xenstore_port);

    /* xenstore */
    xenstore_write_int(dom, "store/ring-ref",   xenstore_mfn);
    xenstore_write_int(dom, "store/port",       xenstore_port);

    /* console */
    xenstore_write_str(dom, "console/type",     "ioemu");
    xenstore_write_int(dom, "console/limit",    128 * 1024);
    xenstore_write_int(dom, "console/ring-ref", console_mfn);
    xenstore_write_int(dom, "console/port",     console_port);
    xen_config_dev_console(0);

    free(dom);
    return 0;
}

/* ------------------------------------------------------------- */

static QEMUTimer *xen_poll;

/* check domain state once per second */
static void xen_domain_poll(void *opaque)
{
    struct xc_dominfo info;
    int rc;

    rc = xc_domain_getinfo(xen_xc, xen_domid, 1, &info);
    if ((1 != rc) || (info.domid != xen_domid)) {
        qemu_log("xen: domain %d is gone\n", xen_domid);
        goto quit;
    }
    if (info.dying) {
        qemu_log("xen: domain %d is dying (%s%s)\n", xen_domid,
                 info.crashed  ? "crashed"  : "",
                 info.shutdown ? "shutdown" : "");
        goto quit;
    }

    qemu_mod_timer(xen_poll, qemu_get_clock(rt_clock) + 1000);
    return;

quit:
    qemu_system_shutdown_request();
    return;
}

static void xen_domain_watcher(void)
{
    int qemu_running = 1;
    int fd[2], i, n, rc;
    char byte;

    pipe(fd);
    if (fork() != 0)
        return; /* not child */

    /* close all file handles, except stdio/out/err,
     * our watch pipe and the xen interface handle */
    n = getdtablesize();
    for (i = 3; i < n; i++) {
        if (i == fd[0])
            continue;
        if (i == xen_xc)
            continue;
        close(i);
    }

    /* ignore term signals */
    signal(SIGINT,  SIG_IGN);
    signal(SIGTERM, SIG_IGN);

    /* wait for qemu exiting */
    while (qemu_running) {
        rc = read(fd[0], &byte, 1);
        switch (rc) {
        case -1:
            if (EINTR == errno)
                continue;
            qemu_log("%s: Huh? read error: %s\n", __FUNCTION__, strerror(errno));
            qemu_running = 0;
            break;
        case 0:
            /* EOF -> qemu exited */
            qemu_running = 0;
            break;
        default:
            qemu_log("%s: Huh? data on the watch pipe?\n", __FUNCTION__);
            break;
        }
    }

    /* cleanup */
    qemu_log("%s: destroy domain %d\n", __FUNCTION__, xen_domid);
    xc_domain_destroy(xen_xc, xen_domid);
    _exit(0);
}

/* normal cleanup */
static void xen_domain_cleanup(void)
{
    char *dom;

    dom = xs_get_domain_path(xenstore, xen_domid);
    if (dom) {
        xs_rm(xenstore, 0, dom);
        free(dom);
    }
    xs_release_domain(xenstore, xen_domid);
}

int xen_domain_build_pv(const char *kernel, const char *ramdisk,
                        const char *cmdline)
{
    uint32_t ssidref = 0;
    uint32_t flags = 0;
    xen_domain_handle_t uuid;
    unsigned int xenstore_port = 0, console_port = 0;
    unsigned long xenstore_mfn = 0, console_mfn = 0;
    int rc;

    memcpy(uuid, qemu_uuid, sizeof(uuid));
    rc = xc_domain_create(xen_xc, ssidref, uuid, flags, &xen_domid);
    if (rc < 0) {
        fprintf(stderr, "xen: xc_domain_create() failed\n");
        goto err;
    }
    qemu_log("xen: created domain %d\n", xen_domid);
    atexit(xen_domain_cleanup);
    xen_domain_watcher();

    xenstore_domain_init1(kernel, ramdisk, cmdline);

    rc = xc_domain_max_vcpus(xen_xc, xen_domid, smp_cpus);
    if (rc < 0) {
        fprintf(stderr, "xen: xc_domain_max_vcpus() failed\n");
        goto err;
    }

#if 0
    rc = xc_domain_setcpuweight(xen_xc, xen_domid, 256);
    if (rc < 0) {
        fprintf(stderr, "xen: xc_domain_setcpuweight() failed\n");
        goto err;
    }
#endif

    rc = xc_domain_setmaxmem(xen_xc, xen_domid, ram_size >> 10);
    if (rc < 0) {
        fprintf(stderr, "xen: xc_domain_setmaxmem() failed\n");
        goto err;
    }

    xenstore_port = xc_evtchn_alloc_unbound(xen_xc, xen_domid, 0);
    console_port = xc_evtchn_alloc_unbound(xen_xc, xen_domid, 0);

    rc = xc_linux_build(xen_xc, xen_domid, ram_size >> 20,
                        kernel, ramdisk, cmdline,
                        0, flags,
                        xenstore_port, &xenstore_mfn,
                        console_port, &console_mfn);
    if (rc < 0) {
        fprintf(stderr, "xen: xc_linux_build() failed\n");
        goto err;
    }

    xenstore_domain_init2(xenstore_port, xenstore_mfn,
                          console_port, console_mfn);

    qemu_log("xen: unpausing domain %d\n", xen_domid);
    rc = xc_domain_unpause(xen_xc, xen_domid);
    if (rc < 0) {
        fprintf(stderr, "xen: xc_domain_unpause() failed\n");
        goto err;
    }

    xen_poll = qemu_new_timer(rt_clock, xen_domain_poll, NULL);
    qemu_mod_timer(xen_poll, qemu_get_clock(rt_clock) + 1000);
    return 0;

err:
    return -1;
}
