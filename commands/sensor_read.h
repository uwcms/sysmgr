#include "../sysmgr.h"
#include "../Crate.h"
#include "../mgmt_protocol.h"

class Command_SENSOR_READ : public Command {
	protected:
		uint8_t target_crate;
	public:
		Command_SENSOR_READ(std::string rawcmd, std::vector<std::string> cmd) : Command(rawcmd, cmd) {
			if (cmd.size() != 5) {
				this->writebuf += stdsprintf("%u ERROR Incorrect number of arguments\n", this->msgid);
				this->reapable = true;
				return;
			};

			std::string err = "";
			target_crate = Command::parse_valid_crate(cmd[2], &err);
			if (err.length())
				this->writebuf += stdsprintf("%u ERROR %s\n", this->msgid, err.c_str());

			if (!target_crate)
				this->reapable = true;
		};
		virtual uint8_t get_required_context() { return target_crate; };
		virtual enum AuthLevel get_required_privilege() { return AUTH_READ; };
		virtual void payload() {
			std::string err = "";
			Card *card = Command::parse_valid_fru(THREADLOCAL.crate, cmd[3], &err);
			if (err.length()) {
				this->writebuf += stdsprintf("%u ERROR %s\n", this->msgid, err.c_str());
				return;
			}

			Sensor *sensor = card->get_sensor(cmd[4]);
			if (!sensor) {
				this->writebuf += stdsprintf("%u ERROR Requested sensor not present.\n", this->msgid);
				return;
			}

			uint8_t raw;
			double *threshold = NULL;
			uint16_t bitmask;
			try {
				sensor->get_readings(&raw, &threshold, &bitmask);
				this->ratelimit();
			}
			catch (Sysmgr_Exception &e) {
				this->writebuf += stdsprintf("%u ERROR Unable to read sensor\n", this->msgid);
				return;
			}

			this->writebuf += stdsprintf("%u RAW 0x%02x\n", this->msgid, raw);
			if (threshold) {
				this->writebuf += stdsprintf("%u THRESHOLD %f\n", this->msgid, *threshold);
				free(threshold);
				threshold = NULL;
			}
			this->writebuf += stdsprintf("%u EVENTMASK 0x%02x\n", this->msgid, bitmask);
		};
		// virtual void finalize(Client& client);
};
