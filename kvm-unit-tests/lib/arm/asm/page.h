#ifndef _ASMARM_PAGE_H_
#define _ASMARM_PAGE_H_
/*
 * Copyright (C) 2014, Red Hat Inc, Andrew Jones <drjones@redhat.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.
 */

#include <const.h>

#define PAGE_SHIFT		12
#define PAGE_SIZE		(_AC(1,UL) << PAGE_SHIFT)
#define PAGE_MASK		(~(PAGE_SIZE-1))

#ifndef __ASSEMBLY__

#define PAGE_ALIGN(addr)	ALIGN(addr, PAGE_SIZE)

#include <alloc.h>

typedef u64 pteval_t;
typedef u64 pmdval_t;
typedef u64 pgdval_t;
typedef struct { pteval_t pte; } pte_t;
typedef struct { pmdval_t pmd; } pmd_t;
typedef struct { pgdval_t pgd; } pgd_t;
typedef struct { pteval_t pgprot; } pgprot_t;

#define pte_val(x)		((x).pte)
#define pmd_val(x)		((x).pmd)
#define pgd_val(x)		((x).pgd)
#define pgprot_val(x)		((x).pgprot)

#define __pte(x)		((pte_t) { (x) } )
#define __pmd(x)		((pmd_t) { (x) } )
#define __pgd(x)		((pgd_t) { (x) } )
#define __pgprot(x)		((pgprot_t) { (x) } )

typedef struct { pgd_t pgd; } pud_t;
#define pud_val(x)		(pgd_val((x).pgd))
#define __pud(x)		((pud_t) { __pgd(x) } )

#ifndef __virt_to_phys
#define __phys_to_virt(x)	((unsigned long) (x))
#define __virt_to_phys(x)	(x)
#endif

#define __va(x)			((void *)__phys_to_virt((phys_addr_t)(x)))
#define __pa(x)			__virt_to_phys((unsigned long)(x))

#define virt_to_pfn(kaddr)	(__pa(kaddr) >> PAGE_SHIFT)
#define pfn_to_virt(pfn)	__va((pfn) << PAGE_SHIFT)

#endif /* !__ASSEMBLY__ */
#endif /* _ASMARM_PAGE_H_ */
