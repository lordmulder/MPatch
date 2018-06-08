/*
   LZ4 - Fast LZ compression algorithm
   Copyright (C) 2011-2017, Yann Collet.

   BSD 2-Clause License (http://www.opensource.org/licenses/bsd-license.php)

   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions are
   met:

       * Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.
       * Redistributions in binary form must reproduce the above
   copyright notice, this list of conditions and the following disclaimer
   in the documentation and/or other materials provided with the
   distribution.

   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

   You can contact the author at :
    - LZ4 homepage : http://www.lz4.org
    - LZ4 source repository : https://github.com/lz4/lz4
*/

/*^***************************************************************
*  Export parameters
*****************************************************************/

/*------   Version   ------*/
#define LZ4_VERSION_MAJOR    1    /* for breaking interface changes  */
#define LZ4_VERSION_MINOR    8    /* for new (non-breaking) interface capabilities */
#define LZ4_VERSION_RELEASE  2    /* for tweaks, bug-fixes, or development */


/*-************************************
*  Tuning parameters
**************************************/
/*!
* LZ4_MEMORY_USAGE :
* Memory usage formula : N->2^N Bytes (examples : 10 -> 1KB; 12 -> 4KB ; 16 -> 64KB; 20 -> 1MB; etc.)
* Increasing memory usage improves compression ratio
* Reduced memory usage may improve speed, thanks to cache effect
* Default value is 14, for 16KB, which nicely fits into Intel x86 L1 cache
*/
#ifndef LZ4_MEMORY_USAGE
# define LZ4_MEMORY_USAGE 14
#endif

/*
 * LZ4_HEAPMODE :
 * Select how default compression functions will allocate memory for their hash table,
 * in memory stack (0:default, fastest), or in memory heap (1:requires malloc()).
 */
#ifndef LZ4_HEAPMODE
#  define LZ4_HEAPMODE 0
#endif

/*
 * ACCELERATION_DEFAULT :
 * Select "acceleration" for LZ4_compress_fast() when parameter value <= 0
 */
#define ACCELERATION_DEFAULT 1


/*-************************************
*  CPU Feature Detection
**************************************/
/* LZ4_FORCE_MEMORY_ACCESS
 * By default, access to unaligned memory is controlled by `memcpy()`, which is safe and portable.
 * Unfortunately, on some target/compiler combinations, the generated assembly is sub-optimal.
 * The below switch allow to select different access method for improved performance.
 * Method 0 (default) : use `memcpy()`. Safe and portable.
 * Method 1 : `__packed` statement. It depends on compiler extension (ie, not portable).
 *            This method is safe if your compiler supports it, and *generally* as fast or faster than `memcpy`.
 * Method 2 : direct access. This method is portable but violate C standard.
 *            It can generate buggy code on targets which assembly generation depends on alignment.
 *            But in some circumstances, it's the only known way to get the most performance (ie GCC + ARMv6)
 * See https://fastcompression.blogspot.fr/2015/08/accessing-unaligned-memory.html for details.
 * Prefer these methods in priority order (0 > 1 > 2)
 */
#ifndef LZ4_FORCE_MEMORY_ACCESS   /* can be defined externally */
#  if defined(__GNUC__) && \
  ( defined(__ARM_ARCH_6__) || defined(__ARM_ARCH_6J__) || defined(__ARM_ARCH_6K__) \
  || defined(__ARM_ARCH_6Z__) || defined(__ARM_ARCH_6ZK__) || defined(__ARM_ARCH_6T2__) )
#    define LZ4_FORCE_MEMORY_ACCESS 2
#  elif (defined(__INTEL_COMPILER) && !defined(_WIN32)) || defined(__GNUC__)
#    define LZ4_FORCE_MEMORY_ACCESS 1
#  endif
#endif

/*
 * LZ4_FORCE_SW_BITCOUNT
 * Define this parameter if your target system or compiler does not support hardware bit count
 */
#if defined(_MSC_VER) && defined(_WIN32_WCE)   /* Visual Studio for WinCE doesn't support Hardware bit count */
#  define LZ4_FORCE_SW_BITCOUNT
#endif


/*-************************************
*  Dependency
**************************************/
#define LZ4_STATIC_LINKING_ONLY
#define LZ4_DISABLE_DEPRECATE_WARNINGS /* due to LZ4_decompress_safe_withPrefix64k */
#include "lz4.h"
/* see also "memory routines" below */

#define LZ4_MAX_INPUT_SIZE        0x7E000000   /* 2 113 929 216 bytes */
#define LZ4_COMPRESSBOUND(isize)  ((unsigned)(isize) > (unsigned)LZ4_MAX_INPUT_SIZE ? 0 : (isize) + ((isize)/255) + 16)

typedef union LZ4_stream_u LZ4_stream_t;  /* incomplete type (defined later) */
typedef union LZ4_streamDecode_u LZ4_streamDecode_t;   /* tracking context */

static void LZ4_resetStream(LZ4_stream_t* streamPtr);

#define LZ4_DECODER_RING_BUFFER_SIZE(mbs) (65536 + 14 + (mbs))  /* for static allocation; mbs presumed valid */


/*-************************************
*  Private definitions
**************************************
* Do not use these definitions.
* They are exposed to allow static allocation of `LZ4_stream_t` and `LZ4_streamDecode_t`.
* Using these definitions will expose code to API and/or ABI break in future versions of the library.
**************************************/

#define LZ4_HASHLOG   (LZ4_MEMORY_USAGE-2)
#define LZ4_HASHTABLESIZE (1 << LZ4_MEMORY_USAGE)
#define LZ4_HASH_SIZE_U32 (1 << LZ4_HASHLOG)       /* required as macro for static allocation */

#if defined(__cplusplus) || (defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 199901L)) || (defined(_MSC_VER) && (_MSC_VER >= 1900)) /* C99 */
#include <stdint.h>

typedef struct LZ4_stream_t_internal LZ4_stream_t_internal;
struct LZ4_stream_t_internal {
	uint32_t hashTable[LZ4_HASH_SIZE_U32];
	uint32_t currentOffset;
	uint16_t initCheck;
	uint16_t tableType;
	const uint8_t* dictionary;
	const LZ4_stream_t_internal* dictCtx;
	uint32_t dictSize;
};

typedef struct {
	const uint8_t* externalDict;
	size_t extDictSize;
	const uint8_t* prefixEnd;
	size_t prefixSize;
} LZ4_streamDecode_t_internal;

#else

typedef struct LZ4_stream_t_internal LZ4_stream_t_internal;
struct LZ4_stream_t_internal {
	unsigned int hashTable[LZ4_HASH_SIZE_U32];
	unsigned int currentOffset;
	unsigned short initCheck;
	unsigned short tableType;
	const unsigned char* dictionary;
	const LZ4_stream_t_internal* dictCtx;
	unsigned int dictSize;
};

typedef struct {
	const unsigned char* externalDict;
	size_t extDictSize;
	const unsigned char* prefixEnd;
	size_t prefixSize;
} LZ4_streamDecode_t_internal;

#endif

/*!
* LZ4_stream_t :
* information structure to track an LZ4 stream.
* init this structure before first use.
* note : only use in association with static linking !
*        this definition is not API/ABI safe,
*        it may change in a future version !
*/
#define LZ4_STREAMSIZE_U64 ((1 << (LZ4_MEMORY_USAGE-3)) + 4)
#define LZ4_STREAMSIZE     (LZ4_STREAMSIZE_U64 * sizeof(unsigned long long))
union LZ4_stream_u {
	unsigned long long table[LZ4_STREAMSIZE_U64];
	LZ4_stream_t_internal internal_donotuse;
};  /* previously typedef'd to LZ4_stream_t */

