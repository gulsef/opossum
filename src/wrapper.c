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
