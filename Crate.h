#ifndef _CRATE_H
#define _CRATE_H

#include <string>
#include <string.h>
#include <assert.h>
#include <map>
#include <vector>
#include "sysmgr.h"
#include "exceptions.h"

DEFINE_EXCEPTION(SDRRepositoryNotPopulatedException, Crate_Exception);
DEFINE_EXCEPTION(InvalidAddressException, Sysmgr_Exception);

class Crate;
class Card;
class Sensor;
class HotswapSensor;

class Crate {
	public:
		enum Mfgr {
			VADATECH,
			NAT
		};

		struct {
			ipmi_ctx_t ipmi;
			ipmi_sdr_ctx_t sdr;
			ipmi_sel_ctx_t sel;
#ifdef INTERPRET_CTX
			ipmi_interpret_ctx_t interpret;
#endif
			ipmi_sensor_read_ctx_t sensor_read;
		} ctx;

	protected:
		uint8_t number;
		Card *cards[256];
		enum Mfgr MCH;
		char sdrfile[40];

		std::string ip;
		char *user;
		char *pass;
		uint8_t ipmi15_authentication_type;
		std::string description;
		bool log_sel;

		TaskQueue::taskid_t selscan_id;
		TaskQueue::taskid_t sdrscan_id;
		uint64_t selscan_lastclr;
		uint64_t selscan_nextent;
		bool force_sdr_scan;
		uint8_t sdr_scan_retries;
		uint8_t sendmessage_seq;

		void scan_sel(void *);
		void scan_sdr(void *);
		friend int crate__parse_sel_cb(ipmi_sel_ctx_t ctx, void *cb_crate);
		friend int crate__identify_slots_cb(ipmi_sdr_ctx_t ctx,
				uint8_t record_type,
				const void *sdr_record,
				unsigned int sdr_record_len,
				void *cb_data);
		friend int crate__identify_sensors_cb(ipmi_sdr_ctx_t ctx,
				uint8_t record_type,
				const void *sdr_record,
				unsigned int sdr_record_len,
				void *cb_data);

	public:
		Crate(uint8_t number, enum Mfgr MCH, std::string ip, const char *user, const char *pass, uint8_t ipmi15_authentication_type, std::string description, bool log_sel)
			: number(number), MCH(MCH), ip(ip), ipmi15_authentication_type(ipmi15_authentication_type), description(description), log_sel(log_sel), selscan_id(0), sdrscan_id(0), selscan_lastclr(0), selscan_nextent(0), force_sdr_scan(false), sdr_scan_retries(0), sendmessage_seq(0) {
				memset(&this->ctx, 0, sizeof(this->ctx));
				memset(&this->cards, 0, sizeof(this->cards));
				memset(&this->sdrfile, 0, sizeof(this->sdrfile));
				this->user = NULL;
				if (user) {
					this->user = new char[strlen(user)+1];
					memcpy(this->user, user, strlen(user)+1);
				}
				this->pass = NULL;
				if (pass) {
					this->pass = new char[strlen(pass)+1];
					memcpy(this->pass, pass, strlen(pass)+1);
				}
			};
		virtual ~Crate();

		uint8_t get_number() { return number; };
		enum Mfgr get_mch() { return MCH; };
		std::string get_description() { return description; };
		std::string get_ip() { return ip; };

		void add_card(Card *card);
		Card *get_card(uint8_t fru);
		Card *get_card(uint8_t entity_id, uint8_t entity_instance);
		void destroy_card(uint8_t fru);

		bool update_sdr_cache();
		void ipmi_connect();
		void ipmi_disconnect();

		int send_bridged(uint8_t br_channel, uint8_t br_addr, uint8_t channel, uint8_t addr, uint8_t netfn, fiid_obj_t msg_rq, fiid_obj_t msg_rs);
};

class Card {
	protected:
		Crate *crate;
		std::string name;
		uint8_t fru;
		uint8_t entity_id;
		uint8_t entity_instance;
		uint8_t access_addr;
		uint8_t channel;
		uint8_t sdrbuflen;
		uint8_t sdrbuf[64];
		std::map<uint8_t,Sensor*> sensors;