/*!
* LZ4_streamDecode_t :
* information structure to track an LZ4 stream during decompression.
* init this structure  using LZ4_setStreamDecode (or memset()) before first use
* note : only use in association with static linking !
*        this definition is not API/ABI safe,
*        and may change in a future version !
*/
#define LZ4_STREAMDECODESIZE_U64  4
#define LZ4_STREAMDECODESIZE     (LZ4_STREAMDECODESIZE_U64 * sizeof(unsigned long long))
union LZ4_streamDecode_u {
	unsigned long long table[LZ4_STREAMDECODESIZE_U64];
	LZ4_streamDecode_t_internal internal_donotuse;
};   /* previously typedef'd to LZ4_streamDecode_t */


/*-************************************
*  Compiler Options
**************************************/
#ifdef _MSC_VER    /* Visual Studio */
#  include <intrin.h>
#  pragma warning(disable : 4127)        /* disable: C4127: conditional expression is constant */
#  pragma warning(disable : 4293)        /* disable: C4293: too large shift (32-bits) */
#endif  /* _MSC_VER */

#ifndef LZ4_FORCE_INLINE
#  ifdef _MSC_VER    /* Visual Studio */
#    define LZ4_FORCE_INLINE static __forceinline
#  else
#    if defined (__cplusplus) || defined (__STDC_VERSION__) && __STDC_VERSION__ >= 199901L   /* C99 */
#      ifdef __GNUC__
#        define LZ4_FORCE_INLINE static inline __attribute__((always_inline))
#      else
#        define LZ4_FORCE_INLINE static inline
#      endif
#    else
#      define LZ4_FORCE_INLINE static
#    endif /* __STDC_VERSION__ */
#  endif  /* _MSC_VER */
#endif /* LZ4_FORCE_INLINE */

/* LZ4_FORCE_O2_GCC_PPC64LE and LZ4_FORCE_O2_INLINE_GCC_PPC64LE
 * Gcc on ppc64le generates an unrolled SIMDized loop for LZ4_wildCopy,
 * together with a simple 8-byte copy loop as a fall-back path.
 * However, this optimization hurts the decompression speed by >30%,
 * because the execution does not go to the optimized loop
 * for typical compressible data, and all of the preamble checks
 * before going to the fall-back path become useless overhead.
 * This optimization happens only with the -O3 flag, and -O2 generates
 * a simple 8-byte copy loop.
 * With gcc on ppc64le, all of the LZ4_decompress_* and LZ4_wildCopy
 * functions are annotated with __attribute__((optimize("O2"))),
 * and also LZ4_wildCopy is forcibly inlined, so that the O2 attribute
 * of LZ4_wildCopy does not affect the compression speed.
 */
#if defined(__PPC64__) && defined(__LITTLE_ENDIAN__) && defined(__GNUC__)
#  define LZ4_FORCE_O2_GCC_PPC64LE __attribute__((optimize("O2")))
#  define LZ4_FORCE_O2_INLINE_GCC_PPC64LE __attribute__((optimize("O2"))) LZ4_FORCE_INLINE
#else
#  define LZ4_FORCE_O2_GCC_PPC64LE
#  define LZ4_FORCE_O2_INLINE_GCC_PPC64LE static
#endif

#if (defined(__GNUC__) && (__GNUC__ >= 3)) || (defined(__INTEL_COMPILER) && (__INTEL_COMPILER >= 800)) || defined(__clang__)
#  define expect(expr,value)    (__builtin_expect ((expr),(value)) )
#else
#  define expect(expr,value)    (expr)
#endif

#ifndef likely
#define likely(expr)     expect((expr) != 0, 1)
#endif
#ifndef unlikely
#define unlikely(expr)   expect((expr) != 0, 0)
#endif


/*-************************************
*  Memory routines
**************************************/
#include <stdlib.h>   /* malloc, calloc, free */
#define ALLOC(s)          malloc(s)
#define ALLOC_AND_ZERO(s) calloc(1,s)
#define FREEMEM(p)        free(p)
#include <string.h>   /* memset, memcpy */
#define MEM_INIT(p,v,s)   memset((p),(v),(s))


/*-************************************
*  Basic Types
**************************************/
typedef unsigned char       BYTE;
typedef unsigned short      U16;
typedef unsigned int        U32;
typedef   signed int        S32;
typedef unsigned long long  U64;
typedef size_t              uptrval;   /* generally true, except OpenVMS-64 */

#if defined(__x86_64__)
  typedef U64    reg_t;   /* 64-bits in x32 mode */
#else
  typedef size_t reg_t;   /* 32-bits in x32 mode */
#endif


/*-************************************
*  Reading and writing into memory
**************************************/
static unsigned LZ4_isLittleEndian(void)
{
    const union { U32 u; BYTE c[4]; } one = { 1 };   /* don't use static : performance detrimental */
    return one.c[0];
}

#if defined(LZ4_FORCE_MEMORY_ACCESS) && (LZ4_FORCE_MEMORY_ACCESS==2)
/* lie to the compiler about data alignment; use with caution */

static U16 LZ4_read16(const void* memPtr) { return *(const U16*) memPtr; }
static U32 LZ4_read32(const void* memPtr) { return *(const U32*) memPtr; }
static reg_t LZ4_read_ARCH(const void* memPtr) { return *(const reg_t*) memPtr; }

static void LZ4_write16(void* memPtr, U16 value) { *(U16*)memPtr = value; }
static void LZ4_write32(void* memPtr, U32 value) { *(U32*)memPtr = value; }

#elif defined(LZ4_FORCE_MEMORY_ACCESS) && (LZ4_FORCE_MEMORY_ACCESS==1)

/* __pack instructions are safer, but compiler specific, hence potentially problematic for some compilers */
/* currently only defined for gcc and icc */
typedef union { U16 u16; U32 u32; reg_t uArch; } __attribute__((packed)) unalign;

static U16 LZ4_read16(const void* ptr) { return ((const unalign*)ptr)->u16; }
static U32 LZ4_read32(const void* ptr) { return ((const unalign*)ptr)->u32; }
static reg_t LZ4_read_ARCH(const void* ptr) { return ((const unalign*)ptr)->uArch; }

static void LZ4_write16(void* memPtr, U16 value) { ((unalign*)memPtr)->u16 = value; }
static void LZ4_write32(void* memPtr, U32 value) { ((unalign*)memPtr)->u32 = value; }

#else  /* safe and portable access through memcpy() */

static U16 LZ4_read16(const void* memPtr)
{
    U16 val; memcpy(&val, memPtr, sizeof(val)); return val;
}

static U32 LZ4_read32(const void* memPtr)
{
    U32 val; memcpy(&val, memPtr, sizeof(val)); return val;
}

static reg_t LZ4_read_ARCH(const void* memPtr)
{
    reg_t val; memcpy(&val, memPtr, sizeof(val)); return val;
}

static void LZ4_write16(void* memPtr, U16 value)
{
    memcpy(memPtr, &value, sizeof(value));
}

static void LZ4_write32(void* memPtr, U32 value)
{
    memcpy(memPtr, &value, sizeof(value));
}

#endif /* LZ4_FORCE_MEMORY_ACCESS */

static U16 LZ4_readLE16(const void* memPtr)
{
    if (LZ4_isLittleEndian()) {
        return LZ4_read16(memPtr);
    } else {
        const BYTE* p = (const BYTE*)memPtr;
        return (U16)((U16)p[0] + (p[1]<<8));
    }
}

