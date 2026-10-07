/*
 * Copyright 2026 Microsoft Corporation. All rights reserved.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef sk_memory_DEFINED
#define sk_memory_DEFINED

#include "include/c/sk_types.h"

SK_C_PLUS_PLUS_BEGIN_GUARD

// Total number of bytes currently held by Skia's allocator
// (sk_malloc/sk_calloc/sk_realloc allocations, measured by
// malloc_usable_size / _msize / malloc_size). Updated atomically on every
// allocation and free.
SK_C_API uint64_t sk_memory_get_native_allocated(void);

// Threshold-crossing notification. When installed, `callback` is invoked
// on the allocator's thread whenever the total native allocation delta
// since the last notification crosses +/- threshold_bytes.
//
// The callback fires from within the allocator hot path and may run on any
// thread; it MUST be reentrancy-safe and MUST NOT call back into Skia or
// take any lock that could be held by the caller. Typical implementation:
// flag a managed work item and return immediately.
//
// Pass callback=NULL or threshold_bytes=0 to disable notifications.
typedef void (*sk_memory_threshold_proc)(void);
SK_C_API void sk_memory_set_threshold_callback(
    sk_memory_threshold_proc callback,
    uint64_t threshold_bytes);

SK_C_PLUS_PLUS_END_GUARD

#endif
