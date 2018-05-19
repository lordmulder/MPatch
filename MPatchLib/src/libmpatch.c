// MPatchLib.cpp : Defines the entry point for the console application.
//

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

#define BOOLIFY(X) (!!(X))

/* ======================================================================= */
/* Bit I/O                                                                 */
/* ======================================================================= */

static const uint8_t BIT_MASK[8U] = { 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80 };

typedef struct
{
	const uint8_t *const buffer;
	uint64_t capacity;
}
rd_buffer_t;

typedef struct
{
	uint8_t *const buffer;
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

static bool read_bit(bool *const value, const rd_buffer_t *const buffer, bit_state_t *const state)
{
	if (state->bit_pos > 7U)
	{
		if (!read_byte(&state->value, buffer, state))
		{
			*value = 0U;
			return false;
		}
	}
	*value = BOOLIFY(state->value & BIT_MASK[state->bit_pos++]);
	return true;
}

static bool write_bit(const bool value, const wr_buffer_t *const buffer, bit_state_t *const state)
{
	if (state->bit_pos > 7U)
	{
		if (!write_byte(state->value, buffer, state))
		{
			return false;
		}
		state->bit_pos = 0U;
	}
	if (value)
	{
		state->value |= BIT_MASK[state->bit_pos];
	}
	state->bit_pos++;
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
    return 42;
}

