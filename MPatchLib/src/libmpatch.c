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

#include "utils.h"
#include "substring.h"
#include "compress.h"

#include <stdlib.h>
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
/* Encoder functions                                                       */
/* ======================================================================= */

typedef struct
{
	io_state_t output_state;
	mpatch_cctx_t *cctx;
	uint_fast32_t prev_offset[2U];
}
encd_state_t;

static const uint_fast32_t LITERAL_LIMIT = 8192;
static const uint_fast32_t COMPRESS_THRESHOLD = 5U;

static const uint_fast32_t SUBSTR_SRC = 0U;
static const uint_fast32_t SUBSTR_REF = 1U;

static inline float chunk_cost(const uint8_t *const input_ptr, encd_state_t *const coder_state, const uint32_t literal_len, const substring_t *const substring)
{
	uint_fast32_t total_bits = literal_len ? (literal_len * 8U) + exp_golomb_size(literal_len) + 1U : 1U;
	if (substring->length > SUBSTRING_THRESHOLD)
	{
	total_bits += exp_golomb_size(substring->length - SUBSTRING_THRESHOLD) + exp_golomb_size(substring->offset_diff) + (substring->offset_diff ? 2U : 1U);
	}
	else
	{
		if (substring->length)
		{
			abort();
		}
		total_bits += 1U;
	}
	return (float)total_bits / ((literal_len + substring->length) * 8U);
}

static bool write_chunk(const uint8_t *const input_ptr, const mpatch_writer_t *const output, encd_state_t *const coder_state, const uint_fast32_t optimal_literal_len, const substring_t *const optimal_substr, const uint_fast32_t optimal_substr_type)
{
	//Write literal
	if (optimal_literal_len > 0U)
	{
		uint_fast32_t compressed_size = optimal_literal_len;
		if (optimal_literal_len > COMPRESS_THRESHOLD)
		{
			if ((compressed_size = mpatch_compress_enc_test(coder_state->cctx, input_ptr, optimal_literal_len)) == UINT_FAST32_MAX)
			{
				return false;
			}
		}
		if (compressed_size < optimal_literal_len)
		{
			const uint8_t *const compressed_data = mpatch_compress_enc_next(coder_state->cctx, input_ptr, optimal_literal_len, &compressed_size);
			if (!(compressed_data && exp_golomb_write(compressed_size, output, &coder_state->output_state) && write_bit(true, output, &coder_state->output_state) && write_bytes(compressed_data, compressed_size, output, &coder_state->output_state)))
			{
				return false;
			}
		}
		else
		{
			if (!(exp_golomb_write(optimal_literal_len, output, &coder_state->output_state) && write_bit(false, output, &coder_state->output_state) && write_bytes(input_ptr, optimal_literal_len, output, &coder_state->output_state)))
			{
				return false;
			}
		}
	}
	else
	{
		if (!exp_golomb_write(0U, output, &coder_state->output_state))
		{
			return false;
		}
	}

	//Write substring
	if (optimal_substr->length > SUBSTRING_THRESHOLD)
	{
		if (!(exp_golomb_write(optimal_substr->length - SUBSTRING_THRESHOLD, output, &coder_state->output_state) && write_bit(BOOLIFY(optimal_substr_type), output, &coder_state->output_state) && exp_golomb_write(optimal_substr->offset_diff, output, &coder_state->output_state)))
		{
			return false;
		}
		if (optimal_substr->offset_diff > 0U)
		{
			if (!write_bit(optimal_substr->offset_sign, output, &coder_state->output_state))
			{
				return false;
			}
		}
	}
	else
	{
		if (optimal_substr->length)
		{
			abort();
		}
		if (!exp_golomb_write(0U, output, &coder_state->output_state))
		{
			return false;
		}
	}

	return true;
}

#define FIND_OPTIMAL_SUBSTRING(TYPE, BUFFER, BUFF_SIZE) do \
{ \
	if (find_optimal_substring(&substr_data[TYPE], literal_len, coder_state->prev_offset[TYPE], thread_pool, input_buffer->buffer + input_pos + literal_len, remaining - literal_len, (BUFFER), (BUFF_SIZE))) \
	{ \
		const float _ccost = chunk_cost(input_buffer->buffer + input_pos, coder_state, literal_len, &substr_data[TYPE]); \
		if (_ccost < substr_cost) \
		{ \
			substr_cost = _ccost; \
			substr_type = (TYPE); \
		} \
	} \
} \
while(0)

