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

#ifdef __cplusplus
extern "C" {
#endif

typedef int(*mpatch_callback_t)(const float progress, const float ratio, void *const user_data);

typedef enum
{
	MPATCH_SUCCESS             = 0,
	MPATCH_INVALID_PARAMETER   = 1,
	MPATCH_INSUFFICIENT_BUFFER = 2,
	MPATCH_CANCELLED_BY_USER   = 3,
	MPATCH_INTERNAL_ERROR      = 4
}
mpatch_error_t;

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
	const uint8_t *message_in;
	uint64_t message_size;
	const uint8_t *reference_in;
	uint64_t reference_size;
	uint8_t *compressed_out;
	uint64_t compressed_capacity;
	uint64_t compressed_size;
	mpatch_callback_t callback;
	void *user_data;
	FILE *trace_log_file;
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
