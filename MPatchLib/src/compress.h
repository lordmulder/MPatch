/* ---------------------------------------------------------------------------------------------- */
/* MPatchLib - simple patch and compression library                                               */
/* Copyright(c) 2018 LoRd_MuldeR <mulder2@gmx.de>                                                 */
/*                                                                                                */
/* Permission is hereby granted, free of charge, to any person obtaining a copy of this software  */
/* and associated documentation files (the "Software"), to deal in the Software without           */
/* restriction, including without limitation the rights to use, copy, modify, merge, publish,     */
/* distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the  */
/* Software is furnished to do so, subject to the following conditions:                           */
/*                                                                                                */
/* The above copyright notice and this permission notice shall be included in all copies or       */
/* substantial portions of the Software.                                                          */
/*                                                                                                */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING  */
/* BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND     */
/* NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,   */
/* DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, */
/* OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.        */
/* ---------------------------------------------------------------------------------------------- */

#ifndef _INC_MPATCH_COMPRESS
#define _INC_MPATCH_COMPRESS

#include <stdint.h>
#include <stdbool.h>

typedef struct _mpatch_cctx_t mpatch_cctx_t;

//Compress
bool mpatch_compress_enc_init(mpatch_cctx_t **const cctx, uint8_t *const header_out, const uint_fast32_t header_capacity, uint_fast32_t *const header_size, const uint_fast32_t max_input_size);
const uint8_t *mpatch_compress_enc_next(mpatch_cctx_t *const cctx, const uint8_t *const message_in, const uint_fast32_t message_size, uint_fast32_t *const compressed_size);
bool mpatch_compress_enc_exit(mpatch_cctx_t *const cctx, uint8_t *const footer_out, const uint_fast32_t footer_capacity, uint_fast32_t *const footer_size);
bool mpatch_compress_enc_free(mpatch_cctx_t **const cctx);

#endif /*_INC_MPATCH_COMPRESS*/