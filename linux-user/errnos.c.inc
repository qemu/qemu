/*
 * This list is the union of errno values overridden in asm-<arch>/errno.h
 * minus the errnos that are not actually generic to all archs.
 *
 * Please keep this list sorted alphabetically.
 *
 *  Copyright (c) 2003 Fabrice Bellard
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

E(EADDRINUSE)
E(EADDRNOTAVAIL)
E(EADV)
E(EAFNOSUPPORT)
E(EAGAIN)
E(EALREADY)
E(EBADE)
E(EBADFD)
E(EBADMSG)
E(EBADR)
E(EBADRQC)
E(EBADSLT)
E(EBFONT)
E(ECANCELED)
E(ECHRNG)
E(ECOMM)
E(ECONNABORTED)
E(ECONNREFUSED)
E(ECONNRESET)
E(EDEADLK)
E(EDESTADDRREQ)
E(EDOTDOT)
E(EDQUOT)
E(EHOSTDOWN)
E(EHOSTUNREACH)
#ifdef EHWPOISON
E(EHWPOISON)
#endif
E(EIDRM)
E(EILSEQ)
E(EINPROGRESS)
E(EISCONN)
E(EISNAM)
#ifdef EKEYEXPIRED
E(EKEYEXPIRED)
#endif
#ifdef EKEYREJECTED
E(EKEYREJECTED)
#endif
#ifdef EKEYREVOKED
E(EKEYREVOKED)
#endif
E(EL2HLT)
E(EL2NSYNC)
E(EL3HLT)
E(EL3RST)
E(ELIBACC)
E(ELIBBAD)
E(ELIBEXEC)
E(ELIBMAX)
E(ELIBSCN)
E(ELNRNG)
E(ELOOP)
E(EMEDIUMTYPE)
E(EMSGSIZE)
E(EMULTIHOP)
E(ENAMETOOLONG)
E(ENAVAIL)
E(ENETDOWN)
E(ENETRESET)
E(ENETUNREACH)
E(ENOANO)
E(ENOBUFS)
E(ENOCSI)
E(ENODATA)
#ifdef ENOKEY
E(ENOKEY)
#endif
E(ENOLCK)
E(ENOLINK)
E(ENOMEDIUM)
#ifdef ENOMSG
E(ENOMSG)
#endif
E(ENONET)
E(ENOPKG)
E(ENOPROTOOPT)
E(ENOSR)
E(ENOSTR)
E(ENOSYS)
E(ENOTCONN)
E(ENOTEMPTY)
E(ENOTNAM)
#ifdef ENOTRECOVERABLE
E(ENOTRECOVERABLE)
#endif
E(ENOTSOCK)
E(ENOTUNIQ)
E(EOPNOTSUPP)
E(EOVERFLOW)
#ifdef EOWNERDEAD
E(EOWNERDEAD)
#endif
E(EPFNOSUPPORT)
E(EPROTO)
E(EPROTONOSUPPORT)
E(EPROTOTYPE)
E(EREMCHG)
E(EREMOTE)
E(EREMOTEIO)
E(ERESTART)
#ifdef ERFKILL
E(ERFKILL)
#endif
E(ESHUTDOWN)
E(ESOCKTNOSUPPORT)
E(ESRMNT)
E(ESTALE)
E(ESTRPIPE)
E(ETIME)
E(ETIMEDOUT)
E(ETOOMANYREFS)
E(EUCLEAN)
E(EUNATCH)
E(EUSERS)
E(EXFULL)
