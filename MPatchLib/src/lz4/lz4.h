/*
 *  LZ4 - Fast LZ compression algorithm
 *  Header File
 *  Copyright (C) 2011-2017, Yann Collet.

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
#ifndef LZ4_H_2983827168210
#define LZ4_H_2983827168210

/* --- Dependency --- */
#include <stddef.h>   /* size_t */
#include <stdint.h>

/**
  Introduction

  LZ4 is lossless compression algorithm, providing compression speed at 400 MB/s per core,
  scalable with multi-cores CPU. It features an extremely fast decoder, with speed in
  multiple GB/s per core, typically reaching RAM speed limits on multi-core systems.

  The LZ4 compression library provides in-memory compression and decompression functions.
  Compression can be done in:
    - a single step (described as Simple Functions)
    - a single step, reusing a context (described in Advanced Functions)
    - unbounded multiple steps (described as Streaming compression)

  lz4.h provides block compression functions. It gives full buffer control to user.
  Decompressing an lz4-compressed block also requires metadata (such as compressed size).
  Each application is free to encode such metadata in whichever way it wants.

  An additional format, called LZ4 frame specification (doc/lz4_Frame_format.md),
  take care of encoding standard metadata alongside LZ4-compressed blocks.
  If your application requires interoperability, it's recommended to use it.
  A library is provided to take care of it, see lz4frame.h.
*/

/*-************************************
*  Simple Functions
**************************************/
/*! LZ4_compress_default() :
    Compresses 'srcSize' bytes from buffer 'src'
    into already allocated 'dst' buffer of size 'dstCapacity'.
    Compression is guaranteed to succeed if 'dstCapacity' >= LZ4_compressBound(srcSize).
    It also runs faster, so it's a recommended setting.
    If the function cannot compress 'src' into a more limited 'dst' budget,
    compression stops *immediately*, and the function result is zero.
    Note : as a consequence, 'dst' content is not valid.
    Note 2 : This function is protected against buffer overflow scenarios (never writes outside 'dst' buffer, nor read outside 'source' buffer).
        srcSize : max supported value is LZ4_MAX_INPUT_SIZE.
        dstCapacity : size of buffer 'dst' (which must be already allocated)
        return  : the number of bytes written into buffer 'dst' (necessarily <= dstCapacity)
                  or 0 if compression fails */
uint_fast32_t mpatch_compress(const uint8_t *const input, const uint_fast32_t input_size, uint8_t *const output, const uint_fast32_t output_capacity);

/*! LZ4_decompress_safe() :
    compressedSize : is the exact complete size of the compressed block.
    dstCapacity : is the size of destination buffer, which must be already allocated.
    return : the number of bytes decompressed into destination buffer (necessarily <= dstCapacity)
             If destination buffer is not large enough, decoding will stop and output an error code (negative value).
             If the source stream is detected malformed, the function will stop decoding and return a negative result.
             This function is protected against malicious data packets.
*/
uint_fast32_t mpatch_decompress(const uint8_t *const input, const uint_fast32_t input_size, uint8_t *const output, const uint_fast32_t output_capacity);

/*!
LZ4_compressBound() :
    Provides the maximum size that LZ4 compression may output in a "worst case" scenario (input data not compressible)
    This function is primarily useful for memory allocation purposes (destination buffer size).
    Macro LZ4_COMPRESSBOUND() is also provided for compilation-time evaluation (stack memory allocation for example).
    Note that LZ4_compress_default() compresses faster when dstCapacity is >= LZ4_compressBound(srcSize)
        inputSize  : max supported value is LZ4_MAX_INPUT_SIZE
        return : maximum output size in a "worst case" scenario
              or 0, if input size is incorrect (too large or negative)
*/
uint_fast32_t mpatch_compress_bound(const uint_fast32_t input_size);

#endif /* LZ4_H_2983827168210 */
