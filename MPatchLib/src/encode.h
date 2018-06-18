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

static const uint_fast32_t LITERAL_LIMIT = 8192;
static const uint_fast32_t COMPRESS_THRESHOLD = 5U;

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
	}
	stats;
}
encd_state_t;

/* ======================================================================= */
/* Encoder functions                                                       */
/* ======================================================================= */

static inline float _chunk_cost(const uint8_t *const input_ptr, encd_state_t *const coder_state, const uint32_t literal_len, const substring_t *const substring)
{
	uint_fast32_t total_bits = literal_len ? (literal_len * 8U) + exp_golomb_size(literal_len) + 1U : 1U;
	if (substring->length > SUBSTRING_THRESHOLD)
	{
		total_bits += exp_golomb_size(substring->length - SUBSTRING_THRESHOLD) + exp_golomb_size(substring->offset_diff) + (substring->offset_diff ? 1U : 0U);
	}
	else
	{
		if (substring->length)
		{
			abort();
		}
		total_bits += 1U;
	}
	return (float)total_bits / ((literal_len + substring->length) * 8U);
}

static bool _write_chunk(const uint8_t *const input_ptr, const mpatch_writer_t *const output, encd_state_t *const coder_state, const uint_fast32_t optimal_literal_len, const substring_t *const optimal_substr)
{
	//Write literal
	if (optimal_literal_len > 0U)
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
			if (optimal_substr->offset_sign)
			{
				coder_state->prev_offset += optimal_substr->offset_diff;
			}
			else
			{
				coder_state->prev_offset -= optimal_substr->offset_diff;
			}
		}
		coder_state->prev_offset += optimal_substr->length;
	}
}

static uint_fast32_t encode_chunk(const mpatch_rd_buffer_t *const input_buffer, const uint_fast32_t input_pos, const mpatch_rd_buffer_t *const reference_buffer, const mpatch_writer_t *const output, encd_state_t *const coder_state, thread_pool_t *const thread_pool, const mpatch_logger_t *const logger)
{
	//Step size LUT
	static const uint_fast32_t STEP_SIZE[18U] = { (uint_fast32_t)(-1), 1U, 1U, 2U, 3U, 4U, 6U, 8U, 11U, 16U, 23U, 32U, 45U, 64U, 91U, 128U, 181U, 256U };

	//Set up limits
	const uint_fast32_t remaining = input_buffer->capacity - input_pos;
	const uint_fast32_t literal_limit = min_uint32(LITERAL_LIMIT, remaining);
	
	//Keep the "optimal" settings
	substring_t optimal_substr = { 0U, 0U, false };
	uint_fast32_t optimal_literal_len;

	//Initialize adaptive step size and bit cost computation
	uint_fast32_t step_size = 0U;
	float cost_optimal = _chunk_cost(input_buffer->buffer + input_pos, coder_state, optimal_literal_len = literal_limit, &optimal_substr);

	//Find the "optimal" encoding of the next chunk
	for (uint_fast32_t literal_len = 0U; literal_len <= literal_limit; literal_len += STEP_SIZE[inc_bound_uint32(&step_size, 17U)])
	{
		substring_t substr_data;
		if (find_optimal_substring(&substr_data, literal_len, coder_state->prev_offset, thread_pool, input_buffer->buffer + input_pos + literal_len, remaining - literal_len, reference_buffer->buffer, reference_buffer->capacity))
		{
			const float substr_cost = _chunk_cost(input_buffer->buffer + input_pos, coder_state, literal_len, &substr_data);
			if (substr_cost < cost_optimal)
			{
				optimal_literal_len = literal_len;
				memcpy(&optimal_substr, &substr_data, sizeof(substring_t));
				cost_optimal = substr_cost;
				continue; /*skip "stop" check this one time*/
			}
		}
		if (optimal_substr.length)
		{
			break; /*stop optimization process here*/
		}
	}

	//Try to refine the decision using "backward" steps
	while (step_size > 2U)
	{
		--step_size;
		while (optimal_literal_len > STEP_SIZE[step_size])
		{
			const uint_fast32_t literal_len = optimal_literal_len - STEP_SIZE[step_size];
			substring_t substr_data;
			if (find_optimal_substring(&substr_data, literal_len, coder_state->prev_offset, thread_pool, input_buffer->buffer + input_pos + literal_len, remaining - literal_len, reference_buffer->buffer, reference_buffer->capacity))
			{
				const float substr_cost = _chunk_cost(input_buffer->buffer + input_pos, coder_state, literal_len, &substr_data);
				if (substr_cost < cost_optimal)
				{
					optimal_literal_len = literal_len;
					memcpy(&optimal_substr, &substr_data, sizeof(substring_t));
					cost_optimal = substr_cost;
					continue; /*skip "stop" check this one time*/
				}
			}
			break; /*no improvement, stop!*/
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
			logger->logging_func("%016lu, %6.4f, %016lu, %016lu, %s, %016lu\n", logger->user_data, input_pos, cost_optimal, optimal_literal_len,
				optimal_substr.length, optimal_substr.offset_diff ? (optimal_substr.offset_sign ? "-->" : "<--") : "~~~", optimal_substr.offset_diff);
		}
		else
		{
			logger->logging_func("%016lu, %6.4f, %016lu, %016lu\n", logger->user_data, input_pos, cost_optimal, optimal_literal_len, optimal_substr.length);
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
