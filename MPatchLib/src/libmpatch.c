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

#include <stdlib.h>
#include <stdbool.h>
#include <memory.h>
#include <string.h>

#include <stdio.h>

/* ======================================================================= */
/* Version                                                                 */
/* ======================================================================= */

static const uint16_t VERSION_MAJOR = 1U;
static const uint16_t VERSION_MINOR = 0U;
static const uint16_t VERSION_PATCH = 0U;

static const char *const BUILD_DATE = __DATE__;
static const char *const BUILD_TIME = __TIME__;

/* ======================================================================= */
/* Utilities                                                               */
/* ======================================================================= */

#define BOOLIFY(X) (!!(X))

static uint64_t min_u64(const uint64_t a, const uint64_t b)
{
	return (a < b) ? a : b;
}

static uint64_t max_u64(const uint64_t a, const uint64_t b)
{
	return (a < b) ? a : b;
}

/* ======================================================================= */
/* Bit I/O                                                                 */
/* ======================================================================= */

static const uint8_t BIT_MASK[8U] = { 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80 };

typedef struct
{
	const uint8_t *buffer;
	uint64_t capacity;
}
rd_buffer_t;

typedef struct
{
	uint8_t *buffer;
	uint64_t capacity;
}
wr_buffer_t;

typedef struct
{
	uint64_t byte_pos;
	uint_fast8_t bit_pos;
	uint8_t value;
}
bit_state_t;

static bool read_byte(uint8_t *const value, const rd_buffer_t *const buffer, bit_state_t *const state)
{
	if (state->byte_pos < buffer->capacity)
	{
		*value = buffer->buffer[state->byte_pos++];
		return true;
	}
	*value = 0U;
	return false;
}

static bool write_byte(const uint8_t value, const wr_buffer_t *const buffer, bit_state_t *const state)
{
	if (state->byte_pos < buffer->capacity)
	{
		buffer->buffer[state->byte_pos++] = value;
		return true;
	}
	return false;
}

static void init_bit_state(bit_state_t *const state)
{
	memset(state, 0, sizeof(bit_state_t));
	state->bit_pos = UINT_FAST8_MAX;
}

static bool read_bit(bool *const value, const rd_buffer_t *const buffer, bit_state_t *const state)
{
	if (state->bit_pos > 7U)
	{
		if (!read_byte(&state->value, buffer, state))
		{
			*value = 0U;
			return false;
		}
		state->bit_pos = 0U;
	}
	*value = BOOLIFY(state->value & BIT_MASK[state->bit_pos++]);
	return true;
}

static bool write_bit(const bool value, const wr_buffer_t *const buffer, bit_state_t *const state)
{
	if (state->bit_pos == UINT_FAST8_MAX)
	{
		state->bit_pos = 0U;
	}
	if (value)
	{
		state->value |= BIT_MASK[state->bit_pos];
	}
	if (++state->bit_pos > 7U)
	{
		if (!write_byte(state->value, buffer, state))
		{
			return false;
		}
		state->bit_pos = state->value = 0U;
	}
	return true;
}

static bool flush_state(const wr_buffer_t *const buffer, bit_state_t *const state)
{
	if ((state->bit_pos > 0U) && (state->bit_pos != UINT_FAST8_MAX))
	{
		if (!write_byte(state->value, buffer, state))
		{
			return false;
		}
		state->bit_pos = state->value = 0U;
	}
	return true;
}

/* ======================================================================= */
/* Exponential Golomb                                                      */
/* ======================================================================= */

static uint64_t exp_golomb_size(uint64_t value)
{
	size_t bit_size = 1U;
	while (value)
	{
		bit_size += 2U;
		value >>= 1U;
	}
	return bit_size;
}

static bool exp_golomb_write(uint64_t value, const wr_buffer_t *const buffer, bit_state_t *const state)
{
	uint64_t temp = 0U;
	size_t bit_pos = 0U;
	while (value)
	{
		temp = (temp << 1U) | (value & 1U);
		value >>= 1U;
	}
	if (bit_pos)
	{
		do
		{
			if (!(write_bit(true, buffer, state) && write_bit(temp & 1U, buffer, state)))
			{
				return false;
			}
			value >>= 1U;
		}
		while (bit_pos);
	}
	if (!write_bit(false, buffer, state))
	{
		return false;
	}
	return true;
}

static int exp_golomb_read(uint64_t *const value, const rd_buffer_t *const buffer, bit_state_t *const state)
{
	uint64_t temp = 0U;
	for (;;)
	{
		bool bitval;
		if (!read_bit(&bitval, buffer, state))
		{
			*value = 0U;
			return false;
		}
		if (bitval)
		{
			if (!read_bit(&bitval, buffer, state))
			{
				*value = 0U;
				return false;
			}
			temp = (temp << 1U) | bitval;
		}
		else
		{
			break; /*no more bits*/
		}
	}
	*value = temp;
	return true;
}

/* ======================================================================= */
/* Substring functions                                                     */
/* ======================================================================= */

typedef struct
{
	uint64_t offset;
	uint64_t length;
}
substring_t;

