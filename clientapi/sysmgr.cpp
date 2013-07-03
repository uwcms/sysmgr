#include <sys/types.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <errno.h>
#include <string>
#include <stdarg.h>

#include "sysmgr.h"
#include "../scope_lock.h"

#ifdef __APPLE__
#define MSG_NOSIGNAL 0
#endif


#define MAX_LINE_LENGTH 1024

namespace sysmgr {

	static inline std::string stdsprintf(const char *fmt, ...) {
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

	static std::vector<std::string> tokenize(std::string line) {
		std::vector<std::string> tokens;
		bool inquote = false;
		bool newtoken = true;

		for (unsigned int i = 0; i < line.size(); i++) {
			char c = line[i];
			bool consume = false;

			if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
				if (!inquote) {
					newtoken = true;
					consume = true;
				}
			}

			if (c == '"') {
				if (newtoken) {
					newtoken = false;
					tokens.push_back(std::string());
				}

				inquote = !inquote;
				if (inquote && i != 0 && line[i-1] == '"') {
					// Append escaped " character.
				}
				else {
					consume = true;
				}
			}

			if (!consume) {
				if (newtoken) {
					newtoken = false;
					tokens.push_back(std::string());
				}
				tokens.back().push_back(line[i]);
			}

			/*
			   dmprintf("\nc:'%c', q:%u, n:%u, x:%u, t%u:'%s\"\n",
			   c,
			   (inquote ? 1 : 0),
			   (newtoken ? 1 : 0),
			   (consume ? 1 : 0),
			   tokens.size(),
			   tokens.back().c_str());
			   */
		}
		if (inquote)
			throw sysmgr_exception("Parsing Failed: Unterminated Quoted String");
		return tokens;
	}

	static uint32_t parse_uint32(std::string token) {
		const char *str = token.c_str();
		char *end;
		unsigned long int val = strtoul(str, &end, 0);
		if (end == str || *end != '\0')
			throw sysmgr_exception("Integer parsing failed: Invalid string");
		if ((val == ULONG_MAX && errno == ERANGE) || val > 0xffffffffu)
			throw sysmgr_exception("Integer parsing failed: Overflow");
		return val;
	}

	static uint8_t parse_uint8(std::string token) {
		uint32_t val = parse_uint32(token);
		if (val > 0xff)
			throw sysmgr_exception("Integer parsing failed: Overflow");
		return val;
	}

	static uint16_t parse_uint16(std::string token) {
		uint32_t val = parse_uint32(token);
		if (val > 0xffff)
			throw sysmgr_exception("Integer parsing failed: Overflow");
		return val;
	}

	static std::string quote_string(const std::string unquot) {
		std::string quot = unquot;
		for (int i = quot.length() - 1; i >= 0; i--)
			if (quot[i] == '"')
				quot.replace(i, 1, "\"\"");

		return quot;
	}

	static double parse_double(std::string token) {
		const char *str = token.c_str();
		char *end;
		errno = 0;
		double val = strtod(str, &end);
		if (end == str || *end != '\0')
			throw sysmgr_exception("Double parsing failed: Invalid string");
		if (errno == ERANGE)
			throw sysmgr_exception("Double parsing failed: ERANGE");
		return val;
	}

	void sysmgr::lock_init() {
		pthread_mutexattr_t attr;
		assert(!pthread_mutexattr_init(&attr));
		assert(!pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE));

