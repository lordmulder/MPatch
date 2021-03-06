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
#include "encode.h"

#include <time.h>
#include <string.h>

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
/* Stats functions                                                         */
/* ======================================================================= */

static void print_stats(const mpatch_logger_t *const trace_logger, const encd_state_t *const coder_state, const uint_fast32_t total_len)
{
	const char *const HIST_GRAPH[2] =
	{
		"################################################################",
		"----------------------------------------------------------------"
	};
	trace_logger->logging_func("\n[STATS]\n", trace_logger->user_data);
	trace_logger->logging_func("literal_bytes: %lu (%.4f%%)\n", trace_logger->user_data, coder_state->stats.literal_bytes, ((float)coder_state->stats.literal_bytes / total_len) * 100.0);
	trace_logger->logging_func("substring_bytes: %lu (%.4f%%)\n", trace_logger->user_data, coder_state->stats.substring_bytes, ((float)coder_state->stats.substring_bytes / total_len) * 100.0);
	trace_logger->logging_func("z_saved_bytes: %lu (%.4f%%)\n", trace_logger->user_data, coder_state->stats.saved_bytes, ((float)coder_state->stats.saved_bytes / total_len) * 100.0);
	trace_logger->logging_func("\n[LITERALS]\n", trace_logger->user_data);
	uint64_t max_val = 0U;
	for (size_t i = 0U; i <= MAX_LITERAL_LEN; ++i)
	{
		if (coder_state->stats.literal_hist[i] > max_val)
		{
			max_val = coder_state->stats.literal_hist[i];
		}
	}
	for (size_t i = 0U; i <= MAX_LITERAL_LEN; ++i)
	{
		const float scaled = max_val ? (float)coder_state->stats.literal_hist[i] / max_val : 0.0f;
		const size_t len = (size_t)(64U * scaled);
		trace_logger->logging_func("%04lu = %08lu (%.3f) [%.*s%.*s]\n", trace_logger->user_data, i, coder_state->stats.literal_hist[i], scaled, len, HIST_GRAPH[0], 64U - len, HIST_GRAPH[1]);
	}
	trace_logger->logging_func("\n", trace_logger->user_data);
}

/* ======================================================================= */
/* Public Interface                                                        */
/* ======================================================================= */

void MPATCH_API mpatch_version(mpatch_version_t *const version)
{
	if (version)
	{
		version->ver_major = VERSION_MAJOR;
		version->ver_minor = VERSION_MINOR;
		version->ver_patch = VERSION_PATCH;
		strncpy_s(version->bld_date, 12U, BUILD_DATE, _TRUNCATE);
		strncpy_s(version->bld_time, 12U, BUILD_TIME, _TRUNCATE);
		strncpy_s(version->zlib_ver, 12U, mpatch_compress_libver(), _TRUNCATE);
		strncpy_s(version->rhsh_ver, 12U, RHASH_VERSION, _TRUNCATE);
	}
}

mpatch_error_t MPATCH_API mpatch_encode(mpatch_enc_param_t *const param)
{
	//Sanity check parameters
	if ((!param) || (!param->message_in.buffer) || (!param->reference_in.buffer) || (!param->compressed_out.writer_func) || (param->message_in.capacity < 1U) || (param->reference_in.capacity < 1U) || (param->thread_count > MAX_THREAD_COUNT))
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

	//Initialize variables
	mpatch_error_t result = MPATCH_SUCCESS;
	thread_pool_t *thread_pool = NULL;
	encd_state_t *const coder_state = calloc(1U, sizeof(encd_state_t));
	if (!coder_state)
	{
		return MPATCH_INTERNAL_ERROR;
	}

	//Create thread pool
	if (param->thread_count > 1U)
	{
		if (!mpatch_pool_create(&thread_pool, param->thread_count))
		{
			result = MPATCH_INTERNAL_ERROR;
			goto final_clean_up;
		}
	}

	//Log the parameters
	if (param->trace_logger.logging_func)
	{
		param->trace_logger.logging_func("[PARAMS]\n", param->trace_logger.user_data);
		param->trace_logger.logging_func("thread_count: %u\n\n", param->trace_logger.user_data, param->thread_count);
	}

	//Write header
	if (!write_header(&param->compressed_out, &param->message_in, &param->reference_in))
	{
		result = MPATCH_IO_ERROR;
		goto final_clean_up;
	}

	//Initialize encoder state
	init_io_state(&coder_state->output_state);
	time_t last_update = time(NULL);
	uint_fast32_t input_pos = 0U;

	//Initialize compression context
	if (!(mpatch_compress_enc_init(&coder_state->cctx, LITERAL_LEN[LITERAL_LEN_COUNT-1U]) && mpatch_compress_enc_load(coder_state->cctx, param->reference_in.buffer, param->reference_in.capacity)))
	{
		result = MPATCH_INTERNAL_ERROR;
		goto final_clean_up;
	}

	//Process input
	while (input_pos < param->message_in.capacity)
	{
		if (param->callback.callback_func)
		{
			const time_t current_time = time(NULL);
			if (current_time != last_update)
			{
				if (!param->callback.callback_func((float)input_pos / param->message_in.capacity, min_flt(999.99f, (float)coder_state->output_state.byte_counter / input_pos), param->callback.user_data))
				{
					result = MPATCH_CANCELLED_BY_USER;
					goto final_clean_up;
				}
				last_update = current_time;
			}
		}
		const uint_fast32_t chunk_len = encode_chunk(&param->message_in, input_pos, &param->reference_in, &param->compressed_out, coder_state, thread_pool, &param->trace_logger);
		if (!chunk_len)
		{
			result = MPATCH_IO_ERROR;
			goto final_clean_up;
		}
		input_pos += chunk_len;
	}

	//Flush encoder state
	if (!flush_state(&param->compressed_out, &coder_state->output_state))
	{
		result = MPATCH_IO_ERROR;
		goto final_clean_up;
	}

	//Write footer
	if (!write_footer(&param->compressed_out, &coder_state->output_state))
	{
		result = MPATCH_IO_ERROR;
		goto final_clean_up;
	}

	//Final progress update
	if (param->callback.callback_func)
	{
		param->callback.callback_func(1.0, min_flt(999.99f, (float)coder_state->output_state.byte_counter / param->message_in.capacity), param->callback.user_data);
	}

	//Log some stats
	if (param->trace_logger.logging_func)
	{
		print_stats(&param->trace_logger, coder_state, param->message_in.capacity);
	}

final_clean_up:

	//Destroy thread pool
	if (thread_pool)
	{
		mpatch_pool_destroy(&thread_pool);
	}

	//Destroy compression context
	if (coder_state->cctx)
	{
		mpatch_compress_enc_free(&coder_state->cctx);
	}

	//Destroy encoder state
	if (coder_state)
	{
		free(coder_state);
	}

	return result;
}

mpatch_error_t MPATCH_API mpatch_getnfo(mpatch_nfo_param_t *const param)
{
	//Sanity check parameters
	if ((!param) || (!param->compressed_in.reader_func))
	{
		return MPATCH_INVALID_PARAMETER;
	}

	//Read the header
	return read_header(&param->file_info, &param->compressed_in);
}

mpatch_error_t MPATCH_API mpatch_decode(mpatch_dec_param_t *const param)
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

void MPATCH_API mpatch_get_limits(mpatch_limit_t *const limits)
{
	memset(limits, 0U, sizeof(mpatch_limit_t));
	limits->max_thread_count = MAX_THREAD_COUNT;
}
