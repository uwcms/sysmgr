#include <stdint.h>
#include <sys/time.h>
#include <string>
#include <vector>
#include <queue>
#include <map>
#include <pthread.h>
#include <assert.h>
#include <stdint.h>

#ifndef _SYSMGR_H
#define _SYSMGR_H

namespace sysmgr {
	/* All exceptions thrown by the sysmgr library will be a subclass of
	 * sysmgr_exception, and will contain a 'message' property with a more
	 * detailed description of the error.
	 */
	class sysmgr_exception {
		public:
			const std::string message;
			sysmgr_exception(const std::string &message) : message(message) { };
	};

	/* A remote_exception refers to any exception returned to us by the system
	 * manager, rather than originating within the client library itself.
	 */
	class remote_exception : public sysmgr_exception {
		public:
			remote_exception(const std::string &message) : sysmgr_exception(message) { };
	};

	/* An IPMI Event from the system manager as passed to an event handler.
	 */
	class event {
		public:
			uint32_t filterid;	// The number of the filter that recieved this event
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

	/* Crate Information
	 */
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

	/* Card Information
	 */
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

	/* Sensor Information
	 */
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

	/* Sensor reading response data
	 *
	 * If 'threshold_set' is false, the system manager was not able to generate
	 * a decoded value (and it is probably not a threshold type sensor).
	 *
	 * It is up to you to know (from sensor_info) what type of reading you
	 * expect from the sensor.
	 */
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

	/* Sensor Threshold Information
	 *
	 * When reading sensor thresholds, xxx_set indicates whether the threshold
	 * was readable.  When writing thresholds, xxx_set indicates whether the
	 * threshold is to be modified.
	 *
	 * Note that these are in RAW form, not interpreted form.  You must handle
	 * the relevant conversions yourself using data acquired from the SDRs.
	 *
	 * lnc	Lower Non-Critical
	 * lc	Lower Critical
	 * lnr	Lower Non-Recoverable
	 * unc	Upper Non-Critical
	 * uc	Upper Critical
	 * unr	Upper Non-Recoverable
	 */
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

	/* This is the primary system manager class, it reflects your connection to
	 * the system manager and handles all interactions with it.
	 */
	class sysmgr {
		public:
			/* This is the event handler prototype for use with the
			 * register_event_filter function.  It will be called during the
			 * execution of process_events() when events are received by the
			 * system.
			 *
			 * If an event matches multiple filters, each filter will have its
			 * event_handler function called.
			 */
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

			/* These functions control the actual connection and disconnection
			 * from the system manager service, as well as identifying whether
			 * the library appears to have an active connection
			 */
			void connect();
			void disconnect();
			bool connected() { return (this->fd != -1); };

			std::string get_last_error() { return last_error; };

			/* You can register an event filter to ask to receive events from
			 * the system manager.  The fields you specify will determine the
			 * type of events you receive.  It is possible to specify wildcards
			 * for all fields.  For uint8_t fields, the wildcard value is 0xff,
			 * for std::string fields, the wildcard mask is "".
			 *
			 * These fields specify which events you are interested in receiving.
			 *
			 * assertmask and deassertmask specify which event offsets you are
			 * interested in.  You may use 0x7fff to match any possible offset.
			 *
			 * Which offsets have what meaning varies completely based on the
			 * type of sensor (including the many subtypes of discrete sensor).
			 * See section 42 of the IPMI specification to learn what offsets
			 * are relevant to any given type.
			 *
			 * For Threshold sensors, the offsets are as follows:
			 * 0	Lower Non-Critical, Going Low
			 * 1	Lower Non-Critical, Going High
			 * 2	Lower Critical, Going Low
			 * 3	Lower Critical, Going High
			 * 4	Lower Non-Recoverable, Going Low
			 * 5	Lower Non-Recoverable, Going High
			 * 6	Upper Non-Critical, Going Low
			 * 7	Upper Non-Critical, Going High
			 * 8	Upper Critical, Going Low
			 * 9	Upper Critical, Going High
			 * a	Upper Non-Recoverable, Going Low
			 * b	Upper Non-Recoverable, Going High
			 *
			 * The registered filter id is returned.
			 */
			int register_event_filter(uint8_t crate, uint8_t fru, std::string card, std::string sensor, uint16_t assertmask, uint16_t deassertmask, event_handler handler);

			/* This function must be called in order for events that are
			 * received to be processed.  It will dispatch any queued events,
			 * and wait for any new events from the system manager until the
			 * timeout has expired.  If the timeout is NULL, this function will
			 * only return by throwing an exception.
			 *
			 * Note that only time spent in select() is counted against this
			 * timeout at this time, not any time spent in your event_handler
			 * callback functions.
			 *
			 * The number of events processed is returned.
			 */
			int process_events(struct timeval const *timeout);

			/* Unregister a filter specified by its filterid.
			 *
			 * It is safe to call this function from within an event handler.
			 */
			void unregister_event_filter(uint32_t filterid);

			std::vector<crate_info> list_crates();
			std::vector<card_info> list_cards(uint8_t crate);
			std::vector<sensor_info> list_sensors(uint8_t crate, uint8_t fru);
			sensor_reading sensor_read(uint8_t crate, uint8_t fru, std::string sensor);

			/* These functions return the raw SDR entries for the specified
			 * object.  It is up to you to know how to parse them.  See the IPMI
			 * specification for details.  This is not required for normal
			 * applications.
			 */
			std::string get_fru_sdr(uint8_t crate, uint8_t fru);
			std::string get_sensor_sdr(uint8_t crate, uint8_t fru, std::string sensor);

			/* These functions allow you to read or write the raw threshold
			 * values for a given sensor.  This only makes sense for Threshold
			 * sensors.
			 *
			 * See the documentation for the sensor_thresholds object for more
			 * details.
			 */
			sensor_thresholds get_sensor_thresholds(uint8_t crate, uint8_t fru, std::string sensor);
			void set_sensor_thresholds(uint8_t crate, uint8_t fru, std::string sensor, sensor_thresholds thresholds);

			// cmd = [ NetFN, CMD, ParamList ]
			// ret = [ CmplCode, ParamList ]
			std::string raw_card(uint8_t crate, uint8_t fru, std::string cmd);
			std::string raw_forwarded(uint8_t crate, uint8_t bridgechan, uint8_t bridgeaddr, uint8_t targetchan, uint8_t targetaddr, std::string cmd);
			std::string raw_direct(uint8_t crate, uint8_t targetchan, uint8_t targetaddr, std::string cmd);

			/* This helper function will return a short human-readable string
			 * version of the given FRU id, such as MCH1 AMC01 PM1 CU1, or
			 * failing all else, FRU1.
			 */
			static std::string get_slotstring(uint8_t fru);
	};
};

#endif
