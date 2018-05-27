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

#include <stdlib.h>
#include <malloc.h>
#include <memory.h>

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
	uint_fast32_t size;
	uint_fast32_t pos_put;
	uint_fast32_t pos_get;
}
pool_queue_t;

typedef struct
{
	uint_fast32_t pending;
	pthread_mutex_t mutex;
	pthread_cond_t cond_ready;
	pool_queue_t task_queue;
}
pool_state_t;

typedef struct
{
	uint_fast32_t thread_count;
	pthread_t *threads;
	pool_state_t pool_state;
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

	return true;
}

static bool destroy_queue(pool_queue_t *const queue)
{
	bool success = true;

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

static inline void put_task(pool_queue_t *const queue, const pool_task_t *const task)
{
	if (sem_wait(&queue->sem_put))
	{
		abort();
	}

	if (pthread_mutex_lock(&queue->mutex))
	{
		abort();
	}
	
	memcpy(queue->tasks + queue->pos_put, task, sizeof(pool_task_t));
	queue->pos_put = (queue->pos_put + 1U) % queue->size;

	if (pthread_mutex_unlock(&queue->mutex))
	{
		abort();
	}

	if (sem_post(&queue->sem_get))
	{
		abort();
	}
}

static inline void get_task(pool_queue_t *const queue, pool_task_t *const task)
{
	if (sem_wait(&queue->sem_get))
	{
		abort();
	}

	if (pthread_mutex_lock(&queue->mutex))
	{
		abort();
	}

	memcpy(task, queue->tasks + queue->pos_get, sizeof(pool_task_t));
	queue->pos_get = (queue->pos_get + 1U) % queue->size;

	if (pthread_mutex_unlock(&queue->mutex))
	{
		abort();
	}

	if (sem_post(&queue->sem_put))
	{
		abort();
	}
}

/* ======================================================================= */
/* Thread function                                                         */
/* ======================================================================= */

static void *thread_func(void *const args)
{
	pool_state_t *const state = (pool_state_t*)args;
	for (;;)
	{
		pool_task_t task;
		get_task(&state->task_queue, &task);
		task.func(task.data);

		if (pthread_mutex_lock(&state->mutex))
		{
			abort();
		}
		
		if (!(--state->pending))
		{
			if (pthread_cond_broadcast(&state->cond_ready))
			{
				abort();
			}
		}

		if (pthread_mutex_unlock(&state->mutex))
		{
			abort();
		}
	}
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

	if (pthread_mutex_init(&pool_data->pool_state.mutex, NULL))
	{
		free(pool_data->threads);
		free(pool_data);
		return false;
	}

	if (pthread_cond_init(&pool_data->pool_state.cond_ready, NULL))
	{
		pthread_mutex_destroy(&pool_data->pool_state.mutex);
		free(pool_data->threads);
		free(pool_data);
		return false;
	}

	if (!init_queue(&pool_data->pool_state.task_queue , queue_size))
	{
		pthread_cond_destroy(&pool_data->pool_state.cond_ready);
		pthread_mutex_destroy(&pool_data->pool_state.mutex);
		free(pool_data->threads);
		free(pool_data);
		return false;
	}
	
	for (uint_fast32_t t = 0; t < thread_count; ++t)
	{
		if (!pthread_create(pool_data->threads + t, NULL, thread_func, &pool_data->pool_state))
		{
			pool_data->thread_count++;
		}
	}

	if (!pool_data->thread_count)
	{
		destroy_queue(&pool_data->pool_state.task_queue);
		pthread_cond_destroy(&pool_data->pool_state.cond_ready);
		pthread_mutex_destroy(&pool_data->pool_state.mutex);
		free(pool_data->threads);
		free(pool_data);
		return false;
	}

	pool->pool_data = (uintptr_t)pool_data;
	return true;
}

bool mpatch_pool_destroy(thread_pool_t *const pool)
{
	bool success = true;
	pthread_pool_t *const pool_data = (pthread_pool_t*)pool->pool_data;

	if (!destroy_queue(&pool_data->pool_state.task_queue))
	{
		success = false;
	}

	if (pthread_cond_destroy(&pool_data->pool_state.cond_ready))
	{
		success = false;
	}

	if (pthread_mutex_destroy(&pool_data->pool_state.mutex))
	{
		success = false;
	}

	memset(&pool_data, 0U, sizeof(pthread_pool_t));

	free(pool_data->threads);
	free(pool_data);

	memset(&pool, 0U, sizeof(thread_pool_t));
	return success;
}

void mpatch_pool_put(thread_pool_t *const pool, const pool_task_func_t func, const uintptr_t data)
{
	pthread_pool_t *const pool_data = (pthread_pool_t*)pool->pool_data;

	if (pthread_mutex_lock(&pool_data->pool_state.mutex))
	{
		abort();
	}

	const pool_task_t task = { func, data };
	put_task(&pool_data->pool_state.task_queue, &task);
	pool_data->pool_state.pending++;

	if (pthread_mutex_unlock(&pool_data->pool_state.mutex))
	{
		abort();
	}
}

void mpatch_pool_wait(thread_pool_t *const pool)
{
	pthread_pool_t *const pool_data = (pthread_pool_t*)pool->pool_data;

	if (pthread_mutex_lock(&pool_data->pool_state.mutex))
	{
		abort();
	}

	while (pool_data->pool_state.pending)
	{
		if (pthread_cond_wait(&pool_data->pool_state.cond_ready, &pool_data->pool_state.mutex))
		{
			abort();
		}
	}

	if (pthread_mutex_unlock(&pool_data->pool_state.mutex))
	{
		abort();
	}
}
