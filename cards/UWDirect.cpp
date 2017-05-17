#include <time.h>
#include <unistd.h>
#include <typeinfo>

#include "UWDirect.h"

/** configure: depends: GenericUW */
/** configure: sysmgr.conf.example
cardmodule {
	module = "UWDirect.so"
	config = {
		"ivtable=ipconfig.xml",
		"poll_count=12",
		"poll_delay=15",
		"support=WISC CTP-7#19",
		"support=WISC CIOZ#14"
	}
}
*/

static std::string ivtable_path;
static std::map<std::string, uint8_t> supported_cards;

static uint32_t cfg_poll_count = GENERICUW_CONFIG_RETRIES;
static uint32_t cfg_poll_delay = GENERICUW_CONFIG_RETRY_DELAY;

extern "C" {
	uint32_t APIVER = 2;
	uint32_t MIN_APIVER = 2;
	bool initialize_module(std::vector<std::string> config) {
		for (std::vector<std::string>::iterator it = config.begin(); it != config.end(); it++) {
			size_t delim = it->find("=");
			if (delim == std::string::npos) {
				mprintf("Failure parsing module options: Invalid option: %s\n", it->c_str());
				return false;
			}
			std::string param = it->substr(0, delim);
			std::string value = it->substr(delim+1);

			if (param == "ivtable") {
				if (access(value.c_str(), R_OK) == 0) {
					ivtable_path = value;
				}
				else if (access((std::string(CONFIG_PATH "/") + value).c_str(), R_OK) == 0) {
					ivtable_path = (std::string(CONFIG_PATH "/") + value);
				}
				else {
					mprintf("Unable to access module configuration file: %s\n", value.c_str());
					return false;
				}
				dmprintf("UWDirect using ivtable %s\n", ivtable_path.c_str());
			}
			else if (param == "support") {
				delim = value.find("#");
				if (delim == std::string::npos) {
					mprintf("Failure parsing module options: Invalid option: %s\n", it->c_str());
					return false;
				}
				std::string cardname = value.substr(0, delim);
				std::string cardsensor = value.substr(delim+1);

				const char *cardsensor_cstr = cardsensor.c_str();
				char *end;
				unsigned long int val = strtoul(cardsensor_cstr, &end, 0);
				if (end == cardsensor_cstr || *end != '\0') {
					mprintf("Failure parsing module options: Invalid[A] sensor id (%s): %s\n", cardsensor.c_str(), it->c_str());
					return false;
				}
				if ((val == ULONG_MAX && errno == ERANGE) || val > 0xffffffffu) {
					mprintf("Failure parsing module options: Invalid[B] sensor id (%s): %s\n", cardsensor.c_str(), it->c_str());
					return false;
				}
				if (val > 0xff) {
					mprintf("Failure parsing module options: Invalid[C] sensor id (%s): %s\n", cardsensor.c_str(), it->c_str());
					return false;
				}

				supported_cards.insert(std::make_pair(cardname, val));
				dmprintf("UWDirect supporting %s cards\n", value.c_str());
			}
			else if (param == "poll_count") {
				char *eptr = NULL;
				cfg_poll_count = strtoull(value.c_str(), &eptr, 0);
				if (!eptr || *eptr != '\0') {
					mprintf("Could not parse poll_count value \"%s\"\n", value.c_str());
					return false;
				}
			}
			else if (param == "poll_delay") {
				char *eptr = NULL;
				cfg_poll_delay = strtoull(value.c_str(), &eptr, 0);
				if (!eptr || *eptr != '\0') {
					mprintf("Could not parse poll_delay value \"%s\"\n", value.c_str());
					return false;
				}
			}
			else {
				mprintf("Unknown configuration option \"%s\"\n", param.c_str());
				return false;
			}
		}
		if (ivtable_path == "") {
			mprintf("No ivtable xml specified.  UWDirect will not service cards.\n");
		}
		else if (supported_cards.size() == 0) {
			mprintf("No supported cards specified.  UWDirect will not service cards.\n");
		}
		return true;
	}
	Card *instantiate_card(Crate *crate, std::string name, void *sdrbuf, uint8_t sdrbuflen) {
		for (std::map<std::string, uint8_t>::iterator it = supported_cards.begin(); it != supported_cards.end(); it++) {
			if (it->first == name) {
				dmprintf("Found and supporting card %s with UWDirect\n", name.c_str());
				return new UWDirect(crate, name, sdrbuf, sdrbuflen, ivtable_path, it->second, cfg_poll_count, cfg_poll_delay);
			}
		}
		return NULL;
	}
}