	public:
		static uint8_t slot_to_addr(uint8_t slot) {
			if (slot < 1 || slot > 13)
				return 0;
			return (slot == 13 ? 0xa4 : (slot*2) + 0x70);
		}

		static uint8_t addr_to_slot(uint8_t addr) {
			if (0x72 <= addr && addr < 0x88)
				return (addr - 0x70) / 2;
			if (addr == 0xa4)
				return 13;
			assert(0);
		}

		Card(Crate *crate, std::string name, void *sdrbuf, uint8_t sdrbuflen);
		const std::string get_name() { return name; };
		uint8_t get_fru() { return fru; };
		std::string get_slotstring() {
			if (this->fru == 3 || this->fru == 4)
				return stdsprintf("MCH%hhu", this->fru-2);
			else if (this->fru >= 5 && this->fru <= 16)
				return stdsprintf("AMC%02hhu", this->fru - 4);
			else if (this->fru == 30)
				return "AMC13";
			else if (this->fru == 29)
				return "AMC14";
			else if (this->fru == 40 || this->fru == 41)
				return stdsprintf("CU%hhu", this->fru - 39);
                        else if ((this->fru >= 50) && (this->fru <= 53))
				return stdsprintf("PM%hhu", this->fru - 49);
			else
				return stdsprintf("FRU%d", this->fru);
		}
		virtual ~Card();

		virtual Crate *get_crate() { return crate; };
		virtual uint8_t get_entity_id() { return entity_id; };
		virtual uint8_t get_entity_instance() { return entity_instance; };
		virtual uint8_t get_bridge_addr() { return access_addr; };
		virtual uint8_t get_channel() { return channel; };
		virtual uint8_t get_addr() {
			if (this->fru == 2 || this->fru == 3) // MCH
				return 0x10 + 2*(this->fru - 2);
			else if (this->fru >= 5 && this->fru <= 16) // AMC1-12
				return 0x70 + 2*(this->fru - 4);
			else if (this->fru == 30) // AMC13
				return 0xa4;
			else if (this->fru == 29) // AMC14
				return 0xa2;
			else if (this->fru == 40 || this->fru == 41) // CU
				return 0xa8 + 2*(this->fru - 40);
                        else if ((this->fru >= 50) && (this->fru <= 53)) // PM
				return 0xc2 + 2*(this->fru - 50);
			else
				THROWMSG(InvalidAddressException, "Cannot deduce the address of the %s in %s", this->name.c_str(), this->get_slotstring().c_str());
		};

		void led_test(uint8_t led_id, uint8_t on_100ms) { assert(on_100ms < 128); this->set_led_state(led_id, 0xfb, on_100ms); };
		void led_flash(uint8_t led_id, uint8_t off_100ms, uint8_t on_100ms) { assert(off_100ms && off_100ms <= 0xfa); assert(on_100ms && on_100ms <= 0xfa); this->set_led_state(led_id, off_100ms, on_100ms); };
		void led_release(uint8_t led_id) { this->set_led_state(led_id, 0xfc, 0); };
		void set_led_state(uint8_t led_id, uint8_t function, uint8_t ontime);

		virtual Sensor *get_sensor(uint8_t number);
		virtual Sensor *get_sensor(const std::string name);
		virtual HotswapSensor* get_hotswap_sensor();
		virtual std::vector<Sensor*> get_sensors();
		virtual void add_sensor(uint8_t sensor_number, const void *sdr_record, uint8_t sdr_record_len);
		virtual void destroy_sensor(uint8_t number);

		virtual void sensor_event(Sensor *sensor, bool assertion, uint8_t offset, void *sel_record, uint8_t sel_record_len) { };
		virtual void hotswap_event(uint8_t oldstate, uint8_t newstate) { };
		virtual void crate_connected() { };

		virtual int get_sdr(void *sdrbuf, int sdrbuflen) {
			if (sdrbuflen < this->sdrbuflen)
				return -1;

			memcpy(sdrbuf, this->sdrbuf, this->sdrbuflen);
			return this->sdrbuflen;
		}

	protected:
		virtual Sensor *instantiate_sensor(uint8_t sensor_number, const void *sdr, uint8_t sdrlen);
};

