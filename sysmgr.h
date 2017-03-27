#ifndef _SYSMGR_H
#define _SYSMGR_H

#define	EXCEPTION_GUARD
#undef	IPMI_TRACE
#undef	DEBUG_EXCEPTION_ABORT
#undef	DEBUG_ONESHOT
#undef	DEBUG_OUTPUT
#undef	DEBUG_SOCKET_FLOW
#define	ABSOLUTE_HOTSWAP // Check sensor value, not event data.

#define CONFIG_PATH "/etc/sysmgr"
#define CONFIG_FILE "sysmgr.conf"
#define DEFAULT_MODULE_PATH "/usr/lib64/sysmgr/modules"

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

class Card;

#define CARD_MODULE_API_VERSION 2
typedef struct {
	void *dl_addr;
	uint32_t APIVER;
	uint32_t MIN_APIVER;
	bool (*initialize_module)(std::vector<std::string> config);
	Card *(*instantiate_card)(Crate *crate, std::string name, void *sdrbuf, uint8_t sdrbuflen);
} cardmodule_t;
extern std::vector<cardmodule_t> card_modules;

extern WakeSock wake_socket;


std::string stdsprintf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

extern pthread_mutex_t stdout_mutex;
extern bool stdout_use_syslog;
int mprintf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void mflush(bool absolute);

#ifdef DEBUG_OUTPUT
#define dmprintf(...) mprintf(__VA_ARGS__)
#define DI(x) mprintf("C%d: %s = %d\n", CRATE_NO, #x, (x))
#define DX(x) mprintf("C%d: %s = 0x%02x\n", CRATE_NO, #x, (x))
#else
#define dmprintf(...)
#define DI(x) (x)
#define DX(x) (x)
#endif

void fiid_dump(fiid_obj_t obj);

#endif
