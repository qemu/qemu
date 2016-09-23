#ifndef HW_SPAPR_RTAS_H
#define HW_SPAPR_RTAS_H
/*
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

uint64_t qtest_rtas_call(char *cmd, uint32_t nargs, uint64_t args,
                         uint32_t nret, uint64_t rets);
#endif /* HW_SPAPR_RTAS_H */
