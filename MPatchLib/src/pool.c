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
	pool_task_t tasks[MAX_THREAD_COUNT];
	pthread_mutex_t mutex;
	sem_t sem_put;
	sem_t sem_get;
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
	pthread_t threads[MAX_THREAD_COUNT];
	pool_state_t pool_state;
}
pthread_pool_t;

/* ======================================================================= */
/* Task queue                                                              */
/* ======================================================================= */

static bool init_queue(pool_queue_t *const queue)
{
	memset(queue, 0U, sizeof(pool_queue_t));

	if (pthread_mutex_init(&queue->mutex, NULL))
	{
		free(queue->tasks);
		memset(&queue, 0U, sizeof(pool_queue_t));
		return false;
	}

	if (sem_init(&queue->sem_get, 0U, 0U) || sem_init(&queue->sem_put, 0U, MAX_THREAD_COUNT))
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
	queue->pos_put = (queue->pos_put + 1U) % MAX_THREAD_COUNT;

	if (pthread_mutex_unlock(&queue->mutex))
	{
		abort();
	}

	if (sem_post(&queue->sem_get))
	{
		abort();
	}
}

#include <stdio.h>

static inline void put_tasks(pool_queue_t *const queue, const pool_task_t *const task, const uint32_t count)
{
	uint32_t done = 0U;

	while (done < count)
	{
		if (sem_wait(&queue->sem_put))
		{
			abort();
		}

		if (pthread_mutex_lock(&queue->mutex))
		{
			abort();
		}

		uint32_t batch_size = 1U;

		while (done + batch_size < count)
		{
			const int ret = sem_trywait(&queue->sem_put);
			if (ret)
			{
				if (ret != EAGAIN)
				{
					abort();
				}
				break;
			}
			else
			{
				++batch_size;
			}
		}

		if (queue->pos_put + batch_size > MAX_THREAD_COUNT)
		{
			const uint32_t copy_len = MAX_THREAD_COUNT - queue->pos_put;
			memcpy(queue->tasks + queue->pos_put, task + done, sizeof(pool_task_t) * copy_len);
			memcpy(queue->tasks, task + done + copy_len, sizeof(pool_task_t) * (batch_size - copy_len));
		}
		else
		{
			memcpy(queue->tasks + queue->pos_put, task + done, sizeof(pool_task_t) * batch_size);
		}

		queue->pos_put = (queue->pos_put + batch_size) % MAX_THREAD_COUNT;
		done += batch_size;

		if (pthread_mutex_unlock(&queue->mutex))
		{
			abort();
		}

		if (sem_post_multiple(&queue->sem_get, (int)batch_size))
		{
			abort();
		}
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
	queue->pos_get = (queue->pos_get + 1U) % MAX_THREAD_COUNT;

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

		if (task.func)
		{
			task.func(task.data);
		}
		else
		{
			break; /*terminate*/
		}

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

	return NULL;
}

/* ======================================================================= */
/* Pool functions                                                          */
/* ======================================================================= */

bool mpatch_pool_create(thread_pool_t *const pool, const uint32_t thread_count)
{
	memset(pool, 0U, sizeof(thread_pool_t));

	if (thread_count > MAX_THREAD_COUNT)
	{
		return false;
	}

	pthread_pool_t *const pool_data = malloc(sizeof(pthread_pool_t));
	if (!pool_data)
	{
		return false;
	}

	memset(pool_data, 0U, sizeof(pthread_pool_t));

	if (pthread_mutex_init(&pool_data->pool_state.mutex, NULL))
	{
		free(pool_data);
		return false;
	}

	if (pthread_cond_init(&pool_data->pool_state.cond_ready, NULL))
	{
		pthread_mutex_destroy(&pool_data->pool_state.mutex);
		free(pool_data);
		return false;
	}

	if (!init_queue(&pool_data->pool_state.task_queue))
	{
		pthread_cond_destroy(&pool_data->pool_state.cond_ready);
		pthread_mutex_destroy(&pool_data->pool_state.mutex);
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
		free(pool_data);
		return false;
	}

	pool->thread_count = thread_count;
	pool->pool_data = (uintptr_t)pool_data;
	return true;
}

bool mpatch_pool_destroy(thread_pool_t *const pool)
{
	bool success = true;
	pthread_pool_t *const pool_data = (pthread_pool_t*)pool->pool_data;

	for (uint_fast32_t t = 0; t < pool_data->thread_count; ++t)
	{
		const pool_task_t  stop_task = { NULL, 0U };
		put_task(&pool_data->pool_state.task_queue, &stop_task);
	}

	for (uint_fast32_t t = 0; t < pool_data->thread_count; ++t)
	{
		if (pthread_join(*(pool_data->threads + t), NULL))
		{
			success = false;
		}
	}

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

	memset(pool_data, 0U, sizeof(pthread_pool_t));
	free(pool_data);

	memset(pool, 0U, sizeof(thread_pool_t));
	return success;
}

void mpatch_pool_put(thread_pool_t *const pool, const pool_task_func_t func, const uintptr_t data)
{
	if (!func)
	{
		return; /*discard*/
	}
	
	pthread_pool_t *const pool_data = (pthread_pool_t*)pool->pool_data;

	if (pthread_mutex_lock(&pool_data->pool_state.mutex))
	{
		abort();
	}

	const pool_task_t task = { func, data };
	pool_data->pool_state.pending++;
	put_task(&pool_data->pool_state.task_queue, &task);

	if (pthread_mutex_unlock(&pool_data->pool_state.mutex))
	{
		abort();
	}
}

void mpatch_pool_put_multiple(thread_pool_t *const pool, const pool_task_t *const task, const uint32_t count)
{
	for (uint32_t i = 0U; i < count; ++i)
	{
		if (!task[i].func)
		{
			return; /*discard*/
		}
	}

	pthread_pool_t *const pool_data = (pthread_pool_t*)pool->pool_data;

	if (pthread_mutex_lock(&pool_data->pool_state.mutex))
	{
		abort();
	}

	pool_data->pool_state.pending += count;
	put_tasks(&pool_data->pool_state.task_queue, task, count);

	if (pthread_mutex_unlock(&pool_data->pool_state.mutex))
	{
		abort();
	}
}

void mpatch_pool_await(thread_pool_t *const pool)
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