static uint64_t compute_substring_cost(const uint64_t literal_len, const uint64_t substr_off, const uint64_t substr_len)
{
	const uint64_t total_bits = (substr_len > 0U)
		? exp_golomb_size(literal_len) + (8U * literal_len) + exp_golomb_size(substr_len) + 1U + exp_golomb_size(substr_off)
		: exp_golomb_size(literal_len) + (8U * literal_len) + exp_golomb_size(substr_len);
	return (total_bits << 10U) / (literal_len + substr_len);
}

static uint64_t find_optimal_substring(substring_t *const substring, const uint64_t literal_len, const uint8_t *const needle, const uint64_t needle_len, const uint8_t *const haystack, const uint64_t haystack_len)
{
	uint64_t cost = UINT64_MAX;
	substring->offset = substring->length = 0U;

	for (uint64_t haystack_off = 0U; haystack_off < haystack_len; haystack_off++)
	{
		if (haystack[haystack_off] != needle[0])
		{
			continue;
		}
		uint64_t matching_len = 0U;
		for (uint64_t needle_off = 0U; needle_off < needle_len; needle_off++)
		{
			if(haystack[haystack_off + needle_off] != needle[needle_off])
			{
				break; /*end of matching sequence*/
			}
			matching_len++;
		}
		if (matching_len > 0U)
		{
			const uint64_t new_cost = compute_substring_cost(literal_len, haystack_off, matching_len);
			if (new_cost < cost)
			{
				substring->offset = haystack_off;
				substring->length = matching_len;
				cost = new_cost;
			}
		}
	}

	return cost;
}

/* ======================================================================= */
/* Encode and decode functions                                             */
/* ======================================================================= */

static const uint64_t LITERAL_LIMIT = 128U;

static uint64_t encode_chunk(const rd_buffer_t *const input_buffer, const uint64_t input_pos, const rd_buffer_t *const reference_buffer, const wr_buffer_t *const output_buffer, bit_state_t *const output_state, FILE *const log_file)
{
	const uint64_t remaining = min_u64(input_buffer->capacity - input_pos, UINT32_MAX);
	substring_t optimal_substr = { 0U, 0U };
	uint64_t cost_optimal , optimal_literal_len;
	bool optimal_select_bit = false;

	//Init bit cost computation
	const uint64_t max_literal_len = min_u64(remaining, LITERAL_LIMIT);
	cost_optimal = compute_substring_cost(optimal_literal_len = max_literal_len, 0U, 0U);

	//Find the "optimal" encoding of the next chunk
	for (uint64_t literal_len = 0U; literal_len <= max_literal_len; literal_len++)
	{
		substring_t substring_src, substring_ref;
		const uint64_t cost_substr_src = find_optimal_substring(&substring_src, literal_len, input_buffer->buffer + input_pos + literal_len, remaining - literal_len, input_buffer->buffer, input_pos);
		const uint64_t cost_substr_ref = find_optimal_substring(&substring_ref, literal_len, input_buffer->buffer + input_pos + literal_len, remaining - literal_len, reference_buffer->buffer, reference_buffer->capacity);
		bool select_bit = (cost_substr_ref < cost_substr_src);
		const uint64_t new_cost = select_bit ? cost_substr_ref : cost_substr_src;
		if (new_cost < cost_optimal)
		{
			optimal_literal_len = literal_len;
			optimal_select_bit = select_bit;
			memcpy(&optimal_substr, select_bit ? &substring_ref : &substring_src, sizeof(substring_t));
			cost_optimal = new_cost;
		}
	}

	//Write detailed info to log
	if (log_file)
	{
		if (optimal_substr.length)
		{
			fprintf(log_file, "%016llu, %016llu, %016llu, %s, %016llu\n", input_pos, optimal_literal_len, optimal_substr.length, optimal_select_bit ? "REF" : "SRC", optimal_substr.offset);
		}
		else
		{
			fprintf(log_file, "%016llu, %016llu, %016llu\n", input_pos, optimal_literal_len, optimal_substr.length);
		}
		fflush(log_file);
	}

	//Write "optimal" encoding to output now!
	if (!exp_golomb_write(optimal_literal_len, output_buffer, output_state))
	{
		return UINT64_MAX;
	}
	for (uint64_t i = 0U; i < optimal_literal_len; ++i)
	{
		if (!write_byte(input_buffer->buffer[input_pos + i], output_buffer, output_state))
		{
			return UINT64_MAX;
		}
	}
	if (!exp_golomb_write(optimal_substr.length, output_buffer, output_state))
	{
		return UINT64_MAX;
	}
	if (optimal_substr.length > 0)
	{
		if (!write_bit(optimal_select_bit, output_buffer, output_state))
		{
			return UINT64_MAX;
		}
		if (!exp_golomb_write(optimal_substr.offset, output_buffer, output_state))
		{
			return UINT64_MAX;
		}
	}

	return optimal_literal_len + optimal_substr.length;
}

/* ======================================================================= */
/* Self-Test                                                               */
/* ======================================================================= */

