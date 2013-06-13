#ifndef _LOCKS_H
#define _LOCKS_H

#include <stdlib.h>
#include <assert.h>
#include <pthread.h>

class scope_lock {
	private:
		scope_lock(const scope_lock &l) { abort(); };
		scope_lock& operator=(const scope_lock &locker) { abort(); };

	public:
		scope_lock(pthread_mutex_t *mutex) 
			: mutex(mutex), locked(false) { this->lock(); };
		scope_lock(pthread_mutex_t *mutex, bool lock)
			: mutex(mutex), locked(false) { if (lock) this->lock(); };

		void lock();
		bool try_lock();
		void unlock();

		~scope_lock() { if (locked) this->unlock(); };

	protected:
		pthread_mutex_t *mutex;
		bool locked;
};

#endif
