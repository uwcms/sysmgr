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
	int error = pthread_mutex_trylock(mutex);
	if (error) {
		if (error != EBUSY) {
			printf("pthread_mutex_trylock returned error (%d) %s\n", error, strerror(error));
			fflush(stdout);
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
