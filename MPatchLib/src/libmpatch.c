// MPatchLib.cpp : Defines the entry point for the console application.
//

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <memory.h>

#define BOOLIFY(X) (!!(X))

//TEST
#include <stdio.h>

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
	if (state->bit_pos > 7U)
	{
		if (state->bit_pos != UINT_FAST8_MAX)
		{
			if (!_write_byte(state->value, buffer, state))
			{
				return false;
			}
		}
		state->bit_pos = state->value = 0U;
	}
	if (value)
	{
		state->value |= BIT_MASK[state->bit_pos];
	}
	state->bit_pos++;
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



/* ======================================================================= */
/* Public Interface                                                        */
/* ======================================================================= */

int mpatch_test()
{
	bool data[8192U], data2[8192U];
	uint8_t test[1024U];

	memset(data2, 0, 8192U * sizeof(bool));
	srand(666);
	for (size_t i = 0; i < 8192U; ++i)
	{
		data[i] = (rand() > (RAND_MAX / 2));
		printf("%d", data[i]);
	}
	puts("");

	const wr_buffer_t  wr_buffer = { test, 1024U };
	bit_state_t state;
	init_bit_state(&state);
	for (size_t i = 0; i < 8192U; ++i)
	{
		if (!write_bit(data[i], &wr_buffer, &state))
		{
			abort();
		}
	}
	flush_state(&wr_buffer, &state);

	for (size_t i = 0; i < 1024U; ++i)
	{
		printf("%02X", test[i]);
	}
	puts("");

	const rd_buffer_t rd_buffer = { test, 1024U };
	init_bit_state(&state);
	for (size_t i = 0; i < 8192; ++i)
	{
		bool bitval;
		if (!read_bit(&bitval, &rd_buffer, &state))
		{
			abort();
		}
		data2[i] = bitval;
	}

	for (size_t i = 0; i < 8192U; ++i)
	{
		printf("%d%d-", data[i], data2[i]);
		if (data[i] != data2[i])
		{
			printf("\nMismatch at i=%zu\n", i);
			abort();
		}
	}
	puts("");

	return 42;
}

