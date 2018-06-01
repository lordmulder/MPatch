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

#include "utils.h"

#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include <wchar.h>

/* ======================================================================= */
/* File system                                                             */
/* ======================================================================= */

const wchar_t *basename(const wchar_t *const path)
{
	const wchar_t *basename = path, *temp = path;
	for (size_t i = 0; i < 3; ++i)
	{
		static const wchar_t PATH_SEP[3] = { L':', L'/', L'\\' };
		if (temp = wcsrchr(path, PATH_SEP[i]))
		{
			basename = temp + 1U;
		}
	}
	return *basename ? basename : path;
}

/* ======================================================================= */
/* Environment                                                             */
/* ======================================================================= */

static bool is_hex_string(const wchar_t *str)
{
	while (iswspace(*str))
	{
		str++;
	}
	if ((*str == L'+') || (*str == L'-'))
	{
		str++;
	}
	if (*str == L'0')
	{
		return (str[1U] == L'x') || (str[1U] == L'X');
	}
	return false;
}

const wchar_t *env_get_string(const wchar_t *const name)
{
	wchar_t *value = NULL;
	size_t size = 0U;
	if (!_wdupenv_s(&value, &size, name))
	{
		return value;
	}
	return NULL;
}

uint_fast32_t env_get_uint32(const wchar_t *const name, const uint_fast32_t max_value, const uint_fast32_t default_value)
{
	const wchar_t *const string = env_get_string(name);
	if (string)
	{
		long long int temp;
		if (swscanf_s(string, is_hex_string(string) ? L"%llx" : L"%llu", &temp) < 1)
		{
			_set_errno(EINVAL);
			temp = 0U;
		}
		if ((temp < 0) || (temp >(long long int)max_value))
		{
			_set_errno(ERANGE);
			temp = 0U;
		}
		free((void*)string);
		return (uint_fast32_t)temp;
	}
	else
	{
		return min_uint32(max_value, default_value);
	}
}

/* ======================================================================= */
/* Gaussian filter                                                         */
/* ======================================================================= */

#define GAUSS_BOUND_IDX(X) \
	do { if ((X) >= GAUSS_FILTER_SIZE) { X = 0U; } } while(0)

void gauss_init(gauss_t *const ctx)
{
	memset(ctx, 0, sizeof(gauss_t));
	ctx->pos = SIZE_MAX;
}

double gauss_update(gauss_t *const ctx, const double value)
{
	static const double WEIGHTS[GAUSS_FILTER_SIZE] =
	{
		0.000940, 0.001239, 0.001619, 0.002097, 0.002691, 0.003422, 0.004312, 0.005385,
		0.006665, 0.008173, 0.009933, 0.011963, 0.014278, 0.016886, 0.019791, 0.022986,
		0.026456, 0.030175, 0.034105, 0.038200, 0.042400, 0.046636, 0.050833, 0.054907,
		0.058772, 0.062342, 0.065530, 0.068260, 0.070462, 0.072078, 0.073066, 0.073398
	};

	if (ctx->pos == SIZE_MAX)
	{
		for (size_t i = 0; i < GAUSS_FILTER_SIZE; ++i)
		{
			ctx->window[i] = value;
		}
		ctx->pos = 0U;
	}

	ctx->window[ctx->pos++] = value;
	GAUSS_BOUND_IDX(ctx->pos);

	double result = 0.0;
	for (size_t i = 0, k = ctx->pos; i < GAUSS_FILTER_SIZE; ++i)
	{
		result += ctx->window[k++] * WEIGHTS[i];
		GAUSS_BOUND_IDX(k);
	}

	return result;
}

#undef GAUSS_BOUND_IDX
