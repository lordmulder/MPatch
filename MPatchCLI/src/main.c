/* ---------------------------------------------------------------------------------------------- */
/* MPatch - simple patch and compression utility                                                  */
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

#include "mem_io.h"

#include <libmpatch.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <share.h>
#include <time.h>
#include <float.h>
#include <stdarg.h>
#include <signal.h>
#include <io.h>
#include <sys/stat.h> 

/* ======================================================================= */
/* Signal handler                                                          */
/* ======================================================================= */

static volatile bool g_stop_flag = false;

static void sigint_handler(const int sig)
{
	g_stop_flag = true;
	signal(SIGINT, sigint_handler);
}

/* ======================================================================= */
/* Math functions                                                          */
/* ======================================================================= */

static inline uint32_t max_u32(const uint32_t a, const uint32_t b)
{
	return (a > b) ? a : b;
}

static inline uint64_t min_u64(const uint64_t a, const uint64_t b)
{
	return (a < b) ? a : b;
}

static inline float min_flt(const float a, const float b)
{
	return (a < b) ? a : b;
}

static inline uint64_t diff_u64(const uint64_t a, const uint64_t b)
{
	return (a > b) ? (a - b) : (b - a);
}

static inline uint32_t log10_u32(uint32_t value)
{
	uint32_t ret = 1U;
	while (value /= 10U)
	{
		ret++;
	}
	return ret;
}
/* ======================================================================= */
/* Online "mean" computation                                              */
/* ======================================================================= */

typedef struct
{
	double mean;
	uint64_t n;
}
mean_t;

static inline void mean_init(mean_t *const ctx)
{
	memset(ctx, 0, sizeof(mean_t));
}

static inline void mean_update(mean_t *const ctx, const double value)
{
	if (ctx->n)
	{
		ctx->mean += (value - ctx->mean) / ++ctx->n;
	}
	else
	{
		ctx->mean = value;
		++ctx->n;
	}
}

/* ======================================================================= */
/* Progress display                                                        */
/* ======================================================================= */

static const char SPINNER[] = "-\\|/";

typedef struct
{
	mean_t pace_estimate;
	time_t time_last;
	double progress_last;
	size_t spinner_pos;
}
progress_t;

static bool progress_callback(const float progress, const float ratio, const uintptr_t data)
{
	static const uint64_t MAX_TIME_LEFT = 359999U;
	progress_t *const progress_data = (progress_t*)data;

	//Update our pace estimate
	const double progress_diff = progress - progress_data->progress_last;
	if (progress_diff > DBL_EPSILON)
	{
		const time_t time_current = time(NULL);
		const uint64_t time_diff = diff_u64((uint64_t)time_current, (uint64_t)progress_data->time_last);
		if (time_diff > 0U)
		{
			mean_update(&progress_data->pace_estimate, progress_diff / time_diff);
			progress_data->progress_last = progress;
			progress_data->time_last = time_current;
		}
	}

	//Print current progress and eta
	if (progress < 1.0)
	{
		if (progress_data->pace_estimate.n > 2U)
		{
			const uint64_t time_left = (progress_data->pace_estimate.mean >= DBL_EPSILON) ? min_u64(MAX_TIME_LEFT, (uint64_t)((1.0f - progress) / progress_data->pace_estimate.mean + 0.5f)) : MAX_TIME_LEFT;
			fprintf(stderr, "\rProgress: %.2f%%, Ratio: ~%.2f%%, ETA: ~%llu:%02llu:%02llu [%c]    \b\b\b\b", 100.0 * progress, 100.0 * ratio, time_left / 3600U, (time_left / 60U) % 60U, time_left % 60, SPINNER[progress_data->spinner_pos]);
		}
		else
		{
			fprintf(stderr, "\rProgress: %.2f%%, Ratio: ~%.2f%%, ETA: N/A [%c]    \b\b\b\b", 100.0 * progress, 100.0 * ratio, SPINNER[progress_data->spinner_pos]);
		}
	}
	else
	{
		fprintf(stderr, "\rProgress: %.2f%%, Ratio: ~%.2f%%, ETA: ~%llu:%02llu:%02llu [#]    \b\b\b\b", 100.0 * progress, 100.0 * ratio, (uint64_t)0U, (uint64_t)0U, (uint64_t)0U);
	}

	//Flush and increment spinner
	fflush(stderr);
	progress_data->spinner_pos = (progress_data->spinner_pos + 1U) & 3U;
	return (!g_stop_flag);
}

