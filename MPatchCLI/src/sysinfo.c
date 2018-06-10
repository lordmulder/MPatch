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

#include "sysinfo.h"

#define WIN32_LEAN_AND_MEAN 1
#include <Windows.h>
#include <malloc.h>

typedef BOOL(WINAPI *GET_LOGICAL_PROCINFO)(PSYSTEM_LOGICAL_PROCESSOR_INFORMATION, PDWORD);

static uint_fast32_t count_set_bits(DWORD_PTR mask)
{
	uint_fast32_t bit_count = 0U;
	while (mask)
	{
		if (mask & 1U)
		{
			bit_count++;
		}
		mask >>= 1U;
	}
	return bit_count;
}

static uint_fast32_t detect_processor_count_ex(const bool logical_cores)
{
	const HMODULE kernel32 = GetModuleHandleW(L"kernel32");
	if (!kernel32)
	{
		return 0U; /*unsupported*/
	}

	const GET_LOGICAL_PROCINFO get_logical_procinfo = (GET_LOGICAL_PROCINFO) GetProcAddress(kernel32, "GetLogicalProcessorInformation");
	if (!get_logical_procinfo)
	{
		return 0U; /*unsupported*/
	}

	PSYSTEM_LOGICAL_PROCESSOR_INFORMATION buffer = NULL;
	DWORD return_length = 0U;
	while(!get_logical_procinfo(buffer, &return_length))
	{
		if (GetLastError() == ERROR_INSUFFICIENT_BUFFER)
		{
			if (!(buffer = (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION)realloc(buffer, return_length)))
			{
				return 0U;
			}
		}
		else
		{
			return 0U; /*failed*/
		}
	}

	uint_fast32_t processor_core_count = 0U;
	const DWORD info_count = return_length / sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION);
	for(DWORD i = 0U; i < info_count; ++i)
	{
		if (buffer[i].Relationship == RelationProcessorCore)
		{
			if (logical_cores)
			{
				const uint_fast32_t bit_count = count_set_bits(buffer[i].ProcessorMask);
				processor_core_count += bit_count ? bit_count : 1U;
			}
			else
			{
				++processor_core_count;
			}
		}
	}

	free(buffer);
	return processor_core_count;
}

static uint_fast32_t detect_processor_count(void)
{
	DWORD_PTR maskProcess, maskSystem;
	if (GetProcessAffinityMask(GetCurrentProcess(), &maskProcess, &maskSystem))
	{
		return count_set_bits(maskSystem);
	}
	return 0U;
}

uint_fast32_t get_processor_count(const bool logical_cores)
{
	uint_fast32_t count = detect_processor_count_ex(logical_cores);
	if (!count)
	{
		count = detect_processor_count();
	}
	return count ? count : 1U;
}
