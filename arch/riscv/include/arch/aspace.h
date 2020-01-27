/*
 * Copyright (c) 2020 Travis Geiselbrecht
 *
 * Use of this source code is governed by a MIT-style
 * license that can be found in the LICENSE file or at
 * https://opensource.org/licenses/MIT
 */
#pragma once

#include <lk/compiler.h>
#include <lk/list.h>
#include <arch/riscv/mmu.h>

__BEGIN_CDECLS

struct arch_aspace {
#if 0
    /* pointer to the translation table */
    paddr_t tt_phys;
    pte_t *tt_virt;

    uint flags;

    /* range of address space */
    vaddr_t base;
    size_t size;
#endif
};

__END_CDECLS