static uint_fast32_t encode_chunk(const mpatch_rd_buffer_t *const input_buffer, const uint_fast32_t input_pos, const mpatch_rd_buffer_t *const reference_buffer, const mpatch_writer_t *const output, encd_state_t *const coder_state, thread_pool_t *const thread_pool, const uint_fast32_t step_size, const mpatch_logger_t *const logger)
{
	const uint_fast32_t remaining = input_buffer->capacity - input_pos;
	const uint_fast32_t literal_limit = min_uint32(LITERAL_LIMIT, remaining);

	//Kepp the "optimal" settings
	uint_fast32_t optimal_substr_type, optimal_literal_len;
	substring_t optimal_substr = { 0U, 0U, false };

	//Initialize bit cost computation
	float cost_optimal = chunk_cost(input_buffer->buffer + input_pos, coder_state, optimal_literal_len = literal_limit, &optimal_substr);

	//Find the "optimal" encoding of the next chunk
	for (uint_fast32_t literal_len = 0U; literal_len <= literal_limit; literal_len += step_size)
	{
		float substr_cost = FLT_MAX;
		uint_fast32_t substr_type;
		substring_t substr_data[2U];

		FIND_OPTIMAL_SUBSTRING(SUBSTR_SRC, input_buffer->buffer, input_pos);
		FIND_OPTIMAL_SUBSTRING(SUBSTR_REF, reference_buffer->buffer, reference_buffer->capacity);

		if (substr_cost < cost_optimal)
		{
			optimal_literal_len = literal_len;
			memcpy(&optimal_substr, &substr_data[substr_type], sizeof(substring_t));
			cost_optimal = substr_cost;
			optimal_substr_type = substr_type;
		}
		else if (optimal_substr.length > SUBSTRING_THRESHOLD)
		{
			break; /*stop optimization process*/
		}
	}

	//Write detailed info to log
	if (logger->logging_func)
	{
		if (optimal_substr.length > SUBSTRING_THRESHOLD)
		{
			logger->logging_func("%016lu, %6.4f, %016lu, %016lu, %s, %s, %016lu\n", logger->user_data, input_pos, cost_optimal, optimal_literal_len,
				optimal_substr.length, optimal_substr_type ? "REF" : "SRC", optimal_substr.offset_diff ? (optimal_substr.offset_sign ? "-->" : "<--") : "~~~", optimal_substr.offset_diff);
		}
		else
		{
			logger->logging_func("%016lu, %6.4f, %016lu, %016lu\n", logger->user_data, input_pos, cost_optimal, optimal_literal_len, optimal_substr.length);
		}
	}

	//Write "optimal" encoding to output now!
	if (!write_chunk(input_buffer->buffer + input_pos, output, coder_state, optimal_literal_len, &optimal_substr, optimal_substr_type))
	{
		return 0U;
	}

	//Update coder state
	if (optimal_substr.length > 1U)
	{
		if (optimal_substr.offset_diff)
		{
			if (optimal_substr.offset_sign)
			{
				coder_state->prev_offset[optimal_substr_type] += optimal_substr.offset_diff;
			}
			else
			{
				coder_state->prev_offset[optimal_substr_type] -= optimal_substr.offset_diff;
			}
		}
		coder_state->prev_offset[optimal_substr_type] += optimal_substr.length;
	}

	//Return total number of "procoseed" bytes
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
	}
}

mpatch_error_t MPATCH_API mpatch_encode(mpatch_enc_param_t *const param)
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

	//Initialize variables
	thread_pool_t *thread_pool = NULL;
	encd_state_t coder_state;
	memset(&coder_state, 0, sizeof(encd_state_t));
	mpatch_error_t result = MPATCH_SUCCESS;

	//Create thread pool
	if (param->thread_count > 1U)
	{
		if (!mpatch_pool_create(&thread_pool, param->thread_count))
		{
			result = MPATCH_INTERNAL_ERROR;
			goto final_clean_up;
		}
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

	//Write header
	if (!write_header(&param->compressed_out, &param->message_in, &param->reference_in))
	{
		result = MPATCH_IO_ERROR;
		goto final_clean_up;
	}

	//Initialize encoder state
	init_io_state(&coder_state.output_state);
	time_t last_update = time(NULL);
	uint_fast32_t input_pos = 0U;

	//Initialize compression context
	if (!(mpatch_compress_enc_init(&coder_state.cctx, LITERAL_LIMIT) && mpatch_compress_enc_load(coder_state.cctx, param->reference_in.buffer, param->reference_in.capacity)))
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
				if (!param->callback.callback_func((float)input_pos / param->message_in.capacity, min_flt(999.99f, (float)coder_state.output_state.byte_counter / input_pos), param->callback.user_data))
				{
					result = MPATCH_CANCELLED_BY_USER;
					goto final_clean_up;
				}
				last_update = current_time;
			}
		}
		const uint_fast32_t chunk_len = encode_chunk(&param->message_in, input_pos, &param->reference_in, &param->compressed_out, &coder_state, thread_pool, step_size, &param->trace_logger);
		if (!chunk_len)
		{
			result = MPATCH_IO_ERROR;
			goto final_clean_up;
		}
		input_pos += chunk_len;
	}

	//Flush encoder state
	if (!flush_state(&param->compressed_out, &coder_state.output_state))
	{
		result = MPATCH_IO_ERROR;
		goto final_clean_up;
	}

	//Write footer
	if (!write_footer(&param->compressed_out, &coder_state.output_state))
	{
		result = MPATCH_IO_ERROR;
		goto final_clean_up;
	}

	//Final progress update
	if (param->callback.callback_func)
	{
		param->callback.callback_func(1.0, min_flt(999.99f, (float)coder_state.output_state.byte_counter / param->message_in.capacity), param->callback.user_data);
	}

final_clean_up:

	//Destroy compression context
	if (coder_state.cctx)
	{
		mpatch_compress_enc_free(&coder_state.cctx);
	}

	//Destroy thread pool
	if (thread_pool)
	{
		mpatch_pool_destroy(&thread_pool);
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
	limits->max_speed_parameter = MAX_SPEED_PARAMETER;
	limits->max_thread_count = MAX_THREAD_COUNT;
}
