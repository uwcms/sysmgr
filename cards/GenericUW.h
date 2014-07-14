#ifndef _UWCARD_H
#define _UWCARD_H

#include <libxml++/libxml++.h>
#include <errno.h>

#include "../Crate.h"

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
				cfg_slot(uint8_t type, std::map<std::string, std::string> attrs)
					: number(number), attrs(attrs) { };
		};
		class cfg_crate {
			public:
				uint8_t number;
				std::map<uint8_t, cfg_slot> slots;
				std::map<std::string, std::string> attrs;
				cfg_crate(uint8_t type, std::map<std::string, std::string> attrs)
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
		TaskQueue::taskid_t fpga_config_check_task;
		std::string ivtable_path;
	public:
		GenericUW(Crate *crate, std::string name, void *sdrbuf, uint8_t sdrbuflen, std::string ivtable_path)
			: Card(crate, name, sdrbuf, sdrbuflen), fpga_config_check_task(0), ivtable_path(ivtable_path) {
				// Set the internal clock on Wisconsin MMCs
				this->set_clock();
			};
		virtual Sensor *instantiate_sensor(uint8_t sensor_number, const void *sdr, uint8_t sdrlen);

		virtual void hotswap_event(uint8_t oldstate, uint8_t newstate);

		virtual void set_clock();
		virtual void configure_fpga(uint8_t fpgaid);

	protected: 
		virtual void configure_fpga_pre(uint8_t crate, uint8_t slot, std::string name, uint8_t fpgaid, IVTableParser::cfg_ivtable &ivtable) { };
		virtual void configure_fpga_post(uint8_t crate, uint8_t slot, std::string name, uint8_t fpgaid, IVTableParser::cfg_ivtable &ivtable) { };
};

class UW_FPGAConfig_Sensor : public Sensor {
	protected:
		int scan_retries;
		TaskQueue::taskid_t scan_retry_task;
	public:
		UW_FPGAConfig_Sensor(GenericUW *card, uint8_t sensor_number, const void *sdrbuf, uint8_t sdrbuflen)
			: Sensor(card, sensor_number, sdrbuf, sdrbuflen), scan_retries(0), scan_retry_task(0) { this->scan_sensor(3); };

		virtual ~UW_FPGAConfig_Sensor() {
			if (this->scan_retry_task)
				THREADLOCAL.taskqueue.cancel(this->scan_retry_task);
		}

		virtual void sensor_event(bool assertion, uint8_t offset, void *sel_record, uint8_t sel_record_len) {
			switch (offset) {
				case 3:  // REQCFG0
				case 6:  // REQCFG1
				case 9:  // REQCFG2
				case 4:  // CFGRDY0
				case 7:  // CFGRDY1
				case 10: // CFGRDY2
					this->scan_sensor(3);
			}
		}

		virtual void scan_sensor(int retries) {
			scan_retries = retries;
			if (!scan_retry_task)
				this->scan_sensor_attempt(NULL);
		}

	protected:
		virtual void scan_sensor_attempt(void *cb_null) {
			try {
				//dmprintf("Performing UW_FPGAConfig_Sensor scan with %d retries remaining\n", this->scan_retries);
				this->get_event_reading();
				//dmprintf("Performed UW_FPGAConfig_Sensor scan successfully\n");
				this->scan_retries = -1;
				this->scan_retry_task = 0;
				return;
			}
			catch (SensorReadingException &e) {
				if (this->scan_retries-- > 0)
					this->scan_retry_task = THREADLOCAL.taskqueue.schedule(time(NULL)+5, callback<void>::create<UW_FPGAConfig_Sensor,&UW_FPGAConfig_Sensor::scan_sensor_attempt>(this), NULL);
				else
					this->scan_retry_task = 0;
			}
		}
		virtual void values_read(uint8_t raw, double *threshold, uint16_t bitmask) {
			for (int fpgaid = 0; fpgaid < 3; fpgaid++) {
				uint8_t fpga_bit_base = 2 + (3*fpgaid);
				bool reqcfg		= bitmask & (1 << (fpga_bit_base+1));
				bool cfgrdy		= bitmask & (1 << (fpga_bit_base+2));
				//dmprintf("C%d: %s (%s): FPGA[%hhu] Config Reading: reqcfg:%u cfgrdy:%u\n", this->card->get_crate()->get_number(), this->card->get_name().c_str(), this->card->get_slotstring().c_str(), fpgaid, reqcfg, cfgrdy);
				if (reqcfg && !cfgrdy)
					static_cast<GenericUW*>(this->card)->configure_fpga(fpgaid);
			}
		};
};

#endif
