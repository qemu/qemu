/*
 * Domain search option for DHCP (RFC 3397)
 *
 * Copyright (c) 2012 Klaus Stengel
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
#include "slirp.h"

static const uint8_t RFC3397_OPT_DOMAIN_SEARCH = 119;
static const uint8_t MAX_OPT_LEN = 255;
static const uint8_t OPT_HEADER_LEN = 2;
static const uint8_t REFERENCE_LEN = 2;

struct compact_domain;

typedef struct compact_domain {
    struct compact_domain *self;
    struct compact_domain *refdom;
    uint8_t *labels;
    size_t len;
    size_t common_octets;
} CompactDomain;

static size_t
domain_suffix_diffoff(const CompactDomain *a, const CompactDomain *b)
{
    size_t la = a->len, lb = b->len;
    uint8_t *da = a->labels + la, *db = b->labels + lb;
    size_t i, lm = (la < lb) ? la : lb;

    for (i = 0; i < lm; i++) {
        da--; db--;
        if (*da != *db) {
            break;
        }
    }
    return i;
}

static int domain_suffix_ord(const void *cva, const void *cvb)
{
    const CompactDomain *a = cva, *b = cvb;
    size_t la = a->len, lb = b->len;
    size_t doff = domain_suffix_diffoff(a, b);
    uint8_t ca = a->labels[la - doff];
    uint8_t cb = b->labels[lb - doff];

    if (ca < cb) {
        return -1;
    }
    if (ca > cb) {
        return 1;
    }
    if (la < lb) {
        return -1;
    }
    if (la > lb) {
        return 1;
    }
    return 0;
}

static size_t domain_common_label(CompactDomain *a, CompactDomain *b)
{
    size_t res, doff = domain_suffix_diffoff(a, b);
    uint8_t *first_eq_pos = a->labels + (a->len - doff);
    uint8_t *label = a->labels;

    while (*label && label < first_eq_pos) {
        label += *label + 1;
    }
    res = a->len - (label - a->labels);
    /* only report if it can help to reduce the packet size */
    return (res > REFERENCE_LEN) ? res : 0;
}

static void domain_fixup_order(CompactDomain *cd, size_t n)
{
    size_t i;

    for (i = 0; i < n; i++) {
        CompactDomain *cur = cd + i, *next = cd[i].self;

        while (!cur->common_octets) {
            CompactDomain *tmp = next->self; /* backup target value */

            next->self = cur;
            cur->common_octets++;

            cur = next;
            next = tmp;
        }
    }
}

static void domain_mklabels(CompactDomain *cd, const char *input)
{
    uint8_t *len_marker = cd->labels;
    uint8_t *output = len_marker; /* pre-incremented */
    const char *in = input;
    char cur_chr;
    size_t len = 0;

    if (cd->len == 0) {
        goto fail;
    }
    cd->len++;

    do {
        cur_chr = *in++;
        if (cur_chr == '.' || cur_chr == '\0') {
            len = output - len_marker;
            if ((len == 0 && cur_chr == '.') || len >= 64) {
                goto fail;
            }
            *len_marker = len;

            output++;
            len_marker = output;
        } else {
            output++;
            *output = cur_chr;
        }
    } while (cur_chr != '\0');

    /* ensure proper zero-termination */
    if (len != 0) {
        *len_marker = 0;
        cd->len++;
    }
    return;

fail:
    g_warning("failed to parse domain name '%s'\n", input);
    cd->len = 0;
}

static void
domain_mkxrefs(CompactDomain *doms, CompactDomain *last, size_t depth)
{
    CompactDomain *i = doms, *target = doms;

    do {
        if (i->labels < target->labels) {
            target = i;
        }
    } while (i++ != last);

    for (i = doms; i != last; i++) {
        CompactDomain *group_last;
        size_t next_depth;

        if (i->common_octets == depth) {
            continue;
        }

        next_depth = -1;
        for (group_last = i; group_last != last; group_last++) {
            size_t co = group_last->common_octets;
            if (co <= depth) {
                break;
            }
            if (co < next_depth) {
                next_depth = co;
            }
        }
        domain_mkxrefs(i, group_last, next_depth);

        i = group_last;
        if (i == last) {
            break;
        }
    }

    if (depth == 0) {
        return;
    }

    i = doms;
    do {
        if (i != target && i->refdom == NULL) {
            i->refdom = target;
            i->common_octets = depth;
        }
    } while (i++ != last);
}