static void selftest_exp_golomb(void)
{
	uint8_t *const test = (uint8_t*)malloc(32768U * sizeof(uint8_t));
	if (!test)
	{
		abort();
	}

	const wr_buffer_t  wr_buffer = { test, 32768U };
	bit_state_t state;
	init_bit_state(&state);
	for (uint64_t i = 1U; i <= 9999U; ++i)
	{
		if (!exp_golomb_write(i, &wr_buffer, &state))
		{
			abort();
		}
	}
	if (!exp_golomb_write(0, &wr_buffer, &state))
	{
		abort();
	}
	flush_state(&wr_buffer, &state);

	const rd_buffer_t rd_buffer = { test, 32768U };
	init_bit_state(&state);
	for (uint64_t i = 1U; i < UINT64_MAX; ++i)
	{
		uint64_t value;
		if (!exp_golomb_read(&value, &rd_buffer, &state))
		{
			abort();
		}
		if (value == 0)
		{
			break;
		}
		else if (value != i)
		{
			abort();
		}
	}

	free(test);
}

static void selftest_bit_iofunc(void)
{
	uint8_t *const test = (uint8_t*)malloc(1024U * sizeof(uint8_t));
	bool *const data_1 = (bool*)malloc(8192U * sizeof(bool));
	bool *const data_2 = (bool*)malloc(8192U * sizeof(bool));
	if (!(test && data_1 && data_2))
	{
		abort();
	}

	memset(data_2, 0, 8192U * sizeof(bool));
	srand(666);
	for (size_t i = 0; i < 8192U; ++i)
	{
		data_1[i] = (rand() > (RAND_MAX / 2));
	}

	const wr_buffer_t  wr_buffer = { test, 1024U };
	bit_state_t state;
	init_bit_state(&state);
	for (size_t i = 0; i < 8192U; ++i)
	{
		if (!write_bit(data_1[i], &wr_buffer, &state))
		{
			abort();
		}
	}
	flush_state(&wr_buffer, &state);

	const rd_buffer_t rd_buffer = { test, 1024U };
	init_bit_state(&state);
	for (size_t i = 0; i < 8192; ++i)
	{
		bool bitval;
		if (!read_bit(&bitval, &rd_buffer, &state))
		{
			abort();
		}
		data_2[i] = bitval;
	}

	for (size_t i = 0; i < 8192U; ++i)
	{
		if (data_1[i] != data_2[i])
		{
			abort();
		}
	}

	free(test);
	free(data_1);
	free(data_2);
}

/* ======================================================================= */
/* Public Interface                                                        */
/* ======================================================================= */

void mpatch_version(mpatch_version_t *const version)
{
	if (version)
	{
		version->ver_major = VERSION_MAJOR;
		version->ver_minor = VERSION_MINOR;
		version->ver_patch = VERSION_PATCH;
		strncpy_s(version->bld_date, 12U, BUILD_DATE, _TRUNCATE);
		strncpy_s(version->bld_time, 12U, BUILD_TIME, _TRUNCATE);
	}
}

mpatch_error_t mpatch_encode(mpatch_enc_param_t *const param)
{
	if (!param)
	{
		return MPATCH_INVALID_PARAMETER;
	}

	param->compressed_size = 0U;

	if ((!param->message_in) || (!param->reference_in) || (!param->compressed_out))
	{
		return MPATCH_INVALID_PARAMETER;
	}
	if((param->message_size < 1U) || (param->message_size == UINT64_MAX) || (param->reference_size < 1U) || (param->compressed_capacity < 1U))
	{
		return MPATCH_INVALID_PARAMETER;
	}

	const rd_buffer_t input_buffer = { param->message_in, param->message_size }, reference_buffer = { param->reference_in, param->reference_size };
	const wr_buffer_t output_buffer = { param->compressed_out, param->compressed_capacity };

	bit_state_t output_state;
	init_bit_state(&output_state);

	uint64_t input_pos = 0U;
	while (input_pos < input_buffer.capacity)
	{
		if (param->callback)
		{
			if (!param->callback((double)input_pos / (double)input_buffer.capacity))
			{
				return MPATCH_CANCELLED_BY_USER;
			}
		}
		const uint64_t chunk_len = encode_chunk(&input_buffer, input_pos, &reference_buffer, &output_buffer, &output_state, param->trace_log_file);
		if (chunk_len == UINT64_MAX)
		{
			return MPATCH_INSUFFICIENT_BUFFER;
		}
		if (chunk_len < 1U)
		{
			return MPATCH_INTERNAL_ERROR;
		}
		input_pos += chunk_len;
	}

	if (!flush_state(&output_buffer, &output_state))
	{
		return MPATCH_INSUFFICIENT_BUFFER;
	}

	if (param->callback)
	{
		param->callback(1.0);
	}

	param->compressed_size = output_state.byte_pos;
	return MPATCH_SUCCESS;
}

mpatch_error_t mpatch_decode(mpatch_dec_param_t *const param)
{
	return MPATCH_INVALID_PARAMETER;
}

void mpatch_selftest()
{
	selftest_bit_iofunc();
	selftest_exp_golomb();
}