Sensor *UWDirect::instantiate_sensor(uint8_t sensor_number, const void *sdr, uint8_t sdrlen)
{
	int rv;

	char namebuf[64] = "";
	rv = ipmi_sdr_parse_sensor_name(this->crate->ctx.sdr, sdr, sdrlen,
			sensor_number, 0, namebuf, 64);
	assert(rv >= 0);

	if (strcmp(namebuf, "FPGA Config") == 0)
		return new UWDirect_FPGAConfig_Sensor(this, sensor_number, sdr, sdrlen, this->raw_config_sensor_id);

	return Card::instantiate_sensor(sensor_number, sdr, sdrlen);
}

void UWDirect_FPGAConfig_Sensor::get_readings(uint8_t *raw, double **threshold, uint16_t *bitmask)
{
	dmprintf("C%u: Getting sensor readings using UWDirect method for %s in %s (using sensor #%hhu)\n", CRATE_NO, this->card->get_name().c_str(), this->card->get_slotstring().c_str(), this->raw_config_sensor_id);
	fiid_template_t tmpl_sensread_rq =
	{
		{ 8, "cmd", FIID_FIELD_REQUIRED | FIID_FIELD_LENGTH_FIXED},
		{ 8, "sensor_number", FIID_FIELD_REQUIRED | FIID_FIELD_LENGTH_FIXED},
		{ 0, "", 0}
	};
	fiid_template_t tmpl_sensread_rs =
	{
		{ 8, "sensor_reading", FIID_FIELD_REQUIRED | FIID_FIELD_LENGTH_FIXED },
		{ 5, "reserved1", FIID_FIELD_REQUIRED | FIID_FIELD_LENGTH_FIXED },
		{ 1, "reading_state", FIID_FIELD_REQUIRED | FIID_FIELD_LENGTH_FIXED },
		{ 1, "sensor_scanning", FIID_FIELD_REQUIRED | FIID_FIELD_LENGTH_FIXED },
		{ 1, "all_event_messages", FIID_FIELD_REQUIRED | FIID_FIELD_LENGTH_FIXED },
		{ 8, "sensor_event_bitmask1", FIID_FIELD_REQUIRED | FIID_FIELD_LENGTH_FIXED },
		{ 7, "sensor_event_bitmask2", FIID_FIELD_OPTIONAL | FIID_FIELD_LENGTH_FIXED },
		{ 1, "reserved2", FIID_FIELD_OPTIONAL | FIID_FIELD_LENGTH_FIXED },
		{ 0, "", 0}
	};

	fiid_obj_t sensread_rq = fiid_obj_create(tmpl_sensread_rq);
	fiid_obj_t sensread_rs = fiid_obj_create(tmpl_sensread_rs);

	fiid_obj_set(sensread_rq, "cmd", 0x2D);
	fiid_obj_set(sensread_rq, "sensor_number", this->raw_config_sensor_id);

	this->card->get_crate()->send_bridged(0, this->card->get_bridge_addr(), this->card->get_channel(), this->card->get_addr(), 0x04, sensread_rq, sensread_rs);

	uint64_t val = 0;
	fiid_obj_get(sensread_rs, "sensor_event_bitmask1", &val);
	*bitmask = val;
	val = 0;
	fiid_obj_get(sensread_rs, "sensor_event_bitmask2", &val);
	*bitmask |= val << 8;

	*threshold = NULL;
	*raw = 0;

	fiid_obj_destroy(sensread_rq);
	fiid_obj_destroy(sensread_rs);

	this->values_read(*raw, *threshold, *bitmask);
}