static void LZ4_writeLE16(void* memPtr, U16 value)
{
    if (LZ4_isLittleEndian()) {
        LZ4_write16(memPtr, value);
    } else {
        BYTE* p = (BYTE*)memPtr;
        p[0] = (BYTE) value;
        p[1] = (BYTE)(value>>8);
    }
}

/* customized variant of memcpy, which can overwrite up to 8 bytes beyond dstEnd */
LZ4_FORCE_O2_INLINE_GCC_PPC64LE
void LZ4_wildCopy(void* dstPtr, const void* srcPtr, void* dstEnd)
{
    BYTE* d = (BYTE*)dstPtr;
    const BYTE* s = (const BYTE*)srcPtr;
    BYTE* const e = (BYTE*)dstEnd;

    do { memcpy(d,s,8); d+=8; s+=8; } while (d<e);
}


/*-************************************
*  Common Constants
**************************************/
#define MINMATCH 4

#define WILDCOPYLENGTH 8
#define LASTLITERALS 5
#define MFLIMIT (WILDCOPYLENGTH+MINMATCH)
static const int LZ4_minLength = (MFLIMIT+1);

#define KB *(1 <<10)
#define MB *(1 <<20)
#define GB *(1U<<30)

#define MAXD_LOG 16
#define MAX_DISTANCE ((1 << MAXD_LOG) - 1)

#define ML_BITS  4
#define ML_MASK  ((1U<<ML_BITS)-1)
#define RUN_BITS (8-ML_BITS)
#define RUN_MASK ((1U<<RUN_BITS)-1)


/*-************************************
*  Error detection
**************************************/
#if defined(LZ4_DEBUG) && (LZ4_DEBUG>=1)
#  include <assert.h>
#else
#  ifndef assert
#    define assert(condition) ((void)0)
#  endif
#endif

#define LZ4_STATIC_ASSERT(c)   { enum { LZ4_static_assert = 1/(int)(!!(c)) }; }   /* use after variable declarations */

#if defined(LZ4_DEBUG) && (LZ4_DEBUG>=2)
#  include <stdio.h>
static int g_debuglog_enable = 1;
#  define DEBUGLOG(l, ...) {                                  \
                if ((g_debuglog_enable) && (l<=LZ4_DEBUG)) {  \
                    fprintf(stderr, __FILE__ ": ");           \
                    fprintf(stderr, __VA_ARGS__);             \
                    fprintf(stderr, " \n");                   \
            }   }
#else
#  define DEBUGLOG(l, ...)      {}    /* disabled */
#endif


/*-************************************
*  Common functions
**************************************/
static unsigned LZ4_NbCommonBytes (reg_t val)
{
    if (LZ4_isLittleEndian()) {
        if (sizeof(val)==8) {
#       if defined(_MSC_VER) && defined(_WIN64) && !defined(LZ4_FORCE_SW_BITCOUNT)
            unsigned long r = 0;
            _BitScanForward64( &r, (U64)val );
            return (int)(r>>3);
#       elif (defined(__clang__) || (defined(__GNUC__) && (__GNUC__>=3))) && !defined(LZ4_FORCE_SW_BITCOUNT)
            return (__builtin_ctzll((U64)val) >> 3);
#       else
            static const int DeBruijnBytePos[64] = { 0, 0, 0, 0, 0, 1, 1, 2,
                                                     0, 3, 1, 3, 1, 4, 2, 7,
                                                     0, 2, 3, 6, 1, 5, 3, 5,
                                                     1, 3, 4, 4, 2, 5, 6, 7,
                                                     7, 0, 1, 2, 3, 3, 4, 6,
                                                     2, 6, 5, 5, 3, 4, 5, 6,
                                                     7, 1, 2, 4, 6, 4, 4, 5,
                                                     7, 2, 6, 5, 7, 6, 7, 7 };
            return DeBruijnBytePos[((U64)((val & -(long long)val) * 0x0218A392CDABBD3FULL)) >> 58];
#       endif
        } else /* 32 bits */ {
#       if defined(_MSC_VER) && !defined(LZ4_FORCE_SW_BITCOUNT)
            unsigned long r;
            _BitScanForward( &r, (U32)val );
            return (int)(r>>3);
#       elif (defined(__clang__) || (defined(__GNUC__) && (__GNUC__>=3))) && !defined(LZ4_FORCE_SW_BITCOUNT)
            return (__builtin_ctz((U32)val) >> 3);
#       else
            static const int DeBruijnBytePos[32] = { 0, 0, 3, 0, 3, 1, 3, 0,
                                                     3, 2, 2, 1, 3, 2, 0, 1,
                                                     3, 3, 1, 2, 2, 2, 2, 0,
                                                     3, 1, 2, 0, 1, 0, 1, 1 };
            return DeBruijnBytePos[((U32)((val & -(S32)val) * 0x077CB531U)) >> 27];
#       endif
        }
    } else   /* Big Endian CPU */ {
        if (sizeof(val)==8) {   /* 64-bits */
#       if defined(_MSC_VER) && defined(_WIN64) && !defined(LZ4_FORCE_SW_BITCOUNT)
            unsigned long r = 0;
            _BitScanReverse64( &r, val );
            return (unsigned)(r>>3);
#       elif (defined(__clang__) || (defined(__GNUC__) && (__GNUC__>=3))) && !defined(LZ4_FORCE_SW_BITCOUNT)
            return (__builtin_clzll((U64)val) >> 3);
#       else
            static const U32 by32 = sizeof(val)*4;  /* 32 on 64 bits (goal), 16 on 32 bits.
                Just to avoid some static analyzer complaining about shift by 32 on 32-bits target.
                Note that this code path is never triggered in 32-bits mode. */
            unsigned r;
            if (!(val>>by32)) { r=4; } else { r=0; val>>=by32; }
            if (!(val>>16)) { r+=2; val>>=8; } else { val>>=24; }
            r += (!val);
            return r;
#       endif
        } else /* 32 bits */ {
#       if defined(_MSC_VER) && !defined(LZ4_FORCE_SW_BITCOUNT)
            unsigned long r = 0;
            _BitScanReverse( &r, (unsigned long)val );
            return (unsigned)(r>>3);
#       elif (defined(__clang__) || (defined(__GNUC__) && (__GNUC__>=3))) && !defined(LZ4_FORCE_SW_BITCOUNT)
            return (__builtin_clz((U32)val) >> 3);
#       else
            unsigned r;
            if (!(val>>16)) { r=2; val>>=8; } else { r=0; val>>=24; }
            r += (!val);
            return r;
#       endif
        }
    }
}

#define STEPSIZE sizeof(reg_t)
LZ4_FORCE_INLINE
unsigned LZ4_count(const BYTE* pIn, const BYTE* pMatch, const BYTE* pInLimit)
{
    const BYTE* const pStart = pIn;

    if (likely(pIn < pInLimit-(STEPSIZE-1))) {
        reg_t const diff = LZ4_read_ARCH(pMatch) ^ LZ4_read_ARCH(pIn);
        if (!diff) {
            pIn+=STEPSIZE; pMatch+=STEPSIZE;
        } else {
            return LZ4_NbCommonBytes(diff);
    }   }

    while (likely(pIn < pInLimit-(STEPSIZE-1))) {
        reg_t const diff = LZ4_read_ARCH(pMatch) ^ LZ4_read_ARCH(pIn);
        if (!diff) { pIn+=STEPSIZE; pMatch+=STEPSIZE; continue; }
        pIn += LZ4_NbCommonBytes(diff);
        return (unsigned)(pIn - pStart);
    }

    if ((STEPSIZE==8) && (pIn<(pInLimit-3)) && (LZ4_read32(pMatch) == LZ4_read32(pIn))) { pIn+=4; pMatch+=4; }
    if ((pIn<(pInLimit-1)) && (LZ4_read16(pMatch) == LZ4_read16(pIn))) { pIn+=2; pMatch+=2; }
    if ((pIn<pInLimit) && (*pMatch == *pIn)) pIn++;
    return (unsigned)(pIn - pStart);
}


