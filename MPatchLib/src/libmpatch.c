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

#include "rhash/md5.h"
#include "rhash/crc32.h"
#include "pool.h"

#include <stdlib.h>
#include <stdbool.h>
#include <memory.h>
#include <string.h>
#include <time.h>
#include <float.h>

#if SIZE_MAX < UINT32_MAX
#error Size of UINT32_MAX exceeds size of SIZE_MAX !!!
#endif

/* ======================================================================= */
/* Version                                                                 */
/* ======================================================================= */

static const uint16_t VERSION_MAJOR = 1U;
static const uint16_t VERSION_MINOR = 0U;
static const uint16_t VERSION_PATCH = 0U;

static const char *const BUILD_DATE = __DATE__;
static const char *const BUILD_TIME = __TIME__;

/* ======================================================================= */
/* Const                                                                   */
/* ======================================================================= */

static const uint8_t MAGIC_HEADER[8U] = { 'M', 'P', 'a', 't', 'c', 'h', '!', '\0' };
static const uint8_t MAGIC_FOOTER[8U] = { '\0', '!', 'h', 'c', 't', 'a', 'P', 'M' };

static const uint16_t FILE_FORMAT_VERSION = 1U;

static const uint8_t PADDING[15] = { 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U };

#define MAX_SPEED_PARAMETER 8U

/* ======================================================================= */
/* Utilities                                                               */
/* ======================================================================= */

#define BOOLIFY(X) (!!(X))

static __forceinline uint_fast32_t min_uf32(const uint_fast32_t a, const uint_fast32_t b)
{
	return (a < b) ? a : b;
}

static __forceinline float min_flt(const float a, const float b)
{
	return (a < b) ? a : b;
}

static __forceinline uint_fast32_t diff_uf32(const uint_fast32_t a, const uint_fast32_t b)
{
	return (a > b) ? (a - b) : (b - a);
}

static inline void enc_uint32(uint8_t *const buffer, const uint32_t value)
{
	static const size_t SHIFT[4] = { 24U, 16U, 8U, 0U };
	for (size_t i = 0; i < 4U; ++i)
	{
		buffer[i] = (uint8_t)(value >> SHIFT[i]);
	}
}

static inline void dec_uint32(uint32_t *const value, const uint8_t *const buffer)
{
	static const size_t SHIFT[4] = { 24U, 16U, 8U, 0U };
	*value = 0U;
	for (size_t i = 0; i < 4U; ++i)
	{
		*value |= (buffer[i] << SHIFT[i]);
	}
}

/* ======================================================================= */
/* Bit I/O                                                                 */
/* ======================================================================= */

static const uint8_t BIT_MASK[8U] = { 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80 };

typedef struct
{
	uint_fast8_t bit_pos;
	uint8_t value;
	uint32_t byte_counter;
	md5_ctx md5_ctx;
	uint32_t crc32_ctx;
}
io_state_t;

static inline void init_io_state(io_state_t *const state)
{
	memset(state, 0, sizeof(io_state_t));
	mpatch_md5_init(&state->md5_ctx);
	mpatch_crc32_init(&state->crc32_ctx);
	state->bit_pos = UINT_FAST8_MAX;
}

static inline bool read_bit(bool *const value, const mpatch_reader_t *const input, io_state_t *const state)
{
	if (state->bit_pos > 7U)
	{
		if (!input->reader_func(&state->value, 1U, input->user_data))
		{
			*value = 0U;
			return false;
		}
		state->bit_pos = 0U;
	}
	*value = BOOLIFY(state->value & BIT_MASK[state->bit_pos++]);
	return true;
}

static inline bool write_bit(const bool value, const mpatch_writer_t *const output, io_state_t *const state)
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
		if (!output->writer_func(&state->value, 1U, output->user_data))
		{
			return false;
		
		}
		mpatch_md5_update(&state->md5_ctx, &state->value, 1U);
		mpatch_crc32_update(&state->crc32_ctx, &state->value, 1U);
		state->byte_counter++;
		state->bit_pos = state->value = 0U;
	}
	return true;
}

static inline bool read_byte(uint8_t *const value, const mpatch_reader_t *const input, io_state_t *const state)
{
	if (state->bit_pos > 7U)
	{
		if (!input->reader_func(&state->value, 1U, input->user_data))
		{
			*value = 0U;
			return false;
		}
		state->bit_pos = 0U;
	}
	*value = (state->value >> state->bit_pos);
	if (state->bit_pos)
	{
		if (!input->reader_func(&state->value, 1U, input->user_data))
		{
			*value = 0U;
			return false;
		}
		*value |= (state->value << (8U - state->bit_pos));
	}
	else
	{
		state->bit_pos = 8U;
	}
	return true;
}

