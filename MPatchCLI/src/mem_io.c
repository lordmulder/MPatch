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
#include <stdio.h>

#define MPATCH_LARGE_PAGES true

/* ======================================================================= */
/* Types                                                                   */
/* ======================================================================= */

typedef struct 
{
	rd_view_t sView;
	HANDLE hFile;
	HANDLE hMapping;
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

static volatile LONG g_privilege_init = 0L;

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
#define OUTPUT_DEBUG_STR(FMT, ...) do \
{ \
	char _buffer[128U]; \
	_snprintf_s(_buffer, 128U, _TRUNCATE, (FMT), __VA_ARGS__); \
	OutputDebugStringA(_buffer); \
} \
while(0)

static bool get_windows_version(OSVERSIONINFOEXW *const win_ver)
{
	typedef long (WINAPI *RTLGETVERSION)(OSVERSIONINFOEXW*);
	memset(win_ver, 0, sizeof(OSVERSIONINFOEXW));
	const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
	if (ntdll)
	{
		win_ver->dwOSVersionInfoSize = sizeof(OSVERSIONINFOEXW);
		const RTLGETVERSION rtl_get_version = (RTLGETVERSION)GetProcAddress(ntdll, "RtlGetVersion");
		if (rtl_get_version)
		{
			return (rtl_get_version(win_ver) == 0L);
		}
	}
	return false;
}

static size_t get_large_page_size(void)
{
	typedef SIZE_T(WINAPI *GETLARGEPAGEMINIMUM)(VOID);
	const HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
	if (kernel32)
	{
		const GETLARGEPAGEMINIMUM get_large_page_minimum = (GETLARGEPAGEMINIMUM)GetProcAddress(kernel32, "GetLargePageMinimum");
		if (get_large_page_minimum)
		{
			return get_large_page_minimum();
		}
	}
	return 0U;
}

static bool enable_privilege(const WCHAR *const name)
{
	bool success = false;
	HANDLE token;
	if (OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES, &token))
	{
		TOKEN_PRIVILEGES tp;
		tp.PrivilegeCount = 1U;
		if (LookupPrivilegeValueW(NULL, name, &tp.Privileges[0].Luid))
		{
			tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
			if (AdjustTokenPrivileges(token, FALSE, &tp, 0U, NULL, NULL))
			{
				success = true;
			}
		}
		CloseHandle(token);
	}
	return success;
}

/* From 7-Zip: We suppose that Window 10 works incorrectly with "Large Pages" at v1703 (15063) and v1709 (16299) */
static bool large_pages_broken(void)
{
	OSVERSIONINFOEXW win_ver;
	if (get_windows_version(&win_ver))
	{
		if ((win_ver.dwPlatformId == VER_PLATFORM_WIN32_NT) && (win_ver.dwMajorVersion == 10) && (!win_ver.dwMinorVersion) && (win_ver.dwBuildNumber <= 16299U))
		{
			return true;
		}
	}
	return false;
}

static bool large_pages_init(void)
{
	static const WCHAR *const PRIV_LOCK_MEM = L"SeLockMemoryPrivilege";
	LONG state;
	while ((state = InterlockedCompareExchange(&g_privilege_init, -1L, 0L)) != 0L)
	{
		if (state > 0)
		{
			return true; /*already initialized*/
		}
	}
	if (large_pages_broken())
	{
		OutputDebugStringA("[MPatch] Large pages are *broken* on this system!\n");
		InterlockedExchange(&g_privilege_init, 0);
		return false;
	}
	if (!enable_privilege(PRIV_LOCK_MEM))
	{
		OutputDebugStringA("[MPatch] Failed to accquire 'SeLockMemoryPrivilege' privilege!");
		InterlockedExchange(&g_privilege_init, 0);
		return false;
	}
	InterlockedExchange(&g_privilege_init, 1);
	return true;
}

static int large_pages_alloc(const size_t size, HANDLE *const mapping, const uint8_t **const data_ptr)
{
	//Check parameters
	if (!(size && large_pages_init()))
	{
		return 0;
	}

	//Get lage page size
	const size_t page_size = get_large_page_size();
	if (!page_size)
	{
		OutputDebugStringA("[MPatch] Large pages are *not* supported on this system!\n");
		return 0;
	}

	//Compute allocation size
	ULARGE_INTEGER lp_size;
	lp_size.QuadPart = page_size * (1U + ((size - 1U) / page_size));

	//Allocate
	const HANDLE lp_mapping = CreateFileMappingW(NULL, NULL, PAGE_READWRITE | SEC_COMMIT | SEC_LARGE_PAGES, lp_size.HighPart, lp_size.LowPart, NULL);
	if (!lp_mapping)
	{
		const DWORD error = GetLastError();
		OUTPUT_DEBUG_STR("[MPatch] Large page CreateFileMappingW failed with error 0x%lX!\n", error);
		return 0;
	}

	//Create new view
	uint8_t *const lp_ptr = (uint8_t*)MapViewOfFile(lp_mapping, FILE_MAP_READ | FILE_MAP_WRITE, 0U, 0U, 0U);
	if (!lp_ptr)
	{
		const DWORD error = GetLastError();
		OUTPUT_DEBUG_STR("[MPatch] Large page MapViewOfFile failed with error 0x%lX!\n", error);
		CloseHandle(lp_mapping);
		return 0;
	}

	//Copy file data
	CopyMemory(lp_ptr, *data_ptr, size);

	//Close original mapping
	if (!(UnmapViewOfFile(*data_ptr) && CloseHandle(*mapping)))
	{
		UnmapViewOfFile(lp_ptr);
		CloseHandle(lp_mapping);
		return -1;
	}

	//Save pointers
	*mapping = lp_mapping;
	*data_ptr = lp_ptr;

	return 1;
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
	p->sView.data_ptr = (uint8_t*)MapViewOfFile(p->hMapping, FILE_MAP_READ, 0U, 0U, 0U);
	if (!p->sView.data_ptr)
	{
		const DWORD error = GetLastError();
		CloseHandle(p->hMapping);
		CloseHandle(p->hFile);
		free(p);
		return translate_error(error);
	}

	//Allocate large pages
	if (MPATCH_LARGE_PAGES && (p->sView.size >= 2097152U))
	{
		const int lp_ret = large_pages_alloc(p->sView.size, &p->hMapping, &p->sView.data_ptr);
		if (lp_ret > 0)
		{
			OutputDebugStringA("[MPatch] Large pages allocated successfully.\n");
		}
		else
		{
			OutputDebugStringA("[MPatch] Failed to allocate large pages!\n");
			if (lp_ret < 0)
			{
				return IO_FAILED;
			}
		}
	}
	else
	{
		OutputDebugStringA("[MPatch] Not using large pages (input too small).\n");
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
