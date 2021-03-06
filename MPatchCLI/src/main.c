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

#include <libmpatch.h>

#include "utils.h"
#include "mem_io.h"
#include "sysinfo.h"
#include "errors.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <share.h>
#include <time.h>
#include <float.h>
#include <stdarg.h>
#include <signal.h>
#include <io.h>
#include <wchar.h>
#include <sys/stat.h> 

//CPU Arch
#if defined(_M_X64) || defined(__amd64__)
#define CPU_ARCH "x64"
#else
#define CPU_ARCH "x86"
#endif

/* ======================================================================= */
/* Utility functions                                                       */
/* ======================================================================= */

static volatile bool g_stop_flag = false;

/* ------------------------------------------------ */
/* Signal handler                                   */
/* ------------------------------------------------ */

static void sigint_handler(const int sig)
{
	g_stop_flag = true;
	signal(SIGINT, sigint_handler);
}

static void sigabrt_handler(const int _sig)
{
	__try
	{
		fputs("\nABNORMAL TERMINATION: The program has been aborted due to an internal error!\n", stderr);
		fflush(stderr);
	}
	__finally
	{
		_Exit(666);
	}
}

static void sigerr_handler(const int _sig)
{
	__try
	{
		fputs("\nGURU MEDITATION: Whoops, something went horribly wrong!\n", stderr);
		fflush(stderr);
	}
	__finally
	{
		_Exit(667);
	}
}

/* ------------------------------------------------ */
/* Error handling                                   */
/* ------------------------------------------------ */

static void print_error(const int error_code)
{
	char buffer[256U];
	if (!strerror_s(buffer, 256U, error_code))
	{
		for (size_t i = 0; ERRNO_CODES[i].value; ++i)
		{
			if (ERRNO_CODES[i].value == error_code)
			{
				fprintf(stderr, "Error: %s [%s]\n\n", buffer, ERRNO_CODES[i].name);
				return;
			}
		}
		fprintf(stderr, "Error: %s [0x%X]\n\n", buffer, error_code);
	}
}

