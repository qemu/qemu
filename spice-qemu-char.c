#include "config-host.h"
#include "trace.h"
#include "ui/qemu-spice.h"
#include "char/char.h"
#include <spice.h>
#include <spice-experimental.h>
#include <spice/protocol.h>

#include "qemu/osdep.h"

typedef struct SpiceCharDriver {
    CharDriverState*      chr;
    SpiceCharDeviceInstance     sin;
    char                  *subtype;
    bool                  active;
    uint8_t               *buffer;
    uint8_t               *datapos;
    ssize_t               bufsize, datalen;
    QLIST_ENTRY(SpiceCharDriver) next;
} SpiceCharDriver;

static QLIST_HEAD(, SpiceCharDriver) spice_chars =
    QLIST_HEAD_INITIALIZER(spice_chars);

static int vmc_write(SpiceCharDeviceInstance *sin, const uint8_t *buf, int len)
{
    SpiceCharDriver *scd = container_of(sin, SpiceCharDriver, sin);
    ssize_t out = 0;
    ssize_t last_out;
    uint8_t* p = (uint8_t*)buf;

    while (len > 0) {
        last_out = MIN(len, qemu_chr_be_can_write(scd->chr));
        if (last_out <= 0) {
            break;
        }
        qemu_chr_be_write(scd->chr, p, last_out);
        out += last_out;
        len -= last_out;
        p += last_out;
    }

    trace_spice_vmc_write(out, len + out);
    return out;
}

static int vmc_read(SpiceCharDeviceInstance *sin, uint8_t *buf, int len)
{
    SpiceCharDriver *scd = container_of(sin, SpiceCharDriver, sin);
    int bytes = MIN(len, scd->datalen);

    if (bytes > 0) {
        memcpy(buf, scd->datapos, bytes);
        scd->datapos += bytes;
        scd->datalen -= bytes;
        assert(scd->datalen >= 0);
        if (scd->datalen == 0) {
            scd->datapos = 0;
        }
    }
    trace_spice_vmc_read(bytes, len);
    return bytes;
}

#if SPICE_SERVER_VERSION >= 0x000c02
static void vmc_event(SpiceCharDeviceInstance *sin, uint8_t event)
{
    SpiceCharDriver *scd = container_of(sin, SpiceCharDriver, sin);
    int chr_event;

    switch (event) {
    case SPICE_PORT_EVENT_BREAK:
        chr_event = CHR_EVENT_BREAK;
        break;
    default:
        return;
    }

    trace_spice_vmc_event(chr_event);
    qemu_chr_be_event(scd->chr, chr_event);
}
#endif

static void vmc_state(SpiceCharDeviceInstance *sin, int connected)
{
    SpiceCharDriver *scd = container_of(sin, SpiceCharDriver, sin);

#if SPICE_SERVER_VERSION < 0x000901
    /*
     * spice-server calls the state callback for the agent channel when the
     * spice client connects / disconnects. Given that not the client but
     * the server is doing the parsing of the messages this is wrong as the
     * server is still listening. Worse, this causes the parser in the server
     * to go out of sync, so we ignore state calls for subtype vdagent
     * spicevmc chardevs. For the full story see:
     * http://lists.freedesktop.org/archives/spice-devel/2011-July/004837.html
     */
    if (strcmp(sin->subtype, "vdagent") == 0) {
        return;
    }
#endif

    if ((scd->chr->be_open && connected) ||
        (!scd->chr->be_open && !connected)) {
        return;
    }

    qemu_chr_be_event(scd->chr,
                      connected ? CHR_EVENT_OPENED : CHR_EVENT_CLOSED);
}

static SpiceCharDeviceInterface vmc_interface = {
    .base.type          = SPICE_INTERFACE_CHAR_DEVICE,
    .base.description   = "spice virtual channel char device",
    .base.major_version = SPICE_INTERFACE_CHAR_DEVICE_MAJOR,
    .base.minor_version = SPICE_INTERFACE_CHAR_DEVICE_MINOR,
    .state              = vmc_state,
    .write              = vmc_write,
    .read               = vmc_read,
#if SPICE_SERVER_VERSION >= 0x000c02
    .event              = vmc_event,
#endif
};


static void vmc_register_interface(SpiceCharDriver *scd)
{
    if (scd->active) {
        return;
    }
    scd->sin.base.sif = &vmc_interface.base;
    qemu_spice_add_interface(&scd->sin.base);
    scd->active = true;
    trace_spice_vmc_register_interface(scd);
}

static void vmc_unregister_interface(SpiceCharDriver *scd)
{
    if (!scd->active) {
        return;
    }
    spice_server_remove_interface(&scd->sin.base);
    scd->active = false;
    trace_spice_vmc_unregister_interface(scd);
}


