#ifndef _UWCARD_H
#define _UWCARD_H

#include "../Crate.h"

class UWCard : public Card {
	protected:
		bool fpga_configure_in_progress;
		TaskQueue::taskid_t fpga_config_check_task;
		uint8_t led_progress_state;
		friend class UW_FPGAConfig_Sensor;
	public:
		static bool check_card_type(Crate *crate, std::string name, void *sdrbuf, uint8_t sdrbuflen) { return name.substr(0,4) == "WISC"; }
		UWCard(Crate *crate, std::string name, void *sdrbuf, uint8_t sdrbuflen)
			: Card(crate, name, sdrbuf, sdrbuflen), fpga_configure_in_progress(false), fpga_config_check_task(0), led_progress_state(0) { this->set_clock(); };
		virtual Sensor *instantiate_sensor(uint8_t sensor_number, const void *sdr, uint8_t sdrlen);

		virtual void hotswap_event(uint8_t oldstate, uint8_t newstate);
		virtual std::string read_ip_config();
	protected:
		virtual void set_clock();
		virtual void configure_fpga();
		virtual void led_progress(uint8_t percent) {
			if (percent == 0) {
				this->led_progress_state = 0;
				if (this->led_progress_state != 0)
					this->led_release(2);
			}
			else if (percent < 100) {
				this->led_progress_state = percent;
				this->led_flash(2, 100-percent, percent);
			}
			else {
				this->led_progress_state = 0;
				this->led_release(2);
				this->led_test(2, 30);
			}
		}
};

class UW_FPGAConfig_Sensor : public Sensor {
	protected:
		time_t config_disarm;
	public:
		UW_FPGAConfig_Sensor(UWCard *card, uint8_t sensor_number, const void *sdrbuf, uint8_t sdrbuflen)
			: Sensor(card, sensor_number, sdrbuf, sdrbuflen), config_disarm(0) { this->get_raw_reading(); };

		virtual void sensor_event(bool assertion, uint8_t offset, void *sel_record, uint8_t sel_record_len) {
			if (assertion && offset == 4)
				this->config_disarm = 0;
			if ((assertion && offset == 0) || offset == 3 || offset == 4)
				this->get_event_reading();
			if (assertion && offset == 4) {
				std::string ip = static_cast<UWCard*>(this->card)->read_ip_config();
				mprintf("C%d: %s card in %s has IP address %s\n", this->card->get_crate()->get_number(), this->card->get_name().c_str(), this->card->get_slotstring().c_str(), ip.c_str());
			}
		}

	protected:
		virtual void values_read(uint8_t raw, double *threshold, uint16_t bitmask) {
			bool reqcfg		= bitmask & (1 << 3);
			bool cfgrdy		= bitmask & (1 << 4);
			//dmprintf("C%d: %s (%s): FPGA Config Reading: reqcfg:%u cfgrdy:%u\n", this->card->get_crate()->get_number(), this->card->get_name().c_str(), this->card->get_slotstring().c_str(), reqcfg, cfgrdy);
			if (reqcfg && !cfgrdy) {
				static_cast<UWCard*>(this->card)->led_progress(50);
				if (this->config_disarm < time(NULL)) {
					this->config_disarm = time(NULL) + 10;
					static_cast<UWCard*>(this->card)->configure_fpga();
				}
			}
			else if (static_cast<UWCard*>(this->card)->led_progress_state) {
				static_cast<UWCard*>(this->card)->led_progress(100);
			}
		};
};

#endif
