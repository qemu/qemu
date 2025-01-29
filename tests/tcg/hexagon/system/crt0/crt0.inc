/*
 *  Copyright(c) 2024-2025 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

	.macro AddrOf Var, To = r0
	\To\() = ## (\Var)
	.endm

	.macro ReadFrom Var, To = r0
	AddrOf \Var, \To
	\To = memw (\To)
	.endm

	.macro WriteTo Var, From = r0, Ptr = r1
	.ifnc "\From", "\Ptr"
	AddrOf \Var, \Ptr
	memw (\Ptr) = \From
	\From = memw (\Ptr)
	.else
	.print "Macro arguments \"From\" and \"Ptr\" cannot be the same."
	.err
	.endif
	.endm
