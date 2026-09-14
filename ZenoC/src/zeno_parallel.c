#ifndef _WIN32
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#endif

#include "zeno_internal.h"

#include <stdlib.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#endif

/* Recursive mutex with lazy initialization. A zero-initialized ZenoMutex is
 * usable directly: the first lock allocates the platform primitive under a
 * static guard, so structs created with calloc need no explicit setup. */

#ifdef _WIN32
typedef struct ZenoMutexImpl { CRITICAL_SECTION critical; } ZenoMutexImpl;
static SRWLOCK mutex_init_guard = SRWLOCK_INIT;
static void mutex_init_enter(void) { AcquireSRWLockExclusive(&mutex_init_guard); }
static void mutex_init_leave(void) { ReleaseSRWLockExclusive(&mutex_init_guard); }
#else
typedef struct ZenoMutexImpl { pthread_mutex_t handle; } ZenoMutexImpl;
static pthread_mutex_t mutex_init_guard = PTHREAD_MUTEX_INITIALIZER;
static void mutex_init_enter(void) { (void)pthread_mutex_lock(&mutex_init_guard); }
static void mutex_init_leave(void) { (void)pthread_mutex_unlock(&mutex_init_guard); }
#endif

static ZenoMutexImpl *mutex_create(void) {
    ZenoMutexImpl *impl = (ZenoMutexImpl *)malloc(sizeof(*impl));
    if (impl == NULL) return NULL;
#ifdef _WIN32
    InitializeCriticalSection(&impl->critical);
#else
    pthread_mutexattr_t attributes;
    (void)pthread_mutexattr_init(&attributes);
    (void)pthread_mutexattr_settype(&attributes, PTHREAD_MUTEX_RECURSIVE);
    if (pthread_mutex_init(&impl->handle, &attributes) != 0) {
        (void)pthread_mutexattr_destroy(&attributes);
        free(impl);
        return NULL;
    }
    (void)pthread_mutexattr_destroy(&attributes);
#endif
    return impl;
}

static void mutex_destroy(ZenoMutexImpl *impl) {
    if (impl == NULL) return;
#ifdef _WIN32
    DeleteCriticalSection(&impl->critical);
#else
    (void)pthread_mutex_destroy(&impl->handle);
#endif
    free(impl);
}

static ZenoMutexImpl *mutex_ensure(ZenoMutex *mutex) {
    if (mutex == NULL) return NULL;
    if (mutex->handle != NULL) return (ZenoMutexImpl *)mutex->handle;
    mutex_init_enter();
    if (mutex->handle == NULL) mutex->handle = mutex_create();
    mutex_init_leave();
    return (ZenoMutexImpl *)mutex->handle;
}

void zeno_mutex_lock(ZenoMutex *mutex) {
    ZenoMutexImpl *impl = mutex_ensure(mutex);
    if (impl == NULL) return;
#ifdef _WIN32
    EnterCriticalSection(&impl->critical);
#else
    (void)pthread_mutex_lock(&impl->handle);
#endif
}

void zeno_mutex_unlock(ZenoMutex *mutex) {
    if (mutex == NULL || mutex->handle == NULL) return;
    ZenoMutexImpl *impl = (ZenoMutexImpl *)mutex->handle;
#ifdef _WIN32
    LeaveCriticalSection(&impl->critical);
#else
    (void)pthread_mutex_unlock(&impl->handle);
#endif
}

void zeno_mutex_destroy(ZenoMutex *mutex) {
    if (mutex == NULL || mutex->handle == NULL) return;
    mutex_destroy((ZenoMutexImpl *)mutex->handle);
    mutex->handle = NULL;
}

/* Threads: a spawn/join pair plus a work-sharing parallel-for. Workers pull
 * indexes from a shared counter, so any worker failure only reduces
 * concurrency; the calling thread always drains what is left. */

typedef struct ZenoThreadStart { void (*fn)(void *); void *context; } ZenoThreadStart;

#ifdef _WIN32
typedef HANDLE ZenoThreadHandle;
static DWORD WINAPI thread_run(LPVOID raw) {
    ZenoThreadStart *job = (ZenoThreadStart *)raw;
    job->fn(job->context);
    free(job);
    return 0;
}
static int thread_spawn(ZenoThreadHandle *out, void (*fn)(void *), void *context) {
    ZenoThreadStart *job = (ZenoThreadStart *)malloc(sizeof(*job));
    if (job == NULL) return 0;
    job->fn = fn; job->context = context;
    ZenoThreadHandle handle = CreateThread(NULL, 0, thread_run, job, 0, NULL);
    if (handle == NULL) { free(job); return 0; }
    *out = handle;
    return 1;
}
static void thread_join(ZenoThreadHandle handle) {
    (void)WaitForSingleObject(handle, INFINITE);
    (void)CloseHandle(handle);
}
#else
typedef pthread_t ZenoThreadHandle;
static void *thread_run(void *raw) {
    ZenoThreadStart *job = (ZenoThreadStart *)raw;
    job->fn(job->context);
    free(job);
    return NULL;
}
static int thread_spawn(ZenoThreadHandle *out, void (*fn)(void *), void *context) {
    ZenoThreadStart *job = (ZenoThreadStart *)malloc(sizeof(*job));
    if (job == NULL) return 0;
    job->fn = fn; job->context = context;
    if (pthread_create(out, NULL, thread_run, job) != 0) { free(job); return 0; }
    return 1;
}
static void thread_join(ZenoThreadHandle handle) { (void)pthread_join(handle, NULL); }
#endif

typedef struct ParallelPool {
    ZenoMutex lock;
    size_t next;
    size_t count;
    ZenoParallelFn fn;
    void *context;
} ParallelPool;

static void parallel_worker(void *raw) {
    ParallelPool *pool = (ParallelPool *)raw;
    for (;;) {
        zeno_mutex_lock(&pool->lock);
        size_t index = pool->next;
        if (index < pool->count) pool->next++;
        zeno_mutex_unlock(&pool->lock);
        if (index >= pool->count) return;
        pool->fn(pool->context, index);
    }
}

int zeno_parallel_for(size_t count, size_t max_threads, ZenoParallelFn fn, void *context) {
    if (count == 0 || fn == NULL) return 1;
    if (max_threads == 0 || max_threads > count) max_threads = count;
    if (max_threads > 16) max_threads = 16;
    if (max_threads <= 1) {
        for (size_t index = 0; index < count; index++) fn(context, index);
        return 1;
    }
    ParallelPool pool;
    memset(&pool, 0, sizeof(pool));
    pool.count = count; pool.fn = fn; pool.context = context;
    ZenoThreadHandle handles[16];
    size_t spawned = 0;
    while (spawned < max_threads - 1) {
        if (!thread_spawn(&handles[spawned], parallel_worker, &pool)) break;
        spawned++;
    }
    parallel_worker(&pool);
    for (size_t index = 0; index < spawned; index++) thread_join(handles[index]);
    return 1;
}
