#ifndef _MGMT_PROTOCOL_H
#define _MGMT_PROTOCOL_H

#define PROTOCOL_MAX_LINE_LENGTH 256
#define PROTOCOL_MAX_WRITEBUF (1024*1024)
#define PROTOCOL_MAX_UNAUTH_BYTES (PROTOCOL_MAX_LINE_LENGTH*5)

#include <errno.h>

#include "sysmgr.h"
#include "scope_lock.h"
#include <limits.h>

DEFINE_EXCEPTION(Protocol_Exception, Sysmgr_Exception);

class Client;

enum AuthLevel {
	AUTH_NONE	= 0,
	AUTH_READ	= 1,
	AUTH_MANAGE	= 2,
	AUTH_RAW	= 3
};

class EventFilter {
	public:
		uint8_t crate;
		uint8_t fru;
		std::string card;
		std::string sensor;
		uint16_t assertmask;
		uint16_t deassertmask;
		EventFilter() : crate(0xff), fru(0xff), card(""), sensor(""), assertmask(0x7fff), deassertmask(0x7fff) { };
		EventFilter(uint8_t crate, uint8_t fru, std::string card, std::string sensor, uint16_t assertmask, uint16_t deassertmask)
			: crate(crate), fru(fru), card(card), sensor(sensor), assertmask(assertmask & 0x7fff), deassertmask(deassertmask & 0x7fff) { };
		bool match(EventData event) {
			if (crate != 0xff && crate != event.crate) return false;
			if (fru != 0xff && fru != event.fru) return false;
			if (card != "" && card != event.card) return false;
			if (sensor != "" && sensor != event.sensor) return false;
			if (event.assertion)
				return (assertmask & (1 << event.offset));
			else
				return (deassertmask & (1 << event.offset));
		}
};

class Command {
	protected:
		pthread_mutex_t lock;

		bool reapable;
		uint32_t msgid;
		const std::string rawcmd;
		const std::vector<std::string> cmd;
		std::string writebuf;

	public:
		DEFINE_EXCEPTION(ProtocolParseException, Protocol_Exception);

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
				THROWMSG(ProtocolParseException, "Unterminated Quoted String");
			return tokens;
		}

		DEFINE_EXCEPTION(InvalidIntegerException, ProtocolParseException);

		static uint32_t parse_uint32(std::string token) {
			const char *str = token.c_str();
			char *end;
			unsigned long int val = strtoul(str, &end, 0);
			if (end == str || *end != '\0')
				THROWMSG(InvalidIntegerException, "Integer parsing failed: Invalid string");
			if ((val == ULONG_MAX && errno == ERANGE) || val > 0xffffffffu)
				THROWMSG(InvalidIntegerException, "Integer parsing failed: Overflow");
			return val;
		}

		static uint8_t parse_uint8(std::string token) {
			uint32_t val = parse_uint32(token);
			if (val > 0xff)
				THROWMSG(InvalidIntegerException, "Integer parsing failed: Overflow");
			return val;
		}

		static uint16_t parse_uint16(std::string token) {
			uint32_t val = parse_uint32(token);
			if (val > 0xffff)
				THROWMSG(InvalidIntegerException, "Integer parsing failed: Overflow");
			return val;
		}

		static std::string quote_string(const std::string unquot) {
			std::string quot = unquot;
			for (int i = quot.length() - 1; i >= 0; i--)
				if (quot[i] == '"')
					quot.replace(i, 1, "\"\"");

			return quot;
		}

		static uint8_t parse_valid_crate(std::string token, std::string *error);
		static Card *parse_valid_fru(Crate *crate, std::string token, std::string *error);

		Command(std::string rawcmd, std::vector<std::string> cmd) : reapable(false), rawcmd(rawcmd), cmd(cmd), writebuf("") {
			this->msgid = Command::parse_uint32(cmd[0]);
			pthread_mutexattr_t attr;
			assert(!pthread_mutexattr_init(&attr));
			assert(!pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE));

			assert(!pthread_mutex_init(&this->lock, &attr));
			pthread_mutexattr_destroy(&attr);
		};

		virtual ~Command() { };

		virtual uint32_t get_msgid() {
			scope_lock dlock(&this->lock);
			return this->msgid;
		};

		virtual std::string get_rawcmd() { return this->rawcmd; };

		virtual bool is_reapable() {
			scope_lock dlock(&this->lock, false);
			if (!dlock.try_lock())
				return false;
			return reapable;
		};

		virtual enum AuthLevel get_required_privilege() { return AUTH_RAW; };
		virtual uint8_t get_required_context() { return 0; };
		virtual void run_here();
		virtual void run_here(void *cb_null) { this->run_here(); }; // for callbacks
		virtual void payload();
		virtual void finalize(Client &client);

	protected:
		virtual void ratelimit();
};

