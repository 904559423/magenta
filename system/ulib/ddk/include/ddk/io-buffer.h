// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/compiler.h>
#include <magenta/types.h>

#include <stdbool.h>
#include <stddef.h>

__BEGIN_CDECLS;

typedef struct {
    mx_handle_t vmo_handle;
    size_t size;
    mx_off_t offset;
    void* virt;
    mx_paddr_t* phys_addrs;
} io_buffer_t;

// flags for io_buffer_init
enum {
    IO_BUFFER_RO = (1 << 0),
    IO_BUFFER_WO = (1 << 1),
    IO_BUFFER_RW = IO_BUFFER_RO | IO_BUFFER_WO,
    IO_BUFFER_CONTIG = (1 << 2),
};

// Initializes a new io_buffer
mx_status_t io_buffer_init(io_buffer_t* buffer, size_t size, uint32_t flags);

// Initializes an io_buffer base on an existing VMO.
// duplicates the provided vmo_handle - does not take ownership
mx_status_t io_buffer_init_vmo(io_buffer_t* buffer, mx_handle_t vmo_handle,
                               mx_off_t offset, uint32_t flags);

// copies an io_buffer. clone gets duplicate of the source's vmo_handle
mx_status_t io_buffer_clone(io_buffer_t* src, io_buffer_t* dest);

mx_status_t io_buffer_cache_op(io_buffer_t* buffer, const uint32_t op,
                               const mx_off_t offset, const size_t size);
// Releases an io_buffer
void io_buffer_release(io_buffer_t* buffer);

static inline bool io_buffer_is_valid(io_buffer_t* buffer) {
    return (buffer->vmo_handle != MX_HANDLE_INVALID);
}

static inline void* io_buffer_virt(io_buffer_t* buffer) {
    return buffer->virt + buffer->offset;
}

mx_paddr_t io_buffer_phys(io_buffer_t* buffer, mx_off_t offset);

__END_CDECLS;
