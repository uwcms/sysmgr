#include <string>
#include <vector>
#include <queue>
#include <map>
#include <pthread.h>
#include <assert.h>

#ifndef _SYSMGR_H
#define _SYSMGR_H

namespace sysmgr {
	class sysmgr_exception {
		public:
			const std::string message;
			sysmgr_exception(const std::string &message) : message(message) { };
	};

	class remote_exception : public sysmgr_exception {
		public:
			remote_exception(const std::string &message) : sysmgr_exception(message) { };
	};

	class event {
		public:
			uint32_t filterid;
			uint8_t crate;
			uint8_t fru;
			std::string card;
			std::string sensor;
			bool assertion;
			uint8_t offset;
		protected:
			event(uint32_t filterid, uint8_t crate, uint8_t fru, std::string card, std::string sensor, bool assertion, uint8_t offset)
				: filterid(filterid), crate(crate), fru(fru), card(card), sensor(sensor), assertion(assertion), offset(offset) { };
			friend class sysmgr;
	};

	class crate_info {
		public:
			uint8_t crateno;
			bool connected;
			std::string mch;
			std::string description;
		protected:
			crate_info(uint8_t crateno, bool connected, std::string mch, std::string description)
				: crateno(crateno), connected(connected), mch(mch), description(description) { };
			friend class sysmgr;
	};

	class card_info {
		public:
			uint8_t fru;
			uint8_t mstate;
			std::string name;
		protected:
			card_info(uint8_t fru, uint8_t mstate, std::string name)
				: fru(fru), mstate(mstate), name(name) { };
			friend class sysmgr;
	};

	class sensor_info {
		public:
			std::string name;
			char type; // 'T'hreshold, 'D'iscrete, 'E'ventOnly, 'O'EM
			std::string longunits;
			std::string shortunits;
		protected:
			sensor_info(std::string name, char type, std::string longunits, std::string shortunits)
				: name(name), type(type), longunits(longunits), shortunits(shortunits) { };
			friend class sysmgr;
	};

	class sensor_reading {
		public:
			uint8_t raw;
			bool threshold_set;
			double threshold;
			uint16_t eventmask;
		protected:
			sensor_reading() : threshold_set(false) { };
			friend class sysmgr;
	};

	class sensor_thresholds {
		public:
			uint8_t lnc;
			uint8_t lc;
			uint8_t lnr;
			bool lnc_set;
			bool lc_set;
			bool lnr_set;
			uint8_t unc;
			uint8_t uc;
			uint8_t unr;
			bool unc_set;
			bool uc_set;
			bool unr_set;
			sensor_thresholds()
				: lnc_set(false), lc_set(false), lnr_set(false), unc_set(false), uc_set(false), unr_set(false) { };
	};

	class sysmgr {
		public:
			typedef void (*event_handler)(const event& event);

		protected:
			pthread_mutex_t lock;

			std::string host;
			std::string password;
			uint16_t port;

			int fd;
			std::queue<event> events;
			uint32_t next_msgid;
			std::string recvbuf;
			std::string last_error;

			void lock_init();

			uint32_t get_msgid();

			int write(const std::string data);
			bool fetch_line(struct timeval const *timeout);
			std::vector<std::string> get_line(uint32_t msgid);


			std::map<uint32_t,event_handler> event_handlers;

		private:
			sysmgr(sysmgr &mgr) { assert(false); };
			void operator=(sysmgr &mgr) { assert(false); };

		public:
			sysmgr(std::string host)
				: host(host), password(""), port(4681), fd(-1), next_msgid(0) { lock_init(); };
			sysmgr(std::string host, std::string password)
				: host(host), password(password), port(4681), fd(-1), next_msgid(0) { lock_init(); };
			sysmgr(std::string host, std::string password, uint16_t port)
				: host(host), password(""), port(port), fd(-1), next_msgid(0) { lock_init(); };

			~sysmgr() { if (this->fd != -1) close(this->fd); }

			void connect();
			void disconnect();
			bool connected() { return (this->fd != -1); };

			std::string get_last_error() { return last_error; };

			int register_event_filter(uint8_t crate, uint8_t fru, std::string card, std::string sensor, uint16_t assertmask, uint16_t deassertmask, event_handler handler);
			int process_events(struct timeval const *timeout);
			void unregister_event_filter(uint32_t filterid);

			std::vector<crate_info> list_crates();
			std::vector<card_info> list_cards(uint8_t crate);
			std::vector<sensor_info> list_sensors(uint8_t crate, uint8_t fru);
			sensor_reading sensor_read(uint8_t crate, uint8_t fru, std::string sensor);
			std::string get_fru_sdr(uint8_t crate, uint8_t fru);
			std::string get_sensor_sdr(uint8_t crate, uint8_t fru, std::string sensor);

			sensor_thresholds get_sensor_thresholds(uint8_t crate, uint8_t fru, std::string sensor);
			void set_sensor_thresholds(uint8_t crate, uint8_t fru, std::string sensor, sensor_thresholds thresholds);

			// cmd = [ NetFN, CMD, ParamList ]
			// ret = [ CmplCode, ParamList ]
			std::string raw_card(uint8_t crate, uint8_t fru, std::string cmd);
			std::string raw_forwarded(uint8_t crate, uint8_t bridgechan, uint8_t bridgeaddr, uint8_t targetchan, uint8_t targetaddr, std::string cmd);
			std::string raw_direct(uint8_t crate, uint8_t targetchan, uint8_t targetaddr, std::string cmd);

			static std::string get_slotstring(uint8_t fru);
	};
};

#endif