DEFINE_EXCEPTION(SensorReadingException, IPMI_CommandError);
DEFINE_EXCEPTION(SensorNotThresholdException, SensorReadingException);

class Sensor {
	protected:
		Card *card;
		std::string name;
		uint8_t sensor_number;
		uint8_t sdrbuflen;
		uint8_t sdrbuf[64];

	public:
		Sensor(Card *card, uint8_t sensor_number, const void *sdrbuf, uint8_t sdrbuflen);
		virtual ~Sensor() { };

		virtual const std::string get_name() { return name; };
		virtual uint8_t get_sensor_number() { return sensor_number; };

		virtual void get_readings(uint8_t *raw, double **threshold, uint16_t *bitmask);
		virtual uint8_t get_raw_reading();
		virtual double get_threshold_reading();
		virtual uint16_t get_event_reading();

		virtual uint8_t get_sensor_type();
		virtual char get_sensor_reading_type();
		virtual std::string get_long_units();
		virtual std::string get_short_units();

		virtual void sensor_event(bool assertion, uint8_t offset, void *sel_record, uint8_t sel_record_len) { };
		virtual void crate_connected() { };

		virtual int get_sdr(void *sdrbuf, int sdrbuflen) {
			if (sdrbuflen < this->sdrbuflen)
				return -1;

			memcpy(sdrbuf, this->sdrbuf, this->sdrbuflen);
			return this->sdrbuflen;
		}

		struct threshold_data_t {
			bool lnc_set;
			bool lc_set;
			bool lnr_set;
			bool unc_set;
			bool uc_set;
			bool unr_set;
			uint8_t lnc;
			uint8_t lc;
			uint8_t lnr;
			uint8_t unc;
			uint8_t uc;
			uint8_t unr;
		};
		virtual void set_thresholds(threshold_data_t thresholds);
		virtual threshold_data_t get_thresholds();

		struct event_enable_data_t {
			bool scanning;
			bool events;
			uint16_t assert;
			uint16_t deassert;
		};
		virtual event_enable_data_t get_event_enables();
		virtual void set_event_enables(event_enable_data_t enables);

		struct hysteresis_data_t {
			uint8_t goinghigh;
			uint8_t goinglow;
		};
		virtual hysteresis_data_t get_hysteresis();

	protected:
		virtual void values_read(uint8_t raw, double *threshold, uint16_t bitmask) { };
};

class HotswapSensor : public Sensor {
	protected:
		uint8_t state;

	public:
		HotswapSensor(Card *card, uint8_t sensor_number, const void *sdrbuf, uint8_t sdrbuflen)
			: Sensor(card, sensor_number, sdrbuf, sdrbuflen), state(0) {
				/*
				event_enable_data_t events = this->get_event_enables();
				events.scanning = true;
				events.events = true;
				events.assert |= 0x00ff;
				this->set_event_enables(events);
				*/
				this->name = "Hotswap";
				this->get_event_reading();
		   	};
		~HotswapSensor() { };

		virtual void hotswap_event(uint8_t oldstate, uint8_t newstate) {
			dmprintf("C%d: %s (%s): Hotswap Event: M%hhd -> M%hhd\n", this->card->get_crate()->get_number(), this->card->get_name().c_str(), this->card->get_slotstring().c_str(), oldstate, newstate);
		};

		virtual void sensor_event(bool assertion, uint8_t offset, void *sel_record, uint8_t sel_record_len) {
#ifdef ABSOLUTE_HOTSWAP
			this->get_event_reading();
#else
			if (!assertion)
				return;

			uint8_t old_state = state;
			state = offset;

			if (old_state != state) {
				this->hotswap_event(old_state, state);
				this->card->hotswap_event(old_state, state);
			}
#endif
		};

		uint8_t get_state() { return state; };

	protected:
		virtual void values_read(uint8_t raw, double *threshold, uint16_t bitmask) {
			uint8_t old_state = state;

			for (int i = 0; i < 8; i++)
				if (bitmask & (1 << i))
					state = i;

			if (old_state != state) {
				this->hotswap_event(old_state, state);
				this->card->hotswap_event(old_state, state);
			}
		};
};

#endif

