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

/* ======================================================================= */
/* Types                                                                   */
/* ======================================================================= */

typedef struct
{
	thread_pool_t pool;
	pthread_t threads[MAX_THREAD_COUNT];
	pthread_mutex_t mutex;
	pthread_cond_t cond_complete;
	sem_t sem_task;
	const pool_task_t *next_task;
	uint_fast32_t pending_tasks;
	bool shutdown_flag;
}
pool_private_t;

/* ======================================================================= */
/* Helper macros                                                           */
/* ======================================================================= */

#define ENTER_CRITICAL_SECTION() do \
{ \
	if (pthread_mutex_lock(&p->mutex)) \
	{ \
		abort(); \
	} \
} \
while (0)

#define LEAVE_CRITICAL_SECTION() do \
{ \
	if (pthread_mutex_unlock(&p->mutex)) \
	{ \
		abort(); \
	} \
} \
while (0)

#define AWAIT_MY_PENDING_TASKS() do \
{ \
	while (p->pending_tasks) \
	{ \
		if (pthread_cond_wait(&p->cond_complete, &p->mutex)) \
		{ \
			abort(); \
		} \
	} \
} \
while(0)

static const pool_task_t STOP_TASKS[MAX_THREAD_COUNT];

/* ======================================================================= */
/* Thread function                                                         */
/* ======================================================================= */

static void *thread_func(void *const args)
{
	pool_private_t *const p = (pool_private_t*)args;
	if (!p)
	{
		return NULL;
	}

	const pool_task_t *task = NULL;
	bool shutting_down = false, queue_drained = false;

	for (;;)
	{
		if (sem_wait(&p->sem_task))
		{
			abort();
		}

		ENTER_CRITICAL_SECTION();
		task = p->next_task++;
		shutting_down = p->shutdown_flag;
		LEAVE_CRITICAL_SECTION();

		if (shutting_down)
		{
			return p; /*shuttong down!*/
		}

		if (task->func)
		{
			task->func(task->data);
		}

		ENTER_CRITICAL_SECTION();
		queue_drained = (!(--p->pending_tasks));
		LEAVE_CRITICAL_SECTION();

		if (queue_drained)
		{
			if (pthread_cond_broadcast(&p->cond_complete))
			{
				abort();
			}
		}
	}
}

/* ======================================================================= */
/* Pool functions                                                          */
/* ======================================================================= */

bool mpatch_pool_create(thread_pool_t **const pool, const uint32_t thread_count)
{
	if ((!pool) || (thread_count > MAX_THREAD_COUNT))
	{
		return false;
	}

	*pool = NULL;

	pool_private_t *const p = (pool_private_t*) calloc(1U, sizeof(pool_private_t));
	if (!p)
	{
		return false;
	}

	if (pthread_mutex_init(&p->mutex, NULL))
	{
		free(p);
		return false;
	}

	if (pthread_cond_init(&p->cond_complete, NULL))
	{
		pthread_mutex_destroy(&p->mutex);
		free(p);
		return false;
	}

	if (sem_init(&p->sem_task, 0, 0))
	{
		pthread_cond_destroy(&p->cond_complete);
		pthread_mutex_destroy(&p->mutex);
		free(p);
		return false;
	}
	
	for (uint_fast32_t t = 0; t < thread_count; ++t)
	{
		if (!pthread_create(p->threads + t, NULL, thread_func, p))
		{
			p->pool.thread_count++;
		}
	}

	if (!p->pool.thread_count)
	{
		sem_destroy(&p->sem_task);
		pthread_cond_destroy(&p->cond_complete);
		pthread_mutex_destroy(&p->mutex);
		free(p);
		return false;
	}

	*pool = (thread_pool_t*)p;
	return true;
}

bool mpatch_pool_destroy(thread_pool_t **const pool)
{
	if (!pool)
	{
		return false;
	}

	pool_private_t *const p = (pool_private_t*)(*pool);
	*pool = NULL;

	ENTER_CRITICAL_SECTION();
	AWAIT_MY_PENDING_TASKS();

	p->next_task = STOP_TASKS;
	p->pending_tasks += MAX_THREAD_COUNT;
	p->shutdown_flag = true;

	LEAVE_CRITICAL_SECTION();

	if (sem_post_multiple(&p->sem_task, MAX_THREAD_COUNT))
	{
		abort();
	}

	bool success = true;
	for (uint_fast32_t t = 0; t < MAX_THREAD_COUNT; ++t)
	{
		pthread_t *const thread = p->threads + t;
		if (thread->p)
		{
			if (pthread_join(*thread, NULL))
			{
				success = false;
			}
		}
	}

	if (sem_destroy(&p->sem_task))
	{
		success = false;
	}

	if (pthread_cond_destroy(&p->cond_complete))
	{
		success = false;
	}

	if (pthread_mutex_destroy(&p->mutex))
	{
		success = false;
	}

	memset(p, 0U, sizeof(pool_private_t));
	free(p);

	return success;
}

void mpatch_pool_exec(thread_pool_t *const pool, const pool_task_t *const tasks, const uint32_t count)
{
	pool_private_t *const p = (pool_private_t*)pool;

	if ((!p) || (!tasks) || (count > INT_MAX))
	{
		abort();
	}

	ENTER_CRITICAL_SECTION();
	AWAIT_MY_PENDING_TASKS();

	p->next_task = tasks;
	p->pending_tasks += count;

	if (sem_post_multiple(&p->sem_task, count))
	{
		abort();
	}

	AWAIT_MY_PENDING_TASKS();
	LEAVE_CRITICAL_SECTION();
}
