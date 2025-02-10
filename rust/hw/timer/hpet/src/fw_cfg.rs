// Copyright (C) 2024 Intel Corporation.
// Author(s): Zhao Liu <zhai1.liu@intel.com>
// SPDX-License-Identifier: GPL-2.0-or-later

use std::ptr::addr_of_mut;

use qemu_api::{cell::bql_locked, impl_zeroable, zeroable::Zeroable};

/// Each `HPETState` represents a Event Timer Block. The v1 spec supports
/// up to 8 blocks. QEMU only uses 1 block (in PC machine).
const HPET_MAX_NUM_EVENT_TIMER_BLOCK: usize = 8;

#[repr(C, packed)]
#[derive(Copy, Clone, Default)]
pub struct HPETFwEntry {
    pub event_timer_block_id: u32,
    pub address: u64,
    pub min_tick: u16,
    pub page_prot: u8,
}
impl_zeroable!(HPETFwEntry);

#[repr(C, packed)]
#[derive(Copy, Clone, Default)]
pub struct HPETFwConfig {
    pub count: u8,
    pub hpet: [HPETFwEntry; HPET_MAX_NUM_EVENT_TIMER_BLOCK],
}
impl_zeroable!(HPETFwConfig);

#[allow(non_upper_case_globals)]
#[no_mangle]
pub static mut hpet_fw_cfg: HPETFwConfig = HPETFwConfig {
    count: u8::MAX,
    ..Zeroable::ZERO
};

impl HPETFwConfig {
    pub(crate) fn assign_hpet_id() -> usize {
        assert!(bql_locked());
        // SAFETY: all accesses go through these methods, which guarantee
        // that the accesses are protected by the BQL.
        let mut fw_cfg = unsafe { *addr_of_mut!(hpet_fw_cfg) };

        if fw_cfg.count == u8::MAX {
            // first instance
            fw_cfg.count = 0;
        }

        if fw_cfg.count == 8 {
            // TODO: Add error binding: error_setg()
            panic!("Only 8 instances of HPET is allowed");
        }

        let id: usize = fw_cfg.count.into();
        fw_cfg.count += 1;
        id
    }

    pub(crate) fn update_hpet_cfg(hpet_id: usize, timer_block_id: u32, address: u64) {
        assert!(bql_locked());
        // SAFETY: all accesses go through these methods, which guarantee
        // that the accesses are protected by the BQL.
        let mut fw_cfg = unsafe { *addr_of_mut!(hpet_fw_cfg) };

        fw_cfg.hpet[hpet_id].event_timer_block_id = timer_block_id;
        fw_cfg.hpet[hpet_id].address = address;
    }
}
