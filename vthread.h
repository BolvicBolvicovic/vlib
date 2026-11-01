#ifndef VTHREAD_H
#define VTHREAD_H

#include "c_types.h"
#include "arena.h"
#include <bits/pthreadtypes.h>
#include <pthread.h>
#include <sched.h>
#include <errno.h> // IWYU pragma: keep
#include <unistd.h>
#include <stdbool.h>

/** Usage!
 * #include "vlib/vthread.h"
 * #include <stdio.h>
 * 
 * TASK_DECL(my_task, {int a; float b;})
 * {
 * 	int   a   = _args->a;
 * 	float b   = _args->b;
 * 
 * 	printf("I found %d and %f\n", a, b);
 * 	return 0;
 * }
 * 
 * int main(void)
 * {
 * 	pthread_t t = TASK(my_task, { 5, 5.6 });
 * 
 * 	pthread_join(t, 0);
 * 	return 0;
 * }
 *
 */

typedef struct VTaskArg VTaskArg;
typedef struct VPool    VPool;
typedef void* (*VJobFn)(void*);
typedef struct VJob     VJob;

struct VTaskArg
{
	void *args;
	s32   handshake;
};

/**
 * Fire and forget task declaration.
 */
#define TASK_DECL(name, ...)                                                     \
	struct name##_args {__VA_ARGS__};                                        \
	void  *name(struct name##_args);                                         \
	void  *name##_as_task(void *args)                                        \
	{                                                                        \
		VTaskArg          *vth = args;                                   \
		struct name##_args   a   = *(struct name##_args*)vth->args;      \
		__atomic_store_n(&vth->handshake, 1, __ATOMIC_RELEASE);          \
		return name(a);                                                  \
	}                                                                        \
	pthread_t name##_run_task(struct name##_args *args, pthread_attr_t *attr)\
	{                                                                        \
		pthread_t p;                                                     \
		VTaskArg  vth = { args, 0 };                                     \
		s32       res;                                                   \
		while ((res = pthread_create(&p, attr, name##_as_task, &vth)) == EAGAIN);\
		if (res) return (pthread_t){ 0 };                                \
		pthread_detach(p);                                               \
		while (!__atomic_load_n(&vth.handshake, __ATOMIC_ACQUIRE))       \
			sched_yield();                                           \
		return p;                                                        \
	}                                                                        \
	void* name(__attribute__((unused)) struct name##_args _args)

/**
 * Fire and forget task runner with pthread_attr_t.
 */
#define TASK_WITH_ATTR(name, attr, ...) name##_run_task(&(struct name##_args){__VA_ARGS__}, attr)
/**
 * Fire and forget task runner.
 */
#define TASK(name, ...)                 TASK_WITH_ATTR(name, 0, __VA_ARGS__)

/* TODO: Pool system where we can submit tasks to a pool of threads */

struct VPool
{
	arena_t        *arena;

	pthread_cond_t  wake;
	pthread_mutex_t lock;
	_Bool           quit;

	pthread_t      *workers;
	u32		nworkers;

	VJob           *jobs;
	u32             jcap, jhead, njobs;
};

struct VJob
{
	VJobFn fn;
	void*  arg;
};

struct VPoolNewArg
{
	u32 nworkers;
	u32 jobs_capacity;
};

VPool*  vpool_new(arena_t *arena, struct VPoolNewArg args);
#define VPOOL_NEW(arena, ...) vpool_new(arena, (struct VPoolNewArg){ .nworkers=sysconf(_SC_NPROCESSORS_ONLN)-1, .jobs_capacity=1024, __VA_ARGS__ })
void	vpool_submit(VPool *pool, VJob job);
#define VPOOL_SUBMIT(pool, name, ...) do {                                  \
	VTaskArg _arg = (VTaskArg){ &(struct name##_args){__VA_ARGS__}, 0 };\
	vpool_submit(pool, (VJob){.fn=name##_as_task, .arg=&_arg});         \
	while (!__atomic_load_n(&_arg.handshake, __ATOMIC_ACQUIRE))         \
		sched_yield();                                              \
} while (0)
void	vpool_destroy(VPool *pool);

#ifdef VPOOL_IMPL
#include <string.h>

static void*
vpool_worker(void* _pool)
{
	VPool* pool = _pool;

	while (1)
	{
		VJob job;
		
		pthread_mutex_lock(&pool->lock);
		
		while (pool->njobs == 0 && pool->quit == false)
		{
			pthread_cond_wait(&pool->wake, &pool->lock);
		}

		if (pool->quit)
		{
			pthread_mutex_unlock(&pool->lock);
			return 0;
		}
		
		job = pool->jobs[pool->jhead];
		pool->jhead = (pool->jhead + 1) % pool->jcap;
		pool->njobs--;

		pthread_mutex_unlock(&pool->lock);
		
		job.fn(job.arg);
	}
}

VPool*
vpool_new(arena_t *arena, struct VPoolNewArg args)
{
	VPool* pool = ARENA_PUSH_STRUCT(arena, VPool);

	pthread_mutex_init(&pool->lock, 0);
	pthread_cond_init(&pool->wake, 0);

	pool->arena    = arena;
	pool->jcap     = args.jobs_capacity;
	pool->nworkers = 0;
	pool->njobs    = 0;
	pool->jhead    = 0;
	pool->jobs     = ARENA_PUSH_ARRAY(arena, VJob, args.jobs_capacity);
	pool->workers  = ARENA_PUSH_ARRAY(arena, pthread_t, args.nworkers);

	for (u32 i = 0; i < args.nworkers; i++)
	{
		if (pthread_create(&pool->workers[i], 0, vpool_worker, pool)) continue;
		pool->nworkers++;
	}

	return pool;
}

void
vpool_submit(VPool *pool, VJob job)
{
	pthread_mutex_lock(&pool->lock);

	if (pool->njobs == pool->jcap)
	{
		VJob* growth = ARENA_PUSH_ARRAY(pool->arena, VJob, pool->jcap * 2);
		u32   first  = pool->jcap - pool->jhead;

		memcpy(growth,         pool->jobs + pool->jhead, sizeof(VJob)*first);
		memcpy(growth + first, pool->jobs,               sizeof(VJob)*pool->jhead);

		pool->jobs  = growth;
		pool->jhead = pool->jcap;
		pool->jcap *= 2;
	}

	pool->jobs[(pool->jhead + pool->njobs) % pool->jcap] = job;
	pool->njobs++;

	pthread_mutex_unlock(&pool->lock);
	pthread_cond_signal(&pool->wake);
}

void
vpool_destroy(VPool *pool)
{
	pthread_mutex_lock(&pool->lock);
	pool->quit = true;
	pthread_mutex_unlock(&pool->lock);
	pthread_cond_broadcast(&pool->wake);
	for (u32 i = 0; i < pool->nworkers; i++)
	{
		pthread_join(pool->workers[i], 0);
	}
}

#endif // VPOOL_IMPL

#endif // VTHREAD_H