static inline bool write_byte(const uint8_t value, const mpatch_writer_t *const output, io_state_t *const state)
{
	if (state->bit_pos == UINT_FAST8_MAX)
	{
		state->bit_pos = 0U;
	}
	state->value |= (value << state->bit_pos);
	if (!output->writer_func(&state->value, 1U, output->user_data))
	{
		return false;
	}
	mpatch_md5_update(&state->md5_ctx, &state->value, 1U);
	mpatch_crc32_update(&state->crc32_ctx, &state->value, 1U);
	state->byte_counter++;
	state->value = (value >> (8U - state->bit_pos));
	return true;
}

static inline bool write_bytes(const uint8_t *data, const uint_fast32_t len, const mpatch_writer_t *const output, io_state_t *const state)
{
	for (uint32_t i = 0U; i < len; ++i)
	{
		if (!write_byte(data[i], output, state))
		{
			return false;
		}
	}
	return true;
}

static inline bool flush_state(const mpatch_writer_t *const output, io_state_t *const state)
{
	if ((state->bit_pos > 0U) && (state->bit_pos != UINT_FAST8_MAX))
	{
		if (!output->writer_func(&state->value, 1U, output->user_data))
		{
			return false;
		}
		mpatch_md5_update(&state->md5_ctx, &state->value, 1U);
		mpatch_crc32_update(&state->crc32_ctx, &state->value, 1U);
		state->byte_counter++;
		state->bit_pos = state->value = 0U;
	}
	return true;
}

/* ======================================================================= */
/* Exponential Golomb                                                      */
/* ======================================================================= */

typedef struct
{
	uint_fast32_t val;
	uint_fast32_t len;
}
exp_golomb_size_t;

static const uint_fast32_t EXP_GOLOMB_SIZE[128] =
{
	0x1, 0x3, 0x5, 0x5, 0x7, 0x7, 0x7, 0x7, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9,
	0xB, 0xB, 0xB, 0xB, 0xB, 0xB, 0xB, 0xB, 0xB, 0xB, 0xB, 0xB, 0xB, 0xB, 0xB, 0xB,
	0xD, 0xD, 0xD, 0xD, 0xD, 0xD, 0xD, 0xD, 0xD, 0xD, 0xD, 0xD, 0xD, 0xD, 0xD, 0xD,
	0xD, 0xD, 0xD, 0xD, 0xD, 0xD, 0xD, 0xD, 0xD, 0xD, 0xD, 0xD, 0xD, 0xD, 0xD, 0xD,
	0xF, 0xF, 0xF, 0xF, 0xF, 0xF, 0xF, 0xF, 0xF, 0xF, 0xF, 0xF, 0xF, 0xF, 0xF, 0xF,
	0xF, 0xF, 0xF, 0xF, 0xF, 0xF, 0xF, 0xF, 0xF, 0xF, 0xF, 0xF, 0xF, 0xF, 0xF, 0xF,
	0xF, 0xF, 0xF, 0xF, 0xF, 0xF, 0xF, 0xF, 0xF, 0xF, 0xF, 0xF, 0xF, 0xF, 0xF, 0xF,
	0xF, 0xF, 0xF, 0xF, 0xF, 0xF, 0xF, 0xF, 0xF, 0xF, 0xF, 0xF, 0xF, 0xF, 0xF, 0xF
};

static inline uint_fast32_t exp_golomb_size(uint_fast32_t value)
{
	if (value < 128U)
	{
		return EXP_GOLOMB_SIZE[value];
	}
	else
	{
		uint_fast32_t bit_size = 1U;
		while (value)
		{
			bit_size += 2U;
			value >>= 1U;
		}
		return bit_size;
	}
}

static inline bool exp_golomb_write(uint_fast32_t value, const mpatch_writer_t *const output, io_state_t *const state)
{
	uint_fast32_t nbits = 0U, temp = 0U;
	while (value)
	{
		temp = (temp << 1U) | (value & 1U);
		++nbits;
		value >>= 1U;
	}
	if (nbits)
	{
		do
		{
			if (!(write_bit(true, output, state) && write_bit(temp & 1U, output, state)))
			{
				return false;
			}
			temp >>= 1U;
		}
		while (--nbits);
	}
	if (!write_bit(false, output, state))
	{
		return false;
	}
	return true;
}

static inline int exp_golomb_read(uint_fast32_t *const value, const mpatch_reader_t *const input, io_state_t *const state)
{
	*value = 0U;
	for (;;)
	{
		bool bitval;
		if (!read_bit(&bitval, input, state))
		{
			return false;
		}
		if (bitval)
		{
			if (!read_bit(&bitval, input, state))
			{
				return false;
			}
			*value = (*value << 1U) | bitval;
		}
		else
		{
			break; /*no more bits*/
		}
	}
	return true;
}

/* ======================================================================= */
/* Substring functions                                                     */
/* ======================================================================= */

#define SUBSTR_SRC false
#define SUBSTR_REF true

#define SUBSTR_BWD false
#define SUBSTR_FWD true

typedef struct
{
	uint_fast32_t length;
	uint_fast32_t offset_diff;
	bool offset_sign;
}
substring_t;

typedef struct
{
	uint_fast32_t literal_len;
	uint_fast32_t prev_offset;
	const uint8_t *needle;
	uint_fast32_t needle_len;
	const uint8_t *haystack;
	uint_fast32_t haystack_len;
}
search_param_t;

