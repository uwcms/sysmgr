/*
 *
 * 	 TaskQueue.h	by  University of Wisconsin
 *
 * 	 task queue of sysmgr: one TaskQueue per crate waits for ScheduledTasks and executes them
 *
 *
 * $Author$
 * $Revision$
 * $Date$
 *
 *
 */


#ifndef _TASKQUEUE_H
#define _TASKQUEUE_H

#include <queue>
#include <functional>
#include <time.h>
#include <pthread.h>
#include <stdint.h>
#include <sys/types.h>

#include "Callback.h"
#include "scope_lock.h"

class TaskQueue {
	public:
		typedef uint64_t taskid_t;

		taskid_t schedule(time_t when, callback<void> cb, void *cb_data);
		void cancel(taskid_t id);
		int run_one() { return run_until(1); };
		int run_forever() { return run_until(0); };
		int run_until(time_t stop_after);

	protected:
		taskid_t next_id;
		pthread_mutex_t lock;
		pthread_cond_t queueNotEmpty;

		class ScheduledTask {
			protected:
				taskid_t id;
				time_t when;
				callback<void> cb;
				void *cb_data;

				ScheduledTask(taskid_t id, time_t when, callback<void> cb, void *cb_data)
					: id(id), when(when), cb(cb), cb_data(cb_data) { };
			public:
				bool operator>(const ScheduledTask &ent) const {
					return this->when > ent.when;
				};
			friend class TaskQueue;
		};

		std::priority_queue< ScheduledTask, std::vector<ScheduledTask>, std::greater<ScheduledTask> > queue;

	public:
		TaskQueue() : next_id(1) {
		   pthread_mutexattr_t attr;
		   assert(!pthread_mutexattr_init(&attr));
		   assert(!pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE));

		   assert(!pthread_mutex_init(&this->lock, &attr));
		   pthread_mutexattr_destroy(&attr);
		   assert(!pthread_cond_init(&queueNotEmpty, NULL));
		};
};

#endif
