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

#ifndef _INC_MPATCH_SUBSTRING_H
#define _INC_MPATCH_SUBSTRING_H

#include "utils.h"
#include "bit_io.h"
#include "pool.h"
#include <float.h>

#include <stdlib.h>

#define SUBSTR_BWD false
#define SUBSTR_FWD true

typedef struct
{
	uint_fast32_t length;
	uint_fast32_t offset_diff;
	bool offset_sign;
}
substring_t;

typedef struct
{
	uint_fast32_t prev_offset;
	const uint8_t *needle;
	uint_fast32_t needle_len;
	const uint8_t *haystack;
	uint_fast32_t haystack_len;
}
search_param_t;

typedef struct
{
	const search_param_t *search_param;
	struct
	{
		uint_fast32_t begin;
		uint_fast32_t end;
	}
	search_range;
	struct
	{
		substring_t data;
		uint64_t score;
	}
	result;
}
search_thread_t;

#define SUBSTRING_THRESHOLD 3U

static __forceinline uint64_t substring_score(const uint_fast32_t length, const uint_fast32_t offset_diff)
{
	const uint64_t offset_bits = exp_golomb_size(offset_diff);
	const uint64_t data_bits = (uint64_t)length << 3U;
	return (data_bits > offset_bits) ? (data_bits - offset_bits) : 0U;
}

static inline uintptr_t _find_optimal_substring(const uintptr_t data)
{
	search_thread_t *const param = (search_thread_t*)data;

	//Get search parameters
	const uint8_t *const haystack_ptr = param->search_param->haystack;
	const uint_fast32_t  haystack_len = param->search_param->haystack_len;
	const uint8_t *const needle_ptr   = param->search_param->needle;
	const uint_fast32_t  needle_len   = param->search_param->needle_len;
	const uint_fast32_t  range_begin  = param->search_range.begin;
	const uint_fast32_t  range_end    = param->search_range.end;
	const uint_fast32_t  prev_offset  = param->search_param->prev_offset;

	//Initialize result
	memset(&param->result.data, 0, sizeof(substring_t));
	param->result.score = 0U;

	//Sanity checking
	if ((haystack_len < 2U) || (needle_len < 2U))
	{
		return 0U;
	}

	//Setup search parameters
	const uint8_t *haystack_off = haystack_ptr + range_begin;
	uint_fast32_t remaining = range_end - range_begin;

	//Find the longest substring in haystack
	while (haystack_off = memchr(haystack_off, *needle_ptr, remaining))
	{
		const uint_fast32_t offset_curr = (uint_fast32_t)(haystack_off - haystack_ptr);
		const uint_fast32_t match_limit = min_uint32(needle_len, haystack_len - offset_curr);
		if ((match_limit > SUBSTRING_THRESHOLD) && (!memcmp(haystack_off, needle_ptr, SUBSTRING_THRESHOLD + 1U)))
		{
			uint_fast32_t matching_len;
			for (matching_len = SUBSTRING_THRESHOLD + 1U; matching_len < match_limit; matching_len++)
			{
				if (haystack_off[matching_len] != needle_ptr[matching_len])
				{
					break; /*end of matching sequence*/
				}
			}
			const uint_fast32_t offset_diff = diff_uint32(offset_curr, prev_offset);
			const uint64_t score = substring_score(matching_len, offset_diff);
			if (score > param->result.score)
			{
				param->result.data.length = matching_len;
				param->result.data.offset_diff = offset_diff;
				param->result.data.offset_sign = (offset_curr >= prev_offset) ? SUBSTR_FWD : SUBSTR_BWD;
				param->result.score = score;
			}
		}
		if (offset_curr < range_end)
		{
			++haystack_off;
			remaining = range_end - offset_curr;
		}
		else
		{
			break; /*end of range*/
		}
	}

	return 1U;
}

static inline uint64_t find_optimal_substring(substring_t *const substring, const uint_fast32_t prev_offset, thread_pool_t *const thread_pool, const uint8_t *const needle, const uint_fast32_t needle_len, const uint8_t *const haystack, const uint_fast32_t haystack_len)
{
	//Common search parameters
	const search_param_t search_param = { prev_offset, needle, needle_len, haystack, haystack_len };

	//Initialize result
	memset(substring, 0, sizeof(substring_t));

	//Set up per-thread parameters
	search_thread_t thread_param[MAX_THREAD_COUNT];
	memset(thread_param, 0, sizeof(thread_param));

	//Threads enabled?
	if ((!thread_pool) || (!thread_pool->thread_count) || (haystack_len <= 16384U))
	{
		thread_param[0U].search_param = &search_param;
		thread_param[0U].search_range.begin = 0U;
		thread_param[0U].search_range.end = haystack_len;
		_find_optimal_substring((uintptr_t)&thread_param[0U]);
		if (thread_param[0U].result.score)
		{
			memcpy(substring, &thread_param[0U].result.data, sizeof(substring_t));
			return thread_param[0U].result.score;
		}
		return 0U;
	}

	//Compute step size
	const uint_fast32_t step_size = (haystack_len / thread_pool->thread_count) + 1U;

	//Set up task parameters
	pool_task_t task_queue[MAX_THREAD_COUNT];
	uint_fast32_t range_offset = 0U;
	for (uint_fast32_t t = 0U; t < thread_pool->thread_count; ++t)
	{
		thread_param[t].search_param = &search_param;
		thread_param[t].search_range.begin = range_offset;
		thread_param[t].search_range.end = min_uint32(haystack_len, range_offset + step_size);
		range_offset = thread_param[t].search_range.end;
		task_queue[t].func = _find_optimal_substring;
		task_queue[t].data = (uintptr_t)(&thread_param[t]);
	}

	//Execute tasks
	mpatch_pool_exec(thread_pool, task_queue, thread_pool->thread_count);

	//Find the "optimal" thread result
	uint64_t best_score = 0U;
	for (uint_fast32_t t = 0U; t < thread_pool->thread_count; ++t)
	{
		if (thread_param[t].result.score > best_score)
		{
			memcpy(substring, &thread_param[t].result.data, sizeof(substring_t));
			best_score = thread_param[t].result.score;
		}
	}
	return best_score;
}

#endif /*_INC_MPATCH_SUBSTRING_H*/