/* ======================================================================= */
/* I/O handler                                                             */
/* ======================================================================= */

static bool input_reader(uint8_t *const data, const uint32_t size, const uintptr_t user_data)
{
	if (fread(data, sizeof(uint8_t), size, (FILE*)user_data) == size)
	{
		return true; /*success*/
	}
	return false;
}

static bool output_writer(const uint8_t *const data, const uint32_t size, const uintptr_t user_data)
{
	if (fwrite(data, sizeof(uint8_t), size, (FILE*)user_data) == size)
	{
		return true; /*success*/
	}
	return false;
}

static int64_t get_file_size(FILE *const file)
{
	struct _stat64 file_info;
	fflush(file);
	if (!_fstat64(_fileno(file), &file_info))
	{
		return file_info.st_size;
	}
	return -1;
}

/* ======================================================================= */
/* Logging handler                                                         */
/* ======================================================================= */

static void logging_handler(const char *const format, const uintptr_t user_data, ...)
{
	va_list args;
	va_start(args, user_data);
	vfprintf((FILE*)user_data, format, args);
	fflush((FILE*)user_data);
	va_end(args);
}

/* ======================================================================= */
/* Arguments                                                               */
/* ======================================================================= */

typedef enum
{
	OP_NONE = 0,
	OP_DIFF = 1,
	OP_INFO = 3,
	OP_EXEC = 4,
	OP_TEST = 5,
	OP_HELP = 6
}
operation_t;

static const struct
{
	const operation_t op_code;
	const wchar_t name_shrt[3];
	const wchar_t name_long[7];
}
OPERATIONS[] =
{
	{ OP_DIFF, L"-d", L"--diff" },
	{ OP_INFO, L"-i", L"--info" },
	{ OP_EXEC, L"-e", L"--exec" },
	{ OP_TEST, L"-t", L"--test" },
	{ OP_HELP, L"-h", L"--help" },
	{ OP_NONE, L"", L"" }
};

static operation_t parse_operation(const wchar_t *const arg)
{
	for (size_t i = 0U; OPERATIONS[i].op_code; ++i)
	{
		if ((!_wcsicmp(arg, OPERATIONS[i].name_shrt)) || (!_wcsicmp(arg, OPERATIONS[i].name_long)))
		{
			return OPERATIONS[i].op_code;
		}
	}
	return OP_NONE;
}

/* ======================================================================= */
/* Misc                                                                    */
/* ======================================================================= */

static void print_logo(void)
{
	mpatch_version_t version;
	mpatch_version(&version);
	fprintf(stderr, "\nMPatch v%u.%u.%u, simple patch and compression utility [%s]\n", (unsigned int)version.ver_major, (unsigned int)version.ver_minor, (unsigned int)version.ver_patch, version.bld_date);
	fprintf(stderr, "Copyright (c) %s LoRd_MuldeR <mulder2@gmx.de>, released under the MIT License.\n\n", &version.bld_date[7]);
}

static void print_manpage(void)
{
	fputs("Usage:\n", stderr);
	fputs("  mpatch.exe <operation> [<input_file> <reference_file> <output_file>]\n\n", stderr);
	fputs("Operations:\n", stderr);
	fputs("  -d --diff  Create a new patch file\n", stderr);
	fputs("  -e --exec  Apply an existing patch file\n", stderr);
	fputs("  -t --test  Run self-test\n", stderr);
	fputs("  -h --help  Print this help screen\n\n", stderr);
	fputs("Environment:\n", stderr);
	fputs("  MPATCH_LOGFILE  Path of the log file\n\n", stderr);
	fputs("Examples:\n", stderr);
	fputs("  mpatch.exe --diff new_prog.exe old_prog.exe update.patch\n", stderr);
	fputs("  mpatch.exe --exec update.patch old_prog.exe new_prog.exe\n\n", stderr);
}

