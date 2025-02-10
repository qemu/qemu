/*
 * QAPI util functions
 *
 * Authors:
 *  Hu Tao       <hutao@cn.fujitsu.com>
 *  Peter Lieven <pl@kamp.de>
 * 
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "qapi/compat-policy.h"
#include "qapi/error.h"
#include "qemu/ctype.h"
#include "qapi/qmp/qerror.h"

CompatPolicy compat_policy;

static bool compat_policy_input_ok1(const char *adjective,
                                    CompatPolicyInput policy,
                                    ErrorClass error_class,
                                    const char *kind, const char *name,
                                    Error **errp)
{
    switch (policy) {
    case COMPAT_POLICY_INPUT_ACCEPT:
        return true;
    case COMPAT_POLICY_INPUT_REJECT:
        error_set(errp, error_class, "%s %s %s disabled by policy",
                  adjective, kind, name);
        return false;
    case COMPAT_POLICY_INPUT_CRASH:
    default:
        abort();
    }
}

bool compat_policy_input_ok(uint64_t features,
                            const CompatPolicy *policy,
                            ErrorClass error_class,
                            const char *kind, const char *name,
                            Error **errp)
{
    if ((features & 1u << QAPI_DEPRECATED)
        && !compat_policy_input_ok1("Deprecated",
                                    policy->deprecated_input,
                                    error_class, kind, name, errp)) {
        return false;
    }
    if ((features & (1u << QAPI_UNSTABLE))
        && !compat_policy_input_ok1("Unstable",
                                    policy->unstable_input,
                                    error_class, kind, name, errp)) {
        return false;
    }
    return true;
}

const char *qapi_enum_lookup(const QEnumLookup *lookup, int val)
{
    assert(val >= 0 && val < lookup->size);

    return lookup->array[val];
}

int qapi_enum_parse(const QEnumLookup *lookup, const char *buf,
                    int def, Error **errp)
{
    int i;

    if (!buf) {
        return def;
    }

    for (i = 0; i < lookup->size; i++) {
        if (!strcmp(buf, lookup->array[i])) {
            return i;
        }
    }

    error_setg(errp, "invalid parameter value: %s", buf);
    return def;
}

bool qapi_bool_parse(const char *name, const char *value, bool *obj, Error **errp)
{
    if (g_str_equal(value, "on") ||
        g_str_equal(value, "yes") ||
        g_str_equal(value, "true") ||
        g_str_equal(value, "y")) {
        *obj = true;
        return true;
    }
    if (g_str_equal(value, "off") ||
        g_str_equal(value, "no") ||
        g_str_equal(value, "false") ||
        g_str_equal(value, "n")) {
        *obj = false;
        return true;
    }

    error_setg(errp, QERR_INVALID_PARAMETER_VALUE, name,
               "'on' or 'off'");
    return false;
}

/*
 * Parse a valid QAPI name from @str.
 * A valid name consists of letters, digits, hyphen and underscore.
 * It may be prefixed by __RFQDN_ (downstream extension), where RFQDN
 * may contain only letters, digits, hyphen and period.
 * The special exception for enumeration names is not implemented.
 * See docs/devel/qapi-code-gen.rst for more on QAPI naming rules.
 * Keep this consistent with scripts/qapi-gen.py!
 * If @complete, the parse fails unless it consumes @str completely.
 * Return its length on success, -1 on failure.
 */
int parse_qapi_name(const char *str, bool complete)
{
    const char *p = str;

    if (*p == '_') {            /* Downstream __RFQDN_ */
        p++;
        if (*p != '_') {
            return -1;
        }
        while (*++p) {
            if (!qemu_isalnum(*p) && *p != '-' && *p != '.') {
                break;
            }
        }

        if (*p != '_') {
            return -1;
        }
        p++;
    }

    if (!qemu_isalpha(*p)) {
        return -1;
    }
    while (*++p) {
        if (!qemu_isalnum(*p) && *p != '-' && *p != '_') {
            break;
        }
    }

    if (complete && *p) {
        return -1;
    }
    return p - str;
}
