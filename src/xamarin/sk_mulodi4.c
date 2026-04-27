// [mono/skia fork patch] Local __mulodi4 for 32-bit Linux cross-compile.
//
// dng_sdk in m147 (revision dbe0a676450d, bumped from c8d0c9b1d16b in
// m134 by upstream chromium roll) uses `__builtin_smul_overflow` for
// 64-bit signed multiply-with-overflow checks. On 32-bit ABIs (i386,
// arm) clang-13 lowers that to a call to the libgcc helper
// `__mulodi4(di_int, di_int, int *overflow)`. Bullseye's
// libgcc-10-dev-{i386,armhf}-cross ships libgcc.a with __muldi3,
// __mulvdi3, __mulsc3, __multf3 etc. but no __mulodi4 — that helper
// only lives in compiler-rt's libclang_rt.builtins-<arch>.a, which
// libclang-common-13-dev provides for i386 (alongside the host x86_64)
// but not arm, so reaching for compiler-rt would force a per-arch
// special case. Defining __mulodi4 ourselves works uniformly on every
// 32-bit Linux target we ship and leaves 64-bit targets unaffected.
//
// Implementation copied verbatim (with the int_lib.h dependency
// inlined) from upstream compiler-rt:
//   compiler-rt/lib/builtins/mulodi4.c, revision 19.1.0.
// Licensed under Apache-2.0 WITH LLVM-exception.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#if defined(__i386__) || defined(__arm__)

typedef long long di_int;

#define CHAR_BIT 8

di_int __mulodi4(di_int a, di_int b, int *overflow) {
    const int N = (int)(sizeof(di_int) * CHAR_BIT);
    const di_int MIN = (di_int)1 << (N - 1);
    const di_int MAX = ~MIN;
    *overflow = 0;
    di_int result = a * b;
    if (a == MIN) {
        if (b != 0 && b != 1)
            *overflow = 1;
        return result;
    }
    if (b == MIN) {
        if (a != 0 && a != 1)
            *overflow = 1;
        return result;
    }
    di_int sa = a >> (N - 1);
    di_int abs_a = (a ^ sa) - sa;
    di_int sb = b >> (N - 1);
    di_int abs_b = (b ^ sb) - sb;
    if (abs_a < 2 || abs_b < 2)
        return result;
    if (sa == sb) {
        if (abs_a > MAX / abs_b)
            *overflow = 1;
    } else {
        if (abs_a > MIN / -abs_b)
            *overflow = 1;
    }
    return result;
}

#endif  // __i386__ || __arm__
