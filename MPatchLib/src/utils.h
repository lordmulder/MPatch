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

#ifndef _INC_MPATCH_UTILS_H
#define _INC_MPATCH_UTILS_H

#include <stdint.h>
#include <stdbool.h>

#define BOOLIFY(X) (!!(X))

static __forceinline uint_fast32_t min_uint32(const uint_fast32_t a, const uint_fast32_t b)
{
	return (a < b) ? a : b;
}

static __forceinline float min_flt(const float a, const float b)
{
	return (a < b) ? a : b;
}

static __forceinline uint_fast32_t diff_uint32(const uint_fast32_t a, const uint_fast32_t b)
{
	return (a > b) ? (a - b) : (b - a);
}

static __forceinline uint_fast32_t mean_uint32(const uint_fast32_t a, const uint_fast32_t b)
{
	return (a / 2U) + (b / 2U) + (a & b & 1U);
}

static __forceinline uint_fast32_t div2ceil_uint32(const uint_fast32_t val)
{
	return (val > 1U) ? (1U + ((val - 1U) >> 1U)) : 0U;
}

static __forceinline uint_fast32_t inc_bound_uint32(uint_fast32_t *const val, const uint_fast32_t max)
{
	return (*val < max) ? ++(*val) : max;
}

static inline void enc_uint32(uint8_t *const buffer, const uint32_t value)
{
	static const size_t SHIFT[4] = { 24U, 16U, 8U, 0U };
	for (size_t i = 0; i < 4U; ++i)
	{
		buffer[i] = (uint8_t)(value >> SHIFT[i]);
	}
}

static inline void dec_uint32(uint32_t *const value, const uint8_t *const buffer)
{
	static const size_t SHIFT[4] = { 24U, 16U, 8U, 0U };
	*value = 0U;
	for (size_t i = 0; i < 4U; ++i)
	{
		*value |= (buffer[i] << SHIFT[i]);
	}
}

#endif /*_INC_MPATCH_UTILS_H*/
