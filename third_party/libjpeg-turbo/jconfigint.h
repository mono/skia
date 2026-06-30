/*
 * jconfigint.h - Pre-baked for libjpeg-turbo 3.1.4.1
 *
 * Generated from src/jconfigint.h.in with platform-adaptive macros.
 * See BUILD.gn header comment for rationale.
 */

/* libjpeg-turbo build number */
#define BUILD  ""

/* How to hide global symbols. */
#ifndef HIDDEN
#if defined(__GNUC__)
#define HIDDEN  __attribute__((visibility("hidden")))
#else
#define HIDDEN
#endif
#endif

/* Compiler's inline keyword */
#undef inline

/* How to obtain function inlining. */
#ifndef INLINE
#if defined(__GNUC__)
#define INLINE  inline __attribute__((always_inline))
#elif defined(_MSC_VER)
#define INLINE  __forceinline
#else
#define INLINE
#endif
#endif

/* How to obtain thread-local storage */
#if defined(_MSC_VER) && (defined(_WIN32) || defined(_WIN64))
#define THREAD_LOCAL  __declspec(thread)
#else
#define THREAD_LOCAL  __thread
#endif

/* Define to the full name of this package. */
#define PACKAGE_NAME  "libjpeg-turbo"

/* Version number of package */
#define VERSION  "3.1.4.1"

/* The size of `size_t', as computed by sizeof. */
#if defined(_WIN64) || defined(__LP64__) || defined(__x86_64__) || defined(__aarch64__) || defined(__ppc64__)
#define SIZEOF_SIZE_T  8
#else
#define SIZEOF_SIZE_T  4
#endif

/* Define if your compiler has __builtin_ctzl() and sizeof(unsigned long) == sizeof(size_t).
 * On Windows LLP64 (including clang-cl), sizeof(long)==4 but sizeof(size_t)==8,
 * so __builtin_ctzl would truncate. Only enable when sizes match. */
#if defined(__GNUC__) && defined(__SIZEOF_LONG__) && defined(__SIZEOF_SIZE_T__) && \
    (__SIZEOF_LONG__ == __SIZEOF_SIZE_T__)
#define HAVE_BUILTIN_CTZL
#endif

/* Define to 1 if you have the <intrin.h> header file. */
#if defined(_MSC_VER)
#define HAVE_INTRIN_H  1
#endif

#if defined(_MSC_VER) && defined(HAVE_INTRIN_H)
#if (SIZEOF_SIZE_T == 8)
#define HAVE_BITSCANFORWARD64
#elif (SIZEOF_SIZE_T == 4)
#define HAVE_BITSCANFORWARD
#endif
#endif

#if defined(__has_attribute)
#if __has_attribute(fallthrough)
#define FALLTHROUGH  __attribute__((fallthrough));
#else
#define FALLTHROUGH
#endif
#else
#define FALLTHROUGH
#endif

/*
 * Define BITS_IN_JSAMPLE as either
 *   8   for 8-bit sample values (the usual setting)
 *   12  for 12-bit sample values
 * Only 8 and 12 are legal data precisions for lossy JPEG according to the
 * JPEG standard, and the IJG code does not support anything else!
 */

#ifndef BITS_IN_JSAMPLE
#define BITS_IN_JSAMPLE  8      /* use 8 or 12 */
#endif

/*
 * Arithmetic coding and SIMD support are controlled by BUILD.gn defines,
 * not by this header. The BUILD.gn file conditionally defines WITH_SIMD,
 * C_ARITH_CODING_SUPPORTED, and D_ARITH_CODING_SUPPORTED as appropriate
 * per-target and per-platform.
 *
 * Safety: upstream restricts these features to 8-bit precision only.
 * Defensively undefine them for 12/16-bit builds in case BUILD.gn
 * defines are ever flattened across targets.
 */
#if BITS_IN_JSAMPLE != 8
#undef C_ARITH_CODING_SUPPORTED
#undef D_ARITH_CODING_SUPPORTED
#undef WITH_SIMD
#endif