static size_t domain_compactify(CompactDomain *domains, size_t n)
{
    uint8_t *start = domains->self->labels, *outptr = start;
    size_t i;

    for (i = 0; i < n; i++) {
        CompactDomain *cd = domains[i].self;
        CompactDomain *rd = cd->refdom;

        if (rd != NULL) {
            size_t moff = (rd->labels - start)
                    + (rd->len - cd->common_octets);
            if (moff < 0x3FFFu) {
                cd->len -= cd->common_octets - 2;
                cd->labels[cd->len - 1] = moff & 0xFFu;
                cd->labels[cd->len - 2] = 0xC0u | (moff >> 8);
            }
        }

        if (cd->labels != outptr) {
            memmove(outptr, cd->labels, cd->len);
            cd->labels = outptr;
        }
        outptr += cd->len;
    }
    return outptr - start;
}

int translate_dnssearch(Slirp *s, const char **names)
{
    size_t blocks, bsrc_start, bsrc_end, bdst_start;
    size_t i, num_domains, memreq = 0;
    uint8_t *result = NULL, *outptr;
    CompactDomain *domains = NULL;
    const char **nameptr = names;

    while (*nameptr != NULL) {
        nameptr++;
    }

    num_domains = nameptr - names;
    if (num_domains == 0) {
        return -2;
    }

    domains = g_malloc(num_domains * sizeof(*domains));

    for (i = 0; i < num_domains; i++) {
        size_t nlen = strlen(names[i]);
        memreq += nlen + 2; /* 1 zero octet + 1 label length octet */
        domains[i].self = domains + i;
        domains[i].len = nlen;
        domains[i].common_octets = 0;
        domains[i].refdom = NULL;
    }

    /* reserve extra 2 header bytes for each 255 bytes of output */
    memreq += DIV_ROUND_UP(memreq, MAX_OPT_LEN) * OPT_HEADER_LEN;
    result = g_malloc(memreq * sizeof(*result));

    outptr = result;
    for (i = 0; i < num_domains; i++) {
        domains[i].labels = outptr;
        domain_mklabels(domains + i, names[i]);
        outptr += domains[i].len;
    }

    if (outptr == result) {
        g_free(domains);
        g_free(result);
        return -1;
    }

    qsort(domains, num_domains, sizeof(*domains), domain_suffix_ord);
    domain_fixup_order(domains, num_domains);

    for (i = 1; i < num_domains; i++) {
        size_t cl = domain_common_label(domains + i - 1, domains + i);
        domains[i - 1].common_octets = cl;
    }

    domain_mkxrefs(domains, domains + num_domains - 1, 0);
    memreq = domain_compactify(domains, num_domains);

    blocks = DIV_ROUND_UP(memreq, MAX_OPT_LEN);
    bsrc_end = memreq;
    bsrc_start = (blocks - 1) * MAX_OPT_LEN;
    bdst_start = bsrc_start + blocks * OPT_HEADER_LEN;
    memreq += blocks * OPT_HEADER_LEN;

    while (blocks--) {
        size_t len = bsrc_end - bsrc_start;
        memmove(result + bdst_start, result + bsrc_start, len);
        result[bdst_start - 2] = RFC3397_OPT_DOMAIN_SEARCH;
        result[bdst_start - 1] = len;
        bsrc_end = bsrc_start;
        bsrc_start -= MAX_OPT_LEN;
        bdst_start -= MAX_OPT_LEN + OPT_HEADER_LEN;
    }

    g_free(domains);
    s->vdnssearch = result;
    s->vdnssearch_len = memreq;
    return 0;
}
