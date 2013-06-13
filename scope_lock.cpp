#include <errno.h>
#include <string.h>
#include <stdio.h>

#include "scope_lock.h"

void scope_lock::lock()
{
	assert(!locked);
	assert(!pthread_mutex_lock(mutex));
	locked = true;
}

bool scope_lock::try_lock()
{
	assert(!locked);
	if (pthread_mutex_trylock(mutex)) {
		if (errno != EBUSY && errno != EAGAIN) {
			/*
			 * man pthread_mutex_trylock says only EBUSY
			 * pthread_mutex_trylock source code from google says only EBUSY
			 * gdb says EAGAIN
			 *
			 * I guess we go with EAGAIN.
			 */
			printf("pthread_mutex_trylock returned errno (%d) %s\n", errno, strerror(errno));
			abort();
		}
		return false;
	}
	locked = true;
	return true;
}

void scope_lock::unlock()
{
	assert(locked);
	assert(!pthread_mutex_unlock(mutex));
	locked = false;
}
