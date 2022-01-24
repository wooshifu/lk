//
// Copyright (c) 2022 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT
#pragma once

#include <dev/bus/pci.h>
#include <lk/cpp.h>
#include <lk/list.h>
#include <kernel/spinlock.h>
#include <kernel/thread.h>

#include "ahci_hw.h"

class ahci_port;

class ahci {
public:
    ahci();
    ~ahci();

    DISALLOW_COPY_ASSIGN_AND_MOVE(ahci);

    int get_unit_num() const { return unit_; }

    // initialize the device at passed in pci location.
    // probe each of the active ports for disks and save
    // a list of them for future probing.
    status_t init_device(pci_location_t loc);

    // start a thread and probe all of the disks found
    status_t start_disk_probe();

private:
    friend class ahci_port;

    uint32_t read_reg(ahci_reg reg);
    void write_reg(ahci_reg reg, uint32_t val);

    uint32_t read_port_reg(uint port, ahci_port_reg reg);
    void write_port_reg(uint port, ahci_port_reg reg, uint32_t val);

    handler_return irq_handler();
    void disk_probe_worker();

    // counter of configured deices
    static volatile int global_count_;
    int unit_ = 0;

    // main spinlock
    spin_lock_t lock_ = SPIN_LOCK_INITIAL_VALUE;

    // configuration
    pci_location_t loc_ = {};
    void *abar_regs_ = nullptr;

    // array of ports
    ahci_port *ports_[32] = {};

    // list of disks we've found
    thread_t *disk_probe_thread_ = nullptr;
    list_node disk_list_ = LIST_INITIAL_VALUE(disk_list_);
};

inline uint32_t ahci::read_reg(ahci_reg reg) {
    volatile uint32_t *r = (volatile uint32_t *)((uintptr_t)abar_regs_ + (size_t)reg);

    return *r;
}

inline void ahci::write_reg(ahci_reg reg, uint32_t val) {
    volatile uint32_t *r = (volatile uint32_t *)((uintptr_t)abar_regs_ + (size_t)reg);

    *r = val;
}

inline uint32_t ahci::read_port_reg(uint port, ahci_port_reg reg) {
    volatile uint32_t *r = (volatile uint32_t *)((uintptr_t)abar_regs_ + (size_t)reg + 0x100 + 0x80 * port);

    return *r;
}

inline void ahci::write_port_reg(uint port, ahci_port_reg reg, uint32_t val) {
    volatile uint32_t *r = (volatile uint32_t *)((uintptr_t)abar_regs_ + (size_t)reg + 0x100 + 0x80 * port);

    *r = val;
}


