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

#define MPATCH_LARGE_PAGES 1

/* ======================================================================= */
/* Types                                                                   */
/* ======================================================================= */

typedef struct 
{
	rd_view_t sView;
	HANDLE hFile;
	HANDLE hMapping;
	bool large_page;
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

static bool get_windows_version(OSVERSIONINFOEXW *const win_ver)
{
	typedef long (WINAPI *RTLGETVERSION)(OSVERSIONINFOEXW*);
	memset(win_ver, 0, sizeof(OSVERSIONINFOEXW));
	const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
	if (ntdll)
	{
		win_ver->dwOSVersionInfoSize = sizeof(OSVERSIONINFOEXW);
		RTLGETVERSION rtl_get_version = (RTLGETVERSION)GetProcAddress(ntdll, "RtlGetVersion");
		if (rtl_get_version)
		{
			return (rtl_get_version(win_ver) == 0L);
		}
	}
	return false;
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
	}
	return success;
}

/* From 7-Zip: We suppose that Window 10 works incorrectly with "Large Pages" at v1703 (15063) and v1709 (16299) */
static bool large_pages_broken(void)
{
	OSVERSIONINFOEXW win_ver;
	if (get_windows_version(&win_ver))
	{
		fprintf(stderr, "%lu.%lu.%lu.%lu\n", win_ver.dwPlatformId, win_ver.dwMajorVersion, win_ver.dwMinorVersion, win_ver.dwBuildNumber);
		if ((win_ver.dwPlatformId == VER_PLATFORM_WIN32_NT) && (win_ver.dwMajorVersion == 10) && (!win_ver.dwMinorVersion) && (win_ver.dwBuildNumber <= 16299U))
		{
			return true;
		}
	}
	return false;
}

static bool large_pages_init(void)
{
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
		InterlockedExchange(&g_privilege_init, 0);
		return false;
	}
	if (!enable_privilege(L"SeLockMemoryPrivilege"))
	{
		InterlockedExchange(&g_privilege_init, 0);
		return false;
	}
	InterlockedExchange(&g_privilege_init, 1);
	return true;
}

static LPVOID large_pages_alloc(const size_t size)
{
	if ((!size) || (!large_pages_init()))
	{
		return NULL;
	}
	const size_t page_size = GetLargePageMinimum();
	if (!page_size)
	{
		return NULL;
	}
	const size_t page_count = 1U + ((size - 1U) / page_size);
	return VirtualAlloc(NULL, page_count * page_size, MEM_COMMIT | MEM_LARGE_PAGES, PAGE_READWRITE);
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
	p->sView.data_ptr = (uint8_t*)MapViewOfFile(p->hMapping, FILE_MAP_READ, 0, 0, p->sView.size);
	if (!p->sView.data_ptr)
	{
		const DWORD error = GetLastError();
		CloseHandle(p->hMapping);
		CloseHandle(p->hFile);
		free(p);
		return translate_error(error);
	}

#if (defined(MPATCH_LARGE_PAGES) && (MPATCH_LARGE_PAGES))
	//Allocate large pages
	if (p->sView.size >= 2097152U)
	{
		const LPVOID large_page_buffer = large_pages_alloc(p->sView.size);
		if (large_page_buffer)
		{
			CopyMemory(large_page_buffer, p->sView.data_ptr, p->sView.size);
			if (!UnmapViewOfFile(p->sView.data_ptr))
			{
				const DWORD error = GetLastError();
				VirtualFree(large_page_buffer, 0U, MEM_RELEASE);
				CloseHandle(p->hMapping);
				CloseHandle(p->hFile);
				free(p);
				return translate_error(error);
			}
			p->sView.data_ptr = (uint8_t*)large_page_buffer;
			p->large_page = true;
			fputs("Large pages allocated!\n", stderr);
		}
	}
#endif /*MPATCH_LARGE_PAGES enabled*/

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
		if (p->large_page)
		{
			if (!VirtualFree((LPVOID)p->sView.data_ptr, 0U, MEM_RELEASE))
			{
				success = FALSE;
			}
		}
		else
		{
			if (!UnmapViewOfFile(p->sView.data_ptr))
			{
				success = FALSE;
			}
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