typedef struct
{
	const search_param_t *search_param;
	struct
	{
		uint_fast32_t begin;
		uint_fast32_t end;
	}
	search_range;
	struct
	{
		substring_t substring;
		float cost;
	}
	result;
}
search_thread_t;

static __forceinline float compute_substring_cost(const uint_fast32_t literal_len, const uint_fast32_t substr_len, const uint_fast32_t substr_off_diff)
{
	const uint_fast32_t total_bits = (substr_len > 1U)
		? (exp_golomb_size(literal_len) + (8U * literal_len) + exp_golomb_size(substr_len - 1U) + exp_golomb_size(substr_off_diff) + (substr_off_diff ? 2U : 1U))
		: (exp_golomb_size(literal_len) + (8U * literal_len) + exp_golomb_size(0U));
	return ((float)total_bits) / ((float)(literal_len + substr_len));
}

static inline uintptr_t find_optimal_substring_thread(const uintptr_t data)
{
	search_thread_t *const param = (search_thread_t*)data;

	//Initialize result
	param->result.substring.length = 0U;
	param->result.substring.offset_diff = UINT_FAST32_MAX;
	param->result.substring.offset_sign = SUBSTR_FWD;
	param->result.cost = FLT_MAX;

	//Sanity checking
	if ((param->search_param->haystack_len < 2U) || (param->search_param->needle_len < 2U))
	{
		return 0U;
	}

	//Setup search parameters
	const uint8_t *haystack_off = param->search_param->haystack + param->search_range.begin;
	uint_fast32_t remaining = param->search_range.end - param->search_range.begin;

	//Find the longest substring in haystack
	while (haystack_off = memchr(haystack_off, param->search_param->needle[0U], remaining))
	{
		const uint_fast32_t offset_curr = (uint_fast32_t)(haystack_off - param->search_param->haystack);
		const uint_fast32_t match_limit = min_uf32(param->search_param->needle_len, param->search_param->haystack_len - offset_curr);
		uint_fast32_t matching_len;
		for (matching_len = 1U; matching_len < match_limit; matching_len++)
		{
			if (haystack_off[matching_len] != param->search_param->needle[matching_len])
			{
				break; /*end of matching sequence*/
			}
		}
		if (matching_len > 1U)
		{
			const uint_fast32_t offset_diff = diff_uf32(offset_curr, param->search_param->prev_offset);
			if ((matching_len > param->result.substring.length) || (offset_diff < param->result.substring.offset_diff))
			{
				const float current_cost = compute_substring_cost(param->search_param->literal_len, matching_len, offset_diff);
				if (current_cost < param->result.cost)
				{
					param->result.substring.length = matching_len;
					param->result.substring.offset_diff = offset_diff;
					param->result.substring.offset_sign = (offset_curr >= param->search_param->prev_offset) ? SUBSTR_FWD : SUBSTR_BWD;
					param->result.cost = current_cost;
				}
			}
		}
		if (offset_curr < param->search_range.end)
		{
			++haystack_off;
			remaining = param->search_range.end - offset_curr;
		}
		else
		{
			break; /*end of range*/
		}
	}

	return 1U;
}

static inline float find_optimal_substring(substring_t *const substring, const uint_fast32_t literal_len, const uint_fast32_t prev_offset, thread_pool_t *const thread_pool, const uint8_t *const needle, const uint_fast32_t needle_len, const uint8_t *const haystack, const uint_fast32_t haystack_len)
{
	//Common search parameters
	const search_param_t search_param = { literal_len, prev_offset, needle, needle_len, haystack, haystack_len };

	//Set up per-thread parameters
	search_thread_t thread_param[MAX_THREAD_COUNT];
	memset(thread_param, 0, sizeof(thread_param));

	//Threads enabled?
	if ((thread_pool->thread_count < 2U) || (haystack_len <= 4096U))
	{
		thread_param[0U].search_param = &search_param;
		thread_param[0U].search_range.begin = 0U;
		thread_param[0U].search_range.end = haystack_len;
		find_optimal_substring_thread((uintptr_t)&thread_param[0U]);
		memcpy(substring, &thread_param[0U].result.substring, sizeof(substring_t));
		return thread_param[0U].result.cost;
	}

	//Compute step size
	const uint_fast32_t step_size = (haystack_len / thread_pool->thread_count) + 1U;

	//Start threads
	pool_task_t task_queue[MAX_THREAD_COUNT];
	uint_fast32_t range_offset = 0U;
	for (uint_fast32_t t = 0U; t < thread_pool->thread_count; ++t)
	{
		thread_param[t].search_param = &search_param;
		thread_param[t].search_range.begin = range_offset;
		thread_param[t].search_range.end = min_uf32(haystack_len, range_offset + step_size);
		range_offset = thread_param[t].search_range.end;
		task_queue[t].func = find_optimal_substring_thread;
		task_queue[t].data = (uintptr_t)(&thread_param[t]);
	}

	//Enqueue all tasks
	mpatch_pool_put_multiple(thread_pool, task_queue, thread_pool->thread_count);

	//Await completion
	mpatch_pool_await(thread_pool);

	//Find the "optimal" result
	uint_fast32_t thread_id = UINT_FAST32_MAX;
	float lowest_cost = FLT_MAX;
	for (uint_fast32_t t = 0U; t < thread_pool->thread_count; ++t)
	{
		if (thread_param[t].result.cost < lowest_cost)
		{
			thread_id = t;
			lowest_cost = thread_param[t].result.cost;
		}
	}

	//Return "optimal" result
	if (thread_id != UINT_FAST32_MAX)
	{
		memcpy(substring, &thread_param[thread_id].result.substring, sizeof(substring_t));
		return thread_param[thread_id].result.cost;
	}

	memset(substring, 0U, sizeof(substring_t));
	return FLT_MAX;
}

