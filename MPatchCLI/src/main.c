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
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <share.h>

#define WIN32_LEAN_AND_MEAN 1
#include <Windows.h>

/* ======================================================================= */
/* Memory mapped I/O                                                       */
/* ======================================================================= */

typedef struct
{
	HANDLE hFile;
	HANDLE hMapping;
	BYTE *pView;
	ULONGLONG size;
}
file_view_t;

static bool map_file(file_view_t *const view, const WCHAR *const fileName, const bool writable, const ULONGLONG requested_size)
{
	//Open file
	const DWORD accessFlags = writable ? (GENERIC_READ | GENERIC_WRITE) : GENERIC_READ;
	const DWORD createFlags = writable ? CREATE_ALWAYS : OPEN_EXISTING;
	view->hFile = CreateFileW(fileName, accessFlags, FILE_SHARE_READ, NULL, createFlags, 0, 0);
	if (view->hFile == INVALID_HANDLE_VALUE)
	{
		const DWORD error = GetLastError();
		fprintf(stderr, "Error: Failed to open file for %s! [0x%08X]\n\n", (writable ? "writing" : "reading"), error);
		return false;
	}

	//Determine size
	ULARGE_INTEGER mapped_size;
	if (!requested_size)
	{
		DWORD fileSizeHigh = 0;
		const DWORD fileSizeLow = GetFileSize(view->hFile, &fileSizeHigh);
		if ((!fileSizeHigh) && (!fileSizeLow))
		{
			fprintf(stderr, "Error: File appears to be empty!\n\n");
			CloseHandle(view->hFile);
			return false;
		}
		mapped_size.HighPart = fileSizeHigh;
		mapped_size.LowPart = fileSizeLow;
	}
	else
	{
		mapped_size.QuadPart = requested_size;
	}

	//Check size
	if (mapped_size.QuadPart > ((ULONGLONG)MAXSIZE_T))
	{
		fprintf(stderr, "Error: File size exceeds maximum mappable size!\n\n");
		CloseHandle(view->hFile);
		return false;
	}

	//Create file mapping
	view->hMapping = CreateFileMappingW(view->hFile, NULL, writable ? PAGE_READWRITE : PAGE_READONLY, mapped_size.HighPart, mapped_size.LowPart, NULL);
	if (!view->hMapping)
	{
		const DWORD error = GetLastError();
		fprintf(stderr, "Error: Failed to create memory mapping of file! [0x%08X]\n\n", error);
		CloseHandle(view->hFile);
		return false;
	}

	//Map view of file
	const DWORD mappingFlags = writable ? (FILE_MAP_WRITE | FILE_MAP_READ) : FILE_MAP_READ;
	view->pView = (BYTE*)MapViewOfFile(view->hMapping, mappingFlags, 0, 0, (SIZE_T)mapped_size.QuadPart);
	if (!view->pView)
	{
		const DWORD error = GetLastError();
		fprintf(stderr, "Error: Failed to create view of file! [0x%08X]\n\n", error);
		CloseHandle(view->hMapping);
		CloseHandle(view->hFile);
		return false;
	}

	view->size = mapped_size.QuadPart;
	return true;
}

static void unmap_file(file_view_t *const view, const ULONGLONG set_size)
{
	if (view)
	{
		UnmapViewOfFile(view->pView);
		CloseHandle(view->hMapping);
		if (set_size != MAXULONGLONG)
		{
			ULARGE_INTEGER file_pointer;
			file_pointer.QuadPart = set_size;
			SetFilePointer(view->hFile, file_pointer.LowPart, &file_pointer.HighPart, FILE_BEGIN);
			SetEndOfFile(view->hFile);
		}
		CloseHandle(view->hFile);
		memset(view, 0, sizeof(file_view_t));
	}
}

/* ======================================================================= */
/* Progress display                                                        */
/* ======================================================================= */

static const char SPINNER[] = "-\\|/";

