/***********************************************************************
** Copyright (C) 2003  ACX100 Open Source Project
**
** The contents of this file are subject to the Mozilla Public
** License Version 1.1 (the "License"); you may not use this file
** except in compliance with the License. You may obtain a copy of
** the License at http://www.mozilla.org/MPL/
**
** Software distributed under the License is distributed on an "AS
** IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or
** implied. See the License for the specific language governing
** rights and limitations under the License.
**
** Alternatively, the contents of this file may be used under the
** terms of the GNU Public License version 2 (the "GPL"), in which
** case the provisions of the GPL are applicable instead of the
** above.  If you wish to allow the use of your version of this file
** only under the terms of the GPL and not to allow others to use
** your version of this file under the MPL, indicate your decision
** by deleting the provisions above and replace them with the notice
** and other provisions required by the GPL.  If you do not delete
** the provisions above, a recipient may use your version of this
** file under either the MPL or the GPL.
** ---------------------------------------------------------------------
** Inquiries regarding the ACX100 Open Source Project can be
** made directly to:
**
** acx100-users@lists.sf.net
** http://acx100.sf.net
** ---------------------------------------------------------------------
*/

/***********************************************************************
** This code is based on elements which are
** Copyright (C) 1999 AbsoluteValue Systems, Inc.  All Rights Reserved.
** info@linux-wlan.com
** http://www.linux-wlan.com
*/

/*=============================================================*/
/*------ Establish Platform Identity --------------------------*/
/*=============================================================*/
/* Key macros: */
/* WLAN_CPU_FAMILY */
#define WLAN_Ix86			1
#define WLAN_PPC			2
#define WLAN_Ix96			3
#define WLAN_ARM			4
#define WLAN_ALPHA			5
#define WLAN_MIPS			6
#define WLAN_HPPA			7
#define WLAN_SPARC			8
#define WLAN_SH				9
#define WLAN_x86_64			10
/* WLAN_CPU_CORE */
#define WLAN_I386CORE			1
#define WLAN_PPCCORE			2
#define WLAN_I296			3
#define WLAN_ARMCORE			4
#define WLAN_ALPHACORE			5
#define WLAN_MIPSCORE			6
#define WLAN_HPPACORE			7
/* WLAN_CPU_PART */
#define WLAN_I386PART			1
#define WLAN_MPC860			2
#define WLAN_MPC823			3
#define WLAN_I296SA			4
#define WLAN_PPCPART			5
#define WLAN_ARMPART			6
#define WLAN_ALPHAPART			7
#define WLAN_MIPSPART			8
#define WLAN_HPPAPART			9
/* WLAN_SYSARCH */
#define WLAN_PCAT			1
#define WLAN_MBX			2
#define WLAN_RPX			3
#define WLAN_LWARCH			4
#define WLAN_PMAC			5
#define WLAN_SKIFF			6
#define WLAN_BITSY			7
#define WLAN_ALPHAARCH			7
#define WLAN_MIPSARCH			9
#define WLAN_HPPAARCH			10
/* WLAN_HOSTIF (generally set on the command line, not detected) */
#define WLAN_PCMCIA			1
#define WLAN_ISA			2
#define WLAN_PCI			3
#define WLAN_USB			4
#define WLAN_PLX			5

/* Note: the PLX HOSTIF above refers to some vendors implementations for */
/*       PCI.  It's a PLX chip that is a PCI to PCMCIA adapter, but it   */
/*       isn't a real PCMCIA host interface adapter providing all the    */
/*       card&socket services.                                           */

#ifdef __powerpc__
#ifndef __ppc__
#define __ppc__
#endif
#endif

#if (defined(CONFIG_PPC) || defined(CONFIG_8xx))
#ifndef __ppc__
#define __ppc__
#endif
#endif

#if defined(__x86_64__)
 #define WLAN_CPU_FAMILY	WLAN_x86_64
 #define WLAN_SYSARCH		WLAN_PCAT
#elif defined(__i386__) || defined(__i486__) || defined(__i586__) || defined(__i686__)
 #define WLAN_CPU_FAMILY	WLAN_Ix86
 #define WLAN_CPU_CORE		WLAN_I386CORE
 #define WLAN_CPU_PART		WLAN_I386PART
 #define WLAN_SYSARCH		WLAN_PCAT
#elif defined(__ppc__)
 #define WLAN_CPU_FAMILY	WLAN_PPC
 #define WLAN_CPU_CORE		WLAN_PPCCORE
 #if defined(CONFIG_MBX)
  #define WLAN_CPU_PART		WLAN_MPC860
  #define WLAN_SYSARCH		WLAN_MBX
 #elif defined(CONFIG_RPXLITE)
  #define WLAN_CPU_PART		WLAN_MPC823
  #define WLAN_SYSARCH		WLAN_RPX
 #elif defined(CONFIG_RPXCLASSIC)
  #define WLAN_CPU_PART		WLAN_MPC860
  #define WLAN_SYSARCH		WLAN_RPX
 #else
  #define WLAN_CPU_PART		WLAN_PPCPART
  #define WLAN_SYSARCH		WLAN_PMAC
 #endif
