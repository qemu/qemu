/*
 * Global variables that (mostly) should not exist
 *
 * Copyright (c) 2003-2020 QEMU contributors
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
#include "exec/cpu-common.h"
#include "hw/display/vga.h"
#include "hw/loader.h"
#include "hw/xen/xen.h"
#include "net/net.h"
#include "sysemu/cpus.h"
#include "sysemu/sysemu.h"

enum vga_retrace_method vga_retrace_method = VGA_RETRACE_DUMB;
int display_opengl;
const char* keyboard_layout;
bool enable_mlock;
bool enable_cpu_pm;
int nb_nics;
NICInfo nd_table[MAX_NICS];
int autostart = 1;
int vga_interface_type = VGA_NONE;
bool vga_interface_created;
Chardev *parallel_hds[MAX_PARALLEL_PORTS];
int win2k_install_hack;
int singlestep;
int fd_bootchk = 1;
int graphic_rotate;
QEMUOptionRom option_rom[MAX_OPTION_ROMS];
int nb_option_roms;
int old_param;
const char *qemu_name;
unsigned int nb_prom_envs;
const char *prom_envs[MAX_PROM_ENVS];
uint8_t *boot_splash_filedata;
int only_migratable; /* turn it off unless user states otherwise */
int icount_align_option;

/* The bytes in qemu_uuid are in the order specified by RFC4122, _not_ in the
 * little-endian "wire format" described in the SMBIOS 2.6 specification.
 */
QemuUUID qemu_uuid;
bool qemu_uuid_set;

uint32_t xen_domid;
enum xen_mode xen_mode = XEN_EMULATE;
bool xen_domid_restrict;
