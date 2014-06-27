#include <stdio.h>
#include <syslog.h>

#include "sysmgr.h"

pthread_mutex_t stdout_mutex = PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP;
bool stdout_use_syslog = false;
static std::string mprintf_linebuf;

std::string stdsprintf(const char *fmt, ...) {
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

extern std::string mprintf_linebuf;
int mprintf(const char *fmt, ...)
{
	scope_lock sl(&stdout_mutex);

	va_list va;
	va_list va2;
	va_start(va, fmt);
	va_copy(va2, va);
	size_t s = vsnprintf(NULL, 0, fmt, va);
	char str[s];
	vsnprintf(str, s+1, fmt, va2);
	va_end(va);
	va_end(va2);

	mprintf_linebuf += std::string(str);
	mflush(false);

	return s;
}

static void raw_output(std::string line)
{
	static bool syslog_is_open = false;

	if (!stdout_use_syslog) {
		printf("%s", line.c_str());
		fflush(stdout);
	}
	else {
		if (!syslog_is_open) {
			openlog("sysmgr", LOG_CONS|LOG_PID, LOG_USER);
			syslog_is_open = true;
		}
		syslog(LOG_INFO, "%s", line.c_str());
	}
}

void mflush(bool absolute)
{
	scope_lock sl(&stdout_mutex);

	while (1) {
		size_t nextnl = mprintf_linebuf.find("\n");

		if (nextnl == std::string::npos && !absolute && stdout_use_syslog)
			break;
		if (mprintf_linebuf.size() == 0)
			break;

		std::string line = mprintf_linebuf.substr(0, nextnl+1);
		mprintf_linebuf.erase(0, nextnl+1);
		raw_output(line);
	}
}