/* ======================================================================= */
/* Encoder functions                                                       */
/* ======================================================================= */

static const uint_fast32_t LITERAL_LIMIT = 255U;

typedef struct
{
	uint_fast32_t prev_offset_value[2U];
}
coder_state_t;

static uint_fast32_t encode_chunk(const mpatch_rd_buffer_t *const input_buffer, const uint_fast32_t input_pos, const mpatch_rd_buffer_t *const reference_buffer, const mpatch_writer_t *const output, io_state_t *const output_state, coder_state_t *const coder_state, thread_pool_t *const thread_pool, const uint_fast32_t step_size, const mpatch_logger_t *const logger)
{
	const uint_fast32_t remaining = input_buffer->capacity - input_pos;

	//Kepp the "optimal" settings
	uint_fast32_t optimal_literal_len;
	bool optimal_substr_select;
	substring_t optimal_substr = { 0U, 0U, false };

	//Initialize bit cost computation
	const uint_fast32_t literal_limit = min_uf32(LITERAL_LIMIT, remaining);
	float cost_optimal = compute_substring_cost(optimal_literal_len = literal_limit, 0U, 0U);

	//Find the "optimal" encoding of the next chunk
	for (uint_fast32_t literal_len = 0U; literal_len <= literal_limit; literal_len += step_size)
	{
		substring_t subst_data[2U];
		float cost_substr[2U];
		cost_substr[SUBSTR_SRC] = find_optimal_substring(&subst_data[SUBSTR_SRC], literal_len, coder_state->prev_offset_value[SUBSTR_SRC], thread_pool, input_buffer->buffer + input_pos + literal_len, remaining - literal_len, input_buffer->buffer, input_pos);
		cost_substr[SUBSTR_REF] = find_optimal_substring(&subst_data[SUBSTR_REF], literal_len, coder_state->prev_offset_value[SUBSTR_REF], thread_pool, input_buffer->buffer + input_pos + literal_len, remaining - literal_len, reference_buffer->buffer, reference_buffer->capacity);
		const bool substr_select = (cost_substr[SUBSTR_REF] < cost_substr[SUBSTR_SRC]) ? SUBSTR_REF : SUBSTR_SRC;
		if (cost_substr[substr_select] < cost_optimal)
		{
			optimal_substr_select = substr_select;
			optimal_literal_len = literal_len;
			memcpy(&optimal_substr, &subst_data[substr_select ? 1U : 0U], sizeof(substring_t));
			cost_optimal = cost_substr[substr_select];
		}
		else if (optimal_substr.length)
		{
			break; /*stop optimization process*/
		}
	}

	//Write detailed info to log
	if (logger->logging_func)
	{
		if (optimal_substr.length > 1U)
		{
			logger->logging_func("%016lu, %06.2f, %016lu, %016lu, %s, %s, %016lu\n", logger->user_data, input_pos, cost_optimal, optimal_literal_len,
				optimal_substr.length, optimal_substr_select ? "REF" : "SRC", optimal_substr.offset_diff ? (optimal_substr.offset_sign ? "-->" : "<--") : "~~~", optimal_substr.offset_diff);
		}
		else
		{
			logger->logging_func("%016lu, %06.2f, %016lu, %016lu\n", logger->user_data, input_pos, cost_optimal, optimal_literal_len, optimal_substr.length);
		}
	}

	//Write "optimal" encoding to output now!
	if (!exp_golomb_write(optimal_literal_len, output, output_state))
	{
		return 0U;
	}
	if (optimal_literal_len > 0U)
	{
		if (!write_bytes(input_buffer->buffer + input_pos, optimal_literal_len, output, output_state))
		{
			return 0U;
		}
	}
	if (optimal_substr.length > 1U)
	{
		if (!exp_golomb_write(optimal_substr.length - 1U, output, output_state))
		{
			return 0U;
		}
		if (!(write_bit(optimal_substr_select, output, output_state) && exp_golomb_write(optimal_substr.offset_diff, output, output_state)))
		{
			return 0U;
		}
		if (optimal_substr.offset_diff > 0U)
		{
			if (!write_bit(optimal_substr.offset_sign, output, output_state))
			{
				return 0U;
			}
		}
	}
	else
	{
		if (!exp_golomb_write(0U, output, output_state))
		{
			return 0U;
		}
	}

	//Update coder state
	if (optimal_substr.length > 1U)
	{
		if (optimal_substr.offset_diff)
		{
			coder_state->prev_offset_value[optimal_substr_select] = optimal_substr.offset_sign
				? (coder_state->prev_offset_value[optimal_substr_select] + optimal_substr.offset_diff)
				: (coder_state->prev_offset_value[optimal_substr_select] - optimal_substr.offset_diff);
		}
		coder_state->prev_offset_value[optimal_substr_select] += optimal_substr.length;
	}

	//Return total number of bytes
	return optimal_literal_len + optimal_substr.length;
}