/*-************************************
*  Local Constants
**************************************/
static const int LZ4_64Klimit = ((64 KB) + (MFLIMIT-1));
static const U32 LZ4_skipTrigger = 6;  /* Increase this value ==> compression run slower on incompressible data */


/*-************************************
*  Local Structures and types
**************************************/
typedef enum { notLimited = 0, limitedOutput = 1, fillOutput = 2 } limitedOutput_directive;
typedef enum { clearedTable = 0, byPtr, byU32, byU16 } tableType_t;

/**
 * This enum distinguishes several different modes of accessing previous
 * content in the stream.
 *
 * - noDict        : There is no preceding content.
 * - withPrefix64k : Table entries up to ctx->dictSize before the current blob
 *                   blob being compressed are valid and refer to the preceding
 *                   content (of length ctx->dictSize), which is available
 *                   contiguously preceding in memory the content currently
 *                   being compressed.
 * - usingExtDict  : Like withPrefix64k, but the preceding content is somewhere
 *                   else in memory, starting at ctx->dictionary with length
 *                   ctx->dictSize.
 * - usingDictCtx  : Like usingExtDict, but everything concerning the preceding
 *                   content is in a separate context, pointed to by
 *                   ctx->dictCtx. ctx->dictionary, ctx->dictSize, and table
 *                   entries in the current context that refer to positions
 *                   preceding the beginning of the current compression are
 *                   ignored. Instead, ctx->dictCtx->dictionary and ctx->dictCtx
 *                   ->dictSize describe the location and size of the preceding
 *                   content, and matches are found by looking in the ctx
 *                   ->dictCtx->hashTable.
 */
typedef enum { noDict = 0, withPrefix64k, usingExtDict, usingDictCtx } dict_directive;
typedef enum { noDictIssue = 0, dictSmall } dictIssue_directive;

typedef enum { endOnOutputSize = 0, endOnInputSize = 1 } endCondition_directive;
typedef enum { full = 0, partial = 1 } earlyEnd_directive;


/*-************************************
*  Local Utils
**************************************/
LZ4_FORCE_INLINE mpatch_LZ4_compressBound(int isize)  { return LZ4_COMPRESSBOUND(isize); }


/*-******************************
*  Compression functions
********************************/
static U32 LZ4_hash4(U32 sequence, tableType_t const tableType)
{
    if (tableType == byU16)
        return ((sequence * 2654435761U) >> ((MINMATCH*8)-(LZ4_HASHLOG+1)));
    else
        return ((sequence * 2654435761U) >> ((MINMATCH*8)-LZ4_HASHLOG));
}

static U32 LZ4_hash5(U64 sequence, tableType_t const tableType)
{
    static const U64 prime5bytes = 889523592379ULL;
    static const U64 prime8bytes = 11400714785074694791ULL;
    const U32 hashLog = (tableType == byU16) ? LZ4_HASHLOG+1 : LZ4_HASHLOG;
    if (LZ4_isLittleEndian())
        return (U32)(((sequence << 24) * prime5bytes) >> (64 - hashLog));
    else
        return (U32)(((sequence >> 24) * prime8bytes) >> (64 - hashLog));
}

LZ4_FORCE_INLINE U32 LZ4_hashPosition(const void* const p, tableType_t const tableType)
{
    if ((sizeof(reg_t)==8) && (tableType != byU16)) return LZ4_hash5(LZ4_read_ARCH(p), tableType);
    return LZ4_hash4(LZ4_read32(p), tableType);
}

static void LZ4_putIndexOnHash(U32 idx, U32 h, void* tableBase, tableType_t const tableType)
{
    switch (tableType)
    {
    default: /* fallthrough */
    case clearedTable: /* fallthrough */
    case byPtr: { /* illegal! */ assert(0); return; }
    case byU32: { U32* hashTable = (U32*) tableBase; hashTable[h] = idx; return; }
    case byU16: { U16* hashTable = (U16*) tableBase; assert(idx < 65536); hashTable[h] = (U16)idx; return; }
    }
}

static void LZ4_putPositionOnHash(const BYTE* p, U32 h,
                                  void* tableBase, tableType_t const tableType,
                            const BYTE* srcBase)
{
    switch (tableType)
    {
    case clearedTable: { /* illegal! */ assert(0); return; }
    case byPtr: { const BYTE** hashTable = (const BYTE**)tableBase; hashTable[h] = p; return; }
    case byU32: { U32* hashTable = (U32*) tableBase; hashTable[h] = (U32)(p-srcBase); return; }
    case byU16: { U16* hashTable = (U16*) tableBase; hashTable[h] = (U16)(p-srcBase); return; }
    }
}

LZ4_FORCE_INLINE void LZ4_putPosition(const BYTE* p, void* tableBase, tableType_t tableType, const BYTE* srcBase)
{
    U32 const h = LZ4_hashPosition(p, tableType);
    LZ4_putPositionOnHash(p, h, tableBase, tableType, srcBase);
}

/* LZ4_getIndexOnHash() :
 * Index of match position registered in hash table.
 * hash position must be calculated by using base+index, or dictBase+index.
 * Assumption 1 : only valid if tableType == byU32 or byU16.
 * Assumption 2 : h is presumed valid (within limits of hash table)
 */
static U32 LZ4_getIndexOnHash(U32 h, const void* tableBase, tableType_t tableType)
{
    LZ4_STATIC_ASSERT(LZ4_MEMORY_USAGE > 2);
    if (tableType == byU32) {
        const U32* const hashTable = (const U32*) tableBase;
        assert(h < (1U << (LZ4_MEMORY_USAGE-2)));
        return hashTable[h];
    }
    if (tableType == byU16) {
        const U16* const hashTable = (const U16*) tableBase;
        assert(h < (1U << (LZ4_MEMORY_USAGE-1)));
        return hashTable[h];
    }
    assert(0); return 0;  /* forbidden case */
}

static const BYTE* LZ4_getPositionOnHash(U32 h, const void* tableBase, tableType_t tableType, const BYTE* srcBase)
{
    if (tableType == byPtr) { const BYTE* const* hashTable = (const BYTE* const*) tableBase; return hashTable[h]; }
    if (tableType == byU32) { const U32* const hashTable = (const U32*) tableBase; return hashTable[h] + srcBase; }
    { const U16* const hashTable = (const U16*) tableBase; return hashTable[h] + srcBase; }   /* default, to ensure a return */
}

LZ4_FORCE_INLINE const BYTE* LZ4_getPosition(const BYTE* p,
                                             const void* tableBase, tableType_t tableType,
                                             const BYTE* srcBase)
{
    U32 const h = LZ4_hashPosition(p, tableType);
    return LZ4_getPositionOnHash(h, tableBase, tableType, srcBase);
}

/** LZ4_compress_generic() :
    inlined, to ensure branches are decided at compilation time */
