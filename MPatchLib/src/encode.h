/* ---------------------------------------------------------------------------------------------- */
/* MPatchLib - patch and compression library                                                      */
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

#include "libmpatch.h"
#include "utils.h"
#include "substring.h"
#include "compress.h"

#include <stdlib.h>

#define COMPRESS_THRESHOLD 5U
#define LITERAL_LEN_COUNT 32U
#define MAX_LITERAL_LEN 2048U

static const uint_fast32_t SUBSTR_SRC = 0U;
static const uint_fast32_t SUBSTR_REF = 1U;

typedef struct
{
	io_state_t output_state;
	mpatch_cctx_t *cctx;
	uint_fast32_t prev_offset;
	struct
	{
		uint_fast32_t literal_bytes;
		uint_fast32_t substring_bytes;
		uint_fast32_t saved_bytes;
		uint_fast32_t literal_hist[MAX_LITERAL_LEN + 1U];
	}
	stats;
}
encd_state_t;

static const uint32_t LITERAL_LEN[LITERAL_LEN_COUNT] =
{
	0U, 1U, 2U, 3U, 5U, 7U, 10U, 13U, 17U, 22U, 28U, 35U, 44U, 55U, 68U, 84U, 103U, 126U, 154U, 189U, 231U, 282U, 344U, 420U, 513U, 626U, 763U, 930U, 1133U, 1380U, 1681U, 2048U
};

/* ======================================================================= */
/* Encoder functions                                                       */
/* ======================================================================= */

static bool _write_chunk(const uint8_t *const input_ptr, const mpatch_writer_t *const output, encd_state_t *const coder_state, const uint_fast32_t optimal_literal_len, const substring_t *const optimal_substr)
{
	//Update histogram
	coder_state->stats.literal_hist[optimal_literal_len]++;

	//Write literal
	if (optimal_literal_len)
	{
		uint_fast32_t compressed_size = optimal_literal_len;
		coder_state->stats.literal_bytes += optimal_literal_len;
		if (optimal_literal_len > COMPRESS_THRESHOLD)
		{
			if ((compressed_size = mpatch_compress_enc_test(coder_state->cctx, input_ptr, optimal_literal_len)) == UINT_FAST32_MAX)
			{
				return false;
			}
		}
		if (compressed_size < optimal_literal_len)
		{
			const uint8_t *const compressed_data = mpatch_compress_enc_next(coder_state->cctx, input_ptr, optimal_literal_len, &compressed_size);
			coder_state->stats.saved_bytes += (optimal_literal_len - compressed_size);
			if (!(compressed_data && exp_golomb_write(compressed_size, output, &coder_state->output_state) && write_bit(true, output, &coder_state->output_state) && write_bytes(compressed_data, compressed_size, output, &coder_state->output_state)))
			{
				return false;
			}
		}
		else
		{
			if (!(exp_golomb_write(optimal_literal_len, output, &coder_state->output_state) && write_bit(false, output, &coder_state->output_state) && write_bytes(input_ptr, optimal_literal_len, output, &coder_state->output_state)))
			{
				return false;
			}
		}
	}
	else
	{
		if (!exp_golomb_write(0U, output, &coder_state->output_state))
		{
			return false;
		}
	}

	//Write substring
	if (optimal_substr->length > SUBSTRING_THRESHOLD)
	{
		coder_state->stats.substring_bytes += optimal_substr->length;
		if (!(exp_golomb_write(optimal_substr->length - SUBSTRING_THRESHOLD, output, &coder_state->output_state) && exp_golomb_write(optimal_substr->offset_diff, output, &coder_state->output_state)))
		{
			return false;
		}
		if (optimal_substr->offset_diff > 0U)
		{
			if (!write_bit(optimal_substr->offset_sign, output, &coder_state->output_state))
			{
				return false;
			}
		}
	}
	else
	{
		if (optimal_substr->length)
		{
			abort();
		}
		if (!exp_golomb_write(0U, output, &coder_state->output_state))
		{
			return false;
		}
	}

	return true;
}

