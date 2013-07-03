/*
 * Abstraction layer for defining and using TLS variables
 *
 * Copyright (c) 2011 Red Hat, Inc
 * Copyright (c) 2011 Linaro Limited
 *
 * Authors:
 *  Paolo Bonzini <pbonzini@redhat.com>
 *  Peter Maydell <peter.maydell@linaro.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef QEMU_TLS_H
#define QEMU_TLS_H

/* Per-thread variables. Note that we only have implementations
 * which are really thread-local on Linux; the dummy implementations
 * define plain global variables.
 *
 * This means that for the moment use should be restricted to
 * per-VCPU variables, which are OK because:
 *  - the only -user mode supporting multiple VCPU threads is linux-user
 *  - TCG system mode is single-threaded regarding VCPUs
 *  - KVM system mode is multi-threaded but limited to Linux
 *
 * TODO: proper implementations via Win32 .tls sections and
 * POSIX pthread_getspecific.
 */
#ifdef __linux__
#define DECLARE_TLS(type, x) extern DEFINE_TLS(type, x)
#define DEFINE_TLS(type, x)  __thread __typeof__(type) tls__##x
#define tls_var(x)           tls__##x
#else
/* Dummy implementations which define plain global variables */
#define DECLARE_TLS(type, x) extern DEFINE_TLS(type, x)
#define DEFINE_TLS(type, x)  __typeof__(type) tls__##x
#define tls_var(x)           tls__##x
#endif

#endif
