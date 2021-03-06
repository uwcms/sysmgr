#ifndef _GENERICUW_H
#define _GENERICUW_H

#include <libxml++/libxml++.h>
#include <errno.h>
#include <time.h>

#include "../Crate.h"

#define GENERICUW_CONFIG_RETRIES 12
#define GENERICUW_CONFIG_RETRY_DELAY 15

class IVTableParser : public xmlpp::SaxParser
{
	public:
		// Crate, Slot, CardName, FPGA => IV
		class cfg_fpga {
			public:
				uint8_t id;
				std::vector<uint8_t> iv;
				std::map<std::string, std::string> attrs;
				cfg_fpga(uint8_t id, std::map<std::string, std::string> attrs)
					: id(id), attrs(attrs) { };
		};
		class cfg_card {
			public:
				std::string type;
				std::map<uint8_t, cfg_fpga> fpgas;
				std::map<std::string, std::string> attrs;
				cfg_card(std::string type, std::map<std::string, std::string> attrs)
					: type(type), attrs(attrs) { };
		};
		class cfg_slot {
			public:
				uint8_t number;
				std::map<std::string, cfg_card> cards;
				std::map<std::string, std::string> attrs;
				cfg_slot(uint8_t number, std::map<std::string, std::string> attrs)
					: number(number), attrs(attrs) { };
		};
		class cfg_crate {
			public:
				uint8_t number;
				std::map<uint8_t, cfg_slot> slots;
				std::map<std::string, std::string> attrs;
				cfg_crate(uint8_t number, std::map<std::string, std::string> attrs)
					: number(number), attrs(attrs) { };
		};
		class cfg_ivtable {
			public:
				std::map<uint8_t, cfg_crate> crates;
				cfg_ivtable() { };
				void clear() { this->crates.clear(); };
				cfg_crate *find(uint8_t crate) {
					std::map<uint8_t, cfg_crate>::iterator it = crates.find(crate);
					if (it == crates.end()) return NULL;
					else return &(it->second);
				};
				cfg_slot *find(uint8_t crate, uint8_t slot) {
					cfg_crate *craterec = this->find(crate);
					if (!craterec) return NULL;
					std::map<uint8_t, cfg_slot>::iterator it = craterec->slots.find(slot);
					if (it == craterec->slots.end()) return NULL;
					else return &(it->second);
				};
				cfg_card *find(uint8_t crate, uint8_t slot, std::string card) {
					cfg_slot *slotrec = this->find(crate, slot);
					if (!slotrec) return NULL;
					std::map<std::string, cfg_card>::iterator it = slotrec->cards.find(card);
					if (it == slotrec->cards.end()) return NULL;
					else return &(it->second);
				};
				cfg_fpga *find(uint8_t crate, uint8_t slot, std::string card, uint8_t fpga) {
					cfg_card *cardrec = this->find(crate, slot, card);
					if (!cardrec) return NULL;
					std::map<uint8_t, cfg_fpga>::iterator it = cardrec->fpgas.find(fpga);
					if (it == cardrec->fpgas.end()) return NULL;
					else return &(it->second);
				};
		};

		cfg_ivtable ivtable;

		DEFINE_EXCEPTION(ConfigParseException, Sysmgr_Exception);

	protected:
		ConfigParseException *last_exception;

		uint8_t cur_crate;
		uint8_t cur_slot;
		std::string cur_card;
		uint8_t cur_fpga;

		enum parse_states_t {
			PST_TOP,
			PST_IVTABLE,
			PST_CRATE,
			PST_SLOT,
			PST_CARD,
			PST_FPGA,
		};
		parse_states_t parse_state;

	public:
		IVTableParser() : xmlpp::SaxParser() { };
		// virtual ~IVTableParser() { };

		cfg_ivtable ParseIVTable(std::string ivtable_file);

	protected:
		virtual void on_start_document();
		// virtual void on_end_document();
		virtual void on_start_element(const Glib::ustring& tagname, const AttributeList& properties);
		virtual void on_end_element(const Glib::ustring& tagname);
		virtual void on_characters(const Glib::ustring& text);
		// virtual void on_comment(const Glib::ustring& text);
		virtual void on_warning(const Glib::ustring& text);
		virtual void on_error(const Glib::ustring& text);
		virtual void on_fatal_error(const Glib::ustring& text);

