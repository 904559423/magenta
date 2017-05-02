// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <assert.h>
#include <err.h>
#include <iovec.h>
#include <kernel/cmdline.h>
#include <kernel/vm.h>
#include <magenta/types.h>
#include <mxtl/algorithm.h>
#include <platform.h>
#include <stdio.h>
#include <string.h>
#include <trace.h>
#include <unittest.h>

#include <new.h>
#include "memory_limit.h"

#define LOCAL_TRACE 0

// This class wraps an iovec_t so that we do math on iov_base without needing
// to constantly cast to/from void pointers.
struct IoVectorWrapper {
public:
    IoVectorWrapper(iovec_t& io_vector)
        : base(*reinterpret_cast<uintptr_t*>(io_vector.iov_base)), len(io_vector.iov_len) {
        base = 0;
        len = 0;
    }

    uintptr_t& base;
    size_t& len;
};

/* Checks if a memory limit has been imposed on the system by the boot
 * command line. NO_ERROR indicates a valid limit is being returned,
 * whereas ERR_NOT_SUPPORTED indicates there is no such restriction
 * on the kernel.
 */
mx_status_t mem_limit_get(uint64_t* limit) {
    uint64_t _limit;

    if (!limit) {
        return ERR_INVALID_ARGS;
    }

    _limit = cmdline_get_uint64("kernel.memory-limit", 0);

    if (_limit == 0) {
        return ERR_NOT_SUPPORTED;
    }

    *limit = _limit;
    return NO_ERROR;
}

// Minimal validation is done here because it isn't the responsibility of this
// library to ensure the kernel and ramdisk aren't overlapping in some manner.
mx_status_t mem_limit_init(mem_limit_cfg_t* cfg) {
    if (!cfg) {
        return ERR_INVALID_ARGS;
    }

    return mem_limit_get(&cfg->memory_limit);
}

/* This will take a contiguous range of memory and return io vectors
 * corresponding to the arenas that needed to be carved out due to placement of
 * the kernel, placement of the ramdisk, and any memory limits being imposed
 * upon the system. The size of the arena is subtracted from the value passed in
 * by 'limit'
 */