LZ4_FORCE_INLINE int LZ4_compress_generic(
                 LZ4_stream_t_internal* const cctx,
                 const char* const source,
                 char* const dest,
                 const int inputSize,
                 int *inputConsumed, /* only written when outputLimited == fillOutput */
                 const int maxOutputSize,
                 const limitedOutput_directive outputLimited,
                 const tableType_t tableType,
                 const dict_directive dictDirective,
                 const dictIssue_directive dictIssue,
                 const U32 acceleration)
{
    const BYTE* ip = (const BYTE*) source;

    U32 const startIndex = cctx->currentOffset;
    const BYTE* base = (const BYTE*) source - startIndex;
    const BYTE* lowLimit;

    const LZ4_stream_t_internal* dictCtx = (const LZ4_stream_t_internal*) cctx->dictCtx;
    const BYTE* const dictionary =
        dictDirective == usingDictCtx ? dictCtx->dictionary : cctx->dictionary;
    const U32 dictSize =
        dictDirective == usingDictCtx ? dictCtx->dictSize : cctx->dictSize;
    const U32 dictDelta = (dictDirective == usingDictCtx) ? startIndex - dictCtx->currentOffset : 0;   /* make indexes in dictCtx comparable with index in current context */

    int const maybe_extMem = (dictDirective == usingExtDict) || (dictDirective == usingDictCtx);
    U32 const prefixIdxLimit = startIndex - dictSize;   /* used when dictDirective == dictSmall */
    const BYTE* const dictEnd = dictionary + dictSize;
    const BYTE* anchor = (const BYTE*) source;
    const BYTE* const iend = ip + inputSize;
    const BYTE* const mflimitPlusOne = iend - MFLIMIT + 1;
    const BYTE* const matchlimit = iend - LASTLITERALS;

    /* the dictCtx currentOffset is indexed on the start of the dictionary,
     * while a dictionary in the current context precedes the currentOffset */
    const BYTE* dictBase = dictDirective == usingDictCtx ?
        dictionary + dictSize - dictCtx->currentOffset :
        dictionary + dictSize - startIndex;

    BYTE* op = (BYTE*) dest;
    BYTE* const olimit = op + maxOutputSize;

    U32 offset = 0;
    U32 forwardH;

    DEBUGLOG(5, "LZ4_compress_generic: srcSize=%i, tableType=%u", inputSize, tableType);
    /* Init conditions */
    if (outputLimited == fillOutput && maxOutputSize < 1) return 0; /* Impossible to store anything */
    if ((U32)inputSize > (U32)LZ4_MAX_INPUT_SIZE) return 0;   /* Unsupported inputSize, too large (or negative) */
    if ((tableType == byU16) && (inputSize>=LZ4_64Klimit)) return 0;  /* Size too large (not within 64K limit) */
    if (tableType==byPtr) assert(dictDirective==noDict);      /* only supported use case with byPtr */
    assert(acceleration >= 1);

    lowLimit = (const BYTE*)source - (dictDirective == withPrefix64k ? dictSize : 0);

    /* Update context state */
    if (dictDirective == usingDictCtx) {
        /* Subsequent linked blocks can't use the dictionary. */
        /* Instead, they use the block we just compressed. */
        cctx->dictCtx = NULL;
        cctx->dictSize = (U32)inputSize;
    } else {
        cctx->dictSize += (U32)inputSize;
    }
    cctx->currentOffset += (U32)inputSize;
    cctx->tableType = (U16)tableType;

    if (inputSize<LZ4_minLength) goto _last_literals;        /* Input too small, no compression (all literals) */

    /* First Byte */
    LZ4_putPosition(ip, cctx->hashTable, tableType, base);
    ip++; forwardH = LZ4_hashPosition(ip, tableType);

    /* Main Loop */
    for ( ; ; ) {
        const BYTE* match;
        BYTE* token;

        /* Find a match */
        if (tableType == byPtr) {
            const BYTE* forwardIp = ip;
            unsigned step = 1;
            unsigned searchMatchNb = acceleration << LZ4_skipTrigger;
            do {
                U32 const h = forwardH;
                ip = forwardIp;
                forwardIp += step;
                step = (searchMatchNb++ >> LZ4_skipTrigger);

                if (unlikely(forwardIp > mflimitPlusOne)) goto _last_literals;
                assert(ip < mflimitPlusOne);

                match = LZ4_getPositionOnHash(h, cctx->hashTable, tableType, base);
                forwardH = LZ4_hashPosition(forwardIp, tableType);
                LZ4_putPositionOnHash(ip, h, cctx->hashTable, tableType, base);

            } while ( (match+MAX_DISTANCE < ip)
                   || (LZ4_read32(match) != LZ4_read32(ip)) );

        } else {   /* byU32, byU16 */

            const BYTE* forwardIp = ip;
            unsigned step = 1;
            unsigned searchMatchNb = acceleration << LZ4_skipTrigger;
            do {
                U32 const h = forwardH;
                U32 const current = (U32)(forwardIp - base);
                U32 matchIndex = LZ4_getIndexOnHash(h, cctx->hashTable, tableType);
                assert(matchIndex <= current);
                assert(forwardIp - base < (ptrdiff_t)(2 GB - 1));
                ip = forwardIp;
                forwardIp += step;
                step = (searchMatchNb++ >> LZ4_skipTrigger);

                if (unlikely(forwardIp > mflimitPlusOne)) goto _last_literals;
                assert(ip < mflimitPlusOne);

                if (dictDirective == usingDictCtx) {
                    if (matchIndex < startIndex) {
                        /* there was no match, try the dictionary */
                        assert(tableType == byU32);
                        matchIndex = LZ4_getIndexOnHash(h, dictCtx->hashTable, byU32);
                        match = dictBase + matchIndex;
                        matchIndex += dictDelta;   /* make dictCtx index comparable with current context */
                        lowLimit = dictionary;
                    } else {
                        match = base + matchIndex;
                        lowLimit = (const BYTE*)source;
                    }
                } else if (dictDirective==usingExtDict) {
                    if (matchIndex < startIndex) {
                        DEBUGLOG(7, "extDict candidate: matchIndex=%5u  <  startIndex=%5u", matchIndex, startIndex);
                        assert(startIndex - matchIndex >= MINMATCH);
                        match = dictBase + matchIndex;
                        lowLimit = dictionary;
                    } else {
                        match = base + matchIndex;
                        lowLimit = (const BYTE*)source;
                    }
                } else {   /* single continuous memory segment */
                    match = base + matchIndex;
                }
                forwardH = LZ4_hashPosition(forwardIp, tableType);
                LZ4_putIndexOnHash(current, h, cctx->hashTable, tableType);

                if ((dictIssue == dictSmall) && (matchIndex < prefixIdxLimit)) continue;    /* match outside of valid area */
                assert(matchIndex < current);
                if ((tableType != byU16) && (matchIndex+MAX_DISTANCE < current)) continue;  /* too far */
                if (tableType == byU16) assert((current - matchIndex) <= MAX_DISTANCE);     /* too_far presumed impossible with byU16 */

                if (LZ4_read32(match) == LZ4_read32(ip)) {
                    if (maybe_extMem) offset = current - matchIndex;
                    break;   /* match found */
                }

            } while(1);
        }

        /* Catch up */
        while (((ip>anchor) & (match > lowLimit)) && (unlikely(ip[-1]==match[-1]))) { ip--; match--; }

        /* Encode Literals */
        {   unsigned const litLength = (unsigned)(ip - anchor);
            token = op++;
            if ((outputLimited == limitedOutput) &&  /* Check output buffer overflow */
                (unlikely(op + litLength + (2 + 1 + LASTLITERALS) + (litLength/255) > olimit)))
                return 0;
            if ((outputLimited == fillOutput) &&
                (unlikely(op + (litLength+240)/255 /* litlen */ + litLength /* literals */ + 2 /* offset */ + 1 /* token */ + MFLIMIT - MINMATCH /* min last literals so last match is <= end - MFLIMIT */ > olimit))) {
                op--;
                goto _last_literals;
            }
            if (litLength >= RUN_MASK) {
                int len = (int)litLength-RUN_MASK;
                *token = (RUN_MASK<<ML_BITS);
                for(; len >= 255 ; len-=255) *op++ = 255;
                *op++ = (BYTE)len;
            }
            else *token = (BYTE)(litLength<<ML_BITS);

            /* Copy Literals */
            LZ4_wildCopy(op, anchor, op+litLength);
            op+=litLength;
            DEBUGLOG(6, "seq.start:%i, literals=%u, match.start:%i",
                        (int)(anchor-(const BYTE*)source), litLength, (int)(ip-(const BYTE*)source));
        }

_next_match:
        /* at this stage, the following variables must be correctly set :
         * - ip : at start of LZ operation
         * - match : at start of previous pattern occurence; can be within current prefix, or within extDict
         * - offset : if maybe_ext_memSegment==1 (constant)
         * - lowLimit : must be == dictionary to mean "match is within extDict"; must be == source otherwise
         * - token and *token : position to write 4-bits for match length; higher 4-bits for literal length supposed already written
         */

        if ((outputLimited == fillOutput) &&
            (op + 2 /* offset */ + 1 /* token */ + MFLIMIT - MINMATCH /* min last literals so last match is <= end - MFLIMIT */ > olimit)) {
            /* the match was too close to the end, rewind and go to last literals */
            op = token;
            goto _last_literals;
        }

        /* Encode Offset */
        if (maybe_extMem) {   /* static test */
            DEBUGLOG(6, "             with offset=%u  (ext if > %i)", offset, (int)(ip - (const BYTE*)source));
            assert(offset <= MAX_DISTANCE && offset > 0);
            LZ4_writeLE16(op, (U16)offset); op+=2;
        } else  {
            DEBUGLOG(6, "             with offset=%u  (same segment)", (U32)(ip - match));
            assert(ip-match <= MAX_DISTANCE);
            LZ4_writeLE16(op, (U16)(ip - match)); op+=2;
        }

        /* Encode MatchLength */
        {   unsigned matchCode;

            if ( (dictDirective==usingExtDict || dictDirective==usingDictCtx)
              && (lowLimit==dictionary) /* match within extDict */ ) {
                const BYTE* limit = ip + (dictEnd-match);
                assert(dictEnd > match);
                if (limit > matchlimit) limit = matchlimit;
                matchCode = LZ4_count(ip+MINMATCH, match+MINMATCH, limit);
                ip += MINMATCH + matchCode;
                if (ip==limit) {
                    unsigned const more = LZ4_count(limit, (const BYTE*)source, matchlimit);
                    matchCode += more;
                    ip += more;
                }
                DEBUGLOG(6, "             with matchLength=%u starting in extDict", matchCode+MINMATCH);
            } else {
                matchCode = LZ4_count(ip+MINMATCH, match+MINMATCH, matchlimit);
                ip += MINMATCH + matchCode;
                DEBUGLOG(6, "             with matchLength=%u", matchCode+MINMATCH);
            }

            if ((outputLimited) &&    /* Check output buffer overflow */
                (unlikely(op + (1 + LASTLITERALS) + (matchCode>>8) > olimit)) ) {
                if (outputLimited == limitedOutput)
                  return 0;
                if (outputLimited == fillOutput) {
                    /* Match description too long : reduce it */
                    U32 newMatchCode = 15 /* in token */ - 1 /* to avoid needing a zero byte */ + ((U32)(olimit - op) - 2 - 1 - LASTLITERALS) * 255;
                    ip -= matchCode - newMatchCode;
                    matchCode = newMatchCode;
                }
            }
            if (matchCode >= ML_MASK) {
                *token += ML_MASK;
                matchCode -= ML_MASK;
                LZ4_write32(op, 0xFFFFFFFF);
                while (matchCode >= 4*255) {
                    op+=4;
                    LZ4_write32(op, 0xFFFFFFFF);
                    matchCode -= 4*255;
                }
                op += matchCode / 255;
                *op++ = (BYTE)(matchCode % 255);
            } else
                *token += (BYTE)(matchCode);
        }

        anchor = ip;

        /* Test end of chunk */
        if (ip >= mflimitPlusOne) break;

        /* Fill table */
        LZ4_putPosition(ip-2, cctx->hashTable, tableType, base);

        /* Test next position */
        if (tableType == byPtr) {

            match = LZ4_getPosition(ip, cctx->hashTable, tableType, base);
            LZ4_putPosition(ip, cctx->hashTable, tableType, base);
            if ( (match+MAX_DISTANCE >= ip)
              && (LZ4_read32(match) == LZ4_read32(ip)) )
            { token=op++; *token=0; goto _next_match; }

        } else {   /* byU32, byU16 */

            U32 const h = LZ4_hashPosition(ip, tableType);
            U32 const current = (U32)(ip-base);
            U32 matchIndex = LZ4_getIndexOnHash(h, cctx->hashTable, tableType);
            assert(matchIndex < current);
            if (dictDirective == usingDictCtx) {
                if (matchIndex < startIndex) {
                    /* there was no match, try the dictionary */
                    matchIndex = LZ4_getIndexOnHash(h, dictCtx->hashTable, byU32);
                    match = dictBase + matchIndex;
                    lowLimit = dictionary;   /* required for match length counter */
                    matchIndex += dictDelta;
                } else {
                    match = base + matchIndex;
                    lowLimit = (const BYTE*)source;  /* required for match length counter */
                }
            } else if (dictDirective==usingExtDict) {
                if (matchIndex < startIndex) {
                    match = dictBase + matchIndex;
                    lowLimit = dictionary;   /* required for match length counter */
                } else {
                    match = base + matchIndex;
                    lowLimit = (const BYTE*)source;   /* required for match length counter */
                }
            } else {   /* single memory segment */
                match = base + matchIndex;
            }
            LZ4_putIndexOnHash(current, h, cctx->hashTable, tableType);
            assert(matchIndex < current);
            if ( ((dictIssue==dictSmall) ? (matchIndex >= prefixIdxLimit) : 1)
              && ((tableType==byU16) ? 1 : (matchIndex+MAX_DISTANCE >= current))
              && (LZ4_read32(match) == LZ4_read32(ip)) ) {
                token=op++;
                *token=0;
                if (maybe_extMem) offset = current - matchIndex;
                DEBUGLOG(6, "seq.start:%i, literals=%u, match.start:%i",
                            (int)(anchor-(const BYTE*)source), 0, (int)(ip-(const BYTE*)source));
                goto _next_match;
            }
        }

        /* Prepare next loop */
        forwardH = LZ4_hashPosition(++ip, tableType);

    }

_last_literals:
    /* Encode Last Literals */
    {   size_t lastRun = (size_t)(iend - anchor);
        if ( (outputLimited) &&  /* Check output buffer overflow */
            (op + lastRun + 1 + ((lastRun+255-RUN_MASK)/255) > olimit)) {
            if (outputLimited == fillOutput) {
                /* adapt lastRun to fill 'dst' */
                lastRun  = (olimit-op) - 1;
                lastRun -= (lastRun+240)/255;
            }
            if (outputLimited == limitedOutput)
                return 0;
        }
        if (lastRun >= RUN_MASK) {
            size_t accumulator = lastRun - RUN_MASK;
            *op++ = RUN_MASK << ML_BITS;
            for(; accumulator >= 255 ; accumulator-=255) *op++ = 255;
            *op++ = (BYTE) accumulator;
        } else {
            *op++ = (BYTE)(lastRun<<ML_BITS);
        }
        memcpy(op, anchor, lastRun);
        ip = anchor + lastRun;
        op += lastRun;
    }

    if (outputLimited == fillOutput) {
        *inputConsumed = (int) (((const char*)ip)-source);
    }
    DEBUGLOG(5, "LZ4_compress_generic: compressed %i bytes into %i bytes", inputSize, (int)(((char*)op) - dest));
    return (int)(((char*)op) - dest);
}


