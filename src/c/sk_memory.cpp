/*
 * Copyright 2026 Microsoft Corporation. All rights reserved.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Native memory accounting for SkiaSharp. The upstream allocator
 * (src/ports/SkMemory_malloc.cpp) is instrumented with a handful of
 * extern "C" hook calls into the helpers in this file; this file holds
 * all of the bookkeeping state and exposes the public C-API.
 *
 * Why a separate file: the only code that lives in the upstream allocator
 * is the hook call sites (a few lines, easy to maintain across Skia
 * milestone bumps). Everything else -- the atomic counter, the threshold
 * watermark, the CAS-claimed crossing logic, malloc_usable_size
 * dispatching, the C-API -- lives here in the SkiaSharp C shim where
 * customizations belong.
 */

#include "include/c/sk_memory.h"
#include "include/private/base/SkFeatures.h"

#include <atomic>
#include <cstdint>
#include <cstdlib>

#if defined(SK_BUILD_FOR_MAC) || defined(SK_BUILD_FOR_IOS)
#include <malloc/malloc.h>
#elif defined(SK_BUILD_FOR_ANDROID) || defined(SK_BUILD_FOR_UNIX)
#include <malloc.h>
#elif defined(SK_BUILD_FOR_WIN)
#include <malloc.h>
#endif

namespace {

std::atomic<int64_t>                  g_allocated{0};
std::atomic<sk_memory_threshold_proc> g_threshold_cb{nullptr};
std::atomic<int64_t>                  g_threshold{0};
std::atomic<int64_t>                  g_last_notified{0};

inline void notify_if_threshold_crossed(int64_t current) {
    // Fast-path bail when nobody is listening.
    auto cb = g_threshold_cb.load(std::memory_order_acquire);
    if (cb == nullptr) {
        return;
    }
    int64_t threshold = g_threshold.load(std::memory_order_relaxed);
    if (threshold <= 0) {
        return;
    }
    int64_t last = g_last_notified.load(std::memory_order_relaxed);
    int64_t diff = current - last;
    if (diff < 0) {
        diff = -diff;
    }
    if (diff < threshold) {
        return;
    }
    // Claim this crossing: only one thread succeeds in advancing
    // last_notified, the rest bail and let the winner fire the callback.
    // The managed side re-reads the current counter on its own, so a
    // missed inter-thread delta is reconciled on the next crossing.
    if (g_last_notified.compare_exchange_strong(
            last, current, std::memory_order_relaxed)) {
        cb();
    }
}

}  // namespace

// ----------------------------------------------------------------------
// Internal hooks called from src/ports/SkMemory_malloc.cpp
// (declared there as `extern "C"` forward decls).
// ----------------------------------------------------------------------

extern "C" size_t sk_memory_internal_size_of(void* p) {
    if (p == nullptr) {
        return 0;
    }
#if defined(SK_BUILD_FOR_MAC) || defined(SK_BUILD_FOR_IOS)
    return malloc_size(p);
#elif defined(SK_BUILD_FOR_ANDROID) || defined(SK_BUILD_FOR_UNIX)
    return malloc_usable_size(p);
#elif defined(SK_BUILD_FOR_WIN)
    return _msize(p);
#else
    return 0;
#endif
}

extern "C" void sk_memory_internal_account_delta(int64_t bytes) {
    if (bytes == 0) {
        return;
    }
    int64_t prev = g_allocated.fetch_add(bytes, std::memory_order_relaxed);
    notify_if_threshold_crossed(prev + bytes);
}

// ----------------------------------------------------------------------
// Public C-API.
// ----------------------------------------------------------------------

uint64_t sk_memory_get_native_allocated(void) {
    int64_t v = g_allocated.load(std::memory_order_relaxed);
    return v < 0 ? 0 : static_cast<uint64_t>(v);
}

void sk_memory_set_threshold_callback(
        sk_memory_threshold_proc callback, uint64_t threshold_bytes) {
    int64_t clamped = threshold_bytes > static_cast<uint64_t>(INT64_MAX)
        ? INT64_MAX
        : static_cast<int64_t>(threshold_bytes);
    g_threshold.store(clamped, std::memory_order_relaxed);
    // Reset the watermark on (re)installation so the first crossing fires
    // relative to the counter value at the time the callback was attached.
    g_last_notified.store(g_allocated.load(std::memory_order_relaxed),
                          std::memory_order_relaxed);
    g_threshold_cb.store(callback, std::memory_order_release);
}
