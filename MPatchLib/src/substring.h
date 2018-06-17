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
		substring_t substring;
		float cost;
	}
	result;
}
search_thread_t;

static const uint_fast32_t SUBSTRING_THRESHOLD = 3U;

static __forceinline float substring_cost(const uint_fast32_t substr_len, const uint_fast32_t substr_off_diff)
{
	if (substr_len > SUBSTRING_THRESHOLD)
	{
		const uint_fast32_t total_bits = exp_golomb_size(substr_len - SUBSTRING_THRESHOLD) + exp_golomb_size(substr_off_diff) + (substr_off_diff ? 1U : 0U);
		return (float)total_bits / (substr_len * 8U);
	}
	return FLT_MAX;
}

static inline uintptr_t find_optimal_substring_thread(const uintptr_t data)
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
	param->result.substring.length = 0U;
	param->result.substring.offset_diff = UINT_FAST32_MAX;
	param->result.substring.offset_sign = SUBSTR_FWD;
	param->result.cost = FLT_MAX;

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
		if ((match_limit > SUBSTRING_THRESHOLD) && (!memcmp(haystack_off, needle_ptr, SUBSTRING_THRESHOLD)))
		{
			uint_fast32_t matching_len;
			for (matching_len = SUBSTRING_THRESHOLD; matching_len < match_limit; matching_len++)
			{
				if (haystack_off[matching_len] != needle_ptr[matching_len])
				{
					break; /*end of matching sequence*/
				}
			}
			if (matching_len > SUBSTRING_THRESHOLD)
			{
				const uint_fast32_t offset_diff = diff_uint32(offset_curr, prev_offset);
				if ((matching_len > param->result.substring.length) || (offset_diff < param->result.substring.offset_diff))
				{
					const float current_cost = substring_cost(matching_len, offset_diff);
					if (current_cost < param->result.cost)
					{
						param->result.substring.length = matching_len;
						param->result.substring.offset_diff = offset_diff;
						param->result.substring.offset_sign = (offset_curr >= prev_offset) ? SUBSTR_FWD : SUBSTR_BWD;
						param->result.cost = current_cost;
					}
				}
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

static inline bool find_optimal_substring(substring_t *const substring, const uint_fast32_t literal_len, const uint_fast32_t prev_offset, thread_pool_t *const thread_pool, const uint8_t *const needle, const uint_fast32_t needle_len, const uint8_t *const haystack, const uint_fast32_t haystack_len)
{
	//Common search parameters
	const search_param_t search_param = { prev_offset, needle, needle_len, haystack, haystack_len };

	//Set up per-thread parameters
	search_thread_t thread_param[MAX_THREAD_COUNT];
	memset(thread_param, 0, sizeof(thread_param));

	//Threads enabled?
	if ((!thread_pool) || (!thread_pool->thread_count) || (haystack_len <= 16384U))
	{
		thread_param[0U].search_param = &search_param;
		thread_param[0U].search_range.begin = 0U;
		thread_param[0U].search_range.end = haystack_len;
		find_optimal_substring_thread((uintptr_t)&thread_param[0U]);
		memcpy(substring, &thread_param[0U].result.substring, sizeof(substring_t));
		return thread_param[0U].result.cost;
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
		task_queue[t].func = find_optimal_substring_thread;
		task_queue[t].data = (uintptr_t)(&thread_param[t]);
	}

	//Execute tasks
	mpatch_pool_exec(thread_pool, task_queue, thread_pool->thread_count);

	//Find the "optimal" result
	uint_fast32_t thread_id = UINT_FAST32_MAX;
	float lowest_cost = FLT_MAX;
	for (uint_fast32_t t = 0U; t < thread_pool->thread_count; ++t)
	{
		if (thread_param[t].result.cost < lowest_cost)
		{
			thread_id = t;
			lowest_cost = thread_param[t].result.cost;
		}
	}

	//Return "optimal" result
	if (thread_id != UINT_FAST32_MAX)
	{
		memcpy(substring, &thread_param[thread_id].result.substring, sizeof(substring_t));
		return true;
	}

	memset(substring, 0U, sizeof(substring_t));
	return false;
}

#endif /*_INC_MPATCH_SUBSTRING_H*/