static int spice_chr_write(CharDriverState *chr, const uint8_t *buf, int len)
{
    SpiceCharDriver *s = chr->opaque;

    assert(s->datalen == 0);
    if (s->bufsize < len) {
        s->bufsize = len;
        s->buffer = g_realloc(s->buffer, s->bufsize);
    }
    memcpy(s->buffer, buf, len);
    s->datapos = s->buffer;
    s->datalen = len;
    spice_server_char_device_wakeup(&s->sin);
    return len;
}

static void spice_chr_close(struct CharDriverState *chr)
{
    SpiceCharDriver *s = chr->opaque;

    vmc_unregister_interface(s);
    QLIST_REMOVE(s, next);

    g_free((char *)s->sin.subtype);
#if SPICE_SERVER_VERSION >= 0x000c02
    g_free((char *)s->sin.portname);
#endif
    g_free(s);
}

static void spice_chr_set_fe_open(struct CharDriverState *chr, int fe_open)
{
    SpiceCharDriver *s = chr->opaque;
    if (fe_open) {
        vmc_register_interface(s);
    } else {
        vmc_unregister_interface(s);
    }
}

static void print_allowed_subtypes(void)
{
    const char** psubtype;
    int i;

    fprintf(stderr, "allowed names: ");
    for(i=0, psubtype = spice_server_char_device_recognized_subtypes();
        *psubtype != NULL; ++psubtype, ++i) {
        if (i == 0) {
            fprintf(stderr, "%s", *psubtype);
        } else {
            fprintf(stderr, ", %s", *psubtype);
        }
    }
    fprintf(stderr, "\n");
}

static CharDriverState *chr_open(const char *subtype)
{
    CharDriverState *chr;
    SpiceCharDriver *s;

    chr = g_malloc0(sizeof(CharDriverState));
    s = g_malloc0(sizeof(SpiceCharDriver));
    s->chr = chr;
    s->active = false;
    s->sin.subtype = g_strdup(subtype);
    chr->opaque = s;
    chr->chr_write = spice_chr_write;
    chr->chr_close = spice_chr_close;
    chr->chr_set_fe_open = spice_chr_set_fe_open;

    QLIST_INSERT_HEAD(&spice_chars, s, next);

    return chr;
}

CharDriverState *qemu_chr_open_spice_vmc(const char *type)
{
    CharDriverState *chr;
    const char **psubtype = spice_server_char_device_recognized_subtypes();

    if (type == NULL) {
        fprintf(stderr, "spice-qemu-char: missing name parameter\n");
        print_allowed_subtypes();
        return NULL;
    }
    for (; *psubtype != NULL; ++psubtype) {
        if (strcmp(type, *psubtype) == 0) {
            break;
        }
    }
    if (*psubtype == NULL) {
        fprintf(stderr, "spice-qemu-char: unsupported type: %s\n", type);
        print_allowed_subtypes();
        return NULL;
    }

    chr = chr_open(type);

#if SPICE_SERVER_VERSION < 0x000901
    /* See comment in vmc_state() */
    if (strcmp(type, "vdagent") == 0) {
        qemu_chr_generic_open(chr);
    }
#endif

    return chr;
}

#if SPICE_SERVER_VERSION >= 0x000c02
CharDriverState *qemu_chr_open_spice_port(const char *name)
{
    CharDriverState *chr;
    SpiceCharDriver *s;

    if (name == NULL) {
        fprintf(stderr, "spice-qemu-char: missing name parameter\n");
        return NULL;
    }

    chr = chr_open("port");
    s = chr->opaque;
    s->sin.portname = g_strdup(name);

    return chr;
}

void qemu_spice_register_ports(void)
{
    SpiceCharDriver *s;

    QLIST_FOREACH(s, &spice_chars, next) {
        if (s->sin.portname == NULL) {
            continue;
        }
        vmc_register_interface(s);
    }
}
#endif

static void qemu_chr_parse_spice_vmc(QemuOpts *opts, ChardevBackend *backend,
                                     Error **errp)
{
    const char *name = qemu_opt_get(opts, "name");

    if (name == NULL) {
        error_setg(errp, "chardev: spice channel: no name given");
        return;
    }
    backend->spicevmc = g_new0(ChardevSpiceChannel, 1);
    backend->spicevmc->type = g_strdup(name);
}

static void qemu_chr_parse_spice_port(QemuOpts *opts, ChardevBackend *backend,
                                      Error **errp)
{
    const char *name = qemu_opt_get(opts, "name");

    if (name == NULL) {
        error_setg(errp, "chardev: spice port: no name given");
        return;
    }
    backend->spiceport = g_new0(ChardevSpicePort, 1);
    backend->spiceport->fqdn = g_strdup(name);
}

static void register_types(void)
{
    register_char_driver_qapi("spicevmc", CHARDEV_BACKEND_KIND_SPICEVMC,
                              qemu_chr_parse_spice_vmc);
    register_char_driver_qapi("spiceport", CHARDEV_BACKEND_KIND_SPICEPORT,
                              qemu_chr_parse_spice_port);
}

type_init(register_types);
