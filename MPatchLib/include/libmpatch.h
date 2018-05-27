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

#ifndef _INC_LIBMPATCH_H
#define _INC_LIBMPATCH_H 

#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Error codes*/
	typedef enum
	{
	MPATCH_SUCCESS = 0,
	MPATCH_INVALID_PARAMETER = 1,
	MPATCH_CANCELLED_BY_USER = 2,
	MPATCH_IO_ERROR          = 3,
	MPATCH_BAD_FILE_FORMAT   = 4,
	MPATCH_BAD_FILE_VERSION  = 5,
	MPATCH_HEADER_CORRUPTED  = 6,
	MPATCH_INTERNAL_ERROR    = 7
}
mpatch_error_t;

/* Function prototypes */
typedef bool (*mpatch_progress_func_t)(const float progress, const float ratio, const uintptr_t user_data);
typedef bool (*mpatch_writer_func_t)(const uint8_t *const data, const uint32_t size, const uintptr_t user_data);
typedef bool (*mpatch_reader_func_t)(uint8_t *const data, const uint32_t size, const uintptr_t user_data);
typedef void (*mpatch_logging_func_t)(const char *const format, const uintptr_t user_data, ...);

/* Version info */
typedef struct
{
	uint16_t ver_major;
	uint16_t ver_minor;
	uint16_t ver_patch;
	char bld_date[12];
	char bld_time[12];
}
mpatch_version_t;

/* Progress callback */
typedef struct
{
	mpatch_progress_func_t callback_func;
	uintptr_t user_data;
}
mpatch_progress_t;

/* Read-only buffer */
typedef struct
{
	const uint8_t *buffer;
	uint_fast32_t capacity;
}
mpatch_rd_buffer_t;

/* Read/write buffer*/
typedef struct
{
	uint8_t *buffer;
	uint_fast32_t capacity;
}
mpatch_wr_buffer_t;

/* Data consumer (writer) */
typedef struct
{
	mpatch_writer_func_t writer_func;
	uintptr_t user_data;
}
mpatch_writer_t;

/* Data provider (reader) */
typedef struct
{
	mpatch_reader_func_t reader_func;
	uintptr_t user_data;
}
mpatch_reader_t;

/* Logging handler */
typedef struct
{
	mpatch_logging_func_t logging_func;
	uintptr_t user_data;
}
mpatch_logger_t;

/* File info data*/
typedef struct
{
	uint32_t time_create;
	uint32_t fmt_version;
	uint32_t length_msg;
	uint32_t length_ref;
	uint8_t crc32_msg[4U];
	uint8_t crc32_ref[4U];
	uint8_t digest_msg[16U];
	uint8_t digest_ref[16U];
}
mpatch_info_t;

/* Encoder parameters */
typedef struct
{
	mpatch_rd_buffer_t message_in;
	mpatch_rd_buffer_t reference_in;
	mpatch_writer_t compressed_out;
	mpatch_progress_t callback;
	mpatch_logger_t trace_logger;
}
mpatch_enc_param_t;

/* Info parameters */
typedef struct
{
	mpatch_reader_t compressed_in;
	mpatch_info_t file_info;
}
mpatch_nfo_param_t;

/* Decoder parameters */
typedef struct
{
	mpatch_reader_t compressed_in;
	mpatch_rd_buffer_t reference_in;
	mpatch_wr_buffer_t message_out;
	mpatch_progress_t callback;
	mpatch_logger_t trace_logger;
}
mpatch_dec_param_t;

/* Encode/deocde functions */
mpatch_error_t mpatch_encode(mpatch_enc_param_t *const param);
mpatch_error_t mpatch_getnfo(mpatch_nfo_param_t *const param);
mpatch_error_t mpatch_decode(mpatch_dec_param_t *const param);

/* Utility functions */
void mpatch_version(mpatch_version_t *const version);
void mpatch_selftest(void);

#ifdef __cplusplus
} //extern "C"
#endif

#endif //_INC_LIBMPATCH_H
