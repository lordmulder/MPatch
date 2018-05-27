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

#define WIN32_LEAN_AND_MEAN 1
#include <Windows.h>

#include <malloc.h>
#include <memory.h>

/* ======================================================================= */
/* Memory mapped I/O                                                       */
/* ======================================================================= */

typedef struct 
{
	HANDLE hFile;
	HANDLE hMapping;
}
file_view_priv_t;

static io_error_t translate_error(const DWORD win32error)
{
	switch (win32error)
	{
	case ERROR_FILE_NOT_FOUND:
	case ERROR_PATH_NOT_FOUND:
		return IO_FILE_NOT_FOUND;
	case ERROR_ACCESS_DENIED:
		return IO_ACCESS_DENIED;
	case ERROR_OUTOFMEMORY:
		return IO_OUT_OF_MEMORY;
	default:
		return IO_FAILED;
	}
}

static BOOL set_end_of_file(const HANDLE hFile, const uint32_t size)
{
	if (SetFilePointer(hFile, size, NULL, FILE_BEGIN))
	{
		return SetEndOfFile(hFile);
	}
	return FALSE;
}

io_error_t map_file_rd(rd_view_t *const view, const wchar_t *const fileName)
{
	//Allocate context
	memset(view, 0, sizeof(rd_view_t));
	file_view_priv_t *const p = (file_view_priv_t*)malloc(sizeof(file_view_priv_t));
	if (!p)
	{
		return IO_OUT_OF_MEMORY;
	}

	//Open file
	p->hFile = CreateFileW(fileName, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, 0);
	if (p->hFile == INVALID_HANDLE_VALUE)
	{
		const DWORD error = GetLastError();
		free(p);
		return translate_error(error);
	}

	//Determine size
	DWORD fileSizeHigh = 0;
	const DWORD fileSizeLow = GetFileSize(p->hFile, &fileSizeHigh);
	if (fileSizeHigh)
	{
		CloseHandle(p->hFile);
		free(p);
		return IO_FILE_TOO_LARGE;
	}
	if (!fileSizeLow)
	{
		CloseHandle(p->hFile);
		free(p);
		return IO_FILE_IS_EMPTY;
	}

	//Create file mapping
	p->hMapping = CreateFileMappingW(p->hFile, NULL, PAGE_READONLY, 0U, fileSizeLow, NULL);
	if (!p->hMapping)
	{
		const DWORD error = GetLastError();
		CloseHandle(p->hFile);
		free(p);
		return translate_error(error);
	}

	//Map view of file
	view->data_ptr = (BYTE*)MapViewOfFile(p->hMapping, FILE_MAP_READ, 0, 0, fileSizeLow);
	if (!view->data_ptr)
	{
		const DWORD error = GetLastError();
		CloseHandle(p->hMapping);
		CloseHandle(p->hFile);
		free(p);
		return translate_error(error);
	}

	view->size = fileSizeLow;
	view->private = (uintptr_t)p;
	return IO_SUCCESS;
}

io_error_t map_file_wr(wr_view_t *const view, const wchar_t *const fileName, const uint32_t size)
{
	//Allocate context
	memset(view, 0, sizeof(wr_view_t));
	file_view_priv_t *const p = (file_view_priv_t*)malloc(sizeof(file_view_priv_t));
	if (!p)
	{
		return IO_OUT_OF_MEMORY;
	}

	//Open file
	p->hFile = CreateFileW(fileName, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, 0, 0);
	if (p->hFile == INVALID_HANDLE_VALUE)
	{
		const DWORD error = GetLastError();
		free(p);
		return translate_error(error);
	}

	//Create file mapping
	p->hMapping = CreateFileMappingW(p->hFile, NULL, PAGE_READWRITE, 0U, size, NULL);
	if (!p->hMapping)
	{
		const DWORD error = GetLastError();
		CloseHandle(p->hFile);
		free(p);
		return translate_error(error);
	}

	//Map view of file
	view->data_ptr = (BYTE*)MapViewOfFile(p->hMapping, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, size);
	if (!view->data_ptr)
	{
		const DWORD error = GetLastError();
		CloseHandle(p->hMapping);
		CloseHandle(p->hFile);
		free(p);
		return translate_error(error);
	}

	view->size = size;
	view->private = (uintptr_t)p;
	return IO_SUCCESS;
}

io_error_t unmap_file_rd(rd_view_t *const view)
{
	BOOL success = FALSE;
	if (view->data_ptr && view->private)
	{
		success = TRUE;
		file_view_priv_t *const p = (file_view_priv_t*)view->private;
		if (!UnmapViewOfFile(view->data_ptr))
		{
			success = FALSE;
		}
		if (!CloseHandle(p->hMapping))
		{
			success = FALSE;
		}
		if (!CloseHandle(p->hFile))
		{
			success = FALSE;
		}
		free(p);
	}
	memset(view, 0, sizeof(rd_view_t));
	return success ? IO_SUCCESS : IO_FAILED;
}

io_error_t unmap_file_wr(wr_view_t *const view, const uint32_t final_size)
{
	BOOL success = FALSE;
	if (view->data_ptr && view->private)
	{
		success = TRUE;
		file_view_priv_t *const p = (file_view_priv_t*)view->private;
		if (!UnmapViewOfFile(view->data_ptr))
		{
			success = FALSE;
		}
		if (!CloseHandle(p->hMapping))
		{
			success = FALSE;
		}
		if (!set_end_of_file(p->hFile, final_size))
		{
			success = FALSE;
		}
		if (!CloseHandle(p->hFile))
		{
			success = FALSE;
		}
		free(p);
	}
	memset(view, 0, sizeof(rd_view_t));
	return success ? IO_SUCCESS : IO_FAILED;
}