/* ======================================================================= */
/* Header functions                                                        */
/* ======================================================================= */

typedef struct
{
	uint8_t time_create[4U];
	uint8_t fmt_version[4U];
	uint8_t length_msg[4U];
	uint8_t length_ref[4U];
	uint8_t crc32_msg[4U];
	uint8_t crc32_ref[4U];
	uint8_t digest_msg[16U];
	uint8_t digest_ref[16U];
}
header_fields_t;

typedef struct
{
	uint8_t magic_string[8U];
	header_fields_t hdr_fields;
	uint8_t checksum[16U];
}
header_t;

static bool write_header(const mpatch_writer_t *const output, const mpatch_rd_buffer_t *const input_buffer, const mpatch_rd_buffer_t *const reference_buffer)
{
	//Initialize header
	header_t header;
	memset(&header, 0, sizeof(header_t));

	//Copy "magic" number
	memcpy(&header.magic_string, MAGIC_HEADER, 8U);

	//Time stamp and version
	enc_uint32(header.hdr_fields.time_create, (uint32_t)(time(NULL) >> 1U));
	enc_uint32(header.hdr_fields.fmt_version, FILE_FORMAT_VERSION);

	//Message and reference
	enc_uint32(header.hdr_fields.length_msg, input_buffer->capacity);
	enc_uint32(header.hdr_fields.length_ref, reference_buffer->capacity);
	mpatch_crc32_compute(input_buffer->buffer, input_buffer->capacity, header.hdr_fields.crc32_msg);
	mpatch_crc32_compute(reference_buffer->buffer, reference_buffer->capacity, header.hdr_fields.crc32_ref);
	mpatch_md5_digest(input_buffer->buffer, input_buffer->capacity, header.hdr_fields.digest_msg);
	mpatch_md5_digest(reference_buffer->buffer, reference_buffer->capacity, header.hdr_fields.digest_ref);

	//Compute checksum
	mpatch_md5_digest((const uint8_t*)&header.hdr_fields, sizeof(header_fields_t), header.checksum);

	//Writer header
	if (!output->writer_func((const uint8_t*)&header, sizeof(header_t), output->user_data))
	{
		return false;
	}

	return true;
}

static mpatch_error_t read_header(mpatch_info_t *const info, const mpatch_reader_t *const input)
{
	//Try read the header
	header_t header;
	if (!input->reader_func((uint8_t*)&header, sizeof(header_t), input->user_data))
	{
		return MPATCH_IO_ERROR;
	}

	//Clear info
	memset(info, 0, sizeof(mpatch_info_t));

	//Check "magic" number
	if (memcmp(&header.magic_string, MAGIC_HEADER, 8U))
	{
		return MPATCH_BAD_FILE_FORMAT;
	}

	//Check format version
	dec_uint32(&info->fmt_version, header.hdr_fields.fmt_version);
	if (info->fmt_version != FILE_FORMAT_VERSION)
	{
		return MPATCH_BAD_FILE_VERSION;
	}

	//Validate header checksum
	uint8_t checksum[16U];
	mpatch_md5_digest((const uint8_t*)&header.hdr_fields, sizeof(header_fields_t), checksum);
	if (memcmp(&header.checksum, checksum, 16U))
	{
		return MPATCH_HEADER_CORRUPTED;
	}

	//Read values
	dec_uint32(&info->time_create, header.hdr_fields.time_create);
	dec_uint32(&info->length_msg, header.hdr_fields.length_msg);
	dec_uint32(&info->length_ref, header.hdr_fields.length_ref);

	//Copy checksums
	memcpy(info->crc32_msg, header.hdr_fields.crc32_msg, 4U);
	memcpy(info->crc32_ref, header.hdr_fields.crc32_ref, 4U);
	memcpy(info->digest_msg, header.hdr_fields.digest_msg, 16U);
	memcpy(info->digest_ref, header.hdr_fields.digest_ref, 16U);

	return MPATCH_SUCCESS;
}

/* ======================================================================= */
/* Footer functions                                                        */
/* ======================================================================= */

typedef struct
{
	uint8_t digest_enc[16U];
	uint8_t length_enc[4U];
	uint8_t crc32_enc[4U];
	uint8_t magic_string[8U];
}
footer_t;

