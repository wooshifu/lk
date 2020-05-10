/*
 * Copyright (c) 2020 Travis Geiselbrecht
 *
 * Use of this source code is governed by a MIT-style
 * license that can be found in the LICENSE file or at
 * https://opensource.org/licenses/MIT
 */
#include "arch/riscv/mmu.h"

#include <assert.h>
#include <lk/debug.h>
#include <lk/err.h>
#include <lk/trace.h>
#include <arch/mmu.h>
#include <arch/riscv.h>
#include <arch/riscv/csr.h>
#include <kernel/vm.h>

#define LOCAL_TRACE 3

#if RISCV_MMU

#include <kernel/vm.h>

#if __riscv_xlen == 32
#error "32 bit mmu not supported yet"
#endif

riscv_pte_t kernel_pgtable[512] __ALIGNED(PAGE_SIZE);
paddr_t kernel_pgtable_phys; // filled in by start.S

/* initial memory mappings. VM uses to construct mappings after the fact */
struct mmu_initial_mapping mmu_initial_mappings[] = {
    /* all of memory, mapped in start.S */
    {
        .phys = 0,
        .virt = KERNEL_BASE,
#if RISCV_MMU == 48
        .size = 512UL * GB,
#elif RISCV_MMU == 39
        .size = 64UL * GB,
#else
#error implement
#endif
        .flags = 0,
        .name = "memory"
    },

    /* null entry to terminate the list */
    { 0 }
};

static inline void riscv_set_satp(uint asid, paddr_t pt) {
    ulong satp;

#if RISCV_MMU == 48
    satp = RISCV_SATP_MODE_SV48;
#elif RISCV_MMU == 39
    satp = RISCV_SATP_MODE_SV39;
#endif

    // make sure the asid is in range
    DEBUG_ASSERT((asid & RISCV_SATP_ASID_MASK) == 0);
    satp |= (ulong)asid << RISCV_SATP_ASID_SHIFT;

    // make sure the page table is aligned
    DEBUG_ASSERT(IS_PAGE_ALIGNED(pt));
    satp |= pt;

    riscv_csr_write(RISCV_CSR_SATP, satp);

    // TODO: TLB flush here or use asid properly
    // sfence.vma zero, zero
}

// given a va address and the level, compute the index in the current PT
static inline uint vaddr_to_index(vaddr_t va, uint level) {
    // levels count down from PT_LEVELS - 1
    DEBUG_ASSERT(level < RISCV_MMU_PT_LEVELS);

    // canonicalize the address
    va &= RISCV_MMU_CANONICAL_MASK;

    uint index = (va >> PAGE_SIZE_SHIFT) >> (level * RISCV_MMU_PT_SHIFT);
    LTRACEF_LEVEL(3, "va %#lx, level %u = index %#x\n", va, level, index);

    DEBUG_ASSERT(index < RISCV_MMU_PT_ENTRIES);

    return index;
}

/* initialize per address space */
status_t arch_mmu_init_aspace(arch_aspace_t *aspace, vaddr_t base, size_t size, uint flags) {
    LTRACEF("aspace %p, base %#lx, size %#zx, flags %#x\n", aspace, base, size, flags);

    DEBUG_ASSERT(aspace);

    /* validate that the base + size is sane and doesn't wrap */
    DEBUG_ASSERT(size > PAGE_SIZE);
    DEBUG_ASSERT(base + size - 1 > base);

    aspace->flags = flags;
    if (flags & ARCH_ASPACE_FLAG_KERNEL) {
        /* at the moment we can only deal with address spaces as globally defined */
        DEBUG_ASSERT(base == KERNEL_ASPACE_BASE);
        DEBUG_ASSERT(size == KERNEL_ASPACE_SIZE);

        aspace->base = base;
        aspace->size = size;
        aspace->pt_virt = kernel_pgtable;
        aspace->pt_phys = kernel_pgtable_phys;
    } else {
        PANIC_UNIMPLEMENTED;
    }

    LTRACEF("pt phys %#lx, pt virt %p\n", aspace->pt_phys, aspace->pt_virt);

    return NO_ERROR;
}
status_t arch_mmu_destroy_aspace(arch_aspace_t *aspace) {
    LTRACEF("aspace %p\n", aspace);

    PANIC_UNIMPLEMENTED;
}

/* routines to map/unmap/query mappings per address space */
int arch_mmu_map(arch_aspace_t *aspace, vaddr_t vaddr, paddr_t paddr, uint count, uint flags) {
    LTRACEF("vaddr %#lx paddr %#lx count %u flags %#x\n", vaddr, paddr, count, flags);

    PANIC_UNIMPLEMENTED;
}

int arch_mmu_unmap(arch_aspace_t *aspace, vaddr_t vaddr, uint count) {
    LTRACEF("vaddr %#lx count %u\n", vaddr, count);

    PANIC_UNIMPLEMENTED;
}

status_t arch_mmu_query(arch_aspace_t *aspace, const vaddr_t vaddr, paddr_t *paddr, uint *flags) {
    LTRACEF("aspace %p, vaddr %#lx\n", aspace, vaddr);

    DEBUG_ASSERT(aspace);

    *paddr = *flags = 0;

    // trim the vaddr to the aspace
    if (vaddr < aspace->base || vaddr > aspace->base + aspace->size - 1) {
        return ERR_OUT_OF_RANGE;
    }

    uint level = RISCV_MMU_PT_LEVELS - 1;
    uint index = vaddr_to_index(vaddr, level);
    volatile riscv_pte_t *ptep = aspace->pt_virt + index;

    // walk down through the levels, looking for a terminal entry that matches our address
    for (;;) {
        LTRACEF_LEVEL(2, "level %u, index %u, pte %p (%#llx)\n", level, index, ptep, *ptep);

        // look at our page table entry
        riscv_pte_t pte = *ptep;
        if ((pte & RISCV_PTE_V) == 0) {
            // invalid entry, terminate search
            return ERR_NOT_FOUND;
        } else if ((pte & RISCV_PTE_PERM_MASK) == 0) {
            // next level page table pointer (RWX = 0)
            PANIC_UNIMPLEMENTED;
        } else {
            // terminal entry
            LTRACEF_LEVEL(3, "terminal entry\n");

            // extract the ppn
            paddr_t pa = RISCV_PTE_PPN(pte);
            LTRACEF_LEVEL(3, "pa %#lx\n", pa);
        }

        DEBUG_ASSERT(level > 0);
        level--;
    }

    PANIC_UNIMPLEMENTED;
}


/* load a new user address space context.
 * aspace argument NULL should unload user space.
 */
void arch_mmu_context_switch(arch_aspace_t *aspace) {
    LTRACEF("aspace %p\n", aspace);

    PANIC_UNIMPLEMENTED;
}

#endif