LZ4_FORCE_INLINE int LZ4_compress_fast_extState(void* state, const char* source, char* dest, int inputSize, int maxOutputSize, int acceleration)
{
    LZ4_stream_t_internal* ctx = &((LZ4_stream_t*)state)->internal_donotuse;
    if (acceleration < 1) acceleration = ACCELERATION_DEFAULT;
    LZ4_resetStream((LZ4_stream_t*)state);
    if (maxOutputSize >= mpatch_LZ4_compressBound(inputSize)) {
        if (inputSize < LZ4_64Klimit) {
            return LZ4_compress_generic(ctx, source, dest, inputSize, NULL, 0, notLimited, byU16, noDict, noDictIssue, acceleration);
        } else {
            const tableType_t tableType = ((sizeof(void*)==4) && ((uptrval)source > MAX_DISTANCE)) ? byPtr : byU32;
            return LZ4_compress_generic(ctx, source, dest, inputSize, NULL, 0, notLimited, tableType, noDict, noDictIssue, acceleration);
        }
    } else {
        if (inputSize < LZ4_64Klimit) {;
            return LZ4_compress_generic(ctx, source, dest, inputSize, NULL, maxOutputSize, limitedOutput, byU16, noDict, noDictIssue, acceleration);
        } else {
            const tableType_t tableType = ((sizeof(void*)==4) && ((uptrval)source > MAX_DISTANCE)) ? byPtr : byU32;
            return LZ4_compress_generic(ctx, source, dest, inputSize, NULL, maxOutputSize, limitedOutput, tableType, noDict, noDictIssue, acceleration);
        }
    }
}