class Client {
	protected:
		pthread_mutex_t lock;

		int fd;
		std::string readbuf;
		std::string writebuf;
		uint32_t next_out_msgid;
		uint64_t received_bytes;

		std::vector<Command*> command_queue;
		std::map<uint32_t,EventFilter> event_filters;
		uint32_t next_filter;

		enum AuthLevel privilege;

		void finalize_queue();

	public:
		Client(int fd)
			: fd(fd), readbuf(""), writebuf(""), next_out_msgid(1),
			received_bytes(0), next_filter(0), privilege(AUTH_NONE) {

				pthread_mutexattr_t attr;
				assert(!pthread_mutexattr_init(&attr));
				assert(!pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE));

				assert(!pthread_mutex_init(&this->lock, &attr));
				pthread_mutexattr_destroy(&attr);


				for (std::vector<std::string>::iterator it = config_authdata.raw.begin(); it != config_authdata.raw.end(); it++) {
					if (*it == "")
						this->upgrade_privilege(AUTH_RAW);
				}

				for (std::vector<std::string>::iterator it = config_authdata.manage.begin(); it != config_authdata.manage.end(); it++) {
					if (*it == "")
						this->upgrade_privilege(AUTH_MANAGE);
				}

				for (std::vector<std::string>::iterator it = config_authdata.read.begin(); it != config_authdata.read.end(); it++) {
					if (*it == "")
						this->upgrade_privilege(AUTH_READ);
				}
			};

		int get_fd() { return this->fd; };
		bool closed() { scope_lock dlock(&this->lock); return (this->fd == -1 && this->command_queue.empty()); };
		bool selectable_read() { scope_lock dlock(&this->lock); return (this->fd != -1); };
		bool selectable_write() {
			scope_lock dlock(&this->lock);
			this->finalize_queue();
			return (this->fd != -1 && this->writebuf.length());
		};

		void selected_read();
		void selected_write();
		void write(std::string data);
		void process_command(std::string line);
		void dispatch_event(EventData event);

		uint32_t get_next_out_msgid() {
			scope_lock dlock(&this->lock);

			uint32_t msgid = this->next_out_msgid;
			this->next_out_msgid += 2;
			this->next_out_msgid %= 4294967294; // (Even) MAX_UINT32-1
			return msgid;
		};

		uint32_t register_event_filter(EventFilter filter) {
			scope_lock dlock(&this->lock);

			this->next_filter %= 0xffffffffull;
			uint32_t filterid = ++this->next_filter;
			this->event_filters.insert(std::pair<uint32_t, EventFilter>(filterid, filter));
			return filterid;
		}

		void unregister_event_filter(uint32_t filterid) {
			scope_lock dlock(&this->lock);

			this->event_filters.erase(filterid);
		}

		std::map<uint32_t,EventFilter> get_event_filters() {
			scope_lock dlock(&this->lock);

			return this->event_filters;
		}

		void upgrade_privilege(enum AuthLevel newpriv) { scope_lock dlock(&this->lock); if (this->privilege < newpriv) this->privilege = newpriv; };
		enum AuthLevel get_privilege() { scope_lock dlock(&this->lock); return this->privilege; };
};

#endif
