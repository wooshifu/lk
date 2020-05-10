/*
 * Copyright (c) 2020 Travis Geiselbrecht
 *
 * Use of this source code is governed by a MIT-style
 * license that can be found in the LICENSE file or at
 * https://opensource.org/licenses/MIT
 */
#include "arch/riscv/mmu.h"

#include <lk/debug.h>
#include <lk/err.h>
#include <lk/trace.h>
#include <arch/mmu.h>

#define LOCAL_TRACE 1

#if RISCV_MMU

#include <kernel/vm.h>

#if __riscv_xlen == 32
#error "32 bit mmu not supported yet"
#endif

uint64_t kernel_pgtable_top[512 * 4] __ALIGNED(PAGE_SIZE);

/* initial memory mappings. VM uses to construct mappings after the fact */
struct mmu_initial_mapping mmu_initial_mappings[] = {
    /* all of memory, mapped in start.S */
    {
        .phys = 0,
        .virt = KERNEL_BASE,
#if RISCV_MMU == 48
        .size = 512UL * 1024 * 1024 * 1024,
#elif RISCV_MMU == 39
        .size = 64UL * 1024 * 1024 * 1024,
#else
#error implement
#endif
        .flags = 0,
        .name = "memory"
    },

    /* null entry to terminate the list */
    { 0 }
};

/* initialize per address space */
status_t arch_mmu_init_aspace(arch_aspace_t *aspace, vaddr_t base, size_t size, uint flags) {
    LTRACEF("aspace %p, base %#lx, size %#zx, flags %#x\n", aspace, base, size, flags);

    // TODO: initialze aspace here

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

status_t arch_mmu_query(arch_aspace_t *aspace, vaddr_t vaddr, paddr_t *paddr, uint *flags) {
    LTRACEF("aspace %p, vaddr %#lx\n", aspace, vaddr);

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
