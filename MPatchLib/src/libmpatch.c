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

#define BOOLIFY(X) (!!(X))

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

static bool _read_byte(uint8_t *const value, const rd_buffer_t *const buffer, bit_state_t *const state)
{
	if (state->byte_pos < buffer->capacity)
	{
		*value = buffer->buffer[state->byte_pos++];
		return true;
	}
	*value = 0U;
	return false;
}

static bool _write_byte(const uint8_t value, const wr_buffer_t *const buffer, bit_state_t *const state)
{
	if (state->byte_pos < buffer->capacity)
	{
		buffer->buffer[state->byte_pos++] = value;
		return true;
	}
	return false;
}

static init_bit_state(bit_state_t *const state)
{
	memset(state, 0, sizeof(bit_state_t));
	state->bit_pos = UINT_FAST8_MAX;
}

static bool read_bit(bool *const value, const rd_buffer_t *const buffer, bit_state_t *const state)
{
	if (state->bit_pos > 7U)
	{
		if (!_read_byte(&state->value, buffer, state))
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
		if (!_write_byte(state->value, buffer, state))
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
		if (!_write_byte(state->value, buffer, state))
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
/* Self-Test                                                               */
/* ======================================================================= */

static void test_exp_golomb(void)
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

static void test_bit_iofunc(void)
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

mpatch_error_t mpatch_encode(mpatch_enc_param_t *const param)
{
	if (!param)
	{
		return MPATCH_INVALID_PARAMETER;
	}
	if ((!param->message_in) || (!param->reference_in) || (!param->compressed_out))
	{
		param->compressed_size = 0U;
		return MPATCH_INVALID_PARAMETER;
	}
	if((param->message_size < 1U) || (param->message_size == UINT64_MAX) || (param->reference_size < 1U) || (param->compressed_capacity < 1U))
	{
		param->compressed_size = 0U;
		return MPATCH_INVALID_PARAMETER;
	}

	const rd_buffer_t input_buffer = { param->message_in, param->message_size }, reference_buffer = { param->reference_in, param->reference_size };
	const wr_buffer_t output_buffer = { param->compressed_out, param->compressed_capacity };

	uint64_t input_pos = 0U;
	while (input_pos < input_buffer.capacity)
	{
	}

	return MPATCH_SUCCESS;
}

mpatch_error_t mpatch_decode(mpatch_dec_param_t *const param)
{
	return MPATCH_INVALID_PARAMETER;
}

void mpatch_selftest()
{
	test_bit_iofunc();
	test_exp_golomb();
}
