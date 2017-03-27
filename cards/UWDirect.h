#ifndef _UWDirect_H
#define _UWDirect_H

#include <libxml++/libxml++.h>
#include <errno.h>
#include <time.h>

#include "../Crate.h"
#include "GenericUW.h"

class UWDirect : public GenericUW {
	protected:
		uint8_t raw_config_sensor_id;

	public:
		UWDirect(Crate *crate, std::string name, void *sdrbuf, uint8_t sdrbuflen, std::string ivtable_path, uint8_t raw_config_sensor_id, uint32_t poll_count = GENERICUW_CONFIG_RETRIES, uint32_t poll_delay = GENERICUW_CONFIG_RETRY_DELAY)
			: GenericUW(crate, name, sdrbuf, sdrbuflen, ivtable_path, poll_count, poll_delay), raw_config_sensor_id(raw_config_sensor_id) { };

		virtual Sensor *instantiate_sensor(uint8_t sensor_number, const void *sdr, uint8_t sdrlen);
};

class UWDirect_FPGAConfig_Sensor : public UW_FPGAConfig_Sensor {
	protected:
		uint8_t raw_config_sensor_id;

	public:
		UWDirect_FPGAConfig_Sensor(UWDirect *card, uint8_t sensor_number, const void *sdrbuf, uint8_t sdrbuflen, uint8_t raw_config_sensor_id)
			: UW_FPGAConfig_Sensor(card, sensor_number, sdrbuf, sdrbuflen), raw_config_sensor_id(raw_config_sensor_id) { };
		virtual void get_readings(uint8_t *raw, double **threshold, uint16_t *bitmask);
};

#endif