static const char *translate_io_error(const io_error_t error)
{
	switch(error)
	{
	case IO_FILE_NOT_FOUND:
		return "File could not be found!";
	case IO_ACCESS_DENIED:
		return "Access was denied!";
	case IO_OUT_OF_MEMORY:
		return "Not enough memory!";
	case IO_FILE_TOO_LARGE:
		return "File size exceeds 4 GB limit!";
	case IO_FILE_IS_EMPTY:
		return "File appears to be empty!";
	default:
		return "Other I/O error!";
	}
}

static const wchar_t *basename(const wchar_t *path)
{
	static const wchar_t PATH_SEP[3] = { L':', L'/', L'\\' };
	const wchar_t *temp;
	for (size_t i = 0; i < 3; ++i)
	{
		if (temp = wcsrchr(path, PATH_SEP[i]))
		{
			path = temp + 1U;
		}
	}
	return path;
}

static FILE *open_log_file(void)
{
	wchar_t *name = NULL;
	size_t size = 0U;
	if (!_wdupenv_s(&name, &size, L"MPATCH_LOGFILE"))
	{
		if (name)
		{
			FILE *file = NULL;
			if (!(file = _wfsopen(name, L"wt", _SH_DENYWR)))
			{
				fprintf(stderr, "Warning: Failed to open logfile \"%S\" for writing!\n\n", name);
			}
			free(name);
			return file;
		}
	}
	return NULL;
}

static const char *print_digest(char *const buffer, const size_t buff_size, const uint8_t *const digest)
{
	for (size_t i = 0U; i < 16U; ++i)
	{
		sprintf_s(buffer + (2U * i), buff_size - (2U * i), "%02X", digest[i]);
	}
	return buffer;
}

/* ======================================================================= */
/* Encode                                                                  */
/* ======================================================================= */

static mpatch_error_t encode(const wchar_t *const input_file, const wchar_t *const refernce_file, const wchar_t *const output_file, FILE *const log_file)
{
	//Print info
	fprintf(stderr, "Creating patch \"%S\" <- \"%S\"\n\n", basename(input_file), basename(refernce_file));

	//Map input file to memory
	rd_view_t input_view;
	const io_error_t error_input = map_file_rd(&input_view, input_file);
	if (error_input != IO_SUCCESS)
	{
		fprintf(stderr, "Failed to open input file: %s\n\n", translate_io_error(error_input));
		return MPATCH_IO_ERROR;
	}

	//Map reference file to memory
	rd_view_t reference_view;
	const io_error_t error_reference = map_file_rd(&reference_view, refernce_file);
	if (error_reference != IO_SUCCESS)
	{
		fprintf(stderr, "Failed to open reference file: %s\n\n", translate_io_error(error_reference));
		unmap_file_rd(&input_view);
		return MPATCH_IO_ERROR;
	}

	//Open the output file for writing
	FILE *output_stream;
	const errno_t error_output = _wfopen_s(&output_stream, output_file, L"wb");
	if (error_output)
	{
		char err_message[128U];
		strerror_s(err_message, 128U, error_output);
		fprintf(stderr, "Failed to open output file: %s!\n\n", err_message);
		unmap_file_rd(&input_view);
		unmap_file_rd(&reference_view);
		return MPATCH_IO_ERROR;
	}

	//Init progress
	progress_t progress_data;
	memset(&progress_data, 0U, sizeof(progress_t));
	progress_data.time_last = time(NULL);
	mean_init(&progress_data.pace_estimate);

	//Set up the encoder parameters
	mpatch_enc_param_t param;
	memset(&param, 0, sizeof(mpatch_enc_param_t));
	param.message_in.buffer = input_view.data_ptr;
	param.message_in.capacity = input_view.size;
	param.reference_in.buffer = reference_view.data_ptr;
	param.reference_in.capacity = reference_view.size;
	param.compressed_out.writer_func = output_writer;
	param.compressed_out.user_data = (uintptr_t)output_stream;
	param.callback.callback_func = progress_callback;
	param.callback.user_data = (uintptr_t)&progress_data;
	param.thread_count = 4U;

	//Enable trace logging
	if (log_file)
	{
		param.trace_logger.logging_func = logging_handler;
		param.trace_logger.user_data = (uintptr_t)log_file;
	}

	//Process!
	const mpatch_error_t result = mpatch_encode(&param);

	//Handle the result
	switch (result)
	{
	case MPATCH_SUCCESS:
		fputs("\nDone.\n\n", stderr);
		break;
	case MPATCH_CANCELLED_BY_USER:
		fputs("\nStopped.\n\nOperation has been cancelled by the user!\n\n", stderr);
		break;
	case MPATCH_IO_ERROR:
		fputs("\nFailed!\n\nFailed to write compressed data to output file!\n\n", stderr);
		break;
	default:
		fputs("\nFailed!\n\nError: An unexpected error has been encoutered\n\n!", stderr);
	}

	//Compute compression ratioo
	if (!result)
	{
		const int64_t final_size = get_file_size(output_stream);
		fprintf(stderr, "Compression ratio : %.2f%%\n\n", (final_size > 0) ? (100.0 * ((double)final_size / input_view.size)) : 0.0);
	}

	//Close the outout file
	if (fclose(output_stream))
	{
		fputs("Warning: Failed to close the output file!\n\n", stderr);
	}

	//Unmap all files from memory
	if (unmap_file_rd(&input_view) != IO_SUCCESS)
	{
		fputs("Warning: Failed to close the input file!\n\n", stderr);
	}
	if (unmap_file_rd(&reference_view) != IO_SUCCESS)
	{
		fputs("Warning: Failed to close the reference file!\n\n", stderr);
	}

	return result;
}

