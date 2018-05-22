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
#include "md5.h"

#include <stdlib.h>
#include <stdbool.h>
#include <memory.h>
#include <string.h>
#include <time.h>
#include <float.h>

static const uint8_t MAGIC_NUMBER[16] = { 0x4D, 0x50, 0x41, 0x54, 0x43, 0x48, 0x21, 0x00, 0xA5, 0x67, 0x55, 0x4B, 0x31, 0x0C, 0xA2, 0x78 };
static const uint8_t ZEROS[16] =        { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

/* ======================================================================= */
/* Version                                                                 */
/* ======================================================================= */

static const uint16_t VERSION_MAJOR = 1U;
static const uint16_t VERSION_MINOR = 0U;
static const uint16_t VERSION_PATCH = 0U;

static const char *const BUILD_DATE = __DATE__;
static const char *const BUILD_TIME = __TIME__;

static const uint16_t FILE_FORMAT_VER = 1U;

/* ======================================================================= */
/* Utilities                                                               */
/* ======================================================================= */

#define BOOLIFY(X) (!!(X))

static inline uint_fast32_t min_uf32(const uint_fast32_t a, const uint_fast32_t b)
{
	return (a < b) ? a : b;
}

static inline float min_flt(const float a, const float b)
{
	return (a < b) ? a : b;
}


static inline uint_fast32_t diff_uf32(const uint_fast32_t a, const uint_fast32_t b)
{
	return (a > b) ? (a - b) : (b - a);
}

static inline void enc_uint64(const uint64_t value, uint8_t *const buffer)
{
	static const size_t SHIFT[8] = { 56U, 48U, 40U, 32U, 24U, 16U, 8U, 0U };
	for (size_t i = 0; i < 8U; ++i)
	{
		buffer[i] = (uint8_t)(value >> SHIFT[i]);
	}
}

/* ======================================================================= */
/* Bit I/O                                                                 */
/* ======================================================================= */

static const uint8_t BIT_MASK[8U] = { 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80 };

typedef struct
{
	const uint8_t *buffer;
	uint_fast32_t capacity;
}
rd_buffer_t;

typedef struct
{
	uint8_t *buffer;
	uint_fast32_t capacity;
}
wr_buffer_t;

typedef struct
{
	uint_fast8_t bit_pos;
	uint_fast32_t byte_pos;
	uint8_t value;
}
bit_state_t;

static inline bool read_byte(uint8_t *const value, const rd_buffer_t *const buffer, bit_state_t *const state)
{
	if (state->byte_pos < buffer->capacity)
	{
		*value = buffer->buffer[state->byte_pos++];
		return true;
	}
	*value = 0U;
	return false;
}

static inline bool write_byte(const uint8_t value, const wr_buffer_t *const buffer, bit_state_t *const state)
{
	if (state->byte_pos < buffer->capacity)
	{
		buffer->buffer[state->byte_pos++] = value;
		return true;
	}
	return false;
}

static inline void init_bit_state(bit_state_t *const state)
{
	memset(state, 0, sizeof(bit_state_t));
	state->bit_pos = UINT_FAST8_MAX;
}

static inline bool read_bit(bool *const value, const rd_buffer_t *const buffer, bit_state_t *const state)
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

static inline bool write_bit(const bool value, const wr_buffer_t *const buffer, bit_state_t *const state)
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

static inline bool flush_state(const wr_buffer_t *const buffer, bit_state_t *const state)
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

static inline bool put_bytes(const uint8_t *data, const uint_fast32_t len, const wr_buffer_t *const buffer, bit_state_t *const state)
{
	for (uint_fast32_t i = 0U; i < len; ++i, ++data)
	{
		if (!write_byte(*data, buffer, state))
		{
			return false;
		}
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

static inline bool exp_golomb_write(uint_fast32_t value, const wr_buffer_t *const buffer, bit_state_t *const state)
{
	uint_fast32_t temp = 0U;
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

static inline int exp_golomb_read(uint_fast32_t *const value, const rd_buffer_t *const buffer, bit_state_t *const state)
{
	uint_fast32_t temp = 0U;
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
	uint_fast32_t length;
	uint_fast32_t offset;
	bool direct;
}
substring_t;

static __forceinline float compute_substring_cost(const uint_fast32_t literal_len, const uint_fast32_t substr_len, const uint_fast32_t substr_off_delta)
{
	const uint_fast32_t total_bits = (substr_len > 0U)
		? (exp_golomb_size(literal_len) + (8U * literal_len) + exp_golomb_size(substr_len) + exp_golomb_size(substr_off_delta) + (substr_off_delta ? 2U : 1U))
		: (exp_golomb_size(literal_len) + (8U * literal_len) + 1U);
	return ((float)total_bits) / ((float)(literal_len + substr_len));
}

static __forceinline uint_fast32_t find_next_byte_pos(const uint8_t value, const uint8_t *const buffer, const uint_fast32_t len, const uint_fast32_t offset)
{
	const uint8_t *const ptr = (uint8_t*)memchr(buffer + offset, value, (size_t)(len - offset));
	return ptr ? (uint_fast32_t)(ptr - buffer) : UINT_FAST32_MAX;
}

static inline float find_optimal_substring(substring_t *const substring, const uint_fast32_t literal_len, const uint_fast32_t last_offset, const uint8_t *const needle, const uint_fast32_t needle_len, const uint8_t *const haystack, const uint_fast32_t haystack_len)
{
	substring->length = 0U;
	substring->offset = UINT_FAST32_MAX;
	float cost = FLT_MAX;

	uint_fast32_t haystack_off = 0U;
	while ((haystack_off = find_next_byte_pos(needle[0], haystack, haystack_len, haystack_off)) != UINT_FAST32_MAX)
	{
		const uint_fast32_t match_limit = min_uf32(needle_len, haystack_len - haystack_off);
		uint_fast32_t matching_len;
		for (matching_len = 1U; matching_len < match_limit; matching_len++)
		{
			if (haystack[haystack_off + matching_len] != needle[matching_len])
			{
				break; /*end of matching sequence*/
			}
		}
		if (matching_len > 1U)
		{
			const uint_fast32_t offset_delta = diff_uf32(haystack_off, last_offset);
			if ((matching_len > substring->length) || (offset_delta < substring->offset))
			{
				const bool direction = (haystack_off > last_offset);
				const float new_cost = compute_substring_cost(literal_len, matching_len - 1U, offset_delta);
				if (new_cost < cost)
				{
					substring->length = matching_len;
					substring->offset = offset_delta;
					substring->direct = direction;
					cost = new_cost;
				}
			}
		}
		++haystack_off;
	}

	return cost;
}

/* ======================================================================= */
/* Encode and decode functions                                             */
/* ======================================================================= */

typedef struct
{
	uint_fast32_t last_offset[2];
}
coder_state_t;

static uint_fast32_t encode_chunk(const rd_buffer_t *const input_buffer, const uint_fast32_t input_pos, const rd_buffer_t *const reference_buffer, const wr_buffer_t *const output_buffer, bit_state_t *const output_state, coder_state_t *const coder_state, FILE *const log_file)
{
	const uint_fast32_t remaining = input_buffer->capacity - input_pos;
	uint_fast32_t optimal_literal_len;
	substring_t optimal_substr = { 0U, 0U, false };
	bool optimal_select_bit = false;

	//Init bit cost computation
	float cost_optimal = compute_substring_cost(optimal_literal_len = remaining, 0U, 0U);

	//Find the "optimal" encoding of the next chunk
	bool stop_flag = false;
	for (uint_fast32_t literal_len = 0U; literal_len <= remaining; ++literal_len)
	{
		substring_t substring_data[2];
		const float cost_substr[2] =
		{
			find_optimal_substring(&substring_data[0], literal_len, coder_state->last_offset[0], input_buffer->buffer + input_pos + literal_len, remaining - literal_len, input_buffer->buffer, input_pos),
			find_optimal_substring(&substring_data[1], literal_len, coder_state->last_offset[1], input_buffer->buffer + input_pos + literal_len, remaining - literal_len, reference_buffer->buffer, reference_buffer->capacity)
		};
		const bool substr_select = (cost_substr[1U] < cost_substr[0U]);
		const float new_substr_cost = cost_substr[substr_select ? 1U : 0U];
		if (new_substr_cost < cost_optimal)
		{
			optimal_literal_len = literal_len;
			optimal_select_bit = substr_select;
			memcpy(&optimal_substr, &substring_data[substr_select ? 1U : 0U], sizeof(substring_t));
			cost_optimal = new_substr_cost;
			stop_flag = true;
		}
		else if (stop_flag)
		{
			break; /*stop optimization at this point*/
		}
	}

	//Write detailed info to log
	if (log_file)
	{
		if (optimal_substr.length)
		{
			fprintf(log_file, "%016lu, %06.2f, %016lu, %016lu, %s, %s, %016lu\n", input_pos, cost_optimal, optimal_literal_len, optimal_substr.length, optimal_select_bit ? "REF" : "SRC", optimal_substr.offset ? (optimal_substr.direct ? "-->" : "<--") : "~~~", optimal_substr.offset);
		}
		else
		{
			fprintf(log_file, "%016lu, %06.2f, %016lu, %016lu\n", input_pos, cost_optimal, optimal_literal_len, optimal_substr.length);
		}
		fflush(log_file);
	}

	//Write "optimal" encoding to output now!
	if (!exp_golomb_write(optimal_literal_len, output_buffer, output_state))
	{
		return UINT64_MAX;
	}
	if (optimal_literal_len > 0U)
	{
		if (!put_bytes(input_buffer->buffer + input_pos, optimal_literal_len, output_buffer, output_state))
		{
			return UINT64_MAX;
		}
	}
	if (optimal_substr.length > 1U)
	{
		if (!exp_golomb_write(optimal_substr.length - 1U, output_buffer, output_state))
		{
			return UINT64_MAX;
		}
		if (!(write_bit(optimal_select_bit, output_buffer, output_state) && exp_golomb_write(optimal_substr.offset, output_buffer, output_state)))
		{
			return UINT64_MAX;
		}
		if (optimal_substr.offset > 0U)
		{
			if (!write_bit(optimal_substr.direct, output_buffer, output_state))
			{
				return UINT64_MAX;
			}
		}
	}
	else
	{
		if (!exp_golomb_write(0U, output_buffer, output_state))
		{
			return UINT64_MAX;
		}
	}

	//Update coder state
	if (optimal_substr.length > 0U)
	{
		coder_state->last_offset[optimal_select_bit ? 1 : 0] = optimal_substr.direct
			? (coder_state->last_offset[optimal_select_bit ? 1 : 0] + optimal_substr.length + optimal_substr.offset)
			: (coder_state->last_offset[optimal_select_bit ? 1 : 0] + optimal_substr.length - optimal_substr.offset);
	}

	//Return total number of bytes
	return optimal_literal_len + optimal_substr.length;
}

/* ======================================================================= */
/* Header functions                                                        */
/* ======================================================================= */

static bool write_header(const wr_buffer_t *const output_buffer, bit_state_t *const output_state, const rd_buffer_t *const input_buffer, const rd_buffer_t *const reference_buffer)
{
	//Write magic number
	if (!put_bytes(MAGIC_NUMBER, 16U, output_buffer, output_state))
	{
		return false;
	}

	//Write header fields
	uint8_t header_flags_1[8], header_flags_2[8], size_input[8], size_reference[8], digest_input[16], digest_reference[16];
	enc_uint64(((uint64_t)(FILE_FORMAT_VER & 0xFFFF)) << 48U, header_flags_1);
	enc_uint64(0U, header_flags_2);
	enc_uint64(input_buffer->capacity, size_input);
	MD5_Digest(input_buffer->buffer, input_buffer->capacity, digest_input);
	enc_uint64(reference_buffer->capacity, size_reference);
	MD5_Digest(reference_buffer->buffer, reference_buffer->capacity, digest_reference);
	if (!(put_bytes(header_flags_1, 8U, output_buffer, output_state) && put_bytes(header_flags_2, 8U, output_buffer, output_state)))
	{
		return false;
	}
	if (!(put_bytes(size_input, 8U, output_buffer, output_state) && put_bytes(size_reference, 8U, output_buffer, output_state)))
	{
		return false;
	}
	if (!(put_bytes(digest_input, 16U, output_buffer, output_state) && put_bytes(digest_reference, 16U, output_buffer, output_state)))
	{
		return false;
	}

	//Write the checksum
	uint8_t digest_checksum[16];
	MD5_CTX ctx_checksum;
	MD5_Init(&ctx_checksum);
	MD5_Update(&ctx_checksum, MAGIC_NUMBER, 16U);
	MD5_Update(&ctx_checksum, header_flags_1, 8U);
	MD5_Update(&ctx_checksum, header_flags_2, 8U);
	MD5_Update(&ctx_checksum, size_input, 8U);
	MD5_Update(&ctx_checksum, size_reference, 8U);
	MD5_Update(&ctx_checksum, digest_input, 16U);
	MD5_Update(&ctx_checksum, digest_reference, 16U);
	MD5_Final(digest_checksum, &ctx_checksum);
	if (!put_bytes(digest_checksum, 16U, output_buffer, output_state))
	{
		return false;
	}

	//TEST
	if (!put_bytes(ZEROS, 16U, output_buffer, output_state))
	{
		return false;
	}
	
	return true;
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
	for (uint_fast32_t i = 1U; i <= 9999U; ++i)
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
	for (uint_fast32_t i = 1U; i < UINT64_MAX; ++i)
	{
		uint_fast32_t value;
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

static void selftest_bit_md5dig(void)
{
	static const char *const PLAINTEXT[3] =
	{
		"",
		"The quick brown fox jumps over the lazy dog",
		"The quick brown fox jumps over the lazy dog."
	};

	static const char DIGEST[3][16] =
	{
		{ 0xD4, 0x1D, 0x8C, 0xD9, 0x8F, 0x00, 0xB2, 0x04, 0xE9, 0x80, 0x09, 0x98, 0xEC, 0xF8, 0x42, 0x7E },
		{ 0x9E, 0x10, 0x7D, 0x9D, 0x37, 0x2B, 0xB6, 0x82, 0x6B, 0xD8, 0x1D, 0x35, 0x42, 0xA4, 0x19, 0xD6 },
		{ 0xE4, 0xD9, 0x09, 0xC2, 0x90, 0xD0, 0xFB, 0x1C, 0xA0, 0x68, 0xFF, 0xAD, 0xDF, 0x22, 0xCB, 0xD0 }
	};

	for (size_t k = 0; k < 3U; ++k)
	{
		uint8_t digest[16];
		MD5_Digest(PLAINTEXT[k], (uint_fast32_t)strlen(PLAINTEXT[k]), digest);
		if (memcmp(digest, DIGEST[k], 16U))
		{
			abort();
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
	if (!param)
	{
		return MPATCH_INVALID_PARAMETER;
	}

	param->compressed_size = 0U;

	if ((!param->message_in) || (!param->reference_in) || (!param->compressed_out) || (param->message_size < 1U) || (param->reference_size < 1U) || (param->compressed_capacity < 1U))
	{
		return MPATCH_INVALID_PARAMETER;
	}

	const rd_buffer_t input_buffer = { param->message_in, param->message_size }, reference_buffer = { param->reference_in, param->reference_size };
	const wr_buffer_t output_buffer = { param->compressed_out, param->compressed_capacity };

	bit_state_t output_state;
	init_bit_state(&output_state);

	coder_state_t coder_state;
	memset(&coder_state, 0, sizeof(coder_state));

	uint_fast32_t input_pos = 0U;
	time_t last_update = time(NULL);

	if (!param->callback(0.0, 1.0, param->user_data))
	{
		return MPATCH_CANCELLED_BY_USER;
	}

	//Write header
	if (!write_header(&output_buffer, &output_state, &input_buffer, &reference_buffer))
	{
		return MPATCH_INSUFFICIENT_BUFFER;
	}
	
	//Process input
	while (input_pos < input_buffer.capacity)
	{
		if (param->callback)
		{
			time_t current_time = time(NULL);
			if (current_time != last_update)
			{
				if (!param->callback((float)input_pos / input_buffer.capacity, min_flt(999.99, (float)output_state.byte_pos / input_pos), param->user_data))
				{
					return MPATCH_CANCELLED_BY_USER;
				}
				last_update = current_time;
			}
		}
		const uint_fast32_t chunk_len = encode_chunk(&input_buffer, input_pos, &reference_buffer, &output_buffer, &output_state, &coder_state, param->trace_log_file);
		if (!chunk_len)
		{
			return MPATCH_INSUFFICIENT_BUFFER;
		}
		input_pos += chunk_len;
	}

	if (!flush_state(&output_buffer, &output_state))
	{
		return MPATCH_INSUFFICIENT_BUFFER;
	}

	if (param->callback)
	{
		param->callback(1.0, min_flt(999.99, (float)output_state.byte_pos / input_buffer.capacity), param->user_data);
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
	selftest_bit_md5dig();
}
