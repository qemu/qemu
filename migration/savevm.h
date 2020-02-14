/*
 * QEMU save vm functions
 *
 * Copyright (c) 2003-2008 Fabrice Bellard
 * Copyright (c) 2009-2017 Red Hat Inc
 *
 * Authors:
 *  Juan Quintela <quintela@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef MIGRATION_SAVEVM_H
#define MIGRATION_SAVEVM_H

#define QEMU_VM_FILE_MAGIC           0x5145564d
#define QEMU_VM_FILE_VERSION_COMPAT  0x00000002
#define QEMU_VM_FILE_VERSION         0x00000003

#define QEMU_VM_EOF                  0x00
#define QEMU_VM_SECTION_START        0x01
#define QEMU_VM_SECTION_PART         0x02
#define QEMU_VM_SECTION_END          0x03
#define QEMU_VM_SECTION_FULL         0x04
#define QEMU_VM_SUBSECTION           0x05
#define QEMU_VM_VMDESCRIPTION        0x06
#define QEMU_VM_CONFIGURATION        0x07
#define QEMU_VM_COMMAND              0x08
#define QEMU_VM_SECTION_FOOTER       0x7e

bool qemu_savevm_state_blocked(Error **errp);
void qemu_savevm_state_setup(QEMUFile *f);
bool qemu_savevm_state_guest_unplug_pending(void);
int qemu_savevm_state_resume_prepare(MigrationState *s);
void qemu_savevm_state_header(QEMUFile *f);
int qemu_savevm_state_iterate(QEMUFile *f, bool postcopy);
void qemu_savevm_state_cleanup(void);
void qemu_savevm_state_complete_postcopy(QEMUFile *f);
int qemu_savevm_state_complete_precopy(QEMUFile *f, bool iterable_only,
                                       bool inactivate_disks);
void qemu_savevm_state_pending(QEMUFile *f, uint64_t max_size,
                               uint64_t *res_precopy_only,
                               uint64_t *res_compatible,
                               uint64_t *res_postcopy_only);
void qemu_savevm_send_ping(QEMUFile *f, uint32_t value);
void qemu_savevm_send_open_return_path(QEMUFile *f);
int qemu_savevm_send_packaged(QEMUFile *f, const uint8_t *buf, size_t len);
void qemu_savevm_send_postcopy_advise(QEMUFile *f);
void qemu_savevm_send_postcopy_listen(QEMUFile *f);
void qemu_savevm_send_postcopy_run(QEMUFile *f);
void qemu_savevm_send_postcopy_resume(QEMUFile *f);
void qemu_savevm_send_recv_bitmap(QEMUFile *f, char *block_name);

void qemu_savevm_send_postcopy_ram_discard(QEMUFile *f, const char *name,
                                           uint16_t len,
                                           uint64_t *start_list,
                                           uint64_t *length_list);
void qemu_savevm_send_colo_enable(QEMUFile *f);
void qemu_savevm_live_state(QEMUFile *f);
int qemu_save_device_state(QEMUFile *f);

int qemu_loadvm_state(QEMUFile *f);
void qemu_loadvm_state_cleanup(void);
int qemu_loadvm_state_main(QEMUFile *f, MigrationIncomingState *mis);
int qemu_load_device_state(QEMUFile *f);

#endif
