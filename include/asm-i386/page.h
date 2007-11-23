#ifndef _I386_PAGE_H
#define _I386_PAGE_H

/* PAGE_SHIFT determines the page size */
#define PAGE_SHIFT	12
#define PAGE_SIZE	(1UL << PAGE_SHIFT)
#define PAGE_MASK	(~(PAGE_SIZE-1))

#ifdef __KERNEL__
#ifndef __ASSEMBLY__

#define STRICT_MM_TYPECHECKS

#include <linux/config.h>

#ifdef CONFIG_X86_USE_3DNOW

#include <asm/mmx.h>

#define clear_page(page)	mmx_clear_page(page)
#define copy_page(to,from)	mmx_copy_page(to,from)

#else

/*
 *	On older X86 processors its not a win to use MMX here it seems.
 *	Maybe the K6-III ?
 */
 
#define clear_page(page)	memset((void *)(page), 0, PAGE_SIZE)
#define copy_page(to,from)	memcpy((void *)(to), (void *)(from), PAGE_SIZE)

#endif

#ifdef STRICT_MM_TYPECHECKS
/*
 * These are used to make use of C type-checking..
 */
typedef struct { unsigned long pte; } pte_t;
typedef struct { unsigned long pmd; } pmd_t;
typedef struct { unsigned long pgd; } pgd_t;
typedef struct { unsigned long pgprot; } pgprot_t;

#define pte_val(x)	((x).pte)
#define pmd_val(x)	((x).pmd)
#define pgd_val(x)	((x).pgd)
#define pgprot_val(x)	((x).pgprot)

#define __pte(x)	((pte_t) { (x) } )
#define __pmd(x)	((pmd_t) { (x) } )
#define __pgd(x)	((pgd_t) { (x) } )
#define __pgprot(x)	((pgprot_t) { (x) } )

#else
/*
 * .. while these make it easier on the compiler
 */
typedef unsigned long pte_t;
typedef unsigned long pmd_t;
typedef unsigned long pgd_t;
typedef unsigned long pgprot_t;

#define pte_val(x)	(x)
#define pmd_val(x)	(x)
#define pgd_val(x)	(x)
#define pgprot_val(x)	(x)

#define __pte(x)	(x)
#define __pmd(x)	(x)
#define __pgd(x)	(x)
#define __pgprot(x)	(x)

#endif
#endif /* !__ASSEMBLY__ */

/* to align the pointer to the (next) page boundary */
#define PAGE_ALIGN(addr)	(((addr)+PAGE_SIZE-1)&PAGE_MASK)

/*
 * This handles the memory map.. We could make this a config
 * option, but too many people screw it up, and too few need
 * it.
 *
 * A __PAGE_OFFSET of 0xC0000000 means that the kernel has
 * a virtual address space of one gigabyte, which limits the
 * amount of physical memory you can use to about 950MB. If
 * you want to use more physical memory, change this define.
 *
 * For example, if you have 2GB worth of physical memory, you
 * could change this define to 0x80000000, which gives the
 * kernel 2GB of virtual memory (enough to most of your physical memory
 * as the kernel needs a bit extra for various io-memory mappings)
 *
 * IF YOU CHANGE THIS, PLEASE ALSO CHANGE
 *
 *	arch/i386/vmlinux.lds
 *
 * which has the same constant encoded..
 */

#include <asm/page_offset.h>

#define __PAGE_OFFSET		(PAGE_OFFSET_RAW)

#ifndef __ASSEMBLY__

#define BUG() do { \
	printk("kernel BUG at %s:%d!\n", __FILE__, __LINE__); \
	__asm__ __volatile__(".byte 0x0f,0x0b"); \
} while (0)

#define PAGE_BUG(page) do { \
	BUG(); \
} while (0)

#endif /* __ASSEMBLY__ */

#define PAGE_OFFSET		((unsigned long)__PAGE_OFFSET)
#define __pa(x)			((unsigned long)(x)-PAGE_OFFSET)
#define __va(x)			((void *)((unsigned long)(x)+PAGE_OFFSET))
#define MAP_NR(addr)		(__pa(addr) >> PAGE_SHIFT)
#define PHYSMAP_NR(addr)	((unsigned long)(addr) >> PAGE_SHIFT)

#endif /* __KERNEL__ */

#endif /* _I386_PAGE_H */