static int progress_callback(const float progress, const float ratio, void *const data)
{
	size_t *const spinner_pos = (size_t*)data;
	fprintf(stderr, "\rProgress: %.2f%%, Ratio: %.2f%% [%c]   \b\b\b", 100.0 * progress, 100.0 * ratio, SPINNER[*spinner_pos]);
	fflush(stderr);
	*spinner_pos = (*spinner_pos + 1U) & 3U;
	return 1; /*continue*/
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
	fprintf(stderr, "Usage:\n");
	fprintf(stderr, "  mpatch.exe --encode <input_file> <reference_file> <output_file>\n");
	fprintf(stderr, "  mpatch.exe --decode <patch_file> <reference_file> <output_file>\n");
	fprintf(stderr, "  mpatch.exe --help\n\n");
	fprintf(stderr, "  mpatch.exe --selftest\n\n");
	fprintf(stderr, "Examples:\n");
	fprintf(stderr, "  mpatch.exe --encode new_prog.exe old_prog.exe update.patch\n");
	fprintf(stderr, "  mpatch.exe --decode update.patch old_prog.exe new_prog.exe\n\n");
}

/* ======================================================================= */
/* MAIN                                                                    */
/* ======================================================================= */

static const wchar_t *const OPERATIONS[4] =
{
	L"--help",
	L"--encode",
	L"--decode",
	L"--selftest"
};

int wmain(int argc, wchar_t *const argv[])
{
	print_logo();

	if ((argc < 2) || (!_wcsicmp(argv[1], OPERATIONS[0])))
	{
		print_manpage();
		return EXIT_FAILURE;
	}

	if (_wcsicmp(argv[1], OPERATIONS[0]) && _wcsicmp(argv[1], OPERATIONS[1]) && _wcsicmp(argv[1], OPERATIONS[2]) && _wcsicmp(argv[1], OPERATIONS[3]))
	{
		fprintf(stderr, "Operation \"%S\" not supported!\n\n", argv[1]);
		return EXIT_FAILURE;
	}

	if (!_wcsicmp(argv[1], OPERATIONS[3]))
	{
		fputs("Self-test is running, please wait...\n", stderr);
		mpatch_selftest();
		fputs("Successful.\n\n", stderr);
		return EXIT_SUCCESS;
	}

	if (argc < 5)
	{
		fputs("Required argument is missing. See \"--help\" for details!\n\n", stderr);
		return EXIT_FAILURE;
	}

	file_view_t input_file;
	if (!map_file(&input_file, argv[2], false, 0U))
	{
		fputs("Failed to open input file!\n\n", stderr);
		return EXIT_FAILURE;
	}

	file_view_t reference_file;
	if (!map_file(&reference_file, argv[3], false, 0U))
	{
		fputs("Failed to open reference file!\n\n", stderr);
		unmap_file(&input_file, MAXULONGLONG);
		return EXIT_FAILURE;
	}

	file_view_t output_file;
	if (!map_file(&output_file, argv[4], true, input_file.size))
	{
		fputs("Failed to open output file!\n\n", stderr);
		unmap_file(&input_file, MAXULONGLONG);
		unmap_file(&reference_file, MAXULONGLONG);
		return EXIT_FAILURE;
	}

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

	mpatch_enc_param_t param;
	memset(&param, 0, sizeof(mpatch_enc_param_t));

	param.message_in = input_file.pView;
	param.message_size = input_file.size;
	param.reference_in = reference_file.pView;
	param.reference_size = reference_file.size;
	param.compressed_out = output_file.pView;
	param.compressed_capacity = output_file.size;
	param.callback = progress_callback;
	param.user_data = &spinner_pos;
	param.trace_log_file = logfile_ptr;

	const mpatch_error_t result = mpatch_encode(&param);
	switch (result)
	{
	case MPATCH_SUCCESS:
		fprintf(stderr, "\nDone.\n\nCompression ratio: %.1f%%\n\n", 100.0 * ((double)param.compressed_size / (double)input_file.size));
		break;
	case MPATCH_INSUFFICIENT_BUFFER:
		fputs("\nFailed!\n\nWhoops, the file could not be compressed!\n\n", stderr);
		break;
	default:
		fprintf(stderr, "\nFailed!\n\nError: Something went wrong! [Code: %d]\n\n", result);
	}

	if (logfile_ptr)
	{
		fclose(logfile_ptr);
		logfile_ptr = NULL;
	}

	unmap_file(&input_file, MAXULONGLONG);
	unmap_file(&reference_file, MAXULONGLONG);
	unmap_file(&output_file, param.compressed_size);
		
    return (result == MPATCH_SUCCESS) ? EXIT_SUCCESS : EXIT_FAILURE;
}