/* ======================================================================= */
/* Get file info                                                           */
/* ======================================================================= */

static mpatch_error_t getnfo(const wchar_t *const input_file)
{
	//Print info
	fprintf(stderr, "Reading info of patch file \"%S\"\n\n", basename(input_file));

	//Open the output file for writing
	FILE *input_stream;
	const errno_t error_input = _wfopen_s(&input_stream, input_file, L"rb");
	if (error_input)
	{
		char err_message[128U];
		strerror_s(err_message, 128U, error_input);
		fprintf(stderr, "Failed to open input file: %s!\n\n", err_message);
		return MPATCH_IO_ERROR;
	}

	//Set up the encoder parameters
	mpatch_nfo_param_t param;
	memset(&param, 0, sizeof(mpatch_nfo_param_t));
	param.compressed_in.reader_func = input_reader;
	param.compressed_in.user_data = (uintptr_t)input_stream;

	//Process!
	fputs("Parsing the file header...", stderr);
	const mpatch_error_t result = mpatch_getnfo(&param);

	//Handle the result
	switch (result)
	{
	case MPATCH_SUCCESS:
		fputs("\nDone\n\n[Patch Info]\n", stderr);
		break;
	case MPATCH_CANCELLED_BY_USER:
		fputs("\nStopped.\n\nOperation has been cancelled by the user!\n\n", stderr);
		break;
	case MPATCH_IO_ERROR:
		fputs("\nFailed!\n\nFailed to read data from input file!\n\n", stderr);
		break;
	case MPATCH_BAD_FILE_FORMAT:
		fputs("\nFailed!\n\nFile does *not* look like an MPatch file!\n\n", stderr);
		break;
	case MPATCH_BAD_FILE_VERSION:
		fputs("\nFailed!\n\nFile uses an unsupported file format version!\n\n", stderr);
		break;
	case MPATCH_HEADER_CORRUPTED:
		fputs("\nFailed!\n\nFile header appears to be corrupted. Take care!\n\n", stderr);
		break;
	default:
		fputs("\nFailed!\n\nError: An unexpected error has been encoutered!\n\n", stderr);
	}

	//Print file info out
	if (!result)
	{
		char digest_buffer[33U];
		const uint32_t digits = log10_u32(max_u32(param.file_info.length_msg, param.file_info.length_msg));
		fprintf(stderr, "File format version     : 0x%X\n", param.file_info.fmt_version);
		fprintf(stderr, "Decompressed file size  : %0*u byte(s)\n", digits, param.file_info.length_msg);
		fprintf(stderr, "Reference file size     : %0*u byte(s)\n", digits, param.file_info.length_ref);
		fprintf(stderr, "Decompressed file CRC32 : 0x%02X%02X%02X%02X\n", param.file_info.crc32_msg[0], param.file_info.crc32_msg[1], param.file_info.crc32_msg[2], param.file_info.crc32_msg[3]);
		fprintf(stderr, "Reference file CRC32    : 0x%02X%02X%02X%02X\n", param.file_info.crc32_ref[0], param.file_info.crc32_ref[1], param.file_info.crc32_ref[2], param.file_info.crc32_ref[3]);
		fprintf(stderr, "Decompressed file hash  : %s\n",   print_digest(digest_buffer, 33U, param.file_info.digest_msg));
		fprintf(stderr, "Reference file hash     : %s\n\n", print_digest(digest_buffer, 33U, param.file_info.digest_ref));
	}
	
	//Close the input file
	if (fclose(input_stream))
	{
		fputs("Warning: Failed to close the output file!\n\n", stderr);
	}

	return result;
}