static bool write_footer(const mpatch_writer_t *const output, io_state_t *const output_state)
{
	//Initialize footer
	footer_t footer;
	memset(&footer, 0, sizeof(footer_t));

	//Compute checksum
	mpatch_md5_final(&output_state->md5_ctx, footer.digest_enc);
	enc_uint32(footer.length_enc, output_state->byte_counter);
	mpatch_crc32_final(&output_state->crc32_ctx, footer.crc32_enc);

	//Copy "magic" number
	memcpy(&footer.magic_string, MAGIC_FOOTER, 8U);

	//Write padding
	const uint_fast32_t padding = (16U - (output_state->byte_counter & 0xF)) & 0xF;
	if (padding)
	{
		if (!output->writer_func(PADDING, padding, output->user_data))
		{
			return false;
		}
	}

	//Write footer
	if (!output->writer_func((const uint8_t*)&footer, sizeof(footer_t), output->user_data))
	{
		return false;
	}

	return true;
}

/* ======================================================================= */
/* Self-Test                                                               */
/* ======================================================================= */

#define TEST_FAIL(X) do \
{ \
	fputs("\nERROR: " X "\n", stderr); \
	fflush(stderr); \
	abort(); \
} \
while(0)

typedef struct 
{
	uint8_t *buffer;
	uint32_t capacity;
	uint32_t offset;
}
selftest_io_t;

static bool _selftest_writer(const uint8_t *const data, const uint32_t size, const uintptr_t user_data)
{
	selftest_io_t *const io = (selftest_io_t*)user_data;
	if (size > 1U)
	{
		if (io->capacity - io->offset < size)
		{
			memcpy(io->buffer + io->offset, data, size);
			io->offset += size;
			return true;
		}
	}
	else if (size == 1U)
	{
		if (io->offset < io->capacity)
		{
			io->buffer[io->offset++] = *data;
			return true;
		}
	}
	return false;
}

static bool _selftest_reader(uint8_t *const data, const uint32_t size, const uintptr_t user_data)
{
	selftest_io_t *const io = (selftest_io_t*)user_data;
	if (size > 1U)
	{
		if (io->capacity - io->offset < size)
		{
			memcpy(data, io->buffer + io->offset, size);
			io->offset += size;
			return true;
		}
	}
	else if (size == 1U)
	{
		if (io->offset < io->capacity)
		{
			*data = io->buffer[io->offset++];
			return true;
		}
	}
	return false;
}

static void selftest_bit_iofunc(void)
{
	//Init I/O routines
	selftest_io_t io = { NULL, 1024U, 0U };
	if (!(io.buffer = (uint8_t*)malloc(io.capacity * sizeof(uint8_t))))
	{
		TEST_FAIL("Memory allocation has failed!");
	}

	//Allocate buffers
	bool *const data_1 = (bool*)malloc(8192U * sizeof(bool));
	bool *const data_2 = (bool*)malloc(8192U * sizeof(bool));
	if (!(data_1 && data_2))
	{
		TEST_FAIL("Memory allocation has failed!");
	}

	//Generate test data
	srand(666);
	for (size_t i = 0; i < 8192U; ++i)
	{
		data_1[i] = (rand() >(RAND_MAX / 2));
	}

	//Write data
	const mpatch_writer_t writer = { _selftest_writer, (uintptr_t)&io };
	io_state_t wr_state;
	init_io_state(&wr_state);
	for (size_t i = 0; i < 8192U; ++i)
	{
		if (!write_bit(data_1[i], &writer, &wr_state))
		{
			TEST_FAIL("Failed to write bit!");
		}
	}

	//Rewind the I/O buffer
	flush_state(&writer, &wr_state);
	io.offset = 0U;

	//Read data
	const mpatch_reader_t reader = { _selftest_reader, (uintptr_t)&io };
	io_state_t rd_state;
	init_io_state(&rd_state);
	memset(data_2, 0, 8192U * sizeof(bool));
	for (size_t i = 0; i < 8192; ++i)
	{
		bool bitval;
		if (!read_bit(&bitval, &reader, &rd_state))
		{
			TEST_FAIL("Failed to read bit!");
		}
		data_2[i] = bitval;
	}

	//Validate data
	for (size_t i = 0; i < 8192U; ++i)
	{
		if (data_1[i] != data_2[i])
		{
			TEST_FAIL("Data validation has failed!");
		}
	}

	//Clean-up memory
	free(data_1);
	free(data_2);
	free(io.buffer);
}