static void _update_encd_state(encd_state_t *const coder_state, const substring_t *const optimal_substr)
{
	if (optimal_substr->length > 1U)
	{
		if (optimal_substr->offset_diff)
		{
			coder_state->prev_offset = optimal_substr->offset_sign ? coder_state->prev_offset + optimal_substr->offset_diff : coder_state->prev_offset - optimal_substr->offset_diff;
		}
		coder_state->prev_offset += optimal_substr->length;
	}
}

static uint_fast32_t encode_chunk(const mpatch_rd_buffer_t *const input_buffer, const uint_fast32_t input_pos, const mpatch_rd_buffer_t *const reference_buffer, const mpatch_writer_t *const output, encd_state_t *const coder_state, thread_pool_t *const thread_pool, const mpatch_logger_t *const logger)
{
	//Step size LUT
	//static const uint_fast32_t STEP_SIZE[18U] = { (uint_fast32_t)(-1), 1U, 1U, 2U, 3U, 4U, 6U, 8U, 11U, 16U, 23U, 32U, 45U, 64U, 91U, 128U, 181U, 256U };

	//Set up limits
	const uint_fast32_t remaining = input_buffer->capacity - input_pos;
	
	//Keep the "optimal" settings
	substring_t optimal_substr = { 0U, 0U, false };
	uint64_t optimal_score = 0U;
	uint_fast32_t optimal_literal_len = min_uint32(remaining, MAX_LITERAL_LEN);

	//Find the "optimal" encoding of the next chunk
	for (uint_fast32_t literal_len_idx = 0U; (literal_len_idx < LITERAL_LEN_COUNT) && (remaining > LITERAL_LEN[literal_len_idx]); ++literal_len_idx)
	{
		substring_t substr_data;
		const uint64_t score = find_optimal_substring(&substr_data, coder_state->prev_offset, thread_pool, input_buffer->buffer + input_pos + LITERAL_LEN[literal_len_idx], remaining - LITERAL_LEN[literal_len_idx], reference_buffer->buffer, reference_buffer->capacity);
		if (score > optimal_score)
		{
			optimal_literal_len = LITERAL_LEN[literal_len_idx];
			memcpy(&optimal_substr, &substr_data, sizeof(substring_t));
			optimal_score = score;
			continue; /*skip "stop" check this one time*/
		}
		if (optimal_substr.length)
		{
			break; /*stop optimization process here*/
		}
	}

	//Refine result
	if (optimal_literal_len > 3U)
	{
		for (uint32_t refine_step = MAX_LITERAL_LEN; refine_step; refine_step /= 2U)
		{
			if (refine_step > optimal_literal_len)
			{
				const uint32_t literal_len = optimal_literal_len - 1U;
				substring_t substr_data;
				const uint64_t score = find_optimal_substring(&substr_data, coder_state->prev_offset, thread_pool, input_buffer->buffer + input_pos + literal_len, remaining - literal_len, reference_buffer->buffer, reference_buffer->capacity);
				if (score > optimal_score)
				{
					optimal_literal_len = literal_len;
					memcpy(&optimal_substr, &substr_data, sizeof(substring_t));
					optimal_score = score;
				}
			}
		}
	}

	//Write detailed info to log
	if (logger->logging_func)
	{
		if (!input_pos)
		{
			logger->logging_func("[CHUNKS]\n", logger->user_data);
		}
		if (optimal_substr.length > SUBSTRING_THRESHOLD)
		{
			logger->logging_func("%016lu, %016llu, %016lu, %016lu, %s, %016lu\n", logger->user_data, input_pos, optimal_score, optimal_literal_len,
				optimal_substr.length, optimal_substr.offset_diff ? (optimal_substr.offset_sign ? "-->" : "<--") : "~~~", optimal_substr.offset_diff);
		}
		else
		{
			logger->logging_func("%016lu, %016llu, %016lu, %016lu\n", logger->user_data, input_pos, optimal_score, optimal_literal_len, optimal_substr.length);
		}
	}

	//Write "optimal" encoding to output now!
	if (!_write_chunk(input_buffer->buffer + input_pos, output, coder_state, optimal_literal_len, &optimal_substr))
	{
		return 0U;
	}

	//Update coder state
	_update_encd_state(coder_state, &optimal_substr);

	//Return total number of "used" bytes
	return optimal_literal_len + optimal_substr.length;
}
