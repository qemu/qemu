#include "vof.h"

struct prom_args {
    uint32_t service;
    uint32_t nargs;
    uint32_t nret;
    uint32_t args[10];
};

typedef unsigned long prom_arg_t;

#define ADDR(x) (uint32_t)(x)

static int prom_handle(struct prom_args *pargs)
{
    void *rtasbase;
    uint32_t rtassize = 0;
    phandle rtas;

    if (strcmp("call-method", (void *)(unsigned long)pargs->service)) {
        return -1;
    }

    if (strcmp("instantiate-rtas", (void *)(unsigned long)pargs->args[0])) {
        return -1;
    }

    rtas = ci_finddevice("/rtas");
    /* rtas-size is set by QEMU depending of FWNMI support */
    ci_getprop(rtas, "rtas-size", &rtassize, sizeof(rtassize));
    if (rtassize < hv_rtas_size) {
        return -1;
    }

    rtasbase = (void *)(unsigned long) pargs->args[2];

    memcpy(rtasbase, hv_rtas, hv_rtas_size);
    pargs->args[pargs->nargs] = 0;
    pargs->args[pargs->nargs + 1] = pargs->args[2];

    return 0;
}

void prom_entry(uint32_t args)
{
    if (prom_handle((void *)(unsigned long) args)) {
        ci_entry(args);
    }
}

static int call_ci(const char *service, int nargs, int nret, ...)
{
    int i;
    struct prom_args args;
    va_list list;

    args.service = ADDR(service);
    args.nargs = nargs;
    args.nret = nret;

    va_start(list, nret);
    for (i = 0; i < nargs; i++) {
        args.args[i] = va_arg(list, prom_arg_t);
    }
    va_end(list);

    for (i = 0; i < nret; i++) {
        args.args[nargs + i] = 0;
    }

    if (ci_entry((uint32_t)(&args)) < 0) {
        return -1;
    }

    return (nret > 0) ? args.args[nargs] : 0;
}

void ci_panic(const char *str)
{
    call_ci("exit", 0, 0);
}

phandle ci_finddevice(const char *path)
{
    return call_ci("finddevice", 1, 1, path);
}

uint32_t ci_getprop(phandle ph, const char *propname, void *prop, int len)
{
    return call_ci("getprop", 4, 1, ph, propname, prop, len);
}