LZ4_FORCE_INLINE int LZ4_compress_fast(const char* source, char* dest, int inputSize, int maxOutputSize, int acceleration)
{
    int result;
#if (LZ4_HEAPMODE)
    LZ4_stream_t* ctxPtr = ALLOC(sizeof(LZ4_stream_t));   /* malloc-calloc always properly aligned */
    if (ctxPtr == NULL) return 0;
#else
    LZ4_stream_t ctx;
    LZ4_stream_t* const ctxPtr = &ctx;
#endif
    result = LZ4_compress_fast_extState(ctxPtr, source, dest, inputSize, maxOutputSize, acceleration);

#if (LZ4_HEAPMODE)
    FREEMEM(ctxPtr);
#endif
    return result;
}

LZ4_FORCE_INLINE int LZ4_compress_default(const char* source, char* dest, int inputSize, int maxOutputSize)
{
    return LZ4_compress_fast(source, dest, inputSize, maxOutputSize, 1);
}


/*-******************************
*  Streaming functions
********************************/

static void LZ4_resetStream (LZ4_stream_t* LZ4_stream)
{
    DEBUGLOG(5, "LZ4_resetStream (ctx:%p)", LZ4_stream);
    MEM_INIT(LZ4_stream, 0, sizeof(LZ4_stream_t));
}


/*-*****************************
*  Decompression functions
*******************************/
/*! LZ4_decompress_generic() :
 *  This generic decompression function covers all use cases.
 *  It shall be instantiated several times, using different sets of directives.
 *  Note that it is important for performance that this function really get inlined,
 *  in order to remove useless branches during compilation optimization.
 */