static const char *translate_io_error(const io_error_t error)
{
	switch (error)
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

/* ------------------------------------------------ */
/* Progress display                                 */
/* ------------------------------------------------ */


static const char SPINNER[] = "-\\|/";

typedef struct
{
	time_t time_last;
	double progress_last;
	gauss_t filter;
	double pace_estimate;
	size_t spinner_pos;
}
progress_t;

static bool progress_callback(const float progress, const float ratio, const uintptr_t data)
{
	static const uint64_t MAX_TIME_LEFT = 359999U;
	progress_t *const progress_data = (progress_t*)data;

	//Update our pace estimate
	const double progress_diff = progress - progress_data->progress_last;
	if (progress_diff >= 0.001)
	{
		const time_t time_current = time(NULL);
		const uint64_t time_diff = diff_uint64((uint64_t)time_current, (uint64_t)progress_data->time_last);
		if (time_diff >= 3U)
		{
			progress_data->pace_estimate = gauss_update(&progress_data->filter, progress_diff / time_diff);
			progress_data->progress_last = progress;
			progress_data->time_last = time_current;
		}
	}

	//Print current progress and eta
	if (progress < 1.0)
	{
		if (progress_data->filter.pos[1] != SIZE_MAX)
		{
			const uint64_t time_left = (progress_data->pace_estimate >= DBL_EPSILON) ? min_uint64(MAX_TIME_LEFT, (uint64_t)((1.0f - progress) / progress_data->pace_estimate + 0.5f)) : MAX_TIME_LEFT;
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

/* ------------------------------------------------ */
/* I/O handler                                      */
/* ------------------------------------------------ */

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

/* ------------------------------------------------ */
/* Logging handler                                  */
/* ------------------------------------------------ */

static void logging_handler(const char *const format, const uintptr_t user_data, ...)
{
	va_list args;
	va_start(args, user_data);
	vfprintf((FILE*)user_data, format, args);
	fflush((FILE*)user_data);
	va_end(args);
}

/* ------------------------------------------------ */
/* Arguments                                        */
/* ------------------------------------------------ */

typedef enum
{
	OP_NONE = 0,
	OP_ENCR = 1,
	OP_DECR = 3,
	OP_INFO = 4,
	OP_TEST = 5,
	OP_LICN = 6,
	OP_HELP = 7
}
operation_t;

static const struct
{
	const operation_t op_code;
	const wchar_t name_shrt;
	const wchar_t name_long[9U];
}
OPERATIONS[] =
{
	{ OP_ENCR, L'c', L"create"   },
	{ OP_DECR, L'a', L"apply"    },
	{ OP_INFO, L'i', L"info"     },
	{ OP_TEST, L't', L"selftest" },
	{ OP_HELP, L'h', L"help"     },
	{ OP_LICN, L'l', L"license" },
	{ OP_NONE, 0x00, L"" }
};

static operation_t parse_operation(const wchar_t *const arg)
{
	if (((arg[0] == L'-') || (arg[0] == L'/')) && iswalpha(arg[1]) && (!arg[2]))
	{
		const wchar_t c = towlower(arg[1]);
		for (size_t i = 0U; OPERATIONS[i].op_code; ++i)
		{
			if (OPERATIONS[i].name_shrt == c)
			{
				return OPERATIONS[i].op_code;
			}
		}
	}
	if ((arg[0] == L'-') || (arg[1] == L'-'))
	{
		for (size_t i = 0U; OPERATIONS[i].op_code; ++i)
		{
			if (!_wcsicmp(arg + 2U, OPERATIONS[i].name_long))
			{
				return OPERATIONS[i].op_code;
			}
		}
	}
	return OP_NONE;
}

/* ------------------------------------------------ */
/* Misc                                             */
/* ------------------------------------------------ */

static const char *print_digest(char *const buffer, const size_t buff_size, const uint8_t *const digest)
{
	for (size_t i = 0U; i < 16U; ++i)
	{
		sprintf_s(buffer + (2U * i), buff_size - (2U * i), "%02X", digest[i]);
	}
	return buffer;
}

static FILE *open_log_file(void)
{
	const wchar_t *file_name = env_get_string(L"MPATCH_LOGFILE");
	if (file_name)
	{
		FILE *log_file = NULL;
		if (!(log_file = _wfsopen(file_name, L"wt", _SH_DENYWR)))
		{
			fprintf(stderr, "Warning: Failed to open logfile \"%S\" for writing!\n\n", basename(file_name));
		}
		free((void*)file_name);
		return log_file;
	}
	return NULL;
}

/* ------------------------------------------------ */
/* Manpage                                          */
/* ------------------------------------------------ */

static void print_logo(void)
{
	mpatch_version_t version;
	mpatch_version(&version);

	struct tm time_info;
	const time_t current_time = time(NULL);
	gmtime_s(&time_info, &current_time);

	char year_str[10U];
	const long int year_from = atol(&version.bld_date[7]), year_now = time_info.tm_year + 1900L;
	_snprintf_s(year_str, 10U, _TRUNCATE, (year_now > year_from) ? "%04ld-%04ld" : "%04ld", year_from, year_now);
	
	fputs("\n-----------------------------------------------------------------------------\n", stderr);
	fprintf(stderr, "MPatch v%u.%u.%u (%s), simple patch and compression utility [%s]\n", (unsigned int)version.ver_major, (unsigned int)version.ver_minor, (unsigned int)version.ver_patch, CPU_ARCH, version.bld_date);
	fprintf(stderr, "Copyright (c) %s LoRd_MuldeR <mulder2@gmx.de>.\n", year_str);
	fputs("This software is released under the MIT License. See COPYING.TXT for details!\n", stderr);
	fputs("-----------------------------------------------------------------------------\n\n", stderr);
}

static void print_manpage(const wchar_t *const argv0)
{
	mpatch_version_t version;
	mpatch_version(&version); 
	
	mpatch_limit_t limits;
	mpatch_get_limits(&limits);

	fprintf(stderr, "using Zlib v%s, Copyright(C) 1995-2017 Jean-loup Gailly and Mark Adler\n", version.zlib_ver);
	fprintf(stderr, "using RHash v%s, Copyright(c) 2005-2014 Aleksey Kravchenko\n\n", version.rhsh_ver);
	fputs("Usage:\n", stderr);
	fprintf(stderr, "  %S <operation> [<input_file> [<reference_file> <output_file>]]\n\n", basename(argv0));
	fputs("Operations:\n", stderr);
	fputs("  -c --create    Create a new patch file\n", stderr);
	fputs("  -a --apply     Apply an existing patch file\n", stderr);
	fputs("  -i --info      Print patch information\n", stderr);
	fputs("  -t --selftest  Run self-test\n", stderr);
	fputs("  -h --help      Print this help screen\n", stderr);
	fputs("  -l --license   Print license information\n\n", stderr);
	fputs("Environment:\n", stderr);
	fprintf(stderr, "  MPATCH_THREADS  Number of compressor threads [0..%lu] (Def.: Auto)\n", limits.max_thread_count);
	fputs("  MPATCH_LOGFILE  Create detailed log file\n\n", stderr);
	fputs("Examples:\n", stderr);
	fputs("  mpatch.exe --c new_prog.exe old_prog.exe update.patch\n", stderr);
	fputs("  mpatch.exe --i update.patch\n", stderr);
	fputs("  mpatch.exe --a update.patch old_prog.exe new_prog.exe\n\n", stderr);
}

static void print_license(void)
{
	mpatch_version_t version;
	mpatch_version(&version);

	fprintf(stderr, "\nMPatch v%u.%u.%u, simple patch and compression utility [%s]\n", (unsigned int)version.ver_major, (unsigned int)version.ver_minor, (unsigned int)version.ver_patch, version.bld_date);
	fputs("Copyright(c) 2018 LoRd_MuldeR <mulder2@gmx.de>\n\n", stderr);
	fputs("Permission is hereby granted, free of charge, to any person obtaining a copy\n", stderr);
	fputs("of this software and associated documentation files (the \"Software\"), to deal\n", stderr);
	fputs("in the Software without restriction, including without limitation the rights\n", stderr);
	fputs("to use, copy, modify, merge, publish, distribute, sublicense, and/or sell\n", stderr);
	fputs("copies of the Software, and to permit persons to whom the Software is\n", stderr);
	fputs("furnished to do so, subject to the following conditions:\n\n", stderr);
	fputs("The above copyright notice and this permission notice shall be included in all\n", stderr);
	fputs("copies or substantial portions of the Software.\n\n", stderr);
	fputs("THE SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR\n", stderr);
	fputs("IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,\n", stderr);
	fputs("FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE\n", stderr);
	fputs("AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER\n", stderr);
	fputs("LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,\n", stderr);
	fputs("OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE\n", stderr);
	fputs("SOFTWARE.\n\n\n\n", stderr);

	fputs("The following third-party libraries are incorporated into MPatch:\n\n", stderr);

	fputs("\n----------------\n", stderr);
	fputs("ZLib\n", stderr);
	fputs("----------------\n\n", stderr);
	fprintf(stderr, "Zlib v%s, general purpose compression library\n", version.zlib_ver);
	fputs("Copyright(C) 1995-2017 Jean-loup Gailly and Mark Adler\n\n", stderr);
	fputs("This software is provided 'as-is', without any express or implied\n", stderr);
	fputs("warranty.  In no event will the authors be held liable for any damages\n", stderr);
	fputs("arising from the use of this software.\n\n", stderr);
	fputs("Permission is granted to anyone to use this software for any purpose,\n", stderr);
	fputs("including commercial applications, and to alter it and redistribute it\n", stderr);
	fputs("freely, subject to the following restrictions:\n\n", stderr);
	fputs("1. The origin of this software must not be misrepresented; you must not\n", stderr);
	fputs("   claim that you wrote the original software. If you use this software\n", stderr);
	fputs("   in a product, an acknowledgment in the product documentation would be\n", stderr);
	fputs("   appreciated but is not required.\n", stderr);
	fputs("2. Altered source versions must be plainly marked as such, and must not be\n", stderr);
	fputs("   misrepresented as being the original software.\n", stderr);
	fputs("3. This notice may not be removed or altered from any source distribution.\n\n", stderr);
	fputs("Jean-loup Gailly        Mark Adler\n", stderr);
	fputs("jloup@gzip.org          madler@alumni.caltech.edu\n\n", stderr);
	fputs("The data format used by the zlib library is described by RFCs (Request for\n", stderr);
	fputs("Comments) 1950 to 1952 in the files http://tools.ietf.org/html/rfc1950\n", stderr);
	fputs("(zlib format), rfc1951 (deflate format) and rfc1952 (gzip format).\n\n", stderr);

	fputs("\n----------------\n", stderr);
	fputs("RHash\n", stderr);
	fputs("----------------\n\n", stderr);
	fprintf(stderr, "RHash v%s, calculate/check CRC32, MD5, SHA1, SHA2 or other hash sums\n", version.rhsh_ver);
	fputs("Copyright (c) 2005-2014 Aleksey Kravchenko <rhash.admin@gmail.com>\n\n", stderr);
	fputs("Permission is hereby granted, free of charge,  to any person obtaining a copy\n", stderr);
	fputs("of this software and associated documentation files (the \"Software\"), to deal\n", stderr);
	fputs("in the Software without restriction,  including without limitation the rights\n", stderr);
	fputs("to  use,  copy,  modify,  merge, publish, distribute, sublicense, and/or sell\n", stderr);
	fputs("copies  of  the Software,  and  to permit  persons  to whom  the Software  is\n", stderr);
	fputs("furnished to do so.\n\n", stderr);
	fputs("The Software  is distributed in the hope that it will be useful,  but WITHOUT\n", stderr);
	fputs("ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS\n", stderr);
	fputs("FOR A PARTICULAR PURPOSE.  Use  this  program  at  your  own  risk!\n\n", stderr);
}

/* ======================================================================= */
/* Self-Test                                                               */
/* ======================================================================= */

static void run_selftest(void)
{
	fputs("Self-test is running, please wait...\n", stderr);
	mpatch_selftest();
	fputs("Successful.\n\n", stderr);
}

/* ======================================================================= */
/* Encode                                                                  */
/* ======================================================================= */

static mpatch_error_t encode(const wchar_t *const input_file, const wchar_t *const refernce_file, const wchar_t *const output_file, FILE *const log_file)
{
	//Get the parameter limits
	mpatch_limit_t limits;
	mpatch_get_limits(&limits);

	//Clear errors initially
	errno = 0;

	//Determine thread count
	const uint_fast32_t thread_count = env_get_uint32(L"MPATCH_THREADS", limits.max_thread_count, 0U);
	if (errno)
	{
		print_error(errno);
		fputs("Number of threads is invalid or outside of the valid range!\n\n", stderr);
		return MPATCH_INVALID_PARAMETER;
	}

	//Print info
	fprintf(stderr, "Creating patch \"%S\" <-- \"%S\"\n\n", basename(input_file), basename(refernce_file));
	
	//Map input file to memory
	rd_view_t *input_view;
	const io_error_t error_input = map_file_rd(&input_view, input_file);
	if (error_input != IO_SUCCESS)
	{
		fprintf(stderr, "Failed to open input file: %s\n\n", translate_io_error(error_input));
		return MPATCH_IO_ERROR;
	}

	//Map reference file to memory
	rd_view_t *reference_view;
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
		print_error(error_output);
		fprintf(stderr, "Failed to open output file \"%S\" for writing!\n\n", basename(output_file));
		unmap_file_rd(&input_view);
		unmap_file_rd(&reference_view);
		return MPATCH_IO_ERROR;
	}
	
	//Init progress
	progress_t progress_data;
	memset(&progress_data, 0U, sizeof(progress_t));
	gauss_init(&progress_data.filter);
	progress_data.time_last = time(NULL);

	//Set up the encoder parameters
	mpatch_enc_param_t param;
	memset(&param, 0, sizeof(mpatch_enc_param_t));
	param.message_in.buffer = input_view->data_ptr;
	param.message_in.capacity = input_view->size;
	param.reference_in.buffer = reference_view->data_ptr;
	param.reference_in.capacity = reference_view->size;
	param.compressed_out.writer_func = output_writer;
	param.compressed_out.user_data = (uintptr_t)output_stream;
	param.callback.callback_func = progress_callback;
	param.callback.user_data = (uintptr_t)&progress_data;
	param.thread_count = thread_count ? thread_count : min_uint32(get_processor_count(true), limits.max_thread_count);

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
		fputs("\nFailed!\n\nError: An unexpected error has been encoutered!\n\n", stderr);
	}

	//Compute compression ratioo
	if (!result)
	{
		const int64_t final_size = get_file_size(output_stream);
		fprintf(stderr, "Patch size ratio : %.2f%%\n\n", (final_size > 0) ? (100.0 * ((double)final_size / input_view->size)) : 0.0);
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
		print_error(error_input);
		fprintf(stderr, "Failed to open input file: %S!\n\n", basename(input_file));
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
		const uint_fast32_t digits = log10_uint32(max_uint32(param.file_info.length_msg, param.file_info.length_msg));
		fprintf(stderr, "File format version     : 0x%X\n", param.file_info.fmt_version);
		fprintf(stderr, "Decompressed file size  : %0*u byte(s)\n", (int)digits, param.file_info.length_msg);
		fprintf(stderr, "Reference file size     : %0*u byte(s)\n", (int)digits, param.file_info.length_ref);
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

static int mpatch_main(int argc, wchar_t *const argv[])
{
	//Install signal handler
	signal(SIGINT, sigint_handler);
#ifdef NDEBUG
	signal(SIGABRT, sigabrt_handler);
	signal(SIGILL, sigerr_handler);
	signal(SIGFPE, sigerr_handler);
	signal(SIGSEGV, sigerr_handler);
#endif //NDBUG

	//Any arguments present?
	if (argc < 2)
	{
		print_logo();
		fputs("A required parameter is missing. Please see \"--help\" for details!\n\n", stderr);
		return EXIT_FAILURE;
	}

	//Determine operation
	const operation_t operation = parse_operation(argv[1]);

	//Print the logo
	if (operation != OP_LICN)
	{
		print_logo();
	}
	
	//Print manpage or run selftest?
	switch (operation)
	{
	case OP_HELP:
		print_manpage(argv[0]);
		return EXIT_SUCCESS;
	case OP_LICN:
		print_license();
		return EXIT_SUCCESS;
	case OP_TEST:
		run_selftest();
		return EXIT_SUCCESS;
	case OP_NONE:
		fprintf(stderr, "Operation \"%S\" not supported!\n\n", argv[1]);
		return EXIT_FAILURE;
	}

	//Check parameter count
	if (argc < ((operation != OP_INFO) ? 5 : 3))
	{
		fputs("Required argument is missing. See \"--help\" for details!\n\n", stderr);
		return EXIT_FAILURE;
	}
	
	//Open log file
	FILE *const log_file = (operation != OP_INFO) ? open_log_file() : NULL;

	//Initilaize status
	const clock_t clock_begin = clock();
	mpatch_error_t result = (-1);

	//Run selected operation
	switch(operation)
	{
	case OP_ENCR:
		result = encode(argv[2], argv[3], argv[4], log_file);
		break;
	case OP_DECR:
		fputs("Sorry, operation *not* implemented yet!\n\n", stderr);
		break;
	case OP_INFO:
		result = getnfo(argv[2]);
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

int wmain(int argc, wchar_t *const argv[])
{
#if defined(_MSC_VER) && defined (NDEBUG)
	__try
	{
		return mpatch_main(argc, argv);
	}
	__except(true)
	{
		sigerr_handler(-1);
	}
#else
	return mpatch_main(argc, argv);
#endif
}