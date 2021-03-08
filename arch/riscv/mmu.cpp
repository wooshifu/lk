/*
 * Copyright (c) 2020 Travis Geiselbrecht
 *
 * Use of this source code is governed by a MIT-style
 * license that can be found in the LICENSE file or at
 * https://opensource.org/licenses/MIT
 */
#if RISCV_MMU

#include "arch/riscv/mmu.h"

#include <assert.h>
#include <string.h>
#include <lk/debug.h>
#include <lk/err.h>
#include <lk/trace.h>
#include <arch/ops.h>
#include <arch/mmu.h>
#include <arch/riscv.h>
#include <arch/riscv/csr.h>
#include <arch/riscv/sbi.h>
#include <kernel/vm.h>

#define LOCAL_TRACE 0

#include <kernel/vm.h>

#if __riscv_xlen == 32
#error "32 bit mmu not supported yet"
#endif

riscv_pte_t kernel_pgtable[512] __ALIGNED(PAGE_SIZE);
paddr_t kernel_pgtable_phys; // filled in by start.S
static ulong riscv_asid_mask;

// initial memory mappings. VM uses to construct mappings after the fact
struct mmu_initial_mapping mmu_initial_mappings[] = {
    // all of memory, mapped in start.S
    {
        .phys = 0,
        .virt = KERNEL_ASPACE_BASE,
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

    // null entry to terminate the list
    { }
};

// called once on the boot cpu during very early (single threaded) init
extern "C"
void riscv_early_mmu_init() {
    // figure out the number of support ASID bits by writing all 1s to
    // the asid field in satp and seeing which ones 'stick'
    auto satp_orig = riscv_csr_read(satp);
    auto satp = satp_orig | (RISCV_SATP_ASID_MASK << RISCV_SATP_ASID_SHIFT);
    riscv_csr_write(satp, satp);
    riscv_asid_mask = (riscv_csr_read(satp) >> RISCV_SATP_ASID_SHIFT) & RISCV_SATP_ASID_MASK;
    riscv_csr_write(satp, satp_orig);
}

// called a bit later once on the boot cpu
extern "C"
void riscv_mmu_init() {
    printf("RISCV: MMU ASID mask %#lx\n", riscv_asid_mask);
}

static inline void riscv_set_satp(uint asid, paddr_t pt) {
    ulong satp;

#if RISCV_MMU == 48
    satp = RISCV_SATP_MODE_SV48;
#elif RISCV_MMU == 39
    satp = RISCV_SATP_MODE_SV39;
#endif

    // make sure the asid is in range
    DEBUG_ASSERT(asid & riscv_asid_mask);
    satp |= (ulong)asid << RISCV_SATP_ASID_SHIFT;

    // make sure the page table is aligned
    DEBUG_ASSERT(IS_PAGE_ALIGNED(pt));
    satp |= pt >> PAGE_SIZE_SHIFT;

    riscv_csr_write(RISCV_CSR_SATP, satp);

    // TODO: TLB flush here or use asid properly
    asm("sfence.vma zero, zero");
}

static void riscv_tlb_flush_vma_range(vaddr_t base, size_t count) {
    if (count == 0)
        return;

    // Use SBI to shoot down a range of vaddrs on all the cpus
    ulong hart_mask = -1; // TODO: be more selective about the cpus
    sbi_rfence_vma(&hart_mask, base, count * PAGE_SIZE);

    // locally shoot down
    // XXX: is this needed or does the sbi call do it if included in the local hart mask?
    while (count > 0) {
        asm volatile("sfence.vma %0, zero" :: "r"(base));
        base += PAGE_SIZE;
        count--;
    }
}

static void riscv_tlb_flush_global() {
    // Use SBI to do a global TLB shoot down on all cpus
    ulong hart_mask = -1; // TODO: be more selective about the cpus
    sbi_rfence_vma(&hart_mask, 0, -1);
}

// given a va address and the level, compute the index in the current PT
static inline uint vaddr_to_index(vaddr_t va, uint level) {
    // levels count down from PT_LEVELS - 1
    DEBUG_ASSERT(level < RISCV_MMU_PT_LEVELS);

    // canonicalize the address
    va &= RISCV_MMU_CANONICAL_MASK;

    uint index = ((va >> PAGE_SIZE_SHIFT) >> (level * RISCV_MMU_PT_SHIFT)) & (RISCV_MMU_PT_ENTRIES - 1);
    LTRACEF_LEVEL(3, "canonical va %#lx, level %u = index %#x\n", va, level, index);

    return index;
}

static uintptr_t page_size_per_level(uint level) {
    // levels count down from PT_LEVELS - 1
    DEBUG_ASSERT(level < RISCV_MMU_PT_LEVELS);

    return 1UL << (PAGE_SIZE_SHIFT + level * RISCV_MMU_PT_SHIFT);
}

static uintptr_t page_mask_per_level(uint level) {
    return page_size_per_level(level) - 1;
}

static volatile riscv_pte_t *alloc_ptable(paddr_t *pa) {
    // grab a page from the pmm
    vm_page_t *p = pmm_alloc_page();
    if (!p) {
        return NULL;
    }

    // get the physical and virtual mappings of the page
    *pa = vm_page_to_paddr(p);
    riscv_pte_t *pte = (riscv_pte_t *)paddr_to_kvaddr(*pa);

    // zero it out
    memset(pte, 0, PAGE_SIZE);

    smp_wmb();

    LTRACEF_LEVEL(3, "returning pa %#lx, va %p\n", *pa, pte);
    return pte;
}

static riscv_pte_t mmu_flags_to_pte(uint flags) {
    riscv_pte_t pte = 0;

    pte |= (flags & ARCH_MMU_FLAG_PERM_USER) ? RISCV_PTE_U : 0;
    pte |= (flags & ARCH_MMU_FLAG_PERM_RO) ? RISCV_PTE_R : (RISCV_PTE_R | RISCV_PTE_W);
    pte |= (flags & ARCH_MMU_FLAG_PERM_NO_EXECUTE) ? 0 : RISCV_PTE_X;

    return pte;
}

static uint pte_flags_to_mmu_flags(riscv_pte_t pte) {
    uint f = 0;
    if ((pte & (RISCV_PTE_R | RISCV_PTE_W)) == RISCV_PTE_R) {
        f |= ARCH_MMU_FLAG_PERM_RO;
    }
    f |= (pte & RISCV_PTE_X) ? 0 : ARCH_MMU_FLAG_PERM_NO_EXECUTE;
    f |= (pte & RISCV_PTE_U) ? ARCH_MMU_FLAG_PERM_USER : 0;
    return f;
}

// public api

// initialize per address space
status_t arch_mmu_init_aspace(arch_aspace_t *aspace, vaddr_t base, size_t size, uint flags) {
    LTRACEF("aspace %p, base %#lx, size %#zx, flags %#x\n", aspace, base, size, flags);

    DEBUG_ASSERT(aspace);

    // validate that the base + size is sane and doesn't wrap
    DEBUG_ASSERT(size > PAGE_SIZE);
    DEBUG_ASSERT(base + size - 1 > base);

    aspace->flags = flags;
    if (flags & ARCH_ASPACE_FLAG_KERNEL) {
        // at the moment we can only deal with address spaces as globally defined
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

enum class walk_cb_ret {
    HALT,
    RESTART,
    COMMIT_AND_RESTART,
    COMMIT_AND_HALT,
    ALLOC_PT,
};

// in the callback arg, define a function or lambda that matches this signature
using page_walk_cb = walk_cb_ret(*)(uint level, uint index, riscv_pte_t *pte, vaddr_t *vaddr, int *err);

// generic walker routine to automate drilling through a page table structure
template <typename F = page_walk_cb>
static int riscv_pt_walk(arch_aspace_t *aspace, vaddr_t vaddr, F callback) {
    LTRACEF("vaddr %#lx\n", vaddr);

    DEBUG_ASSERT(aspace);

    // modifed by callback
    int err = NO_ERROR;
restart:
    // bootstrap the top level walk
    uint level = RISCV_MMU_PT_LEVELS - 1;
    uint index = vaddr_to_index(vaddr, level);
    volatile riscv_pte_t *ptep = aspace->pt_virt + index;

    for (;;) {
        LTRACEF_LEVEL(2, "level %u, index %u, pte %p (%#lx) va %#lx\n",
                      level, index, ptep, *ptep, vaddr);

        // look at our page table entry
        riscv_pte_t pte = *ptep;
        if ((pte & RISCV_PTE_V) && !(pte & RISCV_PTE_PERM_MASK)) {
            // next level page table pointer (RWX = 0)
            paddr_t ptp = RISCV_PTE_PPN(pte);
            volatile riscv_pte_t *ptv = (riscv_pte_t *)paddr_to_kvaddr(ptp);

            LTRACEF_LEVEL(2, "next level page table at %p, pa %#lx\n", ptv, ptp);

            // go one level deeper
            level--;
            index = vaddr_to_index(vaddr, level);
            ptep = ptv + index;
        } else {
            // it's a non valid page entry or a valid terminal entry
            // call the callback, seeing what the user wants
            auto ret = callback(level, index, &pte, &vaddr, &err);
            switch (ret) {
                case walk_cb_ret::HALT:
                    // stop here
                    return err;
                case walk_cb_ret::RESTART:
                    // restart the walk
                    // user should have modified vaddr or we'll probably be in a loop
                    goto restart;
                case walk_cb_ret::COMMIT_AND_RESTART:
                    // callback has (hopefully) modified the pte and vaddr, we'll commit it and start the walk again
                    *ptep = pte;

                    goto restart;
                case walk_cb_ret::COMMIT_AND_HALT:
                    // commit the change and halt
                    *ptep = pte;

                    return err;
                case walk_cb_ret::ALLOC_PT:
                    // user wants us to add a page table and continue
                    paddr_t ptp;
                    volatile riscv_pte_t *ptv = alloc_ptable(&ptp);
                    if (!ptv) {
                        return ERR_NO_MEMORY;
                    }

                    LTRACEF_LEVEL(2, "new ptable table %p, pa %#lx\n", ptv, ptp);

                    // link it in. RMW == 0 is a page table link
                    pte = RISCV_PTE_PPN_TO_PTE(ptp) | RISCV_PTE_V;
                    *ptep = pte;

                    // go one level deeper
                    level--;
                    index = vaddr_to_index(vaddr, level);
                    ptep = ptv + index;
                    break;
            }
        }

        // make sure we didn't decrement level one too many
        DEBUG_ASSERT(level < RISCV_MMU_PT_LEVELS);
    }
    // unreachable
}

// routines to map/unmap/query mappings per address space
int arch_mmu_map(arch_aspace_t *aspace, const vaddr_t _vaddr, paddr_t paddr, uint count, const uint flags) {
    LTRACEF("vaddr %#lx paddr %#lx count %u flags %#x\n", _vaddr, paddr, count, flags);

    DEBUG_ASSERT(aspace);

    if (count == 0) {
        return NO_ERROR;
    }
    // trim the vaddr to the aspace
    if (_vaddr < aspace->base || _vaddr > aspace->base + aspace->size - 1) {
        return ERR_OUT_OF_RANGE;
    }
    // TODO: make sure _vaddr + count * PAGE_SIZE is within the address space

    // construct a local callback for the walker routine that
    // a) tells the walker to build a page table if it's not present
    // b) fills in a terminal page table entry with a page and tells the walker to start over
    auto map_cb = [&paddr, &count, aspace, flags](uint level, uint index, riscv_pte_t *pte, vaddr_t *vaddr, int *err) -> walk_cb_ret {
        LTRACEF("level %u, index %u, pte %#lx, vaddr %#lx [paddr %#lx count %u flags %#x]\n",
                level, index, *pte, *vaddr, paddr, count, flags);

        if ((*pte & RISCV_PTE_V)) {
            // we have hit a valid pte of some kind

            // assert that it's not a page table pointer, which we shouldn't be hitting in the callback
            DEBUG_ASSERT(*pte & RISCV_PTE_PERM_MASK);

            // for now, panic
            if (level > 0) {
                PANIC_UNIMPLEMENTED_MSG("terminal large page entry");
            } else {
                PANIC_UNIMPLEMENTED_MSG("terminal page entry");
            }

            *err = ERR_ALREADY_EXISTS;
            return walk_cb_ret::HALT;
        }

        // hit an open pate table entry
        if (level > 0) {
            // level is > 0, allocate a page table here
            // TODO: optimize by allocating large page here if possible
            return walk_cb_ret::ALLOC_PT;
        }

        // adding a terminal page at level 0
        riscv_pte_t temp_pte = RISCV_PTE_PPN_TO_PTE(paddr);
        temp_pte |= mmu_flags_to_pte(flags);
        temp_pte |= RISCV_PTE_A | RISCV_PTE_D | RISCV_PTE_V;
        temp_pte |= (aspace->flags & ARCH_ASPACE_FLAG_KERNEL) ? RISCV_PTE_G : 0;

        LTRACEF_LEVEL(2, "added new terminal entry: pte %#lx\n", temp_pte);

        // modify what the walker handed us
        *pte = temp_pte;
        *vaddr += PAGE_SIZE;

        // bump our state forward
        paddr += PAGE_SIZE;
        count--;

        // if we're done, tell the caller to commit our changes and either restart the walk or halt
        if (count == 0) {
            return walk_cb_ret::COMMIT_AND_HALT;
        } else {
            return walk_cb_ret::COMMIT_AND_RESTART;
        }
    };

    return riscv_pt_walk(aspace, _vaddr, map_cb);
}

status_t arch_mmu_query(arch_aspace_t *aspace, const vaddr_t _vaddr, paddr_t *paddr, uint *flags) {
    LTRACEF("aspace %p, vaddr %#lx\n", aspace, _vaddr);

    DEBUG_ASSERT(aspace);

    // trim the vaddr to the aspace
    if (_vaddr < aspace->base || _vaddr > aspace->base + aspace->size - 1) {
        return ERR_OUT_OF_RANGE;
    }

    // construct a local callback for the walker routine that
    // a) if it hits a terminal entry construct the flags we want and halt
    // b) all other cases just halt and return ERR_NOT_FOUND
    auto query_cb = [paddr, aspace, flags](uint level, uint index, riscv_pte_t *pte, vaddr_t *vaddr, int *err) -> walk_cb_ret {
        LTRACEF("level %u, index %u, pte %#lx, vaddr %#lx\n", level, index, *pte, *vaddr);

        if (*pte & RISCV_PTE_V) {
            // we have hit a valid pte of some kind
            // assert that it's not a page table pointer, which we shouldn't be hitting in the callback
            DEBUG_ASSERT(*pte & RISCV_PTE_PERM_MASK);

            if (paddr) {
                // extract the ppn
                paddr_t pa = RISCV_PTE_PPN(*pte);
                uintptr_t page_mask = page_mask_per_level(level);

                // add the va offset into the physical address
                *paddr = pa | (*vaddr & page_mask);
                LTRACEF_LEVEL(3, "raw pa %#lx, page_mask %#lx, final pa %#lx\n", pa, page_mask, *paddr);
            }

            if (flags) {
                // compute the flags
                *flags = pte_flags_to_mmu_flags(*pte);
                LTRACEF_LEVEL(3, "computed flags %#x\n", *flags);
            }
            *err = NO_ERROR;
            return walk_cb_ret::HALT;
        } else {
            // any other conditions just stop
            *err = ERR_NOT_FOUND;
            return walk_cb_ret::HALT;
        }
    };

    return riscv_pt_walk(aspace, _vaddr, query_cb);
}

int arch_mmu_unmap(arch_aspace_t *aspace, const vaddr_t _vaddr, const uint _count) {
    LTRACEF("vaddr %#lx count %u\n", _vaddr, _count);

    DEBUG_ASSERT(aspace);

    if (_count == 0) {
        return NO_ERROR;
    }
    // trim the vaddr to the aspace
    if (_vaddr < aspace->base || _vaddr > aspace->base + aspace->size - 1) {
        return ERR_OUT_OF_RANGE;
    }
    // TODO: make sure _vaddr + count * PAGE_SIZE is within the address space

    // construct a local callback for the walker routine that
    // a) if it hits a terminal 4K entry write zeros to it
    // b) if it hits an empty spot continue
    auto count = _count;
    auto unmap_cb = [&count]
        (uint level, uint index, riscv_pte_t *pte, vaddr_t *vaddr, int *err) -> walk_cb_ret {
        LTRACEF("level %u, index %u, pte %#lx, vaddr %#lx\n", level, index, *pte, *vaddr);

        if (*pte & RISCV_PTE_V) {
            // we have hit a valid pte of some kind
            // assert that it's not a page table pointer, which we shouldn't be hitting in the callback
            DEBUG_ASSERT(*pte & RISCV_PTE_PERM_MASK);

            if (level > 0) {
                PANIC_UNIMPLEMENTED_MSG("cannot handle unmapping of large page");
            }

            // zero it out, which should unmap the page
            // TODO: handle freeing upper level page tables
            *pte = 0;
            *vaddr += PAGE_SIZE;
            count--;
            if (count == 0) {
                return walk_cb_ret::COMMIT_AND_HALT;
            } else {
                return walk_cb_ret::COMMIT_AND_RESTART;
            }
        } else {
            // nothing here so skip forward and try the next page
            *vaddr += PAGE_SIZE;
            count--;
            if (count == 0) {
                return walk_cb_ret::HALT;
            } else {
                return walk_cb_ret::RESTART;
            }
        }
    };

    int ret = riscv_pt_walk(aspace, _vaddr, unmap_cb);

    // TLB shootdown the range we've unmapped
    riscv_tlb_flush_vma_range(_vaddr, _count);

    return ret;
}

// load a new user address space context.
// aspace argument NULL should load kernel-only context
void arch_mmu_context_switch(arch_aspace_t *aspace) {
    LTRACEF("aspace %p\n", aspace);

    PANIC_UNIMPLEMENTED;
}

#endif