		assert(!pthread_mutex_init(&this->lock, &attr));
		pthread_mutexattr_destroy(&attr);
	}

	uint32_t sysmgr::get_msgid() {
		scope_lock dlock(&this->lock);

		uint32_t msgid = this->next_msgid;
		this->next_msgid += 2;
		this->next_msgid %= 4294967294; // (Even) MAX_UINT32-1
		return msgid;
	}

	void sysmgr::connect()
	{
		scope_lock llock(&this->lock);

		if (this->fd != -1)
			this->disconnect();

		struct sockaddr_in serv_addr;
		struct hostent *server;

		server = gethostbyname(this->host.c_str());
		if (server == NULL)
			throw sysmgr_exception("Unable to resolve host");

		memset(&serv_addr, 0, sizeof(serv_addr));
		serv_addr.sin_family = AF_INET;
		memcpy(&serv_addr.sin_addr.s_addr, server->h_addr, server->h_length);
		serv_addr.sin_port = htons(port);

		this->fd = socket(AF_INET, SOCK_STREAM, 0);
		if (this->fd < 0)
			throw sysmgr_exception(stdsprintf("Unable to create socket: %d %s", errno, strerror(errno)));

		if (::connect(this->fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)  {
			close(this->fd);
			throw sysmgr_exception(stdsprintf("Unable to connect: %d %s", errno, strerror(errno)));
		}

		uint32_t msgid = this->get_msgid();
		try {
			this->write(stdsprintf("%u AUTHENTICATE \"%s\"\n", msgid, this->password.c_str()));
		}
		catch (sysmgr_exception &e) {
			this->disconnect();
			throw e;
		}

		try {
			std::vector<std::string> rsp = this->get_line(msgid);

			if (rsp.size() < 2 || rsp[1] == "NONE") {
				this->disconnect();
				throw sysmgr_exception("Invalid password: Got privilege level NONE");
			}
		}
		catch (sysmgr_exception &e) {
			this->disconnect();
			throw e;
		}
	}

	void sysmgr::disconnect()
	{
		scope_lock llock(&this->lock);

		if (this->fd != -1)
			close(this->fd);
		this->fd = -1;

		while (!this->events.empty())
			this->events.pop();

		this->event_handlers = std::map<uint32_t,event_handler>();
	}

	int sysmgr::write(const std::string data)
	{
		scope_lock llock(&this->lock);

		unsigned int written = 0;
		int count = data.length();
		char const *outbuf = data.c_str();

		while (written < data.length()) {
			int just_written = send(fd, outbuf, count, MSG_NOSIGNAL);
			if (just_written < 0) {
				if (errno == EAGAIN || errno == EWOULDBLOCK)
					continue;
				this->disconnect();
				throw sysmgr_exception(stdsprintf("send() failed: errno %d", errno));
			}
			written += just_written;
			outbuf += just_written;
			count -= just_written;
		}
		return written;
	}

	std::vector<std::string> sysmgr::get_line(uint32_t msgid)
	{
		scope_lock llock(&this->lock);

		while (1) {
			size_t nextnl;
			while ((nextnl = this->recvbuf.find('\n')) == std::string::npos) {
				this->fetch_line(NULL);
			}

			// Return first buffered line:

			std::string line = this->recvbuf.substr(0, nextnl);
			if (nextnl+1 < this->recvbuf.length())
				this->recvbuf = this->recvbuf.substr(nextnl+1);
			else
				this->recvbuf = "";

			std::vector<std::string> rsp = tokenize(line);
			if (rsp.size() < 1)
				continue;

			uint32_t rsp_msgid = parse_uint32(rsp[0]);

			rsp.erase(rsp.begin());

			if (rsp.size() >= 1 && rsp[0] == "ERROR") {
				/* Error message.
				 * Something went bad.  I dont know what, but let's throw it.
				 */
				std::string err;
				for (std::vector<std::string>::iterator it = rsp.begin()+1; it != rsp.end(); it++)
					err = err + " " + *it;
				throw remote_exception(err.substr(1));
			}
			else if (rsp_msgid == msgid)
				return rsp;
			else if ((rsp_msgid % 2) == 1 && rsp.size() >= 8 && rsp[0] == "EVENT")
				this->events.push(event(
							parse_uint32(rsp[1]),	// filterid
							parse_uint8(rsp[2]),	// crate
							parse_uint8(rsp[3]),	// fru
							rsp[4],					// card
							rsp[5],					// sensor
							parse_uint8(rsp[6]),	// is_assertion
							parse_uint8(rsp[7])		// offset
							));
			else {
				/* An unrelated or unknown message or response.
				 *
				 * This client library version does not presently support multiple
				 * simultaneous in-flight messages, so we will assume it is an
				 * oddity and ignore it.
				 */
			}
		}
	}

	bool sysmgr::fetch_line(struct timeval const *timeout)
	{
		scope_lock llock(&this->lock);

		struct timeval tv_zero;
		timerclear(&tv_zero);

		struct timeval end;
		gettimeofday(&end, NULL);
		if (timeout)
			timeradd(&end, timeout, &end);

		do {
			if (this->fd == -1)
				throw sysmgr_exception("Socket closed");

			struct timeval now;
			gettimeofday(&now, NULL);

			struct timeval period;
			timersub(&end, &now, &period);

			if (timercmp(&period, &tv_zero, <))
				timerclear(&period);

			fd_set fds;
			FD_ZERO(&fds);
			FD_SET(this->fd, &fds);
			int rv = select(this->fd+1, &fds, NULL, NULL, (timeout ? &period : NULL));

			if (rv < 0 && errno != EINTR) {
				this->disconnect();
				throw sysmgr_exception(stdsprintf("select() failed: errno %d", errno));
			}

			if (rv > 0) {
				char inbuf[MAX_LINE_LENGTH+1];
				memset(inbuf, 0, sizeof(inbuf));
				ssize_t readbytes = recv(this->fd, inbuf, MAX_LINE_LENGTH, MSG_DONTWAIT);
				if (readbytes == 0 || (readbytes == -1 && errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK)) {
					this->disconnect();
					throw sysmgr_exception(stdsprintf("recv() failed: errno %d", errno));
				}
				this->recvbuf += inbuf;

				if (this->recvbuf.length() > MAX_LINE_LENGTH && this->recvbuf.find('\n') == std::string::npos) {
					this->disconnect();
					throw sysmgr_exception("Received bad protocol line: too long");
				}
			}

			if (timeout && !timercmp(&period, &tv_zero, !=))
				return false;

		} while (this->recvbuf.find('\n') == std::string::npos);
		return true;
	}

	int sysmgr::register_event_filter(uint8_t crate, uint8_t fru, std::string card, std::string sensor, uint16_t assertmask, uint16_t deassertmask, event_handler handler)
	{
		scope_lock llock(&this->lock);

		uint32_t msgid = this->get_msgid();
		this->write(stdsprintf("%u SUBSCRIBE 0x%02x 0x%02x \"%s\" \"%s\" 0x%04x 0x%04x\n",
					msgid,
					crate,
					fru,
					quote_string(card).c_str(),
					quote_string(sensor).c_str(),
					assertmask,
					deassertmask
					));

		std::vector<std::string> rsp = this->get_line(msgid);
		if (rsp.size() < 2)
			throw sysmgr_exception("Unexpected repsonse to SUBSCRIBE command");

		uint32_t filterid = parse_uint32(rsp[1]);
		event_handlers.insert(std::pair<uint32_t,event_handler>(filterid, handler));

		return filterid;
	}

	int sysmgr::process_events(struct timeval const *timeout)
	{
		scope_lock llock(&this->lock);

		int events_processed = 0;

		struct timeval tv_zero;
		timerclear(&tv_zero);

		struct timeval end;
		gettimeofday(&end, NULL);
		if (timeout)
			timeradd(&end, timeout, &end);

		do {
			struct timeval now;
			gettimeofday(&now, NULL);

			struct timeval period;
			timersub(&end, &now, &period);

			if (timercmp(&period, &tv_zero, <))
				timerclear(&period);

			if (this->recvbuf.find('\n') != std::string::npos || this->fetch_line(timeout ? &period : NULL)) {
				size_t nextnl;
				if ((nextnl = this->recvbuf.find('\n')) != std::string::npos) {

					// Return first buffered line:

					std::string line = this->recvbuf.substr(0, nextnl);
					if (nextnl+1 < this->recvbuf.length())
						this->recvbuf = this->recvbuf.substr(nextnl+1);
					else
						this->recvbuf = "";

					std::vector<std::string> rsp = tokenize(line);
					if (rsp.size() > 0) {
						uint32_t rsp_msgid = parse_uint32(rsp[0]);

						rsp.erase(rsp.begin());

						if (rsp.size() >= 1 && rsp[0] == "ERROR") {
							/* Random error message.
							 * Something went bad.  I dont know what, but
							 * it's unlikely we can do anything about it.
							 */
						}
						else if ((rsp_msgid % 2) == 1 && rsp.size() >= 8 && rsp[0] == "EVENT")
							this->events.push(event(
										parse_uint32(rsp[1]),	// filterid
										parse_uint8(rsp[2]),	// crate
										parse_uint8(rsp[3]),	// fru
										rsp[4],					// card
										rsp[5],					// sensor
										parse_uint8(rsp[6]),	// is_assertion
										parse_uint8(rsp[7])		// offset
										));
						else {
							/* An unrelated or unknown message or response.
							 *
							 * This client library version does not
							 * presently support multiple simultaneous
							 * in-flight messages, so we will assume it is
							 * an oddity and ignore it.
							 */
						}
					}
				}
			}

			// Dispatch Events
			while (!this->events.empty()) {
				scope_lock llock(&this->lock);

				event e = this->events.front();
				this->events.pop();

				llock.unlock();
				std::map<uint32_t,event_handler>::iterator it = this->event_handlers.find(e.filterid);
				if (it != this->event_handlers.end())
					(*(it->second))(e);
				llock.lock();
				events_processed++;
			}

			if (timeout && !timercmp(&period, &tv_zero, !=))
				break;
		} while (1);

		return events_processed;
	}

	void sysmgr::unregister_event_filter(uint32_t filterid)
	{
		scope_lock llock(&this->lock);

		uint32_t msgid = this->get_msgid();
		this->write(stdsprintf("%u UNSUBSCRIBE %u\n", msgid, filterid));

		std::vector<std::string> rsp = this->get_line(msgid);
		if (rsp.size() < 1 || rsp[1] != "UNSUBSCRIBED")
			throw sysmgr_exception("Unexpected repsonse to SUBSCRIBE command");

		std::map<uint32_t,event_handler>::iterator it = this->event_handlers.find(filterid);
		if (it != this->event_handlers.end())
			this->event_handlers.erase(it);
	}

	std::vector<crate_info> sysmgr::list_crates()
	{
		scope_lock llock(&this->lock);

		uint32_t msgid = this->get_msgid();
		this->write(stdsprintf("%u LIST_CRATES\n", msgid));

		std::vector<crate_info> crates;
		while (1) {
			std::vector<std::string> rsp = this->get_line(msgid);
			if (rsp.size() == 0)
				return crates;

			if (rsp.size() < 4)
				throw sysmgr_exception("LIST_CRATES response not understood");

			crates.push_back(crate_info(
						parse_uint8(rsp[0]),
						parse_uint8(rsp[1]),
						rsp[2],
						rsp[3]
						));
		}
	}

	std::vector<card_info> sysmgr::list_cards(uint8_t crate)
	{
		scope_lock llock(&this->lock);

		uint32_t msgid = this->get_msgid();
		this->write(stdsprintf("%u LIST_CARDS %hhu\n", msgid, crate));

		std::vector<card_info> cards;
		while (1) {
			std::vector<std::string> rsp = this->get_line(msgid);
			if (rsp.size() == 0)
				return cards;

			if (rsp.size() < 3)
				throw sysmgr_exception("LIST_CARDS response not understood");

			cards.push_back(card_info(
						parse_uint8(rsp[0]),
						parse_uint8(rsp[1]),
						rsp[2]
						));
		}
	}

	std::vector<sensor_info> sysmgr::list_sensors(uint8_t crate, uint8_t fru)
	{
		scope_lock llock(&this->lock);

		uint32_t msgid = this->get_msgid();
		this->write(stdsprintf("%u LIST_SENSORS %hhu %hhu\n", msgid, crate, fru));

		std::vector<sensor_info> sensors;
		while (1) {
			std::vector<std::string> rsp = this->get_line(msgid);
			if (rsp.size() == 0)
				return sensors;

			if (rsp.size() < 4)
				throw sysmgr_exception("LIST_SENSORS response not understood");

			sensors.push_back(sensor_info(rsp[0], rsp[1][0], rsp[2], rsp[3]));
		}
	}

	sensor_reading sysmgr::sensor_read(uint8_t crate, uint8_t fru, std::string sensor)
	{
		scope_lock llock(&this->lock);

		uint32_t msgid = this->get_msgid();
		this->write(stdsprintf("%u SENSOR_READ %hhu %hhu \"%s\"\n", msgid, crate, fru, quote_string(sensor).c_str()));

		bool raw_set = false;
		bool event_set = false;
		sensor_reading reading;
		while (1) {
			std::vector<std::string> rsp = this->get_line(msgid);
			if (rsp.size() == 0) {
				if (raw_set && event_set)
					return reading;
				else
					throw sysmgr_exception("Incomplete SENSOR_READ response received");
			}

			if (rsp.size() != 2)
				throw sysmgr_exception("SENSOR_READ response not understood");

			if (rsp[0] == "RAW") {
				reading.raw = parse_uint8(rsp[1]);
				raw_set = true;
			}
			else if (rsp[0] == "THRESHOLD") {
				reading.threshold = parse_double(rsp[1]);
				reading.threshold_set = true;
			}
			else if (rsp[0] == "EVENTMASK") {
				reading.eventmask = parse_uint16(rsp[1]);
				event_set = true;
			}
		}
	}

	std::string sysmgr::get_fru_sdr(uint8_t crate, uint8_t fru)
	{
		scope_lock llock(&this->lock);

		uint32_t msgid = this->get_msgid();
		this->write(stdsprintf("%u GET_SDR %hhu %hhu\n", msgid, crate, fru));

		std::vector<std::string> rsp = this->get_line(msgid);
		if (rsp.size() < 1)
			throw sysmgr_exception("GET_SDR response not understood");

		std::string raw;
		for (std::vector<std::string>::iterator it = rsp.begin(); it != rsp.end(); it++)
			raw.push_back(static_cast<char>(parse_uint8(std::string("0x")+*it)));

		return raw;
	}

	std::string sysmgr::get_sensor_sdr(uint8_t crate, uint8_t fru, std::string sensor)
	{
		scope_lock llock(&this->lock);

		uint32_t msgid = this->get_msgid();
		this->write(stdsprintf("%u GET_SDR %hhu %hhu \"%s\"\n", msgid, crate, fru, quote_string(sensor).c_str()));

		std::vector<std::string> rsp = this->get_line(msgid);
		if (rsp.size() < 1)
			throw sysmgr_exception("GET_SDR response not understood");

		std::string raw;
		for (std::vector<std::string>::iterator it = rsp.begin(); it != rsp.end(); it++)
			raw.push_back(static_cast<char>(parse_uint8(std::string("0x")+*it)));

		return raw;
	}

	sensor_thresholds sysmgr::get_sensor_thresholds(uint8_t crate, uint8_t fru, std::string sensor)
	{
		scope_lock llock(&this->lock);

		uint32_t msgid = this->get_msgid();
		this->write(stdsprintf("%u GET_THRESHOLDS %hhu %hhu \"%s\"\n", msgid, crate, fru, quote_string(sensor).c_str()));

		std::vector<std::string> rsp = this->get_line(msgid);
		if (rsp.size() < 6)
			throw sysmgr_exception("GET_THRESHOLD response not understood");
		sensor_thresholds t;

		if (rsp[0] != "-") {
			t.lnc = parse_uint8(rsp[0]);
			t.lnc_set = true;
		}
		if (rsp[1] != "-") {
			t.lc = parse_uint8(rsp[1]);
			t.lc_set = true;
		}
		if (rsp[2] != "-") {
			t.lnr = parse_uint8(rsp[2]);
			t.lnr_set = true;
		}

		if (rsp[3] != "-") {
			t.unc = parse_uint8(rsp[3]);
			t.unc_set = true;
		}
		if (rsp[4] != "-") {
			t.uc = parse_uint8(rsp[4]);
			t.uc_set = true;
		}
		if (rsp[5] != "-") {
			t.unr = parse_uint8(rsp[5]);
			t.unr_set = true;
		}

		return t;
	}

	void sysmgr::set_sensor_thresholds(uint8_t crate, uint8_t fru, std::string sensor, sensor_thresholds thresholds)
	{
		scope_lock llock(&this->lock);

		std::string params = "";

		if (thresholds.lnc_set)
			params += stdsprintf(" 0x%02x", thresholds.lnc);
		else
			params += " -";

		if (thresholds.lc_set)
			params += stdsprintf(" 0x%02x", thresholds.lc);
		else
			params += " -";

		if (thresholds.lnr_set)
			params += stdsprintf(" 0x%02x", thresholds.lnr);
		else
			params += " -";


		if (thresholds.unc_set)
			params += stdsprintf(" 0x%02x", thresholds.unc);
		else
			params += " -";

		if (thresholds.uc_set)
			params += stdsprintf(" 0x%02x", thresholds.uc);
		else
			params += " -";

		if (thresholds.unr_set)
			params += stdsprintf(" 0x%02x", thresholds.unr);
		else
			params += " -";

		uint32_t msgid = this->get_msgid();
		this->write(stdsprintf("%u SET_THRESHOLDS %hhu %hhu \"%s\"%s\n", msgid, crate, fru, quote_string(sensor).c_str(), params.c_str()));

		std::vector<std::string> rsp = this->get_line(msgid);
		if (rsp.size() < 6)
			throw sysmgr_exception("SET_THRESHOLD response not understood");
	}

	std::string sysmgr::raw_card(uint8_t crate, uint8_t fru, std::string cmd)
	{
		scope_lock llock(&this->lock);

		std::string parameters;
		for (std::string::iterator it = cmd.begin(); it != cmd.end(); it++)
			parameters += stdsprintf(" 0x%02x", *it);

		uint32_t msgid = this->get_msgid();
		this->write(stdsprintf("%u RAW_CARD %hhu %hhu %s\n", msgid, crate, fru, parameters.c_str()));

		std::vector<std::string> rsp = this->get_line(msgid);
		if (rsp.size() < 1)
			throw sysmgr_exception("RAW_CARD response not understood");

		std::string raw;
		for (std::vector<std::string>::iterator it = rsp.begin(); it != rsp.end(); it++)
			raw.push_back(static_cast<char>(parse_uint8(*it)));
		return raw;
	}

	std::string sysmgr::raw_forwarded(uint8_t crate, uint8_t bridgechan, uint8_t bridgeaddr, uint8_t targetchan, uint8_t targetaddr, std::string cmd)
	{
		scope_lock llock(&this->lock);

		std::string parameters;
		for (std::string::iterator it = cmd.begin(); it != cmd.end(); it++)
			parameters += stdsprintf(" 0x%02x", *it);

		uint32_t msgid = this->get_msgid();
		this->write(stdsprintf("%u RAW_FORWARDED %hhu %hhu %hhu %hhu %hhu %s\n", msgid, crate, bridgechan, bridgeaddr, targetchan, targetaddr, parameters.c_str()));

		std::vector<std::string> rsp = this->get_line(msgid);
		if (rsp.size() < 1)
			throw sysmgr_exception("RAW_FORWARDED response not understood");

		std::string raw;
		for (std::vector<std::string>::iterator it = rsp.begin(); it != rsp.end(); it++)
			raw.push_back(static_cast<char>(parse_uint8(*it)));
		return raw;
	}

	std::string sysmgr::raw_direct(uint8_t crate, uint8_t targetchan, uint8_t targetaddr, std::string cmd)
	{
		scope_lock llock(&this->lock);

		std::string parameters;
		for (std::string::iterator it = cmd.begin(); it != cmd.end(); it++)
			parameters += stdsprintf(" 0x%02x", *it);

		uint32_t msgid = this->get_msgid();
		this->write(stdsprintf("%u RAW_DIRECT %hhu %hhu %hhu %s\n", msgid, crate, targetchan, targetaddr, parameters.c_str()));

		std::vector<std::string> rsp = this->get_line(msgid);
		if (rsp.size() < 1)
			throw sysmgr_exception("RAW_DIRECT response not understood");

		std::string raw;
		for (std::vector<std::string>::iterator it = rsp.begin(); it != rsp.end(); it++)
			raw.push_back(static_cast<char>(parse_uint8(*it)));
		return raw;
	}
};
