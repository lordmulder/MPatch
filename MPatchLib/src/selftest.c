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

#include "libmpatch.h"
#include "utils.h"
#include "bit_io.h"

#include <stdlib.h>
#include <malloc.h>

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
	static const char *const PLAINTEXT[4U] =
	{
		"",
		"The quick brown fox jumps over the lazy dog",
		"The quick brown fox jumps over the lazy dog.",
		"^*jFwAwz[-V3qmka.dI(!NHE~]Zyqv:@(/_o^P-8{Q"
	};

	static const char DIGEST[4U][16U] =
	{
		{ 0x00, 0x00, 0x00, 0x00 },
		{ 0x41, 0x4F, 0xA3, 0x39 },
		{ 0x51, 0x90, 0x25, 0xE9 },
		{ 0x73, 0xE5, 0x5E, 0x31 }
	};

	for (size_t k = 0; k < 4U; ++k)
	{
		uint8_t digest[4U];
		mpatch_crc32_compute(PLAINTEXT[k], (uint_fast32_t)strlen(PLAINTEXT[k]), digest);
		if (memcmp(digest, DIGEST[k], 4U))
		{
			TEST_FAIL("Data validation has failed!");
		}
	}
}

void mpatch_selftest()
{
	selftest_bit_iofunc();
	selftest_exp_golomb();
	selftest_bit_crc32c();
	selftest_bit_md5dig();
}
