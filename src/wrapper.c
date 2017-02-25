#include "wrapper.h"
#include <pthread.h>
#include <stdio.h>

void wrap_mutex_lock(pthread_mutex_t *mutex)
{
	int ret = pthread_mutex_lock(mutex);
	if (ret) {
		fprintf(stderr, "Cannot lock mutex (err=%d).\n", ret);
	}
}

void wrap_mutex_unlock(pthread_mutex_t *mutex)
{
	int ret = pthread_mutex_unlock(mutex);
	if (ret) {
		fprintf(stderr, "Cannot unlock mutex (err=%d).\n", ret);
	}
}

void wrap_barrier_wait(pthread_barrier_t *barrier)
{
	int ret = pthread_barrier_wait(barrier);
	if (ret != 0 && ret != PTHREAD_BARRIER_SERIAL_THREAD) {
		fprintf(stderr, "Cannot wait on barrier (err=%d)\n", ret);
	}
}
