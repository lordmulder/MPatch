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

#include "compress.h"

#include <stdlib.h>
#include <malloc.h>
#include <memory.h>

#include <zlib.h>

struct _mpatch_cctx_t
{
	z_stream stream;
	uint_fast32_t max_chunk_size, buffer_size;
	uint8_t *buffer;
};

/* ======================================================================= */
/* Compress functions                                                      */
/* ======================================================================= */

bool mpatch_compress_enc_init(mpatch_cctx_t **const cctx, const uint_fast32_t max_chunk_size)
{
	//Check output pointer
	if (!cctx)
	{
		return false;
	}

	//Check parameter
	if (max_chunk_size < 1U)
	{
		*cctx = NULL;
		return false;
	}

	//Alloc context
	if (!(*cctx = (mpatch_cctx_t*)calloc(1U, sizeof(mpatch_cctx_t))))
	{
		return false;
	}

	//Create deflate stream
	if (deflateInit2(&(*cctx)->stream, 9, Z_DEFLATED, -15, 9, Z_DEFAULT_STRATEGY) != Z_OK)
	{
		free(*cctx);
		*cctx = NULL;
		return false;
	}

	//Alloc buffer
	(*cctx)->buffer_size = deflateBound(&(*cctx)->stream, ((*cctx)->max_chunk_size = max_chunk_size) + 1U);
	if (!((*cctx)->buffer = (uint8_t*)calloc((*cctx)->buffer_size, sizeof(uint8_t))))
	{
		deflateEnd(&(*cctx)->stream);
		free(*cctx);
		*cctx = NULL;
		return false;
	}

	return true;
}

bool mpatch_compress_enc_test(mpatch_cctx_t *const cctx, const uint8_t *const message_in, const uint_fast32_t message_size, uint_fast32_t *const compressed_size)
{
	//Check parameters
	if ((!cctx) || (!cctx->buffer) || (!message_in) || (message_size > cctx->max_chunk_size))
	{
		*compressed_size = 0U;
		return false;
	}

	//Copy the deflate stream
	z_stream temp;
	if (deflateCopy(&temp, &cctx->stream) != Z_OK)
	{
		*compressed_size = 0U;
		return false;
	}

	//Setup temporary deflate stream
	temp.next_in = message_in;
	temp.next_out = cctx->buffer;
	temp.avail_in = message_size;
	temp.avail_out = cctx->buffer_size;

	//Try to compress
	if (deflate(&temp, Z_SYNC_FLUSH) != Z_OK)
	{
		deflateEnd(&temp);
		*compressed_size = 0U;
		return false;
	}

	//Sanity check
	if (temp.avail_out < 1U)
	{
		abort();
	}

	//Compute compressed size
	*compressed_size = cctx->buffer_size - temp.avail_out;

	//Free temporary stream
	const int error = deflateEnd(&temp);
	if ((error != Z_OK) && (error != Z_DATA_ERROR))
	{
		*compressed_size = 0U;
		return false;
	}

	return true;
}

const uint8_t *mpatch_compress_enc_next(mpatch_cctx_t *const cctx, const uint8_t *const message_in, const uint_fast32_t message_size, uint_fast32_t *const compressed_size)
{
	//Check parameters
	if ((!cctx) || (!cctx->buffer) || (!message_in) || (message_size > cctx->max_chunk_size))
	{
		*compressed_size = 0U;
		return NULL;
	}

	//Setup deflate stream
	cctx->stream.next_in = message_in;
	cctx->stream.next_out = cctx->buffer;
	cctx->stream.avail_in = message_size;
	cctx->stream.avail_out = cctx->buffer_size;

	//Try to compress
	if (deflate(&cctx->stream, Z_SYNC_FLUSH) != Z_OK)
	{
		return NULL;
	}

	//Sanity check
	if (cctx->stream.avail_out < 1U)
	{
		abort();
	}

	//Compute compressed size
	*compressed_size = cctx->buffer_size - cctx->stream.avail_out;
	return cctx->buffer;
}

bool mpatch_compress_enc_free(mpatch_cctx_t **const cctx)
{
	//Check parameters
	if ((!cctx) || (!(*cctx)))
	{
		return false;
	}

	//Destroy deflate context
	const int error = deflateEnd(&(*cctx)->stream);

	//Free buffer
	if ((*cctx)->buffer)
	{
		free((*cctx)->buffer);
	}

	//Free context
	free(*cctx);
	*cctx = NULL;

	//Check result
	return ((error == Z_OK) || (error == Z_DATA_ERROR));
}