		uint32_t parse_uint8(const Glib::ustring& token) {
			const char *str = token.c_str();
			char *end;
			unsigned long int val = strtoul(str, &end, 0);
			if (end == str || *end != '\0')
				THROWMSG(ConfigParseException, "Error parsing config file (in Crate %hhu, Slot %hhu, Card \"%s\", FPGA %hhu): Integer parsing failed: Invalid string: \"%s\"", this->cur_crate, this->cur_slot, this->cur_card.c_str(), this->cur_fpga, token.c_str());
			if ((val == ULONG_MAX && errno == ERANGE) || val > 0xffffffffu)
				THROWMSG(ConfigParseException, "Error parsing config file (in Crate %hhu, Slot %hhu, Card \"%s\", FPGA %hhu): Integer parsing failed: Overflow", this->cur_crate, this->cur_slot, this->cur_card.c_str(), this->cur_fpga);
			if (val > 0xff)
				THROWMSG(ConfigParseException, "Error parsing config file (in Crate %hhu, Slot %hhu, Card \"%s\", FPGA %hhu): Integer parsing failed: Value too large", this->cur_crate, this->cur_slot, this->cur_card.c_str(), this->cur_fpga);
			return val;
		}
};

class GenericUW : public Card {
	protected:
		TaskQueue::taskid_t set_clock_task;
		std::string ivtable_path;
	public:
		const uint32_t cfg_poll_count;
		const uint32_t cfg_poll_delay;

		GenericUW(Crate *crate, std::string name, void *sdrbuf, uint8_t sdrbuflen, std::string ivtable_path, uint32_t poll_count = GENERICUW_CONFIG_RETRIES, uint32_t poll_delay = GENERICUW_CONFIG_RETRY_DELAY)
			: Card(crate, name, sdrbuf, sdrbuflen), set_clock_task(0), ivtable_path(ivtable_path), cfg_poll_count(poll_count), cfg_poll_delay(poll_delay) {
				// Set the internal clock on Wisconsin MMCs
				this->set_clock_cb(NULL);
			};

		virtual ~GenericUW() {
			if (this->set_clock_task)
				THREADLOCAL.taskqueue.cancel(this->set_clock_task);
		};

		virtual Sensor *instantiate_sensor(uint8_t sensor_number, const void *sdr, uint8_t sdrlen);

		virtual void hotswap_event(uint8_t oldstate, uint8_t newstate);

		virtual void set_clock();
		virtual void configure_fpga(uint8_t fpgaid);

	protected: 
		virtual void configure_fpga_pre(uint8_t crate, uint8_t slot, std::string name, uint8_t fpgaid, IVTableParser::cfg_ivtable &ivtable) { };
		virtual void configure_fpga_post(uint8_t crate, uint8_t slot, std::string name, uint8_t fpgaid, IVTableParser::cfg_ivtable &ivtable) { };

		virtual void set_clock_cb(void *cb_null) {
			this->set_clock();
			this->set_clock_task = THREADLOCAL.taskqueue.schedule(time(NULL)+300, callback<void>::create<GenericUW,&GenericUW::set_clock_cb>(this), NULL);
		}
};

class UW_FPGAConfig_Sensor : public Sensor {
	protected:
		int scan_retries;
		TaskQueue::taskid_t scan_retry_task;
	public:
		UW_FPGAConfig_Sensor(GenericUW *card, uint8_t sensor_number, const void *sdrbuf, uint8_t sdrbuflen)
			: Sensor(card, sensor_number, sdrbuf, sdrbuflen), scan_retries(0), scan_retry_task(0) { this->scan_sensor(static_cast<GenericUW*>(this->card)->cfg_poll_count); };

		virtual ~UW_FPGAConfig_Sensor() {
			if (this->scan_retry_task)
				THREADLOCAL.taskqueue.cancel(this->scan_retry_task);
		}