/* ======================================================================= */
/* MAIN                                                                    */
/* ======================================================================= */

int wmain(int argc, wchar_t *const argv[])
{
	print_logo();

	//Any arguments present?
	if (argc < 2)
	{
		print_manpage();
		return EXIT_FAILURE;
	}

	//Determine operation
	const operation_t operation = parse_operation(argv[1]);
	if (operation == OP_NONE)
	{
		fprintf(stderr, "Operation \"%S\" not supported!\n\n", argv[1]);
		return EXIT_FAILURE;
	}

	//Print manpage?
	if (operation == OP_HELP)
	{
		print_manpage();
		return EXIT_SUCCESS;
	}

	//Run self-test
	if (operation == OP_TEST)
	{
		fputs("Self-test is running, please wait...\n", stderr);
		mpatch_selftest();
		fputs("Successful.\n\n", stderr);
		return EXIT_SUCCESS;
	}

	//Check parameter count
	if (argc < ((operation != OP_INFO) ? 5 : 3))
	{
		fputs("Required argument is missing. See \"--help\" for details!\n\n", stderr);
		return EXIT_FAILURE;
	}

	//Install signal handler
	signal(SIGINT, sigint_handler);

	//Open log file
	FILE *const log_file = (operation != OP_INFO) ? open_log_file() : NULL;

	//Initilaize status
	const clock_t clock_begin = clock();
	mpatch_error_t result = (-1);

	//Run selected operation
	switch(operation)
	{
	case OP_DIFF:
		result = encode(argv[2], argv[3], argv[4], log_file);
		break;
	case OP_INFO:
		result = getnfo(argv[2]);
		break;
	case OP_EXEC:
		fputs("Sorry, operation *not* implemented yet!\n\n", stderr);
		break;
	default:
		abort();
	}
	
	//Compute overall time
	const clock_t total_seconds = (clock() - clock_begin) / CLOCKS_PER_SEC;
	if (total_seconds >= 60U)
	{
		if (total_seconds >= 3600U)
		{
			fprintf(stderr, "--------\n\nOperation took %u hour(s), %u minute(s).\n\n", total_seconds / 3600U, (total_seconds / 60U) % 60U);
		}
		else
		{
			fprintf(stderr, "--------\n\nOperation took %u minute(s), %u second(s).\n\n", total_seconds / 60U, total_seconds % 60U);
		}
	}
	else
	{
		fprintf(stderr, "--------\n\nOperation took %u second(s).\n\n", total_seconds);
	}
	
	//Close log file
	if (log_file)
	{
		fclose(log_file);
	}

	//Completed
	return (int)result;
}
