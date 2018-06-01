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

#ifndef _INC_MPATCH_BITIO_H
#define _INC_MPATCH_BITIO_H

#include <stdint.h>

#include "rhash/md5.h"
#include "rhash/crc32.h"

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

#endif /*_INC_MPATCH_BITIO_H*/