static void selftest_exp_golomb(void)
{
	const uint_fast32_t MAX_TEST_VALUE = 4211U;

	//Init I/O routines
	selftest_io_t io = { NULL, 32768U, 0U };
	if (!(io.buffer = (uint8_t*)malloc(io.capacity * sizeof(uint8_t))))
	{
		TEST_FAIL("Memory allocation has failed!");
	}

	//Write numbers
	const mpatch_writer_t writer = { _selftest_writer, (uintptr_t)&io };
	io_state_t wr_state;
	init_io_state(&wr_state);
	for (uint_fast32_t i = 0U; i < MAX_TEST_VALUE; ++i)
	{
		if (!(exp_golomb_write(i, &writer, &wr_state) && write_byte((uint8_t)i, &writer, &wr_state)))
		{
			TEST_FAIL("Failed to write number!");
		}
	}
	for (uint_fast32_t i = MAX_TEST_VALUE; i > 0U; --i)
	{
		if (!(exp_golomb_write(i, &writer, &wr_state) && write_byte((uint8_t)i, &writer, &wr_state)))
		{
			TEST_FAIL("Failed to write number!");
		}
	}

	//Rewind the I/O buffer
	flush_state(&writer, &wr_state);
	io.offset = 0U;

	//Read numbers (and validate)
	const mpatch_reader_t reader = { _selftest_reader, (uintptr_t)&io };
	io_state_t rd_state;
	init_io_state(&rd_state);
	for (uint_fast32_t i = 0U; i < MAX_TEST_VALUE; ++i)
	{
		uint_fast32_t value_ui32;
		uint8_t value_byte;
		if (!(exp_golomb_read(&value_ui32, &reader, &rd_state) && read_byte(&value_byte, &reader, &rd_state)))
		{
			TEST_FAIL("Failed to read number!");
		}
		if ((value_ui32 != i) || (value_byte != (uint8_t)i))
		{
			TEST_FAIL("Data validation has failed!");
		}
	}
	for (uint_fast32_t i = MAX_TEST_VALUE; i > 0U; --i)
	{
		uint_fast32_t value_ui32;
		uint8_t value_byte;
		if (!(exp_golomb_read(&value_ui32, &reader, &rd_state) && read_byte(&value_byte, &reader, &rd_state)))
		{
			TEST_FAIL("Failed to read number!");
		}
		if ((value_ui32 != i) || (value_byte != (uint8_t)i))
		{
			TEST_FAIL("Data validation has failed!");
		}
	}

	//Clean-up memory
	free(io.buffer);
}

static void selftest_bit_md5dig(void)
{
	static const char *const PLAINTEXT[4U] =
	{
		"",
		"The quick brown fox jumps over the lazy dog",
		"The quick brown fox jumps over the lazy dog.",
		"^*jFwAwz[-V3qmka.dI(!NHE~]Zyqv:@(/_o^P-8{Q"
	};

	static const char DIGEST[4U][16U] =
	{
		{ 0xD4, 0x1D, 0x8C, 0xD9, 0x8F, 0x00, 0xB2, 0x04, 0xE9, 0x80, 0x09, 0x98, 0xEC, 0xF8, 0x42, 0x7E },
		{ 0x9E, 0x10, 0x7D, 0x9D, 0x37, 0x2B, 0xB6, 0x82, 0x6B, 0xD8, 0x1D, 0x35, 0x42, 0xA4, 0x19, 0xD6 },
		{ 0xE4, 0xD9, 0x09, 0xC2, 0x90, 0xD0, 0xFB, 0x1C, 0xA0, 0x68, 0xFF, 0xAD, 0xDF, 0x22, 0xCB, 0xD0 },
	    { 0x78, 0x2F, 0x22, 0x65, 0x84, 0xBF, 0xE3, 0x71, 0xFF, 0xF0, 0xD0, 0x11, 0x69, 0x62, 0x12, 0x10 }
	};

	for (size_t k = 0; k < 4U; ++k)
	{
		uint8_t digest[16U];
		mpatch_md5_digest(PLAINTEXT[k], (uint_fast32_t)strlen(PLAINTEXT[k]), digest);
		if (memcmp(digest, DIGEST[k], 16U))
		{
			TEST_FAIL("Data validation has failed!");
		}
	}
}

