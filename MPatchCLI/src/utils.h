/* ---------------------------------------------------------------------------------------------- */
/* MPatchLib - simple patch and compression library                                               */
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

#ifndef _INC_UTILS_H
#define _INC_UTILS_H

#include <stdint.h>

/* ======================================================================= */
/* Inline functions                                                        */
/* ======================================================================= */

static __forceinline uint_fast32_t max_uint32(const uint_fast32_t a, const uint_fast32_t b)
{
	return (a > b) ? a : b;
}

static __forceinline uint_fast32_t min_uint32(const uint_fast32_t a, const uint_fast32_t b)
{
	return (a < b) ? a : b;
}

static __forceinline uint64_t min_uint64(const uint64_t a, const uint64_t b)
{
	return (a < b) ? a : b;
}

static __forceinline float min_flt(const float a, const float b)
{
	return (a < b) ? a : b;
}

static __forceinline uint64_t diff_uint64(const uint64_t a, const uint64_t b)
{
	return (a > b) ? (a - b) : (b - a);
}

static __forceinline uint32_t log10_uint32(uint32_t value)
{
	uint32_t ret = 1U;
	while (value /= 10U)
	{
		ret++;
	}
	return ret;
}

/* ======================================================================= */
/* Type definitions                                                        */
/* ======================================================================= */

#define GAUSS_FILTER_SIZE 32U
#define MEDIAN_FILTER_SIZE 5U

typedef struct
{
	double window[GAUSS_FILTER_SIZE];
	double median[2U][MEDIAN_FILTER_SIZE];
	size_t pos[2U];
}
gauss_t;

/* ======================================================================= */
/* Function declarations                                                   */
/* ======================================================================= */

const wchar_t *env_get_string(const wchar_t *const name);
uint_fast32_t env_get_uint32(const wchar_t *const name, const uint_fast32_t max_value, const uint_fast32_t default_value);

void gauss_init(gauss_t *const ctx);
double gauss_update(gauss_t *const ctx, const double value);

const wchar_t *basename(const wchar_t *const path);

#endif /*_INC_UTILS_H*/