#elif defined(__arm__)
 #define WLAN_CPU_FAMILY	WLAN_ARM
 #define WLAN_CPU_CORE		WLAN_ARMCORE
 #define WLAN_CPU_PART		WLAN_ARM_PART
 #define WLAN_SYSARCH		WLAN_SKIFF
#elif defined(__alpha__)
 #define WLAN_CPU_FAMILY	WLAN_ALPHA
 #define WLAN_CPU_CORE		WLAN_ALPHACORE
 #define WLAN_CPU_PART		WLAN_ALPHAPART
 #define WLAN_SYSARCH		WLAN_ALPHAARCH
#elif defined(__mips__)
 #define WLAN_CPU_FAMILY	WLAN_MIPS
 #define WLAN_CPU_CORE		WLAN_MIPSCORE
 #define WLAN_CPU_PART		WLAN_MIPSPART
 #define WLAN_SYSARCH		WLAN_MIPSARCH
#elif defined(__hppa__)
 #define WLAN_CPU_FAMILY	WLAN_HPPA
 #define WLAN_CPU_CORE		WLAN_HPPACORE
 #define WLAN_CPU_PART		WLAN_HPPAPART
 #define WLAN_SYSARCH		WLAN_HPPAARCH
#elif defined(__sparc__)
 #define WLAN_CPU_FAMILY	WLAN_SPARC
 #define WLAN_SYSARCH		WLAN_SPARC
#elif defined(__sh__)
 #define WLAN_CPU_FAMILY	WLAN_SH
 #define WLAN_SYSARCH		WLAN_SHARCH
 #ifndef __LITTLE_ENDIAN__
  #define __LITTLE_ENDIAN__
 #endif
#else
 #error "No CPU identified!"
#endif

/*
   Some big endian machines implicitly do all I/O in little endian mode.

   In particular:
	  Linux/PPC on PowerMacs (PCI)
	  Arm/Intel Xscale (PCI)

   This may also affect PLX boards and other BE &| PPC platforms;
   as new ones are discovered, add them below.
*/

#if ((WLAN_SYSARCH == WLAN_SKIFF) || (WLAN_SYSARCH == WLAN_PMAC))
#define REVERSE_ENDIAN
#endif

/*=============================================================*/
/*------ Hardware Portability Macros --------------------------*/
/*=============================================================*/
#if (WLAN_CPU_FAMILY == WLAN_PPC)
#define wlan_inw(a)                     in_be16((unsigned short *)((a)+_IO_BASE))
#define wlan_inw_le16_to_cpu(a)         inw((a))
#define wlan_outw(v,a)                  out_be16((unsigned short *)((a)+_IO_BASE), (v))
#define wlan_outw_cpu_to_le16(v,a)      outw((v),(a))
#else
#define wlan_inw(a)                     inw((a))
#define wlan_inw_le16_to_cpu(a)         __cpu_to_le16(inw((a)))
#define wlan_outw(v,a)                  outw((v),(a))
#define wlan_outw_cpu_to_le16(v,a)      outw(__cpu_to_le16((v)),(a))
#endif

/*=============================================================*/
/*------ Bit settings -----------------------------------------*/
/*=============================================================*/
#define ieee2host16(n)	__le16_to_cpu(n)
#define ieee2host32(n)	__le32_to_cpu(n)
#define host2ieee16(n)	__cpu_to_le16(n)
#define host2ieee32(n)	__cpu_to_le32(n)

/* for constants */
#ifdef __LITTLE_ENDIAN
 #define IEEE16(a,n)     a = n, a##i = n,
#else
 #ifdef __BIG_ENDIAN
  /* shifts would produce gcc warnings. Oh well... */
  #define IEEE16(a,n)     a = n, a##i = ((n&0xff)*256 + ((n&0xff00)/256)),
 #else
  #error give me endianness or give me death
 #endif
#endif

/*=============================================================*/
/*------ Compiler Portability Macros --------------------------*/
/*=============================================================*/
#define WLAN_PACKED	__attribute__ ((packed))

/* Interrupt handler backwards compatibility stuff */
#ifndef IRQ_NONE
#define IRQ_NONE
#define IRQ_HANDLED
typedef void irqreturn_t;
#endif

#ifndef ARPHRD_IEEE80211_PRISM
#define ARPHRD_IEEE80211_PRISM 802
#endif

#define ETH_P_80211_RAW		(ETH_P_ECONET + 1)

/*============================================================================*
 * Constants                                                                  *
 *============================================================================*/
#define WLAN_IEEE_OUI_LEN	3

/*============================================================================*
 * Types                                                                      *
 *============================================================================*/

/* local ether header type */
typedef struct wlan_ethhdr {
	u8	daddr[ETH_ALEN];
	u8	saddr[ETH_ALEN];
	u16	type;
} WLAN_PACKED wlan_ethhdr_t;

/* local llc header type */
typedef struct wlan_llc {
	u8	dsap;
	u8	ssap;
	u8	ctl;
} WLAN_PACKED wlan_llc_t;

/* local snap header type */
typedef struct wlan_snap {
	u8	oui[WLAN_IEEE_OUI_LEN];
	u16	type;
} WLAN_PACKED wlan_snap_t;