mx_status_t mem_limit_apply(mem_limit_cfg_t* cfg,
                            uintptr_t range_base,
                            size_t range_size,
                            iovec_t* iovs,
                            size_t iov_cnt) {
    // We need at most 2 iovec_ts to handle both the kernel and ramdisk in any
    // memory layout within a single range if we grow/shrink sub-ranges.
    if (!cfg || !iovs || iov_cnt < 2) {
        return ERR_INVALID_ARGS;
    }

    if (cfg->memory_limit == 0) {
        /* If our limit has been reached this range can be skipped */
        return NO_ERROR;
    }

    // Wrappers around the iovec_ts to keep the logic below simple.
    IoVectorWrapper kernel_iov = IoVectorWrapper(iovs[0]);
    IoVectorWrapper ramdisk_iov = IoVectorWrapper(iovs[1]);
    // Convenience values for the ranges.
    uintptr_t range_end = range_base + range_size;
    uintptr_t k_base = cfg->kernel_base;
    size_t k_size = cfg->kernel_end - cfg->kernel_base;
    uintptr_t k_end = cfg->kernel_end;
    uintptr_t r_base = cfg->ramdisk_base;
    size_t r_size = cfg->ramdisk_sz;
    uintptr_t r_end = r_base + r_size;

    /* The entire range fits into memory */
    if (range_size <= cfg->memory_limit) {
        kernel_iov.base = range_base;
        kernel_iov.len = range_size;
        cfg->memory_limit -= range_size;
        return NO_ERROR;
    }

    /* This is where things get more complicated if we found the kernel_iov. On both
     * x86 and ARM the kernel and ramdisk will exist in the same memory range.
     * On x86 this is the lowmem region below 4GB based on where UEFI's page
     * allocations placed it. For ARM, it depends on the platform's bootrom, but
     * the important detail is that they both should be in the same contiguous
     * block of DRAM. Either way, we know the kernel + bss needs to be included
     * in memory regardless so that's the first priority.
     *
     * If we booted in the first place then we can assume we have enough space
     * for ourselves. k_low/k_high/r_high represent spans as follows:
     * |base|<k_low>[kernel]<k_high>[ramdisk]<r_high>|_end|
     *
     * Alternatively, if there is no ramdisk then the situation looks more like:
     * |base|<k_low>[kernel]< k_high >[end]
     *
     * TODO: when kernel relocation exists this will need to handle the ramdisk
     * being before the kernel_iov.
     */
    if (range_base <= k_base && k_base < range_end) {
        uint64_t k_low = 0, k_high = 0, r_high = 0, tmp = 0;

        k_low = k_base - range_base;
        k_high = range_end;

        // First set up the kernel
        LTRACEF("kernel base %" PRIxPTR " size %" PRIxPTR "\n", k_base, k_size);
        kernel_iov.base = k_base;
        kernel_iov.len = k_size;
        cfg->memory_limit -= k_size;

        // Add the ramdisk_iov. Truncate if we must and warn the user if it happens
        if (r_size) {
            LTRACEF("ramdisk base %" PRIxPTR " size %" PRIxPTR "\n", r_base, r_size);
            tmp = mxtl::min(cfg->memory_limit, r_size);
            if (tmp != r_size) {
                printf("Warning: ramdisk has been truncated from %zu to %zu"
                       "bytes due to cmdline memory limits\n",
                       r_size, cfg->memory_limit);
            }
            ramdisk_iov.base = r_base;
            ramdisk_iov.len = tmp;
            cfg->memory_limit -= tmp;

            k_high = r_base - k_end;
            r_high = range_end - r_end;
        }

        // We've created our kernel and ramdisk vecs, and now we expand them as
        // much as possible within the imposed limit, starting with the k_high
        // gap between the kernel and ramdisk_iov.
        tmp = mxtl::min(cfg->memory_limit, k_high);
        if (tmp) {
            LTRACEF("growing kernel iov by %zu bytes.\n", tmp);
            kernel_iov.len += tmp;
            cfg->memory_limit -= tmp;
        }

        // Handle space between the start of the range and the kernel base
        tmp = mxtl::min(cfg->memory_limit, k_low);
        if (tmp) {
            kernel_iov.base -= tmp;
            kernel_iov.len += tmp;
            cfg->memory_limit -= tmp;
            LTRACEF("moving kernel iov base back by %zu to %" PRIxPTR ".\n",
                    tmp, kernel_iov.base);
        }

        // If we have no ramdisk then k_high will have encompassed this region,
        // but this is also accounted for by r_high being 0.
        tmp = mxtl::min(cfg->memory_limit, r_high);
        if (tmp) {
            LTRACEF("growing ramdisk iov by %zu bytes.\n", tmp);
            ramdisk_iov.len += tmp;
            cfg->memory_limit -= tmp;
        }

        // Collapse the kernel and ramdisk into a single io vector if they're
        // adjacent to each other.
        if ((kernel_iov.base + kernel_iov.len) == ramdisk_iov.base) {
            kernel_iov.len += ramdisk_iov.len;
            ramdisk_iov.base = 0;
            ramdisk_iov.len = 0;
            LTRACEF("Merging kernel and ramdisk iovs into a single iov base %" PRIxPTR
                    " size %zu\n",
                    kernel_iov.base, kernel_iov.len);
        }
    } else {
        // No kernel here, presumably no ramdisk_iov. Just add what we can.
        uint64_t tmp = mxtl::min(cfg->memory_limit, range_size);
        kernel_iov.base = range_base;
        kernel_iov.len = tmp;
        cfg->memory_limit -= tmp;
    }

    return NO_ERROR;
}

static bool ml_kernel_eor(void* context) {
    BEGIN_TEST;

    printf("Test\n");

    uintptr_t base = 0x0;
    size_t size = 128 * MB;
    size_t k_size = 2 * MB;
    iovec_t* vecs = (iovec_t*)malloc(sizeof(iovec_t) * 2);

    mem_limit_cfg_t cfg = {
        .kernel_base = (base + size) - k_size,
        .kernel_end = (base + size),
        .ramdisk_base = 0,
        .ramdisk_sz = 0,
        .memory_limit = 256 * MB,
    };

    REQUIRE_NEQ(vecs, nullptr, "allocation");
    REQUIRE_EQ(NO_ERROR, mem_limit_apply(&cfg, base, size, vecs, 2), "apply");
    EXPECT_EQ(vecs[0].iov_len, k_size, "check size");
    EXPECT_EQ(reinterpret_cast<uintptr_t>(vecs[1].iov_base), 0u, "No second vector");
    EXPECT_EQ(reinterpret_cast<uintptr_t>(vecs[1].iov_len), 0u, "No second vector");

    END_TEST;
}

#define ML_UNITTEST(fname) UNITTEST(#fname, fname)
UNITTEST_START_TESTCASE(memlimit_tests)
ML_UNITTEST(ml_kernel_eor)
UNITTEST_END_TESTCASE(memlimit_tests, "memlim_tests", "Memory limit tests", nullptr, nullptr);
#undef ML_UNITTEST
