#ifndef __WRAPPER_H
#define __WRAPPER_H

#include <pthread.h>

void wrap_mutex_lock(pthread_mutex_t *mutex);
void wrap_mutex_unlock(pthread_mutex_t *mutex);

#endif