LZ4_FORCE_O2_GCC_PPC64LE
LZ4_FORCE_INLINE int LZ4_decompress_generic(
                 const char* const src,
                 char* const dst,
                 int srcSize,
                 int outputSize,         /* If endOnInput==endOnInputSize, this value is `dstCapacity` */

                 int endOnInput,         /* endOnOutputSize, endOnInputSize */
                 int partialDecoding,    /* full, partial */
                 int targetOutputSize,   /* only used if partialDecoding==partial */
                 int dict,               /* noDict, withPrefix64k, usingExtDict */
                 const BYTE* const lowPrefix,  /* always <= dst, == dst when no prefix */
                 const BYTE* const dictStart,  /* only if dict==usingExtDict */
                 const size_t dictSize         /* note : = 0 if noDict */
                 )
{
    const BYTE* ip = (const BYTE*) src;
    const BYTE* const iend = ip + srcSize;

    BYTE* op = (BYTE*) dst;
    BYTE* const oend = op + outputSize;
    BYTE* cpy;
    BYTE* oexit = op + targetOutputSize;

    const BYTE* const dictEnd = (const BYTE*)dictStart + dictSize;
    const unsigned inc32table[8] = {0, 1, 2,  1,  0,  4, 4, 4};
    const int      dec64table[8] = {0, 0, 0, -1, -4,  1, 2, 3};

    const int safeDecode = (endOnInput==endOnInputSize);
    const int checkOffset = ((safeDecode) && (dictSize < (int)(64 KB)));

    /* Set up the "end" pointers for the shortcut. */
    const BYTE* const shortiend = iend - (endOnInput ? 14 : 8) /*maxLL*/ - 2 /*offset*/;
    const BYTE* const shortoend = oend - (endOnInput ? 14 : 8) /*maxLL*/ - 18 /*maxML*/;

    DEBUGLOG(5, "LZ4_decompress_generic (srcSize:%i)", srcSize);

    /* Special cases */
    if ((partialDecoding) && (oexit > oend-MFLIMIT)) oexit = oend-MFLIMIT;                      /* targetOutputSize too high => just decode everything */
    if ((endOnInput) && (unlikely(outputSize==0))) return ((srcSize==1) && (*ip==0)) ? 0 : -1;  /* Empty output buffer */
    if ((!endOnInput) && (unlikely(outputSize==0))) return (*ip==0?1:-1);
    if ((endOnInput) && unlikely(srcSize==0)) return -1;

    /* Main Loop : decode sequences */
    while (1) {
        const BYTE* match;
        size_t offset;

        unsigned const token = *ip++;
        size_t length = token >> ML_BITS; /* literal length */

        assert(!endOnInput || ip <= iend); /* ip < iend before the increment */

        /* A two-stage shortcut for the most common case:
         * 1) If the literal length is 0..14, and there is enough space,
         * enter the shortcut and copy 16 bytes on behalf of the literals
         * (in the fast mode, only 8 bytes can be safely copied this way).
         * 2) Further if the match length is 4..18, copy 18 bytes in a similar
         * manner; but we ensure that there's enough space in the output for
         * those 18 bytes earlier, upon entering the shortcut (in other words,
         * there is a combined check for both stages).
         */
        if ( (endOnInput ? length != RUN_MASK : length <= 8)
            /* strictly "less than" on input, to re-enter the loop with at least one byte */
          && likely((endOnInput ? ip < shortiend : 1) & (op <= shortoend)) ) {
            /* Copy the literals */
            memcpy(op, ip, endOnInput ? 16 : 8);
            op += length; ip += length;

            /* The second stage: prepare for match copying, decode full info.
             * If it doesn't work out, the info won't be wasted. */
            length = token & ML_MASK; /* match length */
            offset = LZ4_readLE16(ip); ip += 2;
            match = op - offset;

            /* Do not deal with overlapping matches. */
            if ( (length != ML_MASK)
              && (offset >= 8)
              && (dict==withPrefix64k || match >= lowPrefix) ) {
                /* Copy the match. */
                memcpy(op + 0, match + 0, 8);
                memcpy(op + 8, match + 8, 8);
                memcpy(op +16, match +16, 2);
                op += length + MINMATCH;
                /* Both stages worked, load the next token. */
                continue;
            }

            /* The second stage didn't work out, but the info is ready.
             * Propel it right to the point of match copying. */
            goto _copy_match;
        }

        /* decode literal length */
        if (length == RUN_MASK) {
            unsigned s;
            if (unlikely(endOnInput ? ip >= iend-RUN_MASK : 0)) goto _output_error;   /* overflow detection */
            do {
                s = *ip++;
                length += s;
            } while ( likely(endOnInput ? ip<iend-RUN_MASK : 1) & (s==255) );
            if ((safeDecode) && unlikely((uptrval)(op)+length<(uptrval)(op))) goto _output_error;   /* overflow detection */
            if ((safeDecode) && unlikely((uptrval)(ip)+length<(uptrval)(ip))) goto _output_error;   /* overflow detection */
        }

        /* copy literals */
        cpy = op+length;
        if ( ((endOnInput) && ((cpy>(partialDecoding?oexit:oend-MFLIMIT)) || (ip+length>iend-(2+1+LASTLITERALS))) )
            || ((!endOnInput) && (cpy>oend-WILDCOPYLENGTH)) )
        {
            if (partialDecoding) {
                if (cpy > oend) goto _output_error;                           /* Error : write attempt beyond end of output buffer */
                if ((endOnInput) && (ip+length > iend)) goto _output_error;   /* Error : read attempt beyond end of input buffer */
            } else {
                if ((!endOnInput) && (cpy != oend)) goto _output_error;       /* Error : block decoding must stop exactly there */
                if ((endOnInput) && ((ip+length != iend) || (cpy > oend))) goto _output_error;   /* Error : input must be consumed */
            }
            memcpy(op, ip, length);
            ip += length;
            op += length;
            break;     /* Necessarily EOF, due to parsing restrictions */
        }
        LZ4_wildCopy(op, ip, cpy);
        ip += length; op = cpy;

        /* get offset */
        offset = LZ4_readLE16(ip); ip+=2;
        match = op - offset;

        /* get matchlength */
        length = token & ML_MASK;

_copy_match:
        if ((checkOffset) && (unlikely(match + dictSize < lowPrefix))) goto _output_error;   /* Error : offset outside buffers */
        LZ4_write32(op, (U32)offset);   /* costs ~1%; silence an msan warning when offset==0 */

        if (length == ML_MASK) {
            unsigned s;
            do {
                s = *ip++;
                if ((endOnInput) && (ip > iend-LASTLITERALS)) goto _output_error;
                length += s;
            } while (s==255);
            if ((safeDecode) && unlikely((uptrval)(op)+length<(uptrval)op)) goto _output_error;   /* overflow detection */
        }
        length += MINMATCH;

        /* check external dictionary */
        if ((dict==usingExtDict) && (match < lowPrefix)) {
            if (unlikely(op+length > oend-LASTLITERALS)) goto _output_error;   /* doesn't respect parsing restriction */

            if (length <= (size_t)(lowPrefix-match)) {
                /* match can be copied as a single segment from external dictionary */
                memmove(op, dictEnd - (lowPrefix-match), length);
                op += length;
            } else {
                /* match encompass external dictionary and current block */
                size_t const copySize = (size_t)(lowPrefix-match);
                size_t const restSize = length - copySize;
                memcpy(op, dictEnd - copySize, copySize);
                op += copySize;
                if (restSize > (size_t)(op-lowPrefix)) {  /* overlap copy */
                    BYTE* const endOfMatch = op + restSize;
                    const BYTE* copyFrom = lowPrefix;
                    while (op < endOfMatch) *op++ = *copyFrom++;
                } else {
                    memcpy(op, lowPrefix, restSize);
                    op += restSize;
            }   }
            continue;
        }

        /* copy match within block */
        cpy = op + length;
        if (unlikely(offset<8)) {
            op[0] = match[0];
            op[1] = match[1];
            op[2] = match[2];
            op[3] = match[3];
            match += inc32table[offset];
            memcpy(op+4, match, 4);
            match -= dec64table[offset];
        } else { memcpy(op, match, 8); match+=8; }
        op += 8;

        if (unlikely(cpy>oend-12)) {
            BYTE* const oCopyLimit = oend-(WILDCOPYLENGTH-1);
            if (cpy > oend-LASTLITERALS) goto _output_error;    /* Error : last LASTLITERALS bytes must be literals (uncompressed) */
            if (op < oCopyLimit) {
                LZ4_wildCopy(op, match, oCopyLimit);
                match += oCopyLimit - op;
                op = oCopyLimit;
            }
            while (op<cpy) *op++ = *match++;
        } else {
            memcpy(op, match, 8);
            if (length>16) LZ4_wildCopy(op+8, match+8, cpy);
        }
        op = cpy;   /* correction */
    }

    /* end of decoding */
    if (endOnInput)
       return (int) (((char*)op)-dst);     /* Nb of output bytes decoded */
    else
       return (int) (((const char*)ip)-src);   /* Nb of input bytes read */

    /* Overflow error detected */
_output_error:
    return (int) (-(((const char*)ip)-src))-1;
}

/*===== Instantiate the API decoding functions. =====*/

LZ4_FORCE_O2_GCC_PPC64LE
static int LZ4_decompress_safe(const char* source, char* dest, int compressedSize, int maxDecompressedSize)
{
    return LZ4_decompress_generic(source, dest, compressedSize, maxDecompressedSize,
                                  endOnInputSize, full, 0, noDict,
                                  (BYTE*)dest, NULL, 0);
}


/*-************************************
*  Wrapper functions
**************************************/

uint_fast32_t mpatch_compress(const uint8_t *const input, const uint_fast32_t input_size, uint8_t *const output, const uint_fast32_t output_capacity)
{
	if (input && output && (input_size <= LZ4_MAX_INPUT_SIZE) && (output_capacity <= INT_MAX))
	{
		const int result = LZ4_compress_default((const char*)input, (char*)output, (int)input_size, (int)output_capacity);
		return (uint_fast32_t)((result >= 0) ? (int)result : 0);
	}
	return 0U;
}

uint_fast32_t mpatch_decompress(const uint8_t *const input, const uint_fast32_t input_size, uint8_t *const output, const uint_fast32_t output_capacity)
{
	if (input && output && (input_size <= INT_MAX) && (output_capacity <= INT_MAX))
	{
		const int result = LZ4_decompress_safe((const char*)input, (char*)output, (int)input_size, (int)output_capacity);
		return (uint_fast32_t)((result >= 0) ? (int)result : 0);
	}
	return 0U;
}

uint_fast32_t mpatch_compress_bound(const uint_fast32_t input_size)
{
	if (input_size <= LZ4_MAX_INPUT_SIZE)
	{
		return (uint_fast32_t)mpatch_LZ4_compressBound((int)input_size);
	}
	return 0U;
}
