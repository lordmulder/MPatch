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
#include <io.h>
#include <sys/stat.h> 
#include <stdarg.h>

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
	return true; /*continue*/
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
/* I/O handler                                                             */
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

static const wchar_t *const OPERATIONS[] =
{
	L"--diff",
	L"--exec",
	L"--test",
	L"--help",
	NULL
};

static size_t parse_operation(const wchar_t *const arg)
{
	for (size_t i = 0U; OPERATIONS[i]; ++i)
	{
		if (!_wcsicmp(arg, OPERATIONS[i]))
		{
			return i;
		}
	}
	return SIZE_MAX;
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
	fputs("  --diff  Create a new patch file\n", stderr);
	fputs("  --diff  Apply an existing patch file\n", stderr);
	fputs("  --test  Run self-test\n", stderr);
	fputs("  --help  Print this help screen\n\n", stderr);
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
	const size_t operation = parse_operation(argv[1]);
	if (operation == SIZE_MAX)
	{
		fprintf(stderr, "Operation \"%S\" not supported!\n\n", argv[1]);
		return EXIT_FAILURE;
	}

	//Print manpage?
	if (operation == 3U)
	{
		print_manpage();
		return EXIT_SUCCESS;
	}

	//Run self-test
	if (operation == 2U)
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

	//Print info
	fprintf(stderr, "Creating patch \"%S\" <- \"%S\"\n\n", basename(argv[2]), basename(argv[3]));

	//Map input file to memory
	rd_view_t input_file;
	const io_error_t error_input = map_file_rd(&input_file, argv[2]);
	if (error_input != IO_SUCCESS)
	{
		fprintf(stderr, "Failed to open input file: %s\n\n", translate_io_error(error_input));
		return EXIT_FAILURE;
	}

	//Map reference file to memory
	rd_view_t reference_file;
	const io_error_t error_reference = map_file_rd(&reference_file, argv[3]);
	if (error_reference != IO_SUCCESS)
	{
		fprintf(stderr, "Failed to open reference file: %s\n\n", translate_io_error(error_reference));
		unmap_file_rd(&input_file);
		return EXIT_FAILURE;
	}

	//Open the output file for writing
	FILE *output_file;
	const errno_t error_output = _wfopen_s(&output_file, argv[4], L"wb");
	if (error_output)
	{
		char err_message[128U];
		strerror_s(err_message, 128U, error_output);
		fprintf(stderr, "Failed to open output file: %s!\n\n", err_message);
		unmap_file_rd(&input_file);
		unmap_file_rd(&reference_file);
		return EXIT_FAILURE;
	}

	//Open log file
	wchar_t *logfile_name = NULL;
	size_t logfile_size = 0U, spinner_pos = 0U;
	FILE *logfile_ptr = NULL;
	if (!_wdupenv_s(&logfile_name, &logfile_size, L"MPATCH_LOGFILE"))
	{
		if (logfile_name)
		{
			if (!(logfile_ptr = _wfsopen(logfile_name, L"wt", _SH_DENYWR)))
			{
				fprintf(stderr, "Warning: Failed to open logfile \"%S\" for writing!\n\n", logfile_name);
			}
			free(logfile_name);
			logfile_name = NULL;
		}
	}

	//Set up the encoder parameters
	mpatch_enc_param_t param;
	memset(&param, 0, sizeof(mpatch_enc_param_t));
	param.message_in.buffer = input_file.data_ptr;
	param.message_in.capacity = input_file.size;
	param.reference_in.buffer = reference_file.data_ptr;
	param.reference_in.capacity = reference_file.size;
	param.compressed_out.writer_func = output_writer;
	param.compressed_out.user_data = (uintptr_t)output_file;
	param.callback.callback_func = progress_callback;
	param.callback.user_data = (uintptr_t)&spinner_pos;

	//Enable trace logging
	if (logfile_ptr)
	{
		param.trace_logger.logging_func = logging_handler;
		param.trace_logger.user_data = (uintptr_t)logfile_ptr;
	}

	//Process!
	const clock_t clock_begin = clock();
	mpatch_error_t result = mpatch_encode(&param);
	const clock_t clock_end = clock();

	//Handle the result
	switch (result)
	{
	case MPATCH_SUCCESS:
		{
			const int64_t final_size = get_file_size(output_file);
			fprintf(stderr, "\nDone.\n\nCompression ratio: %.2f%%\n\n", (final_size > 0) ? (100.0 * ((double)final_size / input_file.size)) : 0.0);
		}
		break;
	case MPATCH_IO_ERROR:
		fputs("\nFailed!\n\nFailed to write compressed data to output file!\n\n", stderr);
		break;
	default:
		fprintf(stderr, "\nFailed!\n\nError: An unexpected error has been encoutered! [Code: %d]\n\n", result);
	}

	//Close the outout file
	fclose(output_file);
	output_file = NULL;

	//Unmap all files from memory
	if (unmap_file_rd(&input_file) != IO_SUCCESS)
	{
		fputs("Failed to close the input file!\n\n", stderr);
		result = MPATCH_INTERNAL_ERROR;
	}
	if (unmap_file_rd(&reference_file) != IO_SUCCESS)
	{
		fputs("Failed to close the reference file!\n\n", stderr);
		result = MPATCH_INTERNAL_ERROR;
	}

	//Close the log file
	if (logfile_ptr)
	{
		fclose(logfile_ptr);
		logfile_ptr = NULL;
	}

	//Compute overall time
	const clock_t total_seconds = (clock_end - clock_begin) / CLOCKS_PER_SEC;
	if (total_seconds >= 60U)
	{
		fprintf(stderr, "--------\n\nOperation took %u minute(s), %u second(s).\n\n", total_seconds / 60U, total_seconds % 60U);
	}
	else
	{
		fprintf(stderr, "--------\n\nOperation took %u second(s).\n\n", total_seconds);
	}
	

	//Completed
	return (result == MPATCH_SUCCESS) ? EXIT_SUCCESS : EXIT_FAILURE;
}
