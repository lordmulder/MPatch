/* ---------------------------------------------------------------------------------------------- */
/* MPatchLib - patch and compression library                                                      */
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

#ifndef _INC_POOL_H
#define _INC_POOL_H 

#include <stdint.h>
#include <stdbool.h>

#define MAX_THREAD_COUNT 16U

typedef void (*pool_task_func_t)(const uintptr_t user_data);

typedef struct
{
	uint32_t thread_count;
	uintptr_t pool_data;
}
thread_pool_t;

typedef struct
{
	pool_task_func_t func;
	uintptr_t data;
}
pool_task_t;

bool mpatch_pool_create(thread_pool_t *const pool, const uint32_t thread_count);
void mpatch_pool_put(thread_pool_t *const pool, const pool_task_func_t func, const uintptr_t data);
void mpatch_pool_put_multiple(thread_pool_t *const pool, const pool_task_t *const task, const uint32_t count);
void mpatch_pool_await(thread_pool_t *const pool);
bool mpatch_pool_destroy(thread_pool_t *const pool);

#endif