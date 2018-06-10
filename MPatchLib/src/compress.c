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

#include <malloc.h>
#include <memory.h>

#include <lz4.h>
#include <lz4frame.h>

struct _mpatch_cctx_t
{
	LZ4F_cctx *lz4f_cctx;
	uint_fast32_t max_input_size, max_compressed_size;
	uint8_t *compressed_buffer;
};

static const LZ4F_compressOptions_t s_compress_options = { 0U, { 0U, 0U, 0U } };

/* ======================================================================= */
/* Compress functions                                                      */
/* ======================================================================= */

bool mpatch_compress_enc_init(mpatch_cctx_t **const cctx, uint8_t *const header_out, const uint_fast32_t header_capacity, uint_fast32_t *const header_size, const uint_fast32_t max_input_size)
{
	//Check size
	if ((header_capacity < LZ4F_HEADER_SIZE_MAX) || (max_input_size > LZ4_MAX_INPUT_SIZE))
	{
		*cctx = NULL;
		*header_size = 0U;
		return false;
	}

	//Alloc context
	if (!(*cctx = (mpatch_cctx_t*)calloc(1U, sizeof(mpatch_cctx_t))))
	{
		*header_size = 0U;
		return false;
	}

	//Setup preferences
	LZ4F_preferences_t preferences;
	memset(&preferences, 0, sizeof(LZ4F_preferences_t));
	preferences.compressionLevel = LZ4F_compressionLevel_max();
	preferences.autoFlush = 1U;
	preferences.frameInfo.blockSizeID = LZ4F_max4MB;

	//Alloc buffer
	(*cctx)->max_compressed_size = (uint_fast32_t)LZ4F_compressBound((*cctx)->max_input_size = max_input_size, &preferences);
	if (!((*cctx)->compressed_buffer = (uint8_t*)calloc((size_t)(*cctx)->max_compressed_size, sizeof(uint8_t))))
	{
		free(*cctx);
		*cctx = NULL;
		*header_size = 0U;
		return false;
	}

	//Create context
	if (LZ4F_createCompressionContext(&(*cctx)->lz4f_cctx, LZ4F_VERSION))
	{
		free((*cctx)->compressed_buffer);
		free(*cctx);
		*cctx = NULL;
		*header_size = 0U;
		return false;
	}

	//Create the header
	const size_t error_code = LZ4F_compressBegin((*cctx)->lz4f_cctx, header_out, header_capacity, &preferences);
	if (LZ4F_isError(error_code))
	{
		LZ4F_freeCompressionContext((*cctx)->lz4f_cctx);
		free((*cctx)->compressed_buffer);
		free(*cctx);
		*cctx = NULL;
		*header_size = 0U;
		return false;
	}

	*header_size = (uint_fast32_t)error_code;
	return true;
}

const uint8_t *mpatch_compress_enc_next(mpatch_cctx_t *const cctx, const uint8_t *const message_in, const uint_fast32_t message_size, uint_fast32_t *const compressed_size)
{
	//Check parameters
	if ((!cctx) || (!cctx->lz4f_cctx) || (!cctx->compressed_buffer) || (!message_in) || (message_size > cctx->max_input_size))
	{
		*compressed_size = 0U;
		return NULL;
	}

	//Try to compress
	const size_t error_code = LZ4F_compressUpdate(cctx->lz4f_cctx, cctx->compressed_buffer, cctx->max_compressed_size, message_in, message_size, &s_compress_options);
	if (LZ4F_isError(error_code))
	{
		*compressed_size = 0U;
		return NULL;
	}

	*compressed_size = (uint_fast32_t)error_code;
	return cctx->compressed_buffer;
}

bool mpatch_compress_enc_exit(mpatch_cctx_t *const cctx, uint8_t *const footer_out, const uint_fast32_t footer_capacity, uint_fast32_t *const footer_size)
{
	//Check parameters
	if ((!cctx) ||  (!cctx->lz4f_cctx) || (footer_capacity < 4U))
	{
		*footer_size = 0U;
		return false;
	}

	//Try to finalize
	const size_t error_code = LZ4F_compressEnd(cctx->lz4f_cctx, footer_out, footer_capacity, &s_compress_options);
	if (LZ4F_isError(error_code))
	{
		*footer_size = 0U;
		return false;
	}

	*footer_size = (uint_fast32_t)error_code;
	return true;
}

bool mpatch_compress_enc_free(mpatch_cctx_t **const cctx)
{
	//Check parameters
	if ((!cctx) || (!(*cctx)) || (!(*cctx)->lz4f_cctx))
	{
		return false;
	}

	//Destroy context
	const bool success = (!LZ4F_isError(LZ4F_freeCompressionContext((*cctx)->lz4f_cctx)));
	(*cctx)->lz4f_cctx = NULL;

	//Free buffer
	if ((*cctx)->compressed_buffer)
	{
		free((*cctx)->compressed_buffer);
		(*cctx)->compressed_buffer = NULL;
	}

	//Free context
	free(*cctx);
	*cctx = NULL;

	return success;
}