static void selftest_bit_crc32c(void)
{
	static const char *const PLAINTEXT[3] =
	{
		"",
		"The quick brown fox jumps over the lazy dog",
		"The quick brown fox jumps over the lazy dog."
	};

	static const char DIGEST[3][16] =
	{
		{ 0x00, 0x00, 0x00, 0x00 },
		{ 0x41, 0x4F, 0xA3, 0x39 },
		{ 0x51, 0x90, 0x25, 0xE9 }
	};

	for (size_t k = 0; k < 3U; ++k)
	{
		uint8_t digest[4];
		mpatch_crc32_compute(PLAINTEXT[k], (uint_fast32_t)strlen(PLAINTEXT[k]), digest);
		if (memcmp(digest, DIGEST[k], 4U))
		{
			TEST_FAIL("Data validation has failed!");
		}
	}
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
	//Sanity check parameters
	if (!param)
	{
		return MPATCH_INVALID_PARAMETER;
	}
	if ((!param->message_in.buffer) || (!param->reference_in.buffer) || (!param->compressed_out.writer_func) || (param->message_in.capacity < 1U) || (param->reference_in.capacity < 1U))
	{
		return MPATCH_INVALID_PARAMETER;
	}
	if ((param->thread_count > MAX_THREAD_COUNT) || (param->speed_parameter > MAX_SPEED_PARAMETER))
	{
		return MPATCH_INVALID_PARAMETER;
	}

	//Initial progress update
	if (param->callback.callback_func)
	{
		if (!param->callback.callback_func(0.0, 1.0, param->callback.user_data))
		{
			return MPATCH_CANCELLED_BY_USER;
		}
	}

	//Create thread pool
	thread_pool_t thread_pool;
	memset(&thread_pool, 0U, sizeof(thread_pool_t));
	if (param->thread_count > 1U)
	{
		if (!mpatch_pool_create(&thread_pool, param->thread_count))
		{
			return MPATCH_INTERNAL_ERROR;
		}
	}

	//Write header
	if (!write_header(&param->compressed_out, &param->message_in, &param->reference_in))
	{
		return MPATCH_IO_ERROR;
	}

	//Initialize the step size
	const uint_fast32_t step_size = (1U << (MAX_SPEED_PARAMETER - param->speed_parameter));

	//Log the parameters
	if (param->trace_logger.logging_func)
	{
		param->trace_logger.logging_func("thread_count: %u\n", param->trace_logger.user_data, param->thread_count);
		param->trace_logger.logging_func("speed_parameter: %u\n", param->trace_logger.user_data, param->speed_parameter);
		param->trace_logger.logging_func("step_size: %u\n", param->trace_logger.user_data, step_size);
	}

	//Initialize encoder state
	io_state_t output_state;
	init_io_state(&output_state);
	coder_state_t coder_state;
	memset(&coder_state, 0, sizeof(coder_state));
	uint_fast32_t input_pos = 0U;
	time_t last_update = time(NULL);
		
	//Process input
	while (input_pos < param->message_in.capacity)
	{
		if (param->callback.callback_func)
		{
			const time_t current_time = time(NULL);
			if (current_time != last_update)
			{
				if (!param->callback.callback_func((float)input_pos / param->message_in.capacity, min_flt(999.99f, (float)output_state.byte_counter / input_pos), param->callback.user_data))
				{
					return MPATCH_CANCELLED_BY_USER;
				}
				last_update = current_time;
			}
		}
		const uint_fast32_t chunk_len = encode_chunk(&param->message_in, input_pos, &param->reference_in, &param->compressed_out, &output_state, &coder_state, &thread_pool, step_size, &param->trace_logger);
		if (!chunk_len)
		{
			return MPATCH_IO_ERROR;
		}
		input_pos += chunk_len;
	}

	//Flush encoder state
	if (!flush_state(&param->compressed_out, &output_state))
	{
		return MPATCH_IO_ERROR;
	}

	//Write footer
	if (!write_footer(&param->compressed_out, &output_state))
	{
		return MPATCH_IO_ERROR;
	}

	//Final progress update
	if (param->callback.callback_func)
	{
		param->callback.callback_func(1.0, min_flt(999.99f, (float)output_state.byte_counter / param->message_in.capacity), param->callback.user_data);
	}

	//Destroy thread pool
	if (thread_pool.thread_count)
	{
		mpatch_pool_destroy(&thread_pool);
	}

	return MPATCH_SUCCESS;
}

mpatch_error_t mpatch_getnfo(mpatch_nfo_param_t *const param)
{
	//Sanity check parameters
	if ((!param) || (!param->compressed_in.reader_func))
	{
		return MPATCH_INVALID_PARAMETER;
	}

	//Read the header
	return read_header(&param->file_info, &param->compressed_in);
}

mpatch_error_t mpatch_decode(mpatch_dec_param_t *const param)
{
	//Sanity check parameters
	if (!param)
	{
		return MPATCH_INVALID_PARAMETER;
	}
	if ((!param->compressed_in.reader_func) || (!param->reference_in.buffer) || (!param->message_out.buffer) || (param->message_out.capacity < 1U) || (param->reference_in.capacity < 1U))
	{
		return MPATCH_INVALID_PARAMETER;
	}

	//Initial progress update
	if (param->callback.callback_func)
	{
		if (!param->callback.callback_func(0.0, 1.0, param->callback.user_data))
		{
			return MPATCH_CANCELLED_BY_USER;
		}
	}

	//Try to read the header info
	mpatch_info_t file_info;
	const mpatch_error_t hdr_result = read_header(&file_info, &param->compressed_in);
	if (hdr_result != MPATCH_SUCCESS)
	{
		return hdr_result;
	}

	return MPATCH_SUCCESS;
}

void mpatch_selftest()
{
	selftest_bit_iofunc();
	selftest_exp_golomb();
	selftest_bit_crc32c();
	selftest_bit_md5dig();
}

void mpatch_get_limits(mpatch_limit_t *const limits)
{
	memset(limits, 0U, sizeof(mpatch_limit_t));
	limits->max_speed_parameter = MAX_SPEED_PARAMETER;
	limits->max_thread_count = MAX_THREAD_COUNT;
}
