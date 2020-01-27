/*
 * Copyright (c) 2020 Travis Geiselbrecht
 *
 * Use of this source code is governed by a MIT-style
 * license that can be found in the LICENSE file or at
 * https://opensource.org/licenses/MIT
 */
#include "arch/riscv/mmu.h"

#include <lk/debug.h>
#include <arch/mmu.h>


#if RISCV_MMU

/* initialize per address space */
status_t arch_mmu_init_aspace(arch_aspace_t *aspace, vaddr_t base, size_t size, uint flags) {
    PANIC_UNIMPLEMENTED;
}
status_t arch_mmu_destroy_aspace(arch_aspace_t *aspace) {
    PANIC_UNIMPLEMENTED;
}

/* routines to map/unmap/query mappings per address space */
int arch_mmu_map(arch_aspace_t *aspace, vaddr_t vaddr, paddr_t paddr, uint count, uint flags) {
    PANIC_UNIMPLEMENTED;
}

int arch_mmu_unmap(arch_aspace_t *aspace, vaddr_t vaddr, uint count) {
    PANIC_UNIMPLEMENTED;
}

status_t arch_mmu_query(arch_aspace_t *aspace, vaddr_t vaddr, paddr_t *paddr, uint *flags) {
    PANIC_UNIMPLEMENTED;
}


vaddr_t arch_mmu_pick_spot(arch_aspace_t *aspace,
                           vaddr_t base, uint prev_region_arch_mmu_flags,
                           vaddr_t end,  uint next_region_arch_mmu_flags,
                           vaddr_t align, size_t size, uint arch_mmu_flags) {
    PANIC_UNIMPLEMENTED;
}


/* load a new user address space context.
 * aspace argument NULL should unload user space.
 */
void arch_mmu_context_switch(arch_aspace_t *aspace) {
    PANIC_UNIMPLEMENTED;
}

void arch_disable_mmu(void) {
    PANIC_UNIMPLEMENTED;
}



#endif
