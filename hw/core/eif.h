/*
 * EIF (Enclave Image Format) related helpers
 *
 * Copyright (c) 2024 Dorjoy Chowdhury <dorjoychy111@gmail.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * (at your option) any later version.  See the COPYING file in the
 * top-level directory.
 */

#ifndef HW_CORE_EIF_H
#define HW_CORE_EIF_H

bool read_eif_file(const char *eif_path, const char *machine_initrd,
                   char **kernel_path, char **initrd_path,
                   char **kernel_cmdline, uint8_t *image_sha384,
                   uint8_t *bootstrap_sha384, uint8_t *app_sha384,
                   uint8_t *fingerprint_sha384, bool *signature_found,
                   Error **errp);

#endif

