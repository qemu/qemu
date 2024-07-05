/*
 * QEMU CXL Mailbox
 *
 * This work is licensed under the terms of the GNU GPL, version 2. See the
 * COPYING file in the top-level directory.
 */

#ifndef CXL_MAILBOX_H
#define CXL_MAILBOX_H

#define CXL_MBOX_IMMEDIATE_CONFIG_CHANGE (1 << 1)
#define CXL_MBOX_IMMEDIATE_DATA_CHANGE (1 << 2)
#define CXL_MBOX_IMMEDIATE_POLICY_CHANGE (1 << 3)
#define CXL_MBOX_IMMEDIATE_LOG_CHANGE (1 << 4)
#define CXL_MBOX_SECURITY_STATE_CHANGE (1 << 5)
#define CXL_MBOX_BACKGROUND_OPERATION (1 << 6)

#endif
