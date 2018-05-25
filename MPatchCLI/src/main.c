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
/* Progress display                                                        */
/* ======================================================================= */

static const char SPINNER[] = "-\\|/";

static bool progress_callback(const float progress, const float ratio, const uintptr_t data)
{
	size_t *const spinner_pos = (size_t*)data;
	fprintf(stderr, "\rProgress: %.2f%%, Ratio: ~%.2f%% [%c]   \b\b\b", 100.0 * progress, 100.0 * ratio, SPINNER[*spinner_pos]);
	fflush(stderr);
	*spinner_pos = (*spinner_pos + 1U) & 3U;
	return (!g_stop_flag);
}

/* ======================================================================= */
/* I/O handler                                                             */
/* ======================================================================= */

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
	OP_EXEC = 2,
	OP_TEST = 3,
	OP_HELP = 4
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
	{ OP_EXEC, L"-e", L"--exec" },
	{ OP_TEST, L"-t", L"--test" },
	{ OP_HELP, L"-h", L"--help" },
	{ OP_NONE }
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

	//Set up the encoder parameters
	mpatch_enc_param_t param;
	size_t spinner_pos = 0U;
	memset(&param, 0, sizeof(mpatch_enc_param_t));
	param.message_in.buffer = input_view.data_ptr;
	param.message_in.capacity = input_view.size;
	param.reference_in.buffer = reference_view.data_ptr;
	param.reference_in.capacity = reference_view.size;
	param.compressed_out.writer_func = output_writer;
	param.compressed_out.user_data = (uintptr_t)output_stream;
	param.callback.callback_func = progress_callback;
	param.callback.user_data = (uintptr_t)&spinner_pos;

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
		fputs("\nFailed!\n\nError: An unexpected error has been encoutered!", stderr);
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
	if (argc < 5)
	{
		fputs("Required argument is missing. See \"--help\" for details!\n\n", stderr);
		return EXIT_FAILURE;
	}

	//Install signal handler
	signal(SIGINT, sigint_handler);

	//Open log file
	FILE *const log_file = open_log_file();

	//Initilaize status
	const clock_t clock_begin = clock();
	mpatch_error_t result = (-1);

	//Run selected operation
	switch(operation)
	{
	case OP_DIFF:
		result = encode(argv[2], argv[3], argv[4], log_file);
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
		fprintf(stderr, "--------\n\nOperation took %u minute(s), %u second(s).\n\n", total_seconds / 60U, total_seconds % 60U);
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
