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
#include <stdbool.h>

/* ======================================================================= */
/* Types                                                                   */
/* ======================================================================= */

typedef struct 
{
	rd_view_t sView;
	HANDLE hFile;
	HANDLE hMapping;
	bool is_locked;
}
rd_private_t;

typedef struct
{
	wr_view_t sView;
	HANDLE hFile;
	HANDLE hMapping;
}
wr_private_t;

/* ======================================================================= */
/* Internal Functions                                                      */
/* ======================================================================= */

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

static io_error_t prefetch_virtual_memory(const void *const address, const size_t size)
{
	typedef struct _WIN32_MEMORY_RANGE_ENTRY { PVOID address; SIZE_T size; } WIN32_MEMORY_RANGE_ENTRY, *PWIN32_MEMORY_RANGE_ENTRY;
	typedef BOOL(WINAPI *PREFETCH_VIRTUAL_MEMORY)(HANDLE, ULONG_PTR, PWIN32_MEMORY_RANGE_ENTRY, ULONG);

	const HMODULE kernel32 = GetModuleHandleW(L"kernel32");
	if (!kernel32)
	{
		return IO_SUCCESS; /*unsupported*/
	}

	const PREFETCH_VIRTUAL_MEMORY prefetch_virtual_memory = (PREFETCH_VIRTUAL_MEMORY)GetProcAddress(kernel32, "PrefetchVirtualMemory");
	if (!prefetch_virtual_memory)
	{
		return IO_SUCCESS; /*unsupported*/
	}
	
	WIN32_MEMORY_RANGE_ENTRY range_entry = { (PVOID)address, size };
	if (!prefetch_virtual_memory(GetCurrentProcess(), 1UL, &range_entry, 0UL))
	{
		const DWORD error = GetLastError();
		return translate_error(error);
	}

	return IO_SUCCESS;
}

static bool increment_working_set_size(const size_t amount)
{
	SIZE_T ws_size_min, ws_size_max;
	if (GetProcessWorkingSetSize(GetCurrentProcess(), &ws_size_min, &ws_size_max))
	{
		if (ws_size_max < (ws_size_min += amount))
		{
			ws_size_max = ws_size_min;
		}
		if (SetProcessWorkingSetSize(GetCurrentProcess(), ws_size_min, ws_size_max))
		{
			return true;
		}
	}
	return false;
}

/* ======================================================================= */
/* Public Interface                                                        */
/* ======================================================================= */

io_error_t map_file_rd(rd_view_t **const view, const wchar_t *const fileName)
{
	*view = NULL;

	//Allocate context
	rd_private_t *const p = (rd_private_t*) calloc(1U, sizeof(rd_private_t));
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
	p->sView.size = GetFileSize(p->hFile, &fileSizeHigh);
	if (fileSizeHigh)
	{
		CloseHandle(p->hFile);
		free(p);
		return IO_FILE_TOO_LARGE;
	}
	if (!p->sView.size)
	{
		CloseHandle(p->hFile);
		free(p);
		return IO_FILE_IS_EMPTY;
	}

	//Create file mapping
	p->hMapping = CreateFileMappingW(p->hFile, NULL, PAGE_READONLY, 0U, p->sView.size, NULL);
	if (!p->hMapping)
	{
		const DWORD error = GetLastError();
		CloseHandle(p->hFile);
		free(p);
		return translate_error(error);
	}

	//Map view of file
	p->sView.data_ptr = (BYTE*)MapViewOfFile(p->hMapping, FILE_MAP_READ, 0, 0, p->sView.size);
	if (!p->sView.data_ptr)
	{
		const DWORD error = GetLastError();
		CloseHandle(p->hMapping);
		CloseHandle(p->hFile);
		free(p);
		return translate_error(error);
	}

	//Prefetch memory
	const io_error_t prefetch_error = prefetch_virtual_memory(p->sView.data_ptr, p->sView.size);
	if (prefetch_error)
	{
		UnmapViewOfFile(p->sView.data_ptr);
		CloseHandle(p->hMapping);
		CloseHandle(p->hFile);
		free(p);
		return prefetch_error;
	}

	//Try to increment working set
	increment_working_set_size(p->sView.size);
	
	//Try to lock in memory
	if (VirtualLock((PVOID)p->sView.data_ptr, p->sView.size))
	{
		p->is_locked = true;
	}

	*view = (rd_view_t*)p;
	return IO_SUCCESS;
}

io_error_t map_file_wr(wr_view_t **const view, const wchar_t *const fileName, const uint32_t size)
{
	*view = NULL;

	//Allocate context
	wr_private_t *const p = (wr_private_t*) calloc(1U, sizeof(wr_private_t));
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
	p->hMapping = CreateFileMappingW(p->hFile, NULL, PAGE_READWRITE, 0U, p->sView.size = size, NULL);
	if (!p->hMapping)
	{
		const DWORD error = GetLastError();
		CloseHandle(p->hFile);
		free(p);
		return translate_error(error);
	}

	//Map view of file
	p->sView.data_ptr = (BYTE*)MapViewOfFile(p->hMapping, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, size);
	if (!p->sView.data_ptr)
	{
		const DWORD error = GetLastError();
		CloseHandle(p->hMapping);
		CloseHandle(p->hFile);
		free(p);
		return translate_error(error);
	}

	*view = (wr_view_t*)p;
	return IO_SUCCESS;
}

io_error_t unmap_file_rd(rd_view_t **const view)
{
	BOOL success = FALSE;
	rd_private_t *const p = (rd_private_t*)(*view);

	if (p && p->sView.data_ptr)
	{
		success = TRUE;
		if (p->is_locked)
		{
			if (!VirtualUnlock((PVOID)p->sView.data_ptr, p->sView.size))
			{
				success = FALSE;
			}
		}
		if (!UnmapViewOfFile(p->sView.data_ptr))
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
	}

	free(*view);
	*view = NULL;

	return success ? IO_SUCCESS : IO_FAILED;
}

io_error_t unmap_file_wr(wr_view_t **const view)
{
	BOOL success = FALSE;
	wr_private_t *const p = (wr_private_t*)(*view);

	if (p && p->sView.data_ptr)
	{
		success = TRUE;
		if (!UnmapViewOfFile(p->sView.data_ptr))
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
	}

	free(*view);
	*view = NULL;

	return success ? IO_SUCCESS : IO_FAILED;
}
