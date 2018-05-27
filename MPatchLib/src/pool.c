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

#include "pool.h"

#ifdef _MSC_VER
#define HAVE_STRUCT_TIMESPEC
#endif

#include <pthread.h>
#include <semaphore.h>

#include <malloc.h>

typedef struct
{
	pool_task_func_t func;
	uintptr_t data;
}
pool_task_t;

typedef struct
{
	pool_task_t *tasks;
	pthread_mutex_t mutex;
	sem_t sem_put;
	sem_t sem_get;
	pthread_cond_t cond_put;
	pthread_cond_t cond_get;
	uint_fast32_t size;
	uint_fast32_t pos_put;
	uint_fast32_t pos_get;
}
pool_queue_t;

typedef struct
{
	uint_fast32_t thread_count;
	pthread_t *threads;
	pool_queue_t task_queue;
}
pthread_pool_t;

/* ======================================================================= */
/* Task queue                                                              */
/* ======================================================================= */

static bool init_queue(pool_queue_t *const queue, const uint_fast32_t size)
{
	memset(&queue, 0U, sizeof(pool_queue_t));

	queue->tasks = (pool_task_t*)malloc(sizeof(pool_task_t) * (queue->size = size));
	if (!queue->tasks)
	{
		memset(&queue, 0U, sizeof(pool_queue_t));
		return false;
	}

	if (pthread_mutex_init(&queue->mutex, NULL))
	{
		free(queue->tasks);
		memset(&queue, 0U, sizeof(pool_queue_t));
		return false;
	}

	if (sem_init(&queue->sem_get, 0U, 0U) || sem_init(&queue->sem_put, 0U, size))
	{
		pthread_mutex_destroy(&queue->mutex);
		free(queue->tasks);
		memset(&queue, 0U, sizeof(pool_queue_t));
		return false;
	}

	if (pthread_cond_init(&queue->cond_get, NULL) || pthread_cond_init(&queue->cond_get, NULL))
	{
		pthread_mutex_destroy(&queue->mutex);
		pthread_cond_destroy(&queue->cond_get);
		pthread_cond_destroy(&queue->cond_put);
		free(queue->tasks);
		memset(&queue, 0U, sizeof(pool_queue_t));
		return false;
	}

	return true;
}

static bool destroy_queue(pool_queue_t *const queue)
{
	bool success = true;

	if (pthread_cond_destroy(&queue->cond_get) || pthread_cond_destroy(&queue->cond_get))
	{
		success = false;
	}

	if (sem_destroy(&queue->sem_get) || sem_destroy(&queue->sem_get))
	{
		success = false;
	}

	if (pthread_mutex_destroy(&queue->mutex))
	{
		success = false;
	}

	if (queue->tasks)
	{
		free(queue->tasks);
	}

	memset(&queue, 0U, sizeof(pool_queue_t));
	return success;
}

/* ======================================================================= */
/* Thread function                                                         */
/* ======================================================================= */

static void *thread_func(void *const args)
{
	return NULL;
}

/* ======================================================================= */
/* Pool functions                                                          */
/* ======================================================================= */

bool mpatch_pool_create(thread_pool_t *const pool, const uint32_t thread_count, const uint32_t queue_size)
{
	memset(&pool, 0U, sizeof(thread_pool_t));

	pthread_pool_t *const pool_data = malloc(sizeof(pthread_pool_t));
	if (!pool_data)
	{
		return false;
	}

	memset(&pool_data, 0U, sizeof(pthread_pool_t));

	pool_data->threads = (pthread_t*)malloc(sizeof(pthread_t) * thread_count);
	if (!pool_data->threads)
	{
		return false;
	}

	if (!init_queue(&pool_data->task_queue, queue_size))
	{
		free(pool_data->threads);
		free(pool_data);
		return false;
	}

	for (uint_fast32_t t = 0; t < thread_count; ++t)
	{
		if (!pthread_create(pool_data->threads + t, NULL, thread_func, &pool_data->task_queue))
		{
			pool_data->thread_count++;
		}
	}

	if (!pool_data->thread_count)
	{
		destroy_queue(&pool_data->task_queue);
		free(pool_data->threads);
		free(pool_data);
		return false;
	}

	pool->pool_data = (uintptr_t)pool_data;
	return true;
}