		virtual void sensor_event(bool assertion, uint8_t offset, void *sel_record, uint8_t sel_record_len) {
			switch (offset) {
				case 3:  // REQCFG0
				case 6:  // REQCFG1
				case 9:  // REQCFG2
					if (assertion)
						static_cast<GenericUW*>(this->card)->set_clock();
				case 4:  // CFGRDY0
				case 7:  // CFGRDY1
				case 10: // CFGRDY2
					this->scan_sensor(static_cast<GenericUW*>(this->card)->cfg_poll_count);
			}
		}

		virtual void crate_connected() {
			dmprintf("C%d: GenericUW in %s: crate_connection() received.  Scanning.\n", CRATE_NO, this->card->get_slotstring().c_str());
			this->scan_sensor(static_cast<GenericUW*>(this->card)->cfg_poll_count);
		}

		virtual void scan_sensor(int retries) {
			scan_retries = retries;
			if (!scan_retry_task)
				this->scan_sensor_attempt(NULL);
		}

	protected:
		virtual void scan_sensor_attempt(void *cb_null) {
			if (this->card->get_crate()->ctx.sel == NULL) {
				// The crate is offline.  Hold off until it returns.
				this->scan_retry_task = THREADLOCAL.taskqueue.schedule(time(NULL)+static_cast<GenericUW*>(this->card)->cfg_poll_delay, callback<void>::create<UW_FPGAConfig_Sensor,&UW_FPGAConfig_Sensor::scan_sensor_attempt>(this), NULL);

				// Do not decrement retry count here.
				// We're postponing this one, not consuming it.
				dmprintf("C%d: %s: Postpone UW_FPGAConfig_Sensor scan with %d retries remaining: Crate is offline\n", CRATE_NO, this->card->get_slotstring().c_str(), this->scan_retries);
				return;
			}
			try {
				dmprintf("C%d: %s: Performing UW_FPGAConfig_Sensor scan with %d retries remaining\n", CRATE_NO, this->card->get_slotstring().c_str(), this->scan_retries);
				this->get_event_reading();
				dmprintf("C%d: %s: Performed UW_FPGAConfig_Sensor scan successfully\n", CRATE_NO, this->card->get_slotstring().c_str());

				/* Don't return.  Continue monitoring the sensor actively for
				 * up to 30 seconds (retries*period) after initial trigger.
				 * This should handle configuration failures as well as any
				 * issues with the config sensor reading (from catch block).
				 */
				//this->scan_retries = -1;
				this->scan_retry_task = 0;
				//return;
			}
			catch (SensorReadingException &e) {
				// Error.  Retry.
			}

			// We didn't return.  Cycle through once more.
			if (--this->scan_retries > 0) {
				dmprintf("C%d: %s: UW_FPGAConfig_Sensor scan: Scheduling next polling.  %d remaining\n", CRATE_NO, this->card->get_slotstring().c_str(), this->scan_retries);
				this->scan_retry_task = THREADLOCAL.taskqueue.schedule(time(NULL)+static_cast<GenericUW*>(this->card)->cfg_poll_delay, callback<void>::create<UW_FPGAConfig_Sensor,&UW_FPGAConfig_Sensor::scan_sensor_attempt>(this), NULL);
			}
			else {
				dmprintf("C%d: %s: UW_FPGAConfig_Sensor scan: No further polling scheduled.\n", CRATE_NO, this->card->get_slotstring().c_str());
				this->scan_retry_task = 0;
			}
		}
		virtual void values_read(uint8_t raw, double *threshold, uint16_t bitmask) {
			for (int fpgaid = 0; fpgaid < 3; fpgaid++) {
				uint8_t fpga_bit_base = 2 + (3*fpgaid);
				bool reqcfg		= bitmask & (1 << (fpga_bit_base+1));
				bool cfgrdy		= bitmask & (1 << (fpga_bit_base+2));
				dmprintf("C%d: [%u] %s (%s): FPGA[%hhu] Config Reading: reqcfg:%u cfgrdy:%u\n", this->card->get_crate()->get_number(), static_cast<uint32_t>(time(NULL)), this->card->get_name().c_str(), this->card->get_slotstring().c_str(), fpgaid, reqcfg, cfgrdy);
				if (reqcfg && !cfgrdy) {
					static_cast<GenericUW*>(this->card)->configure_fpga(fpgaid);
				}
			}
		};
};

#endif
