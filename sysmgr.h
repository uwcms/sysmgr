#ifndef _SYSMGR_H
#define _SYSMGR_H

#define	EXCEPTION_GUARD
#undef	IPMI_TRACE
#undef	DEBUG_EXCEPTION_ABORT
#undef	DEBUG_ONESHOT
#undef	DEBUG_OUTPUT
#undef	DEBUG_SOCKET_FLOW
#undef	PRINT_SEL
#define	ABSOLUTE_HOTSWAP // Check sensor value, not event data.

#define CONFIG_FILE "/etc/sysmgr.conf"

/* -------------------------------------------------------------------------- */

#include <pthread.h>
#include <stdio.h>
#include <stdarg.h>
#include <string>
#include <typeinfo>
#include <freeipmi/freeipmi.h>
#include <assert.h>
#include <vector>
#include <queue>

#include "scope_lock.h"
#include "TaskQueue.h"
#include "WakeSock.h"

class Crate;

class threadlocaldata_t {
	public:
		pthread_t thread;
		bool enabled;
		Crate *crate;
		TaskQueue taskqueue;
		threadlocaldata_t(Crate *crate, bool enabled)
			: enabled(enabled), crate(crate) { };
};

extern pthread_key_t threadid_key;
#define CRATE_NO (*((uint8_t*)pthread_getspecific(threadid_key)))
#define THREADLOCAL (threadlocal[CRATE_NO-1])
extern std::vector<threadlocaldata_t> threadlocal;

typedef struct {
	std::vector<std::string> raw;
	std::vector<std::string> manage;
	std::vector<std::string> read;
} config_authdata_t;

extern config_authdata_t config_authdata;
extern uint32_t config_ratelimit_delay;

class EventData {
	public:
		const uint8_t crate;
		const uint8_t fru;
		const std::string card;
		const std::string sensor;
		const bool assertion;
		const uint8_t offset;
		EventData(uint8_t crate, uint8_t fru, std::string card, std::string sensor, bool assertion, uint8_t offset)
			: crate(crate), fru(fru), card(card), sensor(sensor), assertion(assertion), offset(offset) { };
};
extern pthread_mutex_t events_mutex;
extern std::queue<EventData> events;

extern WakeSock wake_socket;


extern pthread_mutex_t stdout_mutex;
inline int mprintf(const char *fmt, ...) {
	scope_lock sl(&stdout_mutex);

	va_list va;
	va_start(va, fmt);
	int rv = vprintf(fmt, va);
	va_end(va);

	fflush(stdout);
	return rv;
}
#ifdef DEBUG_OUTPUT
#define dmprintf(...) mprintf(__VA_ARGS__)
#define DI(x) mprintf("C%d: %s = %d\n", CRATE_NO, #x, (x))
#define DX(x) mprintf("C%d: %s = 0x%02x\n", CRATE_NO, #x, (x))
#else
#define dmprintf(...)
#define DI(x) (x)
#define DX(x) (x)
#endif

inline std::string stdsprintf(const char *fmt, ...) {
	va_list va;
	va_list va2;
	va_start(va, fmt);
	va_copy(va2, va);
	size_t s = vsnprintf(NULL, 0, fmt, va);
	char str[s];
	vsnprintf(str, s+1, fmt, va2);
	va_end(va);
	va_end(va2);
	return std::string(str);
}

void fiid_dump(fiid_obj_t obj);

#endif
