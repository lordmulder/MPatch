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

#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
	MPATCH_SUCCESS             = 0,
	MPATCH_INVALID_PARAMETER   = 1,
	MPATCH_IO_ERROR            = 2,
	MPATCH_CANCELLED_BY_USER   = 3,
	MPATCH_INTERNAL_ERROR      = 4
}
mpatch_error_t;

typedef bool (*mpatch_callback_func_t)(const float progress, const float ratio, const uintptr_t user_data);
typedef bool (*mpatch_writer_func_t)(const uint8_t *const data, const uint32_t size, const uintptr_t user_data);
typedef void (*mpatch_logging_func_t)(const char *const format, const uintptr_t user_data, ...);

typedef struct
{
	uint16_t ver_major;
	uint16_t ver_minor;
	uint16_t ver_patch;
	char bld_date[12];
	char bld_time[12];
}
mpatch_version_t;

typedef struct
{
	mpatch_callback_func_t callback_func;
	uintptr_t user_data;
}
mpatch_callback_t;

typedef struct
{
	const uint8_t *buffer;
	uint_fast32_t capacity;
}
mpatch_rd_buffer_t;

typedef struct
{
	uint8_t *buffer;
	uint_fast32_t capacity;
}
mpatch_wr_buffer_t;

typedef struct
{
	mpatch_writer_func_t writer_func;
	uintptr_t user_data;
	uint32_t total_bytes_written;
}
mpatch_writer_t;

typedef struct
{
	mpatch_logging_func_t logging_func;
	uintptr_t user_data;
}
mpatch_logger_t;

typedef struct
{
	mpatch_rd_buffer_t message_in;
	mpatch_rd_buffer_t reference_in;
	mpatch_writer_t compressed_out;
	mpatch_callback_t callback;
	mpatch_logger_t trace_logger;
}
mpatch_enc_param_t;

typedef struct
{
	int x;
}
mpatch_dec_param_t;

mpatch_error_t mpatch_encode(mpatch_enc_param_t *const param);
mpatch_error_t mpatch_decode(mpatch_dec_param_t *const param);

void mpatch_version(mpatch_version_t *const version);
void mpatch_selftest(void);

#ifdef __cplusplus
} //extern "C"
#endif
